#pragma once
#include <stdint.h>
static inline uint8_t eeprom_read_byte(const uint8_t *) { return 0; }
static inline void eeprom_write_byte(uint8_t *, uint8_t) {}
