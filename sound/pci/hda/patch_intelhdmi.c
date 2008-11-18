/*
 *
 *  patch_intelhdmi.c - Patch for Intel HDMI codecs
 *
 *  Copyright(c) 2008 Intel Corporation. All rights reserved.
 *
 *  Authors:
 *  			Jiang Zhe <zhe.jiang@intel.com>
 *  			Wu Fengguang <wfg@linux.intel.com>
 *
 *  Maintained by:
 *  			Wu Fengguang <wfg@linux.intel.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_patch.h"

#define CVT_NID		0x02	/* audio converter */
#define PIN_NID		0x03	/* HDMI output pin */

#define INTEL_HDMI_EVENT_TAG		0x08

struct intel_hdmi_spec {
	struct hda_multi_out multiout;
	struct hda_pcm pcm_rec;
	struct sink_eld sink;
};

static struct hda_verb pinout_enable_verb[] = {
	{PIN_NID, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{} /* terminator */
};

static struct hda_verb pinout_disable_verb[] = {
	{PIN_NID, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00},
	{}
};

static struct hda_verb unsolicited_response_verb[] = {
	{PIN_NID, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN |
						  INTEL_HDMI_EVENT_TAG},
	{}
};

static struct hda_verb def_chan_map[] = {
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x00},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x11},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x22},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x33},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x44},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x55},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x66},
	{CVT_NID, AC_VERB_SET_HDMI_CHAN_SLOT, 0x77},
	{}
};


struct hdmi_audio_infoframe {
	u8 type; /* 0x84 */
	u8 ver;  /* 0x01 */
	u8 len;  /* 0x0a */

	u8 checksum;	/* PB0 */
	u8 CC02_CT47;	/* CC in bits 0:2, CT in 4:7 */
	u8 SS01_SF24;
	u8 CXT04;
	u8 CA;
	u8 LFEPBL01_LSV36_DM_INH7;
	u8 reserved[5];	/* PB6  - PB10 */
};

/*
 * HDMI routines
 */

#ifdef BE_PARANOID
static void hdmi_get_dip_index(struct hda_codec *codec, hda_nid_t nid,
				int *packet_index, int *byte_index)
{
	int val;

	val = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_HDMI_DIP_INDEX, 0);

	*packet_index = val >> 5;
	*byte_index = val & 0x1f;
}
#endif

static void hdmi_set_dip_index(struct hda_codec *codec, hda_nid_t nid,
				int packet_index, int byte_index)
{
	int val;

	val = (packet_index << 5) | (byte_index & 0x1f);

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_HDMI_DIP_INDEX, val);
}

static void hdmi_write_dip_byte(struct hda_codec *codec, hda_nid_t nid,
				unsigned char val)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_HDMI_DIP_DATA, val);
}

static void hdmi_enable_output(struct hda_codec *codec)
{
	/* Enable Audio InfoFrame Transmission */
	hdmi_set_dip_index(codec, PIN_NID, 0x0, 0x0);
	snd_hda_codec_write(codec, PIN_NID, 0, AC_VERB_SET_HDMI_DIP_XMIT,
						AC_DIPXMIT_BEST);
	/* Unmute */
	if (get_wcaps(codec, PIN_NID) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, PIN_NID, 0,
				AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
	/* Enable pin out */
	snd_hda_sequence_write(codec, pinout_enable_verb);
}

static void hdmi_disable_output(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, pinout_disable_verb);
	if (get_wcaps(codec, PIN_NID) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, PIN_NID, 0,
				AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);

	/*
	 * FIXME: noises may arise when playing music after reloading the
	 * kernel module, until the next X restart or monitor repower.
	 */
}

static int hdmi_get_channel_count(struct hda_codec *codec)
{
	return 1 + snd_hda_codec_read(codec, CVT_NID, 0,
					AC_VERB_GET_CVT_CHAN_COUNT, 0);
}

static void hdmi_set_channel_count(struct hda_codec *codec, int chs)
{
	snd_hda_codec_write(codec, CVT_NID, 0,
					AC_VERB_SET_CVT_CHAN_COUNT, chs - 1);

	if (chs != hdmi_get_channel_count(codec))
		snd_printd(KERN_INFO "Channel count expect=%d, real=%d\n",
				chs, hdmi_get_channel_count(codec));
}

static void hdmi_debug_slot_mapping(struct hda_codec *codec)
{
#ifdef CONFIG_SND_DEBUG_VERBOSE
	int i;
	int slot;

	for (i = 0; i < 8; i++) {
		slot = snd_hda_codec_read(codec, CVT_NID, 0,
						AC_VERB_GET_HDMI_CHAN_SLOT, i);
		printk(KERN_DEBUG "ASP channel %d => slot %d\n",
				slot >> 4, slot & 0x7);
	}
#endif
}

static void hdmi_setup_channel_mapping(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, def_chan_map);
	hdmi_debug_slot_mapping(codec);
}


static void hdmi_parse_eld(struct hda_codec *codec)
{
	struct intel_hdmi_spec *spec = codec->spec;
	struct sink_eld *eld = &spec->sink;

	if (!snd_hdmi_get_eld(eld, codec, PIN_NID))
		snd_hdmi_show_eld(eld);
}


/*
 * Audio Infoframe routines
 */

static void hdmi_debug_dip_size(struct hda_codec *codec)
{
#ifdef CONFIG_SND_DEBUG_VERBOSE
	int i;
	int size;

	size = snd_hdmi_get_eld_size(codec, PIN_NID);
	printk(KERN_DEBUG "ELD buf size is %d\n", size);

	for (i = 0; i < 8; i++) {
		size = snd_hda_codec_read(codec, PIN_NID, 0,
						AC_VERB_GET_HDMI_DIP_SIZE, i);
		printk(KERN_DEBUG "DIP GP[%d] buf size is %d\n", i, size);
	}
#endif
}

static void hdmi_clear_dip_buffers(struct hda_codec *codec)
{
#ifdef BE_PARANOID
	int i, j;
	int size;
	int pi, bi;
	for (i = 0; i < 8; i++) {
		size = snd_hda_codec_read(codec, PIN_NID, 0,
						AC_VERB_GET_HDMI_DIP_SIZE, i);
		if (size == 0)
			continue;

		hdmi_set_dip_index(codec, PIN_NID, i, 0x0);
		for (j = 1; j < 1000; j++) {
			hdmi_write_dip_byte(codec, PIN_NID, 0x0);
			hdmi_get_dip_index(codec, PIN_NID, &pi, &bi);
			if (pi != i)
				snd_printd(KERN_INFO "dip index %d: %d != %d\n",
						bi, pi, i);
			if (bi == 0) /* byte index wrapped around */
				break;
		}
		snd_printd(KERN_INFO
				"DIP GP[%d] buf reported size=%d, written=%d\n",
				i, size, j);
	}
#endif
}

static void hdmi_setup_audio_infoframe(struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct hdmi_audio_infoframe audio_infoframe = {
		.type		= 0x84,
		.ver		= 0x01,
		.len		= 0x0a,
		.CC02_CT47	= substream->runtime->channels - 1,
	};
	u8 *params = (u8 *)&audio_infoframe;
	int i;

	hdmi_debug_dip_size(codec);
	hdmi_clear_dip_buffers(codec); /* be paranoid */

	hdmi_set_dip_index(codec, PIN_NID, 0x0, 0x0);
	for (i = 0; i < sizeof(audio_infoframe); i++)
		hdmi_write_dip_byte(codec, PIN_NID, params[i]);
}


/*
 * Unsolicited events
 */

static void hdmi_intrinsic_event(struct hda_codec *codec, unsigned int res)
{
	int pind = !!(res & AC_UNSOL_RES_PD);
	int eldv = !!(res & AC_UNSOL_RES_ELDV);

	printk(KERN_INFO "HDMI intrinsic event: PD=%d ELDV=%d\n", pind, eldv);

	if (pind && eldv) {
		hdmi_parse_eld(codec);
		/* TODO: do real things about ELD */
	}
}

static void hdmi_non_intrinsic_event(struct hda_codec *codec, unsigned int res)
{
	int subtag = (res & AC_UNSOL_RES_SUBTAG) >> AC_UNSOL_RES_SUBTAG_SHIFT;
	int cp_state = !!(res & AC_UNSOL_RES_CP_STATE);
	int cp_ready = !!(res & AC_UNSOL_RES_CP_READY);

	printk(KERN_INFO "HDMI non-intrinsic event: "
			"SUBTAG=0x%x CP_STATE=%d CP_READY=%d\n",
			subtag,
			cp_state,
			cp_ready);

	/* who cares? */
	if (cp_state)
		;
	if (cp_ready)
		;
}


static void intel_hdmi_unsol_event(struct hda_codec *codec, unsigned int res)
{
	int tag = res >> AC_UNSOL_RES_TAG_SHIFT;
	int subtag = (res & AC_UNSOL_RES_SUBTAG) >> AC_UNSOL_RES_SUBTAG_SHIFT;

	if (tag != INTEL_HDMI_EVENT_TAG) {
		snd_printd(KERN_INFO
				"Unexpected HDMI unsolicited event tag 0x%x\n",
				tag);
		return;
	}

	if (subtag == 0)
		hdmi_intrinsic_event(codec, res);
	else
		hdmi_non_intrinsic_event(codec, res);
}

/*
 * Callbacks
 */

static int intel_hdmi_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct intel_hdmi_spec *spec = codec->spec;

	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int intel_hdmi_playback_pcm_close(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct intel_hdmi_spec *spec = codec->spec;

	hdmi_disable_output(codec);

	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int intel_hdmi_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	struct intel_hdmi_spec *spec = codec->spec;

	snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					     format, substream);

	hdmi_set_channel_count(codec, substream->runtime->channels);

	/* wfg: channel mapping not supported by DEVCTG */
	hdmi_setup_channel_mapping(codec);

	hdmi_setup_audio_infoframe(codec, substream);

	hdmi_enable_output(codec);

	return 0;
}

static struct hda_pcm_stream intel_hdmi_pcm_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = CVT_NID, /* NID to query formats and rates and setup streams */
	.ops = {
		.open = intel_hdmi_playback_pcm_open,
		.close = intel_hdmi_playback_pcm_close,
		.prepare = intel_hdmi_playback_pcm_prepare
	},
};

static int intel_hdmi_build_pcms(struct hda_codec *codec)
{
	struct intel_hdmi_spec *spec = codec->spec;
	struct hda_pcm *info = &spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "INTEL HDMI";
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = intel_hdmi_pcm_playback;

	return 0;
}

static int intel_hdmi_build_controls(struct hda_codec *codec)
{
	struct intel_hdmi_spec *spec = codec->spec;
	int err;

	err = snd_hda_create_spdif_out_ctls(codec, spec->multiout.dig_out_nid);
	if (err < 0)
		return err;

	return 0;
}

static int intel_hdmi_init(struct hda_codec *codec)
{
	/* disable audio output as early as possible */
	hdmi_disable_output(codec);

	snd_hda_sequence_write(codec, unsolicited_response_verb);

	return 0;
}

static void intel_hdmi_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}

static struct hda_codec_ops intel_hdmi_patch_ops = {
	.init			= intel_hdmi_init,
	.free			= intel_hdmi_free,
	.build_pcms		= intel_hdmi_build_pcms,
	.build_controls 	= intel_hdmi_build_controls,
	.unsol_event		= intel_hdmi_unsol_event,
};

static int patch_intel_hdmi(struct hda_codec *codec)
{
	struct intel_hdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	spec->multiout.num_dacs = 0;	  /* no analog */
	spec->multiout.max_channels = 8;
	spec->multiout.dig_out_nid = CVT_NID;

	codec->spec = spec;
	codec->patch_ops = intel_hdmi_patch_ops;

	snd_hda_eld_proc_new(codec, &spec->sink);

	return 0;
}

struct hda_codec_preset snd_hda_preset_intelhdmi[] = {
	{ .id = 0x808629fb, .name = "INTEL G45 DEVCL",  .patch = patch_intel_hdmi },
	{ .id = 0x80862801, .name = "INTEL G45 DEVBLC", .patch = patch_intel_hdmi },
	{ .id = 0x80862802, .name = "INTEL G45 DEVCTG", .patch = patch_intel_hdmi },
	{ .id = 0x80862803, .name = "INTEL G45 DEVELK", .patch = patch_intel_hdmi },
	{ .id = 0x10951392, .name = "SiI1392 HDMI",     .patch = patch_intel_hdmi },
	{} /* terminator */
};
