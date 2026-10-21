import { getDb, isForeignKeyViolation } from "../../lib/api/db";
import { ApiError, methodNotAllowed, runApi } from "../../lib/api/http";

export function DELETE(request: Request): Promise<Response> {
  return runApi(request, async () => {
    const name = decodeURIComponent(new URL(request.url).pathname.split("/").pop() || "");
    if (!name) throw new ApiError(404, "Category not found.");
    const sql = await getDb();
    const categories = await sql`SELECT is_default FROM expense_categories WHERE name = ${name}`;
    if (!categories.length) throw new ApiError(404, "Category not found.");
    if (categories[0].is_default) throw new ApiError(400, "Default categories cannot be removed.");
    const used = await sql`SELECT 1 FROM expense_transactions WHERE category = ${name} LIMIT 1`;
    if (used.length) throw new ApiError(409, "This category has transactions. Reassign or remove them first.");
    try {
      await sql`DELETE FROM expense_categories WHERE name = ${name}`;
    } catch (error) {
      if (isForeignKeyViolation(error)) throw new ApiError(409, "This category has transactions. Reassign or remove them first.");
      throw error;
    }
    await sql`UPDATE expense_settings SET budgets = budgets - ${name} WHERE id = 1`;
    return Response.json({ ok: true });
  });
}

export function POST(): Response {
  return methodNotAllowed("DELETE");
}
