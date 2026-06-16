import { getSql } from "@/lib/db";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// POST /api/ack  { device, id }
// Marks the given message AND every older pending message as read for this
// device, so old notes never resurface after the latest one is acknowledged.
export async function POST(request) {
  let data;
  try {
    data = await request.json();
  } catch {
    return Response.json({ error: "bad json" }, { status: 400 });
  }

  const device = (data.device || "").toString().trim();
  const id = Number(data.id);
  if (!device || !Number.isFinite(id) || id <= 0) {
    return Response.json({ error: "device and id required" }, { status: 400 });
  }

  const sql = getSql();
  await sql`
    INSERT INTO reads (message_id, device_id)
    SELECT m.id, ${device}
    FROM messages m
    WHERE m.id <= ${id}
      AND m.sender IS DISTINCT FROM ${device}
    ON CONFLICT DO NOTHING`;

  return Response.json({ ok: true });
}
