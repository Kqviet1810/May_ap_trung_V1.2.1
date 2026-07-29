# MAYAP WEB GITHUB v9.0.2 — TEST REPORT

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
