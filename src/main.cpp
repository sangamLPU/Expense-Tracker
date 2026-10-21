#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t invalid_socket = INVALID_SOCKET;
static void close_socket(socket_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t invalid_socket = -1;
static void close_socket(socket_t s) { close(s); }
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
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
                    if (cp >= 0xd800 && cp <= 0xdbff && p + 2 <= s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                        p += 2; unsigned low = hex4();
                        if (low >= 0xdc00 && low <= 0xdfff) cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    }
                    utf8(out, cp); break;
                }
                default: throw std::runtime_error("Invalid string escape");
            }
        }
        throw std::runtime_error("Unterminated string");
    }
    Json value() {
        ws();
        if (p >= s.size()) throw std::runtime_error("Expected JSON value");
        if (s[p] == '"') return Json(str());
        if (s[p] == '{') {
            ++p; Json::Object o; ws(); if (p < s.size() && s[p] == '}') { ++p; return Json(std::move(o)); }
            while (true) { ws(); if (p >= s.size() || s[p] != '"') throw std::runtime_error("Expected object key"); auto k = str(); ws(); if (take() != ':') throw std::runtime_error("Expected colon"); o[k] = value(); ws(); char c = take(); if (c == '}') break; if (c != ',') throw std::runtime_error("Expected comma"); }
            return Json(std::move(o));
        }
        if (s[p] == '[') {
            ++p; Json::Array a; ws(); if (p < s.size() && s[p] == ']') { ++p; return Json(std::move(a)); }
            while (true) { a.push_back(value()); ws(); char c = take(); if (c == ']') break; if (c != ',') throw std::runtime_error("Expected comma"); }
            return Json(std::move(a));
        }
        if (consume("true")) return Json(true);
        if (consume("false")) return Json(false);
        if (consume("null")) return Json();
        size_t begin = p;
        if (s[p] == '-') ++p;
        if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) throw std::runtime_error("Invalid number");
        while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
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
static std::string url_decode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) { try { o.push_back(static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16))); i += 2; } catch (...) { o.push_back(s[i]); } }
        else if (s[i] == '+') o.push_back(' '); else o.push_back(s[i]);
    }
    return o;
}
static std::string csv_cell(const std::string& s) { std::string x = s; size_t pos = 0; while ((pos = x.find('"', pos)) != std::string::npos) { x.insert(pos, 1, '"'); pos += 2; } return '"' + x + '"'; }
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
        out << json_text(state); out.close();
        std::error_code ec; fs::rename(tmp, file, ec);
        if (ec) { fs::remove(file, ec); ec.clear(); fs::rename(tmp, file, ec); }
        if (ec) throw std::runtime_error("Could not save data file: " + ec.message());
    }
public:
    explicit Store(fs::path path) : file(std::move(path)), state(initial_state()) {
        if (fs::exists(file)) {
            std::ifstream in(file, std::ios::binary); std::ostringstream buf; buf << in.rdbuf();
            try { Json loaded = JsonParser(buf.str()).parse(); if (loaded.is_object() && loaded.at("transactions").is_array() && loaded.at("categories").is_array() && loaded.at("budgets").is_object()) state = std::move(loaded); }
            catch (const std::exception& e) { std::cerr << "Warning: data file could not be read: " << e.what() << "\n"; }
        } else { if (!file.parent_path().empty()) fs::create_directories(file.parent_path()); std::lock_guard<std::mutex> lock(mutex); save_locked(); }
        if (!state.at("monthBudget").is_number()) state["monthBudget"] = Json(0);
        if (!state.at("currency").is_string()) state["currency"] = Json("INR");
    }
    Json snapshot() const { std::lock_guard<std::mutex> lock(mutex); return state; }
    Json add_or_update(Json body, const std::string& id = {}) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!body.is_object()) throw std::runtime_error("Request body must be a JSON object");
        const Json& type = body.at("type"); const Json& amount = body.at("amount"); const Json& category = body.at("category"); const Json& date = body.at("date"); const Json& note = body.at("note");
        if (!type.is_string() || (type.string() != "expense" && type.string() != "income")) throw std::runtime_error("Choose income or expense");
        if (!amount.is_number() || !std::isfinite(amount.number()) || amount.number() <= 0 || amount.number() > 1000000000.0) throw std::runtime_error("Enter an amount greater than zero");
        if (!category.is_string() || !has_category(state, category.string())) throw std::runtime_error("Choose a valid category");
        if (!date.is_string() || !valid_date(date.string())) throw std::runtime_error("Enter a valid date (YYYY-MM-DD)");
        if (note.is_string() && note.string().size() > 240) throw std::runtime_error("Notes must be 240 characters or fewer");
        Json row(Json::Object{}); row["id"] = id.empty() ? Json(std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) + "-" + std::to_string(next_id++)) : Json(id);
        row["type"] = type; row["amount"] = amount; row["category"] = category; row["date"] = date; row["note"] = note.is_string() ? note : Json("");
        auto& rows = state["transactions"].array(); auto found = std::find_if(rows.begin(), rows.end(), [&](const Json& x) { return x.at("id").is_string() && x.at("id").string() == id && !id.empty(); });
        if (!id.empty() && found == rows.end()) throw HttpError(404, "Transaction not found");
        if (found == rows.end()) rows.push_back(row); else *found = row;
        save_locked(); return row;
    }
    bool erase_transaction(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex); auto& rows = state["transactions"].array(); auto old = rows.size();
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Json& x) { return x.at("id").is_string() && x.at("id").string() == id; }), rows.end());
        if (rows.size() != old) save_locked(); return rows.size() != old;
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
        state["monthBudget"] = body.at("monthBudget"); state["budgets"] = Json(std::move(clean)); state["currency"] = currency; save_locked();
    }
    void add_category(const Json& body) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto& n = body.at("name"); if (!n.is_string()) throw std::runtime_error("Category name is required");
        std::string name = trim(n.string()); if (name.empty() || name.size() > 30) throw std::runtime_error("Category name must be 1–30 characters");
        for (const auto& c : state.at("categories").array()) if (lower(c.string()) == lower(name)) throw std::runtime_error("That category already exists");
        state["categories"].array().push_back(Json(name)); save_locked();
    }
    void delete_category(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex); auto& cats = state["categories"].array();
        if (cats.size() <= 1) throw std::runtime_error("Keep at least one category");
        if (!has_category(state, name)) throw std::runtime_error("Category not found");
        if (name == "Food" || name == "Transport" || name == "Shopping" || name == "Bills" || name == "Entertainment" || name == "Health" || name == "Home" || name == "Travel" || name == "Education" || name == "Salary" || name == "Other") throw std::runtime_error("Default categories cannot be removed");
        for (const auto& t : state.at("transactions").array()) if (t.at("category").is_string() && t.at("category").string() == name) throw std::runtime_error("This category has transactions. Reassign or remove them first.");
        cats.erase(std::remove_if(cats.begin(), cats.end(), [&](const Json& c) { return c.is_string() && c.string() == name; }), cats.end());
        state["budgets"].object().erase(name); save_locked();
    }
};

struct Request { std::string method, target, path, body; std::map<std::string, std::string> headers; };
static std::string status_text(int status) { switch (status) { case 200: return "OK"; case 201: return "Created"; case 204: return "No Content"; case 400: return "Bad Request"; case 404: return "Not Found"; case 405: return "Method Not Allowed"; case 413: return "Payload Too Large"; case 500: return "Internal Server Error"; default: return "OK"; } }
static void respond(socket_t s, int status, const std::string& body, const std::string& type = "application/json; charset=utf-8", const std::vector<std::pair<std::string, std::string>>& extra = {}) {
    std::ostringstream head; head << "HTTP/1.1 " << status << ' ' << status_text(status) << "\r\nContent-Type: " << type << "\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n";
    for (const auto& h : extra) head << h.first << ": " << h.second << "\r\n";
    head << "\r\n"; std::string data = head.str() + body;
    size_t sent = 0; while (sent < data.size()) {
        int n = static_cast<int>(send(s, data.data() + sent, static_cast<int>(std::min<size_t>(data.size() - sent, 65536)), 0));
        if (n <= 0) break; sent += static_cast<size_t>(n);
    }
}
static bool read_request(socket_t s, Request& r) {
    std::string data; char buf[8192]; size_t split = std::string::npos;
    while ((split = data.find("\r\n\r\n")) == std::string::npos && data.size() < 1024 * 1024) {
        int n = static_cast<int>(recv(s, buf, sizeof(buf), 0)); if (n <= 0) return false; data.append(buf, static_cast<size_t>(n));
    }
    if (split == std::string::npos) { respond(s, 413, "{\"error\":\"Headers too large\"}"); return false; }
    std::istringstream headers(data.substr(0, split)); std::string line;
    if (!std::getline(headers, line)) return false; if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream first(line); first >> r.method >> r.target;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back(); auto colon = line.find(':'); if (colon == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, colon))), value = trim(line.substr(colon + 1)); r.headers[key] = value;
    }
    size_t length = 0; try { if (r.headers.count("content-length")) length = static_cast<size_t>(std::stoull(r.headers["content-length"])); } catch (...) { respond(s, 400, "{\"error\":\"Invalid content length\"}"); return false; }
    if (length > 1024 * 1024) { respond(s, 413, "{\"error\":\"Request body too large\"}"); return false; }
    size_t body_start = split + 4; while (data.size() - body_start < length) { int n = static_cast<int>(recv(s, buf, sizeof(buf), 0)); if (n <= 0) return false; data.append(buf, static_cast<size_t>(n)); }
    r.body = data.substr(body_start, length);
    auto q = r.target.find('?'); r.path = r.target.substr(0, q);
    return !r.method.empty();
}

static std::string error_json(const std::string& e) { Json j(Json::Object{}); j["error"] = Json(e); return json_text(j); }
static void handle(socket_t s, Store& store, const fs::path& web) {
    Request r;
    try {
        if (!read_request(s, r)) return;
        if (r.path == "/api/state" && r.method == "GET") { respond(s, 200, json_text(store.snapshot())); return; }
        if (r.path == "/api/transactions" && (r.method == "POST" || r.method == "PUT")) {
            Json result = store.add_or_update(JsonParser(r.body).parse()); respond(s, r.method == "POST" ? 201 : 200, json_text(result)); return;
        }
        const std::string txPrefix = "/api/transactions/";
        if (r.path.rfind(txPrefix, 0) == 0) {
            std::string id = url_decode(r.path.substr(txPrefix.size()));
            if (id.empty() || id.find('/') != std::string::npos) { respond(s, 404, error_json("Transaction not found")); return; }
            if (r.method == "PUT") { Json result = store.add_or_update(JsonParser(r.body).parse(), id); respond(s, 200, json_text(result)); return; }
            if (r.method == "DELETE") { if (!store.erase_transaction(id)) respond(s, 404, error_json("Transaction not found")); else respond(s, 200, "{\"ok\":true}"); return; }
            respond(s, 405, error_json("Method not allowed")); return;
        }
        if (r.path == "/api/budgets" && r.method == "PUT") { store.set_budgets(JsonParser(r.body).parse()); respond(s, 200, "{\"ok\":true}"); return; }
        if (r.path == "/api/categories" && r.method == "POST") { store.add_category(JsonParser(r.body).parse()); respond(s, 201, "{\"ok\":true}"); return; }
        const std::string catPrefix = "/api/categories/";
        if (r.path.rfind(catPrefix, 0) == 0 && r.method == "DELETE") { store.delete_category(url_decode(r.path.substr(catPrefix.size()))); respond(s, 200, "{\"ok\":true}"); return; }
        if (r.path == "/api/export.csv" && r.method == "GET") {
            Json state = store.snapshot(); std::ostringstream csv; csv << "Date,Type,Category,Note,Amount\r\n";
            std::vector<Json> rows = state.at("transactions").array(); std::sort(rows.begin(), rows.end(), [](const Json& a, const Json& b) { return a.at("date").is_string() && b.at("date").is_string() && a.at("date").string() > b.at("date").string(); });
            for (const auto& t : rows) {
                std::string note = t.at("note").string();
                if (!note.empty() && (note[0] == '=' || note[0] == '+' || note[0] == '-' || note[0] == '@' || note[0] == '\t' || note[0] == '\r')) note.insert(note.begin(), '\'');
                csv << csv_cell(t.at("date").string()) << ',' << csv_cell(t.at("type").string()) << ',' << csv_cell(t.at("category").string()) << ',' << csv_cell(note) << ',' << std::fixed << std::setprecision(2) << t.at("amount").number() << "\r\n";
            }
            respond(s, 200, csv.str(), "text/csv; charset=utf-8", {{"Content-Disposition", "attachment; filename=expense-tracker.csv"}}); return;
        }
        if (r.path.rfind("/api/", 0) == 0) { respond(s, 404, error_json("API route not found")); return; }
        if (r.method != "GET" && r.method != "HEAD") { respond(s, 405, "Method not allowed", "text/plain; charset=utf-8"); return; }
        std::string asset = r.path == "/" || r.path.empty() ? "index.html" : r.path.substr(1);
        if (asset.find('/') != std::string::npos || asset.find("..") != std::string::npos || (asset != "index.html" && asset != "app.js" && asset != "styles.css")) { respond(s, 404, "Not found", "text/plain; charset=utf-8"); return; }
        std::ifstream in(web / asset, std::ios::binary); if (!in) { respond(s, 404, "Web asset not found", "text/plain; charset=utf-8"); return; }
        std::ostringstream content; content << in.rdbuf(); std::string type = asset.size() >= 4 && asset.substr(asset.size() - 4) == ".css" ? "text/css; charset=utf-8" : asset.size() >= 3 && asset.substr(asset.size() - 3) == ".js" ? "text/javascript; charset=utf-8" : "text/html; charset=utf-8";
        if (r.method == "HEAD") respond(s, 200, "", type); else respond(s, 200, content.str(), type);
    } catch (const HttpError& e) { respond(s, e.status, error_json(e.what())); }
    catch (const std::exception& e) { respond(s, 400, error_json(e.what())); }
    catch (...) { respond(s, 500, error_json("Unexpected server error")); }
}

static fs::path find_project_root(const char* argv0) {
    std::error_code ec; fs::path cwd = fs::current_path(ec); if (fs::exists(cwd / "web" / "index.html")) return cwd;
    fs::path exe = fs::absolute(argv0, ec); if (!ec) { auto parent = exe.parent_path(); if (fs::exists(parent / "web" / "index.html")) return parent; if (fs::exists(parent.parent_path() / "web" / "index.html")) return parent.parent_path(); if (fs::exists(parent.parent_path().parent_path() / "web" / "index.html")) return parent.parent_path().parent_path(); }
    return cwd;
}
int main(int argc, char** argv) {
    fs::path root = find_project_root(argv[0]), web = root / "web", data = root / "data" / "expense-tracker.json";
    int port = 8080;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) { try { port = std::stoi(argv[++i]); } catch (...) { std::cerr << "Invalid port\n"; return 1; } }
        else if (arg == "--data" && i + 1 < argc) data = argv[++i];
        else if (arg == "--web" && i + 1 < argc) web = argv[++i];
        else { std::cerr << "Usage: expense-tracker [--port 8080] [--data path] [--web path]\n"; return 1; }
    }
    if (port < 1 || port > 65535) { std::cerr << "Port must be between 1 and 65535\n"; return 1; }
#ifdef _WIN32
    WSADATA wsa{}; if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { std::cerr << "Could not start Winsock\n"; return 1; }
#endif
    Store store(data);
    socket_t server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_socket) { std::cerr << "Could not create server socket\n"; return 1; }
    int reuse = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(static_cast<unsigned short>(port)); addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(server, 32) != 0) { std::cerr << "Could not listen on 127.0.0.1:" << port << " (port may be in use)\n"; close_socket(server); return 1; }
    std::cout << "Expense Tracker is ready at http://127.0.0.1:" << port << "\nData file: " << fs::absolute(data) << "\nPress Ctrl+C to stop.\n" << std::flush;
    while (true) {
        sockaddr_in client{};
#ifdef _WIN32
        int len = sizeof(client);
#else
        socklen_t len = sizeof(client);
#endif
        socket_t peer = accept(server, reinterpret_cast<sockaddr*>(&client), &len);
        if (peer == invalid_socket) continue;
        std::thread([peer, &store, web] { handle(peer, store, web); close_socket(peer); }).detach();
    }
    close_socket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
