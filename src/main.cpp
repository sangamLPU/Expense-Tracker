#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
using socket_t = SOCKET;
static constexpr socket_t invalid_socket = INVALID_SOCKET;
static void close_socket(socket_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t invalid_socket = -1;
static void close_socket(socket_t s) { close(s); }
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

struct Json {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v = nullptr;

    Json() = default;
    Json(std::nullptr_t) : v(nullptr) {}
    Json(bool x) : v(x) {}
    Json(double x) : v(x) {}
    Json(int x) : v(static_cast<double>(x)) {}
    Json(std::string x) : v(std::move(x)) {}
    Json(const char* x) : v(std::string(x)) {}
    Json(Array x) : v(std::move(x)) {}
    Json(Object x) : v(std::move(x)) {}
    bool is_object() const { return std::holds_alternative<Object>(v); }
    bool is_array() const { return std::holds_alternative<Array>(v); }
    bool is_string() const { return std::holds_alternative<std::string>(v); }
    bool is_number() const { return std::holds_alternative<double>(v); }
    const Object& object() const { return std::get<Object>(v); }
    Object& object() { return std::get<Object>(v); }
    const Array& array() const { return std::get<Array>(v); }
    Array& array() { return std::get<Array>(v); }
    const std::string& string() const { return std::get<std::string>(v); }
    double number() const { return std::get<double>(v); }
    const Json& at(const std::string& key) const {
        static const Json missing;
        if (!is_object()) return missing;
        auto it = object().find(key);
        return it == object().end() ? missing : it->second;
    }
    Json& operator[](const std::string& key) { return object()[key]; }
};

class JsonParser {
    const std::string& s;
    size_t p = 0;
    void ws() { while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p; }
    char take() { if (p >= s.size()) throw std::runtime_error("Unexpected end of JSON"); return s[p++]; }
    bool consume(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (s.compare(p, n, word) == 0) { p += n; return true; }
        return false;
    }
    unsigned hex4() {
        unsigned n = 0;
        for (int i = 0; i < 4; ++i) {
            char c = take(); n <<= 4;
            if (c >= '0' && c <= '9') n += static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') n += static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') n += static_cast<unsigned>(c - 'A' + 10);
            else throw std::runtime_error("Invalid unicode escape");
        }
        return n;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ff) { out.push_back(static_cast<char>(0xc0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
        else if (cp <= 0xffff) { out.push_back(static_cast<char>(0xe0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
        else { out.push_back(static_cast<char>(0xf0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
    }
    std::string str() {
        if (take() != '"') throw std::runtime_error("Expected string");
        std::string out;
        while (p < s.size()) {
            char c = take();
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) throw std::runtime_error("Control character in string");
            if (c != '\\') { out.push_back(c); continue; }
            switch (take()) {
                case '"': out.push_back('"'); break; case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break; case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break; case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break; case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (p + 2 > s.size() || s[p] != '\\' || s[p + 1] != 'u') throw std::runtime_error("Unpaired unicode surrogate");
                        p += 2; unsigned low = hex4();
                        if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error("Invalid unicode surrogate pair");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) throw std::runtime_error("Unpaired unicode surrogate");
                    utf8(out, cp); break;
                }
                default: throw std::runtime_error("Invalid string escape");
            }
        }
        throw std::runtime_error("Unterminated string");
    }
    Json value(size_t depth = 0) {
        if (depth > 64) throw std::runtime_error("JSON nesting is too deep");
        ws();
        if (p >= s.size()) throw std::runtime_error("Expected JSON value");
        if (s[p] == '"') return Json(str());
        if (s[p] == '{') {
            ++p; Json::Object o; ws(); if (p < s.size() && s[p] == '}') { ++p; return Json(std::move(o)); }
            while (true) { ws(); if (p >= s.size() || s[p] != '"') throw std::runtime_error("Expected object key"); auto k = str(); ws(); if (take() != ':') throw std::runtime_error("Expected colon"); o[k] = value(depth + 1); ws(); char c = take(); if (c == '}') break; if (c != ',') throw std::runtime_error("Expected comma"); }
            return Json(std::move(o));
        }
        if (s[p] == '[') {
            ++p; Json::Array a; ws(); if (p < s.size() && s[p] == ']') { ++p; return Json(std::move(a)); }
            while (true) { a.push_back(value(depth + 1)); ws(); char c = take(); if (c == ']') break; if (c != ',') throw std::runtime_error("Expected comma"); }
            return Json(std::move(a));
        }
        if (consume("true")) return Json(true);
        if (consume("false")) return Json(false);
        if (consume("null")) return Json();
        size_t begin = p;
        if (s[p] == '-') ++p;
        if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) throw std::runtime_error("Invalid number");
        if (s[p] == '0') ++p;
        else while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
        if (p < s.size() && s[p] == '.') { ++p; if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) throw std::runtime_error("Invalid decimal"); while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p; }
        if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) { ++p; if (p < s.size() && (s[p] == '+' || s[p] == '-')) ++p; if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) throw std::runtime_error("Invalid exponent"); while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p; }
        return Json(std::stod(s.substr(begin, p - begin)));
    }
public:
    explicit JsonParser(const std::string& source) : s(source) {}
    Json parse() { Json result = value(); ws(); if (p != s.size()) throw std::runtime_error("Trailing JSON data"); return result; }
};

static void quote_json(std::ostringstream& out, const std::string& s) {
    out << '"';
    for (unsigned char c : s) {
        switch (c) { case '"': out << "\\\""; break; case '\\': out << "\\\\"; break; case '\b': out << "\\b"; break; case '\f': out << "\\f"; break; case '\n': out << "\\n"; break; case '\r': out << "\\r"; break; case '\t': out << "\\t"; break; default: if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec; else out << static_cast<char>(c); }
    }
    out << '"';
}
static void encode_json(std::ostringstream& out, const Json& j) {
    if (std::holds_alternative<std::nullptr_t>(j.v)) out << "null";
    else if (std::holds_alternative<bool>(j.v)) out << (std::get<bool>(j.v) ? "true" : "false");
    else if (j.is_number()) out << std::setprecision(15) << j.number();
    else if (j.is_string()) quote_json(out, j.string());
    else if (j.is_array()) { out << '['; bool first = true; for (const auto& x : j.array()) { if (!first) out << ','; first = false; encode_json(out, x); } out << ']'; }
    else { out << '{'; bool first = true; for (const auto& [k, x] : j.object()) { if (!first) out << ','; first = false; quote_json(out, k); out << ':'; encode_json(out, x); } out << '}'; }
}
static std::string json_text(const Json& j) { std::ostringstream out; encode_json(out, j); return out.str(); }
static std::string lower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
static std::string trim(std::string s) { auto a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return {}; auto b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b - a + 1); }
static size_t utf8_length(const std::string& s) { size_t count = 0; for (unsigned char c : s) if ((c & 0xc0) != 0x80) ++count; return count; }
static std::string url_decode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) { o.push_back(static_cast<char>((hi << 4) | lo)); i += 2; }
            else o.push_back(s[i]);
        }
        else if (s[i] == '+') o.push_back(' '); else o.push_back(s[i]);
    }
    return o;
}
static std::string csv_cell(const std::string& s) {
    std::string x = s;
    size_t first = 0;
    while (first < x.size() && (static_cast<unsigned char>(x[first]) <= 0x20 || static_cast<unsigned char>(x[first]) == 0x7f)) ++first;
    if (first < x.size() && (x[first] == '=' || x[first] == '+' || x[first] == '-' || x[first] == '@')) x.insert(x.begin(), '\'');
    size_t pos = 0;
    while ((pos = x.find('"', pos)) != std::string::npos) { x.insert(pos, 1, '"'); pos += 2; }
    return '"' + x + '"';
}
static Json::Array default_categories() { return {"Food", "Transport", "Shopping", "Bills", "Entertainment", "Health", "Home", "Travel", "Education", "Salary", "Other"}; }
static bool has_category(const Json& state, const std::string& name) {
    if (!state.at("categories").is_array()) return false;
    for (const auto& c : state.at("categories").array()) if (c.is_string() && c.string() == name) return true;
    return false;
}
static bool valid_date(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (size_t i = 0; i < s.size(); ++i) if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    int y = std::stoi(s.substr(0, 4)), m = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2));
    if (m < 1 || m > 12 || d < 1) return false;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maxDay = days[m - 1];
    if (m == 2 && (y % 400 == 0 || (y % 4 == 0 && y % 100 != 0))) maxDay = 29;
    return d <= maxDay;
}
static Json initial_state() { Json s(Json::Object{}); s["transactions"] = Json(Json::Array{}); s["categories"] = Json(default_categories()); s["budgets"] = Json(Json::Object{}); s["monthBudget"] = Json(0); s["currency"] = Json("INR"); return s; }

class HttpError : public std::runtime_error {
public:
    int status;
    HttpError(int code, const std::string& message) : std::runtime_error(message), status(code) {}
};

class Store {
    fs::path file;
    mutable std::mutex mutex;
    Json state;
    std::atomic<unsigned long long> next_id{1};
    void save_locked() {
        if (!file.parent_path().empty()) fs::create_directories(file.parent_path());
        fs::path tmp = file; tmp += ".tmp";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Could not write data file");
        out << json_text(state);
        out.flush();
        if (!out) { out.close(); std::error_code cleanup; fs::remove(tmp, cleanup); throw std::runtime_error("Could not flush data file"); }
        out.close();
        if (!out) { std::error_code cleanup; fs::remove(tmp, cleanup); throw std::runtime_error("Could not close data file"); }
#ifdef _WIN32
        if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto error = GetLastError(); std::error_code cleanup; fs::remove(tmp, cleanup);
            throw std::runtime_error("Could not replace data file (Windows error " + std::to_string(error) + ")");
        }
#else
        std::error_code ec; fs::rename(tmp, file, ec);
        if (ec) { std::error_code cleanup; fs::remove(tmp, cleanup); throw std::runtime_error("Could not save data file: " + ec.message()); }
#endif
    }
    void save_or_restore(Json previous) {
        try { save_locked(); }
        catch (const std::exception& e) { state = std::move(previous); throw HttpError(500, std::string("Could not save tracker data: ") + e.what()); }
        catch (...) { state = std::move(previous); throw HttpError(500, "Could not save tracker data"); }
    }
public:
    explicit Store(fs::path path) : file(std::move(path)), state(initial_state()) {
        if (fs::exists(file)) {
            std::ifstream in(file, std::ios::binary);
            if (!in) throw std::runtime_error("Could not open tracker data file: " + file.string());
            std::ostringstream buf; buf << in.rdbuf();
            Json loaded = JsonParser(buf.str()).parse();
            if (!loaded.is_object() || !loaded.at("transactions").is_array() || !loaded.at("categories").is_array() || !loaded.at("budgets").is_object()) throw std::runtime_error("Tracker data file has an unsupported format: " + file.string());
            if (loaded.at("categories").array().empty()) throw std::runtime_error("Tracker data file must contain at least one category: " + file.string());
            for (const auto& c : loaded.at("categories").array()) if (!c.is_string()) throw std::runtime_error("Tracker data file contains an invalid category: " + file.string());
            for (auto& t : loaded["transactions"].array()) {
                if (!t.is_object() || !t.at("id").is_string() || t.at("id").string().empty() || !t.at("type").is_string() || (t.at("type").string() != "income" && t.at("type").string() != "expense") || !t.at("amount").is_number() || !std::isfinite(t.at("amount").number()) || t.at("amount").number() <= 0 || t.at("amount").number() > 1000000000.0 || !t.at("category").is_string() || !has_category(loaded, t.at("category").string()) || !t.at("date").is_string() || !valid_date(t.at("date").string())) throw std::runtime_error("Tracker data file contains an invalid transaction: " + file.string());
                if (!t.at("note").is_string()) t["note"] = Json("");
                if (utf8_length(t.at("note").string()) > 240) throw std::runtime_error("Tracker data file contains an oversized note: " + file.string());
            }
            for (const auto& entry : loaded.at("budgets").object()) if (!has_category(loaded, entry.first) || !entry.second.is_number() || !std::isfinite(entry.second.number()) || entry.second.number() < 0 || entry.second.number() > 1000000000.0) throw std::runtime_error("Tracker data file contains an invalid budget: " + file.string());
            state = std::move(loaded);
        } else { if (!file.parent_path().empty()) fs::create_directories(file.parent_path()); std::lock_guard<std::mutex> lock(mutex); save_locked(); }
        if (!state.at("monthBudget").is_number() || !std::isfinite(state.at("monthBudget").number()) || state.at("monthBudget").number() < 0 || state.at("monthBudget").number() > 1000000000.0) state["monthBudget"] = Json(0);
        const auto& currency = state.at("currency");
        if (!currency.is_string() || (currency.string() != "INR" && currency.string() != "USD" && currency.string() != "EUR" && currency.string() != "GBP" && currency.string() != "CAD" && currency.string() != "AUD" && currency.string() != "JPY")) state["currency"] = Json("INR");
    }
    Json snapshot() const { std::lock_guard<std::mutex> lock(mutex); return state; }
    Json::Array categories_snapshot() const { std::lock_guard<std::mutex> lock(mutex); return state.at("categories").array(); }
    Json add_or_update(Json body, const std::string& id = {}) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!body.is_object()) throw std::runtime_error("Request body must be a JSON object");
        const Json& type = body.at("type"); const Json& amount = body.at("amount"); const Json& category = body.at("category"); const Json& date = body.at("date"); const Json& note = body.at("note");
        if (!type.is_string() || (type.string() != "expense" && type.string() != "income")) throw std::runtime_error("Choose income or expense");
        if (!amount.is_number() || !std::isfinite(amount.number()) || amount.number() <= 0 || amount.number() > 1000000000.0) throw std::runtime_error("Enter an amount greater than zero");
        if (!category.is_string() || !has_category(state, category.string())) throw std::runtime_error("Choose a valid category");
        if (!date.is_string() || !valid_date(date.string())) throw std::runtime_error("Enter a valid date (YYYY-MM-DD)");
        if (note.is_string() && utf8_length(note.string()) > 240) throw std::runtime_error("Notes must be 240 characters or fewer");
        Json row(Json::Object{}); row["id"] = id.empty() ? Json(std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "-" + std::to_string(next_id++)) : Json(id);
        row["type"] = type; row["amount"] = amount; row["category"] = category; row["date"] = date; row["note"] = note.is_string() ? note : Json("");
        auto& rows = state["transactions"].array(); auto found = std::find_if(rows.begin(), rows.end(), [&](const Json& x) { return x.at("id").is_string() && x.at("id").string() == id && !id.empty(); });
        if (!id.empty() && found == rows.end()) throw HttpError(404, "Transaction not found");
        Json previous = state;
        if (found == rows.end()) rows.push_back(row); else *found = row;
        save_or_restore(std::move(previous)); return row;
    }
    bool erase_transaction(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex); auto& rows = state["transactions"].array(); auto old = rows.size();
        auto found = std::find_if(rows.begin(), rows.end(), [&](const Json& x) { return x.at("id").is_string() && x.at("id").string() == id; });
        if (found == rows.end()) return false;
        Json previous = state;
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Json& x) { return x.at("id").is_string() && x.at("id").string() == id; }), rows.end());
        if (rows.size() != old) save_or_restore(std::move(previous)); return rows.size() != old;
    }
    void set_budgets(const Json& body) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!body.is_object() || !body.at("budgets").is_object() || !body.at("monthBudget").is_number()) throw std::runtime_error("Budget settings are invalid");
        if (body.at("monthBudget").number() < 0 || body.at("monthBudget").number() > 1000000000.0) throw std::runtime_error("Enter a valid monthly budget");
        const auto& currency = body.at("currency");
        if (!currency.is_string() || (currency.string() != "INR" && currency.string() != "USD" && currency.string() != "EUR" && currency.string() != "GBP" && currency.string() != "CAD" && currency.string() != "AUD" && currency.string() != "JPY")) throw std::runtime_error("Choose a supported currency");
        Json::Object clean;
        for (const auto& [key, value] : body.at("budgets").object()) {
            if (!has_category(state, key)) throw std::runtime_error("Unknown category in budget settings");
            if (!value.is_number() || value.number() < 0 || value.number() > 1000000000.0) throw std::runtime_error("Category budgets must be zero or more");
            if (value.number() > 0) clean[key] = value;
        }
        Json previous = state;
        state["monthBudget"] = body.at("monthBudget"); state["budgets"] = Json(std::move(clean)); state["currency"] = currency; save_or_restore(std::move(previous));
    }
    void add_category(const Json& body) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto& n = body.at("name"); if (!n.is_string()) throw std::runtime_error("Category name is required");
        std::string name = trim(n.string()); if (name.empty() || utf8_length(name) > 30) throw std::runtime_error("Category name must be 1–30 characters");
        if (std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; })) throw std::runtime_error("Category names cannot contain control characters");
        if (state.at("categories").array().size() >= 100) throw std::runtime_error("You can have up to 100 categories");
        for (const auto& c : state.at("categories").array()) if (lower(c.string()) == lower(name)) throw std::runtime_error("That category already exists");
        Json previous = state;
        state["categories"].array().push_back(Json(name)); save_or_restore(std::move(previous));
    }
    void delete_category(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex); auto& cats = state["categories"].array();
        if (cats.size() <= 1) throw std::runtime_error("Keep at least one category");
        if (!has_category(state, name)) throw std::runtime_error("Category not found");
        if (name == "Food" || name == "Transport" || name == "Shopping" || name == "Bills" || name == "Entertainment" || name == "Health" || name == "Home" || name == "Travel" || name == "Education" || name == "Salary" || name == "Other") throw std::runtime_error("Default categories cannot be removed");
        for (const auto& t : state.at("transactions").array()) if (t.at("category").is_string() && t.at("category").string() == name) throw std::runtime_error("This category has transactions. Reassign or remove them first.");
        Json previous = state;
        cats.erase(std::remove_if(cats.begin(), cats.end(), [&](const Json& c) { return c.is_string() && c.string() == name; }), cats.end());
        state["budgets"].object().erase(name); save_or_restore(std::move(previous));
    }
};

struct Request { std::string method, target, path, body; std::map<std::string, std::string> headers; };
static std::string status_text(int status) { switch (status) { case 200: return "OK"; case 201: return "Created"; case 204: return "No Content"; case 303: return "See Other"; case 400: return "Bad Request"; case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 413: return "Payload Too Large"; case 500: return "Internal Server Error"; case 503: return "Service Unavailable"; default: return "OK"; } }
static thread_local bool suppress_response_body = false;
static void respond(socket_t s, int status, const std::string& body, const std::string& type = "application/json; charset=utf-8", const std::vector<std::pair<std::string, std::string>>& extra = {}, bool head_only = false) {
    std::ostringstream head; head << "HTTP/1.1 " << status << ' ' << status_text(status) << "\r\nContent-Type: " << type << "\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\nReferrer-Policy: strict-origin-when-cross-origin\r\nPermissions-Policy: camera=(), microphone=(), geolocation=()\r\n";
    for (const auto& h : extra) head << h.first << ": " << h.second << "\r\n";
    head << "\r\n"; std::string data = head.str() + ((head_only || suppress_response_body) ? std::string{} : body);
    size_t sent = 0; while (sent < data.size()) {
        int n = static_cast<int>(send(s, data.data() + sent, static_cast<int>(std::min<size_t>(data.size() - sent, 65536)), 0));
        if (n <= 0) break; sent += static_cast<size_t>(n);
    }
}
static bool read_request(socket_t s, Request& r) {
    std::string data; char buf[8192]; size_t split = std::string::npos;
    while ((split = data.find("\r\n\r\n")) == std::string::npos && data.size() < 64 * 1024) {
        int n = static_cast<int>(recv(s, buf, sizeof(buf), 0)); if (n <= 0) return false; data.append(buf, static_cast<size_t>(n));
    }
    if (split == std::string::npos || split > 64 * 1024) { respond(s, 413, "{\"error\":\"Headers too large\"}"); return false; }
    std::istringstream headers(data.substr(0, split)); std::string line;
    if (!std::getline(headers, line)) return false; if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream first(line); std::string version; first >> r.method >> r.target >> version;
    if (r.method == "HEAD") suppress_response_body = true;
    if (r.method.empty() || r.target.empty() || r.target.front() != '/' || (version != "HTTP/1.0" && version != "HTTP/1.1")) { respond(s, 400, "{\"error\":\"Invalid HTTP request line\"}"); return false; }
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); auto colon = line.find(':');
        if (colon == std::string::npos || colon == 0) { respond(s, 400, "{\"error\":\"Invalid request header\"}"); return false; }
        std::string key = lower(trim(line.substr(0, colon))), value = trim(line.substr(colon + 1));
        if (key.empty()) { respond(s, 400, "{\"error\":\"Invalid request header\"}"); return false; }
        if (r.headers.count(key)) { respond(s, 400, "{\"error\":\"Duplicate request header\"}"); return false; }
        r.headers[key] = value;
    }
    if (r.headers.count("transfer-encoding") && lower(r.headers["transfer-encoding"]) != "identity") { respond(s, 400, "{\"error\":\"Transfer-Encoding is not supported\"}"); return false; }
    size_t length = 0;
    try {
        if (r.headers.count("content-length")) {
            const auto& raw_length = r.headers["content-length"];
            if (raw_length.empty() || std::any_of(raw_length.begin(), raw_length.end(), [](unsigned char c) { return !std::isdigit(c); })) { respond(s, 400, "{\"error\":\"Invalid content length\"}"); return false; }
            size_t used = 0; unsigned long long parsed = std::stoull(raw_length, &used);
            if (used != raw_length.size() || parsed > 1024 * 1024) { respond(s, 413, "{\"error\":\"Request body too large\"}"); return false; }
            length = static_cast<size_t>(parsed);
        }
    } catch (...) { respond(s, 400, "{\"error\":\"Invalid content length\"}"); return false; }
    if (length > 1024 * 1024) { respond(s, 413, "{\"error\":\"Request body too large\"}"); return false; }
    size_t body_start = split + 4; while (data.size() - body_start < length) { int n = static_cast<int>(recv(s, buf, sizeof(buf), 0)); if (n <= 0) return false; data.append(buf, static_cast<size_t>(n)); }
    r.body = data.substr(body_start, length);
    auto q = r.target.find('?'); r.path = r.target.substr(0, q);
    return !r.method.empty();
}

static std::string error_json(const std::string& e) { Json j(Json::Object{}); j["error"] = Json(e); return json_text(j); }
static std::string html_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '\"') out += "&quot;";
        else if (c == '\'') out += "&#39;";
        else out += c;
    }
    return out;
}
static std::map<std::string, std::string> parse_form(const std::string& body) {
    std::map<std::string, std::string> values;
    size_t start = 0;
    while (start <= body.size()) {
        size_t end = body.find('&', start);
        std::string pair = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t equal = pair.find('=');
        values[url_decode(pair.substr(0, equal))] = equal == std::string::npos ? "" : url_decode(pair.substr(equal + 1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return values;
}
static std::map<std::string, std::string> parse_query(const std::string& target) {
    size_t query = target.find('?');
    return query == std::string::npos ? std::map<std::string, std::string>{} : parse_form(target.substr(query + 1));
}
static std::string url_encode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(static_cast<char>(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}
static std::string current_month() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m");
    return out.str();
}
static std::string today() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d");
    return out.str();
}
static bool valid_month(const std::string& month) {
    return month.size() == 7 && month[4] == '-' && valid_date(month + "-01");
}
static std::string currency_symbol(const Json& state) {
    const auto& value = state.at("currency");
    const std::string code = value.is_string() ? value.string() : "INR";
    if (code == "USD" || code == "CAD" || code == "AUD") return "$";
    if (code == "EUR") return "€";
    if (code == "GBP") return "£";
    if (code == "JPY") return "¥";
    return "₹";
}
static std::string money(double amount, const std::string& symbol) {
    std::ostringstream out;
    out << symbol << std::fixed << std::setprecision(2) << amount;
    return out.str();
}
static std::string month_label(const std::string& month) {
    static const char* names[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    if (!valid_month(month)) return month;
    int year = std::stoi(month.substr(0, 4)), index = std::stoi(month.substr(5, 2)) - 1;
    return std::string(names[index]) + " " + std::to_string(year);
}
static std::string shifted_month(const std::string& month, int delta) {
    if (!valid_month(month)) return current_month();
    int year = std::stoi(month.substr(0, 4)), number = std::stoi(month.substr(5, 2)) - 1 + delta;
    year += number / 12;
    number %= 12;
    if (number < 0) { number += 12; --year; }
    std::ostringstream out;
    out << std::setw(4) << std::setfill('0') << year << '-' << std::setw(2) << number + 1;
    return out.str();
}
static std::string html_page_start(const std::string& title) {
    return "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><meta name=\"color-scheme\" content=\"light\"><title>" + html_escape(title) + " · Milo</title><link rel=\"stylesheet\" href=\"/styles.css\"></head><body><main class=\"page\">";
}
static std::string render_error_page(const std::string& message) {
    return html_page_start("Something went wrong") + "<section class=\"card error-page\"><a class=\"brand\" href=\"/\"><span class=\"brand-mark\">m</span>milo<span class=\"brand-dot\">.</span></a><p class=\"eyebrow\">REQUEST NOT SAVED</p><h1>That didn’t work.</h1><p>" + html_escape(message) + "</p><a class=\"button primary\" href=\"/\">Back to my tracker</a></section></main></body></html>";
}
static std::string render_delete_confirmation(Store& store, const std::string& target) {
    const auto query = parse_query(target);
    auto id_it = query.find("id");
    if (id_it == query.end() || id_it->second.empty()) throw HttpError(404, "Transaction not found.");
    std::string month = query.count("month") && valid_month(query.at("month")) ? query.at("month") : current_month();
    Json state = store.snapshot();
    for (const auto& row : state.at("transactions").array()) {
        if (!row.at("id").is_string() || row.at("id").string() != id_it->second) continue;
        const std::string note = row.at("note").is_string() && !row.at("note").string().empty() ? row.at("note").string() : row.at("category").string();
        return html_page_start("Confirm transaction deletion") + "<section class=\"card error-page\"><a class=\"brand\" href=\"/\"><span class=\"brand-mark\">m</span>milo<span class=\"brand-dot\">.</span></a><p class=\"eyebrow\">PLEASE CONFIRM</p><h1>Delete this transaction?</h1><p>“" + html_escape(note) + "” · " + money(row.at("amount").number(), currency_symbol(state)) + "</p><div class=\"confirm-actions\"><a class=\"button quiet\" href=\"/?month=" + url_encode(month) + "#transactions\">Keep transaction</a><form method=\"post\" action=\"/transaction/delete\"><input type=\"hidden\" name=\"id\" value=\"" + html_escape(id_it->second) + "\"><input type=\"hidden\" name=\"month\" value=\"" + html_escape(month) + "\"><button class=\"button delete-button\">Delete permanently</button></form></div></section></main></body></html>";
    }
    throw HttpError(404, "Transaction not found.");
}
static std::string render_dashboard(Store& store, const std::string& target) {
    Json state = store.snapshot();
    const auto& transactions = state.at("transactions").array();
    std::string month = current_month(), search, category, type, edit_id;
    int page = 0;
    auto query = parse_query(target);
    if (query.count("month") && valid_month(query["month"])) month = query["month"];
    if (query.count("q")) search = trim(query["q"]);
    if (query.count("category")) category = query["category"];
    if (query.count("type")) type = query["type"];
    if (search.size() > 200) search.resize(200);
    if (!category.empty() && !has_category(state, category)) category.clear();
    if (type != "income" && type != "expense") type.clear();
    if (query.count("page")) { try { page = std::max(0, std::stoi(query["page"])); } catch (...) {} }
    if (query.count("id") && target.find("/transaction/edit") != std::string::npos) edit_id = query["id"];
    const std::string symbol = currency_symbol(state);
    std::map<std::string, double> income_by_month, expenses_by_month, category_totals;
    for (const auto& row : transactions) {
        if (!row.at("date").is_string() || !row.at("amount").is_number() || !row.at("type").is_string()) continue;
        const std::string row_month = row.at("date").string().substr(0, 7);
        if (row.at("type").string() == "income") income_by_month[row_month] += row.at("amount").number();
        else if (row.at("type").string() == "expense") {
            expenses_by_month[row_month] += row.at("amount").number();
            if (row_month == month && row.at("category").is_string()) category_totals[row.at("category").string()] += row.at("amount").number();
        }
    }
    auto monthly_amount = [&](const std::string& for_month, const std::string& kind) {
        const auto& values = kind == "income" ? income_by_month : expenses_by_month;
        auto found = values.find(for_month);
        return found == values.end() ? 0.0 : found->second;
    };
    const double income = monthly_amount(month, "income"), expenses = monthly_amount(month, "expense"), balance = income - expenses;
    static const char* colors[] = {"#2b7058", "#d89470", "#779b76", "#7b86a8", "#c59a49", "#9c7eb4", "#d16f6b", "#5e9fa3", "#9ba56a", "#b77591"};
    auto color_of = [&](const std::string& name) { size_t index = 0; for (const auto& c : state.at("categories").array()) { if (c.is_string() && c.string() == name) break; ++index; } return std::string(colors[index % (sizeof(colors) / sizeof(colors[0]))]); };

    std::ostringstream out;
    out << html_page_start("Your money, made clear")
        << "<header class=\"topbar\"><a class=\"brand\" href=\"/\"><span class=\"brand-mark\">m</span>milo<span class=\"brand-dot\">.</span></a><span class=\"top-note\">YOUR PERSONAL FINANCES</span><a class=\"button primary\" href=\"#transaction-form\">Add transaction +</a><a class=\"button quiet\" href=\"/api/export.csv\">Export CSV ↓</a></header>"
        << "<section class=\"intro\"><div><p class=\"eyebrow\">YOUR PERSONAL FINANCES</p><h1>Your money, <em>made clear.</em></h1><p class=\"muted\">A calmer view of what comes in and where it goes.</p></div><form method=\"get\" class=\"month-form\"><label for=\"month\">Month</label><input id=\"month\" type=\"month\" name=\"month\" value=\"" << html_escape(month) << "\"><button class=\"button primary\">View</button></form></section>"
        << "<section class=\"summary-grid\"><article class=\"card balance-card\"><p class=\"eyebrow\">NET BALANCE · " << html_escape(month_label(month)) << "</p><div class=\"big-number\">" << money(balance, symbol) << "</div><p class=\"muted\">" << money(income, symbol) << " in <span class=\"separator\">·</span> " << money(expenses, symbol) << " out</p></article>"
        << "<article class=\"card metric-card\"><p class=\"eyebrow\">INCOME</p><div class=\"metric-number income\">" << money(income, symbol) << "</div><p class=\"muted\">Money in this month</p></article>"
        << "<article class=\"card metric-card\"><p class=\"eyebrow\">SPENDING</p><div class=\"metric-number expense\">" << money(expenses, symbol) << "</div><p class=\"muted\">Money out this month</p></article>"
        << "<article class=\"card metric-card\"><p class=\"eyebrow\">SAVINGS RATE</p><div class=\"metric-number\">" << (income > 0 ? std::to_string(static_cast<long long>(std::llround(std::clamp(balance / income * 100, -1000000000.0, 1000000000.0)))) + "%" : "—") << "</div><p class=\"muted\">" << (balance >= 0 ? "Kept this month" : "Spending is above income") << "</p></article></section>";

    out << "<section class=\"content-grid\"><article class=\"card\"><div class=\"section-heading\"><div><p class=\"eyebrow\">WHERE IT GOES</p><h2>Spending breakdown</h2></div></div>";
    if (category_totals.empty()) out << "<p class=\"empty-note\">Your spending story will appear here once you add an expense.</p>";
    else {
        double max = expenses > 0 ? expenses : 1;
        std::vector<std::pair<std::string, double>> ranked_categories(category_totals.begin(), category_totals.end());
        std::sort(ranked_categories.begin(), ranked_categories.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (const auto& item : ranked_categories) out << "<div class=\"breakdown-row\"><div class=\"row-top\"><span>" << html_escape(item.first) << "</span><strong>" << money(item.second, symbol) << "</strong></div><div class=\"track\"><span style=\"width:" << std::min(100.0, item.second / max * 100) << "%;background:" << color_of(item.first) << "\"></span></div></div>";
    }
    out << "</article><article class=\"card\"><div class=\"section-heading\"><div><p class=\"eyebrow\">OVER TIME</p><h2>Income & spending</h2></div><span class=\"muted\">Last 6 months</span></div><div class=\"flow-chart\">";
    double peak = 1;
    for (int i = -5; i <= 0; ++i) peak = std::max(peak, std::max(monthly_amount(shifted_month(month, i), "income"), monthly_amount(shifted_month(month, i), "expense")));
    for (int i = -5; i <= 0; ++i) {
        std::string m = shifted_month(month, i); double in = monthly_amount(m, "income"), spent = monthly_amount(m, "expense");
        out << "<div class=\"flow-month\"><div class=\"bars\"><i class=\"bar income-bar\" style=\"height:" << std::max(2.0, in / peak * 100) << "%\" title=\"Income " << money(in, symbol) << "\"></i><i class=\"bar expense-bar\" style=\"height:" << std::max(2.0, spent / peak * 100) << "%\" title=\"Spending " << money(spent, symbol) << "\"></i></div><span>" << html_escape(month_label(m).substr(0, 3)) << "</span></div>";
    }
    out << "</div><div class=\"chart-legend\"><span><i class=\"legend-income\"></i>Income</span><span><i class=\"legend-expense\"></i>Spending</span></div></article></section>";

    double month_budget = state.at("monthBudget").is_number() ? state.at("monthBudget").number() : 0;
    out << "<section class=\"content-grid\"><article class=\"card\"><div class=\"section-heading\"><div><p class=\"eyebrow\">SPEND WITH A PLAN</p><h2>Monthly budget</h2></div></div>";
    if (month_budget > 0) out << "<div class=\"row-top\"><strong>" << money(expenses, symbol) << " <span class=\"muted\">of " << money(month_budget, symbol) << "</span></strong><span>" << static_cast<int>(std::min(100.0, expenses / month_budget * 100)) << "% used</span></div><div class=\"track\"><span class=\"budget-fill\" style=\"width:" << std::min(100.0, expenses / month_budget * 100) << "%\"></span></div><p class=\"muted\">" << (expenses > month_budget ? money(expenses - month_budget, symbol) + " over your limit" : money(month_budget - expenses, symbol) + " left to spend") << "</p>";
    else out << "<p class=\"muted\">" << money(expenses, symbol) << " spent so far · Set a monthly limit below to stay on track.</p>";
    for (const auto& item : state.at("budgets").object()) {
        if (!item.second.is_number() || item.second.number() <= 0) continue;
        double spent = category_totals.count(item.first) ? category_totals[item.first] : 0;
        out << "<div class=\"breakdown-row\"><div class=\"row-top\"><span>" << html_escape(item.first) << "</span><strong>" << money(spent, symbol) << " <span class=\"muted\">of " << money(item.second.number(), symbol) << "</span></strong></div><div class=\"track\"><span class=\"budget-fill\" style=\"width:" << std::min(100.0, spent / item.second.number() * 100) << "%\"></span></div></div>";
    }
    out << "</article><article class=\"card\"><div class=\"section-heading\"><div><p class=\"eyebrow\">MAKE A LITTLE PLAN</p><h2>Budget settings</h2></div></div><form method=\"post\" action=\"/settings/save\" class=\"stack-form\"><input type=\"hidden\" name=\"month\" value=\"" << html_escape(month) << "\"><label>Overall monthly budget<div class=\"input-with-prefix\"><span>" << html_escape(symbol) << "</span><input type=\"number\" min=\"0\" max=\"1000000000\" step=\"0.01\" name=\"monthBudget\" value=\"" << (month_budget > 0 ? std::to_string(month_budget) : "") << "\" placeholder=\"No limit\"></div></label><label>Display currency<select name=\"currency\">";
    const std::pair<const char*, const char*> currencies[] = {{"INR", "Indian rupee · INR"}, {"USD", "US dollar · USD"}, {"EUR", "Euro · EUR"}, {"GBP", "British pound · GBP"}, {"CAD", "Canadian dollar · CAD"}, {"AUD", "Australian dollar · AUD"}, {"JPY", "Japanese yen · JPY"}};
    for (const auto& currency : currencies) out << "<option value=\"" << currency.first << "\"" << (state.at("currency").is_string() && state.at("currency").string() == currency.first ? " selected" : "") << ">" << currency.second << "</option>";
    out << "</select></label><details><summary>Category limits</summary><div class=\"category-budget-list\">";
    for (const auto& c : state.at("categories").array()) if (c.is_string() && c.string() != "Salary") { const auto& value = state.at("budgets").at(c.string()); out << "<label>" << html_escape(c.string()) << "<input type=\"number\" min=\"0\" max=\"1000000000\" step=\"0.01\" name=\"budget." << html_escape(c.string()) << "\" value=\"" << (value.is_number() ? std::to_string(value.number()) : "") << "\" placeholder=\"No limit\"></label>"; }
    out << "</div></details><button class=\"button primary\">Save budgets</button></form></article></section>";

    out << "<section class=\"card transaction-card\" id=\"transactions\"><div class=\"section-heading\"><div><p class=\"eyebrow\">THE LATEST</p><h2>Transactions</h2></div><span class=\"muted\">" << transactions.size() << " total</span></div><form method=\"get\" action=\"/\" class=\"filters\"><input type=\"month\" name=\"month\" aria-label=\"Filter month\" value=\"" << html_escape(month) << "\"><input type=\"search\" name=\"q\" aria-label=\"Search transactions\" value=\"" << html_escape(search) << "\" placeholder=\"Search notes or categories\"><select name=\"category\" aria-label=\"Filter category\"><option value=\"\">All categories</option>";
    for (const auto& c : state.at("categories").array()) if (c.is_string()) out << "<option value=\"" << html_escape(c.string()) << "\"" << (category == c.string() ? " selected" : "") << ">" << html_escape(c.string()) << "</option>";
    out << "</select><select name=\"type\" aria-label=\"Filter transaction type\"><option value=\"\">All types</option><option value=\"expense\"" << (type == "expense" ? " selected" : "") << ">Expenses</option><option value=\"income\"" << (type == "income" ? " selected" : "") << ">Income</option></select><button class=\"button quiet\">Filter</button><a class=\"text-link\" href=\"/?month=" << html_escape(month) << "#transactions\">Clear</a></form><div class=\"table-wrap\"><table><thead><tr><th scope=\"col\">TRANSACTION</th><th scope=\"col\">CATEGORY</th><th scope=\"col\">DATE</th><th class=\"align-right\" scope=\"col\">AMOUNT</th><th scope=\"col\">ACTIONS</th></tr></thead><tbody>";
    std::vector<const Json*> rows;
    const std::string needle = lower(search);
    for (const auto& t : transactions) {
        if (!t.at("date").is_string() || t.at("date").string().substr(0, 7) != month) continue;
        const std::string cat = t.at("category").is_string() ? t.at("category").string() : "";
        const std::string ttype = t.at("type").is_string() ? t.at("type").string() : "";
        if ((!category.empty() && cat != category) || (!type.empty() && ttype != type)) continue;
        if (!needle.empty()) {
            const std::string note = t.at("note").is_string() ? t.at("note").string() : "";
            if (lower(cat + " " + ttype + " " + note).find(needle) == std::string::npos) continue;
        }
        rows.push_back(&t);
    }
    std::sort(rows.begin(), rows.end(), [](const Json* ap, const Json* bp) { const Json& a = *ap; const Json& b = *bp; const std::string ad = a.at("date").is_string() ? a.at("date").string() : "", bd = b.at("date").is_string() ? b.at("date").string() : ""; return ad != bd ? ad > bd : a.at("id").string() > b.at("id").string(); });
    const size_t page_size = 8, pages = std::max<size_t>(1, (rows.size() + page_size - 1) / page_size);
    page = std::min<int>(page, static_cast<int>(pages) - 1);
    size_t first = static_cast<size_t>(page) * page_size, end = std::min(rows.size(), first + page_size);
    for (size_t i = first; i < end; ++i) {
        const auto& t = *rows[i]; std::string id = t.at("id").is_string() ? t.at("id").string() : "", cat = t.at("category").is_string() ? t.at("category").string() : "", ttype = t.at("type").is_string() ? t.at("type").string() : "expense", note = t.at("note").is_string() ? t.at("note").string() : "";
        out << "<tr><td><strong>" << html_escape(note.empty() ? cat : note) << "</strong><small>" << (ttype == "income" ? "Money in" : "Money out") << "</small></td><td><span class=\"pill\"><i style=\"background:" << color_of(cat) << "\"></i>" << html_escape(cat) << "</span></td><td>" << html_escape(t.at("date").string()) << "</td><td class=\"align-right " << (ttype == "income" ? "income" : "expense") << "\">" << (ttype == "income" ? "+" : "−") << money(t.at("amount").number(), symbol) << "</td><td class=\"actions\"><a href=\"/transaction/edit?id=" << url_encode(id) << "&amp;month=" << html_escape(month) << "#transaction-form\">Edit</a><a class=\"danger\" href=\"/transaction/confirm-delete?id=" << url_encode(id) << "&amp;month=" << html_escape(month) << "\">Delete</a></td></tr>";
    }
    if (rows.empty()) out << "<tr><td colspan=\"5\" class=\"empty-note\">No matching transactions for " << html_escape(month_label(month)) << ".</td></tr>";
    out << "</tbody></table></div><div class=\"pagination\"><span>Showing " << (rows.empty() ? 0 : first + 1) << "–" << end << " of " << rows.size() << "</span><div>";
    auto page_link = [&](int n, const std::string& text) { out << "<a class=\"page-link\" href=\"/?month=" << url_encode(month) << "&amp;q=" << url_encode(search) << "&amp;category=" << url_encode(category) << "&amp;type=" << url_encode(type) << "&amp;page=" << n << "#transactions\">" << text << "</a>"; };
    if (page > 0) page_link(page - 1, "← Previous"); if (page + 1 < static_cast<int>(pages)) page_link(page + 1, "Next →");
    out << "</div></div></section>";

    const Json* edit = nullptr;
    if (!edit_id.empty()) for (const auto& t : transactions) if (t.at("id").is_string() && t.at("id").string() == edit_id) { edit = &t; break; }
    if (!edit_id.empty() && !edit) throw HttpError(404, "Transaction not found.");
    std::string form_type = edit && edit->at("type").is_string() ? edit->at("type").string() : "expense";
    std::string form_category = edit && edit->at("category").is_string() ? edit->at("category").string() : (form_type == "income" && has_category(state, "Salary") ? "Salary" : (state.at("categories").array().empty() ? "Other" : state.at("categories").array().front().string()));
    std::string form_date = edit && edit->at("date").is_string() ? edit->at("date").string() : today();
    std::string form_note = edit && edit->at("note").is_string() ? edit->at("note").string() : "";
    out << "<section class=\"content-grid lower-grid\"><article class=\"card\" id=\"transaction-form\"><div class=\"section-heading\"><div><p class=\"eyebrow\">YOUR MONEY, YOUR WAY</p><h2>" << (edit ? "Edit transaction" : "Add a transaction") << "</h2></div>" << (edit ? "<a class=\"text-link\" href=\"/?month=" + html_escape(month) + "#transaction-form\">Cancel edit</a>" : "") << "</div>";
    out << "<form method=\"post\" action=\"/transaction/save\" class=\"stack-form\"><input type=\"hidden\" name=\"id\" value=\"" << html_escape(edit_id) << "\"><input type=\"hidden\" name=\"month\" value=\"" << html_escape(month) << "\"><label>Type<select name=\"type\"><option value=\"expense\"" << (form_type == "expense" ? " selected" : "") << ">Expense</option><option value=\"income\"" << (form_type == "income" ? " selected" : "") << ">Income</option></select></label><label>Amount<div class=\"input-with-prefix\"><span>" << html_escape(symbol) << "</span><input type=\"number\" min=\"0.01\" max=\"1000000000\" step=\"0.01\" name=\"amount\" value=\"" << (edit && edit->at("amount").is_number() ? std::to_string(edit->at("amount").number()) : "") << "\" placeholder=\"0.00\" required></div></label><label>Category<select name=\"category\" required>";
    for (const auto& c : state.at("categories").array()) if (c.is_string()) out << "<option value=\"" << html_escape(c.string()) << "\"" << (form_category == c.string() ? " selected" : "") << ">" << html_escape(c.string()) << "</option>";
    out << "</select></label><label>Date<input type=\"date\" name=\"date\" value=\"" << html_escape(form_date) << "\" required></label><label>Note <span class=\"muted\">optional</span><input type=\"text\" name=\"note\" maxlength=\"240\" value=\"" << html_escape(form_note) << "\" placeholder=\"What was this for?\"></label><button class=\"button primary\">" << (edit ? "Save changes" : "Add transaction") << "</button></form></article>";
    out << "<article class=\"card\"><div class=\"section-heading\"><div><p class=\"eyebrow\">MAKE IT YOURS</p><h2>Categories</h2></div></div><form method=\"post\" action=\"/categories/add\" class=\"inline-form\"><input type=\"hidden\" name=\"month\" value=\"" << html_escape(month) << "\"><input type=\"text\" name=\"name\" maxlength=\"30\" placeholder=\"New category\" required><button class=\"button primary\">Add</button></form><div class=\"category-list\">";
    static const char* defaults[] = {"Food", "Transport", "Shopping", "Bills", "Entertainment", "Health", "Home", "Travel", "Education", "Salary", "Other"};
    for (const auto& c : state.at("categories").array()) if (c.is_string()) {
        bool is_default = std::find_if(std::begin(defaults), std::end(defaults), [&](const char* d) { return c.string() == d; }) != std::end(defaults);
        out << "<div class=\"category-item\"><span><i style=\"background:" << color_of(c.string()) << "\"></i>" << html_escape(c.string()) << "</span>";
        if (is_default) out << "<small>DEFAULT</small>";
        else out << "<form method=\"post\" action=\"/categories/delete\"><input type=\"hidden\" name=\"name\" value=\"" << html_escape(c.string()) << "\"><input type=\"hidden\" name=\"month\" value=\"" << html_escape(month) << "\"><button class=\"link-button danger\">Remove</button></form>";
        out << "</div>";
    }
    out << "</div></article></section><footer class=\"footer\">Milo · Your finances stay on this server. Anyone with this public link can access the tracker.</footer></main></body></html>";
    return out.str();
}
static void redirect_to(socket_t s, const std::string& target) {
    respond(s, 303, "", "text/plain; charset=utf-8", {{"Location", target}, {"Cache-Control", "no-store"}});
}
static bool parse_ipv4(const std::string& host, in_addr* address) {
#ifdef _WIN32
    return InetPtonA(AF_INET, host.c_str(), address) == 1;
#else
    return inet_pton(AF_INET, host.c_str(), address) == 1;
#endif
}
static void configure_client_socket(socket_t s) {
#ifdef _WIN32
    DWORD timeout_ms = 15000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval timeout{}; timeout.tv_sec = 15;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}
static std::atomic<unsigned> active_clients{0};
static constexpr unsigned max_active_clients = 64;
static bool is_form_action(const std::string& path) {
    return path == "/transaction/save" || path == "/transaction/delete" || path == "/settings/save" || path == "/categories/add" || path == "/categories/delete";
}
static void handle(socket_t s, Store& store, const fs::path& web) {
    Request r;
    try {
        if (!read_request(s, r)) return;
        if (r.method == "HEAD") { suppress_response_body = true; r.method = "GET"; }
        if ((r.path == "/" || r.path == "/transaction/edit") && (r.method == "GET" || r.method == "HEAD")) {
            std::string page = render_dashboard(store, r.target);
            respond(s, 200, page, "text/html; charset=utf-8", {{"Cache-Control", "no-store"}, {"Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'; script-src 'none'; object-src 'none'"}}, r.method == "HEAD");
            return;
        }
        if (r.path == "/transaction/confirm-delete" && (r.method == "GET" || r.method == "HEAD")) {
            std::string page = render_delete_confirmation(store, r.target);
            respond(s, 200, page, "text/html; charset=utf-8", {{"Cache-Control", "no-store"}, {"Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'; script-src 'none'; object-src 'none'"}}, r.method == "HEAD");
            return;
        }
        if (r.method == "POST" && is_form_action(r.path)) {
            const auto form = parse_form(r.body);
            auto field = [&](const std::string& name) { auto it = form.find(name); return it == form.end() ? std::string{} : it->second; };
            std::string requested_month = valid_month(field("month")) ? field("month") : current_month();
            if (r.path == "/transaction/save") {
                if (field("date").size() >= 7 && valid_month(field("date").substr(0, 7))) requested_month = field("date").substr(0, 7);
                size_t used = 0; double amount = std::stod(field("amount"), &used);
                if (used != field("amount").size() || !std::isfinite(amount)) throw std::runtime_error("Enter a valid amount.");
                Json body(Json::Object{}); body["type"] = Json(field("type")); body["amount"] = Json(amount); body["category"] = Json(field("category")); body["date"] = Json(field("date")); body["note"] = Json(field("note"));
                store.add_or_update(std::move(body), field("id"));
                redirect_to(s, "/?month=" + requested_month + "#transactions"); return;
            }
            if (r.path == "/transaction/delete") {
                if (!store.erase_transaction(field("id"))) throw HttpError(404, "Transaction not found.");
                redirect_to(s, "/?month=" + requested_month + "#transactions"); return;
            }
            if (r.path == "/settings/save") {
                Json body(Json::Object{}), budgets(Json::Object{}); size_t used = 0; std::string raw_month_budget = field("monthBudget");
                double month_budget = raw_month_budget.empty() ? 0 : std::stod(raw_month_budget, &used);
                if (!raw_month_budget.empty() && (used != raw_month_budget.size() || !std::isfinite(month_budget))) throw std::runtime_error("Enter a valid monthly budget.");
                const auto categories = store.categories_snapshot();
                for (const auto& c : categories) if (c.is_string()) {
                    std::string raw = field("budget." + c.string()); if (raw.empty()) continue;
                    used = 0; double value = std::stod(raw, &used);
                    if (used != raw.size() || !std::isfinite(value)) throw std::runtime_error("Enter valid category budget amounts.");
                    if (value > 0) budgets[c.string()] = Json(value);
                }
                body["monthBudget"] = Json(month_budget); body["currency"] = Json(field("currency")); body["budgets"] = Json(std::move(budgets));
                store.set_budgets(body);
                redirect_to(s, "/?month=" + requested_month); return;
            }
            if (r.path == "/categories/add") { Json body(Json::Object{}); body["name"] = Json(field("name")); store.add_category(body); redirect_to(s, "/?month=" + requested_month); return; }
            if (r.path == "/categories/delete") { store.delete_category(field("name")); redirect_to(s, "/?month=" + requested_month); return; }
        }
        if (r.path == "/api/state") {
            if (r.method != "GET") { respond(s, 405, error_json("Method not allowed")); return; }
            respond(s, 200, json_text(store.snapshot()), "application/json; charset=utf-8", {{"Cache-Control", "no-store"}}); return;
        }
        if (r.path == "/api/transactions") {
            if (r.method != "POST") { respond(s, 405, error_json("Method not allowed")); return; }
            Json result = store.add_or_update(JsonParser(r.body).parse()); respond(s, 201, json_text(result), "application/json; charset=utf-8", {{"Cache-Control", "no-store"}}); return;
        }
        const std::string txPrefix = "/api/transactions/";
        if (r.path.rfind(txPrefix, 0) == 0) {
            std::string id = url_decode(r.path.substr(txPrefix.size()));
            if (id.empty() || id.find('/') != std::string::npos) { respond(s, 404, error_json("Transaction not found")); return; }
            if (r.method == "PUT") { Json result = store.add_or_update(JsonParser(r.body).parse(), id); respond(s, 200, json_text(result), "application/json; charset=utf-8", {{"Cache-Control", "no-store"}}); return; }
            if (r.method == "DELETE") { if (!store.erase_transaction(id)) respond(s, 404, error_json("Transaction not found")); else respond(s, 200, "{\"ok\":true}", "application/json; charset=utf-8", {{"Cache-Control", "no-store"}}); return; }
            respond(s, 405, error_json("Method not allowed")); return;
        }
        if (r.path == "/api/budgets") {
            if (r.method != "PUT") { respond(s, 405, error_json("Method not allowed")); return; }
            store.set_budgets(JsonParser(r.body).parse()); respond(s, 200, "{\"ok\":true}", "application/json; charset=utf-8", {{"Cache-Control", "no-store"}}); return;
        }
        if (r.path == "/api/categories") {
            if (r.method != "POST") { respond(s, 405, error_json("Method not allowed")); return; }
            store.add_category(JsonParser(r.body).parse()); respond(s, 201, "{\"ok\":true}", "application/json; charset=utf-8", {{"Cache-Control", "no-store"}}); return;
        }
        const std::string catPrefix = "/api/categories/";
        if (r.path.rfind(catPrefix, 0) == 0) {
            if (r.method != "DELETE") { respond(s, 405, error_json("Method not allowed")); return; }
            store.delete_category(url_decode(r.path.substr(catPrefix.size()))); respond(s, 200, "{\"ok\":true}", "application/json; charset=utf-8", {{"Cache-Control", "no-store"}}); return;
        }
        if (r.path == "/api/export.csv" && r.method == "GET") {
            Json state = store.snapshot(); std::ostringstream csv; csv << "Date,Type,Category,Note,Amount\r\n";
            std::vector<const Json*> rows; rows.reserve(state.at("transactions").array().size());
            for (const auto& t : state.at("transactions").array()) rows.push_back(&t);
            std::sort(rows.begin(), rows.end(), [](const Json* ap, const Json* bp) { const Json& a = *ap; const Json& b = *bp; return a.at("date").string() != b.at("date").string() ? a.at("date").string() > b.at("date").string() : a.at("id").string() > b.at("id").string(); });
            for (const Json* row : rows) {
                const auto& t = *row;
                std::string note = t.at("note").string();
                csv << csv_cell(t.at("date").string()) << ',' << csv_cell(t.at("type").string()) << ',' << csv_cell(t.at("category").string()) << ',' << csv_cell(note) << ',' << std::fixed << std::setprecision(2) << t.at("amount").number() << "\r\n";
            }
            respond(s, 200, csv.str(), "text/csv; charset=utf-8", {{"Content-Disposition", "attachment; filename=expense-tracker.csv"}, {"Cache-Control", "no-store"}}); return;
        }
        if (r.path.rfind("/api/", 0) == 0) { respond(s, 404, error_json("API route not found")); return; }
        if (r.method != "GET" && r.method != "HEAD") { respond(s, 405, "Method not allowed", "text/plain; charset=utf-8"); return; }
        std::string asset = r.path.empty() ? "index.html" : r.path.substr(1);
        if (asset.find('/') != std::string::npos || asset.find("..") != std::string::npos || asset != "styles.css") { respond(s, 404, "Not found", "text/plain; charset=utf-8"); return; }
        std::ifstream in(web / asset, std::ios::binary); if (!in) { respond(s, 404, "Web asset not found", "text/plain; charset=utf-8"); return; }
        std::ostringstream content; content << in.rdbuf(); std::string type = "text/css; charset=utf-8";
        respond(s, 200, content.str(), type, {{"Cache-Control", "public, max-age=0, must-revalidate"}}, r.method == "HEAD");
    } catch (const HttpError& e) {
        if (r.path.rfind("/api/", 0) == 0) respond(s, e.status, error_json(e.what()));
        else respond(s, e.status, render_error_page(e.what()), "text/html; charset=utf-8", {{"Cache-Control", "no-store"}, {"Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'; script-src 'none'; object-src 'none'}});
    }
    catch (const std::exception& e) {
        if (r.path.rfind("/api/", 0) == 0) respond(s, 400, error_json(e.what()));
        else if (is_form_action(r.path)) respond(s, 400, render_error_page(e.what()), "text/html; charset=utf-8", {{"Cache-Control", "no-store"}, {"Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'; script-src 'none'; object-src 'none'}});
        else respond(s, 500, render_error_page("Could not render the tracker page."), "text/html; charset=utf-8", {{"Cache-Control", "no-store"}, {"Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'; script-src 'none'; object-src 'none'}});
    }
    catch (...) {
        if (r.path.rfind("/api/", 0) == 0) respond(s, 500, error_json("Unexpected server error"));
        else respond(s, 500, render_error_page("Unexpected server error."), "text/html; charset=utf-8", {{"Cache-Control", "no-store"}, {"Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'; script-src 'none'; object-src 'none'}});
    }
}

static fs::path find_project_root(const char* argv0) {
    std::error_code ec; fs::path cwd = fs::current_path(ec); if (fs::exists(cwd / "web" / "styles.css")) return cwd;
    fs::path exe = fs::absolute(argv0, ec); if (!ec) { auto parent = exe.parent_path(); if (fs::exists(parent / "web" / "styles.css")) return parent; if (fs::exists(parent.parent_path() / "web" / "styles.css")) return parent.parent_path(); if (fs::exists(parent.parent_path().parent_path() / "web" / "styles.css")) return parent.parent_path().parent_path(); }
    return cwd;
}
int main(int argc, char** argv) {
    fs::path root = find_project_root(argv[0]), web = root / "web", data = root / "data" / "expense-tracker.json";
    int port = 8080;
    std::string host = "127.0.0.1";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) { try { port = std::stoi(argv[++i]); } catch (...) { std::cerr << "Invalid port\n"; return 1; } }
        else if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--data" && i + 1 < argc) data = argv[++i];
        else if (arg == "--web" && i + 1 < argc) web = argv[++i];
        else { std::cerr << "Usage: expense-tracker [--host 127.0.0.1] [--port 8080] [--data path] [--web path]\n"; return 1; }
    }
    if (port < 1 || port > 65535) { std::cerr << "Port must be between 1 and 65535\n"; return 1; }
#ifdef _WIN32
    WSADATA wsa{}; if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::cerr << "Could not start Winsock\n"; return 1; }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
    in_addr bind_address{};
    if (!parse_ipv4(host, &bind_address)) {
        std::cerr << "Host must be a valid IPv4 address\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    std::unique_ptr<Store> store;
    try { store = std::make_unique<Store>(data); }
    catch (const std::exception& e) { std::cerr << "Could not open tracker data: " << e.what() << "\n"; return 1; }
    socket_t server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_socket) { std::cerr << "Could not create server socket\n"; return 1; }
    int reuse = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(static_cast<unsigned short>(port)); addr.sin_addr = bind_address;
    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(server, 128) != 0) { std::cerr << "Could not listen on " << host << ':' << port << " (port may be in use)\n"; close_socket(server); return 1; }
    std::cout << "Expense Tracker is ready at http://" << host << ':' << port << "\nData file: " << fs::absolute(data) << "\nPress Ctrl+C to stop.\n" << std::flush;
    while (true) {
        sockaddr_in client{};
#ifdef _WIN32
        int len = sizeof(client);
#else
        socklen_t len = sizeof(client);
#endif
        socket_t peer = accept(server, reinterpret_cast<sockaddr*>(&client), &len);
        if (peer == invalid_socket) continue;
        configure_client_socket(peer);
        if (active_clients.fetch_add(1) >= max_active_clients) {
            active_clients.fetch_sub(1);
            respond(peer, 503, "Tracker is busy. Please retry shortly.", "text/plain; charset=utf-8", {{"Retry-After", "2"}});
            close_socket(peer);
            continue;
        }
        try {
            std::thread([peer, store_ptr = store.get(), web] {
                try { handle(peer, *store_ptr, web); } catch (...) {}
                close_socket(peer);
                active_clients.fetch_sub(1);
            }).detach();
        } catch (...) {
            active_clients.fetch_sub(1);
            respond(peer, 503, "Tracker could not start a request worker.", "text/plain; charset=utf-8", {{"Retry-After", "2"}});
            close_socket(peer);
        }
    }
    close_socket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
