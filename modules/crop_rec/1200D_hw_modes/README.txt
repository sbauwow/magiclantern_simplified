# 1200D hw mode capture — 2026-11-06 night (v8 patch-hook logger)

Source: adtg_p001-003.bin (202,815 entries), run: 1080p start -> 720p ->
1080p -> x5 -> x10 -> full -> P-LV -> Movie. v8 logger = patch_hook_function
on ADTG_WRITE 0xFF3A246C (r0=chip mask, r1=u32 list ptr) + CMOS_WRITE
0xFF3A265C (r0=u16 list ptr). Filenames are PROVISIONAL labels from run
order; confirmed identities below.

## CONFIRMED mode signatures

Zoom (crop) family — unambiguous:
  c6=800 -> A6A, c7=001 -> 401, c4=014 -> 2D4, c5=C64 -> C60
  x5:  ADTG 8000=0005, 805F/8061=00C5, 8172=067D, 8173=0684, 888x=0410/0410/0413/0412
  x10: ADTG 8000=0006, 805F/8061=00E9, 8172=06F0, 8173=05FE (888x same as x5)
  => ADTG 8000 = zoom level selector; 805F/8061 + 8172/8173 = readout window/blanking

Normal family — four 888x variants (assignment provisional, confirm via
dwell order/next run):
  A: 888x=0417 0416 0414 0416, 805F/8061=00E9  (start state + dwell7)
  B: 888x=040B 040B 040C 040B, 805F/8061=0058/01D0 (dwell6 + extra8)
  C: 888x=0415 0414 0418 0417                  (dwell8)
  D: 888x=041C 041D 041E 041D, 805F/8061=01D0  (final long dwell)

CMOS: c1=443 c3=7D1 only in the very first dwell (1080p LV at select);
after the 720p switch c1=423 c3=7D0 for ALL subsequent states incl. zoom.
c0 = exposure (ignore). Junk-run "baseline" c1=423 = post-switch normal.

## crop_rec entry point

The zoom state IS a center-crop 1:1-ish readout the sensor supports natively.
Preset recipe: take zoom_x5/x10 register set as base, vary 805F/8061/8172/8173
(+) 888x to size the window -> 3K/2.5K/1080p-1:1 presets. Validate against
ROM corpus twins (modules/crop_rec/1200D_reg_corpus.txt) — 0x8826-class
regs are the binned-vs-1:1 candidates.

## Files
00-09 = per-dwell full ADTG tables (291 regs normal, 73 zoom) + c1-7 header.
adtg_p00*.bin = raw dumps (md5s alongside). Parser: ~/scripts/adtg_modes2.py
(dwell clustering, c0-excluded), adtg_modes.py (raw timeline).
