/**
 * 1200D (T5) CMOS/ADTG register logger — crop_rec ground-truth instrument.
 * Port of the 2018-tree adtg_log (5D3) to 1200D 1.0.2.
 *
 * Addresses statically derived from ROM (Device\SIO\Sio0Driver.c cluster):
 *   ADTG_WRITE_FUNC = 0xFF3A246C  push {r4-sl,lr}; walks uint32 list
 *       (terminator 0xFFFFFFFF), 4 chip-select stores ([drv+24/28/32/36]),
 *       prints "[REG] ADTG:[0x%08x]".
 *   CMOS_WRITE_FUNC = 0xFF3A265C  push {r4-r8,lr}; walks uint16 list
 *       ((reg<<12)|val), ENGIO preamble + LED strobe, prints
 *       "[REG] CMOS:[0x%04x]". (No separate 5D3-style CMOS16 fn found.)
 *
 * Debug menu -> "ADTG/CMOS log (1200D)": hardware watchpoints on both
 * writers, live CMOS values on screen, full dump to DCIM/adtg.bin when
 * the buffer fills. Toggle LV modes (1080p/720p/zoom x5/x10) while running.
 * Watchpoints only — if CMOS regs don't animate in LV, addresses are wrong
 * and nothing else was touched.
 */

#define ADTG_WRITE_FUNC   0xFF3A246C
#define CMOS_WRITE_FUNC   0xFF3A265C

#include <module.h>
#include <dryos.h>
#include <property.h>
#include <bmp.h>
#include <menu.h>
#include <config.h>
#include <beep.h>
#include <gdb.h>

unsigned int *adtg_buf = NULL;
unsigned int adtg_buf_pos = 0;
unsigned int adtg_buf_pos_max = 0;

static volatile uint32_t adtg_running = 0;
static volatile uint32_t adtg_stop = 0;

/* change-only logging with jitter threshold: adjacent LV replays differ by
 * 2-6 regs (measured run3: median 4, max 6) — flap regs 0x1D4/0x4F00/0x174...
 * A real mode/state change rewrites 100+ regs (measured: 202). Threshold 8
 * sits between. Exact-match dedup (v4) never triggered because of jitter. */
static unsigned short adtg_prev_val[4096];
static unsigned char adtg_prev_present[4096];
static unsigned int adtg_logged = 0;
static volatile unsigned int adtg_last_activity = 0;

static unsigned int cmos_last[16];
static unsigned int cmos_last_len = 0;

unsigned short cmos_regs[8];

static void cmos_reg_update(unsigned short cmos_data)
{
    unsigned short reg = (cmos_data >> 12) & 0x07;
    unsigned short data = cmos_data & 0x0FFF;
    cmos_regs[reg] = data;
}

static void adtg_log(breakpoint_t *bkpt)
{
    uint32_t *regs = bkpt->ctx;   /* r0..r15, cpsr */

    if(!adtg_buf)
    {
        return;
    }

    /* r0-at-entry = chip-select mask (bits 0-4, tst r4,#1/2/4/8/16),
     * r1-at-entry = POINTER to u32 word list, 0xFFFFFFFF terminator
     * (ROM: ldr r0,[r6] @0xFF3A2514, add r6,#4 @0xFF3A259C, cmn r0,#1
     * @0xFF3A25A4). Walking r0 was the run2-4 bug: mask (e.g. 0x1F) used
     * as a pointer -> read a low-address code mirror -> 982 words of
     * halfword-swapped ARM instructions as "register data". */
    uint32_t* data_buf = (uint32_t*) regs[1];

    if(adtg_buf_pos + 4 >= adtg_buf_pos_max) return;

    unsigned int start = adtg_buf_pos;
    adtg_buf[adtg_buf_pos++] = 0xFFFFFFFF;

    while(*data_buf != 0xFFFFFFFF && adtg_buf_pos + 2 < adtg_buf_pos_max)
    {
        adtg_buf[adtg_buf_pos++] = *(data_buf++);
    }

    adtg_buf[adtg_buf_pos++] = 0xFFFFFFFF;

    /* jitter-threshold dedup: count regs whose value changed vs previous
     * table; <= 8 = replay jitter -> drop, > 8 = real change -> keep */
    unsigned int changed = 0;
    for(unsigned int p = start + 1; p < adtg_buf_pos - 1; p++)
    {
        unsigned int r = adtg_buf[p] >> 16;
        if(r >= 4096)
        {
            changed++;
        }
        else if(!adtg_prev_present[r] || adtg_prev_val[r] != (adtg_buf[p] & 0xFFFF))
        {
            changed++;
        }
    }

    if(changed <= 8)
    {
        adtg_buf_pos = start;
    }
    else
    {
        for(unsigned int p = start + 1; p < adtg_buf_pos - 1; p++)
        {
            unsigned int r = adtg_buf[p] >> 16;
            if(r < 4096)
            {
                adtg_prev_val[r] = adtg_buf[p] & 0xFFFF;
                adtg_prev_present[r] = 1;
            }
        }
        adtg_logged++;
    }
    adtg_last_activity = get_ms_clock();
}

static void cmos_log(breakpoint_t *bkpt)
{
    uint32_t *regs = bkpt->ctx;   /* r0..r15, cpsr */

    if(!adtg_buf)
    {
        return;
    }

    unsigned short* data_buf = (unsigned short*) regs[0];

    if(adtg_buf_pos + 4 >= adtg_buf_pos_max) return;

    unsigned int start = adtg_buf_pos;
    adtg_buf[adtg_buf_pos++] = 0x00FF0000;

    while(*data_buf != 0xFFFF && adtg_buf_pos + 2 < adtg_buf_pos_max)
    {
        cmos_reg_update(*data_buf);
        adtg_buf[adtg_buf_pos++] = *(data_buf++);
    }

    adtg_buf[adtg_buf_pos++] = 0xFFFF0000;

    /* change-only: drop burst if identical to the last logged one */
    unsigned int n = adtg_buf_pos - start - 2;
    unsigned int same = (n == cmos_last_len && n <= 16);
    if(same)
    {
        for(unsigned int i = 0; i < n; i++)
        {
            if(adtg_buf[start + 1 + i] != cmos_last[i]) { same = 0; break; }
        }
    }
    if(same)
    {
        adtg_buf_pos = start;
    }
    else
    {
        if(n <= 16)
        {
            for(unsigned int i = 0; i < n; i++) cmos_last[i] = adtg_buf[start + 1 + i];
            cmos_last_len = n;
        }
        adtg_logged++;
    }
    adtg_last_activity = get_ms_clock();
}

static void adtg_log_task(void *unused, int unused2)
{
    if(adtg_running)
    {
        return;
    }
    adtg_running = 1;
    adtg_stop = 0;
    cmos_last_len = 0;
    adtg_logged = 0;
    for(unsigned int i = 0; i < 4096; i++) adtg_prev_present[i] = 0;
    adtg_last_activity = get_ms_clock();

    adtg_buf_pos_max = 128 * 1024;

    adtg_buf = malloc(adtg_buf_pos_max * 4 + 0x100);

    if(!adtg_buf)
    {
        NotifyBox(2000, "malloc failed");
        adtg_running = 0;
        return;
    }

    /* initialize gdb engine first — without gdb_setup() gdb_installed stays 0
     * and every gdb_add_watchpoint returns NULL ("watchpoint setup failed",
     * live cmos stays 0 — callbacks never fire). 2018 adtg_log.c:195 parity. */
    if(!gdb_setup())
    {
        NotifyBox(2000, "gdb setup failed");
        free(adtg_buf);
        adtg_buf = NULL;
        adtg_running = 0;
        return;
    }

    /* set watchpoints at ADTG and CMOS writes */
    breakpoint_t *bkpt1 = gdb_add_watchpoint(ADTG_WRITE_FUNC, 0, adtg_log);
    breakpoint_t *bkpt2 = gdb_add_watchpoint(CMOS_WRITE_FUNC, 0, cmos_log);
    if(!bkpt1 || !bkpt2)
    {
        NotifyBox(2000, "watchpoint setup failed");
    }

    /* wait for real changes; exit paths: buffer full, second select
     * (adtg_stop), or 30 s with no writer activity at all */
    while(adtg_buf_pos + 2 < adtg_buf_pos_max && !adtg_stop)
    {
        uint32_t fill_pct = adtg_buf_pos_max ? (adtg_buf_pos * 100 / adtg_buf_pos_max) : 0;
        bmp_printf(FONT_MED, 10, 20, "cmos: new=%u buf %3u%% SET=stop", adtg_logged, fill_pct);
        for(int pos = 0; pos < 8; pos++)
        {
            bmp_printf(FONT_MED, 10, 40 + pos * font_med.height, "[%d] 0x%03X ", pos, cmos_regs[pos]);
        }
        if(get_ms_clock() - adtg_last_activity > 30000)
        {
            break;
        }
        msleep(100);
    }

    beep();

    /* uninstall watchpoints */
    gdb_delete_bkpt(bkpt1);
    gdb_delete_bkpt(bkpt2);

    /* dump all stuff */
    char filename[100];
    snprintf(filename, sizeof(filename), "%s/adtg.bin", get_dcim_dir());
    FILE* f = FIO_CreateFile(filename);
    FIO_WriteFile(f, adtg_buf, adtg_buf_pos * 4);
    FIO_CloseFile(f);

    /* free buffer */
    void *buf = adtg_buf;
    adtg_buf = NULL;
    free(buf);

    adtg_running = 0;
    NotifyBox(2000, "ADTG dump saved: DCIM/adtg.bin");
}

static void adtg_log_select(void *priv, int argument)
{
    if(adtg_running)
    {
        /* second select while running: stop loop and save what we have */
        adtg_stop = 1;
        NotifyBox(2000, "stopping + saving...");
        return;
    }
    run_in_separate_task(priv, argument);
}

static struct menu_entry adtg_log_menu[] =
{
    {
        .name = "ADTG/CMOS log (1200D)",
        .select = adtg_log_select,
        .priv = adtg_log_task,
        .help = "Log CMOS/ADTG writes to DCIM/adtg.bin. Select again to stop+save.",
        .depends_on = DEP_LIVEVIEW,
    },
};

static unsigned int adtg_log_init()
{
    /* CONFIG_1200D-only module (modules.included); guard removed so the
       standalone module make (no platform defines) still references the menu */
    menu_add("Debug", adtg_log_menu, COUNT(adtg_log_menu));
    return 0;
}

static unsigned int adtg_log_deinit()
{
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(adtg_log_init)
    MODULE_DEINIT(adtg_log_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
MODULE_CBRS_END()

MODULE_PROPHANDLERS_START()
MODULE_PROPHANDLERS_END()
