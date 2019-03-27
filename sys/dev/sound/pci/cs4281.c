/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Orion Hodson <O.Hodson@cs.ucl.ac.uk>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THEPOSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * The order of pokes in the initiation sequence is based on Linux
 * driver by Thomas Sailer, gw boynton (wesb@crystal.cirrus.com), tom
 * woller (twoller@crystal.cirrus.com).  Shingo Watanabe (nabe@nabechan.org)
 * contributed towards power management.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/sound/pci/cs4281.h>

SND_DECLARE_FILE("$FreeBSD$");

#define CS4281_DEFAULT_BUFSZ 16384

/* Max fifo size for full duplex is 64 */
#define CS4281_FIFO_SIZE 15

/* DMA Engine Indices */
#define CS4281_DMA_PLAY 0
#define CS4281_DMA_REC  1

/* Misc */

#define inline __inline

#ifndef DEB
#define DEB(x) /* x */
#endif /* DEB */

/* ------------------------------------------------------------------------- */
/* Structures */

struct sc_info;

/* channel registers */
struct sc_chinfo {
    struct sc_info *parent;

    struct snd_dbuf *buffer;
    struct pcm_channel *channel;

    u_int32_t spd, fmt, bps, blksz;

    int dma_setup, dma_active, dma_chan;
};

/* device private data */
struct sc_info {
    device_t dev;
    u_int32_t type;

    bus_space_tag_t st;
    bus_space_handle_t sh;
    bus_dma_tag_t parent_dmat;

    struct resource *reg, *irq, *mem;
    int regtype, regid, irqid, memid;
    void *ih;

    int power;
    unsigned long bufsz;
    struct sc_chinfo pch;
    struct sc_chinfo rch;
};

/* -------------------------------------------------------------------- */
/* prototypes */

/* ADC/DAC control */
static u_int32_t adcdac_go(struct sc_chinfo *ch, u_int32_t go);
static void      adcdac_prog(struct sc_chinfo *ch);

/* power management and interrupt control */
static void      cs4281_intr(void *);
static int       cs4281_power(struct sc_info *, int);
static int       cs4281_init(struct sc_info *);

/* talk to the card */
static u_int32_t cs4281_rd(struct sc_info *, int);
static void 	 cs4281_wr(struct sc_info *, int, u_int32_t);

/* misc */
static u_int8_t  cs4281_rate_to_rv(u_int32_t);
static u_int32_t cs4281_format_to_dmr(u_int32_t);
static u_int32_t cs4281_format_to_bps(u_int32_t);

/* -------------------------------------------------------------------- */
/* formats (do not add formats without editing cs_fmt_tab)              */

static u_int32_t cs4281_fmts[] = {
    SND_FORMAT(AFMT_U8, 1, 0),
    SND_FORMAT(AFMT_U8, 2, 0),
    SND_FORMAT(AFMT_S8, 1, 0),
    SND_FORMAT(AFMT_S8, 2, 0),
    SND_FORMAT(AFMT_S16_LE, 1, 0),
    SND_FORMAT(AFMT_S16_LE, 2, 0),
    SND_FORMAT(AFMT_U16_LE, 1, 0),
    SND_FORMAT(AFMT_U16_LE, 2, 0),
    SND_FORMAT(AFMT_S16_BE, 1, 0),
    SND_FORMAT(AFMT_S16_BE, 2, 0),
    SND_FORMAT(AFMT_U16_BE, 1, 0),
    SND_FORMAT(AFMT_U16_BE, 2, 0),
    0
};

static struct pcmchan_caps cs4281_caps = {6024, 48000, cs4281_fmts, 0};

/* -------------------------------------------------------------------- */
/* Hardware */

static inline u_int32_t
cs4281_rd(struct sc_info *sc, int regno)
{
    return bus_space_read_4(sc->st, sc->sh, regno);
}

static inline void
cs4281_wr(struct sc_info *sc, int regno, u_int32_t data)
{
    bus_space_write_4(sc->st, sc->sh, regno, data);
    DELAY(100);
}

static inline void
cs4281_clr4(struct sc_info *sc, int regno, u_int32_t mask)
{
    u_int32_t r;
    r = cs4281_rd(sc, regno);
    cs4281_wr(sc, regno, r & ~mask);
}

static inline void
cs4281_set4(struct sc_info *sc, int regno, u_int32_t mask)
{
    u_int32_t v;
    v = cs4281_rd(sc, regno);
    cs4281_wr(sc, regno, v | mask);
}

static int
cs4281_waitset(struct sc_info *sc, int regno, u_int32_t mask, int tries)
{
    u_int32_t v;

    while (tries > 0) {
	DELAY(100);
	v = cs4281_rd(sc, regno);
	if ((v & mask) == mask) break;
	tries --;
    }
    return tries;
}

static int
cs4281_waitclr(struct sc_info *sc, int regno, u_int32_t mask, int tries)
{
    u_int32_t v;

    while (tries > 0) {
	DELAY(100);
	v = ~ cs4281_rd(sc, regno);
	if (v & mask) break;
	tries --;
    }
    return tries;
}

/* ------------------------------------------------------------------------- */
/* Register value mapping functions */

static u_int32_t cs4281_rates[] = {48000, 44100, 22050, 16000, 11025, 8000};
#define CS4281_NUM_RATES sizeof(cs4281_rates)/sizeof(cs4281_rates[0])

static u_int8_t
cs4281_rate_to_rv(u_int32_t rate)
{
    u_int32_t v;

    for (v = 0; v < CS4281_NUM_RATES; v++) {
	if (rate == cs4281_rates[v]) return v;
    }

    v = 1536000 / rate;
    if (v > 255 || v < 32) v = 5; /* default to 8k */
    return v;
}

static u_int32_t
cs4281_rv_to_rate(u_int8_t rv)
{
    u_int32_t r;

    if (rv < CS4281_NUM_RATES) return cs4281_rates[rv];
    r = 1536000 / rv;
    return r;
}

static inline u_int32_t
cs4281_format_to_dmr(u_int32_t format)
{
    u_int32_t dmr = 0;
    if (AFMT_8BIT & format)      dmr |= CS4281PCI_DMR_SIZE8;
    if (AFMT_CHANNEL(format) < 2) dmr |= CS4281PCI_DMR_MONO;
    if (AFMT_BIGENDIAN & format) dmr |= CS4281PCI_DMR_BEND;
    if (!(AFMT_SIGNED & format)) dmr |= CS4281PCI_DMR_USIGN;
    return dmr;
}

static inline u_int32_t
cs4281_format_to_bps(u_int32_t format)
{
    return ((AFMT_8BIT & format) ? 1 : 2) *
	((AFMT_CHANNEL(format) > 1) ? 2 : 1);
}

/* -------------------------------------------------------------------- */
/* ac97 codec */

static int
cs4281_rdcd(kobj_t obj, void *devinfo, int regno)
{
    struct sc_info *sc = (struct sc_info *)devinfo;
    int codecno;

    codecno = regno >> 8;
    regno &= 0xff;

    /* Remove old state */
    cs4281_rd(sc, CS4281PCI_ACSDA);

    /* Fill in AC97 register value request form */
    cs4281_wr(sc, CS4281PCI_ACCAD, regno);
    cs4281_wr(sc, CS4281PCI_ACCDA, 0);
    cs4281_wr(sc, CS4281PCI_ACCTL, CS4281PCI_ACCTL_ESYN |
	      CS4281PCI_ACCTL_VFRM | CS4281PCI_ACCTL_DCV |
	      CS4281PCI_ACCTL_CRW);

    /* Wait for read to complete */
    if (cs4281_waitclr(sc, CS4281PCI_ACCTL, CS4281PCI_ACCTL_DCV, 250) == 0) {
	device_printf(sc->dev, "cs4281_rdcd: DCV did not go\n");
	return -1;
    }

    /* Wait for valid status */
    if (cs4281_waitset(sc, CS4281PCI_ACSTS, CS4281PCI_ACSTS_VSTS, 250) == 0) {
	device_printf(sc->dev,"cs4281_rdcd: VSTS did not come\n");
	return -1;
    }

    return cs4281_rd(sc, CS4281PCI_ACSDA);
}

static int
cs4281_wrcd(kobj_t obj, void *devinfo, int regno, u_int32_t data)
{
    struct sc_info *sc = (struct sc_info *)devinfo;
    int codecno;

    codecno = regno >> 8;
    regno &= 0xff;

    cs4281_wr(sc, CS4281PCI_ACCAD, regno);
    cs4281_wr(sc, CS4281PCI_ACCDA, data);
    cs4281_wr(sc, CS4281PCI_ACCTL, CS4281PCI_ACCTL_ESYN |
	      CS4281PCI_ACCTL_VFRM | CS4281PCI_ACCTL_DCV);

    if (cs4281_waitclr(sc, CS4281PCI_ACCTL, CS4281PCI_ACCTL_DCV, 250) == 0) {
	device_printf(sc->dev,"cs4281_wrcd: DCV did not go\n");
    }

    return 0;
}

static kobj_method_t cs4281_ac97_methods[] = {
        KOBJMETHOD(ac97_read,           cs4281_rdcd),
        KOBJMETHOD(ac97_write,          cs4281_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(cs4281_ac97);

/* ------------------------------------------------------------------------- */
/* shared rec/play channel interface */

static void *
cs4281chan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
    struct sc_info *sc = devinfo;
    struct sc_chinfo *ch = (dir == PCMDIR_PLAY) ? &sc->pch : &sc->rch;

    ch->buffer = b;
    if (sndbuf_alloc(ch->buffer, sc->parent_dmat, 0, sc->bufsz) != 0) {
	return NULL;
    }
    ch->parent = sc;
    ch->channel = c;

    ch->fmt = SND_FORMAT(AFMT_U8, 1, 0);
    ch->spd = DSP_DEFAULT_SPEED;
    ch->bps = 1;
    ch->blksz = sndbuf_getsize(ch->buffer);

    ch->dma_chan = (dir == PCMDIR_PLAY) ? CS4281_DMA_PLAY : CS4281_DMA_REC;
    ch->dma_setup = 0;

    adcdac_go(ch, 0);
    adcdac_prog(ch);

    return ch;
}

static u_int32_t
cs4281chan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
    struct sc_chinfo *ch = data;
    struct sc_info *sc = ch->parent;
    u_int32_t go;

    go = adcdac_go(ch, 0);

    /* 2 interrupts are possible and used in buffer (half-empty,empty),
     * hence factor of 2. */
    ch->blksz = MIN(blocksize, sc->bufsz / 2);
    sndbuf_resize(ch->buffer, 2, ch->blksz);
    ch->dma_setup = 0;
    adcdac_prog(ch);
    adcdac_go(ch, go);

    DEB(printf("cs4281chan_setblocksize: blksz %d Setting %d\n", blocksize, ch->blksz));

    return ch->blksz;
}

static u_int32_t
cs4281chan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
    struct sc_chinfo *ch = data;
    struct sc_info *sc = ch->parent;
    u_int32_t go, v, r;

    go = adcdac_go(ch, 0); /* pause */
    r = (ch->dma_chan == CS4281_DMA_PLAY) ? CS4281PCI_DACSR : CS4281PCI_ADCSR;
    v = cs4281_rate_to_rv(speed);
    cs4281_wr(sc, r, v);
    adcdac_go(ch, go); /* unpause */

    ch->spd = cs4281_rv_to_rate(v);
    return ch->spd;
}

static int
cs4281chan_setformat(kobj_t obj, void *data, u_int32_t format)
{
    struct sc_chinfo *ch = data;
    struct sc_info *sc = ch->parent;
    u_int32_t v, go;

    go = adcdac_go(ch, 0); /* pause */

    if (ch->dma_chan == CS4281_DMA_PLAY)
	v = CS4281PCI_DMR_TR_PLAY;
    else
	v = CS4281PCI_DMR_TR_REC;
    v |= CS4281PCI_DMR_DMA | CS4281PCI_DMR_AUTO;
    v |= cs4281_format_to_dmr(format);
    cs4281_wr(sc, CS4281PCI_DMR(ch->dma_chan), v);

    adcdac_go(ch, go); /* unpause */

    ch->fmt = format;
    ch->bps = cs4281_format_to_bps(format);
    ch->dma_setup = 0;

    return 0;
}

static u_int32_t
cs4281chan_getptr(kobj_t obj, void *data)
{
    struct sc_chinfo *ch = data;
    struct sc_info *sc = ch->parent;
    u_int32_t  dba, dca, ptr;
    int sz;

    sz  = sndbuf_getsize(ch->buffer);
    dba = cs4281_rd(sc, CS4281PCI_DBA(ch->dma_chan));
    dca = cs4281_rd(sc, CS4281PCI_DCA(ch->dma_chan));
    ptr = (dca - dba + sz) % sz;

    return ptr;
}

static int
cs4281chan_trigger(kobj_t obj, void *data, int go)
{
    struct sc_chinfo *ch = data;

    switch(go) {
    case PCMTRIG_START:
	adcdac_prog(ch);
	adcdac_go(ch, 1);
	break;
    case PCMTRIG_STOP:
    case PCMTRIG_ABORT:
	adcdac_go(ch, 0);
	break;
    default:
	break;
    }

    /* return 0 if ok */
    return 0;
}

static struct pcmchan_caps *
cs4281chan_getcaps(kobj_t obj, void *data)
{
    return &cs4281_caps;
}

static kobj_method_t cs4281chan_methods[] = {
    	KOBJMETHOD(channel_init,		cs4281chan_init),
    	KOBJMETHOD(channel_setformat,		cs4281chan_setformat),
    	KOBJMETHOD(channel_setspeed,		cs4281chan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	cs4281chan_setblocksize),
    	KOBJMETHOD(channel_trigger,		cs4281chan_trigger),
    	KOBJMETHOD(channel_getptr,		cs4281chan_getptr),
    	KOBJMETHOD(channel_getcaps,		cs4281chan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(cs4281chan);

/* -------------------------------------------------------------------- */
/* ADC/DAC control */

/* adcdac_go enables/disable DMA channel, returns non-zero if DMA was
 * active before call */

static u_int32_t
adcdac_go(struct sc_chinfo *ch, u_int32_t go)
{
    struct sc_info *sc = ch->parent;
    u_int32_t going;

    going = !(cs4281_rd(sc, CS4281PCI_DCR(ch->dma_chan)) & CS4281PCI_DCR_MSK);

    if (go)
	cs4281_clr4(sc, CS4281PCI_DCR(ch->dma_chan), CS4281PCI_DCR_MSK);
    else
	cs4281_set4(sc, CS4281PCI_DCR(ch->dma_chan), CS4281PCI_DCR_MSK);

    cs4281_wr(sc, CS4281PCI_HICR, CS4281PCI_HICR_EOI);

    return going;
}

static void
adcdac_prog(struct sc_chinfo *ch)
{
    struct sc_info *sc = ch->parent;
    u_int32_t go;

    if (!ch->dma_setup) {
	go = adcdac_go(ch, 0);
	cs4281_wr(sc, CS4281PCI_DBA(ch->dma_chan),
		  sndbuf_getbufaddr(ch->buffer));
	cs4281_wr(sc, CS4281PCI_DBC(ch->dma_chan),
		  sndbuf_getsize(ch->buffer) / ch->bps - 1);
	ch->dma_setup = 1;
	adcdac_go(ch, go);
    }
}

/* -------------------------------------------------------------------- */
/* The interrupt handler */

static void
cs4281_intr(void *p)
{
    struct sc_info *sc = (struct sc_info *)p;
    u_int32_t hisr;

    hisr = cs4281_rd(sc, CS4281PCI_HISR);

    if (hisr == 0) return;

    if (hisr & CS4281PCI_HISR_DMA(CS4281_DMA_PLAY)) {
	chn_intr(sc->pch.channel);
	cs4281_rd(sc, CS4281PCI_HDSR(CS4281_DMA_PLAY)); /* Clear interrupt */
    }

    if (hisr & CS4281PCI_HISR_DMA(CS4281_DMA_REC)) {
	chn_intr(sc->rch.channel);
	cs4281_rd(sc, CS4281PCI_HDSR(CS4281_DMA_REC)); /* Clear interrupt */
    }

    /* Signal End-of-Interrupt */
    cs4281_wr(sc, CS4281PCI_HICR, CS4281PCI_HICR_EOI);
}

/* -------------------------------------------------------------------- */
/* power management related */

static int
cs4281_power(struct sc_info *sc, int state)
{

    switch (state) {
    case 0:
        /* Permit r/w access to all BA0 registers */
        cs4281_wr(sc, CS4281PCI_CWPR, CS4281PCI_CWPR_MAGIC);
        /* Power on */
        cs4281_clr4(sc, CS4281PCI_EPPMC, CS4281PCI_EPPMC_FPDN);
        break;
    case 3:
    	/* Power off card and codec */
    	cs4281_set4(sc, CS4281PCI_EPPMC, CS4281PCI_EPPMC_FPDN);
    	cs4281_clr4(sc, CS4281PCI_SPMC, CS4281PCI_SPMC_RSTN);
        break;
    }

    DEB(printf("cs4281_power %d -> %d\n", sc->power, state));
    sc->power = state;

    return 0;
}

static int
cs4281_init(struct sc_info *sc)
{
    u_int32_t i, v;

    /* (0) Blast clock register and serial port */
    cs4281_wr(sc, CS4281PCI_CLKCR1, 0);
    cs4281_wr(sc, CS4281PCI_SERMC,  0);

    /* (1) Make ESYN 0 to turn sync pulse on AC97 link */
    cs4281_wr(sc, CS4281PCI_ACCTL, 0);
    DELAY(50);

    /* (2) Effect Reset */
    cs4281_wr(sc, CS4281PCI_SPMC, 0);
    DELAY(100);
    cs4281_wr(sc, CS4281PCI_SPMC, CS4281PCI_SPMC_RSTN);
    /* Wait 50ms for ABITCLK to become stable */
    DELAY(50000);

    /* (3) Enable Sound System Clocks */
    cs4281_wr(sc, CS4281PCI_CLKCR1, CS4281PCI_CLKCR1_DLLP);
    DELAY(50000); /* Wait for PLL to stabilize */
    cs4281_wr(sc, CS4281PCI_CLKCR1,
	      CS4281PCI_CLKCR1_DLLP | CS4281PCI_CLKCR1_SWCE);

    /* (4) Power Up - this combination is essential. */
    cs4281_set4(sc, CS4281PCI_SSPM,
		CS4281PCI_SSPM_ACLEN | CS4281PCI_SSPM_PSRCEN |
		CS4281PCI_SSPM_CSRCEN | CS4281PCI_SSPM_MIXEN);

    /* (5) Wait for clock stabilization */
    if (cs4281_waitset(sc,
		       CS4281PCI_CLKCR1,
		       CS4281PCI_CLKCR1_DLLRDY,
		       250) == 0) {
	device_printf(sc->dev, "Clock stabilization failed\n");
	return -1;
    }

    /* (6) Enable ASYNC generation. */
    cs4281_wr(sc, CS4281PCI_ACCTL,CS4281PCI_ACCTL_ESYN);

    /* Wait to allow AC97 to start generating clock bit */
    DELAY(50000);

    /* Set AC97 timing */
    cs4281_wr(sc, CS4281PCI_SERMC, CS4281PCI_SERMC_PTC_AC97);

    /* (7) Wait for AC97 ready signal */
    if (cs4281_waitset(sc, CS4281PCI_ACSTS, CS4281PCI_ACSTS_CRDY, 250) == 0) {
	device_printf(sc->dev, "codec did not avail\n");
	return -1;
    }

    /* (8) Assert valid frame signal to begin sending commands to
     *     AC97 codec */
    cs4281_wr(sc,
	      CS4281PCI_ACCTL,
	      CS4281PCI_ACCTL_VFRM | CS4281PCI_ACCTL_ESYN);

    /* (9) Wait for codec calibration */
    for(i = 0 ; i < 1000; i++) {
	DELAY(10000);
	v = cs4281_rdcd(0, sc, AC97_REG_POWER);
	if ((v & 0x0f) == 0x0f) {
	    break;
	}
    }
    if (i == 1000) {
	device_printf(sc->dev, "codec failed to calibrate\n");
	return -1;
    }

    /* (10) Set AC97 timing */
    cs4281_wr(sc, CS4281PCI_SERMC, CS4281PCI_SERMC_PTC_AC97);

    /* (11) Wait for valid data to arrive */
    if (cs4281_waitset(sc,
		       CS4281PCI_ACISV,
		       CS4281PCI_ACISV_ISV(3) | CS4281PCI_ACISV_ISV(4),
		       10000) == 0) {
	device_printf(sc->dev, "cs4281 never got valid data\n");
	return -1;
    }

    /* (12) Start digital data transfer of audio data to codec */
    cs4281_wr(sc,
	      CS4281PCI_ACOSV,
	      CS4281PCI_ACOSV_SLV(3) | CS4281PCI_ACOSV_SLV(4));

    /* Set Master and headphone to max */
    cs4281_wrcd(0, sc, AC97_MIX_AUXOUT, 0);
    cs4281_wrcd(0, sc, AC97_MIX_MASTER, 0);

    /* Power on the DAC */
    v = cs4281_rdcd(0, sc, AC97_REG_POWER) & 0xfdff;
    cs4281_wrcd(0, sc, AC97_REG_POWER, v);

    /* Wait until DAC state ready */
    for(i = 0; i < 320; i++) {
	DELAY(100);
	v = cs4281_rdcd(0, sc, AC97_REG_POWER);
	if (v & 0x02) break;
    }

    /* Power on the ADC */
    v = cs4281_rdcd(0, sc, AC97_REG_POWER) & 0xfeff;
    cs4281_wrcd(0, sc, AC97_REG_POWER, v);

    /* Wait until ADC state ready */
    for(i = 0; i < 320; i++) {
	DELAY(100);
	v = cs4281_rdcd(0, sc, AC97_REG_POWER);
	if (v & 0x01) break;
    }

    /* FIFO configuration (driver is DMA orientated, implicit FIFO) */
    /* Play FIFO */

    v = CS4281PCI_FCR_RS(CS4281PCI_RPCM_PLAY_SLOT) |
	CS4281PCI_FCR_LS(CS4281PCI_LPCM_PLAY_SLOT) |
	CS4281PCI_FCR_SZ(CS4281_FIFO_SIZE)|
	CS4281PCI_FCR_OF(0);
    cs4281_wr(sc, CS4281PCI_FCR(CS4281_DMA_PLAY), v);

    cs4281_wr(sc, CS4281PCI_FCR(CS4281_DMA_PLAY), v | CS4281PCI_FCR_FEN);

    /* Record FIFO */
    v = CS4281PCI_FCR_RS(CS4281PCI_RPCM_REC_SLOT) |
	CS4281PCI_FCR_LS(CS4281PCI_LPCM_REC_SLOT) |
	CS4281PCI_FCR_SZ(CS4281_FIFO_SIZE)|
	CS4281PCI_FCR_OF(CS4281_FIFO_SIZE + 1);
    cs4281_wr(sc, CS4281PCI_FCR(CS4281_DMA_REC), v | CS4281PCI_FCR_PSH);
    cs4281_wr(sc, CS4281PCI_FCR(CS4281_DMA_REC), v | CS4281PCI_FCR_FEN);

    /* Match AC97 slots to FIFOs */
    v = CS4281PCI_SRCSA_PLSS(CS4281PCI_LPCM_PLAY_SLOT) |
	CS4281PCI_SRCSA_PRSS(CS4281PCI_RPCM_PLAY_SLOT) |
	CS4281PCI_SRCSA_CLSS(CS4281PCI_LPCM_REC_SLOT) |
	CS4281PCI_SRCSA_CRSS(CS4281PCI_RPCM_REC_SLOT);
    cs4281_wr(sc, CS4281PCI_SRCSA, v);

    /* Set Auto-Initialize and set directions */
    cs4281_wr(sc,
	      CS4281PCI_DMR(CS4281_DMA_PLAY),
	      CS4281PCI_DMR_DMA  |
	      CS4281PCI_DMR_AUTO |
	      CS4281PCI_DMR_TR_PLAY);
    cs4281_wr(sc,
	      CS4281PCI_DMR(CS4281_DMA_REC),
	      CS4281PCI_DMR_DMA  |
	      CS4281PCI_DMR_AUTO |
	      CS4281PCI_DMR_TR_REC);

    /* Enable half and empty buffer interrupts keeping DMA paused */
    cs4281_wr(sc,
	      CS4281PCI_DCR(CS4281_DMA_PLAY),
	      CS4281PCI_DCR_TCIE | CS4281PCI_DCR_HTCIE | CS4281PCI_DCR_MSK);
    cs4281_wr(sc,
	      CS4281PCI_DCR(CS4281_DMA_REC),
	      CS4281PCI_DCR_TCIE | CS4281PCI_DCR_HTCIE | CS4281PCI_DCR_MSK);

    /* Enable Interrupts */
    cs4281_clr4(sc,
		CS4281PCI_HIMR,
		CS4281PCI_HIMR_DMAI |
		CS4281PCI_HIMR_DMA(CS4281_DMA_PLAY) |
		CS4281PCI_HIMR_DMA(CS4281_DMA_REC));

    /* Set playback volume */
    cs4281_wr(sc, CS4281PCI_PPLVC, 7);
    cs4281_wr(sc, CS4281PCI_PPRVC, 7);

    return 0;
}

/* -------------------------------------------------------------------- */
/* Probe and attach the card */

static int
cs4281_pci_probe(device_t dev)
{
    char *s = NULL;

    switch (pci_get_devid(dev)) {
    case CS4281_PCI_ID:
	s = "Crystal Semiconductor CS4281";
	break;
    }

    if (s)
	device_set_desc(dev, s);
    return s ? BUS_PROBE_DEFAULT : ENXIO;
}

static int
cs4281_pci_attach(device_t dev)
{
    struct sc_info *sc;
    struct ac97_info *codec = NULL;
    char status[SND_STATUSLEN];

    sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
    sc->dev = dev;
    sc->type = pci_get_devid(dev);

    pci_enable_busmaster(dev);

    if (pci_get_powerstate(dev) != PCI_POWERSTATE_D0) {
	/* Reset the power state. */
	device_printf(dev, "chip is in D%d power mode "
		      "-- setting to D0\n", pci_get_powerstate(dev));

	pci_set_powerstate(dev, PCI_POWERSTATE_D0);
    }

    sc->regid   = PCIR_BAR(0);
    sc->regtype = SYS_RES_MEMORY;
    sc->reg = bus_alloc_resource_any(dev, sc->regtype, &sc->regid, RF_ACTIVE);
    if (!sc->reg) {
	sc->regtype = SYS_RES_IOPORT;
	sc->reg = bus_alloc_resource_any(dev, sc->regtype, &sc->regid,
					 RF_ACTIVE);
	if (!sc->reg) {
	    device_printf(dev, "unable to allocate register space\n");
	    goto bad;
	}
    }
    sc->st = rman_get_bustag(sc->reg);
    sc->sh = rman_get_bushandle(sc->reg);

    sc->memid = PCIR_BAR(1);
    sc->mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->memid,
				     RF_ACTIVE);
    if (sc->mem == NULL) {
	device_printf(dev, "unable to allocate fifo space\n");
	goto bad;
    }

    sc->irqid = 0;
    sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqid,
				     RF_ACTIVE | RF_SHAREABLE);
    if (!sc->irq) {
	device_printf(dev, "unable to allocate interrupt\n");
	goto bad;
    }

    if (snd_setup_intr(dev, sc->irq, 0, cs4281_intr, sc, &sc->ih)) {
	device_printf(dev, "unable to setup interrupt\n");
	goto bad;
    }

    sc->bufsz = pcm_getbuffersize(dev, 4096, CS4281_DEFAULT_BUFSZ, 65536);

    if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
			   /*boundary*/0,
			   /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
			   /*highaddr*/BUS_SPACE_MAXADDR,
			   /*filter*/NULL, /*filterarg*/NULL,
			   /*maxsize*/sc->bufsz, /*nsegments*/1,
			   /*maxsegz*/0x3ffff,
			   /*flags*/0, /*lockfunc*/busdma_lock_mutex,
			   /*lockarg*/&Giant, &sc->parent_dmat) != 0) {
	device_printf(dev, "unable to create dma tag\n");
	goto bad;
    }

    /* power up */
    cs4281_power(sc, 0);

    /* init chip */
    if (cs4281_init(sc) == -1) {
	device_printf(dev, "unable to initialize the card\n");
	goto bad;
    }

    /* create/init mixer */
    codec = AC97_CREATE(dev, sc, cs4281_ac97);
    if (codec == NULL)
        goto bad;

    mixer_init(dev, ac97_getmixerclass(), codec);

    if (pcm_register(dev, sc, 1, 1))
	goto bad;

    pcm_addchan(dev, PCMDIR_PLAY, &cs4281chan_class, sc);
    pcm_addchan(dev, PCMDIR_REC, &cs4281chan_class, sc);

    snprintf(status, SND_STATUSLEN, "at %s 0x%jx irq %jd %s",
	     (sc->regtype == SYS_RES_IOPORT)? "io" : "memory",
	     rman_get_start(sc->reg), rman_get_start(sc->irq),PCM_KLDSTRING(snd_cs4281));
    pcm_setstatus(dev, status);

    return 0;

 bad:
    if (codec)
	ac97_destroy(codec);
    if (sc->reg)
	bus_release_resource(dev, sc->regtype, sc->regid, sc->reg);
    if (sc->mem)
	bus_release_resource(dev, SYS_RES_MEMORY, sc->memid, sc->mem);
    if (sc->ih)
	bus_teardown_intr(dev, sc->irq, sc->ih);
    if (sc->irq)
	bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
    if (sc->parent_dmat)
	bus_dma_tag_destroy(sc->parent_dmat);
    free(sc, M_DEVBUF);

    return ENXIO;
}

static int
cs4281_pci_detach(device_t dev)
{
    int r;
    struct sc_info *sc;

    r = pcm_unregister(dev);
    if (r)
	return r;

    sc = pcm_getdevinfo(dev);

    /* power off */
    cs4281_power(sc, 3);

    bus_release_resource(dev, sc->regtype, sc->regid, sc->reg);
    bus_release_resource(dev, SYS_RES_MEMORY, sc->memid, sc->mem);
    bus_teardown_intr(dev, sc->irq, sc->ih);
    bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
    bus_dma_tag_destroy(sc->parent_dmat);
    free(sc, M_DEVBUF);

    return 0;
}

static int
cs4281_pci_suspend(device_t dev)
{
    struct sc_info *sc;

    sc = pcm_getdevinfo(dev);

    sc->rch.dma_active = adcdac_go(&sc->rch, 0);
    sc->pch.dma_active = adcdac_go(&sc->pch, 0);

    cs4281_power(sc, 3);

    return 0;
}

static int
cs4281_pci_resume(device_t dev)
{
    struct sc_info *sc;

    sc = pcm_getdevinfo(dev);

    /* power up */
    cs4281_power(sc, 0);

    /* initialize chip */
    if (cs4281_init(sc) == -1) {
        device_printf(dev, "unable to reinitialize the card\n");
        return ENXIO;
    }

    /* restore mixer state */
    if (mixer_reinit(dev) == -1) {
	device_printf(dev, "unable to reinitialize the mixer\n");
	return ENXIO;
    }

    /* restore chip state */
    cs4281chan_setspeed(NULL, &sc->rch, sc->rch.spd);
    cs4281chan_setblocksize(NULL, &sc->rch, sc->rch.blksz);
    cs4281chan_setformat(NULL, &sc->rch, sc->rch.fmt);
    adcdac_go(&sc->rch, sc->rch.dma_active);

    cs4281chan_setspeed(NULL, &sc->pch, sc->pch.spd);
    cs4281chan_setblocksize(NULL, &sc->pch, sc->pch.blksz);
    cs4281chan_setformat(NULL, &sc->pch, sc->pch.fmt);
    adcdac_go(&sc->pch, sc->pch.dma_active);

    return 0;
}

static device_method_t cs4281_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		cs4281_pci_probe),
    DEVMETHOD(device_attach,		cs4281_pci_attach),
    DEVMETHOD(device_detach,		cs4281_pci_detach),
    DEVMETHOD(device_suspend,		cs4281_pci_suspend),
    DEVMETHOD(device_resume,		cs4281_pci_resume),
    { 0, 0 }
};

static driver_t cs4281_driver = {
    "pcm",
    cs4281_methods,
    PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_cs4281, pci, cs4281_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_cs4281, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_cs4281, 1);
