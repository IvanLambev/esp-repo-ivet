import { getSql } from "@/lib/db";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// GET /api/wifi?device=<MAC>
// Returns the newest WiFi credential command not yet acknowledged by the board.
export async function GET(request) {
  const { searchParams } = new URL(request.url);
  const device = (searchParams.get("device") || "").trim();
  if (!device) {
    return Response.json({ error: "device required" }, { status: 400 });
  }

  const sql = getSql();
  const rows = await sql`
    SELECT c.id, c.ssid, c.password, c.created_at
    FROM wifi_commands c
    WHERE (c.target_device IS NULL OR c.target_device = ${device})
      AND NOT EXISTS (
        SELECT 1 FROM wifi_command_acks a
        WHERE a.command_id = c.id AND a.device_id = ${device}
      )
    ORDER BY c.id DESC
    LIMIT 1`;

  if (rows.length === 0) {
    return Response.json({ id: null });
  }

  const c = rows[0];
  return Response.json({
    id: Number(c.id),
    ssid: c.ssid,
    password: c.password,
    ts: c.created_at,
  });
}

// POST /api/wifi  { ssid, password, target? }
// target omitted/null queues the credential for every known board.
export async function POST(request) {
  let data;
  try {
    data = await request.json();
  } catch {
    return Response.json({ error: "bad json" }, { status: 400 });
  }

  const ssid = (data.ssid || "").toString().trim();
  const password = data.password == null ? "" : data.password.toString();
  const target = data.target ? data.target.toString().trim() : null;

  if (!ssid) {
    return Response.json({ error: "ssid required" }, { status: 400 });
  }
  if (ssid.length > 32) {
    return Response.json({ error: "ssid too long (max 32)" }, { status: 400 });
  }
  if (password.length < 8 || password.length > 63) {
    return Response.json({ error: "password must be 8-63 characters" }, { status: 400 });
  }

  const sql = getSql();
  const rows = await sql`
    INSERT INTO wifi_commands (ssid, password, target_device)
    VALUES (${ssid}, ${password}, ${target})
    RETURNING id`;

  return Response.json({ ok: true, id: Number(rows[0].id) });
}
