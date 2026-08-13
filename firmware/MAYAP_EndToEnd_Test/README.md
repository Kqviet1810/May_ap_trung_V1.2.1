# MAYAP End-to-End Test

Firmware này giả lập một máy ấp MAYAP để kiểm tra toàn tuyến:

- `ESP32 -> HiveMQ -> PWA MAYAP`
- `ESP32 -> Cloudflare /api/alarm -> Web Push`

## Arduino IDE

Cài:

- board package `esp32 by Espressif Systems`;
- `PubSubClient by Nick O'Leary`;
- `ArduinoJson by Benoit Blanchon`.

Mở `MAYAP_EndToEnd_Test.ino`, điền Wi-Fi, HiveMQ hostname/credential, Cloudflare alarm URL và alarmSecret.

Nếu `DEVICE_ID_OVERRIDE` để trống, firmware tự sinh `MAP-XXXXXXXXXXXX` từ MAC và in Device ID ở Serial Monitor 115200.

## Test

- `S`: ép đồng bộ presence/config/snapshot.
- `L`: tạo log MQTT code 41.
- `A`: gọi Cloudflare `/api/alarm` để kiểm tra Web Push khi PWA đóng.
- `B`: bật/tắt mẻ giả lập.
- Nút BOOT: gửi cả log MQTT và Cloud Web Push.

`MAYAP_INSECURE_TLS_TEST=1` chỉ dành cho bring-up. Production phải chuyển về `0` và cài CA certificate phù hợp.