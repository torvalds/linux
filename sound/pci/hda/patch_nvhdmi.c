/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for NVIDIA HDMI codecs
 *
 * Copyright (c) 2008 NVIDIA Corp.  All rights reserved.
 * Copyright (c) 2008 Wei Ni <wni@nvidia.com>
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

struct nvhdmi_spec {
	struct hda_multi_out multiout;

	struct hda_pcm pcm_rec;
};

static struct hda_verb nvhdmi_basic_init[] = {
	/* enable digital output on pin widget */
	{ 0x05, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{} /* terminator */
};

/*
 * Controls
 */
static int nvhdmi_build_controls(struct hda_codec *codec)
{
	struct nvhdmi_spec *spec = codec->spec;
	int err;

	err = snd_hda_create_spdif_out_ctls(codec, spec->multiout.dig_out_nid);
	if (err < 0)
		return err;

	return 0;
}

static int nvhdmi_init(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, nvhdmi_basic_init);
	return 0;
}

/*
 * Digital out
 */
static int nvhdmi_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct nvhdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int nvhdmi_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct nvhdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int nvhdmi_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					    struct hda_codec *codec,
					    unsigned int stream_tag,
					    unsigned int format,
					    struct snd_pcm_substream *substream)
{
	struct nvhdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					     format, substream);
}

static struct hda_pcm_stream nvhdmi_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x4, /* NID to query formats and rates and setup streams */
	.rates = SNDRV_PCM_RATE_48000,
	.maxbps = 16,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.ops = {
		.open = nvhdmi_dig_playback_pcm_open,
		.close = nvhdmi_dig_playback_pcm_close,
		.prepare = nvhdmi_dig_playback_pcm_prepare
	},
};

static int nvhdmi_build_pcms(struct hda_codec *codec)
{
	struct nvhdmi_spec *spec = codec->spec;
	struct hda_pcm *info = &spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "NVIDIA HDMI";
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = nvhdmi_pcm_digital_playback;

	return 0;
}

static void nvhdmi_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}

static struct hda_codec_ops nvhdmi_patch_ops = {
	.build_controls = nvhdmi_build_controls,
	.build_pcms = nvhdmi_build_pcms,
	.init = nvhdmi_init,
	.free = nvhdmi_free,
};

static int patch_nvhdmi(struct hda_codec *codec)
{
	struct nvhdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.num_dacs = 0;	  /* no analog */
	spec->multiout.max_channels = 2;
	spec->multiout.dig_out_nid = 0x4; /* NID for copying analog to digital,
					   * seems to be unused in pure-digital
					   * case. */

	codec->patch_ops = nvhdmi_patch_ops;

	return 0;
}

/*
 * patch entries
 */
static struct hda_codec_preset snd_hda_preset_nvhdmi[] = {
	{ .id = 0x10de0002, .name = "MCP78 HDMI", .patch = patch_nvhdmi },
	{ .id = 0x10de0007, .name = "MCP7A HDMI", .patch = patch_nvhdmi },
	{ .id = 0x10de0067, .name = "MCP67 HDMI", .patch = patch_nvhdmi },
	{ .id = 0x10de8001, .name = "MCP73 HDMI", .patch = patch_nvhdmi },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:10de0002");
MODULE_ALIAS("snd-hda-codec-id:10de0007");
MODULE_ALIAS("snd-hda-codec-id:10de0067");
MODULE_ALIAS("snd-hda-codec-id:10de8001");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Nvidia HDMI HD-audio codec");

static struct hda_codec_preset_list nvhdmi_list = {
	.preset = snd_hda_preset_nvhdmi,
	.owner = THIS_MODULE,
};

static int __init patch_nvhdmi_init(void)
{
	return snd_hda_add_codec_preset(&nvhdmi_list);
}

static void __exit patch_nvhdmi_exit(void)
{
	snd_hda_delete_codec_preset(&nvhdmi_list);
}

module_init(patch_nvhdmi_init)
module_exit(patch_nvhdmi_exit)
