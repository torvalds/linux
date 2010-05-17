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

#define MAX_HDMI_CVTS	1
#define MAX_HDMI_PINS	1

#include "patch_hdmi.c"

static char *nvhdmi_pcm_names[MAX_HDMI_CVTS] = {
	"NVIDIA HDMI",
};

/* define below to restrict the supported rates and formats */
/* #define LIMITED_RATE_FMT_SUPPORT */

enum HDACodec {
	HDA_CODEC_NVIDIA_MCP7X,
	HDA_CODEC_NVIDIA_MCP89,
	HDA_CODEC_NVIDIA_GT21X,
	HDA_CODEC_INVALID
};

#define Nv_VERB_SET_Channel_Allocation          0xF79
#define Nv_VERB_SET_Info_Frame_Checksum         0xF7A
#define Nv_VERB_SET_Audio_Protection_On         0xF98
#define Nv_VERB_SET_Audio_Protection_Off        0xF99

#define nvhdmi_master_con_nid_7x	0x04
#define nvhdmi_master_pin_nid_7x	0x05

#define nvhdmi_master_con_nid_89	0x04
#define nvhdmi_master_pin_nid_89	0x05

static hda_nid_t nvhdmi_con_nids_7x[4] = {
	/*front, rear, clfe, rear_surr */
	0x6, 0x8, 0xa, 0xc,
};

static struct hda_verb nvhdmi_basic_init_7x[] = {
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

#ifdef LIMITED_RATE_FMT_SUPPORT
/* support only the safe format and rate */
#define SUPPORTED_RATES		SNDRV_PCM_RATE_48000
#define SUPPORTED_MAXBPS	16
#define SUPPORTED_FORMATS	SNDRV_PCM_FMTBIT_S16_LE
#else
/* support all rates and formats */
#define SUPPORTED_RATES \
	(SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
	SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
	 SNDRV_PCM_RATE_192000)
#define SUPPORTED_MAXBPS	24
#define SUPPORTED_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE)
#endif

/*
 * Controls
 */
static int nvhdmi_build_controls(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int err;
	int i;

	if ((spec->codec_type == HDA_CODEC_NVIDIA_MCP89)
	|| (spec->codec_type == HDA_CODEC_NVIDIA_GT21X)) {
		for (i = 0; i < codec->num_pcms; i++) {
			err = snd_hda_create_spdif_out_ctls(codec,
							    spec->cvt[i]);
			if (err < 0)
				return err;
		}
	} else {
		err = snd_hda_create_spdif_out_ctls(codec,
						    spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
	}

	return 0;
}

static int nvhdmi_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int i;
	if ((spec->codec_type == HDA_CODEC_NVIDIA_MCP89)
	|| (spec->codec_type == HDA_CODEC_NVIDIA_GT21X)) {
		for (i = 0; spec->pin[i]; i++) {
			hdmi_enable_output(codec, spec->pin[i]);
			snd_hda_codec_write(codec, spec->pin[i], 0,
					    AC_VERB_SET_UNSOLICITED_ENABLE,
					    AC_USRSP_EN | spec->pin[i]);
		}
	} else {
		snd_hda_sequence_write(codec, nvhdmi_basic_init_7x);
	}
	return 0;
}

static void nvhdmi_free(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int i;

	if ((spec->codec_type == HDA_CODEC_NVIDIA_MCP89)
	|| (spec->codec_type == HDA_CODEC_NVIDIA_GT21X)) {
		for (i = 0; i < spec->num_pins; i++)
			snd_hda_eld_proc_free(codec, &spec->sink_eld[i]);
	}

	kfree(spec);
}

/*
 * Digital out
 */
static int nvhdmi_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int nvhdmi_dig_playback_pcm_close_8ch_7x(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	int i;

	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x,
			0, AC_VERB_SET_CHANNEL_STREAMID, 0);
	for (i = 0; i < 4; i++) {
		/* set the stream id */
		snd_hda_codec_write(codec, nvhdmi_con_nids_7x[i], 0,
				AC_VERB_SET_CHANNEL_STREAMID, 0);
		/* set the stream format */
		snd_hda_codec_write(codec, nvhdmi_con_nids_7x[i], 0,
				AC_VERB_SET_STREAM_FORMAT, 0);
	}

	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int nvhdmi_dig_playback_pcm_close_2ch(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int nvhdmi_dig_playback_pcm_prepare_8ch_89(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	hdmi_set_channel_count(codec, hinfo->nid,
			       substream->runtime->channels);

	hdmi_setup_audio_infoframe(codec, hinfo->nid, substream);

	hdmi_setup_stream(codec, hinfo->nid, stream_tag, format);
	return 0;
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
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				codec->spdif_ctls & ~AC_DIG1_ENABLE & 0xff);

	/* set the stream id */
	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x, 0,
			AC_VERB_SET_CHANNEL_STREAMID, (stream_tag << 4) | 0x0);

	/* set the stream format */
	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x, 0,
			AC_VERB_SET_STREAM_FORMAT, format);

	/* turn on again (if needed) */
	/* enable and set the channel status audio/data flag */
	if (codec->spdif_status_reset && (codec->spdif_ctls & AC_DIG1_ENABLE)) {
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				codec->spdif_ctls & 0xff);
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
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
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				codec->spdif_ctls & ~AC_DIG1_ENABLE & 0xff);
		/* set the stream id */
		snd_hda_codec_write(codec,
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_CHANNEL_STREAMID,
				(stream_tag << 4) | channel_id);
		/* set the stream format */
		snd_hda_codec_write(codec,
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_STREAM_FORMAT,
				format);
		/* turn on again (if needed) */
		/* enable and set the channel status audio/data flag */
		if (codec->spdif_status_reset &&
		(codec->spdif_ctls & AC_DIG1_ENABLE)) {
			snd_hda_codec_write(codec,
					nvhdmi_con_nids_7x[i],
					0,
					AC_VERB_SET_DIGI_CONVERT_1,
					codec->spdif_ctls & 0xff);
			snd_hda_codec_write(codec,
					nvhdmi_con_nids_7x[i],
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

static int nvhdmi_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   struct snd_pcm_substream *substream)
{
	return 0;
}

static int nvhdmi_dig_playback_pcm_prepare_2ch(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					format, substream);
}

static struct hda_pcm_stream nvhdmi_pcm_digital_playback_8ch_89 = {
	.substreams = 1,
	.channels_min = 2,
	.rates = SUPPORTED_RATES,
	.maxbps = SUPPORTED_MAXBPS,
	.formats = SUPPORTED_FORMATS,
	.ops = {
		.prepare = nvhdmi_dig_playback_pcm_prepare_8ch_89,
		.cleanup = nvhdmi_playback_pcm_cleanup,
	},
};

static struct hda_pcm_stream nvhdmi_pcm_digital_playback_8ch_7x = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = nvhdmi_master_con_nid_7x,
	.rates = SUPPORTED_RATES,
	.maxbps = SUPPORTED_MAXBPS,
	.formats = SUPPORTED_FORMATS,
	.ops = {
		.open = nvhdmi_dig_playback_pcm_open,
		.close = nvhdmi_dig_playback_pcm_close_8ch_7x,
		.prepare = nvhdmi_dig_playback_pcm_prepare_8ch
	},
};

static struct hda_pcm_stream nvhdmi_pcm_digital_playback_2ch = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = nvhdmi_master_con_nid_7x,
	.rates = SUPPORTED_RATES,
	.maxbps = SUPPORTED_MAXBPS,
	.formats = SUPPORTED_FORMATS,
	.ops = {
		.open = nvhdmi_dig_playback_pcm_open,
		.close = nvhdmi_dig_playback_pcm_close_2ch,
		.prepare = nvhdmi_dig_playback_pcm_prepare_2ch
	},
};

static int nvhdmi_build_pcms_8ch_89(struct hda_codec *codec)
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

		info->name = nvhdmi_pcm_names[i];
		info->pcm_type = HDA_PCM_TYPE_HDMI;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK]
					= nvhdmi_pcm_digital_playback_8ch_89;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->cvt[i];
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = chans;
	}

	return 0;
}

static int nvhdmi_build_pcms_8ch_7x(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "NVIDIA HDMI";
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK]
					= nvhdmi_pcm_digital_playback_8ch_7x;

	return 0;
}

static int nvhdmi_build_pcms_2ch(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "NVIDIA HDMI";
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK]
					= nvhdmi_pcm_digital_playback_2ch;

	return 0;
}

static struct hda_codec_ops nvhdmi_patch_ops_8ch_89 = {
	.build_controls = nvhdmi_build_controls,
	.build_pcms = nvhdmi_build_pcms_8ch_89,
	.init = nvhdmi_init,
	.free = nvhdmi_free,
	.unsol_event = hdmi_unsol_event,
};

static struct hda_codec_ops nvhdmi_patch_ops_8ch_7x = {
	.build_controls = nvhdmi_build_controls,
	.build_pcms = nvhdmi_build_pcms_8ch_7x,
	.init = nvhdmi_init,
	.free = nvhdmi_free,
};

static struct hda_codec_ops nvhdmi_patch_ops_2ch = {
	.build_controls = nvhdmi_build_controls,
	.build_pcms = nvhdmi_build_pcms_2ch,
	.init = nvhdmi_init,
	.free = nvhdmi_free,
};

static int patch_nvhdmi_8ch_89(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int i;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->codec_type = HDA_CODEC_NVIDIA_MCP89;

	if (hdmi_parse_codec(codec) < 0) {
		codec->spec = NULL;
		kfree(spec);
		return -EINVAL;
	}
	codec->patch_ops = nvhdmi_patch_ops_8ch_89;

	for (i = 0; i < spec->num_pins; i++)
		snd_hda_eld_proc_new(codec, &spec->sink_eld[i], i);

	init_channel_allocations();

	return 0;
}

static int patch_nvhdmi_8ch_7x(struct hda_codec *codec)
{
	struct hdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.num_dacs = 0;  /* no analog */
	spec->multiout.max_channels = 8;
	spec->multiout.dig_out_nid = nvhdmi_master_con_nid_7x;
	spec->codec_type = HDA_CODEC_NVIDIA_MCP7X;

	codec->patch_ops = nvhdmi_patch_ops_8ch_7x;

	return 0;
}

static int patch_nvhdmi_2ch(struct hda_codec *codec)
{
	struct hdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.num_dacs = 0;  /* no analog */
	spec->multiout.max_channels = 2;
	spec->multiout.dig_out_nid = nvhdmi_master_con_nid_7x;
	spec->codec_type = HDA_CODEC_NVIDIA_MCP7X;

	codec->patch_ops = nvhdmi_patch_ops_2ch;

	return 0;
}

/*
 * patch entries
 */
static struct hda_codec_preset snd_hda_preset_nvhdmi[] = {
	{ .id = 0x10de0002, .name = "MCP77/78 HDMI",
	  .patch = patch_nvhdmi_8ch_7x },
	{ .id = 0x10de0003, .name = "MCP77/78 HDMI",
	  .patch = patch_nvhdmi_8ch_7x },
	{ .id = 0x10de0005, .name = "MCP77/78 HDMI",
	  .patch = patch_nvhdmi_8ch_7x },
	{ .id = 0x10de0006, .name = "MCP77/78 HDMI",
	  .patch = patch_nvhdmi_8ch_7x },
	{ .id = 0x10de0007, .name = "MCP79/7A HDMI",
	  .patch = patch_nvhdmi_8ch_7x },
	{ .id = 0x10de000a, .name = "GT220 HDMI",
	  .patch = patch_nvhdmi_8ch_89 },
	{ .id = 0x10de000b, .name = "GT21x HDMI",
	  .patch = patch_nvhdmi_8ch_89 },
	{ .id = 0x10de000c, .name = "MCP89 HDMI",
	  .patch = patch_nvhdmi_8ch_89 },
	{ .id = 0x10de000d, .name = "GT240 HDMI",
	  .patch = patch_nvhdmi_8ch_89 },
	{ .id = 0x10de0067, .name = "MCP67 HDMI", .patch = patch_nvhdmi_2ch },
	{ .id = 0x10de8001, .name = "MCP73 HDMI", .patch = patch_nvhdmi_2ch },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:10de0002");
MODULE_ALIAS("snd-hda-codec-id:10de0003");
MODULE_ALIAS("snd-hda-codec-id:10de0005");
MODULE_ALIAS("snd-hda-codec-id:10de0006");
MODULE_ALIAS("snd-hda-codec-id:10de0007");
MODULE_ALIAS("snd-hda-codec-id:10de000a");
MODULE_ALIAS("snd-hda-codec-id:10de000b");
MODULE_ALIAS("snd-hda-codec-id:10de000c");
MODULE_ALIAS("snd-hda-codec-id:10de000d");
MODULE_ALIAS("snd-hda-codec-id:10de0067");
MODULE_ALIAS("snd-hda-codec-id:10de8001");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NVIDIA HDMI HD-audio codec");

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
