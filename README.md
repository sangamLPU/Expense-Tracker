# Milo expense tracker

Milo is a server-rendered expense tracker. The HTTP server, page rendering, validation, transaction handling, reports, and persistent storage are implemented in C++17. The browser receives HTML and CSS; there is no JavaScript, TypeScript, Node.js, or Vercel backend.

> **Public access:** This version has no sign-in. Anyone who can reach the website can view, add, edit, or delete its data. Do not store sensitive financial information in it. Add authentication and backups before using it for private records.

## Deploy on a VPS

The included Docker Compose setup runs the C++ application behind Caddy. Caddy obtains and renews HTTPS certificates for your domain.

1. Install Docker Engine and the Docker Compose plugin on a Linux VPS.
2. Point your domain's DNS `A` record to the VPS IPv4 address. Allow inbound TCP ports `80` and `443` in the VPS firewall.
3. On the VPS, clone the repository and create the environment file:

   ```sh
   git clone https://github.com/sangamLPU/Expense-Tracker.git
   cd Expense-Tracker
   cp .env.example .env
   ```

4. Edit `.env` and set `DOMAIN` to your real domain.
5. Build and start the services:

   ```sh
   docker compose up -d --build
   ```

Open `https://your-domain`. The C++ tracker is reachable only through Caddy. Data lives in the persistent Docker volume `tracker_data` at `/data/expense-tracker.json`.

### Update and back up

Run these commands from the repository directory on your VPS to deploy a pushed change:

```sh
git pull
docker compose up -d --build
```

View application logs with `docker compose logs -f tracker`. Copy a data backup with:

```sh
docker compose cp tracker:/data/expense-tracker.json ./expense-tracker-backup.json
```

Keep the `tracker_data` volume when updating or restarting. Removing it deletes saved transactions and settings.

## Build locally

Requirements: CMake 3.16 or newer and a C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/expense-tracker --host 127.0.0.1 --port 8080 --data ./data/expense-tracker.json --web ./web
```

Open `http://127.0.0.1:8080`. On Windows, start `build\Release\expense-tracker.exe` (or the generated executable path for your compiler) with the same arguments.

## Included functionality

- Add, edit, and delete income and expense transactions.
- Month summaries for balance, income, spending, and savings rate.
- Spending breakdown and six-month income/spending chart.
- Monthly and per-category budgets, with seven supported currencies.
- Search, month/category/type filters, and transaction pagination.
- Add and remove custom categories.
- CSV export.
- JSON file persistence, protected by a mutex and atomic file replacement.
- REST API for the same data, in addition to the server-rendered forms.

## API routes

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/api/state` | Read transactions, categories, currency, and budgets |
| `POST` | `/api/transactions` | Add a transaction |
| `PUT` | `/api/transactions/{id}` | Update a transaction |
| `DELETE` | `/api/transactions/{id}` | Delete a transaction |
| `PUT` | `/api/budgets` | Save budgets and currency |
| `POST` | `/api/categories` | Add a category |
| `DELETE` | `/api/categories/{name}` | Remove an unused custom category |
| `GET` | `/api/export.csv` | Download all transactions as CSV |

## Source layout

- `src/main.cpp` — C++17 HTTP server, server-rendered HTML, validation, REST routes, CSV generation, and persistent storage.
- `web/styles.css` — responsive presentation styles; no browser-side application code.
- `Dockerfile`, `docker-compose.yml`, and `Caddyfile` — VPS deployment with persistent storage and HTTPS.
- `CMakeLists.txt` — native C++ build configuration.
