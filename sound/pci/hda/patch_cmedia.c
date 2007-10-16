/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for C-Media CMI9880
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
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

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#define NUM_PINS	11


/* board config type */
enum {
	CMI_MINIMAL,	/* back 3-jack */
	CMI_MIN_FP,	/* back 3-jack + front-panel 2-jack */
	CMI_FULL,	/* back 6-jack + front-panel 2-jack */
	CMI_FULL_DIG,	/* back 6-jack + front-panel 2-jack + digital I/O */
	CMI_ALLOUT,	/* back 5-jack + front-panel 2-jack + digital out */
	CMI_AUTO,	/* let driver guess it */
	CMI_MODELS
};

struct cmi_spec {
	int board_config;
	unsigned int no_line_in: 1;	/* no line-in (5-jack) */
	unsigned int front_panel: 1;	/* has front-panel 2-jack */

	/* playback */
	struct hda_multi_out multiout;
	hda_nid_t dac_nids[4];		/* NID for each DAC */
	int num_dacs;

	/* capture */
	hda_nid_t *adc_nids;
	hda_nid_t dig_in_nid;

	/* capture source */
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[2];

	/* channel mode */
	int num_channel_modes;
	const struct hda_channel_mode *channel_modes;

	struct hda_pcm pcm_rec[2];	/* PCM information */

	/* pin deafault configuration */
	hda_nid_t pin_nid[NUM_PINS];
	unsigned int def_conf[NUM_PINS];
	unsigned int pin_def_confs;

	/* multichannel pins */
	hda_nid_t multich_pin[4];	/* max 8-channel */
	struct hda_verb multi_init[9];	/* 2 verbs for each pin + terminator */
};

/*
 * input MUX
 */
static int cmi_mux_enum_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int cmi_mux_enum_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int cmi_mux_enum_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->adc_nids[adc_idx], &spec->cur_mux[adc_idx]);
}

/*
 * shared line-in, mic for surrounds
 */

/* 3-stack / 2 channel */
static struct hda_verb cmi9880_ch2_init[] = {
	/* set line-in PIN for input */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	/* set mic PIN for input, also enable vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	/* route front PCM (DAC1) to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{}
};

/* 3-stack / 6 channel */
static struct hda_verb cmi9880_ch6_init[] = {
	/* set line-in PIN for output */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	/* set mic PIN for output */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	/* route front PCM (DAC1) to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{}
};

/* 3-stack+front / 8 channel */
static struct hda_verb cmi9880_ch8_init[] = {
	/* set line-in PIN for output */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	/* set mic PIN for output */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	/* route rear-surround PCM (DAC4) to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{}
};

static struct hda_channel_mode cmi9880_channel_modes[3] = {
	{ 2, cmi9880_ch2_init },
	{ 6, cmi9880_ch6_init },
	{ 8, cmi9880_ch8_init },
};

static int cmi_ch_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;
	return snd_hda_ch_mode_info(codec, uinfo, spec->channel_modes,
				    spec->num_channel_modes);
}

static int cmi_ch_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;
	return snd_hda_ch_mode_get(codec, ucontrol, spec->channel_modes,
				   spec->num_channel_modes, spec->multiout.max_channels);
}

static int cmi_ch_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;
	return snd_hda_ch_mode_put(codec, ucontrol, spec->channel_modes,
				   spec->num_channel_modes, &spec->multiout.max_channels);
}

/*
 */
static struct snd_kcontrol_new cmi9880_basic_mixer[] = {
	/* CMI9880 has no playback volumes! */
	HDA_CODEC_MUTE("PCM Playback Switch", 0x03, 0x0, HDA_OUTPUT), /* front */
	HDA_CODEC_MUTE("Surround Playback Switch", 0x04, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Side Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = cmi_mux_enum_info,
		.get = cmi_mux_enum_get,
		.put = cmi_mux_enum_put,
	},
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x23, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x23, 0, HDA_OUTPUT),
	{ } /* end */
};

/*
 * shared I/O pins
 */
static struct snd_kcontrol_new cmi9880_ch_mode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = cmi_ch_mode_info,
		.get = cmi_ch_mode_get,
		.put = cmi_ch_mode_put,
	},
	{ } /* end */
};

/* AUD-in selections:
 * 0x0b 0x0c 0x0d 0x0e 0x0f 0x10 0x11 0x1f 0x20
 */
static struct hda_input_mux cmi9880_basic_mux = {
	.num_items = 4,
	.items = {
		{ "Front Mic", 0x5 },
		{ "Rear Mic", 0x2 },
		{ "Line", 0x1 },
		{ "CD", 0x7 },
	}
};

static struct hda_input_mux cmi9880_no_line_mux = {
	.num_items = 3,
	.items = {
		{ "Front Mic", 0x5 },
		{ "Rear Mic", 0x2 },
		{ "CD", 0x7 },
	}
};

/* front, rear, clfe, rear_surr */
static hda_nid_t cmi9880_dac_nids[4] = {
	0x03, 0x04, 0x05, 0x06
};
/* ADC0, ADC1 */
static hda_nid_t cmi9880_adc_nids[2] = {
	0x08, 0x09
};

#define CMI_DIG_OUT_NID	0x07
#define CMI_DIG_IN_NID	0x0a

/*
 */
static struct hda_verb cmi9880_basic_init[] = {
	/* port-D for line out (rear panel) */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x1f, AC_VERB_SET_CONNECT_SEL, 0x02 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x01 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	/* route front mic to ADC1/2 */
	{ 0x08, AC_VERB_SET_CONNECT_SEL, 0x05 },
	{ 0x09, AC_VERB_SET_CONNECT_SEL, 0x05 },
	{} /* terminator */
};

static struct hda_verb cmi9880_allout_init[] = {
	/* port-D for line out (rear panel) */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-A for side (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x1f, AC_VERB_SET_CONNECT_SEL, 0x02 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x01 },
	/* port-C for surround (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	/* route front mic to ADC1/2 */
	{ 0x08, AC_VERB_SET_CONNECT_SEL, 0x05 },
	{ 0x09, AC_VERB_SET_CONNECT_SEL, 0x05 },
	{} /* terminator */
};

/*
 */
static int cmi9880_build_controls(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;
	int err;

	err = snd_hda_add_new_ctls(codec, cmi9880_basic_mixer);
	if (err < 0)
		return err;
	if (spec->channel_modes) {
		err = snd_hda_add_new_ctls(codec, cmi9880_ch_mode_mixer);
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

/* fill in the multi_dac_nids table, which will decide
   which audio widget to use for each channel */
static int cmi9880_fill_multi_dac_nids(struct hda_codec *codec, const struct auto_pin_cfg *cfg)
{
	struct cmi_spec *spec = codec->spec;
	hda_nid_t nid;
	int assigned[4];
	int i, j;

	/* clear the table, only one c-media dac assumed here */
	memset(spec->dac_nids, 0, sizeof(spec->dac_nids));
	memset(assigned, 0, sizeof(assigned));
	/* check the pins we found */
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		/* nid 0x0b~0x0e is hardwired to audio widget 0x3~0x6 */
		if (nid >= 0x0b && nid <= 0x0e) {
			spec->dac_nids[i] = (nid - 0x0b) + 0x03;
			assigned[nid - 0x0b] = 1;
		}
	}
	/* left pin can be connect to any audio widget */
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		if (nid <= 0x0e)
			continue;
		/* search for an empty channel */
		for (j = 0; j < cfg->line_outs; j++) {
			if (! assigned[j]) {
				spec->dac_nids[i] = j + 0x03;
				assigned[j] = 1;
				break;
			}
		}
	}
	spec->num_dacs = cfg->line_outs;
	return 0;
}

/* create multi_init table, which is used for multichannel initialization */
static int cmi9880_fill_multi_init(struct hda_codec *codec, const struct auto_pin_cfg *cfg)
{
	struct cmi_spec *spec = codec->spec;
	hda_nid_t nid;
	int i, j, k, len;

	/* clear the table, only one c-media dac assumed here */
	memset(spec->multi_init, 0, sizeof(spec->multi_init));
	for (j = 0, i = 0; i < cfg->line_outs; i++) {
		hda_nid_t conn[4];
		nid = cfg->line_out_pins[i];
		/* set as output */
		spec->multi_init[j].nid = nid;
		spec->multi_init[j].verb = AC_VERB_SET_PIN_WIDGET_CONTROL;
		spec->multi_init[j].param = PIN_OUT;
		j++;
		if (nid > 0x0e) {
			/* set connection */
			spec->multi_init[j].nid = nid;
			spec->multi_init[j].verb = AC_VERB_SET_CONNECT_SEL;
			spec->multi_init[j].param = 0;
			/* find the index in connect list */
			len = snd_hda_get_connections(codec, nid, conn, 4);
			for (k = 0; k < len; k++)
				if (conn[k] == spec->dac_nids[i]) {
					spec->multi_init[j].param = k;
					break;
				}
			j++;
		}
	}
	return 0;
}

static int cmi9880_init(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;
	if (spec->board_config == CMI_ALLOUT)
		snd_hda_sequence_write(codec, cmi9880_allout_init);
	else
		snd_hda_sequence_write(codec, cmi9880_basic_init);
	if (spec->board_config == CMI_AUTO)
		snd_hda_sequence_write(codec, spec->multi_init);
	return 0;
}

/*
 * Analog playback callbacks
 */
static int cmi9880_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream);
}

static int cmi9880_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag,
						format, substream);
}

static int cmi9880_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int cmi9880_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int cmi9880_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int cmi9880_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					    struct hda_codec *codec,
					    unsigned int stream_tag,
					    unsigned int format,
					    struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					     format, substream);
}

/*
 * Analog capture
 */
static int cmi9880_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int cmi9880_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct cmi_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number], 0, 0, 0);
	return 0;
}


/*
 */
static struct hda_pcm_stream cmi9880_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x03, /* NID to query formats and rates */
	.ops = {
		.open = cmi9880_playback_pcm_open,
		.prepare = cmi9880_playback_pcm_prepare,
		.cleanup = cmi9880_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream cmi9880_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x08, /* NID to query formats and rates */
	.ops = {
		.prepare = cmi9880_capture_pcm_prepare,
		.cleanup = cmi9880_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream cmi9880_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in cmi9880_build_pcms */
	.ops = {
		.open = cmi9880_dig_playback_pcm_open,
		.close = cmi9880_dig_playback_pcm_close,
		.prepare = cmi9880_dig_playback_pcm_prepare
	},
};

static struct hda_pcm_stream cmi9880_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in cmi9880_build_pcms */
};

static int cmi9880_build_pcms(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "CMI9880";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = cmi9880_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = cmi9880_pcm_analog_capture;

	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		codec->num_pcms++;
		info++;
		info->name = "CMI9880 Digital";
		if (spec->multiout.dig_out_nid) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = cmi9880_pcm_digital_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = cmi9880_pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
	}

	return 0;
}

static void cmi9880_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}

/*
 */

static const char *cmi9880_models[CMI_MODELS] = {
	[CMI_MINIMAL]	= "minimal",
	[CMI_MIN_FP]	= "min_fp",
	[CMI_FULL]	= "full",
	[CMI_FULL_DIG]	= "full_dig",
	[CMI_ALLOUT]	= "allout",
	[CMI_AUTO]	= "auto",
};

static struct snd_pci_quirk cmi9880_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x813d, "ASUS P5AD2", CMI_FULL_DIG),
	{} /* terminator */
};

static struct hda_codec_ops cmi9880_patch_ops = {
	.build_controls = cmi9880_build_controls,
	.build_pcms = cmi9880_build_pcms,
	.init = cmi9880_init,
	.free = cmi9880_free,
};

static int patch_cmi9880(struct hda_codec *codec)
{
	struct cmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->board_config = snd_hda_check_board_config(codec, CMI_MODELS,
							cmi9880_models,
							cmi9880_cfg_tbl);
	if (spec->board_config < 0) {
		snd_printdd(KERN_INFO "hda_codec: Unknown model for CMI9880\n");
		spec->board_config = CMI_AUTO; /* try everything */
	}

	/* copy default DAC NIDs */
	memcpy(spec->dac_nids, cmi9880_dac_nids, sizeof(spec->dac_nids));
	spec->num_dacs = 4;

	switch (spec->board_config) {
	case CMI_MINIMAL:
	case CMI_MIN_FP:
		spec->channel_modes = cmi9880_channel_modes;
		if (spec->board_config == CMI_MINIMAL)
			spec->num_channel_modes = 2;
		else {
			spec->front_panel = 1;
			spec->num_channel_modes = 3;
		}
		spec->multiout.max_channels = cmi9880_channel_modes[0].channels;
		spec->input_mux = &cmi9880_basic_mux;
		break;
	case CMI_FULL:
	case CMI_FULL_DIG:
		spec->front_panel = 1;
		spec->multiout.max_channels = 8;
		spec->input_mux = &cmi9880_basic_mux;
		if (spec->board_config == CMI_FULL_DIG) {
			spec->multiout.dig_out_nid = CMI_DIG_OUT_NID;
			spec->dig_in_nid = CMI_DIG_IN_NID;
		}
		break;
	case CMI_ALLOUT:
		spec->front_panel = 1;
		spec->multiout.max_channels = 8;
		spec->no_line_in = 1;
		spec->input_mux = &cmi9880_no_line_mux;
		spec->multiout.dig_out_nid = CMI_DIG_OUT_NID;
		break;
	case CMI_AUTO:
		{
		unsigned int port_e, port_f, port_g, port_h;
		unsigned int port_spdifi, port_spdifo;
		struct auto_pin_cfg cfg;

		/* collect pin default configuration */
		port_e = snd_hda_codec_read(codec, 0x0f, 0, AC_VERB_GET_CONFIG_DEFAULT, 0);
		port_f = snd_hda_codec_read(codec, 0x10, 0, AC_VERB_GET_CONFIG_DEFAULT, 0);
		spec->front_panel = 1;
		if (get_defcfg_connect(port_e) == AC_JACK_PORT_NONE ||
		    get_defcfg_connect(port_f) == AC_JACK_PORT_NONE) {
			port_g = snd_hda_codec_read(codec, 0x1f, 0, AC_VERB_GET_CONFIG_DEFAULT, 0);
			port_h = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_CONFIG_DEFAULT, 0);
			spec->channel_modes = cmi9880_channel_modes;
			/* no front panel */
			if (get_defcfg_connect(port_g) == AC_JACK_PORT_NONE ||
			    get_defcfg_connect(port_h) == AC_JACK_PORT_NONE) {
				/* no optional rear panel */
				spec->board_config = CMI_MINIMAL;
				spec->front_panel = 0;
				spec->num_channel_modes = 2;
			} else {
				spec->board_config = CMI_MIN_FP;
				spec->num_channel_modes = 3;
			}
			spec->input_mux = &cmi9880_basic_mux;
			spec->multiout.max_channels = cmi9880_channel_modes[0].channels;
		} else {
			spec->input_mux = &cmi9880_basic_mux;
			port_spdifi = snd_hda_codec_read(codec, 0x13, 0, AC_VERB_GET_CONFIG_DEFAULT, 0);
			port_spdifo = snd_hda_codec_read(codec, 0x12, 0, AC_VERB_GET_CONFIG_DEFAULT, 0);
			if (get_defcfg_connect(port_spdifo) != AC_JACK_PORT_NONE)
				spec->multiout.dig_out_nid = CMI_DIG_OUT_NID;
			if (get_defcfg_connect(port_spdifi) != AC_JACK_PORT_NONE)
				spec->dig_in_nid = CMI_DIG_IN_NID;
			spec->multiout.max_channels = 8;
		}
		snd_hda_parse_pin_def_config(codec, &cfg, NULL);
		if (cfg.line_outs) {
			spec->multiout.max_channels = cfg.line_outs * 2;
			cmi9880_fill_multi_dac_nids(codec, &cfg);
			cmi9880_fill_multi_init(codec, &cfg);
		} else
			snd_printd("patch_cmedia: cannot detect association in defcfg\n");
		break;
		}
	}

	spec->multiout.num_dacs = spec->num_dacs;
	spec->multiout.dac_nids = spec->dac_nids;

	spec->adc_nids = cmi9880_adc_nids;

	codec->patch_ops = cmi9880_patch_ops;

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_cmedia[] = {
	{ .id = 0x13f69880, .name = "CMI9880", .patch = patch_cmi9880 },
 	{ .id = 0x434d4980, .name = "CMI9880", .patch = patch_cmi9880 },
	{} /* terminator */
};
