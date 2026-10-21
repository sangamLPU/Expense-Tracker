import { getDb } from "../lib/api/db";
import { methodNotAllowed, runApi } from "../lib/api/http";

export function GET(request: Request): Promise<Response> {
  return runApi(request, async () => {
    const sql = await getDb();
    const [transactions, categories, settings] = await Promise.all([
      sql`SELECT id, type, amount::float8 AS amount, category,
          to_char(date, 'YYYY-MM-DD') AS date, note
        FROM expense_transactions ORDER BY date DESC, created_at DESC`,
      sql`SELECT name FROM expense_categories ORDER BY is_default DESC, created_at ASC`,
      sql`SELECT month_budget::float8 AS month_budget, budgets, currency
        FROM expense_settings WHERE id = 1`,
    ]);
    const saved = settings[0] as { month_budget: number; budgets: Record<string, number>; currency: string };
    return Response.json({
      transactions,
      categories: categories.map((row) => row.name),
      budgets: saved.budgets || {},
      monthBudget: Number(saved.month_budget || 0),
      currency: saved.currency || "INR",
    });
  });
}

export function POST(): Response {
  return methodNotAllowed("GET");
}
