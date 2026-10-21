import { createHmac, createHash, timingSafeEqual } from "node:crypto";
import { ApiError } from "./errors";

const COOKIE_NAME = "milo_session";
const SESSION_SECONDS = 60 * 60 * 24 * 7;

function authConfig() {
  const password = process.env.APP_PASSWORD;
  const secret = process.env.SESSION_SECRET;
  if (!password || password.length < 12) {
    throw new ApiError(503, "Set APP_PASSWORD to a unique password of at least 12 characters in Vercel.");
  }
  if (!secret || secret.length < 32) {
    throw new ApiError(503, "Set SESSION_SECRET to a random secret of at least 32 characters in Vercel.");
  }
  return { password, secret };
}

function signature(expiresAt: string, secret: string): string {
  return createHmac("sha256", secret).update(expiresAt).digest("hex");
}

function constantTimeEqual(left: string, right: string): boolean {
  const leftHash = createHash("sha256").update(left).digest();
  const rightHash = createHash("sha256").update(right).digest();
  return timingSafeEqual(leftHash, rightHash);
}

function cookieValue(request: Request): string | undefined {
  const entry = request.headers.get("cookie")?.split(";").map((part) => part.trim())
    .find((part) => part.startsWith(`${COOKIE_NAME}=`));
  return entry?.slice(COOKIE_NAME.length + 1);
}

export function assertAuthConfigured(): void {
  authConfig();
}

export function passwordIsValid(candidate: string): boolean {
  const { password } = authConfig();
  return constantTimeEqual(candidate, password);
}

export function isAuthenticated(request: Request): boolean {
  const { secret } = authConfig();
  const value = cookieValue(request);
  if (!value) return false;
  const separator = value.lastIndexOf(".");
  if (separator < 1) return false;
  const expiresAt = value.slice(0, separator);
  const suppliedSignature = value.slice(separator + 1);
  if (!/^\d+$/.test(expiresAt) || Number(expiresAt) <= Date.now()) return false;
  return constantTimeEqual(suppliedSignature, signature(expiresAt, secret));
}

export function sessionCookie(request: Request): string {
  const { secret } = authConfig();
  const expiresAt = String(Date.now() + SESSION_SECONDS * 1000);
  const value = `${expiresAt}.${signature(expiresAt, secret)}`;
  const secure = new URL(request.url).protocol === "https:" ? "; Secure" : "";
  return `${COOKIE_NAME}=${value}; Path=/; HttpOnly; SameSite=Lax; Max-Age=${SESSION_SECONDS}${secure}`;
}

export function expiredSessionCookie(request: Request): string {
  const secure = new URL(request.url).protocol === "https:" ? "; Secure" : "";
  return `${COOKIE_NAME}=; Path=/; HttpOnly; SameSite=Lax; Max-Age=0${secure}`;
}
