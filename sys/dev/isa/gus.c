/*	$OpenBSD: gus.c,v 1.57 2024/06/22 10:22:29 jsg Exp $	*/
/*	$NetBSD: gus.c,v 1.51 1998/01/25 23:48:06 mycroft Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ken Hornstein and John Kohl.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *
 * TODO:
 *	. figure out why mixer activity while sound is playing causes problems
 *	  (phantom interrupts?)
 *	. figure out a better deinterleave strategy that avoids sucking up
 *	  CPU, memory and cache bandwidth.  (Maybe a special encoding?
 *	  Maybe use the double-speed sampling/hardware deinterleave trick
 *	  from the GUS SDK?)  A 486/33 isn't quite fast enough to keep
 *	  up with 44.1kHz 16-bit stereo output without some drop-outs.
 *	. use CS4231 for 16-bit sampling, for a-law and mu-law playback.
 *	. actually test full-duplex sampling(recording) and playback.
 */

/*
 * Gravis UltraSound driver
 *
 * For more detailed information, see the GUS developers' kit
 * available on the net at:
 *
 * http://www.gravis.com/Public/sdk/GUSDK222.ZIP
 *
 *		See ultrawrd.doc inside--it's MS Word (ick), but it's the bible
 *
 */

/*
 * The GUS Max has a slightly strange set of connections between the CS4231
 * and the GF1 and the DMA interconnects.  It's set up so that the CS4231 can
 * be playing while the GF1 is loading patches from the system.
 *
 * Here's a recreation of the DMA interconnect diagram:
 *
 *       GF1
 *   +---------+				 digital
 *   |         |  record			 ASIC
 *   |         |--------------+
 *   |         |              |		       +--------+
 *   |         | play (dram)  |      +----+    |	|
 *   |         |--------------(------|-\  |    |   +-+  |
 *   +---------+              |      |  >-|----|---|C|--|------  dma chan 1
 *                            |  +---|-/  |    |   +-+	|
 *                            |  |   +----+    |    |   |
 *                            |	 |   +----+    |    |   |
 *   +---------+        +-+   +--(---|-\  |    |    |   |
 *   |         | play   |8|      |   |  >-|----|----+---|------  dma chan 2
 *   | ---C----|--------|/|------(---|-/  |    |        |
 *   |    ^    |record  |1|      |   +----+    |	|
 *   |    |    |   /----|6|------+	       +--------+
 *   | ---+----|--/     +-+
 *   +---------+
 *     CS4231	8-to-16 bit bus conversion, if needed
 *
 *
 * "C" is an optional combiner.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/timeout.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/cpufunc.h>
#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/ic/ics2101reg.h>
#include <dev/ic/cs4231reg.h>
#include <dev/ic/ad1848reg.h>
#include <dev/isa/ics2101var.h>
#include <dev/isa/ad1848var.h>
#include "gusreg.h"
#include "gusvar.h"

#ifdef AUDIO_DEBUG
#define GUSPLAYDEBUG	/*XXX*/
#define DPRINTF(x)	if (gusdebug) printf x
#define DMAPRINTF(x)	if (gusdmadebug) printf x
int	gusdebug = 0;
int	gusdmadebug = 0;
#else
#define DPRINTF(x)
#define DMAPRINTF(x)
#endif
int	gus_dostereo = 1;

#define NDMARECS 2048
#ifdef GUSPLAYDEBUG
int	gusstats = 0;

struct dma_record dmarecords[NDMARECS];

int dmarecord_index = 0;
#endif

struct cfdriver gus_cd = {
	NULL, "gus", DV_DULL
};

/*
 * A mapping from IRQ/DRQ values to the values used in the GUS's internal
 * registers.  A zero means that the referenced IRQ/DRQ is invalid
 */
const int gus_irq_map[] = {
	IRQUNK, IRQUNK, 1, 3, IRQUNK, 2, IRQUNK, 4, IRQUNK, 1, IRQUNK, 5, 6,
	IRQUNK, IRQUNK, 7
};
const int gus_drq_map[] = {
	DRQUNK, 1, DRQUNK, 2, DRQUNK, 3, 4, 5
};

/*
 * A list of valid base addresses for the GUS
 */

const int gus_base_addrs[] = {
	0x210, 0x220, 0x230, 0x240, 0x250, 0x260
};
const int gus_addrs = sizeof(gus_base_addrs) / sizeof(gus_base_addrs[0]);

/*
 * Maximum frequency values of the GUS based on the number of currently active
 * voices.  Since the GUS samples a voice every 1.6 us, the maximum frequency
 * is dependent on the number of active voices.  Yes, it is pretty weird.
 */

static const int gus_max_frequency[] = {
		44100,		/* 14 voices */
		41160,		/* 15 voices */
		38587,		/* 16 voices */
		36317,		/* 17 voices */
		34300,		/* 18 voices */
		32494,		/* 19 voices */
		30870,		/* 20 voices */
		29400,		/* 21 voices */
		28063,		/* 22 voices */
		26843,		/* 23 voices */
		25725,		/* 24 voices */
		24696,		/* 25 voices */
		23746,		/* 26 voices */
		22866,		/* 27 voices */
		22050,		/* 28 voices */
		21289,		/* 29 voices */
		20580,		/* 30 voices */
		19916,		/* 31 voices */
		19293		/* 32 voices */
};
/*
 * A mapping of linear volume levels to the logarithmic volume values used
 * by the GF1 chip on the GUS.  From GUS SDK vol1.c.
 */

static const unsigned short gus_log_volumes[512] = {
 0x0000,
 0x0700, 0x07ff, 0x0880, 0x08ff, 0x0940, 0x0980, 0x09c0, 0x09ff, 0x0a20,
 0x0a40, 0x0a60, 0x0a80, 0x0aa0, 0x0ac0, 0x0ae0, 0x0aff, 0x0b10, 0x0b20,
 0x0b30, 0x0b40, 0x0b50, 0x0b60, 0x0b70, 0x0b80, 0x0b90, 0x0ba0, 0x0bb0,
 0x0bc0, 0x0bd0, 0x0be0, 0x0bf0, 0x0bff, 0x0c08, 0x0c10, 0x0c18, 0x0c20,
 0x0c28, 0x0c30, 0x0c38, 0x0c40, 0x0c48, 0x0c50, 0x0c58, 0x0c60, 0x0c68,
 0x0c70, 0x0c78, 0x0c80, 0x0c88, 0x0c90, 0x0c98, 0x0ca0, 0x0ca8, 0x0cb0,
 0x0cb8, 0x0cc0, 0x0cc8, 0x0cd0, 0x0cd8, 0x0ce0, 0x0ce8, 0x0cf0, 0x0cf8,
 0x0cff, 0x0d04, 0x0d08, 0x0d0c, 0x0d10, 0x0d14, 0x0d18, 0x0d1c, 0x0d20,
 0x0d24, 0x0d28, 0x0d2c, 0x0d30, 0x0d34, 0x0d38, 0x0d3c, 0x0d40, 0x0d44,
 0x0d48, 0x0d4c, 0x0d50, 0x0d54, 0x0d58, 0x0d5c, 0x0d60, 0x0d64, 0x0d68,
 0x0d6c, 0x0d70, 0x0d74, 0x0d78, 0x0d7c, 0x0d80, 0x0d84, 0x0d88, 0x0d8c,
 0x0d90, 0x0d94, 0x0d98, 0x0d9c, 0x0da0, 0x0da4, 0x0da8, 0x0dac, 0x0db0,
 0x0db4, 0x0db8, 0x0dbc, 0x0dc0, 0x0dc4, 0x0dc8, 0x0dcc, 0x0dd0, 0x0dd4,
 0x0dd8, 0x0ddc, 0x0de0, 0x0de4, 0x0de8, 0x0dec, 0x0df0, 0x0df4, 0x0df8,
 0x0dfc, 0x0dff, 0x0e02, 0x0e04, 0x0e06, 0x0e08, 0x0e0a, 0x0e0c, 0x0e0e,
 0x0e10, 0x0e12, 0x0e14, 0x0e16, 0x0e18, 0x0e1a, 0x0e1c, 0x0e1e, 0x0e20,
 0x0e22, 0x0e24, 0x0e26, 0x0e28, 0x0e2a, 0x0e2c, 0x0e2e, 0x0e30, 0x0e32,
 0x0e34, 0x0e36, 0x0e38, 0x0e3a, 0x0e3c, 0x0e3e, 0x0e40, 0x0e42, 0x0e44,
 0x0e46, 0x0e48, 0x0e4a, 0x0e4c, 0x0e4e, 0x0e50, 0x0e52, 0x0e54, 0x0e56,
 0x0e58, 0x0e5a, 0x0e5c, 0x0e5e, 0x0e60, 0x0e62, 0x0e64, 0x0e66, 0x0e68,
 0x0e6a, 0x0e6c, 0x0e6e, 0x0e70, 0x0e72, 0x0e74, 0x0e76, 0x0e78, 0x0e7a,
 0x0e7c, 0x0e7e, 0x0e80, 0x0e82, 0x0e84, 0x0e86, 0x0e88, 0x0e8a, 0x0e8c,
 0x0e8e, 0x0e90, 0x0e92, 0x0e94, 0x0e96, 0x0e98, 0x0e9a, 0x0e9c, 0x0e9e,
 0x0ea0, 0x0ea2, 0x0ea4, 0x0ea6, 0x0ea8, 0x0eaa, 0x0eac, 0x0eae, 0x0eb0,
 0x0eb2, 0x0eb4, 0x0eb6, 0x0eb8, 0x0eba, 0x0ebc, 0x0ebe, 0x0ec0, 0x0ec2,
 0x0ec4, 0x0ec6, 0x0ec8, 0x0eca, 0x0ecc, 0x0ece, 0x0ed0, 0x0ed2, 0x0ed4,
 0x0ed6, 0x0ed8, 0x0eda, 0x0edc, 0x0ede, 0x0ee0, 0x0ee2, 0x0ee4, 0x0ee6,
 0x0ee8, 0x0eea, 0x0eec, 0x0eee, 0x0ef0, 0x0ef2, 0x0ef4, 0x0ef6, 0x0ef8,
 0x0efa, 0x0efc, 0x0efe, 0x0eff, 0x0f01, 0x0f02, 0x0f03, 0x0f04, 0x0f05,
 0x0f06, 0x0f07, 0x0f08, 0x0f09, 0x0f0a, 0x0f0b, 0x0f0c, 0x0f0d, 0x0f0e,
 0x0f0f, 0x0f10, 0x0f11, 0x0f12, 0x0f13, 0x0f14, 0x0f15, 0x0f16, 0x0f17,
 0x0f18, 0x0f19, 0x0f1a, 0x0f1b, 0x0f1c, 0x0f1d, 0x0f1e, 0x0f1f, 0x0f20,
 0x0f21, 0x0f22, 0x0f23, 0x0f24, 0x0f25, 0x0f26, 0x0f27, 0x0f28, 0x0f29,
 0x0f2a, 0x0f2b, 0x0f2c, 0x0f2d, 0x0f2e, 0x0f2f, 0x0f30, 0x0f31, 0x0f32,
 0x0f33, 0x0f34, 0x0f35, 0x0f36, 0x0f37, 0x0f38, 0x0f39, 0x0f3a, 0x0f3b,
 0x0f3c, 0x0f3d, 0x0f3e, 0x0f3f, 0x0f40, 0x0f41, 0x0f42, 0x0f43, 0x0f44,
 0x0f45, 0x0f46, 0x0f47, 0x0f48, 0x0f49, 0x0f4a, 0x0f4b, 0x0f4c, 0x0f4d,
 0x0f4e, 0x0f4f, 0x0f50, 0x0f51, 0x0f52, 0x0f53, 0x0f54, 0x0f55, 0x0f56,
 0x0f57, 0x0f58, 0x0f59, 0x0f5a, 0x0f5b, 0x0f5c, 0x0f5d, 0x0f5e, 0x0f5f,
 0x0f60, 0x0f61, 0x0f62, 0x0f63, 0x0f64, 0x0f65, 0x0f66, 0x0f67, 0x0f68,
 0x0f69, 0x0f6a, 0x0f6b, 0x0f6c, 0x0f6d, 0x0f6e, 0x0f6f, 0x0f70, 0x0f71,
 0x0f72, 0x0f73, 0x0f74, 0x0f75, 0x0f76, 0x0f77, 0x0f78, 0x0f79, 0x0f7a,
 0x0f7b, 0x0f7c, 0x0f7d, 0x0f7e, 0x0f7f, 0x0f80, 0x0f81, 0x0f82, 0x0f83,
 0x0f84, 0x0f85, 0x0f86, 0x0f87, 0x0f88, 0x0f89, 0x0f8a, 0x0f8b, 0x0f8c,
 0x0f8d, 0x0f8e, 0x0f8f, 0x0f90, 0x0f91, 0x0f92, 0x0f93, 0x0f94, 0x0f95,
 0x0f96, 0x0f97, 0x0f98, 0x0f99, 0x0f9a, 0x0f9b, 0x0f9c, 0x0f9d, 0x0f9e,
 0x0f9f, 0x0fa0, 0x0fa1, 0x0fa2, 0x0fa3, 0x0fa4, 0x0fa5, 0x0fa6, 0x0fa7,
 0x0fa8, 0x0fa9, 0x0faa, 0x0fab, 0x0fac, 0x0fad, 0x0fae, 0x0faf, 0x0fb0,
 0x0fb1, 0x0fb2, 0x0fb3, 0x0fb4, 0x0fb5, 0x0fb6, 0x0fb7, 0x0fb8, 0x0fb9,
 0x0fba, 0x0fbb, 0x0fbc, 0x0fbd, 0x0fbe, 0x0fbf, 0x0fc0, 0x0fc1, 0x0fc2,
 0x0fc3, 0x0fc4, 0x0fc5, 0x0fc6, 0x0fc7, 0x0fc8, 0x0fc9, 0x0fca, 0x0fcb,
 0x0fcc, 0x0fcd, 0x0fce, 0x0fcf, 0x0fd0, 0x0fd1, 0x0fd2, 0x0fd3, 0x0fd4,
 0x0fd5, 0x0fd6, 0x0fd7, 0x0fd8, 0x0fd9, 0x0fda, 0x0fdb, 0x0fdc, 0x0fdd,
 0x0fde, 0x0fdf, 0x0fe0, 0x0fe1, 0x0fe2, 0x0fe3, 0x0fe4, 0x0fe5, 0x0fe6,
 0x0fe7, 0x0fe8, 0x0fe9, 0x0fea, 0x0feb, 0x0fec, 0x0fed, 0x0fee, 0x0fef,
 0x0ff0, 0x0ff1, 0x0ff2, 0x0ff3, 0x0ff4, 0x0ff5, 0x0ff6, 0x0ff7, 0x0ff8,
 0x0ff9, 0x0ffa, 0x0ffb, 0x0ffc, 0x0ffd, 0x0ffe, 0x0fff};

/*
 * Interface to higher level audio driver
 */
const struct audio_hw_if gus_hw_if = {
	.open = gusopen,
	.close = gusclose,
	.set_params = gus_set_params,
	.round_blocksize = gus_round_blocksize,
	.commit_settings = gus_commit_settings,
	.start_output = gus_dma_output,
	.start_input = gus_dma_input,
	.halt_output = gus_halt_out_dma,
	.halt_input = gus_halt_in_dma,
	.set_port = gus_mixer_set_port,
	.get_port = gus_mixer_get_port,
	.query_devinfo = gus_mixer_query_devinfo,
	.allocm = gus_malloc,
	.freem = gus_free,
	.round_buffersize = gus_round,
};

static const struct audio_hw_if gusmax_hw_if = {
	.open = gusmaxopen,
	.close = gusmax_close,
	.set_params = gusmax_set_params,
	.round_blocksize = gusmax_round_blocksize,
	.commit_settings = gusmax_commit_settings,
	.start_output = gusmax_dma_output,
	.start_input = gusmax_dma_input,
	.halt_output = gusmax_halt_out_dma,
	.halt_input = gusmax_halt_in_dma,
	.set_port = gusmax_mixer_set_port,
	.get_port = gusmax_mixer_get_port,
	.query_devinfo = gusmax_mixer_query_devinfo,
	.allocm = ad1848_malloc,
	.freem = ad1848_free,
	.round_buffersize = ad1848_round,
};

int
gusopen(void *addr, int flags)
{
	struct gus_softc *sc = addr;

	DPRINTF(("gusopen() called\n"));

	if ((flags & (FWRITE | FREAD)) == (FWRITE | FREAD) &&
	    sc->sc_recdrq == sc->sc_drq)
		return ENXIO;

	if (sc->sc_flags & GUS_OPEN)
		return EBUSY;

	gus_speaker_ctl(sc, (flags & FWRITE) ? SPKR_ON : SPKR_OFF);

	/*
	 * Some initialization
	 */

	sc->sc_flags |= GUS_OPEN;
	sc->sc_dmabuf = 0;
	sc->sc_playbuf = -1;
	sc->sc_bufcnt = 0;
	sc->sc_voc[GUS_VOICE_LEFT].start_addr = GUS_MEM_OFFSET - 1;
	sc->sc_voc[GUS_VOICE_LEFT].current_addr = GUS_MEM_OFFSET;

	if (HAS_CODEC(sc)) {
		ad1848_open(&sc->sc_codec, flags);
		sc->sc_codec.mute[AD1848_AUX1_CHANNEL] = 0;
		ad1848_mute_channel(&sc->sc_codec, AD1848_AUX1_CHANNEL, 0); /* turn on DAC output */
		if (flags & FREAD) {
			sc->sc_codec.mute[AD1848_MONO_CHANNEL] = 0;
			ad1848_mute_channel(&sc->sc_codec, AD1848_MONO_CHANNEL, 0);
		}
	} else if (flags & FREAD) {
		/* enable/unmute the microphone */
		if (HAS_MIXER(sc)) {
			gusics_mic_mute(&sc->sc_mixer, 0);
		} else
			gus_mic_ctl(sc, SPKR_ON);
	}
	if (sc->sc_nbufs == 0)
	    gus_round_blocksize(sc, GUS_BUFFER_MULTIPLE); /* default blksiz */
	return 0;
}

int
gusmaxopen(void *addr, int flags)
{
	struct ad1848_softc *ac = addr;
	return gusopen(ac->parent, flags);
}

void
gus_deinterleave(struct gus_softc *sc, void *buf, int size)
{
	/* deinterleave the stereo data.  We can use sc->sc_deintr_buf
	   for scratch space. */
	int i;

	if (size > sc->sc_blocksize) {
		printf("gus: deinterleave %d > %d\n", size, sc->sc_blocksize);
		return;
	} else if (size < sc->sc_blocksize) {
		DPRINTF(("gus: deinterleave %d < %d\n", size, sc->sc_blocksize));
	}

	/*
	 * size is in bytes.
	 */
	if (sc->sc_precision == 16) {
		u_short *dei = sc->sc_deintr_buf;
		u_short *sbuf = buf;
		size >>= 1;		/* bytecnt to shortcnt */
		/* copy 2nd of each pair of samples to the staging area, while
		   compacting the 1st of each pair into the original area. */
		for (i = 0; i < size/2-1; i++)  {
			dei[i] = sbuf[i*2+1];
			sbuf[i+1] = sbuf[i*2+2];
		}
		/*
		 * this has copied one less sample than half of the
		 * buffer.  The first sample of the 1st stream was
		 * already in place and didn't need copying.
		 * Therefore, we've moved all of the 1st stream's
		 * samples into place.  We have one sample from 2nd
		 * stream in the last slot of original area, not
		 * copied to the staging area (But we don't need to!).
		 * Copy the remainder of the original stream into place.
		 */
		bcopy(dei, &sbuf[size/2], i * sizeof(short));
	} else {
		u_char *dei = sc->sc_deintr_buf;
		u_char *sbuf = buf;
		for (i = 0; i < size/2-1; i++)  {
			dei[i] = sbuf[i*2+1];
			sbuf[i+1] = sbuf[i*2+2];
		}
		bcopy(dei, &sbuf[size/2], i);
	}
}

/*
 * Actually output a buffer to the DSP chip
 */

int
gusmax_dma_output(void *addr, void *buf, int size, void (*intr)(void *),
    void *arg)
{
	struct ad1848_softc *ac = addr;
	return gus_dma_output(ac->parent, buf, size, intr, arg);
}

/*
 * called at splaudio() from interrupt handler.
 */
void
stereo_dmaintr(void *arg)
{
    struct gus_softc *sc = arg;
    struct stereo_dma_intr *sa = &sc->sc_stereo;

    DMAPRINTF(("stereo_dmaintr"));

    /*
     * Put other half in its place, then call the real interrupt routine :)
     */

    sc->sc_dmaoutintr = sa->intr;
    sc->sc_outarg = sa->arg;

#ifdef GUSPLAYDEBUG
    if (gusstats) {
      microtime(&dmarecords[dmarecord_index].tv);
      dmarecords[dmarecord_index].gusaddr = sa->dmabuf;
      dmarecords[dmarecord_index].bsdaddr = sa->buffer;
      dmarecords[dmarecord_index].count = sa->size;
      dmarecords[dmarecord_index].channel = 1;
      dmarecords[dmarecord_index++].direction = 1;
      dmarecord_index = dmarecord_index % NDMARECS;
    }
#endif

    gusdmaout(sc, sa->flags, sa->dmabuf, (caddr_t) sa->buffer, sa->size);

    sa->flags = 0;
    sa->dmabuf = 0;
    sa->buffer = 0;
    sa->size = 0;
    sa->intr = 0;
    sa->arg = 0;
}

/*
 * Start up DMA output to the card.
 * Called at splaudio(), either from intr handler or from
 * generic audio code.
 */
int
gus_dma_output(void *addr, void *buf, int size, void (*intr)(void *), void *arg)
{
	struct gus_softc *sc = addr;
	u_char *buffer = buf;
	u_long boarddma;
	int flags;

	DMAPRINTF(("gus_dma_output %d @ %p\n", size, buf));
	if (size != sc->sc_blocksize) {
	    DPRINTF(("gus_dma_output reqsize %d not sc_blocksize %d\n",
		     size, sc->sc_blocksize));
	    return EINVAL;
	}

	flags = GUSMASK_DMA_WRITE;
	if (sc->sc_precision == 16)
	    flags |= GUSMASK_DMA_DATA_SIZE;
	if (sc->sc_encoding == AUDIO_ENCODING_ULAW ||
	    sc->sc_encoding == AUDIO_ENCODING_ALAW ||
	    sc->sc_encoding == AUDIO_ENCODING_ULINEAR_BE ||
	    sc->sc_encoding == AUDIO_ENCODING_ULINEAR_LE)
	    flags |= GUSMASK_DMA_INVBIT;

	if (sc->sc_channels == 2) {
		if (sc->sc_precision == 16) {
			if (size & 3) {
				DPRINTF(("gus_dma_output: unpaired 16bit samples"));
				size &= 3;
			}
		} else if (size & 1) {
			DPRINTF(("gus_dma_output: unpaired samples"));
			size &= 1;
		}
		if (size == 0) {
			return 0;
		}

		gus_deinterleave(sc, (void *)buffer, size);

		size >>= 1;

		boarddma = size * sc->sc_dmabuf + GUS_MEM_OFFSET;

		sc->sc_stereo.intr = intr;
		sc->sc_stereo.arg = arg;
		sc->sc_stereo.size = size;
		sc->sc_stereo.dmabuf = boarddma + GUS_LEFT_RIGHT_OFFSET;
		sc->sc_stereo.buffer = buffer + size;
		sc->sc_stereo.flags = flags;
		if (gus_dostereo) {
		  intr = stereo_dmaintr;
		  arg = sc;
		}
	} else
		boarddma = size * sc->sc_dmabuf + GUS_MEM_OFFSET;


	sc->sc_flags |= GUS_LOCKED;
	sc->sc_dmaoutintr = intr;
	sc->sc_outarg = arg;

#ifdef GUSPLAYDEBUG
	if (gusstats) {
	  microtime(&dmarecords[dmarecord_index].tv);
	  dmarecords[dmarecord_index].gusaddr = boarddma;
	  dmarecords[dmarecord_index].bsdaddr = buffer;
	  dmarecords[dmarecord_index].count = size;
	  dmarecords[dmarecord_index].channel = 0;
	  dmarecords[dmarecord_index++].direction = 1;
	  dmarecord_index = dmarecord_index % NDMARECS;
	}
#endif

	gusdmaout(sc, flags, boarddma, (caddr_t) buffer, size);
	return 0;
}

void
gusmax_close(void *addr)
{
	struct ad1848_softc *ac = addr;
	struct gus_softc *sc = ac->parent;
#if 0
	ac->mute[AD1848_AUX1_CHANNEL] = MUTE_ALL;
	ad1848_mute_channel(ac, MUTE_ALL); /* turn off DAC output */
#endif
	ad1848_close(ac);
	gusclose(sc);
}

/*
 * Close out device stuff.  Called at splaudio() from generic audio layer.
 */
void
gusclose(void *addr)
{
	struct gus_softc *sc = addr;

        DPRINTF(("gus_close: sc=%p\n", sc));

/*	if (sc->sc_flags & GUS_DMAOUT_ACTIVE) */ {
		gus_halt_out_dma(sc);
	}
/*	if (sc->sc_flags & GUS_DMAIN_ACTIVE) */ {
		gus_halt_in_dma(sc);
	}
	sc->sc_flags &= ~(GUS_OPEN|GUS_LOCKED|GUS_DMAOUT_ACTIVE|GUS_DMAIN_ACTIVE);

	if (sc->sc_deintr_buf) {
		free(sc->sc_deintr_buf, M_DEVBUF, 0);
		sc->sc_deintr_buf = NULL;
	}
	/* turn off speaker, etc. */

	/* make sure the voices shut up: */
	gus_stop_voice(sc, GUS_VOICE_LEFT, 1);
	gus_stop_voice(sc, GUS_VOICE_RIGHT, 0);
}

/*
 * Service interrupts.  Farm them off to helper routines if we are using the
 * GUS for simple playback/record
 */

#ifdef AUDIO_DEBUG
int gusintrcnt;
int gusdmaintrcnt;
int gusvocintrcnt;
#endif

int
gusintr(void *arg)
{
	struct gus_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh1 = sc->sc_ioh1;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	unsigned char intr;

	int retval = 0;

	DPRINTF(("gusintr\n"));
#ifdef AUDIO_DEBUG
	gusintrcnt++;
#endif
	if (HAS_CODEC(sc))
		retval = ad1848_intr(&sc->sc_codec);
	mtx_enter(&audio_lock);
	if ((intr = bus_space_read_1(iot, ioh1, GUS_IRQ_STATUS)) & GUSMASK_IRQ_DMATC) {
		DMAPRINTF(("gusintr dma flags=%x\n", sc->sc_flags));
#ifdef AUDIO_DEBUG
		gusdmaintrcnt++;
#endif
		retval += gus_dmaout_intr(sc);
		if (sc->sc_flags & GUS_DMAIN_ACTIVE) {
		    SELECT_GUS_REG(iot, ioh2, GUSREG_SAMPLE_CONTROL);
		    intr = bus_space_read_1(iot, ioh2, GUS_DATA_HIGH);
		    if (intr & GUSMASK_SAMPLE_DMATC) {
			retval += gus_dmain_intr(sc);
		    }
		}
	}
	if (intr & (GUSMASK_IRQ_VOICE | GUSMASK_IRQ_VOLUME)) {
		DMAPRINTF(("gusintr voice flags=%x\n", sc->sc_flags));
#ifdef AUDIO_DEBUG
		gusvocintrcnt++;
#endif
		retval += gus_voice_intr(sc);
	}
	if (retval) {
		mtx_leave(&audio_lock);
		return 1;
	}
	mtx_leave(&audio_lock);
	return retval;
}

int gus_bufcnt[GUS_MEM_FOR_BUFFERS / GUS_BUFFER_MULTIPLE];
int gus_restart;				/* how many restarts? */
int gus_stops;				/* how many times did voice stop? */
int gus_falsestops;			/* stopped but not done? */
int gus_continues;

struct playcont {
	struct timeval tv;
	u_int playbuf;
	u_int dmabuf;
	u_char bufcnt;
	u_char vaction;
	u_char voccntl;
	u_char volcntl;
	u_long curaddr;
	u_long endaddr;
} playstats[NDMARECS];

int playcntr;

void
gus_dmaout_timeout(void *arg)
{
	struct gus_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	printf("%s: dmaout timeout\n", sc->sc_dev.dv_xname);
	/*
	 * Stop any DMA.
	 */

	mtx_enter(&audio_lock);
	SELECT_GUS_REG(iot, ioh2, GUSREG_DMA_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0);
 
#if 0
	/* XXX we will dmadone below? */
	isa_dmaabort(sc->sc_dev.dv_parent, sc->sc_drq);
#endif
 
	gus_dmaout_dointr(sc);
	mtx_leave(&audio_lock);
}


/*
 * Service DMA interrupts.  This routine will only get called if we're doing
 * a DMA transfer for playback/record requests from the audio layer.
 */

int
gus_dmaout_intr(struct gus_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	/*
	 * If we got a DMA transfer complete from the GUS DRAM, then deal
	 * with it.
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_DMA_CONTROL);
	if (bus_space_read_1(iot, ioh2, GUS_DATA_HIGH) & GUSMASK_DMA_IRQPEND) {
	    timeout_del(&sc->sc_dma_tmo);
	    gus_dmaout_dointr(sc);
	    return 1;
	}
	return 0;
}

void
gus_dmaout_dointr(struct gus_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	/* sc->sc_dmaoutcnt - 1 because DMA controller counts from zero?. */
	isa_dmadone(sc->sc_dev.dv_parent, sc->sc_drq);
	sc->sc_flags &= ~GUS_DMAOUT_ACTIVE;  /* pending DMA is done */
	DMAPRINTF(("gus_dmaout_dointr %d @ %p\n", sc->sc_dmaoutcnt,
		   sc->sc_dmaoutaddr));

	/*
	 * to prevent clicking, we need to copy last sample
	 * from last buffer to scratch area just before beginning of
	 * buffer.  However, if we're doing formats that are converted by
	 * the card during the DMA process, we need to pick up the converted
	 * byte rather than the one we have in memory.
	 */
	if (sc->sc_dmabuf == sc->sc_nbufs - 1) {
	  int i;
	  switch (sc->sc_encoding) {
	  case AUDIO_ENCODING_SLINEAR_LE:
	  case AUDIO_ENCODING_SLINEAR_BE:
	    if (sc->sc_precision == 8)
	      goto byte;
	    /* we have the native format */
	    for (i = 1; i <= 2; i++)
	      guspoke(iot, ioh2, sc->sc_gusaddr -
		      (sc->sc_nbufs - 1) * sc->sc_chanblocksize - i,
		      sc->sc_dmaoutaddr[sc->sc_dmaoutcnt-i]);
	    break;
	  case AUDIO_ENCODING_ULINEAR_LE:
	  case AUDIO_ENCODING_ULINEAR_BE:
	    guspoke(iot, ioh2, sc->sc_gusaddr -
		    (sc->sc_nbufs - 1) * sc->sc_chanblocksize - 2,
		    guspeek(iot, ioh2,
			    sc->sc_gusaddr + sc->sc_chanblocksize - 2));
	  case AUDIO_ENCODING_ALAW:
	  case AUDIO_ENCODING_ULAW:
	  byte:
	    /* we need to fetch the translated byte, then stuff it. */
	    guspoke(iot, ioh2, sc->sc_gusaddr -
		    (sc->sc_nbufs - 1) * sc->sc_chanblocksize - 1,
		    guspeek(iot, ioh2,
			    sc->sc_gusaddr + sc->sc_chanblocksize - 1));
	    break;
	  }
	}
	/*
	 * If this is the first half of stereo, "ignore" this one
	 * and copy out the second half.
	 */
	if (sc->sc_dmaoutintr == stereo_dmaintr) {
	    (*sc->sc_dmaoutintr)(sc->sc_outarg);
	    return;
	}
	/*
	 * If the voice is stopped, then start it.  Reset the loop
	 * and roll bits.  Call the audio layer routine, since if
	 * we're starting a stopped voice, that means that the next
	 * buffer can be filled
	 */

	sc->sc_flags &= ~GUS_LOCKED;
	if (sc->sc_voc[GUS_VOICE_LEFT].voccntl &
	    GUSMASK_VOICE_STOPPED) {
	    if (sc->sc_flags & GUS_PLAYING) {
		printf("%s: playing yet stopped?\n", sc->sc_dev.dv_xname);
	    }
	    sc->sc_bufcnt++; /* another yet to be played */
	    gus_start_playing(sc, sc->sc_dmabuf);
	    gus_restart++;
	} else {
	    /*
	     * set the sound action based on which buffer we
	     * just transferred.  If we just transferred buffer 0
	     * we want the sound to loop when it gets to the nth
	     * buffer; if we just transferred
	     * any other buffer, we want the sound to roll over
	     * at least one more time.  The voice interrupt
	     * handlers will take care of accounting &
	     * setting control bits if it's not caught up to us
	     * yet.
	     */
	    if (++sc->sc_bufcnt == 2) {
		/*
		 * XXX
		 * If we're too slow in reaction here,
		 * the voice could be just approaching the
		 * end of its run.  It should be set to stop,
		 * so these adjustments might not DTRT.
		 */
		if (sc->sc_dmabuf == 0 &&
		    sc->sc_playbuf == sc->sc_nbufs - 1) {
		    /* player is just at the last buf, we're at the
		       first.  Turn on looping, turn off rolling. */
		    sc->sc_voc[GUS_VOICE_LEFT].voccntl |= GUSMASK_LOOP_ENABLE;
		    sc->sc_voc[GUS_VOICE_LEFT].volcntl &= ~GUSMASK_VOICE_ROLL;
		    playstats[playcntr].vaction = 3;
		} else {
		    /* player is at previous buf:
		       turn on rolling, turn off looping */
		    sc->sc_voc[GUS_VOICE_LEFT].voccntl &= ~GUSMASK_LOOP_ENABLE;
		    sc->sc_voc[GUS_VOICE_LEFT].volcntl |= GUSMASK_VOICE_ROLL;
		    playstats[playcntr].vaction = 4;
		}
#ifdef GUSPLAYDEBUG
		if (gusstats) {
		  microtime(&playstats[playcntr].tv);
		  playstats[playcntr].endaddr = sc->sc_voc[GUS_VOICE_LEFT].end_addr;
		  playstats[playcntr].voccntl = sc->sc_voc[GUS_VOICE_LEFT].voccntl;
		  playstats[playcntr].volcntl = sc->sc_voc[GUS_VOICE_LEFT].volcntl;
		  playstats[playcntr].playbuf = sc->sc_playbuf;
		  playstats[playcntr].dmabuf = sc->sc_dmabuf;
		  playstats[playcntr].bufcnt = sc->sc_bufcnt;
		  playstats[playcntr++].curaddr = gus_get_curaddr(sc, GUS_VOICE_LEFT);
		  playcntr = playcntr % NDMARECS;
		}
#endif
		bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, GUS_VOICE_LEFT);
		SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);
		bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[GUS_VOICE_LEFT].voccntl);
		SELECT_GUS_REG(iot, ioh2, GUSREG_VOLUME_CONTROL);
		bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[GUS_VOICE_LEFT].volcntl);
	    }
	}
	gus_bufcnt[sc->sc_bufcnt-1]++;
	/*
	 * flip to the next DMA buffer
	 */

	sc->sc_dmabuf = (sc->sc_dmabuf + 1) % sc->sc_nbufs;
	/*
	 * See comments below about DMA admission control strategy.
	 * We can call the upper level here if we have an
	 * idle buffer (not currently playing) to DMA into.
	 */
	if (sc->sc_dmaoutintr && sc->sc_bufcnt < sc->sc_nbufs) {
	    /* clean out to prevent double calls */
	    void (*pfunc)(void *) = sc->sc_dmaoutintr;
	    void *arg = sc->sc_outarg;

	    sc->sc_outarg = 0;
	    sc->sc_dmaoutintr = 0;
	    (*pfunc)(arg);
	}
}

/*
 * Service voice interrupts
 */

int
gus_voice_intr(struct gus_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	int ignore = 0, voice, rval = 0;
	unsigned char intr, status;

	/*
	 * The point of this may not be obvious at first.  A voice can
	 * interrupt more than once; according to the GUS SDK we are supposed
	 * to ignore multiple interrupts for the same voice.
	 */

	while(1) {
		SELECT_GUS_REG(iot, ioh2, GUSREG_IRQ_STATUS);
		intr = bus_space_read_1(iot, ioh2, GUS_DATA_HIGH);

		if ((intr & (GUSMASK_WIRQ_VOLUME | GUSMASK_WIRQ_VOICE))
			== (GUSMASK_WIRQ_VOLUME | GUSMASK_WIRQ_VOICE))
			/*
			 * No more interrupts, time to return
			 */
			return rval;

		if ((intr & GUSMASK_WIRQ_VOICE) == 0) {

		    /*
		     * We've got a voice interrupt.  Ignore previous
		     * interrupts by the same voice.
		     */

		    rval = 1;
		    voice = intr & GUSMASK_WIRQ_VOICEMASK;

		    if ((1 << voice) & ignore)
			break;

		    ignore |= 1 << voice;

		    /*
		     * If the voice is stopped, then force it to stop
		     * (this stops it from continuously generating IRQs)
		     */

		    SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL+0x80);
		    status = bus_space_read_1(iot, ioh2, GUS_DATA_HIGH);
		    if (status & GUSMASK_VOICE_STOPPED) {
			if (voice != GUS_VOICE_LEFT) {
			    DMAPRINTF(("%s: spurious voice %d stop?\n",
				       sc->sc_dev.dv_xname, voice));
			    gus_stop_voice(sc, voice, 0);
			    continue;
			}
			gus_stop_voice(sc, voice, 1);
			/* also kill right voice */
			gus_stop_voice(sc, GUS_VOICE_RIGHT, 0);
			sc->sc_bufcnt--; /* it finished a buffer */
			if (sc->sc_bufcnt > 0) {
			    /*
			     * probably a race to get here: the voice
			     * stopped while the DMA code was just trying to
			     * get the next buffer in place.
			     * Start the voice again.
			     */
			    printf("%s: stopped voice not drained? (%x)\n",
				   sc->sc_dev.dv_xname, sc->sc_bufcnt);
			    gus_falsestops++;

			    sc->sc_playbuf = (sc->sc_playbuf + 1) % sc->sc_nbufs;
			    gus_start_playing(sc, sc->sc_playbuf);
			} else if (sc->sc_bufcnt < 0) {
			    panic("%s: negative bufcnt in stopped voice",
				   sc->sc_dev.dv_xname);
			} else {
			    sc->sc_playbuf = -1; /* none are active */
			    gus_stops++;
			}
			/* fall through to callback and admit another
			   buffer.... */
		    } else if (sc->sc_bufcnt != 0) {
			/*
			 * This should always be taken if the voice
			 * is not stopped.
			 */
			gus_continues++;
			if (gus_continue_playing(sc, voice)) {
				/*
				 * we shouldn't have continued--active DMA
				 * is in the way in the ring, for
				 * some as-yet undebugged reason.
				 */
				gus_stop_voice(sc, GUS_VOICE_LEFT, 1);
				/* also kill right voice */
				gus_stop_voice(sc, GUS_VOICE_RIGHT, 0);
				sc->sc_playbuf = -1;
				gus_stops++;
			}
		    }
		    /*
		     * call the upper level to send on down another
		     * block. We do admission rate control as follows:
		     *
		     * When starting up output (in the first N
		     * blocks), call the upper layer after the DMA is
		     * complete (see above in gus_dmaout_intr()).
		     *
		     * When output is already in progress and we have
		     * no more GUS buffers to use for DMA, the DMA
		     * output routines do not call the upper layer.
		     * Instead, we call the DMA completion routine
		     * here, after the voice interrupts indicating
		     * that it's finished with a buffer.
		     *
		     * However, don't call anything here if the DMA
		     * output flag is set, (which shouldn't happen)
		     * because we'll squish somebody else's DMA if
		     * that's the case.  When DMA is done, it will
		     * call back if there is a spare buffer.
		     */
		    if (sc->sc_dmaoutintr && !(sc->sc_flags & GUS_LOCKED)) {
			if (sc->sc_dmaoutintr == stereo_dmaintr)
			    printf("gusdmaout botch?\n");
			else {
			    /* clean out to avoid double calls */
			    void (*pfunc)(void *) = sc->sc_dmaoutintr;
			    void *arg = sc->sc_outarg;

			    sc->sc_outarg = 0;
			    sc->sc_dmaoutintr = 0;
			    (*pfunc)(arg);
			}
		    }
		}

		/*
		 * Ignore other interrupts for now
		 */
	}
	return 0;
}

void
gus_start_playing(struct gus_softc *sc, int bufno)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	/*
	 * Start the voices playing, with buffer BUFNO.
	 */

	/*
	 * Loop or roll if we have buffers ready.
	 */

	if (sc->sc_bufcnt == 1) {
		sc->sc_voc[GUS_VOICE_LEFT].voccntl &= ~(GUSMASK_LOOP_ENABLE);
		sc->sc_voc[GUS_VOICE_LEFT].volcntl &= ~(GUSMASK_VOICE_ROLL);
	} else {
		if (bufno == sc->sc_nbufs - 1) {
			sc->sc_voc[GUS_VOICE_LEFT].voccntl |= GUSMASK_LOOP_ENABLE;
			sc->sc_voc[GUS_VOICE_LEFT].volcntl &= ~(GUSMASK_VOICE_ROLL);
		} else {
			sc->sc_voc[GUS_VOICE_LEFT].voccntl &= ~GUSMASK_LOOP_ENABLE;
			sc->sc_voc[GUS_VOICE_LEFT].volcntl |= GUSMASK_VOICE_ROLL;
		}
	}

	bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, GUS_VOICE_LEFT);

	SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[GUS_VOICE_LEFT].voccntl);

	SELECT_GUS_REG(iot, ioh2, GUSREG_VOLUME_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[GUS_VOICE_LEFT].volcntl);

	sc->sc_voc[GUS_VOICE_LEFT].current_addr =
		GUS_MEM_OFFSET + sc->sc_chanblocksize * bufno;
	sc->sc_voc[GUS_VOICE_LEFT].end_addr =
		sc->sc_voc[GUS_VOICE_LEFT].current_addr + sc->sc_chanblocksize - 1;
	sc->sc_voc[GUS_VOICE_RIGHT].current_addr =
		sc->sc_voc[GUS_VOICE_LEFT].current_addr +
		(gus_dostereo && sc->sc_channels == 2 ? GUS_LEFT_RIGHT_OFFSET : 0);
	/*
	 * set up right channel to just loop forever, no interrupts,
	 * starting at the buffer we just filled.  We'll feed it data
	 * at the same time as left channel.
	 */
	sc->sc_voc[GUS_VOICE_RIGHT].voccntl |= GUSMASK_LOOP_ENABLE;
	sc->sc_voc[GUS_VOICE_RIGHT].volcntl &= ~(GUSMASK_VOICE_ROLL);

#ifdef GUSPLAYDEBUG
	if (gusstats) {
		microtime(&playstats[playcntr].tv);
		playstats[playcntr].curaddr = sc->sc_voc[GUS_VOICE_LEFT].current_addr;

		playstats[playcntr].voccntl = sc->sc_voc[GUS_VOICE_LEFT].voccntl;
		playstats[playcntr].volcntl = sc->sc_voc[GUS_VOICE_LEFT].volcntl;
		playstats[playcntr].endaddr = sc->sc_voc[GUS_VOICE_LEFT].end_addr;
		playstats[playcntr].playbuf = bufno;
		playstats[playcntr].dmabuf = sc->sc_dmabuf;
		playstats[playcntr].bufcnt = sc->sc_bufcnt;
		playstats[playcntr++].vaction = 5;
		playcntr = playcntr % NDMARECS;
	}
#endif

	bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, GUS_VOICE_RIGHT);
	SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[GUS_VOICE_RIGHT].voccntl);
	SELECT_GUS_REG(iot, ioh2, GUSREG_VOLUME_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[GUS_VOICE_RIGHT].volcntl);

	gus_start_voice(sc, GUS_VOICE_RIGHT, 0);
	gus_start_voice(sc, GUS_VOICE_LEFT, 1);
	if (sc->sc_playbuf == -1)
		/* mark start of playing */
		sc->sc_playbuf = bufno;
}

int
gus_continue_playing(struct gus_softc *sc, int voice)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	/*
	 * stop this voice from interrupting while we work.
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[voice].voccntl & ~(GUSMASK_VOICE_IRQ));

	/*
	 * update playbuf to point to the buffer the hardware just started
	 * playing
	 */
	sc->sc_playbuf = (sc->sc_playbuf + 1) % sc->sc_nbufs;

	/*
	 * account for buffer just finished
	 */
	if (--sc->sc_bufcnt == 0) {
		DPRINTF(("gus: bufcnt 0 on continuing voice?\n"));
	}
	if (sc->sc_playbuf == sc->sc_dmabuf && (sc->sc_flags & GUS_LOCKED)) {
		printf("%s: continue into active dmabuf?\n", sc->sc_dev.dv_xname);
		return 1;
	}

	/*
	 * Select the end of the buffer based on the currently active
	 * buffer, [plus extra contiguous buffers (if ready)].
	 */

	/*
	 * set endpoint at end of buffer we just started playing.
	 *
	 * The total gets -1 because end addrs are one less than you might
	 * think (the end_addr is the address of the last sample to play)
	 */
	gus_set_endaddr(sc, voice, GUS_MEM_OFFSET +
			sc->sc_chanblocksize * (sc->sc_playbuf + 1) - 1);

	if (sc->sc_bufcnt < 2) {
		/*
		 * Clear out the loop and roll flags, and rotate the currently
		 * playing buffer.  That way, if we don't manage to get more
		 * data before this buffer finishes, we'll just stop.
		 */
		sc->sc_voc[voice].voccntl &= ~GUSMASK_LOOP_ENABLE;
		sc->sc_voc[voice].volcntl &= ~GUSMASK_VOICE_ROLL;
		playstats[playcntr].vaction = 0;
	} else {
		/*
		 * We have some buffers to play.  set LOOP if we're on the
		 * last buffer in the ring, otherwise set ROLL.
		 */
		if (sc->sc_playbuf == sc->sc_nbufs - 1) {
			sc->sc_voc[voice].voccntl |= GUSMASK_LOOP_ENABLE;
			sc->sc_voc[voice].volcntl &= ~GUSMASK_VOICE_ROLL;
			playstats[playcntr].vaction = 1;
		} else {
			sc->sc_voc[voice].voccntl &= ~GUSMASK_LOOP_ENABLE;
			sc->sc_voc[voice].volcntl |= GUSMASK_VOICE_ROLL;
			playstats[playcntr].vaction = 2;
		}
	}
#ifdef GUSPLAYDEBUG
	if (gusstats) {
		microtime(&playstats[playcntr].tv);
		playstats[playcntr].curaddr = gus_get_curaddr(sc, voice);

		playstats[playcntr].voccntl = sc->sc_voc[voice].voccntl;
		playstats[playcntr].volcntl = sc->sc_voc[voice].volcntl;
		playstats[playcntr].endaddr = sc->sc_voc[voice].end_addr;
		playstats[playcntr].playbuf = sc->sc_playbuf;
		playstats[playcntr].dmabuf = sc->sc_dmabuf;
		playstats[playcntr++].bufcnt = sc->sc_bufcnt;
		playcntr = playcntr % NDMARECS;
	}
#endif

	/*
	 * (re-)set voice parameters.  This will reenable interrupts from this
	 * voice.
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[voice].voccntl);
	SELECT_GUS_REG(iot, ioh2, GUSREG_VOLUME_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[voice].volcntl);
	return 0;
}

/*
 * Send/receive data into GUS's DRAM using DMA.  Called at splaudio()
 */

void
gusdmaout(struct gus_softc *sc, int flags, u_long gusaddr, caddr_t buffaddr,
    int length)
{
	unsigned char c = (unsigned char) flags;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	DMAPRINTF(("gusdmaout flags=%x scflags=%x\n", flags, sc->sc_flags));

	sc->sc_gusaddr = gusaddr;

	/*
	 * If we're using a 16 bit DMA channel, we have to jump through some
	 * extra hoops; this includes translating the DRAM address a bit
	 */

	if (sc->sc_drq >= 4) {
		c |= GUSMASK_DMA_WIDTH;
		gusaddr = convert_to_16bit(gusaddr);
	}

	/*
	 * Add flag bits that we always set - fast DMA, enable IRQ
	 */

	c |= GUSMASK_DMA_ENABLE | GUSMASK_DMA_R0 | GUSMASK_DMA_IRQ;

	/*
	 * Make sure the GUS _isn't_ setup for DMA
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_DMA_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0);

	/*
	 * Tell the PC DMA controller to start doing DMA
	 */

	sc->sc_dmaoutaddr = (u_char *) buffaddr;
	sc->sc_dmaoutcnt = length;
	isa_dmastart(sc->sc_dev.dv_parent, sc->sc_drq, buffaddr, length,
	    NULL, DMAMODE_WRITE, BUS_DMA_NOWAIT);

	/*
	 * Set up DMA address - use the upper 16 bits ONLY
	 */

	sc->sc_flags |= GUS_DMAOUT_ACTIVE;

	SELECT_GUS_REG(iot, ioh2, GUSREG_DMA_START);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, (int) (gusaddr >> 4));

	/*
	 * Tell the GUS to start doing DMA
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_DMA_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, c);

	/*
	 * XXX If we don't finish in one second, give up...
	 */
	timeout_add_sec(&sc->sc_dma_tmo, 1);
}

/*
 * Start a voice playing on the GUS.  Called from interrupt handler at
 * splaudio().
 */

void
gus_start_voice(struct gus_softc *sc, int voice, int intrs)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	u_long start;
	u_long current;
	u_long end;

	/*
	 * Pick all the values for the voice out of the gus_voice struct
	 * and use those to program the voice
	 */

	start = sc->sc_voc[voice].start_addr;
	current = sc->sc_voc[voice].current_addr;
	end = sc->sc_voc[voice].end_addr;

	/*
	 * If we're using 16 bit data, mangle the addresses a bit
	 */

	if (sc->sc_voc[voice].voccntl & GUSMASK_DATA_SIZE16) {
	        /* -1 on start so that we get onto sample boundary--other
		   code always sets it for 1-byte rollover protection */
		start = convert_to_16bit(start-1);
		current = convert_to_16bit(current);
		end = convert_to_16bit(end);
	}

	/*
	 * Select the voice we want to use, and program the data addresses
	 */

	bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, (unsigned char) voice);

	SELECT_GUS_REG(iot, ioh2, GUSREG_START_ADDR_HIGH);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_HIGH(start));
	SELECT_GUS_REG(iot, ioh2, GUSREG_START_ADDR_LOW);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_LOW(start));

	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_HIGH);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_HIGH(current));
	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_LOW);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_LOW(current));

	SELECT_GUS_REG(iot, ioh2, GUSREG_END_ADDR_HIGH);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_HIGH(end));
	SELECT_GUS_REG(iot, ioh2, GUSREG_END_ADDR_LOW);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_LOW(end));

	/*
	 * (maybe) enable interrupts, disable voice stopping
	 */

	if (intrs) {
		sc->sc_flags |= GUS_PLAYING; /* playing is about to start */
		sc->sc_voc[voice].voccntl |= GUSMASK_VOICE_IRQ;
		DMAPRINTF(("gus voice playing=%x\n", sc->sc_flags));
	} else
		sc->sc_voc[voice].voccntl &= ~GUSMASK_VOICE_IRQ;
	sc->sc_voc[voice].voccntl &= ~(GUSMASK_VOICE_STOPPED |
		GUSMASK_STOP_VOICE);

	/*
	 * Tell the GUS about it.  Note that we're doing volume ramping here
	 * from 0 up to the set volume to help reduce clicks.
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_START_VOLUME);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x00);
	SELECT_GUS_REG(iot, ioh2, GUSREG_END_VOLUME);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[voice].current_volume >> 4);
	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_VOLUME);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x00);
	SELECT_GUS_REG(iot, ioh2, GUSREG_VOLUME_RATE);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 63);

	SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[voice].voccntl);
	SELECT_GUS_REG(iot, ioh2, GUSREG_VOLUME_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x00);
	delay(50);
	SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[voice].voccntl);
	SELECT_GUS_REG(iot, ioh2, GUSREG_VOLUME_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x00);

}

/*
 * Stop a given voice.  Called at splaudio().
 */

void
gus_stop_voice(struct gus_softc *sc, int voice, int intrs_too)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	sc->sc_voc[voice].voccntl |= GUSMASK_VOICE_STOPPED |
		GUSMASK_STOP_VOICE;
	if (intrs_too) {
	  sc->sc_voc[voice].voccntl &= ~(GUSMASK_VOICE_IRQ);
	  /* no more DMA to do */
	  sc->sc_flags &= ~GUS_PLAYING;
	}
	DMAPRINTF(("gusintr voice notplaying=%x\n", sc->sc_flags));

	guspoke(iot, ioh2, 0L, 0);

	bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, (unsigned char) voice);

	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_VOLUME);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);
	SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[voice].voccntl);
	delay(100);
	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_VOLUME);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);
	SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[voice].voccntl);

	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_HIGH);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);
	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_LOW);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);

}


/*
 * Set the volume of a given voice.  Called at splaudio().
 */
void
gus_set_volume(struct gus_softc *sc, int voice, int volume)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	unsigned int gusvol;

	gusvol = gus_log_volumes[volume < 512 ? volume : 511];

	sc->sc_voc[voice].current_volume = gusvol;

	bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, (unsigned char) voice);

	SELECT_GUS_REG(iot, ioh2, GUSREG_START_VOLUME);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, (unsigned char) (gusvol >> 4));

	SELECT_GUS_REG(iot, ioh2, GUSREG_END_VOLUME);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, (unsigned char) (gusvol >> 4));

	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_VOLUME);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, gusvol << 4);
	delay(500);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, gusvol << 4);

}

/*
 * Interface to the audio layer.
 */

int
gusmax_set_params(void *addr, int setmode, int usemode, struct audio_params *p,
    struct audio_params *r)
{
	struct ad1848_softc *ac = addr;
	struct gus_softc *sc = ac->parent;
	int error;

	error = ad1848_set_params(ac, setmode, usemode, p, r);
	if (error)
		return error;
	error = gus_set_params(sc, setmode, usemode, p, r);
	return error;
}

int
gus_set_params(void *addr, int setmode, int usemode, struct audio_params *p,
    struct audio_params *r)
{
	struct gus_softc *sc = addr;

	switch (p->encoding) {
	case AUDIO_ENCODING_SLINEAR_LE:
	case AUDIO_ENCODING_ULINEAR_LE:
		break;
	default:
		return (EINVAL);
	}

	/* XXX: why?! this is called with interrupts disabled */
	mtx_enter(&audio_lock);

	if (p->precision == 8) {
		sc->sc_voc[GUS_VOICE_LEFT].voccntl &= ~GUSMASK_DATA_SIZE16;
		sc->sc_voc[GUS_VOICE_RIGHT].voccntl &= ~GUSMASK_DATA_SIZE16;
	} else {
		sc->sc_voc[GUS_VOICE_LEFT].voccntl |= GUSMASK_DATA_SIZE16;
		sc->sc_voc[GUS_VOICE_RIGHT].voccntl |= GUSMASK_DATA_SIZE16;
	}

	sc->sc_encoding = p->encoding;
	sc->sc_precision = p->precision;
	sc->sc_channels = p->channels;

	mtx_leave(&audio_lock);

	if (p->sample_rate > gus_max_frequency[sc->sc_voices - GUS_MIN_VOICES])
		p->sample_rate = gus_max_frequency[sc->sc_voices - GUS_MIN_VOICES];
	if (setmode & AUMODE_RECORD)
		sc->sc_irate = p->sample_rate;
	if (setmode & AUMODE_PLAY)
		sc->sc_orate = p->sample_rate;

	p->bps = AUDIO_BPS(p->precision);
	r->bps = AUDIO_BPS(r->precision);
	p->msb = r->msb = 1;

	return 0;
}

/*
 * Interface to the audio layer - set the blocksize to the correct number
 * of units
 */

int
gusmax_round_blocksize(void *addr, int blocksize)
{
	struct ad1848_softc *ac = addr;
	struct gus_softc *sc = ac->parent;

/*	blocksize = ad1848_round_blocksize(ac, blocksize);*/
	return gus_round_blocksize(sc, blocksize);
}

int
gus_round_blocksize(void *addr, int blocksize)
{
	struct gus_softc *sc = addr;

	DPRINTF(("gus_round_blocksize called\n"));

	if ((sc->sc_encoding == AUDIO_ENCODING_ULAW ||
	     sc->sc_encoding == AUDIO_ENCODING_ALAW) && blocksize > 32768)
		blocksize = 32768;
	else if (blocksize > 65536)
		blocksize = 65536;

	if ((blocksize % GUS_BUFFER_MULTIPLE) != 0)
		blocksize = (blocksize / GUS_BUFFER_MULTIPLE + 1) *
			GUS_BUFFER_MULTIPLE;

	/* set up temporary buffer to hold the deinterleave, if necessary
	   for stereo output */
	if (sc->sc_deintr_buf) {
		free(sc->sc_deintr_buf, M_DEVBUF, 0);
		sc->sc_deintr_buf = NULL;
	}
	sc->sc_deintr_buf = malloc(blocksize/2, M_DEVBUF, M_WAITOK);

	sc->sc_blocksize = blocksize;
	/* multi-buffering not quite working yet. */
	sc->sc_nbufs = /*GUS_MEM_FOR_BUFFERS / blocksize*/ 2;

	gus_set_chan_addrs(sc);

	return blocksize;
}

int
gus_get_out_gain(caddr_t addr)
{
	struct gus_softc *sc = (struct gus_softc *) addr;

	DPRINTF(("gus_get_out_gain called\n"));
	return sc->sc_ogain / 2;
}

inline void
gus_set_voices(struct gus_softc *sc, int voices)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	/*
	 * Select the active number of voices
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_ACTIVE_VOICES);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, (voices-1) | 0xc0);

	sc->sc_voices = voices;
}

/*
 * Actually set the settings of various values on the card
 */

int
gusmax_commit_settings(void *addr)
{
	struct ad1848_softc *ac = addr;
	struct gus_softc *sc = ac->parent;
	int error;

	error = ad1848_commit_settings(ac);
	if (error)
		return error;
	return gus_commit_settings(sc);
}

/*
 * Commit the settings.  Called at normal IPL.
 */
int
gus_commit_settings(void *addr)
{
	struct gus_softc *sc = addr;

	DPRINTF(("gus_commit_settings called (gain = %d)\n",sc->sc_ogain));


	/* XXX: why?! this is called with interrupts disabled */
	mtx_enter(&audio_lock);

	gus_set_recrate(sc, sc->sc_irate);
	gus_set_volume(sc, GUS_VOICE_LEFT, sc->sc_ogain);
	gus_set_volume(sc, GUS_VOICE_RIGHT, sc->sc_ogain);
	gus_set_samprate(sc, GUS_VOICE_LEFT, sc->sc_orate);
	gus_set_samprate(sc, GUS_VOICE_RIGHT, sc->sc_orate);
	mtx_leave(&audio_lock);
	gus_set_chan_addrs(sc);

	return 0;
}

void
gus_set_chan_addrs(struct gus_softc *sc)
{
	/*
	 * We use sc_nbufs * blocksize bytes of storage in the on-board GUS
	 * ram.
	 * For mono, each of the sc_nbufs buffers is DMA'd to in one chunk,
	 * and both left & right channels play the same buffer.
	 *
	 * For stereo, each channel gets a contiguous half of the memory,
	 * and each has sc_nbufs buffers of size blocksize/2.
	 * Stereo data are deinterleaved in main memory before the DMA out
	 * routines are called to queue the output.
	 *
	 * The blocksize per channel is kept in sc_chanblocksize.
	 */
	if (sc->sc_channels == 2)
	    sc->sc_chanblocksize = sc->sc_blocksize/2;
	else
	    sc->sc_chanblocksize = sc->sc_blocksize;

	sc->sc_voc[GUS_VOICE_LEFT].start_addr = GUS_MEM_OFFSET - 1;
	sc->sc_voc[GUS_VOICE_RIGHT].start_addr =
	    (gus_dostereo && sc->sc_channels == 2 ? GUS_LEFT_RIGHT_OFFSET : 0)
	      + GUS_MEM_OFFSET - 1;
	sc->sc_voc[GUS_VOICE_RIGHT].current_addr =
	    sc->sc_voc[GUS_VOICE_RIGHT].start_addr + 1;
	sc->sc_voc[GUS_VOICE_RIGHT].end_addr =
	    sc->sc_voc[GUS_VOICE_RIGHT].start_addr +
	    sc->sc_nbufs * sc->sc_chanblocksize;

}

/*
 * Set the sample rate of the given voice.  Called at splaudio().
 */

void
gus_set_samprate(struct gus_softc *sc, int voice, int freq)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	unsigned int fc;
	u_long temp, f = (u_long) freq;

	/*
	 * calculate fc based on the number of active voices;
	 * we need to use longs to preserve enough bits
	 */

	temp = (u_long) gus_max_frequency[sc->sc_voices-GUS_MIN_VOICES];

	fc = (unsigned int)(((f << 9L) + (temp >> 1L)) / temp);

	fc <<= 1;


	/*
	 * Program the voice frequency, and set it in the voice data record
	 */

	bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, (unsigned char) voice);
	SELECT_GUS_REG(iot, ioh2, GUSREG_FREQ_CONTROL);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, fc);

	sc->sc_voc[voice].rate = freq;

}

/*
 * Set the sample rate of the recording frequency.  Formula is from the GUS
 * SDK.  Called at splaudio().
 */

void
gus_set_recrate(struct gus_softc *sc, u_long rate)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	u_char realrate;
	DPRINTF(("gus_set_recrate %lu\n", rate));

#if 0
	realrate = 9878400/(16*(rate+2)); /* formula from GUS docs */
#endif
	realrate = (9878400 >> 4)/rate - 2; /* formula from code, sigh. */

	SELECT_GUS_REG(iot, ioh2, GUSREG_SAMPLE_FREQ);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, realrate);
}

/*
 * Turn the output on or off.  Note that some
 * of these bits are flipped in the register
 */

int
gus_speaker_ctl(void *addr, int newstate)
{
	struct gus_softc *sc = (struct gus_softc *) addr;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh1 = sc->sc_ioh1;

	/* Line out bit is flipped: 0 enables, 1 disables */
	if ((newstate == SPKR_ON) &&
	    (sc->sc_mixcontrol & GUSMASK_LINE_OUT)) {
		sc->sc_mixcontrol &= ~GUSMASK_LINE_OUT;
		bus_space_write_1(iot, ioh1, GUS_MIX_CONTROL, sc->sc_mixcontrol);
	}
	if ((newstate == SPKR_OFF) &&
	    (sc->sc_mixcontrol & GUSMASK_LINE_OUT) == 0) {
		sc->sc_mixcontrol |= GUSMASK_LINE_OUT;
		bus_space_write_1(iot, ioh1, GUS_MIX_CONTROL, sc->sc_mixcontrol);
	}

	return 0;
}

int
gus_linein_ctl(void *addr, int newstate)
{
	struct gus_softc *sc = (struct gus_softc *) addr;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh1 = sc->sc_ioh1;

	/* Line in bit is flipped: 0 enables, 1 disables */
	if ((newstate == SPKR_ON) &&
	    (sc->sc_mixcontrol & GUSMASK_LINE_IN)) {
		sc->sc_mixcontrol &= ~GUSMASK_LINE_IN;
		bus_space_write_1(iot, ioh1, GUS_MIX_CONTROL, sc->sc_mixcontrol);
	}
	if ((newstate == SPKR_OFF) &&
	    (sc->sc_mixcontrol & GUSMASK_LINE_IN) == 0) {
		sc->sc_mixcontrol |= GUSMASK_LINE_IN;
		bus_space_write_1(iot, ioh1, GUS_MIX_CONTROL, sc->sc_mixcontrol);
	}

	return 0;
}

int
gus_mic_ctl(void *addr, int newstate)
{
	struct gus_softc *sc = (struct gus_softc *) addr;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh1 = sc->sc_ioh1;

	/* Mic bit is normal: 1 enables, 0 disables */
	if ((newstate == SPKR_ON) &&
	    (sc->sc_mixcontrol & GUSMASK_MIC_IN) == 0) {
		sc->sc_mixcontrol |= GUSMASK_MIC_IN;
		bus_space_write_1(iot, ioh1, GUS_MIX_CONTROL, sc->sc_mixcontrol);
	}
	if ((newstate == SPKR_OFF) &&
	    (sc->sc_mixcontrol & GUSMASK_MIC_IN)) {
		sc->sc_mixcontrol &= ~GUSMASK_MIC_IN;
		bus_space_write_1(iot, ioh1, GUS_MIX_CONTROL, sc->sc_mixcontrol);
	}

	return 0;
}

/*
 * Set the end address of a give voice.  Called at splaudio().
 */

void
gus_set_endaddr(struct gus_softc *sc, int voice, u_long addr)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	sc->sc_voc[voice].end_addr = addr;

	if (sc->sc_voc[voice].voccntl & GUSMASK_DATA_SIZE16)
		addr = convert_to_16bit(addr);

	SELECT_GUS_REG(iot, ioh2, GUSREG_END_ADDR_HIGH);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_HIGH(addr));
	SELECT_GUS_REG(iot, ioh2, GUSREG_END_ADDR_LOW);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_LOW(addr));

}

#ifdef GUSPLAYDEBUG
/*
 * Set current address.  Called at splaudio().
 */
void
gus_set_curaddr(struct gus_softc *sc, int voice, u_long addr)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	sc->sc_voc[voice].current_addr = addr;

	if (sc->sc_voc[voice].voccntl & GUSMASK_DATA_SIZE16)
		addr = convert_to_16bit(addr);

	bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, (unsigned char) voice);

	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_HIGH);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_HIGH(addr));
	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_LOW);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, ADDR_LOW(addr));

}

/*
 * Get current GUS playback address.  Called at splaudio().
 */
u_long
gus_get_curaddr(struct gus_softc *sc, int voice)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	u_long addr;

	bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, (unsigned char) voice);
	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_HIGH|GUSREG_READ);
	addr = (bus_space_read_2(iot, ioh2, GUS_DATA_LOW) & 0x1fff) << 7;
	SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_LOW|GUSREG_READ);
	addr |= (bus_space_read_2(iot, ioh2, GUS_DATA_LOW) >> 9L) & 0x7f;

	if (sc->sc_voc[voice].voccntl & GUSMASK_DATA_SIZE16)
	    addr = (addr & 0xc0000) | ((addr & 0x1ffff) << 1); /* undo 16-bit change */
	DPRINTF(("gus voice %d curaddr %ld end_addr %ld\n",
		 voice, addr, sc->sc_voc[voice].end_addr));
	/* XXX sanity check the address? */

	return(addr);
}
#endif

/*
 * Convert an address value to a "16 bit" value - why this is necessary I
 * have NO idea
 */

u_long
convert_to_16bit(u_long address)
{
	u_long old_address;

	old_address = address;
	address >>= 1;
	address &= 0x0001ffffL;
	address |= (old_address & 0x000c0000L);

	return (address);
}

/*
 * Write a value into the GUS's DRAM
 */

void
guspoke(bus_space_tag_t iot, bus_space_handle_t ioh2, long address,
    unsigned char value)
{

	/*
	 * Select the DRAM address
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_DRAM_ADDR_LOW);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, (unsigned int) (address & 0xffff));
	SELECT_GUS_REG(iot, ioh2, GUSREG_DRAM_ADDR_HIGH);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, (unsigned char) ((address >> 16) & 0xff));

	/*
	 * Actually write the data
	 */

	bus_space_write_1(iot, ioh2, GUS_DRAM_DATA, value);
}

/*
 * Read a value from the GUS's DRAM
 */

unsigned char
guspeek(bus_space_tag_t iot, bus_space_handle_t ioh2, u_long address)
{

	/*
	 * Select the DRAM address
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_DRAM_ADDR_LOW);
	bus_space_write_2(iot, ioh2, GUS_DATA_LOW, (unsigned int) (address & 0xffff));
	SELECT_GUS_REG(iot, ioh2, GUSREG_DRAM_ADDR_HIGH);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, (unsigned char) ((address >> 16) & 0xff));

	/*
	 * Read in the data from the board
	 */

	return (unsigned char) bus_space_read_1(iot, ioh2, GUS_DRAM_DATA);
}

/*
 * Reset the Gravis UltraSound card, completely
 */

void
gusreset(struct gus_softc *sc, int voices)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh1 = sc->sc_ioh1;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	bus_space_handle_t ioh4 = sc->sc_ioh4;
	int i;

	mtx_enter(&audio_lock);

	/*
	 * Reset the GF1 chip
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_RESET);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x00);

	delay(500);

	/*
	 * Release reset
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_RESET);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, GUSMASK_MASTER_RESET);

	delay(500);

	/*
	 * Reset MIDI port as well, if applicable
	 */

	if (ioh4 != (bus_space_handle_t)NULL) {
		bus_space_write_1(iot, ioh4, GUS_MIDI_CONTROL, MIDI_RESET);

		delay(500);

		bus_space_write_1(iot, ioh4, GUS_MIDI_CONTROL, 0x00);
	}

	/*
	 * Clear interrupts
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_DMA_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x00);
	SELECT_GUS_REG(iot, ioh2, GUSREG_TIMER_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x00);
	SELECT_GUS_REG(iot, ioh2, GUSREG_SAMPLE_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x00);

	gus_set_voices(sc, voices);

	bus_space_read_1(iot, ioh1, GUS_IRQ_STATUS);
	SELECT_GUS_REG(iot, ioh2, GUSREG_DMA_CONTROL);
	bus_space_read_1(iot, ioh2, GUS_DATA_HIGH);
	SELECT_GUS_REG(iot, ioh2, GUSREG_SAMPLE_CONTROL);
	bus_space_read_1(iot, ioh2, GUS_DATA_HIGH);
	SELECT_GUS_REG(iot, ioh2, GUSREG_IRQ_STATUS);
	bus_space_read_1(iot, ioh2, GUS_DATA_HIGH);

	/*
	 * Reset voice specific information
	 */

	for(i = 0; i < voices; i++) {
		bus_space_write_1(iot, ioh2, GUS_VOICE_SELECT, (unsigned char) i);

		SELECT_GUS_REG(iot, ioh2, GUSREG_VOICE_CNTL);

		sc->sc_voc[i].voccntl = GUSMASK_VOICE_STOPPED |
			GUSMASK_STOP_VOICE;

		bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[i].voccntl);

		sc->sc_voc[i].volcntl = GUSMASK_VOLUME_STOPPED |
				GUSMASK_STOP_VOLUME;

		SELECT_GUS_REG(iot, ioh2, GUSREG_VOLUME_CONTROL);
		bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, sc->sc_voc[i].volcntl);

		delay(100);

		gus_set_samprate(sc, i, 8000);
		SELECT_GUS_REG(iot, ioh2, GUSREG_START_ADDR_HIGH);
		bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);
		SELECT_GUS_REG(iot, ioh2, GUSREG_START_ADDR_LOW);
		bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);
		SELECT_GUS_REG(iot, ioh2, GUSREG_END_ADDR_HIGH);
		bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);
		SELECT_GUS_REG(iot, ioh2, GUSREG_END_ADDR_LOW);
		bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);
		SELECT_GUS_REG(iot, ioh2, GUSREG_VOLUME_RATE);
		bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x01);
		SELECT_GUS_REG(iot, ioh2, GUSREG_START_VOLUME);
		bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x10);
		SELECT_GUS_REG(iot, ioh2, GUSREG_END_VOLUME);
		bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0xe0);
		SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_VOLUME);
		bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);

		SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_HIGH);
		bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);
		SELECT_GUS_REG(iot, ioh2, GUSREG_CUR_ADDR_LOW);
		bus_space_write_2(iot, ioh2, GUS_DATA_LOW, 0x0000);
		SELECT_GUS_REG(iot, ioh2, GUSREG_PAN_POS);
		bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x07);
	}

	/*
	 * Clear out any pending IRQs
	 */

	bus_space_read_1(iot, ioh1, GUS_IRQ_STATUS);
	SELECT_GUS_REG(iot, ioh2, GUSREG_DMA_CONTROL);
	bus_space_read_1(iot, ioh2, GUS_DATA_HIGH);
	SELECT_GUS_REG(iot, ioh2, GUSREG_SAMPLE_CONTROL);
	bus_space_read_1(iot, ioh2, GUS_DATA_HIGH);
	SELECT_GUS_REG(iot, ioh2, GUSREG_IRQ_STATUS);
	bus_space_read_1(iot, ioh2, GUS_DATA_HIGH);

	SELECT_GUS_REG(iot, ioh2, GUSREG_RESET);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, GUSMASK_MASTER_RESET | GUSMASK_DAC_ENABLE |
		GUSMASK_IRQ_ENABLE);

	mtx_leave(&audio_lock);
}


int
gus_init_cs4231(struct gus_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh1 = sc->sc_ioh1;
	int port = sc->sc_iobase;
	u_char ctrl;

	ctrl = (port & 0xf0) >> 4;	/* set port address middle nibble */
	/*
	 * The codec is a bit weird--swapped dma channels.
	 */
	ctrl |= GUS_MAX_CODEC_ENABLE;
	if (sc->sc_drq >= 4)
		ctrl |= GUS_MAX_RECCHAN16;
	if (sc->sc_recdrq >= 4)
		ctrl |= GUS_MAX_PLAYCHAN16;

	bus_space_write_1(iot, ioh1, GUS_MAX_CTRL, ctrl);

	sc->sc_codec.sc_iot = sc->sc_iot;
	sc->sc_codec.sc_iobase = port+GUS_MAX_CODEC_BASE;

	if (ad1848_mapprobe(&sc->sc_codec, sc->sc_codec.sc_iobase) == 0) {
		sc->sc_flags &= ~GUS_CODEC_INSTALLED;
		return (0);
	} else {
		struct ad1848_volume vol = {AUDIO_MAX_GAIN, AUDIO_MAX_GAIN};
		sc->sc_flags |= GUS_CODEC_INSTALLED;
		sc->sc_codec.parent = sc;
		sc->sc_codec.sc_drq = sc->sc_recdrq;
		sc->sc_codec.sc_recdrq = sc->sc_drq;
		/* enable line in and mic in the GUS mixer; the codec chip
		   will do the real mixing for them. */
		sc->sc_mixcontrol &= ~GUSMASK_LINE_IN; /* 0 enables. */
		sc->sc_mixcontrol |= GUSMASK_MIC_IN; /* 1 enables. */
		bus_space_write_1(iot, ioh1, GUS_MIX_CONTROL, sc->sc_mixcontrol);

		ad1848_attach(&sc->sc_codec);
		/* turn on pre-MUX microphone gain. */
		ad1848_set_mic_gain(&sc->sc_codec, &vol);

		return (1);
	}
}

/*
 * stubs (XXX)
 */

int
gus_set_in_gain(caddr_t addr, u_int gain,  u_char balance)
{
	DPRINTF(("gus_set_in_gain called\n"));
	return 0;
}

int
gus_get_in_gain(caddr_t addr)
{
	DPRINTF(("gus_get_in_gain called\n"));
	return 0;
}

int
gusmax_dma_input(void *addr, void *buf, int size, void (*callback)(void *),
    void *arg)
{
	struct ad1848_softc *sc = addr;
	return gus_dma_input(sc->parent, buf, size, callback, arg);
}

/*
 * Start sampling the input source into the requested DMA buffer.
 * Called at splaudio(), either from top-half or from interrupt handler.
 */
int
gus_dma_input(void *addr, void *buf, int size, void (*callback)(void *),
    void *arg)
{
	struct gus_softc *sc = addr;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;
	u_char dmac;
	DMAPRINTF(("gus_dma_input called\n"));

	/*
	 * Sample SIZE bytes of data from the card, into buffer at BUF.
	 */
	if (sc->sc_precision == 16) {
	    return EINVAL;		/* XXX */
	}

	/* set DMA modes */
	dmac = GUSMASK_SAMPLE_IRQ|GUSMASK_SAMPLE_START;
	if (sc->sc_recdrq >= 4)
		dmac |= GUSMASK_SAMPLE_DATA16;
	if (sc->sc_encoding == AUDIO_ENCODING_ULAW ||
	    sc->sc_encoding == AUDIO_ENCODING_ALAW ||
	    sc->sc_encoding == AUDIO_ENCODING_ULINEAR_LE ||
	    sc->sc_encoding == AUDIO_ENCODING_ULINEAR_BE)
	    dmac |= GUSMASK_SAMPLE_INVBIT;
	if (sc->sc_channels == 2)
	    dmac |= GUSMASK_SAMPLE_STEREO;
	isa_dmastart(sc->sc_dev.dv_parent, sc->sc_recdrq, buf, size,
	    NULL, DMAMODE_READ, BUS_DMA_NOWAIT);

	DMAPRINTF(("gus_dma_input isadma_started\n"));
	sc->sc_flags |= GUS_DMAIN_ACTIVE;
	sc->sc_dmainintr = callback;
	sc->sc_inarg = arg;
	sc->sc_dmaincnt = size;
	sc->sc_dmainaddr = buf;

	SELECT_GUS_REG(iot, ioh2, GUSREG_SAMPLE_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, dmac);	/* Go! */


	DMAPRINTF(("gus_dma_input returning\n"));
	return 0;
}

int
gus_dmain_intr(struct gus_softc *sc)
{
        void (*callback)(void *);
	void *arg;

	DMAPRINTF(("gus_dmain_intr called\n"));
	if (sc->sc_dmainintr) {
	    isa_dmadone(sc->sc_dev.dv_parent, sc->sc_recdrq);
	    callback = sc->sc_dmainintr;
	    arg = sc->sc_inarg;

	    sc->sc_dmainaddr = 0;
	    sc->sc_dmaincnt = 0;
	    sc->sc_dmainintr = 0;
	    sc->sc_inarg = 0;

	    sc->sc_flags &= ~GUS_DMAIN_ACTIVE;
	    DMAPRINTF(("calling dmain_intr callback %p(%p)\n", callback, arg));
	    (*callback)(arg);
	    return 1;
	} else {
	    DMAPRINTF(("gus_dmain_intr false?\n"));
	    return 0;			/* XXX ??? */
	}
}

int
gusmax_halt_out_dma(void *addr)
{
	struct ad1848_softc *sc = addr;
	return gus_halt_out_dma(sc->parent);
}


int
gusmax_halt_in_dma(void *addr)
{
	struct ad1848_softc *sc = addr;
	return gus_halt_in_dma(sc->parent);
}

/*
 * Stop any DMA output.  Called at splaudio().
 */
int
gus_halt_out_dma(void *addr)
{
	struct gus_softc *sc = addr;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	mtx_enter(&audio_lock);
	DMAPRINTF(("gus_halt_out_dma called\n"));
	/*
	 * Make sure the GUS _isn't_ setup for DMA
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_DMA_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0);

	timeout_del(&sc->sc_dma_tmo);
	isa_dmaabort(sc->sc_dev.dv_parent, sc->sc_drq);
	sc->sc_flags &= ~(GUS_DMAOUT_ACTIVE|GUS_LOCKED);
	sc->sc_dmaoutintr = 0;
	sc->sc_outarg = 0;
	sc->sc_dmaoutaddr = 0;
	sc->sc_dmaoutcnt = 0;
	sc->sc_dmabuf = 0;
	sc->sc_bufcnt = 0;
	sc->sc_playbuf = -1;
	/* also stop playing */
	gus_stop_voice(sc, GUS_VOICE_LEFT, 1);
	gus_stop_voice(sc, GUS_VOICE_RIGHT, 0);
	mtx_leave(&audio_lock);
	return 0;
}

/*
 * Stop any DMA output.  Called at splaudio().
 */
int
gus_halt_in_dma(void *addr)
{
	struct gus_softc *sc = addr;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh2 = sc->sc_ioh2;

	mtx_enter(&audio_lock);
	DMAPRINTF(("gus_halt_in_dma called\n"));

	/*
	 * Make sure the GUS _isn't_ setup for DMA
	 */

	SELECT_GUS_REG(iot, ioh2, GUSREG_SAMPLE_CONTROL);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH,
	     bus_space_read_1(iot, ioh2, GUS_DATA_HIGH) & ~(GUSMASK_SAMPLE_START|GUSMASK_SAMPLE_IRQ));

	isa_dmaabort(sc->sc_dev.dv_parent, sc->sc_recdrq);
	sc->sc_flags &= ~GUS_DMAIN_ACTIVE;
	sc->sc_dmainintr = 0;
	sc->sc_inarg = 0;
	sc->sc_dmainaddr = 0;
	sc->sc_dmaincnt = 0;
	mtx_leave(&audio_lock);
	return 0;
}


ad1848_devmap_t gusmapping[] = {
  {GUSMAX_DAC_LVL, AD1848_KIND_LVL, AD1848_AUX1_CHANNEL},
  {GUSMAX_LINE_IN_LVL, AD1848_KIND_LVL, AD1848_LINE_CHANNEL},
  {GUSMAX_MONO_LVL, AD1848_KIND_LVL, AD1848_MONO_CHANNEL},
  {GUSMAX_CD_LVL, AD1848_KIND_LVL, AD1848_AUX2_CHANNEL},
  {GUSMAX_MONITOR_LVL, AD1848_KIND_LVL, AD1848_MONITOR_CHANNEL},
  {GUSMAX_OUT_LVL, AD1848_KIND_LVL, AD1848_DAC_CHANNEL},
  {GUSMAX_DAC_MUTE, AD1848_KIND_MUTE, AD1848_AUX1_CHANNEL},
  {GUSMAX_LINE_IN_MUTE, AD1848_KIND_MUTE, AD1848_LINE_CHANNEL},
  {GUSMAX_MONO_MUTE, AD1848_KIND_MUTE, AD1848_MONO_CHANNEL},
  {GUSMAX_CD_MUTE, AD1848_KIND_MUTE, AD1848_AUX2_CHANNEL},
  {GUSMAX_MONITOR_MUTE, AD1848_KIND_MUTE, AD1848_MONITOR_CHANNEL},
  {GUSMAX_REC_LVL, AD1848_KIND_RECORDGAIN, -1},
  {GUSMAX_RECORD_SOURCE, AD1848_KIND_RECORDSOURCE, -1}
};

int nummap = sizeof(gusmapping) / sizeof(gusmapping[0]);

int
gusmax_mixer_get_port(void *addr, mixer_ctrl_t *cp)
{
	struct ad1848_softc *ac = addr;
	struct gus_softc *sc = ac->parent;
	struct ad1848_volume vol;
	int error = ad1848_mixer_get_port(ac, gusmapping, nummap, cp);

	if (error != ENXIO)
	  return (error);

	error = EINVAL;

	switch (cp->dev) {
	case GUSMAX_SPEAKER_LVL:	/* fake speaker for mute naming */
		if (cp->type == AUDIO_MIXER_VALUE) {
			if (sc->sc_mixcontrol & GUSMASK_LINE_OUT)
				vol.left = vol.right = AUDIO_MAX_GAIN;
			else
				vol.left = vol.right = AUDIO_MIN_GAIN;
			error = 0;
			ad1848_from_vol(cp, &vol);
		}
		break;

	case GUSMAX_SPEAKER_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			cp->un.ord = sc->sc_mixcontrol & GUSMASK_LINE_OUT ? 1 : 0;
			error = 0;
		}
		break;
	default:
		error = ENXIO;
		break;
	}

	return(error);
}

int
gus_mixer_get_port(void *addr, mixer_ctrl_t *cp)
{
	struct gus_softc *sc = addr;
	struct ics2101_softc *ic = &sc->sc_mixer;
	struct ad1848_volume vol;
	int error = EINVAL;

	DPRINTF(("gus_mixer_get_port: dev=%d type=%d\n", cp->dev, cp->type));

	if (!HAS_MIXER(sc) && cp->dev > GUSICS_MASTER_MUTE)
		return ENXIO;

	switch (cp->dev) {

	case GUSICS_MIC_IN_MUTE:	/* Microphone */
		if (cp->type == AUDIO_MIXER_ENUM) {
			if (HAS_MIXER(sc))
				cp->un.ord = ic->sc_mute[GUSMIX_CHAN_MIC][ICSMIX_LEFT];
			else
				cp->un.ord =
				    sc->sc_mixcontrol & GUSMASK_MIC_IN ? 0 : 1;
			error = 0;
		}
		break;

	case GUSICS_LINE_IN_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			if (HAS_MIXER(sc))
				cp->un.ord = ic->sc_mute[GUSMIX_CHAN_LINE][ICSMIX_LEFT];
			else
				cp->un.ord =
				    sc->sc_mixcontrol & GUSMASK_LINE_IN ? 1 : 0;
			error = 0;
		}
		break;

	case GUSICS_MASTER_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			if (HAS_MIXER(sc))
				cp->un.ord = ic->sc_mute[GUSMIX_CHAN_MASTER][ICSMIX_LEFT];
			else
				cp->un.ord =
				    sc->sc_mixcontrol & GUSMASK_LINE_OUT ? 1 : 0;
			error = 0;
		}
		break;

	case GUSICS_DAC_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			cp->un.ord = ic->sc_mute[GUSMIX_CHAN_DAC][ICSMIX_LEFT];
			error = 0;
		}
		break;

	case GUSICS_CD_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			cp->un.ord = ic->sc_mute[GUSMIX_CHAN_CD][ICSMIX_LEFT];
			error = 0;
		}
		break;

	case GUSICS_MASTER_LVL:
		if (cp->type == AUDIO_MIXER_VALUE) {
			vol.left = ic->sc_setting[GUSMIX_CHAN_MASTER][ICSMIX_LEFT];
			vol.right = ic->sc_setting[GUSMIX_CHAN_MASTER][ICSMIX_RIGHT];
			if (ad1848_from_vol(cp, &vol))
				error = 0;
		}
		break;

	case GUSICS_MIC_IN_LVL:	/* Microphone */
		if (cp->type == AUDIO_MIXER_VALUE) {
			vol.left = ic->sc_setting[GUSMIX_CHAN_MIC][ICSMIX_LEFT];
			vol.right = ic->sc_setting[GUSMIX_CHAN_MIC][ICSMIX_RIGHT];
			if (ad1848_from_vol(cp, &vol))
				error = 0;
		}
		break;

	case GUSICS_LINE_IN_LVL:	/* line in */
		if (cp->type == AUDIO_MIXER_VALUE) {
			vol.left = ic->sc_setting[GUSMIX_CHAN_LINE][ICSMIX_LEFT];
			vol.right = ic->sc_setting[GUSMIX_CHAN_LINE][ICSMIX_RIGHT];
			if (ad1848_from_vol(cp, &vol))
				error = 0;
		}
		break;


	case GUSICS_CD_LVL:
		if (cp->type == AUDIO_MIXER_VALUE) {
			vol.left = ic->sc_setting[GUSMIX_CHAN_CD][ICSMIX_LEFT];
			vol.right = ic->sc_setting[GUSMIX_CHAN_CD][ICSMIX_RIGHT];
			if (ad1848_from_vol(cp, &vol))
				error = 0;
		}
		break;

	case GUSICS_DAC_LVL:		/* dac out */
		if (cp->type == AUDIO_MIXER_VALUE) {
			vol.left = ic->sc_setting[GUSMIX_CHAN_DAC][ICSMIX_LEFT];
			vol.right = ic->sc_setting[GUSMIX_CHAN_DAC][ICSMIX_RIGHT];
			if (ad1848_from_vol(cp, &vol))
				error = 0;
		}
		break;


	case GUSICS_RECORD_SOURCE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			/* Can't set anything else useful, sigh. */
			 cp->un.ord = 0;
		}
		break;

	default:
		return ENXIO;
	    /*NOTREACHED*/
	}
	return error;
}

void
gusics_master_mute(struct ics2101_softc *ic, int mute)
{
	ics2101_mix_mute(ic, GUSMIX_CHAN_MASTER, ICSMIX_LEFT, mute);
	ics2101_mix_mute(ic, GUSMIX_CHAN_MASTER, ICSMIX_RIGHT, mute);
}

void
gusics_mic_mute(struct ics2101_softc *ic, int mute)
{
	ics2101_mix_mute(ic, GUSMIX_CHAN_MIC, ICSMIX_LEFT, mute);
	ics2101_mix_mute(ic, GUSMIX_CHAN_MIC, ICSMIX_RIGHT, mute);
}

void
gusics_linein_mute(struct ics2101_softc *ic, int mute)
{
	ics2101_mix_mute(ic, GUSMIX_CHAN_LINE, ICSMIX_LEFT, mute);
	ics2101_mix_mute(ic, GUSMIX_CHAN_LINE, ICSMIX_RIGHT, mute);
}

void
gusics_cd_mute(struct ics2101_softc *ic, int mute)
{
	ics2101_mix_mute(ic, GUSMIX_CHAN_CD, ICSMIX_LEFT, mute);
	ics2101_mix_mute(ic, GUSMIX_CHAN_CD, ICSMIX_RIGHT, mute);
}

void
gusics_dac_mute(struct ics2101_softc *ic, int mute)
{
	ics2101_mix_mute(ic, GUSMIX_CHAN_DAC, ICSMIX_LEFT, mute);
	ics2101_mix_mute(ic, GUSMIX_CHAN_DAC, ICSMIX_RIGHT, mute);
}

int
gusmax_mixer_set_port(void *addr, mixer_ctrl_t *cp)
{
	struct ad1848_softc *ac = addr;
	struct gus_softc *sc = ac->parent;
	struct ad1848_volume vol;
	int error = ad1848_mixer_set_port(ac, gusmapping, nummap, cp);

	if (error != ENXIO)
	  return (error);

	DPRINTF(("gusmax_mixer_set_port: dev=%d type=%d\n", cp->dev, cp->type));

	switch (cp->dev) {
	case GUSMAX_SPEAKER_LVL:
		if (cp->type == AUDIO_MIXER_VALUE &&
		    cp->un.value.num_channels == 1) {
			if (ad1848_to_vol(cp, &vol)) {
				gus_speaker_ctl(sc, vol.left > AUDIO_MIN_GAIN ?
						SPKR_ON : SPKR_OFF);
				error = 0;
			}
		}
		break;

	case GUSMAX_SPEAKER_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			gus_speaker_ctl(sc, cp->un.ord ? SPKR_OFF : SPKR_ON);
			error = 0;
		}
		break;

	default:
		return ENXIO;
	    /*NOTREACHED*/
    }
    return error;
}

int
gus_mixer_set_port(void *addr, mixer_ctrl_t *cp)
{
	struct gus_softc *sc = addr;
	struct ics2101_softc *ic = &sc->sc_mixer;
	struct ad1848_volume vol;
	int error = EINVAL;

	DPRINTF(("gus_mixer_set_port: dev=%d type=%d\n", cp->dev, cp->type));

	if (!HAS_MIXER(sc) && cp->dev > GUSICS_MASTER_MUTE)
		return ENXIO;

	switch (cp->dev) {

	case GUSICS_MIC_IN_MUTE:	/* Microphone */
		if (cp->type == AUDIO_MIXER_ENUM) {
			DPRINTF(("mic mute %d\n", cp->un.ord));
			if (HAS_MIXER(sc)) {
				gusics_mic_mute(ic, cp->un.ord);
			}
			gus_mic_ctl(sc, cp->un.ord ? SPKR_OFF : SPKR_ON);
			error = 0;
		}
		break;

	case GUSICS_LINE_IN_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			DPRINTF(("linein mute %d\n", cp->un.ord));
			if (HAS_MIXER(sc)) {
				gusics_linein_mute(ic, cp->un.ord);
			}
			gus_linein_ctl(sc, cp->un.ord ? SPKR_OFF : SPKR_ON);
			error = 0;
		}
		break;

	case GUSICS_MASTER_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			DPRINTF(("master mute %d\n", cp->un.ord));
			if (HAS_MIXER(sc)) {
				gusics_master_mute(ic, cp->un.ord);
			}
			gus_speaker_ctl(sc, cp->un.ord ? SPKR_OFF : SPKR_ON);
			error = 0;
		}
		break;

	case GUSICS_DAC_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			gusics_dac_mute(ic, cp->un.ord);
			error = 0;
		}
		break;

	case GUSICS_CD_MUTE:
		if (cp->type == AUDIO_MIXER_ENUM) {
			gusics_cd_mute(ic, cp->un.ord);
			error = 0;
		}
		break;

	case GUSICS_MASTER_LVL:
		if (cp->type == AUDIO_MIXER_VALUE) {
			if (ad1848_to_vol(cp, &vol)) {
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_MASTER,
						      ICSMIX_LEFT,
						      vol.left);
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_MASTER,
						      ICSMIX_RIGHT,
						      vol.right);
				error = 0;
			}
		}
		break;

	case GUSICS_MIC_IN_LVL:	/* Microphone */
		if (cp->type == AUDIO_MIXER_VALUE) {
			if (ad1848_to_vol(cp, &vol)) {
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_MIC,
						      ICSMIX_LEFT,
						      vol.left);
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_MIC,
						      ICSMIX_RIGHT,
						      vol.right);
				error = 0;
			}
		}
		break;

	case GUSICS_LINE_IN_LVL:	/* line in */
		if (cp->type == AUDIO_MIXER_VALUE) {
			if (ad1848_to_vol(cp, &vol)) {
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_LINE,
						      ICSMIX_LEFT,
						      vol.left);
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_LINE,
						      ICSMIX_RIGHT,
						      vol.right);
				error = 0;
			}
		}
		break;


	case GUSICS_CD_LVL:
		if (cp->type == AUDIO_MIXER_VALUE) {
			if (ad1848_to_vol(cp, &vol)) {
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_CD,
						      ICSMIX_LEFT,
						      vol.left);
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_CD,
						      ICSMIX_RIGHT,
						      vol.right);
				error = 0;
			}
		}
		break;

	case GUSICS_DAC_LVL:		/* dac out */
		if (cp->type == AUDIO_MIXER_VALUE) {
			if (ad1848_to_vol(cp, &vol)) {
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_DAC,
						      ICSMIX_LEFT,
						      vol.left);
				ics2101_mix_attenuate(ic,
						      GUSMIX_CHAN_DAC,
						      ICSMIX_RIGHT,
						      vol.right);
				error = 0;
			}
		}
		break;


	case GUSICS_RECORD_SOURCE:
		if (cp->type == AUDIO_MIXER_ENUM && cp->un.ord == 0) {
			/* Can't set anything else useful, sigh. */
			error = 0;
		}
		break;

	default:
		return ENXIO;
	    /*NOTREACHED*/
	}
	return error;
}

int
gusmax_mixer_query_devinfo(void *addr, mixer_devinfo_t *dip)
{
	DPRINTF(("gusmax_query_devinfo: index=%d\n", dip->index));

	switch(dip->index) {
#if 0
    case GUSMAX_MIC_IN_LVL:	/* Microphone */
	dip->type = AUDIO_MIXER_VALUE;
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->prev = AUDIO_MIXER_LAST;
	dip->next = GUSMAX_MIC_IN_MUTE;
	strlcpy(dip->label.name, AudioNmicrophone, sizeof dip->label.name);
	dip->un.v.num_channels = 2;
	strlcpy(dip->un.v.units.name, AudioNvolume,
	    sizeof dip->un.v.units.name);
	break;
#endif

    case GUSMAX_MONO_LVL:	/* mono/microphone mixer */
	dip->type = AUDIO_MIXER_VALUE;
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->prev = AUDIO_MIXER_LAST;
	dip->next = GUSMAX_MONO_MUTE;
	strlcpy(dip->label.name, AudioNmicrophone, sizeof dip->label.name);
	dip->un.v.num_channels = 1;
	strlcpy(dip->un.v.units.name, AudioNvolume,
	    sizeof dip->un.v.units.name);
	break;

    case GUSMAX_DAC_LVL:		/*  dacout */
	dip->type = AUDIO_MIXER_VALUE;
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->prev = AUDIO_MIXER_LAST;
	dip->next = GUSMAX_DAC_MUTE;
	strlcpy(dip->label.name, AudioNdac, sizeof dip->label.name);
	dip->un.v.num_channels = 2;
	strlcpy(dip->un.v.units.name, AudioNvolume,
	    sizeof dip->un.v.units.name);
	break;

    case GUSMAX_LINE_IN_LVL:	/* line */
	dip->type = AUDIO_MIXER_VALUE;
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->prev = AUDIO_MIXER_LAST;
	dip->next = GUSMAX_LINE_IN_MUTE;
	strlcpy(dip->label.name, AudioNline, sizeof dip->label.name);
	dip->un.v.num_channels = 2;
	strlcpy(dip->un.v.units.name, AudioNvolume,
	    sizeof dip->un.v.units.name);
	break;

    case GUSMAX_CD_LVL:		/* cd */
	dip->type = AUDIO_MIXER_VALUE;
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->prev = AUDIO_MIXER_LAST;
	dip->next = GUSMAX_CD_MUTE;
	strlcpy(dip->label.name, AudioNcd, sizeof dip->label.name);
	dip->un.v.num_channels = 2;
	strlcpy(dip->un.v.units.name, AudioNvolume,
	    sizeof dip->un.v.units.name);
	break;


    case GUSMAX_MONITOR_LVL:	/* monitor level */
	dip->type = AUDIO_MIXER_VALUE;
	dip->mixer_class = GUSMAX_MONITOR_CLASS;
	dip->next = GUSMAX_MONITOR_MUTE;
	dip->prev = AUDIO_MIXER_LAST;
	strlcpy(dip->label.name, AudioNmonitor, sizeof dip->label.name);
	dip->un.v.num_channels = 1;
	strlcpy(dip->un.v.units.name, AudioNvolume,
	    sizeof dip->un.v.units.name);
	break;

    case GUSMAX_OUT_LVL:		/* cs4231 output volume: not useful? */
	dip->type = AUDIO_MIXER_VALUE;
	dip->mixer_class = GUSMAX_MONITOR_CLASS;
	dip->prev = dip->next = AUDIO_MIXER_LAST;
	strlcpy(dip->label.name, AudioNoutput, sizeof dip->label.name);
	dip->un.v.num_channels = 2;
	strlcpy(dip->un.v.units.name, AudioNvolume,
	    sizeof dip->un.v.units.name);
	break;

    case GUSMAX_SPEAKER_LVL:		/* fake speaker volume */
	dip->type = AUDIO_MIXER_VALUE;
	dip->mixer_class = GUSMAX_MONITOR_CLASS;
	dip->prev = AUDIO_MIXER_LAST;
	dip->next = GUSMAX_SPEAKER_MUTE;
	strlcpy(dip->label.name, AudioNmaster, sizeof dip->label.name);
	dip->un.v.num_channels = 2;
	strlcpy(dip->un.v.units.name, AudioNvolume,
	    sizeof dip->un.v.units.name);
	break;

    case GUSMAX_LINE_IN_MUTE:
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->type = AUDIO_MIXER_ENUM;
	dip->prev = GUSMAX_LINE_IN_LVL;
	dip->next = AUDIO_MIXER_LAST;
	goto mute;

    case GUSMAX_DAC_MUTE:
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->type = AUDIO_MIXER_ENUM;
	dip->prev = GUSMAX_DAC_LVL;
	dip->next = AUDIO_MIXER_LAST;
	goto mute;

    case GUSMAX_CD_MUTE:
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->type = AUDIO_MIXER_ENUM;
	dip->prev = GUSMAX_CD_LVL;
	dip->next = AUDIO_MIXER_LAST;
	goto mute;

    case GUSMAX_MONO_MUTE:
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->type = AUDIO_MIXER_ENUM;
	dip->prev = GUSMAX_MONO_LVL;
	dip->next = AUDIO_MIXER_LAST;
	goto mute;

    case GUSMAX_MONITOR_MUTE:
	dip->mixer_class = GUSMAX_OUTPUT_CLASS;
	dip->type = AUDIO_MIXER_ENUM;
	dip->prev = GUSMAX_MONITOR_LVL;
	dip->next = AUDIO_MIXER_LAST;
	goto mute;

    case GUSMAX_SPEAKER_MUTE:
	dip->mixer_class = GUSMAX_OUTPUT_CLASS;
	dip->type = AUDIO_MIXER_ENUM;
	dip->prev = GUSMAX_SPEAKER_LVL;
	dip->next = AUDIO_MIXER_LAST;
    mute:
	strlcpy(dip->label.name, AudioNmute, sizeof dip->label.name);
	dip->un.e.num_mem = 2;
	strlcpy(dip->un.e.member[0].label.name, AudioNoff,
	    sizeof dip->un.e.member[0].label.name);
	dip->un.e.member[0].ord = 0;
	strlcpy(dip->un.e.member[1].label.name, AudioNon,
	    sizeof dip->un.e.member[1].label.name);
	dip->un.e.member[1].ord = 1;
	break;

    case GUSMAX_REC_LVL:	/* record level */
	dip->type = AUDIO_MIXER_VALUE;
	dip->mixer_class = GUSMAX_RECORD_CLASS;
	dip->prev = AUDIO_MIXER_LAST;
	dip->next = GUSMAX_RECORD_SOURCE;
	strlcpy(dip->label.name, AudioNrecord, sizeof dip->label.name);
	dip->un.v.num_channels = 2;
	strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
	break;

    case GUSMAX_RECORD_SOURCE:
	dip->mixer_class = GUSMAX_RECORD_CLASS;
	dip->type = AUDIO_MIXER_ENUM;
	dip->prev = GUSMAX_REC_LVL;
	dip->next = AUDIO_MIXER_LAST;
	strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
	dip->un.e.num_mem = 4;
	strlcpy(dip->un.e.member[0].label.name, AudioNoutput,
	    sizeof dip->un.e.member[0].label.name);
	dip->un.e.member[0].ord = DAC_IN_PORT;
	strlcpy(dip->un.e.member[1].label.name, AudioNmicrophone,
	    sizeof dip->un.e.member[1].label.name);
	dip->un.e.member[1].ord = MIC_IN_PORT;
	strlcpy(dip->un.e.member[2].label.name, AudioNdac,
	    sizeof dip->un.e.member[2].label.name);
	dip->un.e.member[2].ord = AUX1_IN_PORT;
	strlcpy(dip->un.e.member[3].label.name, AudioNline,
	    sizeof dip->un.e.member[3].label.name);
	dip->un.e.member[3].ord = LINE_IN_PORT;
	break;

    case GUSMAX_INPUT_CLASS:			/* input class descriptor */
	dip->type = AUDIO_MIXER_CLASS;
	dip->mixer_class = GUSMAX_INPUT_CLASS;
	dip->next = dip->prev = AUDIO_MIXER_LAST;
	strlcpy(dip->label.name, AudioCinputs, sizeof dip->label.name);
	break;

    case GUSMAX_OUTPUT_CLASS:			/* output class descriptor */
	dip->type = AUDIO_MIXER_CLASS;
	dip->mixer_class = GUSMAX_OUTPUT_CLASS;
	dip->next = dip->prev = AUDIO_MIXER_LAST;
	strlcpy(dip->label.name, AudioCoutputs, sizeof dip->label.name);
	break;

    case GUSMAX_MONITOR_CLASS:			/* monitor class descriptor */
	dip->type = AUDIO_MIXER_CLASS;
	dip->mixer_class = GUSMAX_MONITOR_CLASS;
	dip->next = dip->prev = AUDIO_MIXER_LAST;
	strlcpy(dip->label.name, AudioCmonitor, sizeof dip->label.name);
	break;

    case GUSMAX_RECORD_CLASS:			/* record source class */
	dip->type = AUDIO_MIXER_CLASS;
	dip->mixer_class = GUSMAX_RECORD_CLASS;
	dip->next = dip->prev = AUDIO_MIXER_LAST;
	strlcpy(dip->label.name, AudioCrecord, sizeof dip->label.name);
	break;

    default:
	return ENXIO;
	/*NOTREACHED*/
    }
    DPRINTF(("AUDIO_MIXER_DEVINFO: name=%s\n", dip->label.name));
	return 0;
}

int
gus_mixer_query_devinfo(void *addr, mixer_devinfo_t *dip)
{
	struct gus_softc *sc = addr;

	DPRINTF(("gusmax_query_devinfo: index=%d\n", dip->index));

	if (!HAS_MIXER(sc) && dip->index > GUSICS_MASTER_MUTE)
		return ENXIO;

	switch(dip->index) {

	case GUSICS_MIC_IN_LVL:	/* Microphone */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = GUSICS_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = GUSICS_MIC_IN_MUTE;
		strlcpy(dip->label.name, AudioNmicrophone,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;

	case GUSICS_LINE_IN_LVL:	/* line */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = GUSICS_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = GUSICS_LINE_IN_MUTE;
		strlcpy(dip->label.name, AudioNline, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;

	case GUSICS_CD_LVL:		/* cd */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = GUSICS_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = GUSICS_CD_MUTE;
		strlcpy(dip->label.name, AudioNcd, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;

	case GUSICS_DAC_LVL:		/*  dacout */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = GUSICS_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = GUSICS_DAC_MUTE;
		strlcpy(dip->label.name, AudioNdac, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;

	case GUSICS_MASTER_LVL:		/*  master output */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = GUSICS_OUTPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = GUSICS_MASTER_MUTE;
		strlcpy(dip->label.name, AudioNmaster, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;


	case GUSICS_LINE_IN_MUTE:
		dip->mixer_class = GUSICS_INPUT_CLASS;
		dip->type = AUDIO_MIXER_ENUM;
		dip->prev = GUSICS_LINE_IN_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;

	case GUSICS_DAC_MUTE:
		dip->mixer_class = GUSICS_INPUT_CLASS;
		dip->type = AUDIO_MIXER_ENUM;
		dip->prev = GUSICS_DAC_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;

	case GUSICS_CD_MUTE:
		dip->mixer_class = GUSICS_INPUT_CLASS;
		dip->type = AUDIO_MIXER_ENUM;
		dip->prev = GUSICS_CD_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;

	case GUSICS_MIC_IN_MUTE:
		dip->mixer_class = GUSICS_INPUT_CLASS;
		dip->type = AUDIO_MIXER_ENUM;
		dip->prev = GUSICS_MIC_IN_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;

	case GUSICS_MASTER_MUTE:
		dip->mixer_class = GUSICS_OUTPUT_CLASS;
		dip->type = AUDIO_MIXER_ENUM;
		dip->prev = GUSICS_MASTER_LVL;
		dip->next = AUDIO_MIXER_LAST;
mute:
		strlcpy(dip->label.name, AudioNmute, sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;

	case GUSICS_RECORD_SOURCE:
		dip->mixer_class = GUSICS_RECORD_CLASS;
		dip->type = AUDIO_MIXER_ENUM;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
		dip->un.e.num_mem = 1;
		strlcpy(dip->un.e.member[0].label.name, AudioNoutput,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = GUSICS_MASTER_LVL;
		break;

	case GUSICS_INPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = GUSICS_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCinputs, sizeof dip->label.name);
		break;

	case GUSICS_OUTPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = GUSICS_OUTPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCoutputs, sizeof dip->label.name);
		break;

	case GUSICS_RECORD_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = GUSICS_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCrecord, sizeof dip->label.name);
		break;

	default:
		return ENXIO;
	/*NOTREACHED*/
	}
	DPRINTF(("AUDIO_MIXER_DEVINFO: name=%s\n", dip->label.name));
	return 0;
}

void *
gus_malloc(void *addr, int direction, size_t size, int pool, int flags)
{
	struct gus_softc *sc = addr;
	int drq;

	if (direction == AUMODE_PLAY)
		drq = sc->sc_drq;
	else
		drq = sc->sc_recdrq;

	return isa_malloc(sc->sc_isa, drq, size, pool, flags);
}

void
gus_free(void *addr, void *ptr, int pool)
{
	isa_free(ptr, pool);
}

size_t
gus_round(void *addr, int direction, size_t size)
{
	if (size > MAX_ISADMA)
		size = MAX_ISADMA;
	return size;
}

/*
 * Setup the ICS mixer in "transparent" mode: reset everything to a sensible
 * level.  Levels as suggested by GUS SDK code.
 */

void
gus_init_ics2101(struct gus_softc *sc)
{
	struct ics2101_softc *ic = &sc->sc_mixer;
	sc->sc_mixer.sc_iot = sc->sc_iot;
	sc->sc_mixer.sc_selio = GUS_MIXER_SELECT;
	sc->sc_mixer.sc_selio_ioh = sc->sc_ioh3;
	sc->sc_mixer.sc_dataio = GUS_MIXER_DATA;
	sc->sc_mixer.sc_dataio_ioh = sc->sc_ioh2;
	sc->sc_mixer.sc_flags = (sc->sc_revision == 5) ? ICS_FLIP : 0;

	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_MIC,
			      ICSMIX_LEFT,
			      ICSMIX_MIN_ATTN);
	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_MIC,
			      ICSMIX_RIGHT,
			      ICSMIX_MIN_ATTN);
	/*
	 * Start with microphone muted by the mixer...
	 */
	gusics_mic_mute(ic, 1);

	/* ... and enabled by the GUS master mix control */
	gus_mic_ctl(sc, SPKR_ON);

	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_LINE,
			      ICSMIX_LEFT,
			      ICSMIX_MIN_ATTN);
	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_LINE,
			      ICSMIX_RIGHT,
			      ICSMIX_MIN_ATTN);

	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_CD,
			      ICSMIX_LEFT,
			      ICSMIX_MIN_ATTN);
	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_CD,
			      ICSMIX_RIGHT,
			      ICSMIX_MIN_ATTN);

	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_DAC,
			      ICSMIX_LEFT,
			      ICSMIX_MIN_ATTN);
	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_DAC,
			      ICSMIX_RIGHT,
			      ICSMIX_MIN_ATTN);

	ics2101_mix_attenuate(ic,
			      ICSMIX_CHAN_4,
			      ICSMIX_LEFT,
			      ICSMIX_MAX_ATTN);
	ics2101_mix_attenuate(ic,
			      ICSMIX_CHAN_4,
			      ICSMIX_RIGHT,
			      ICSMIX_MAX_ATTN);

	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_MASTER,
			      ICSMIX_LEFT,
			      ICSMIX_MIN_ATTN);
	ics2101_mix_attenuate(ic,
			      GUSMIX_CHAN_MASTER,
			      ICSMIX_RIGHT,
			      ICSMIX_MIN_ATTN);
	/* unmute other stuff: */
	gusics_cd_mute(ic, 0);
	gusics_dac_mute(ic, 0);
	gusics_linein_mute(ic, 0);
	return;
}



void
gus_subattach(struct gus_softc *sc, struct isa_attach_args *ia)
{
	int		i;
	bus_space_tag_t iot;
	unsigned char	c,d,m;
	u_long		s;

	iot = sc->sc_iot;

	/*
	 * Figure out our board rev, and see if we need to initialize the
	 * mixer
	 */

	c = bus_space_read_1(iot, sc->sc_ioh3, GUS_BOARD_REV);
	if (c != 0xff)
		sc->sc_revision = c;
	else
		sc->sc_revision = 0;

	SELECT_GUS_REG(iot, sc->sc_ioh2, GUSREG_RESET);
	bus_space_write_1(iot, sc->sc_ioh2, GUS_DATA_HIGH, 0x00);

	gusreset(sc, GUS_MAX_VOICES); /* initialize all voices */
	gusreset(sc, GUS_MIN_VOICES); /* then set to just the ones we use */

	/*
	 * Setup the IRQ and DRQ lines in software, using values from
	 * config file
	 */

	m = GUSMASK_LINE_IN|GUSMASK_LINE_OUT;		/* disable all */

	c = ((unsigned char) gus_irq_map[ia->ia_irq]) | GUSMASK_BOTH_RQ;

	if (sc->sc_recdrq == sc->sc_drq)
		d = (unsigned char) (gus_drq_map[sc->sc_drq] |
				GUSMASK_BOTH_RQ);
	else
		d = (unsigned char) (gus_drq_map[sc->sc_drq] |
				gus_drq_map[sc->sc_recdrq] << 3);

	/*
	 * Program the IRQ and DMA channels on the GUS.  Note that we hardwire
	 * the GUS to only use one IRQ channel, but we give the user the
	 * option of using two DMA channels (the other one given by the drq2
	 * option in the config file).  Two DMA channels are needed for full-
	 * duplex operation.
	 *
	 * The order of these operations is very magical.
	 */

	s = intr_disable();		/* XXX needed? */

	bus_space_write_1(iot, sc->sc_ioh1, GUS_REG_CONTROL, GUS_REG_IRQCTL);
	bus_space_write_1(iot, sc->sc_ioh1, GUS_MIX_CONTROL, m);
	bus_space_write_1(iot, sc->sc_ioh1, GUS_IRQCTL_CONTROL, 0x00);
	bus_space_write_1(iot, sc->sc_ioh1, 0x0f, 0x00);

	bus_space_write_1(iot, sc->sc_ioh1, GUS_MIX_CONTROL, m);

	/* magic reset? */
	bus_space_write_1(iot, sc->sc_ioh1, GUS_DMA_CONTROL, d | 0x80);

	bus_space_write_1(iot, sc->sc_ioh1, GUS_MIX_CONTROL,
	    m | GUSMASK_CONTROL_SEL);
	bus_space_write_1(iot, sc->sc_ioh1, GUS_IRQ_CONTROL, c);

	bus_space_write_1(iot, sc->sc_ioh1, GUS_MIX_CONTROL, m);
	bus_space_write_1(iot, sc->sc_ioh1, GUS_DMA_CONTROL, d);

	bus_space_write_1(iot, sc->sc_ioh1, GUS_MIX_CONTROL,
	    m | GUSMASK_CONTROL_SEL);
	bus_space_write_1(iot, sc->sc_ioh1, GUS_IRQ_CONTROL, c);

	bus_space_write_1(iot, sc->sc_ioh2, GUS_VOICE_SELECT, 0x00);

	/* enable line in, line out.  leave mic disabled. */
	bus_space_write_1(iot, sc->sc_ioh1, GUS_MIX_CONTROL,
	     (m | GUSMASK_LATCHES) & ~(GUSMASK_LINE_OUT|GUSMASK_LINE_IN));
	bus_space_write_1(iot, sc->sc_ioh2, GUS_VOICE_SELECT, 0x00);

	intr_restore(s);

	sc->sc_mixcontrol =
		(m | GUSMASK_LATCHES) & ~(GUSMASK_LINE_OUT|GUSMASK_LINE_IN);

	sc->sc_codec.sc_isa = sc->sc_isa;

	if (sc->sc_revision >= 5 && sc->sc_revision <= 9) {
		sc->sc_flags |= GUS_MIXER_INSTALLED;
		gus_init_ics2101(sc);
	}
	if (sc->sc_revision < 10 || !gus_init_cs4231(sc)) {
		/* Not using the CS4231, so create our DMA maps. */
		if (sc->sc_drq != -1) {
			if (isa_dmamap_create(sc->sc_isa, sc->sc_drq,
			    MAX_ISADMA, BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW)) {
				printf("%s: can't create map for drq %d\n",
				       sc->sc_dev.dv_xname, sc->sc_drq);
				return;
			}
		}
		if (sc->sc_recdrq != -1 && sc->sc_recdrq != sc->sc_drq) {
			if (isa_dmamap_create(sc->sc_isa, sc->sc_recdrq,
			    MAX_ISADMA, BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW)) {
				printf("%s: can't create map for drq %d\n",
				       sc->sc_dev.dv_xname, sc->sc_recdrq);
				return;
			}
		}
	}

	timeout_set(&sc->sc_dma_tmo, gus_dmaout_timeout, sc);

	SELECT_GUS_REG(iot, sc->sc_ioh2, GUSREG_RESET);
	/*
	 * Check to see how much memory we have on this card; see if any
	 * "mirroring" occurs.  We're assuming at least 256K already exists
	 * on the card; otherwise the initial probe would have failed
	 */

	guspoke(iot, sc->sc_ioh2, 0L, 0x00);
	for(i = 1; i < 1024; i++) {
		u_long loc;

		/*
		 * See if we've run into mirroring yet
		 */

		if (guspeek(iot, sc->sc_ioh2, 0L) != 0)
			break;

		loc = i << 10;

		guspoke(iot, sc->sc_ioh2, loc, 0xaa);
		if (guspeek(iot, sc->sc_ioh2, loc) != 0xaa)
			break;
	}

	sc->sc_dsize = i;
	/*
	 * The "official" (3.x) version number cannot easily be obtained.
	 * The revision register does not correspond to the minor number
	 * of the board version. Simply use the revision register as
	 * identification.
	 */
	printf(": ver %d", sc->sc_revision);
	if (sc->sc_revision >= 10)
		printf(", MAX");
	else {
		if (HAS_MIXER(sc))
			printf(", ICS2101 mixer");
		if (HAS_CODEC(sc))
			printf(", %s codec/mixer", sc->sc_codec.chip_name);
	}
	printf(", %dKB DRAM, ", sc->sc_dsize);
	if (sc->sc_recdrq == sc->sc_drq) {
		printf("half-duplex");
	} else {
		printf("full-duplex, record drq %d", sc->sc_recdrq);
	}

	printf("\n");

	/*
	 * Setup a default interrupt handler
	 */

	sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq,
	    IST_EDGE, IPL_AUDIO | IPL_MPSAFE,
	    gusintr, sc /* sc->sc_gusdsp */, sc->sc_dev.dv_xname);

	/*
	 * Set some default values
	 * XXX others start with 8kHz mono mulaw
	 */

	sc->sc_irate = sc->sc_orate = 44100;
	sc->sc_encoding = AUDIO_ENCODING_SLINEAR_LE;
	sc->sc_precision = 16;
	sc->sc_voc[GUS_VOICE_LEFT].voccntl |= GUSMASK_DATA_SIZE16;
	sc->sc_voc[GUS_VOICE_RIGHT].voccntl |= GUSMASK_DATA_SIZE16;
	sc->sc_channels = 1;
	sc->sc_ogain = 340;
	gus_commit_settings(sc);

	/*
	 * We always put the left channel full left & right channel
	 * full right.
	 * For mono playback, we set up both voices playing the same buffer.
	 */
	bus_space_write_1(iot, sc->sc_ioh2, GUS_VOICE_SELECT,
	    (u_char)GUS_VOICE_LEFT);
	SELECT_GUS_REG(iot, sc->sc_ioh2, GUSREG_PAN_POS);
	bus_space_write_1(iot, sc->sc_ioh2, GUS_DATA_HIGH, GUS_PAN_FULL_LEFT);

	bus_space_write_1(iot, sc->sc_ioh2, GUS_VOICE_SELECT,
	    (u_char)GUS_VOICE_RIGHT);
	SELECT_GUS_REG(iot, sc->sc_ioh2, GUSREG_PAN_POS);
	bus_space_write_1(iot, sc->sc_ioh2, GUS_DATA_HIGH, GUS_PAN_FULL_RIGHT);

	/*
	 * Attach to the generic audio layer
	 */

	if (HAS_CODEC(sc)) {
		audio_attach_mi(&gusmax_hw_if, (void *)&sc->sc_codec, NULL,
		    &sc->sc_dev);
	} else {
		audio_attach_mi(&gus_hw_if, (void *)sc, NULL, &sc->sc_dev);
	}
}

/*
 * Test to see if a particular I/O base is valid for the GUS.  Return true
 * if it is.
 */

int
gus_test_iobase(bus_space_tag_t iot, int iobase)
{
	bus_space_handle_t ioh1, ioh2, ioh3, ioh4;
	u_char s1, s2;
	int rv = 0;

	/* Map i/o space */
	if (bus_space_map(iot, iobase, GUS_NPORT1, 0, &ioh1))
		return 0;
	if (bus_space_map(iot, iobase+GUS_IOH2_OFFSET, GUS_NPORT2, 0, &ioh2))
		goto bad1;

	/* XXX Maybe we shouldn't fail on mapping this, but just assume
	 * the card is of revision 0? */
	if (bus_space_map(iot, iobase+GUS_IOH3_OFFSET, GUS_NPORT3, 0, &ioh3))
		goto bad2;

	if (bus_space_map(iot, iobase+GUS_IOH4_OFFSET, GUS_NPORT4, 0, &ioh4))
		goto bad3;

	/*
	 * Reset GUS to an initial state before we do anything.
	 */

	mtx_enter(&audio_lock);
	delay(500);

	SELECT_GUS_REG(iot, ioh2, GUSREG_RESET);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, 0x00);

	delay(500);

	SELECT_GUS_REG(iot, ioh2, GUSREG_RESET);
	bus_space_write_1(iot, ioh2, GUS_DATA_HIGH, GUSMASK_MASTER_RESET);

	delay(500);

	mtx_leave(&audio_lock);

	/*
	 * See if we can write to the board's memory
	 */

	s1 = guspeek(iot, ioh2, 0L);
	s2 = guspeek(iot, ioh2, 1L);

	guspoke(iot, ioh2, 0L, 0xaa);
	guspoke(iot, ioh2, 1L, 0x55);

	if (guspeek(iot, ioh2, 0L) != 0xaa)
		goto bad;

	guspoke(iot, ioh2, 0L, s1);
	guspoke(iot, ioh2, 1L, s2);

	rv = 1;

bad:
	bus_space_unmap(iot, ioh4, GUS_NPORT4);
bad3:
	bus_space_unmap(iot, ioh3, GUS_NPORT3);
bad2:
	bus_space_unmap(iot, ioh2, GUS_NPORT2);
bad1:
	bus_space_unmap(iot, ioh1, GUS_NPORT1);
	return rv;
}
