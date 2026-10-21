import { getDb, isUniqueViolation } from "../lib/api/db.js";
import { ApiError, methodNotAllowed, readJson, runApi } from "../lib/api/http.js";

export function POST(request: Request): Promise<Response> {
  return runApi(request, async () => {
    const body = await readJson(request);
    if (typeof body.name !== "string") throw new ApiError(400, "Category name is required.");
    const name = body.name.trim();
    if (!name || name.length > 30) throw new ApiError(400, "Category name must be 1–30 characters.");
    const sql = await getDb();
    const duplicate = await sql`SELECT name FROM expense_categories WHERE lower(name) = lower(${name})`;
    if (duplicate.length) throw new ApiError(409, "That category already exists.");
    try {
      await sql`INSERT INTO expense_categories (name) VALUES (${name})`;
    } catch (error) {
      if (isUniqueViolation(error)) throw new ApiError(409, "That category already exists.");
      throw error;
    }
    return Response.json({ ok: true }, { status: 201 });
  });
}

export function GET(): Response {
  return methodNotAllowed("POST");
}
