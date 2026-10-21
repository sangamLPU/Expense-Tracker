import { getDb } from "../lib/api/db";
import { runApi } from "../lib/api/http";

function csvCell(value: unknown): string {
  let text = String(value ?? "");
  if (/^[=+@\-\t\r]/.test(text)) text = `'${text}`;
  return `"${text.replace(/"/g, '""')}"`;
}

export function GET(request: Request): Promise<Response> {
  return runApi(request, async () => {
    const sql = await getDb();
    const rows = await sql`SELECT to_char(date, 'YYYY-MM-DD') AS date, type, category, note,
      amount::float8 AS amount FROM expense_transactions ORDER BY date DESC, created_at DESC`;
    const csv = [
      "Date,Type,Category,Note,Amount",
      ...rows.map((row) => [row.date, row.type, row.category, row.note, Number(row.amount).toFixed(2)].map(csvCell).join(",")),
    ].join("\r\n");
    return new Response(csv, { headers: {
      "Content-Type": "text/csv; charset=utf-8",
      "Content-Disposition": "attachment; filename=expense-tracker.csv",
      "Cache-Control": "no-store",
    } });
  });
}
