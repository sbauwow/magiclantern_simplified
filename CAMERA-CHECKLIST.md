# 1200D camera session — NEXT TEST (post 2026-11-06 crop-engine success)
# Card is READY AS-IS: latest build (crop engine 6281dee3b+checklist 31f5af9a2),
# crop.preset=0, modules lua+mlv_lite enabled. NO desk staging needed.
# Time: ~15 min full session (Parts 1-3 are the core; Part 4 optional).
# Desk context: skill magiclantern-1200d-build (STATE block first), 
# modules/crop_rec/1200D_preset_derivation.txt, 1200D_hw_modes/

## PART 1 — ZOOM RAW DUMPS (geometry calibration, THE key data) ~4 min
1. Boot. Modules → Load mlv_lite (if not loaded). Movie LV, rec size 1080p.
2. RAW video ON (Movie menu). Record 3s in 1080p normal — sanity: works.
3. Magnify → x5. Record ~8s raw. Note LV appearance while recording.
4. Magnify → x10. Record ~8s raw.
5. Magnify → full view. RAW video OFF.
NOTE: if raw recording errors at a zoom level ("raw error falling back"),
that is DATA — record which zoom level failed and what the screen said.

## PART 2 — CROP PRESET MATRIX ~3 min (crop_rec and adtg_log CONFLICT —
   never run both; crop tests FIRST, logger only in Part 3)
6. Modules → Load crop_rec. Movie tab → "Crop preset (1200D)" → toggle to
   Zoom-crop (beep). Watch LV 15s; if nothing, exit+re-enter LV once.
7. Q-button toggles it OFF. Confirm LV recovers clean.
8. Repeat once with rec size 720p (preset ON → observe → OFF).
NOTE per state: EXACT appearance — red/orange scramble? partially readable?
stretched/squeezed? frozen? dim? This matrix feeds the geometry fix.

## PART 3 — MODE LABELS via adtg_log ~3 min
9. Modules → Load adtg_log. Debug → "ADTG/CMOS log (1200D)" → select →
   cycle immediately, 15s dwell each: Movie-1080p → Movie-720p (Canon MENU,
   red movie tab) → P-mode + LV → back to Movie. Select entry again to save.
   (Buffer rolls to parts automatically; no timing pressure. If idle-saved
   early — select again and keep switches under ~25s apart.)
10. Power off. Card → reader → tell Hermes "keep going".

## PART 4 — optional leftovers
11. Selftest: RAW video OFF, mic connected, Modules → Lua → run 'selftest',
    hands off until "Self-test complete."
12. One 5s Canon MOV with mic, play back in-camera (audible y/n?).

## FIELD REPORT CHECKLIST (terse notes are fine)
- P1: raw recorded at x5/x10 y/n; error text if any
- P2: preset appearance per rec size; OFF recovery y/n
- P3: logger saved y/n, how many parts
- P4 (if run): selftest completed y/n; MOV audible y/n
- Anything red, any hang (power-off always safe; completed part files survive)

## DESK-SIDE AFTER (Hermes)
- Parse Part 1 MLVs: raw_info dims per zoom level → pixels-per-unit for
  8172/8173 + 805F/8061 → geometry update in crop_rec when crop engages
  (kills the red/orange scramble) → then real 3K presets per derivation file
- Parse Part 3: confirm the four 888x normal-mode labels
- Open threads: EDMAC photo-raw (EDMACMon never captured), dual_iso runtime
  (links clean; needs photo-raw first), MOV/mic playback question

## RECOVERY (unchanged)
- Solid red at boot = loader died → reseat card
- Boots with NO card = bootflag intact, card suspect
- Screen too garbled to menu → power off, pull card, desk-zero
  ML/SETTINGS/crop_rec.cfg (crop.preset = 0)
- Restore 2018: rename autoexec.2018-nightly.bak → autoexec.bin
