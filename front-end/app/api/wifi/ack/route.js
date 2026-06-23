import { getSql } from "@/lib/db";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// POST /api/wifi/ack  { device, id, status, detail? }
export async function POST(request) {
  let data;
  try {
    data = await request.json();
  } catch {
    return Response.json({ error: "bad json" }, { status: 400 });
  }

  const device = (data.device || "").toString().trim();
  const id = Number(data.id);
  const status = (data.status || "saved").toString().trim();
  const detail = data.detail == null ? null : data.detail.toString().slice(0, 200);

  if (!device || !Number.isFinite(id) || id <= 0) {
    return Response.json({ error: "device and id required" }, { status: 400 });
  }

  const sql = getSql();
  await sql`
    INSERT INTO wifi_command_acks (command_id, device_id, status, detail)
    VALUES (${id}, ${device}, ${status}, ${detail})
    ON CONFLICT (command_id, device_id) DO UPDATE
    SET status = EXCLUDED.status,
        detail = EXCLUDED.detail,
        acked_at = now()`;

  return Response.json({ ok: true });
}
