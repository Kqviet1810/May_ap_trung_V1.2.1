# MAYAP WEB GITHUB v9.0.2

Website tĩnh đã nối với **MAYAP WebBridge v2.0.0** và firmware
**MAYAP ONLINE INDUSTRIAL v3.4.0**.

## 1. Đưa lên GitHub Pages

1. Tạo repository mới trên GitHub.
2. Upload toàn bộ nội dung trong thư mục này vào **gốc repository**.
3. Mở **Settings → Pages**.
4. Chọn **Deploy from a branch**.
5. Chọn nhánh `main`, thư mục `/ (root)`, sau đó Save.
6. Chờ GitHub cấp địa chỉ `https://TEN-TAI-KHOAN.github.io/TEN-REPOSITORY/`.

Không đổi cấu trúc thư mục và không chỉ upload riêng `index.html`.

## 2. Test với ESP32 hiện tại

Trong `mayap_web_credentials.h` của firmware đặt tạm:

```cpp
#define MAYAP_USE_PUBLIC_TEST_BROKER 1
```

Website mặc định dùng:

```text
ESP32 TLS:  mqtts://broker.emqx.io:8883
Web WSS:    wss://broker.emqx.io:8084/mqtt
Topic:      mayap/v1/{deviceId}/...
```

Sau khi nạp ESP32:

1. Cấu hình Wi-Fi lần đầu bằng captive portal `MAYAP-XXXX`.
2. Mở website GitHub Pages.
3. Nhấn dấu `+`.
4. Nhập Device ID đúng dạng `MAP-XXXXXXXXXXXX`.
5. Chờ trạng thái chuyển sang `ONLINE`.

## 3. Những phần đã nối thật

- Presence online/offline retained.
- Snapshot runtime/status.
- Config reported đủ 28 trường.
- Lưu cấu hình bằng `config/set`, chờ ACK `applied` và đối chiếu `config/reported`.
- Lệnh bắt đầu/kết thúc mẻ.
- Auto Tune PID.
- Xác nhận tiếp tục hoặc hủy mẻ sau mất điện.
- Nhật ký từ topic `log`.
- Realtime session 2 giây khi đang mở máy; trở lại 30 giây khi đóng web.
- Quản lý nhiều Device ID bằng một MQTT client.

## 4. Ba trường chỉ lưu trên trình duyệt

Firmware v3.4.0 chưa có các trường sau trong `MachineConfig`:

- Tên mẻ ấp.
- Ngày bắt đầu do người dùng nhập.
- Độ ẩm tham khảo.

Website giữ ba giá trị này trong `localStorage`. Nhiệt độ, tổng ngày ấp và chính
sách khôi phục sau mất điện vẫn được gửi xuống ESP32.

## 5. Đổi Wi-Fi

Mật khẩu Wi-Fi không gửi qua MQTT. Quy trình thực tế:

1. Giữ nút BOOT trên máy 3 giây.
2. Kết nối điện thoại vào `MAYAP-XXXX`.
3. Mở `http://192.168.4.1/`.
4. Chọn hoặc nhập Wi-Fi mới và mật khẩu.

Nút trong website chỉ mở trang portal sau khi người dùng đã kết nối vào AP của máy.

## 6. Chuyển sang broker thương mại

Sao chép `config.production.example.js` thành `config.js`, rồi thay `mqttUrl`.
Firmware và website phải dùng cùng broker, topic root và chính sách xác thực.

**Không đặt mật khẩu broker dùng chung trong repository GitHub công khai.** GitHub
Pages phục vụ JavaScript tĩnh nên người dùng có thể xem nội dung `config.js`.
Bản thương mại nên dùng broker riêng, ACL theo thiết bị và token ngắn hạn do backend cấp.

## 7. Cấu trúc

```text
index.html
styles.css
app.js
config.js
manifest.webmanifest
sw.js
.nojekyll
icons/
```

Website không cần Node.js, npm, Vite hoặc bước build.


## Thay đổi v9.0.2
- Khôi phục bố cục gọn của v8.3.
- Bỏ các thanh trạng thái cấu hình chiếm chỗ; trạng thái vẫn được giữ trong logic và phản hồi bằng nút/toast.
- Trang Mẻ ấp tự lấp phần chiều cao còn lại bằng nhật ký, không tạo khoảng trắng lớn trên điện thoại.
- Không đổi giao thức MQTT, topic, schema hay quy trình ACK + config/reported.


## Thay đổi v9.0.2

- Gộp hai nút **Bắt đầu** và **Kết thúc** thành một nút duy nhất.
- Nút hiển thị **Bắt đầu** khi mẻ chưa chạy và tự chuyển thành **Kết thúc** sau khi snapshot ESP32 xác nhận `batchRunning=true`.
- Khi đang gửi lệnh, nút bị khóa và hiển thị trạng thái đang xử lý để tránh bấm lặp.
- Giữ nguyên giao thức MQTT và toàn bộ bố cục v9.0.1.
