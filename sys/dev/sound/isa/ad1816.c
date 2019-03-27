/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
 * Copyright (c) 1997,1998 Luigi Rizzo
 * Copyright (c) 1994,1995 Hannu Savolainen
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

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/isa/ad1816.h>

#include <isa/isavar.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

struct ad1816_info;

struct ad1816_chinfo {
	struct ad1816_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	int dir, blksz;
};

struct ad1816_info {
	struct resource *io_base;	/* primary I/O address for the board */
	int io_rid;
	struct resource *irq;
	int irq_rid;
	struct resource *drq1;		/* play */
	int drq1_rid;
	struct resource *drq2;		/* rec */
	int drq2_rid;
	void *ih;
	bus_dma_tag_t parent_dmat;
	struct mtx *lock;

	unsigned int bufsize;
	struct ad1816_chinfo pch, rch;
};

static u_int32_t ad1816_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 1, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_MU_LAW, 1, 0),
	SND_FORMAT(AFMT_MU_LAW, 2, 0),
	SND_FORMAT(AFMT_A_LAW, 1, 0),
	SND_FORMAT(AFMT_A_LAW, 2, 0),
	0
};

static struct pcmchan_caps ad1816_caps = {4000, 55200, ad1816_fmt, 0};

#define AD1816_MUTE 31		/* value for mute */

static void
ad1816_lock(struct ad1816_info *ad1816)
{
	snd_mtxlock(ad1816->lock);
}

static void
ad1816_unlock(struct ad1816_info *ad1816)
{
	snd_mtxunlock(ad1816->lock);
}

static int
port_rd(struct resource *port, int off)
{
	if (port)
		return bus_space_read_1(rman_get_bustag(port),
					rman_get_bushandle(port),
					off);
	else
		return -1;
}

static void
port_wr(struct resource *port, int off, u_int8_t data)
{
	if (port)
		bus_space_write_1(rman_get_bustag(port),
				  rman_get_bushandle(port),
				  off, data);
}

static int
io_rd(struct ad1816_info *ad1816, int reg)
{
	return port_rd(ad1816->io_base, reg);
}

static void
io_wr(struct ad1816_info *ad1816, int reg, u_int8_t data)
{
	port_wr(ad1816->io_base, reg, data);
}

static void
ad1816_intr(void *arg)
{
    	struct ad1816_info *ad1816 = (struct ad1816_info *)arg;
    	unsigned char   c, served = 0;

	ad1816_lock(ad1816);
    	/* get interrupt status */
    	c = io_rd(ad1816, AD1816_INT);

    	/* check for stray interrupts */
    	if (c & ~(AD1816_INTRCI | AD1816_INTRPI)) {
		printf("pcm: stray int (%x)\n", c);
		c &= AD1816_INTRCI | AD1816_INTRPI;
    	}
    	/* check for capture interrupt */
    	if (sndbuf_runsz(ad1816->rch.buffer) && (c & AD1816_INTRCI)) {
		ad1816_unlock(ad1816);
		chn_intr(ad1816->rch.channel);
		ad1816_lock(ad1816);
		served |= AD1816_INTRCI;		/* cp served */
    	}
    	/* check for playback interrupt */
    	if (sndbuf_runsz(ad1816->pch.buffer) && (c & AD1816_INTRPI)) {
		ad1816_unlock(ad1816);
		chn_intr(ad1816->pch.channel);
		ad1816_lock(ad1816);
		served |= AD1816_INTRPI;		/* pb served */
    	}
    	if (served == 0) {
		/* this probably means this is not a (working) ad1816 chip, */
		/* or an error in dma handling                              */
		printf("pcm: int without reason (%x)\n", c);
		c = 0;
    	} else c &= ~served;
    	io_wr(ad1816, AD1816_INT, c);
    	c = io_rd(ad1816, AD1816_INT);
    	if (c != 0) printf("pcm: int clear failed (%x)\n", c);
	ad1816_unlock(ad1816);
}

static int
ad1816_wait_init(struct ad1816_info *ad1816, int x)
{
    	int             n = 0;	/* to shut up the compiler... */

    	for (; x--;)
		if ((n = (io_rd(ad1816, AD1816_ALE) & AD1816_BUSY)) == 0) DELAY(10);
		else return n;
    	printf("ad1816_wait_init failed 0x%02x.\n", n);
    	return -1;
}

static unsigned short
ad1816_read(struct ad1816_info *ad1816, unsigned int reg)
{
    	u_short         x = 0;

    	if (ad1816_wait_init(ad1816, 100) == -1) return 0;
    	io_wr(ad1816, AD1816_ALE, 0);
    	io_wr(ad1816, AD1816_ALE, (reg & AD1816_ALEMASK));
    	if (ad1816_wait_init(ad1816, 100) == -1) return 0;
    	x = (io_rd(ad1816, AD1816_HIGH) << 8) | io_rd(ad1816, AD1816_LOW);
    	return x;
}

static void
ad1816_write(struct ad1816_info *ad1816, unsigned int reg, unsigned short data)
{
    	if (ad1816_wait_init(ad1816, 100) == -1) return;
    	io_wr(ad1816, AD1816_ALE, (reg & AD1816_ALEMASK));
    	io_wr(ad1816, AD1816_LOW,  (data & 0x000000ff));
    	io_wr(ad1816, AD1816_HIGH, (data & 0x0000ff00) >> 8);
}

/* -------------------------------------------------------------------- */

static int
ad1816mix_init(struct snd_mixer *m)
{
	mix_setdevs(m, AD1816_MIXER_DEVICES);
	mix_setrecdevs(m, AD1816_REC_DEVICES);
	return 0;
}

static int
ad1816mix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct ad1816_info *ad1816 = mix_getdevinfo(m);
    	u_short reg = 0;

    	/* Scale volumes */
    	left = AD1816_MUTE - (AD1816_MUTE * left) / 100;
    	right = AD1816_MUTE - (AD1816_MUTE * right) / 100;

    	reg = (left << 8) | right;

    	/* do channel selective muting if volume is zero */
    	if (left == AD1816_MUTE)	reg |= 0x8000;
    	if (right == AD1816_MUTE)	reg |= 0x0080;

	ad1816_lock(ad1816);
    	switch (dev) {
    	case SOUND_MIXER_VOLUME:	/* Register 14 master volume */
		ad1816_write(ad1816, 14, reg);
		break;

    	case SOUND_MIXER_CD:	/* Register 15 cd */
    	case SOUND_MIXER_LINE1:
		ad1816_write(ad1816, 15, reg);
		break;

    	case SOUND_MIXER_SYNTH:	/* Register 16 synth */
		ad1816_write(ad1816, 16, reg);
		break;

    	case SOUND_MIXER_PCM:	/* Register 4 pcm */
		ad1816_write(ad1816, 4, reg);
		break;

    	case SOUND_MIXER_LINE:
    	case SOUND_MIXER_LINE3:	/* Register 18 line in */
		ad1816_write(ad1816, 18, reg);
		break;

    	case SOUND_MIXER_MIC:	/* Register 19 mic volume */
		ad1816_write(ad1816, 19, reg & ~0xff);	/* mic is mono */
		break;

    	case SOUND_MIXER_IGAIN:
		/* and now to something completely different ... */
		ad1816_write(ad1816, 20, ((ad1816_read(ad1816, 20) & ~0x0f0f)
	      	| (((AD1816_MUTE - left) / 2) << 8) /* four bits of adc gain */
	      	| ((AD1816_MUTE - right) / 2)));
		break;

    	default:
		printf("ad1816_mixer_set(): unknown device.\n");
		break;
    	}
	ad1816_unlock(ad1816);

    	left = ((AD1816_MUTE - left) * 100) / AD1816_MUTE;
    	right = ((AD1816_MUTE - right) * 100) / AD1816_MUTE;

    	return left | (right << 8);
}

static u_int32_t
ad1816mix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	struct ad1816_info *ad1816 = mix_getdevinfo(m);
    	int dev;

    	switch (src) {
    	case SOUND_MASK_LINE:
    	case SOUND_MASK_LINE3:
		dev = 0x00;
		break;

    	case SOUND_MASK_CD:
    	case SOUND_MASK_LINE1:
		dev = 0x20;
		break;

    	case SOUND_MASK_MIC:
    	default:
		dev = 0x50;
		src = SOUND_MASK_MIC;
    	}

    	dev |= dev << 8;
	ad1816_lock(ad1816);
    	ad1816_write(ad1816, 20, (ad1816_read(ad1816, 20) & ~0x7070) | dev);
	ad1816_unlock(ad1816);
    	return src;
}

static kobj_method_t ad1816mixer_methods[] = {
    	KOBJMETHOD(mixer_init,		ad1816mix_init),
    	KOBJMETHOD(mixer_set,		ad1816mix_set),
    	KOBJMETHOD(mixer_setrecsrc,	ad1816mix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(ad1816mixer);

/* -------------------------------------------------------------------- */
/* channel interface */
static void *
ad1816chan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct ad1816_info *ad1816 = devinfo;
	struct ad1816_chinfo *ch = (dir == PCMDIR_PLAY)? &ad1816->pch : &ad1816->rch;

	ch->dir = dir;
	ch->parent = ad1816;
	ch->channel = c;
	ch->buffer = b;
	if (sndbuf_alloc(ch->buffer, ad1816->parent_dmat, 0, ad1816->bufsize) != 0)
		return NULL;

	sndbuf_dmasetup(ch->buffer, (dir == PCMDIR_PLAY) ? ad1816->drq1 :
	    ad1816->drq2);
	if (SND_DMA(ch->buffer))
		sndbuf_dmasetdir(ch->buffer, dir);

	return ch;
}

static int
ad1816chan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct ad1816_chinfo *ch = data;
  	struct ad1816_info *ad1816 = ch->parent;
    	int fmt = AD1816_U8, reg;

	ad1816_lock(ad1816);
    	if (ch->dir == PCMDIR_PLAY) {
        	reg = AD1816_PLAY;
        	ad1816_write(ad1816, 8, 0x0000);	/* reset base and current counter */
        	ad1816_write(ad1816, 9, 0x0000);	/* for playback and capture */
    	} else {
        	reg = AD1816_CAPT;
        	ad1816_write(ad1816, 10, 0x0000);
        	ad1816_write(ad1816, 11, 0x0000);
    	}
    	switch (AFMT_ENCODING(format)) {
    	case AFMT_A_LAW:
        	fmt = AD1816_ALAW;
		break;

    	case AFMT_MU_LAW:
		fmt = AD1816_MULAW;
		break;

    	case AFMT_S16_LE:
		fmt = AD1816_S16LE;
		break;

    	case AFMT_S16_BE:
		fmt = AD1816_S16BE;
		break;

    	case AFMT_U8:
		fmt = AD1816_U8;
		break;
    	}
    	if (AFMT_CHANNEL(format) > 1) fmt |= AD1816_STEREO;
    	io_wr(ad1816, reg, fmt);
	ad1816_unlock(ad1816);
#if 0
    	return format;
#else
    	return 0;
#endif
}

static u_int32_t
ad1816chan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct ad1816_chinfo *ch = data;
    	struct ad1816_info *ad1816 = ch->parent;

    	RANGE(speed, 4000, 55200);
	ad1816_lock(ad1816);
    	ad1816_write(ad1816, (ch->dir == PCMDIR_PLAY)? 2 : 3, speed);
	ad1816_unlock(ad1816);
    	return speed;
}

static u_int32_t
ad1816chan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct ad1816_chinfo *ch = data;

	ch->blksz = blocksize;
	return ch->blksz;
}

static int
ad1816chan_trigger(kobj_t obj, void *data, int go)
{
	struct ad1816_chinfo *ch = data;
    	struct ad1816_info *ad1816 = ch->parent;
    	int wr, reg;

	if (!PCMTRIG_COMMON(go))
		return 0;

	sndbuf_dma(ch->buffer, go);
    	wr = (ch->dir == PCMDIR_PLAY);
    	reg = wr? AD1816_PLAY : AD1816_CAPT;
	ad1816_lock(ad1816);
    	switch (go) {
    	case PCMTRIG_START:
		/* start only if not already running */
		if (!(io_rd(ad1816, reg) & AD1816_ENABLE)) {
	    		int cnt = ((ch->blksz) >> 2) - 1;
	    		ad1816_write(ad1816, wr? 8 : 10, cnt); /* count */
	    		ad1816_write(ad1816, wr? 9 : 11, 0); /* reset cur cnt */
	    		ad1816_write(ad1816, 1, ad1816_read(ad1816, 1) |
				     (wr? 0x8000 : 0x4000)); /* enable int */
	    		/* enable playback */
	    		io_wr(ad1816, reg, io_rd(ad1816, reg) | AD1816_ENABLE);
	    		if (!(io_rd(ad1816, reg) & AD1816_ENABLE))
				printf("ad1816: failed to start %s DMA!\n",
				       wr? "play" : "rec");
		}
		break;

    	case PCMTRIG_STOP:
    	case PCMTRIG_ABORT:		/* XXX check this... */
		/* we don't test here if it is running... */
		if (wr) {
	    		ad1816_write(ad1816, 1, ad1816_read(ad1816, 1) &
				     ~(wr? 0x8000 : 0x4000));
	    		/* disable int */
	    		io_wr(ad1816, reg, io_rd(ad1816, reg) & ~AD1816_ENABLE);
	    		/* disable playback */
	    		if (io_rd(ad1816, reg) & AD1816_ENABLE)
				printf("ad1816: failed to stop %s DMA!\n",
				       wr? "play" : "rec");
	    		ad1816_write(ad1816, wr? 8 : 10, 0); /* reset base cnt */
	    		ad1816_write(ad1816, wr? 9 : 11, 0); /* reset cur cnt */
		}
		break;
    	}
	ad1816_unlock(ad1816);
    	return 0;
}

static u_int32_t
ad1816chan_getptr(kobj_t obj, void *data)
{
	struct ad1816_chinfo *ch = data;
	return sndbuf_dmaptr(ch->buffer);
}

static struct pcmchan_caps *
ad1816chan_getcaps(kobj_t obj, void *data)
{
	return &ad1816_caps;
}

static kobj_method_t ad1816chan_methods[] = {
    	KOBJMETHOD(channel_init,		ad1816chan_init),
    	KOBJMETHOD(channel_setformat,		ad1816chan_setformat),
    	KOBJMETHOD(channel_setspeed,		ad1816chan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	ad1816chan_setblocksize),
    	KOBJMETHOD(channel_trigger,		ad1816chan_trigger),
    	KOBJMETHOD(channel_getptr,		ad1816chan_getptr),
    	KOBJMETHOD(channel_getcaps,		ad1816chan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(ad1816chan);

/* -------------------------------------------------------------------- */

static void
ad1816_release_resources(struct ad1816_info *ad1816, device_t dev)
{
    	if (ad1816->irq) {
   		if (ad1816->ih)
			bus_teardown_intr(dev, ad1816->irq, ad1816->ih);
		bus_release_resource(dev, SYS_RES_IRQ, ad1816->irq_rid, ad1816->irq);
		ad1816->irq = NULL;
    	}
    	if (ad1816->drq1) {
		isa_dma_release(rman_get_start(ad1816->drq1));
		bus_release_resource(dev, SYS_RES_DRQ, ad1816->drq1_rid, ad1816->drq1);
		ad1816->drq1 = NULL;
    	}
    	if (ad1816->drq2) {
		isa_dma_release(rman_get_start(ad1816->drq2));
		bus_release_resource(dev, SYS_RES_DRQ, ad1816->drq2_rid, ad1816->drq2);
		ad1816->drq2 = NULL;
    	}
    	if (ad1816->io_base) {
		bus_release_resource(dev, SYS_RES_IOPORT, ad1816->io_rid, ad1816->io_base);
		ad1816->io_base = NULL;
    	}
    	if (ad1816->parent_dmat) {
		bus_dma_tag_destroy(ad1816->parent_dmat);
		ad1816->parent_dmat = 0;
    	}
	if (ad1816->lock)
		snd_mtxfree(ad1816->lock);

     	free(ad1816, M_DEVBUF);
}

static int
ad1816_alloc_resources(struct ad1816_info *ad1816, device_t dev)
{
    	int ok = 1, pdma, rdma;

	if (!ad1816->io_base)
    		ad1816->io_base = bus_alloc_resource_any(dev, 
			SYS_RES_IOPORT, &ad1816->io_rid, RF_ACTIVE);
	if (!ad1816->irq)
    		ad1816->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
			&ad1816->irq_rid, RF_ACTIVE);
	if (!ad1816->drq1)
    		ad1816->drq1 = bus_alloc_resource_any(dev, SYS_RES_DRQ,
			&ad1816->drq1_rid, RF_ACTIVE);
    	if (!ad1816->drq2)
        	ad1816->drq2 = bus_alloc_resource_any(dev, SYS_RES_DRQ, 
			&ad1816->drq2_rid, RF_ACTIVE);

    	if (!ad1816->io_base || !ad1816->drq1 || !ad1816->irq) ok = 0;

	if (ok) {
		pdma = rman_get_start(ad1816->drq1);
		isa_dma_acquire(pdma);
		isa_dmainit(pdma, ad1816->bufsize);
		if (ad1816->drq2) {
			rdma = rman_get_start(ad1816->drq2);
			isa_dma_acquire(rdma);
			isa_dmainit(rdma, ad1816->bufsize);
		} else
			rdma = pdma;
    		if (pdma == rdma)
			pcm_setflags(dev, pcm_getflags(dev) | SD_F_SIMPLEX);
	}

    	return ok;
}

static int
ad1816_init(struct ad1816_info *ad1816, device_t dev)
{
    	ad1816_write(ad1816, 1, 0x2);	/* disable interrupts */
    	ad1816_write(ad1816, 32, 0x90F0);	/* SoundSys Mode, split fmt */

    	ad1816_write(ad1816, 5, 0x8080);	/* FM volume mute */
    	ad1816_write(ad1816, 6, 0x8080);	/* I2S1 volume mute */
    	ad1816_write(ad1816, 7, 0x8080);	/* I2S0 volume mute */
    	ad1816_write(ad1816, 17, 0x8888);	/* VID Volume mute */
    	ad1816_write(ad1816, 20, 0x5050);	/* recsrc mic, agc off */
    	/* adc gain is set to 0 */

	return 0;
}

static int
ad1816_probe(device_t dev)
{
    	char *s = NULL;
    	u_int32_t logical_id = isa_get_logicalid(dev);

    	switch (logical_id) {
    	case 0x80719304: /* ADS7180 */
 		s = "AD1816";
 		break;
    	case 0x50719304: /* ADS7150 */
 		s = "AD1815";
 		break;
    	}

    	if (s) {
		device_set_desc(dev, s);
		return BUS_PROBE_DEFAULT;
    	}
    	return ENXIO;
}

static int
ad1816_attach(device_t dev)
{
	struct ad1816_info *ad1816;
    	char status[SND_STATUSLEN], status2[SND_STATUSLEN];

	ad1816 = malloc(sizeof(*ad1816), M_DEVBUF, M_WAITOK | M_ZERO);
	ad1816->lock = snd_mtxcreate(device_get_nameunit(dev),
	    "snd_ad1816 softc");
	ad1816->io_rid = 2;
	ad1816->irq_rid = 0;
	ad1816->drq1_rid = 0;
	ad1816->drq2_rid = 1;
	ad1816->bufsize = pcm_getbuffersize(dev, 4096, DSP_BUFFSIZE, 65536);

    	if (!ad1816_alloc_resources(ad1816, dev)) goto no;
    	ad1816_init(ad1816, dev);
    	if (mixer_init(dev, &ad1816mixer_class, ad1816)) goto no;

	snd_setup_intr(dev, ad1816->irq, 0, ad1816_intr, ad1816, &ad1816->ih);
    	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
			/*boundary*/0,
			/*lowaddr*/BUS_SPACE_MAXADDR_24BIT,
			/*highaddr*/BUS_SPACE_MAXADDR,
			/*filter*/NULL, /*filterarg*/NULL,
			/*maxsize*/ad1816->bufsize, /*nsegments*/1,
			/*maxsegz*/0x3ffff,
			/*flags*/0, /*lockfunc*/busdma_lock_mutex,
			/*lockarg*/ &Giant, &ad1816->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto no;
    	}
    	if (ad1816->drq2)
		snprintf(status2, SND_STATUSLEN, ":%jd", rman_get_start(ad1816->drq2));
	else
		status2[0] = '\0';

    	snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd drq %jd%s bufsz %u %s",
    	     	rman_get_start(ad1816->io_base),
		rman_get_start(ad1816->irq),
		rman_get_start(ad1816->drq1),
		status2,
		ad1816->bufsize,
		PCM_KLDSTRING(snd_ad1816));

    	if (pcm_register(dev, ad1816, 1, 1)) goto no;
    	pcm_addchan(dev, PCMDIR_REC, &ad1816chan_class, ad1816);
    	pcm_addchan(dev, PCMDIR_PLAY, &ad1816chan_class, ad1816);
    	pcm_setstatus(dev, status);

    	return 0;
no:
    	ad1816_release_resources(ad1816, dev);

    	return ENXIO;

}

static int
ad1816_detach(device_t dev)
{
	int r;
	struct ad1816_info *ad1816;

	r = pcm_unregister(dev);
	if (r)
		return r;

	ad1816 = pcm_getdevinfo(dev);
    	ad1816_release_resources(ad1816, dev);
	return 0;
}

static device_method_t ad1816_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ad1816_probe),
	DEVMETHOD(device_attach,	ad1816_attach),
	DEVMETHOD(device_detach,	ad1816_detach),

	{ 0, 0 }
};

static driver_t ad1816_driver = {
	"pcm",
	ad1816_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_ad1816, isa, ad1816_driver, pcm_devclass, 0, 0);
DRIVER_MODULE(snd_ad1816, acpi, ad1816_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_ad1816, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_VERSION(snd_ad1816, 1);


