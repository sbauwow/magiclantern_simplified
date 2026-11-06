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

    uint32_t* data_buf = (uint32_t*) regs[0];

    if(adtg_buf_pos + 4 >= adtg_buf_pos_max) return;

    adtg_buf[adtg_buf_pos++] = 0xFFFFFFFF;

    while(*data_buf != 0xFFFFFFFF && adtg_buf_pos + 2 < adtg_buf_pos_max)
    {
        adtg_buf[adtg_buf_pos++] = *(data_buf++);
    }

    adtg_buf[adtg_buf_pos++] = 0xFFFFFFFF;
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

    adtg_buf[adtg_buf_pos++] = 0x00FF0000;

    while(*data_buf != 0xFFFF && adtg_buf_pos + 2 < adtg_buf_pos_max)
    {
        cmos_reg_update(*data_buf);
        adtg_buf[adtg_buf_pos++] = *(data_buf++);
    }

    adtg_buf[adtg_buf_pos++] = 0xFFFF0000;
}

static void adtg_log_task(void *unused, int unused2)
{
    if(adtg_running)
    {
        return;
    }
    adtg_running = 1;
    adtg_stop = 0;

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

    /* wait for buffer being filled; exit paths: buffer full, second select
     * (adtg_stop), or 30 s with no new writes (idle — dump what we have) */
    uint32_t last_pos = 0;
    uint32_t last_change = get_ms_clock();
    while(adtg_buf_pos + 2 < adtg_buf_pos_max && !adtg_stop)
    {
        uint32_t fill_pct = adtg_buf_pos_max ? (adtg_buf_pos * 100 / adtg_buf_pos_max) : 0;
        bmp_printf(FONT_MED, 10, 20, "cmos: buf %3u%%  SET=stop ", fill_pct);
        for(int pos = 0; pos < 8; pos++)
        {
            bmp_printf(FONT_MED, 10, 40 + pos * font_med.height, "[%d] 0x%03X ", pos, cmos_regs[pos]);
        }
        uint32_t now = get_ms_clock();
        if(adtg_buf_pos != last_pos)
        {
            last_pos = adtg_buf_pos;
            last_change = now;
        }
        else if(now - last_change > 30000)
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
