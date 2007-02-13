/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for VIA VT1708 codec
 *
 * Copyright (c) 2006 Lydia Wang <lydiawang@viatech.com>
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

/* * * * * * * * * * * * * * Release History * * * * * * * * * * * * * * * * */
/*                                                                           */
/* 2006-03-03  Lydia Wang  Create the basic patch to support VT1708 codec    */
/* 2006-03-14  Lydia Wang  Modify hard code for some pin widget nid          */
/* 2006-08-02  Lydia Wang  Add support to VT1709 codec                       */
/* 2006-09-08  Lydia Wang  Fix internal loopback recording source select bug */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include <sound/driver.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"


/* amp values */
#define AMP_VAL_IDX_SHIFT	19
#define AMP_VAL_IDX_MASK	(0x0f<<19)

#define NUM_CONTROL_ALLOC	32
#define NUM_VERB_ALLOC		32

/* Pin Widget NID */
#define VT1708_HP_NID		0x13
#define VT1708_DIGOUT_NID	0x14
#define VT1708_DIGIN_NID	0x16

#define VT1709_HP_DAC_NID	0x28
#define VT1709_DIGOUT_NID	0x13
#define VT1709_DIGIN_NID	0x17

#define IS_VT1708_VENDORID(x)		((x) >= 0x11061708 && (x) <= 0x1106170b)
#define IS_VT1709_10CH_VENDORID(x)	((x) >= 0x1106e710 && (x) <= 0x1106e713)
#define IS_VT1709_6CH_VENDORID(x)	((x) >= 0x1106e714 && (x) <= 0x1106e717)


enum {
	VIA_CTL_WIDGET_VOL,
	VIA_CTL_WIDGET_MUTE,
};

enum {
	AUTO_SEQ_FRONT,
	AUTO_SEQ_SURROUND,
	AUTO_SEQ_CENLFE,
	AUTO_SEQ_SIDE
};

static struct snd_kcontrol_new vt1708_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
};


struct via_spec {
	/* codec parameterization */
	struct snd_kcontrol_new *mixers[3];
	unsigned int num_mixers;

	struct hda_verb *init_verbs;

	char *stream_name_analog;
	struct hda_pcm_stream *stream_analog_playback;
	struct hda_pcm_stream *stream_analog_capture;

	char *stream_name_digital;
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

	/* PCM information */
	struct hda_pcm pcm_rec[2];

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	struct snd_kcontrol_new *kctl_alloc;
	struct hda_input_mux private_imux;
	hda_nid_t private_dac_nids[4];	
};

static hda_nid_t vt1708_adc_nids[2] = {
	/* ADC1-2 */
	0x15, 0x27
};

static hda_nid_t vt1709_adc_nids[3] = {
	/* ADC1-2 */
	0x14, 0x15, 0x16
};

/* add dynamic controls */
static int via_add_control(struct via_spec *spec, int type, const char *name,
			   unsigned long val)
{
	struct snd_kcontrol_new *knew;

	if (spec->num_kctl_used >= spec->num_kctl_alloc) {
		int num = spec->num_kctl_alloc + NUM_CONTROL_ALLOC;

		/* array + terminator */
		knew = kcalloc(num + 1, sizeof(*knew), GFP_KERNEL);
		if (!knew)
			return -ENOMEM;
		if (spec->kctl_alloc) {
			memcpy(knew, spec->kctl_alloc,
			       sizeof(*knew) * spec->num_kctl_alloc);
			kfree(spec->kctl_alloc);
		}
		spec->kctl_alloc = knew;
		spec->num_kctl_alloc = num;
	}

	knew = &spec->kctl_alloc[spec->num_kctl_used];
	*knew = vt1708_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);

	if (!knew->name)
		return -ENOMEM;
	knew->private_value = val;
	spec->num_kctl_used++;
	return 0;
}

/* create input playback/capture controls for the given pin */
static int via_new_analog_input(struct via_spec *spec, hda_nid_t pin,
				const char *ctlname, int idx, int mix_nid)
{
	char name[32];
	int err;

	sprintf(name, "%s Playback Volume", ctlname);
	err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
			      HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	sprintf(name, "%s Playback Switch", ctlname);
	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
			      HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	return 0;
}

static void via_auto_set_output_and_unmute(struct hda_codec *codec,
					   hda_nid_t nid, int pin_type,
					   int dac_idx)
{
	/* set as output */
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pin_type);
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_OUT_UNMUTE);
}


static void via_auto_init_multi_out(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i <= AUTO_SEQ_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		if (nid)
			via_auto_set_output_and_unmute(codec, nid, PIN_OUT, i);
	}
}

static void via_auto_init_hp_out(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		via_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
}

static void via_auto_init_analog_input(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];

		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    (i <= AUTO_PIN_FRONT_MIC ?
				     PIN_VREF50 : PIN_IN));

	}
}
/*
 * input MUX handling
 */
static int via_mux_enum_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int via_mux_enum_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int via_mux_enum_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int vendor_id = codec->vendor_id;

	/* AIW0  lydia 060801 add for correct sw0 input select */
	if (IS_VT1708_VENDORID(vendor_id) && (adc_idx == 0))
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     0x18, &spec->cur_mux[adc_idx]);
	else if ((IS_VT1709_10CH_VENDORID(vendor_id) ||
		  IS_VT1709_6CH_VENDORID(vendor_id)) && (adc_idx == 0) )
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     0x19, &spec->cur_mux[adc_idx]);
	else
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     spec->adc_nids[adc_idx],
					     &spec->cur_mux[adc_idx]);
}

/* capture mixer elements */
static struct snd_kcontrol_new vt1708_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x27, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x27, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = via_mux_enum_info,
		.get = via_mux_enum_get,
		.put = via_mux_enum_put,
	},
	{ } /* end */
};
/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb vt1708_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x27, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output mixers (0x19 - 0x1b)
	 */
	/* set vol=0 to output mixers */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	
	/* Setup default input to PW4 */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* Set mic as default input of sw0 */
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x2},
	/* PW9 Output enable */
	{0x25, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
};

static int via_playback_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream);
}

static int via_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    unsigned int stream_tag,
				    unsigned int format,
				    struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag, format, substream);
}

static int via_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int via_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int via_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int via_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   unsigned int stream_tag,
				   unsigned int format,
				   struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int via_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   0, 0, 0);
	return 0;
}

static struct hda_pcm_stream vt1708_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x15, /* NID to query formats and rates */
	.ops = {
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close
	},
};

static struct hda_pcm_stream vt1708_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

static int via_build_controls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;
	int i;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}

	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec,
						    spec->multiout.dig_out_nid);
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

static int via_build_pcms(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = spec->stream_name_analog;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *(spec->stream_analog_playback);
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dac_nids[0];
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = *(spec->stream_analog_capture);
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];

	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
		spec->multiout.max_channels;

	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		codec->num_pcms++;
		info++;
		info->name = spec->stream_name_digital;
		if (spec->multiout.dig_out_nid) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				*(spec->stream_digital_playback);
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
				spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				*(spec->stream_digital_capture);
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->dig_in_nid;
		}
	}

	return 0;
}

static void via_free(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	unsigned int i;

	if (!spec)
		return;

	if (spec->kctl_alloc) {
		for (i = 0; i < spec->num_kctl_used; i++)
			kfree(spec->kctl_alloc[i].name);
		kfree(spec->kctl_alloc);
	}

	kfree(codec->spec);
}

static int via_init(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	snd_hda_sequence_write(codec, spec->init_verbs);
 	return 0;
}

#ifdef CONFIG_PM
/*
 * resume
 */
static int via_resume(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;

	via_init(codec);
	for (i = 0; i < spec->num_mixers; i++)
		snd_hda_resume_ctls(codec, spec->mixers[i]);
	if (spec->multiout.dig_out_nid)
		snd_hda_resume_spdif_out(codec);
	if (spec->dig_in_nid)
		snd_hda_resume_spdif_in(codec);

	return 0;
}
#endif

/*
 */
static struct hda_codec_ops via_patch_ops = {
	.build_controls = via_build_controls,
	.build_pcms = via_build_pcms,
	.init = via_init,
	.free = via_free,
#ifdef CONFIG_PM
	.resume = via_resume,
#endif
};

/* fill in the dac_nids table from the parsed pin configuration */
static int vt1708_auto_fill_dac_nids(struct via_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	int i;
	hda_nid_t nid;

	spec->multiout.num_dacs = cfg->line_outs;

	spec->multiout.dac_nids = spec->private_dac_nids;
 	
	for(i = 0; i < 4; i++) {
		nid = cfg->line_out_pins[i];
		if (nid) {
			/* config dac list */
			switch (i) {
			case AUTO_SEQ_FRONT:
				spec->multiout.dac_nids[i] = 0x10;
				break;
			case AUTO_SEQ_CENLFE:
				spec->multiout.dac_nids[i] = 0x12;
				break;
			case AUTO_SEQ_SURROUND:
				spec->multiout.dac_nids[i] = 0x13;
				break;
			case AUTO_SEQ_SIDE:
				spec->multiout.dac_nids[i] = 0x11;
				break;
			}
		}
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1708_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", "C/LFE", "Side" };
	hda_nid_t nid, nid_vol = 0;
	int i, err;

	for (i = 0; i <= AUTO_SEQ_SIDE; i++) {
		nid = cfg->line_out_pins[i];

		if (!nid)
			continue;
		
		if (i != AUTO_SEQ_FRONT)
			nid_vol = 0x1b - i + 1;

		if (i == AUTO_SEQ_CENLFE) {
			/* Center/LFE */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Center Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "LFE Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Center Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "LFE Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT){
			/* add control to mixer index 0 */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Master Front Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x17, 3, 0, HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Master Front Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x17, 3, 0, HDA_INPUT));
			if (err < 0)
				return err;
			
			/* add control to PW3 */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int vt1708_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = VT1708_HP_NID; /* AOW3 */

	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Headphone Playback Switch",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	return 0;
}

/* create playback/capture controls for input pins */
static int vt1708_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx = 0;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = idx;
	imux->num_items++;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x1d: /* Mic */
			idx = 2;
			break;
				
		case 0x1e: /* Line In */
			idx = 3;
			break;

		case 0x21: /* Front Mic */
			idx = 4;
			break;

		case 0x24: /* CD */
			idx = 1;
			break;
		}
		err = via_new_analog_input(spec, cfg->input_pins[i], labels[i],
					   idx, 0x17);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx;
		imux->num_items++;
	}
	return 0;
}

static int vt1708_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	err = vt1708_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1708_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1708_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1708_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = VT1708_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = VT1708_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs = vt1708_volume_init_verbs;	

	spec->input_mux = &spec->private_imux;

	return 1;
}

/* init callback for auto-configuration model -- overriding the default init */
static int via_auto_init(struct hda_codec *codec)
{
	via_init(codec);
	via_auto_init_multi_out(codec);
	via_auto_init_hp_out(codec);
	via_auto_init_analog_input(codec);
	return 0;
}

static int patch_vt1708(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1708_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	
	spec->stream_name_analog = "VT1708 Analog";
	spec->stream_analog_playback = &vt1708_pcm_analog_playback;
	spec->stream_analog_capture = &vt1708_pcm_analog_capture;

	spec->stream_name_digital = "VT1708 Digital";
	spec->stream_digital_playback = &vt1708_pcm_digital_playback;
	spec->stream_digital_capture = &vt1708_pcm_digital_capture;

	
	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1708_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1708_adc_nids);
		spec->mixers[spec->num_mixers] = vt1708_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;

	return 0;
}

/* capture mixer elements */
static struct snd_kcontrol_new vt1709_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 2, 0x16, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 2, 0x16, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 * FIXME: the controls appear in the "playback" view!
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = via_mux_enum_info,
		.get = via_mux_enum_get,
		.put = via_mux_enum_put,
	},
	{ } /* end */
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb vt1709_10ch_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: AOW0=0, CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output selector (0x1a, 0x1b, 0x29)
	 */
	/* set vol=0 to output mixers */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/*
	 *  Unmute PW3 and PW4
	 */
	{0x1f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Set input of PW4 as AOW4 */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* Set mic as default input of sw0 */
	{0x19, AC_VERB_SET_CONNECT_SEL, 0x2},
	/* PW9 Output enable */
	{0x24, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static struct hda_pcm_stream vt1709_10ch_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 10,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1709_6ch_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 6,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1709_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x14, /* NID to query formats and rates */
	.ops = {
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1709_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close
	},
};

static struct hda_pcm_stream vt1709_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

static int vt1709_auto_fill_dac_nids(struct via_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	int i;
	hda_nid_t nid;

	if (cfg->line_outs == 4)  /* 10 channels */
		spec->multiout.num_dacs = cfg->line_outs+1; /* AOW0~AOW4 */
	else if (cfg->line_outs == 3) /* 6 channels */
		spec->multiout.num_dacs = cfg->line_outs; /* AOW0~AOW2 */

	spec->multiout.dac_nids = spec->private_dac_nids;

	if (cfg->line_outs == 4) { /* 10 channels */
		for (i = 0; i < cfg->line_outs; i++) {
			nid = cfg->line_out_pins[i];
			if (nid) {
				/* config dac list */
				switch (i) {
				case AUTO_SEQ_FRONT:
					/* AOW0 */
					spec->multiout.dac_nids[i] = 0x10;
					break;
				case AUTO_SEQ_CENLFE:
					/* AOW2 */
					spec->multiout.dac_nids[i] = 0x12;
					break;
				case AUTO_SEQ_SURROUND:
					/* AOW3 */
					spec->multiout.dac_nids[i] = 0x27;
					break;
				case AUTO_SEQ_SIDE:
					/* AOW1 */
					spec->multiout.dac_nids[i] = 0x11;
					break;
				default:
					break;
				}
			}
		}
		spec->multiout.dac_nids[cfg->line_outs] = 0x28; /* AOW4 */

	} else if (cfg->line_outs == 3) { /* 6 channels */
		for(i = 0; i < cfg->line_outs; i++) {
			nid = cfg->line_out_pins[i];
			if (nid) {
				/* config dac list */
				switch(i) {
				case AUTO_SEQ_FRONT:
					/* AOW0 */
					spec->multiout.dac_nids[i] = 0x10;
					break;
				case AUTO_SEQ_CENLFE:
					/* AOW2 */
					spec->multiout.dac_nids[i] = 0x12;
					break;
				case AUTO_SEQ_SURROUND:
					/* AOW1 */
					spec->multiout.dac_nids[i] = 0x11;
					break;
				default:
					break;
				}
			}
		}
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1709_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", "C/LFE", "Side" };
	hda_nid_t nid = 0;
	int i, err;

	for (i = 0; i <= AUTO_SEQ_SIDE; i++) {
		nid = cfg->line_out_pins[i];

		if (!nid)	
			continue;

		if (i == AUTO_SEQ_CENLFE) {
			/* Center/LFE */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Center Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x1b, 1, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "LFE Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x1b, 2, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Center Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x1b, 1, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "LFE Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x1b, 2, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT){
			/* add control to mixer index 0 */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Master Front Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x18, 3, 0, HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Master Front Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x18, 3, 0, HDA_INPUT));
			if (err < 0)
				return err;
			
			/* add control to PW3 */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_SURROUND) {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(0x29, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(0x29, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_SIDE) {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(0x1a, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(0x1a, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int vt1709_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	if (spec->multiout.num_dacs == 5) /* 10 channels */
		spec->multiout.hp_nid = VT1709_HP_DAC_NID;
	else if (spec->multiout.num_dacs == 3) /* 6 channels */
		spec->multiout.hp_nid = 0;

	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Headphone Playback Switch",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	return 0;
}

/* create playback/capture controls for input pins */
static int vt1709_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx = 0;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = idx;
	imux->num_items++;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x1d: /* Mic */
			idx = 2;
			break;
				
		case 0x1e: /* Line In */
			idx = 3;
			break;

		case 0x21: /* Front Mic */
			idx = 4;
			break;

		case 0x23: /* CD */
			idx = 1;
			break;
		}
		err = via_new_analog_input(spec, cfg->input_pins[i], labels[i],
					   idx, 0x18);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx;
		imux->num_items++;
	}
	return 0;
}

static int vt1709_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	err = vt1709_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1709_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1709_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1709_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = VT1709_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = VT1709_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux;

	return 1;
}

static int patch_vt1709_10ch(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	err = vt1709_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration.  "
		       "Using genenic mode...\n");
	}

	spec->init_verbs = vt1709_10ch_volume_init_verbs;	

	spec->stream_name_analog = "VT1709 Analog";
	spec->stream_analog_playback = &vt1709_10ch_pcm_analog_playback;
	spec->stream_analog_capture = &vt1709_pcm_analog_capture;

	spec->stream_name_digital = "VT1709 Digital";
	spec->stream_digital_playback = &vt1709_pcm_digital_playback;
	spec->stream_digital_capture = &vt1709_pcm_digital_capture;

	
	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1709_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1709_adc_nids);
		spec->mixers[spec->num_mixers] = vt1709_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;

	return 0;
}
/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb vt1709_6ch_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: AOW0=0, CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output selector (0x1a, 0x1b, 0x29)
	 */
	/* set vol=0 to output mixers */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/*
	 *  Unmute PW3 and PW4
	 */
	{0x1f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Set input of PW4 as MW0 */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0},
	/* Set mic as default input of sw0 */
	{0x19, AC_VERB_SET_CONNECT_SEL, 0x2},
	/* PW9 Output enable */
	{0x24, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static int patch_vt1709_6ch(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	err = vt1709_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration.  "
		       "Using genenic mode...\n");
	}

	spec->init_verbs = vt1709_6ch_volume_init_verbs;	

	spec->stream_name_analog = "VT1709 Analog";
	spec->stream_analog_playback = &vt1709_6ch_pcm_analog_playback;
	spec->stream_analog_capture = &vt1709_pcm_analog_capture;

	spec->stream_name_digital = "VT1709 Digital";
	spec->stream_digital_playback = &vt1709_pcm_digital_playback;
	spec->stream_digital_capture = &vt1709_pcm_digital_capture;

	
	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1709_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1709_adc_nids);
		spec->mixers[spec->num_mixers] = vt1709_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_via[] = {
	{ .id = 0x11061708, .name = "VIA VT1708", .patch = patch_vt1708},
	{ .id = 0x11061709, .name = "VIA VT1708", .patch = patch_vt1708},
	{ .id = 0x1106170A, .name = "VIA VT1708", .patch = patch_vt1708},
	{ .id = 0x1106170B, .name = "VIA VT1708", .patch = patch_vt1708},
	{ .id = 0x1106E710, .name = "VIA VT1709 10-Ch", .patch = patch_vt1709_10ch},
	{ .id = 0x1106E711, .name = "VIA VT1709 10-Ch", .patch = patch_vt1709_10ch},
	{ .id = 0x1106E712, .name = "VIA VT1709 10-Ch", .patch = patch_vt1709_10ch},
	{ .id = 0x1106E713, .name = "VIA VT1709 10-Ch", .patch = patch_vt1709_10ch},
	{ .id = 0x1106E714, .name = "VIA VT1709 6-Ch", .patch = patch_vt1709_6ch},
	{ .id = 0x1106E715, .name = "VIA VT1709 6-Ch", .patch = patch_vt1709_6ch},
	{ .id = 0x1106E716, .name = "VIA VT1709 6-Ch", .patch = patch_vt1709_6ch},
	{ .id = 0x1106E717, .name = "VIA VT1709 6-Ch", .patch = patch_vt1709_6ch},
	{} /* terminator */
};
