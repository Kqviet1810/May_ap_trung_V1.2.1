import { ensureSchema } from '../../_lib/db.js';
import { currentUser } from '../../_lib/auth.js';
import { json } from '../../_lib/http.js';

export async function onRequestGet({ request, env }) {
  await ensureSchema(env.DB);
  const user = await currentUser(env.DB, request);
  if (!user) return json({ ok: false, error: 'unauthorized' }, 401);
  return json({ ok: true, user });
}
