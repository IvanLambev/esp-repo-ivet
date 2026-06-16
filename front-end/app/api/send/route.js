import { getSql } from "@/lib/db";

export const runtime = "nodejs";
export const dynamic = "force-dynamic";

// POST /api/send  { body, from? }
// Inserts a new message. The web dashboard sends from=null (every device then
// receives it); an ESP button sends from=<its MAC> so only the *other* board
// shows it.
export async function POST(request) {
  let data;
  try {
    data = await request.json();
  } catch {
    return Response.json({ error: "bad json" }, { status: 400 });
  }

  const body = (data.body || "").toString().trim();
  const from = data.from ? data.from.toString().trim() : null;

  if (!body) {
    return Response.json({ error: "body required" }, { status: 400 });
  }
  if (body.length > 500) {
    return Response.json({ error: "body too long (max 500)" }, { status: 400 });
  }

  const sql = getSql();
  const rows = await sql`
    INSERT INTO messages (body, sender)
    VALUES (${body}, ${from})
    RETURNING id`;

  return Response.json({ ok: true, id: Number(rows[0].id) });
}
