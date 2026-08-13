import { ensureSchema } from '../_lib/db.js';
import { json } from '../_lib/http.js';

export async function onRequestGet({ env }) {
  try {
    await ensureSchema(env.DB);
    const row = await env.DB.prepare('SELECT COUNT(*) AS count FROM users').first();
    return json({ ok: true, service: 'mayap-cloud', setupRequired: Number(row?.count || 0) === 0 });
  } catch (error) {
    return json({ ok: false, service: 'mayap-cloud', error: error?.message || 'cloud_not_configured' }, 503);
  }
}
