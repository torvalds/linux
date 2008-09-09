/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for VIA VT1702/VT1708/VT1709 codec
 *
 * Copyright (c) 2006-2008 Lydia Wang <lydiawang@viatech.com>
 *			   Takashi Iwai <tiwai@suse.de>
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
/* 2007-09-12  Lydia Wang  Add EAPD enable during driver initialization      */
/* 2007-09-17  Lydia Wang  Add VT1708B codec support                        */
/* 2007-11-14  Lydia Wang  Add VT1708A codec HP and CD pin connect config    */
/* 2008-02-03  Lydia Wang  Fix Rear channels and Back channels inverse issue */
/* 2008-03-06  Lydia Wang  Add VT1702 codec and VT1708S codec support        */
/* 2008-04-09  Lydia Wang  Add mute front speaker when HP plugin             */
/* 2008-04-09  Lydia Wang  Add Independent HP feature                        */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_patch.h"

/* amp values */
#define AMP_VAL_IDX_SHIFT	19
#define AMP_VAL_IDX_MASK	(0x0f<<19)

#define NUM_CONTROL_ALLOC	32
#define NUM_VERB_ALLOC		32

/* Pin Widget NID */
#define VT1708_HP_NID		0x13
#define VT1708_DIGOUT_NID	0x14
#define VT1708_DIGIN_NID	0x16
#define VT1708_DIGIN_PIN	0x26
#define VT1708_HP_PIN_NID	0x20
#define VT1708_CD_PIN_NID	0x24

#define VT1709_HP_DAC_NID	0x28
#define VT1709_DIGOUT_NID	0x13
#define VT1709_DIGIN_NID	0x17
#define VT1709_DIGIN_PIN	0x25

#define VT1708B_HP_NID		0x25
#define VT1708B_DIGOUT_NID	0x12
#define VT1708B_DIGIN_NID	0x15
#define VT1708B_DIGIN_PIN	0x21

#define VT1708S_HP_NID		0x25
#define VT1708S_DIGOUT_NID	0x12

#define VT1702_HP_NID		0x17
#define VT1702_DIGOUT_NID	0x11

#define IS_VT1708_VENDORID(x)		((x) >= 0x11061708 && (x) <= 0x1106170b)
#define IS_VT1709_10CH_VENDORID(x)	((x) >= 0x1106e710 && (x) <= 0x1106e713)
#define IS_VT1709_6CH_VENDORID(x)	((x) >= 0x1106e714 && (x) <= 0x1106e717)
#define IS_VT1708B_8CH_VENDORID(x)	((x) >= 0x1106e720 && (x) <= 0x1106e723)
#define IS_VT1708B_4CH_VENDORID(x)	((x) >= 0x1106e724 && (x) <= 0x1106e727)
#define IS_VT1708S_VENDORID(x)		((x) >= 0x11060397 && (x) <= 0x11067397)
#define IS_VT1702_VENDORID(x)		((x) >= 0x11060398 && (x) <= 0x11067398)

#define VIA_HP_EVENT		0x01
#define VIA_GPIO_EVENT		0x02

enum {
	VIA_CTL_WIDGET_VOL,
	VIA_CTL_WIDGET_MUTE,
};

enum {
	AUTO_SEQ_FRONT = 0,
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

	struct hda_verb *init_verbs[5];
	unsigned int num_iverbs;

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
	struct hda_input_mux private_imux[2];
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];

	/* HP mode source */
	const struct hda_input_mux *hp_mux;
	unsigned int hp_independent_mode;

#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_loopback_check loopback;
#endif
};

static hda_nid_t vt1708_adc_nids[2] = {
	/* ADC1-2 */
	0x15, 0x27
};

static hda_nid_t vt1709_adc_nids[3] = {
	/* ADC1-2 */
	0x14, 0x15, 0x16
};

static hda_nid_t vt1708B_adc_nids[2] = {
	/* ADC1-2 */
	0x13, 0x14
};

static hda_nid_t vt1708S_adc_nids[2] = {
	/* ADC1-2 */
	0x13, 0x14
};

static hda_nid_t vt1702_adc_nids[3] = {
	/* ADC1-2 */
	0x12, 0x20, 0x1F
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
		  IS_VT1709_6CH_VENDORID(vendor_id)) && (adc_idx == 0))
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     0x19, &spec->cur_mux[adc_idx]);
	else if ((IS_VT1708B_8CH_VENDORID(vendor_id) ||
		  IS_VT1708B_4CH_VENDORID(vendor_id)) && (adc_idx == 0))
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     0x17, &spec->cur_mux[adc_idx]);
	else if (IS_VT1702_VENDORID(vendor_id) && (adc_idx == 0))
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     0x13, &spec->cur_mux[adc_idx]);
	else
		return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
					     spec->adc_nids[adc_idx],
					     &spec->cur_mux[adc_idx]);
}

static int via_independent_hp_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->hp_mux, uinfo);
}

static int via_independent_hp_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	hda_nid_t nid = spec->autocfg.hp_pins[0];
	unsigned int pinsel = snd_hda_codec_read(codec, nid, 0,
						 AC_VERB_GET_CONNECT_SEL,
						 0x00);

	ucontrol->value.enumerated.item[0] = pinsel;

	return 0;
}

static int via_independent_hp_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	hda_nid_t nid = spec->autocfg.hp_pins[0];
	unsigned int pinsel = ucontrol->value.enumerated.item[0];
	unsigned int con_nid = snd_hda_codec_read(codec, nid, 0,
					 AC_VERB_GET_CONNECT_LIST, 0) & 0xff;

	if (con_nid == spec->multiout.hp_nid) {
		if (pinsel == 0) {
			if (!spec->hp_independent_mode) {
				if (spec->multiout.num_dacs > 1)
					spec->multiout.num_dacs -= 1;
				spec->hp_independent_mode = 1;
			}
		} else if (pinsel == 1) {
		       if (spec->hp_independent_mode) {
				if (spec->multiout.num_dacs > 1)
					spec->multiout.num_dacs += 1;
				spec->hp_independent_mode = 0;
		       }
		}
	} else {
		if (pinsel == 0) {
			if (spec->hp_independent_mode) {
				if (spec->multiout.num_dacs > 1)
					spec->multiout.num_dacs += 1;
				spec->hp_independent_mode = 0;
			}
		} else if (pinsel == 1) {
		       if (!spec->hp_independent_mode) {
				if (spec->multiout.num_dacs > 1)
					spec->multiout.num_dacs -= 1;
				spec->hp_independent_mode = 1;
		       }
		}
	}
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CONNECT_SEL,
			    pinsel);

	if (spec->multiout.hp_nid &&
	    spec->multiout.hp_nid != spec->multiout.dac_nids[HDA_FRONT])
			snd_hda_codec_setup_stream(codec,
						   spec->multiout.hp_nid,
						   0, 0, 0);

	return 0;
}

static struct snd_kcontrol_new via_hp_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Independent HP",
		.count = 1,
		.info = via_independent_hp_info,
		.get = via_independent_hp_get,
		.put = via_independent_hp_put,
	},
	{ } /* end */
};

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
	/* PW9 Output enable */
	{0x25, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static int via_playback_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
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


static void playback_multi_pcm_prep_0(struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	struct hda_multi_out *mout = &spec->multiout;
	hda_nid_t *nids = mout->dac_nids;
	int chs = substream->runtime->channels;
	int i;

	mutex_lock(&codec->spdif_mutex);
	if (mout->dig_out_nid && mout->dig_out_used != HDA_DIG_EXCLUSIVE) {
		if (chs == 2 &&
		    snd_hda_is_supported_format(codec, mout->dig_out_nid,
						format) &&
		    !(codec->spdif_status & IEC958_AES0_NONAUDIO)) {
			mout->dig_out_used = HDA_DIG_ANALOG_DUP;
			/* turn off SPDIF once; otherwise the IEC958 bits won't
			 * be updated */
			if (codec->spdif_ctls & AC_DIG1_ENABLE)
				snd_hda_codec_write(codec, mout->dig_out_nid, 0,
						    AC_VERB_SET_DIGI_CONVERT_1,
						    codec->spdif_ctls &
							~AC_DIG1_ENABLE & 0xff);
			snd_hda_codec_setup_stream(codec, mout->dig_out_nid,
						   stream_tag, 0, format);
			/* turn on again (if needed) */
			if (codec->spdif_ctls & AC_DIG1_ENABLE)
				snd_hda_codec_write(codec, mout->dig_out_nid, 0,
						    AC_VERB_SET_DIGI_CONVERT_1,
						    codec->spdif_ctls & 0xff);
		} else {
			mout->dig_out_used = 0;
			snd_hda_codec_setup_stream(codec, mout->dig_out_nid,
						   0, 0, 0);
		}
	}
	mutex_unlock(&codec->spdif_mutex);

	/* front */
	snd_hda_codec_setup_stream(codec, nids[HDA_FRONT], stream_tag,
				   0, format);

	if (mout->hp_nid && mout->hp_nid != nids[HDA_FRONT] &&
	    !spec->hp_independent_mode)
		/* headphone out will just decode front left/right (stereo) */
		snd_hda_codec_setup_stream(codec, mout->hp_nid, stream_tag,
					   0, format);

	/* extra outputs copied from front */
	for (i = 0; i < ARRAY_SIZE(mout->extra_out_nid); i++)
		if (mout->extra_out_nid[i])
			snd_hda_codec_setup_stream(codec,
						   mout->extra_out_nid[i],
						   stream_tag, 0, format);

	/* surrounds */
	for (i = 1; i < mout->num_dacs; i++) {
		if (chs >= (i + 1) * 2) /* independent out */
			snd_hda_codec_setup_stream(codec, nids[i], stream_tag,
						   i * 2, format);
		else /* copy front */
			snd_hda_codec_setup_stream(codec, nids[i], stream_tag,
						   0, format);
	}
}

static int via_playback_multi_pcm_prepare(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  unsigned int stream_tag,
					  unsigned int format,
					  struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	struct hda_multi_out *mout = &spec->multiout;
	hda_nid_t *nids = mout->dac_nids;

	if (substream->number == 0)
		playback_multi_pcm_prep_0(codec, stream_tag, format,
					  substream);
	else {
		if (mout->hp_nid && mout->hp_nid != nids[HDA_FRONT] &&
		    spec->hp_independent_mode)
			snd_hda_codec_setup_stream(codec, mout->hp_nid,
						   stream_tag, 0, format);
	}

	return 0;
}

static int via_playback_multi_pcm_cleanup(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	struct hda_multi_out *mout = &spec->multiout;
	hda_nid_t *nids = mout->dac_nids;
	int i;

	if (substream->number == 0) {
		for (i = 0; i < mout->num_dacs; i++)
			snd_hda_codec_setup_stream(codec, nids[i], 0, 0, 0);

		if (mout->hp_nid && !spec->hp_independent_mode)
			snd_hda_codec_setup_stream(codec, mout->hp_nid,
						   0, 0, 0);

		for (i = 0; i < ARRAY_SIZE(mout->extra_out_nid); i++)
			if (mout->extra_out_nid[i])
				snd_hda_codec_setup_stream(codec,
							mout->extra_out_nid[i],
							0, 0, 0);
		mutex_lock(&codec->spdif_mutex);
		if (mout->dig_out_nid &&
		    mout->dig_out_used == HDA_DIG_ANALOG_DUP) {
			snd_hda_codec_setup_stream(codec, mout->dig_out_nid,
						   0, 0, 0);
			mout->dig_out_used = 0;
		}
		mutex_unlock(&codec->spdif_mutex);
	} else {
		if (mout->hp_nid && mout->hp_nid != nids[HDA_FRONT] &&
		    spec->hp_independent_mode)
			snd_hda_codec_setup_stream(codec, mout->hp_nid,
						   0, 0, 0);
	}

	return 0;
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

static int via_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
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
	snd_hda_codec_cleanup_stream(codec, spec->adc_nids[substream->number]);
	return 0;
}

static struct hda_pcm_stream vt1708_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708_pcm_analog_s16_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	/* We got noisy outputs on the right channel on VT1708 when
	 * 24bit samples are used.  Until any workaround is found,
	 * disable the 24bit format, so far.
	 */
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
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
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare
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
		err = snd_hda_create_spdif_share_sw(codec,
						    &spec->multiout);
		if (err < 0)
			return err;
		spec->multiout.share_spdif = 1;
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
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
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

/* mute internal speaker if HP is plugged */
static void via_hp_automute(struct hda_codec *codec)
{
	unsigned int present;
	struct via_spec *spec = codec->spec;

	present = snd_hda_codec_read(codec, spec->autocfg.hp_pins[0], 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, spec->autocfg.line_out_pins[0],
				 HDA_OUTPUT, 0, HDA_AMP_MUTE,
				 present ? HDA_AMP_MUTE : 0);
}

static void via_gpio_control(struct hda_codec *codec)
{
	unsigned int gpio_data;
	unsigned int vol_counter;
	unsigned int vol;
	unsigned int master_vol;

	struct via_spec *spec = codec->spec;

	gpio_data = snd_hda_codec_read(codec, codec->afg, 0,
				       AC_VERB_GET_GPIO_DATA, 0) & 0x03;

	vol_counter = (snd_hda_codec_read(codec, codec->afg, 0,
					  0xF84, 0) & 0x3F0000) >> 16;

	vol = vol_counter & 0x1F;
	master_vol = snd_hda_codec_read(codec, 0x1A, 0,
					AC_VERB_GET_AMP_GAIN_MUTE,
					AC_AMP_GET_INPUT);

	if (gpio_data == 0x02) {
		/* unmute line out */
		snd_hda_codec_amp_stereo(codec, spec->autocfg.line_out_pins[0],
					 HDA_OUTPUT, 0, HDA_AMP_MUTE, 0);

		if (vol_counter & 0x20) {
			/* decrease volume */
			if (vol > master_vol)
				vol = master_vol;
			snd_hda_codec_amp_stereo(codec, 0x1A, HDA_INPUT,
						 0, HDA_AMP_VOLMASK,
						 master_vol-vol);
		} else {
			/* increase volume */
			snd_hda_codec_amp_stereo(codec, 0x1A, HDA_INPUT, 0,
					 HDA_AMP_VOLMASK,
					 ((master_vol+vol) > 0x2A) ? 0x2A :
					  (master_vol+vol));
		}
	} else if (!(gpio_data & 0x02)) {
		/* mute line out */
		snd_hda_codec_amp_stereo(codec,
					 spec->autocfg.line_out_pins[0],
					 HDA_OUTPUT, 0, HDA_AMP_MUTE,
					 HDA_AMP_MUTE);
	}
}

/* unsolicited event for jack sensing */
static void via_unsol_event(struct hda_codec *codec,
				  unsigned int res)
{
	res >>= 26;
	if (res == VIA_HP_EVENT)
		via_hp_automute(codec);
	else if (res == VIA_GPIO_EVENT)
		via_gpio_control(codec);
}

static int via_init(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;
	for (i = 0; i < spec->num_iverbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);

	/* Lydia Add for EAPD enable */
	if (!spec->dig_in_nid) { /* No Digital In connection */
		if (IS_VT1708_VENDORID(codec->vendor_id)) {
			snd_hda_codec_write(codec, VT1708_DIGIN_PIN, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    PIN_OUT);
			snd_hda_codec_write(codec, VT1708_DIGIN_PIN, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 0x02);
		} else if (IS_VT1709_10CH_VENDORID(codec->vendor_id) ||
			   IS_VT1709_6CH_VENDORID(codec->vendor_id)) {
			snd_hda_codec_write(codec, VT1709_DIGIN_PIN, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    PIN_OUT);
			snd_hda_codec_write(codec, VT1709_DIGIN_PIN, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 0x02);
		} else if (IS_VT1708B_8CH_VENDORID(codec->vendor_id) ||
			   IS_VT1708B_4CH_VENDORID(codec->vendor_id)) {
			snd_hda_codec_write(codec, VT1708B_DIGIN_PIN, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    PIN_OUT);
			snd_hda_codec_write(codec, VT1708B_DIGIN_PIN, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 0x02);
		}
	} else /* enable SPDIF-input pin */
		snd_hda_codec_write(codec, spec->autocfg.dig_in_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN);

 	return 0;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static int via_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
}
#endif

/*
 */
static struct hda_codec_ops via_patch_ops = {
	.build_controls = via_build_controls,
	.build_pcms = via_build_pcms,
	.init = via_init,
	.free = via_free,
#ifdef CONFIG_SND_HDA_POWER_SAVE
	.check_power_status = via_check_power_status,
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
				spec->multiout.dac_nids[i] = 0x11;
				break;
			case AUTO_SEQ_SIDE:
				spec->multiout.dac_nids[i] = 0x13;
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
			nid_vol = 0x18 + i;

		if (i == AUTO_SEQ_CENLFE) {
			/* Center/LFE */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					"Center Playback Volume",
					HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0,
							    HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "LFE Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Center Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "LFE Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT){
			/* add control to mixer index 0 */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Master Front Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x17, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Master Front Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x17, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			
			/* add control to PW3 */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static void create_hp_imux(struct via_spec *spec)
{
	int i;
	struct hda_input_mux *imux = &spec->private_imux[1];
	static const char *texts[] = { "OFF", "ON", NULL};

	/* for hp mode select */
	i = 0;
	while (texts[i] != NULL) {
		imux->items[imux->num_items].label =  texts[i];
		imux->items[imux->num_items].index = i;
		imux->num_items++;
		i++;
	}

	spec->hp_mux = &spec->private_imux[1];
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

	create_hp_imux(spec);

	return 0;
}

/* create playback/capture controls for input pins */
static int vt1708_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux[0];
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

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1708_loopbacks[] = {
	{ 0x17, HDA_INPUT, 1 },
	{ 0x17, HDA_INPUT, 2 },
	{ 0x17, HDA_INPUT, 3 },
	{ 0x17, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static void vt1708_set_pinconfig_connect(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int def_conf;
	unsigned char seqassoc;

	def_conf = snd_hda_codec_read(codec, nid, 0,
				      AC_VERB_GET_CONFIG_DEFAULT, 0);
	seqassoc = (unsigned char) get_defcfg_association(def_conf);
	seqassoc = (seqassoc << 4) | get_defcfg_sequence(def_conf);
	if (get_defcfg_connect(def_conf) == AC_JACK_PORT_NONE) {
		if (seqassoc == 0xff) {
			def_conf = def_conf & (~(AC_JACK_PORT_BOTH << 30));
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_CONFIG_DEFAULT_BYTES_3,
					    def_conf >> 24);
		}
	}

	return;
}

static int vt1708_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	/* Add HP and CD pin config connect bit re-config action */
	vt1708_set_pinconfig_connect(codec, VT1708_HP_PIN_NID);
	vt1708_set_pinconfig_connect(codec, VT1708_CD_PIN_NID);

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

	spec->init_verbs[spec->num_iverbs++] = vt1708_volume_init_verbs;

	spec->input_mux = &spec->private_imux[0];

	spec->mixers[spec->num_mixers++] = via_hp_mixer;

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
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
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
	/* disable 32bit format on VT1708 */
	if (codec->vendor_id == 0x11061708)
		spec->stream_analog_playback = &vt1708_pcm_analog_s16_playback;
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
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1708_loopbacks;
#endif

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

static struct hda_verb vt1709_uniwill_init_verbs[] = {
	{0x20, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_HP_EVENT},
	{ }
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
					spec->multiout.dac_nids[i] = 0x11;
					break;
				case AUTO_SEQ_SIDE:
					/* AOW1 */
					spec->multiout.dac_nids[i] = 0x27;
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
					      HDA_COMPOSE_AMP_VAL(0x1b, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "LFE Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x1b, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Center Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x1b, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "LFE Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x1b, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT){
			/* add control to mixer index 0 */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Master Front Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x18, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Master Front Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x18, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			
			/* add control to PW3 */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_SURROUND) {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(0x1a, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(0x1a, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_SIDE) {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(0x29, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(0x29, 3, 0,
								  HDA_OUTPUT));
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
	struct hda_input_mux *imux = &spec->private_imux[0];
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

	spec->input_mux = &spec->private_imux[0];

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1709_loopbacks[] = {
	{ 0x18, HDA_INPUT, 1 },
	{ 0x18, HDA_INPUT, 2 },
	{ 0x18, HDA_INPUT, 3 },
	{ 0x18, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static int patch_vt1709_10ch(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
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

	spec->init_verbs[spec->num_iverbs++] = vt1709_10ch_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt1709_uniwill_init_verbs;

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
	codec->patch_ops.unsol_event = via_unsol_event;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1709_loopbacks;
#endif

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
	/* PW9 Output enable */
	{0x24, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static int patch_vt1709_6ch(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
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

	spec->init_verbs[spec->num_iverbs++] = vt1709_6ch_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt1709_uniwill_init_verbs;

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
	codec->patch_ops.unsol_event = via_unsol_event;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1709_loopbacks;
#endif
	return 0;
}

/* capture mixer elements */
static struct snd_kcontrol_new vt1708B_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x13, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x13, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x14, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
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
static struct hda_verb vt1708B_8ch_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output mixers
	 */
	/* set vol=0 to output mixers */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x27, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Setup default input to PW4 */
	{0x1d, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* PW9 Output enable */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* PW10 Input enable */
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{ }
};

static struct hda_verb vt1708B_4ch_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output mixers
	 */
	/* set vol=0 to output mixers */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x27, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Setup default input of PW4 to MW0 */
	{0x1d, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* PW9 Output enable */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* PW10 Input enable */
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{ }
};

static struct hda_verb vt1708B_uniwill_init_verbs[] = {
	{0x1D, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_HP_EVENT},
	{ }
};

static struct hda_pcm_stream vt1708B_8ch_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708B_4ch_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 4,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708B_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x13, /* NID to query formats and rates */
	.ops = {
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708B_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare
	},
};

static struct hda_pcm_stream vt1708B_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

/* fill in the dac_nids table from the parsed pin configuration */
static int vt1708B_auto_fill_dac_nids(struct via_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	int i;
	hda_nid_t nid;

	spec->multiout.num_dacs = cfg->line_outs;

	spec->multiout.dac_nids = spec->private_dac_nids;

	for (i = 0; i < 4; i++) {
		nid = cfg->line_out_pins[i];
		if (nid) {
			/* config dac list */
			switch (i) {
			case AUTO_SEQ_FRONT:
				spec->multiout.dac_nids[i] = 0x10;
				break;
			case AUTO_SEQ_CENLFE:
				spec->multiout.dac_nids[i] = 0x24;
				break;
			case AUTO_SEQ_SURROUND:
				spec->multiout.dac_nids[i] = 0x11;
				break;
			case AUTO_SEQ_SIDE:
				spec->multiout.dac_nids[i] = 0x25;
				break;
			}
		}
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1708B_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", "C/LFE", "Side" };
	hda_nid_t nid_vols[] = {0x16, 0x18, 0x26, 0x27};
	hda_nid_t nid, nid_vol = 0;
	int i, err;

	for (i = 0; i <= AUTO_SEQ_SIDE; i++) {
		nid = cfg->line_out_pins[i];

		if (!nid)
			continue;

		nid_vol = nid_vols[i];

		if (i == AUTO_SEQ_CENLFE) {
			/* Center/LFE */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Center Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "LFE Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Center Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "LFE Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT) {
			/* add control to mixer index 0 */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Master Front Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Master Front Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;

			/* add control to PW3 */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int vt1708B_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = VT1708B_HP_NID; /* AOW3 */

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

	create_hp_imux(spec);

	return 0;
}

/* create playback/capture controls for input pins */
static int vt1708B_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux[0];
	int i, err, idx = 0;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = idx;
	imux->num_items++;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x1a: /* Mic */
			idx = 2;
			break;

		case 0x1b: /* Line In */
			idx = 3;
			break;

		case 0x1e: /* Front Mic */
			idx = 4;
			break;

		case 0x1f: /* CD */
			idx = 1;
			break;
		}
		err = via_new_analog_input(spec, cfg->input_pins[i], labels[i],
					   idx, 0x16);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx;
		imux->num_items++;
	}
	return 0;
}

static int vt1708B_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	err = vt1708B_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1708B_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1708B_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1708B_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = VT1708B_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = VT1708B_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux[0];

	spec->mixers[spec->num_mixers++] = via_hp_mixer;

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1708B_loopbacks[] = {
	{ 0x16, HDA_INPUT, 1 },
	{ 0x16, HDA_INPUT, 2 },
	{ 0x16, HDA_INPUT, 3 },
	{ 0x16, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static int patch_vt1708B_8ch(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1708B_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	spec->init_verbs[spec->num_iverbs++] = vt1708B_8ch_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt1708B_uniwill_init_verbs;

	spec->stream_name_analog = "VT1708B Analog";
	spec->stream_analog_playback = &vt1708B_8ch_pcm_analog_playback;
	spec->stream_analog_capture = &vt1708B_pcm_analog_capture;

	spec->stream_name_digital = "VT1708B Digital";
	spec->stream_digital_playback = &vt1708B_pcm_digital_playback;
	spec->stream_digital_capture = &vt1708B_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1708B_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1708B_adc_nids);
		spec->mixers[spec->num_mixers] = vt1708B_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1708B_loopbacks;
#endif

	return 0;
}

static int patch_vt1708B_4ch(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1708B_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	spec->init_verbs[spec->num_iverbs++] = vt1708B_4ch_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt1708B_uniwill_init_verbs;

	spec->stream_name_analog = "VT1708B Analog";
	spec->stream_analog_playback = &vt1708B_4ch_pcm_analog_playback;
	spec->stream_analog_capture = &vt1708B_pcm_analog_capture;

	spec->stream_name_digital = "VT1708B Digital";
	spec->stream_digital_playback = &vt1708B_pcm_digital_playback;
	spec->stream_digital_capture = &vt1708B_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1708B_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1708B_adc_nids);
		spec->mixers[spec->num_mixers] = vt1708B_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1708B_loopbacks;
#endif

	return 0;
}

/* Patch for VT1708S */

/* capture mixer elements */
static struct snd_kcontrol_new vt1708S_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x13, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x13, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x1A, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x1E, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
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

static struct hda_verb vt1708S_volume_init_verbs[] = {
	/* Unmute ADC0-1 and set the default input to mic-in */
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the
	 * analog-loopback mixer widget */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/* Setup default input of PW4 to MW0 */
	{0x1d, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* PW9 Output enable */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static struct hda_verb vt1708S_uniwill_init_verbs[] = {
	{0x1D, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_HP_EVENT},
	{ }
};

static struct hda_pcm_stream vt1708S_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_pcm_prepare,
		.cleanup = via_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708S_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x13, /* NID to query formats and rates */
	.ops = {
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1708S_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare
	},
};

/* fill in the dac_nids table from the parsed pin configuration */
static int vt1708S_auto_fill_dac_nids(struct via_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	int i;
	hda_nid_t nid;

	spec->multiout.num_dacs = cfg->line_outs;

	spec->multiout.dac_nids = spec->private_dac_nids;

	for (i = 0; i < 4; i++) {
		nid = cfg->line_out_pins[i];
		if (nid) {
			/* config dac list */
			switch (i) {
			case AUTO_SEQ_FRONT:
				spec->multiout.dac_nids[i] = 0x10;
				break;
			case AUTO_SEQ_CENLFE:
				spec->multiout.dac_nids[i] = 0x24;
				break;
			case AUTO_SEQ_SURROUND:
				spec->multiout.dac_nids[i] = 0x11;
				break;
			case AUTO_SEQ_SIDE:
				spec->multiout.dac_nids[i] = 0x25;
				break;
			}
		}
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1708S_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", "C/LFE", "Side" };
	hda_nid_t nid_vols[] = {0x10, 0x11, 0x24, 0x25};
	hda_nid_t nid_mutes[] = {0x1C, 0x18, 0x26, 0x27};
	hda_nid_t nid, nid_vol, nid_mute;
	int i, err;

	for (i = 0; i <= AUTO_SEQ_SIDE; i++) {
		nid = cfg->line_out_pins[i];

		if (!nid)
			continue;

		nid_vol = nid_vols[i];
		nid_mute = nid_mutes[i];

		if (i == AUTO_SEQ_CENLFE) {
			/* Center/LFE */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Center Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "LFE Playback Volume",
					      HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Center Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_mute,
								  1, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "LFE Playback Switch",
					      HDA_COMPOSE_AMP_VAL(nid_mute,
								  2, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT) {
			/* add control to mixer index 0 */
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
					      "Master Front Playback Volume",
					      HDA_COMPOSE_AMP_VAL(0x16, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					      "Master Front Playback Switch",
					      HDA_COMPOSE_AMP_VAL(0x16, 3, 0,
								  HDA_INPUT));
			if (err < 0)
				return err;

			/* Front */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid_mute,
								  3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
					      HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
					      HDA_COMPOSE_AMP_VAL(nid_mute,
								  3, 0,
								  HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int vt1708S_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = VT1708S_HP_NID; /* AOW3 */

	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(0x25, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Headphone Playback Switch",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	create_hp_imux(spec);

	return 0;
}

/* create playback/capture controls for input pins */
static int vt1708S_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux[0];
	int i, err, idx = 0;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = 5;
	imux->num_items++;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x1a: /* Mic */
			idx = 2;
			break;

		case 0x1b: /* Line In */
			idx = 3;
			break;

		case 0x1e: /* Front Mic */
			idx = 4;
			break;

		case 0x1f: /* CD */
			idx = 1;
			break;
		}
		err = via_new_analog_input(spec, cfg->input_pins[i], labels[i],
					   idx, 0x16);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx-1;
		imux->num_items++;
	}
	return 0;
}

static int vt1708S_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;
	static hda_nid_t vt1708s_ignore[] = {0x21, 0};

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   vt1708s_ignore);
	if (err < 0)
		return err;
	err = vt1708S_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1708S_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1708S_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1708S_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = VT1708S_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux[0];

	spec->mixers[spec->num_mixers++] = via_hp_mixer;

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1708S_loopbacks[] = {
	{ 0x16, HDA_INPUT, 1 },
	{ 0x16, HDA_INPUT, 2 },
	{ 0x16, HDA_INPUT, 3 },
	{ 0x16, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static int patch_vt1708S(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1708S_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	spec->init_verbs[spec->num_iverbs++] = vt1708S_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt1708S_uniwill_init_verbs;

	spec->stream_name_analog = "VT1708S Analog";
	spec->stream_analog_playback = &vt1708S_pcm_analog_playback;
	spec->stream_analog_capture = &vt1708S_pcm_analog_capture;

	spec->stream_name_digital = "VT1708S Digital";
	spec->stream_digital_playback = &vt1708S_pcm_digital_playback;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1708S_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1708S_adc_nids);
		spec->mixers[spec->num_mixers] = vt1708S_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1708S_loopbacks;
#endif

	return 0;
}

/* Patch for VT1702 */

/* capture mixer elements */
static struct snd_kcontrol_new vt1702_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x20, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x20, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Digital Mic Capture Volume", 0x1F, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Digital Mic Capture Switch", 0x1F, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Digital Mic Boost Capture Volume", 0x1E, 0x0,
			 HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
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

static struct hda_verb vt1702_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1F, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x20, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: Mic1 = 1, Line = 1, Mic2 = 3 */
	{0x1A, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1A, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x1A, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x1A, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x1A, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/* Setup default input of PW4 to MW0 */
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* PW6 PW7 Output enable */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x1C, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static struct hda_verb vt1702_uniwill_init_verbs[] = {
	{0x01, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_GPIO_EVENT},
	{0x17, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_HP_EVENT},
	{ }
};

static struct hda_pcm_stream vt1702_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1702_pcm_analog_capture = {
	.substreams = 3,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x12, /* NID to query formats and rates */
	.ops = {
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1702_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare
	},
};

/* fill in the dac_nids table from the parsed pin configuration */
static int vt1702_auto_fill_dac_nids(struct via_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = spec->private_dac_nids;

	if (cfg->line_out_pins[0]) {
		/* config dac list */
		spec->multiout.dac_nids[0] = 0x10;
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1702_auto_create_line_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	int err;

	if (!cfg->line_out_pins[0])
		return -1;

	/* add control to mixer index 0 */
	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Master Front Playback Volume",
			      HDA_COMPOSE_AMP_VAL(0x1A, 3, 0, HDA_INPUT));
	if (err < 0)
		return err;
	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Master Front Playback Switch",
			      HDA_COMPOSE_AMP_VAL(0x1A, 3, 0, HDA_INPUT));
	if (err < 0)
		return err;

	/* Front */
	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Front Playback Volume",
			      HDA_COMPOSE_AMP_VAL(0x10, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Front Playback Switch",
			      HDA_COMPOSE_AMP_VAL(0x16, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	return 0;
}

static int vt1702_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = 0x1D;

	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(0x1D, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Headphone Playback Switch",
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	create_hp_imux(spec);

	return 0;
}

/* create playback/capture controls for input pins */
static int vt1702_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux[0];
	int i, err, idx = 0;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = 3;
	imux->num_items++;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x14: /* Mic */
			idx = 1;
			break;

		case 0x15: /* Line In */
			idx = 2;
			break;

		case 0x18: /* Front Mic */
			idx = 3;
			break;
		}
		err = via_new_analog_input(spec, cfg->input_pins[i],
					   labels[i], idx, 0x1A);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx-1;
		imux->num_items++;
	}
	return 0;
}

static int vt1702_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;
	static hda_nid_t vt1702_ignore[] = {0x1C, 0};

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   vt1702_ignore);
	if (err < 0)
		return err;
	err = vt1702_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1702_auto_create_line_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1702_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1702_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = VT1702_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux[0];

	spec->mixers[spec->num_mixers++] = via_hp_mixer;

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1702_loopbacks[] = {
	{ 0x1A, HDA_INPUT, 1 },
	{ 0x1A, HDA_INPUT, 2 },
	{ 0x1A, HDA_INPUT, 3 },
	{ 0x1A, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static int patch_vt1702(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;
	unsigned int response;
	unsigned char control;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1702_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	spec->init_verbs[spec->num_iverbs++] = vt1702_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt1702_uniwill_init_verbs;

	spec->stream_name_analog = "VT1702 Analog";
	spec->stream_analog_playback = &vt1702_pcm_analog_playback;
	spec->stream_analog_capture = &vt1702_pcm_analog_capture;

	spec->stream_name_digital = "VT1702 Digital";
	spec->stream_digital_playback = &vt1702_pcm_digital_playback;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1702_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1702_adc_nids);
		spec->mixers[spec->num_mixers] = vt1702_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1702_loopbacks;
#endif

	/* Open backdoor */
	response = snd_hda_codec_read(codec, codec->afg, 0, 0xF8C, 0);
	control = (unsigned char)(response & 0xff);
	control |= 0x3;
	snd_hda_codec_write(codec,  codec->afg, 0, 0xF88, control);

	/* Enable GPIO 0&1 for volume&mute control */
	/* Enable GPIO 2 for DMIC-DATA */
	response = snd_hda_codec_read(codec, codec->afg, 0, 0xF84, 0);
	control = (unsigned char)((response >> 16) & 0x3f);
	snd_hda_codec_write(codec,  codec->afg, 0, 0xF82, control);

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
	{ .id = 0x1106E710, .name = "VIA VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106E711, .name = "VIA VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106E712, .name = "VIA VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106E713, .name = "VIA VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106E714, .name = "VIA VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106E715, .name = "VIA VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106E716, .name = "VIA VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106E717, .name = "VIA VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106E720, .name = "VIA VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106E721, .name = "VIA VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106E722, .name = "VIA VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106E723, .name = "VIA VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106E724, .name = "VIA VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x1106E725, .name = "VIA VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x1106E726, .name = "VIA VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x1106E727, .name = "VIA VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x11060397, .name = "VIA VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11061397, .name = "VIA VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11062397, .name = "VIA VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11063397, .name = "VIA VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11064397, .name = "VIA VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11065397, .name = "VIA VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11066397, .name = "VIA VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11067397, .name = "VIA VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11060398, .name = "VIA VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11061398, .name = "VIA VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11062398, .name = "VIA VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11063398, .name = "VIA VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11064398, .name = "VIA VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11065398, .name = "VIA VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11066398, .name = "VIA VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11067398, .name = "VIA VT1702",
	  .patch = patch_vt1702},
	{} /* terminator */
};
