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


/* board config type */
enum {
	CMI_MINIMAL,	/* back 3-jack */
	CMI_MIN_FP,	/* back 3-jack + front-panel 2-jack */
	CMI_FULL,	/* back 6-jack + front-panel 2-jack */
	CMI_FULL_DIG,	/* back 6-jack + front-panel 2-jack + digital I/O */
	CMI_ALLOUT,	/* back 5-jack + front-panel 2-jack + digital out */
};

struct cmi_spec {
	int board_config;
	unsigned int surr_switch: 1;	/* switchable line,mic */
	unsigned int no_line_in: 1;	/* no line-in (5-jack) */
	unsigned int front_panel: 1;	/* has front-panel 2-jack */

	/* playback */
	struct hda_multi_out multiout;

	/* capture */
	hda_nid_t *adc_nids;
	hda_nid_t dig_in_nid;

	/* capture source */
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[2];

	/* channel mode */
	unsigned int num_ch_modes;
	unsigned int cur_ch_mode;
	const struct cmi_channel_mode *channel_modes;

	struct hda_pcm pcm_rec[2];	/* PCM information */
};

/*
 * input MUX
 */
static int cmi_mux_enum_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int cmi_mux_enum_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int cmi_mux_enum_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
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
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* set mic PIN for input, also enable vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* route front PCM (DAC1) to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{}
};

/* 3-stack / 6 channel */
static struct hda_verb cmi9880_ch6_init[] = {
	/* set line-in PIN for output */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* set mic PIN for output */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* route front PCM (DAC1) to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{}
};

/* 3-stack+front / 8 channel */
static struct hda_verb cmi9880_ch8_init[] = {
	/* set line-in PIN for output */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* set mic PIN for output */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* route rear-surround PCM (DAC4) to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{}
};

struct cmi_channel_mode {
	unsigned int channels;
	const struct hda_verb *sequence;
};

static struct cmi_channel_mode cmi9880_channel_modes[3] = {
	{ 2, cmi9880_ch2_init },
	{ 6, cmi9880_ch6_init },
	{ 8, cmi9880_ch8_init },
};

static int cmi_ch_mode_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;

	snd_assert(spec->channel_modes, return -EINVAL);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->num_ch_modes;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	sprintf(uinfo->value.enumerated.name, "%dch",
		spec->channel_modes[uinfo->value.enumerated.item].channels);
	return 0;
}

static int cmi_ch_mode_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_ch_mode;
	return 0;
}

static int cmi_ch_mode_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cmi_spec *spec = codec->spec;

	snd_assert(spec->channel_modes, return -EINVAL);
	if (ucontrol->value.enumerated.item[0] >= spec->num_ch_modes)
		ucontrol->value.enumerated.item[0] = spec->num_ch_modes;
	if (ucontrol->value.enumerated.item[0] == spec->cur_ch_mode &&
	    ! codec->in_resume)
		return 0;

	spec->cur_ch_mode = ucontrol->value.enumerated.item[0];
	snd_hda_sequence_write(codec, spec->channel_modes[spec->cur_ch_mode].sequence);
	spec->multiout.max_channels = spec->channel_modes[spec->cur_ch_mode].channels;
	return 1;
}

/*
 */
static snd_kcontrol_new_t cmi9880_basic_mixer[] = {
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
static snd_kcontrol_new_t cmi9880_ch_mode_mixer[] = {
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
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1/2 */
	{ 0x08, AC_VERB_SET_CONNECT_SEL, 0x05 },
	{ 0x09, AC_VERB_SET_CONNECT_SEL, 0x05 },
	{} /* terminator */
};

static struct hda_verb cmi9880_allout_init[] = {
	/* port-D for line out (rear panel) */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-A for side (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* port-C for surround (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
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
	if (spec->surr_switch) {
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

static int cmi9880_init(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;
	if (spec->board_config == CMI_ALLOUT)
		snd_hda_sequence_write(codec, cmi9880_allout_init);
	else
		snd_hda_sequence_write(codec, cmi9880_basic_init);
	return 0;
}

#ifdef CONFIG_PM
/*
 * resume
 */
static int cmi9880_resume(struct hda_codec *codec)
{
	struct cmi_spec *spec = codec->spec;

	cmi9880_init(codec);
	snd_hda_resume_ctls(codec, cmi9880_basic_mixer);
	if (spec->surr_switch)
		snd_hda_resume_ctls(codec, cmi9880_ch_mode_mixer);
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
static int cmi9880_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     snd_pcm_substream_t *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream);
}

static int cmi9880_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					snd_pcm_substream_t *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag,
						format, substream);
}

static int cmi9880_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       snd_pcm_substream_t *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int cmi9880_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 snd_pcm_substream_t *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int cmi9880_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  snd_pcm_substream_t *substream)
{
	struct cmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int cmi9880_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      snd_pcm_substream_t *substream)
{
	struct cmi_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int cmi9880_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      snd_pcm_substream_t *substream)
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
		.close = cmi9880_dig_playback_pcm_close
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

static struct hda_board_config cmi9880_cfg_tbl[] = {
	{ .modelname = "minimal", .config = CMI_MINIMAL },
	{ .modelname = "min_fp", .config = CMI_MIN_FP },
	{ .modelname = "full", .config = CMI_FULL },
	{ .modelname = "full_dig", .config = CMI_FULL_DIG },
	{ .modelname = "allout", .config = CMI_ALLOUT },
	{} /* terminator */
};

static struct hda_codec_ops cmi9880_patch_ops = {
	.build_controls = cmi9880_build_controls,
	.build_pcms = cmi9880_build_pcms,
	.init = cmi9880_init,
	.free = cmi9880_free,
#ifdef CONFIG_PM
	.resume = cmi9880_resume,
#endif
};

static int patch_cmi9880(struct hda_codec *codec)
{
	struct cmi_spec *spec;

	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->board_config = snd_hda_check_board_config(codec, cmi9880_cfg_tbl);
	if (spec->board_config < 0) {
		snd_printd(KERN_INFO "hda_codec: Unknown model for CMI9880\n");
		spec->board_config = CMI_FULL_DIG; /* try everything */
	}

	switch (spec->board_config) {
	case CMI_MINIMAL:
	case CMI_MIN_FP:
		spec->surr_switch = 1;
		if (spec->board_config == CMI_MINIMAL)
			spec->num_ch_modes = 2;
		else {
			spec->front_panel = 1;
			spec->num_ch_modes = 3;
		}
		spec->channel_modes = cmi9880_channel_modes;
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
	}

	spec->multiout.num_dacs = 4;
	spec->multiout.dac_nids = cmi9880_dac_nids;

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
