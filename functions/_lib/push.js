import webpush from 'web-push';
import { getSetting } from './db.js';

export async function getVapid(db) {
  const [publicKey, privateKey, subject] = await Promise.all([
    getSetting(db, 'vapid_public_key'),
    getSetting(db, 'vapid_private_key'),
    getSetting(db, 'vapid_subject')
  ]);
  if (!publicKey || !privateKey) throw new Error('VAPID chưa được khởi tạo');
  return { publicKey, privateKey, subject: subject || 'mailto:admin@mayap.local' };
}

export async function sendPushToUsers(db, userIds, payload) {
  if (!userIds.length) return { sent: 0, removed: 0 };
  const vapid = await getVapid(db);
  webpush.setVapidDetails(vapid.subject, vapid.publicKey, vapid.privateKey);
  const placeholders = userIds.map(() => '?').join(',');
  const { results = [] } = await db.prepare(`SELECT id, endpoint, p256dh, auth FROM push_subscriptions WHERE user_id IN (${placeholders})`)
    .bind(...userIds).all();
  let sent = 0;
  let removed = 0;
  for (const row of results) {
    try {
      await webpush.sendNotification({ endpoint: row.endpoint, keys: { p256dh: row.p256dh, auth: row.auth } }, JSON.stringify(payload), { TTL: 300, urgency: 'high' });
      sent++;
    } catch (error) {
      const status = Number(error?.statusCode || error?.status || 0);
      if (status === 404 || status === 410) {
        await db.prepare('DELETE FROM push_subscriptions WHERE id=?').bind(row.id).run();
        removed++;
      } else {
        console.error('push_failed', status, error?.message || error);
      }
    }
  }
  return { sent, removed };
}
