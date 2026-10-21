# Milo expense tracker

A responsive expense tracker with a static HTML/CSS/JavaScript interface, Vercel serverless API functions, Postgres persistence, transaction management, budgets, custom categories, filters, charts, and CSV export.

> **Public access:** There is no sign-in. Anyone with the deployed URL can read, add, change, or delete the stored transactions and budgets. Do not store sensitive financial information in this deployment.

## Deploy on Vercel

1. Import this GitHub repository in Vercel and keep the project root as the Vercel root directory.
2. `vercel.json` serves the static interface from `web/`; API functions are in the root `api/` directory.
3. Add a Postgres integration (for example, Neon) and make sure the project has `DATABASE_URL` or `POSTGRES_URL` set.
4. Deploy or redeploy. The API creates the tables and default categories on its first database request.

## Local development

Install dependencies, link the folder to your Vercel project, pull its database environment variable, then start the local Vercel server:

```powershell
npm install
npx vercel link
npx vercel env pull .env.local
npx vercel dev
```

To preview the UI without a database, open `/index.html?demo=1`. Demo changes are stored only in that browser and never fall back silently from a failed deployed API.

## API routes

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/api/state` | Read transactions, categories, currency, and budgets |
| `POST` | `/api/transactions` | Add an income or expense |
| `PUT` | `/api/transactions/{id}` | Update a transaction |
| `DELETE` | `/api/transactions/{id}` | Delete a transaction |
| `PUT` | `/api/budgets` | Save monthly and category budgets and currency |
| `POST` | `/api/categories` | Add a category |
| `DELETE` | `/api/categories/{name}` | Remove an unused custom category |
| `GET` | `/api/export.csv` | Download all transactions as CSV |

## Project layout

- `web/` — static user interface served by Vercel.
- `api/` — public Vercel request handlers.
- `lib/api/` — shared database and HTTP helpers.
- `src/main.cpp` and `CMakeLists.txt` — the original local C++ server, retained as source reference; Vercel serves the TypeScript API in `api/`.
