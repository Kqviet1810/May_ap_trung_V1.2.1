import { ensureSchema } from '../../_lib/db.js';
import { requireUser, makePassword, randomToken, sha256Hex } from '../../_lib/auth.js';
import { json, readJson, normalizeEmail, normalizeDeviceId } from '../../_lib/http.js';

const DEVICE_RE = /^MAP-[A-F0-9]{12}$/;

export async function onRequestPost({ request, env }) {
  await ensureSchema(env.DB);
  const admin = await requireUser(env.DB, request, 'admin');
  if (!admin) return json({ ok: false, error: 'forbidden' }, 403);
  const body = await readJson(request);
  const email = normalizeEmail(body?.email);
  const name = String(body?.name || '').trim() || email;
  const password = String(body?.password || '');
  const deviceId = normalizeDeviceId(body?.deviceId);
  const deviceName = String(body?.deviceName || '').trim() || deviceId;
  if (!email.includes('@') || !DEVICE_RE.test(deviceId)) return json({ ok: false, error: 'invalid_input' }, 400);

  let user = await env.DB.prepare('SELECT id,email,name FROM users WHERE email=?').bind(email).first();
  if (!user) {
    if (password.length < 10) return json({ ok: false, error: 'password_min_10' }, 400);
    const credential = await makePassword(password);
    const result = await env.DB.prepare(`INSERT INTO users(email,name,role,password_salt,password_hash,created_at) VALUES(?,?,?,?,?,?)`)
      .bind(email, name, 'user', credential.salt, credential.hash, Date.now()).run();
    user = { id: Number(result.meta.last_row_id), email, name };
  }

  let device = await env.DB.prepare('SELECT id,device_id,name FROM devices WHERE device_id=?').bind(deviceId).first();
  let alarmSecret = '';
  if (!device) {
    alarmSecret = `mayap_${randomToken(24)}`;
    const result = await env.DB.prepare(`INSERT INTO devices(device_id,name,alarm_secret_hash,created_at) VALUES(?,?,?,?)`)
      .bind(deviceId, deviceName, await sha256Hex(alarmSecret), Date.now()).run();
    device = { id: Number(result.meta.last_row_id), device_id: deviceId, name: deviceName };
  }

  await env.DB.prepare(`INSERT INTO user_devices(user_id,device_id,permission) VALUES(?,?,?)
    ON CONFLICT(user_id,device_id) DO UPDATE SET permission=excluded.permission`)
    .bind(user.id, device.id, 'owner').run();

  return json({
    ok: true,
    user: { email: user.email, name: user.name },
    device: { deviceId: device.device_id, name: device.name },
    alarmSecret: alarmSecret || null,
    note: alarmSecret ? 'Lưu secret này vào ESP32; server sẽ không trả lại lần nữa.' : 'Thiết bị đã tồn tại; secret hiện tại không bị thay đổi.'
  });
}
