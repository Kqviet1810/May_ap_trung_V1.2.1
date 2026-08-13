export function json(data, status = 200, headers = {}) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store', ...headers }
  });
}

export async function readJson(request) {
  try { return await request.json(); } catch { return null; }
}

export function methodNotAllowed() {
  return json({ ok: false, error: 'method_not_allowed' }, 405, { allow: 'GET, POST, DELETE' });
}

export function normalizeEmail(value) {
  return String(value || '').trim().toLowerCase();
}

export function normalizeDeviceId(value) {
  return String(value || '').trim().toUpperCase().replace(/\s+/g, '');
}
