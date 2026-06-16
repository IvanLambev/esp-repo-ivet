import { neon } from "@neondatabase/serverless";

// Lazily create the Neon HTTP client so that `next build` doesn't crash if
// DATABASE_URL is momentarily absent. All API routes are force-dynamic, so the
// client is only ever created at request time (where the env var is set).
let _sql;

export function getSql() {
  if (!_sql) {
    const url = process.env.DATABASE_URL;
    if (!url) throw new Error("DATABASE_URL is not set");
    _sql = neon(url);
  }
  return _sql;
}
