# Magic Lantern 1200D (Canon Rebel T5, fw 1.0.2) — Handoff

State as of 2026-11-01. Owner: Stathis. Agent: Hermes.
Companion skill (procedural memory): ~/.hermes/skills/gaming/magiclantern-1200d-build/SKILL.md

## Goal

Modern Magic Lantern port for the Rebel T5 / EOS 1200D firmware 1.0.2, developed on the
maintained fork (magiclantern_simplified), verified in QEMU before touching the camera,
then feature work (raw, EDMAC, stubs, MLU...).

## Directory map

- ~/magiclantern           reticulatedpines/magiclantern_simplified, branch `1200D-port`
                          (COMMITTED) = active dev base. Platform dir platform/1200D.102.
- ~/magiclantern_1200D    2018-era `1200D` branch of magiclantern_hg_02 (nikfreak's port).
                          Reference source + QEMU base. Contains his fw 1.0.1-era work.
                          ~/magic-lantern is a symlink to it (QEMU scripts require the name).
- ~/qemu-eos              QEMU 2.5.0 + EOS patches, BUILT and booting real firmware.
                          Camera ROMs: 1200D/ROM0.BIN + ROM1.BIN (16MB each, from camera).
                          Local commits: 1200D model entry, SIO1 MPU delegation, timer fixes.
- ~/gcc-arm-none-eabi-5.4 gcc 5.4.1 + binutils 2.35 toolchain (Debian debs, user-local,
                          wrappers fix libisl/libmpfr + newlib include path).
                          ~/scripts/ml_toolchain_setup.sh is idempotent.
- ~/gdb-arm-none-eabi-8_1  arm-none-eabi-gdb 8.1 (built for QEMU stub attach).
- ~/py2                   python2.7.18 (qemu-2.5.0 configure requires it).
- ~/magiclantern_1200D/firmware/  CCF15102.FIR (official 1.0.2 update, md5 363cab0c...)
                          + ml2018/ (the nightly Stathis runs on the camera).
- Scripts: ~/scripts/ml_* — toolchain, builds, ROM ingest, signature, FIR decoders,
  MPU stream decoder, gdb command files, console extractor.

## What works (done)

1. 2018 branch builds: bash ~/scripts/ml_1200d_build.sh → autoexec.bin + nightly zip.
2. Simplified-fork port builds clean: cd ~/magiclantern/platform/1200D.102 &&
   make ARM_BINPATH=$HOME/gcc-arm-none-eabi-5.4/bin  (with PATH pinned, see Gotchas).
   autoexec.bin ~440KB + magiclantern.zip. Port mechanics recorded in SKILL.md.
3. Camera ROM dumps ingested (ML Debug menu → "Dump ROM and RAM" → ML/LOGS/).
4. Firmware signature: SIG_1200D_102 = 0xBA19C526 (wordsum of 0x10000 WORDS at 0xFF0C0000
   = ROM1.BIN offset 0xC0000). NOTE: compute_signature counts WORDS — unit error here
   cost days; see "Lessons".
5. QEMU boots REAL 1.0.2 firmware with our autoexec: Canon bootloader runs, boot flag
   honored, "Now jump to AUTOEXEC.BIN!!", ML cstart passes signature, DryOS starts:
   "K327 READY" / "ICU Firmware Version 1.0.2 (4.4.4)" on the console.
6. Interrupt delivery verified working (vector 0x18 hits, thousands/min when tuned).
7. DryOS timer identified: Timer #2 (C0210200), tick IRQ 0x0A, model entry fixed,
   re-arm delay floored at 10 (reload value 2 is prescaled; delay=1 → IRQ storm).

## The blocker (single dependency chain, fully mapped)

DryOS creates exactly ONE task — "PowerMgr" (entry 0xFF2B5CCC, name @0xFF2B5F00) —
then blocks in its mailbox wait. Chain:

  MPU exchange never completes
  → intercom/init never finishes
  → interrupt handler table pointer at RAM 0x1944 stays NULL
  → every IRQ is ACKed but dispatched to nothing
  → no task progress, console stops after version banner.

IRQ dispatch path (validated live): vector 0x18 → RAM dispatcher 0x4B0 →
ack [0x1A30]←1, call C handler 0xFF0C973C, read pending id at C0201004
(QEMU returns irq_id<<2, resets on read), id>>2 stored at 0x674, jump via
[0x684]→[0x1944] (NULL at stall).

## MPU protocol (decoded from ROM, offline)

Send primitive at ROM 0xFFFF76C0-0xFFFF772C (per 16-bit word):
  1. wait SIO1 [base 0xC0820100] +0x104 bit0 clear
  2. GPIO 0xC0220028 ← 0x44 (request)
  3. word → [base+0x118], wire-transform: wire = ((w & 0xff00) << 1) | (w & 0xff)
     (decode: w = ((wire >> 9) << 8) | (wire & 0xff); bit 8 = separator)
  4. strobe [base+0x104] ← 1, wait clear
  5. GPIO 0xC0220028 ← 0x46 (idle), delay
  6. message complete: GPIO 0xC0220020 ← 0x48

Firmware→MPU init "message" = config-table upload of (index,value) word pairs,
e.g. (1,60)(2,7A)(3,24)(5,00)(7,03)(A,20)... — NOT the old [len][cmd] framing;
that's why all existing spell sets fail to match. Decoder: ~/scripts/ml_mpu_stream.py
(raw captures in /tmp/qemu_boot19.log — regenerate if lost).

Emu status: eos_handle_sio3 delegated to serve SIO1 for 1200D (committed); handshake
register C0220028 wired (gdb-verified eos_handle_mpu fires). Reply delivery is the
missing half — see Next steps.

## Key addresses (1.0.2, gdb/trace-validated unless noted)

  DebugMsg            0xFF11F5DC   (unchanged from 1.0.1; use QEMU_EOS_DEBUGMSG=... -d debugmsg)
  task_create         0xFF122824   (unchanged from 1.0.1)
  register_interrupt  0xFF1220D4   (1.0.1 value, likely valid — verify)
  MPU send word loop  0xFFFF76C0   (93 hits/run; r4 = true word value)
  PowerMgr task       entry 0xFF2B5CCC, name 0xFF2B5F00, mailbox pool 0xFF2B5EC4
  IRQ dispatcher      RAM 0x4B0 (vector 0x18 → [0x30])
  pending-IRQ reg     C0201004 (id<<2, read-to-reset)
  handler table ptr   [0x684] → [0x1944] (NULL at stall)
  current_task        MEM(0x1A2C); task struct V2: entry +0x0C, name +0x24
  DryOS timer         Timer #2 @ C0210200, IRQ 0x0A
  MPU handshake       GPIO 0xC0220028 (0x44/0x46), completion GPIO 0xC0220020 ← 0x48
  SIO1 base           0xC0820100 (data out +0x118, ack/strobe +0x104, data in +0x11C)
  fw signature        SIG_1200D_102 = 0xBA19C526
  Intercom registrar  0xFF4F8448 (walks 8-byte {arg,handler} entries;
                       core ISRs in RAM at 0x19C4+: FF0C29D8 FF0C32A4 FF0C35F0
                       FF0C3B2C FF0C3E00 FF0C3F48)
  Name/vector tables  ROM 0xFF71C280 (27 halfwords 0x3A..0) beside string ptr table
                       0xFF71C2B8 with SIO1/SIO3/SIO4 strings

## Gotchas (each one cost real time)

- ALWAYS export PATH="/usr/bin:$HOME/.local/bin:$PATH" (or toolchain gdb paths) —
  theos' iOS clang shadows gcc; prowler-agent venv shadows python3.
- Tirith (shell security): blocks inline loops, pipes→interpreters, multi-`-ex` gdb
  one-liners, rm -rf, printf>>file. Use script files + gdb command files + patch tool.
- QEMU 2.5.0 on modern host: configure flags in SKILL.md; glib pkg-config injects
  -D_FORTIFY_SOURCE AFTER user -U — append -U_FORTIFY_SOURCE at END of CFLAGS line
  in config-host.mak post-configure (the "longjmp causes uninitialized stack frame"
  abort). After `make clean`, run TOP-LEVEL make first (config-host.h regeneration),
  then make -C arm-softmmu. Run headless: -display none + stdbuf -oL (SIGTERM eats
  stdio buffers — a silent log is not a hang). Kill stale qemu-system-arm orphans;
  they hold sd.img and gdb :4444.
- SD image: partition offset 99*512 = 50688 → mcopy -i sd.img@@50688 ... (literal, no $(( ))).
- compute_signature counts WORDS not bytes.
- gdb on running QEMU: launch with -gdb tcp::4444 (add -S to break from reset);
  attach via command file. bt fails on TCM stacks — read raw stack words instead.

## Session addendum: boot cascade mapped end-to-end (pure RE)

The definitive stall chain, each link live-verified in the emulator:

1. Task cascade: PowerMgr → DbgMgr → TaskMain → stall. TaskMain (entry 0xFF0C1CB8) is ALIVE, parked on a kernel wait (0xFF122118 call site) — earlier "only PowerMgr" was a single-wait-queue walk artifact; its struct lives at 0x103BC8 (task structs malloc'd around 0x10xxxx — RAM scans must go that high).
2. register_interrupt = 0xFF1220D4 (from contrib/qemu/scripts/1200D/debugmsg.gdb — VALIDATED live; that script also carries mpu_send/mpu_recv addrs). 16 kernel timer ISRs registered (ids 0x1a-0x27, 0x10) — but they never run:
3. Master IRQ handler cell [0x1944] (reached via [0x684], trampoline pool 0xFF0C03B8) is NULL at stall → dispatcher dispatches nothing → no ISR executes → sequencer completions never post → "NotifyComplete: Illegal Handle" → TaskMain sleeps forever.
4. The installer that never runs: intercom name table @0xFF71C2B8 ("INTC","SIO1","SIO3","SIO4","WDT",... after a descending halfword array) consumed by the function with pool ref 0xFF4F7F24 (≈0xFF4F7Exx, near "CreateMessageQueue" string 0xFF4F7F3A).
5. MPU driver init = 0xFF2976A8: registers MREQ_ISR (id from caller, handler 0xFF297580) + SIO3_ISR (**id 0x36 confirmed** — model_list guess was right; handler 0xFF297610, byteswaps rx, state machine 0xFF2978C0). No static refs to 0xFF2976A8 anywhere → runtime-computed driver-table call.
6. Emu IRQ delivery itself is SOUND (timer fires, id readback via C0201004 works) — the dead dispatch target is the blocker, not delivery.

Next session opens with: find the 0xFF4F7Exx registrar's start + callers, breakpoint it (or hw-bp on writes to 0x1944), and RE kernel-init's 0xFF125E04/0xFF125E54 pair — suspected master-handler install/enable path. New tooling: ~/scripts/ml_find_bl.py, ml_rom_ptr.py, ml_rom_str.py, ml_rom_find_str.py + the three gdb trace scripts.

## Session addendum 2: timer FIXED, dispatch decoded, the circle closed

1. TIMER BUG FOUND & FIXED: the old int 0x0A was NEVER delivered — interrupt delivery requires the firmware to enable the id via C0201010 (done inside register_interrupt), and 0x0A was never registered. The kernel registered its timer ISRs under ids 0x10 (OC4_14) and 0x1a-0x27 (OCH_SPx/OCHxEPx/ICAPCHx). model_list.c now: dryos_timer_interrupt = 0x1D (timer#2 period-end, handler 0xFF121530) — verified delivering via [0x674]=0x74. NOTE: all previous "thousands of IRQ hits" were other sources (ids 0/2/0x38) — the tick never fired until this fix.
2. Dispatcher (RAM 0x4B0) fully decoded: dispatch = handler from 0x400006F8 + id*4 (arg from 0x40000AF8 + id*4); [0x684]→[0x1944] is only an OPTIONAL master hook (NULL is normal — prior theory retracted); default handler 0xFF0C6C94 for empty slots. EXIT HAS NO TASK SWITCH: this DryOS is cooperative-only.
3. register_interrupt path: 0xFF1220D4 → 0xFF2B3BCC → 0xFF0C0954 stores handler/arg into the 0x400006F8/0x40000AF8 tables (HIGH RAM — scan there, not 0x0-0x200000) + enables the id at C0201010.
4. Handler table verified populated at stall (ids 0,2,0x10,0x1a-0x27). Slot 0x36 (SIO3/MPU) EMPTY — GMSC init 0xFF2976A8 never ran (it registers MREQ_ISR + SIO3_ISR id 0x36).
5. PowerMgr (0xFF2B5CD8 loop) = infinite poll of its own mailbox + WFI helper (mcr CP15 cr15 under QEMU = ignored → tight spin). It NEVER blocks at kernel level, so TaskMain (created, never ran — saved-SP==stack-top) starves.
6. THE CIRCLE: no MPU message → PowerMgr spins → TaskMain starves → GMSC init never runs → MPU ISRs (SIO3 0x36) never registered → no MPU message. On real hardware something breaks this circle — ground truth needed.
7. NEXT — two options: (A) emulator synthesizes the GMSC registration (write SIO3_ISR 0xFF297610 → slot 0x36, MREQ_ISR 0xFF297580 → slot 0x50, set args + irq_enabled) and answers the MPU exchange via the working SIO1 transport; (B) DEFINITIVE: dm-spy MPU capture on the real T5 (ML 2018 nightly Debug tab) — captures the true first-boot mpu_send/mpu_recv flow and reply dialect in one shot.

## Next steps (ordered)

1. Deliver ONE MPU reply. Most likely mechanism (hypothesis): on real hw the MPU
   asserts its request line → GPIO interrupt on C0220028 wakes the exchange. Find
   the GPIO IRQ vector for that line (candidate range ~0x50+; registrar at 0xFF4F8448
   + the 0xFF71C280 tables are the static leads; the live dispatcher dump is the
   dynamic method). Then make eos trigger it on reply-ready instead of mpu_sio3_interrupt.
   Alternative: camera-side dm-spy capture of the real exchange → build
   mpu_spells/1200D.h (extract_init_spells.py in-tree consumes such logs).
2. Once PowerMgr wakes: watch handler table [0x1944] populate, console should flow
   (expect sequencer errors CreateMessageQueue/CreateSequencer 0x5 next — assess then).
3. ML banner → GUI → then ROM mining for the feature gap: EDMAC channels,
   get_task_info_by_id (stubbed in cfn.c), TG_FREQ verify, raw pipeline
   (CONFIG_RAW_* / EDMAC_MEMCPY / SRM currently disabled pending RE).
4. Camera flash-test of the simplified autoexec only after QEMU reaches banner.
   Camera SD is boot-flagged; autoexec drop-in works.
5. Commit discipline: ~/magiclantern branch committed; never push/commit without
   Stathis's word (house rule). qemu-eos local commits exist on top of vanilla.

## BREAKTHROUGH 2 (2026-11-01, offline session 3): ROOT CAUSE = RESTARTSTART; DryOS FULLY UNBLOCKED

The "PowerMgr/TaskMain starvation + MPU dead circle" was a MEMORY-MAP bug in OUR PORT:
- The kernel builds a v1 memory region: remaining = 0x103958(fixed, ROM literal) - _bss_end(patched by boot-d45).
  Port had RESTARTSTART=0xC80100 (D5-style value) → _bss_end=0xCFCE00 > 0x103958 → remaining NEGATIVE (0xFF406B58)
  → top-down region {0x1F8D0 base,[0x1F8D4] remaining} born dead
  → AllocateMemory pool (struct 0x16B48, malloc_status V2: +0xC freelist, +0x10 complemented remaining)
    created with negative size → first alloc consumed it → freelist NULL forever
  → CreateMessageQueue (0xFF2B3C44 → alloc 0xFF0C8BA4 → 0xFF1319D4) returns 5
  → [SEQ ERROR] CreateMessageQueue (0x5) → Startup/CreateSequencer fails → boot stalls at 4-5 console lines
  → GMSC/MPU driver init 0xFF2976A8 (which needs CreateMessageQueue for 'GMSC_Runtime') NEVER RAN
    → slots 0x36/0x50 empty → MPU dead. ALL previous MPU theories were downstream symptoms.
- FIX: platform/1200D.102/Makefile RESTARTSTART = 0x82900 (2018-port value; MUST stay below 0x103958).
  UNCOMMITTED. Image+bss ends ~0xF4E00 < 0x103958 (tight, ~60KB margin — keep the build lean).

### Verified after fix (QEMU, real 1.0.2 fw + our autoexec)
- CreateSequencer(Startup) SUCCEEDS; [SEQ ERROR] gone; Startup task RUNS as its own task
  (DebugMsg caller "Startup:", DisablePowerSave/EnablePowerSave cycles).
- GMSC init ran with REAL args: drv struct 0x5D48 = {+0x1C=0xFF10F768 (completion cb = mpu_recv!),
  +0x18=0xC022009C (GPIO — 1200D's second MPU GPIO; NOT C0220028), +0x20=0xC020302C, +0x24=1}.
- Handler slots: 0x36=0xFF297610 (SIO3_ISR), 0x50=0xFF297580 (MREQ_ISR), 0x1D=0xFF121530 (tick).
- TaskMain RAN (struct saved-SP 0x3D2044, entry 0xFF0D8270). PowerMgr parks in WFI helper = normal idle.
- ML banner prints ("Magic Lantern 2026-10-31.1200D.102") — was already true pre-fix.

### MPU driver RE facts (this session, offline)
- GMSC init 0xFF2976A8(r0=status reg ptr→[drv+24], r1=mreq id, r2=reg ptr→[drv+28] (written 0xC/0x1C),
  r3=completion cb→[drv+20]); pools at FIXED RAM 0x29724 (120×128B) + 0x29904 (20×128B), drv struct 0x5D48.
- register_interrupt(name, id, handler, arg=0) — SIO3 id 0x36 hardcoded, MREQ id from caller (=0x50 live).
- RX word reg = 0xC082031C (SIO3 port @ C0820300; TX uses SIO1 @ C0820100+0x118). ISR byteswaps.
- 0xFF125E54/EC0 (power req helpers) IGNORE incoming r0 (reload [0x26BC]) — safe to call with garbage.
- Intercom walker 0xFF4F7DE8: fn-table literal [0xFF4F7FB0]→RAM 0x10F5C (runtime-populated, NOT ROM);
  entries {fn,arg} 8B; per entry: call fn() then registrar 0xFF4F8448(idx,arg) → linked list at [0xFF4F7CE8].
- Memory-map plumbing: 0xFF0C7E54 builds default map; 0xFF1317D8 publishes map→globals 0x2948..;
  0xFF1333F0 inits top-down region {0x1F8D0=base,[+4]=remaining,[+8]=flag} from map;
  0xFF133468(n)=top-down carve (returns base+remaining_after); 0xFF13348C()=peek remaining;
  pool create = 0xFF13194C(struct,start,size,flags); pool consistency checker 0xFF2CCC90.
- Region-size constant 0x10AC00 @ ROM 0xFF0C10CC (right beside HIJACK_INSTR_BSS_END 0xFF0C10D0);
  v1 boundary 0x103958 (from live stack, not yet ROM-located).

### NEXT (ordered)
1. Let the fixed build run LONG (10-30 min TCG): expect MPU exchange activity on SIO1 now that
   ISRs 0x36/0x50 are live; watch -d mpu log; goal = ML GUI/banner in eos display + K-task cascade.
   If firmware waits for unsolicited MPU messages (real hw MPU sends boot messages), synthesize:
   eos writes reply word to 0xC082031C (byteswap16) + trigger int 0x36 (model mpu_sio3_interrupt=0x36 ✓)
   — and/or MREQ 0x50. mpu_recv 0xFF10F768 = the real completion cb (drv+0x1C) for spell RE.
2. Then: ROM mining for the feature gap (get_task_info_by_id, EDMAC, TG_FREQ, raw pipeline).
3. Flash-test fixed autoexec.bin on the real T5 (card boot-flagged; drop-in). RESTARTSTART change makes
   it match the known-working 2018 memory layout — LOW RISK.
4. Commit (qemu-eos clean through 08d9e403 + uncommitted 0x1D timer working tree; ML port: RESTARTSTART
   fix + fw-signature 0xBA19C526 still uncommitted) — only when Stathis says so.
5. Session tooling added: ~/scripts/ml_boot_qemu.sh (ML_BOOT=0 → -M without boot flag; NOTE: plain
   -M and no-autoexec both land in the bootloader ROM-UTILITY MENU — plain-fw boot NOT reachable),
   ml_gdb_run.sh (Tirith-safe gdb wrapper), gdb: ml_gmsc_probe/ml_poolwalk/ml_poolwatch/ml_regionwatch/
   ml_2948watch. ORPHAN-QEMU RULE REINFORCED: pkill + verify BEFORE every relaunch (cost 2 bad reads).

## BREAKTHROUGH 3 (2026-11-01, offline session 4): MPU EXCHANGE ALIVE; boot reaches FileMgr/SD

Two emulator fixes (both in ~/qemu-eos working tree, UNCOMMITTED on top of 08d9e403):
1. model_list.c 1200D MPU dialect CORRECTED (was the blocker for the whole GMSC exchange):
   request/status = 0xC022009C, control = 0xC020302C, mreq 0x50, sio3 0x36 — live-verified
   against drv struct [0x5D48+24/28] AND matches the sibling D4 entry (60D/550D family).
   The old C0220028 was the EARLY pre-driver SIO1 primitive's reg (FFFF76C0 era), wrong for GMSC.
   With the wrong status reg, eos never engaged mpu.receiving/sending → exchange dead.
2. mpu.c 1200D unknown-reply hack heap-corruption FIXED: it enqueued STATIC spell arrays;
   mpu_send_next_spell free()s queue entries → "free(): invalid size" core dump. Now enqueues
   malloc'd copies. (Hack itself = reply-with-spell-set-#1 on unknown messages — works.)

### Result (QEMU, real 1.0.2 + our autoexec, RESTARTSTART fix from session 3)
- SUSTAINED MPU exchange: firmware Init matched spell #1 → full property cascade delivered →
  firmware ACKs (e.g. "Complete WaitID 0x8000003F PROP_MULTIPLE_EXPOSURE_SETTING").
- Tasks alive: Startup, PropMgr (AUTO_POWEROFF/UILOCK cbrs), MainCtrl (DisplaySensor poll,
  PROP_VARIANGLE_GUICTRL Enable), FileMgr (PROP_CARD2_* burst) + CreateTask Master End.
- Console timeline: [DMA2] OK, Initialize Adjective & Situation, startupPropAdminMain End.
- NOTE: spell replies are 60D-era content — props are plausible fakes until a real 1200D
  dm-spy capture builds mpu_spells/1200D.h. Good enough to boot; don't trust prop VALUES.

### NEW BLOCKER (next session start): SD driver
- `[SD] ERROR UNEXPECTED ERROR` x15 at ~54s virtual → FileMgr card I/O fails → boot parks
  after the PROP_CARD2_* burst (log tail: PROP_CARD2_FILE_NUMBER spell #15 + Request more data).
- Model entry still lacks sd_driver_interrupt/sd_dma_interrupt/uart (1300D uses 0x4B/0x32/0x38).
  RE path (proven recipe from MPU): find 1200D SD driver init in ROM (registers ISRs via
  register_interrupt 0xFF1220D4 — name strings like "SD" make it findable via ml_rom_str.py),
  read the ids, wire model entry. Check 1300D/other D4 entries for the expected values first.
- Also pending: uart_rx 0x38? (1300D), CF/IDE path (cf.img mounted?), and the spell ground-truth.

### Session-4 tooling/facts
- ~/scripts/ml_logcheck.py <log> [filters] — ANSI-clean QEMU log summarizer (Tirith-safe).
- gdb bp-sampling while target runs: MUST issue a top-level `continue` after setting bps
  (bp with silent/continue commands only fires AFTER a continue; forgot it once = false zeros).
- eos mpu reply loop (works for 1200D now): firmware TX → eos recv → spell match/reply hack →
  mpu_start_sending → eos_trigger_int(mreq 0x50) → firmware MREQ_ISR → firmware reads reply
  via SIO3 rx on C082031C-read (mpu_handle_get_data) → eos_trigger_int(sio3 0x36) per word.

## BREAKTHROUGH 4 (2026-11-01, offline session 5): SD MOUNTED; boot reaches audio/filesystem

- SD interrupt ids RE'd from ROM (RESTARTSTART-style recipe, 30 min): SDDriver init at
  0xFF5B17xx registers via 0xFF1220D4 with id from getter fns 0xFF5B2C28 (mov r0,#0xB8) and
  0xFF5B2C30 (mov r0,#0xB1) → sd_driver_interrupt=0xB1, sd_dma_interrupt=0xB8 (matches
  550D/600D-family entry — same family as our MPU regs; 1200D timer int differs from that
  era (0x1D vs 0x0A) so family values must always be verified against ROM).
  Model entry wired (UNCOMMITTED). Names live in string-ptr groups @ROM 0xFF7E2158
  ({SDSemInterrupt,SDSemExclusive,SDDriver}; CF group @0xFF7E2108); driver module 0xFF5A0000-0xFF5B3000.
- ~/scripts/ml_regint_inventory.py: full 62-site register_interrupt inventory (names+ids) for 1.0.2 —
  USBDET 0x55, HDMIDET 0x54, WDT 0x3, DiUSB20Hal 0x7b, JpCore 0x64/0xa3, SEQ 0x9c, BLTDMA reg-passed,
  IMGPOWDET 0x52, CAPREADY 0x51, HEAD1-4 0x6a/0x6b/0xd9/0xe0, Kaiser 0xd8... (ids for future devices).
- RESULT: `CSMgrTask: SDEventHandler(ID=1:<B:>) Mounted`, [SD] UNEXPECTED ERROR 15→2 (transient),
  FSUmount/remount cycle OK, fmAllocDcfNo (DCF file numbering!), AudioLevel task alive
  (movie volume/sound/wind-cut props), RscMgr srmEventDispatch, fmSetInitialCardInfo(Drive=2).
  MPU driver counters advanced (drv+0=0x75 msgs, +4=0x2BA, rx idx 0x10) — real traffic both ways.
- CURRENT STALL (content, not mechanism): PowerMgr WFI-idle = all tasks parked on events/props.
  eos spell queue drained ("spells finished"); firmware waiting for property content the 60D-era
  spell set doesn't deliver. Mechanism chain (memory→tasks→MPU→SD→FS→audio) is COMPLETE.
- NOTE: one 1500s run self-exited at 835s cleanly (no assert/shutdown-print/core — cause unknown,
  possibly eos powerdown path without print; reproduce if it recurs).
- NEXT: (1) enrich spells — biggest gaps: whatever props the firmware waits on post-AudioLevel
  (watch drv counters stall + DebugMsg "WaitID" timeouts; dm-spy capture from real T5 = ground truth
  for mpu_spells/1200D.h); (2) eos key injection to poke MainCtrl/GUI (input backend with -display
  none — check eos sdl/input handlers); (3) display path (eos display device + PROP_GUI events);
  (4) uart_rx (1300D 0x38, sibling 0x2E — RE if serial needed).

## BREAKTHROUGH 5 (2026-11-01, offline session 6): 0x20000 injected → past step 3; GUI cbrs fire

- Blanket-cascade flood was NOT the stall cause (once-only variant: same stall point).
  Hack now fires ONCE then logs silently (mpu.c, cascade_sent).
- Startup sequencer FULLY decoded (live RAM step table @0x19C0, 6 steps {mask, fn, pad}):
  0:{0x10000, 0xFF0C29D8} 1:{0x20000002, 0xFF0C32A4} 2:{0x10, 0xFF0C35F0}
  3:{0xE0110, 0xFF0C3B2C} 4:{0x110, 0xFF0C3E00} 5:{0x80200200, ...}
  Seq handle lives at [[literal 0xFF0C2044]→RAM 0x1960] = e.g. 0x3D2044.
- THE INJECTION (decisive): at stall, gdb force-call NotifyComplete (0xFF0D85C4) with
  (seq, 0x20000), save/restore PowerMgr context (script ml_inject20000.gdb; return-sentinel
  bp @0xFF7FF000) → seqEventDispatch(Startup,3) fired, boot resumed:
  [GUI] MasterResultCBR, AudioICSerialCommand running, FileMgr PROP_CARD1_RECORD,
  spell machinery kept flowing. Injection = the general unblock for un-emulated devices.
- NEW BLOCKER: "[AUDIO] WARN AudioICSerialCommand:Wait sem check 7" — audio IC serial
  (I2C/SIO to the audio chip) never answers → its semaphore never posts → later startup
  events (step 4/5 bits) blocked. Same class: devices needing emulation or completion-injection.
- Sequence of stall events so far (healthy-boot checklist): pool → MPU dialect → SD ids →
  0x20000 (unknown poster — probably one of step-3's init calls blocked on hw) → audio IC.
- NEXT: (1) identify the 0x20000 poster properly (RE step-3 fn 0xFF0C3B2C's children — one
  of them posts it on hw-ack; likely also audio/serial-ish); (2) audio IC: find its SIO/I2C
  regs + ISR in ROM (regint inventory has candidates), either emulate minimal ACK or inject
  the sem; (3) iterate injections through steps 4-5 to GUI; (4) THEN backfill proper device
  emulation + real spells. gdb inject recipe: save ctx, set r0/r1, LR=0xFF7FF000 sentinel
  (bp there), PC=target, continue, restore.

## BREAKTHROUGH 6 (2026-11-01, offline session 7): sequencer walked to step 5; dispatch mechanics mapped

- Staged injection WORKS (scripts ml_inject_s3/s4a/s4b/s5a-c.gdb, one bit each):
  0x20000 → step 3 completed + handler ran (InitializeAudioIC, micResultCBR, AudioBuzzerSet(5),
  AudioSEStop, spsActivate, "Regist RPC Handler Master ALO"). Then 0x10+0x100 → "Cur = 4"
  accepted both, mask cleared.
- SEQUENCER RULES LEARNED: out-of-step bits are REJECTED ("[SEQ ERROR] NotifyComplete
  (Cur=N, mask, Flag)" and DROPPED — the early blast-through failed for this reason);
  must inject in step order, one step's mask at a time.
- Sequencer struct (handle via [[0xFF0C2044]→RAM], 36B): +0 name ptr, +0x0C packed sem/mq ids,
  +0x10 state(1), +0x14 step table ptr (RAM 0x19C0), +0x18 Num=6, +0x1C current step idx,
  +0x20 current mask accumulator. Step 5 = {0x80200200, fn 0xFF0C3F48}.
- NEW SUBTLETY: after step-4 mask cleared, "Cur = 4, mask 0x0" but NO dispatch to step 5 —
  injected completions ran in hijacked PowerMgr context; the wake didn't dispatch.
  Dispatch fn = 0xFF0D8270 = TASK-BODY LOOP (blocking mq recv at 0xFF2B3D54 + step exec) —
  DO NOT force-call it (blocks; wedged a session — gdb timeout, context unrestored).
  NEXT: inject step-5 bits ONLY after confirming dispatch(4) ran; the right hook is either
  (a) find why the wake didn't dispatch (state +0x10? count check at 0xFF0D861C/+24/+28?),
  or (b) inject completions from the CORRECT task context (break on seqEventDispatch site
  0xFF0D82B8 hits and inspect), or (c) eos-side: patch the sequencer wake path.
- Audio IC detail: transport = SIO regs 0xC0820104 (cmd/status bit0, spin-wait),
  0xC0820118 (tx), 0xC082011C (rx result) — inside eos's SIO1 window but at offsets the
  MPU handler ignores; generic sio returns 0 on read (bit0 clear = never busy, spins exit),
  result reads 0 → audio chip ID check fails → wrapper WARNs+retries (not fatal, just slow+dead).
  Proper fix: minimal audio device in eos_handle_sio (1200D-gated): complete trigger writes,
  return plausible chip ID on 0x1C reads. AudioIC fns: cmd 0xFF2AD554, wait 0xFF2AD178,
  GPIO power [0xC0223010]|0x80 (r7=[0xFF2AD6E4]=0xC0223000).

## BREAKTHROUGH 7 (2026-11-01, offline session 8): audio emu attempt; step-3 handler anatomy

- Wake path CONFIRMED (NotifyComplete tail 0xFF0D864C-96): mask-clear via `bics [seq+0x20],flag`;
  on zero → 0xFF2B3F40(mq=[seq+0xC], state) posts the wake; dispatch loop = SEPARATE TASK
  (struct @0x103BB8, entry 0xFF0D8270, arg=seq handle, name "Startup", stack 0x10C350+0x2800,
  saved-SP @struct+0x18 = 0x10EAE0). Park signatures (read saved stack):
  * 0xFF0D828C/0xFF2B3DF4/0xFF0C7AD0 frames = healthy park in dispatch-recv ✓
  * 0xFF1227B8 + 0x65(101 ticks) + 0xFF11C57C frames = parked in audio wait INSIDE step-3 handler.
- 0x20000 injection → dispatch(3) → handler ran FULL init cascade each time (InitializeAudioIC,
  micResultCBR, AudioBuzzerSet(5), AudioSEStop, spsActivate, RPC Master ALO) but the handler
  NEVER RETURNS: audio serial commands retry ~1000ms each forever (sem never posted).
  The 0x20000 poster is NOT audio-completion — still unidentified (likely another step-3 child).
- AUDIO EMU (eos.c eos_handle_sio, PC-gated 0xFF2AD000-0xFF2AF000, 1200D+parm==1): on cmd write
  (addr&0xFF)==0x04 bit0 → echo reply + eos_trigger_int(0x33/0x34); status reads → 0; rx reads
  → echoed byte. DID NOT FIRE (audio still parked identically; emu log lines absent — note
  io_log needs -d io, so absence is NOT proof; but the 101-tick sleep park persists).
  NEXT DEBUG: relaunch with -d io,io_log or convert prints to EPRINTF; watch the C08201xx
  window during the audio phase. Suspects: (a) trigger ints 0x33/0x34 not enabled at emu
  level (event-init 0xFF126074 ordering), (b) handler dispatch layer not calling eos_handle_sio
  for these writes (RMW form?), (c) PC gate wrong moment (store at 0xFF2AD5E8 in range — ok).
- ALSO confirmed: audio event ISRs registered live: slot 0x33=0xFF106C60, 0x34=0xFF12602C
  (generic event dispatcher: table [0xFF126268][id], row+0x38=CBR called, else flag-clear+post).
- Sequencer task-vs-TaskMain clarified: TaskMain @0x103C0C (entry 0xFF0C1CA8, name@0xFF0C26E8)
  is a DIFFERENT task from the seq task @0x103BB8.

## BREAKTHROUGH 8 (2026-11-01, offline session 9): GUI REACHED — DRYOS PANIC at GUI.c:664

- Audio chain root-caused one level deeper: InitializeAudioIC → 0xFF10C730 = DryOS RPC submit
  (malloc request, post to mq via 0xFF10CB2C; "Regist RPC Handler Master" family) → the
  audio RPC Master NEVER REPLIES (needs un-emulated audio DSP / or Master task itself waits
  on hw). The wait fn 0xFF2AD178 uses sem [0xFF2AD2FC-struct+4]; live struct @0x6E9C shows
  sems NULL/0xFFFFFFF9-ish = sem-creator 0xFF2AD4EC never ran → upstream init-order issue.
  NEXT THREAD: find 0xFF2AD4EC's caller (who should create the audio sems) and why it didn't run.
- eos audio SIO intercept (PC-gated, 1200D): CONFIRMED NEVER REACHED — unconditional
  fprintf showed zero [AudioIC] access lines; the blocking op never touches those SIO regs
  (it's RPC-layer, not the 0xFF2AD554 serial fn). Intercept is harmless; keep or remove.
- FORCE-CALL EXPERIMENT (ml_callstep4/5.gdb): calling step handlers directly from hijacked
  context: step-4 fn 0xFF0C3E00 BLOCKS (contains a wait — gdb race, first call wedged,
  context left inside 0xFF0C08D4); step-5 fn 0xFF0C3F48 RAN THE GUI INIT:
    [MainCtrl] GUI.c ASSERT Line 664 "err == SUCCESS" → PROP_PERMIT_ICU_EVENT (SW=1,4) →
    startupErrorRequestChangeCBR OverWrite (0x1d) → **DRYOS PANIC: Module Code = 1, Panic Code = 2**
  = genuine firmware startup-error path. GUI code executes; MainCtrl's GUI op fails (needs
  display/prop state), asserts, firmware crashes CLEANLY (panic screen state on real hw).
- NEXT (ordered): (1) RE the GUI.c:664 assert context: MainCtrl call site near 0xFF0C238C —
  which GUI call returns err != SUCCESS (likely a prop/GUI-state dependency or display drv);
  this is now a NORMAL bring-up bug hunt (firmware fully alive). (2) Audio sem creator caller.
  (3) Consider eos display device emulation (PROP_GUI events) instead of step-force-calls.
- gdb force-call caveat CONFIRMED twice: handlers containing blocking calls wedge the
  hijacked context (sentinel bp never hit; gdb times out; CPU left inside the callee).
  Only force-call fns known not to block (or accept the mess and reboot after).

## BREAKTHROUGH 9 (2026-11-01, offline session 10): GUI assert chain fully mapped

- Assert capture recipe (proven, ml_assertcatch2.gdb): bp 0xFF0C4148 (generic assert) in the
  SAME gdb session as the step-5 force-call; assert fn args = (r0,r1=string ptrs, r2=LINE, ...)
  — GUI.c @0xFF0D1298, line 0x298=664, wrapper LR 0xFF0D10A8.
- GUI MASTER DISPATCH DECODED (0xFF2B95D0-0xFF2B9688): walks a request table {tag 0xFF7EC37B,
  name2, 0, self 0xFF2B95D4, code_ptr, id, count}; per entry: 0xFF2B9654 `blx r3` handler
  (args r0=ctx, r1=arg, r2=request id); **handler return != 0 → 0xFF2B9684 → GUI.c:664 assert**.
- LIVE CAPTURES (ml_guicatch.gdb, bp at the blx, staged: s3-inject → step5 force-call):
  * GUI master code ptr = 0xFF6A2984 (table ctx 0x3D1D5C, ids seen: 0x39,0x3B,0x40,0x41,0x42,0x43)
    → handler 0xFF2B19C0 (r0=0x3D1A10) — the GUI request handler; one of these ids fails.
  * Other masters: 0xFF69E0E4 (id 0x54 → 0xFF29309C), 0xFF68734C (→ 0xFF161FEC).
- NEXT (precise): bp at 0xFF2B9658 (AFTER blx) printing r0(err)+r3+request id — one staged run
  names the failing GUI request + err code. Then RE 0xFF2B19C0's switch (ids 0x39-0x43 =
  GUI start/display state ops) to see the dependency (display drv / prop / GUI state) and
  feed it. The GUI code path itself is fully alive — this is a dependency bug, not mechanism.
- Scripts: ml_assertcatch2.gdb, ml_guicatch.gdb (bp+force-call single-session pattern works;
  the sentinel/hijack never needs to return when the bp catches mid-flight).

## BREAKTHROUGH 10 (2026-11-01, offline session 11): REAL-HW GROUND TRUTH + flash-test staged

- Camera card logs (log000-010.log on card root, Oct 31 sessions) = REAL DryOS console boots.
  log007 (181KB) = full healthy boot: all 6 sequencer steps dispatch + "seqEventDispatch
  (Startup) : End". THE reference timeline for emu diffing.
- REAL-HW FACTS vs emulation:
  * "[AUDIO] WARN AudioICSerialCommand:Wait sem check 7" HAPPENS ON HW TOO — once (~305s),
    then boot proceeds (spsActivate 0.5s later). NOT a blocker on hw; emu's infinite retry
    = the audio RPC never answers (sems created by audio init 0xFF11C378→0xFF2AD4EC which
    emu's audio init path skips). Emu fix: make the audio RPC eventually answer.
  * step3→4 gap on hw = 835s, mostly SD catalog scan (FC_OpenCatalog @1109s after
    SD capacity/access-mode). FC_OpenCatalog NEVER APPEARS in QEMU logs — and QEMU sd.img
    DCIM is EMPTY (camera card: 100CANON, 523 files). Experiment: mcopy a few DCIM JPGs
    onto sd.img → does the catalog phase run/complete (0x20000 poster candidate)?
  * GUI MainEventHandler props fire DURING step 4 (after DP_Initialize, INDEV, HDMI CEC,
    H264E, LVDS inits) — the QEMU step-5 GUI assert = missing step-4 content, consistent.
  * hw step5 = startupInitializeComplete → bind CBRs → seq End.
- FLASH-TEST STAGED on camera card (done 2026-11-01 21:18, card unmounted, safe to pull):
  autoexec.bin = OUR build (439,840B, RESTARTSTART=0x82900, sig 0xBA19C526);
  autoexec.2018-nightly.bak = original 2018 nightly (restore = rename back).
  ML/ dir on card = 2018 nightly's (modules won't match our core — first test doesn't
  need modules; if ML core loads, banner says "Magic Lantern 2026-10-31.1200D.102").
  Card is boot-flagged. LOOK FOR: normal Canon boot + ML version on screen; if it hangs
  early (black screen / no ML), restore .bak. Logs land in ML/LOGS + root logNNN.log.
- NEXT: (1) Stathis boots the card, reports; (2) stage DCIM subset on QEMU sd.img for the
  catalog experiment; (3) audio-RPC answer emu; (4) optional: boot .bak + dm-spy MPU
  capture if the 2018 nightly's Debug tab exposes it (spells ground truth).

## SESSION 12 (2026-11-02): FLASH TEST 1 FAILED (solid red) → blink-trace bisect build staged

- RESULT: Stathis booted the staged card → SOLID red LED, camera dead; camera boots fine
  with card OUT (no brick; bootflag alone harmless). Solid red at power-on = hang in the
  bootloader/autoexec stage, BEFORE ML's own crash-blink codes exist.
- Offline diff (no smoking gun): fork boot-d45.c vs 2018 boot-hack.c — 2018 patches
  HIJACK_INSTR_BL_CSTART=RET, RETURNS from reloc_entry(), installs task_dispatch_hook,
  then calls ram_cstart(); fork jumps reloc_entry() and never returns. QEMU validated the
  fork flow through the REAL bootloader ROM, and sync_caches impls are equivalent →
  not prime suspects. Top hw-only candidates: corrupted card write (flaky reader!) or
  real-cache behavior QEMU doesn't model. Decided: empirical bisect over theorizing.
- Blink-trace build (READY, awaiting test 2): ML_BOOT_TRACE 1 in platform internals.h
  (reaches all TUs via dryos.h→config-defines.h); helper+macro at TOP of src/boot.h
  (my_init_task is defined there — helper in boot-d45.c is invisible to it, first attempt).
  CK1 entry / CK2 bss / CK3 fw copy / CK4 patches / CK5 cache sync (copy_and_restart);
  CK6 init-hook / CK7 pre-Canon-init_task (my_init_task). Raw LED 0xC0220134 = 0x46/0x44.
  Readout: LAST blink-group count after power-on; 0 = code never ran (bootloader/load:
  md5 card copy + boot .bak control test to split corruption vs card/FAT).
- VERIFY pitfall: gcc emits ldr-pool 0xc0220000 + str [r4,#0x134] — grepping disasm for
  "c0220134" finds nothing (false "not compiled in"). Check .o symtab for
  boot_trace_marker, then bl-sites in linked autoexec (7 calls, r0=1..7, @0x8ae98+).
- QEMU regression PASSED for trace build (canonical 4-line console). Artifacts:
  ~/magiclantern/autoexec.staged-20261101.bin = crashed build (md5 25361c0c…, 439,840B,
  card's autoexec.test1.bin must match); ~/magiclantern/autoexec.boot-trace.bin (md5
  a89c1911…, 440,032B) = next flash. Tree edits (internals.h/boot.h/boot-d45.c) marked
  TEMPORARY — revert after first successful hw boot. Stathis to: card→PC (firm reader
  seat), we md5-verify + stage trace build, he counts blinks.

## Rebuild/run cheat sheet

  # simplified-fork ML
  cd ~/magiclantern/platform/1200D.102 && \
    PATH=/usr/bin:$HOME/.local/bin:$PATH make ARM_BINPATH=$HOME/gcc-arm-none-eabi-5.4/bin

  # stage autoexec into QEMU SD image
  mcopy -o -i ~/qemu-eos/sd.img@@50688 \
    ~/magiclantern/platform/1200D.102/build/autoexec.bin ::autoexec.bin

  # run QEMU (console visible)
  cd ~/qemu-eos && PATH=/usr/bin:$PATH timeout 600 stdbuf -oL -eL \
    env QEMU_EOS_DEBUGMSG=0xFF11F5DC qemu-2.5.0/arm-softmmu/qemu-system-arm \
    -display none -d debugmsg,mpu -drive if=sd,format=raw,file=sd.img \
    -drive if=ide,format=raw,file=cf.img -name 1200D -M 1200D,firmware=boot=1

  # console timeline extractor
  /usr/bin/python3 ~/scripts/ml_console.py /tmp/qemu_bootNN.log

  # gdb attach (add -gdb tcp::4444 to QEMU invocation; -S to start frozen)
  timeout 60 ~/gdb-arm-none-eabi-8_1/bin/arm-none-eabi-gdb -batch \
    -x <(echo 'target remote :4444'; echo 'x/8i $pc-12'; echo quit)  # or a cmd file

## BREAKTHROUGH 11 (2026-11-02): HW flash-test root cause FOUND + fixed — stale generated lds
- HW test #1 (staged 11-01 21:18): solid red, no boot. Card copy md5-perfect (not reader corruption), no new logs (died pre-file-IO).
- Blink-trace bisect (ML_BOOT_TRACE=1 in internals.h; boot_trace_marker in boot.h, ML_TRACE_MARKER 1-5 in copy_and_restart, 6/7 in my_init_task): **5 blinks** = our boot chain complete (entry, zero_bss, blob_memcpy, patches, sync_caches), dies INSIDE relocated firmware entry.
- ROOT CAUSE: fork autoexec.bin is TWO-STAGE: loader @0x800000 (reboot.c: polyglot stub + xor self-checksum + sig check → **turns card LED ON** ← the solid red) embeds main binary via .incbin magiclantern.bin, blob_memcpy's it to RESTARTSTART (0x82900) and jumps. The main binary is linked via build/magiclantern.lds (preprocessed from src/magiclantern.lds.S with -DRESTARTSTART). **The make rule for the lds depends ONLY on the .S file — NOT on the RESTARTSTART value** → the 11-01 Makefile fix (0xC80100→0x82900) never reached the lds; build/magiclantern.lds stayed `. = 0xC80100` through every relink incl. the staged build and 11-02 trace builds.
- Failure mechanics on hw: blob linked 0xC80100 but running at 0x82900 (Δ0x45D800): zero_bss cleared the WRONG region ([0xCEA9C0,0xCFCF00) — real BSS left as loader garbage), and copy_and_restart patched HIJACK_INSTR_BSS_END with _bss_end=0xCFCF00 > fixed kernel boundary 0x103958 → inverted malloc pool → kernel dies right after we hand off (exactly "5 blinks then hang"). QEMU tolerated it (booted K327) — memory-map leniency; hw did not.
- FIX: `touch src/magiclantern.lds.S` (forces lds regen with current -DRESTARTSTART=0x82900) + rebuild. Verified: readelf .text=0x82900, location.map _bss_end=0xFF700 < 0x103958 ✓, QEMU boots (4-line canonical). Artifact: ~/magiclantern/autoexec.lds-fix.bin (md5 703b16a8416ab684eba1ebdad3deea28, 440032B). The fragile make rule is NOT fixed upstream — after any RESTARTSTART change, ALWAYS `touch src/magiclantern.lds.S` and verify `readelf -S build/magiclantern | grep .text` == RESTARTSTART.
- Blink-trace still enabled (internals.h ML_BOOT_TRACE) for the next flash test; remove after first successful hw boot. Card state: autoexec.bin=trace build (stale-base), autoexec.test1.bin=hw-crashed build, autoexec.2018-nightly.bak=restore.

## BREAKTHROUGH 12 (2026-11-02 PM): the REAL design = AllocateMemory pool boot (boot-d45-am)
- Flash test #2 (lds-fixed 0x82900 build): **7 blinks**, display blank → full ML boot chain + firmware handoff OK; dies inside Canon's startup cascade after init_task.
- Diagnosis: with plain boot-d45.o + RESTARTSTART 0x82900, HIJACK_INSTR_BSS_END patches the MALLOC pool start to our _bss_end (0xFF700) → Canon's pool [bss_end, 0x103958) shrinks to 17KB. Canon's native pool = [0x82884, 0x103958) ≈ 529KB; the 2018 nightly (image [0x82900, 0x86c5e0) ≈ 302KB) steals only 16KB. Fork full build span = 511KB → cannot fit the malloc pool on this camera, period.
- THE FIX (the ORIGINAL port config, mis-"fixeded" on 11-01): internals.h already had CONFIG_ALLOCATE_MEMORY_POOL + consts.h had all of mk11174's RE'd AM-boot addresses — but ML_BOOT_OBJ was boot-d45.o (plain). Correct: **ML_BOOT_OBJ = boot-d45-am.o + RESTARTSTART = 0x00C80100**. AM design: AllocateMemory pool = [0x3D0000, 0xD00000) (ROM-verified: mov r1,#0xd00000 / mov r0,#0x3d0000 / bl allocatememory_init @ 0xFF0C2430-38; b CreateTaskMain @ 0xFF0C96BC); boot-d45-am relocates init_task+CreateTaskMain, patches pool end 0xD00000→0xC80000, ML lives in [0xC80100, 0xD00000) = 512KB; malloc pool UNTOUCHED. The 11-01 QEMU failure (inverted pool, CreateMessageQueue 5) was caused by plain-boot-d45 + 0xC80100 (patches malloc pool start to ~0xD1xxxx = nonsense); changing RESTARTSTART to 0x82900 made the WRONG design's math work instead of switching the boot file.
- Verified: .text @ 0xC80100, _bss_end = 0xCFD500 < 0xD00000 (11KB margin), boot-d45-am.o linked, QEMU boots through the relocated+patched init (console version print = cascade ran; sanity checks passed). Artifact: ~/magiclantern/autoexec.am-boot.bin (md5 6d4b8ea6fb22843ae06320d4209bc45e, 440288B). Markers now CK1-8 (8 = AM pool patch applied).
- If flash test #3 boots: remove ML_BOOT_TRACE from internals.h; then commit (Stathis's call). Fork full build fits the AM window but only just (11KB) — future feature growth needs either the 6D/70D-style R0 patch (bigger window at pool start side... actually R0 patch reserves from 0x44C000→0x4E0000 = the BOTTOM side) or slimming.

## FLASH TEST #3 SUCCESS (2026-11-02): 1200D PORT BOOTS ON REAL HARDWARE
- AM-boot build (md5 6d4b8ea6fb22843ae06320d4209bc45e): camera boots, ML loads and works (menu via TRASH, banner 2026-11-02.1200D.102). First hardware boot of the simplified-fork port.
- Card state: autoexec.bin = AM build; autoexec.2018-nightly.bak + autoexec.test1.bin = restore points.
- REMAINING (ordered): (1) strip ML_BOOT_TRACE from internals.h + rebuild + reflash (markers add ~10s boot delay); (2) commit — Stathis's call; tree has AM fix + trace markers + earlier uncommitted fixes; (3) build 2026-era modules (card has 2018-era, won't load); (4) the RE gap list: 196 stubs, get_task_info_by_id, EDMAC channels, TG_FREQ verify, RAW pipeline re-enable.
