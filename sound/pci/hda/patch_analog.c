/*
 * HD audio interface patch for AD1882, AD1884, AD1981HD, AD1983, AD1984,
 *   AD1986A, AD1988
 *
 * Copyright (c) 2005-2007 Takashi Iwai <tiwai@suse.de>
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
#include <linux/mutex.h>

#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"

struct ad198x_spec {
	struct snd_kcontrol_new *mixers[5];
	int num_mixers;

	const struct hda_verb *init_verbs[5];	/* initialization verbs
						 * don't forget NULL termination!
						 */
	unsigned int num_init_verbs;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	unsigned int cur_eapd;
	unsigned int need_dac_fix;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */

	/* capture source */
	const struct hda_input_mux *input_mux;
	hda_nid_t *capsrc_nids;
	unsigned int cur_mux[3];

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;

	/* PCM information */
	struct hda_pcm pcm_rec[3];	/* used in alc_build_pcms() */

	struct mutex amp_mutex;	/* PCM volume/mute control mutex */
	unsigned int spdif_route;

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	struct snd_kcontrol_new *kctl_alloc;
	struct hda_input_mux private_imux;
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];

	unsigned int jack_present :1;

#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_loopback_check loopback;
#endif
};

/*
 * input MUX handling (common part)
 */
static int ad198x_mux_enum_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;

	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int ad198x_mux_enum_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int ad198x_mux_enum_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->capsrc_nids[adc_idx],
				     &spec->cur_mux[adc_idx]);
}

/*
 * initialization (common callbacks)
 */
static int ad198x_init(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->num_init_verbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);
	return 0;
}

static int ad198x_build_controls(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	unsigned int i;
	int err;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}
	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec, spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
	} 
	if (spec->dig_in_nid) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in_nid);
		if (err < 0)
			return err;
	}
	return 0;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static int ad198x_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
}
#endif

/*
 * Analog playback callbacks
 */
static int ad198x_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream);
}

static int ad198x_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag,
						format, substream);
}

static int ad198x_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int ad198x_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int ad198x_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int ad198x_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					     format, substream);
}

/*
 * Analog capture
 */
static int ad198x_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int ad198x_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   0, 0, 0);
	return 0;
}


/*
 */
static struct hda_pcm_stream ad198x_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 6, /* changed later */
	.nid = 0, /* fill later */
	.ops = {
		.open = ad198x_playback_pcm_open,
		.prepare = ad198x_playback_pcm_prepare,
		.cleanup = ad198x_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream ad198x_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.prepare = ad198x_capture_pcm_prepare,
		.cleanup = ad198x_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream ad198x_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.open = ad198x_dig_playback_pcm_open,
		.close = ad198x_dig_playback_pcm_close,
		.prepare = ad198x_dig_playback_pcm_prepare
	},
};

static struct hda_pcm_stream ad198x_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static int ad198x_build_pcms(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "AD198x Analog";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = ad198x_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = spec->multiout.max_channels;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dac_nids[0];
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = ad198x_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = spec->num_adc_nids;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];

	if (spec->multiout.dig_out_nid) {
		info++;
		codec->num_pcms++;
		info->name = "AD198x Digital";
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] = ad198x_pcm_digital_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = ad198x_pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
	}

	return 0;
}

static void ad198x_free(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	unsigned int i;

	if (spec->kctl_alloc) {
		for (i = 0; i < spec->num_kctl_used; i++)
			kfree(spec->kctl_alloc[i].name);
		kfree(spec->kctl_alloc);
	}
	kfree(codec->spec);
}

static struct hda_codec_ops ad198x_patch_ops = {
	.build_controls = ad198x_build_controls,
	.build_pcms = ad198x_build_pcms,
	.init = ad198x_init,
	.free = ad198x_free,
#ifdef CONFIG_SND_HDA_POWER_SAVE
	.check_power_status = ad198x_check_power_status,
#endif
};


/*
 * EAPD control
 * the private value = nid | (invert << 8)
 */
#define ad198x_eapd_info	snd_ctl_boolean_mono_info

static int ad198x_eapd_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	int invert = (kcontrol->private_value >> 8) & 1;
	if (invert)
		ucontrol->value.integer.value[0] = ! spec->cur_eapd;
	else
		ucontrol->value.integer.value[0] = spec->cur_eapd;
	return 0;
}

static int ad198x_eapd_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	int invert = (kcontrol->private_value >> 8) & 1;
	hda_nid_t nid = kcontrol->private_value & 0xff;
	unsigned int eapd;
	eapd = ucontrol->value.integer.value[0];
	if (invert)
		eapd = !eapd;
	if (eapd == spec->cur_eapd)
		return 0;
	spec->cur_eapd = eapd;
	snd_hda_codec_write_cache(codec, nid,
				  0, AC_VERB_SET_EAPD_BTLENABLE,
				  eapd ? 0x02 : 0x00);
	return 1;
}

static int ad198x_ch_mode_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo);
static int ad198x_ch_mode_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);
static int ad198x_ch_mode_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);


/*
 * AD1986A specific
 */

#define AD1986A_SPDIF_OUT	0x02
#define AD1986A_FRONT_DAC	0x03
#define AD1986A_SURR_DAC	0x04
#define AD1986A_CLFE_DAC	0x05
#define AD1986A_ADC		0x06

static hda_nid_t ad1986a_dac_nids[3] = {
	AD1986A_FRONT_DAC, AD1986A_SURR_DAC, AD1986A_CLFE_DAC
};
static hda_nid_t ad1986a_adc_nids[1] = { AD1986A_ADC };
static hda_nid_t ad1986a_capsrc_nids[1] = { 0x12 };

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


static struct hda_bind_ctls ad1986a_bind_pcm_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(AD1986A_FRONT_DAC, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(AD1986A_SURR_DAC, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(AD1986A_CLFE_DAC, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct hda_bind_ctls ad1986a_bind_pcm_sw = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(AD1986A_FRONT_DAC, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(AD1986A_SURR_DAC, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(AD1986A_CLFE_DAC, 3, 0, HDA_OUTPUT),
		0
	},
};

/*
 * mixers
 */
static struct snd_kcontrol_new ad1986a_mixers[] = {
	/*
	 * bind volumes/mutes of 3 DACs as a single PCM control for simplicity
	 */
	HDA_BIND_VOL("PCM Playback Volume", &ad1986a_bind_pcm_vol),
	HDA_BIND_SW("PCM Playback Switch", &ad1986a_bind_pcm_sw),
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
	HDA_CODEC_VOLUME("Mic Boost", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mono Playback Volume", 0x1e, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mono Playback Switch", 0x1e, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	HDA_CODEC_MUTE("Stereo Downmix Switch", 0x09, 0x0, HDA_OUTPUT),
	{ } /* end */
};

/* additional mixers for 3stack mode */
static struct snd_kcontrol_new ad1986a_3st_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = ad198x_ch_mode_info,
		.get = ad198x_ch_mode_get,
		.put = ad198x_ch_mode_put,
	},
	{ } /* end */
};

/* laptop model - 2ch only */
static hda_nid_t ad1986a_laptop_dac_nids[1] = { AD1986A_FRONT_DAC };

/* master controls both pins 0x1a and 0x1b */
static struct hda_bind_ctls ad1986a_laptop_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x1a, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
		0,
	},
};

static struct hda_bind_ctls ad1986a_laptop_master_sw = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x1a, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
		0,
	},
};

static struct snd_kcontrol_new ad1986a_laptop_mixers[] = {
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_VOL("Master Playback Volume", &ad1986a_laptop_master_vol),
	HDA_BIND_SW("Master Playback Switch", &ad1986a_laptop_master_sw),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Aux Playback Volume", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Aux Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x0f, 0x0, HDA_OUTPUT),
	/* HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x18, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x18, 0x0, HDA_OUTPUT),
	   HDA_CODEC_VOLUME("Mono Playback Volume", 0x1e, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Mono Playback Switch", 0x1e, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	{ } /* end */
};

/* laptop-eapd model - 2ch only */

static struct hda_input_mux ad1986a_laptop_eapd_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x4 },
		{ "Mix", 0x5 },
	},
};

static struct snd_kcontrol_new ad1986a_laptop_eapd_mixers[] = {
	HDA_BIND_VOL("Master Playback Volume", &ad1986a_laptop_master_vol),
	HDA_BIND_SW("Master Playback Switch", &ad1986a_laptop_master_sw),
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "External Amplifier",
		.info = ad198x_eapd_info,
		.get = ad198x_eapd_get,
		.put = ad198x_eapd_put,
		.private_value = 0x1b | (1 << 8), /* port-D, inversed */
	},
	{ } /* end */
};

/* laptop-automute - 2ch only */

static void ad1986a_update_hp(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	unsigned int mute;

	if (spec->jack_present)
		mute = HDA_AMP_MUTE; /* mute internal speaker */
	else
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x1a, 0, HDA_OUTPUT, 0);
	snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
}

static void ad1986a_hp_automute(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x1a, 0, AC_VERB_GET_PIN_SENSE, 0);
	/* Lenovo N100 seems to report the reversed bit for HP jack-sensing */
	spec->jack_present = !(present & 0x80000000);
	ad1986a_update_hp(codec);
}

#define AD1986A_HP_EVENT		0x37

static void ad1986a_hp_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) != AD1986A_HP_EVENT)
		return;
	ad1986a_hp_automute(codec);
}

static int ad1986a_hp_init(struct hda_codec *codec)
{
	ad198x_init(codec);
	ad1986a_hp_automute(codec);
	return 0;
}

/* bind hp and internal speaker mute (with plug check) */
static int ad1986a_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	change = snd_hda_codec_amp_update(codec, 0x1a, 0, HDA_OUTPUT, 0,
					  HDA_AMP_MUTE,
					  valp[0] ? 0 : HDA_AMP_MUTE);
	change |= snd_hda_codec_amp_update(codec, 0x1a, 1, HDA_OUTPUT, 0,
					   HDA_AMP_MUTE,
					   valp[1] ? 0 : HDA_AMP_MUTE);
	if (change)
		ad1986a_update_hp(codec);
	return change;
}

static struct snd_kcontrol_new ad1986a_laptop_automute_mixers[] = {
	HDA_BIND_VOL("Master Playback Volume", &ad1986a_laptop_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = ad1986a_hp_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x1a, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "External Amplifier",
		.info = ad198x_eapd_info,
		.get = ad198x_eapd_get,
		.put = ad198x_eapd_put,
		.private_value = 0x1b | (1 << 8), /* port-D, inversed */
	},
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
	/* HP Pin */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* Front, Surround, CLFE Pins */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* Mono Pin */
	{0x1e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* Mic Pin */
	{0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* Line, Aux, CD, Beep-In Pin */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{0x22, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{0x24, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{ } /* end */
};

static struct hda_verb ad1986a_ch2_init[] = {
	/* Surround out -> Line In */
	{ 0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
 	/* Line-in selectors */
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x1 },
	/* CLFE -> Mic in */
	{ 0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	/* Mic selector, mix C/LFE (backmic) and Mic (frontmic) */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x4 },
	{ } /* end */
};

static struct hda_verb ad1986a_ch4_init[] = {
	/* Surround out -> Surround */
	{ 0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* CLFE -> Mic in */
	{ 0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x4 },
	{ } /* end */
};

static struct hda_verb ad1986a_ch6_init[] = {
	/* Surround out -> Surround out */
	{ 0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* CLFE -> CLFE */
	{ 0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x0 },
	{ } /* end */
};

static struct hda_channel_mode ad1986a_modes[3] = {
	{ 2, ad1986a_ch2_init },
	{ 4, ad1986a_ch4_init },
	{ 6, ad1986a_ch6_init },
};

/* eapd initialization */
static struct hda_verb ad1986a_eapd_init_verbs[] = {
	{0x1b, AC_VERB_SET_EAPD_BTLENABLE, 0x00 },
	{}
};

/* Ultra initialization */
static struct hda_verb ad1986a_ultra_init[] = {
	/* eapd initialization */
	{ 0x1b, AC_VERB_SET_EAPD_BTLENABLE, 0x00 },
	/* CLFE -> Mic in */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x2 },
	{ 0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	{ 0x1d, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
	{ } /* end */
};

/* pin sensing on HP jack */
static struct hda_verb ad1986a_hp_init_verbs[] = {
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | AD1986A_HP_EVENT},
	{}
};


/* models */
enum {
	AD1986A_6STACK,
	AD1986A_3STACK,
	AD1986A_LAPTOP,
	AD1986A_LAPTOP_EAPD,
	AD1986A_LAPTOP_AUTOMUTE,
	AD1986A_ULTRA,
	AD1986A_MODELS
};

static const char *ad1986a_models[AD1986A_MODELS] = {
	[AD1986A_6STACK]	= "6stack",
	[AD1986A_3STACK]	= "3stack",
	[AD1986A_LAPTOP]	= "laptop",
	[AD1986A_LAPTOP_EAPD]	= "laptop-eapd",
	[AD1986A_LAPTOP_AUTOMUTE] = "laptop-automute",
	[AD1986A_ULTRA]		= "ultra",
};

static struct snd_pci_quirk ad1986a_cfg_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30af, "HP B2800", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x10de, 0xcb84, "ASUS A8N-VM", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1043, 0x1153, "ASUS M9", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1213, "ASUS A6J", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x11f7, "ASUS U5A", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1263, "ASUS U5F", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1297, "ASUS Z62F", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x12b3, "ASUS V1j", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1302, "ASUS W3j", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1447, "ASUS A8J", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1043, 0x817f, "ASUS P5", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1043, 0x818f, "ASUS P5", AD1986A_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x81b3, "ASUS P5", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1043, 0x81cb, "ASUS M2N", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1043, 0x8234, "ASUS M2N", AD1986A_3STACK),
	SND_PCI_QUIRK(0x144d, 0xb03c, "Samsung R55", AD1986A_3STACK),
	SND_PCI_QUIRK(0x144d, 0xc01e, "FSC V2060", AD1986A_LAPTOP),
	SND_PCI_QUIRK(0x144d, 0xc023, "Samsung X60", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x144d, 0xc024, "Samsung R65", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x144d, 0xc026, "Samsung X11", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x144d, 0xc504, "Samsung Q35", AD1986A_3STACK),
	SND_PCI_QUIRK(0x144d, 0xc027, "Samsung Q1", AD1986A_ULTRA),
	SND_PCI_QUIRK(0x17aa, 0x1011, "Lenovo M55", AD1986A_LAPTOP),
	SND_PCI_QUIRK(0x17aa, 0x1017, "Lenovo A60", AD1986A_3STACK),
	SND_PCI_QUIRK(0x17aa, 0x2066, "Lenovo N100", AD1986A_LAPTOP_AUTOMUTE),
	SND_PCI_QUIRK(0x17c0, 0x2017, "Samsung M50", AD1986A_LAPTOP),
	{}
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list ad1986a_loopbacks[] = {
	{ 0x13, HDA_OUTPUT, 0 }, /* Mic */
	{ 0x14, HDA_OUTPUT, 0 }, /* Phone */
	{ 0x15, HDA_OUTPUT, 0 }, /* CD */
	{ 0x16, HDA_OUTPUT, 0 }, /* Aux */
	{ 0x17, HDA_OUTPUT, 0 }, /* Line */
	{ } /* end */
};
#endif

static int patch_ad1986a(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.max_channels = 6;
	spec->multiout.num_dacs = ARRAY_SIZE(ad1986a_dac_nids);
	spec->multiout.dac_nids = ad1986a_dac_nids;
	spec->multiout.dig_out_nid = AD1986A_SPDIF_OUT;
	spec->num_adc_nids = 1;
	spec->adc_nids = ad1986a_adc_nids;
	spec->capsrc_nids = ad1986a_capsrc_nids;
	spec->input_mux = &ad1986a_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = ad1986a_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = ad1986a_init_verbs;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = ad1986a_loopbacks;
#endif

	codec->patch_ops = ad198x_patch_ops;

	/* override some parameters */
	board_config = snd_hda_check_board_config(codec, AD1986A_MODELS,
						  ad1986a_models,
						  ad1986a_cfg_tbl);
	switch (board_config) {
	case AD1986A_3STACK:
		spec->num_mixers = 2;
		spec->mixers[1] = ad1986a_3st_mixers;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = ad1986a_ch2_init;
		spec->channel_mode = ad1986a_modes;
		spec->num_channel_mode = ARRAY_SIZE(ad1986a_modes);
		spec->need_dac_fix = 1;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		break;
	case AD1986A_LAPTOP:
		spec->mixers[0] = ad1986a_laptop_mixers;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1986a_laptop_dac_nids;
		break;
	case AD1986A_LAPTOP_EAPD:
		spec->mixers[0] = ad1986a_laptop_eapd_mixers;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = ad1986a_eapd_init_verbs;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1986a_laptop_dac_nids;
		spec->multiout.dig_out_nid = 0;
		spec->input_mux = &ad1986a_laptop_eapd_capture_source;
		break;
	case AD1986A_LAPTOP_AUTOMUTE:
		spec->mixers[0] = ad1986a_laptop_automute_mixers;
		spec->num_init_verbs = 3;
		spec->init_verbs[1] = ad1986a_eapd_init_verbs;
		spec->init_verbs[2] = ad1986a_hp_init_verbs;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1986a_laptop_dac_nids;
		spec->multiout.dig_out_nid = 0;
		spec->input_mux = &ad1986a_laptop_eapd_capture_source;
		codec->patch_ops.unsol_event = ad1986a_hp_unsol_event;
		codec->patch_ops.init = ad1986a_hp_init;
		break;
	case AD1986A_ULTRA:
		spec->mixers[0] = ad1986a_laptop_eapd_mixers;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = ad1986a_ultra_init;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1986a_laptop_dac_nids;
		spec->multiout.dig_out_nid = 0;
		break;
	}

	return 0;
}

/*
 * AD1983 specific
 */

#define AD1983_SPDIF_OUT	0x02
#define AD1983_DAC		0x03
#define AD1983_ADC		0x04

static hda_nid_t ad1983_dac_nids[1] = { AD1983_DAC };
static hda_nid_t ad1983_adc_nids[1] = { AD1983_ADC };
static hda_nid_t ad1983_capsrc_nids[1] = { 0x15 };

static struct hda_input_mux ad1983_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Line", 0x1 },
		{ "Mix", 0x2 },
		{ "Mix Mono", 0x3 },
	},
};

/*
 * SPDIF playback route
 */
static int ad1983_spdif_route_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = { "PCM", "ADC" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int ad1983_spdif_route_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->spdif_route;
	return 0;
}

static int ad1983_spdif_route_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;

	if (spec->spdif_route != ucontrol->value.enumerated.item[0]) {
		spec->spdif_route = ucontrol->value.enumerated.item[0];
		snd_hda_codec_write_cache(codec, spec->multiout.dig_out_nid, 0,
					  AC_VERB_SET_CONNECT_SEL,
					  spec->spdif_route);
		return 1;
	}
	return 0;
}

static struct snd_kcontrol_new ad1983_mixers[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x05, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x05, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x07, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x07, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("PC Speaker Playback Volume", 0x10, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("PC Speaker Playback Switch", 0x10, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x15, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		.info = ad1983_spdif_route_info,
		.get = ad1983_spdif_route_get,
		.put = ad1983_spdif_route_put,
	},
	{ } /* end */
};

static struct hda_verb ad1983_init_verbs[] = {
	/* Front, HP, Mono; mute as default */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* Beep, PCM, Mic, Line-In: mute */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* Front, HP selectors; from Mix */
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x06, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* Mono selector; from Mix */
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Mic selector; Mic */
	{0x0c, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Line-in selector: Line-in */
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Mic boost: 0dB */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* Record selector: mic */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* SPDIF route: PCM */
	{0x02, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Front Pin */
	{0x05, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* HP Pin */
	{0x06, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* Mono Pin */
	{0x07, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* Mic Pin */
	{0x08, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* Line Pin */
	{0x09, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{ } /* end */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list ad1983_loopbacks[] = {
	{ 0x12, HDA_OUTPUT, 0 }, /* Mic */
	{ 0x13, HDA_OUTPUT, 0 }, /* Line */
	{ } /* end */
};
#endif

static int patch_ad1983(struct hda_codec *codec)
{
	struct ad198x_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(ad1983_dac_nids);
	spec->multiout.dac_nids = ad1983_dac_nids;
	spec->multiout.dig_out_nid = AD1983_SPDIF_OUT;
	spec->num_adc_nids = 1;
	spec->adc_nids = ad1983_adc_nids;
	spec->capsrc_nids = ad1983_capsrc_nids;
	spec->input_mux = &ad1983_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = ad1983_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = ad1983_init_verbs;
	spec->spdif_route = 0;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = ad1983_loopbacks;
#endif

	codec->patch_ops = ad198x_patch_ops;

	return 0;
}


/*
 * AD1981 HD specific
 */

#define AD1981_SPDIF_OUT	0x02
#define AD1981_DAC		0x03
#define AD1981_ADC		0x04

static hda_nid_t ad1981_dac_nids[1] = { AD1981_DAC };
static hda_nid_t ad1981_adc_nids[1] = { AD1981_ADC };
static hda_nid_t ad1981_capsrc_nids[1] = { 0x15 };

/* 0x0c, 0x09, 0x0e, 0x0f, 0x19, 0x05, 0x18, 0x17 */
static struct hda_input_mux ad1981_capture_source = {
	.num_items = 7,
	.items = {
		{ "Front Mic", 0x0 },
		{ "Line", 0x1 },
		{ "Mix", 0x2 },
		{ "Mix Mono", 0x3 },
		{ "CD", 0x4 },
		{ "Mic", 0x6 },
		{ "Aux", 0x7 },
	},
};

static struct snd_kcontrol_new ad1981_mixers[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x05, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x05, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x07, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x07, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Aux Playback Volume", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Aux Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("PC Speaker Playback Volume", 0x0d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("PC Speaker Playback Switch", 0x0d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x15, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	/* identical with AD1983 */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		.info = ad1983_spdif_route_info,
		.get = ad1983_spdif_route_get,
		.put = ad1983_spdif_route_put,
	},
	{ } /* end */
};

static struct hda_verb ad1981_init_verbs[] = {
	/* Front, HP, Mono; mute as default */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* Beep, PCM, Front Mic, Line, Rear Mic, Aux, CD-In: mute */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* Front, HP selectors; from Mix */
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x06, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* Mono selector; from Mix */
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Mic Mixer; select Front Mic */
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	{0x1f, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* Mic boost: 0dB */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* Record selector: Front mic */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* SPDIF route: PCM */
	{0x02, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Front Pin */
	{0x05, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* HP Pin */
	{0x06, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* Mono Pin */
	{0x07, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* Front & Rear Mic Pins */
	{0x08, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* Line Pin */
	{0x09, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* Digital Beep */
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Line-Out as Input: disabled */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{ } /* end */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list ad1981_loopbacks[] = {
	{ 0x12, HDA_OUTPUT, 0 }, /* Front Mic */
	{ 0x13, HDA_OUTPUT, 0 }, /* Line */
	{ 0x1b, HDA_OUTPUT, 0 }, /* Aux */
	{ 0x1c, HDA_OUTPUT, 0 }, /* Mic */
	{ 0x1d, HDA_OUTPUT, 0 }, /* CD */
	{ } /* end */
};
#endif

/*
 * Patch for HP nx6320
 *
 * nx6320 uses EAPD in the reverse way - EAPD-on means the internal
 * speaker output enabled _and_ mute-LED off.
 */

#define AD1981_HP_EVENT		0x37
#define AD1981_MIC_EVENT	0x38

static struct hda_verb ad1981_hp_init_verbs[] = {
	{0x05, AC_VERB_SET_EAPD_BTLENABLE, 0x00 }, /* default off */
	/* pin sensing on HP and Mic jacks */
	{0x06, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | AD1981_HP_EVENT},
	{0x08, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | AD1981_MIC_EVENT},
	{}
};

/* turn on/off EAPD (+ mute HP) as a master switch */
static int ad1981_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;

	if (! ad198x_eapd_put(kcontrol, ucontrol))
		return 0;

	/* toggle HP mute appropriately */
	snd_hda_codec_amp_stereo(codec, 0x06, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE,
				 spec->cur_eapd ? 0 : HDA_AMP_MUTE);
	return 1;
}

/* bind volumes of both NID 0x05 and 0x06 */
static struct hda_bind_ctls ad1981_hp_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x05, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x06, 3, 0, HDA_OUTPUT),
		0
	},
};

/* mute internal speaker if HP is plugged */
static void ad1981_hp_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x06, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x05, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

/* toggle input of built-in and mic jack appropriately */
static void ad1981_hp_automic(struct hda_codec *codec)
{
	static struct hda_verb mic_jack_on[] = {
		{0x1f, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
		{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
		{}
	};
	static struct hda_verb mic_jack_off[] = {
		{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
		{0x1f, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
		{}
	};
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x08, 0,
			    	 AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	if (present)
		snd_hda_sequence_write(codec, mic_jack_on);
	else
		snd_hda_sequence_write(codec, mic_jack_off);
}

/* unsolicited event for HP jack sensing */
static void ad1981_hp_unsol_event(struct hda_codec *codec,
				  unsigned int res)
{
	res >>= 26;
	switch (res) {
	case AD1981_HP_EVENT:
		ad1981_hp_automute(codec);
		break;
	case AD1981_MIC_EVENT:
		ad1981_hp_automic(codec);
		break;
	}
}

static struct hda_input_mux ad1981_hp_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Docking-Station", 0x1 },
		{ "Mix", 0x2 },
	},
};

static struct snd_kcontrol_new ad1981_hp_mixers[] = {
	HDA_BIND_VOL("Master Playback Volume", &ad1981_hp_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = ad198x_eapd_info,
		.get = ad198x_eapd_get,
		.put = ad1981_hp_master_sw_put,
		.private_value = 0x05,
	},
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x11, 0x0, HDA_OUTPUT),
#if 0
	/* FIXME: analog mic/line loopback doesn't work with my tests...
	 *        (although recording is OK)
	 */
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Docking-Station Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Docking-Station Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x1c, 0x0, HDA_OUTPUT),
	/* FIXME: does this laptop have analog CD connection? */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x1d, 0x0, HDA_OUTPUT),
#endif
	HDA_CODEC_VOLUME("Mic Boost", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost", 0x18, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x15, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	{ } /* end */
};

/* initialize jack-sensing, too */
static int ad1981_hp_init(struct hda_codec *codec)
{
	ad198x_init(codec);
	ad1981_hp_automute(codec);
	ad1981_hp_automic(codec);
	return 0;
}

/* configuration for Toshiba Laptops */
static struct hda_verb ad1981_toshiba_init_verbs[] = {
	{0x05, AC_VERB_SET_EAPD_BTLENABLE, 0x01 }, /* default on */
	/* pin sensing on HP and Mic jacks */
	{0x06, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | AD1981_HP_EVENT},
	{0x08, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | AD1981_MIC_EVENT},
	{}
};

static struct snd_kcontrol_new ad1981_toshiba_mixers[] = {
	HDA_CODEC_VOLUME("Amp Volume", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Amp Switch", 0x1a, 0x0, HDA_OUTPUT),
	{ }
};

/* configuration for Lenovo Thinkpad T60 */
static struct snd_kcontrol_new ad1981_thinkpad_mixers[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x05, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Master Playback Switch", 0x05, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x15, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	/* identical with AD1983 */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		.info = ad1983_spdif_route_info,
		.get = ad1983_spdif_route_get,
		.put = ad1983_spdif_route_put,
	},
	{ } /* end */
};

static struct hda_input_mux ad1981_thinkpad_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Mix", 0x2 },
		{ "CD", 0x4 },
	},
};

/* models */
enum {
	AD1981_BASIC,
	AD1981_HP,
	AD1981_THINKPAD,
	AD1981_TOSHIBA,
	AD1981_MODELS
};

static const char *ad1981_models[AD1981_MODELS] = {
	[AD1981_HP]		= "hp",
	[AD1981_THINKPAD]	= "thinkpad",
	[AD1981_BASIC]		= "basic",
	[AD1981_TOSHIBA]	= "toshiba"
};

static struct snd_pci_quirk ad1981_cfg_tbl[] = {
	/* All HP models */
	SND_PCI_QUIRK(0x103c, 0, "HP nx", AD1981_HP),
	/* HP nx6320 (reversed SSID, H/W bug) */
	SND_PCI_QUIRK(0x30b0, 0x103c, "HP nx6320", AD1981_HP),
	/* Lenovo Thinkpad T60/X60/Z6xx */
	SND_PCI_QUIRK(0x17aa, 0, "Lenovo Thinkpad", AD1981_THINKPAD),
	SND_PCI_QUIRK(0x1014, 0x0597, "Lenovo Z60", AD1981_THINKPAD),
	SND_PCI_QUIRK(0x1179, 0x0001, "Toshiba U205", AD1981_TOSHIBA),
	{}
};

static int patch_ad1981(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(ad1981_dac_nids);
	spec->multiout.dac_nids = ad1981_dac_nids;
	spec->multiout.dig_out_nid = AD1981_SPDIF_OUT;
	spec->num_adc_nids = 1;
	spec->adc_nids = ad1981_adc_nids;
	spec->capsrc_nids = ad1981_capsrc_nids;
	spec->input_mux = &ad1981_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = ad1981_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = ad1981_init_verbs;
	spec->spdif_route = 0;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = ad1981_loopbacks;
#endif

	codec->patch_ops = ad198x_patch_ops;

	/* override some parameters */
	board_config = snd_hda_check_board_config(codec, AD1981_MODELS,
						  ad1981_models,
						  ad1981_cfg_tbl);
	switch (board_config) {
	case AD1981_HP:
		spec->mixers[0] = ad1981_hp_mixers;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = ad1981_hp_init_verbs;
		spec->multiout.dig_out_nid = 0;
		spec->input_mux = &ad1981_hp_capture_source;

		codec->patch_ops.init = ad1981_hp_init;
		codec->patch_ops.unsol_event = ad1981_hp_unsol_event;
		break;
	case AD1981_THINKPAD:
		spec->mixers[0] = ad1981_thinkpad_mixers;
		spec->input_mux = &ad1981_thinkpad_capture_source;
		break;
	case AD1981_TOSHIBA:
		spec->mixers[0] = ad1981_hp_mixers;
		spec->mixers[1] = ad1981_toshiba_mixers;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = ad1981_toshiba_init_verbs;
		spec->multiout.dig_out_nid = 0;
		spec->input_mux = &ad1981_hp_capture_source;
		codec->patch_ops.init = ad1981_hp_init;
		codec->patch_ops.unsol_event = ad1981_hp_unsol_event;
		break;
	}
	return 0;
}


/*
 * AD1988
 *
 * Output pins and routes
 *
 *        Pin               Mix     Sel     DAC (*)
 * port-A 0x11 (mute/hp) <- 0x22 <- 0x37 <- 03/04/06
 * port-B 0x14 (mute/hp) <- 0x2b <- 0x30 <- 03/04/06
 * port-C 0x15 (mute)    <- 0x2c <- 0x31 <- 05/0a
 * port-D 0x12 (mute/hp) <- 0x29         <- 04
 * port-E 0x17 (mute/hp) <- 0x26 <- 0x32 <- 05/0a
 * port-F 0x16 (mute)    <- 0x2a         <- 06
 * port-G 0x24 (mute)    <- 0x27         <- 05
 * port-H 0x25 (mute)    <- 0x28         <- 0a
 * mono   0x13 (mute/amp)<- 0x1e <- 0x36 <- 03/04/06
 *
 * DAC0 = 03h, DAC1 = 04h, DAC2 = 05h, DAC3 = 06h, DAC4 = 0ah
 * (*) DAC2/3/4 are swapped to DAC3/4/2 on AD198A rev.2 due to a h/w bug.
 *
 * Input pins and routes
 *
 *        pin     boost   mix input # / adc input #
 * port-A 0x11 -> 0x38 -> mix 2, ADC 0
 * port-B 0x14 -> 0x39 -> mix 0, ADC 1
 * port-C 0x15 -> 0x3a -> 33:0 - mix 1, ADC 2
 * port-D 0x12 -> 0x3d -> mix 3, ADC 8
 * port-E 0x17 -> 0x3c -> 34:0 - mix 4, ADC 4
 * port-F 0x16 -> 0x3b -> mix 5, ADC 3
 * port-G 0x24 -> N/A  -> 33:1 - mix 1, 34:1 - mix 4, ADC 6
 * port-H 0x25 -> N/A  -> 33:2 - mix 1, 34:2 - mix 4, ADC 7
 *
 *
 * DAC assignment
 *   6stack - front/surr/CLFE/side/opt DACs - 04/06/05/0a/03
 *   3stack - front/surr/CLFE/opt DACs - 04/05/0a/03
 *
 * Inputs of Analog Mix (0x20)
 *   0:Port-B (front mic)
 *   1:Port-C/G/H (line-in)
 *   2:Port-A
 *   3:Port-D (line-in/2)
 *   4:Port-E/G/H (mic-in)
 *   5:Port-F (mic2-in)
 *   6:CD
 *   7:Beep
 *
 * ADC selection
 *   0:Port-A
 *   1:Port-B (front mic-in)
 *   2:Port-C (line-in)
 *   3:Port-F (mic2-in)
 *   4:Port-E (mic-in)
 *   5:CD
 *   6:Port-G
 *   7:Port-H
 *   8:Port-D (line-in/2)
 *   9:Mix
 *
 * Proposed pin assignments by the datasheet
 *
 * 6-stack
 * Port-A front headphone
 *      B front mic-in
 *      C rear line-in
 *      D rear front-out
 *      E rear mic-in
 *      F rear surround
 *      G rear CLFE
 *      H rear side
 *
 * 3-stack
 * Port-A front headphone
 *      B front mic
 *      C rear line-in/surround
 *      D rear front-out
 *      E rear mic-in/CLFE
 *
 * laptop
 * Port-A headphone
 *      B mic-in
 *      C docking station
 *      D internal speaker (with EAPD)
 *      E/F quad mic array
 */


/* models */
enum {
	AD1988_6STACK,
	AD1988_6STACK_DIG,
	AD1988_3STACK,
	AD1988_3STACK_DIG,
	AD1988_LAPTOP,
	AD1988_LAPTOP_DIG,
	AD1988_AUTO,
	AD1988_MODEL_LAST,
};

/* reivision id to check workarounds */
#define AD1988A_REV2		0x100200

#define is_rev2(codec) \
	((codec)->vendor_id == 0x11d41988 && \
	 (codec)->revision_id == AD1988A_REV2)

/*
 * mixers
 */

static hda_nid_t ad1988_6stack_dac_nids[4] = {
	0x04, 0x06, 0x05, 0x0a
};

static hda_nid_t ad1988_3stack_dac_nids[3] = {
	0x04, 0x05, 0x0a
};

/* for AD1988A revision-2, DAC2-4 are swapped */
static hda_nid_t ad1988_6stack_dac_nids_rev2[4] = {
	0x04, 0x05, 0x0a, 0x06
};

static hda_nid_t ad1988_3stack_dac_nids_rev2[3] = {
	0x04, 0x0a, 0x06
};

static hda_nid_t ad1988_adc_nids[3] = {
	0x08, 0x09, 0x0f
};

static hda_nid_t ad1988_capsrc_nids[3] = {
	0x0c, 0x0d, 0x0e
};

#define AD1988_SPDIF_OUT	0x02
#define AD1988_SPDIF_IN		0x07

static struct hda_input_mux ad1988_6stack_capture_source = {
	.num_items = 5,
	.items = {
		{ "Front Mic", 0x0 },
		{ "Line", 0x1 },
		{ "Mic", 0x4 },
		{ "CD", 0x5 },
		{ "Mix", 0x9 },
	},
};

static struct hda_input_mux ad1988_laptop_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic/Line", 0x0 },
		{ "CD", 0x5 },
		{ "Mix", 0x9 },
	},
};

/*
 */
static int ad198x_ch_mode_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_ch_mode_info(codec, uinfo, spec->channel_mode,
				    spec->num_channel_mode);
}

static int ad198x_ch_mode_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_ch_mode_get(codec, ucontrol, spec->channel_mode,
				   spec->num_channel_mode, spec->multiout.max_channels);
}

static int ad198x_ch_mode_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	int err = snd_hda_ch_mode_put(codec, ucontrol, spec->channel_mode,
				      spec->num_channel_mode,
				      &spec->multiout.max_channels);
	if (err >= 0 && spec->need_dac_fix)
		spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	return err;
}

/* 6-stack mode */
static struct snd_kcontrol_new ad1988_6stack_mixers1[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x04, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x05, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0a, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new ad1988_6stack_mixers1_rev2[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x04, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x05, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0a, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0a, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x06, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new ad1988_6stack_mixers2[] = {
	HDA_BIND_MUTE("Front Playback Switch", 0x29, 2, HDA_INPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x2a, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x27, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x27, 2, 2, HDA_INPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x28, 2, HDA_INPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x22, 2, HDA_INPUT),
	HDA_BIND_MUTE("Mono Playback Switch", 0x1e, 2, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x20, 0x6, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x20, 0x6, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x20, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x20, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x20, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x20, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x20, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x20, 0x4, HDA_INPUT),

	HDA_CODEC_VOLUME("Beep Playback Volume", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x10, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Analog Mix Playback Volume", 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Analog Mix Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Front Mic Boost", 0x39, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x3c, 0x0, HDA_OUTPUT),

	{ } /* end */
};

/* 3-stack mode */
static struct snd_kcontrol_new ad1988_3stack_mixers1[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x04, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x05, 2, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new ad1988_3stack_mixers1_rev2[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x04, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x06, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x06, 2, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new ad1988_3stack_mixers2[] = {
	HDA_BIND_MUTE("Front Playback Switch", 0x29, 2, HDA_INPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x2c, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x26, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x26, 2, 2, HDA_INPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x22, 2, HDA_INPUT),
	HDA_BIND_MUTE("Mono Playback Switch", 0x1e, 2, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x20, 0x6, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x20, 0x6, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x20, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x20, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x20, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x20, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x20, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x20, 0x4, HDA_INPUT),

	HDA_CODEC_VOLUME("Beep Playback Volume", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x10, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Analog Mix Playback Volume", 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Analog Mix Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Front Mic Boost", 0x39, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x3c, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = ad198x_ch_mode_info,
		.get = ad198x_ch_mode_get,
		.put = ad198x_ch_mode_put,
	},

	{ } /* end */
};

/* laptop mode */
static struct snd_kcontrol_new ad1988_laptop_mixers[] = {
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x04, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x29, 0x0, HDA_INPUT),
	HDA_BIND_MUTE("Mono Playback Switch", 0x1e, 2, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x20, 0x6, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x20, 0x6, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x20, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x20, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x20, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x20, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Beep Playback Volume", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x10, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Analog Mix Playback Volume", 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Analog Mix Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Boost", 0x39, 0x0, HDA_OUTPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "External Amplifier",
		.info = ad198x_eapd_info,
		.get = ad198x_eapd_get,
		.put = ad198x_eapd_put,
		.private_value = 0x12 | (1 << 8), /* port-D, inversed */
	},

	{ } /* end */
};

/* capture */
static struct snd_kcontrol_new ad1988_capture_mixers[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 2, 0x0e, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 2, 0x0e, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 3,
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	{ } /* end */
};

static int ad1988_spdif_playback_source_info(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {
		"PCM", "ADC1", "ADC2", "ADC3"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= 4)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int ad1988_spdif_playback_source_get(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int sel;

	sel = snd_hda_codec_read(codec, 0x1d, 0, AC_VERB_GET_AMP_GAIN_MUTE,
				 AC_AMP_GET_INPUT);
	if (!(sel & 0x80))
		ucontrol->value.enumerated.item[0] = 0;
	else {
		sel = snd_hda_codec_read(codec, 0x0b, 0,
					 AC_VERB_GET_CONNECT_SEL, 0);
		if (sel < 3)
			sel++;
		else
			sel = 0;
		ucontrol->value.enumerated.item[0] = sel;
	}
	return 0;
}

static int ad1988_spdif_playback_source_put(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int val, sel;
	int change;

	val = ucontrol->value.enumerated.item[0];
	if (!val) {
		sel = snd_hda_codec_read(codec, 0x1d, 0,
					 AC_VERB_GET_AMP_GAIN_MUTE,
					 AC_AMP_GET_INPUT);
		change = sel & 0x80;
		if (change) {
			snd_hda_codec_write_cache(codec, 0x1d, 0,
						  AC_VERB_SET_AMP_GAIN_MUTE,
						  AMP_IN_UNMUTE(0));
			snd_hda_codec_write_cache(codec, 0x1d, 0,
						  AC_VERB_SET_AMP_GAIN_MUTE,
						  AMP_IN_MUTE(1));
		}
	} else {
		sel = snd_hda_codec_read(codec, 0x1d, 0,
					 AC_VERB_GET_AMP_GAIN_MUTE,
					 AC_AMP_GET_INPUT | 0x01);
		change = sel & 0x80;
		if (change) {
			snd_hda_codec_write_cache(codec, 0x1d, 0,
						  AC_VERB_SET_AMP_GAIN_MUTE,
						  AMP_IN_MUTE(0));
			snd_hda_codec_write_cache(codec, 0x1d, 0,
						  AC_VERB_SET_AMP_GAIN_MUTE,
						  AMP_IN_UNMUTE(1));
		}
		sel = snd_hda_codec_read(codec, 0x0b, 0,
					 AC_VERB_GET_CONNECT_SEL, 0) + 1;
		change |= sel != val;
		if (change)
			snd_hda_codec_write_cache(codec, 0x0b, 0,
						  AC_VERB_SET_CONNECT_SEL,
						  val - 1);
	}
	return change;
}

static struct snd_kcontrol_new ad1988_spdif_out_mixers[] = {
	HDA_CODEC_VOLUME("IEC958 Playback Volume", 0x1b, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "IEC958 Playback Source",
		.info = ad1988_spdif_playback_source_info,
		.get = ad1988_spdif_playback_source_get,
		.put = ad1988_spdif_playback_source_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new ad1988_spdif_in_mixers[] = {
	HDA_CODEC_VOLUME("IEC958 Capture Volume", 0x1c, 0x0, HDA_INPUT),
	{ } /* end */
};


/*
 * initialization verbs
 */

/*
 * for 6-stack (+dig)
 */
static struct hda_verb ad1988_6stack_init_verbs[] = {
	/* Front, Surround, CLFE, side DAC; unmute as default */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Port-A front headphon path */
	{0x37, AC_VERB_SET_CONNECT_SEL, 0x01}, /* DAC1:04h */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Port-D line-out path */
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Port-F surround path */
	{0x2a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x2a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Port-G CLFE path */
	{0x27, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x27, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x24, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Port-H side path */
	{0x28, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x28, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x25, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x25, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Mono out path */
	{0x36, AC_VERB_SET_CONNECT_SEL, 0x1}, /* DAC1:04h */
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, 0xb01f}, /* unmute, 0dB */
	/* Port-B front mic-in path */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x39, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* Port-C line-in path */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x3a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x33, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Port-E mic-in path */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x3c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x34, AC_VERB_SET_CONNECT_SEL, 0x0},

	{ }
};

static struct hda_verb ad1988_capture_init_verbs[] = {
	/* mute analog mix */
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/* select ADCs - front-mic */
	{0x0c, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* ADCs; muted */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{ }
};

static struct hda_verb ad1988_spdif_init_verbs[] = {
	/* SPDIF out sel */
	{0x02, AC_VERB_SET_CONNECT_SEL, 0x0}, /* PCM */
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0x0}, /* ADC1 */
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* SPDIF out pin */
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x27}, /* 0dB */

	{ }
};

/*
 * verbs for 3stack (+dig)
 */
static struct hda_verb ad1988_3stack_ch2_init[] = {
	/* set port-C to line-in */
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	/* set port-E to mic-in */
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ } /* end */
};

static struct hda_verb ad1988_3stack_ch6_init[] = {
	/* set port-C to surround out */
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	/* set port-E to CLFE out */
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static struct hda_channel_mode ad1988_3stack_modes[2] = {
	{ 2, ad1988_3stack_ch2_init },
	{ 6, ad1988_3stack_ch6_init },
};

static struct hda_verb ad1988_3stack_init_verbs[] = {
	/* Front, Surround, CLFE, side DAC; unmute as default */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Port-A front headphon path */
	{0x37, AC_VERB_SET_CONNECT_SEL, 0x01}, /* DAC1:04h */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Port-D line-out path */
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Mono out path */
	{0x36, AC_VERB_SET_CONNECT_SEL, 0x1}, /* DAC1:04h */
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, 0xb01f}, /* unmute, 0dB */
	/* Port-B front mic-in path */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x39, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* Port-C line-in/surround path - 6ch mode as default */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x3a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x31, AC_VERB_SET_CONNECT_SEL, 0x0}, /* output sel: DAC 0x05 */
	{0x33, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Port-E mic-in/CLFE path - 6ch mode as default */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x3c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x32, AC_VERB_SET_CONNECT_SEL, 0x1}, /* output sel: DAC 0x0a */
	{0x34, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* mute analog mix */
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/* select ADCs - front-mic */
	{0x0c, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* ADCs; muted */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{ }
};

/*
 * verbs for laptop mode (+dig)
 */
static struct hda_verb ad1988_laptop_hp_on[] = {
	/* unmute port-A and mute port-D */
	{ 0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};
static struct hda_verb ad1988_laptop_hp_off[] = {
	/* mute port-A and unmute port-D */
	{ 0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

#define AD1988_HP_EVENT	0x01

static struct hda_verb ad1988_laptop_init_verbs[] = {
	/* Front, Surround, CLFE, side DAC; unmute as default */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Port-A front headphon path */
	{0x37, AC_VERB_SET_CONNECT_SEL, 0x01}, /* DAC1:04h */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* unsolicited event for pin-sense */
	{0x11, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | AD1988_HP_EVENT },
	/* Port-D line-out path + EAPD */
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x12, AC_VERB_SET_EAPD_BTLENABLE, 0x00}, /* EAPD-off */
	/* Mono out path */
	{0x36, AC_VERB_SET_CONNECT_SEL, 0x1}, /* DAC1:04h */
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, 0xb01f}, /* unmute, 0dB */
	/* Port-B mic-in path */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x39, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* Port-C docking station - try to output */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x3a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x33, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* mute analog mix */
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/* select ADCs - mic */
	{0x0c, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* ADCs; muted */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{ }
};

static void ad1988_laptop_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) != AD1988_HP_EVENT)
		return;
	if (snd_hda_codec_read(codec, 0x11, 0, AC_VERB_GET_PIN_SENSE, 0) & (1 << 31))
		snd_hda_sequence_write(codec, ad1988_laptop_hp_on);
	else
		snd_hda_sequence_write(codec, ad1988_laptop_hp_off);
} 

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list ad1988_loopbacks[] = {
	{ 0x20, HDA_INPUT, 0 }, /* Front Mic */
	{ 0x20, HDA_INPUT, 1 }, /* Line */
	{ 0x20, HDA_INPUT, 4 }, /* Mic */
	{ 0x20, HDA_INPUT, 6 }, /* CD */
	{ } /* end */
};
#endif

/*
 * Automatic parse of I/O pins from the BIOS configuration
 */

#define NUM_CONTROL_ALLOC	32
#define NUM_VERB_ALLOC		32

enum {
	AD_CTL_WIDGET_VOL,
	AD_CTL_WIDGET_MUTE,
	AD_CTL_BIND_MUTE,
};
static struct snd_kcontrol_new ad1988_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	HDA_BIND_MUTE(NULL, 0, 0, 0),
};

/* add dynamic controls */
static int add_control(struct ad198x_spec *spec, int type, const char *name,
		       unsigned long val)
{
	struct snd_kcontrol_new *knew;

	if (spec->num_kctl_used >= spec->num_kctl_alloc) {
		int num = spec->num_kctl_alloc + NUM_CONTROL_ALLOC;

		knew = kcalloc(num + 1, sizeof(*knew), GFP_KERNEL); /* array + terminator */
		if (! knew)
			return -ENOMEM;
		if (spec->kctl_alloc) {
			memcpy(knew, spec->kctl_alloc, sizeof(*knew) * spec->num_kctl_alloc);
			kfree(spec->kctl_alloc);
		}
		spec->kctl_alloc = knew;
		spec->num_kctl_alloc = num;
	}

	knew = &spec->kctl_alloc[spec->num_kctl_used];
	*knew = ad1988_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);
	if (! knew->name)
		return -ENOMEM;
	knew->private_value = val;
	spec->num_kctl_used++;
	return 0;
}

#define AD1988_PIN_CD_NID		0x18
#define AD1988_PIN_BEEP_NID		0x10

static hda_nid_t ad1988_mixer_nids[8] = {
	/* A     B     C     D     E     F     G     H */
	0x22, 0x2b, 0x2c, 0x29, 0x26, 0x2a, 0x27, 0x28
};

static inline hda_nid_t ad1988_idx_to_dac(struct hda_codec *codec, int idx)
{
	static hda_nid_t idx_to_dac[8] = {
		/* A     B     C     D     E     F     G     H */
		0x04, 0x06, 0x05, 0x04, 0x0a, 0x06, 0x05, 0x0a
	};
	static hda_nid_t idx_to_dac_rev2[8] = {
		/* A     B     C     D     E     F     G     H */
		0x04, 0x05, 0x0a, 0x04, 0x06, 0x05, 0x0a, 0x06
	};
	if (is_rev2(codec))
		return idx_to_dac_rev2[idx];
	else
		return idx_to_dac[idx];
}

static hda_nid_t ad1988_boost_nids[8] = {
	0x38, 0x39, 0x3a, 0x3d, 0x3c, 0x3b, 0, 0
};

static int ad1988_pin_idx(hda_nid_t nid)
{
	static hda_nid_t ad1988_io_pins[8] = {
		0x11, 0x14, 0x15, 0x12, 0x17, 0x16, 0x24, 0x25
	};
	int i;
	for (i = 0; i < ARRAY_SIZE(ad1988_io_pins); i++)
		if (ad1988_io_pins[i] == nid)
			return i;
	return 0; /* should be -1 */
}

static int ad1988_pin_to_loopback_idx(hda_nid_t nid)
{
	static int loopback_idx[8] = {
		2, 0, 1, 3, 4, 5, 1, 4
	};
	switch (nid) {
	case AD1988_PIN_CD_NID:
		return 6;
	default:
		return loopback_idx[ad1988_pin_idx(nid)];
	}
}

static int ad1988_pin_to_adc_idx(hda_nid_t nid)
{
	static int adc_idx[8] = {
		0, 1, 2, 8, 4, 3, 6, 7
	};
	switch (nid) {
	case AD1988_PIN_CD_NID:
		return 5;
	default:
		return adc_idx[ad1988_pin_idx(nid)];
	}
}

/* fill in the dac_nids table from the parsed pin configuration */
static int ad1988_auto_fill_dac_nids(struct hda_codec *codec,
				     const struct auto_pin_cfg *cfg)
{
	struct ad198x_spec *spec = codec->spec;
	int i, idx;

	spec->multiout.dac_nids = spec->private_dac_nids;

	/* check the pins hardwired to audio widget */
	for (i = 0; i < cfg->line_outs; i++) {
		idx = ad1988_pin_idx(cfg->line_out_pins[i]);
		spec->multiout.dac_nids[i] = ad1988_idx_to_dac(codec, idx);
	}
	spec->multiout.num_dacs = cfg->line_outs;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int ad1988_auto_create_multi_out_ctls(struct ad198x_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", NULL /*CLFE*/, "Side" };
	hda_nid_t nid;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		hda_nid_t dac = spec->multiout.dac_nids[i];
		if (! dac)
			continue;
		nid = ad1988_mixer_nids[ad1988_pin_idx(cfg->line_out_pins[i])];
		if (i == 2) {
			/* Center/LFE */
			err = add_control(spec, AD_CTL_WIDGET_VOL,
					  "Center Playback Volume",
					  HDA_COMPOSE_AMP_VAL(dac, 1, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, AD_CTL_WIDGET_VOL,
					  "LFE Playback Volume",
					  HDA_COMPOSE_AMP_VAL(dac, 2, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, AD_CTL_BIND_MUTE,
					  "Center Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 2, HDA_INPUT));
			if (err < 0)
				return err;
			err = add_control(spec, AD_CTL_BIND_MUTE,
					  "LFE Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 2, HDA_INPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = add_control(spec, AD_CTL_WIDGET_VOL, name,
					  HDA_COMPOSE_AMP_VAL(dac, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = add_control(spec, AD_CTL_BIND_MUTE, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 2, HDA_INPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* add playback controls for speaker and HP outputs */
static int ad1988_auto_create_extra_out(struct hda_codec *codec, hda_nid_t pin,
					const char *pfx)
{
	struct ad198x_spec *spec = codec->spec;
	hda_nid_t nid;
	int idx, err;
	char name[32];

	if (! pin)
		return 0;

	idx = ad1988_pin_idx(pin);
	nid = ad1988_idx_to_dac(codec, idx);
	/* specify the DAC as the extra output */
	if (! spec->multiout.hp_nid)
		spec->multiout.hp_nid = nid;
	else
		spec->multiout.extra_out_nid[0] = nid;
	/* control HP volume/switch on the output mixer amp */
	sprintf(name, "%s Playback Volume", pfx);
	if ((err = add_control(spec, AD_CTL_WIDGET_VOL, name,
			       HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT))) < 0)
		return err;
	nid = ad1988_mixer_nids[idx];
	sprintf(name, "%s Playback Switch", pfx);
	if ((err = add_control(spec, AD_CTL_BIND_MUTE, name,
			       HDA_COMPOSE_AMP_VAL(nid, 3, 2, HDA_INPUT))) < 0)
		return err;
	return 0;
}

/* create input playback/capture controls for the given pin */
static int new_analog_input(struct ad198x_spec *spec, hda_nid_t pin,
			    const char *ctlname, int boost)
{
	char name[32];
	int err, idx;

	sprintf(name, "%s Playback Volume", ctlname);
	idx = ad1988_pin_to_loopback_idx(pin);
	if ((err = add_control(spec, AD_CTL_WIDGET_VOL, name,
			       HDA_COMPOSE_AMP_VAL(0x20, 3, idx, HDA_INPUT))) < 0)
		return err;
	sprintf(name, "%s Playback Switch", ctlname);
	if ((err = add_control(spec, AD_CTL_WIDGET_MUTE, name,
			       HDA_COMPOSE_AMP_VAL(0x20, 3, idx, HDA_INPUT))) < 0)
		return err;
	if (boost) {
		hda_nid_t bnid;
		idx = ad1988_pin_idx(pin);
		bnid = ad1988_boost_nids[idx];
		if (bnid) {
			sprintf(name, "%s Boost", ctlname);
			return add_control(spec, AD_CTL_WIDGET_VOL, name,
					   HDA_COMPOSE_AMP_VAL(bnid, 3, idx, HDA_OUTPUT));

		}
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int ad1988_auto_create_analog_input_ctls(struct ad198x_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		err = new_analog_input(spec, cfg->input_pins[i],
				       auto_pin_cfg_labels[i],
				       i <= AUTO_PIN_FRONT_MIC);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = auto_pin_cfg_labels[i];
		imux->items[imux->num_items].index = ad1988_pin_to_adc_idx(cfg->input_pins[i]);
		imux->num_items++;
	}
	imux->items[imux->num_items].label = "Mix";
	imux->items[imux->num_items].index = 9;
	imux->num_items++;

	if ((err = add_control(spec, AD_CTL_WIDGET_VOL,
			       "Analog Mix Playback Volume",
			       HDA_COMPOSE_AMP_VAL(0x21, 3, 0x0, HDA_OUTPUT))) < 0)
		return err;
	if ((err = add_control(spec, AD_CTL_WIDGET_MUTE,
			       "Analog Mix Playback Switch",
			       HDA_COMPOSE_AMP_VAL(0x21, 3, 0x0, HDA_OUTPUT))) < 0)
		return err;

	return 0;
}

static void ad1988_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int dac_idx)
{
	/* set as output */
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, pin_type);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);
	switch (nid) {
	case 0x11: /* port-A - DAC 04 */
		snd_hda_codec_write(codec, 0x37, 0, AC_VERB_SET_CONNECT_SEL, 0x01);
		break;
	case 0x14: /* port-B - DAC 06 */
		snd_hda_codec_write(codec, 0x30, 0, AC_VERB_SET_CONNECT_SEL, 0x02);
		break;
	case 0x15: /* port-C - DAC 05 */
		snd_hda_codec_write(codec, 0x31, 0, AC_VERB_SET_CONNECT_SEL, 0x00);
		break;
	case 0x17: /* port-E - DAC 0a */
		snd_hda_codec_write(codec, 0x32, 0, AC_VERB_SET_CONNECT_SEL, 0x01);
		break;
	case 0x13: /* mono - DAC 04 */
		snd_hda_codec_write(codec, 0x36, 0, AC_VERB_SET_CONNECT_SEL, 0x01);
		break;
	}
}

static void ad1988_auto_init_multi_out(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->autocfg.line_outs; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		ad1988_auto_set_output_and_unmute(codec, nid, PIN_OUT, i);
	}
}

static void ad1988_auto_init_extra_out(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.speaker_pins[0];
	if (pin) /* connect to front */
		ad1988_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		ad1988_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
}

static void ad1988_auto_init_analog_input(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	int i, idx;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (! nid)
			continue;
		switch (nid) {
		case 0x15: /* port-C */
			snd_hda_codec_write(codec, 0x33, 0, AC_VERB_SET_CONNECT_SEL, 0x0);
			break;
		case 0x17: /* port-E */
			snd_hda_codec_write(codec, 0x34, 0, AC_VERB_SET_CONNECT_SEL, 0x0);
			break;
		}
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
				    i <= AUTO_PIN_FRONT_MIC ? PIN_VREF80 : PIN_IN);
		if (nid != AD1988_PIN_CD_NID)
			snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_OUT_MUTE);
		idx = ad1988_pin_idx(nid);
		if (ad1988_boost_nids[idx])
			snd_hda_codec_write(codec, ad1988_boost_nids[idx], 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_OUT_ZERO);
	}
}

/* parse the BIOS configuration and set up the alc_spec */
/* return 1 if successful, 0 if the proper config is not found, or a negative error code */
static int ad1988_parse_auto_config(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	int err;

	if ((err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL)) < 0)
		return err;
	if ((err = ad1988_auto_fill_dac_nids(codec, &spec->autocfg)) < 0)
		return err;
	if (! spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */
	if ((err = ad1988_auto_create_multi_out_ctls(spec, &spec->autocfg)) < 0 ||
	    (err = ad1988_auto_create_extra_out(codec,
						spec->autocfg.speaker_pins[0],
						"Speaker")) < 0 ||
	    (err = ad1988_auto_create_extra_out(codec, spec->autocfg.hp_pins[0],
						"Headphone")) < 0 ||
	    (err = ad1988_auto_create_analog_input_ctls(spec, &spec->autocfg)) < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = AD1988_SPDIF_OUT;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = AD1988_SPDIF_IN;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = ad1988_6stack_init_verbs;

	spec->input_mux = &spec->private_imux;

	return 1;
}

/* init callback for auto-configuration model -- overriding the default init */
static int ad1988_auto_init(struct hda_codec *codec)
{
	ad198x_init(codec);
	ad1988_auto_init_multi_out(codec);
	ad1988_auto_init_extra_out(codec);
	ad1988_auto_init_analog_input(codec);
	return 0;
}


/*
 */

static const char *ad1988_models[AD1988_MODEL_LAST] = {
	[AD1988_6STACK]		= "6stack",
	[AD1988_6STACK_DIG]	= "6stack-dig",
	[AD1988_3STACK]		= "3stack",
	[AD1988_3STACK_DIG]	= "3stack-dig",
	[AD1988_LAPTOP]		= "laptop",
	[AD1988_LAPTOP_DIG]	= "laptop-dig",
	[AD1988_AUTO]		= "auto",
};

static struct snd_pci_quirk ad1988_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x81f6, "Asus M2N-SLI", AD1988_6STACK_DIG),
	SND_PCI_QUIRK(0x1043, 0x81ec, "Asus P5B-DLX", AD1988_6STACK_DIG),
	{}
};

static int patch_ad1988(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	if (is_rev2(codec))
		snd_printk(KERN_INFO "patch_analog: AD1988A rev.2 is detected, enable workarounds\n");

	board_config = snd_hda_check_board_config(codec, AD1988_MODEL_LAST,
						  ad1988_models, ad1988_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for AD1988, trying auto-probe from BIOS...\n");
		board_config = AD1988_AUTO;
	}

	if (board_config == AD1988_AUTO) {
		/* automatic parse from the BIOS config */
		int err = ad1988_parse_auto_config(codec);
		if (err < 0) {
			ad198x_free(codec);
			return err;
		} else if (! err) {
			printk(KERN_INFO "hda_codec: Cannot set up configuration from BIOS.  Using 6-stack mode...\n");
			board_config = AD1988_6STACK;
		}
	}

	switch (board_config) {
	case AD1988_6STACK:
	case AD1988_6STACK_DIG:
		spec->multiout.max_channels = 8;
		spec->multiout.num_dacs = 4;
		if (is_rev2(codec))
			spec->multiout.dac_nids = ad1988_6stack_dac_nids_rev2;
		else
			spec->multiout.dac_nids = ad1988_6stack_dac_nids;
		spec->input_mux = &ad1988_6stack_capture_source;
		spec->num_mixers = 2;
		if (is_rev2(codec))
			spec->mixers[0] = ad1988_6stack_mixers1_rev2;
		else
			spec->mixers[0] = ad1988_6stack_mixers1;
		spec->mixers[1] = ad1988_6stack_mixers2;
		spec->num_init_verbs = 1;
		spec->init_verbs[0] = ad1988_6stack_init_verbs;
		if (board_config == AD1988_6STACK_DIG) {
			spec->multiout.dig_out_nid = AD1988_SPDIF_OUT;
			spec->dig_in_nid = AD1988_SPDIF_IN;
		}
		break;
	case AD1988_3STACK:
	case AD1988_3STACK_DIG:
		spec->multiout.max_channels = 6;
		spec->multiout.num_dacs = 3;
		if (is_rev2(codec))
			spec->multiout.dac_nids = ad1988_3stack_dac_nids_rev2;
		else
			spec->multiout.dac_nids = ad1988_3stack_dac_nids;
		spec->input_mux = &ad1988_6stack_capture_source;
		spec->channel_mode = ad1988_3stack_modes;
		spec->num_channel_mode = ARRAY_SIZE(ad1988_3stack_modes);
		spec->num_mixers = 2;
		if (is_rev2(codec))
			spec->mixers[0] = ad1988_3stack_mixers1_rev2;
		else
			spec->mixers[0] = ad1988_3stack_mixers1;
		spec->mixers[1] = ad1988_3stack_mixers2;
		spec->num_init_verbs = 1;
		spec->init_verbs[0] = ad1988_3stack_init_verbs;
		if (board_config == AD1988_3STACK_DIG)
			spec->multiout.dig_out_nid = AD1988_SPDIF_OUT;
		break;
	case AD1988_LAPTOP:
	case AD1988_LAPTOP_DIG:
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1988_3stack_dac_nids;
		spec->input_mux = &ad1988_laptop_capture_source;
		spec->num_mixers = 1;
		spec->mixers[0] = ad1988_laptop_mixers;
		spec->num_init_verbs = 1;
		spec->init_verbs[0] = ad1988_laptop_init_verbs;
		if (board_config == AD1988_LAPTOP_DIG)
			spec->multiout.dig_out_nid = AD1988_SPDIF_OUT;
		break;
	}

	spec->num_adc_nids = ARRAY_SIZE(ad1988_adc_nids);
	spec->adc_nids = ad1988_adc_nids;
	spec->capsrc_nids = ad1988_capsrc_nids;
	spec->mixers[spec->num_mixers++] = ad1988_capture_mixers;
	spec->init_verbs[spec->num_init_verbs++] = ad1988_capture_init_verbs;
	if (spec->multiout.dig_out_nid) {
		spec->mixers[spec->num_mixers++] = ad1988_spdif_out_mixers;
		spec->init_verbs[spec->num_init_verbs++] = ad1988_spdif_init_verbs;
	}
	if (spec->dig_in_nid)
		spec->mixers[spec->num_mixers++] = ad1988_spdif_in_mixers;

	codec->patch_ops = ad198x_patch_ops;
	switch (board_config) {
	case AD1988_AUTO:
		codec->patch_ops.init = ad1988_auto_init;
		break;
	case AD1988_LAPTOP:
	case AD1988_LAPTOP_DIG:
		codec->patch_ops.unsol_event = ad1988_laptop_unsol_event;
		break;
	}
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = ad1988_loopbacks;
#endif

	return 0;
}


/*
 * AD1884 / AD1984
 *
 * port-B - front line/mic-in
 * port-E - aux in/out
 * port-F - aux in/out
 * port-C - rear line/mic-in
 * port-D - rear line/hp-out
 * port-A - front line/hp-out
 *
 * AD1984 = AD1884 + two digital mic-ins
 *
 * FIXME:
 * For simplicity, we share the single DAC for both HP and line-outs
 * right now.  The inidividual playbacks could be easily implemented,
 * but no build-up framework is given, so far.
 */

static hda_nid_t ad1884_dac_nids[1] = {
	0x04,
};

static hda_nid_t ad1884_adc_nids[2] = {
	0x08, 0x09,
};

static hda_nid_t ad1884_capsrc_nids[2] = {
	0x0c, 0x0d,
};

#define AD1884_SPDIF_OUT	0x02

static struct hda_input_mux ad1884_capture_source = {
	.num_items = 4,
	.items = {
		{ "Front Mic", 0x0 },
		{ "Mic", 0x1 },
		{ "CD", 0x2 },
		{ "Mix", 0x3 },
	},
};

static struct snd_kcontrol_new ad1884_base_mixers[] = {
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x04, 0x0, HDA_OUTPUT),
	/* HDA_CODEC_VOLUME_IDX("PCM Playback Volume", 1, 0x03, 0x0, HDA_OUTPUT), */
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x13, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x13, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x20, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x20, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x20, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x20, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x20, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x20, 0x02, HDA_INPUT),
	/*
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x20, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x20, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("Digital Beep Playback Volume", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Digital Beep Playback Switch", 0x10, 0x0, HDA_OUTPUT),
	*/
	HDA_CODEC_VOLUME("Mic Boost", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x0d, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	/* SPDIF controls */
	HDA_CODEC_VOLUME("IEC958 Playback Volume", 0x1b, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		/* identical with ad1983 */
		.info = ad1983_spdif_route_info,
		.get = ad1983_spdif_route_get,
		.put = ad1983_spdif_route_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new ad1984_dmic_mixers[] = {
	HDA_CODEC_VOLUME("Digital Mic Capture Volume", 0x05, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Digital Mic Capture Switch", 0x05, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Digital Mic Capture Volume", 1, 0x06, 0x0,
			     HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Digital Mic Capture Switch", 1, 0x06, 0x0,
			   HDA_INPUT),
	{ } /* end */
};

/*
 * initialization verbs
 */
static struct hda_verb ad1884_init_verbs[] = {
	/* DACs; mute as default */
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* Port-A (HP) mixer */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Port-A pin */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* HP selector - select DAC2 */
	{0x22, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* Port-D (Line-out) mixer */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Port-D pin */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mono-out mixer */
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Mono-out pin */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mono selector */
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* Port-B (front mic) pin */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Port-C (rear mic) pin */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Analog mixer; mute as default */
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	/* Analog Mix output amp */
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x1f}, /* 0dB */
	/* SPDIF output selector */
	{0x02, AC_VERB_SET_CONNECT_SEL, 0x0}, /* PCM */
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x27}, /* 0dB */
	{ } /* end */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list ad1884_loopbacks[] = {
	{ 0x20, HDA_INPUT, 0 }, /* Front Mic */
	{ 0x20, HDA_INPUT, 1 }, /* Mic */
	{ 0x20, HDA_INPUT, 2 }, /* CD */
	{ 0x20, HDA_INPUT, 4 }, /* Docking */
	{ } /* end */
};
#endif

static int patch_ad1884(struct hda_codec *codec)
{
	struct ad198x_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	mutex_init(&spec->amp_mutex);
	codec->spec = spec;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(ad1884_dac_nids);
	spec->multiout.dac_nids = ad1884_dac_nids;
	spec->multiout.dig_out_nid = AD1884_SPDIF_OUT;
	spec->num_adc_nids = ARRAY_SIZE(ad1884_adc_nids);
	spec->adc_nids = ad1884_adc_nids;
	spec->capsrc_nids = ad1884_capsrc_nids;
	spec->input_mux = &ad1884_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = ad1884_base_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = ad1884_init_verbs;
	spec->spdif_route = 0;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = ad1884_loopbacks;
#endif

	codec->patch_ops = ad198x_patch_ops;

	return 0;
}

/*
 * Lenovo Thinkpad T61/X61
 */
static struct hda_input_mux ad1984_thinkpad_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
		{ "Mix", 0x3 },
	},
};

static struct snd_kcontrol_new ad1984_thinkpad_mixers[] = {
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x04, 0x0, HDA_OUTPUT),
	/* HDA_CODEC_VOLUME_IDX("PCM Playback Volume", 1, 0x03, 0x0, HDA_OUTPUT), */
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x20, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x20, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x20, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x20, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Docking Mic Playback Volume", 0x20, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("Docking Mic Playback Switch", 0x20, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Docking Mic Boost", 0x25, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x20, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x20, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x0d, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	{ } /* end */
};

/* additional verbs */
static struct hda_verb ad1984_thinkpad_init_verbs[] = {
	/* Port-E (docking station mic) pin */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* docking mic boost */
	{0x25, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Analog mixer - docking mic; mute as default */
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* enable EAPD bit */
	{0x12, AC_VERB_SET_EAPD_BTLENABLE, 0x02},
	{ } /* end */
};

/* Digial MIC ADC NID 0x05 + 0x06 */
static int ad1984_pcm_dmic_prepare(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   unsigned int stream_tag,
				   unsigned int format,
				   struct snd_pcm_substream *substream)
{
	snd_hda_codec_setup_stream(codec, 0x05 + substream->number,
				   stream_tag, 0, format);
	return 0;
}

static int ad1984_pcm_dmic_cleanup(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	snd_hda_codec_setup_stream(codec, 0x05 + substream->number,
				   0, 0, 0);
	return 0;
}

static struct hda_pcm_stream ad1984_pcm_dmic_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x05,
	.ops = {
		.prepare = ad1984_pcm_dmic_prepare,
		.cleanup = ad1984_pcm_dmic_cleanup
	},
};

static int ad1984_build_pcms(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	struct hda_pcm *info;
	int err;

	err = ad198x_build_pcms(codec);
	if (err < 0)
		return err;

	info = spec->pcm_rec + codec->num_pcms;
	codec->num_pcms++;
	info->name = "AD1984 Digital Mic";
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = ad1984_pcm_dmic_capture;
	return 0;
}

/* models */
enum {
	AD1984_BASIC,
	AD1984_THINKPAD,
	AD1984_MODELS
};

static const char *ad1984_models[AD1984_MODELS] = {
	[AD1984_BASIC]		= "basic",
	[AD1984_THINKPAD]	= "thinkpad",
};

static struct snd_pci_quirk ad1984_cfg_tbl[] = {
	/* Lenovo Thinkpad T61/X61 */
	SND_PCI_QUIRK(0x17aa, 0, "Lenovo Thinkpad", AD1984_THINKPAD),
	{}
};

static int patch_ad1984(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int board_config, err;

	err = patch_ad1884(codec);
	if (err < 0)
		return err;
	spec = codec->spec;
	board_config = snd_hda_check_board_config(codec, AD1984_MODELS,
						  ad1984_models, ad1984_cfg_tbl);
	switch (board_config) {
	case AD1984_BASIC:
		/* additional digital mics */
		spec->mixers[spec->num_mixers++] = ad1984_dmic_mixers;
		codec->patch_ops.build_pcms = ad1984_build_pcms;
		break;
	case AD1984_THINKPAD:
		spec->multiout.dig_out_nid = 0;
		spec->input_mux = &ad1984_thinkpad_capture_source;
		spec->mixers[0] = ad1984_thinkpad_mixers;
		spec->init_verbs[spec->num_init_verbs++] = ad1984_thinkpad_init_verbs;
		break;
	}
	return 0;
}


/*
 * AD1882
 *
 * port-A - front hp-out
 * port-B - front mic-in
 * port-C - rear line-in, shared surr-out (3stack)
 * port-D - rear line-out
 * port-E - rear mic-in, shared clfe-out (3stack)
 * port-F - rear surr-out (6stack)
 * port-G - rear clfe-out (6stack)
 */

static hda_nid_t ad1882_dac_nids[3] = {
	0x04, 0x03, 0x05
};

static hda_nid_t ad1882_adc_nids[2] = {
	0x08, 0x09,
};

static hda_nid_t ad1882_capsrc_nids[2] = {
	0x0c, 0x0d,
};

#define AD1882_SPDIF_OUT	0x02

/* list: 0x11, 0x39, 0x3a, 0x18, 0x3c, 0x3b, 0x12, 0x20 */
static struct hda_input_mux ad1882_capture_source = {
	.num_items = 5,
	.items = {
		{ "Front Mic", 0x1 },
		{ "Mic", 0x4 },
		{ "Line", 0x2 },
		{ "CD", 0x3 },
		{ "Mix", 0x7 },
	},
};

static struct snd_kcontrol_new ad1882_base_mixers[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x04, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x05, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x13, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x13, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x20, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x20, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x20, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x20, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x20, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x20, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x20, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x20, 0x06, HDA_INPUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x20, 0x07, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x20, 0x07, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x3c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x39, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line-In Boost", 0x3a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x0d, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	/* SPDIF controls */
	HDA_CODEC_VOLUME("IEC958 Playback Volume", 0x1b, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source",
		/* identical with ad1983 */
		.info = ad1983_spdif_route_info,
		.get = ad1983_spdif_route_get,
		.put = ad1983_spdif_route_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new ad1882_3stack_mixers[] = {
	HDA_CODEC_MUTE("Surround Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x17, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x17, 2, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = ad198x_ch_mode_info,
		.get = ad198x_ch_mode_get,
		.put = ad198x_ch_mode_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new ad1882_6stack_mixers[] = {
	HDA_CODEC_MUTE("Surround Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x24, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x24, 2, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct hda_verb ad1882_ch2_init[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x2c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x2c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ } /* end */
};

static struct hda_verb ad1882_ch4_init[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x2c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x2c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ } /* end */
};

static struct hda_verb ad1882_ch6_init[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x2c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x2c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{ } /* end */
};

static struct hda_channel_mode ad1882_modes[3] = {
	{ 2, ad1882_ch2_init },
	{ 4, ad1882_ch4_init },
	{ 6, ad1882_ch6_init },
};

/*
 * initialization verbs
 */
static struct hda_verb ad1882_init_verbs[] = {
	/* DACs; mute as default */
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* Port-A (HP) mixer */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Port-A pin */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* HP selector - select DAC2 */
	{0x37, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* Port-D (Line-out) mixer */
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Port-D pin */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mono-out mixer */
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Mono-out pin */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Port-B (front mic) pin */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x39, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO}, /* boost */
	/* Port-C (line-in) pin */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x3a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO}, /* boost */
	/* Port-C mixer - mute as input */
	{0x2c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x2c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Port-E (mic-in) pin */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x3c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO}, /* boost */
	/* Port-E mixer - mute as input */
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Port-F (surround) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Port-G (CLFE) */
	{0x24, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Analog mixer; mute as default */
	/* list: 0x39, 0x3a, 0x11, 0x12, 0x3c, 0x3b, 0x18, 0x1a */
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/* Analog Mix output amp */
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x1f}, /* 0dB */
	/* SPDIF output selector */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x27}, /* 0dB */
	{0x02, AC_VERB_SET_CONNECT_SEL, 0x0}, /* PCM */
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x27}, /* 0dB */
	{ } /* end */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list ad1882_loopbacks[] = {
	{ 0x20, HDA_INPUT, 0 }, /* Front Mic */
	{ 0x20, HDA_INPUT, 1 }, /* Mic */
	{ 0x20, HDA_INPUT, 4 }, /* Line */
	{ 0x20, HDA_INPUT, 6 }, /* CD */
	{ } /* end */
};
#endif

/* models */
enum {
	AD1882_3STACK,
	AD1882_6STACK,
	AD1882_MODELS
};

static const char *ad1882_models[AD1986A_MODELS] = {
	[AD1882_3STACK]		= "3stack",
	[AD1882_6STACK]		= "6stack",
};


static int patch_ad1882(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	mutex_init(&spec->amp_mutex);
	codec->spec = spec;

	spec->multiout.max_channels = 6;
	spec->multiout.num_dacs = 3;
	spec->multiout.dac_nids = ad1882_dac_nids;
	spec->multiout.dig_out_nid = AD1882_SPDIF_OUT;
	spec->num_adc_nids = ARRAY_SIZE(ad1882_adc_nids);
	spec->adc_nids = ad1882_adc_nids;
	spec->capsrc_nids = ad1882_capsrc_nids;
	spec->input_mux = &ad1882_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = ad1882_base_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = ad1882_init_verbs;
	spec->spdif_route = 0;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = ad1882_loopbacks;
#endif

	codec->patch_ops = ad198x_patch_ops;

	/* override some parameters */
	board_config = snd_hda_check_board_config(codec, AD1882_MODELS,
						  ad1882_models, NULL);
	switch (board_config) {
	default:
	case AD1882_3STACK:
		spec->num_mixers = 2;
		spec->mixers[1] = ad1882_3stack_mixers;
		spec->channel_mode = ad1882_modes;
		spec->num_channel_mode = ARRAY_SIZE(ad1882_modes);
		spec->need_dac_fix = 1;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		break;
	case AD1882_6STACK:
		spec->num_mixers = 2;
		spec->mixers[1] = ad1882_6stack_mixers;
		break;
	}
	return 0;
}


/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_analog[] = {
	{ .id = 0x11d41882, .name = "AD1882", .patch = patch_ad1882 },
	{ .id = 0x11d41884, .name = "AD1884", .patch = patch_ad1884 },
	{ .id = 0x11d41981, .name = "AD1981", .patch = patch_ad1981 },
	{ .id = 0x11d41983, .name = "AD1983", .patch = patch_ad1983 },
	{ .id = 0x11d41984, .name = "AD1984", .patch = patch_ad1984 },
	{ .id = 0x11d41986, .name = "AD1986A", .patch = patch_ad1986a },
	{ .id = 0x11d41988, .name = "AD1988", .patch = patch_ad1988 },
	{ .id = 0x11d4198b, .name = "AD1988B", .patch = patch_ad1988 },
	{} /* terminator */
};
