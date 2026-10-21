import { getDb } from "../../lib/api/db";
import { ApiError, methodNotAllowed, readJson, runApi } from "../../lib/api/http";

const validDate = (value: string) => {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
  const date = new Date(`${value}T00:00:00.000Z`);
  return Number.isFinite(date.getTime()) && date.toISOString().slice(0, 10) === value;
};

export function PUT(request: Request): Promise<Response> {
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
    const transactionId = decodeURIComponent(new URL(request.url).pathname.split("/").pop() || "");
    if (!transactionId) throw new ApiError(404, "Transaction not found.");
    const foundCategory = await sql`SELECT name FROM expense_categories WHERE name = ${category}`;
    if (!foundCategory.length) throw new ApiError(400, "Choose a valid category.");
    const rows = await sql`UPDATE expense_transactions SET type = ${type}, amount = ${amount}, category = ${category},
      date = ${date}, note = ${note} WHERE id = ${transactionId}
      RETURNING id, type, amount::float8 AS amount, category, to_char(date, 'YYYY-MM-DD') AS date, note`;
    if (!rows.length) throw new ApiError(404, "Transaction not found.");
    return Response.json(rows[0]);
  });
}

export function DELETE(request: Request): Promise<Response> {
  return runApi(request, async () => {
    const sql = await getDb();
    const transactionId = decodeURIComponent(new URL(request.url).pathname.split("/").pop() || "");
    if (!transactionId) throw new ApiError(404, "Transaction not found.");
    const rows = await sql`DELETE FROM expense_transactions WHERE id = ${transactionId} RETURNING id`;
    if (!rows.length) throw new ApiError(404, "Transaction not found.");
    return Response.json({ ok: true });
  });
}

export function POST(): Response {
  return methodNotAllowed("PUT, DELETE");
}
