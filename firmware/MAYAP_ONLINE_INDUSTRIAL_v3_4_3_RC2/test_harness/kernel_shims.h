#pragma once
// Mirrors the kernel-level functions the .ino defines before including hmi.h.
#include "config.h"
inline bool mayapI2cLock(uint32_t) { return true; }
inline void mayapI2cUnlock() {}
inline void mayapI2cRecordResult(bool) {}
inline bool mayapI2cRecoveryDue(uint32_t) { return false; }
inline void mayapI2cRecoveryFinished(uint32_t, bool) {}
inline uint16_t mayapI2cRecoveryCount() { return 0; }
inline uint16_t mayapI2cConsecutiveErrors() { return 0; }
inline bool mayapSystemTripActive() { return false; }
inline uint16_t mayapSystemTripReason() { return 0; }
inline void mayapRequestSystemTrip(uint16_t) {}
inline KernelHealthSnapshot mayapKernelHealthSnapshot() { return KernelHealthSnapshot{}; }
