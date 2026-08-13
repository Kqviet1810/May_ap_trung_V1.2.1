import { ensureSchema } from '../../_lib/db.js';
import { requireUser } from '../../_lib/auth.js';
import { json, readJson } from '../../_lib/http.js';

export async function onRequestPost({ request, env }) {
  await ensureSchema(env.DB);
  const user = await requireUser(env.DB, request);
  if (!user) return json({ ok: false, error: 'unauthorized' }, 401);
  const body = await readJson(request);
  const endpoint = String(body?.endpoint || '');
  if (endpoint) await env.DB.prepare('DELETE FROM push_subscriptions WHERE user_id=? AND endpoint=?').bind(user.id, endpoint).run();
  return json({ ok: true });
}
