/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for ATI HDMI codecs
 *
 * Copyright (c) 2006 ATI Technologies Inc.
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
#include "hda_patch.h"

struct atihdmi_spec {
	struct hda_multi_out multiout;

	struct hda_pcm pcm_rec;
};

#define CVT_NID		0x02	/* audio converter */
#define PIN_NID		0x03	/* HDMI output pin */

static struct hda_verb atihdmi_basic_init[] = {
	/* enable digital output on pin widget */
	{ 0x03, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{} /* terminator */
};

/*
 * Controls
 */
static int atihdmi_build_controls(struct hda_codec *codec)
{
	struct atihdmi_spec *spec = codec->spec;
	int err;

	err = snd_hda_create_spdif_out_ctls(codec, spec->multiout.dig_out_nid);
	if (err < 0)
		return err;

	return 0;
}

static int atihdmi_init(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, atihdmi_basic_init);
	/* SI codec requires to unmute the pin */
	if (get_wcaps(codec, PIN_NID) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, PIN_NID, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_OUT_UNMUTE);
	return 0;
}

/*
 * Digital out
 */
static int atihdmi_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct atihdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int atihdmi_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct atihdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int atihdmi_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					    struct hda_codec *codec,
					    unsigned int stream_tag,
					    unsigned int format,
					    struct snd_pcm_substream *substream)
{
	struct atihdmi_spec *spec = codec->spec;
	int chans = substream->runtime->channels;
	int i, err;

	err = snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					    format, substream);
	if (err < 0)
		return err;
	snd_hda_codec_write(codec, CVT_NID, 0, AC_VERB_SET_CVT_CHAN_COUNT,
			    chans - 1);
	/* FIXME: XXX */
	for (i = 0; i < chans; i++) {
		snd_hda_codec_write(codec, CVT_NID, 0,
				    AC_VERB_SET_HDMI_CHAN_SLOT,
				    (i << 4) | i);
	}
	return 0;
}

static struct hda_pcm_stream atihdmi_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = CVT_NID, /* NID to query formats and rates and setup streams */
	.ops = {
		.open = atihdmi_dig_playback_pcm_open,
		.close = atihdmi_dig_playback_pcm_close,
		.prepare = atihdmi_dig_playback_pcm_prepare
	},
};

static int atihdmi_build_pcms(struct hda_codec *codec)
{
	struct atihdmi_spec *spec = codec->spec;
	struct hda_pcm *info = &spec->pcm_rec;
	unsigned int chans;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "ATI HDMI";
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = atihdmi_pcm_digital_playback;

	/* FIXME: we must check ELD and change the PCM parameters dynamically
	 */
	chans = get_wcaps(codec, CVT_NID);
	chans = (chans & AC_WCAP_CHAN_CNT_EXT) >> 13;
	chans = ((chans << 1) | 1) + 1;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = chans;

	return 0;
}

static void atihdmi_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}

static struct hda_codec_ops atihdmi_patch_ops = {
	.build_controls = atihdmi_build_controls,
	.build_pcms = atihdmi_build_pcms,
	.init = atihdmi_init,
	.free = atihdmi_free,
};

static int patch_atihdmi(struct hda_codec *codec)
{
	struct atihdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.num_dacs = 0;	  /* no analog */
	spec->multiout.max_channels = 2;
	/* NID for copying analog to digital,
	 * seems to be unused in pure-digital
	 * case.
	 */
	spec->multiout.dig_out_nid = CVT_NID;

	codec->patch_ops = atihdmi_patch_ops;

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_atihdmi[] = {
	{ .id = 0x1002793c, .name = "ATI RS600 HDMI", .patch = patch_atihdmi },
	{ .id = 0x10027919, .name = "ATI RS600 HDMI", .patch = patch_atihdmi },
	{ .id = 0x1002791a, .name = "ATI RS690/780 HDMI", .patch = patch_atihdmi },
	{ .id = 0x1002aa01, .name = "ATI R6xx HDMI", .patch = patch_atihdmi },
	{ .id = 0x10951390, .name = "SiI1390 HDMI", .patch = patch_atihdmi },
	{ .id = 0x10951392, .name = "SiI1392 HDMI", .patch = patch_atihdmi },
	{ .id = 0x17e80047, .name = "Chrontel HDMI",  .patch = patch_atihdmi },
	{} /* terminator */
};
