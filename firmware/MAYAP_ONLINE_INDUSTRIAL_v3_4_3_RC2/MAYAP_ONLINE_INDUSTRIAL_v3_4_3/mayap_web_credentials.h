#pragma once

// 1 = bat Wi-Fi/MQTT/captive portal; 0 = bien dich nhu ban OFFLINE.
#ifndef MAYAP_WEB_ENABLED
#define MAYAP_WEB_ENABLED 1
#endif

// ============================================================================
// MAYAP WEB CREDENTIALS
// 0 = van hanh thuong mai/offline: khai bao broker rieng ben duoi.
// 1 = KIEM TRA THUC TE TAM THOI voi broker cong cong EMQX; chi dung giai doan thu nghiem.
// ============================================================================
#ifndef MAYAP_USE_PUBLIC_TEST_BROKER
#define MAYAP_USE_PUBLIC_TEST_BROKER 1
#endif

#if MAYAP_USE_PUBLIC_TEST_BROKER
#include "mayap_test_root_ca.h"
#define MAYAP_MQTT_BROKER_URI "mqtts://broker.emqx.io:8883"
#define MAYAP_MQTT_USERNAME ""
#define MAYAP_MQTT_PASSWORD ""
#define MAYAP_MQTT_ROOT_CA MAYAP_PUBLIC_TEST_ROOT_CA
#else
// Ban thuong mai: dien broker rieng + tai khoan co ACL theo deviceId.
// De trong van cho phep may chay OFFLINE va mo captive portal Wi-Fi.
#ifndef MAYAP_MQTT_BROKER_URI
#define MAYAP_MQTT_BROKER_URI ""
#endif
#ifndef MAYAP_MQTT_USERNAME
#define MAYAP_MQTT_USERNAME ""
#endif
#ifndef MAYAP_MQTT_PASSWORD
#define MAYAP_MQTT_PASSWORD ""
#endif
#ifndef MAYAP_MQTT_ROOT_CA
#define MAYAP_MQTT_ROOT_CA nullptr
#endif
#endif

#ifndef MAYAP_WEB_ENABLE_SERIAL_LOG
#define MAYAP_WEB_ENABLE_SERIAL_LOG 1
#endif
