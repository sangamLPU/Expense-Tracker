# Milo expense tracker

A responsive personal expense tracker with a static HTML/CSS/JavaScript interface, Vercel serverless API functions, a Postgres database, password sign-in, transaction management, budgets, custom categories, filters, charts, and CSV export.

## Deploy on Vercel

1. Push this folder to a GitHub repository and import that repository in Vercel.
2. Keep the project root as the Vercel root directory. `vercel.json` sets `web/` as the static output directory, and the root `api/` directory contains the serverless functions.
3. In the Vercel project, add a Postgres integration from **Storage / Marketplace** (for example Neon). Make sure the project receives `DATABASE_URL` or `POSTGRES_URL`.
4. Add these environment variables in **Project Settings → Environment Variables**:

   - `APP_PASSWORD`: a unique password with at least 12 characters.
   - `SESSION_SECRET`: a random secret with at least 32 characters. Generate one in PowerShell with:

     ```powershell
     [Convert]::ToBase64String([Security.Cryptography.RandomNumberGenerator]::GetBytes(48))
     ```

5. Deploy or redeploy. The API creates the tables and default categories on its first database request.

The site is password-protected. The session cookie is HTTP-only, uses `SameSite=Lax`, and expires after seven days. Use a long, unique workspace password and keep these environment values private.

## Local development

Install dependencies, link the folder to your Vercel project, pull its development environment variables, then start the local Vercel server:

```powershell
npm install
npx vercel link
npx vercel env pull .env.local
npx vercel dev
```

To view the UI without a database or sign-in, open `/index.html?demo=1`. Demo changes are saved only in that browser and never fall back silently from a failed deployed API.

## API routes

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/api/auth/status` | Check the sign-in session |
| `POST` | `/api/auth/login` | Start a password-protected session |
| `POST` | `/api/auth/logout` | End the session |
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
- `api/` — Vercel request handlers and shared authentication/database code.
- `src/main.cpp` and `CMakeLists.txt` — the original local C++ server, retained as source reference; Vercel serves the TypeScript API in `api/`.
