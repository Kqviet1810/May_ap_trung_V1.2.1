# MAYAP WEB GITHUB v9.0.2 — TEST REPORT

---

# MAYAP WEB GITHUB v9.0.3 — ĐỐI CHIẾU FIRMWARE RC2 (ESP32 ↔ WebBridge ↔ Web)

Audit đồng bộ giao thức giữa `firmware/MAYAP_ONLINE_INDUSTRIAL_v3_4_3_RC2` (firmware
`3.4.3-RC2` + thư viện `MAYAP_WebBridge v2.0.1`, đã có sẵn 73 kiểm chứng tích hợp
trong `test_harness/libtest.cpp` dùng đúng thư viện thật) với `app.js`/`config.js`.
Đối chiếu trực tiếp trên mã nguồn thật, không suy đoán: 8 topic, QoS/retain từng
kênh, 28 trường cấu hình, bảng lệnh, luồng ACK/applied, các enum
(`TurnState`, `AutoTuneState`, `TurnDirection`, `NetState`, `PortalState`,
`FaultCode`), presence/LWT Online-Offline, và cơ chế reconnect/resubscribe.

## Lỗi nghiêm trọng đã sửa

**`requestId` của web dài 40 ký tự, vượt giới hạn firmware (39 ký tự khả dụng)
→ MỌI lệnh lưu cấu hình và MỌI lệnh điều khiển từ web thật bị RC2 từ chối
`"invalid"`.**

- `WEB_REQUEST_ID_CAPACITY = 40` (`config.h`, tính cả byte `'\0'`) nên
  `mayap_web_adapter.h::readString()` chỉ giữ được tối đa 39 ký tự.
- `app.js` (bản trước sửa) tạo `requestId` bằng
  `` `${prefix}-${crypto.randomUUID()}` `` — tiền tố `cfg-`/`cmd-` (4 ký tự) +
  UUID chuẩn có gạch ngang (36 ký tự) = **40 ký tự đúng ranh giới lỗi**.
  `readString()` trả về `false` ngay khi chạm giới hạn, khiến `handleConfig()`
  và `handleCommand()` coi như thiếu `requestId` và trả `ack.result = "invalid"`
  cho toàn bộ gói — nghĩa là web thật không lưu được cấu hình và không điều
  khiển được máy thật chạy firmware RC2, dù mọi thứ khác trên giao thức đều
  đúng.
- Đã tái hiện lỗi bằng cách biên dịch trực tiếp logic `readString()` gốc của
  firmware và đưa đúng chuỗi `requestId` mà `app.js` cũ sinh ra vào — xác nhận
  `readString()` trả `false`.
- **Sửa `app.js`**: `requestId()` bỏ dấu gạch ngang của UUID (còn 32 ký tự hex)
  và giới hạn cứng ở 36 ký tự tổng cộng (`REQUEST_ID_MAX = 36`, dưới ngưỡng 39
  của firmware một khoảng an toàn).
- **Thêm hồi quy vĩnh viễn**: mục `9. REQUESTID DAI NHU WEBSITE THAT (UUID)`
  trong `test_harness/libtest.cpp`, gửi đúng khuôn dạng `requestId` mà web thật
  phát sinh (36 ký tự) qua cả `config/set` và `command`, xác nhận ACK
  `"applied"`/không phải `"invalid"`. Đã kiểm chứng ngược: phục hồi tạm định
  dạng UUID có gạch ngang (40 ký tự) làm 5 test trong mục này FAIL đúng như dự
  đoán, sau đó khôi phục bản sửa và chạy lại PASS.

## Gap đã sửa (không phá giao thức nhưng làm web hiểu sai/thiếu văn bản)

- `app.js::eventText()` chưa có văn bản cho các mã sự kiện có thật mà firmware
  RC2 phát ra: `90` (`NetStateChanged`), `91` (`WifiPortalStarted`), `92`
  (`WifiPortalFailed`) — trước đây rơi vào nhánh mặc định "Sự kiện máy #90".
  Đã thêm văn bản tiếng Việt, với mã 90 giải mã đúng `NetState` (0–4) mang
  trong trường `value` của sự kiện.
- Mã lỗi hệ thống (`code >= 1000`, tức `code - 1000 = FaultCode` trong
  `config.h`) trước đây chỉ hiện `"Cảnh báo hệ thống #101"` — không phân biệt
  được lỗi vừa phát sinh (`EventType::FaultRaised`), vừa hết
  (`FaultCleared`), hay vừa được xác nhận (`FaultAck`), và không có tên lỗi.
  Đã thêm bảng `FAULT_TITLES` đối chiếu đủ 26/26 mã trong `FaultCode`
  (`config.h`), dùng trường `type` của sự kiện để phân biệt 3 trạng thái trên.

## Version bump

- `sw.js`: `CACHE = 'mayap-web-v9.0.2'` → `'mayap-web-v9.0.3'`. Bắt buộc phải
  đổi vì service worker cache theo kiểu "cache trước, không hết hạn" — nếu
  không đổi tên cache, các máy đã cài PWA từ bản có lỗi `requestId` sẽ tiếp
  tục phục vụ `app.js` cũ vĩnh viễn, không bao giờ nhận được bản vá.
- `index.html`: tiêu đề trang `v9.0.2` → `v9.0.3` cho khớp.

## Kiểm tra tự động (v9.0.3)

- `node --check` trên `app.js`, `config.js`, `config.production.example.js`,
  `sw.js`: PASS.
- Đối chiếu tự động toàn bộ `$('id')` trong `app.js` với `id="..."` trong
  `index.html`: 69/69, thiếu 0.
- Mô phỏng 5000 lần gọi `requestId('cfg')`/`requestId('cmd')` bằng đúng hàm
  trong `app.js`: độ dài tối đa luôn là 36 ký tự (dưới ngưỡng lỗi 40 của
  firmware).
- Bộ kiểm thử firmware/host (biên dịch bằng g++, dùng đúng thư viện thật
  `MAYAP_WebBridge v2.0.1` cho các mục cần MQTT that):
  - `tests.cpp` (đơn vị: sanitize, bảng lỗi, EEPROM schema, đổi SV...):
    42177/42177 PASS.
  - `libtest.cpp` (tích hợp qua thư viện `MAYAP_WebBridge` thật — topic,
    QoS/retain, config/set, ACK, reconnect, mất Wi-Fi, tương thích phiên bản,
    echo cấu hình, snapshot, mapping enum, captive portal): 73/73 PASS
    (gồm 5 kiểm tra hồi quy `requestId` mới thêm).
  - `offlinetest.cpp` (chế độ OFFLINE với thư viện thật): 11/11 PASS.
  - `sketch_tu.cpp` biên dịch + chạy 3 cấu hình (`MAYAP_WEB_ENABLED=1`,
    `=0`, và `-O2`): PASS cả 3.
  - `screens.cpp` (khung LCD HMI, không liên quan web nhưng dùng chung
    `config.h`): PASS.

## Đối chiếu không phát hiện lỗi (đã kiểm tra kỹ, giữ nguyên)

- 8 topic (`config/set`, `config/reported`, `command`, `ack`, `snapshot`,
  `log`, `presence`, `session`) dưới `mayap/v1/{deviceId}/...`: khớp tuyệt đối
  hai phía.
- QoS/retain từng kênh: `config/set` QoS1 retain=true (web) khớp yêu cầu
  firmware; `config/reported` QoS1 retain=1 (mặc định thư viện); `command`
  QoS1 retain=false — và firmware chủ động từ chối `command` retained (chống
  phát lại lệnh cũ khi reconnect); `snapshot` QoS0; `session` QoS0 không
  retain, firmware bỏ qua nếu retained.
- 28 trường cấu hình: tên và thứ tự trong `CONFIG_KEYS` (`app.js`) khớp
  100% với `MAYAP_WEB_CONFIG_FLOAT/U16/U8/BOOL/ENUM_FIELDS`
  (`mayap_web_schema.h`) — 13 float + 6 u16 + 2 u8 + 5 bool + 2 enum.
  `cloudEnabled` (chỉ đọc) được firmware tự ghi đè bất kể web gửi gì, đúng
  thiết kế "web không tắt được ONLINE từ xa".
  `controlMode` chỉ có 1 giá trị hợp lệ khi vận hành máy (Pid) — firmware
  luôn ép giá trị này bất kể web gửi, web echo lại đúng như vậy.
- Bảng lệnh (`batch_start/stop`, `turn_left/right/stop`, `alarm_ack`,
  `autotune_start`, `resume_yes/no`): action string, `bootId`, `expiresAt`
  (cho lệnh `requireFresh`), `sequence` tăng dần đều khớp.
- ACK/applied: đã lần theo đúng thứ tự gói thực tế qua `WebAdapter::loop()`
  ("mỗi loop chỉ publish một gói") — `config/reported` (revision mới) thường
  đến TRƯỚC ack `"applied"`; `app.js` khớp `pending` bằng `config/reported`
  (không phụ thuộc ack), nên đúng bất kể thứ tự gói.
- Enum: `TurnState` {Stopped,Left,Right,Waiting,Fault}, `AutoTuneState`
  {Idle,Running,Success,Failed}, `TurnDirection` {Left,Right} — khớp giữa
  `config.h` và `app.js`.
- Presence/LWT: topic `presence`, QoS1 retain1, LWT publish
  `{"online":false,...}` khi mất kết nối bất thường; `connectionStatus()`
  trong `app.js` dùng đúng `presence.online` + ngưỡng "cũ" `staleAfterMs`
  (90s) tương thích với keepalive 60s (broker phát hiện mất kết nối trong
  khoảng 1.5×keepalive ≈ 90s).
- Reconnect: `resubscribe:false` phía `mqtt.js` nhưng `app.js` tự resubscribe
  toàn bộ thiết bị + gửi `session{sync:true}` trong handler `'connect'` (chạy
  lại ở MỌI lần reconnect, không chỉ lần đầu); phía firmware, `onConnection()`
  cũng tự đặt lại cờ để phát lại `config/reported` + `snapshot` bất kể web có
  gửi sync hay không — hai lớp an toàn độc lập, đã có test `libtest.cpp` mục 7
  xác nhận.

## Chưa kiểm tra trong môi trường này (mô phỏng, KHÔNG phải end-to-end thật)

- Kết nối thật GitHub Pages → broker WSS `broker.emqx.io:8084` → ESP32-S3 nạp
  firmware RC2 qua `mqtts://broker.emqx.io:8883`. Toàn bộ kiểm chứng ở trên
  chạy trên host (g++) với thư viện `MAYAP_WebBridge` thật nhưng phần cứng
  Wi-Fi/TCP/MQTT socket là stub — KHÔNG phải phần cứng ESP32 thật.
  `libtest.cpp` mô phỏng sự kiện MQTT qua hàm `mqttFire()` gọi thẳng handler
  của thư viện, không đi qua mạng thật hay broker thật.
- Trình duyệt thật (Chrome/Safari/PWA đã cài) kết nối `mqtt.js` qua WSS tới
  broker thật rồi round-trip với firmware thật trên board ESP32-S3.
- Timing thực tế của `expiresAt` lệnh `requireFresh` (web tính `+8s`, firmware
  cho phép tối đa `+30s`) dưới độ trễ mạng thật và lệch giờ NTP thực tế của
  ESP32.
- Safari iOS, chế độ PWA cài trên điện thoại thật, cỡ chữ hệ thống > 100%
  (kế thừa từ v9.0.2, chưa kiểm tra lại).

## Phạm vi thay đổi

- Giữ nguyên MQTT protocol, 8 topic, schema 28 trường và luồng ACK + `config/reported` của v9.0.0.
- Khôi phục mật độ bố cục của v8.3.
- Loại bỏ toàn bộ thanh trạng thái cấu hình chiếm chiều cao.
- Trạng thái lưu vẫn hoạt động nội bộ; nút Lưu hiển thị `Đang lưu…` và kết quả được báo bằng toast.
- Trang Mẻ ấp dùng toàn bộ phần chiều cao còn lại cho nhật ký, không để khoảng trống giữa nội dung và thanh điều hướng.
- Sửa checkbox ẩn kế thừa `width:100%`, nguyên nhân tiềm ẩn gây tràn ngang.

## Kiểm tra tự động

- JavaScript `node --check`: PASS
- HTML ID được JavaScript tham chiếu: 70/70, thiếu 0
- Thanh trạng thái cấu hình nhìn thấy: 0
- Service Worker cache: `mayap-web-v9.0.2`
- Manifest JSON: PASS
- ZIP integrity: PASS

## Kiểm tra Chromium — trang Mẻ ấp

| Viewport | Cuộn ngang | Cuộn dọc trang | Khoảng cách log → thanh menu | Log hiển thị |
|---:|:---:|:---:|---:|---:|
| 320×700 | Không | Không | 10 px | 4 dòng gần nhất |
| 360×800 | Không | Không | 10 px | 6 dòng |
| 390×844 | Không | Không | 10 px | 6 dòng |
| 430×932 | Không | Không | 10 px | 6 dòng |
| 768×1024 | Không | Không | 10 px | 6 dòng |

Ở màn hình rất thấp, giao diện tự ẩn các dòng log cũ hơn thay vì làm trang phát sinh cuộn.

## Chưa kiểm tra trong môi trường này

- Kết nối thật GitHub Pages → broker WSS → ESP32-S3.
- Safari iOS và chế độ PWA đã cài trên điện thoại thật.
- Cỡ chữ hệ thống lớn hơn 100%.


## Kiểm tra nút mẻ hợp nhất

- Chỉ còn một nút `batchAction`: PASS
- Không còn ID `startBatch`/`endBatch`: PASS
- `batchRunning=false` → Bắt đầu: PASS
- `batchRunning=true` → Kết thúc: PASS
- Trạng thái chờ ngăn bấm lặp: PASS
- Lệnh MQTT giữ nguyên `batch_start`/`batch_stop`: PASS


## Kiểm thử Chromium thực tế

| Viewport | Nút khi mẻ chạy | Cuộn ngang | Cuộn dọc |
|---:|---|:---:|:---:|
| 320 × 700 | Kết thúc | Không | Không |
| 360 × 800 | Kết thúc | Không | Không |
| 390 × 844 | Kết thúc | Không | Không |
| 430 × 932 | Kết thúc | Không | Không |
| 768 × 1024 | Kết thúc | Không | Không |

Kiểm thử luồng MQTT giả lập:

- `batchRunning=false` → nút **Bắt đầu**: PASS
- `batchRunning=true` → nút **Kết thúc**: PASS
- Bấm **Kết thúc** → hộp xác nhận: PASS
- Xác nhận → publish `action=batch_stop`, QoS 1, retain false: PASS
- Trong lúc chờ → nút **Đang kết thúc…** và bị khóa: PASS
- JavaScript page errors: 0
