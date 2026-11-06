# 1200D camera session — Tue 2026-11-03 evening
# Build: color-crop-a7ed5a7df (md5 1b2d64f3b5c32b8cd4e12fc6028a9ef0)
# Time needed: ~20 min camera + ~5 min desk

# RESUME (Nov 6): build a7ed5a7df is ON the card already. Section A DONE, B mostly done
# (boot OK; dual_iso load err noted; "audio not playing back" = the MLV clip, expected
# silent). Run B2-retry, then C, D, E, then pull card for desk verify.
# CORRECTIONS: selftest MUST run with RAW video OFF (A/B clips = Canon MOVs, not MLVs).
# NEW: 5s Canon MOV playback test validates the audio output path.

## A. DESK — stage the card (~5 min)  [DONE Nov 3]
1. Card in reader (firm seat). Mount: udisksctl mount -b /dev/sdX1
2. On card: rename autoexec.bin -> autoexec.edmac-diag.bak (preserves the EDMAC diag build)
3. Copy onto card:
   - ~/magiclantern/platform/1200D.102/build/autoexec.bin          -> card root
   - ~/magiclantern/platform/1200D.102/build/zip/ML/*               -> card ML/   (modules + t5_102.sym — MUST go together with autoexec.bin)
   - ~/magiclantern/platform/1200D.102/scripts/selftest.lua        -> card ML/SCRIPTS/   (NOT in zip — manual copy)
4. Verify: md5sum card autoexec.bin == 1b2d64f3b5c32b8cd4e12fc6028a9ef0
5. Card needs >= 4 GB free (clips are ~55 MB/s raw)
6. sync, udisksctl unmount, power off reader, pull

## B. BOOT + SANITY (~3 min)  [boot OK Nov 4 — only B4/B5 remain]
1. Card in, power on. ML boots (TRASH button = menu). Debug tab = last tab (RIGHT arrow / main dial)
2. Modules tab: load-check these — adtg_log, raw_vidx (NEW), lua, mlv_lite, mlv_rec
   -> select each once (Load) if not marked loaded; note ANY red "load failed" text
3. Sanity: Movie mode, enter LV, confirm mlv_lite raw record works as before
4. DUAL_ISO RETRY (Nov 4 err): Modules tab -> dual_iso -> Load it ALONE
   (suspected batch artifact). Loads OK -> done. Fails alone -> note exact red text.
5. CANON MOV PLAYBACK TEST: RAW video OFF, mic in, record 5s normal MOV, play it back
   in-camera (expect AUDIBLE). This closes the Nov 4 "audio not playing back" question.

## C. ADTG/CMOS DUMP — crop_rec data (~6 min)   [BEFORE selftest]
1. Movie mode, LV on, 1080p
2. ML menu -> Debug -> "ADTG/CMOS log (1200D)" -> select
3. Screen shows "cmos:" + 8 values. THE KEY CHECK: values animate/change while you
   toggle modes -> addresses confirmed. Write down: animated y/n
4. Cycle modes, ~30s each, no recording:
   1080p -> 720p -> zoom x5 -> zoom x10 -> back 1080p -> P-mode LV 30s (full-sensor
   reference) -> back to Movie 1080p
5. Ends with BEEP + "ADTG dump saved: DCIM/adtg.bin". If no beep within ~6 min of
   toggling: just power off (buffer didn't fill — that's data, report it)

## D. SELFTEST SCRIPT (~2.5 min runtime)
0. RAW video OFF first! (Movie menu: RAW video -> OFF. A/B audio clips must be Canon
   MOVs or the loudness A/B measures nothing. Verify: record icon shows normal MOV, no
   raw overlay/banner)
1. Still in Movie mode, LV on, MIC CONNECTED (A/B audio test)
2. Modules tab -> Lua -> Run script 'selftest'  (first time: Load module lua)
3. Leave it alone until console prints "Self-test complete." (~2.5 min:
   A clip 8s -> B clip 8s -> EDMAC monitor + test RAW photo + 35s wait -> 20s rate clip)
4. Produces: ML/LOGS/SELFTEST.TXT, ML/LOGS/EDMACMon.TXT, 4 MLV clips

## E. ONE RAW CLIP WITH NEW BUILD (~1 min)
Record ~10s raw video in 1080p (this validates the color/IDNT fix — the MLV
header should now carry cameraModel 0x80000327; PC check confirms)

## F. DESK — verification (~2 min)
bash ~/scripts/mlv_ingest.sh /run/media/stathis/<card>
Expect per clip: frames MATCH + "DNG-CHECK: OK UniqueCameraModel='Canon EOS 1200D'
ColorMatrix2=dcraw-1200D exact". DON'T delete anything from the card.

## REPORT BACK (field notes, I take it from there)
- boot OK / weirdness (solid red LED, hangs, red text)
- module load failures (exact names)
- C3: cmos panel animated y/n; C5: beep y/n
- ingest output (or just hand me the card dirs)
- files to preserve: ML/LOGS/*, DCIM/adtg.bin, all new MLVs

## RECOVERY
- Solid red LED at boot = loader stage died -> reseat card, re-verify md5
- Boot fine WITHOUT card = bootflag intact, card is the suspect
- Restore 2018 nightly: rename autoexec.2018-nightly.bak -> autoexec.bin
