import { getDb } from "../lib/api/db.js";
import { ApiError, methodNotAllowed, readJson, runApi } from "../lib/api/http.js";

const allowedCurrencies = new Set(["INR", "USD", "EUR", "GBP", "CAD", "AUD", "JPY"]);

export function PUT(request: Request): Promise<Response> {
  return runApi(request, async () => {
    const body = await readJson(request);
    const monthBudget = Number(body.monthBudget || 0);
    const currency = body.currency;
    const suppliedBudgets = body.budgets;
    if (!Number.isFinite(monthBudget) || monthBudget < 0 || monthBudget > 1_000_000_000) {
      throw new ApiError(400, "Enter a valid monthly budget.");
    }
    if (typeof currency !== "string" || !allowedCurrencies.has(currency)) throw new ApiError(400, "Choose a supported currency.");
    if (!suppliedBudgets || typeof suppliedBudgets !== "object" || Array.isArray(suppliedBudgets)) {
      throw new ApiError(400, "Category budgets must be an object.");
    }
    const budgets: Record<string, number> = {};
    for (const [name, value] of Object.entries(suppliedBudgets)) {
      const amount = Number(value);
      if (!Number.isFinite(amount) || amount < 0 || amount > 1_000_000_000) {
        throw new ApiError(400, "Category budgets must be zero or less than 1,000,000,000.");
      }
      if (amount > 0) budgets[name] = amount;
    }

    const sql = await getDb();
    const categories = await sql`SELECT name FROM expense_categories`;
    const names = new Set(categories.map((row) => row.name as string));
    for (const name of Object.keys(budgets)) {
      if (!names.has(name)) throw new ApiError(400, `Unknown category: ${name}.`);
    }
    await sql`UPDATE expense_settings SET month_budget = ${monthBudget},
      budgets = ${JSON.stringify(budgets)}::jsonb, currency = ${currency} WHERE id = 1`;
    return Response.json({ ok: true });
  });
}

export function GET(): Response {
  return methodNotAllowed("PUT");
}
