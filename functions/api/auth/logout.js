import { ensureSchema } from '../../_lib/db.js';
import { destroySession, clearSessionCookie } from '../../_lib/auth.js';
import { json } from '../../_lib/http.js';

export async function onRequestPost({ request, env }) {
  await ensureSchema(env.DB);
  await destroySession(env.DB, request);
  return json({ ok: true }, 200, { 'set-cookie': clearSessionCookie() });
}
