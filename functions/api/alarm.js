import { ensureSchema } from '../_lib/db.js';
import { sha256Hex } from '../_lib/auth.js';
import { sendPushToUsers } from '../_lib/push.js';
import { json, readJson, normalizeDeviceId } from '../_lib/http.js';

function titleFor(code) {
  const map = {
    23: 'Cần xác nhận sau mất điện',
    41: 'Mất tín hiệu cảm biến',
    53: 'Auto Tune PID thất bại',
    80: 'Lệnh điều khiển bị từ chối'
  };
  if (map[code]) return map[code];
  if (code >= 1000) return `Cảnh báo hệ thống #${code - 1000}`;
  return 'Cảnh báo MAYAP';
}

function bodyFor(payload) {
  const parts = [];
  if (Number.isFinite(Number(payload.temperature))) parts.push(`${Number(payload.temperature).toFixed(1).replace('.', ',')}°C`);
  if (Number.isFinite(Number(payload.humidity))) parts.push(`${Math.round(Number(payload.humidity))}%RH`);
  if (payload.message) parts.push(String(payload.message).slice(0, 100));
  return parts.join(' · ') || 'Máy vừa phát sinh cảnh báo.';
}

export async function onRequestPost({ request, env }) {
  try {
    await ensureSchema(env.DB);
    const payload = await readJson(request);
    const deviceId = normalizeDeviceId(payload?.deviceId);
    const authHeader = request.headers.get('authorization') || '';
    const secret = authHeader.startsWith('Bearer ') ? authHeader.slice(7).trim() : '';
    if (!deviceId || !secret) return json({ ok: false, error: 'unauthorized' }, 401);

    const device = await env.DB.prepare('SELECT id,device_id,name,alarm_secret_hash,disabled FROM devices WHERE device_id=?').bind(deviceId).first();
    if (!device || device.disabled || (await sha256Hex(secret)) !== device.alarm_secret_hash) {
      return json({ ok: false, error: 'unauthorized' }, 401);
    }

    const code = Number(payload?.code || 0);
    const now = Date.now();
    await env.DB.prepare('INSERT INTO alarm_events(device_id,code,value,payload_json,created_at) VALUES(?,?,?,?,?)')
      .bind(device.id, code, Number.isFinite(Number(payload?.value)) ? Number(payload.value) : null, JSON.stringify(payload), now).run();

    const { results = [] } = await env.DB.prepare('SELECT user_id FROM user_devices WHERE device_id=?').bind(device.id).all();
    const userIds = results.map((row) => Number(row.user_id)).filter(Boolean);
    const pushResult = await sendPushToUsers(env.DB, userIds, {
      title: `${device.name}: ${titleFor(code)}`,
      body: bodyFor(payload),
      icon: '/icons/icon-192.png',
      badge: '/icons/icon-192.png',
      tag: `${device.device_id}-${code}`,
      data: { deviceId: device.device_id, code, url: `/?device=${encodeURIComponent(device.device_id)}` },
      requireInteraction: code === 23 || code === 41 || code >= 1000
    });
    return json({ ok: true, push: pushResult });
  } catch (error) {
    console.error('alarm_error', error);
    return json({ ok: false, error: error?.message || 'alarm_failed' }, 500);
  }
}
