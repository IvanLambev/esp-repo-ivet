import { getSql } from "@/lib/db";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// GET /api/next?device=<MAC>
// Records the device's heartbeat and returns the newest *pending* message for
// it (a message not sent by this device and not yet read by it), or {id:null}.
export async function GET(request) {
  const { searchParams } = new URL(request.url);
  const device = (searchParams.get("device") || "").trim();
  if (!device) {
    return Response.json({ error: "device required" }, { status: 400 });
  }

  const sql = getSql();

  await sql`
    INSERT INTO devices (id, last_seen)
    VALUES (${device}, now())
    ON CONFLICT (id) DO UPDATE SET last_seen = now()`;

  const rows = await sql`
    SELECT m.id, m.body, m.created_at
    FROM messages m
    WHERE m.sender IS DISTINCT FROM ${device}
      AND NOT EXISTS (
        SELECT 1 FROM reads r
        WHERE r.message_id = m.id AND r.device_id = ${device}
      )
    ORDER BY m.id DESC
    LIMIT 1`;

  if (rows.length === 0) {
    return Response.json({ id: null });
  }

  const m = rows[0];
  return Response.json({ id: Number(m.id), body: m.body, ts: m.created_at });
}
