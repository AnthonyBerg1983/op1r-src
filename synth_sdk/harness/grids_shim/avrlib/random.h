#pragma once
#include <stdint.h>
namespace avrlib {
extern uint8_t (*ref_random_byte)(void);   /* the test feeds the bytes */
class Random { public: static void Update() {} static uint8_t state() { return 0; } static uint8_t GetByte() { return ref_random_byte(); } };
}
