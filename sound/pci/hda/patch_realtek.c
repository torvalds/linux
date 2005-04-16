/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for ALC 260/880/882 codecs
 *
 * Copyright (c) 2004 PeiSen Hou <pshou@realtek.com.tw>
 *                    Takashi Iwai <tiwai@suse.de>
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


/* ALC880 board config type */
enum {
	ALC880_MINIMAL,
	ALC880_3ST,
	ALC880_3ST_DIG,
	ALC880_5ST,
	ALC880_5ST_DIG,
	ALC880_W810,
};

struct alc_spec {
	/* codec parameterization */
	unsigned int front_panel: 1;

	snd_kcontrol_new_t* mixers[2];
	unsigned int num_mixers;

	struct hda_verb *init_verbs;

	char* stream_name_analog;
	struct hda_pcm_stream *stream_analog_playback;
	struct hda_pcm_stream *stream_analog_capture;

	char* stream_name_digital;
	struct hda_pcm_stream *stream_digital_playback;
	struct hda_pcm_stream *stream_digital_capture;

	/* playback */
	struct hda_multi_out multiout;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t dig_in_nid;

	/* capture source */
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];

	/* channel model */
	const struct alc_channel_mode *channel_mode;
	int num_channel_mode;

	/* PCM information */
	struct hda_pcm pcm_rec[2];
};

/* DAC/ADC assignment */

static hda_nid_t alc880_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x05, 0x04, 0x03
};

static hda_nid_t alc880_w810_dac_nids[3] = {
	/* front, rear/surround, clfe */
	0x02, 0x03, 0x04
};

static hda_nid_t alc880_adc_nids[3] = {
	/* ADC0-2 */
	0x07, 0x08, 0x09,
};

#define ALC880_DIGOUT_NID	0x06
#define ALC880_DIGIN_NID	0x0a

static hda_nid_t alc260_dac_nids[1] = {
	/* front */
	0x02,
};

static hda_nid_t alc260_adc_nids[2] = {
	/* ADC0-1 */
	0x04, 0x05,
};

#define ALC260_DIGOUT_NID	0x03
#define ALC260_DIGIN_NID	0x06

static struct hda_input_mux alc880_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x3 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc260_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/*
 * input MUX handling
 */
static int alc_mux_enum_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int alc_mux_enum_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int alc_mux_enum_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->adc_nids[adc_idx], &spec->cur_mux[adc_idx]);
}

/*
 * channel mode setting
 */
struct alc_channel_mode {
	int channels;
	const struct hda_verb *sequence;
};


/*
 * channel source setting (2/6 channel selection for 3-stack)
 */

/*
 * set the path ways for 2 channel output
 * need to set the codec line out and mic 1 pin widgets to inputs
 */
static struct hda_verb alc880_threestack_ch2_init[] = {
	/* set pin widget 1Ah (line in) for input */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* set pin widget 18h (mic1) for input, for mic also enable the vref */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* mute the output for Line In PW */
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
	/* mute for Mic1 PW */
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
	{ } /* end */
};

/*
 * 6ch mode
 * need to set the codec line out and mic 1 pin widgets to outputs
 */
static struct hda_verb alc880_threestack_ch6_init[] = {
	/* set pin widget 1Ah (line in) for output */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* set pin widget 18h (mic1) for output */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* unmute the output for Line In PW */
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000 },
	/* unmute for Mic1 PW */
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000 },
	/* for rear channel output using Line In 1
	 * set select widget connection (nid = 0x12) - to summer node
	 * for rear NID = 0x0f...offset 3 in connection list
	 */
	{ 0x12, AC_VERB_SET_CONNECT_SEL, 0x3 },
	/* for Mic1 - retask for center/lfe */
	/* set select widget connection (nid = 0x10) - to summer node for
	 * front CLFE NID = 0x0e...offset 2 in connection list
	 */
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x2 },
	{ } /* end */
};

static struct alc_channel_mode alc880_threestack_modes[2] = {
	{ 2, alc880_threestack_ch2_init },
	{ 6, alc880_threestack_ch6_init },
};


/*
 * channel source setting (6/8 channel selection for 5-stack)
 */

/* set the path ways for 6 channel output
 * need to set the codec line out and mic 1 pin widgets to inputs
 */
static struct hda_verb alc880_fivestack_ch6_init[] = {
	/* set pin widget 1Ah (line in) for input */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* mute the output for Line In PW */
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
	{ } /* end */
};

/* need to set the codec line out and mic 1 pin widgets to outputs */
static struct hda_verb alc880_fivestack_ch8_init[] = {
	/* set pin widget 1Ah (line in) for output */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* unmute the output for Line In PW */
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000 },
	/* output for surround channel output using Line In 1 */
	/* set select widget connection (nid = 0x12) - to summer node
	 * for surr_rear NID = 0x0d...offset 1 in connection list
	 */
	{ 0x12, AC_VERB_SET_CONNECT_SEL, 0x1 },
	{ } /* end */
};

static struct alc_channel_mode alc880_fivestack_modes[2] = {
	{ 6, alc880_fivestack_ch6_init },
	{ 8, alc880_fivestack_ch8_init },
};

/*
 * channel source setting for W810 system
 *
 * W810 has rear IO for:
 * Front (DAC 02)
 * Surround (DAC 03)
 * Center/LFE (DAC 04)
 * Digital out (06)
 *
 * The system also has a pair of internal speakers, and a headphone jack.
 * These are both connected to Line2 on the codec, hence to DAC 02.
 * 
 * There is a variable resistor to control the speaker or headphone
 * volume. This is a hardware-only device without a software API.
 *
 * Plugging headphones in will disable the internal speakers. This is
 * implemented in hardware, not via the driver using jack sense. In
 * a similar fashion, plugging into the rear socket marked "front" will
 * disable both the speakers and headphones.
 *
 * For input, there's a microphone jack, and an "audio in" jack.
 * These may not do anything useful with this driver yet, because I
 * haven't setup any initialization verbs for these yet...
 */

static struct alc_channel_mode alc880_w810_modes[1] = {
	{ 6, NULL }
};

/*
 */
static int alc880_ch_mode_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;

	snd_assert(spec->channel_mode, return -ENXIO);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= 2)
		uinfo->value.enumerated.item = 1;
	sprintf(uinfo->value.enumerated.name, "%dch",
		spec->channel_mode[uinfo->value.enumerated.item].channels);
	return 0;
}

static int alc880_ch_mode_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;

	snd_assert(spec->channel_mode, return -ENXIO);
	ucontrol->value.enumerated.item[0] =
		(spec->multiout.max_channels == spec->channel_mode[0].channels) ? 0 : 1;
	return 0;
}

static int alc880_ch_mode_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int mode;

	snd_assert(spec->channel_mode, return -ENXIO);
	mode = ucontrol->value.enumerated.item[0] ? 1 : 0;
	if (spec->multiout.max_channels == spec->channel_mode[mode].channels &&
	    ! codec->in_resume)
		return 0;

	/* change the current channel setting */
	spec->multiout.max_channels = spec->channel_mode[mode].channels;
	if (spec->channel_mode[mode].sequence)
		snd_hda_sequence_write(codec, spec->channel_mode[mode].sequence);

	return 1;
}


/*
 */

/* 3-stack mode
 * Pin assignment: Front=0x14, Line-In/Rear=0x1a, Mic/CLFE=0x18, F-Mic=0x1b
 *                 HP=0x19
 */
static snd_kcontrol_new_t alc880_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x18, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x18, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x19, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc880_ch_mode_info,
		.get = alc880_ch_mode_get,
		.put = alc880_ch_mode_put,
	},
	{ } /* end */
};

/* 5-stack mode
 * Pin assignment: Front=0x14, Rear=0x17, CLFE=0x16
 *                 Line-In/Side=0x1a, Mic=0x18, F-Mic=0x1b, HP=0x19
 */
static snd_kcontrol_new_t alc880_five_stack_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x16, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x16, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Side Playback Switch", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x19, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc880_ch_mode_info,
		.get = alc880_ch_mode_get,
		.put = alc880_ch_mode_put,
	},
	{ } /* end */
};

static snd_kcontrol_new_t alc880_w810_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x16, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x16, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 2, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 2, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 3,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

/*
 */
static int alc_build_controls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	int i;

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

/*
 * initialize the codec volumes, etc
 */

static struct hda_verb alc880_init_verbs_three_stack[] = {
	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Mic2 (front panel) pin widget for input and vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* unmute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},
	/* set connection select to line in (default select for this ADC) */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* unmute front mixer amp left (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* unmute rear mixer amp left and right (volume = 0) */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* unmute rear mixer amp left and right (volume = 0) */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},

	/* using rear surround as the path for headphone output */
	/* unmute rear surround mixer amp left and right (volume = 0) */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* PASD 3 stack boards use the Mic 2 as the headphone output */
	/* need to program the selector associated with the Mic 2 pin widget to
	 * surround path (index 0x01) for headphone output */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* need to retask the Mic 2 pin widget to output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) for mixer widget(nid=0x0B)
	 * to support the input path of analog loopback
	 * Note: PASD motherboards uses the Line In 2 as the input for front panel
	 * mic (mic 2)
	 */
	/* Amp Indexes: CD = 0x04, Line In 1 = 0x02, Mic 1 = 0x00 & Line In 2 = 0x03 */
	/* unmute CD */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
	/* unmute Line In */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	/* unmute Mic 1 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	/* unmute Line In 2 (for PASD boards Mic 2) */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},

	/* Unmute input amps for the line out paths to support the output path of
	 * analog loopback
	 * the mixers on the output path has 2 inputs, one from the DAC and one
	 * from the mixer
	 */
	/* Amp Indexes: DAC = 0x01 & mixer = 0x00 */
	/* Unmute Front out path */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Surround (used as HP) out path */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute C/LFE out path */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8))}, /* mute */
	/* Unmute rear Surround out path */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},

	{ }
};

static struct hda_verb alc880_init_verbs_five_stack[] = {
	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Mic2 (front panel) pin widget for input and vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* unmute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},
	/* set connection select to line in (default select for this ADC) */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* unmute front mixer amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* five rear and clfe */
	/* unmute rear mixer amp left and right (volume = 0)  */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* unmute clfe mixer amp left and right (volume = 0) */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},

	/* using rear surround as the path for headphone output */
	/* unmute rear surround mixer amp left and right (volume = 0) */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* PASD 3 stack boards use the Mic 2 as the headphone output */
	/* need to program the selector associated with the Mic 2 pin widget to
	 * surround path (index 0x01) for headphone output
	 */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* need to retask the Mic 2 pin widget to output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) for mixer
	 * widget(nid=0x0B) to support the input path of analog loopback
	 */
	/* Note: PASD motherboards uses the Line In 2 as the input for front panel mic (mic 2) */
	/* Amp Indexes: CD = 0x04, Line In 1 = 0x02, Mic 1 = 0x00 & Line In 2 = 0x03*/
	/* unmute CD */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
	/* unmute Line In */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	/* unmute Mic 1 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	/* unmute Line In 2 (for PASD boards Mic 2) */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},

	/* Unmute input amps for the line out paths to support the output path of
	 * analog loopback
	 * the mixers on the output path has 2 inputs, one from the DAC and
	 * one from the mixer
	 */
	/* Amp Indexes: DAC = 0x01 & mixer = 0x00 */
	/* Unmute Front out path */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Surround (used as HP) out path */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute C/LFE out path */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8))}, /* mute */
	/* Unmute rear Surround out path */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},

	{ }
};

static struct hda_verb alc880_w810_init_verbs[] = {
	/* front channel selector/amp: input 0: DAC: unmuted, (no volume selection) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},

	/* front channel selector/amp: input 1: capture mix: muted, (no volume selection) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, 0x7180},

	/* front channel selector/amp: output 0: unmuted, max volume */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},

	/* front out pin: muted, (no volume selection)  */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},

	/* front out pin: NOT headphone enable, out enable, vref disabled */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},


	/* surround channel selector/amp: input 0: DAC: unmuted, (no volume selection) */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},

	/* surround channel selector/amp: input 1: capture mix: muted, (no volume selection) */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, 0x7180},

	/* surround channel selector/amp: output 0: unmuted, max volume */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},

	/* surround out pin: muted, (no volume selection)  */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},

	/* surround out pin: NOT headphone enable, out enable, vref disabled */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},


	/* c/lfe channel selector/amp: input 0: DAC: unmuted, (no volume selection) */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},

	/* c/lfe channel selector/amp: input 1: capture mix: muted, (no volume selection) */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, 0x7180},

	/* c/lfe channel selector/amp: output 0: unmuted, max volume */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},

	/* c/lfe out pin: muted, (no volume selection)  */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},

	/* c/lfe out pin: NOT headphone enable, out enable, vref disabled */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},


	/* hphone/speaker input selector: front DAC */
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x0},

	/* hphone/speaker out pin: muted, (no volume selection)  */
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},

	/* hphone/speaker out pin: NOT headphone enable, out enable, vref disabled */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},


	{ }
};

static int alc_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	snd_hda_sequence_write(codec, spec->init_verbs);
	return 0;
}

#ifdef CONFIG_PM
/*
 * resume
 */
static int alc_resume(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_init(codec);
	for (i = 0; i < spec->num_mixers; i++) {
		snd_hda_resume_ctls(codec, spec->mixers[i]);
	}
	if (spec->multiout.dig_out_nid)
		snd_hda_resume_spdif_out(codec);
	if (spec->dig_in_nid)
		snd_hda_resume_spdif_in(codec);

	return 0;
}
#endif

/*
 * Analog playback callbacks
 */
static int alc880_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    snd_pcm_substream_t *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream);
}

static int alc880_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       snd_pcm_substream_t *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag,
						format, substream);
}

static int alc880_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       snd_pcm_substream_t *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int alc880_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					snd_pcm_substream_t *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int alc880_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 snd_pcm_substream_t *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int alc880_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      snd_pcm_substream_t *substream)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int alc880_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      snd_pcm_substream_t *substream)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number], 0, 0, 0);
	return 0;
}


/*
 */
static struct hda_pcm_stream alc880_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x02, /* NID to query formats and rates */
	.ops = {
		.open = alc880_playback_pcm_open,
		.prepare = alc880_playback_pcm_prepare,
		.cleanup = alc880_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream alc880_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x07, /* NID to query formats and rates */
	.ops = {
		.prepare = alc880_capture_pcm_prepare,
		.cleanup = alc880_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream alc880_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc880_dig_playback_pcm_open,
		.close = alc880_dig_playback_pcm_close
	},
};

static struct hda_pcm_stream alc880_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static int alc_build_pcms(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;
	int i;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = spec->stream_name_analog;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *(spec->stream_analog_playback);
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = *(spec->stream_analog_capture);

	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = 0;
	for (i = 0; i < spec->num_channel_mode; i++) {
		if (spec->channel_mode[i].channels > info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max) {
		    info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = spec->channel_mode[i].channels;
		}
	}

	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		codec->num_pcms++;
		info++;
		info->name = spec->stream_name_digital;
		if (spec->multiout.dig_out_nid) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *(spec->stream_digital_playback);
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = *(spec->stream_digital_capture);
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
	}

	return 0;
}

static void alc_free(struct hda_codec *codec)
{
	kfree(codec->spec);
}

/*
 */
static struct hda_codec_ops alc_patch_ops = {
	.build_controls = alc_build_controls,
	.build_pcms = alc_build_pcms,
	.init = alc_init,
	.free = alc_free,
#ifdef CONFIG_PM
	.resume = alc_resume,
#endif
};

/*
 */

static struct hda_board_config alc880_cfg_tbl[] = {
	/* Back 3 jack, front 2 jack */
	{ .modelname = "3stack", .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe200, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe201, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe202, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe203, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe204, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe205, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe206, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe207, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe208, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe209, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe20a, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe20b, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe20c, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe20d, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe20e, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe20f, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe210, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe211, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe214, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe302, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe303, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe304, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe306, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe307, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xe404, .config = ALC880_3ST },
	{ .pci_vendor = 0x8086, .pci_device = 0xa101, .config = ALC880_3ST },
	{ .pci_vendor = 0x107b, .pci_device = 0x3031, .config = ALC880_3ST },
	{ .pci_vendor = 0x107b, .pci_device = 0x4036, .config = ALC880_3ST },
	{ .pci_vendor = 0x107b, .pci_device = 0x4037, .config = ALC880_3ST },
	{ .pci_vendor = 0x107b, .pci_device = 0x4038, .config = ALC880_3ST },
	{ .pci_vendor = 0x107b, .pci_device = 0x4040, .config = ALC880_3ST },
	{ .pci_vendor = 0x107b, .pci_device = 0x4041, .config = ALC880_3ST },

	/* Back 3 jack, front 2 jack (Internal add Aux-In) */
	{ .pci_vendor = 0x1025, .pci_device = 0xe310, .config = ALC880_3ST },

	/* Back 3 jack plus 1 SPDIF out jack, front 2 jack */
	{ .modelname = "3stack-digout", .config = ALC880_3ST_DIG },
	{ .pci_vendor = 0x8086, .pci_device = 0xe308, .config = ALC880_3ST_DIG },

	/* Back 3 jack plus 1 SPDIF out jack, front 2 jack (Internal add Aux-In)*/
	{ .pci_vendor = 0x8086, .pci_device = 0xe305, .config = ALC880_3ST_DIG },
	{ .pci_vendor = 0x8086, .pci_device = 0xd402, .config = ALC880_3ST_DIG },
	{ .pci_vendor = 0x1025, .pci_device = 0xe309, .config = ALC880_3ST_DIG },

	/* Back 5 jack, front 2 jack */
	{ .modelname = "5stack", .config = ALC880_5ST },
	{ .pci_vendor = 0x107b, .pci_device = 0x3033, .config = ALC880_5ST },
	{ .pci_vendor = 0x107b, .pci_device = 0x4039, .config = ALC880_5ST },
	{ .pci_vendor = 0x107b, .pci_device = 0x3032, .config = ALC880_5ST },
	{ .pci_vendor = 0x103c, .pci_device = 0x2a09, .config = ALC880_5ST },

	/* Back 5 jack plus 1 SPDIF out jack, front 2 jack */
	{ .modelname = "5stack-digout", .config = ALC880_5ST_DIG },
	{ .pci_vendor = 0x8086, .pci_device = 0xe224, .config = ALC880_5ST_DIG },
	{ .pci_vendor = 0x8086, .pci_device = 0xe400, .config = ALC880_5ST_DIG },
	{ .pci_vendor = 0x8086, .pci_device = 0xe401, .config = ALC880_5ST_DIG },
	{ .pci_vendor = 0x8086, .pci_device = 0xe402, .config = ALC880_5ST_DIG },
	{ .pci_vendor = 0x8086, .pci_device = 0xd400, .config = ALC880_5ST_DIG },
	{ .pci_vendor = 0x8086, .pci_device = 0xd401, .config = ALC880_5ST_DIG },
	{ .pci_vendor = 0x8086, .pci_device = 0xa100, .config = ALC880_5ST_DIG },
	{ .pci_vendor = 0x1565, .pci_device = 0x8202, .config = ALC880_5ST_DIG },

	{ .modelname = "w810", .config = ALC880_W810 },
	{ .pci_vendor = 0x161f, .pci_device = 0x203d, .config = ALC880_W810 },

	{}
};

static int patch_alc880(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;

	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, alc880_cfg_tbl);
	if (board_config < 0) {
		snd_printd(KERN_INFO "hda_codec: Unknown model for ALC880\n");
		board_config = ALC880_MINIMAL;
	}

	switch (board_config) {
	case ALC880_W810:
		spec->mixers[spec->num_mixers] = alc880_w810_base_mixer;
		spec->num_mixers++;
		break;
	case ALC880_5ST:
	case ALC880_5ST_DIG:
		spec->mixers[spec->num_mixers] = alc880_five_stack_mixer;
		spec->num_mixers++;
		break;
	default:
		spec->mixers[spec->num_mixers] = alc880_base_mixer;
		spec->num_mixers++;
		break;
	}

	switch (board_config) {
	case ALC880_3ST_DIG:
	case ALC880_5ST_DIG:
	case ALC880_W810:
		spec->multiout.dig_out_nid = ALC880_DIGOUT_NID;
		break;
	default:
		break;
	}

	switch (board_config) {
	case ALC880_3ST:
	case ALC880_3ST_DIG:
	case ALC880_5ST:
	case ALC880_5ST_DIG:
	case ALC880_W810:
		spec->front_panel = 1;
		break;
	default:
		break;
	}

	switch (board_config) {
	case ALC880_5ST:
	case ALC880_5ST_DIG:
		spec->init_verbs = alc880_init_verbs_five_stack;
		spec->channel_mode = alc880_fivestack_modes;
		spec->num_channel_mode = ARRAY_SIZE(alc880_fivestack_modes);
		break;
	case ALC880_W810:
		spec->init_verbs = alc880_w810_init_verbs;
		spec->channel_mode = alc880_w810_modes;
		spec->num_channel_mode = ARRAY_SIZE(alc880_w810_modes);
		break;
	default:
		spec->init_verbs = alc880_init_verbs_three_stack;
		spec->channel_mode = alc880_threestack_modes;
		spec->num_channel_mode = ARRAY_SIZE(alc880_threestack_modes);
		break;
	}

	spec->stream_name_analog = "ALC880 Analog";
	spec->stream_analog_playback = &alc880_pcm_analog_playback;
	spec->stream_analog_capture = &alc880_pcm_analog_capture;

	spec->stream_name_digital = "ALC880 Digital";
	spec->stream_digital_playback = &alc880_pcm_digital_playback;
	spec->stream_digital_capture = &alc880_pcm_digital_capture;

	spec->multiout.max_channels = spec->channel_mode[0].channels;

	switch (board_config) {
	case ALC880_W810:
		spec->multiout.num_dacs = ARRAY_SIZE(alc880_w810_dac_nids);
		spec->multiout.dac_nids = alc880_w810_dac_nids;
		// No dedicated headphone socket - it's shared with built-in speakers.
		break;
	default:
		spec->multiout.num_dacs = ARRAY_SIZE(alc880_dac_nids);
		spec->multiout.dac_nids = alc880_dac_nids;
		spec->multiout.hp_nid = 0x03; /* rear-surround NID */
		break;
	}

	spec->input_mux = &alc880_capture_source;
	spec->num_adc_nids = ARRAY_SIZE(alc880_adc_nids);
	spec->adc_nids = alc880_adc_nids;

	codec->patch_ops = alc_patch_ops;

	return 0;
}

/*
 * ALC260 support
 */

/*
 * This is just place-holder, so there's something for alc_build_pcms to look
 * at when it calculates the maximum number of channels. ALC260 has no mixer
 * element which allows changing the channel mode, so the verb list is
 * never used.
 */
static struct alc_channel_mode alc260_modes[1] = {
	{ 2, NULL },
};

snd_kcontrol_new_t alc260_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	/* use LINE2 for the output */
	/* HDA_CODEC_MUTE("Front Playback Switch", 0x0f, 0x0, HDA_OUTPUT), */
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x07, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x07, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x0a, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x11, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x04, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x04, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_verb alc260_init_verbs[] = {
	/* Line In pin widget for input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* CD pin widget for input */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Mic2 (front panel) pin widget for input and vref at 80% */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* LINE-2 is used for line-out in rear */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* select line-out */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* LINE-OUT pin */
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* enable HP */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* enable Mono */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* unmute amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},
	/* set connection select to line in (default select for this ADC) */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* unmute Line-Out mixer amp left and right (volume = 0) */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* unmute HP mixer amp left and right (volume = 0) */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* unmute Mono mixer amp left and right (volume = 0) */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* mute LINE-2 out */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* Amp Indexes: CD = 0x04, Line In 1 = 0x02, Mic 1 = 0x00 & Line In 2 = 0x03 */
	/* unmute CD */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
	/* unmute Line In */
	{0x07,  AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	/* unmute Mic */
	{0x07,  AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	/* Amp Indexes: DAC = 0x01 & mixer = 0x00 */
	/* Unmute Front out path */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Headphone out path */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Mono out path */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{ }
};

static struct hda_pcm_stream alc260_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x2,
};

static struct hda_pcm_stream alc260_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x4,
};

static int patch_alc260(struct hda_codec *codec)
{
	struct alc_spec *spec;

	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixers[spec->num_mixers] = alc260_base_mixer;
	spec->num_mixers++;

	spec->init_verbs = alc260_init_verbs;
	spec->channel_mode = alc260_modes;
	spec->num_channel_mode = ARRAY_SIZE(alc260_modes);

	spec->stream_name_analog = "ALC260 Analog";
	spec->stream_analog_playback = &alc260_pcm_analog_playback;
	spec->stream_analog_capture = &alc260_pcm_analog_capture;

	spec->multiout.max_channels = spec->channel_mode[0].channels;
	spec->multiout.num_dacs = ARRAY_SIZE(alc260_dac_nids);
	spec->multiout.dac_nids = alc260_dac_nids;

	spec->input_mux = &alc260_capture_source;
	spec->num_adc_nids = ARRAY_SIZE(alc260_adc_nids);
	spec->adc_nids = alc260_adc_nids;

	codec->patch_ops = alc_patch_ops;

	return 0;
}

/*
 * ALC882 support
 *
 * ALC882 is almost identical with ALC880 but has cleaner and more flexible
 * configuration.  Each pin widget can choose any input DACs and a mixer.
 * Each ADC is connected from a mixer of all inputs.  This makes possible
 * 6-channel independent captures.
 *
 * In addition, an independent DAC for the multi-playback (not used in this
 * driver yet).
 */

static struct alc_channel_mode alc882_ch_modes[1] = {
	{ 8, NULL }
};

static hda_nid_t alc882_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};

static hda_nid_t alc882_adc_nids[3] = {
	/* ADC0-2 */
	0x07, 0x08, 0x09,
};

/* input MUX */
/* FIXME: should be a matrix-type input source selection */

static struct hda_input_mux alc882_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

#define alc882_mux_enum_info alc_mux_enum_info
#define alc882_mux_enum_get alc_mux_enum_get

static int alc882_mux_enum_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	const struct hda_input_mux *imux = spec->input_mux;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	static hda_nid_t capture_mixers[3] = { 0x24, 0x23, 0x22 };
	hda_nid_t nid = capture_mixers[adc_idx];
	unsigned int *cur_val = &spec->cur_mux[adc_idx];
	unsigned int i, idx;

	idx = ucontrol->value.enumerated.item[0];
	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (*cur_val == idx && ! codec->in_resume)
		return 0;
	for (i = 0; i < imux->num_items; i++) {
		unsigned int v = (i == idx) ? 0x7000 : 0x7080;
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    v | (imux->items[i].index << 8));
	}
	*cur_val = idx;
	return 1;
}

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */
static snd_kcontrol_new_t alc882_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x16, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x16, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Side Playback Switch", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 2, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 2, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 3,
		.info = alc882_mux_enum_info,
		.get = alc882_mux_enum_get,
		.put = alc882_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_verb alc882_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	/* CLFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	/* Side mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},

	/* Front Pin: to output mode */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Front Pin: mute amp left and right (no volume) */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* select Front mixer (0x0c, index 0) */
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Rear Pin */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Rear Pin: mute amp left and right (no volume) */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* select Rear mixer (0x0d, index 1) */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* CLFE Pin: mute amp left and right (no volume) */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* select CLFE mixer (0x0e, index 2) */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Side Pin: mute amp left and right (no volume) */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* select Side mixer (0x0f, index 3) */
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Headphone Pin */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Headphone Pin: mute amp left and right (no volume) */
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* select Front mixer (0x0c, index 0) */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Mic (rear) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Front Mic pin widget for input and vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* ADC1: unmute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},
	/* ADC2: unmute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},
	/* ADC3: unmute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},

	/* Unmute front loopback */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute rear loopback */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Mute CLFE loopback */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8))},
	/* Unmute side loopback */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},

	{ }
};

static int patch_alc882(struct hda_codec *codec)
{
	struct alc_spec *spec;

	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixers[spec->num_mixers] = alc882_base_mixer;
	spec->num_mixers++;

	spec->multiout.dig_out_nid = ALC880_DIGOUT_NID;
	spec->dig_in_nid = ALC880_DIGIN_NID;
	spec->front_panel = 1;
	spec->init_verbs = alc882_init_verbs;
	spec->channel_mode = alc882_ch_modes;
	spec->num_channel_mode = ARRAY_SIZE(alc882_ch_modes);

	spec->stream_name_analog = "ALC882 Analog";
	spec->stream_analog_playback = &alc880_pcm_analog_playback;
	spec->stream_analog_capture = &alc880_pcm_analog_capture;

	spec->stream_name_digital = "ALC882 Digital";
	spec->stream_digital_playback = &alc880_pcm_digital_playback;
	spec->stream_digital_capture = &alc880_pcm_digital_capture;

	spec->multiout.max_channels = spec->channel_mode[0].channels;
	spec->multiout.num_dacs = ARRAY_SIZE(alc882_dac_nids);
	spec->multiout.dac_nids = alc882_dac_nids;

	spec->input_mux = &alc882_capture_source;
	spec->num_adc_nids = ARRAY_SIZE(alc882_adc_nids);
	spec->adc_nids = alc882_adc_nids;

	codec->patch_ops = alc_patch_ops;

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_realtek[] = {
	{ .id = 0x10ec0260, .name = "ALC260", .patch = patch_alc260 },
 	{ .id = 0x10ec0880, .name = "ALC880", .patch = patch_alc880 },
	{ .id = 0x10ec0882, .name = "ALC882", .patch = patch_alc882 },
	{} /* terminator */
};
