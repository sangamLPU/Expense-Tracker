import { neon, type NeonQueryFunction } from "@neondatabase/serverless";
import { ApiError } from "./errors";

type Database = NeonQueryFunction<false, false>;
let client: Database | undefined;
let schemaReady: Promise<void> | undefined;

function database() {
  const connectionString = process.env.DATABASE_URL || process.env.POSTGRES_URL;
  if (!connectionString) {
    throw new ApiError(503, "Connect a Postgres database and set DATABASE_URL or POSTGRES_URL in Vercel.");
  }
  if (!client) client = neon<false, false>(connectionString);
  return client;
}

async function initializeSchema(sql: Database): Promise<void> {
  await sql`CREATE TABLE IF NOT EXISTS expense_settings (
    id integer PRIMARY KEY CHECK (id = 1),
    month_budget numeric NOT NULL DEFAULT 0 CHECK (month_budget >= 0),
    budgets jsonb NOT NULL DEFAULT '{}'::jsonb,
    currency text NOT NULL DEFAULT 'INR' CHECK (currency IN ('INR','USD','EUR','GBP','CAD','AUD','JPY'))
  )`;
  await sql`CREATE TABLE IF NOT EXISTS expense_categories (
    name text PRIMARY KEY,
    is_default boolean NOT NULL DEFAULT false,
    created_at timestamptz NOT NULL DEFAULT now()
  )`;
  await sql`CREATE UNIQUE INDEX IF NOT EXISTS expense_categories_lower_name_idx
    ON expense_categories (lower(name))`;
  await sql`CREATE TABLE IF NOT EXISTS expense_transactions (
    id text PRIMARY KEY,
    type text NOT NULL CHECK (type IN ('expense','income')),
    amount numeric NOT NULL CHECK (amount > 0 AND amount <= 1000000000),
    category text NOT NULL REFERENCES expense_categories(name) ON UPDATE CASCADE,
    date date NOT NULL,
    note text NOT NULL DEFAULT '' CHECK (char_length(note) <= 240),
    created_at timestamptz NOT NULL DEFAULT now()
  )`;
  await sql`CREATE INDEX IF NOT EXISTS expense_transactions_date_idx
    ON expense_transactions (date DESC, created_at DESC)`;
  await sql`INSERT INTO expense_categories (name, is_default) VALUES
    ('Food', true), ('Transport', true), ('Shopping', true), ('Bills', true),
    ('Entertainment', true), ('Health', true), ('Home', true), ('Travel', true),
    ('Education', true), ('Salary', true), ('Other', true)
    ON CONFLICT (name) DO NOTHING`;
  await sql`INSERT INTO expense_settings (id) VALUES (1) ON CONFLICT (id) DO NOTHING`;
}

export async function getDb(): Promise<Database> {
  const sql = database();
  if (!schemaReady) {
    schemaReady = initializeSchema(sql).catch((error) => {
      schemaReady = undefined;
      throw error;
    });
  }
  await schemaReady;
  return sql;
}

export function isUniqueViolation(error: unknown): boolean {
  return Boolean(error && typeof error === "object" && "code" in error && error.code === "23505");
}

export function isForeignKeyViolation(error: unknown): boolean {
  return Boolean(error && typeof error === "object" && "code" in error && error.code === "23503");
}
