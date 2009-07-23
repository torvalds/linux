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

#define Nv_VERB_SET_Channel_Allocation          0xF79
#define Nv_VERB_SET_Info_Frame_Checksum         0xF7A
#define Nv_VERB_SET_Audio_Protection_On         0xF98
#define Nv_VERB_SET_Audio_Protection_Off        0xF99

#define Nv_Master_Convert_nid   0x04
#define Nv_Master_Pin_nid       0x05

static hda_nid_t nvhdmi_convert_nids[4] = {
	/*front, rear, clfe, rear_surr */
	0x6, 0x8, 0xa, 0xc,
};

static struct hda_verb nvhdmi_basic_init[] = {
	/* set audio protect on */
	{ 0x1, Nv_VERB_SET_Audio_Protection_On, 0x1},
	/* enable digital output on pin widget */
	{ 0x5, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0x7, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0x9, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0xb, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0xd, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
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

static int nvhdmi_dig_playback_pcm_close_8ch(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct nvhdmi_spec *spec = codec->spec;
	int i;

	snd_hda_codec_write(codec, Nv_Master_Convert_nid,
			0, AC_VERB_SET_CHANNEL_STREAMID, 0);
	for (i = 0; i < 4; i++) {
		/* set the stream id */
		snd_hda_codec_write(codec, nvhdmi_convert_nids[i], 0,
				AC_VERB_SET_CHANNEL_STREAMID, 0);
		/* set the stream format */
		snd_hda_codec_write(codec, nvhdmi_convert_nids[i], 0,
				AC_VERB_SET_STREAM_FORMAT, 0);
	}

	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int nvhdmi_dig_playback_pcm_close_2ch(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct nvhdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int nvhdmi_dig_playback_pcm_prepare_8ch(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	int chs;
	unsigned int dataDCC1, dataDCC2, chan, chanmask, channel_id;
	int i;

	mutex_lock(&codec->spdif_mutex);

	chs = substream->runtime->channels;
	chan = chs ? (chs - 1) : 1;

	switch (chs) {
	default:
	case 0:
	case 2:
		chanmask = 0x00;
		break;
	case 4:
		chanmask = 0x08;
		break;
	case 6:
		chanmask = 0x0b;
		break;
	case 8:
		chanmask = 0x13;
		break;
	}
	dataDCC1 = AC_DIG1_ENABLE | AC_DIG1_COPYRIGHT;
	dataDCC2 = 0x2;

	/* set the Audio InforFrame Channel Allocation */
	snd_hda_codec_write(codec, 0x1, 0,
			Nv_VERB_SET_Channel_Allocation, chanmask);

	/* turn off SPDIF once; otherwise the IEC958 bits won't be updated */
	if (codec->spdif_status_reset && (codec->spdif_ctls & AC_DIG1_ENABLE))
		snd_hda_codec_write(codec,
				Nv_Master_Convert_nid,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				codec->spdif_ctls & ~AC_DIG1_ENABLE & 0xff);

	/* set the stream id */
	snd_hda_codec_write(codec, Nv_Master_Convert_nid, 0,
			AC_VERB_SET_CHANNEL_STREAMID, (stream_tag << 4) | 0x0);

	/* set the stream format */
	snd_hda_codec_write(codec, Nv_Master_Convert_nid, 0,
			AC_VERB_SET_STREAM_FORMAT, format);

	/* turn on again (if needed) */
	/* enable and set the channel status audio/data flag */
	if (codec->spdif_status_reset && (codec->spdif_ctls & AC_DIG1_ENABLE)) {
		snd_hda_codec_write(codec,
				Nv_Master_Convert_nid,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				codec->spdif_ctls & 0xff);
		snd_hda_codec_write(codec,
				Nv_Master_Convert_nid,
				0,
				AC_VERB_SET_DIGI_CONVERT_2, dataDCC2);
	}

	for (i = 0; i < 4; i++) {
		if (chs == 2)
			channel_id = 0;
		else
			channel_id = i * 2;

		/* turn off SPDIF once;
		 *otherwise the IEC958 bits won't be updated
		 */
		if (codec->spdif_status_reset &&
		(codec->spdif_ctls & AC_DIG1_ENABLE))
			snd_hda_codec_write(codec,
				nvhdmi_convert_nids[i],
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				codec->spdif_ctls & ~AC_DIG1_ENABLE & 0xff);
		/* set the stream id */
		snd_hda_codec_write(codec,
				nvhdmi_convert_nids[i],
				0,
				AC_VERB_SET_CHANNEL_STREAMID,
				(stream_tag << 4) | channel_id);
		/* set the stream format */
		snd_hda_codec_write(codec,
				nvhdmi_convert_nids[i],
				0,
				AC_VERB_SET_STREAM_FORMAT,
				format);
		/* turn on again (if needed) */
		/* enable and set the channel status audio/data flag */
		if (codec->spdif_status_reset &&
		(codec->spdif_ctls & AC_DIG1_ENABLE)) {
			snd_hda_codec_write(codec,
					nvhdmi_convert_nids[i],
					0,
					AC_VERB_SET_DIGI_CONVERT_1,
					codec->spdif_ctls & 0xff);
			snd_hda_codec_write(codec,
					nvhdmi_convert_nids[i],
					0,
					AC_VERB_SET_DIGI_CONVERT_2, dataDCC2);
		}
	}

	/* set the Audio Info Frame Checksum */
	snd_hda_codec_write(codec, 0x1, 0,
			Nv_VERB_SET_Info_Frame_Checksum,
			(0x71 - chan - chanmask));

	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

static int nvhdmi_dig_playback_pcm_prepare_2ch(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	struct nvhdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					format, substream);
}

static struct hda_pcm_stream nvhdmi_pcm_digital_playback_8ch = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = Nv_Master_Convert_nid,
	.rates = SNDRV_PCM_RATE_48000,
	.maxbps = 16,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.ops = {
		.open = nvhdmi_dig_playback_pcm_open,
		.close = nvhdmi_dig_playback_pcm_close_8ch,
		.prepare = nvhdmi_dig_playback_pcm_prepare_8ch
	},
};

static struct hda_pcm_stream nvhdmi_pcm_digital_playback_2ch = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = Nv_Master_Convert_nid,
	.rates = SNDRV_PCM_RATE_48000,
	.maxbps = 16,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.ops = {
		.open = nvhdmi_dig_playback_pcm_open,
		.close = nvhdmi_dig_playback_pcm_close_2ch,
		.prepare = nvhdmi_dig_playback_pcm_prepare_2ch
	},
};

static int nvhdmi_build_pcms_8ch(struct hda_codec *codec)
{
	struct nvhdmi_spec *spec = codec->spec;
	struct hda_pcm *info = &spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "NVIDIA HDMI";
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK]
					= nvhdmi_pcm_digital_playback_8ch;

	return 0;
}

static int nvhdmi_build_pcms_2ch(struct hda_codec *codec)
{
	struct nvhdmi_spec *spec = codec->spec;
	struct hda_pcm *info = &spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "NVIDIA HDMI";
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK]
					= nvhdmi_pcm_digital_playback_2ch;

	return 0;
}

static void nvhdmi_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}

static struct hda_codec_ops nvhdmi_patch_ops_8ch = {
	.build_controls = nvhdmi_build_controls,
	.build_pcms = nvhdmi_build_pcms_8ch,
	.init = nvhdmi_init,
	.free = nvhdmi_free,
};

static struct hda_codec_ops nvhdmi_patch_ops_2ch = {
	.build_controls = nvhdmi_build_controls,
	.build_pcms = nvhdmi_build_pcms_2ch,
	.init = nvhdmi_init,
	.free = nvhdmi_free,
};

static int patch_nvhdmi_8ch(struct hda_codec *codec)
{
	struct nvhdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.num_dacs = 0;  /* no analog */
	spec->multiout.max_channels = 8;
	spec->multiout.dig_out_nid = Nv_Master_Convert_nid;

	codec->patch_ops = nvhdmi_patch_ops_8ch;

	return 0;
}

static int patch_nvhdmi_2ch(struct hda_codec *codec)
{
	struct nvhdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.num_dacs = 0;  /* no analog */
	spec->multiout.max_channels = 2;
	spec->multiout.dig_out_nid = Nv_Master_Convert_nid;

	codec->patch_ops = nvhdmi_patch_ops_2ch;

	return 0;
}

/*
 * patch entries
 */
static struct hda_codec_preset snd_hda_preset_nvhdmi[] = {
	{ .id = 0x10de0002, .name = "MCP78 HDMI", .patch = patch_nvhdmi_8ch },
	{ .id = 0x10de0006, .name = "MCP78 HDMI", .patch = patch_nvhdmi_8ch },
	{ .id = 0x10de0007, .name = "MCP7A HDMI", .patch = patch_nvhdmi_8ch },
	{ .id = 0x10de0067, .name = "MCP67 HDMI", .patch = patch_nvhdmi_2ch },
	{ .id = 0x10de8001, .name = "MCP73 HDMI", .patch = patch_nvhdmi_2ch },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:10de0002");
MODULE_ALIAS("snd-hda-codec-id:10de0006");
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
