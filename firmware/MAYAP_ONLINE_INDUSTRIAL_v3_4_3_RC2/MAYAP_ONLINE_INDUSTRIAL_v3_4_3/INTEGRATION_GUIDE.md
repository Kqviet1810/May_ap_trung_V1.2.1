# Tích hợp WebBridge vào MAYAP v3.3.1

Bản này được tạo trực tiếp từ `MAYAP_OFFLINE_INDUSTRIAL_v3_3_1` và chỉ bổ sung lớp web/MQTT.
`hmi.h` được giữ nguyên 100% để bảo toàn bản sửa chống nháy/vỡ màn hình của v3.3.1.

## Cài đặt

1. Cài `MAYAP_WebBridge_v2.0.0_LIBRARY.zip` bằng Arduino IDE.
2. Mở `MAYAP_ONLINE_INDUSTRIAL_v3_4_2_TEST.ino`.
3. Trong `mayap_web_credentials.h`:
   - Test tạm: `MAYAP_USE_PUBLIC_TEST_BROKER 1` (đã bật sẵn trong bản TEST này).
   - Thương mại: để `0`, điền broker riêng, tài khoản và CA.
4. Nạp firmware. Nếu chưa có Wi-Fi, ESP32 mở AP `MAYAP-XXXX`.
5. Kết nối AP, mở `192.168.4.1`, chọn Wi-Fi và lưu.
6. Thêm Device ID `MAP-XXXXXXXXXXXX` vào website.

## Nơi đã nối

- `.ino`: chỉ khởi động bridge sau khi Control/HMI/Supervisor task đã chạy.
- `loop()`: gọi `WebBridge.loop()` và `WebAdapter.loop()` ở loopTask ưu tiên thấp.
- `config.h`: thêm hợp đồng mailbox web và trường `autoResumeAfterPower`.
- `machine_control.h`: thêm mailbox cố định, snapshot, log, ACK bất đồng bộ và dùng chung xử lý command.
- `mayap_web_adapter.h`: ánh xạ JSON ↔ dữ liệu máy.
- `mayap_web_schema.h`: danh sách trường và bảng action duy nhất.

## Thêm nút web mới

Không sửa thư viện `MAYAP_WebBridge`.

1. Thêm giá trị vào `HmiCommandType` trong `config.h`.
2. Thêm một `case` trong `executeMachineCommand()` của `machine_control.h`.
3. Thêm một dòng vào `MAYAP_WEB_COMMAND_TABLE()` trong `mayap_web_schema.h`.
4. Website gửi action mới.

## Tắt toàn bộ web để kiểm tra offline

Trong `mayap_web_credentials.h`:

```cpp
#define MAYAP_WEB_ENABLED 0
```

Khi đó project dùng lại loop 1 giây và không cần khởi tạo Wi-Fi/MQTT.


## Cấu hình TEST đã bật sẵn

```cpp
#define MAYAP_USE_PUBLIC_TEST_BROKER 1
#define MAYAP_WEB_ENABLE_SERIAL_LOG 1
```

ESP32 dùng `mqtts://broker.emqx.io:8883`. Website phải dùng cùng broker qua WSS `wss://broker.emqx.io:8084/mqtt` và cùng topic root `mayap/v1`.

Sau khi thử xong, không dùng broker công cộng cho máy thương mại.
