/**
 * 1200D (T5) CMOS/ADTG register logger — crop_rec ground-truth instrument.
 *
 * v8 (2026-11-06 night): capture via patch_hook_function (fork-native,
 * single-point cache patch, production mechanism) — the gdb watchpoint
 * pair (v3-v7) wedged the camera to a black screen on the first real mode
 * transition (re-arm race under burst traffic; 720p switch, LED off,
 * logger-off 720p switch works fine).
 *
 * Writers (ROM-verified 1.0.2, Device\SIO\Sio0Driver.c cluster):
 *   ADTG_WRITE_FUNC = 0xFF3A246C  r0 = chip-select mask (bits 0-4),
 *       r1 = u32 list ptr, 0xFFFFFFFF terminator (ldr r0,[r6] @0xFF3A2514,
 *       add r6,#4, cmn r0,#1). Word = (reg<<16)|val; families <0x1000 and
 *       0x8000-0x8FFF.
 *   CMOS_WRITE_FUNC = 0xFF3A265C  r0 = u16 list ptr, 0xFFFF terminator
 *       (ldrh walk). Word = (reg<<12)|val, regs 0-7.
 *
 * Raw logging (no on-camera dedup — replay jitter analysis moved offline):
 * markers 0xFFFFFFFF/0x00FF0000/0xFFFF0000 as before. Buffer 128K entries;
 * on overflow the hook drops and flags; the task writes part files
 * DCIM/100CANON/adtg_pNNN.bin and continues — capture length unbounded.
 * Second select stops + saves; 30 s writer silence stops + saves.
 */

#define ADTG_WRITE_FUNC   0xFF3A246C
#define ADTG_WRITE_INSTR  0xE92D47F0   /* push {r4-r9,lr} — sanity check */
#define CMOS_WRITE_FUNC   0xFF3A265C
#define CMOS_WRITE_INSTR  0xE92D41F0   /* push {r4-r7,lr} */

#include <module.h>
#include <dryos.h>
#include <property.h>
#include <bmp.h>
#include <menu.h>
#include <config.h>
#include <beep.h>
#include <patch.h>

unsigned int *adtg_buf = NULL;
unsigned int adtg_buf_pos = 0;
unsigned int adtg_buf_pos_max = 0;

static volatile uint32_t adtg_running = 0;
static volatile uint32_t adtg_stop = 0;
static volatile uint32_t adtg_overflow = 0;
static volatile uint32_t adtg_last_activity = 0;
static unsigned int adtg_part = 0;

unsigned short cmos_regs[8];

static void cmos_reg_update(unsigned short cmos_data)
{
    unsigned short reg = (cmos_data >> 12) & 0x07;
    unsigned short data = cmos_data & 0x0FFF;
    cmos_regs[reg] = data;
}

/* hook context: regs[0..3] = r0..r3, pc = hook address */
static void adtg_hook(uint32_t *regs, uint32_t *stack, uint32_t pc)
{
    (void)stack; (void)pc;
    if(!adtg_buf) return;

    uint32_t* data_buf = (uint32_t*) regs[1];   /* list ptr (r1) */
    adtg_last_activity = get_ms_clock();

    if(adtg_buf_pos + 4 >= adtg_buf_pos_max)
    {
        adtg_overflow = 1;
        return;
    }

    adtg_buf[adtg_buf_pos++] = 0xFFFFFFFF;

    while(*data_buf != 0xFFFFFFFF && adtg_buf_pos + 2 < adtg_buf_pos_max)
    {
        adtg_buf[adtg_buf_pos++] = *(data_buf++);
    }

    adtg_buf[adtg_buf_pos++] = 0xFFFFFFFF;

    if(adtg_buf_pos + 4 >= adtg_buf_pos_max)
    {
        adtg_overflow = 1;
    }
}

static void cmos_hook(uint32_t *regs, uint32_t *stack, uint32_t pc)
{
    (void)stack; (void)pc;
    if(!adtg_buf) return;

    unsigned short* data_buf = (unsigned short*) regs[0];  /* list ptr (r0) */
    adtg_last_activity = get_ms_clock();

    if(adtg_buf_pos + 4 >= adtg_buf_pos_max)
    {
        adtg_overflow = 1;
        return;
    }

    adtg_buf[adtg_buf_pos++] = 0x00FF0000;

    while(*data_buf != 0xFFFF && adtg_buf_pos + 2 < adtg_buf_pos_max)
    {
        cmos_reg_update(*data_buf);
        adtg_buf[adtg_buf_pos++] = *(data_buf++);
    }

    adtg_buf[adtg_buf_pos++] = 0xFFFF0000;

    if(adtg_buf_pos + 4 >= adtg_buf_pos_max)
    {
        adtg_overflow = 1;
    }
}

static void adtg_write_part(void)
{
    char filename[100];
    snprintf(filename, sizeof(filename), "%s/adtg_p%03d.bin", get_dcim_dir(), ++adtg_part);
    FILE* f = FIO_CreateFile(filename);
    if(f)
    {
        FIO_WriteFile(f, adtg_buf, adtg_buf_pos * 4);
        FIO_CloseFile(f);
    }
    adtg_buf_pos = 0;
}

static void adtg_log_task(void *unused, int unused2)
{
    if(adtg_running)
    {
        return;
    }
    adtg_running = 1;
    adtg_stop = 0;
    adtg_overflow = 0;
    adtg_part = 0;

    adtg_buf_pos_max = 128 * 1024;

    adtg_buf = malloc(adtg_buf_pos_max * 4 + 0x100);

    if(!adtg_buf)
    {
        NotifyBox(2000, "malloc failed");
        adtg_running = 0;
        return;
    }

    adtg_last_activity = get_ms_clock();

    /* hook the two writers via the fork-native patch system */
    int h1 = patch_hook_function(ADTG_WRITE_FUNC, ADTG_WRITE_INSTR, adtg_hook, "adtg_log");
    int h2 = patch_hook_function(CMOS_WRITE_FUNC, CMOS_WRITE_INSTR, cmos_hook, "adtg_cmos");
    if(h1 != E_PATCH_OK || h2 != E_PATCH_OK)
    {
        NotifyBox(2000, "hook setup failed (%d,%d)", h1, h2);
        if(h1 == E_PATCH_OK) unpatch_memory(ADTG_WRITE_FUNC);
        if(h2 == E_PATCH_OK) unpatch_memory(CMOS_WRITE_FUNC);
        free(adtg_buf);
        adtg_buf = NULL;
        adtg_running = 0;
        return;
    }

    /* run until: manual stop, or 30 s of writer silence */
    while(!adtg_stop)
    {
        uint32_t fill_pct = adtg_buf_pos_max ? (adtg_buf_pos * 100 / adtg_buf_pos_max) : 0;
        bmp_printf(FONT_MED, 10, 20, "cmos: p%u buf %3u%% SET=stop", adtg_part + 1, fill_pct);
        for(int pos = 0; pos < 8; pos++)
        {
            bmp_printf(FONT_MED, 10, 40 + pos * font_med.height, "[%d] 0x%03X ", pos, cmos_regs[pos]);
        }

        if(adtg_overflow)
        {
            adtg_overflow = 0;
            adtg_write_part();
        }

        if(get_ms_clock() - adtg_last_activity > 30000)
        {
            break;
        }
        msleep(100);
    }

    beep();

    /* remove hooks BEFORE writing the final part */
    unpatch_memory(ADTG_WRITE_FUNC);
    unpatch_memory(CMOS_WRITE_FUNC);
    msleep(50);

    if(adtg_buf_pos)
    {
        adtg_write_part();
    }

    /* free buffer */
    void *buf = adtg_buf;
    adtg_buf = NULL;
    free(buf);

    adtg_running = 0;
    NotifyBox(2000, "ADTG dump: %d part(s) saved", adtg_part);
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
        .help = "Log CMOS/ADTG writes to DCIM/adtg_pNNN.bin. Select again to stop+save.",
        .depends_on = DEP_LIVEVIEW,
    },
};

static unsigned int adtg_log_init()
{
    menu_add("Debug", adtg_log_menu, COUNT(adtg_log_menu));
    return 0;
}

static unsigned int adtg_log_deinit()
{
    if(adtg_running)
    {
        adtg_stop = 1;
        msleep(200);
    }
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
