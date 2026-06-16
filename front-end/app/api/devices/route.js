import { getSql } from "@/lib/db";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// GET /api/devices  -> all known boards and when they last polled
export async function GET() {
  const sql = getSql();
  const rows = await sql`
    SELECT id, label, last_seen
    FROM devices
    ORDER BY last_seen DESC NULLS LAST`;

  return Response.json(
    rows.map((d) => ({ id: d.id, label: d.label, last_seen: d.last_seen }))
  );
}
