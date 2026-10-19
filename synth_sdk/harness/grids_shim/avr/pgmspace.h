#pragma once
#include <stdint.h>
#define PROGMEM
typedef char prog_char; typedef uint8_t prog_uint8_t; typedef uint16_t prog_uint16_t; typedef uint32_t prog_uint32_t;
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
