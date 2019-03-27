/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * i.MX6 Synchronous Serial Interface (SSI)
 *
 * Chapter 61, i.MX 6Dual/6Quad Applications Processor Reference Manual,
 * Rev. 1, 04/2013
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>

#include <dev/sound/pcm/sound.h>
#include <dev/sound/chip.h>
#include <mixer_if.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/imx/imx6_sdma.h>
#include <arm/freescale/imx/imx6_anatopvar.h>
#include <arm/freescale/imx/imx_ccmvar.h>

#define	READ4(_sc, _reg)	\
	bus_space_read_4(_sc->bst, _sc->bsh, _reg)
#define	WRITE4(_sc, _reg, _val)	\
	bus_space_write_4(_sc->bst, _sc->bsh, _reg, _val)

#define	SSI_NCHANNELS	1
#define	DMAS_TOTAL	8

/* i.MX6 SSI registers */

#define	SSI_STX0	0x00 /* Transmit Data Register n */
#define	SSI_STX1	0x04 /* Transmit Data Register n */
#define	SSI_SRX0	0x08 /* Receive Data Register n */
#define	SSI_SRX1	0x0C /* Receive Data Register n */
#define	SSI_SCR		0x10 /* Control Register */
#define	 SCR_I2S_MODE_S	5    /* I2S Mode Select. */
#define	 SCR_I2S_MODE_M	0x3
#define	 SCR_SYN	(1 << 4)
#define	 SCR_NET       	(1 << 3)  /* Network mode */
#define	 SCR_RE		(1 << 2)  /* Receive Enable. */
#define	 SCR_TE		(1 << 1)  /* Transmit Enable. */
#define	 SCR_SSIEN	(1 << 0)  /* SSI Enable */
#define	SSI_SISR	0x14      /* Interrupt Status Register */
#define	SSI_SIER	0x18      /* Interrupt Enable Register */
#define	 SIER_RDMAE	(1 << 22) /* Receive DMA Enable. */
#define	 SIER_RIE	(1 << 21) /* Receive Interrupt Enable. */
#define	 SIER_TDMAE	(1 << 20) /* Transmit DMA Enable. */
#define	 SIER_TIE	(1 << 19) /* Transmit Interrupt Enable. */
#define	 SIER_TDE0IE	(1 << 12) /* Transmit Data Register Empty 0. */
#define	 SIER_TUE0IE	(1 << 8)  /* Transmitter Underrun Error 0. */
#define	 SIER_TFE0IE	(1 << 0)  /* Transmit FIFO Empty 0 IE. */
#define	SSI_STCR	0x1C	  /* Transmit Configuration Register */
#define	 STCR_TXBIT0	(1 << 9)  /* Transmit Bit 0 shift MSB/LSB */
#define	 STCR_TFEN1	(1 << 8)  /* Transmit FIFO Enable 1. */
#define	 STCR_TFEN0	(1 << 7)  /* Transmit FIFO Enable 0. */
#define	 STCR_TFDIR	(1 << 6)  /* Transmit Frame Direction. */
#define	 STCR_TXDIR	(1 << 5)  /* Transmit Clock Direction. */
#define	 STCR_TSHFD	(1 << 4)  /* Transmit Shift Direction. */
#define	 STCR_TSCKP	(1 << 3)  /* Transmit Clock Polarity. */
#define	 STCR_TFSI	(1 << 2)  /* Transmit Frame Sync Invert. */
#define	 STCR_TFSL	(1 << 1)  /* Transmit Frame Sync Length. */
#define	 STCR_TEFS	(1 << 0)  /* Transmit Early Frame Sync. */
#define	SSI_SRCR	0x20      /* Receive Configuration Register */
#define	SSI_STCCR	0x24      /* Transmit Clock Control Register */
#define	 STCCR_DIV2	(1 << 18) /* Divide By 2. */
#define	 STCCR_PSR	(1 << 17) /* Divide clock by 8. */
#define	 WL3_WL0_S	13
#define	 WL3_WL0_M	0xf
#define	 DC4_DC0_S	8
#define	 DC4_DC0_M	0x1f
#define	 PM7_PM0_S	0
#define	 PM7_PM0_M	0xff
#define	SSI_SRCCR	0x28	/* Receive Clock Control Register */
#define	SSI_SFCSR	0x2C	/* FIFO Control/Status Register */
#define	 SFCSR_RFWM1_S	20	/* Receive FIFO Empty WaterMark 1 */
#define	 SFCSR_RFWM1_M	0xf
#define	 SFCSR_TFWM1_S	16	/* Transmit FIFO Empty WaterMark 1 */
#define	 SFCSR_TFWM1_M	0xf
#define	 SFCSR_RFWM0_S	4	/* Receive FIFO Empty WaterMark 0 */
#define	 SFCSR_RFWM0_M	0xf
#define	 SFCSR_TFWM0_S	0	/* Transmit FIFO Empty WaterMark 0 */
#define	 SFCSR_TFWM0_M	0xf
#define	SSI_SACNT	0x38	/* AC97 Control Register */
#define	SSI_SACADD	0x3C	/* AC97 Command Address Register */
#define	SSI_SACDAT	0x40	/* AC97 Command Data Register */
#define	SSI_SATAG	0x44	/* AC97 Tag Register */
#define	SSI_STMSK	0x48	/* Transmit Time Slot Mask Register */
#define	SSI_SRMSK	0x4C	/* Receive Time Slot Mask Register */
#define	SSI_SACCST	0x50	/* AC97 Channel Status Register */
#define	SSI_SACCEN	0x54	/* AC97 Channel Enable Register */
#define	SSI_SACCDIS	0x58	/* AC97 Channel Disable Register */

static MALLOC_DEFINE(M_SSI, "ssi", "ssi audio");

uint32_t ssi_dma_intr(void *arg, int chn);

struct ssi_rate {
	uint32_t speed;
	uint32_t mfi; /* PLL4 Multiplication Factor Integer */
	uint32_t mfn; /* PLL4 Multiplication Factor Numerator */
	uint32_t mfd; /* PLL4 Multiplication Factor Denominator */
	/* More dividers to configure can be added here */
};

static struct ssi_rate rate_map[] = {
	{ 192000, 49, 152, 1000 }, /* PLL4 49.152 Mhz */
	/* TODO: add more frequences */
	{ 0, 0 },
};

/*
 *  i.MX6 example bit clock formula
 *
 *  BCLK = 2 channels * 192000 hz * 24 bit = 9216000 hz = 
 *     (24000000 * (49 + 152/1000.0) / 4 / 4 / 2 / 2 / 2 / 1 / 1)
 *             ^     ^     ^      ^    ^   ^   ^   ^   ^   ^   ^
 *             |     |     |      |    |   |   |   |   |   |   |
 *  Fref ------/     |     |      |    |   |   |   |   |   |   |
 *  PLL4 div select -/     |      |    |   |   |   |   |   |   |
 *  PLL4 num --------------/      |    |   |   |   |   |   |   |
 *  PLL4 denom -------------------/    |   |   |   |   |   |   |
 *  PLL4 post div ---------------------/   |   |   |   |   |   |
 *  CCM ssi pre div (CCM_CS1CDR) ----------/   |   |   |   |   |
 *  CCM ssi post div (CCM_CS1CDR) -------------/   |   |   |   |
 *  SSI PM7_PM0_S ---------------------------------/   |   |   |
 *  SSI Fixed divider ---------------------------------/   |   |
 *  SSI DIV2 ----------------------------------------------/   |
 *  SSI PSR (prescaler /1 or /8) ------------------------------/
 *
 *  MCLK (Master clock) depends on DAC, usually BCLK * 4
 */

struct sc_info {
	struct resource		*res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
	struct mtx		*lock;
	void			*ih;
	int			pos;
	int			dma_size;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_addr_t		buf_base_phys;
	uint32_t		*buf_base;
	struct sdma_conf	*conf;
	struct ssi_rate		*sr;
	struct sdma_softc	*sdma_sc;
	uint32_t		sdma_ev_rx;
	uint32_t		sdma_ev_tx;
	int			sdma_channel;
};

/* Channel registers */
struct sc_chinfo {
	struct snd_dbuf		*buffer;
	struct pcm_channel	*channel;
	struct sc_pcminfo	*parent;

	/* Channel information */
	uint32_t	dir;
	uint32_t	format;

	/* Flags */
	uint32_t	run;
};

/* PCM device private data */
struct sc_pcminfo {
	device_t		dev;
	uint32_t		(*ih)(struct sc_pcminfo *scp);
	uint32_t		chnum;
	struct sc_chinfo	chan[SSI_NCHANNELS];
	struct sc_info		*sc;
};

static struct resource_spec ssi_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int setup_dma(struct sc_pcminfo *scp);
static void setup_ssi(struct sc_info *);
static void ssi_configure_clock(struct sc_info *);

/*
 * Mixer interface.
 */

static int
ssimixer_init(struct snd_mixer *m)
{
	struct sc_pcminfo *scp;
	struct sc_info *sc;
	int mask;

	scp = mix_getdevinfo(m);
	sc = scp->sc;

	if (sc == NULL)
		return -1;

	mask = SOUND_MASK_PCM;
	mask |= SOUND_MASK_VOLUME;

	snd_mtxlock(sc->lock);
	pcm_setflags(scp->dev, pcm_getflags(scp->dev) | SD_F_SOFTPCMVOL);
	mix_setdevs(m, mask);
	snd_mtxunlock(sc->lock);

	return (0);
}

static int
ssimixer_set(struct snd_mixer *m, unsigned dev,
    unsigned left, unsigned right)
{
	struct sc_pcminfo *scp;

	scp = mix_getdevinfo(m);

	/* Here we can configure hardware volume on our DAC */

#if 1
	device_printf(scp->dev, "ssimixer_set() %d %d\n",
	    left, right);
#endif

	return (0);
}

static kobj_method_t ssimixer_methods[] = {
	KOBJMETHOD(mixer_init,      ssimixer_init),
	KOBJMETHOD(mixer_set,       ssimixer_set),
	KOBJMETHOD_END
};
MIXER_DECLARE(ssimixer);


/*
 * Channel interface.
 */

static void *
ssichan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct sc_info *sc;

	scp = (struct sc_pcminfo *)devinfo;
	sc = scp->sc;

	snd_mtxlock(sc->lock);
	ch = &scp->chan[0];
	ch->dir = dir;
	ch->run = 0;
	ch->buffer = b;
	ch->channel = c;
	ch->parent = scp;
	snd_mtxunlock(sc->lock);

	if (sndbuf_setup(ch->buffer, sc->buf_base, sc->dma_size) != 0) {
		device_printf(scp->dev, "Can't setup sndbuf.\n");
		return NULL;
	}

	return ch;
}

static int
ssichan_free(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_pcminfo *scp = ch->parent;
	struct sc_info *sc = scp->sc;

#if 0
	device_printf(scp->dev, "ssichan_free()\n");
#endif

	snd_mtxlock(sc->lock);
	/* TODO: free channel buffer */
	snd_mtxunlock(sc->lock);

	return (0);
}

static int
ssichan_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct sc_chinfo *ch = data;

	ch->format = format;

	return (0);
}

static uint32_t
ssichan_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct ssi_rate *sr;
	struct sc_info *sc;
	int threshold;
	int i;

	ch = data;
	scp = ch->parent;
	sc = scp->sc;

	sr = NULL;

	/* First look for equal frequency. */
	for (i = 0; rate_map[i].speed != 0; i++) {
		if (rate_map[i].speed == speed)
			sr = &rate_map[i];
	}

	/* If no match, just find nearest. */
	if (sr == NULL) {
		for (i = 0; rate_map[i].speed != 0; i++) {
			sr = &rate_map[i];
			threshold = sr->speed + ((rate_map[i + 1].speed != 0) ?
			    ((rate_map[i + 1].speed - sr->speed) >> 1) : 0);
			if (speed < threshold)
				break;
		}
	}

	sc->sr = sr;

	ssi_configure_clock(sc);

	return (sr->speed);
}

static void
ssi_configure_clock(struct sc_info *sc)
{
	struct ssi_rate *sr;

	sr = sc->sr;

	pll4_configure_output(sr->mfi, sr->mfn, sr->mfd);

	/* Configure other dividers here, if any */
}

static uint32_t
ssichan_setblocksize(kobj_t obj, void *data, uint32_t blocksize)
{
	struct sc_chinfo *ch = data;
	struct sc_pcminfo *scp = ch->parent;
	struct sc_info *sc = scp->sc;

	sndbuf_resize(ch->buffer, sc->dma_size / blocksize, blocksize);

	setup_dma(scp);

	return (sndbuf_getblksz(ch->buffer));
}

uint32_t
ssi_dma_intr(void *arg, int chn)
{
	struct sc_pcminfo *scp;
	struct sdma_conf *conf;
	struct sc_chinfo *ch;
	struct sc_info *sc;
	int bufsize;

	scp = arg;
	ch = &scp->chan[0];
	sc = scp->sc;
	conf = sc->conf;

	bufsize = sndbuf_getsize(ch->buffer);

	sc->pos += conf->period;
	if (sc->pos >= bufsize)
		sc->pos -= bufsize;

	if (ch->run)
		chn_intr(ch->channel);

	return (0);
}

static int
find_sdma_controller(struct sc_info *sc)
{
	struct sdma_softc *sdma_sc;
	phandle_t node, sdma_node;
	device_t sdma_dev;
	pcell_t dts_value[DMAS_TOTAL];
	int len;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	if ((len = OF_getproplen(node, "dmas")) <= 0)
		return (ENXIO);

	if (len != sizeof(dts_value)) {
		device_printf(sc->dev,
		    "\"dmas\" property length is invalid: %d (expected %d)",
		    len, sizeof(dts_value));
		return (ENXIO);
	}

	OF_getencprop(node, "dmas", dts_value, sizeof(dts_value));

	sc->sdma_ev_rx = dts_value[1];
	sc->sdma_ev_tx = dts_value[5];

	sdma_node = OF_node_from_xref(dts_value[0]);

	sdma_sc = NULL;

	sdma_dev = devclass_get_device(devclass_find("sdma"), 0);
	if (sdma_dev)
		sdma_sc = device_get_softc(sdma_dev);

	if (sdma_sc == NULL) {
		device_printf(sc->dev, "No sDMA found. Can't operate\n");
		return (ENXIO);
	}

	sc->sdma_sc = sdma_sc;

	return (0);
};

static int
setup_dma(struct sc_pcminfo *scp)
{
	struct sdma_conf *conf;
	struct sc_chinfo *ch;
	struct sc_info *sc;
	int fmt;

	ch = &scp->chan[0];
	sc = scp->sc;
	conf = sc->conf;

	conf->ih = ssi_dma_intr;
	conf->ih_user = scp;
	conf->saddr = sc->buf_base_phys;
	conf->daddr = rman_get_start(sc->res[0]) + SSI_STX0;
	conf->event = sc->sdma_ev_tx; /* SDMA TX event */
	conf->period = sndbuf_getblksz(ch->buffer);
	conf->num_bd = sndbuf_getblkcnt(ch->buffer);

	/*
	 * Word Length
	 * Can be 32, 24, 16 or 8 for sDMA.
	 *
	 * SSI supports 24 at max.
	 */

	fmt = sndbuf_getfmt(ch->buffer);

	if (fmt & AFMT_16BIT) {
		conf->word_length = 16;
		conf->command = CMD_2BYTES;
	} else if (fmt & AFMT_24BIT) {
		conf->word_length = 24;
		conf->command = CMD_3BYTES;
	} else {
		device_printf(sc->dev, "Unknown format\n");
		return (-1);
	}

	return (0);
}

static int
ssi_start(struct sc_pcminfo *scp)
{
	struct sc_info *sc;
	int reg;

	sc = scp->sc;

	if (sdma_configure(sc->sdma_channel, sc->conf) != 0) {
		device_printf(sc->dev, "Can't configure sDMA\n");
		return (-1);
	}

	/* Enable DMA interrupt */
	reg = (SIER_TDMAE);
	WRITE4(sc, SSI_SIER, reg);

	sdma_start(sc->sdma_channel);

	return (0);
}

static int
ssi_stop(struct sc_pcminfo *scp)
{
	struct sc_info *sc;
	int reg;

	sc = scp->sc;

	reg = READ4(sc, SSI_SIER);
	reg &= ~(SIER_TDMAE);
	WRITE4(sc, SSI_SIER, reg);

	sdma_stop(sc->sdma_channel);

	bzero(sc->buf_base, sc->dma_size);

	return (0);
}

static int
ssichan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct sc_info *sc;

	ch = data;
	scp = ch->parent;
	sc = scp->sc;

	snd_mtxlock(sc->lock);

	switch (go) {
	case PCMTRIG_START:
#if 0
		device_printf(scp->dev, "trigger start\n");
#endif
		ch->run = 1;

		ssi_start(scp);

		break;

	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
#if 0
		device_printf(scp->dev, "trigger stop or abort\n");
#endif
		ch->run = 0;

		ssi_stop(scp);

		break;
	}

	snd_mtxunlock(sc->lock);

	return (0);
}

static uint32_t
ssichan_getptr(kobj_t obj, void *data)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct sc_info *sc;

	ch = data;
	scp = ch->parent;
	sc = scp->sc;

	return (sc->pos);
}

static uint32_t ssi_pfmt[] = {
	SND_FORMAT(AFMT_S24_LE, 2, 0),
	0
};

static struct pcmchan_caps ssi_pcaps = {44100, 192000, ssi_pfmt, 0};

static struct pcmchan_caps *
ssichan_getcaps(kobj_t obj, void *data)
{

	return (&ssi_pcaps);
}

static kobj_method_t ssichan_methods[] = {
	KOBJMETHOD(channel_init,         ssichan_init),
	KOBJMETHOD(channel_free,         ssichan_free),
	KOBJMETHOD(channel_setformat,    ssichan_setformat),
	KOBJMETHOD(channel_setspeed,     ssichan_setspeed),
	KOBJMETHOD(channel_setblocksize, ssichan_setblocksize),
	KOBJMETHOD(channel_trigger,      ssichan_trigger),
	KOBJMETHOD(channel_getptr,       ssichan_getptr),
	KOBJMETHOD(channel_getcaps,      ssichan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(ssichan);

static int
ssi_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,imx6q-ssi"))
		return (ENXIO);

	device_set_desc(dev, "i.MX6 Synchronous Serial Interface (SSI)");
	return (BUS_PROBE_DEFAULT);
}

static void
ssi_intr(void *arg)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct sc_info *sc;

	scp = arg;
	sc = scp->sc;
	ch = &scp->chan[0];

	/* We don't use SSI interrupt */
#if 0
	device_printf(sc->dev, "SSI Intr 0x%08x\n",
	    READ4(sc, SSI_SISR));
#endif
}

static void
setup_ssi(struct sc_info *sc)
{
	int reg;

	reg = READ4(sc, SSI_STCCR);
	reg &= ~(WL3_WL0_M << WL3_WL0_S);
	reg |= (0xb << WL3_WL0_S); /* 24 bit */
	reg &= ~(DC4_DC0_M << DC4_DC0_S);
	reg |= (1 << DC4_DC0_S); /* 2 words per frame */
	reg &= ~(STCCR_DIV2); /* Divide by 1 */
	reg &= ~(STCCR_PSR); /* Divide by 1 */
	reg &= ~(PM7_PM0_M << PM7_PM0_S);
	reg |= (1 << PM7_PM0_S); /* Divide by 2 */
	WRITE4(sc, SSI_STCCR, reg);

	reg = READ4(sc, SSI_SFCSR);
	reg &= ~(SFCSR_TFWM0_M << SFCSR_TFWM0_S);
	reg |= (8 << SFCSR_TFWM0_S); /* empty slots */
	WRITE4(sc, SSI_SFCSR, reg);

	reg = READ4(sc, SSI_STCR);
	reg |= (STCR_TFEN0);
	reg &= ~(STCR_TFEN1);
	reg &= ~(STCR_TSHFD); /* MSB */
	reg |= (STCR_TXBIT0);
	reg |= (STCR_TXDIR | STCR_TFDIR);
	reg |= (STCR_TSCKP); /* falling edge */
	reg |= (STCR_TFSI);
	reg &= ~(STCR_TFSI); /* active high frame sync */
	reg &= ~(STCR_TFSL);
	reg |= STCR_TEFS;
	WRITE4(sc, SSI_STCR, reg);

	reg = READ4(sc, SSI_SCR);
	reg &= ~(SCR_I2S_MODE_M << SCR_I2S_MODE_S); /* Not master */
	reg |= (SCR_SSIEN | SCR_TE);
	reg |= (SCR_NET);
	reg |= (SCR_SYN);
	WRITE4(sc, SSI_SCR, reg);
}

static void
ssi_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	bus_addr_t *addr;

	if (err)
		return;

	addr = (bus_addr_t*)arg;
	*addr = segs[0].ds_addr;
}

static int
ssi_attach(device_t dev)
{
	char status[SND_STATUSLEN];
	struct sc_pcminfo *scp;
	struct sc_info *sc;
	int err;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->dev = dev;
	sc->sr = &rate_map[0];
	sc->pos = 0;
	sc->conf = malloc(sizeof(struct sdma_conf), M_DEVBUF, M_WAITOK | M_ZERO);

	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "ssi softc");
	if (sc->lock == NULL) {
		device_printf(dev, "Can't create mtx\n");
		return (ENXIO);
	}

	if (bus_alloc_resources(dev, ssi_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	/* SDMA */
	if (find_sdma_controller(sc)) {
		device_printf(dev, "could not find active SDMA\n");
		return (ENXIO);
	}

	/* Setup PCM */
	scp = malloc(sizeof(struct sc_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
	scp->sc = sc;
	scp->dev = dev;

	/*
	 * Maximum possible DMA buffer.
	 * Will be used partially to match 24 bit word.
	 */
	sc->dma_size = 131072;

	/*
	 * Must use dma_size boundary as modulo feature required.
	 * Modulo feature allows setup circular buffer.
	 */

	err = bus_dma_tag_create(
	    bus_get_dma_tag(sc->dev),
	    4, sc->dma_size,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    sc->dma_size, 1,		/* maxsize, nsegments */
	    sc->dma_size, 0,		/* maxsegsize, flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->dma_tag);

	err = bus_dmamem_alloc(sc->dma_tag, (void **)&sc->buf_base,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT, &sc->dma_map);
	if (err) {
		device_printf(dev, "cannot allocate framebuffer\n");
		return (ENXIO);
	}

	err = bus_dmamap_load(sc->dma_tag, sc->dma_map, sc->buf_base,
	    sc->dma_size, ssi_dmamap_cb, &sc->buf_base_phys, BUS_DMA_NOWAIT);
	if (err) {
		device_printf(dev, "cannot load DMA map\n");
		return (ENXIO);
	}

	bzero(sc->buf_base, sc->dma_size);

	/* Setup interrupt handler */
	err = bus_setup_intr(dev, sc->res[1], INTR_MPSAFE | INTR_TYPE_AV,
	    NULL, ssi_intr, scp, &sc->ih);
	if (err) {
		device_printf(dev, "Unable to alloc interrupt resource.\n");
		return (ENXIO);
	}

	pcm_setflags(dev, pcm_getflags(dev) | SD_F_MPSAFE);

	err = pcm_register(dev, scp, 1, 0);
	if (err) {
		device_printf(dev, "Can't register pcm.\n");
		return (ENXIO);
	}

	scp->chnum = 0;
	pcm_addchan(dev, PCMDIR_PLAY, &ssichan_class, scp);
	scp->chnum++;

	snprintf(status, SND_STATUSLEN, "at simplebus");
	pcm_setstatus(dev, status);

	mixer_init(dev, &ssimixer_class, scp);
	setup_ssi(sc);

	imx_ccm_ssi_configure(dev);

	sc->sdma_channel = sdma_alloc();
	if (sc->sdma_channel < 0) {
		device_printf(sc->dev, "Can't get sDMA channel\n");
		return (1);
	}

	return (0);
}

static device_method_t ssi_pcm_methods[] = {
	DEVMETHOD(device_probe,		ssi_probe),
	DEVMETHOD(device_attach,	ssi_attach),
	{ 0, 0 }
};

static driver_t ssi_pcm_driver = {
	"pcm",
	ssi_pcm_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(ssi, simplebus, ssi_pcm_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(ssi, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_DEPEND(ssi, sdma, 0, 0, 0);
MODULE_VERSION(ssi, 1);
