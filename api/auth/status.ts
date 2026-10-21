import { assertAuthConfigured, isAuthenticated } from "../../lib/api/session";
import { methodNotAllowed, runApi } from "../../lib/api/http";

export function GET(request: Request): Promise<Response> {
  return runApi(request, async () => {
    assertAuthConfigured();
    return Response.json({ authenticated: isAuthenticated(request) });
  }, { publicRoute: true });
}

export function POST(): Response {
  return methodNotAllowed("GET");
}
