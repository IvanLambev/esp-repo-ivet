"use client";

import { useEffect, useState, useCallback } from "react";

const PRESETS = [
  "I love you ❤️",
  "Sent you kisses",
  "Thinking of you",
  "Miss you",
  "Good morning sunshine",
  "Goodnight, love",
];

// Boards are considered online if they polled within this window.
const ONLINE_MS = 45_000;

function shortId(id) {
  if (!id) return "?";
  return id.length > 4 ? id.slice(-4).toUpperCase() : id.toUpperCase();
}

function relTime(ts) {
  if (!ts) return "never";
  const diff = Date.now() - new Date(ts).getTime();
  if (diff < 0) return "just now";
  const s = Math.floor(diff / 1000);
  if (s < 10) return "just now";
  if (s < 60) return `${s}s ago`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m ago`;
  const h = Math.floor(m / 60);
  if (h < 24) return `${h}h ago`;
  return `${Math.floor(h / 24)}d ago`;
}

export default function Home() {
  const [text, setText] = useState("");
  const [sending, setSending] = useState(false);
  const [messages, setMessages] = useState([]);
  const [devices, setDevices] = useState([]);

  const refresh = useCallback(async () => {
    try {
      const [m, d] = await Promise.all([
        fetch("/api/messages", { cache: "no-store" }).then((r) => r.json()),
        fetch("/api/devices", { cache: "no-store" }).then((r) => r.json()),
      ]);
      if (Array.isArray(m)) setMessages(m);
      if (Array.isArray(d)) setDevices(d);
    } catch {
      /* transient network hiccup; next tick retries */
    }
  }, []);

  useEffect(() => {
    refresh();
    const t = setInterval(refresh, 4000);
    return () => clearInterval(t);
  }, [refresh]);

  const send = useCallback(
    async (body) => {
      const value = (body ?? text).trim();
      if (!value || sending) return;
      setSending(true);
      try {
        const res = await fetch("/api/send", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ body: value }),
        });
        if (res.ok) {
          setText("");
          await refresh();
        }
      } finally {
        setSending(false);
      }
    },
    [text, sending, refresh]
  );

  const onlineCount = devices.filter(
    (d) => d.last_seen && Date.now() - new Date(d.last_seen).getTime() < ONLINE_MS
  ).length;

  return (
    <div className="wrap">
      <h1>💌 LoveBox</h1>
      <p className="sub">
        Send a little note — it lights up the boxes. {onlineCount}/{devices.length || 0} online.
      </p>

      <div className="card">
        <h2>New message</h2>
        <textarea
          value={text}
          onChange={(e) => setText(e.target.value)}
          placeholder="Write something sweet…"
          maxLength={500}
          onKeyDown={(e) => {
            if ((e.metaKey || e.ctrlKey) && e.key === "Enter") send();
          }}
        />
        <button className="send" onClick={() => send()} disabled={sending || !text.trim()}>
          {sending ? "Sending…" : "Send 💌"}
        </button>
        <div className="row chips">
          {PRESETS.map((p) => (
            <button key={p} className="chip" onClick={() => send(p)} disabled={sending}>
              {p}
            </button>
          ))}
        </div>
      </div>

      <div className="card">
        <h2>Boxes</h2>
        {devices.length === 0 && (
          <div className="empty">No boxes have checked in yet.</div>
        )}
        {devices.map((d) => {
          const online =
            d.last_seen && Date.now() - new Date(d.last_seen).getTime() < ONLINE_MS;
          return (
            <div className="device" key={d.id}>
              <span>
                <span className={"dot" + (online ? " on" : "")} />
                <span className="mono">{d.label || shortId(d.id)}</span>
              </span>
              <span className="meta">{online ? "online" : relTime(d.last_seen)}</span>
            </div>
          );
        })}
      </div>

      <div className="card">
        <h2>Recent notes</h2>
        {messages.length === 0 && <div className="empty">Nothing sent yet.</div>}
        {messages.map((m) => (
          <div className="msg" key={m.id}>
            <div className="body">{m.body}</div>
            <div className="meta">
              <span>from {m.sender ? shortId(m.sender) : "web"}</span>
              <span>·</span>
              <span>{relTime(m.ts)}</span>
              {m.read_by && m.read_by.length > 0 ? (
                m.read_by.map((r) => (
                  <span className="badge" key={r}>
                    read · {shortId(r)}
                  </span>
                ))
              ) : (
                <span className="badge unread">unread</span>
              )}
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
