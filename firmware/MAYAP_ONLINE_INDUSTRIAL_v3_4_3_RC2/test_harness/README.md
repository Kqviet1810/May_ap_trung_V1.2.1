# Harness kiem thu tren may tinh

Dung THU VIEN THAT MAYAP_WebBridge v2.0.1. Chi stub tang PHAN CUNG khong the
chay tren host: radio Wi-Fi, TCP server, DNS, NVS flash, socket MQTT, I2C
(co gia lap AT24C32 4 KB that), RS485, LCD U8g2 (co framebuffer 128x64 that).

## Chuan bi
    cd test_harness
    cp ../MAYAP_ONLINE_INDUSTRIAL_v3_4_3/*.h  .
    cp ../MAYAP_ONLINE_INDUSTRIAL_v3_4_3/*.ino .
    mkdir -p reallib && cp <duong_dan>/MAYAP_WebBridge/src/* reallib/

## Chay
    F="-std=gnu++17 -I. -Istub -Ireallib -Wall -Wextra -Wno-unused-parameter \
       -Wno-missing-field-initializers -Wno-unused-function -Wno-unused-variable"
    L="reallib/MAYAP_WebBridge.cpp"

    g++ $F -o sk sketch_tu.cpp $L globals.cpp && ./sk        # bien dich sketch
    g++ $F -DMAYAP_WEB_ENABLED=0 -o sk0 sketch_tu.cpp $L globals.cpp && ./sk0
    g++ $F -O2 -o skO2 sketch_tu.cpp $L globals.cpp && ./skO2

    g++ -std=gnu++17 -I. -Istub -o tests tests.cpp globals.cpp && ./tests
    g++ -std=gnu++17 -I. -Istub -Ireallib -o libtest libtest.cpp $L globals.cpp && ./libtest
    g++ -std=gnu++17 -I. -Istub -Ireallib -o off offlinetest.cpp $L globals.cpp && ./off
    g++ -std=gnu++17 -I. -Istub -o sc screens.cpp globals.cpp && ./sc

libtest.cpp va offlinetest.cpp nap CHINH file .ino nen test dung doan tich hop
that, khong phai ban sao.

LUU Y: stub CHI de kiem thu tren may tinh. Khong bao gio nap len ESP32.
