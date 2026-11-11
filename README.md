## 👤 Tác giả
Họ và Tên: **Dương Cảnh Thái** 
Mã sinh viên: 231432342
Lớp KTDTTHCN 2 - K64

🔗 **Wokwi project:** https://wokwi.com/projects/480222431350092801


#  Hệ thống giám sát môi trường bể nuôi thủy, hải sản (ESP32 + FreeRTOS + MQTT)

Dự án mô phỏng trên **Wokwi**: ESP32 đo **mực nước** (cảm biến siêu âm HC-SR04) và **nhiệt độ bể** (DS18B20), hiển thị qua **Serial**, gửi lên **MQTT Broker** qua WiFi và hiển thị/điều khiển trên **trang Web**. Chương trình chạy trên **FreeRTOS** với 3 task phân bổ trên 2 lõi, tự động điều khiển **máy bơm nước vào** (PWM 50%/100%) và **van thoát nước ra** theo mực nước.


---

##  Tính năng

- Đo mực nước không tiếp xúc bằng HC-SR04, đo nhiệt độ bể bằng DS18B20 (1-Wire).
- Hiển thị dữ liệu qua Serial Monitor (115200 baud), chu kỳ 2 giây.
- Gửi dữ liệu JSON lên MQTT Broker **HiveMQ** (`broker.hivemq.com`).
- Web hiển thị **mực nước (m)** và **nhiệt độ (°C)** dạng số, kèm các nút **điều khiển thủ công** bơm/van.
- Kiến trúc **RTOS**: 3 task, 2 hàng đợi (queue), ghim lõi CPU.
- Điều khiển tự động theo mực nước, công suất bơm bằng **PWM**.

##  Phần cứng (mô phỏng Wokwi)

| Linh kiện | Chân ESP32 | Ghi chú |
|---|---|---|
| HC-SR04 – TRIG | GPIO13 | Ngõ ra, xung 10 µs |
| HC-SR04 – ECHO | GPIO12 | Ngõ vào |
| DS18B20 – DQ | GPIO19 | 1-Wire, kéo lên 3V3 qua 4,7 kΩ |
| LED xanh dương (Bơm) | GPIO26 | PWM 5 kHz, 8 bit |
| LED đỏ (Van thoát) | GPIO25 | Mức cao = mở van |

> Trong mô phỏng, máy bơm và van được thay bằng LED. Độ sáng LED xanh thể hiện công suất bơm.


##  Thuật toán điều khiển

Mực nước được tính: `water_level = 2,5 − khoảng_cách` (bể cao 2,5 m).

| Mực nước | Hành động | Thời gian |
|---|---|---|
| `< 0,5 m` | Mở van thoát | 10 s |
| `0,5 – 1 m` | Không tác động | – |
| `1 – 1,5 m` | Bơm 50% (PWM 127) | 10 s |
| `1,5 – 2 m` | Bơm 100% (PWM 255) | 20 s |
| `> 2 m` | Không tác động | – |
