/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <cg@freebsd.org>
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
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/pcm/ac97_patch.h>

#include <dev/pci/pcivar.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

static MALLOC_DEFINE(M_AC97, "ac97", "ac97 codec");

struct ac97mixtable_entry {
	int reg;		/* register index		*/
				/* reg < 0 if inverted polarity	*/
	unsigned bits:4;	/* width of control field	*/
	unsigned ofs:4;		/* offset (only if stereo=0)	*/
	unsigned stereo:1;	/* set for stereo controls	*/
	unsigned mute:1;	/* bit15 is MUTE		*/
	unsigned recidx:4;	/* index in rec mux		*/
	unsigned mask:1;	/* use only masked bits		*/
	unsigned enable:1;	/* entry is enabled		*/
};

#define AC97_MIXER_SIZE		SOUND_MIXER_NRDEVICES

struct ac97_info {
	kobj_t methods;
	device_t dev;
	void *devinfo;
	u_int32_t id;
	u_int32_t subvendor;
	unsigned count, caps, se, extcaps, extid, extstat, noext:1;
	u_int32_t flags;
	struct ac97mixtable_entry mix[AC97_MIXER_SIZE];
	char name[16];
	struct mtx *lock;
};

struct ac97_vendorid {
	u_int32_t   id;
	const char *name;
};

struct ac97_codecid {
	u_int32_t  id;
	u_int8_t   stepmask;
	u_int8_t   noext:1;
	char 	  *name;
	ac97_patch patch;
};

static const struct ac97mixtable_entry ac97mixtable_default[AC97_MIXER_SIZE] = {
    /*	[offset]			reg	     bits of st mu re mk en */
	[SOUND_MIXER_VOLUME]	= { AC97_MIX_MASTER, 	5, 0, 1, 1, 6, 0, 1 },
	[SOUND_MIXER_OGAIN]	= { AC97_MIX_AUXOUT, 	5, 0, 1, 1, 0, 0, 0 },
	[SOUND_MIXER_PHONEOUT]	= { AC97_MIX_MONO, 	5, 0, 0, 1, 7, 0, 0 },
	[SOUND_MIXER_BASS]	= { AC97_MIX_TONE, 	4, 8, 0, 0, 0, 1, 0 },
	[SOUND_MIXER_TREBLE]	= { AC97_MIX_TONE, 	4, 0, 0, 0, 0, 1, 0 },
	[SOUND_MIXER_PCM]	= { AC97_MIX_PCM, 	5, 0, 1, 1, 0, 0, 1 },
	[SOUND_MIXER_SPEAKER]	= { AC97_MIX_BEEP, 	4, 1, 0, 1, 0, 0, 0 },
	[SOUND_MIXER_LINE]	= { AC97_MIX_LINE, 	5, 0, 1, 1, 5, 0, 1 },
	[SOUND_MIXER_PHONEIN]	= { AC97_MIX_PHONE, 	5, 0, 0, 1, 8, 0, 0 },
	[SOUND_MIXER_MIC] 	= { AC97_MIX_MIC, 	5, 0, 0, 1, 1, 1, 1 },
	/* use igain for the mic 20dB boost */
	[SOUND_MIXER_IGAIN] 	= { -AC97_MIX_MIC, 	1, 6, 0, 0, 0, 1, 1 },
	[SOUND_MIXER_CD]	= { AC97_MIX_CD, 	5, 0, 1, 1, 2, 0, 1 },
	[SOUND_MIXER_LINE1]	= { AC97_MIX_AUX, 	5, 0, 1, 1, 4, 0, 0 },
	[SOUND_MIXER_VIDEO]	= { AC97_MIX_VIDEO, 	5, 0, 1, 1, 3, 0, 0 },
	[SOUND_MIXER_RECLEV]	= { -AC97_MIX_RGAIN, 	4, 0, 1, 1, 0, 0, 1 }
};

static const struct ac97_vendorid ac97vendorid[] = {
	{ 0x41445300, "Analog Devices" },
	{ 0x414b4d00, "Asahi Kasei" },
	{ 0x414c4300, "Realtek" },
	{ 0x414c4700, "Avance Logic" },
	{ 0x43525900, "Cirrus Logic" },
	{ 0x434d4900, "C-Media Electronics" },
	{ 0x43585400, "Conexant" },
	{ 0x44543000, "Diamond Technology" },
	{ 0x454d4300, "eMicro" },
	{ 0x45838300, "ESS Technology" },
	{ 0x48525300, "Intersil" },
	{ 0x49434500, "ICEnsemble" },
	{ 0x49544500, "ITE, Inc." },
	{ 0x4e534300, "National Semiconductor" },
	{ 0x50534300, "Philips Semiconductor" },
	{ 0x83847600, "SigmaTel" },
	{ 0x53494c00, "Silicon Laboratories" },
	{ 0x54524100, "TriTech" },
	{ 0x54584e00, "Texas Instruments" },
	{ 0x56494100, "VIA Technologies" },
	{ 0x57454300, "Winbond" },
	{ 0x574d4c00, "Wolfson" },
	{ 0x594d4800, "Yamaha" },
	/* 
	 * XXX This is a fluke, really! The real vendor
	 * should be SigmaTel, not this! This should be
	 * removed someday!
	 */
	{ 0x01408300, "Creative" },
	{ 0x00000000, NULL }
};

static struct ac97_codecid ac97codecid[] = {
	{ 0x41445303, 0x00, 0, "AD1819",	0 },
	{ 0x41445340, 0x00, 0, "AD1881",	0 },
	{ 0x41445348, 0x00, 0, "AD1881A",	0 },
	{ 0x41445360, 0x00, 0, "AD1885",	0 },
	{ 0x41445361, 0x00, 0, "AD1886", 	ad1886_patch },
	{ 0x41445362, 0x00, 0, "AD1887", 	0 },
	{ 0x41445363, 0x00, 0, "AD1886A", 	0 },
	{ 0x41445368, 0x00, 0, "AD1888", 	ad198x_patch },
	{ 0x41445370, 0x00, 0, "AD1980",	ad198x_patch },
	{ 0x41445372, 0x00, 0, "AD1981A",	0 },
	{ 0x41445374, 0x00, 0, "AD1981B",	ad1981b_patch },
	{ 0x41445375, 0x00, 0, "AD1985",	ad198x_patch },
	{ 0x41445378, 0x00, 0, "AD1986",	ad198x_patch },
	{ 0x414b4d00, 0x00, 1, "AK4540", 	0 },
	{ 0x414b4d01, 0x00, 1, "AK4542", 	0 },
	{ 0x414b4d02, 0x00, 1, "AK4543", 	0 },
	{ 0x414b4d06, 0x00, 0, "AK4544A",	0 },
	{ 0x454b4d07, 0x00, 0, "AK4545",	0 },
	{ 0x414c4320, 0x0f, 0, "ALC100",	0 },
	{ 0x414c4730, 0x0f, 0, "ALC101",	0 },
	{ 0x414c4710, 0x0f, 0, "ALC200", 	0 },
	{ 0x414c4740, 0x0f, 0, "ALC202", 	0 },
	{ 0x414c4720, 0x0f, 0, "ALC650", 	0 },
	{ 0x414c4752, 0x0f, 0, "ALC250",	0 },
	{ 0x414c4760, 0x0f, 0, "ALC655",	alc655_patch },
	{ 0x414c4770, 0x0f, 0, "ALC203",	0 },
	{ 0x414c4780, 0x0f, 0, "ALC658",	0 },
	{ 0x414c4790, 0x0f, 0, "ALC850",	0 },
	{ 0x43525900, 0x07, 0, "CS4297", 	0 },
	{ 0x43525910, 0x07, 0, "CS4297A", 	0 },
	{ 0x43525920, 0x07, 0, "CS4294/98",	0 },
	{ 0x4352592d, 0x07, 0, "CS4294",	0 },
	{ 0x43525930, 0x07, 0, "CS4299",	0 },
	{ 0x43525940, 0x07, 0, "CS4201",	0 },
	{ 0x43525958, 0x07, 0, "CS4205",	0 },
	{ 0x43525960, 0x07, 0, "CS4291A",	0 },
	{ 0x434d4961, 0x00, 0, "CMI9739",	cmi9739_patch },
	{ 0x434d4941, 0x00, 0, "CMI9738",	0 },
	{ 0x434d4978, 0x00, 0, "CMI9761",	0 },
	{ 0x434d4982, 0x00, 0, "CMI9761",	0 },
	{ 0x434d4983, 0x00, 0, "CMI9761",	0 },
	{ 0x43585421, 0x00, 0, "HSD11246",	0 },
	{ 0x43585428, 0x07, 0, "CX20468",	0 },
	{ 0x43585430, 0x00, 0, "CX20468-21",	0 },
	{ 0x44543000, 0x00, 0, "DT0398",	0 },
	{ 0x454d4323, 0x00, 0, "EM28023",	0 },
	{ 0x454d4328, 0x00, 0, "EM28028",	0 },
	{ 0x45838308, 0x00, 0, "ES1988",	0 }, /* Formerly ES1921(?) */
	{ 0x48525300, 0x00, 0, "HMP9701",	0 },
	{ 0x49434501, 0x00, 0, "ICE1230",	0 },
	{ 0x49434511, 0x00, 0, "ICE1232",	0 },
	{ 0x49434514, 0x00, 0, "ICE1232A",	0 },
	{ 0x49434551, 0x03, 0, "VT1616",	0 }, /* Via badged ICE */
	{ 0x49544520, 0x00, 0, "ITE2226E",	0 },
	{ 0x49544560, 0x07, 0, "ITE2646E",	0 }, /* XXX: patch needed */
	{ 0x4e534340, 0x00, 0, "LM4540",	0 }, /* Spec blank on revid */
	{ 0x4e534343, 0x00, 0, "LM4543",	0 }, /* Ditto */
	{ 0x4e534346, 0x00, 0, "LM4546A",	0 },
	{ 0x4e534348, 0x00, 0, "LM4548A",	0 },
	{ 0x4e534331, 0x00, 0, "LM4549",	0 },
	{ 0x4e534349, 0x00, 0, "LM4549A",	0 },
	{ 0x4e534350, 0x00, 0, "LM4550",	0 },
	{ 0x50534301, 0x00, 0, "UCB1510",	0 },
	{ 0x50534304, 0x00, 0, "UCB1400",	0 },
	{ 0x83847600, 0x00, 0, "STAC9700/83/84",	0 },
	{ 0x83847604, 0x00, 0, "STAC9701/03/04/05", 0 },
	{ 0x83847605, 0x00, 0, "STAC9704",	0 },
	{ 0x83847608, 0x00, 0, "STAC9708/11",	0 },
	{ 0x83847609, 0x00, 0, "STAC9721/23",	0 },
	{ 0x83847644, 0x00, 0, "STAC9744/45",	0 },
	{ 0x83847650, 0x00, 0, "STAC9750/51",	0 },
	{ 0x83847652, 0x00, 0, "STAC9752/53",	0 },
	{ 0x83847656, 0x00, 0, "STAC9756/57",	0 },
	{ 0x83847658, 0x00, 0, "STAC9758/59",	0 },
	{ 0x83847660, 0x00, 0, "STAC9760/61",	0 }, /* Extrapolated */
	{ 0x83847662, 0x00, 0, "STAC9762/63",	0 }, /* Extrapolated */
	{ 0x83847666, 0x00, 0, "STAC9766/67",	0 },
	{ 0x53494c22, 0x00, 0, "Si3036",	0 },
	{ 0x53494c23, 0x00, 0, "Si3038",	0 },
	{ 0x54524103, 0x00, 0, "TR28023",	0 }, /* Extrapolated */
	{ 0x54524106, 0x00, 0, "TR28026",	0 },
	{ 0x54524108, 0x00, 0, "TR28028",	0 },
	{ 0x54524123, 0x00, 0, "TR28602",	0 },
	{ 0x54524e03, 0x07, 0, "TLV320AIC27",	0 },
	{ 0x54584e20, 0x00, 0, "TLC320AD90",	0 },
	{ 0x56494161, 0x00, 0, "VIA1612A",      0 },
	{ 0x56494170, 0x00, 0, "VIA1617A",      0 },
	{ 0x574d4c00, 0x00, 0, "WM9701A",	0 },
	{ 0x574d4c03, 0x00, 0, "WM9703/4/7/8",	0 },
	{ 0x574d4c04, 0x00, 0, "WM9704Q",	0 },
	{ 0x574d4c05, 0x00, 0, "WM9705/10",	0 },
	{ 0x574d4d09, 0x00, 0, "WM9709",	0 },
	{ 0x574d4c12, 0x00, 0, "WM9711/12",	0 }, /* XXX: patch needed */
	{ 0x57454301, 0x00, 0, "W83971D",	0 },
	{ 0x594d4800, 0x00, 0, "YMF743",	0 },
	{ 0x594d4802, 0x00, 0, "YMF752",	0 },
	{ 0x594d4803, 0x00, 0, "YMF753",	0 },
	/* 
	 * XXX This is a fluke, really! The real codec
	 * should be STAC9704, not this! This should be
	 * removed someday!
	 */
	{ 0x01408384, 0x00, 0, "EV1938",	0 },
	{ 0, 0, 0, NULL, 0 }
};

static char *ac97enhancement[] = {
	"no 3D Stereo Enhancement",
	"Analog Devices Phat Stereo",
	"Creative Stereo Enhancement",
	"National Semi 3D Stereo Enhancement",
	"Yamaha Ymersion",
	"BBE 3D Stereo Enhancement",
	"Crystal Semi 3D Stereo Enhancement",
	"Qsound QXpander",
	"Spatializer 3D Stereo Enhancement",
	"SRS 3D Stereo Enhancement",
	"Platform Tech 3D Stereo Enhancement",
	"AKM 3D Audio",
	"Aureal Stereo Enhancement",
	"Aztech 3D Enhancement",
	"Binaura 3D Audio Enhancement",
	"ESS Technology Stereo Enhancement",
	"Harman International VMAx",
	"Nvidea 3D Stereo Enhancement",
	"Philips Incredible Sound",
	"Texas Instruments 3D Stereo Enhancement",
	"VLSI Technology 3D Stereo Enhancement",
	"TriTech 3D Stereo Enhancement",
	"Realtek 3D Stereo Enhancement",
	"Samsung 3D Stereo Enhancement",
	"Wolfson Microelectronics 3D Enhancement",
	"Delta Integration 3D Enhancement",
	"SigmaTel 3D Enhancement",
	"Reserved 27",
	"Rockwell 3D Stereo Enhancement",
	"Reserved 29",
	"Reserved 30",
	"Reserved 31"
};

static char *ac97feature[] = {
	"mic channel",
	"reserved",
	"tone",
	"simulated stereo",
	"headphone",
	"bass boost",
	"18 bit DAC",
	"20 bit DAC",
	"18 bit ADC",
	"20 bit ADC"
};

static char *ac97extfeature[] = {
	"variable rate PCM",
	"double rate PCM",
	"reserved 1",
	"variable rate mic",
	"reserved 2",
	"reserved 3",
	"center DAC",
	"surround DAC",
	"LFE DAC",
	"AMAP",
	"reserved 4",
	"reserved 5",
	"reserved 6",
	"reserved 7",
};

u_int16_t
ac97_rdcd(struct ac97_info *codec, int reg)
{
	if (codec->flags & AC97_F_RDCD_BUG) {
		u_int16_t i[2], j = 100;

		i[0] = AC97_READ(codec->methods, codec->devinfo, reg);
		i[1] = AC97_READ(codec->methods, codec->devinfo, reg);
		while (i[0] != i[1] && j)
			i[j-- & 1] = AC97_READ(codec->methods, codec->devinfo, reg);
#if 0
		if (j < 100) {
			device_printf(codec->dev, "%s(): Inconsistent register value at"
					" 0x%08x (retry: %d)\n", __func__, reg, 100 - j);
		}
#endif
		return i[!(j & 1)];
	}
	return AC97_READ(codec->methods, codec->devinfo, reg);
}

void
ac97_wrcd(struct ac97_info *codec, int reg, u_int16_t val)
{
	AC97_WRITE(codec->methods, codec->devinfo, reg, val);
}

static void
ac97_reset(struct ac97_info *codec)
{
	u_int32_t i, ps;
	ac97_wrcd(codec, AC97_REG_RESET, 0);
	for (i = 0; i < 500; i++) {
		ps = ac97_rdcd(codec, AC97_REG_POWER) & AC97_POWER_STATUS;
		if (ps == AC97_POWER_STATUS)
			return;
		DELAY(1000);
	}
	device_printf(codec->dev, "AC97 reset timed out.\n");
}

int
ac97_setrate(struct ac97_info *codec, int which, int rate)
{
	u_int16_t v;

	switch(which) {
	case AC97_REGEXT_FDACRATE:
	case AC97_REGEXT_SDACRATE:
	case AC97_REGEXT_LDACRATE:
	case AC97_REGEXT_LADCRATE:
	case AC97_REGEXT_MADCRATE:
		break;

	default:
		return -1;
	}

	snd_mtxlock(codec->lock);
	if (rate != 0) {
		v = rate;
		if (codec->extstat & AC97_EXTCAP_DRA)
			v >>= 1;
		ac97_wrcd(codec, which, v);
	}
	v = ac97_rdcd(codec, which);
	if (codec->extstat & AC97_EXTCAP_DRA)
		v <<= 1;
	snd_mtxunlock(codec->lock);
	return v;
}

int
ac97_setextmode(struct ac97_info *codec, u_int16_t mode)
{
	mode &= AC97_EXTCAPS;
	if ((mode & ~codec->extcaps) != 0) {
		device_printf(codec->dev, "ac97 invalid mode set 0x%04x\n",
			      mode);
		return -1;
	}
	snd_mtxlock(codec->lock);
	ac97_wrcd(codec, AC97_REGEXT_STAT, mode);
	codec->extstat = ac97_rdcd(codec, AC97_REGEXT_STAT) & AC97_EXTCAPS;
	snd_mtxunlock(codec->lock);
	return (mode == codec->extstat)? 0 : -1;
}

u_int16_t
ac97_getextmode(struct ac97_info *codec)
{
	return codec->extstat;
}

u_int16_t
ac97_getextcaps(struct ac97_info *codec)
{
	return codec->extcaps;
}

u_int16_t
ac97_getcaps(struct ac97_info *codec)
{
	return codec->caps;
}

u_int32_t
ac97_getsubvendor(struct ac97_info *codec)
{
	return codec->subvendor;
}

static int
ac97_setrecsrc(struct ac97_info *codec, int channel)
{
	struct ac97mixtable_entry *e = &codec->mix[channel];

	if (e->recidx > 0) {
		int val = e->recidx - 1;
		val |= val << 8;
		snd_mtxlock(codec->lock);
		ac97_wrcd(codec, AC97_REG_RECSEL, val);
		snd_mtxunlock(codec->lock);
		return 0;
	} else
		return -1;
}

static int
ac97_setmixer(struct ac97_info *codec, unsigned channel, unsigned left, unsigned right)
{
	struct ac97mixtable_entry *e = &codec->mix[channel];

	if (e->reg && e->enable && e->bits) {
		int mask, max, val, reg;

		reg = (e->reg >= 0) ? e->reg : -e->reg;	/* AC97 register    */
		max = (1 << e->bits) - 1;		/* actual range	    */
		mask = (max << 8) | max;		/* bits of interest */

		if (!e->stereo)
			right = left;

		/*
		 * Invert the range if the polarity requires so,
		 * then scale to 0..max-1 to compute the value to
		 * write into the codec, and scale back to 0..100
		 * for the return value.
		 */
		if (e->reg > 0) {
			left = 100 - left;
			right = 100 - right;
		}

		left = (left * max) / 100;
		right = (right * max) / 100;

		val = (left << 8) | right;

		left = (left * 100) / max;
		right = (right * 100) / max;

		if (e->reg > 0) {
			left = 100 - left;
			right = 100 - right;
		}

		/*
		 * For mono controls, trim val and mask, also taking
		 * care of e->ofs (offset of control field).
		 */
		if (e->ofs) {
			val &= max;
			val <<= e->ofs;
			mask = (max << e->ofs);
		}

		/*
		 * If we have a mute bit, add it to the mask and
		 * update val and set mute if both channels require a
		 * zero volume.
		 */
		if (e->mute == 1) {
			mask |= AC97_MUTE;
			if (left == 0 && right == 0)
				val = AC97_MUTE;
		}

		/*
		 * If the mask bit is set, do not alter the other bits.
		 */
		snd_mtxlock(codec->lock);
		if (e->mask) {
			int cur = ac97_rdcd(codec, reg);
			val |= cur & ~(mask);
		}
		ac97_wrcd(codec, reg, val);
		snd_mtxunlock(codec->lock);
		return left | (right << 8);
	} else {
#if 0
		printf("ac97_setmixer: reg=%d, bits=%d, enable=%d\n", e->reg, e->bits, e->enable);
#endif
		return -1;
	}
}

static void
ac97_fix_auxout(struct ac97_info *codec)
{
	int keep_ogain;

	/*
	 * By default, The ac97 aux_out register (0x04) corresponds to OSS's
	 * OGAIN setting.
	 *
	 * We first check whether aux_out is a valid register.  If not
	 * we may not want to keep ogain.
	 */
	keep_ogain = ac97_rdcd(codec, AC97_MIX_AUXOUT) & 0x8000;

	/*
	 * Determine what AUX_OUT really means, it can be:
	 *
	 * 1. Headphone out.
	 * 2. 4-Channel Out
	 * 3. True line level out (effectively master volume).
	 *
	 * See Sections 5.2.1 and 5.27 for AUX_OUT Options in AC97r2.{2,3}.
	 */
	if (codec->extcaps & AC97_EXTCAP_SDAC &&
	    ac97_rdcd(codec, AC97_MIXEXT_SURROUND) == 0x8080) {
		codec->mix[SOUND_MIXER_OGAIN].reg = AC97_MIXEXT_SURROUND;
		keep_ogain = 1;
	}

	if (keep_ogain == 0) {
		bzero(&codec->mix[SOUND_MIXER_OGAIN],
		      sizeof(codec->mix[SOUND_MIXER_OGAIN]));
	}
}

static void
ac97_fix_tone(struct ac97_info *codec)
{
	/*
	 * YMF chips does not indicate tone and 3D enhancement capability
	 * in the AC97_REG_RESET register.
	 */
	switch (codec->id) {
	case 0x594d4800:	/* YMF743 */
	case 0x594d4803:	/* YMF753 */
		codec->caps |= AC97_CAP_TONE;
		codec->se |= 0x04;
		break;
	case 0x594d4802:	/* YMF752 */
		codec->se |= 0x04;
		break;
	default:
		break;
	}

	/* Hide treble and bass if they don't exist */
	if ((codec->caps & AC97_CAP_TONE) == 0) {
		bzero(&codec->mix[SOUND_MIXER_BASS],
		      sizeof(codec->mix[SOUND_MIXER_BASS]));
		bzero(&codec->mix[SOUND_MIXER_TREBLE],
		      sizeof(codec->mix[SOUND_MIXER_TREBLE]));
	}
}

static const char*
ac97_hw_desc(u_int32_t id, const char* vname, const char* cname, char* buf)
{
	if (cname == NULL) {
		sprintf(buf, "Unknown AC97 Codec (id = 0x%08x)", id);
		return buf;
	}

	if (vname == NULL) vname = "Unknown";

	if (bootverbose) {
		sprintf(buf, "%s %s AC97 Codec (id = 0x%08x)", vname, cname, id);
	} else {
		sprintf(buf, "%s %s AC97 Codec", vname, cname);
	}
	return buf;
}

static unsigned
ac97_initmixer(struct ac97_info *codec)
{
	ac97_patch codec_patch;
	const char *cname, *vname;
	char desc[80];
	u_int8_t model, step;
	unsigned i, j, k, bit, old;
	u_int32_t id;
	int reg;

	snd_mtxlock(codec->lock);
	codec->count = AC97_INIT(codec->methods, codec->devinfo);
	if (codec->count == 0) {
		device_printf(codec->dev, "ac97 codec init failed\n");
		snd_mtxunlock(codec->lock);
		return ENODEV;
	}

	ac97_wrcd(codec, AC97_REG_POWER, (codec->flags & AC97_F_EAPD_INV)? 0x8000 : 0x0000);
	ac97_reset(codec);
	ac97_wrcd(codec, AC97_REG_POWER, (codec->flags & AC97_F_EAPD_INV)? 0x8000 : 0x0000);

	i = ac97_rdcd(codec, AC97_REG_RESET);
	j = ac97_rdcd(codec, AC97_REG_RESET);
	k = ac97_rdcd(codec, AC97_REG_RESET);
	/*
	 * Let see if this codec can return consistent value.
	 * If not, turn on aggressive read workaround
	 * (STAC9704 comes in mind).
	 */
	if (i != j || j != k) {
		codec->flags |= AC97_F_RDCD_BUG;
		i = ac97_rdcd(codec, AC97_REG_RESET);
	}
	codec->caps = i & 0x03ff;
	codec->se =  (i & 0x7c00) >> 10;

	id = (ac97_rdcd(codec, AC97_REG_ID1) << 16) | ac97_rdcd(codec, AC97_REG_ID2);
	if (id == 0 || id == 0xffffffff) {
		device_printf(codec->dev, "ac97 codec invalid or not present (id == %x)\n", id);
		snd_mtxunlock(codec->lock);
		return ENODEV;
	}

	codec->id = id;
	codec->subvendor = (u_int32_t)pci_get_subdevice(codec->dev) << 16;
	codec->subvendor |= (u_int32_t)pci_get_subvendor(codec->dev) &
	    0x0000ffff;
	codec->noext = 0;
	codec_patch = NULL;

	cname = NULL;
	model = step = 0;
	for (i = 0; ac97codecid[i].id; i++) {
		u_int32_t modelmask = 0xffffffff ^ ac97codecid[i].stepmask;
		if ((ac97codecid[i].id & modelmask) == (id & modelmask)) {
			codec->noext = ac97codecid[i].noext;
			codec_patch = ac97codecid[i].patch;
			cname = ac97codecid[i].name;
			model = (id & modelmask) & 0xff;
			step = (id & ~modelmask) & 0xff;
			break;
		}
	}

	vname = NULL;
	for (i = 0; ac97vendorid[i].id; i++) {
		if (ac97vendorid[i].id == (id & 0xffffff00)) {
			vname = ac97vendorid[i].name;
			break;
		}
	}

	codec->extcaps = 0;
	codec->extid = 0;
	codec->extstat = 0;
	if (!codec->noext) {
		i = ac97_rdcd(codec, AC97_REGEXT_ID);
		if (i != 0xffff) {
			codec->extcaps = i & 0x3fff;
			codec->extid =  (i & 0xc000) >> 14;
			codec->extstat = ac97_rdcd(codec, AC97_REGEXT_STAT) & AC97_EXTCAPS;
		}
	}

	for (i = 0; i < AC97_MIXER_SIZE; i++) {
		codec->mix[i] = ac97mixtable_default[i];
	}
	ac97_fix_auxout(codec);
	ac97_fix_tone(codec);
	if (codec_patch)
		codec_patch(codec);

	for (i = 0; i < AC97_MIXER_SIZE; i++) {
		k = codec->noext? codec->mix[i].enable : 1;
		reg = codec->mix[i].reg;
		if (reg < 0)
			reg = -reg;
		if (k && reg) {
			j = old = ac97_rdcd(codec, reg);
			/*
			 * Test for mute bit (except for AC97_MIX_TONE,
			 * where we simply assume it as available).
			 */
			if (codec->mix[i].mute) {
				ac97_wrcd(codec, reg, j | 0x8000);
				j = ac97_rdcd(codec, reg);
			} else
				j |= 0x8000;
			if ((j & 0x8000)) {
				/*
				 * Test whether the control width should be
				 * 4, 5 or 6 bit. For 5bit register, we should
				 * test it whether it's really 5 or 6bit. Leave
				 * 4bit register alone, because sometimes an
				 * attempt to write past 4th bit may cause
				 * incorrect result especially for AC97_MIX_BEEP
				 * (ac97 2.3).
				 */
				bit = codec->mix[i].bits;
				if (bit == 5)
					bit++;
				j = ((1 << bit) - 1) << codec->mix[i].ofs;
				ac97_wrcd(codec, reg,
					j | (codec->mix[i].mute ? 0x8000 : 0));
				k = ac97_rdcd(codec, reg) & j;
				k >>= codec->mix[i].ofs;
				if (reg == AC97_MIX_TONE &&
							((k & 0x0001) == 0x0000))
					k >>= 1;
				for (j = 0; k >> j; j++)
					;
				if (j != 0) {
#if 0
					device_printf(codec->dev, "%2d: [ac97_rdcd() = %d] [Testbit = %d] %d -> %d\n",
						i, k, bit, codec->mix[i].bits, j);
#endif
					codec->mix[i].enable = 1;
					codec->mix[i].bits = j;
				} else if (reg == AC97_MIX_BEEP) {
					/*
					 * Few codec such as CX20468-21 does
					 * have this control register, although
					 * the only usable part is the mute bit.
					 */
					codec->mix[i].enable = 1;
				} else
					codec->mix[i].enable = 0;
			} else
				codec->mix[i].enable = 0;
			ac97_wrcd(codec, reg, old);
		}
#if 0
		printf("mixch %d, en=%d, b=%d\n", i, codec->mix[i].enable, codec->mix[i].bits);
#endif
	}

	device_printf(codec->dev, "<%s>\n",
		      ac97_hw_desc(codec->id, vname, cname, desc));

	if (bootverbose) {
		if (codec->flags & AC97_F_RDCD_BUG)
			device_printf(codec->dev, "Buggy AC97 Codec: aggressive ac97_rdcd() workaround enabled\n");
		device_printf(codec->dev, "Codec features ");
		for (i = j = 0; i < 10; i++)
			if (codec->caps & (1 << i))
				printf("%s%s", j++? ", " : "", ac97feature[i]);
		printf("%s%d bit master volume", j++? ", " : "", codec->mix[SOUND_MIXER_VOLUME].bits);
		printf("%s%s\n", j? ", " : "", ac97enhancement[codec->se]);

		if (codec->extcaps != 0 || codec->extid) {
			device_printf(codec->dev, "%s codec",
				      codec->extid? "Secondary" : "Primary");
			if (codec->extcaps)
				printf(" extended features ");
			for (i = j = 0; i < 14; i++)
				if (codec->extcaps & (1 << i))
					printf("%s%s", j++? ", " : "", ac97extfeature[i]);
			printf("\n");
		}
	}

	i = 0;
	while ((ac97_rdcd(codec, AC97_REG_POWER) & 2) == 0) {
		if (++i == 100) {
			device_printf(codec->dev, "ac97 codec reports dac not ready\n");
			break;
		}
		DELAY(1000);
	}
	if (bootverbose)
		device_printf(codec->dev, "ac97 codec dac ready count: %d\n", i);
	snd_mtxunlock(codec->lock);
	return 0;
}

static unsigned
ac97_reinitmixer(struct ac97_info *codec)
{
	snd_mtxlock(codec->lock);
	codec->count = AC97_INIT(codec->methods, codec->devinfo);
	if (codec->count == 0) {
		device_printf(codec->dev, "ac97 codec init failed\n");
		snd_mtxunlock(codec->lock);
		return ENODEV;
	}

	ac97_wrcd(codec, AC97_REG_POWER, (codec->flags & AC97_F_EAPD_INV)? 0x8000 : 0x0000);
	ac97_reset(codec);
	ac97_wrcd(codec, AC97_REG_POWER, (codec->flags & AC97_F_EAPD_INV)? 0x8000 : 0x0000);

	if (!codec->noext) {
		ac97_wrcd(codec, AC97_REGEXT_STAT, codec->extstat);
		if ((ac97_rdcd(codec, AC97_REGEXT_STAT) & AC97_EXTCAPS)
		    != codec->extstat)
			device_printf(codec->dev, "ac97 codec failed to reset extended mode (%x, got %x)\n",
				      codec->extstat,
				      ac97_rdcd(codec, AC97_REGEXT_STAT) &
				      AC97_EXTCAPS);
	}

	if ((ac97_rdcd(codec, AC97_REG_POWER) & 2) == 0)
		device_printf(codec->dev, "ac97 codec reports dac not ready\n");
	snd_mtxunlock(codec->lock);
	return 0;
}

struct ac97_info *
ac97_create(device_t dev, void *devinfo, kobj_class_t cls)
{
	struct ac97_info *codec;
	int i;

	codec = malloc(sizeof(*codec), M_AC97, M_WAITOK | M_ZERO);
	snprintf(codec->name, sizeof(codec->name), "%s:ac97",
	    device_get_nameunit(dev));
	codec->lock = snd_mtxcreate(codec->name, "ac97 codec");
	codec->methods = kobj_create(cls, M_AC97, M_WAITOK | M_ZERO);
	codec->dev = dev;
	codec->devinfo = devinfo;
	codec->flags = 0;

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "eapdinv", &i) == 0 && i != 0)
		codec->flags |= AC97_F_EAPD_INV;

	if (resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "softpcmvol", &i) == 0 && i != 0)
		pcm_setflags(dev, pcm_getflags(dev) | SD_F_SOFTPCMVOL);

	return codec;
}

void
ac97_destroy(struct ac97_info *codec)
{
	snd_mtxlock(codec->lock);
	if (codec->methods != NULL)
		kobj_delete(codec->methods, M_AC97);
	snd_mtxfree(codec->lock);
	free(codec, M_AC97);
}

void
ac97_setflags(struct ac97_info *codec, u_int32_t val)
{
	codec->flags = val;
}

u_int32_t
ac97_getflags(struct ac97_info *codec)
{
	return codec->flags;
}

/* -------------------------------------------------------------------- */

static int
sysctl_hw_snd_ac97_eapd(SYSCTL_HANDLER_ARGS)
{
	struct ac97_info *codec;
	int ea, inv, err = 0;
	u_int16_t val;

	codec = oidp->oid_arg1;
	if (codec == NULL || codec->id == 0 || codec->lock == NULL)
		return EINVAL;
	snd_mtxlock(codec->lock);
	val = ac97_rdcd(codec, AC97_REG_POWER);
	inv = (codec->flags & AC97_F_EAPD_INV) ? 0 : 1;
	ea = (val >> 15) ^ inv;
	snd_mtxunlock(codec->lock);
	err = sysctl_handle_int(oidp, &ea, 0, req);
	if (err == 0 && req->newptr != NULL) {
		if (ea != 0 && ea != 1)
			return EINVAL;
		if (ea != ((val >> 15) ^ inv)) {
			snd_mtxlock(codec->lock);
			ac97_wrcd(codec, AC97_REG_POWER, val ^ 0x8000);
			snd_mtxunlock(codec->lock);
		}
	}
	return err;
}

static void
ac97_init_sysctl(struct ac97_info *codec)
{
	u_int16_t orig, val;

	if (codec == NULL || codec->dev == NULL)
		return;
	snd_mtxlock(codec->lock);
	orig = ac97_rdcd(codec, AC97_REG_POWER);
	ac97_wrcd(codec, AC97_REG_POWER, orig ^ 0x8000);
	val = ac97_rdcd(codec, AC97_REG_POWER);
	ac97_wrcd(codec, AC97_REG_POWER, orig);
	snd_mtxunlock(codec->lock);
	if ((val & 0x8000) == (orig & 0x8000))
		return;
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(codec->dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(codec->dev)),
            OID_AUTO, "eapd", CTLTYPE_INT | CTLFLAG_RW,
	    codec, sizeof(codec), sysctl_hw_snd_ac97_eapd,
	    "I", "AC97 External Amplifier");
}

static int
ac97mix_init(struct snd_mixer *m)
{
	struct ac97_info *codec = mix_getdevinfo(m);
	u_int32_t i, mask;

	if (codec == NULL)
		return -1;

	if (ac97_initmixer(codec))
		return -1;

	switch (codec->id) {
	case 0x41445374:	/* AD1981B */
		switch (codec->subvendor) {
		case 0x02d91014:
			/*
			 * IBM Thinkcentre:
			 *
			 * Tie "ogain" and "phout" to "vol" since its
			 * master volume is basically useless and can't
			 * control anything.
			 */
			mask = 0;
			if (codec->mix[SOUND_MIXER_OGAIN].enable)
				mask |= SOUND_MASK_OGAIN;
			if (codec->mix[SOUND_MIXER_PHONEOUT].enable)
				mask |= SOUND_MASK_PHONEOUT;
			if (codec->mix[SOUND_MIXER_VOLUME].enable)
				mix_setparentchild(m, SOUND_MIXER_VOLUME,
				    mask);
			else {
				mix_setparentchild(m, SOUND_MIXER_VOLUME,
				    mask);
				mix_setrealdev(m, SOUND_MIXER_VOLUME,
				    SOUND_MIXER_NONE);
			}
			break;
		case 0x099c103c:
			/*
			 * HP nx6110:
			 *
			 * By default, "vol" is controlling internal speakers
			 * (not a master volume!) and "ogain" is controlling
			 * headphone. Enable dummy "phout" so it can be
			 * remapped to internal speakers and virtualize
			 * "vol" to control both.
			 */
			codec->mix[SOUND_MIXER_OGAIN].enable = 1;
			codec->mix[SOUND_MIXER_PHONEOUT].enable = 1;
			mix_setrealdev(m, SOUND_MIXER_PHONEOUT,
			    SOUND_MIXER_VOLUME);
			mix_setrealdev(m, SOUND_MIXER_VOLUME,
			    SOUND_MIXER_NONE);
			mix_setparentchild(m, SOUND_MIXER_VOLUME,
			    SOUND_MASK_OGAIN | SOUND_MASK_PHONEOUT);
			break;
		default:
			break;
		}
		break;
	case 0x434d4941:	/* CMI9738 */
	case 0x434d4961:	/* CMI9739 */
	case 0x434d4978:	/* CMI9761 */
	case 0x434d4982:	/* CMI9761 */
	case 0x434d4983:	/* CMI9761 */
		bzero(&codec->mix[SOUND_MIXER_PCM],
		    sizeof(codec->mix[SOUND_MIXER_PCM]));
		pcm_setflags(codec->dev, pcm_getflags(codec->dev) |
		    SD_F_SOFTPCMVOL);
		/* XXX How about master volume ? */
		break;
	default:
		break;
	}

	if (pcm_getflags(codec->dev) & SD_F_SOFTPCMVOL)
		ac97_wrcd(codec, AC97_MIX_PCM, 0);
#if 0
	/* XXX For the sake of debugging purposes */
	mix_setparentchild(m, SOUND_MIXER_VOLUME,
	    SOUND_MASK_PCM | SOUND_MASK_CD);
	mix_setrealdev(m, SOUND_MIXER_VOLUME, SOUND_MIXER_NONE);
	ac97_wrcd(codec, AC97_MIX_MASTER, 0);
#endif

	mask = 0;
	for (i = 0; i < AC97_MIXER_SIZE; i++)
		mask |= codec->mix[i].enable? 1 << i : 0;
	mix_setdevs(m, mask);

	mask = 0;
	for (i = 0; i < AC97_MIXER_SIZE; i++)
		mask |= codec->mix[i].recidx? 1 << i : 0;
	mix_setrecdevs(m, mask);

	ac97_init_sysctl(codec);

	return 0;
}

static int
ac97mix_uninit(struct snd_mixer *m)
{
	struct ac97_info *codec = mix_getdevinfo(m);

	if (codec == NULL)
		return -1;
	/*
	if (ac97_uninitmixer(codec))
		return -1;
	*/
	ac97_destroy(codec);
	return 0;
}

static int
ac97mix_reinit(struct snd_mixer *m)
{
	struct ac97_info *codec = mix_getdevinfo(m);

	if (codec == NULL)
		return -1;
	return ac97_reinitmixer(codec);
}

static int
ac97mix_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct ac97_info *codec = mix_getdevinfo(m);

	if (codec == NULL || dev >= AC97_MIXER_SIZE)
		return -1;
	return ac97_setmixer(codec, dev, left, right);
}

static u_int32_t
ac97mix_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	int i;
	struct ac97_info *codec = mix_getdevinfo(m);

	if (codec == NULL)
		return -1;
	for (i = 0; i < AC97_MIXER_SIZE; i++)
		if ((src & (1 << i)) != 0)
			break;
	return (ac97_setrecsrc(codec, i) == 0)? 1U << i : 0xffffffffU;
}

static kobj_method_t ac97mixer_methods[] = {
    	KOBJMETHOD(mixer_init,		ac97mix_init),
    	KOBJMETHOD(mixer_uninit,	ac97mix_uninit),
    	KOBJMETHOD(mixer_reinit,	ac97mix_reinit),
    	KOBJMETHOD(mixer_set,		ac97mix_set),
    	KOBJMETHOD(mixer_setrecsrc,	ac97mix_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(ac97mixer);

/* -------------------------------------------------------------------- */

kobj_class_t
ac97_getmixerclass(void)
{
	return &ac97mixer_class;
}
