/* scale mode core (docs/55), shared with the host test */
#ifndef SC_CORE_H
#define SC_CORE_H
#include <stdint.h>
enum { SC_OFF = 0, SC_NTYPES = 13 };
enum { SC_SNAP, SC_WHITE, SC_MUTE, SC_NMODES };
extern uint8_t sc_root, sc_type, sc_mode;
extern const char *const sc_type_names[SC_NTYPES], *const sc_mode_names[SC_NMODES], *const sc_root_names[12];
uint16_t sc_mask(void);
int sc_map(int note);
int sc_unmap(int note);
int sc_down(int note, int mapped_ok);
int sc_up(int note);
#endif
