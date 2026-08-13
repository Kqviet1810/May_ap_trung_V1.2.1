import { ensureSchema } from '../../_lib/db.js';
import { requireUser } from '../../_lib/auth.js';
import { json, readJson } from '../../_lib/http.js';

export async function onRequestPost({ request, env }) {
  await ensureSchema(env.DB);
  const user = await requireUser(env.DB, request);
  if (!user) return json({ ok: false, error: 'unauthorized' }, 401);
  const body = await readJson(request);
  const endpoint = String(body?.endpoint || '');
  const p256dh = String(body?.keys?.p256dh || '');
  const auth = String(body?.keys?.auth || '');
  if (!endpoint || !p256dh || !auth) return json({ ok: false, error: 'invalid_subscription' }, 400);
  const now = Date.now();
  await env.DB.prepare(`INSERT INTO push_subscriptions(user_id,endpoint,p256dh,auth,user_agent,created_at,updated_at)
    VALUES(?,?,?,?,?,?,?) ON CONFLICT(endpoint) DO UPDATE SET user_id=excluded.user_id,p256dh=excluded.p256dh,auth=excluded.auth,user_agent=excluded.user_agent,updated_at=excluded.updated_at`)
    .bind(user.id, endpoint, p256dh, auth, request.headers.get('user-agent') || '', now, now).run();
  return json({ ok: true });
}
