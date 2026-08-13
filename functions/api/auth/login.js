import { ensureSchema } from '../../_lib/db.js';
import { verifyPassword, createSession, sessionCookie } from '../../_lib/auth.js';
import { json, readJson, normalizeEmail } from '../../_lib/http.js';

export async function onRequestPost({ request, env }) {
  await ensureSchema(env.DB);
  const body = await readJson(request);
  const email = normalizeEmail(body?.email);
  const password = String(body?.password || '');
  const user = await env.DB.prepare('SELECT * FROM users WHERE email=?').bind(email).first();
  if (!user || user.disabled || !(await verifyPassword(password, user.password_salt, user.password_hash))) {
    return json({ ok: false, error: 'invalid_credentials' }, 401);
  }
  const session = await createSession(env.DB, user.id);
  return json({ ok: true, user: { id: user.id, email: user.email, name: user.name, role: user.role } }, 200, {
    'set-cookie': sessionCookie(session.token, session.expiresAt)
  });
}
