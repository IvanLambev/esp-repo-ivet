# LoveBox 💌

Two Heltec WiFi LoRa 32 V4 boards that display little love notes, backed by a
Next.js web app on Vercel and a Neon Postgres database.

- **`front-end/`** — Next.js app (the dashboard + API). Deploy this on Vercel with
  **Root Directory = `front-end`** and a `DATABASE_URL` env var pointing at Neon.
- **`src/main.cpp` + `platformio.ini`** — ESP32-S3 firmware. Each board polls
  `GET /api/next?device=<MAC>`, shows new messages for 5 minutes, acknowledges them
  (`POST /api/ack`), and can send a note with its button (`POST /api/send`).

## Web app

```bash
cd front-end
npm install
npm run init-db        # creates the devices/messages/reads tables in Neon
npm run dev            # http://localhost:3000
```

### API

| Method | Path                       | Purpose                                    |
| ------ | -------------------------- | ------------------------------------------ |
| GET    | `/api/next?device=<MAC>`   | newest unread message for a board (or null) + heartbeat |
| POST   | `/api/ack` `{device,id}`   | mark a message (and older) read for a board |
| POST   | `/api/send` `{body,from?}` | create a message (web: from=null; board: from=MAC) |
| GET    | `/api/messages`            | recent notes + read-by info (dashboard)    |
| GET    | `/api/devices`             | boards + last-seen (dashboard)             |

## Firmware

Edit `API_BASE` in `src/main.cpp` (or set it via the WiFi config portal) to your
Vercel URL, then:

```bash
python -m platformio run -t upload
```

On first boot with no saved WiFi, the board opens a **`LoveBox-Setup`** Wi-Fi
access point — join it to enter your WiFi credentials and server URL (persisted in
flash). Hold the **PRG** button during boot to re-open that portal later.
