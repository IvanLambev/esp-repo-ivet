import { getSql } from "@/lib/db";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// GET /api/messages  -> recent messages with the list of devices that read each
export async function GET() {
  const sql = getSql();
  const rows = await sql`
    SELECT m.id, m.body, m.sender, m.created_at,
      COALESCE(
        array_agg(r.device_id) FILTER (WHERE r.device_id IS NOT NULL),
        ARRAY[]::text[]
      ) AS read_by
    FROM messages m
    LEFT JOIN reads r ON r.message_id = m.id
    GROUP BY m.id
    ORDER BY m.id DESC
    LIMIT 50`;

  return Response.json(
    rows.map((m) => ({
      id: Number(m.id),
      body: m.body,
      sender: m.sender,
      ts: m.created_at,
      read_by: m.read_by,
    }))
  );
}
