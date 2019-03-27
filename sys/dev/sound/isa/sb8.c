/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
 * Copyright (c) 1997,1998 Luigi Rizzo
 *
 * Derived from files in the Voxware 3.5 distribution,
 * Copyright by Hannu Savolainen 1994, under the same copyright
 * conditions.
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

#include  <dev/sound/isa/sb.h>
#include  <dev/sound/chip.h>

#include <isa/isavar.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

#define SB_DEFAULT_BUFSZ	4096

static u_int32_t sb_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	0
};
static struct pcmchan_caps sb200_playcaps = {4000, 23000, sb_fmt, 0};
static struct pcmchan_caps sb200_reccaps = {4000, 13000, sb_fmt, 0};
static struct pcmchan_caps sb201_playcaps = {4000, 44100, sb_fmt, 0};
static struct pcmchan_caps sb201_reccaps = {4000, 15000, sb_fmt, 0};

static u_int32_t sbpro_fmt[] = {
	SND_FORMAT(AFMT_U8, 1, 0),
	SND_FORMAT(AFMT_U8, 2, 0),
	0
};
static struct pcmchan_caps sbpro_playcaps = {4000, 44100, sbpro_fmt, 0};
static struct pcmchan_caps sbpro_reccaps = {4000, 44100, sbpro_fmt, 0};

struct sb_info;

struct sb_chinfo {
	struct sb_info *parent;
	struct pcm_channel *channel;
	struct snd_dbuf *buffer;
	int dir;
	u_int32_t fmt, spd, blksz;
};

struct sb_info {
	device_t parent_dev;
    	struct resource *io_base;	/* I/O address for the board */
    	struct resource *irq;
   	struct resource *drq;
    	void *ih;
    	bus_dma_tag_t parent_dmat;

	unsigned int bufsize;
    	int bd_id;
    	u_long bd_flags;       /* board-specific flags */
    	struct sb_chinfo pch, rch;
};

static int sb_rd(struct sb_info *sb, int reg);
static void sb_wr(struct sb_info *sb, int reg, u_int8_t val);
static int sb_dspready(struct sb_info *sb);
static int sb_cmd(struct sb_info *sb, u_char val);
static int sb_cmd1(struct sb_info *sb, u_char cmd, int val);
static int sb_cmd2(struct sb_info *sb, u_char cmd, int val);
static u_int sb_get_byte(struct sb_info *sb);
static void sb_setmixer(struct sb_info *sb, u_int port, u_int value);
static int sb_getmixer(struct sb_info *sb, u_int port);
static int sb_reset_dsp(struct sb_info *sb);

static void sb_intr(void *arg);
static int sb_speed(struct sb_chinfo *ch);
static int sb_start(struct sb_chinfo *ch);
static int sb_stop(struct sb_chinfo *ch);

/*
 * Common code for the midi and pcm functions
 *
 * sb_cmd write a single byte to the CMD port.
 * sb_cmd1 write a CMD + 1 byte arg
 * sb_cmd2 write a CMD + 2 byte arg
 * sb_get_byte returns a single byte from the DSP data port
 */

static void
sb_lock(struct sb_info *sb) {

	sbc_lock(device_get_softc(sb->parent_dev));
}

static void
sb_unlock(struct sb_info *sb) {

	sbc_unlock(device_get_softc(sb->parent_dev));
}

static int
port_rd(struct resource *port, int off)
{
	return bus_space_read_1(rman_get_bustag(port), rman_get_bushandle(port), off);
}

static void
port_wr(struct resource *port, int off, u_int8_t data)
{
	bus_space_write_1(rman_get_bustag(port), rman_get_bushandle(port), off, data);
}

static int
sb_rd(struct sb_info *sb, int reg)
{
	return port_rd(sb->io_base, reg);
}

static void
sb_wr(struct sb_info *sb, int reg, u_int8_t val)
{
	port_wr(sb->io_base, reg, val);
}

static int
sb_dspready(struct sb_info *sb)
{
	return ((sb_rd(sb, SBDSP_STATUS) & 0x80) == 0);
}

static int
sb_dspwr(struct sb_info *sb, u_char val)
{
    	int  i;

    	for (i = 0; i < 1000; i++) {
		if (sb_dspready(sb)) {
	    		sb_wr(sb, SBDSP_CMD, val);
	    		return 1;
		}
		if (i > 10) DELAY((i > 100)? 1000 : 10);
    	}
    	printf("sb_dspwr(0x%02x) timed out.\n", val);
    	return 0;
}

static int
sb_cmd(struct sb_info *sb, u_char val)
{
#if 0
	printf("sb_cmd: %x\n", val);
#endif
    	return sb_dspwr(sb, val);
}

static int
sb_cmd1(struct sb_info *sb, u_char cmd, int val)
{
#if 0
    	printf("sb_cmd1: %x, %x\n", cmd, val);
#endif
    	if (sb_dspwr(sb, cmd)) {
		return sb_dspwr(sb, val & 0xff);
    	} else return 0;
}

static int
sb_cmd2(struct sb_info *sb, u_char cmd, int val)
{
#if 0
    	printf("sb_cmd2: %x, %x\n", cmd, val);
#endif
    	if (sb_dspwr(sb, cmd)) {
		return sb_dspwr(sb, val & 0xff) &&
		       sb_dspwr(sb, (val >> 8) & 0xff);
    	} else return 0;
}

/*
 * in the SB, there is a set of indirect "mixer" registers with
 * address at offset 4, data at offset 5
 *
 * we don't need to interlock these, the mixer lock will suffice.
 */
static void
sb_setmixer(struct sb_info *sb, u_int port, u_int value)
{
    	sb_wr(sb, SB_MIX_ADDR, (u_char) (port & 0xff)); /* Select register */
    	DELAY(10);
    	sb_wr(sb, SB_MIX_DATA, (u_char) (value & 0xff));
    	DELAY(10);
}

static int
sb_getmixer(struct sb_info *sb, u_int port)
{
    	int val;

    	sb_wr(sb, SB_MIX_ADDR, (u_char) (port & 0xff)); /* Select register */
    	DELAY(10);
    	val = sb_rd(sb, SB_MIX_DATA);
    	DELAY(10);

    	return val;
}

static u_int
sb_get_byte(struct sb_info *sb)
{
    	int i;

    	for (i = 1000; i > 0; i--) {
		if (sb_rd(sb, DSP_DATA_AVAIL) & 0x80)
			return sb_rd(sb, DSP_READ);
		else
			DELAY(20);
    	}
    	return 0xffff;
}

static int
sb_reset_dsp(struct sb_info *sb)
{
    	sb_wr(sb, SBDSP_RST, 3);
    	DELAY(100);
    	sb_wr(sb, SBDSP_RST, 0);
    	if (sb_get_byte(sb) != 0xAA) {
        	DEB(printf("sb_reset_dsp 0x%lx failed\n",
			   rman_get_start(sb->io_base)));
		return ENXIO;	/* Sorry */
    	}
    	return 0;
}

static void
sb_release_resources(struct sb_info *sb, device_t dev)
{
    	if (sb->irq) {
    		if (sb->ih)
			bus_teardown_intr(dev, sb->irq, sb->ih);
 		bus_release_resource(dev, SYS_RES_IRQ, 0, sb->irq);
		sb->irq = NULL;
    	}
    	if (sb->drq) {
		isa_dma_release(rman_get_start(sb->drq));
		bus_release_resource(dev, SYS_RES_DRQ, 0, sb->drq);
		sb->drq = NULL;
    	}
    	if (sb->io_base) {
		bus_release_resource(dev, SYS_RES_IOPORT, 0, sb->io_base);
		sb->io_base = NULL;
    	}
    	if (sb->parent_dmat) {
		bus_dma_tag_destroy(sb->parent_dmat);
		sb->parent_dmat = 0;
    	}
     	free(sb, M_DEVBUF);
}

static int
sb_alloc_resources(struct sb_info *sb, device_t dev)
{
	int rid;

	rid = 0;
	if (!sb->io_base)
    		sb->io_base = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
			&rid, RF_ACTIVE);
	rid = 0;
	if (!sb->irq)
    		sb->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
			&rid, RF_ACTIVE);
	rid = 0;
	if (!sb->drq)
    		sb->drq = bus_alloc_resource_any(dev, SYS_RES_DRQ,
			&rid, RF_ACTIVE);

	if (sb->io_base && sb->drq && sb->irq) {
		isa_dma_acquire(rman_get_start(sb->drq));
		isa_dmainit(rman_get_start(sb->drq), sb->bufsize);

		return 0;
	} else return ENXIO;
}

/************************************************************/

static int
sbpromix_init(struct snd_mixer *m)
{
    	struct sb_info *sb = mix_getdevinfo(m);

	mix_setdevs(m, SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_LINE |
		       SOUND_MASK_MIC | SOUND_MASK_CD | SOUND_MASK_VOLUME);

	mix_setrecdevs(m, SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD);

	sb_setmixer(sb, 0, 1); /* reset mixer */

    	return 0;
}

static int
sbpromix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
    	struct sb_info *sb = mix_getdevinfo(m);
    	int reg, max;
    	u_char val;

	max = 7;
	switch (dev) {
	case SOUND_MIXER_PCM:
		reg = 0x04;
		break;

	case SOUND_MIXER_MIC:
		reg = 0x0a;
		max = 3;
		break;

	case SOUND_MIXER_VOLUME:
		reg = 0x22;
		break;

	case SOUND_MIXER_SYNTH:
		reg = 0x26;
		break;

	case SOUND_MIXER_CD:
		reg = 0x28;
		break;

	case SOUND_MIXER_LINE:
		reg = 0x2e;
		break;

	default:
		return -1;
	}

	left = (left * max) / 100;
	right = (dev == SOUND_MIXER_MIC)? left : ((right * max) / 100);

	val = (dev == SOUND_MIXER_MIC)? (left << 1) : (left << 5 | right << 1);
	sb_setmixer(sb, reg, val);

	left = (left * 100) / max;
	right = (right * 100) / max;

    	return left | (right << 8);
}

static u_int32_t
sbpromix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
    	struct sb_info *sb = mix_getdevinfo(m);
    	u_char recdev;

	if      (src == SOUND_MASK_LINE)
		recdev = 0x06;
	else if (src == SOUND_MASK_CD)
		recdev = 0x02;
	else { /* default: mic */
	    	src = SOUND_MASK_MIC;
	    	recdev = 0;
	}
	sb_setmixer(sb, RECORD_SRC, recdev | (sb_getmixer(sb, RECORD_SRC) & ~0x07));

	return src;
}

static kobj_method_t sbpromix_mixer_methods[] = {
    	KOBJMETHOD(mixer_init,		sbpromix_init),
    	KOBJMETHOD(mixer_set,		sbpromix_set),
    	KOBJMETHOD(mixer_setrecsrc,	sbpromix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(sbpromix_mixer);

/************************************************************/

static int
sbmix_init(struct snd_mixer *m)
{
    	struct sb_info *sb = mix_getdevinfo(m);

	mix_setdevs(m, SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_CD | SOUND_MASK_VOLUME);

	mix_setrecdevs(m, 0);

	sb_setmixer(sb, 0, 1); /* reset mixer */

    	return 0;
}

static int
sbmix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
    	struct sb_info *sb = mix_getdevinfo(m);
    	int reg, max;

	max = 7;
	switch (dev) {
	case SOUND_MIXER_VOLUME:
		reg = 0x2;
		break;

	case SOUND_MIXER_SYNTH:
		reg = 0x6;
		break;

	case SOUND_MIXER_CD:
		reg = 0x8;
		break;

	case SOUND_MIXER_PCM:
		reg = 0x0a;
		max = 3;
		break;

	default:
		return -1;
	}

	left = (left * max) / 100;

	sb_setmixer(sb, reg, left << 1);

	left = (left * 100) / max;

    	return left | (left << 8);
}

static u_int32_t
sbmix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	return 0;
}

static kobj_method_t sbmix_mixer_methods[] = {
    	KOBJMETHOD(mixer_init,		sbmix_init),
    	KOBJMETHOD(mixer_set,		sbmix_set),
    	KOBJMETHOD(mixer_setrecsrc,	sbmix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(sbmix_mixer);

/************************************************************/

static void
sb_intr(void *arg)
{
    	struct sb_info *sb = (struct sb_info *)arg;

	sb_lock(sb);
    	if (sndbuf_runsz(sb->pch.buffer) > 0) {
		sb_unlock(sb);
		chn_intr(sb->pch.channel);
		sb_lock(sb);
	}

    	if (sndbuf_runsz(sb->rch.buffer) > 0) {
		sb_unlock(sb);
		chn_intr(sb->rch.channel);
		sb_lock(sb);
	}

	sb_rd(sb, DSP_DATA_AVAIL); /* int ack */
	sb_unlock(sb);
}

static int
sb_speed(struct sb_chinfo *ch)
{
	struct sb_info *sb = ch->parent;
    	int play = (ch->dir == PCMDIR_PLAY)? 1 : 0;
	int stereo = (AFMT_CHANNEL(ch->fmt) > 1)? 1 : 0;
	int speed, tmp, thresh, max;
	u_char tconst;

	if (sb->bd_id >= 0x300) {
		thresh = stereo? 11025 : 23000;
		max = stereo? 22050 : 44100;
	} else if (sb->bd_id > 0x200) {
		thresh = play? 23000 : 13000;
		max = play? 44100 : 15000;
	} else {
		thresh = 999999;
		max = play? 23000 : 13000;
	}

	speed = ch->spd;
	if (speed > max)
		speed = max;

	sb_lock(sb);
	sb->bd_flags &= ~BD_F_HISPEED;
	if (speed > thresh)
		sb->bd_flags |= BD_F_HISPEED;

	tmp = 65536 - (256000000 / (speed << stereo));
	tconst = tmp >> 8;

	sb_cmd1(sb, 0x40, tconst); /* set time constant */

	speed = (256000000 / (65536 - tmp)) >> stereo;

	ch->spd = speed;
	sb_unlock(sb);
	return speed;
}

static int
sb_start(struct sb_chinfo *ch)
{
	struct sb_info *sb = ch->parent;
    	int play = (ch->dir == PCMDIR_PLAY)? 1 : 0;
    	int stereo = (AFMT_CHANNEL(ch->fmt) > 1)? 1 : 0;
	int l = ch->blksz;
	u_char i;

	l--;

	sb_lock(sb);
	if (play)
		sb_cmd(sb, DSP_CMD_SPKON);

	if (sb->bd_flags & BD_F_HISPEED)
		i = play? 0x90 : 0x98;
	else
		i = play? 0x1c : 0x2c;

	sb_setmixer(sb, 0x0e, stereo? 2 : 0);
	sb_cmd2(sb, 0x48, l);
       	sb_cmd(sb, i);

	sb->bd_flags |= BD_F_DMARUN;
	sb_unlock(sb);
	return 0;
}

static int
sb_stop(struct sb_chinfo *ch)
{
	struct sb_info *sb = ch->parent;
    	int play = (ch->dir == PCMDIR_PLAY)? 1 : 0;

	sb_lock(sb);
    	if (sb->bd_flags & BD_F_HISPEED)
		sb_reset_dsp(sb);
	else {
#if 0
		/*
		 * NOTE: DSP_CMD_DMAEXIT_8 does not work with old
		 * soundblaster.
		 */
		sb_cmd(sb, DSP_CMD_DMAEXIT_8);
#endif
		sb_reset_dsp(sb);
	}

	if (play)
		sb_cmd(sb, DSP_CMD_SPKOFF); /* speaker off */
	sb_unlock(sb);
	sb->bd_flags &= ~BD_F_DMARUN;
	return 0;
}

/* channel interface */
static void *
sbchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct sb_info *sb = devinfo;
	struct sb_chinfo *ch = (dir == PCMDIR_PLAY)? &sb->pch : &sb->rch;

	ch->parent = sb;
	ch->channel = c;
	ch->dir = dir;
	ch->buffer = b;
	if (sndbuf_alloc(ch->buffer, sb->parent_dmat, 0, sb->bufsize) != 0)
		return NULL;
	sndbuf_dmasetup(ch->buffer, sb->drq);
	return ch;
}

static int
sbchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sb_chinfo *ch = data;

	ch->fmt = format;
	return 0;
}

static u_int32_t
sbchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sb_chinfo *ch = data;

	ch->spd = speed;
	return sb_speed(ch);
}

static u_int32_t
sbchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sb_chinfo *ch = data;

	ch->blksz = blocksize;
	return ch->blksz;
}

static int
sbchan_trigger(kobj_t obj, void *data, int go)
{
	struct sb_chinfo *ch = data;

	if (!PCMTRIG_COMMON(go))
		return 0;

	sndbuf_dma(ch->buffer, go);
	if (go == PCMTRIG_START)
		sb_start(ch);
	else
		sb_stop(ch);
	return 0;
}

static u_int32_t
sbchan_getptr(kobj_t obj, void *data)
{
	struct sb_chinfo *ch = data;

	return sndbuf_dmaptr(ch->buffer);
}

static struct pcmchan_caps *
sbchan_getcaps(kobj_t obj, void *data)
{
	struct sb_chinfo *ch = data;
	int p = (ch->dir == PCMDIR_PLAY)? 1 : 0;

	if (ch->parent->bd_id == 0x200)
		return p? &sb200_playcaps : &sb200_reccaps;
	if (ch->parent->bd_id < 0x300)
		return p? &sb201_playcaps : &sb201_reccaps;
	return p? &sbpro_playcaps : &sbpro_reccaps;
}

static kobj_method_t sbchan_methods[] = {
    	KOBJMETHOD(channel_init,		sbchan_init),
    	KOBJMETHOD(channel_setformat,		sbchan_setformat),
    	KOBJMETHOD(channel_setspeed,		sbchan_setspeed),
    	KOBJMETHOD(channel_setblocksize,	sbchan_setblocksize),
    	KOBJMETHOD(channel_trigger,		sbchan_trigger),
    	KOBJMETHOD(channel_getptr,		sbchan_getptr),
    	KOBJMETHOD(channel_getcaps,		sbchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(sbchan);

/************************************************************/

static int
sb_probe(device_t dev)
{
    	char buf[64];
	uintptr_t func, ver, r, f;

	/* The parent device has already been probed. */
	r = BUS_READ_IVAR(device_get_parent(dev), dev, 0, &func);
	if (func != SCF_PCM)
		return (ENXIO);

	r = BUS_READ_IVAR(device_get_parent(dev), dev, 1, &ver);
	f = (ver & 0xffff0000) >> 16;
	ver &= 0x0000ffff;
	if ((f & BD_F_ESS) || (ver >= 0x400))
		return (ENXIO);

	snprintf(buf, sizeof buf, "SB DSP %d.%02d", (int) ver >> 8, (int) ver & 0xff);

    	device_set_desc_copy(dev, buf);

	return 0;
}

static int
sb_attach(device_t dev)
{
    	struct sb_info *sb;
    	char status[SND_STATUSLEN];
	uintptr_t ver;

    	sb = malloc(sizeof(*sb), M_DEVBUF, M_WAITOK | M_ZERO);
	sb->parent_dev = device_get_parent(dev);
	BUS_READ_IVAR(device_get_parent(dev), dev, 1, &ver);
	sb->bd_id = ver & 0x0000ffff;
	sb->bd_flags = (ver & 0xffff0000) >> 16;
	sb->bufsize = pcm_getbuffersize(dev, 4096, SB_DEFAULT_BUFSZ, 65536);

    	if (sb_alloc_resources(sb, dev))
		goto no;
    	if (sb_reset_dsp(sb))
		goto no;
    	if (mixer_init(dev, (sb->bd_id < 0x300)? &sbmix_mixer_class : &sbpromix_mixer_class, sb))
		goto no;
	if (snd_setup_intr(dev, sb->irq, 0, sb_intr, sb, &sb->ih))
		goto no;

	pcm_setflags(dev, pcm_getflags(dev) | SD_F_SIMPLEX);

    	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(dev), /*alignment*/2,
			/*boundary*/0,
			/*lowaddr*/BUS_SPACE_MAXADDR_24BIT,
			/*highaddr*/BUS_SPACE_MAXADDR,
			/*filter*/NULL, /*filterarg*/NULL,
			/*maxsize*/sb->bufsize, /*nsegments*/1,
			/*maxsegz*/0x3ffff, /*flags*/0,
			/*lockfunc*/busdma_lock_mutex, /*lockarg*/&Giant,
			&sb->parent_dmat) != 0) {
		device_printf(dev, "unable to create dma tag\n");
		goto no;
    	}

    	snprintf(status, SND_STATUSLEN, "at io 0x%jx irq %jd drq %jd bufsz %u %s",
    	     	rman_get_start(sb->io_base), rman_get_start(sb->irq),
		rman_get_start(sb->drq), sb->bufsize, PCM_KLDSTRING(snd_sb8));

    	if (pcm_register(dev, sb, 1, 1))
		goto no;
	pcm_addchan(dev, PCMDIR_REC, &sbchan_class, sb);
	pcm_addchan(dev, PCMDIR_PLAY, &sbchan_class, sb);

    	pcm_setstatus(dev, status);

    	return 0;

no:
    	sb_release_resources(sb, dev);
    	return ENXIO;
}

static int
sb_detach(device_t dev)
{
	int r;
	struct sb_info *sb;

	r = pcm_unregister(dev);
	if (r)
		return r;

	sb = pcm_getdevinfo(dev);
    	sb_release_resources(sb, dev);
	return 0;
}

static device_method_t sb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sb_probe),
	DEVMETHOD(device_attach,	sb_attach),
	DEVMETHOD(device_detach,	sb_detach),

	{ 0, 0 }
};

static driver_t sb_driver = {
	"pcm",
	sb_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_sb8, sbc, sb_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_sb8, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_DEPEND(snd_sb8, snd_sbc, 1, 1, 1);
MODULE_VERSION(snd_sb8, 1);




