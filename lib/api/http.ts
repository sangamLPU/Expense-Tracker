import { ApiError } from "./errors.js";

export { ApiError } from "./errors.js";

function noStore(response: Response): Response {
  const headers = new Headers(response.headers);
  headers.set("Cache-Control", "no-store");
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
}

export async function runApi(
  _request: Request,
  action: () => Promise<Response>,
): Promise<Response> {
  try {
    return noStore(await action());
  } catch (error) {
    if (error instanceof ApiError) {
      return noStore(Response.json({ error: error.message }, { status: error.status }));
    }
    console.error("Expense tracker API error:", error);
    return noStore(Response.json(
      { error: "The request could not be completed. Check the Vercel function logs." },
      { status: 500 },
    ));
  }
}

export async function readJson(request: Request): Promise<Record<string, unknown>> {
  let value: unknown;
  try {
    value = await request.json();
  } catch {
    throw new ApiError(400, "Request body must be valid JSON.");
  }
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new ApiError(400, "Request body must be a JSON object.");
  }
  return value as Record<string, unknown>;
}

export function methodNotAllowed(allow: string): Response {
  return Response.json(
    { error: "Method not allowed." },
    { status: 405, headers: { Allow: allow } },
  );
}
