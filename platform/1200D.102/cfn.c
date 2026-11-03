#include <dryos.h>
#include <property.h>
#include <cfn-generic.h>
#include <menu.h>
#include <bmp.h>
#include <mem.h>
#include <stdio.h>

// look on camera menu or review sites to get custom function numbers

int get_htp() { return GetCFnData(0, 5); }
void set_htp(int value) { SetCFnData(0, 5, value); }

// No MLU on the 1200D :(
int get_mlu() { return 0; }
void set_mlu(int value) { return; }

/* unsure - from https://www.manualslib.com/manual/1147171/Canon-Eos-1200d.html?page=257 */
int cfn_get_af_button_assignment() { return GetCFnData(0, 8); }
void cfn_set_af_button(int value) { SetCFnData(0, 8, value); }

GENERIC_GET_ALO

/* 1200D: get_task_info_by_id not RE'd yet — safe stub, task menu shows "?" */
int get_task_info_by_id(int a, int b, void *c) { (void)a; (void)b; (void)c; return -1; }

/* 1200D photo-raw diagnostics v2: the photo-write EDMAC is reassigned before
 * image review (dump v1: chan 0x4200 held a 2112B/line display transfer, rest
 * zero). Monitor the write-channel window at 20 Hz and log every state/buffer
 * change — catches the ~32MB photo buffer (yb_xb ~ 0x2370) mid-develop.
 * True D4 per-channel layout (edmac.h): +0 state, +4 flags, +8 ram_addr,
 * +C yn_xn, +10 yb_xb. Channels stride 0x50 from 0xC0F04050 (ROM table 0xFF6A32xx). */
static int photo_edmac_monitor_enabled = 0;

static void photo_edmac_monitor_task(void* unused)
{
    (void)unused;
    FILE* f = FIO_CreateFile("ML/LOGS/EDMACMon.TXT");
    if (!f) return;
    static uint32_t last[8][3];
    uint32_t bases[8] = { 0xC0F04050, 0xC0F04100, 0xC0F04150, 0xC0F04200,
                          0xC0F04250, 0xC0F04300, 0xC0F04350, 0xC0F04400 };
    for (uint32_t iter = 0; iter < 600 && photo_edmac_monitor_enabled; iter++) /* 30s */
    {
        for (int i = 0; i < 8; i++)
        {
            uint32_t st = shamem_read(bases[i]);
            uint32_t ra = shamem_read(bases[i] + 8);
            uint32_t xb = shamem_read(bases[i] + 0x10);
            if (st != last[i][0] || ra != last[i][1] || xb != last[i][2])
            {
                last[i][0] = st; last[i][1] = ra; last[i][2] = xb;
                uint32_t yn = shamem_read(bases[i] + 0xC);
                char line[112];
                int len = snprintf(line, sizeof(line),
                    "%08x st=%d ram=%08x yn=%5d xb=%5d (0x%x)\r\n",
                    bases[i], st, ra, yn, xb, xb);
                FIO_WriteFile(f, line, len);
            }
        }
        msleep(50);
    }
    FIO_CloseFile(f);
}

static void photo_edmac_monitor_sel(void* priv, int delta)
{
    (void)priv; (void)delta;
    photo_edmac_monitor_enabled = !photo_edmac_monitor_enabled;
    if (photo_edmac_monitor_enabled)
    {
        NotifyBox(2000, "EDMAC monitor ON (30s) - take a RAW photo now");
        run_in_separate_task(photo_edmac_monitor_task, 0);
    }
    else
    {
        NotifyBox(2000, "EDMAC monitor OFF");
    }
}

static struct menu_entry photo_edmac_menus[] = {
    {
        .name       = "Photo EDMAC monitor (1200D)",
        .select     = photo_edmac_monitor_sel,
        .help       = "Log write-channel changes 30s to ML/LOGS/EDMACMon.TXT — take a RAW photo right after enabling",
    },
};

static void photo_edmac_menu_init() { menu_add("Debug", photo_edmac_menus, COUNT(photo_edmac_menus)); }
INIT_FUNC("photo_edmac.dump", photo_edmac_menu_init);

/* raw_histo_update: real implementation in histogram.c since raw pipeline
 * enabled (2026-11-02) — the old no-op shim removed. */
