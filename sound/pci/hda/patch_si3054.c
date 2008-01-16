/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for Silicon Labs 3054/5 modem codec
 *
 * Copyright (c) 2005 Sasha Khapyorsky <sashak@alsa-project.org>
 *                    Takashi Iwai <tiwai@suse.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"


/* si3054 verbs */
#define SI3054_VERB_READ_NODE  0x900
#define SI3054_VERB_WRITE_NODE 0x100

/* si3054 nodes (registers) */
#define SI3054_EXTENDED_MID    2
#define SI3054_LINE_RATE       3
#define SI3054_LINE_LEVEL      4
#define SI3054_GPIO_CFG        5
#define SI3054_GPIO_POLARITY   6
#define SI3054_GPIO_STICKY     7
#define SI3054_GPIO_WAKEUP     8
#define SI3054_GPIO_STATUS     9
#define SI3054_GPIO_CONTROL   10
#define SI3054_MISC_AFE       11
#define SI3054_CHIPID         12
#define SI3054_LINE_CFG1      13
#define SI3054_LINE_STATUS    14
#define SI3054_DC_TERMINATION 15
#define SI3054_LINE_CONFIG    16
#define SI3054_CALLPROG_ATT   17
#define SI3054_SQ_CONTROL     18
#define SI3054_MISC_CONTROL   19
#define SI3054_RING_CTRL1     20
#define SI3054_RING_CTRL2     21

/* extended MID */
#define SI3054_MEI_READY 0xf

/* line level */
#define SI3054_ATAG_MASK 0x00f0
#define SI3054_DTAG_MASK 0xf000

/* GPIO bits */
#define SI3054_GPIO_OH    0x0001
#define SI3054_GPIO_CID   0x0002

/* chipid and revisions */
#define SI3054_CHIPID_CODEC_REV_MASK 0x000f
#define SI3054_CHIPID_DAA_REV_MASK   0x00f0
#define SI3054_CHIPID_INTERNATIONAL  0x0100
#define SI3054_CHIPID_DAA_ID         0x0f00
#define SI3054_CHIPID_CODEC_ID      (1<<12)

/* si3054 codec registers (nodes) access macros */
#define GET_REG(codec,reg) (snd_hda_codec_read(codec,reg,0,SI3054_VERB_READ_NODE,0))
#define SET_REG(codec,reg,val) (snd_hda_codec_write(codec,reg,0,SI3054_VERB_WRITE_NODE,val))
#define SET_REG_CACHE(codec,reg,val) \
	snd_hda_codec_write_cache(codec,reg,0,SI3054_VERB_WRITE_NODE,val)


struct si3054_spec {
	unsigned international;
	struct hda_pcm pcm;
};


/*
 * Modem mixer
 */

#define PRIVATE_VALUE(reg,mask) ((reg<<16)|(mask&0xffff))
#define PRIVATE_REG(val) ((val>>16)&0xffff)
#define PRIVATE_MASK(val) (val&0xffff)

#define si3054_switch_info	snd_ctl_boolean_mono_info

static int si3054_switch_get(struct snd_kcontrol *kcontrol,
		               struct snd_ctl_elem_value *uvalue)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 reg  = PRIVATE_REG(kcontrol->private_value);
	u16 mask = PRIVATE_MASK(kcontrol->private_value);
	uvalue->value.integer.value[0] = (GET_REG(codec, reg)) & mask ? 1 : 0 ;
	return 0;
}

static int si3054_switch_put(struct snd_kcontrol *kcontrol,
		               struct snd_ctl_elem_value *uvalue)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 reg  = PRIVATE_REG(kcontrol->private_value);
	u16 mask = PRIVATE_MASK(kcontrol->private_value);
	if (uvalue->value.integer.value[0])
		SET_REG_CACHE(codec, reg, (GET_REG(codec, reg)) | mask);
	else
		SET_REG_CACHE(codec, reg, (GET_REG(codec, reg)) & ~mask);
	return 0;
}

#define SI3054_KCONTROL(kname,reg,mask) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name = kname, \
	.info = si3054_switch_info, \
	.get  = si3054_switch_get, \
	.put  = si3054_switch_put, \
	.private_value = PRIVATE_VALUE(reg,mask), \
}
		

static struct snd_kcontrol_new si3054_modem_mixer[] = {
	SI3054_KCONTROL("Off-hook Switch", SI3054_GPIO_CONTROL, SI3054_GPIO_OH),
	SI3054_KCONTROL("Caller ID Switch", SI3054_GPIO_CONTROL, SI3054_GPIO_CID),
	{}
};

static int si3054_build_controls(struct hda_codec *codec)
{
	return snd_hda_add_new_ctls(codec, si3054_modem_mixer);
}


/*
 * PCM callbacks
 */

static int si3054_pcm_prepare(struct hda_pcm_stream *hinfo,
			      struct hda_codec *codec,
			      unsigned int stream_tag,
			      unsigned int format,
			      struct snd_pcm_substream *substream)
{
	u16 val;

	SET_REG(codec, SI3054_LINE_RATE, substream->runtime->rate);
	val = GET_REG(codec, SI3054_LINE_LEVEL);
	val &= 0xff << (8 * (substream->stream != SNDRV_PCM_STREAM_PLAYBACK));
	val |= ((stream_tag & 0xf) << 4) << (8 * (substream->stream == SNDRV_PCM_STREAM_PLAYBACK));
	SET_REG(codec, SI3054_LINE_LEVEL, val);

	snd_hda_codec_setup_stream(codec, hinfo->nid,
				   stream_tag, 0, format);
	return 0;
}

static int si3054_pcm_open(struct hda_pcm_stream *hinfo,
			   struct hda_codec *codec,
			    struct snd_pcm_substream *substream)
{
	static unsigned int rates[] = { 8000, 9600, 16000 };
	static struct snd_pcm_hw_constraint_list hw_constraints_rates = {
		.count = ARRAY_SIZE(rates),
		.list = rates,
		.mask = 0,
	};
	substream->runtime->hw.period_bytes_min = 80;
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates);
}


static struct hda_pcm_stream si3054_pcm = {
	.substreams = 1,
	.channels_min = 1,
	.channels_max = 1,
	.nid = 0x1,
	.rates = SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|SNDRV_PCM_RATE_KNOT,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.maxbps = 16,
	.ops = {
		.open = si3054_pcm_open,
		.prepare = si3054_pcm_prepare,
	},
};


static int si3054_build_pcms(struct hda_codec *codec)
{
	struct si3054_spec *spec = codec->spec;
	struct hda_pcm *info = &spec->pcm;
	si3054_pcm.nid = codec->mfg;
	codec->num_pcms = 1;
	codec->pcm_info = info;
	info->name = "Si3054 Modem";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = si3054_pcm;
	info->stream[SNDRV_PCM_STREAM_CAPTURE]  = si3054_pcm;
	info->is_modem = 1;
	return 0;
}


/*
 * Init part
 */

static int si3054_init(struct hda_codec *codec)
{
	struct si3054_spec *spec = codec->spec;
	unsigned wait_count;
	u16 val;

	snd_hda_codec_write(codec, AC_NODE_ROOT, 0, AC_VERB_SET_CODEC_RESET, 0);
	snd_hda_codec_write(codec, codec->mfg, 0, AC_VERB_SET_STREAM_FORMAT, 0);
	SET_REG(codec, SI3054_LINE_RATE, 9600);
	SET_REG(codec, SI3054_LINE_LEVEL, SI3054_DTAG_MASK|SI3054_ATAG_MASK);
	SET_REG(codec, SI3054_EXTENDED_MID, 0);

	wait_count = 10;
	do {
		msleep(2);
		val = GET_REG(codec, SI3054_EXTENDED_MID);
	} while ((val & SI3054_MEI_READY) != SI3054_MEI_READY && wait_count--);

	if((val&SI3054_MEI_READY) != SI3054_MEI_READY) {
		snd_printk(KERN_ERR "si3054: cannot initialize. EXT MID = %04x\n", val);
		/* let's pray that this is no fatal error */
		/* return -EACCES; */
	}

	SET_REG(codec, SI3054_GPIO_POLARITY, 0xffff);
	SET_REG(codec, SI3054_GPIO_CFG, 0x0);
	SET_REG(codec, SI3054_MISC_AFE, 0);
	SET_REG(codec, SI3054_LINE_CFG1,0x200);

	if((GET_REG(codec,SI3054_LINE_STATUS) & (1<<6)) == 0) {
		snd_printd("Link Frame Detect(FDT) is not ready (line status: %04x)\n",
				GET_REG(codec,SI3054_LINE_STATUS));
	}

	spec->international = GET_REG(codec, SI3054_CHIPID) & SI3054_CHIPID_INTERNATIONAL;

	return 0;
}

static void si3054_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}


/*
 */

static struct hda_codec_ops si3054_patch_ops = {
	.build_controls = si3054_build_controls,
	.build_pcms = si3054_build_pcms,
	.init = si3054_init,
	.free = si3054_free,
};

static int patch_si3054(struct hda_codec *codec)
{
	struct si3054_spec *spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;
	codec->spec = spec;
	codec->patch_ops = si3054_patch_ops;
	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_si3054[] = {
 	{ .id = 0x163c3055, .name = "Si3054", .patch = patch_si3054 },
 	{ .id = 0x163c3155, .name = "Si3054", .patch = patch_si3054 },
 	{ .id = 0x11c13026, .name = "Si3054", .patch = patch_si3054 },
 	{ .id = 0x11c13055, .name = "Si3054", .patch = patch_si3054 },
 	{ .id = 0x11c13155, .name = "Si3054", .patch = patch_si3054 },
 	{ .id = 0x10573055, .name = "Si3054", .patch = patch_si3054 },
 	{ .id = 0x10573057, .name = "Si3054", .patch = patch_si3054 },
 	{ .id = 0x10573155, .name = "Si3054", .patch = patch_si3054 },
	/* VIA HDA on Clevo m540 */
	{ .id = 0x11063288, .name = "Si3054", .patch = patch_si3054 },
	/* Asus A8J Modem (SM56) */
	{ .id = 0x15433155, .name = "Si3054", .patch = patch_si3054 },
	/* LG LW20 modem */
	{ .id = 0x18540018, .name = "Si3054", .patch = patch_si3054 },
	{}
};

