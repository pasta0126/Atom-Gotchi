'use strict';

const express    = require('express');
const Database   = require('better-sqlite3');
const rateLimit  = require('express-rate-limit');
const cors       = require('cors');
const path       = require('path');

// ── Database ──────────────────────────────────────────────────────────────────

const db = new Database(path.join('/app/data', 'gotchi.db'));
db.pragma('journal_mode = WAL');

db.exec(`
  CREATE TABLE IF NOT EXISTS commands (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    action     TEXT    NOT NULL CHECK(action IN ('feed','drink','pet')),
    created_at INTEGER NOT NULL DEFAULT (unixepoch())
  );

  CREATE TABLE IF NOT EXISTS state (
    id         INTEGER PRIMARY KEY CHECK(id = 1),
    mood       INTEGER NOT NULL DEFAULT 0,
    hunger     INTEGER NOT NULL DEFAULT 80,
    thirst     INTEGER NOT NULL DEFAULT 80,
    energy     INTEGER NOT NULL DEFAULT 80,
    steps      INTEGER NOT NULL DEFAULT 0,
    flags      INTEGER NOT NULL DEFAULT 0,
    updated_at INTEGER NOT NULL DEFAULT (unixepoch())
  );

  CREATE TABLE IF NOT EXISTS feed (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    action     TEXT    NOT NULL,
    flag       TEXT    NOT NULL DEFAULT '🌍',
    created_at INTEGER NOT NULL DEFAULT (unixepoch())
  );

  INSERT OR IGNORE INTO state (id) VALUES (1);
`);

// Prepared statements
const stmts = {
  insertCmd:    db.prepare('INSERT INTO commands (action) VALUES (?)'),
  nextCmd:      db.prepare('SELECT id, action FROM commands ORDER BY id ASC LIMIT 1'),
  deleteCmd:    db.prepare('DELETE FROM commands WHERE id = ?'),
  updateState:  db.prepare(`
    UPDATE state
    SET mood=?, hunger=?, thirst=?, energy=?, steps=?, flags=?, updated_at=unixepoch()
    WHERE id=1
  `),
  getState:     db.prepare('SELECT * FROM state WHERE id=1'),
  insertFeed:   db.prepare('INSERT INTO feed (action, flag) VALUES (?, ?)'),
  trimFeed:     db.prepare('DELETE FROM feed WHERE id NOT IN (SELECT id FROM feed ORDER BY id DESC LIMIT 50)'),
  getFeed:      db.prepare('SELECT action, flag, created_at FROM feed ORDER BY id DESC LIMIT 20'),
};

// ── App ───────────────────────────────────────────────────────────────────────

const app = express();
app.set('trust proxy', 1);
app.use(express.json());
app.use(cors());

// Rate limit: 1 command per IP per 5 seconds
const cmdLimiter = rateLimit({
  windowMs: 5_000,
  max: 1,
  standardHeaders: true,
  legacyHeaders: false,
  message: { error: 'Too fast — wait a moment before sending another command.' },
});

// ── Flags pool ────────────────────────────────────────────────────────────────

const FLAGS = [
  '🇺🇸','🇯🇵','🇧🇷','🇩🇪','🇫🇷','🇬🇧','🇪🇸','🇮🇹','🇦🇺','🇨🇦',
  '🇲🇽','🇦🇷','🇰🇷','🇨🇳','🇮🇳','🇷🇺','🇵🇱','🇳🇱','🇸🇪','🇵🇹',
  '🇨🇭','🇦🇹','🇧🇪','🇳🇴','🇩🇰','🇿🇦','🇳🇬','🇵🇭','🇹🇭','🇮🇩',
];
const randomFlag = () => FLAGS[Math.floor(Math.random() * FLAGS.length)];

// ── Routes ────────────────────────────────────────────────────────────────────

// POST /api/command — web enqueues a command
app.post('/api/command', cmdLimiter, (req, res) => {
  const { action } = req.body ?? {};
  if (!['feed', 'drink', 'pet'].includes(action)) {
    return res.status(400).json({ error: 'action must be feed, drink, or pet' });
  }
  const result = stmts.insertCmd.run(action);

  // Also write to public feed
  stmts.insertFeed.run(action, randomFlag());
  stmts.trimFeed.run();

  res.status(201).json({ id: result.lastInsertRowid, action });
});

// GET /api/command/next — device polls; returns and immediately removes the oldest command
app.get('/api/command/next', (req, res) => {
  const cmd = stmts.nextCmd.get();
  if (!cmd) return res.json(null);
  stmts.deleteCmd.run(cmd.id);
  res.json({ action: cmd.action });
});

// POST /api/state — device pushes its current state
app.post('/api/state', (req, res) => {
  const { mood = 0, hunger = 80, thirst = 80, energy = 80, steps = 0, flags = 0 } = req.body ?? {};
  stmts.updateState.run(mood, hunger, thirst, energy, steps, flags);
  res.json({ ok: true });
});

// GET /api/state — web reads current state
app.get('/api/state', (req, res) => {
  res.json(stmts.getState.get());
});

// GET /api/feed — web reads recent global actions
app.get('/api/feed', (req, res) => {
  res.json(stmts.getFeed.all());
});

// ── Start ─────────────────────────────────────────────────────────────────────

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`[gotchi-api] listening on :${PORT}`);
});
