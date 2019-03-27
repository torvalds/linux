/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Jason L. Wright (jason@thought.net)
 * Copyright (c) 2004 Pyun YongHyeon
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 *	from: OpenBSD: cs4231.c,v 1.21 2003/07/03 20:36:07 jason Exp
 */

/*
 * Driver for CS4231 based audio found in some sun4m systems (cs4231)
 * based on ideas from the S/Linux project and the NetBSD project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/resource.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>
#include <machine/bus.h>
#include <machine/ofw_machdep.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/sbus/apcdmareg.h>
#include <dev/sound/sbus/cs4231.h>

#include <sparc64/sbus/sbusvar.h>
#include <sparc64/ebus/ebusreg.h>

#include "mixer_if.h"

/*
 * The driver supports CS4231A audio chips found on Sbus/Ebus based 
 * UltraSPARCs. Though, CS4231A says it supports full-duplex mode, I
 * doubt it due to the lack of independent sampling frequency register
 * for playback/capture.
 * Since I couldn't find any documentation for APCDMA programming
 * information, I guessed the usage of APCDMA from that of OpenBSD's
 * driver. The EBDMA information of PCIO can be obtained from
 *  http://solutions.sun.com/embedded/databook/web/microprocessors/pcio.html
 * And CS4231A datasheet can also be obtained from
 *  ftp://ftp.alsa-project.org/pub/manuals/cirrus/4231a.pdf
 *
 * Audio capture(recording) was not tested at all and may have bugs.
 * Sorry, I don't have microphone. Don't try to use full-duplex mode.
 * It wouldn't work.
 */
#define CS_TIMEOUT		90000

#define CS4231_MIN_BUF_SZ	(16*1024)
#define CS4231_DEFAULT_BUF_SZ	(32*1024)
#define CS4231_MAX_BUF_SZ	(64*1024)
#define CS4231_MAX_BLK_SZ	(8*1024)
#define CS4231_MAX_APC_DMA_SZ	(8*1024)


#undef CS4231_DEBUG
#ifdef CS4231_DEBUG
#define DPRINTF(x)		printf x
#else
#define DPRINTF(x)
#endif
#define CS4231_AUTO_CALIBRATION

struct cs4231_softc;

struct cs4231_channel {
	struct cs4231_softc	*parent;
	struct pcm_channel	*channel;
	struct snd_dbuf		*buffer;
	u_int32_t		format;
	u_int32_t		speed;
	u_int32_t		nextaddr;
	u_int32_t		togo;
	int			dir;
	int			locked;
};

#define CS4231_RES_MEM_MAX	4
#define CS4231_RES_IRQ_MAX	2
struct cs4231_softc {
	device_t		sc_dev;
	int			sc_rid[CS4231_RES_MEM_MAX];
	struct resource		*sc_res[CS4231_RES_MEM_MAX];
	bus_space_handle_t	sc_regh[CS4231_RES_MEM_MAX];
	bus_space_tag_t		sc_regt[CS4231_RES_MEM_MAX];

	int			sc_irqrid[CS4231_RES_IRQ_MAX];
	struct resource		*sc_irqres[CS4231_RES_IRQ_MAX];
	void			*sc_ih[CS4231_RES_IRQ_MAX];
	bus_dma_tag_t		sc_dmat[CS4231_RES_IRQ_MAX];
	int			sc_burst;

	u_int32_t		sc_bufsz;
	struct cs4231_channel	sc_pch;
	struct cs4231_channel	sc_rch;
	int			sc_enabled;
	int			sc_nmres;
	int			sc_nires;
	int			sc_codecv;
	int			sc_chipvid;
	int			sc_flags;
#define CS4231_SBUS		0x01
#define CS4231_EBUS		0x02

	struct mtx		*sc_lock;
};

struct mix_table {
	u_int32_t	reg:8;
	u_int32_t	bits:8;
	u_int32_t	mute:8;
	u_int32_t	shift:4;
	u_int32_t	neg:1;
	u_int32_t	avail:1;
	u_int32_t	recdev:1;
};

static int	cs4231_bus_probe(device_t);
static int	cs4231_sbus_attach(device_t);
static int	cs4231_ebus_attach(device_t);
static int	cs4231_attach_common(struct cs4231_softc *);
static int	cs4231_bus_detach(device_t);
static int	cs4231_bus_suspend(device_t);
static int	cs4231_bus_resume(device_t);
static void	cs4231_getversion(struct cs4231_softc *);
static void	cs4231_free_resource(struct cs4231_softc *);
static void	cs4231_ebdma_reset(struct cs4231_softc *);
static void	cs4231_power_reset(struct cs4231_softc *, int);
static int	cs4231_enable(struct cs4231_softc *, int);
static void	cs4231_disable(struct cs4231_softc *);
static void	cs4231_write(struct cs4231_softc *, u_int8_t, u_int8_t);
static u_int8_t cs4231_read(struct cs4231_softc *, u_int8_t);
static void	cs4231_sbus_intr(void *);
static void	cs4231_ebus_pintr(void *arg);
static void	cs4231_ebus_cintr(void *arg);
static int	cs4231_mixer_init(struct snd_mixer *);
static void	cs4231_mixer_set_value(struct cs4231_softc *,
    const struct mix_table *, u_int8_t);
static int	cs4231_mixer_set(struct snd_mixer *, u_int32_t, u_int32_t,
    u_int32_t);
static u_int32_t	cs4231_mixer_setrecsrc(struct snd_mixer *, u_int32_t);
static void	*cs4231_chan_init(kobj_t, void *, struct snd_dbuf *,
    struct pcm_channel *, int);
static int	cs4231_chan_setformat(kobj_t, void *, u_int32_t);
static u_int32_t	cs4231_chan_setspeed(kobj_t, void *, u_int32_t);
static void	cs4231_chan_fs(struct cs4231_softc *, int, u_int8_t);
static u_int32_t	cs4231_chan_setblocksize(kobj_t, void *, u_int32_t);
static int	cs4231_chan_trigger(kobj_t, void *, int);
static u_int32_t	cs4231_chan_getptr(kobj_t, void *);
static struct pcmchan_caps *
    cs4231_chan_getcaps(kobj_t, void *);
static void	cs4231_trigger(struct cs4231_channel *);
static void	cs4231_apcdma_trigger(struct cs4231_softc *,
    struct cs4231_channel *);
static void	cs4231_ebdma_trigger(struct cs4231_softc *,
    struct cs4231_channel *);
static void	cs4231_halt(struct cs4231_channel *);

#define CS4231_LOCK(sc)		snd_mtxlock(sc->sc_lock)
#define CS4231_UNLOCK(sc)	snd_mtxunlock(sc->sc_lock)
#define CS4231_LOCK_ASSERT(sc)	snd_mtxassert(sc->sc_lock)

#define CS_WRITE(sc,r,v)	\
    bus_space_write_1((sc)->sc_regt[0], (sc)->sc_regh[0], (r) << 2, (v))
#define CS_READ(sc,r)		\
    bus_space_read_1((sc)->sc_regt[0], (sc)->sc_regh[0], (r) << 2)

#define APC_WRITE(sc,r,v)	\
    bus_space_write_4(sc->sc_regt[0], sc->sc_regh[0], r, v)
#define APC_READ(sc,r)		\
    bus_space_read_4(sc->sc_regt[0], sc->sc_regh[0], r)

#define EBDMA_P_WRITE(sc,r,v)	\
    bus_space_write_4((sc)->sc_regt[1], (sc)->sc_regh[1], (r), (v))
#define EBDMA_P_READ(sc,r)	\
    bus_space_read_4((sc)->sc_regt[1], (sc)->sc_regh[1], (r))

#define EBDMA_C_WRITE(sc,r,v)	\
    bus_space_write_4((sc)->sc_regt[2], (sc)->sc_regh[2], (r), (v))
#define EBDMA_C_READ(sc,r)	\
    bus_space_read_4((sc)->sc_regt[2], (sc)->sc_regh[2], (r))

#define AUXIO_CODEC		0x00
#define AUXIO_WRITE(sc,r,v)	\
    bus_space_write_4((sc)->sc_regt[3], (sc)->sc_regh[3], (r), (v))
#define AUXIO_READ(sc,r)	\
    bus_space_read_4((sc)->sc_regt[3], (sc)->sc_regh[3], (r))

#define CODEC_WARM_RESET	0
#define CODEC_COLD_RESET	1

/* SBus */
static device_method_t cs4231_sbus_methods[] = {
	DEVMETHOD(device_probe,		cs4231_bus_probe),
	DEVMETHOD(device_attach,	cs4231_sbus_attach),
	DEVMETHOD(device_detach,	cs4231_bus_detach),
	DEVMETHOD(device_suspend,	cs4231_bus_suspend),
	DEVMETHOD(device_resume,	cs4231_bus_resume),

	DEVMETHOD_END
};

static driver_t cs4231_sbus_driver = {
	"pcm",
	cs4231_sbus_methods,
	PCM_SOFTC_SIZE
};

DRIVER_MODULE(snd_audiocs, sbus, cs4231_sbus_driver, pcm_devclass, 0, 0);

/* EBus */
static device_method_t cs4231_ebus_methods[] = {
	DEVMETHOD(device_probe,		cs4231_bus_probe),
	DEVMETHOD(device_attach,	cs4231_ebus_attach),
	DEVMETHOD(device_detach,	cs4231_bus_detach),
	DEVMETHOD(device_suspend,	cs4231_bus_suspend),
	DEVMETHOD(device_resume,	cs4231_bus_resume),

	DEVMETHOD_END
};

static driver_t cs4231_ebus_driver = {
	"pcm",
	cs4231_ebus_methods,
	PCM_SOFTC_SIZE
};

DRIVER_MODULE(snd_audiocs, ebus, cs4231_ebus_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_audiocs, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_audiocs, 1);


static u_int32_t cs4231_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_MU_LAW, 1, 0),
	SND_FORMAT(AFMT_MU_LAW, 2, 0),
	SND_FORMAT(AFMT_A_LAW, 1, 0),
	SND_FORMAT(AFMT_A_LAW, 2, 0),
	SND_FORMAT(AFMT_IMA_ADPCM, 1, 0),
	SND_FORMAT(AFMT_IMA_ADPCM, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_S16_BE, 1, 0),
	SND_FORMAT(AFMT_S16_BE, 2, 0),
	0
};

static struct pcmchan_caps cs4231_caps = {5510, 48000, cs4231_fmt, 0};

/*
 * sound(4) channel interface
 */
static kobj_method_t cs4231_chan_methods[] = {
	KOBJMETHOD(channel_init,		cs4231_chan_init),
	KOBJMETHOD(channel_setformat,		cs4231_chan_setformat),
	KOBJMETHOD(channel_setspeed,		cs4231_chan_setspeed),
	KOBJMETHOD(channel_setblocksize,	cs4231_chan_setblocksize),
	KOBJMETHOD(channel_trigger,		cs4231_chan_trigger),
	KOBJMETHOD(channel_getptr,		cs4231_chan_getptr),
	KOBJMETHOD(channel_getcaps,		cs4231_chan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(cs4231_chan); 

/*
 * sound(4) mixer interface
 */
static kobj_method_t cs4231_mixer_methods[] = {
	KOBJMETHOD(mixer_init,		cs4231_mixer_init),
	KOBJMETHOD(mixer_set,		cs4231_mixer_set),
	KOBJMETHOD(mixer_setrecsrc,	cs4231_mixer_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(cs4231_mixer);

static int
cs4231_bus_probe(device_t dev)
{
	const char *compat, *name;

	compat = ofw_bus_get_compat(dev);
	name = ofw_bus_get_name(dev);
	if (strcmp("SUNW,CS4231", name) == 0 ||
	    (compat != NULL && strcmp("SUNW,CS4231", compat) == 0)) {
		device_set_desc(dev, "Sun Audiocs");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
cs4231_sbus_attach(device_t dev)
{
	struct cs4231_softc *sc;
	int burst;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_dev = dev;
	/*
	 * XXX
	 * No public documentation exists on programming burst size of APCDMA.
	 */
	burst = sbus_get_burstsz(sc->sc_dev);
	if ((burst & SBUS_BURST_64))
		sc->sc_burst = 64;
	else if ((burst & SBUS_BURST_32))
		sc->sc_burst = 32;
	else if ((burst & SBUS_BURST_16))
		sc->sc_burst = 16;
	else
		sc->sc_burst = 0;
	sc->sc_flags = CS4231_SBUS;
	sc->sc_nmres = 1;
	sc->sc_nires = 1;
	return cs4231_attach_common(sc);
}

static int
cs4231_ebus_attach(device_t dev)
{
	struct cs4231_softc *sc;

	sc = malloc(sizeof(struct cs4231_softc), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc == NULL) {
		device_printf(dev, "cannot allocate softc\n");
		return (ENOMEM);
	}
	sc->sc_dev = dev;
	sc->sc_burst = EBDCSR_BURST_1;
	sc->sc_nmres = CS4231_RES_MEM_MAX;
	sc->sc_nires = CS4231_RES_IRQ_MAX;
	sc->sc_flags = CS4231_EBUS;
	return cs4231_attach_common(sc);
}

static int
cs4231_attach_common(struct cs4231_softc *sc)
{
	char status[SND_STATUSLEN];
	driver_intr_t *ihandler;
	int i;

	sc->sc_lock = snd_mtxcreate(device_get_nameunit(sc->sc_dev),
	    "snd_cs4231 softc");

	for (i = 0; i < sc->sc_nmres; i++) {
		sc->sc_rid[i] = i;
		if ((sc->sc_res[i] = bus_alloc_resource_any(sc->sc_dev,
		    SYS_RES_MEMORY, &sc->sc_rid[i], RF_ACTIVE)) == NULL) {
			device_printf(sc->sc_dev,
			    "cannot map register %d\n", i);
			goto fail;
		}
		sc->sc_regt[i] = rman_get_bustag(sc->sc_res[i]);
		sc->sc_regh[i] = rman_get_bushandle(sc->sc_res[i]);
	}
	for (i = 0; i < sc->sc_nires; i++) {
		sc->sc_irqrid[i] = i;
		if ((sc->sc_irqres[i] = bus_alloc_resource_any(sc->sc_dev,
		    SYS_RES_IRQ, &sc->sc_irqrid[i], RF_SHAREABLE | RF_ACTIVE))
		    == NULL) {
			if ((sc->sc_flags & CS4231_SBUS) != 0)
				device_printf(sc->sc_dev,
				    "cannot allocate interrupt\n");
			else
				device_printf(sc->sc_dev, "cannot allocate %s "
				    "interrupt\n", i == 0 ? "capture" :
				    "playback");
			goto fail;
		}
	}

	ihandler = cs4231_sbus_intr;
	for (i = 0; i < sc->sc_nires; i++) {
		if ((sc->sc_flags & CS4231_EBUS) != 0) {
			if (i == 0)
				ihandler = cs4231_ebus_cintr;
			else
				ihandler = cs4231_ebus_pintr;
		}
		if (snd_setup_intr(sc->sc_dev, sc->sc_irqres[i], INTR_MPSAFE,
		    ihandler, sc, &sc->sc_ih[i])) {
			if ((sc->sc_flags & CS4231_SBUS) != 0)
				device_printf(sc->sc_dev,
				    "cannot set up interrupt\n");
			else
				device_printf(sc->sc_dev, "cannot set up %s "
				    " interrupt\n", i == 0 ? "capture" :
				    "playback");
			goto fail;
		}
	}

	sc->sc_bufsz = pcm_getbuffersize(sc->sc_dev, CS4231_MIN_BUF_SZ,
	    CS4231_DEFAULT_BUF_SZ, CS4231_MAX_BUF_SZ);
	for (i = 0; i < sc->sc_nires; i++) {
		if (bus_dma_tag_create(
		    bus_get_dma_tag(sc->sc_dev),/* parent */
		    64, 0,			/* alignment, boundary */
		    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filtfunc, filtfuncarg */
		    sc->sc_bufsz,		/* maxsize */
		    1,				/* nsegments */
		    sc->sc_bufsz,		/* maxsegsz */
		    BUS_DMA_ALLOCNOW,		/* flags */
		    NULL,			/* lockfunc */
		    NULL,			/* lockfuncarg */
		    &sc->sc_dmat[i])) {
			if ((sc->sc_flags & CS4231_SBUS) != 0)
				device_printf(sc->sc_dev,
				    "cannot allocate DMA tag\n");
			else
				device_printf(sc->sc_dev, "cannot allocate %s "
				    "DMA tag\n", i == 0 ? "capture" :
				    "playback");
			goto fail;
		}
	}
	cs4231_enable(sc, CODEC_WARM_RESET);
	cs4231_getversion(sc);
	if (mixer_init(sc->sc_dev, &cs4231_mixer_class, sc) != 0)
		goto fail;
	if (pcm_register(sc->sc_dev, sc, 1, 1)) {
		device_printf(sc->sc_dev, "cannot register to pcm\n");
		goto fail;
	}
	if (pcm_addchan(sc->sc_dev, PCMDIR_REC, &cs4231_chan_class, sc) != 0)
		goto chan_fail;
	if (pcm_addchan(sc->sc_dev, PCMDIR_PLAY, &cs4231_chan_class, sc) != 0)
		goto chan_fail;
	if ((sc->sc_flags & CS4231_SBUS) != 0)
		snprintf(status, SND_STATUSLEN, "at mem 0x%lx irq %ld bufsz %u",
		    rman_get_start(sc->sc_res[0]),
		    rman_get_start(sc->sc_irqres[0]), sc->sc_bufsz);
	else
		snprintf(status, SND_STATUSLEN, "at io 0x%lx 0x%lx 0x%lx 0x%lx "
		    "irq %ld %ld bufsz %u", rman_get_start(sc->sc_res[0]),
		    rman_get_start(sc->sc_res[1]),
		    rman_get_start(sc->sc_res[2]),
		    rman_get_start(sc->sc_res[3]),
		    rman_get_start(sc->sc_irqres[0]),
		    rman_get_start(sc->sc_irqres[1]), sc->sc_bufsz);
	pcm_setstatus(sc->sc_dev, status);
	return (0);

chan_fail:
	pcm_unregister(sc->sc_dev);
fail:
	cs4231_free_resource(sc);
	return (ENXIO);
}

static int
cs4231_bus_detach(device_t dev)
{
	struct cs4231_softc *sc;
	struct cs4231_channel *pch, *rch;
	int error;

	sc = pcm_getdevinfo(dev);
	CS4231_LOCK(sc);
	pch = &sc->sc_pch;
	rch = &sc->sc_pch;
	if (pch->locked || rch->locked) {
		CS4231_UNLOCK(sc);
		return (EBUSY);
	}
	/*
	 * Since EBDMA requires valid DMA buffer to drain its FIFO, we need
	 * real DMA buffer for draining.
	 */
	if ((sc->sc_flags & CS4231_EBUS) != 0)
		cs4231_ebdma_reset(sc);
	CS4231_UNLOCK(sc);
	error = pcm_unregister(dev);
	if (error)
		return (error);
	cs4231_free_resource(sc);
	return (0);
}

static int
cs4231_bus_suspend(device_t dev)
{

	return (ENXIO);
}

static int
cs4231_bus_resume(device_t dev)
{

	return (ENXIO);
}

static void
cs4231_getversion(struct cs4231_softc *sc)
{
	u_int8_t v;

	v = cs4231_read(sc, CS_MISC_INFO);
	sc->sc_codecv = v & CS_CODEC_ID_MASK;
	v = cs4231_read(sc, CS_VERSION_ID);
	v &= (CS_VERSION_NUMBER | CS_VERSION_CHIPID);
	sc->sc_chipvid = v;
	switch(v) {
		case 0x80:
			device_printf(sc->sc_dev, "<CS4231 Codec Id. %d>\n",
			    sc->sc_codecv);
			break;
		case 0xa0:
			device_printf(sc->sc_dev, "<CS4231A Codec Id. %d>\n",
			    sc->sc_codecv);
			break;
		case 0x82:
			device_printf(sc->sc_dev, "<CS4232 Codec Id. %d>\n",
			    sc->sc_codecv);
			break;
		default:
			device_printf(sc->sc_dev,
			    "<Unknown 0x%x Codec Id. %d\n", v, sc->sc_codecv);
			break;
	}
}

static void
cs4231_ebdma_reset(struct cs4231_softc *sc)
{
	int i;

	/* playback */
	EBDMA_P_WRITE(sc, EBDMA_DCSR,
	    EBDMA_P_READ(sc, EBDMA_DCSR) & ~(EBDCSR_INTEN | EBDCSR_NEXTEN));
	EBDMA_P_WRITE(sc, EBDMA_DCSR, EBDCSR_RESET);
	for (i = CS_TIMEOUT;
	    i && EBDMA_P_READ(sc, EBDMA_DCSR) & EBDCSR_DRAIN; i--)
		DELAY(1);
	if (i == 0)
		device_printf(sc->sc_dev,
		    "timeout waiting for playback DMA reset\n");
	EBDMA_P_WRITE(sc, EBDMA_DCSR, sc->sc_burst);
	/* capture */
	EBDMA_C_WRITE(sc, EBDMA_DCSR,
	    EBDMA_C_READ(sc, EBDMA_DCSR) & ~(EBDCSR_INTEN | EBDCSR_NEXTEN));
	EBDMA_C_WRITE(sc, EBDMA_DCSR, EBDCSR_RESET);
	for (i = CS_TIMEOUT;
	    i && EBDMA_C_READ(sc, EBDMA_DCSR) & EBDCSR_DRAIN; i--)
		DELAY(1);
	if (i == 0)
		device_printf(sc->sc_dev,
		    "timeout waiting for capture DMA reset\n");
	EBDMA_C_WRITE(sc, EBDMA_DCSR, sc->sc_burst);
}

static void
cs4231_power_reset(struct cs4231_softc *sc, int how)
{
	u_int32_t v;
	int i;

	if ((sc->sc_flags & CS4231_SBUS) != 0) {
		APC_WRITE(sc, APC_CSR, APC_CSR_RESET);
		DELAY(10);
		APC_WRITE(sc, APC_CSR, 0);
		DELAY(10);
		APC_WRITE(sc,
		    APC_CSR, APC_READ(sc, APC_CSR) | APC_CSR_CODEC_RESET);
		DELAY(20);
		APC_WRITE(sc,
		    APC_CSR, APC_READ(sc, APC_CSR) & (~APC_CSR_CODEC_RESET));
	} else {
		v = AUXIO_READ(sc, AUXIO_CODEC);
		if (how == CODEC_WARM_RESET && v != 0) {
			AUXIO_WRITE(sc, AUXIO_CODEC, 0);
			DELAY(20);
		} else if (how == CODEC_COLD_RESET){
			AUXIO_WRITE(sc, AUXIO_CODEC, 1);
			DELAY(20);
			AUXIO_WRITE(sc, AUXIO_CODEC, 0);
			DELAY(20);
		}
		cs4231_ebdma_reset(sc);
	}

	for (i = CS_TIMEOUT;
	    i && CS_READ(sc, CS4231_IADDR) == CS_IN_INIT; i--)
		DELAY(10);
	if (i == 0)
		device_printf(sc->sc_dev, "timeout waiting for reset\n");

	/* turn on cs4231 mode */
	cs4231_write(sc, CS_MISC_INFO,
	    cs4231_read(sc, CS_MISC_INFO) | CS_MODE2);
	/* enable interrupts & clear CSR */
        cs4231_write(sc, CS_PIN_CONTROL,
            cs4231_read(sc, CS_PIN_CONTROL) | INTERRUPT_ENABLE);
	CS_WRITE(sc, CS4231_STATUS, 0);
	/* enable DAC output */
	cs4231_write(sc, CS_LEFT_OUTPUT_CONTROL,
	    cs4231_read(sc, CS_LEFT_OUTPUT_CONTROL) & ~OUTPUT_MUTE);
	cs4231_write(sc, CS_RIGHT_OUTPUT_CONTROL,
	    cs4231_read(sc, CS_RIGHT_OUTPUT_CONTROL) & ~OUTPUT_MUTE);
	/* mute AUX1 since it generates noises */
	cs4231_write(sc, CS_LEFT_AUX1_CONTROL,
	    cs4231_read(sc, CS_LEFT_AUX1_CONTROL) | AUX_INPUT_MUTE);
	cs4231_write(sc, CS_RIGHT_AUX1_CONTROL,
	    cs4231_read(sc, CS_RIGHT_AUX1_CONTROL) | AUX_INPUT_MUTE);
	/* protect buffer underrun and set output level to 0dB */
	cs4231_write(sc, CS_ALT_FEATURE1,
	    cs4231_read(sc, CS_ALT_FEATURE1) | CS_DAC_ZERO | CS_OUTPUT_LVL);
	/* enable high pass filter, dual xtal was disabled due to noises */
	cs4231_write(sc, CS_ALT_FEATURE2,
	    cs4231_read(sc, CS_ALT_FEATURE2) | CS_HPF_ENABLE);
}

static int
cs4231_enable(struct cs4231_softc *sc, int how)
{
	cs4231_power_reset(sc, how);
	sc->sc_enabled = 1;
        return (0);
}

static void
cs4231_disable(struct cs4231_softc *sc)
{
	u_int8_t v;

	CS4231_LOCK_ASSERT(sc);

	if (sc->sc_enabled == 0)
		return;
	sc->sc_enabled = 0;
	CS4231_UNLOCK(sc);
	cs4231_halt(&sc->sc_pch);
	cs4231_halt(&sc->sc_rch);
	CS4231_LOCK(sc);
	v = cs4231_read(sc, CS_PIN_CONTROL) & ~INTERRUPT_ENABLE;
	cs4231_write(sc, CS_PIN_CONTROL, v);

	if ((sc->sc_flags & CS4231_SBUS) != 0) {
		APC_WRITE(sc, APC_CSR, APC_CSR_RESET);
		DELAY(10);
		APC_WRITE(sc, APC_CSR, 0);
		DELAY(10);
	} else
		cs4231_ebdma_reset(sc);
}

static void
cs4231_free_resource(struct cs4231_softc *sc)
{
	int i;

	CS4231_LOCK(sc);
	cs4231_disable(sc);
	CS4231_UNLOCK(sc);
	for (i = 0; i < sc->sc_nires; i++) {
		if (sc->sc_irqres[i]) {
			if (sc->sc_ih[i]) {
				bus_teardown_intr(sc->sc_dev, sc->sc_irqres[i],
				    sc->sc_ih[i]);
				sc->sc_ih[i] = NULL;
			}
			bus_release_resource(sc->sc_dev, SYS_RES_IRQ,
			    sc->sc_irqrid[i], sc->sc_irqres[i]);
			sc->sc_irqres[i] = NULL;
		}
	}
	for (i = 0; i < sc->sc_nires; i++) {
		if (sc->sc_dmat[i])
			bus_dma_tag_destroy(sc->sc_dmat[i]);
	}
	for (i = 0; i < sc->sc_nmres; i++) {
		if (sc->sc_res[i])
			bus_release_resource(sc->sc_dev, SYS_RES_MEMORY,
			    sc->sc_rid[i], sc->sc_res[i]);
	}
	snd_mtxfree(sc->sc_lock);
	free(sc, M_DEVBUF);
}

static void
cs4231_write(struct cs4231_softc *sc, u_int8_t r, u_int8_t v)
{
	CS_WRITE(sc, CS4231_IADDR, r);
	CS_WRITE(sc, CS4231_IDATA, v);
}

static u_int8_t
cs4231_read(struct cs4231_softc *sc, u_int8_t r)
{
	CS_WRITE(sc, CS4231_IADDR, r);
	return (CS_READ(sc, CS4231_IDATA));
}

static void
cs4231_sbus_intr(void *arg)
{
	struct cs4231_softc *sc;
	struct cs4231_channel *pch, *rch;
	u_int32_t csr;
	u_int8_t status;

	sc = arg;
	CS4231_LOCK(sc);

	csr = APC_READ(sc, APC_CSR);
	if ((csr & APC_CSR_GI) == 0) {
		CS4231_UNLOCK(sc);
		return;
	}
	APC_WRITE(sc, APC_CSR, csr);

	if ((csr & APC_CSR_EIE) && (csr & APC_CSR_EI)) {
		status = cs4231_read(sc, CS_TEST_AND_INIT);
		device_printf(sc->sc_dev,
		    "apc error interrupt : stat = 0x%x\n", status);
	}

	pch = rch = NULL;
	if ((csr & APC_CSR_PMIE) && (csr & APC_CSR_PMI)) {
		u_long nextaddr, saddr;
		u_int32_t togo;

		pch = &sc->sc_pch;
		togo = pch->togo;
		saddr = sndbuf_getbufaddr(pch->buffer);
		nextaddr = pch->nextaddr + togo;
		if (nextaddr >=  saddr + sndbuf_getsize(pch->buffer))
			nextaddr = saddr;
		APC_WRITE(sc, APC_PNVA, nextaddr);
		APC_WRITE(sc, APC_PNC, togo);
		pch->nextaddr = nextaddr;
	}

	if ((csr & APC_CSR_CIE) && (csr & APC_CSR_CI) && (csr & APC_CSR_CD)) {
		u_long nextaddr, saddr;
		u_int32_t togo;

		rch = &sc->sc_rch;
		togo = rch->togo;
		saddr = sndbuf_getbufaddr(rch->buffer);
		nextaddr = rch->nextaddr + togo;
		if (nextaddr >= saddr + sndbuf_getsize(rch->buffer))
			nextaddr = saddr; 
		APC_WRITE(sc, APC_CNVA, nextaddr);
		APC_WRITE(sc, APC_CNC, togo);
		rch->nextaddr = nextaddr;
	}
	CS4231_UNLOCK(sc);
	if (pch)
		chn_intr(pch->channel);
	if (rch)
		chn_intr(rch->channel);
}

/* playback interrupt handler */
static void
cs4231_ebus_pintr(void *arg)
{
	struct cs4231_softc *sc;
	struct cs4231_channel *ch;
	u_int32_t csr;
	u_int8_t status;

	sc = arg;
	CS4231_LOCK(sc);

	csr = EBDMA_P_READ(sc, EBDMA_DCSR);
	if ((csr & EBDCSR_INT) == 0) {
		CS4231_UNLOCK(sc);
		return;
	}

	if ((csr & EBDCSR_ERR)) {
		status = cs4231_read(sc, CS_TEST_AND_INIT);
		device_printf(sc->sc_dev,
		    "ebdma error interrupt : stat = 0x%x\n", status);
	}
	EBDMA_P_WRITE(sc, EBDMA_DCSR, csr | EBDCSR_TC);

	ch = NULL;
	if (csr & EBDCSR_TC) {
		u_long nextaddr, saddr;
		u_int32_t togo;

		ch = &sc->sc_pch;
		togo = ch->togo;
		saddr = sndbuf_getbufaddr(ch->buffer);
		nextaddr = ch->nextaddr + togo;
		if (nextaddr >=  saddr + sndbuf_getsize(ch->buffer))
			nextaddr = saddr;
		/*
		 * EBDMA_DCNT is loaded automatically
		 * EBDMA_P_WRITE(sc, EBDMA_DCNT, togo);
		 */
		EBDMA_P_WRITE(sc, EBDMA_DADDR, nextaddr);
		ch->nextaddr = nextaddr;
	}
	CS4231_UNLOCK(sc);
	if (ch)
		chn_intr(ch->channel);
}

/* capture interrupt handler */
static void
cs4231_ebus_cintr(void *arg)
{
	struct cs4231_softc *sc;
	struct cs4231_channel *ch;
	u_int32_t csr;
	u_int8_t status;

	sc = arg;
	CS4231_LOCK(sc);

	csr = EBDMA_C_READ(sc, EBDMA_DCSR);
	if ((csr & EBDCSR_INT) == 0) {
		CS4231_UNLOCK(sc);
		return;
	}
	if ((csr & EBDCSR_ERR)) {
		status = cs4231_read(sc, CS_TEST_AND_INIT);
		device_printf(sc->sc_dev,
		    "dma error interrupt : stat = 0x%x\n", status);
	}
	EBDMA_C_WRITE(sc, EBDMA_DCSR, csr | EBDCSR_TC);

	ch = NULL;
	if (csr & EBDCSR_TC) {
		u_long nextaddr, saddr;
		u_int32_t togo;

		ch = &sc->sc_rch;
		togo = ch->togo;
		saddr = sndbuf_getbufaddr(ch->buffer);
		nextaddr = ch->nextaddr + togo;
		if (nextaddr >= saddr + sndbuf_getblksz(ch->buffer))
			nextaddr = saddr; 
		/*
		 * EBDMA_DCNT is loaded automatically
		 * EBDMA_C_WRITE(sc, EBDMA_DCNT, togo);
		 */
		EBDMA_C_WRITE(sc, EBDMA_DADDR, nextaddr);
		ch->nextaddr = nextaddr;
	}
	CS4231_UNLOCK(sc);
	if (ch)
		chn_intr(ch->channel);
}

static const struct mix_table cs4231_mix_table[SOUND_MIXER_NRDEVICES][2] = {
	[SOUND_MIXER_PCM] = {
		{ CS_LEFT_OUTPUT_CONTROL,	6, OUTPUT_MUTE, 0, 1, 1, 0 },
		{ CS_RIGHT_OUTPUT_CONTROL,	6, OUTPUT_MUTE, 0, 1, 1, 0 }
	},
	[SOUND_MIXER_SPEAKER] = {
		{ CS_MONO_IO_CONTROL,		4, MONO_OUTPUT_MUTE, 0, 1, 1, 0 },
		{ CS_REG_NONE,			0, 0, 0, 0, 1, 0 }
	},
	[SOUND_MIXER_LINE] = {
		{ CS_LEFT_LINE_CONTROL,		5, LINE_INPUT_MUTE, 0, 1, 1, 1 },
		{ CS_RIGHT_LINE_CONTROL,	5, LINE_INPUT_MUTE, 0, 1, 1, 1 }
	},
	/*
	 * AUX1 : removed intentionally since it generates noises
	 * AUX2 : Ultra1/Ultra2 has no internal CD-ROM audio in
	 */
	[SOUND_MIXER_CD] = {
		{ CS_LEFT_AUX2_CONTROL,		5, LINE_INPUT_MUTE, 0, 1, 1, 1 },
		{ CS_RIGHT_AUX2_CONTROL,	5, LINE_INPUT_MUTE, 0, 1, 1, 1 }
	},
	[SOUND_MIXER_MIC] = {
		{ CS_LEFT_INPUT_CONTROL,	4, 0, 0, 0, 1, 1 },
		{ CS_RIGHT_INPUT_CONTROL,	4, 0, 0, 0, 1, 1 }
	},
	[SOUND_MIXER_IGAIN] = {
		{ CS_LEFT_INPUT_CONTROL,	4, 0, 0, 1, 0 },
		{ CS_RIGHT_INPUT_CONTROL,	4, 0, 0, 1, 0 }
	}
};

static int
cs4231_mixer_init(struct snd_mixer *m)
{
	u_int32_t v;
	int i;

	v = 0;
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		if (cs4231_mix_table[i][0].avail != 0)
			v |= (1 << i);
	mix_setdevs(m, v);
	v = 0;
	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		if (cs4231_mix_table[i][0].recdev != 0)
			v |= (1 << i);
	mix_setrecdevs(m, v);
	return (0);
}

static void
cs4231_mixer_set_value(struct cs4231_softc *sc,  const struct mix_table *mt,
    u_int8_t v)
{
	u_int8_t mask, reg;
	u_int8_t old, shift, val;

	if (mt->avail == 0 || mt->reg == CS_REG_NONE)
		return;
	reg = mt->reg;
	if (mt->neg != 0)
		val = 100 - v;
	else
		val = v;
	mask = (1 << mt->bits) - 1;
	val = ((val * mask) + 50) / 100;
	shift = mt->shift;
	val <<= shift;
	if (v == 0)
		val |= mt->mute;
	old = cs4231_read(sc, reg);
	old &= ~(mt->mute | (mask << shift));
	val |= old;
	if (reg == CS_LEFT_INPUT_CONTROL || reg == CS_RIGHT_INPUT_CONTROL) {
		if ((val & (mask << shift)) != 0)
			val |= ADC_INPUT_GAIN_ENABLE;
		else
			val &= ~ADC_INPUT_GAIN_ENABLE;
	}
	cs4231_write(sc, reg, val);	
}

static int
cs4231_mixer_set(struct snd_mixer *m, u_int32_t dev, u_int32_t left,
    u_int32_t right)
{
	struct cs4231_softc *sc;

	sc = mix_getdevinfo(m);
	CS4231_LOCK(sc);
	cs4231_mixer_set_value(sc, &cs4231_mix_table[dev][0], left);
	cs4231_mixer_set_value(sc, &cs4231_mix_table[dev][1], right);
	CS4231_UNLOCK(sc);

	return (left | (right << 8));
}

static u_int32_t
cs4231_mixer_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	struct cs4231_softc *sc;
	u_int8_t	v;

	sc = mix_getdevinfo(m);
	switch (src) {
	case SOUND_MASK_LINE:
		v = CS_IN_LINE;
		break;

	case SOUND_MASK_CD:
		v = CS_IN_DAC;
		break;

	case SOUND_MASK_MIC:
	default:
		v = CS_IN_MIC;
		src = SOUND_MASK_MIC;
		break;
	}
	CS4231_LOCK(sc);
	cs4231_write(sc, CS_LEFT_INPUT_CONTROL,
	    (cs4231_read(sc, CS_LEFT_INPUT_CONTROL) & CS_IN_MASK) | v);
	cs4231_write(sc, CS_RIGHT_INPUT_CONTROL,
	    (cs4231_read(sc, CS_RIGHT_INPUT_CONTROL) & CS_IN_MASK) | v);
	CS4231_UNLOCK(sc);

	return (src);
}

static void *
cs4231_chan_init(kobj_t obj, void *dev, struct snd_dbuf *b,
    struct pcm_channel *c, int dir)
{
	struct cs4231_softc *sc;
	struct cs4231_channel *ch;
	bus_dma_tag_t dmat;

	sc = dev;
	ch = (dir == PCMDIR_PLAY) ? &sc->sc_pch : &sc->sc_rch;
	ch->parent = sc;
	ch->channel = c;
	ch->dir = dir;
	ch->buffer = b;
	if ((sc->sc_flags & CS4231_SBUS) != 0)
		dmat = sc->sc_dmat[0];
	else {
		if (dir == PCMDIR_PLAY)
			dmat = sc->sc_dmat[1];
		else
			dmat = sc->sc_dmat[0];
	}
	if (sndbuf_alloc(ch->buffer, dmat, 0, sc->sc_bufsz) != 0)
		return (NULL);
	DPRINTF(("%s channel addr: 0x%lx\n", dir == PCMDIR_PLAY ? "playback" :
	    "capture", sndbuf_getbufaddr(ch->buffer)));

	return (ch);
}

static int
cs4231_chan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct cs4231_softc *sc;
	struct cs4231_channel *ch;
	u_int32_t encoding;
	u_int8_t fs, v;

	ch = data;
	sc = ch->parent;

	CS4231_LOCK(sc);
	if (ch->format == format) {
		CS4231_UNLOCK(sc);
		return (0);
	}

	encoding = AFMT_ENCODING(format);
	fs = 0;
	switch (encoding) {
	case AFMT_U8:
		fs = CS_AFMT_U8;
		break;
	case AFMT_MU_LAW:
		fs = CS_AFMT_MU_LAW;
		break;
	case AFMT_S16_LE:
		fs = CS_AFMT_S16_LE;
		break;
	case AFMT_A_LAW:
		fs = CS_AFMT_A_LAW;
		break;
	case AFMT_IMA_ADPCM:
		fs = CS_AFMT_IMA_ADPCM;
		break;
	case AFMT_S16_BE:
		fs = CS_AFMT_S16_BE;
		break;
	default:
		fs = CS_AFMT_U8;
		format = AFMT_U8;
		break;
	}

	if (AFMT_CHANNEL(format) > 1)
		fs |= CS_AFMT_STEREO;
	
	DPRINTF(("FORMAT: %s : 0x%x\n", ch->dir == PCMDIR_PLAY ? "playback" :
	    "capture", format));
	v = cs4231_read(sc, CS_CLOCK_DATA_FORMAT);
	v &= CS_CLOCK_DATA_FORMAT_MASK;
	fs |= v;
	cs4231_chan_fs(sc, ch->dir, fs);
	ch->format = format;
	CS4231_UNLOCK(sc);

	return (0);
}

static u_int32_t
cs4231_chan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	typedef struct {
		u_int32_t speed;
		u_int8_t bits;
	} speed_struct;

	const static speed_struct speed_table[] = {
		{5510,  (0 << 1) | CLOCK_XTAL2},
		{5510,  (0 << 1) | CLOCK_XTAL2},
		{6620,  (7 << 1) | CLOCK_XTAL2},
		{8000,  (0 << 1) | CLOCK_XTAL1},
		{9600,  (7 << 1) | CLOCK_XTAL1},
		{11025, (1 << 1) | CLOCK_XTAL2},
		{16000, (1 << 1) | CLOCK_XTAL1},
		{18900, (2 << 1) | CLOCK_XTAL2},
		{22050, (3 << 1) | CLOCK_XTAL2},
		{27420, (2 << 1) | CLOCK_XTAL1},
		{32000, (3 << 1) | CLOCK_XTAL1},
		{33075, (6 << 1) | CLOCK_XTAL2},
		{33075, (4 << 1) | CLOCK_XTAL2},
		{44100, (5 << 1) | CLOCK_XTAL2},
		{48000, (6 << 1) | CLOCK_XTAL1},
	};

	struct cs4231_softc *sc;
	struct cs4231_channel *ch;
	int i, n, sel;
	u_int8_t fs;

	ch = data;
	sc = ch->parent;
	CS4231_LOCK(sc);
	if (ch->speed == speed) {
		CS4231_UNLOCK(sc);
		return (speed);
	}
	n = sizeof(speed_table) / sizeof(speed_struct);

	for (i = 1, sel =0; i < n - 1; i++)
		if (abs(speed - speed_table[i].speed) <
		    abs(speed - speed_table[sel].speed))
			sel = i;	
	DPRINTF(("SPEED: %s : %dHz -> %dHz\n", ch->dir == PCMDIR_PLAY ?
	    "playback" : "capture", speed, speed_table[sel].speed));
	speed = speed_table[sel].speed;

	fs = cs4231_read(sc, CS_CLOCK_DATA_FORMAT);
	fs &= ~CS_CLOCK_DATA_FORMAT_MASK;
	fs |= speed_table[sel].bits;
	cs4231_chan_fs(sc, ch->dir, fs);
	ch->speed = speed;
	CS4231_UNLOCK(sc);

	return (speed);
}

static void
cs4231_chan_fs(struct cs4231_softc *sc, int dir, u_int8_t fs)
{
	int i, doreset;
#ifdef CS4231_AUTO_CALIBRATION
	u_int8_t v;
#endif

	CS4231_LOCK_ASSERT(sc);

	/* set autocalibration */
	doreset = 0;
#ifdef CS4231_AUTO_CALIBRATION
	v = cs4231_read(sc, CS_INTERFACE_CONFIG) | AUTO_CAL_ENABLE;
	CS_WRITE(sc, CS4231_IADDR, MODE_CHANGE_ENABLE);
	CS_WRITE(sc, CS4231_IADDR, MODE_CHANGE_ENABLE | CS_INTERFACE_CONFIG);
	CS_WRITE(sc, CS4231_IDATA, v);
#endif

	/*
	 * We always need to write CS_CLOCK_DATA_FORMAT register since
	 * the clock frequency is shared with playback/capture.
	 */
	CS_WRITE(sc, CS4231_IADDR, MODE_CHANGE_ENABLE | CS_CLOCK_DATA_FORMAT);
	CS_WRITE(sc, CS4231_IDATA, fs);
	CS_READ(sc, CS4231_IDATA);
	CS_READ(sc, CS4231_IDATA);
	for (i = CS_TIMEOUT;
	    i && CS_READ(sc, CS4231_IADDR) == CS_IN_INIT; i--)
		DELAY(10);
	if (i == 0) {
		device_printf(sc->sc_dev, "timeout setting playback speed\n");
		doreset++;
	}

	/*
	 * capture channel
	 * cs4231 doesn't allow separate fs setup for playback/capture.
	 * I believe this will break full-duplex operation.
	 */
	if (dir == PCMDIR_REC) {
		CS_WRITE(sc, CS4231_IADDR, MODE_CHANGE_ENABLE | CS_REC_FORMAT);
		CS_WRITE(sc, CS4231_IDATA, fs);
		CS_READ(sc, CS4231_IDATA);
		CS_READ(sc, CS4231_IDATA);
		for (i = CS_TIMEOUT;
		    i && CS_READ(sc, CS4231_IADDR) == CS_IN_INIT; i--)
			DELAY(10);
		if (i == 0) {
			device_printf(sc->sc_dev,
			    "timeout setting capture format\n");
			doreset++;
		}
	}

	CS_WRITE(sc, CS4231_IADDR, 0);
	for (i = CS_TIMEOUT;
	    i && CS_READ(sc, CS4231_IADDR) == CS_IN_INIT; i--)
		DELAY(10);
	if (i == 0) {
		device_printf(sc->sc_dev, "timeout waiting for !MCE\n");
		doreset++;
	}

#ifdef CS4231_AUTO_CALIBRATION
	CS_WRITE(sc, CS4231_IADDR, CS_TEST_AND_INIT);
	for (i = CS_TIMEOUT;
	    i && CS_READ(sc, CS4231_IDATA) & AUTO_CAL_IN_PROG; i--)
		DELAY(10);
	if (i == 0) {
		device_printf(sc->sc_dev,
		    "timeout waiting for autocalibration\n");
		doreset++;
	}
#endif
	if (doreset) {
		/*
		 * Maybe the last resort to avoid a dreadful message like
		 * "pcm0:play:0: play interrupt timeout, channel dead" would
		 * be hardware reset.
		 */
		device_printf(sc->sc_dev, "trying to hardware reset\n");
		cs4231_disable(sc);
		cs4231_enable(sc, CODEC_COLD_RESET);
		CS4231_UNLOCK(sc); /* XXX */
		if (mixer_reinit(sc->sc_dev) != 0) 
			device_printf(sc->sc_dev,
			    "unable to reinitialize the mixer\n");
		CS4231_LOCK(sc);
	}
}

static u_int32_t
cs4231_chan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct cs4231_softc *sc;
	struct cs4231_channel *ch;
	int nblks, error;

	ch = data;
	sc = ch->parent;

	if (blocksize > CS4231_MAX_BLK_SZ)
		blocksize = CS4231_MAX_BLK_SZ;
	nblks = sc->sc_bufsz / blocksize;
	error = sndbuf_resize(ch->buffer, nblks, blocksize);
	if (error != 0)
		device_printf(sc->sc_dev,
		    "unable to block size, blksz = %d, error = %d\n",
		    blocksize, error);

        return (blocksize);
}

static int
cs4231_chan_trigger(kobj_t obj, void *data, int go)
{
	struct cs4231_channel *ch;

	ch = data;
	switch (go) {
	case PCMTRIG_EMLDMAWR:
	case PCMTRIG_EMLDMARD:
		break;
	case PCMTRIG_START:
		cs4231_trigger(ch);
		break;
	case PCMTRIG_ABORT:
	case PCMTRIG_STOP:
		cs4231_halt(ch);
		break;
	default:
		break;
	}

	return (0);
}

static u_int32_t
cs4231_chan_getptr(kobj_t obj, void *data)
{
	struct cs4231_softc *sc;
	struct cs4231_channel *ch;
	u_int32_t cur, ptr, sz;

	ch = data;
	sc = ch->parent;

	CS4231_LOCK(sc);
	if ((sc->sc_flags & CS4231_SBUS) != 0)
		cur = (ch->dir == PCMDIR_PLAY) ? APC_READ(sc, APC_PVA) :
		    APC_READ(sc, APC_CVA);
	else
		cur = (ch->dir == PCMDIR_PLAY) ? EBDMA_P_READ(sc, EBDMA_DADDR) :
			EBDMA_C_READ(sc, EBDMA_DADDR);
	sz = sndbuf_getsize(ch->buffer);
	ptr = cur - sndbuf_getbufaddr(ch->buffer) + sz;
	CS4231_UNLOCK(sc);

	ptr %= sz;
	return (ptr);
}

static struct pcmchan_caps *
cs4231_chan_getcaps(kobj_t obj, void *data)
{

	return (&cs4231_caps);
}

static void
cs4231_trigger(struct cs4231_channel *ch)
{
	struct cs4231_softc *sc;

	sc = ch->parent;
	if ((sc->sc_flags & CS4231_SBUS) != 0)
		cs4231_apcdma_trigger(sc, ch);
	else
		cs4231_ebdma_trigger(sc, ch);
}

static void
cs4231_apcdma_trigger(struct cs4231_softc *sc, struct cs4231_channel *ch)
{
	u_int32_t csr, togo;
	u_int32_t nextaddr;

	CS4231_LOCK(sc);
	if (ch->locked) {
		device_printf(sc->sc_dev, "%s channel already triggered\n",
		    ch->dir == PCMDIR_PLAY ? "playback" : "capture");
		CS4231_UNLOCK(sc);
		return;
	}

	nextaddr = sndbuf_getbufaddr(ch->buffer);
	togo = sndbuf_getsize(ch->buffer) / 2;
	if (togo > CS4231_MAX_APC_DMA_SZ)
		togo = CS4231_MAX_APC_DMA_SZ;
	ch->togo = togo;
	if (ch->dir == PCMDIR_PLAY) {
		DPRINTF(("TRG: PNVA = 0x%x, togo = 0x%x\n", nextaddr, togo));

		cs4231_read(sc, CS_TEST_AND_INIT); /* clear pending error */
		csr = APC_READ(sc, APC_CSR);
		APC_WRITE(sc, APC_PNVA, nextaddr);
		APC_WRITE(sc, APC_PNC, togo);
			
		if ((csr & APC_CSR_PDMA_GO) == 0 ||
		    (csr & APC_CSR_PPAUSE) != 0) {
			APC_WRITE(sc, APC_CSR, APC_READ(sc, APC_CSR) &
			    ~(APC_CSR_PIE | APC_CSR_PPAUSE));
			APC_WRITE(sc, APC_CSR, APC_READ(sc, APC_CSR) |
			    APC_CSR_GIE | APC_CSR_PIE | APC_CSR_EIE |
			    APC_CSR_EI | APC_CSR_PMIE | APC_CSR_PDMA_GO);
			cs4231_write(sc, CS_INTERFACE_CONFIG,
			    cs4231_read(sc, CS_INTERFACE_CONFIG) |
			    PLAYBACK_ENABLE);
		}
		/* load next address */
		if (APC_READ(sc, APC_CSR) & APC_CSR_PD) {
			nextaddr += togo;
			APC_WRITE(sc, APC_PNVA, nextaddr);
			APC_WRITE(sc, APC_PNC, togo);
		}
	} else {
		DPRINTF(("TRG: CNVA = 0x%x, togo = 0x%x\n", nextaddr, togo));

		cs4231_read(sc, CS_TEST_AND_INIT); /* clear pending error */
		APC_WRITE(sc, APC_CNVA, nextaddr);
		APC_WRITE(sc, APC_CNC, togo);
		csr = APC_READ(sc, APC_CSR);
		if ((csr & APC_CSR_CDMA_GO) == 0 ||
		    (csr & APC_CSR_CPAUSE) != 0) {
			csr &= APC_CSR_CPAUSE;
			csr |= APC_CSR_GIE | APC_CSR_CMIE | APC_CSR_CIE |
			    APC_CSR_EI | APC_CSR_CDMA_GO;
			APC_WRITE(sc, APC_CSR, csr);
			cs4231_write(sc, CS_INTERFACE_CONFIG,
			    cs4231_read(sc, CS_INTERFACE_CONFIG) |
			    CAPTURE_ENABLE);
		}
		/* load next address */
		if (APC_READ(sc, APC_CSR) & APC_CSR_CD) {
			nextaddr += togo;
			APC_WRITE(sc, APC_CNVA, nextaddr);
			APC_WRITE(sc, APC_CNC, togo);
		}
	}
	ch->nextaddr = nextaddr;
	ch->locked = 1;
	CS4231_UNLOCK(sc);
}

static void
cs4231_ebdma_trigger(struct cs4231_softc *sc, struct cs4231_channel *ch)
{
	u_int32_t csr, togo;
	u_int32_t nextaddr;

	CS4231_LOCK(sc);
	if (ch->locked) {
		device_printf(sc->sc_dev, "%s channel already triggered\n",
		    ch->dir == PCMDIR_PLAY ? "playback" : "capture");
		CS4231_UNLOCK(sc);
		return;
	}

	nextaddr = sndbuf_getbufaddr(ch->buffer);
	togo = sndbuf_getsize(ch->buffer) / 2;
	if (togo % 64 == 0)
		sc->sc_burst = EBDCSR_BURST_16;
	else if (togo % 32 == 0)
		sc->sc_burst = EBDCSR_BURST_8;
	else if (togo % 16 == 0)
		sc->sc_burst = EBDCSR_BURST_4;
	else 
		sc->sc_burst = EBDCSR_BURST_1;
	ch->togo = togo;
	DPRINTF(("TRG: DNAR = 0x%x, togo = 0x%x\n", nextaddr, togo));
	if (ch->dir == PCMDIR_PLAY) {
		cs4231_read(sc, CS_TEST_AND_INIT); /* clear pending error */
		csr = EBDMA_P_READ(sc, EBDMA_DCSR);

		if (csr & EBDCSR_DMAEN) {
			EBDMA_P_WRITE(sc, EBDMA_DCNT, togo);
			EBDMA_P_WRITE(sc, EBDMA_DADDR, nextaddr);
		} else {
			EBDMA_P_WRITE(sc, EBDMA_DCSR, EBDCSR_RESET);
			EBDMA_P_WRITE(sc, EBDMA_DCSR, sc->sc_burst);
			EBDMA_P_WRITE(sc, EBDMA_DCNT, togo);
			EBDMA_P_WRITE(sc, EBDMA_DADDR, nextaddr);

			EBDMA_P_WRITE(sc, EBDMA_DCSR, sc->sc_burst |
			    EBDCSR_DMAEN | EBDCSR_INTEN | EBDCSR_CNTEN |
			    EBDCSR_NEXTEN);
			cs4231_write(sc, CS_INTERFACE_CONFIG,
			    cs4231_read(sc, CS_INTERFACE_CONFIG) |
			    PLAYBACK_ENABLE);
		}
		/* load next address */
		if (EBDMA_P_READ(sc, EBDMA_DCSR) & EBDCSR_A_LOADED) {
			nextaddr += togo;
			EBDMA_P_WRITE(sc, EBDMA_DCNT, togo);
			EBDMA_P_WRITE(sc, EBDMA_DADDR, nextaddr);
		}
	} else {
		cs4231_read(sc, CS_TEST_AND_INIT); /* clear pending error */
		csr = EBDMA_C_READ(sc, EBDMA_DCSR);

		if (csr & EBDCSR_DMAEN) {
			EBDMA_C_WRITE(sc, EBDMA_DCNT, togo);
			EBDMA_C_WRITE(sc, EBDMA_DADDR, nextaddr);
		} else {
			EBDMA_C_WRITE(sc, EBDMA_DCSR, EBDCSR_RESET);
			EBDMA_C_WRITE(sc, EBDMA_DCSR, sc->sc_burst);
			EBDMA_C_WRITE(sc, EBDMA_DCNT, togo);
			EBDMA_C_WRITE(sc, EBDMA_DADDR, nextaddr);

			EBDMA_C_WRITE(sc, EBDMA_DCSR, sc->sc_burst |
			    EBDCSR_WRITE | EBDCSR_DMAEN | EBDCSR_INTEN |
			    EBDCSR_CNTEN | EBDCSR_NEXTEN);
			cs4231_write(sc, CS_INTERFACE_CONFIG,
			    cs4231_read(sc, CS_INTERFACE_CONFIG) |
			    CAPTURE_ENABLE);
		}
		/* load next address */
		if (EBDMA_C_READ(sc, EBDMA_DCSR) & EBDCSR_A_LOADED) {
			nextaddr += togo;
			EBDMA_C_WRITE(sc, EBDMA_DCNT, togo);
			EBDMA_C_WRITE(sc, EBDMA_DADDR, nextaddr);
		}
	}
	ch->nextaddr = nextaddr;
	ch->locked = 1;
	CS4231_UNLOCK(sc);
}

static void
cs4231_halt(struct cs4231_channel *ch)
{
	struct cs4231_softc *sc;
	u_int8_t status;
	int i;

	sc = ch->parent;
	CS4231_LOCK(sc);
	if (ch->locked == 0) {
		CS4231_UNLOCK(sc);
		return;
	}

	if (ch->dir == PCMDIR_PLAY ) {
		if ((sc->sc_flags & CS4231_SBUS) != 0) {
			/* XXX Kills some capture bits */
			APC_WRITE(sc, APC_CSR, APC_READ(sc, APC_CSR) &
			    ~(APC_CSR_EI | APC_CSR_GIE | APC_CSR_PIE |
			    APC_CSR_EIE | APC_CSR_PDMA_GO | APC_CSR_PMIE));
		} else {
			EBDMA_P_WRITE(sc, EBDMA_DCSR,
			    EBDMA_P_READ(sc, EBDMA_DCSR) & ~EBDCSR_DMAEN);
		}
		/* Waiting for playback FIFO to empty */
		status = cs4231_read(sc, CS_TEST_AND_INIT);
		for (i = CS_TIMEOUT;
		    i && (status & PLAYBACK_UNDERRUN) == 0; i--) {
			DELAY(5);
			status = cs4231_read(sc, CS_TEST_AND_INIT);
		}
		if (i == 0)
			device_printf(sc->sc_dev, "timeout waiting for "
			    "playback FIFO drain\n");
		cs4231_write(sc, CS_INTERFACE_CONFIG,
		    cs4231_read(sc, CS_INTERFACE_CONFIG) & (~PLAYBACK_ENABLE));
	} else {
		if ((sc->sc_flags & CS4231_SBUS) != 0) {
			/* XXX Kills some playback bits */
			APC_WRITE(sc, APC_CSR, APC_CSR_CAPTURE_PAUSE);
		} else {
			EBDMA_C_WRITE(sc, EBDMA_DCSR,
			    EBDMA_C_READ(sc, EBDMA_DCSR) & ~EBDCSR_DMAEN);
		}
		/* Waiting for capture FIFO to empty */
		status = cs4231_read(sc, CS_TEST_AND_INIT);
		for (i = CS_TIMEOUT;
		    i && (status & CAPTURE_OVERRUN) == 0; i--) {
			DELAY(5);
			status = cs4231_read(sc, CS_TEST_AND_INIT);
		}
		if (i == 0)
			device_printf(sc->sc_dev, "timeout waiting for "
			    "capture FIFO drain\n");
		cs4231_write(sc, CS_INTERFACE_CONFIG,
		    cs4231_read(sc, CS_INTERFACE_CONFIG) & (~CAPTURE_ENABLE));
	}
	ch->locked = 0;
	CS4231_UNLOCK(sc);
}
