#pragma once
#include <stdint.h>
namespace avrlib {
static inline uint8_t U8Mix(uint8_t a, uint8_t b, uint8_t balance) { uint16_t s = a * (255 - balance); s += b * balance; return s >> 8; }
static inline uint8_t U8U8MulShift8(uint8_t a, uint8_t b) { return (uint16_t)a * b >> 8; }
static inline uint16_t U8U8Mul(uint8_t a, uint8_t b) { return (uint16_t)a * b; }
}
