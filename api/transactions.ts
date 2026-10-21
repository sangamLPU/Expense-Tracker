import { getDb } from "../lib/api/db.js";
import { ApiError, methodNotAllowed, readJson, runApi } from "../lib/api/http.js";
import { randomUUID } from "node:crypto";

const validDate = (value: string) => {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
  const date = new Date(`${value}T00:00:00.000Z`);
  return Number.isFinite(date.getTime()) && date.toISOString().slice(0, 10) === value;
};

async function saveTransaction(request: Request, id?: string): Promise<Response> {
  return runApi(request, async () => {
    const body = await readJson(request);
    const type = body.type;
    const amount = Number(body.amount);
    const category = body.category;
    const date = body.date;
    const note = body.note == null ? "" : body.note;
    if (type !== "expense" && type !== "income") throw new ApiError(400, "Choose income or expense.");
    if (!Number.isFinite(amount) || amount <= 0 || amount > 1_000_000_000) throw new ApiError(400, "Enter an amount greater than zero.");
    if (typeof category !== "string") throw new ApiError(400, "Choose a valid category.");
    if (typeof date !== "string" || !validDate(date)) throw new ApiError(400, "Enter a valid date (YYYY-MM-DD).");
    if (typeof note !== "string" || note.length > 240) throw new ApiError(400, "Notes must be 240 characters or fewer.");

    const sql = await getDb();
    const foundCategory = await sql`SELECT name FROM expense_categories WHERE name = ${category}`;
    if (!foundCategory.length) throw new ApiError(400, "Choose a valid category.");

    const transactionId = id || randomUUID();
    const rows = id
      ? await sql`UPDATE expense_transactions SET type = ${type}, amount = ${amount}, category = ${category},
          date = ${date}, note = ${note} WHERE id = ${transactionId}
          RETURNING id, type, amount::float8 AS amount, category, to_char(date, 'YYYY-MM-DD') AS date, note`
      : await sql`INSERT INTO expense_transactions (id, type, amount, category, date, note)
          VALUES (${transactionId}, ${type}, ${amount}, ${category}, ${date}, ${note})
          RETURNING id, type, amount::float8 AS amount, category, to_char(date, 'YYYY-MM-DD') AS date, note`;
    if (!rows.length) throw new ApiError(404, "Transaction not found.");
    return Response.json(rows[0], { status: id ? 200 : 201 });
  });
}

export function POST(request: Request): Promise<Response> {
  return saveTransaction(request);
}

export function GET(): Response {
  return methodNotAllowed("POST");
}
