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

/*
 * The HDMI/DisplayPort configuration can be highly dynamic. A graphics device
 * could support two independent pipes, each of them can be connected to one or
 * more ports (DVI, HDMI or DisplayPort).
 *
 * The HDA correspondence of pipes/ports are converter/pin nodes.
 */
#define MAX_HDMI_CVTS	3
#define MAX_HDMI_PINS	3

#include "patch_hdmi.c"

static char *intel_hdmi_pcm_names[MAX_HDMI_CVTS] = {
	"INTEL HDMI 0",
	"INTEL HDMI 1",
	"INTEL HDMI 2",
};

/*
 * HDMI callbacks
 */

static int intel_hdmi_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	hdmi_set_channel_count(codec, hinfo->nid,
			       substream->runtime->channels);

	hdmi_setup_audio_infoframe(codec, hinfo->nid, substream);

	return hdmi_setup_stream(codec, hinfo->nid, stream_tag, format);
}

static struct hda_pcm_stream intel_hdmi_pcm_playback = {
	.substreams = 1,
	.channels_min = 2,
	.ops = {
		.open = hdmi_pcm_open,
		.prepare = intel_hdmi_playback_pcm_prepare,
	},
};

static int intel_hdmi_build_pcms(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;
	int i;

	codec->num_pcms = spec->num_cvts;
	codec->pcm_info = info;

	for (i = 0; i < codec->num_pcms; i++, info++) {
		unsigned int chans;

		chans = get_wcaps(codec, spec->cvt[i]);
		chans = get_wcaps_channels(chans);

		info->name = intel_hdmi_pcm_names[i];
		info->pcm_type = HDA_PCM_TYPE_HDMI;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
							intel_hdmi_pcm_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->cvt[i];
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = chans;
	}

	return 0;
}

static int intel_hdmi_build_controls(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int err;
	int i;

	for (i = 0; i < codec->num_pcms; i++) {
		err = snd_hda_create_spdif_out_ctls(codec, spec->cvt[i]);
		if (err < 0)
			return err;
	}

	return 0;
}

static int intel_hdmi_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int i;

	for (i = 0; spec->pin[i]; i++) {
		hdmi_enable_output(codec, spec->pin[i]);
		snd_hda_codec_write(codec, spec->pin[i], 0,
				    AC_VERB_SET_UNSOLICITED_ENABLE,
				    AC_USRSP_EN | spec->pin[i]);
	}
	return 0;
}

static void intel_hdmi_free(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->num_pins; i++)
		snd_hda_eld_proc_free(codec, &spec->sink_eld[i]);

	kfree(spec);
}

static struct hda_codec_ops intel_hdmi_patch_ops = {
	.init			= intel_hdmi_init,
	.free			= intel_hdmi_free,
	.build_pcms		= intel_hdmi_build_pcms,
	.build_controls 	= intel_hdmi_build_controls,
	.unsol_event		= hdmi_unsol_event,
};

static int patch_intel_hdmi(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int i;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	if (hdmi_parse_codec(codec) < 0) {
		codec->spec = NULL;
		kfree(spec);
		return -EINVAL;
	}
	codec->patch_ops = intel_hdmi_patch_ops;

	for (i = 0; i < spec->num_pins; i++)
		snd_hda_eld_proc_new(codec, &spec->sink_eld[i], i);

	init_channel_allocations();

	return 0;
}

static struct hda_codec_preset snd_hda_preset_intelhdmi[] = {
{ .id = 0x808629fb, .name = "Crestline HDMI",	.patch = patch_intel_hdmi },
{ .id = 0x80862801, .name = "Bearlake HDMI",	.patch = patch_intel_hdmi },
{ .id = 0x80862802, .name = "Cantiga HDMI",	.patch = patch_intel_hdmi },
{ .id = 0x80862803, .name = "Eaglelake HDMI",	.patch = patch_intel_hdmi },
{ .id = 0x80862804, .name = "IbexPeak HDMI",	.patch = patch_intel_hdmi },
{ .id = 0x80860054, .name = "IbexPeak HDMI",	.patch = patch_intel_hdmi },
{ .id = 0x80862805, .name = "CougarPoint HDMI",	.patch = patch_intel_hdmi },
{ .id = 0x10951392, .name = "SiI1392 HDMI",	.patch = patch_intel_hdmi },
{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:808629fb");
MODULE_ALIAS("snd-hda-codec-id:80862801");
MODULE_ALIAS("snd-hda-codec-id:80862802");
MODULE_ALIAS("snd-hda-codec-id:80862803");
MODULE_ALIAS("snd-hda-codec-id:80862804");
MODULE_ALIAS("snd-hda-codec-id:80862805");
MODULE_ALIAS("snd-hda-codec-id:80860054");
MODULE_ALIAS("snd-hda-codec-id:10951392");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel HDMI HD-audio codec");

static struct hda_codec_preset_list intel_list = {
	.preset = snd_hda_preset_intelhdmi,
	.owner = THIS_MODULE,
};

static int __init patch_intelhdmi_init(void)
{
	return snd_hda_add_codec_preset(&intel_list);
}

static void __exit patch_intelhdmi_exit(void)
{
	snd_hda_delete_codec_preset(&intel_list);
}

module_init(patch_intelhdmi_init)
module_exit(patch_intelhdmi_exit)
