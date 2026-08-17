#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 0x800
#define MALLOC_CAP_8BIT 0x04
inline size_t heap_caps_get_free_size(uint32_t) { return 200000; }
inline bool heap_caps_check_integrity_all(bool) { return true; }
