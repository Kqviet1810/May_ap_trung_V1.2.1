# MAYAP Cloud - triển khai tối thiểu

Nhánh này được thiết kế để `main`/GitHub Pages vẫn chạy như cũ. Cloud features chỉ bật khi `/api/health` hoạt động.

## Việc cần làm một lần trên Cloudflare

1. Tạo **Pages project** từ repo `Kqviet1810/May_ap_trung_V1.2.1`, chọn branch thử nghiệm `agent/mayap-cloud-foundation`.
2. Tạo **D1 database** tên gợi ý `mayap-prod`.
3. Trong Pages → Settings → Bindings, bind D1 với variable name chính xác là `DB`.
4. Trong Pages → Settings → Variables and Secrets, tạo secret `ADMIN_BOOTSTRAP_TOKEN` với chuỗi ngẫu nhiên dài (ít nhất 32 ký tự).
5. Redeploy Pages.
6. Mở `/admin.html`, nhập bootstrap token + tài khoản Admin. Schema D1 và VAPID Web Push được tạo tự động.

Không cần chạy migration thủ công. Không cần Firebase. Không cần VPS.

## Tạo khách + gán máy

Vào `/admin.html` → đăng nhập Admin → nhập:

- email khách;
- tên khách;
- mật khẩu ban đầu (>=10 ký tự) nếu là khách mới;
- Device ID dạng `MAP-A1B2C3D4E5F6`;
- tên máy.

Khi tạo máy mới, server trả `alarmSecret` đúng một lần. Lưu secret này để nạp vào ESP32.

## ESP32 gửi cảnh báo nền

Endpoint:

`POST https://<domain>/api/alarm`

Header:

`Authorization: Bearer <alarmSecret>`

JSON ví dụ:

```json
{
  "deviceId": "MAP-A1B2C3D4E5F6",
  "code": 41,
  "value": 1,
  "temperature": 37.8,
  "humidity": 58.0,
  "message": "Mất tín hiệu cảm biến"
}
```

## HiveMQ

Giữ protocol MQTT hiện tại `mayap/v1/{deviceId}/...`. Khi có thông tin cluster HiveMQ thật, thay `mqttUrl`, `mqttUsername`, `mqttPassword` trong `config.js` (hoặc file cấu hình production riêng).

Serverless Free phù hợp thử nghiệm/triển khai nhỏ; khi thương mại nhiều máy, nâng broker mà không đổi protocol MAYAP.

## Quy tắc version lâu dài

- MQTT protocol giữ `mayap/v1` tương thích ngược.
- Push API giữ `/api/alarm` tương thích ngược.
- Mỗi ESP32 có `deviceId` + `alarmSecret` riêng.
- Không dùng một secret chung cho toàn bộ máy.
- Web, firmware và backend có thể nâng version độc lập nếu giữ hai contract trên.
