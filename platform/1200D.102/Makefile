MODEL = 1200D
FW_VERSION = 102

TOP_DIR = ../..

AUTOEXEC_BASE = 0x800000
FIR_BASE = 0x800120

CONFIG_LVAPP_HACK_RELOC = y

# LAPIS codec (600D family, ROM-verified 2026-11-02) — not the AK default
ML_AUDIO_OBJ = audio-lapis.o

ML_MODULES_SYM_NAME = t5_$(FW_VERSION).sym

MAIN_FIRMWARE_ADDR = 0xFF0C0000
PLATFORM_ARCH = armv5te

# DryOS memory map — ALLOCATE MEMORY POOL boot, BOTTOM-RESERVE variant (2026-11-02 v3):
# pool = [0x3D0000, 0xD00000) (ROM: mov r1,#0xd00000 @0xFF0C2430, mov r0,#0x3d0000 @0xFF0C2434,
# bl allocatememory_init @0xFF0C2438; b CreateTaskMain @0xFF0C96BC). boot-d45-am patches the
# pool START 0x3D0000 -> 0x4E0000 (6D/70D pattern); ML lives in [0x3D0000, 0x4E0000) = 1.1M;
# Canon keeps [0x4E0000, 0xD00000) = 8.3M; malloc pool [0x82884, 0x103958) untouched.
# (v1/v2 history: top-carve end->0xC80000 with RESTARTSTART=0xC80100 left only 512K,
# ~5K free after audio — see HANDOFF BT11-13.)
RESTARTSTART = 0x003D0000
ML_BOOT_OBJ = boot-d45-am.o

FIRMWARE_ID_1200D = 0x80000327

include ../Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/cam_complete
