// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Legacy Nvidia HDMI codec support
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/hdaudio.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hdmi_local.h"

#define Nv_VERB_SET_Channel_Allocation          0xF79
#define Nv_VERB_SET_Info_Frame_Checksum         0xF7A
#define Nv_VERB_SET_Audio_Protection_On         0xF98
#define Nv_VERB_SET_Audio_Protection_Off        0xF99

#define nvhdmi_master_con_nid_7x	0x04
#define nvhdmi_master_pin_nid_7x	0x05

static const hda_nid_t nvhdmi_con_nids_7x[4] = {
	/*front, rear, clfe, rear_surr */
	0x6, 0x8, 0xa, 0xc,
};

static const struct hda_verb nvhdmi_basic_init_7x_2ch[] = {
	/* set audio protect on */
	{ 0x1, Nv_VERB_SET_Audio_Protection_On, 0x1},
	/* enable digital output on pin widget */
	{ 0x5, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{} /* terminator */
};

static const struct hda_verb nvhdmi_basic_init_7x_8ch[] = {
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

static int nvhdmi_7x_init_2ch(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, nvhdmi_basic_init_7x_2ch);
	return 0;
}

static int nvhdmi_7x_init_8ch(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, nvhdmi_basic_init_7x_8ch);
	return 0;
}

static void nvhdmi_8ch_7x_set_info_frame_parameters(struct hda_codec *codec,
						    int channels)
{
	unsigned int chanmask;
	int chan = channels ? (channels - 1) : 1;

	switch (channels) {
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

	/* Set the audio infoframe channel allocation and checksum fields.  The
	 * channel count is computed implicitly by the hardware.
	 */
	snd_hda_codec_write(codec, 0x1, 0,
			Nv_VERB_SET_Channel_Allocation, chanmask);

	snd_hda_codec_write(codec, 0x1, 0,
			Nv_VERB_SET_Info_Frame_Checksum,
			(0x71 - chan - chanmask));
}

static int nvhdmi_8ch_7x_pcm_close(struct hda_pcm_stream *hinfo,
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

	/* The audio hardware sends a channel count of 0x7 (8ch) when all the
	 * streams are disabled.
	 */
	nvhdmi_8ch_7x_set_info_frame_parameters(codec, 8);

	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int nvhdmi_8ch_7x_pcm_prepare(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream)
{
	int chs;
	unsigned int dataDCC2, channel_id;
	int i;
	struct hdmi_spec *spec = codec->spec;
	struct hda_spdif_out *spdif;
	struct hdmi_spec_per_cvt *per_cvt;

	mutex_lock(&codec->spdif_mutex);
	per_cvt = get_cvt(spec, 0);
	spdif = snd_hda_spdif_out_of_nid(codec, per_cvt->cvt_nid);

	chs = substream->runtime->channels;

	dataDCC2 = 0x2;

	/* turn off SPDIF once; otherwise the IEC958 bits won't be updated */
	if (codec->spdif_status_reset && (spdif->ctls & AC_DIG1_ENABLE))
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				spdif->ctls & ~AC_DIG1_ENABLE & 0xff);

	/* set the stream id */
	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x, 0,
			AC_VERB_SET_CHANNEL_STREAMID, (stream_tag << 4) | 0x0);

	/* set the stream format */
	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x, 0,
			AC_VERB_SET_STREAM_FORMAT, format);

	/* turn on again (if needed) */
	/* enable and set the channel status audio/data flag */
	if (codec->spdif_status_reset && (spdif->ctls & AC_DIG1_ENABLE)) {
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				spdif->ctls & 0xff);
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
		(spdif->ctls & AC_DIG1_ENABLE))
			snd_hda_codec_write(codec,
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				spdif->ctls & ~AC_DIG1_ENABLE & 0xff);
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
		(spdif->ctls & AC_DIG1_ENABLE)) {
			snd_hda_codec_write(codec,
					nvhdmi_con_nids_7x[i],
					0,
					AC_VERB_SET_DIGI_CONVERT_1,
					spdif->ctls & 0xff);
			snd_hda_codec_write(codec,
					nvhdmi_con_nids_7x[i],
					0,
					AC_VERB_SET_DIGI_CONVERT_2, dataDCC2);
		}
	}

	nvhdmi_8ch_7x_set_info_frame_parameters(codec, chs);

	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

static const struct hda_pcm_stream nvhdmi_pcm_playback_8ch_7x = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = nvhdmi_master_con_nid_7x,
	.rates = SUPPORTED_RATES,
	.maxbps = SUPPORTED_MAXBPS,
	.formats = SUPPORTED_FORMATS,
	.ops = {
		.open = snd_hda_hdmi_simple_pcm_open,
		.close = nvhdmi_8ch_7x_pcm_close,
		.prepare = nvhdmi_8ch_7x_pcm_prepare
	},
};

static int patch_nvhdmi_2ch(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err = patch_simple_hdmi(codec, nvhdmi_master_con_nid_7x,
				    nvhdmi_master_pin_nid_7x);
	if (err < 0)
		return err;

	codec->patch_ops.init = nvhdmi_7x_init_2ch;
	/* override the PCM rates, etc, as the codec doesn't give full list */
	spec = codec->spec;
	spec->pcm_playback.rates = SUPPORTED_RATES;
	spec->pcm_playback.maxbps = SUPPORTED_MAXBPS;
	spec->pcm_playback.formats = SUPPORTED_FORMATS;
	spec->nv_dp_workaround = true;
	return 0;
}

static int nvhdmi_7x_8ch_build_pcms(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int err;

	err = snd_hda_hdmi_simple_build_pcms(codec);
	if (!err) {
		struct hda_pcm *info = get_pcm_rec(spec, 0);

		info->own_chmap = true;
	}
	return err;
}

static int nvhdmi_7x_8ch_build_controls(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hda_pcm *info;
	struct snd_pcm_chmap *chmap;
	int err;

	err = snd_hda_hdmi_simple_build_controls(codec);
	if (err < 0)
		return err;

	/* add channel maps */
	info = get_pcm_rec(spec, 0);
	err = snd_pcm_add_chmap_ctls(info->pcm,
				     SNDRV_PCM_STREAM_PLAYBACK,
				     snd_pcm_alt_chmaps, 8, 0, &chmap);
	if (err < 0)
		return err;
	switch (codec->preset->vendor_id) {
	case 0x10de0002:
	case 0x10de0003:
	case 0x10de0005:
	case 0x10de0006:
		chmap->channel_mask = (1U << 2) | (1U << 8);
		break;
	case 0x10de0007:
		chmap->channel_mask = (1U << 2) | (1U << 6) | (1U << 8);
	}
	return 0;
}

static const unsigned int channels_2_6_8[] = {
	2, 6, 8
};

static const unsigned int channels_2_8[] = {
	2, 8
};

static const struct snd_pcm_hw_constraint_list hw_constraints_2_6_8_channels = {
	.count = ARRAY_SIZE(channels_2_6_8),
	.list = channels_2_6_8,
	.mask = 0,
};

static const struct snd_pcm_hw_constraint_list hw_constraints_2_8_channels = {
	.count = ARRAY_SIZE(channels_2_8),
	.list = channels_2_8,
	.mask = 0,
};

static int patch_nvhdmi_8ch_7x(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err;

	err = patch_nvhdmi_2ch(codec);
	if (err < 0)
		return err;
	spec = codec->spec;
	spec->multiout.max_channels = 8;
	spec->pcm_playback = nvhdmi_pcm_playback_8ch_7x;
	codec->patch_ops.init = nvhdmi_7x_init_8ch;
	codec->patch_ops.build_pcms = nvhdmi_7x_8ch_build_pcms;
	codec->patch_ops.build_controls = nvhdmi_7x_8ch_build_controls;

	switch (codec->preset->vendor_id) {
	case 0x10de0002:
	case 0x10de0003:
	case 0x10de0005:
	case 0x10de0006:
		spec->hw_constraints_channels = &hw_constraints_2_8_channels;
		break;
	case 0x10de0007:
		spec->hw_constraints_channels = &hw_constraints_2_6_8_channels;
		break;
	default:
		break;
	}

	/* Initialize the audio infoframe channel mask and checksum to something
	 * valid
	 */
	nvhdmi_8ch_7x_set_info_frame_parameters(codec, 8);

	return 0;
}

/*
 * patch entries
 */
static const struct hda_device_id snd_hda_id_nvhdmi_mcp[] = {
HDA_CODEC_ENTRY(0x10de0001, "MCP73 HDMI",	patch_nvhdmi_2ch),
HDA_CODEC_ENTRY(0x10de0002, "MCP77/78 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0003, "MCP77/78 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0004, "GPU 04 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0005, "MCP77/78 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0006, "MCP77/78 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0007, "MCP79/7A HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0067, "MCP67 HDMI",	patch_nvhdmi_2ch),
HDA_CODEC_ENTRY(0x10de8001, "MCP73 HDMI",	patch_nvhdmi_2ch),
HDA_CODEC_ENTRY(0x10de8067, "MCP67/68 HDMI",	patch_nvhdmi_2ch),
{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_nvhdmi_mcp);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Legacy Nvidia HDMI HD-audio codec");
MODULE_IMPORT_NS("SND_HDA_CODEC_HDMI");

static struct hda_codec_driver nvhdmi_mcp_driver = {
	.id = snd_hda_id_nvhdmi_mcp,
};

module_hda_codec_driver(nvhdmi_mcp_driver);
