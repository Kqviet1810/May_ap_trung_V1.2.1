(() => {
  'use strict';
  const STORAGE = 'mayap.web.v9';
  const $ = (id) => document.getElementById(id);
  const state = { active: false, user: null, reloading: false };

  async function api(url, options = {}) {
    const response = await fetch(url, {
      credentials: 'same-origin',
      headers: { 'content-type': 'application/json', ...(options.headers || {}) },
      ...options
    });
    let data = {};
    try { data = await response.json(); } catch (_) {}
    if (!response.ok) throw Object.assign(new Error(data.error || `HTTP ${response.status}`), { status: response.status, data });
    return data;
  }

  function b64ToBytes(value) {
    const padding = '='.repeat((4 - value.length % 4) % 4);
    const raw = atob((value + padding).replace(/-/g, '+').replace(/_/g, '/'));
    return Uint8Array.from(raw, (c) => c.charCodeAt(0));
  }

  function showLogin() {
    if ($('mayapCloudLogin')) return;
    const root = document.createElement('div');
    root.id = 'mayapCloudLogin';
    root.innerHTML = `<div class="mc-card"><div class="mc-brand">MAYAP</div><h2>Đăng nhập</h2><p>Đăng nhập tài khoản MAYAP để xem đúng thiết bị của bạn và nhận cảnh báo nền.</p><label>Email<input id="mcEmail" type="email" autocomplete="username"></label><label>Mật khẩu<input id="mcPassword" type="password" autocomplete="current-password"></label><button id="mcLoginBtn" type="button">Đăng nhập</button><small id="mcError"></small></div>`;
    const style = document.createElement('style');
    style.textContent = `#mayapCloudLogin{position:fixed;inset:0;z-index:9999;background:#edf6f4;display:grid;place-items:center;padding:20px}.mc-card{width:min(420px,100%);background:#fff;border:1px solid #d8e7e3;border-radius:20px;padding:24px;box-shadow:0 20px 60px #1234}.mc-brand{font-weight:900;color:#07594f;letter-spacing:.12em}.mc-card p{color:#617671}.mc-card label{display:grid;gap:6px;margin:12px 0;font-weight:700}.mc-card input{font:inherit;padding:12px;border:1px solid #b9ccc7;border-radius:10px}.mc-card button{width:100%;padding:13px;border:0;border-radius:10px;background:#07594f;color:white;font-weight:800}.mc-card small{display:block;color:#a33;margin-top:10px;min-height:1.2em}`;
    document.head.append(style);
    document.body.append(root);
    $('mcLoginBtn').addEventListener('click', login);
    $('mcPassword').addEventListener('keydown', (e) => { if (e.key === 'Enter') login(); });
  }

  async function login() {
    const error = $('mcError');
    error.textContent = '';
    try {
      await api('./api/auth/login', { method: 'POST', body: JSON.stringify({ email: $('mcEmail').value, password: $('mcPassword').value }) });
      location.reload();
    } catch (e) {
      error.textContent = e.message === 'invalid_credentials' ? 'Email hoặc mật khẩu không đúng.' : e.message;
    }
  }

  async function syncOwnedDevices() {
    const data = await api('./api/devices');
    const devices = (data.devices || []).filter((d) => !d.disabled).map((d) => ({ id: d.device_id, name: d.name || 'Máy ấp' }));
    const key = `${STORAGE}.devices`;
    const old = localStorage.getItem(key) || '[]';
    const next = JSON.stringify(devices);
    if (old !== next) {
      localStorage.setItem(key, next);
      if (devices.length && !devices.some((d) => d.id === localStorage.getItem(`${STORAGE}.selected`))) localStorage.setItem(`${STORAGE}.selected`, devices[0].id);
      if (!state.reloading) { state.reloading = true; location.reload(); }
    }
  }

  async function enablePush() {
    if (!state.active || !state.user || !('serviceWorker' in navigator) || !('PushManager' in window)) return false;
    if (Notification.permission !== 'granted') return false;
    const registration = await navigator.serviceWorker.ready;
    const key = await api('./api/push/public-key');
    let subscription = await registration.pushManager.getSubscription();
    if (!subscription) subscription = await registration.pushManager.subscribe({ userVisibleOnly: true, applicationServerKey: b64ToBytes(key.publicKey) });
    await api('./api/push/subscribe', { method: 'POST', body: JSON.stringify(subscription.toJSON()) });
    return true;
  }

  async function disablePush() {
    if (!('serviceWorker' in navigator)) return;
    const registration = await navigator.serviceWorker.ready;
    const subscription = await registration.pushManager.getSubscription();
    if (!subscription) return;
    try { await api('./api/push/unsubscribe', { method: 'POST', body: JSON.stringify({ endpoint: subscription.endpoint }) }); } catch (_) {}
    try { await subscription.unsubscribe(); } catch (_) {}
  }

  function hookNotificationToggle() {
    const toggle = $('notificationsEnabled');
    if (!toggle) return;
    toggle.addEventListener('change', () => setTimeout(async () => {
      try {
        if (toggle.checked && Notification.permission === 'granted') await enablePush();
        else if (!toggle.checked) await disablePush();
      } catch (error) {
        console.warn('MAYAP Push:', error);
      }
    }, 100));
  }

  async function init() {
    try {
      const health = await api('./api/health');
      state.active = Boolean(health.ok);
    } catch (_) {
      return; // GitHub Pages / môi trường chưa cấu hình Cloudflare: giữ nguyên web cũ.
    }
    if (!state.active) return;
    if ((await api('./api/health')).setupRequired) {
      location.href = './admin.html';
      return;
    }
    try {
      const me = await api('./api/auth/me');
      state.user = me.user;
    } catch (error) {
      if (error.status === 401) return showLogin();
      return;
    }
    await syncOwnedDevices();
    hookNotificationToggle();
    if (localStorage.getItem(`${STORAGE}.notifications`) === '1' && Notification.permission === 'granted') {
      enablePush().catch((e) => console.warn('MAYAP Push:', e));
    }
  }

  window.MAYAP_CLOUD = { enablePush, disablePush };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init, { once: true });
  else init();
})();
