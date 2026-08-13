import webpush from 'web-push';
import { ensureSchema, setSetting } from '../../_lib/db.js';
import { makePassword } from '../../_lib/auth.js';
import { json, readJson, normalizeEmail } from '../../_lib/http.js';

export async function onRequestPost({ request, env }) {
  try {
    await ensureSchema(env.DB);
    const count = await env.DB.prepare('SELECT COUNT(*) AS count FROM users').first();
    if (Number(count?.count || 0) > 0) return json({ ok: false, error: 'already_bootstrapped' }, 409);
    const body = await readJson(request);
    if (!body) return json({ ok: false, error: 'invalid_json' }, 400);
    if (!env.ADMIN_BOOTSTRAP_TOKEN || String(body.bootstrapToken || '') !== String(env.ADMIN_BOOTSTRAP_TOKEN)) {
      return json({ ok: false, error: 'invalid_bootstrap_token' }, 403);
    }
    const email = normalizeEmail(body.email);
    const name = String(body.name || '').trim() || 'MAYAP Admin';
    const password = String(body.password || '');
    if (!email.includes('@') || password.length < 10) return json({ ok: false, error: 'email_or_password_invalid' }, 400);
    const credential = await makePassword(password);
    const now = Date.now();
    await env.DB.prepare(`INSERT INTO users(email,name,role,password_salt,password_hash,created_at) VALUES(?,?,?,?,?,?)`)
      .bind(email, name, 'admin', credential.salt, credential.hash, now).run();
    const vapid = webpush.generateVAPIDKeys();
    await Promise.all([
      setSetting(env.DB, 'vapid_public_key', vapid.publicKey),
      setSetting(env.DB, 'vapid_private_key', vapid.privateKey),
      setSetting(env.DB, 'vapid_subject', `mailto:${email}`)
    ]);
    return json({ ok: true, admin: email, vapidPublicKey: vapid.publicKey });
  } catch (error) {
    return json({ ok: false, error: error?.message || 'bootstrap_failed' }, 500);
  }
}
