/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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
 * Vybrid Family Synchronous Audio Interface (SAI)
 * Chapter 51, Vybrid Reference Manual, Rev. 5, 07/2013
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
#include <sys/watchdog.h>

#include <dev/sound/pcm/sound.h>
#include <dev/sound/chip.h>
#include <mixer_if.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <arm/freescale/vybrid/vf_common.h>
#include <arm/freescale/vybrid/vf_dmamux.h>
#include <arm/freescale/vybrid/vf_edma.h>

#define	I2S_TCSR	0x00	/* SAI Transmit Control */
#define	I2S_TCR1	0x04	/* SAI Transmit Configuration 1 */
#define	I2S_TCR2	0x08	/* SAI Transmit Configuration 2 */
#define	I2S_TCR3	0x0C	/* SAI Transmit Configuration 3 */
#define	I2S_TCR4	0x10	/* SAI Transmit Configuration 4 */
#define	I2S_TCR5	0x14	/* SAI Transmit Configuration 5 */
#define	I2S_TDR0	0x20	/* SAI Transmit Data */
#define	I2S_TFR0	0x40	/* SAI Transmit FIFO */
#define	I2S_TMR		0x60	/* SAI Transmit Mask */
#define	I2S_RCSR	0x80	/* SAI Receive Control */
#define	I2S_RCR1	0x84	/* SAI Receive Configuration 1 */
#define	I2S_RCR2	0x88	/* SAI Receive Configuration 2 */
#define	I2S_RCR3	0x8C	/* SAI Receive Configuration 3 */
#define	I2S_RCR4	0x90	/* SAI Receive Configuration 4 */
#define	I2S_RCR5	0x94	/* SAI Receive Configuration 5 */
#define	I2S_RDR0	0xA0	/* SAI Receive Data */
#define	I2S_RFR0	0xC0	/* SAI Receive FIFO */
#define	I2S_RMR		0xE0	/* SAI Receive Mask */

#define	TCR1_TFW_M	0x1f		/* Transmit FIFO Watermark Mask */
#define	TCR1_TFW_S	0		/* Transmit FIFO Watermark Shift */
#define	TCR2_MSEL_M	0x3		/* MCLK Select Mask*/
#define	TCR2_MSEL_S	26		/* MCLK Select Shift*/
#define	TCR2_BCP	(1 << 25)	/* Bit Clock Polarity */
#define	TCR2_BCD	(1 << 24)	/* Bit Clock Direction */
#define	TCR3_TCE	(1 << 16)	/* Transmit Channel Enable */
#define	TCR4_FRSZ_M	0x1f		/* Frame size Mask */
#define	TCR4_FRSZ_S	16		/* Frame size Shift */
#define	TCR4_SYWD_M	0x1f		/* Sync Width Mask */
#define	TCR4_SYWD_S	8		/* Sync Width Shift */
#define	TCR4_MF		(1 << 4)	/* MSB First */
#define	TCR4_FSE	(1 << 3)	/* Frame Sync Early */
#define	TCR4_FSP	(1 << 1)	/* Frame Sync Polarity Low */
#define	TCR4_FSD	(1 << 0)	/* Frame Sync Direction Master */
#define	TCR5_FBT_M	0x1f		/* First Bit Shifted */
#define	TCR5_FBT_S	8		/* First Bit Shifted */
#define	TCR5_W0W_M	0x1f		/* Word 0 Width */
#define	TCR5_W0W_S	16		/* Word 0 Width */
#define	TCR5_WNW_M	0x1f		/* Word N Width */
#define	TCR5_WNW_S	24		/* Word N Width */
#define	TCSR_TE		(1 << 31)	/* Transmitter Enable */
#define	TCSR_BCE	(1 << 28)	/* Bit Clock Enable */
#define	TCSR_FRDE	(1 << 0)	/* FIFO Request DMA Enable */

#define	SAI_NCHANNELS	1

static MALLOC_DEFINE(M_SAI, "sai", "sai audio");

struct sai_rate {
	uint32_t speed;
	uint32_t div; /* Bit Clock Divide. Division value is (div + 1) * 2. */
	uint32_t mfi; /* PLL4 Multiplication Factor Integer */
	uint32_t mfn; /* PLL4 Multiplication Factor Numerator */
	uint32_t mfd; /* PLL4 Multiplication Factor Denominator */
};

/*
 * Bit clock divider formula
 * (div + 1) * 2 = MCLK/(nch * LRCLK * bits/1000000),
 * where:
 *   MCLK - master clock
 *   nch - number of channels
 *   LRCLK - left right clock
 * e.g. (div + 1) * 2 = 16.9344/(2 * 44100 * 24/1000000)
 *
 * Example for 96khz, 24bit, 18.432 Mhz mclk (192fs)
 * { 96000, 1, 18, 40176000, 93000000 },
 */

static struct sai_rate rate_map[] = {
	{ 44100, 7, 33, 80798400, 93000000 }, /* 33.8688 Mhz */
	{ 96000, 3, 36, 80352000, 93000000 }, /* 36.864 Mhz */
	{ 192000, 1, 36, 80352000, 93000000 }, /* 36.864 Mhz */
	{ 0, 0 },
};

struct sc_info {
	struct resource		*res[2];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	device_t		dev;
	struct mtx		*lock;
	uint32_t		speed;
	uint32_t		period;
	void			*ih;
	int			pos;
	int			dma_size;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_addr_t		buf_base_phys;
	uint32_t		*buf_base;
	struct tcd_conf		*tcd;
	struct sai_rate		*sr;
	struct edma_softc	*edma_sc;
	int			edma_chnum;
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
	uint32_t		(*ih) (struct sc_pcminfo *scp);
	uint32_t		chnum;
	struct sc_chinfo	chan[SAI_NCHANNELS];
	struct sc_info		*sc;
};

static struct resource_spec sai_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static int setup_dma(struct sc_pcminfo *scp);
static void setup_sai(struct sc_info *);
static void sai_configure_clock(struct sc_info *);

/*
 * Mixer interface.
 */

static int
saimixer_init(struct snd_mixer *m)
{
	struct sc_pcminfo *scp;
	struct sc_info *sc;
	int mask;

	scp = mix_getdevinfo(m);
	sc = scp->sc;

	if (sc == NULL)
		return -1;

	mask = SOUND_MASK_PCM;

	snd_mtxlock(sc->lock);
	pcm_setflags(scp->dev, pcm_getflags(scp->dev) | SD_F_SOFTPCMVOL);
	mix_setdevs(m, mask);
	snd_mtxunlock(sc->lock);

	return (0);
}

static int
saimixer_set(struct snd_mixer *m, unsigned dev,
    unsigned left, unsigned right)
{
	struct sc_pcminfo *scp;

	scp = mix_getdevinfo(m);

#if 0
	device_printf(scp->dev, "saimixer_set() %d %d\n",
	    left, right);
#endif

	return (0);
}

static kobj_method_t saimixer_methods[] = {
	KOBJMETHOD(mixer_init,      saimixer_init),
	KOBJMETHOD(mixer_set,       saimixer_set),
	KOBJMETHOD_END
};
MIXER_DECLARE(saimixer);

/*
 * Channel interface.
 */

static void *
saichan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
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
saichan_free(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_pcminfo *scp = ch->parent;
	struct sc_info *sc = scp->sc;

#if 0
	device_printf(scp->dev, "saichan_free()\n");
#endif

	snd_mtxlock(sc->lock);
	/* TODO: free channel buffer */
	snd_mtxunlock(sc->lock);

	return (0);
}

static int
saichan_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct sc_chinfo *ch = data;

	ch->format = format;

	return (0);
}

static uint32_t
saichan_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct sai_rate *sr;
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

	sai_configure_clock(sc);

	return (sr->speed);
}

static void
sai_configure_clock(struct sc_info *sc)
{
	struct sai_rate *sr;
	int reg;

	sr = sc->sr;

	/*
	 * Manual says that TCR/RCR registers must not be
	 * altered when TCSR[TE] is set.
	 * We ignore it since we have problem sometimes
	 * after re-enabling transmitter (DMA goes stall).
	 */

	reg = READ4(sc, I2S_TCR2);
	reg &= ~(0xff << 0);
	reg |= (sr->div << 0);
	WRITE4(sc, I2S_TCR2, reg);

	pll4_configure_output(sr->mfi, sr->mfn, sr->mfd);
}

static uint32_t
saichan_setblocksize(kobj_t obj, void *data, uint32_t blocksize)
{
	struct sc_chinfo *ch = data;
	struct sc_pcminfo *scp = ch->parent;
	struct sc_info *sc = scp->sc;

	sndbuf_resize(ch->buffer, sc->dma_size / blocksize, blocksize);

	sc->period = sndbuf_getblksz(ch->buffer);
	return (sc->period);
}

uint32_t sai_dma_intr(void *arg, int chn);
uint32_t
sai_dma_intr(void *arg, int chn)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct sc_info *sc;
	struct tcd_conf *tcd;

	scp = arg;
	ch = &scp->chan[0];

	sc = scp->sc;
	tcd = sc->tcd;

	sc->pos += (tcd->nbytes * tcd->nmajor);
	if (sc->pos >= sc->dma_size)
		sc->pos -= sc->dma_size;

	if (ch->run)
		chn_intr(ch->channel);

	return (0);
}

static int
find_edma_controller(struct sc_info *sc)
{
	struct edma_softc *edma_sc;
	phandle_t node, edma_node;
	int edma_src_transmit;
	int edma_mux_group;
	int edma_device_id;
	device_t edma_dev;
	int dts_value;
	int len;
	int i;

	if ((node = ofw_bus_get_node(sc->dev)) == -1)
		return (ENXIO);

	if ((len = OF_getproplen(node, "edma-controller")) <= 0)
		return (ENXIO);
	if ((len = OF_getproplen(node, "edma-src-transmit")) <= 0)
		return (ENXIO);
	if ((len = OF_getproplen(node, "edma-mux-group")) <= 0)
		return (ENXIO);

	OF_getencprop(node, "edma-src-transmit", &dts_value, len);
	edma_src_transmit = dts_value;
	OF_getencprop(node, "edma-mux-group", &dts_value, len);
	edma_mux_group = dts_value;
	OF_getencprop(node, "edma-controller", &dts_value, len);
	edma_node = OF_node_from_xref(dts_value);

	if ((len = OF_getproplen(edma_node, "device-id")) <= 0) {
		return (ENXIO);
	}

	OF_getencprop(edma_node, "device-id", &dts_value, len);
	edma_device_id = dts_value;

	edma_sc = NULL;

	for (i = 0; i < EDMA_NUM_DEVICES; i++) {
		edma_dev = devclass_get_device(devclass_find("edma"), i);
		if (edma_dev) {
			edma_sc = device_get_softc(edma_dev);
			if (edma_sc->device_id == edma_device_id) {
				/* found */
				break;
			}

			edma_sc = NULL;
		}
	}

	if (edma_sc == NULL) {
		device_printf(sc->dev, "no eDMA. can't operate\n");
		return (ENXIO);
	}

	sc->edma_sc = edma_sc;

	sc->edma_chnum = edma_sc->channel_configure(edma_sc, edma_mux_group,
	    edma_src_transmit);
	if (sc->edma_chnum < 0) {
		/* cant setup eDMA */
		return (ENXIO);
	}

	return (0);
};

static int
setup_dma(struct sc_pcminfo *scp)
{
	struct tcd_conf *tcd;
	struct sc_info *sc;

	sc = scp->sc;

	tcd = malloc(sizeof(struct tcd_conf), M_DEVBUF, M_WAITOK | M_ZERO);
	tcd->channel = sc->edma_chnum;
	tcd->ih = sai_dma_intr;
	tcd->ih_user = scp;
	tcd->saddr = sc->buf_base_phys;
	tcd->daddr = rman_get_start(sc->res[0]) + I2S_TDR0;

	/*
	 * Bytes to transfer per each minor loop.
	 * Hardware FIFO buffer size is 32x32bits.
	 */
	tcd->nbytes = 64;

	tcd->nmajor = 512;
	tcd->smod = 17;	/* dma_size range */
	tcd->dmod = 0;
	tcd->esg = 0;
	tcd->soff = 0x4;
	tcd->doff = 0;
	tcd->ssize = 0x2;
	tcd->dsize = 0x2;
	tcd->slast = 0;
	tcd->dlast_sga = 0;

	sc->tcd = tcd;

	sc->edma_sc->dma_setup(sc->edma_sc, sc->tcd);

	return (0);
}

static int
saichan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_chinfo *ch = data;
	struct sc_pcminfo *scp = ch->parent;
	struct sc_info *sc = scp->sc;

	snd_mtxlock(sc->lock);

	switch (go) {
	case PCMTRIG_START:
#if 0
		device_printf(scp->dev, "trigger start\n");
#endif
		ch->run = 1;
		break;

	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
#if 0
		device_printf(scp->dev, "trigger stop or abort\n");
#endif
		ch->run = 0;
		break;
	}

	snd_mtxunlock(sc->lock);

	return (0);
}

static uint32_t
saichan_getptr(kobj_t obj, void *data)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct sc_info *sc;

	ch = data;
	scp = ch->parent;
	sc = scp->sc;

	return (sc->pos);
}

static uint32_t sai_pfmt[] = {
	/*
	 * eDMA doesn't allow 24-bit coping,
	 * so we use 32.
	 */
	SND_FORMAT(AFMT_S32_LE, 2, 0),
	0
};

static struct pcmchan_caps sai_pcaps = {44100, 192000, sai_pfmt, 0};

static struct pcmchan_caps *
saichan_getcaps(kobj_t obj, void *data)
{

	return (&sai_pcaps);
}

static kobj_method_t saichan_methods[] = {
	KOBJMETHOD(channel_init,         saichan_init),
	KOBJMETHOD(channel_free,         saichan_free),
	KOBJMETHOD(channel_setformat,    saichan_setformat),
	KOBJMETHOD(channel_setspeed,     saichan_setspeed),
	KOBJMETHOD(channel_setblocksize, saichan_setblocksize),
	KOBJMETHOD(channel_trigger,      saichan_trigger),
	KOBJMETHOD(channel_getptr,       saichan_getptr),
	KOBJMETHOD(channel_getcaps,      saichan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(saichan);

static int
sai_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fsl,mvf600-sai"))
		return (ENXIO);

	device_set_desc(dev, "Vybrid Family Synchronous Audio Interface");
	return (BUS_PROBE_DEFAULT);
}

static void
sai_intr(void *arg)
{
	struct sc_pcminfo *scp;
	struct sc_info *sc;

	scp = arg;
	sc = scp->sc;

	device_printf(sc->dev, "Error I2S_TCSR == 0x%08x\n",
	    READ4(sc, I2S_TCSR));
}

static void
setup_sai(struct sc_info *sc)
{
	int reg;

	/*
	 * TCR/RCR registers must not be altered when TCSR[TE] is set.
	 */

	reg = READ4(sc, I2S_TCSR);
	reg &= ~(TCSR_BCE | TCSR_TE | TCSR_FRDE);
	WRITE4(sc, I2S_TCSR, reg);

	reg = READ4(sc, I2S_TCR3);
	reg &= ~(TCR3_TCE);
	WRITE4(sc, I2S_TCR3, reg);

	reg = (64 << TCR1_TFW_S);
	WRITE4(sc, I2S_TCR1, reg);

	reg = READ4(sc, I2S_TCR2);
	reg &= ~(TCR2_MSEL_M << TCR2_MSEL_S);
	reg |= (1 << TCR2_MSEL_S);
	reg |= (TCR2_BCP | TCR2_BCD);
	WRITE4(sc, I2S_TCR2, reg);

	sai_configure_clock(sc);

	reg = READ4(sc, I2S_TCR3);
	reg |= (TCR3_TCE);
	WRITE4(sc, I2S_TCR3, reg);

	/* Configure to 32-bit I2S mode */
	reg = READ4(sc, I2S_TCR4);
	reg &= ~(TCR4_FRSZ_M << TCR4_FRSZ_S);
	reg |= (1 << TCR4_FRSZ_S); /* 2 words per frame */
	reg &= ~(TCR4_SYWD_M << TCR4_SYWD_S);
	reg |= (23 << TCR4_SYWD_S);
	reg |= (TCR4_MF | TCR4_FSE | TCR4_FSP | TCR4_FSD);
	WRITE4(sc, I2S_TCR4, reg);

	reg = READ4(sc, I2S_TCR5);
	reg &= ~(TCR5_W0W_M << TCR5_W0W_S);
	reg |= (23 << TCR5_W0W_S);
	reg &= ~(TCR5_WNW_M << TCR5_WNW_S);
	reg |= (23 << TCR5_WNW_S);
	reg &= ~(TCR5_FBT_M << TCR5_FBT_S);
	reg |= (31 << TCR5_FBT_S);
	WRITE4(sc, I2S_TCR5, reg);

	/* Enable transmitter */
	reg = READ4(sc, I2S_TCSR);
	reg |= (TCSR_BCE | TCSR_TE | TCSR_FRDE);
	reg |= (1 << 10); /* FEIE */
	WRITE4(sc, I2S_TCSR, reg);
}


static void
sai_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	bus_addr_t *addr;

	if (err)
		return;

	addr = (bus_addr_t*)arg;
	*addr = segs[0].ds_addr;
}

static int
sai_attach(device_t dev)
{
	char status[SND_STATUSLEN];
	struct sc_pcminfo *scp;
	struct sc_info *sc;
	int err;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->dev = dev;
	sc->sr = &rate_map[0];
	sc->pos = 0;

	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "sai softc");
	if (sc->lock == NULL) {
		device_printf(dev, "Cant create mtx\n");
		return (ENXIO);
	}

	if (bus_alloc_resources(dev, sai_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	/* eDMA */
	if (find_edma_controller(sc)) {
		device_printf(dev, "could not find active eDMA\n");
		return (ENXIO);
	}

	/* Setup PCM */
	scp = malloc(sizeof(struct sc_pcminfo), M_DEVBUF, M_NOWAIT | M_ZERO);
	scp->sc = sc;
	scp->dev = dev;

	/* DMA */
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
	    sc->dma_size, sai_dmamap_cb, &sc->buf_base_phys, BUS_DMA_NOWAIT);
	if (err) {
		device_printf(dev, "cannot load DMA map\n");
		return (ENXIO);
	}

	bzero(sc->buf_base, sc->dma_size);

	/* Setup interrupt handler */
	err = bus_setup_intr(dev, sc->res[1], INTR_MPSAFE | INTR_TYPE_AV,
	    NULL, sai_intr, scp, &sc->ih);
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
	pcm_addchan(dev, PCMDIR_PLAY, &saichan_class, scp);
	scp->chnum++;

	snprintf(status, SND_STATUSLEN, "at simplebus");
	pcm_setstatus(dev, status);

	mixer_init(dev, &saimixer_class, scp);

	setup_dma(scp);
	setup_sai(sc);

	return (0);
}

static device_method_t sai_pcm_methods[] = {
	DEVMETHOD(device_probe,		sai_probe),
	DEVMETHOD(device_attach,	sai_attach),
	{ 0, 0 }
};

static driver_t sai_pcm_driver = {
	"pcm",
	sai_pcm_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(sai, simplebus, sai_pcm_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(sai, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(sai, 1);
