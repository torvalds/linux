/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Allwinner A10/A20 HDMI Audio
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/sound/pcm/sound.h>
#include <dev/sound/chip.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "sunxi_dma_if.h"
#include "mixer_if.h"

#define	DRQTYPE_HDMIAUDIO	24
#define	DRQTYPE_SDRAM		1

#define	DMA_WIDTH		32
#define	DMA_BURST_LEN		8
#define	DDMA_BLKSIZE		32
#define	DDMA_WAIT_CYC		8

#define	DMABUF_MIN		4096
#define	DMABUF_DEFAULT		65536
#define	DMABUF_MAX		131072

#define	HDMI_SAMPLERATE		48000

#define	TX_FIFO			0x01c16400

static uint32_t a10hdmiaudio_fmt[] = {
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	0
};

static struct pcmchan_caps a10hdmiaudio_pcaps = {
    HDMI_SAMPLERATE, HDMI_SAMPLERATE, a10hdmiaudio_fmt, 0
};

struct a10hdmiaudio_info;

struct a10hdmiaudio_chinfo {
	struct snd_dbuf		*buffer;
	struct pcm_channel	*channel;	
	struct a10hdmiaudio_info	*parent;
	bus_dmamap_t		dmamap;
	void			*dmaaddr;
	bus_addr_t		physaddr;
	device_t		dmac;
	void			*dmachan;

	int			run;
	uint32_t		pos;
	uint32_t		blocksize;
};

struct a10hdmiaudio_info {
	device_t		dev;
	struct mtx		*lock;
	bus_dma_tag_t		dmat;
	unsigned		dmasize;

	struct a10hdmiaudio_chinfo	play;
};

/*
 * Mixer interface
 */

static int
a10hdmiaudio_mixer_init(struct snd_mixer *m)
{
	mix_setdevs(m, SOUND_MASK_PCM);

	return (0);
}

static int
a10hdmiaudio_mixer_set(struct snd_mixer *m, unsigned dev, unsigned left,
    unsigned right)
{
	return (-1);
}

static kobj_method_t a10hdmiaudio_mixer_methods[] = {
	KOBJMETHOD(mixer_init,		a10hdmiaudio_mixer_init),
	KOBJMETHOD(mixer_set,		a10hdmiaudio_mixer_set),
	KOBJMETHOD_END
};
MIXER_DECLARE(a10hdmiaudio_mixer);


/*
 * Channel interface
 */

static void
a10hdmiaudio_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct a10hdmiaudio_chinfo *ch = arg;

	if (error != 0)
		return;

	ch->physaddr = segs[0].ds_addr;
}

static void
a10hdmiaudio_transfer(struct a10hdmiaudio_chinfo *ch)
{
	int error;

	error = SUNXI_DMA_TRANSFER(ch->dmac, ch->dmachan,
	    ch->physaddr + ch->pos, TX_FIFO, ch->blocksize);
	if (error) {
		ch->run = 0;
		device_printf(ch->parent->dev, "DMA transfer failed: %d\n",
		    error);
	}
}

static void
a10hdmiaudio_dmaconfig(struct a10hdmiaudio_chinfo *ch)
{
	struct sunxi_dma_config conf;

	memset(&conf, 0, sizeof(conf));
	conf.src_width = conf.dst_width = DMA_WIDTH;
	conf.src_burst_len = conf.dst_burst_len = DMA_BURST_LEN;
	conf.src_blksize = conf.dst_blksize = DDMA_BLKSIZE;
	conf.src_wait_cyc = conf.dst_wait_cyc = DDMA_WAIT_CYC;
	conf.src_drqtype = DRQTYPE_SDRAM;
	conf.dst_drqtype = DRQTYPE_HDMIAUDIO;
	conf.dst_noincr = true;

	SUNXI_DMA_SET_CONFIG(ch->dmac, ch->dmachan, &conf);
}

static void
a10hdmiaudio_dmaintr(void *priv)
{
	struct a10hdmiaudio_chinfo *ch = priv;
	unsigned bufsize;

	bufsize = sndbuf_getsize(ch->buffer);

	ch->pos += ch->blocksize;
	if (ch->pos >= bufsize)
		ch->pos -= bufsize;

	if (ch->run) {
		chn_intr(ch->channel);
		a10hdmiaudio_transfer(ch);
	}
}

static void
a10hdmiaudio_start(struct a10hdmiaudio_chinfo *ch)
{
	ch->pos = 0;

	/* Configure DMA channel */
	a10hdmiaudio_dmaconfig(ch);

	/* Start DMA transfer */
	a10hdmiaudio_transfer(ch);
}

static void
a10hdmiaudio_stop(struct a10hdmiaudio_chinfo *ch)
{
	/* Disable DMA channel */
	SUNXI_DMA_HALT(ch->dmac, ch->dmachan);
}

static void *
a10hdmiaudio_chan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct a10hdmiaudio_info *sc = devinfo;
	struct a10hdmiaudio_chinfo *ch = &sc->play;
	int error;

	ch->parent = sc;
	ch->channel = c;
	ch->buffer = b;

	ch->dmac = devclass_get_device(devclass_find("a10dmac"), 0);
	if (ch->dmac == NULL) {
		device_printf(sc->dev, "cannot find DMA controller\n");
		return (NULL);
	}
	ch->dmachan = SUNXI_DMA_ALLOC(ch->dmac, true, a10hdmiaudio_dmaintr, ch);
	if (ch->dmachan == NULL) {
		device_printf(sc->dev, "cannot allocate DMA channel\n");
		return (NULL);
	}

	error = bus_dmamem_alloc(sc->dmat, &ch->dmaaddr,
	    BUS_DMA_NOWAIT | BUS_DMA_COHERENT, &ch->dmamap);
	if (error != 0) {
		device_printf(sc->dev, "cannot allocate channel buffer\n");
		return (NULL);
	}
	error = bus_dmamap_load(sc->dmat, ch->dmamap, ch->dmaaddr,
	    sc->dmasize, a10hdmiaudio_dmamap_cb, ch, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->dev, "cannot load DMA map\n");
		return (NULL);
	}
	memset(ch->dmaaddr, 0, sc->dmasize);

	if (sndbuf_setup(ch->buffer, ch->dmaaddr, sc->dmasize) != 0) {
		device_printf(sc->dev, "cannot setup sndbuf\n");
		return (NULL);
	}

	return (ch);
}

static int
a10hdmiaudio_chan_free(kobj_t obj, void *data)
{
	struct a10hdmiaudio_chinfo *ch = data;
	struct a10hdmiaudio_info *sc = ch->parent;

	SUNXI_DMA_FREE(ch->dmac, ch->dmachan);
	bus_dmamap_unload(sc->dmat, ch->dmamap);
	bus_dmamem_free(sc->dmat, ch->dmaaddr, ch->dmamap);

	return (0);
}

static int
a10hdmiaudio_chan_setformat(kobj_t obj, void *data, uint32_t format)
{
	return (0);
}

static uint32_t
a10hdmiaudio_chan_setspeed(kobj_t obj, void *data, uint32_t speed)
{
	return (HDMI_SAMPLERATE);
}

static uint32_t
a10hdmiaudio_chan_setblocksize(kobj_t obj, void *data, uint32_t blocksize)
{
	struct a10hdmiaudio_chinfo *ch = data;

	ch->blocksize = blocksize & ~3;

	return (ch->blocksize);
}

static int
a10hdmiaudio_chan_trigger(kobj_t obj, void *data, int go)
{
	struct a10hdmiaudio_chinfo *ch = data;
	struct a10hdmiaudio_info *sc = ch->parent;

	if (!PCMTRIG_COMMON(go))
		return (0);

	snd_mtxlock(sc->lock);
	switch (go) {
	case PCMTRIG_START:
		ch->run = 1;
		a10hdmiaudio_start(ch);
		break;
	case PCMTRIG_STOP:
	case PCMTRIG_ABORT:
		ch->run = 0;
		a10hdmiaudio_stop(ch);
		break;
	default:
		break;
	}
	snd_mtxunlock(sc->lock);

	return (0);
}

static uint32_t
a10hdmiaudio_chan_getptr(kobj_t obj, void *data)
{
	struct a10hdmiaudio_chinfo *ch = data;

	return (ch->pos);
}

static struct pcmchan_caps *
a10hdmiaudio_chan_getcaps(kobj_t obj, void *data)
{
	return (&a10hdmiaudio_pcaps);
}

static kobj_method_t a10hdmiaudio_chan_methods[] = {
	KOBJMETHOD(channel_init,		a10hdmiaudio_chan_init),
	KOBJMETHOD(channel_free,		a10hdmiaudio_chan_free),
	KOBJMETHOD(channel_setformat,		a10hdmiaudio_chan_setformat),
	KOBJMETHOD(channel_setspeed,		a10hdmiaudio_chan_setspeed),
	KOBJMETHOD(channel_setblocksize,	a10hdmiaudio_chan_setblocksize),
	KOBJMETHOD(channel_trigger,		a10hdmiaudio_chan_trigger),
	KOBJMETHOD(channel_getptr,		a10hdmiaudio_chan_getptr),
	KOBJMETHOD(channel_getcaps,		a10hdmiaudio_chan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(a10hdmiaudio_chan);


/*
 * Device interface
 */

static int
a10hdmiaudio_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun7i-a20-hdmiaudio"))
		return (ENXIO);

	device_set_desc(dev, "Allwinner HDMI Audio");
	return (BUS_PROBE_DEFAULT);
}

static int
a10hdmiaudio_attach(device_t dev)
{
	struct a10hdmiaudio_info *sc;
	char status[SND_STATUSLEN];
	int error;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->dev = dev;
	sc->lock = snd_mtxcreate(device_get_nameunit(dev), "a10hdmiaudio softc");

	sc->dmasize = pcm_getbuffersize(dev, DMABUF_MIN,
	    DMABUF_DEFAULT, DMABUF_MAX);
	error = bus_dma_tag_create(
	    bus_get_dma_tag(dev),
	    4, sc->dmasize,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    sc->dmasize, 1,		/* maxsize, nsegs */
	    sc->dmasize, 0,		/* maxsegsize, flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->dmat);
	if (error != 0) {
		device_printf(dev, "cannot create DMA tag\n");
		goto fail;
	}

	if (mixer_init(dev, &a10hdmiaudio_mixer_class, sc)) {
		device_printf(dev, "mixer_init failed\n");
		goto fail;
	}

	pcm_setflags(dev, pcm_getflags(dev) | SD_F_MPSAFE);
	pcm_setflags(dev, pcm_getflags(dev) | SD_F_SOFTPCMVOL);

	if (pcm_register(dev, sc, 1, 0)) {
		device_printf(dev, "pcm_register failed\n");
		goto fail;
	}

	pcm_addchan(dev, PCMDIR_PLAY, &a10hdmiaudio_chan_class, sc);

	snprintf(status, SND_STATUSLEN, "at %s", ofw_bus_get_name(dev));
	pcm_setstatus(dev, status);

	return (0);

fail:
	snd_mtxfree(sc->lock);
	free(sc, M_DEVBUF);

	return (error);
}

static device_method_t a10hdmiaudio_pcm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		a10hdmiaudio_probe),
	DEVMETHOD(device_attach,	a10hdmiaudio_attach),

	DEVMETHOD_END
};

static driver_t a10hdmiaudio_pcm_driver = {
	"pcm",
	a10hdmiaudio_pcm_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(a10hdmiaudio, simplebus, a10hdmiaudio_pcm_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(a10hdmiaudio, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(a10hdmiaudio, 1);
