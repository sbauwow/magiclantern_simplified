MODEL = 1200D
FW_VERSION = 102

TOP_DIR = ../..

AUTOEXEC_BASE = 0x800000
FIR_BASE = 0x800120

CONFIG_LVAPP_HACK_RELOC = y

ML_MODULES_SYM_NAME = t5_$(FW_VERSION).sym

MAIN_FIRMWARE_ADDR = 0xFF0C0000
PLATFORM_ARCH = armv5te

# DryOS memory map — ALLOCATE MEMORY POOL design (mk11174's original port config,
# restored 2026-11-02). ML lives in the top 512K of the AllocateMemory pool:
# pool = [0x3D0000, 0xD00000), boot-d45-am patches end 0xD00000 -> 0xC80000,
# ML spans [0xC80100, ~0xCFCF00) — malloc pool [0x82884, 0x103958) stays UNTOUCHED.
# (The 11-01 change to 0x82900 + boot-d45.o was wrong: with the plain boot file it
# shrank Canon's malloc pool to 17K and hw hung after init_task — see HANDOFF BT11/12.)
RESTARTSTART = 0x00C80100
ML_BOOT_OBJ = boot-d45-am.o

FIRMWARE_ID_1200D = 0x80000327

include ../Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/cam_complete
