import { expiredSessionCookie } from "../../lib/api/session";
import { methodNotAllowed, runApi } from "../../lib/api/http";

export function POST(request: Request): Promise<Response> {
  return runApi(request, async () => Response.json(
    { ok: true }, { headers: { "Set-Cookie": expiredSessionCookie(request) } },
  ), { publicRoute: true });
}

export function GET(): Response {
  return methodNotAllowed("POST");
}
