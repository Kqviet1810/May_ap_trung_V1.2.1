import { ensureSchema } from '../../_lib/db.js';
import { requireUser } from '../../_lib/auth.js';
import { json } from '../../_lib/http.js';

export async function onRequestGet({ request, env }) {
  await ensureSchema(env.DB);
  const user = await requireUser(env.DB, request);
  if (!user) return json({ ok: false, error: 'unauthorized' }, 401);
  const query = user.role === 'admin'
    ? env.DB.prepare(`SELECT d.device_id, d.name, d.disabled, u.email AS owner_email
        FROM devices d LEFT JOIN user_devices ud ON ud.device_id=d.id
        LEFT JOIN users u ON u.id=ud.user_id ORDER BY d.created_at DESC`)
    : env.DB.prepare(`SELECT d.device_id, d.name, d.disabled
        FROM devices d JOIN user_devices ud ON ud.device_id=d.id
        WHERE ud.user_id=? ORDER BY d.created_at DESC`).bind(user.id);
  const { results = [] } = await query.all();
  return json({ ok: true, devices: results });
}
