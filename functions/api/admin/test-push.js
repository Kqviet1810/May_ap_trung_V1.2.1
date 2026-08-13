import { ensureSchema } from '../../_lib/db.js';
import { requireUser } from '../../_lib/auth.js';
import { sendPushToUsers } from '../../_lib/push.js';
import { json } from '../../_lib/http.js';

export async function onRequestPost({ request, env }) {
  await ensureSchema(env.DB);
  const admin = await requireUser(env.DB, request, 'admin');
  if (!admin) return json({ ok: false, error: 'forbidden' }, 403);
  const result = await sendPushToUsers(env.DB, [Number(admin.id)], {
    title: 'MAYAP · Kiểm tra Web Push',
    body: 'Thông báo nền hoạt động. Có thể đóng PWA và tiếp tục kiểm tra cảnh báo ESP32.',
    icon: '/icons/icon-192.png',
    badge: '/icons/icon-192.png',
    tag: 'mayap-push-test',
    data: { url: '/' }
  });
  return json({ ok: true, push: result });
}
