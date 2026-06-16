// One-shot, non-destructive migration. Run with:
//   node --env-file=.env scripts/init-db.mjs   (or: npm run init-db)
import { neon } from "@neondatabase/serverless";

const url = process.env.DATABASE_URL;
if (!url) {
  console.error("DATABASE_URL not set. Run: node --env-file=.env scripts/init-db.mjs");
  process.exit(1);
}

const sql = neon(url);

console.log("Creating tables (IF NOT EXISTS)...");

await sql`
  CREATE TABLE IF NOT EXISTS devices (
    id         TEXT PRIMARY KEY,
    label      TEXT,
    last_seen  TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
  )`;

await sql`
  CREATE TABLE IF NOT EXISTS messages (
    id         BIGSERIAL PRIMARY KEY,
    body       TEXT NOT NULL,
    sender     TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
  )`;

await sql`
  CREATE TABLE IF NOT EXISTS reads (
    message_id BIGINT NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
    device_id  TEXT NOT NULL,
    read_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (message_id, device_id)
  )`;

await sql`CREATE INDEX IF NOT EXISTS idx_messages_id ON messages (id DESC)`;

const tables = await sql`
  SELECT table_name FROM information_schema.tables
  WHERE table_schema = 'public'
  ORDER BY table_name`;

console.log("Public tables:", tables.map((r) => r.table_name).join(", "));
console.log("Done.");
