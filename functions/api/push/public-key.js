import { ensureSchema, getSetting } from '../../_lib/db.js';
import { json } from '../../_lib/http.js';

export async function onRequestGet({ env }) {
  await ensureSchema(env.DB);
  const publicKey = await getSetting(env.DB, 'vapid_public_key');
  if (!publicKey) return json({ ok: false, error: 'vapid_not_initialized' }, 503);
  return json({ ok: true, publicKey });
}
