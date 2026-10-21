import { methodNotAllowed, readJson, runApi } from "../../lib/api/http";
import { passwordIsValid, sessionCookie } from "../../lib/api/session";

export function POST(request: Request): Promise<Response> {
  return runApi(request, async () => {
    const body = await readJson(request);
    if (typeof body.password !== "string" || !passwordIsValid(body.password)) {
      return Response.json({ error: "That password did not match." }, { status: 401 });
    }
    return Response.json({ ok: true }, { headers: { "Set-Cookie": sessionCookie(request) } });
  }, { publicRoute: true });
}

export function GET(): Response {
  return methodNotAllowed("POST");
}
