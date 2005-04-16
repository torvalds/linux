/*
 * HD audio interface patch for AD1986A
 *
 * Copyright (c) 2005 Takashi Iwai <tiwai@suse.de>
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

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"

struct ad1986a_spec {
	struct semaphore amp_mutex;	/* PCM volume/mute control mutex */
	struct hda_multi_out multiout;	/* playback */
	unsigned int cur_mux;		/* capture source */
	struct hda_pcm pcm_rec[2];	/* PCM information */
};

#define AD1986A_SPDIF_OUT	0x02
#define AD1986A_FRONT_DAC	0x03
#define AD1986A_SURR_DAC	0x04
#define AD1986A_CLFE_DAC	0x05
#define AD1986A_ADC		0x06

static hda_nid_t ad1986a_dac_nids[3] = {
	AD1986A_FRONT_DAC, AD1986A_SURR_DAC, AD1986A_CLFE_DAC
};

static struct hda_input_mux ad1986a_capture_source = {
	.num_items = 7,
	.items = {
		{ "Mic", 0x0 },
		{ "CD", 0x1 },
		{ "Aux", 0x3 },
		{ "Line", 0x4 },
		{ "Mix", 0x5 },
		{ "Mono", 0x6 },
		{ "Phone", 0x7 },
	},
};

/*
 * PCM control
 *
 * bind volumes/mutes of 3 DACs as a single PCM control for simplicity
 */

#define ad1986a_pcm_amp_vol_info	snd_hda_mixer_amp_volume_info

static int ad1986a_pcm_amp_vol_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad1986a_spec *ad = codec->spec;

	down(&ad->amp_mutex);
	snd_hda_mixer_amp_volume_get(kcontrol, ucontrol);
	up(&ad->amp_mutex);
	return 0;
}

static int ad1986a_pcm_amp_vol_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad1986a_spec *ad = codec->spec;
	int i, change = 0;

	down(&ad->amp_mutex);
	for (i = 0; i < ARRAY_SIZE(ad1986a_dac_nids); i++) {
		kcontrol->private_value = HDA_COMPOSE_AMP_VAL(ad1986a_dac_nids[i], 3, 0, HDA_OUTPUT);
		change |= snd_hda_mixer_amp_volume_put(kcontrol, ucontrol);
	}
	kcontrol->private_value = HDA_COMPOSE_AMP_VAL(AD1986A_FRONT_DAC, 3, 0, HDA_OUTPUT);
	up(&ad->amp_mutex);
	return change;
}

#define ad1986a_pcm_amp_sw_info		snd_hda_mixer_amp_volume_info

static int ad1986a_pcm_amp_sw_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad1986a_spec *ad = codec->spec;

	down(&ad->amp_mutex);
	snd_hda_mixer_amp_switch_get(kcontrol, ucontrol);
	up(&ad->amp_mutex);
	return 0;
}

static int ad1986a_pcm_amp_sw_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad1986a_spec *ad = codec->spec;
	int i, change = 0;

	down(&ad->amp_mutex);
	for (i = 0; i < ARRAY_SIZE(ad1986a_dac_nids); i++) {
		kcontrol->private_value = HDA_COMPOSE_AMP_VAL(ad1986a_dac_nids[i], 3, 0, HDA_OUTPUT);
		change |= snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
	}
	kcontrol->private_value = HDA_COMPOSE_AMP_VAL(AD1986A_FRONT_DAC, 3, 0, HDA_OUTPUT);
	up(&ad->amp_mutex);
	return change;
}

/*
 * input MUX handling
 */
static int ad1986a_mux_enum_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	return snd_hda_input_mux_info(&ad1986a_capture_source, uinfo);
}

static int ad1986a_mux_enum_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad1986a_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_mux;
	return 0;
}

static int ad1986a_mux_enum_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad1986a_spec *spec = codec->spec;

	return snd_hda_input_mux_put(codec, &ad1986a_capture_source, ucontrol,
				     AD1986A_ADC, &spec->cur_mux);
}

/*
 * mixers
 */
static snd_kcontrol_new_t ad1986a_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Volume",
		.info = ad1986a_pcm_amp_vol_info,
		.get = ad1986a_pcm_amp_vol_get,
		.put = ad1986a_pcm_amp_vol_put,
		.private_value = HDA_COMPOSE_AMP_VAL(AD1986A_FRONT_DAC, 3, 0, HDA_OUTPUT)
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Switch",
		.info = ad1986a_pcm_amp_sw_info,
		.get = ad1986a_pcm_amp_sw_get,
		.put = ad1986a_pcm_amp_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(AD1986A_FRONT_DAC, 3, 0, HDA_OUTPUT)
	},
	HDA_CODEC_VOLUME("Front Playback Volume", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x1d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x1d, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x1d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x1d, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Aux Playback Volume", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Aux Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mono Playback Volume", 0x1e, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mono Playback Switch", 0x1e, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad1986a_mux_enum_info,
		.get = ad1986a_mux_enum_get,
		.put = ad1986a_mux_enum_put,
	},
	HDA_CODEC_MUTE("Stereo Downmix Switch", 0x09, 0x0, HDA_OUTPUT),
	{ } /* end */
};

/*
 * initialization verbs
 */
static struct hda_verb ad1986a_init_verbs[] = {
	/* Front, Surround, CLFE DAC; mute as default */
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* Downmix - off */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* HP, Line-Out, Surround, CLFE selectors */
	{0x0a, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x0c, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Mono selector */
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Mic selector: Mic 1/2 pin */
	{0x0f, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Line-in selector: Line-in */
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Mic 1/2 swap */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Record selector: mic */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Mic, Phone, CD, Aux, Line-In amp; mute as default */
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* PC beep */
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* HP, Line-Out, Surround, CLFE, Mono pins; mute as default */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{ } /* end */
};


static int ad1986a_init(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, ad1986a_init_verbs);
	return 0;
}

static int ad1986a_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_add_new_ctls(codec, ad1986a_mixers);
	if (err < 0)
		return err;
	err = snd_hda_create_spdif_out_ctls(codec, AD1986A_SPDIF_OUT);
	if (err < 0)
		return err;
	return 0;
}

/*
 * Analog playback callbacks
 */
static int ad1986a_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     snd_pcm_substream_t *substream)
{
	struct ad1986a_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream);
}

static int ad1986a_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					snd_pcm_substream_t *substream)
{
	struct ad1986a_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag,
						format, substream);
}

static int ad1986a_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					snd_pcm_substream_t *substream)
{
	struct ad1986a_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int ad1986a_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 snd_pcm_substream_t *substream)
{
	struct ad1986a_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int ad1986a_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  snd_pcm_substream_t *substream)
{
	struct ad1986a_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int ad1986a_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       snd_pcm_substream_t *substream)
{
	snd_hda_codec_setup_stream(codec, AD1986A_ADC, stream_tag, 0, format);
	return 0;
}

static int ad1986a_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       snd_pcm_substream_t *substream)
{
	snd_hda_codec_setup_stream(codec, AD1986A_ADC, 0, 0, 0);
	return 0;
}


/*
 */
static struct hda_pcm_stream ad1986a_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 6,
	.nid = AD1986A_FRONT_DAC, /* NID to query formats and rates */
	.ops = {
		.open = ad1986a_playback_pcm_open,
		.prepare = ad1986a_playback_pcm_prepare,
		.cleanup = ad1986a_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream ad1986a_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = AD1986A_ADC, /* NID to query formats and rates */
	.ops = {
		.prepare = ad1986a_capture_pcm_prepare,
		.cleanup = ad1986a_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream ad1986a_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = AD1986A_SPDIF_OUT, 
	.ops = {
		.open = ad1986a_dig_playback_pcm_open,
		.close = ad1986a_dig_playback_pcm_close
	},
};

static int ad1986a_build_pcms(struct hda_codec *codec)
{
	struct ad1986a_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 2;
	codec->pcm_info = info;

	info->name = "AD1986A Analog";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = ad1986a_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = ad1986a_pcm_analog_capture;
	info++;

	info->name = "AD1986A Digital";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = ad1986a_pcm_digital_playback;

	return 0;
}

static void ad1986a_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}

#ifdef CONFIG_PM
static int ad1986a_resume(struct hda_codec *codec)
{
	ad1986a_init(codec);
	snd_hda_resume_ctls(codec, ad1986a_mixers);
	snd_hda_resume_spdif_out(codec);
	return 0;
}
#endif

static struct hda_codec_ops ad1986a_patch_ops = {
	.build_controls = ad1986a_build_controls,
	.build_pcms = ad1986a_build_pcms,
	.init = ad1986a_init,
	.free = ad1986a_free,
#ifdef CONFIG_PM
	.resume = ad1986a_resume,
#endif
};

static int patch_ad1986a(struct hda_codec *codec)
{
	struct ad1986a_spec *spec;

	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	init_MUTEX(&spec->amp_mutex);
	codec->spec = spec;

	spec->multiout.max_channels = 6;
	spec->multiout.num_dacs = ARRAY_SIZE(ad1986a_dac_nids);
	spec->multiout.dac_nids = ad1986a_dac_nids;
	spec->multiout.dig_out_nid = AD1986A_SPDIF_OUT;

	codec->patch_ops = ad1986a_patch_ops;

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_analog[] = {
	{ .id = 0x11d41986, .name = "AD1986A", .patch = patch_ad1986a },
	{} /* terminator */
};
