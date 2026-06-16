// Dev utility (DESTRUCTIVE): wipe all notes and any test devices.
// Run: node --env-file=.env scripts/reset-db.mjs
import { neon } from "@neondatabase/serverless";

const url = process.env.DATABASE_URL;
if (!url) {
  console.error("DATABASE_URL not set. Run: node --env-file=.env scripts/reset-db.mjs");
  process.exit(1);
}

const sql = neon(url);

await sql`TRUNCATE reads, messages RESTART IDENTITY CASCADE`;
await sql`DELETE FROM devices WHERE id IN ('TESTDEV01', 'AAAA', 'BBBB')`;

console.log("Wiped messages + reads (ids restarted) and removed test devices.");
