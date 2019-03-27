/*-
 * Copyright (c) 2016-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

/* Ingenic JZ4780 Audio Interface Controller (AIC). */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/sound/pcm/sound.h>
#include <dev/sound/chip.h>
#include <mixer_if.h>

#include <dev/extres/clk/clk.h>
#include <dev/xdma/xdma.h>

#include <mips/ingenic/jz4780_common.h>
#include <mips/ingenic/jz4780_aic.h>

#define	AIC_NCHANNELS		1

struct aic_softc {
	device_t		dev;
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	struct mtx		*lock;
	int			pos;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_addr_t		buf_base_phys;
	uint32_t		*buf_base;
	uintptr_t		aic_fifo_paddr;
	int			dma_size;
	clk_t			clk_aic;
	clk_t			clk_i2s;
	struct aic_rate		*sr;
	void			*ih;
	int			internal_codec;

	/* xDMA */
	struct xdma_channel	*xchan;
	xdma_controller_t	*xdma_tx;
	struct xdma_request	req;
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
	uint32_t		chnum;
	struct sc_chinfo	chan[AIC_NCHANNELS];
	struct aic_softc	*sc;
};

static struct resource_spec aic_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int aic_probe(device_t dev);
static int aic_attach(device_t dev);
static int aic_detach(device_t dev);
static int setup_xdma(struct sc_pcminfo *scp);

struct aic_rate {
        uint32_t speed;
};

static struct aic_rate rate_map[] = {
	{ 48000 },
	/* TODO: add more frequences */
	{ 0 },
};

/*
 * Mixer interface.
 */
static int
aicmixer_init(struct snd_mixer *m)
{
	struct sc_pcminfo *scp;
	struct aic_softc *sc;
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
aicmixer_set(struct snd_mixer *m, unsigned dev,
    unsigned left, unsigned right)
{
	struct sc_pcminfo *scp;

	scp = mix_getdevinfo(m);

	/* Here we can configure hardware volume on our DAC */

	return (0);
}

static kobj_method_t aicmixer_methods[] = {
	KOBJMETHOD(mixer_init,      aicmixer_init),
	KOBJMETHOD(mixer_set,       aicmixer_set),
	KOBJMETHOD_END
};
MIXER_DECLARE(aicmixer);

/*
 * Channel interface.
 */
static void *
aicchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct aic_softc *sc;

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

	return (ch);
}

static int
aicchan_free(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_pcminfo *scp = ch->parent;
	struct aic_softc *sc = scp->sc;

	snd_mtxlock(sc->lock);
	/* TODO: free channel buffer */
	snd_mtxunlock(sc->lock);

	return (0);
}

static int
aicchan_setformat(kobj_t obj, void *data, uint32_t format)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;

	ch = data;
	scp = ch->parent;

	ch->format = format;

	return (0);
}

static uint32_t
aicchan_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct aic_rate *sr;
	struct aic_softc *sc;
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

	/* Clocks can be reconfigured here. */

	return (sr->speed);
}

static uint32_t
aicchan_setblocksize(kobj_t obj, void *data, uint32_t blocksize)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct aic_softc *sc;

	ch = data;
	scp = ch->parent;
	sc = scp->sc;

	sndbuf_resize(ch->buffer, sc->dma_size / blocksize, blocksize);

	return (sndbuf_getblksz(ch->buffer));
}

static int
aic_intr(void *arg, xdma_transfer_status_t *status)
{
	struct sc_pcminfo *scp;
	struct xdma_request *req;
	xdma_channel_t *xchan;
	struct sc_chinfo *ch;
	struct aic_softc *sc;
	int bufsize;

	scp = arg;
	sc = scp->sc;
	ch = &scp->chan[0];
	req = &sc->req;

	xchan = sc->xchan;

	bufsize = sndbuf_getsize(ch->buffer);

	sc->pos += req->block_len;
	if (sc->pos >= bufsize)
		sc->pos -= bufsize;

	if (ch->run)
		chn_intr(ch->channel);

	return (0);
}

static int
setup_xdma(struct sc_pcminfo *scp)
{
	struct aic_softc *sc;
	struct sc_chinfo *ch;
	int fmt;
	int err;

	ch = &scp->chan[0];
	sc = scp->sc;

	fmt = sndbuf_getfmt(ch->buffer);

	KASSERT(fmt & AFMT_16BIT, ("16-bit audio supported only."));

	sc->req.operation = XDMA_CYCLIC;
	sc->req.req_type = XR_TYPE_PHYS;
	sc->req.direction = XDMA_MEM_TO_DEV;
	sc->req.src_addr = sc->buf_base_phys;
	sc->req.dst_addr = sc->aic_fifo_paddr;
	sc->req.src_width = 2;
	sc->req.dst_width = 2;
	sc->req.block_len = sndbuf_getblksz(ch->buffer);
	sc->req.block_num = sndbuf_getblkcnt(ch->buffer);

	err = xdma_request(sc->xchan, &sc->req);
	if (err != 0) {
		device_printf(sc->dev, "Can't configure virtual channel\n");
		return (-1);
	}

	xdma_control(sc->xchan, XDMA_CMD_BEGIN);

	return (0);
}

static int
aic_start(struct sc_pcminfo *scp)
{
	struct aic_softc *sc;
	int reg;

	sc = scp->sc;

	/* Ensure clock enabled. */
	reg = READ4(sc, I2SCR);
	reg |= (I2SCR_ESCLK);
	WRITE4(sc, I2SCR, reg);

	setup_xdma(scp);

	reg = (AICCR_OSS_16 | AICCR_ISS_16);
	reg |= (AICCR_CHANNEL_2);
	reg |= (AICCR_TDMS);
	reg |= (AICCR_ERPL);
	WRITE4(sc, AICCR, reg);

	return (0);
}

static int
aic_stop(struct sc_pcminfo *scp)
{
	struct aic_softc *sc;
	int reg;

	sc = scp->sc;

	reg = READ4(sc, AICCR);
	reg &= ~(AICCR_TDMS | AICCR_ERPL);
	WRITE4(sc, AICCR, reg);

	xdma_control(sc->xchan, XDMA_CMD_TERMINATE);

	return (0);
}

static int
aicchan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct aic_softc *sc;

	ch = data;
	scp = ch->parent;
	sc = scp->sc;

	snd_mtxlock(sc->lock);

	switch (go) {
	case PCMTRIG_START:
		ch->run = 1;

		sc->pos = 0;

		aic_start(scp);

		break;

	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		ch->run = 0;

		aic_stop(scp);

		sc->pos = 0;

		bzero(sc->buf_base, sc->dma_size);

		break;
	}

	snd_mtxunlock(sc->lock);

	return (0);
}

static uint32_t
aicchan_getptr(kobj_t obj, void *data)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct aic_softc *sc;

	ch = data;
	scp = ch->parent;
	sc = scp->sc;

	return (sc->pos);
}

static uint32_t aic_pfmt[] = {
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps aic_pcaps = {48000, 48000, aic_pfmt, 0};

static struct pcmchan_caps *
aicchan_getcaps(kobj_t obj, void *data)
{

	return (&aic_pcaps);
}

static kobj_method_t aicchan_methods[] = {
	KOBJMETHOD(channel_init,         aicchan_init),
	KOBJMETHOD(channel_free,         aicchan_free),
	KOBJMETHOD(channel_setformat,    aicchan_setformat),
	KOBJMETHOD(channel_setspeed,     aicchan_setspeed),
	KOBJMETHOD(channel_setblocksize, aicchan_setblocksize),
	KOBJMETHOD(channel_trigger,      aicchan_trigger),
	KOBJMETHOD(channel_getptr,       aicchan_getptr),
	KOBJMETHOD(channel_getcaps,      aicchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(aicchan);

static void
aic_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	bus_addr_t *addr;

	if (err)
		return;

	addr = (bus_addr_t*)arg;
	*addr = segs[0].ds_addr;
}

static int
aic_dma_setup(struct aic_softc *sc)
{
	device_t dev;
	int err;

	dev = sc->dev;

	/* DMA buffer size. */
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
	if (err) {
		device_printf(dev, "cannot create bus dma tag\n");
		return (-1);
	}

	err = bus_dmamem_alloc(sc->dma_tag, (void **)&sc->buf_base,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT, &sc->dma_map);
	if (err) {
		device_printf(dev, "cannot allocate memory\n");
		return (-1);
	}

	err = bus_dmamap_load(sc->dma_tag, sc->dma_map, sc->buf_base,
	    sc->dma_size, aic_dmamap_cb, &sc->buf_base_phys, BUS_DMA_WAITOK);
	if (err) {
		device_printf(dev, "cannot load DMA map\n");
		return (-1);
	}

	bzero(sc->buf_base, sc->dma_size);

	return (0);
}

static int
aic_configure_clocks(struct aic_softc *sc)
{
	uint64_t aic_freq;
	uint64_t i2s_freq;
	device_t dev;
	int err;

	dev = sc->dev;

	err = clk_get_by_ofw_name(sc->dev, 0, "aic", &sc->clk_aic);
	if (err != 0) {
		device_printf(dev, "Can't find aic clock.\n");
		return (-1);
	}

	err = clk_enable(sc->clk_aic);
	if (err != 0) {
		device_printf(dev, "Can't enable aic clock.\n");
		return (-1);
	}

	err = clk_get_by_ofw_name(sc->dev, 0, "i2s", &sc->clk_i2s);
	if (err != 0) {
		device_printf(dev, "Can't find i2s clock.\n");
		return (-1);
	}

	err = clk_enable(sc->clk_i2s);
	if (err != 0) {
		device_printf(dev, "Can't enable i2s clock.\n");
		return (-1);
	}

	err = clk_set_freq(sc->clk_i2s, 12000000, 0);
	if (err != 0) {
		device_printf(dev, "Can't set i2s frequency.\n");
		return (-1);
	}

	clk_get_freq(sc->clk_aic, &aic_freq);
	clk_get_freq(sc->clk_i2s, &i2s_freq);

	device_printf(dev, "Frequency aic %d i2s %d\n",
	    (uint32_t)aic_freq, (uint32_t)i2s_freq);

	return (0);
}

static int
aic_configure(struct aic_softc *sc)
{
	int reg;

	WRITE4(sc, AICFR, AICFR_RST);

	/* Configure AIC */
	reg = 0;
	if (sc->internal_codec) {
		reg |= (AICFR_ICDC);
	} else {
		reg |= (AICFR_SYNCD | AICFR_BCKD);
	}
	reg |= (AICFR_AUSEL);	/* I2S/MSB-justified format. */
	reg |= (AICFR_TFTH(8));	/* Transmit FIFO threshold */
	reg |= (AICFR_RFTH(7));	/* Receive FIFO threshold */
	WRITE4(sc, AICFR, reg);

	reg = READ4(sc, AICFR);
	reg |= (AICFR_ENB);	/* Enable the controller. */
	WRITE4(sc, AICFR, reg);

	return (0);
}

static int
sysctl_hw_pcm_internal_codec(SYSCTL_HANDLER_ARGS)
{
	struct sc_pcminfo *scp;
	struct sc_chinfo *ch;
	struct aic_softc *sc;
	int error, val;

	if (arg1 == NULL)
		return (EINVAL);

	scp = arg1;
	sc = scp->sc;
	ch = &scp->chan[0];

	snd_mtxlock(sc->lock);

	val = sc->internal_codec;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error || req->newptr == NULL) {
		snd_mtxunlock(sc->lock);
		return (error);
	}
	if (val < 0 || val > 1) {
		snd_mtxunlock(sc->lock);
		return (EINVAL);
	}

	if (sc->internal_codec != val) {
		sc->internal_codec = val;
		if (ch->run)
			aic_stop(scp);
		aic_configure(sc);
		if (ch->run)
			aic_start(scp);
	}

	snd_mtxunlock(sc->lock);

	return (0);
}

static int
aic_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-i2s"))
		return (ENXIO);

	device_set_desc(dev, "Audio Interface Controller");

	return (BUS_PROBE_DEFAULT);
}

static int
aic_attach(device_t dev)
{
	char status[SND_STATUSLEN];
	struct sc_pcminfo *scp;
	struct aic_softc *sc;
	int err;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->dev = dev;
	sc->pos = 0;
	sc->internal_codec = 1;

	/* Get xDMA controller */
	sc->xdma_tx = xdma_ofw_get(sc->dev, "tx");
	if (sc->xdma_tx == NULL) {
		device_printf(dev, "Can't find DMA controller.\n");
		return (ENXIO);
	}

	/* Alloc xDMA virtual channel. */
	sc->xchan = xdma_channel_alloc(sc->xdma_tx, 0);
	if (sc->xchan == NULL) {
		device_printf(dev, "Can't alloc virtual DMA channel.\n");
		return (ENXIO);
	}

	/* Setup sound subsystem */
	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "aic softc");
	if (sc->lock == NULL) {
		device_printf(dev, "Can't create mtx.\n");
		return (ENXIO);
	}

	if (bus_alloc_resources(dev, aic_spec, sc->res)) {
		device_printf(dev,
		    "could not allocate resources for device\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);
	sc->aic_fifo_paddr = rman_get_start(sc->res[0]) + AICDR;

	/* Setup PCM. */
	scp = malloc(sizeof(struct sc_pcminfo), M_DEVBUF, M_WAITOK | M_ZERO);
	scp->sc = sc;
	scp->dev = dev;

	/* Setup audio buffer. */
	err = aic_dma_setup(sc);
	if (err != 0) {
		device_printf(dev, "Can't setup sound buffer.\n");
		return (ENXIO);
	}

	/* Setup clocks. */
	err = aic_configure_clocks(sc);
	if (err != 0) {
		device_printf(dev, "Can't configure clocks.\n");
		return (ENXIO);
	}

	err = aic_configure(sc);
	if (err != 0) {
		device_printf(dev, "Can't configure AIC.\n");
		return (ENXIO);
	}

	pcm_setflags(dev, pcm_getflags(dev) | SD_F_MPSAFE);

	/* Setup interrupt handler. */
	err = xdma_setup_intr(sc->xchan, aic_intr, scp, &sc->ih);
	if (err) {
		device_printf(sc->dev,
		    "Can't setup xDMA interrupt handler.\n");
		return (ENXIO);
	}

	err = pcm_register(dev, scp, 1, 0);
	if (err) {
		device_printf(dev, "Can't register pcm.\n");
		return (ENXIO);
	}

	scp->chnum = 0;
	pcm_addchan(dev, PCMDIR_PLAY, &aicchan_class, scp);
	scp->chnum++;

	snprintf(status, SND_STATUSLEN, "at %s", ofw_bus_get_name(dev));
	pcm_setstatus(dev, status);

	mixer_init(dev, &aicmixer_class, scp);

	/* Create device sysctl node. */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "internal_codec", CTLTYPE_INT | CTLFLAG_RW,
	    scp, 0, sysctl_hw_pcm_internal_codec, "I",
	    "use internal audio codec");

	return (0);
}

static int
aic_detach(device_t dev)
{
	struct aic_softc *sc;

	sc = device_get_softc(dev);

	xdma_channel_free(sc->xchan);

	bus_release_resources(dev, aic_spec, sc->res);

	return (0);
}

static device_method_t aic_pcm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aic_probe),
	DEVMETHOD(device_attach,	aic_attach),
	DEVMETHOD(device_detach,	aic_detach),
	DEVMETHOD_END
};

static driver_t aic_pcm_driver = {
	"pcm",
	aic_pcm_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(aic, simplebus, aic_pcm_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(aic, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(aic, 1);
