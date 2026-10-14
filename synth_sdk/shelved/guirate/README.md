# shelved: GUI frame rate (docs/32, docs/33)

Adaptive GUI frame rate: renders only when something changes (input, sound, tape, changed frame), own double
buffering, FPS cap 60 / 30 / 20 / 12 on the USB out page, `fps` on the resource monitor. Hardware-tested up to
`a449ef2` + `e3013e8` (no tearing, 30 fps fine); idle CPU in the emulator: tape 19 → 11 %, sampler 59 → 19 %.

Not in the build. To bring it back:
1. `git mv synth_sdk/shelved/guirate/guirate.c synth_sdk/screens/` (include path back to `../op1_synth.h`) and
   the test to `synth_sdk/harness/` (include `../screens/guirate.c`).
2. `git apply synth_sdk/shelved/guirate/integration.patch` (from the repo root): hooks (hook.S, link.ld,
   make_firmware.py HOOKS 0x01A3BBE0 / 0x019AB8CE + source list), input flag in dispatcher.c, FPS row on the USB
   out page (+ test), time-based sampling and the fps line in sysmon.c (+ test). Made against 93bc359 (the build without it).
3. Host tests: guirate_test, usbout_test, sysmon_test; render_usbout.py; then emulator checks as in docs/33.
