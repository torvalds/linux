#ifndef AIC3XXX_CFW_HOST_BLD
#   include <linux/module.h>
#   include <linux/delay.h>
#   define warn(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#   define error(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#else
#   define _GNU_SOURCE
#   include <stdlib.h>
#   include "utils.h"
#   include <string.h>
#endif

#include "aic3xxx_cfw.h"
#include "aic3xxx_cfw_ops.h"


/*
 * Firmware version numbers are used to make sure that the
 * host and target code stay in sync.  It is _not_ recommended
 * to provide this number from the outside (E.g., from a makefile)
 * Instead, a set of automated tools are relied upon to keep the numbers
 * in sync at the time of host testing.
 */
#define CFW_FW_VERSION 0x000100AF


static int aic3xxx_cfw_dlimage(void *pv, cfw_image *pim);
static int aic3xxx_cfw_dlcfg(void *pv, cfw_image *pim);
static void aic3xxx_cfw_dlcmds(void *pv, cfw_block *pb);
cfw_meta_register *aic3xxx_read_meta(cfw_block *pb, int i);
void aic3xxx_wait(void *p, unsigned int reg, u8 mask, u8 data);
static cfw_project *aic3xxx_cfw_unpickle(void *p, int n);

#if defined(AIC3XXX_CFW_HOST_BLD)
// Host test only...
extern const aic3xxx_codec_ops dummy_codec_ops;
extern void *dummy_codec_ops_obj;

void mdelay(int val)
{
    int i;
    for (i=0; i<val*10; i++);
}
void *aic3xxx_cfw_init(void *pcfw, int n)
{
    cfw_state *ps = malloc(sizeof(cfw_state));
    ps->ops = &dummy_codec_ops;
    ps->ops_obj = dummy_codec_ops_obj;
    aic3xxx_cfw_reload(ps, pcfw, n);
    return ps;
}
void *aic3xxx_cfw_getpjt(void *ps)
{
    return ((cfw_state *)ps)->pjt;
}
// ...till here
#endif

int aic3xxx_cfw_reload(void *pv, void *pcfw, int n)
{
    cfw_state *ps = pv;
    ps->pjt = aic3xxx_cfw_unpickle(pcfw, n);
    ps->cur_mode =
            ps->cur_pfw =
            ps->cur_ovly =
            ps->cur_cfg = -1;
    return 0;
}
int aic3xxx_cfw_setmode(void *pv, int mode)
{
    cfw_state *ps = pv;
    cfw_project *pjt = ps->pjt;
    return aic3xxx_cfw_setmode_cfg(pv, mode, pjt->mode[mode]->cfg);
}
int aic3xxx_cfw_setcfg(void *pv, int cfg)
{
    cfw_state *ps = pv;
    cfw_project *pjt = ps->pjt;
    cfw_pfw *pfw;
    if (ps->cur_pfw >= pjt->npfw)
        return -1; // Non miniDSP
    if (!(pjt->mode[ps->cur_mode]->supported_cfgs&(1 << cfg)))
        return -1;
    if (ps->cur_cfg == cfg)
        return 0;
    pfw = pjt->pfw[ps->cur_pfw];
    ps->cur_cfg = cfg;
    return aic3xxx_cfw_dlcfg(pv, pfw->ovly_cfg[ps->cur_ovly][ps->cur_cfg]);
}
int aic3xxx_cfw_setmode_cfg(void *pv, int mode, int cfg)
{
    cfw_state *ps = pv;
    cfw_project *pjt = ps->pjt;
    int which = 0;

    if (mode >= pjt->nmode)
        return -1;
    if (pjt->mode[mode]->pfw < pjt->npfw) {   // New mode uses miniDSP
        // FIXME: Add support for entry and exit sequences
        cfw_pfw *pfw = pjt->pfw[pjt->mode[mode]->pfw];
        // Make sure cfg is valid and supported in this mode
        if (cfg >= pfw->ncfg ||
                !(pjt->mode[mode]->supported_cfgs&(1u<<cfg)))
            return -1;
        /*
         * Decisions about which miniDSP to stop/restart are taken
         * on the basis of sections present in the _base_ image
         * This allows for correct sync mode operation even in cases
         * where the base PFW uses both miniDSPs where a particular
         * overlay applies only to one
         */
        cfw_image *im = pfw->base;
        if (im->block[CFW_BLOCK_A_INST])
            which |= AIC3XX_COPS_MDSP_A;
        if (im->block[CFW_BLOCK_D_INST])
            which |= AIC3XX_COPS_MDSP_D;

        if (pjt->mode[mode]->pfw !=  ps->cur_pfw) { // New mode requires different PFW
            ps->cur_pfw = pjt->mode[mode]->pfw;
            ps->cur_ovly = 0;
            ps->cur_cfg = 0;

            which = ps->ops->stop(ps->ops_obj, which);
            aic3xxx_cfw_dlimage(pv, im);
            if (pjt->mode[mode]->ovly && pjt->mode[mode]->ovly < pfw->novly) {
                // New mode uses ovly
                aic3xxx_cfw_dlimage(pv, pfw->ovly_cfg[pjt->mode[mode]->ovly][cfg]);
            } else if (cfg) {
                // new mode needs only a cfg change
                aic3xxx_cfw_dlimage(pv, pfw->ovly_cfg[0][cfg]);
            }
            ps->ops->restore(ps->ops_obj, which);

        } else if (pjt->mode[mode]->ovly != ps->cur_ovly) {
            // New mode requires only an ovly change
            which = ps->ops->stop(ps->ops_obj, which);
            aic3xxx_cfw_dlimage(pv, pfw->ovly_cfg[pjt->mode[mode]->ovly][cfg]);
            ps->ops->restore(ps->ops_obj, which);
        } else if (cfg != ps->cur_cfg) { // New mode requires only a cfg change
            aic3xxx_cfw_dlcfg(pv, pfw->ovly_cfg[pjt->mode[mode]->ovly][cfg]);
        }
        ps->cur_ovly = pjt->mode[mode]->ovly;
        ps->cur_cfg = cfg;

        // FIXME: Update PLL settings if present
        // FIXME: This is hack and needs to go one way or another
        ps->cur_mode = mode;
        aic3xxx_cfw_set_pll(pv, 0);

    } else if (pjt->mode[mode]->pfw  == 0xFF) { // Bypass mode
        // FIXME
    } else { // Error
        warn("Bad pfw setting detected (%d).  Max pfw=%d", pjt->mode[mode]->pfw,
                pjt->npfw);
    }
    ps->cur_mode = mode;
    warn("setmode_cfg: DONE (mode=%d pfw=%d ovly=%d cfg=%d)", ps->cur_mode, ps->cur_pfw, ps->cur_ovly, ps->cur_cfg);
    return 0;
}
int aic3xxx_cfw_transition(void *pv, char *ttype)
{
    cfw_state *ps = pv;
     int i;
     for (i = 0; i < CFW_TRN_N; ++i) {
         if (!strcasecmp(ttype, cfw_transition_id[i])) {
            if (ps->pjt->transition[i]) {
                DBG("Sending transition %s[%d]", ttype, i);
                aic3xxx_cfw_dlcmds(pv, ps->pjt->transition[i]->block);
            }
            return 0;
         }
     }
     warn("Transition %s not present or invalid", ttype);
     return 0;
}
int aic3xxx_cfw_set_pll(void *pv, int asi)
{
    // FIXME: no error checks!!
    cfw_state *ps = pv;
    cfw_project *pjt = ps->pjt;
    cfw_pfw *pfw = pjt->pfw[pjt->mode[ps->cur_mode]->pfw];
    if (pfw->pll) {
        warn("Configuring PLL for ASI%d using PFW%d", asi,
                pjt->mode[ps->cur_mode]->pfw);
        aic3xxx_cfw_dlcmds(pv, pfw->pll);
    }
    return 0;
}
static void aic3xxx_cfw_dlcmds(void *pv, cfw_block *pb)
{
    cfw_state *ps = pv;
    int i = 0, lock = 0;

    while (i < pb->ncmds) {
        if (CFW_BLOCK_BURSTS(pb->type))
            ps->ops->bulk_write(ps->ops_obj, pb->cmd[i].burst->reg.bpod,
                pb->cmd[i].burst->length,
                pb->cmd[i].burst->data);
        else {
            cfw_meta_delay d = pb->cmd[i].reg.meta.delay;
            cfw_meta_bitop b = pb->cmd[i].reg.meta.bitop;
            switch (pb->cmd[i].reg.meta.mcmd) {
            case CFW_META_DELAY:
                mdelay(d.delay);
                break;
            case CFW_META_UPDTBITS:
                ps->ops->set_bits(ps->ops_obj,  pb->cmd[i+1].reg.bpod,
                        b.mask, pb->cmd[i+1].reg.data);
                i++;
                break;
            case CFW_META_WAITBITS:
                aic3xxx_wait(ps,  pb->cmd[i+1].reg.bpod,
                        b.mask, pb->cmd[i+1].reg.data);
                i++;
                break;
            case CFW_META_LOCK:
                if (d.delay) {
                    ps->ops->lock(ps->ops_obj);
                    lock = 1;
                } else {
                    if (!lock)
                        error("Attempt to unlock without first locking");
                    ps->ops->unlock(ps->ops_obj);
                    lock = 0;
                }
                break;
            default:
                ps->ops->reg_write(ps->ops_obj, pb->cmd[i].reg.bpod,  pb->cmd[i].reg.data);
            }
        }
        ++i;
    }
    if (lock)
        error("exiting blkcmds with lock ON");
}

void aic3xxx_wait(void *p, unsigned int reg, u8 mask, u8 data)
{
    cfw_state *ps = p;
    while ((ps->ops->reg_read(ps->ops_obj, reg)&mask) != data)
        mdelay(2);
}

static int aic3xxx_cfw_dlcfg(void *pv, cfw_image *pim)
{
    cfw_state *ps = pv;
    int i,run_state,swap;
    struct {
        u32 mdsp;
        int buf_a, buf_b;
        u32 swap;
    } csecs[] = {
        {
            .mdsp=AIC3XX_COPS_MDSP_A,
            .swap=AIC3XX_ABUF_MDSP_A,
            .buf_a=CFW_BLOCK_A_A_COEF,
            .buf_b=CFW_BLOCK_A_B_COEF
        },
        {
            .mdsp=AIC3XX_COPS_MDSP_D,
            .swap=AIC3XX_ABUF_MDSP_D1,
            .buf_a=CFW_BLOCK_D_A1_COEF,
            .buf_b=CFW_BLOCK_D_B1_COEF
        },
        {
            .mdsp=AIC3XX_COPS_MDSP_D,
            .swap=AIC3XX_ABUF_MDSP_D2,
            .buf_a=CFW_BLOCK_D_A2_COEF,
            .buf_b=CFW_BLOCK_D_B2_COEF
        },
    };
    DBG("Download CFG %s", pim->name);
    run_state = ps->ops->lock(ps->ops_obj);
    swap = 0;
    for (i = 0; i < sizeof(csecs)/sizeof(csecs[0]); ++i) {
        if (pim->block[csecs[i].buf_a]) {
            if (run_state & csecs[i].mdsp) {
                aic3xxx_cfw_dlcmds(pv, pim->block[csecs[i].buf_a]);
                swap |=  csecs[i].swap;
                aic3xxx_cfw_dlcmds(pv, pim->block[csecs[i].buf_b]);
            } else {
                aic3xxx_cfw_dlcmds(pv, pim->block[csecs[i].buf_a]);
                aic3xxx_cfw_dlcmds(pv, pim->block[csecs[i].buf_b]);
            }
        }
    }
    if (swap) // Check needed? FIXME
        ps->ops->bswap(ps->ops_obj, swap);
    ps->ops->unlock(ps->ops_obj);
    return 0;
}
static int aic3xxx_cfw_dlimage(void *pv, cfw_image *pim)
{
    //cfw_state *ps = pv;
    int i;
    DBG("Download IMAGE %s", pim->name);
    for (i = 0; i < CFW_BLOCK_N; ++i)
        if (pim->block[i])
            aic3xxx_cfw_dlcmds(pv, pim->block[i]);
    return 0;
}

#   define FW_NDX2PTR(x, b) do {                        \
        x = (void *)((u8 *)(b) + ((int)(x)));           \
    } while (0)
static void aic3xxx_cfw_unpickle_block(cfw_block *pb, void *p)
{
    int i;
    if (CFW_BLOCK_BURSTS(pb->type)) {
        for (i = 0; i < pb->ncmds; ++i) {
            FW_NDX2PTR(pb->cmd[i].burst, p);
        }
    }
}
static void aic3xxx_cfw_unpickle_image(cfw_image *im, void *p)
{
    int i;
    for (i = 0; i < CFW_BLOCK_N; ++i)
        if (im->block[i]) {
            FW_NDX2PTR(im->block[i], p);
            aic3xxx_cfw_unpickle_block(im->block[i], p);
        }
}
#ifndef AIC3XXX_CFW_HOST_BLD
static
#endif
unsigned int crc32(unsigned int *pdata, int n)
{
    u32 crc = 0, i, crc_poly = 0x04C11DB7;                  /* CRC - 32 */
    u32 msb;
    u32 residue_value;
    int bits;

    for (i = 0; i < (n >> 2); i++) {
        bits = 32;
        while (--bits >= 0) {
            msb = crc & 0x80000000;
            crc = (crc << 1) ^ ((*pdata >> bits) & 1);
            if (msb)
                crc = crc ^ crc_poly;
        }
        pdata++;
    }

    switch (n & 3) {
        case 0:
            break;
        case 1:
            residue_value = (*pdata & 0xFF);
            bits = 8;
            break;
        case 2:
            residue_value = (*pdata & 0xFFFF);
            bits = 16;
            break;
        case 3:
            residue_value = (*pdata & 0xFFFFFF);
            bits = 24;
            break;
    }

    if (n & 3) {
        while (--bits >= 0) {
            msb = crc & 0x80000000;
            crc = (crc << 1) ^ ((residue_value >> bits) & 1);
            if (msb)
                crc = crc ^ crc_poly;
        }
    }
    return (crc);
}
static int crc_chk(void *p, int n)
{
    cfw_project *pjt = (void *)p;
    u32 crc  = pjt->cksum, crc_comp;
    pjt->cksum = 0;
    DBG("Entering crc %d",n);
    crc_comp = crc32(p, n);
    if (crc_comp != crc) {
        DBG("CRC mismatch 0x%08X != 0x%08X", crc, crc_comp);
        return 0; // Dead code
    }
    DBG("CRC pass");
    pjt->cksum = crc;
    return 1;
}
#ifndef AIC3XXX_CFW_HOST_BLD
static
#endif
cfw_project *aic3xxx_cfw_unpickle(void *p, int n)
{
    cfw_project *pjt = p;
    int i, j, k;
    if (pjt->magic != CFW_FW_MAGIC ||
           pjt->size != n ||
           pjt->bmagic != CFW_FW_VERSION ||
            !crc_chk(p, n)) {
        error("magic:0x%08X!=0x%08X || size:%d!=%d || version:0x%08X!=0x%08X",
               pjt->magic, CFW_FW_MAGIC, pjt->size, n, pjt->cksum,
               CFW_FW_VERSION);
        return NULL;
    }
    DBG("Loaded firmware inside unpickle\n");

    for (i = 0; i < CFW_MAX_TRANSITIONS; i++) {
        if (pjt->transition[i]) {
            FW_NDX2PTR(pjt->transition[i], p);
            FW_NDX2PTR(pjt->transition[i]->block, p);
            aic3xxx_cfw_unpickle_block(pjt->transition[i]->block, p);
        }
    }

    for (i = 0; i < pjt->npfw; i++) {
        DBG("loading pfw %d\n",i);
        FW_NDX2PTR(pjt->pfw[i], p);
        if (pjt->pfw[i]->base) {
            FW_NDX2PTR(pjt->pfw[i]->base, p);
            aic3xxx_cfw_unpickle_image(pjt->pfw[i]->base, p);
        }
        if (pjt->pfw[i]->pll) {
            FW_NDX2PTR(pjt->pfw[i]->pll, p);
            aic3xxx_cfw_unpickle_block(pjt->pfw[i]->pll, p);
        }
        for (j =0; j < pjt->pfw[i]->novly; ++j)
            for (k =0; k < pjt->pfw[i]->ncfg; ++k) {
                FW_NDX2PTR(pjt->pfw[i]->ovly_cfg[j][k], p);
                aic3xxx_cfw_unpickle_image(pjt->pfw[i]->ovly_cfg[j][k], p);
            }
    }

    DBG("loaded pfw's\n");
    for (i = 0; i < pjt->nmode; i++) {
        FW_NDX2PTR(pjt->mode[i], p);
        if (pjt->mode[i]->entry) {
            FW_NDX2PTR(pjt->mode[i]->entry, p);
            aic3xxx_cfw_unpickle_block(pjt->mode[i]->entry, p);
        }
        if (pjt->mode[i]->exit) {
            FW_NDX2PTR(pjt->mode[i]->exit, p);
            aic3xxx_cfw_unpickle_block(pjt->mode[i]->exit, p);
        }
    }
    DBG("loaded modes\n");
    return pjt;
}


