const COOKIE_NAME = 'mayap_session';
const SESSION_TTL_MS = 30 * 24 * 60 * 60 * 1000;
const PBKDF2_ITERATIONS = 210000;

function bytesToHex(bytes) {
  return [...new Uint8Array(bytes)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

function hexToBytes(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
  return out;
}

export async function sha256Hex(value) {
  return bytesToHex(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(String(value))));
}

export function randomToken(bytes = 32) {
  const raw = new Uint8Array(bytes);
  crypto.getRandomValues(raw);
  return bytesToHex(raw);
}

export async function makePassword(password, saltHex = '') {
  const salt = saltHex ? hexToBytes(saltHex) : crypto.getRandomValues(new Uint8Array(16));
  const key = await crypto.subtle.importKey('raw', new TextEncoder().encode(String(password)), 'PBKDF2', false, ['deriveBits']);
  const bits = await crypto.subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations: PBKDF2_ITERATIONS }, key, 256);
  return { salt: bytesToHex(salt), hash: bytesToHex(bits) };
}

function safeEqual(a, b) {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

export async function verifyPassword(password, salt, expectedHash) {
  const result = await makePassword(password, salt);
  return safeEqual(result.hash, String(expectedHash || ''));
}

function getCookie(request, name) {
  const raw = request.headers.get('cookie') || '';
  for (const part of raw.split(';')) {
    const [key, ...rest] = part.trim().split('=');
    if (key === name) return decodeURIComponent(rest.join('='));
  }
  return '';
}

export async function createSession(db, userId) {
  const token = randomToken();
  const hash = await sha256Hex(token);
  const now = Date.now();
  await db.prepare('INSERT INTO sessions(token_hash, user_id, expires_at, created_at) VALUES(?,?,?,?)')
    .bind(hash, userId, now + SESSION_TTL_MS, now).run();
  return { token, expiresAt: now + SESSION_TTL_MS };
}

export function sessionCookie(token, expiresAt) {
  return `${COOKIE_NAME}=${encodeURIComponent(token)}; Path=/; HttpOnly; Secure; SameSite=Lax; Expires=${new Date(expiresAt).toUTCString()}`;
}

export function clearSessionCookie() {
  return `${COOKIE_NAME}=; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=0`;
}

export async function destroySession(db, request) {
  const token = getCookie(request, COOKIE_NAME);
  if (!token) return;
  await db.prepare('DELETE FROM sessions WHERE token_hash=?').bind(await sha256Hex(token)).run();
}

export async function currentUser(db, request) {
  const token = getCookie(request, COOKIE_NAME);
  if (!token) return null;
  const hash = await sha256Hex(token);
  const now = Date.now();
  const row = await db.prepare(`SELECT u.id, u.email, u.name, u.role, u.disabled
    FROM sessions s JOIN users u ON u.id=s.user_id
    WHERE s.token_hash=? AND s.expires_at>?`).bind(hash, now).first();
  if (!row || row.disabled) return null;
  return row;
}

export async function requireUser(db, request, role = '') {
  const user = await currentUser(db, request);
  if (!user) return null;
  if (role && user.role !== role) return null;
  return user;
}
