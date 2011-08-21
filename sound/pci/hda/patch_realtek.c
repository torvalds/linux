/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for Realtek ALC codecs
 *
 * Copyright (c) 2004 Kailang Yang <kailang@realtek.com.tw>
 *                    PeiSen Hou <pshou@realtek.com.tw>
 *                    Takashi Iwai <tiwai@suse.de>
 *                    Jonathan Woithe <jwoithe@physics.adelaide.edu.au>
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
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/jack.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_beep.h"

/* unsol event tags */
#define ALC_FRONT_EVENT		0x01
#define ALC_DCVOL_EVENT		0x02
#define ALC_HP_EVENT		0x04
#define ALC_MIC_EVENT		0x08

/* for GPIO Poll */
#define GPIO_MASK	0x03

/* extra amp-initialization sequence types */
enum {
	ALC_INIT_NONE,
	ALC_INIT_DEFAULT,
	ALC_INIT_GPIO1,
	ALC_INIT_GPIO2,
	ALC_INIT_GPIO3,
};

struct alc_customize_define {
	unsigned int  sku_cfg;
	unsigned char port_connectivity;
	unsigned char check_sum;
	unsigned char customization;
	unsigned char external_amp;
	unsigned int  enable_pcbeep:1;
	unsigned int  platform_type:1;
	unsigned int  swap:1;
	unsigned int  override:1;
	unsigned int  fixup:1; /* Means that this sku is set by driver, not read from hw */
};

struct alc_fixup;

struct alc_multi_io {
	hda_nid_t pin;		/* multi-io widget pin NID */
	hda_nid_t dac;		/* DAC to be connected */
	unsigned int ctl_in;	/* cached input-pin control value */
};

enum {
	ALC_AUTOMUTE_PIN,	/* change the pin control */
	ALC_AUTOMUTE_AMP,	/* mute/unmute the pin AMP */
	ALC_AUTOMUTE_MIXER,	/* mute/unmute mixer widget AMP */
};

struct alc_spec {
	/* codec parameterization */
	const struct snd_kcontrol_new *mixers[5];	/* mixer arrays */
	unsigned int num_mixers;
	const struct snd_kcontrol_new *cap_mixer;	/* capture mixer */
	unsigned int beep_amp;	/* beep amp value, set via set_beep_amp() */

	const struct hda_verb *init_verbs[10];	/* initialization verbs
						 * don't forget NULL
						 * termination!
						 */
	unsigned int num_init_verbs;

	char stream_name_analog[32];	/* analog PCM stream */
	const struct hda_pcm_stream *stream_analog_playback;
	const struct hda_pcm_stream *stream_analog_capture;
	const struct hda_pcm_stream *stream_analog_alt_playback;
	const struct hda_pcm_stream *stream_analog_alt_capture;

	char stream_name_digital[32];	/* digital PCM stream */
	const struct hda_pcm_stream *stream_digital_playback;
	const struct hda_pcm_stream *stream_digital_capture;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	hda_nid_t alt_dac_nid;
	hda_nid_t slave_dig_outs[3];	/* optional - for auto-parsing */
	int dig_out_type;

	/* capture */
	unsigned int num_adc_nids;
	const hda_nid_t *adc_nids;
	const hda_nid_t *capsrc_nids;
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */
	hda_nid_t mixer_nid;		/* analog-mixer NID */

	/* capture setup for dynamic dual-adc switch */
	hda_nid_t cur_adc;
	unsigned int cur_adc_stream_tag;
	unsigned int cur_adc_format;

	/* capture source */
	unsigned int num_mux_defs;
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];
	hda_nid_t ext_mic_pin;
	hda_nid_t dock_mic_pin;
	hda_nid_t int_mic_pin;

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;
	int need_dac_fix;
	int const_channel_count;
	int ext_channel_count;

	/* PCM information */
	struct hda_pcm pcm_rec[3];	/* used in alc_build_pcms() */

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	struct alc_customize_define cdefine;
	struct snd_array kctls;
	struct hda_input_mux private_imux[3];
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];
	hda_nid_t private_adc_nids[AUTO_CFG_MAX_OUTS];
	hda_nid_t private_capsrc_nids[AUTO_CFG_MAX_OUTS];
	hda_nid_t imux_pins[HDA_MAX_NUM_INPUTS];
	unsigned int dyn_adc_idx[HDA_MAX_NUM_INPUTS];
	int int_mic_idx, ext_mic_idx, dock_mic_idx; /* for auto-mic */

	/* hooks */
	void (*init_hook)(struct hda_codec *codec);
	void (*unsol_event)(struct hda_codec *codec, unsigned int res);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	void (*power_hook)(struct hda_codec *codec);
#endif
	void (*shutup)(struct hda_codec *codec);

	/* for pin sensing */
	unsigned int jack_present: 1;
	unsigned int line_jack_present:1;
	unsigned int master_mute:1;
	unsigned int auto_mic:1;
	unsigned int auto_mic_valid_imux:1;	/* valid imux for auto-mic */
	unsigned int automute:1;	/* HP automute enabled */
	unsigned int detect_line:1;	/* Line-out detection enabled */
	unsigned int automute_lines:1;	/* automute line-out as well */
	unsigned int automute_hp_lo:1;	/* both HP and LO available */

	/* other flags */
	unsigned int no_analog :1; /* digital I/O only */
	unsigned int dyn_adc_switch:1; /* switch ADCs (for ALC275) */
	unsigned int single_input_src:1;
	unsigned int vol_in_capsrc:1; /* use capsrc volume (ADC has no vol) */

	/* auto-mute control */
	int automute_mode;
	hda_nid_t automute_mixer_nid[AUTO_CFG_MAX_OUTS];

	int init_amp;
	int codec_variant;	/* flag for other variants */

	/* for virtual master */
	hda_nid_t vmaster_nid;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_loopback_check loopback;
#endif

	/* for PLL fix */
	hda_nid_t pll_nid;
	unsigned int pll_coef_idx, pll_coef_bit;

	/* fix-up list */
	int fixup_id;
	const struct alc_fixup *fixup_list;
	const char *fixup_name;

	/* multi-io */
	int multi_ios;
	struct alc_multi_io multi_io[4];
};

#define ALC_MODEL_AUTO		0	/* common for all chips */

static bool check_amp_caps(struct hda_codec *codec, hda_nid_t nid,
			   int dir, unsigned int bits)
{
	if (!nid)
		return false;
	if (get_wcaps(codec, nid) & (1 << (dir + 1)))
		if (query_amp_caps(codec, nid, dir) & bits)
			return true;
	return false;
}

#define nid_has_mute(codec, nid, dir) \
	check_amp_caps(codec, nid, dir, AC_AMPCAP_MUTE)
#define nid_has_volume(codec, nid, dir) \
	check_amp_caps(codec, nid, dir, AC_AMPCAP_NUM_STEPS)

/*
 * input MUX handling
 */
static int alc_mux_enum_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int mux_idx = snd_ctl_get_ioffidx(kcontrol, &uinfo->id);
	if (mux_idx >= spec->num_mux_defs)
		mux_idx = 0;
	if (!spec->input_mux[mux_idx].num_items && mux_idx > 0)
		mux_idx = 0;
	return snd_hda_input_mux_info(&spec->input_mux[mux_idx], uinfo);
}

static int alc_mux_enum_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static bool alc_dyn_adc_pcm_resetup(struct hda_codec *codec, int cur)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t new_adc = spec->adc_nids[spec->dyn_adc_idx[cur]];

	if (spec->cur_adc && spec->cur_adc != new_adc) {
		/* stream is running, let's swap the current ADC */
		__snd_hda_codec_cleanup_stream(codec, spec->cur_adc, 1);
		spec->cur_adc = new_adc;
		snd_hda_codec_setup_stream(codec, new_adc,
					   spec->cur_adc_stream_tag, 0,
					   spec->cur_adc_format);
		return true;
	}
	return false;
}

/* select the given imux item; either unmute exclusively or select the route */
static int alc_mux_select(struct hda_codec *codec, unsigned int adc_idx,
			  unsigned int idx, bool force)
{
	struct alc_spec *spec = codec->spec;
	const struct hda_input_mux *imux;
	unsigned int mux_idx;
	int i, type;
	hda_nid_t nid;

	mux_idx = adc_idx >= spec->num_mux_defs ? 0 : adc_idx;
	imux = &spec->input_mux[mux_idx];
	if (!imux->num_items && mux_idx > 0)
		imux = &spec->input_mux[0];

	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (spec->cur_mux[adc_idx] == idx && !force)
		return 0;
	spec->cur_mux[adc_idx] = idx;

	if (spec->dyn_adc_switch) {
		alc_dyn_adc_pcm_resetup(codec, idx);
		adc_idx = spec->dyn_adc_idx[idx];
	}

	nid = spec->capsrc_nids ?
		spec->capsrc_nids[adc_idx] : spec->adc_nids[adc_idx];

	/* no selection? */
	if (snd_hda_get_conn_list(codec, nid, NULL) <= 1)
		return 1;

	type = get_wcaps_type(get_wcaps(codec, nid));
	if (type == AC_WID_AUD_MIX) {
		/* Matrix-mixer style (e.g. ALC882) */
		for (i = 0; i < imux->num_items; i++) {
			unsigned int v = (i == idx) ? 0 : HDA_AMP_MUTE;
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT,
						 imux->items[i].index,
						 HDA_AMP_MUTE, v);
		}
	} else {
		/* MUX style (e.g. ALC880) */
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_CONNECT_SEL,
					  imux->items[idx].index);
	}
	return 1;
}

static int alc_mux_enum_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	return alc_mux_select(codec, adc_idx,
			      ucontrol->value.enumerated.item[0], false);
}

/*
 * set up the input pin config (depending on the given auto-pin type)
 */
static void alc_set_input_pin(struct hda_codec *codec, hda_nid_t nid,
			      int auto_pin_type)
{
	unsigned int val = PIN_IN;

	if (auto_pin_type == AUTO_PIN_MIC) {
		unsigned int pincap;
		unsigned int oldval;
		oldval = snd_hda_codec_read(codec, nid, 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		pincap = snd_hda_query_pin_caps(codec, nid);
		pincap = (pincap & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
		/* if the default pin setup is vref50, we give it priority */
		if ((pincap & AC_PINCAP_VREF_80) && oldval != PIN_VREF50)
			val = PIN_VREF80;
		else if (pincap & AC_PINCAP_VREF_50)
			val = PIN_VREF50;
		else if (pincap & AC_PINCAP_VREF_100)
			val = PIN_VREF100;
		else if (pincap & AC_PINCAP_VREF_GRD)
			val = PIN_VREFGRD;
	}
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, val);
}

/*
 * Append the given mixer and verb elements for the later use
 * The mixer array is referred in build_controls(), and init_verbs are
 * called in init().
 */
static void add_mixer(struct alc_spec *spec, const struct snd_kcontrol_new *mix)
{
	if (snd_BUG_ON(spec->num_mixers >= ARRAY_SIZE(spec->mixers)))
		return;
	spec->mixers[spec->num_mixers++] = mix;
}

static void add_verb(struct alc_spec *spec, const struct hda_verb *verb)
{
	if (snd_BUG_ON(spec->num_init_verbs >= ARRAY_SIZE(spec->init_verbs)))
		return;
	spec->init_verbs[spec->num_init_verbs++] = verb;
}

/*
 * GPIO setup tables, used in initialization
 */
/* Enable GPIO mask and set output */
static const struct hda_verb alc_gpio1_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x01},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
	{ }
};

static const struct hda_verb alc_gpio2_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x02},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x02},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x02},
	{ }
};

static const struct hda_verb alc_gpio3_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x03},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x03},
	{ }
};

/*
 * Fix hardware PLL issue
 * On some codecs, the analog PLL gating control must be off while
 * the default value is 1.
 */
static void alc_fix_pll(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int val;

	if (!spec->pll_nid)
		return;
	snd_hda_codec_write(codec, spec->pll_nid, 0, AC_VERB_SET_COEF_INDEX,
			    spec->pll_coef_idx);
	val = snd_hda_codec_read(codec, spec->pll_nid, 0,
				 AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, spec->pll_nid, 0, AC_VERB_SET_COEF_INDEX,
			    spec->pll_coef_idx);
	snd_hda_codec_write(codec, spec->pll_nid, 0, AC_VERB_SET_PROC_COEF,
			    val & ~(1 << spec->pll_coef_bit));
}

static void alc_fix_pll_init(struct hda_codec *codec, hda_nid_t nid,
			     unsigned int coef_idx, unsigned int coef_bit)
{
	struct alc_spec *spec = codec->spec;
	spec->pll_nid = nid;
	spec->pll_coef_idx = coef_idx;
	spec->pll_coef_bit = coef_bit;
	alc_fix_pll(codec);
}

/*
 * Jack-reporting via input-jack layer
 */

/* initialization of jacks; currently checks only a few known pins */
static int alc_init_jacks(struct hda_codec *codec)
{
#ifdef CONFIG_SND_HDA_INPUT_JACK
	struct alc_spec *spec = codec->spec;
	int err;
	unsigned int hp_nid = spec->autocfg.hp_pins[0];
	unsigned int mic_nid = spec->ext_mic_pin;
	unsigned int dock_nid = spec->dock_mic_pin;

	if (hp_nid) {
		err = snd_hda_input_jack_add(codec, hp_nid,
					     SND_JACK_HEADPHONE, NULL);
		if (err < 0)
			return err;
		snd_hda_input_jack_report(codec, hp_nid);
	}

	if (mic_nid) {
		err = snd_hda_input_jack_add(codec, mic_nid,
					     SND_JACK_MICROPHONE, NULL);
		if (err < 0)
			return err;
		snd_hda_input_jack_report(codec, mic_nid);
	}
	if (dock_nid) {
		err = snd_hda_input_jack_add(codec, dock_nid,
					     SND_JACK_MICROPHONE, NULL);
		if (err < 0)
			return err;
		snd_hda_input_jack_report(codec, dock_nid);
	}
#endif /* CONFIG_SND_HDA_INPUT_JACK */
	return 0;
}

/*
 * Jack detections for HP auto-mute and mic-switch
 */

/* check each pin in the given array; returns true if any of them is plugged */
static bool detect_jacks(struct hda_codec *codec, int num_pins, hda_nid_t *pins)
{
	int i, present = 0;

	for (i = 0; i < num_pins; i++) {
		hda_nid_t nid = pins[i];
		if (!nid)
			break;
		snd_hda_input_jack_report(codec, nid);
		present |= snd_hda_jack_detect(codec, nid);
	}
	return present;
}

/* standard HP/line-out auto-mute helper */
static void do_automute(struct hda_codec *codec, int num_pins, hda_nid_t *pins,
			bool mute, bool hp_out)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute_bits = mute ? HDA_AMP_MUTE : 0;
	unsigned int pin_bits = mute ? 0 : (hp_out ? PIN_HP : PIN_OUT);
	int i;

	for (i = 0; i < num_pins; i++) {
		hda_nid_t nid = pins[i];
		if (!nid)
			break;
		switch (spec->automute_mode) {
		case ALC_AUTOMUTE_PIN:
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    pin_bits);
			break;
		case ALC_AUTOMUTE_AMP:
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, mute_bits);
			break;
		case ALC_AUTOMUTE_MIXER:
			nid = spec->automute_mixer_nid[i];
			if (!nid)
				break;
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT, 0,
						 HDA_AMP_MUTE, mute_bits);
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT, 1,
						 HDA_AMP_MUTE, mute_bits);
			break;
		}
	}
}

/* Toggle internal speakers muting */
static void update_speakers(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int on;

	/* Control HP pins/amps depending on master_mute state;
	 * in general, HP pins/amps control should be enabled in all cases,
	 * but currently set only for master_mute, just to be safe
	 */
	do_automute(codec, ARRAY_SIZE(spec->autocfg.hp_pins),
		    spec->autocfg.hp_pins, spec->master_mute, true);

	if (!spec->automute)
		on = 0;
	else
		on = spec->jack_present | spec->line_jack_present;
	on |= spec->master_mute;
	do_automute(codec, ARRAY_SIZE(spec->autocfg.speaker_pins),
		    spec->autocfg.speaker_pins, on, false);

	/* toggle line-out mutes if needed, too */
	/* if LO is a copy of either HP or Speaker, don't need to handle it */
	if (spec->autocfg.line_out_pins[0] == spec->autocfg.hp_pins[0] ||
	    spec->autocfg.line_out_pins[0] == spec->autocfg.speaker_pins[0])
		return;
	if (!spec->automute_lines || !spec->automute)
		on = 0;
	else
		on = spec->jack_present;
	on |= spec->master_mute;
	do_automute(codec, ARRAY_SIZE(spec->autocfg.line_out_pins),
		    spec->autocfg.line_out_pins, on, false);
}

/* standard HP-automute helper */
static void alc_hp_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->automute)
		return;
	spec->jack_present =
		detect_jacks(codec, ARRAY_SIZE(spec->autocfg.hp_pins),
			     spec->autocfg.hp_pins);
	update_speakers(codec);
}

/* standard line-out-automute helper */
static void alc_line_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->automute || !spec->detect_line)
		return;
	spec->line_jack_present =
		detect_jacks(codec, ARRAY_SIZE(spec->autocfg.line_out_pins),
			     spec->autocfg.line_out_pins);
	update_speakers(codec);
}

#define get_connection_index(codec, mux, nid) \
	snd_hda_get_conn_index(codec, mux, nid, 0)

/* standard mic auto-switch helper */
static void alc_mic_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t *pins = spec->imux_pins;

	if (!spec->auto_mic || !spec->auto_mic_valid_imux)
		return;
	if (snd_BUG_ON(!spec->adc_nids))
		return;
	if (snd_BUG_ON(spec->int_mic_idx < 0 || spec->ext_mic_idx < 0))
		return;

	if (snd_hda_jack_detect(codec, pins[spec->ext_mic_idx]))
		alc_mux_select(codec, 0, spec->ext_mic_idx, false);
	else if (spec->dock_mic_idx >= 0 &&
		   snd_hda_jack_detect(codec, pins[spec->dock_mic_idx]))
		alc_mux_select(codec, 0, spec->dock_mic_idx, false);
	else
		alc_mux_select(codec, 0, spec->int_mic_idx, false);

	snd_hda_input_jack_report(codec, pins[spec->ext_mic_idx]);
	if (spec->dock_mic_idx >= 0)
		snd_hda_input_jack_report(codec, pins[spec->dock_mic_idx]);
}

/* unsolicited event for HP jack sensing */
static void alc_sku_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if (codec->vendor_id == 0x10ec0880)
		res >>= 28;
	else
		res >>= 26;
	switch (res) {
	case ALC_HP_EVENT:
		alc_hp_automute(codec);
		break;
	case ALC_FRONT_EVENT:
		alc_line_automute(codec);
		break;
	case ALC_MIC_EVENT:
		alc_mic_automute(codec);
		break;
	}
}

/* call init functions of standard auto-mute helpers */
static void alc_inithook(struct hda_codec *codec)
{
	alc_hp_automute(codec);
	alc_line_automute(codec);
	alc_mic_automute(codec);
}

/* additional initialization for ALC888 variants */
static void alc888_coef_init(struct hda_codec *codec)
{
	unsigned int tmp;

	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 0);
	tmp = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 7);
	if ((tmp & 0xf0) == 0x20)
		/* alc888S-VC */
		snd_hda_codec_read(codec, 0x20, 0,
				   AC_VERB_SET_PROC_COEF, 0x830);
	 else
		 /* alc888-VB */
		 snd_hda_codec_read(codec, 0x20, 0,
				    AC_VERB_SET_PROC_COEF, 0x3030);
}

/* additional initialization for ALC889 variants */
static void alc889_coef_init(struct hda_codec *codec)
{
	unsigned int tmp;

	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 7);
	tmp = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 7);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_PROC_COEF, tmp|0x2010);
}

/* turn on/off EAPD control (only if available) */
static void set_eapd(struct hda_codec *codec, hda_nid_t nid, int on)
{
	if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
		return;
	if (snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_EAPD)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_EAPD_BTLENABLE,
				    on ? 2 : 0);
}

/* turn on/off EAPD controls of the codec */
static void alc_auto_setup_eapd(struct hda_codec *codec, bool on)
{
	/* We currently only handle front, HP */
	static hda_nid_t pins[] = {
		0x0f, 0x10, 0x14, 0x15, 0
	};
	hda_nid_t *p;
	for (p = pins; *p; p++)
		set_eapd(codec, *p, on);
}

/* generic shutup callback;
 * just turning off EPAD and a little pause for avoiding pop-noise
 */
static void alc_eapd_shutup(struct hda_codec *codec)
{
	alc_auto_setup_eapd(codec, false);
	msleep(200);
}

/* generic EAPD initialization */
static void alc_auto_init_amp(struct hda_codec *codec, int type)
{
	unsigned int tmp;

	alc_auto_setup_eapd(codec, true);
	switch (type) {
	case ALC_INIT_GPIO1:
		snd_hda_sequence_write(codec, alc_gpio1_init_verbs);
		break;
	case ALC_INIT_GPIO2:
		snd_hda_sequence_write(codec, alc_gpio2_init_verbs);
		break;
	case ALC_INIT_GPIO3:
		snd_hda_sequence_write(codec, alc_gpio3_init_verbs);
		break;
	case ALC_INIT_DEFAULT:
		switch (codec->vendor_id) {
		case 0x10ec0260:
			snd_hda_codec_write(codec, 0x1a, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			tmp = snd_hda_codec_read(codec, 0x1a, 0,
						 AC_VERB_GET_PROC_COEF, 0);
			snd_hda_codec_write(codec, 0x1a, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			snd_hda_codec_write(codec, 0x1a, 0,
					    AC_VERB_SET_PROC_COEF,
					    tmp | 0x2010);
			break;
		case 0x10ec0262:
		case 0x10ec0880:
		case 0x10ec0882:
		case 0x10ec0883:
		case 0x10ec0885:
		case 0x10ec0887:
		/*case 0x10ec0889:*/ /* this causes an SPDIF problem */
			alc889_coef_init(codec);
			break;
		case 0x10ec0888:
			alc888_coef_init(codec);
			break;
#if 0 /* XXX: This may cause the silent output on speaker on some machines */
		case 0x10ec0267:
		case 0x10ec0268:
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			tmp = snd_hda_codec_read(codec, 0x20, 0,
						 AC_VERB_GET_PROC_COEF, 0);
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_PROC_COEF,
					    tmp | 0x3000);
			break;
#endif /* XXX */
		}
		break;
	}
}

/*
 * Auto-Mute mode mixer enum support
 */
static int alc_automute_mode_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	static const char * const texts2[] = {
		"Disabled", "Enabled"
	};
	static const char * const texts3[] = {
		"Disabled", "Speaker Only", "Line-Out+Speaker"
	};
	const char * const *texts;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	if (spec->automute_hp_lo) {
		uinfo->value.enumerated.items = 3;
		texts = texts3;
	} else {
		uinfo->value.enumerated.items = 2;
		texts = texts2;
	}
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int alc_automute_mode_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int val;
	if (!spec->automute)
		val = 0;
	else if (!spec->automute_lines)
		val = 1;
	else
		val = 2;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int alc_automute_mode_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;

	switch (ucontrol->value.enumerated.item[0]) {
	case 0:
		if (!spec->automute)
			return 0;
		spec->automute = 0;
		break;
	case 1:
		if (spec->automute && !spec->automute_lines)
			return 0;
		spec->automute = 1;
		spec->automute_lines = 0;
		break;
	case 2:
		if (!spec->automute_hp_lo)
			return -EINVAL;
		if (spec->automute && spec->automute_lines)
			return 0;
		spec->automute = 1;
		spec->automute_lines = 1;
		break;
	default:
		return -EINVAL;
	}
	update_speakers(codec);
	return 1;
}

static const struct snd_kcontrol_new alc_automute_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Auto-Mute Mode",
	.info = alc_automute_mode_info,
	.get = alc_automute_mode_get,
	.put = alc_automute_mode_put,
};

static struct snd_kcontrol_new *alc_kcontrol_new(struct alc_spec *spec)
{
	snd_array_init(&spec->kctls, sizeof(struct snd_kcontrol_new), 32);
	return snd_array_new(&spec->kctls);
}

static int alc_add_automute_mode_enum(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;

	knew = alc_kcontrol_new(spec);
	if (!knew)
		return -ENOMEM;
	*knew = alc_automute_mode_enum;
	knew->name = kstrdup("Auto-Mute Mode", GFP_KERNEL);
	if (!knew->name)
		return -ENOMEM;
	return 0;
}

/*
 * Check the availability of HP/line-out auto-mute;
 * Set up appropriately if really supported
 */
static void alc_init_auto_hp(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int present = 0;
	int i;

	if (cfg->hp_pins[0])
		present++;
	if (cfg->line_out_pins[0])
		present++;
	if (cfg->speaker_pins[0])
		present++;
	if (present < 2) /* need two different output types */
		return;
	if (present == 3)
		spec->automute_hp_lo = 1; /* both HP and LO automute */

	if (!cfg->speaker_pins[0] &&
	    cfg->line_out_type == AUTO_PIN_SPEAKER_OUT) {
		memcpy(cfg->speaker_pins, cfg->line_out_pins,
		       sizeof(cfg->speaker_pins));
		cfg->speaker_outs = cfg->line_outs;
	}

	if (!cfg->hp_pins[0] &&
	    cfg->line_out_type == AUTO_PIN_HP_OUT) {
		memcpy(cfg->hp_pins, cfg->line_out_pins,
		       sizeof(cfg->hp_pins));
		cfg->hp_outs = cfg->line_outs;
	}

	for (i = 0; i < cfg->hp_outs; i++) {
		hda_nid_t nid = cfg->hp_pins[i];
		if (!is_jack_detectable(codec, nid))
			continue;
		snd_printdd("realtek: Enable HP auto-muting on NID 0x%x\n",
			    nid);
		snd_hda_codec_write_cache(codec, nid, 0,
				  AC_VERB_SET_UNSOLICITED_ENABLE,
				  AC_USRSP_EN | ALC_HP_EVENT);
		spec->automute = 1;
		spec->automute_mode = ALC_AUTOMUTE_PIN;
	}
	if (spec->automute && cfg->line_out_pins[0] &&
	    cfg->speaker_pins[0] &&
	    cfg->line_out_pins[0] != cfg->hp_pins[0] &&
	    cfg->line_out_pins[0] != cfg->speaker_pins[0]) {
		for (i = 0; i < cfg->line_outs; i++) {
			hda_nid_t nid = cfg->line_out_pins[i];
			if (!is_jack_detectable(codec, nid))
				continue;
			snd_printdd("realtek: Enable Line-Out auto-muting "
				    "on NID 0x%x\n", nid);
			snd_hda_codec_write_cache(codec, nid, 0,
					AC_VERB_SET_UNSOLICITED_ENABLE,
					AC_USRSP_EN | ALC_FRONT_EVENT);
			spec->detect_line = 1;
		}
		spec->automute_lines = spec->detect_line;
	}

	if (spec->automute) {
		/* create a control for automute mode */
		alc_add_automute_mode_enum(codec);
		spec->unsol_event = alc_sku_unsol_event;
	}
}

/* return the position of NID in the list, or -1 if not found */
static int find_idx_in_nid_list(hda_nid_t nid, const hda_nid_t *list, int nums)
{
	int i;
	for (i = 0; i < nums; i++)
		if (list[i] == nid)
			return i;
	return -1;
}

/* check whether dynamic ADC-switching is available */
static bool alc_check_dyn_adc_switch(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->private_imux[0];
	int i, n, idx;
	hda_nid_t cap, pin;

	if (imux != spec->input_mux) /* no dynamic imux? */
		return false;

	for (n = 0; n < spec->num_adc_nids; n++) {
		cap = spec->private_capsrc_nids[n];
		for (i = 0; i < imux->num_items; i++) {
			pin = spec->imux_pins[i];
			if (!pin)
				return false;
			if (get_connection_index(codec, cap, pin) < 0)
				break;
		}
		if (i >= imux->num_items)
			return true; /* no ADC-switch is needed */
	}

	for (i = 0; i < imux->num_items; i++) {
		pin = spec->imux_pins[i];
		for (n = 0; n < spec->num_adc_nids; n++) {
			cap = spec->private_capsrc_nids[n];
			idx = get_connection_index(codec, cap, pin);
			if (idx >= 0) {
				imux->items[i].index = idx;
				spec->dyn_adc_idx[i] = n;
				break;
			}
		}
	}

	snd_printdd("realtek: enabling ADC switching\n");
	spec->dyn_adc_switch = 1;
	return true;
}

/* rebuild imux for matching with the given auto-mic pins (if not yet) */
static bool alc_rebuild_imux_for_auto_mic(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct hda_input_mux *imux;
	static char * const texts[3] = {
		"Mic", "Internal Mic", "Dock Mic"
	};
	int i;

	if (!spec->auto_mic)
		return false;
	imux = &spec->private_imux[0];
	if (spec->input_mux == imux)
		return true;
	spec->imux_pins[0] = spec->ext_mic_pin;
	spec->imux_pins[1] = spec->int_mic_pin;
	spec->imux_pins[2] = spec->dock_mic_pin;
	for (i = 0; i < 3; i++) {
		strcpy(imux->items[i].label, texts[i]);
		if (spec->imux_pins[i])
			imux->num_items = i + 1;
	}
	spec->num_mux_defs = 1;
	spec->input_mux = imux;
	return true;
}

/* check whether all auto-mic pins are valid; setup indices if OK */
static bool alc_auto_mic_check_imux(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct hda_input_mux *imux;

	if (!spec->auto_mic)
		return false;
	if (spec->auto_mic_valid_imux)
		return true; /* already checked */

	/* fill up imux indices */
	if (!alc_check_dyn_adc_switch(codec)) {
		spec->auto_mic = 0;
		return false;
	}

	imux = spec->input_mux;
	spec->ext_mic_idx = find_idx_in_nid_list(spec->ext_mic_pin,
					spec->imux_pins, imux->num_items);
	spec->int_mic_idx = find_idx_in_nid_list(spec->int_mic_pin,
					spec->imux_pins, imux->num_items);
	spec->dock_mic_idx = find_idx_in_nid_list(spec->dock_mic_pin,
					spec->imux_pins, imux->num_items);
	if (spec->ext_mic_idx < 0 || spec->int_mic_idx < 0) {
		spec->auto_mic = 0;
		return false; /* no corresponding imux */
	}

	snd_hda_codec_write_cache(codec, spec->ext_mic_pin, 0,
				  AC_VERB_SET_UNSOLICITED_ENABLE,
				  AC_USRSP_EN | ALC_MIC_EVENT);
	if (spec->dock_mic_pin)
		snd_hda_codec_write_cache(codec, spec->dock_mic_pin, 0,
				  AC_VERB_SET_UNSOLICITED_ENABLE,
				  AC_USRSP_EN | ALC_MIC_EVENT);

	spec->auto_mic_valid_imux = 1;
	spec->auto_mic = 1;
	return true;
}

/*
 * Check the availability of auto-mic switch;
 * Set up if really supported
 */
static void alc_init_auto_mic(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t fixed, ext, dock;
	int i;

	spec->ext_mic_idx = spec->int_mic_idx = spec->dock_mic_idx = -1;

	fixed = ext = dock = 0;
	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		unsigned int defcfg;
		defcfg = snd_hda_codec_get_pincfg(codec, nid);
		switch (snd_hda_get_input_pin_attr(defcfg)) {
		case INPUT_PIN_ATTR_INT:
			if (fixed)
				return; /* already occupied */
			if (cfg->inputs[i].type != AUTO_PIN_MIC)
				return; /* invalid type */
			fixed = nid;
			break;
		case INPUT_PIN_ATTR_UNUSED:
			return; /* invalid entry */
		case INPUT_PIN_ATTR_DOCK:
			if (dock)
				return; /* already occupied */
			if (cfg->inputs[i].type > AUTO_PIN_LINE_IN)
				return; /* invalid type */
			dock = nid;
			break;
		default:
			if (ext)
				return; /* already occupied */
			if (cfg->inputs[i].type != AUTO_PIN_MIC)
				return; /* invalid type */
			ext = nid;
			break;
		}
	}
	if (!ext && dock) {
		ext = dock;
		dock = 0;
	}
	if (!ext || !fixed)
		return;
	if (!is_jack_detectable(codec, ext))
		return; /* no unsol support */
	if (dock && !is_jack_detectable(codec, dock))
		return; /* no unsol support */

	/* check imux indices */
	spec->ext_mic_pin = ext;
	spec->int_mic_pin = fixed;
	spec->dock_mic_pin = dock;

	spec->auto_mic = 1;
	if (!alc_auto_mic_check_imux(codec))
		return;

	snd_printdd("realtek: Enable auto-mic switch on NID 0x%x/0x%x/0x%x\n",
		    ext, fixed, dock);
	spec->unsol_event = alc_sku_unsol_event;
}

/* check the availabilities of auto-mute and auto-mic switches */
static void alc_auto_check_switches(struct hda_codec *codec)
{
	alc_init_auto_hp(codec);
	alc_init_auto_mic(codec);
}

/*
 * Realtek SSID verification
 */

/* Could be any non-zero and even value. When used as fixup, tells
 * the driver to ignore any present sku defines.
 */
#define ALC_FIXUP_SKU_IGNORE (2)

static int alc_auto_parse_customize_define(struct hda_codec *codec)
{
	unsigned int ass, tmp, i;
	unsigned nid = 0;
	struct alc_spec *spec = codec->spec;

	spec->cdefine.enable_pcbeep = 1; /* assume always enabled */

	if (spec->cdefine.fixup) {
		ass = spec->cdefine.sku_cfg;
		if (ass == ALC_FIXUP_SKU_IGNORE)
			return -1;
		goto do_sku;
	}

	ass = codec->subsystem_id & 0xffff;
	if (ass != codec->bus->pci->subsystem_device && (ass & 1))
		goto do_sku;

	nid = 0x1d;
	if (codec->vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_get_pincfg(codec, nid);

	if (!(ass & 1)) {
		printk(KERN_INFO "hda_codec: %s: SKU not ready 0x%08x\n",
		       codec->chip_name, ass);
		return -1;
	}

	/* check sum */
	tmp = 0;
	for (i = 1; i < 16; i++) {
		if ((ass >> i) & 1)
			tmp++;
	}
	if (((ass >> 16) & 0xf) != tmp)
		return -1;

	spec->cdefine.port_connectivity = ass >> 30;
	spec->cdefine.enable_pcbeep = (ass & 0x100000) >> 20;
	spec->cdefine.check_sum = (ass >> 16) & 0xf;
	spec->cdefine.customization = ass >> 8;
do_sku:
	spec->cdefine.sku_cfg = ass;
	spec->cdefine.external_amp = (ass & 0x38) >> 3;
	spec->cdefine.platform_type = (ass & 0x4) >> 2;
	spec->cdefine.swap = (ass & 0x2) >> 1;
	spec->cdefine.override = ass & 0x1;

	snd_printd("SKU: Nid=0x%x sku_cfg=0x%08x\n",
		   nid, spec->cdefine.sku_cfg);
	snd_printd("SKU: port_connectivity=0x%x\n",
		   spec->cdefine.port_connectivity);
	snd_printd("SKU: enable_pcbeep=0x%x\n", spec->cdefine.enable_pcbeep);
	snd_printd("SKU: check_sum=0x%08x\n", spec->cdefine.check_sum);
	snd_printd("SKU: customization=0x%08x\n", spec->cdefine.customization);
	snd_printd("SKU: external_amp=0x%x\n", spec->cdefine.external_amp);
	snd_printd("SKU: platform_type=0x%x\n", spec->cdefine.platform_type);
	snd_printd("SKU: swap=0x%x\n", spec->cdefine.swap);
	snd_printd("SKU: override=0x%x\n", spec->cdefine.override);

	return 0;
}

/* return true if the given NID is found in the list */
static bool found_in_nid_list(hda_nid_t nid, const hda_nid_t *list, int nums)
{
	return find_idx_in_nid_list(nid, list, nums) >= 0;
}

/* check subsystem ID and set up device-specific initialization;
 * return 1 if initialized, 0 if invalid SSID
 */
/* 32-bit subsystem ID for BIOS loading in HD Audio codec.
 *	31 ~ 16 :	Manufacture ID
 *	15 ~ 8	:	SKU ID
 *	7  ~ 0	:	Assembly ID
 *	port-A --> pin 39/41, port-E --> pin 14/15, port-D --> pin 35/36
 */
static int alc_subsystem_id(struct hda_codec *codec,
			    hda_nid_t porta, hda_nid_t porte,
			    hda_nid_t portd, hda_nid_t porti)
{
	unsigned int ass, tmp, i;
	unsigned nid;
	struct alc_spec *spec = codec->spec;

	if (spec->cdefine.fixup) {
		ass = spec->cdefine.sku_cfg;
		if (ass == ALC_FIXUP_SKU_IGNORE)
			return 0;
		goto do_sku;
	}

	ass = codec->subsystem_id & 0xffff;
	if ((ass != codec->bus->pci->subsystem_device) && (ass & 1))
		goto do_sku;

	/* invalid SSID, check the special NID pin defcfg instead */
	/*
	 * 31~30	: port connectivity
	 * 29~21	: reserve
	 * 20		: PCBEEP input
	 * 19~16	: Check sum (15:1)
	 * 15~1		: Custom
	 * 0		: override
	*/
	nid = 0x1d;
	if (codec->vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_get_pincfg(codec, nid);
	snd_printd("realtek: No valid SSID, "
		   "checking pincfg 0x%08x for NID 0x%x\n",
		   ass, nid);
	if (!(ass & 1))
		return 0;
	if ((ass >> 30) != 1)	/* no physical connection */
		return 0;

	/* check sum */
	tmp = 0;
	for (i = 1; i < 16; i++) {
		if ((ass >> i) & 1)
			tmp++;
	}
	if (((ass >> 16) & 0xf) != tmp)
		return 0;
do_sku:
	snd_printd("realtek: Enabling init ASM_ID=0x%04x CODEC_ID=%08x\n",
		   ass & 0xffff, codec->vendor_id);
	/*
	 * 0 : override
	 * 1 :	Swap Jack
	 * 2 : 0 --> Desktop, 1 --> Laptop
	 * 3~5 : External Amplifier control
	 * 7~6 : Reserved
	*/
	tmp = (ass & 0x38) >> 3;	/* external Amp control */
	switch (tmp) {
	case 1:
		spec->init_amp = ALC_INIT_GPIO1;
		break;
	case 3:
		spec->init_amp = ALC_INIT_GPIO2;
		break;
	case 7:
		spec->init_amp = ALC_INIT_GPIO3;
		break;
	case 5:
	default:
		spec->init_amp = ALC_INIT_DEFAULT;
		break;
	}

	/* is laptop or Desktop and enable the function "Mute internal speaker
	 * when the external headphone out jack is plugged"
	 */
	if (!(ass & 0x8000))
		return 1;
	/*
	 * 10~8 : Jack location
	 * 12~11: Headphone out -> 00: PortA, 01: PortE, 02: PortD, 03: Resvered
	 * 14~13: Resvered
	 * 15   : 1 --> enable the function "Mute internal speaker
	 *	        when the external headphone out jack is plugged"
	 */
	if (!spec->autocfg.hp_pins[0]) {
		hda_nid_t nid;
		tmp = (ass >> 11) & 0x3;	/* HP to chassis */
		if (tmp == 0)
			nid = porta;
		else if (tmp == 1)
			nid = porte;
		else if (tmp == 2)
			nid = portd;
		else if (tmp == 3)
			nid = porti;
		else
			return 1;
		if (found_in_nid_list(nid, spec->autocfg.line_out_pins,
				      spec->autocfg.line_outs))
			return 1;
		spec->autocfg.hp_pins[0] = nid;
	}
	return 1;
}

/* Check the validity of ALC subsystem-id
 * ports contains an array of 4 pin NIDs for port-A, E, D and I */
static void alc_ssid_check(struct hda_codec *codec, const hda_nid_t *ports)
{
	if (!alc_subsystem_id(codec, ports[0], ports[1], ports[2], ports[3])) {
		struct alc_spec *spec = codec->spec;
		snd_printd("realtek: "
			   "Enable default setup for auto mode as fallback\n");
		spec->init_amp = ALC_INIT_DEFAULT;
	}
}

/*
 * Fix-up pin default configurations and add default verbs
 */

struct alc_pincfg {
	hda_nid_t nid;
	u32 val;
};

struct alc_model_fixup {
	const int id;
	const char *name;
};

struct alc_fixup {
	int type;
	bool chained;
	int chain_id;
	union {
		unsigned int sku;
		const struct alc_pincfg *pins;
		const struct hda_verb *verbs;
		void (*func)(struct hda_codec *codec,
			     const struct alc_fixup *fix,
			     int action);
	} v;
};

enum {
	ALC_FIXUP_INVALID,
	ALC_FIXUP_SKU,
	ALC_FIXUP_PINS,
	ALC_FIXUP_VERBS,
	ALC_FIXUP_FUNC,
};

enum {
	ALC_FIXUP_ACT_PRE_PROBE,
	ALC_FIXUP_ACT_PROBE,
	ALC_FIXUP_ACT_INIT,
};

static void alc_apply_fixup(struct hda_codec *codec, int action)
{
	struct alc_spec *spec = codec->spec;
	int id = spec->fixup_id;
#ifdef CONFIG_SND_DEBUG_VERBOSE
	const char *modelname = spec->fixup_name;
#endif
	int depth = 0;

	if (!spec->fixup_list)
		return;

	while (id >= 0) {
		const struct alc_fixup *fix = spec->fixup_list + id;
		const struct alc_pincfg *cfg;

		switch (fix->type) {
		case ALC_FIXUP_SKU:
			if (action != ALC_FIXUP_ACT_PRE_PROBE || !fix->v.sku)
				break;;
			snd_printdd(KERN_INFO "hda_codec: %s: "
				    "Apply sku override for %s\n",
				    codec->chip_name, modelname);
			spec->cdefine.sku_cfg = fix->v.sku;
			spec->cdefine.fixup = 1;
			break;
		case ALC_FIXUP_PINS:
			cfg = fix->v.pins;
			if (action != ALC_FIXUP_ACT_PRE_PROBE || !cfg)
				break;
			snd_printdd(KERN_INFO "hda_codec: %s: "
				    "Apply pincfg for %s\n",
				    codec->chip_name, modelname);
			for (; cfg->nid; cfg++)
				snd_hda_codec_set_pincfg(codec, cfg->nid,
							 cfg->val);
			break;
		case ALC_FIXUP_VERBS:
			if (action != ALC_FIXUP_ACT_PROBE || !fix->v.verbs)
				break;
			snd_printdd(KERN_INFO "hda_codec: %s: "
				    "Apply fix-verbs for %s\n",
				    codec->chip_name, modelname);
			add_verb(codec->spec, fix->v.verbs);
			break;
		case ALC_FIXUP_FUNC:
			if (!fix->v.func)
				break;
			snd_printdd(KERN_INFO "hda_codec: %s: "
				    "Apply fix-func for %s\n",
				    codec->chip_name, modelname);
			fix->v.func(codec, fix, action);
			break;
		default:
			snd_printk(KERN_ERR "hda_codec: %s: "
				   "Invalid fixup type %d\n",
				   codec->chip_name, fix->type);
			break;
		}
		if (!fix->chained)
			break;
		if (++depth > 10)
			break;
		id = fix->chain_id;
	}
}

static void alc_pick_fixup(struct hda_codec *codec,
			   const struct alc_model_fixup *models,
			   const struct snd_pci_quirk *quirk,
			   const struct alc_fixup *fixlist)
{
	struct alc_spec *spec = codec->spec;
	int id = -1;
	const char *name = NULL;

	if (codec->modelname && models) {
		while (models->name) {
			if (!strcmp(codec->modelname, models->name)) {
				id = models->id;
				name = models->name;
				break;
			}
			models++;
		}
	}
	if (id < 0) {
		quirk = snd_pci_quirk_lookup(codec->bus->pci, quirk);
		if (quirk) {
			id = quirk->value;
#ifdef CONFIG_SND_DEBUG_VERBOSE
			name = quirk->name;
#endif
		}
	}

	spec->fixup_id = id;
	if (id >= 0) {
		spec->fixup_list = fixlist;
		spec->fixup_name = name;
	}
}

/*
 * COEF access helper functions
 */
static int alc_read_coef_idx(struct hda_codec *codec,
			unsigned int coef_idx)
{
	unsigned int val;
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX,
		    		coef_idx);
	val = snd_hda_codec_read(codec, 0x20, 0,
			 	AC_VERB_GET_PROC_COEF, 0);
	return val;
}

static void alc_write_coef_idx(struct hda_codec *codec, unsigned int coef_idx,
							unsigned int coef_val)
{
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX,
			    coef_idx);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_PROC_COEF,
			    coef_val);
}

/*
 * Digital I/O handling
 */

/* set right pin controls for digital I/O */
static void alc_auto_init_digital(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;
	hda_nid_t pin, dac;

	for (i = 0; i < spec->autocfg.dig_outs; i++) {
		pin = spec->autocfg.dig_out_pins[i];
		if (!pin)
			continue;
		snd_hda_codec_write(codec, pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		if (!i)
			dac = spec->multiout.dig_out_nid;
		else
			dac = spec->slave_dig_outs[i - 1];
		if (!dac || !(get_wcaps(codec, dac) & AC_WCAP_OUT_AMP))
			continue;
		snd_hda_codec_write(codec, dac, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_OUT_UNMUTE);
	}
	pin = spec->autocfg.dig_in_pin;
	if (pin)
		snd_hda_codec_write(codec, pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    PIN_IN);
}

/* parse digital I/Os and set up NIDs in BIOS auto-parse mode */
static void alc_auto_parse_digital(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i, err;
	hda_nid_t dig_nid;

	/* support multiple SPDIFs; the secondary is set up as a slave */
	for (i = 0; i < spec->autocfg.dig_outs; i++) {
		hda_nid_t conn[4];
		err = snd_hda_get_connections(codec,
					      spec->autocfg.dig_out_pins[i],
					      conn, ARRAY_SIZE(conn));
		if (err < 0)
			continue;
		dig_nid = conn[0]; /* assume the first element is audio-out */
		if (!i) {
			spec->multiout.dig_out_nid = dig_nid;
			spec->dig_out_type = spec->autocfg.dig_out_type[0];
		} else {
			spec->multiout.slave_dig_outs = spec->slave_dig_outs;
			if (i >= ARRAY_SIZE(spec->slave_dig_outs) - 1)
				break;
			spec->slave_dig_outs[i - 1] = dig_nid;
		}
	}

	if (spec->autocfg.dig_in_pin) {
		dig_nid = codec->start_nid;
		for (i = 0; i < codec->num_nodes; i++, dig_nid++) {
			unsigned int wcaps = get_wcaps(codec, dig_nid);
			if (get_wcaps_type(wcaps) != AC_WID_AUD_IN)
				continue;
			if (!(wcaps & AC_WCAP_DIGITAL))
				continue;
			if (!(wcaps & AC_WCAP_CONN_LIST))
				continue;
			err = get_connection_index(codec, dig_nid,
						   spec->autocfg.dig_in_pin);
			if (err >= 0) {
				spec->dig_in_nid = dig_nid;
				break;
			}
		}
	}
}

/*
 * capture mixer elements
 */
static int alc_cap_vol_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned long val;
	int err;

	mutex_lock(&codec->control_mutex);
	if (spec->vol_in_capsrc)
		val = HDA_COMPOSE_AMP_VAL(spec->capsrc_nids[0], 3, 0, HDA_OUTPUT);
	else
		val = HDA_COMPOSE_AMP_VAL(spec->adc_nids[0], 3, 0, HDA_INPUT);
	kcontrol->private_value = val;
	err = snd_hda_mixer_amp_volume_info(kcontrol, uinfo);
	mutex_unlock(&codec->control_mutex);
	return err;
}

static int alc_cap_vol_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			   unsigned int size, unsigned int __user *tlv)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned long val;
	int err;

	mutex_lock(&codec->control_mutex);
	if (spec->vol_in_capsrc)
		val = HDA_COMPOSE_AMP_VAL(spec->capsrc_nids[0], 3, 0, HDA_OUTPUT);
	else
		val = HDA_COMPOSE_AMP_VAL(spec->adc_nids[0], 3, 0, HDA_INPUT);
	kcontrol->private_value = val;
	err = snd_hda_mixer_amp_tlv(kcontrol, op_flag, size, tlv);
	mutex_unlock(&codec->control_mutex);
	return err;
}

typedef int (*getput_call_t)(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol);

static int alc_cap_getput_caller(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol,
				 getput_call_t func, bool check_adc_switch)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int i, err = 0;

	mutex_lock(&codec->control_mutex);
	if (check_adc_switch && spec->dyn_adc_switch) {
		for (i = 0; i < spec->num_adc_nids; i++) {
			kcontrol->private_value =
				HDA_COMPOSE_AMP_VAL(spec->adc_nids[i],
						    3, 0, HDA_INPUT);
			err = func(kcontrol, ucontrol);
			if (err < 0)
				goto error;
		}
	} else {
		i = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
		if (spec->vol_in_capsrc)
			kcontrol->private_value =
				HDA_COMPOSE_AMP_VAL(spec->capsrc_nids[i],
						    3, 0, HDA_OUTPUT);
		else
			kcontrol->private_value =
				HDA_COMPOSE_AMP_VAL(spec->adc_nids[i],
						    3, 0, HDA_INPUT);
		err = func(kcontrol, ucontrol);
	}
 error:
	mutex_unlock(&codec->control_mutex);
	return err;
}

static int alc_cap_vol_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	return alc_cap_getput_caller(kcontrol, ucontrol,
				     snd_hda_mixer_amp_volume_get, false);
}

static int alc_cap_vol_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	return alc_cap_getput_caller(kcontrol, ucontrol,
				     snd_hda_mixer_amp_volume_put, true);
}

/* capture mixer elements */
#define alc_cap_sw_info		snd_ctl_boolean_stereo_info

static int alc_cap_sw_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	return alc_cap_getput_caller(kcontrol, ucontrol,
				     snd_hda_mixer_amp_switch_get, false);
}

static int alc_cap_sw_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	return alc_cap_getput_caller(kcontrol, ucontrol,
				     snd_hda_mixer_amp_switch_put, true);
}

#define _DEFINE_CAPMIX(num) \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name = "Capture Switch", \
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
		.count = num, \
		.info = alc_cap_sw_info, \
		.get = alc_cap_sw_get, \
		.put = alc_cap_sw_put, \
	}, \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name = "Capture Volume", \
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE | \
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
			   SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK), \
		.count = num, \
		.info = alc_cap_vol_info, \
		.get = alc_cap_vol_get, \
		.put = alc_cap_vol_put, \
		.tlv = { .c = alc_cap_vol_tlv }, \
	}

#define _DEFINE_CAPSRC(num) \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		/* .name = "Capture Source", */ \
		.name = "Input Source", \
		.count = num, \
		.info = alc_mux_enum_info, \
		.get = alc_mux_enum_get, \
		.put = alc_mux_enum_put, \
	}

#define DEFINE_CAPMIX(num) \
static const struct snd_kcontrol_new alc_capture_mixer ## num[] = { \
	_DEFINE_CAPMIX(num),				      \
	_DEFINE_CAPSRC(num),				      \
	{ } /* end */					      \
}

#define DEFINE_CAPMIX_NOSRC(num) \
static const struct snd_kcontrol_new alc_capture_mixer_nosrc ## num[] = { \
	_DEFINE_CAPMIX(num),					    \
	{ } /* end */						    \
}

/* up to three ADCs */
DEFINE_CAPMIX(1);
DEFINE_CAPMIX(2);
DEFINE_CAPMIX(3);
DEFINE_CAPMIX_NOSRC(1);
DEFINE_CAPMIX_NOSRC(2);
DEFINE_CAPMIX_NOSRC(3);

/*
 * virtual master controls
 */

/*
 * slave controls for virtual master
 */
static const char * const alc_slave_vols[] = {
	"Front Playback Volume",
	"Surround Playback Volume",
	"Center Playback Volume",
	"LFE Playback Volume",
	"Side Playback Volume",
	"Headphone Playback Volume",
	"Speaker Playback Volume",
	"Mono Playback Volume",
	"Line-Out Playback Volume",
	"PCM Playback Volume",
	NULL,
};

static const char * const alc_slave_sws[] = {
	"Front Playback Switch",
	"Surround Playback Switch",
	"Center Playback Switch",
	"LFE Playback Switch",
	"Side Playback Switch",
	"Headphone Playback Switch",
	"Speaker Playback Switch",
	"Mono Playback Switch",
	"IEC958 Playback Switch",
	"Line-Out Playback Switch",
	"PCM Playback Switch",
	NULL,
};

/*
 * build control elements
 */

#define NID_MAPPING		(-1)

#define SUBDEV_SPEAKER_		(0 << 6)
#define SUBDEV_HP_		(1 << 6)
#define SUBDEV_LINE_		(2 << 6)
#define SUBDEV_SPEAKER(x)	(SUBDEV_SPEAKER_ | ((x) & 0x3f))
#define SUBDEV_HP(x)		(SUBDEV_HP_ | ((x) & 0x3f))
#define SUBDEV_LINE(x)		(SUBDEV_LINE_ | ((x) & 0x3f))

static void alc_free_kctls(struct hda_codec *codec);

#ifdef CONFIG_SND_HDA_INPUT_BEEP
/* additional beep mixers; the actual parameters are overwritten at build */
static const struct snd_kcontrol_new alc_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0, 0, HDA_INPUT),
	HDA_CODEC_MUTE_BEEP("Beep Playback Switch", 0, 0, HDA_INPUT),
	{ } /* end */
};
#endif

static int alc_build_controls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct snd_kcontrol *kctl = NULL;
	const struct snd_kcontrol_new *knew;
	int i, j, err;
	unsigned int u;
	hda_nid_t nid;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}
	if (spec->cap_mixer) {
		err = snd_hda_add_new_ctls(codec, spec->cap_mixer);
		if (err < 0)
			return err;
	}
	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec,
						    spec->multiout.dig_out_nid,
						    spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
		if (!spec->no_analog) {
			err = snd_hda_create_spdif_share_sw(codec,
							    &spec->multiout);
			if (err < 0)
				return err;
			spec->multiout.share_spdif = 1;
		}
	}
	if (spec->dig_in_nid) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in_nid);
		if (err < 0)
			return err;
	}

#ifdef CONFIG_SND_HDA_INPUT_BEEP
	/* create beep controls if needed */
	if (spec->beep_amp) {
		const struct snd_kcontrol_new *knew;
		for (knew = alc_beep_mixer; knew->name; knew++) {
			struct snd_kcontrol *kctl;
			kctl = snd_ctl_new1(knew, codec);
			if (!kctl)
				return -ENOMEM;
			kctl->private_value = spec->beep_amp;
			err = snd_hda_ctl_add(codec, 0, kctl);
			if (err < 0)
				return err;
		}
	}
#endif

	/* if we have no master control, let's create it */
	if (!spec->no_analog &&
	    !snd_hda_find_mixer_ctl(codec, "Master Playback Volume")) {
		unsigned int vmaster_tlv[4];
		snd_hda_set_vmaster_tlv(codec, spec->vmaster_nid,
					HDA_OUTPUT, vmaster_tlv);
		err = snd_hda_add_vmaster(codec, "Master Playback Volume",
					  vmaster_tlv, alc_slave_vols);
		if (err < 0)
			return err;
	}
	if (!spec->no_analog &&
	    !snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = snd_hda_add_vmaster(codec, "Master Playback Switch",
					  NULL, alc_slave_sws);
		if (err < 0)
			return err;
	}

	/* assign Capture Source enums to NID */
	if (spec->capsrc_nids || spec->adc_nids) {
		kctl = snd_hda_find_mixer_ctl(codec, "Capture Source");
		if (!kctl)
			kctl = snd_hda_find_mixer_ctl(codec, "Input Source");
		for (i = 0; kctl && i < kctl->count; i++) {
			const hda_nid_t *nids = spec->capsrc_nids;
			if (!nids)
				nids = spec->adc_nids;
			err = snd_hda_add_nid(codec, kctl, i, nids[i]);
			if (err < 0)
				return err;
		}
	}
	if (spec->cap_mixer && spec->adc_nids) {
		const char *kname = kctl ? kctl->id.name : NULL;
		for (knew = spec->cap_mixer; knew->name; knew++) {
			if (kname && strcmp(knew->name, kname) == 0)
				continue;
			kctl = snd_hda_find_mixer_ctl(codec, knew->name);
			for (i = 0; kctl && i < kctl->count; i++) {
				err = snd_hda_add_nid(codec, kctl, i,
						      spec->adc_nids[i]);
				if (err < 0)
					return err;
			}
		}
	}

	/* other nid->control mapping */
	for (i = 0; i < spec->num_mixers; i++) {
		for (knew = spec->mixers[i]; knew->name; knew++) {
			if (knew->iface != NID_MAPPING)
				continue;
			kctl = snd_hda_find_mixer_ctl(codec, knew->name);
			if (kctl == NULL)
				continue;
			u = knew->subdevice;
			for (j = 0; j < 4; j++, u >>= 8) {
				nid = u & 0x3f;
				if (nid == 0)
					continue;
				switch (u & 0xc0) {
				case SUBDEV_SPEAKER_:
					nid = spec->autocfg.speaker_pins[nid];
					break;
				case SUBDEV_LINE_:
					nid = spec->autocfg.line_out_pins[nid];
					break;
				case SUBDEV_HP_:
					nid = spec->autocfg.hp_pins[nid];
					break;
				default:
					continue;
				}
				err = snd_hda_add_nid(codec, kctl, 0, nid);
				if (err < 0)
					return err;
			}
			u = knew->private_value;
			for (j = 0; j < 4; j++, u >>= 8) {
				nid = u & 0xff;
				if (nid == 0)
					continue;
				err = snd_hda_add_nid(codec, kctl, 0, nid);
				if (err < 0)
					return err;
			}
		}
	}

	alc_free_kctls(codec); /* no longer needed */

	return 0;
}


/*
 * Common callbacks
 */

static void alc_init_special_input_src(struct hda_codec *codec);

static int alc_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int i;

	alc_fix_pll(codec);
	alc_auto_init_amp(codec, spec->init_amp);

	for (i = 0; i < spec->num_init_verbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);
	alc_init_special_input_src(codec);

	if (spec->init_hook)
		spec->init_hook(codec);

	alc_apply_fixup(codec, ALC_FIXUP_ACT_INIT);

	hda_call_check_power_status(codec, 0x01);
	return 0;
}

static void alc_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct alc_spec *spec = codec->spec;

	if (spec->unsol_event)
		spec->unsol_event(codec, res);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static int alc_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
}
#endif

/*
 * Analog playback callbacks
 */
static int alc_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
}

static int alc_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag, format, substream);
}

static int alc_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int alc_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int alc_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

static int alc_dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_cleanup(codec, &spec->multiout);
}

static int alc_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int alc_alt_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number + 1],
				   stream_tag, 0, format);
	return 0;
}

static int alc_alt_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_cleanup_stream(codec,
				     spec->adc_nids[substream->number + 1]);
	return 0;
}

/* analog capture with dynamic dual-adc changes */
static int dyn_adc_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	spec->cur_adc = spec->adc_nids[spec->dyn_adc_idx[spec->cur_mux[0]]];
	spec->cur_adc_stream_tag = stream_tag;
	spec->cur_adc_format = format;
	snd_hda_codec_setup_stream(codec, spec->cur_adc, stream_tag, 0, format);
	return 0;
}

static int dyn_adc_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	snd_hda_codec_cleanup_stream(codec, spec->cur_adc);
	spec->cur_adc = 0;
	return 0;
}

static const struct hda_pcm_stream dyn_adc_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.prepare = dyn_adc_capture_pcm_prepare,
		.cleanup = dyn_adc_capture_pcm_cleanup
	},
};

/*
 */
static const struct hda_pcm_stream alc_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc_playback_pcm_open,
		.prepare = alc_playback_pcm_prepare,
		.cleanup = alc_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream alc_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static const struct hda_pcm_stream alc_pcm_analog_alt_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static const struct hda_pcm_stream alc_pcm_analog_alt_capture = {
	.substreams = 2, /* can be overridden */
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.prepare = alc_alt_capture_pcm_prepare,
		.cleanup = alc_alt_capture_pcm_cleanup
	},
};

static const struct hda_pcm_stream alc_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc_dig_playback_pcm_open,
		.close = alc_dig_playback_pcm_close,
		.prepare = alc_dig_playback_pcm_prepare,
		.cleanup = alc_dig_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream alc_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

/* Used by alc_build_pcms to flag that a PCM has no playback stream */
static const struct hda_pcm_stream alc_pcm_null_stream = {
	.substreams = 0,
	.channels_min = 0,
	.channels_max = 0,
};

static int alc_build_pcms(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;
	const struct hda_pcm_stream *p;
	int i;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	if (spec->no_analog)
		goto skip_analog;

	snprintf(spec->stream_name_analog, sizeof(spec->stream_name_analog),
		 "%s Analog", codec->chip_name);
	info->name = spec->stream_name_analog;

	if (spec->multiout.dac_nids > 0) {
		p = spec->stream_analog_playback;
		if (!p)
			p = &alc_pcm_analog_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *p;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dac_nids[0];
	}
	if (spec->adc_nids) {
		p = spec->stream_analog_capture;
		if (!p) {
			if (spec->dyn_adc_switch)
				p = &dyn_adc_pcm_analog_capture;
			else
				p = &alc_pcm_analog_capture;
		}
		info->stream[SNDRV_PCM_STREAM_CAPTURE] = *p;
		info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];
	}

	if (spec->channel_mode) {
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = 0;
		for (i = 0; i < spec->num_channel_mode; i++) {
			if (spec->channel_mode[i].channels > info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max) {
				info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = spec->channel_mode[i].channels;
			}
		}
	}

 skip_analog:
	/* SPDIF for stream index #1 */
	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		snprintf(spec->stream_name_digital,
			 sizeof(spec->stream_name_digital),
			 "%s Digital", codec->chip_name);
		codec->num_pcms = 2;
	        codec->slave_dig_outs = spec->multiout.slave_dig_outs;
		info = spec->pcm_rec + 1;
		info->name = spec->stream_name_digital;
		if (spec->dig_out_type)
			info->pcm_type = spec->dig_out_type;
		else
			info->pcm_type = HDA_PCM_TYPE_SPDIF;
		if (spec->multiout.dig_out_nid) {
			p = spec->stream_digital_playback;
			if (!p)
				p = &alc_pcm_digital_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *p;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid) {
			p = spec->stream_digital_capture;
			if (!p)
				p = &alc_pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = *p;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
		/* FIXME: do we need this for all Realtek codec models? */
		codec->spdif_status_reset = 1;
	}

	if (spec->no_analog)
		return 0;

	/* If the use of more than one ADC is requested for the current
	 * model, configure a second analog capture-only PCM.
	 */
	/* Additional Analaog capture for index #2 */
	if (spec->alt_dac_nid || spec->num_adc_nids > 1) {
		codec->num_pcms = 3;
		info = spec->pcm_rec + 2;
		info->name = spec->stream_name_analog;
		if (spec->alt_dac_nid) {
			p = spec->stream_analog_alt_playback;
			if (!p)
				p = &alc_pcm_analog_alt_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *p;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
				spec->alt_dac_nid;
		} else {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				alc_pcm_null_stream;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = 0;
		}
		if (spec->num_adc_nids > 1) {
			p = spec->stream_analog_alt_capture;
			if (!p)
				p = &alc_pcm_analog_alt_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = *p;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->adc_nids[1];
			info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams =
				spec->num_adc_nids - 1;
		} else {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				alc_pcm_null_stream;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = 0;
		}
	}

	return 0;
}

static inline void alc_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec && spec->shutup)
		spec->shutup(codec);
	snd_hda_shutup_pins(codec);
}

static void alc_free_kctls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->kctls.list) {
		struct snd_kcontrol_new *kctl = spec->kctls.list;
		int i;
		for (i = 0; i < spec->kctls.used; i++)
			kfree(kctl[i].name);
	}
	snd_array_free(&spec->kctls);
}

static void alc_free(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec)
		return;

	alc_shutup(codec);
	snd_hda_input_jack_free(codec);
	alc_free_kctls(codec);
	kfree(spec);
	snd_hda_detach_beep_device(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static void alc_power_eapd(struct hda_codec *codec)
{
	alc_auto_setup_eapd(codec, false);
}

static int alc_suspend(struct hda_codec *codec, pm_message_t state)
{
	struct alc_spec *spec = codec->spec;
	alc_shutup(codec);
	if (spec && spec->power_hook)
		spec->power_hook(codec);
	return 0;
}
#endif

#ifdef CONFIG_PM
static int alc_resume(struct hda_codec *codec)
{
	msleep(150); /* to avoid pop noise */
	codec->patch_ops.init(codec);
	snd_hda_codec_resume_amp(codec);
	snd_hda_codec_resume_cache(codec);
	hda_call_check_power_status(codec, 0x01);
	return 0;
}
#endif

/*
 */
static const struct hda_codec_ops alc_patch_ops = {
	.build_controls = alc_build_controls,
	.build_pcms = alc_build_pcms,
	.init = alc_init,
	.free = alc_free,
	.unsol_event = alc_unsol_event,
#ifdef CONFIG_PM
	.resume = alc_resume,
#endif
#ifdef CONFIG_SND_HDA_POWER_SAVE
	.suspend = alc_suspend,
	.check_power_status = alc_check_power_status,
#endif
	.reboot_notify = alc_shutup,
};

/* replace the codec chip_name with the given string */
static int alc_codec_rename(struct hda_codec *codec, const char *name)
{
	kfree(codec->chip_name);
	codec->chip_name = kstrdup(name, GFP_KERNEL);
	if (!codec->chip_name) {
		alc_free(codec);
		return -ENOMEM;
	}
	return 0;
}

/*
 * Automatic parse of I/O pins from the BIOS configuration
 */

enum {
	ALC_CTL_WIDGET_VOL,
	ALC_CTL_WIDGET_MUTE,
	ALC_CTL_BIND_MUTE,
};
static const struct snd_kcontrol_new alc_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	HDA_BIND_MUTE(NULL, 0, 0, 0),
};

/* add dynamic controls */
static int add_control(struct alc_spec *spec, int type, const char *name,
		       int cidx, unsigned long val)
{
	struct snd_kcontrol_new *knew;

	knew = alc_kcontrol_new(spec);
	if (!knew)
		return -ENOMEM;
	*knew = alc_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);
	if (!knew->name)
		return -ENOMEM;
	knew->index = cidx;
	if (get_amp_nid_(val))
		knew->subdevice = HDA_SUBDEV_AMP_FLAG;
	knew->private_value = val;
	return 0;
}

static int add_control_with_pfx(struct alc_spec *spec, int type,
				const char *pfx, const char *dir,
				const char *sfx, int cidx, unsigned long val)
{
	char name[32];
	snprintf(name, sizeof(name), "%s %s %s", pfx, dir, sfx);
	return add_control(spec, type, name, cidx, val);
}

#define add_pb_vol_ctrl(spec, type, pfx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Volume", 0, val)
#define add_pb_sw_ctrl(spec, type, pfx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Switch", 0, val)
#define __add_pb_vol_ctrl(spec, type, pfx, cidx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Volume", cidx, val)
#define __add_pb_sw_ctrl(spec, type, pfx, cidx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Switch", cidx, val)

static const char *alc_get_line_out_pfx(struct alc_spec *spec, int ch,
					bool can_be_master, int *index)
{
	struct auto_pin_cfg *cfg = &spec->autocfg;
	static const char * const chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};

	*index = 0;
	if (cfg->line_outs == 1 && !spec->multi_ios &&
	    !cfg->hp_outs && !cfg->speaker_outs && can_be_master)
		return "Master";

	switch (cfg->line_out_type) {
	case AUTO_PIN_SPEAKER_OUT:
		if (cfg->line_outs == 1)
			return "Speaker";
		break;
	case AUTO_PIN_HP_OUT:
		/* for multi-io case, only the primary out */
		if (ch && spec->multi_ios)
			break;
		*index = ch;
		return "Headphone";
	default:
		if (cfg->line_outs == 1 && !spec->multi_ios)
			return "PCM";
		break;
	}
	return chname[ch];
}

/* create input playback/capture controls for the given pin */
static int new_analog_input(struct alc_spec *spec, hda_nid_t pin,
			    const char *ctlname, int ctlidx,
			    int idx, hda_nid_t mix_nid)
{
	int err;

	err = __add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, ctlname, ctlidx,
			  HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	err = __add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, ctlname, ctlidx,
			  HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	return 0;
}

static int alc_is_input_pin(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int pincap = snd_hda_query_pin_caps(codec, nid);
	return (pincap & AC_PINCAP_IN) != 0;
}

/* Parse the codec tree and retrieve ADCs and corresponding capsrc MUXs */
static int alc_auto_fill_adc_caps(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid;
	hda_nid_t *adc_nids = spec->private_adc_nids;
	hda_nid_t *cap_nids = spec->private_capsrc_nids;
	int max_nums = ARRAY_SIZE(spec->private_adc_nids);
	bool indep_capsrc = false;
	int i, nums = 0;

	nid = codec->start_nid;
	for (i = 0; i < codec->num_nodes; i++, nid++) {
		hda_nid_t src;
		const hda_nid_t *list;
		unsigned int caps = get_wcaps(codec, nid);
		int type = get_wcaps_type(caps);

		if (type != AC_WID_AUD_IN || (caps & AC_WCAP_DIGITAL))
			continue;
		adc_nids[nums] = nid;
		cap_nids[nums] = nid;
		src = nid;
		for (;;) {
			int n;
			type = get_wcaps_type(get_wcaps(codec, src));
			if (type == AC_WID_PIN)
				break;
			if (type == AC_WID_AUD_SEL) {
				cap_nids[nums] = src;
				indep_capsrc = true;
				break;
			}
			n = snd_hda_get_conn_list(codec, src, &list);
			if (n > 1) {
				cap_nids[nums] = src;
				indep_capsrc = true;
				break;
			} else if (n != 1)
				break;
			src = *list;
		}
		if (++nums >= max_nums)
			break;
	}
	spec->adc_nids = spec->private_adc_nids;
	spec->capsrc_nids = spec->private_capsrc_nids;
	spec->num_adc_nids = nums;
	return nums;
}

/* create playback/capture controls for input pins */
static int alc_auto_create_input_ctls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	struct hda_input_mux *imux = &spec->private_imux[0];
	int num_adcs;
	int i, c, err, idx, type_idx = 0;
	const char *prev_label = NULL;

	num_adcs = alc_auto_fill_adc_caps(codec);
	if (num_adcs < 0)
		return 0;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin;
		const char *label;

		pin = cfg->inputs[i].pin;
		if (!alc_is_input_pin(codec, pin))
			continue;

		label = hda_get_autocfg_input_label(codec, cfg, i);
		if (prev_label && !strcmp(label, prev_label))
			type_idx++;
		else
			type_idx = 0;
		prev_label = label;

		if (mixer) {
			idx = get_connection_index(codec, mixer, pin);
			if (idx >= 0) {
				err = new_analog_input(spec, pin,
						       label, type_idx,
						       idx, mixer);
				if (err < 0)
					return err;
			}
		}

		for (c = 0; c < num_adcs; c++) {
			hda_nid_t cap = spec->capsrc_nids ?
				spec->capsrc_nids[c] : spec->adc_nids[c];
			idx = get_connection_index(codec, cap, pin);
			if (idx >= 0) {
				spec->imux_pins[imux->num_items] = pin;
				snd_hda_add_imux_item(imux, label, idx, NULL);
				break;
			}
		}
	}

	spec->num_mux_defs = 1;
	spec->input_mux = imux;

	return 0;
}

static void alc_set_pin_output(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int pin_type)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pin_type);
	/* unmute pin */
	if (nid_has_mute(codec, nid, HDA_OUTPUT))
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_OUT_UNMUTE);
}

static int get_pin_type(int line_out_type)
{
	if (line_out_type == AUTO_PIN_HP_OUT)
		return PIN_HP;
	else
		return PIN_OUT;
}

static void alc_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		if (alc_is_input_pin(codec, nid)) {
			alc_set_input_pin(codec, nid, cfg->inputs[i].type);
			if (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}

	/* mute all loopback inputs */
	if (spec->mixer_nid) {
		int nums = snd_hda_get_conn_list(codec, spec->mixer_nid, NULL);
		for (i = 0; i < nums; i++)
			snd_hda_codec_write(codec, spec->mixer_nid, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_IN_MUTE(i));
	}
}

/* convert from MIX nid to DAC */
static hda_nid_t alc_auto_mix_to_dac(struct hda_codec *codec, hda_nid_t nid)
{
	hda_nid_t list[5];
	int i, num;

	if (get_wcaps_type(get_wcaps(codec, nid)) == AC_WID_AUD_OUT)
		return nid;
	num = snd_hda_get_connections(codec, nid, list, ARRAY_SIZE(list));
	for (i = 0; i < num; i++) {
		if (get_wcaps_type(get_wcaps(codec, list[i])) == AC_WID_AUD_OUT)
			return list[i];
	}
	return 0;
}

/* go down to the selector widget before the mixer */
static hda_nid_t alc_go_down_to_selector(struct hda_codec *codec, hda_nid_t pin)
{
	hda_nid_t srcs[5];
	int num = snd_hda_get_connections(codec, pin, srcs,
					  ARRAY_SIZE(srcs));
	if (num != 1 ||
	    get_wcaps_type(get_wcaps(codec, srcs[0])) != AC_WID_AUD_SEL)
		return pin;
	return srcs[0];
}

/* get MIX nid connected to the given pin targeted to DAC */
static hda_nid_t alc_auto_dac_to_mix(struct hda_codec *codec, hda_nid_t pin,
				   hda_nid_t dac)
{
	hda_nid_t mix[5];
	int i, num;

	pin = alc_go_down_to_selector(codec, pin);
	num = snd_hda_get_connections(codec, pin, mix, ARRAY_SIZE(mix));
	for (i = 0; i < num; i++) {
		if (alc_auto_mix_to_dac(codec, mix[i]) == dac)
			return mix[i];
	}
	return 0;
}

/* select the connection from pin to DAC if needed */
static int alc_auto_select_dac(struct hda_codec *codec, hda_nid_t pin,
			       hda_nid_t dac)
{
	hda_nid_t mix[5];
	int i, num;

	pin = alc_go_down_to_selector(codec, pin);
	num = snd_hda_get_connections(codec, pin, mix, ARRAY_SIZE(mix));
	if (num < 2)
		return 0;
	for (i = 0; i < num; i++) {
		if (alc_auto_mix_to_dac(codec, mix[i]) == dac) {
			snd_hda_codec_update_cache(codec, pin, 0,
						   AC_VERB_SET_CONNECT_SEL, i);
			return 0;
		}
	}
	return 0;
}

/* look for an empty DAC slot */
static hda_nid_t alc_auto_look_for_dac(struct hda_codec *codec, hda_nid_t pin)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t srcs[5];
	int i, num;

	pin = alc_go_down_to_selector(codec, pin);
	num = snd_hda_get_connections(codec, pin, srcs, ARRAY_SIZE(srcs));
	for (i = 0; i < num; i++) {
		hda_nid_t nid = alc_auto_mix_to_dac(codec, srcs[i]);
		if (!nid)
			continue;
		if (found_in_nid_list(nid, spec->multiout.dac_nids,
				      spec->multiout.num_dacs))
			continue;
		if (spec->multiout.hp_nid == nid)
			continue;
		if (found_in_nid_list(nid, spec->multiout.extra_out_nid,
				      ARRAY_SIZE(spec->multiout.extra_out_nid)))
		    continue;
		return nid;
	}
	return 0;
}

static hda_nid_t get_dac_if_single(struct hda_codec *codec, hda_nid_t pin)
{
	hda_nid_t sel = alc_go_down_to_selector(codec, pin);
	if (snd_hda_get_conn_list(codec, sel, NULL) == 1)
		return alc_auto_look_for_dac(codec, pin);
	return 0;
}

/* fill in the dac_nids table from the parsed pin configuration */
static int alc_auto_fill_dac_nids(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	bool redone = false;
	int i;

 again:
	/* set num_dacs once to full for alc_auto_look_for_dac() */
	spec->multiout.num_dacs = cfg->line_outs;
	spec->multiout.hp_nid = 0;
	spec->multiout.extra_out_nid[0] = 0;
	memset(spec->private_dac_nids, 0, sizeof(spec->private_dac_nids));
	spec->multiout.dac_nids = spec->private_dac_nids;

	/* fill hard-wired DACs first */
	if (!redone) {
		for (i = 0; i < cfg->line_outs; i++)
			spec->private_dac_nids[i] =
				get_dac_if_single(codec, cfg->line_out_pins[i]);
		if (cfg->hp_outs)
			spec->multiout.hp_nid =
				get_dac_if_single(codec, cfg->hp_pins[0]);
		if (cfg->speaker_outs)
			spec->multiout.extra_out_nid[0] =
				get_dac_if_single(codec, cfg->speaker_pins[0]);
	}

	for (i = 0; i < cfg->line_outs; i++) {
		hda_nid_t pin = cfg->line_out_pins[i];
		if (spec->private_dac_nids[i])
			continue;
		spec->private_dac_nids[i] = alc_auto_look_for_dac(codec, pin);
		if (!spec->private_dac_nids[i] && !redone) {
			/* if we can't find primary DACs, re-probe without
			 * checking the hard-wired DACs
			 */
			redone = true;
			goto again;
		}
	}

	/* re-count num_dacs and squash invalid entries */
	spec->multiout.num_dacs = 0;
	for (i = 0; i < cfg->line_outs; i++) {
		if (spec->private_dac_nids[i])
			spec->multiout.num_dacs++;
		else
			memmove(spec->private_dac_nids + i,
				spec->private_dac_nids + i + 1,
				sizeof(hda_nid_t) * (cfg->line_outs - i - 1));
	}

	if (cfg->hp_outs && !spec->multiout.hp_nid)
		spec->multiout.hp_nid =
			alc_auto_look_for_dac(codec, cfg->hp_pins[0]);
	if (cfg->speaker_outs && !spec->multiout.extra_out_nid[0])
		spec->multiout.extra_out_nid[0] =
			alc_auto_look_for_dac(codec, cfg->speaker_pins[0]);

	return 0;
}

static int alc_auto_add_vol_ctl(struct hda_codec *codec,
			      const char *pfx, int cidx,
			      hda_nid_t nid, unsigned int chs)
{
	if (!nid)
		return 0;
	return __add_pb_vol_ctrl(codec->spec, ALC_CTL_WIDGET_VOL, pfx, cidx,
				 HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT));
}

#define alc_auto_add_stereo_vol(codec, pfx, cidx, nid)	\
	alc_auto_add_vol_ctl(codec, pfx, cidx, nid, 3)

/* create a mute-switch for the given mixer widget;
 * if it has multiple sources (e.g. DAC and loopback), create a bind-mute
 */
static int alc_auto_add_sw_ctl(struct hda_codec *codec,
			     const char *pfx, int cidx,
			     hda_nid_t nid, unsigned int chs)
{
	int wid_type;
	int type;
	unsigned long val;
	if (!nid)
		return 0;
	wid_type = get_wcaps_type(get_wcaps(codec, nid));
	if (wid_type == AC_WID_PIN || wid_type == AC_WID_AUD_OUT) {
		type = ALC_CTL_WIDGET_MUTE;
		val = HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT);
	} else if (snd_hda_get_conn_list(codec, nid, NULL) == 1) {
		type = ALC_CTL_WIDGET_MUTE;
		val = HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_INPUT);
	} else {
		type = ALC_CTL_BIND_MUTE;
		val = HDA_COMPOSE_AMP_VAL(nid, chs, 2, HDA_INPUT);
	}
	return __add_pb_sw_ctrl(codec->spec, type, pfx, cidx, val);
}

#define alc_auto_add_stereo_sw(codec, pfx, cidx, nid)	\
	alc_auto_add_sw_ctl(codec, pfx, cidx, nid, 3)

static hda_nid_t alc_look_for_out_mute_nid(struct hda_codec *codec,
					   hda_nid_t pin, hda_nid_t dac)
{
	hda_nid_t mix = alc_auto_dac_to_mix(codec, pin, dac);
	if (nid_has_mute(codec, pin, HDA_OUTPUT))
		return pin;
	else if (mix && nid_has_mute(codec, mix, HDA_INPUT))
		return mix;
	else if (nid_has_mute(codec, dac, HDA_OUTPUT))
		return dac;
	return 0;
}

static hda_nid_t alc_look_for_out_vol_nid(struct hda_codec *codec,
					  hda_nid_t pin, hda_nid_t dac)
{
	hda_nid_t mix = alc_auto_dac_to_mix(codec, pin, dac);
	if (nid_has_volume(codec, dac, HDA_OUTPUT))
		return dac;
	else if (nid_has_volume(codec, mix, HDA_OUTPUT))
		return mix;
	else if (nid_has_volume(codec, pin, HDA_OUTPUT))
		return pin;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int alc_auto_create_multi_out_ctls(struct hda_codec *codec,
					     const struct auto_pin_cfg *cfg)
{
	struct alc_spec *spec = codec->spec;
	int i, err, noutputs;

	noutputs = cfg->line_outs;
	if (spec->multi_ios > 0)
		noutputs += spec->multi_ios;

	for (i = 0; i < noutputs; i++) {
		const char *name;
		int index;
		hda_nid_t dac, pin;
		hda_nid_t sw, vol;

		dac = spec->multiout.dac_nids[i];
		if (!dac)
			continue;
		if (i >= cfg->line_outs)
			pin = spec->multi_io[i - 1].pin;
		else
			pin = cfg->line_out_pins[i];

		sw = alc_look_for_out_mute_nid(codec, pin, dac);
		vol = alc_look_for_out_vol_nid(codec, pin, dac);
		name = alc_get_line_out_pfx(spec, i, true, &index);
		if (!name) {
			/* Center/LFE */
			err = alc_auto_add_vol_ctl(codec, "Center", 0, vol, 1);
			if (err < 0)
				return err;
			err = alc_auto_add_vol_ctl(codec, "LFE", 0, vol, 2);
			if (err < 0)
				return err;
			err = alc_auto_add_sw_ctl(codec, "Center", 0, sw, 1);
			if (err < 0)
				return err;
			err = alc_auto_add_sw_ctl(codec, "LFE", 0, sw, 2);
			if (err < 0)
				return err;
		} else {
			err = alc_auto_add_stereo_vol(codec, name, index, vol);
			if (err < 0)
				return err;
			err = alc_auto_add_stereo_sw(codec, name, index, sw);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* add playback controls for speaker and HP outputs */
static int alc_auto_create_extra_out(struct hda_codec *codec, hda_nid_t pin,
					hda_nid_t dac, const char *pfx)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t sw, vol;
	int err;

	if (!pin)
		return 0;
	if (!dac) {
		/* the corresponding DAC is already occupied */
		if (!(get_wcaps(codec, pin) & AC_WCAP_OUT_AMP))
			return 0; /* no way */
		/* create a switch only */
		return add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, pfx,
				   HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	}

	sw = alc_look_for_out_mute_nid(codec, pin, dac);
	vol = alc_look_for_out_vol_nid(codec, pin, dac);
	err = alc_auto_add_stereo_vol(codec, pfx, 0, vol);
	if (err < 0)
		return err;
	err = alc_auto_add_stereo_sw(codec, pfx, 0, sw);
	if (err < 0)
		return err;
	return 0;
}

static int alc_auto_create_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	return alc_auto_create_extra_out(codec, spec->autocfg.hp_pins[0],
					 spec->multiout.hp_nid,
					 "Headphone");
}

static int alc_auto_create_speaker_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	return alc_auto_create_extra_out(codec, spec->autocfg.speaker_pins[0],
					 spec->multiout.extra_out_nid[0],
					 "Speaker");
}

static void alc_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t pin, int pin_type,
					      hda_nid_t dac)
{
	int i, num;
	hda_nid_t nid, mix = 0;
	hda_nid_t srcs[HDA_MAX_CONNECTIONS];

	alc_set_pin_output(codec, pin, pin_type);
	nid = alc_go_down_to_selector(codec, pin);
	num = snd_hda_get_connections(codec, nid, srcs, ARRAY_SIZE(srcs));
	for (i = 0; i < num; i++) {
		if (alc_auto_mix_to_dac(codec, srcs[i]) != dac)
			continue;
		mix = srcs[i];
		break;
	}
	if (!mix)
		return;

	/* need the manual connection? */
	if (num > 1)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CONNECT_SEL, i);
	/* unmute mixer widget inputs */
	if (nid_has_mute(codec, mix, HDA_INPUT)) {
		snd_hda_codec_write(codec, mix, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_IN_UNMUTE(0));
		snd_hda_codec_write(codec, mix, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_IN_UNMUTE(1));
	}
	/* initialize volume */
	nid = alc_look_for_out_vol_nid(codec, pin, dac);
	if (nid)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_OUT_ZERO);
}

static void alc_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int pin_type = get_pin_type(spec->autocfg.line_out_type);
	int i;

	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		if (nid)
			alc_auto_set_output_and_unmute(codec, nid, pin_type,
					spec->multiout.dac_nids[i]);
	}
}

static void alc_auto_init_extra_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin)
		alc_auto_set_output_and_unmute(codec, pin, PIN_HP,
						  spec->multiout.hp_nid);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc_auto_set_output_and_unmute(codec, pin, PIN_OUT,
					spec->multiout.extra_out_nid[0]);
}

/*
 * multi-io helper
 */
static int alc_auto_fill_multi_ios(struct hda_codec *codec,
				   unsigned int location)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int type, i, num_pins = 0;

	for (type = AUTO_PIN_LINE_IN; type >= AUTO_PIN_MIC; type--) {
		for (i = 0; i < cfg->num_inputs; i++) {
			hda_nid_t nid = cfg->inputs[i].pin;
			hda_nid_t dac;
			unsigned int defcfg, caps;
			if (cfg->inputs[i].type != type)
				continue;
			defcfg = snd_hda_codec_get_pincfg(codec, nid);
			if (get_defcfg_connect(defcfg) != AC_JACK_PORT_COMPLEX)
				continue;
			if (location && get_defcfg_location(defcfg) != location)
				continue;
			caps = snd_hda_query_pin_caps(codec, nid);
			if (!(caps & AC_PINCAP_OUT))
				continue;
			dac = alc_auto_look_for_dac(codec, nid);
			if (!dac)
				continue;
			spec->multi_io[num_pins].pin = nid;
			spec->multi_io[num_pins].dac = dac;
			num_pins++;
			spec->private_dac_nids[spec->multiout.num_dacs++] = dac;
		}
	}
	spec->multiout.num_dacs = 1;
	if (num_pins < 2)
		return 0;
	return num_pins;
}

static int alc_auto_ch_mode_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->multi_ios + 1;
	if (uinfo->value.enumerated.item > spec->multi_ios)
		uinfo->value.enumerated.item = spec->multi_ios;
	sprintf(uinfo->value.enumerated.name, "%dch",
		(uinfo->value.enumerated.item + 1) * 2);
	return 0;
}

static int alc_auto_ch_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = (spec->ext_channel_count - 1) / 2;
	return 0;
}

static int alc_set_multi_io(struct hda_codec *codec, int idx, bool output)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid = spec->multi_io[idx].pin;

	if (!spec->multi_io[idx].ctl_in)
		spec->multi_io[idx].ctl_in =
			snd_hda_codec_read(codec, nid, 0,
					   AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	if (output) {
		snd_hda_codec_update_cache(codec, nid, 0,
					   AC_VERB_SET_PIN_WIDGET_CONTROL,
					   PIN_OUT);
		if (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP)
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, 0);
		alc_auto_select_dac(codec, nid, spec->multi_io[idx].dac);
	} else {
		if (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP)
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, HDA_AMP_MUTE);
		snd_hda_codec_update_cache(codec, nid, 0,
					   AC_VERB_SET_PIN_WIDGET_CONTROL,
					   spec->multi_io[idx].ctl_in);
	}
	return 0;
}

static int alc_auto_ch_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int i, ch;

	ch = ucontrol->value.enumerated.item[0];
	if (ch < 0 || ch > spec->multi_ios)
		return -EINVAL;
	if (ch == (spec->ext_channel_count - 1) / 2)
		return 0;
	spec->ext_channel_count = (ch + 1) * 2;
	for (i = 0; i < spec->multi_ios; i++)
		alc_set_multi_io(codec, i, i < ch);
	spec->multiout.max_channels = spec->ext_channel_count;
	if (spec->need_dac_fix && !spec->const_channel_count)
		spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	return 1;
}

static const struct snd_kcontrol_new alc_auto_channel_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Channel Mode",
	.info = alc_auto_ch_mode_info,
	.get = alc_auto_ch_mode_get,
	.put = alc_auto_ch_mode_put,
};

static int alc_auto_add_multi_channel_mode(struct hda_codec *codec,
					   int (*fill_dac)(struct hda_codec *))
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int location, defcfg;
	int num_pins;

	if (cfg->line_out_type == AUTO_PIN_SPEAKER_OUT && cfg->hp_outs == 1) {
		/* use HP as primary out */
		cfg->speaker_outs = cfg->line_outs;
		memcpy(cfg->speaker_pins, cfg->line_out_pins,
		       sizeof(cfg->speaker_pins));
		cfg->line_outs = cfg->hp_outs;
		memcpy(cfg->line_out_pins, cfg->hp_pins, sizeof(cfg->hp_pins));
		cfg->hp_outs = 0;
		memset(cfg->hp_pins, 0, sizeof(cfg->hp_pins));
		cfg->line_out_type = AUTO_PIN_HP_OUT;
		if (fill_dac)
			fill_dac(codec);
	}
	if (cfg->line_outs != 1 ||
	    cfg->line_out_type == AUTO_PIN_SPEAKER_OUT)
		return 0;

	defcfg = snd_hda_codec_get_pincfg(codec, cfg->line_out_pins[0]);
	location = get_defcfg_location(defcfg);

	num_pins = alc_auto_fill_multi_ios(codec, location);
	if (num_pins > 0) {
		struct snd_kcontrol_new *knew;

		knew = alc_kcontrol_new(spec);
		if (!knew)
			return -ENOMEM;
		*knew = alc_auto_channel_mode_enum;
		knew->name = kstrdup("Channel Mode", GFP_KERNEL);
		if (!knew->name)
			return -ENOMEM;

		spec->multi_ios = num_pins;
		spec->ext_channel_count = 2;
		spec->multiout.num_dacs = num_pins + 1;
	}
	return 0;
}

/* filter out invalid adc_nids (and capsrc_nids) that don't give all
 * active input pins
 */
static void alc_remove_invalid_adc_nids(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct hda_input_mux *imux;
	hda_nid_t adc_nids[ARRAY_SIZE(spec->private_adc_nids)];
	hda_nid_t capsrc_nids[ARRAY_SIZE(spec->private_adc_nids)];
	int i, n, nums;

	imux = spec->input_mux;
	if (!imux)
		return;
	if (spec->dyn_adc_switch)
		return;

	nums = 0;
	for (n = 0; n < spec->num_adc_nids; n++) {
		hda_nid_t cap = spec->private_capsrc_nids[n];
		int num_conns = snd_hda_get_conn_list(codec, cap, NULL);
		for (i = 0; i < imux->num_items; i++) {
			hda_nid_t pin = spec->imux_pins[i];
			if (pin) {
				if (get_connection_index(codec, cap, pin) < 0)
					break;
			} else if (num_conns <= imux->items[i].index)
				break;
		}
		if (i >= imux->num_items) {
			adc_nids[nums] = spec->private_adc_nids[n];
			capsrc_nids[nums++] = cap;
		}
	}
	if (!nums) {
		/* check whether ADC-switch is possible */
		if (!alc_check_dyn_adc_switch(codec)) {
			printk(KERN_WARNING "hda_codec: %s: no valid ADC found;"
			       " using fallback 0x%x\n",
			       codec->chip_name, spec->private_adc_nids[0]);
			spec->num_adc_nids = 1;
			spec->auto_mic = 0;
			return;
		}
	} else if (nums != spec->num_adc_nids) {
		memcpy(spec->private_adc_nids, adc_nids,
		       nums * sizeof(hda_nid_t));
		memcpy(spec->private_capsrc_nids, capsrc_nids,
		       nums * sizeof(hda_nid_t));
		spec->num_adc_nids = nums;
	}

	if (spec->auto_mic)
		alc_auto_mic_check_imux(codec); /* check auto-mic setups */
	else if (spec->input_mux->num_items == 1)
		spec->num_adc_nids = 1; /* reduce to a single ADC */
}

/*
 * initialize ADC paths
 */
static void alc_auto_init_adc(struct hda_codec *codec, int adc_idx)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid;

	nid = spec->adc_nids[adc_idx];
	/* mute ADC */
	if (nid_has_mute(codec, nid, HDA_INPUT)) {
		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_MUTE(0));
		return;
	}
	if (!spec->capsrc_nids)
		return;
	nid = spec->capsrc_nids[adc_idx];
	if (nid_has_mute(codec, nid, HDA_OUTPUT))
		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_OUT_MUTE);
}

static void alc_auto_init_input_src(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int c, nums;

	for (c = 0; c < spec->num_adc_nids; c++)
		alc_auto_init_adc(codec, c);
	if (spec->dyn_adc_switch)
		nums = 1;
	else
		nums = spec->num_adc_nids;
	for (c = 0; c < nums; c++)
		alc_mux_select(codec, 0, spec->cur_mux[c], true);
}

/* add mic boosts if needed */
static int alc_auto_add_mic_boost(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, err;
	int type_idx = 0;
	hda_nid_t nid;
	const char *prev_label = NULL;

	for (i = 0; i < cfg->num_inputs; i++) {
		if (cfg->inputs[i].type > AUTO_PIN_MIC)
			break;
		nid = cfg->inputs[i].pin;
		if (get_wcaps(codec, nid) & AC_WCAP_IN_AMP) {
			const char *label;
			char boost_label[32];

			label = hda_get_autocfg_input_label(codec, cfg, i);
			if (prev_label && !strcmp(label, prev_label))
				type_idx++;
			else
				type_idx = 0;
			prev_label = label;

			snprintf(boost_label, sizeof(boost_label),
				 "%s Boost Volume", label);
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  boost_label, type_idx,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* select or unmute the given capsrc route */
static void select_or_unmute_capsrc(struct hda_codec *codec, hda_nid_t cap,
				    int idx)
{
	if (get_wcaps_type(get_wcaps(codec, cap)) == AC_WID_AUD_MIX) {
		snd_hda_codec_amp_stereo(codec, cap, HDA_INPUT, idx,
					 HDA_AMP_MUTE, 0);
	} else if (snd_hda_get_conn_list(codec, cap, NULL) > 1) {
		snd_hda_codec_write_cache(codec, cap, 0,
					  AC_VERB_SET_CONNECT_SEL, idx);
	}
}

/* set the default connection to that pin */
static int init_capsrc_for_pin(struct hda_codec *codec, hda_nid_t pin)
{
	struct alc_spec *spec = codec->spec;
	int i;

	if (!pin)
		return 0;
	for (i = 0; i < spec->num_adc_nids; i++) {
		hda_nid_t cap = spec->capsrc_nids ?
			spec->capsrc_nids[i] : spec->adc_nids[i];
		int idx;

		idx = get_connection_index(codec, cap, pin);
		if (idx < 0)
			continue;
		select_or_unmute_capsrc(codec, cap, idx);
		return i; /* return the found index */
	}
	return -1; /* not found */
}

/* initialize some special cases for input sources */
static void alc_init_special_input_src(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->autocfg.num_inputs; i++)
		init_capsrc_for_pin(codec, spec->autocfg.inputs[i].pin);
}

/* assign appropriate capture mixers */
static void set_capture_mixer(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	static const struct snd_kcontrol_new *caps[2][3] = {
		{ alc_capture_mixer_nosrc1,
		  alc_capture_mixer_nosrc2,
		  alc_capture_mixer_nosrc3 },
		{ alc_capture_mixer1,
		  alc_capture_mixer2,
		  alc_capture_mixer3 },
	};

	/* check whether either of ADC or MUX has a volume control */
	if (!nid_has_volume(codec, spec->adc_nids[0], HDA_INPUT)) {
		if (!spec->capsrc_nids)
			return; /* no volume */
		if (!nid_has_volume(codec, spec->capsrc_nids[0], HDA_OUTPUT))
			return; /* no volume in capsrc, too */
		spec->vol_in_capsrc = 1;
	}

	if (spec->num_adc_nids > 0) {
		int mux = 0;
		int num_adcs = 0;

		if (spec->input_mux && spec->input_mux->num_items > 1)
			mux = 1;
		if (spec->auto_mic) {
			num_adcs = 1;
			mux = 0;
		} else if (spec->dyn_adc_switch)
			num_adcs = 1;
		if (!num_adcs) {
			if (spec->num_adc_nids > 3)
				spec->num_adc_nids = 3;
			else if (!spec->num_adc_nids)
				return;
			num_adcs = spec->num_adc_nids;
		}
		spec->cap_mixer = caps[mux][num_adcs - 1];
	}
}

/*
 * standard auto-parser initializations
 */
static void alc_auto_init_std(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc_auto_init_multi_out(codec);
	alc_auto_init_extra_out(codec);
	alc_auto_init_analog_input(codec);
	alc_auto_init_input_src(codec);
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

/*
 * Digital-beep handlers
 */
#ifdef CONFIG_SND_HDA_INPUT_BEEP
#define set_beep_amp(spec, nid, idx, dir) \
	((spec)->beep_amp = HDA_COMPOSE_AMP_VAL(nid, 3, idx, dir))

static const struct snd_pci_quirk beep_white_list[] = {
	SND_PCI_QUIRK(0x1043, 0x829f, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x83ce, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x831a, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x834a, "EeePC", 1),
	SND_PCI_QUIRK(0x8086, 0xd613, "Intel", 1),
	{}
};

static inline int has_cdefine_beep(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct snd_pci_quirk *q;
	q = snd_pci_quirk_lookup(codec->bus->pci, beep_white_list);
	if (q)
		return q->value;
	return spec->cdefine.enable_pcbeep;
}
#else
#define set_beep_amp(spec, nid, idx, dir) /* NOP */
#define has_cdefine_beep(codec)		0
#endif

/* parse the BIOS configuration and set up the alc_spec */
/* return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
static int alc_parse_auto_config(struct hda_codec *codec,
				 const hda_nid_t *ignore_nids,
				 const hda_nid_t *ssid_nids)
{
	struct alc_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   ignore_nids);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs) {
		if (spec->autocfg.dig_outs || spec->autocfg.dig_in_pin) {
			spec->multiout.max_channels = 2;
			spec->no_analog = 1;
			goto dig_only;
		}
		return 0; /* can't find valid BIOS pin config */
	}
	err = alc_auto_fill_dac_nids(codec);
	if (err < 0)
		return err;
	err = alc_auto_add_multi_channel_mode(codec, alc_auto_fill_dac_nids);
	if (err < 0)
		return err;
	err = alc_auto_create_multi_out_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc_auto_create_hp_out(codec);
	if (err < 0)
		return err;
	err = alc_auto_create_speaker_out(codec);
	if (err < 0)
		return err;
	err = alc_auto_create_input_ctls(codec);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

 dig_only:
	alc_auto_parse_digital(codec);

	if (!spec->no_analog)
		alc_remove_invalid_adc_nids(codec);

	if (ssid_nids)
		alc_ssid_check(codec, ssid_nids);

	if (!spec->no_analog) {
		alc_auto_check_switches(codec);
		err = alc_auto_add_mic_boost(codec);
		if (err < 0)
			return err;
	}

	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	return 1;
}

static int alc880_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc880_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc880_ssids[] = { 0x15, 0x1b, 0x14, 0 }; 
	return alc_parse_auto_config(codec, alc880_ignore, alc880_ssids);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static const struct hda_amp_list alc880_loopbacks[] = {
	{ 0x0b, HDA_INPUT, 0 },
	{ 0x0b, HDA_INPUT, 1 },
	{ 0x0b, HDA_INPUT, 2 },
	{ 0x0b, HDA_INPUT, 3 },
	{ 0x0b, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

/*
 * board setups
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#define alc_board_config \
	snd_hda_check_board_config
#define alc_board_codec_sid_config \
	snd_hda_check_board_codec_sid_config
#include "alc_quirks.c"
#else
#define alc_board_config(codec, nums, models, tbl)	-1
#define alc_board_codec_sid_config(codec, nums, models, tbl)	-1
#define setup_preset(codec, x)	/* NOP */
#endif

/*
 * OK, here we have finally the patch for ALC880
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc880_quirks.c"
#endif

static int patch_alc880(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;
	spec->need_dac_fix = 1;

	board_config = alc_board_config(codec, ALC880_MODEL_LAST,
					alc880_models, alc880_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc880_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using 3-stack mode...\n");
			board_config = ALC880_3ST;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO)
		setup_preset(codec, &alc880_presets[board_config]);

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	if (!spec->no_analog) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
	}

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC_MODEL_AUTO)
		spec->init_hook = alc_auto_init_std;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc880_loopbacks;
#endif

	return 0;
}


/*
 * ALC260 support
 */
static int alc260_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc260_ignore[] = { 0x17, 0 };
	static const hda_nid_t alc260_ssids[] = { 0x10, 0x15, 0x0f, 0 };
	return alc_parse_auto_config(codec, alc260_ignore, alc260_ssids);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static const struct hda_amp_list alc260_loopbacks[] = {
	{ 0x07, HDA_INPUT, 0 },
	{ 0x07, HDA_INPUT, 1 },
	{ 0x07, HDA_INPUT, 2 },
	{ 0x07, HDA_INPUT, 3 },
	{ 0x07, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

/*
 * Pin config fixes
 */
enum {
	PINFIX_HP_DC5750,
};

static const struct alc_fixup alc260_fixups[] = {
	[PINFIX_HP_DC5750] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x11, 0x90130110 }, /* speaker */
			{ }
		}
	},
};

static const struct snd_pci_quirk alc260_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x280a, "HP dc5750", PINFIX_HP_DC5750),
	{}
};

/*
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc260_quirks.c"
#endif

static int patch_alc260(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x07;

	board_config = alc_board_config(codec, ALC260_MODEL_LAST,
					alc260_models, alc260_cfg_tbl);
	if (board_config < 0) {
		snd_printd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			   codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		alc_pick_fixup(codec, NULL, alc260_fixup_tbl, alc260_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
	}

	if (board_config == ALC_MODEL_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc260_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC260_BASIC;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO)
		setup_preset(codec, &alc260_presets[board_config]);

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	if (!spec->no_analog) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		set_beep_amp(spec, 0x07, 0x05, HDA_INPUT);
	}

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	spec->vmaster_nid = 0x08;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC_MODEL_AUTO)
		spec->init_hook = alc_auto_init_std;
	spec->shutup = alc_eapd_shutup;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc260_loopbacks;
#endif

	return 0;
}


/*
 * ALC882/883/885/888/889 support
 *
 * ALC882 is almost identical with ALC880 but has cleaner and more flexible
 * configuration.  Each pin widget can choose any input DACs and a mixer.
 * Each ADC is connected from a mixer of all inputs.  This makes possible
 * 6-channel independent captures.
 *
 * In addition, an independent DAC for the multi-playback (not used in this
 * driver yet).
 */
#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc882_loopbacks	alc880_loopbacks
#endif

/*
 * Pin config fixes
 */
enum {
	PINFIX_ABIT_AW9D_MAX,
	PINFIX_LENOVO_Y530,
	PINFIX_PB_M5210,
	PINFIX_ACER_ASPIRE_7736,
};

static const struct alc_fixup alc882_fixups[] = {
	[PINFIX_ABIT_AW9D_MAX] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x15, 0x01080104 }, /* side */
			{ 0x16, 0x01011012 }, /* rear */
			{ 0x17, 0x01016011 }, /* clfe */
			{ }
		}
	},
	[PINFIX_LENOVO_Y530] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x15, 0x99130112 }, /* rear int speakers */
			{ 0x16, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[PINFIX_PB_M5210] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50 },
			{}
		}
	},
	[PINFIX_ACER_ASPIRE_7736] = {
		.type = ALC_FIXUP_SKU,
		.v.sku = ALC_FIXUP_SKU_IGNORE,
	},
};

static const struct snd_pci_quirk alc882_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0155, "Packard-Bell M5120", PINFIX_PB_M5210),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Y530", PINFIX_LENOVO_Y530),
	SND_PCI_QUIRK(0x147b, 0x107a, "Abit AW9D-MAX", PINFIX_ABIT_AW9D_MAX),
	SND_PCI_QUIRK(0x1025, 0x0296, "Acer Aspire 7736z", PINFIX_ACER_ASPIRE_7736),
	{}
};

/*
 * BIOS auto configuration
 */
/* almost identical with ALC880 parser... */
static int alc882_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc882_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc882_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc882_ignore, alc882_ssids);
}

/*
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc882_quirks.c"
#endif

static int patch_alc882(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;

	switch (codec->vendor_id) {
	case 0x10ec0882:
	case 0x10ec0885:
		break;
	default:
		/* ALC883 and variants */
		alc_fix_pll_init(codec, 0x20, 0x0a, 10);
		break;
	}

	board_config = alc_board_config(codec, ALC882_MODEL_LAST,
					alc882_models, alc882_cfg_tbl);

	if (board_config < 0)
		board_config = alc_board_codec_sid_config(codec,
			ALC882_MODEL_LAST, alc882_models, alc882_ssid_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		alc_pick_fixup(codec, NULL, alc882_fixup_tbl, alc882_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
	}

	alc_auto_parse_customize_define(codec);

	if (board_config == ALC_MODEL_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc882_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC882_3ST_DIG;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO)
		setup_preset(codec, &alc882_presets[board_config]);

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	if (!spec->no_analog && has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
	}

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC_MODEL_AUTO)
		spec->init_hook = alc_auto_init_std;

	alc_init_jacks(codec);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc882_loopbacks;
#endif

	return 0;
}


/*
 * ALC262 support
 */
static int alc262_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc262_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc262_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc262_ignore, alc262_ssids);
}

/*
 * Pin config fixes
 */
enum {
	PINFIX_FSC_H270,
	PINFIX_HP_Z200,
};

static const struct alc_fixup alc262_fixups[] = {
	[PINFIX_FSC_H270] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0221142f }, /* front HP */
			{ 0x1b, 0x0121141f }, /* rear HP */
			{ }
		}
	},
	[PINFIX_HP_Z200] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x16, 0x99130120 }, /* internal speaker */
			{ }
		}
	},
};

static const struct snd_pci_quirk alc262_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x170b, "HP Z200", PINFIX_HP_Z200),
	SND_PCI_QUIRK(0x1734, 0x1147, "FSC Celsius H270", PINFIX_FSC_H270),
	{}
};


#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc262_loopbacks	alc880_loopbacks
#endif

/*
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc262_quirks.c"
#endif

static int patch_alc262(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;

#if 0
	/* pshou 07/11/05  set a zero PCM sample to DAC when FIFO is
	 * under-run
	 */
	{
	int tmp;
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_COEF_INDEX, 7);
	tmp = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_COEF_INDEX, 7);
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_PROC_COEF, tmp | 0x80);
	}
#endif
	alc_auto_parse_customize_define(codec);

	alc_fix_pll_init(codec, 0x20, 0x0a, 10);

	board_config = alc_board_config(codec, ALC262_MODEL_LAST,
					alc262_models, alc262_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		alc_pick_fixup(codec, NULL, alc262_fixup_tbl, alc262_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
	}

	if (board_config == ALC_MODEL_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc262_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC262_BASIC;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO)
		setup_preset(codec, &alc262_presets[board_config]);

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	if (!spec->no_analog && has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
	}

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC_MODEL_AUTO)
		spec->init_hook = alc_auto_init_std;
	spec->shutup = alc_eapd_shutup;

	alc_init_jacks(codec);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc262_loopbacks;
#endif

	return 0;
}

/*
 *  ALC268
 */
/* bind Beep switches of both NID 0x0f and 0x10 */
static const struct hda_bind_ctls alc268_bind_beep_sw = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x0f, 3, 1, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x10, 3, 1, HDA_INPUT),
		0
	},
};

static const struct snd_kcontrol_new alc268_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x1d, 0x0, HDA_INPUT),
	HDA_BIND_SW("Beep Playback Switch", &alc268_bind_beep_sw),
	{ }
};

/* set PCBEEP vol = 0, mute connections */
static const struct hda_verb alc268_beep_init_verbs[] = {
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ }
};

/*
 * BIOS auto configuration
 */
static int alc268_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc268_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	struct alc_spec *spec = codec->spec;
	int err = alc_parse_auto_config(codec, NULL, alc268_ssids);
	if (err > 0) {
		if (!spec->no_analog && spec->autocfg.speaker_pins[0] != 0x1d) {
			add_mixer(spec, alc268_beep_mixer);
			add_verb(spec, alc268_beep_init_verbs);
		}
	}
	return err;
}

/*
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc268_quirks.c"
#endif

static int patch_alc268(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int i, has_beep, err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* ALC268 has no aa-loopback mixer */

	board_config = alc_board_config(codec, ALC268_MODEL_LAST,
					alc268_models, alc268_cfg_tbl);

	if (board_config < 0)
		board_config = alc_board_codec_sid_config(codec,
			ALC268_MODEL_LAST, alc268_models, alc268_ssid_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc268_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC268_3ST;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO)
		setup_preset(codec, &alc268_presets[board_config]);

	has_beep = 0;
	for (i = 0; i < spec->num_mixers; i++) {
		if (spec->mixers[i] == alc268_beep_mixer) {
			has_beep = 1;
			break;
		}
	}

	if (has_beep) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		if (!query_amp_caps(codec, 0x1d, HDA_INPUT))
			/* override the amp caps for beep generator */
			snd_hda_override_amp_caps(codec, 0x1d, HDA_INPUT,
					  (0x0c << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x0c << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x07 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (0 << AC_AMPCAP_MUTE_SHIFT));
	}

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC_MODEL_AUTO)
		spec->init_hook = alc_auto_init_std;
	spec->shutup = alc_eapd_shutup;

	alc_init_jacks(codec);

	return 0;
}

/*
 * ALC269
 */
#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc269_loopbacks	alc880_loopbacks
#endif

static const struct hda_pcm_stream alc269_44k_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc_playback_pcm_open,
		.prepare = alc_playback_pcm_prepare,
		.cleanup = alc_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream alc269_44k_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
	/* NID is set in alc_build_pcms */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
static int alc269_mic2_for_mute_led(struct hda_codec *codec)
{
	switch (codec->subsystem_id) {
	case 0x103c1586:
		return 1;
	}
	return 0;
}

static int alc269_mic2_mute_check_ps(struct hda_codec *codec, hda_nid_t nid)
{
	/* update mute-LED according to the speaker mute state */
	if (nid == 0x01 || nid == 0x14) {
		int pinval;
		if (snd_hda_codec_amp_read(codec, 0x14, 0, HDA_OUTPUT, 0) &
		    HDA_AMP_MUTE)
			pinval = 0x24;
		else
			pinval = 0x20;
		/* mic2 vref pin is used for mute LED control */
		snd_hda_codec_update_cache(codec, 0x19, 0,
					   AC_VERB_SET_PIN_WIDGET_CONTROL,
					   pinval);
	}
	return alc_check_power_status(codec, nid);
}
#endif /* CONFIG_SND_HDA_POWER_SAVE */

/* different alc269-variants */
enum {
	ALC269_TYPE_ALC269VA,
	ALC269_TYPE_ALC269VB,
	ALC269_TYPE_ALC269VC,
};

/*
 * BIOS auto configuration
 */
static int alc269_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc269_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc269_ssids[] = { 0, 0x1b, 0x14, 0x21 };
	static const hda_nid_t alc269va_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	struct alc_spec *spec = codec->spec;
	const hda_nid_t *ssids = spec->codec_variant == ALC269_TYPE_ALC269VA ?
		alc269va_ssids : alc269_ssids;

	return alc_parse_auto_config(codec, alc269_ignore, ssids);
}

static void alc269_toggle_power_output(struct hda_codec *codec, int power_up)
{
	int val = alc_read_coef_idx(codec, 0x04);
	if (power_up)
		val |= 1 << 11;
	else
		val &= ~(1 << 11);
	alc_write_coef_idx(codec, 0x04, val);
}

static void alc269_shutup(struct hda_codec *codec)
{
	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x017)
		alc269_toggle_power_output(codec, 0);
	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x018) {
		alc269_toggle_power_output(codec, 0);
		msleep(150);
	}
}

#ifdef CONFIG_PM
static int alc269_resume(struct hda_codec *codec)
{
	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x018) {
		alc269_toggle_power_output(codec, 0);
		msleep(150);
	}

	codec->patch_ops.init(codec);

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x017) {
		alc269_toggle_power_output(codec, 1);
		msleep(200);
	}

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x018)
		alc269_toggle_power_output(codec, 1);

	snd_hda_codec_resume_amp(codec);
	snd_hda_codec_resume_cache(codec);
	hda_call_check_power_status(codec, 0x01);
	return 0;
}
#endif /* CONFIG_PM */

static void alc269_fixup_hweq(struct hda_codec *codec,
			       const struct alc_fixup *fix, int action)
{
	int coef;

	if (action != ALC_FIXUP_ACT_INIT)
		return;
	coef = alc_read_coef_idx(codec, 0x1e);
	alc_write_coef_idx(codec, 0x1e, coef | 0x80);
}

static void alc271_fixup_dmic(struct hda_codec *codec,
			      const struct alc_fixup *fix, int action)
{
	static const struct hda_verb verbs[] = {
		{0x20, AC_VERB_SET_COEF_INDEX, 0x0d},
		{0x20, AC_VERB_SET_PROC_COEF, 0x4000},
		{}
	};
	unsigned int cfg;

	if (strcmp(codec->chip_name, "ALC271X"))
		return;
	cfg = snd_hda_codec_get_pincfg(codec, 0x12);
	if (get_defcfg_connect(cfg) == AC_JACK_PORT_FIXED)
		snd_hda_sequence_write(codec, verbs);
}

static void alc269_fixup_pcm_44k(struct hda_codec *codec,
				 const struct alc_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;

	if (action != ALC_FIXUP_ACT_PROBE)
		return;

	/* Due to a hardware problem on Lenovo Ideadpad, we need to
	 * fix the sample rate of analog I/O to 44.1kHz
	 */
	spec->stream_analog_playback = &alc269_44k_pcm_analog_playback;
	spec->stream_analog_capture = &alc269_44k_pcm_analog_capture;
}

static void alc269_fixup_stereo_dmic(struct hda_codec *codec,
				     const struct alc_fixup *fix, int action)
{
	int coef;

	if (action != ALC_FIXUP_ACT_INIT)
		return;
	/* The digital-mic unit sends PDM (differential signal) instead of
	 * the standard PCM, thus you can't record a valid mono stream as is.
	 * Below is a workaround specific to ALC269 to control the dmic
	 * signal source as mono.
	 */
	coef = alc_read_coef_idx(codec, 0x07);
	alc_write_coef_idx(codec, 0x07, coef | 0x80);
}

enum {
	ALC269_FIXUP_SONY_VAIO,
	ALC275_FIXUP_SONY_VAIO_GPIO2,
	ALC269_FIXUP_DELL_M101Z,
	ALC269_FIXUP_SKU_IGNORE,
	ALC269_FIXUP_ASUS_G73JW,
	ALC269_FIXUP_LENOVO_EAPD,
	ALC275_FIXUP_SONY_HWEQ,
	ALC271_FIXUP_DMIC,
	ALC269_FIXUP_PCM_44K,
	ALC269_FIXUP_STEREO_DMIC,
};

static const struct alc_fixup alc269_fixups[] = {
	[ALC269_FIXUP_SONY_VAIO] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREFGRD},
			{}
		}
	},
	[ALC275_FIXUP_SONY_VAIO_GPIO2] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x01, AC_VERB_SET_GPIO_MASK, 0x04},
			{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x04},
			{0x01, AC_VERB_SET_GPIO_DATA, 0x00},
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_SONY_VAIO
	},
	[ALC269_FIXUP_DELL_M101Z] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enables internal speaker */
			{0x20, AC_VERB_SET_COEF_INDEX, 13},
			{0x20, AC_VERB_SET_PROC_COEF, 0x4040},
			{}
		}
	},
	[ALC269_FIXUP_SKU_IGNORE] = {
		.type = ALC_FIXUP_SKU,
		.v.sku = ALC_FIXUP_SKU_IGNORE,
	},
	[ALC269_FIXUP_ASUS_G73JW] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x17, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[ALC269_FIXUP_LENOVO_EAPD] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x14, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC275_FIXUP_SONY_HWEQ] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc269_fixup_hweq,
		.chained = true,
		.chain_id = ALC275_FIXUP_SONY_VAIO_GPIO2
	},
	[ALC271_FIXUP_DMIC] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc271_fixup_dmic,
	},
	[ALC269_FIXUP_PCM_44K] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc269_fixup_pcm_44k,
	},
	[ALC269_FIXUP_STEREO_DMIC] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc269_fixup_stereo_dmic,
	},
};

static const struct snd_pci_quirk alc269_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1a13, "Asus G73Jw", ALC269_FIXUP_ASUS_G73JW),
	SND_PCI_QUIRK(0x1043, 0x16e3, "ASUS UX50", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x831a, "ASUS P901", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x834a, "ASUS S101", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x8398, "ASUS P1005", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1043, 0x83ce, "ASUS P1005", ALC269_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x104d, 0x9073, "Sony VAIO", ALC275_FIXUP_SONY_VAIO_GPIO2),
	SND_PCI_QUIRK(0x104d, 0x907b, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK(0x104d, 0x9084, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK_VENDOR(0x104d, "Sony VAIO", ALC269_FIXUP_SONY_VAIO),
	SND_PCI_QUIRK(0x1028, 0x0470, "Dell M101z", ALC269_FIXUP_DELL_M101Z),
	SND_PCI_QUIRK_VENDOR(0x1025, "Acer Aspire", ALC271_FIXUP_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Thinkpad SL410/510", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x215e, "Thinkpad L512", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21b8, "Thinkpad Edge 14", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21ca, "Thinkpad L412", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21e9, "Thinkpad Edge 15", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Lenovo Ideapd", ALC269_FIXUP_PCM_44K),
	SND_PCI_QUIRK(0x17aa, 0x9e54, "LENOVO NB", ALC269_FIXUP_LENOVO_EAPD),
	{}
};


static int alc269_fill_coef(struct hda_codec *codec)
{
	int val;

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) < 0x015) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8817);
	}

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x016) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8814);
	}

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x017) {
		val = alc_read_coef_idx(codec, 0x04);
		/* Power up output pin */
		alc_write_coef_idx(codec, 0x04, val | (1<<11));
	}

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x018) {
		val = alc_read_coef_idx(codec, 0xd);
		if ((val & 0x0c00) >> 10 != 0x1) {
			/* Capless ramp up clock control */
			alc_write_coef_idx(codec, 0xd, val | (1<<10));
		}
		val = alc_read_coef_idx(codec, 0x17);
		if ((val & 0x01c0) >> 6 != 0x4) {
			/* Class D power on reset */
			alc_write_coef_idx(codec, 0x17, val | (1<<7));
		}
	}

	val = alc_read_coef_idx(codec, 0xd); /* Class D */
	alc_write_coef_idx(codec, 0xd, val | (1<<14));

	val = alc_read_coef_idx(codec, 0x4); /* HP */
	alc_write_coef_idx(codec, 0x4, val | (1<<11));

	return 0;
}

/*
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc269_quirks.c"
#endif

static int patch_alc269(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config, coef;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;

	alc_auto_parse_customize_define(codec);

	if (codec->vendor_id == 0x10ec0269) {
		spec->codec_variant = ALC269_TYPE_ALC269VA;
		coef = alc_read_coef_idx(codec, 0);
		if ((coef & 0x00f0) == 0x0010) {
			if (codec->bus->pci->subsystem_vendor == 0x1025 &&
			    spec->cdefine.platform_type == 1) {
				alc_codec_rename(codec, "ALC271X");
			} else if ((coef & 0xf000) == 0x2000) {
				alc_codec_rename(codec, "ALC259");
			} else if ((coef & 0xf000) == 0x3000) {
				alc_codec_rename(codec, "ALC258");
			} else if ((coef & 0xfff0) == 0x3010) {
				alc_codec_rename(codec, "ALC277");
			} else {
				alc_codec_rename(codec, "ALC269VB");
			}
			spec->codec_variant = ALC269_TYPE_ALC269VB;
		} else if ((coef & 0x00f0) == 0x0020) {
			if (coef == 0xa023)
				alc_codec_rename(codec, "ALC259");
			else if (coef == 0x6023)
				alc_codec_rename(codec, "ALC281X");
			else if (codec->bus->pci->subsystem_vendor == 0x17aa &&
				 codec->bus->pci->subsystem_device == 0x21f3)
				alc_codec_rename(codec, "ALC3202");
			else
				alc_codec_rename(codec, "ALC269VC");
			spec->codec_variant = ALC269_TYPE_ALC269VC;
		} else
			alc_fix_pll_init(codec, 0x20, 0x04, 15);
		alc269_fill_coef(codec);
	}

	board_config = alc_board_config(codec, ALC269_MODEL_LAST,
					alc269_models, alc269_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		alc_pick_fixup(codec, NULL, alc269_fixup_tbl, alc269_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
	}

	if (board_config == ALC_MODEL_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc269_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC269_BASIC;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO)
		setup_preset(codec, &alc269_presets[board_config]);

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	if (!spec->no_analog && has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		set_beep_amp(spec, 0x0b, 0x04, HDA_INPUT);
	}

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
#ifdef CONFIG_PM
	codec->patch_ops.resume = alc269_resume;
#endif
	if (board_config == ALC_MODEL_AUTO)
		spec->init_hook = alc_auto_init_std;
	spec->shutup = alc269_shutup;

	alc_init_jacks(codec);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc269_loopbacks;
	if (alc269_mic2_for_mute_led(codec))
		codec->patch_ops.check_power_status = alc269_mic2_mute_check_ps;
#endif

	return 0;
}

/*
 * ALC861
 */

static int alc861_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc861_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc861_ssids[] = { 0x0e, 0x0f, 0x0b, 0 };
	return alc_parse_auto_config(codec, alc861_ignore, alc861_ssids);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static const struct hda_amp_list alc861_loopbacks[] = {
	{ 0x15, HDA_INPUT, 0 },
	{ 0x15, HDA_INPUT, 1 },
	{ 0x15, HDA_INPUT, 2 },
	{ 0x15, HDA_INPUT, 3 },
	{ } /* end */
};
#endif


/* Pin config fixes */
enum {
	PINFIX_FSC_AMILO_PI1505,
};

static const struct alc_fixup alc861_fixups[] = {
	[PINFIX_FSC_AMILO_PI1505] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x0b, 0x0221101f }, /* HP */
			{ 0x0f, 0x90170310 }, /* speaker */
			{ }
		}
	},
};

static const struct snd_pci_quirk alc861_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1734, 0x10c7, "FSC Amilo Pi1505", PINFIX_FSC_AMILO_PI1505),
	{}
};

/*
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc861_quirks.c"
#endif

static int patch_alc861(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x15;

        board_config = alc_board_config(codec, ALC861_MODEL_LAST,
					alc861_models, alc861_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		alc_pick_fixup(codec, NULL, alc861_fixup_tbl, alc861_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
	}

	if (board_config == ALC_MODEL_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc861_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
		   board_config = ALC861_3ST_DIG;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO)
		setup_preset(codec, &alc861_presets[board_config]);

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	if (!spec->no_analog) {
		err = snd_hda_attach_beep_device(codec, 0x23);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		set_beep_amp(spec, 0x23, 0, HDA_OUTPUT);
	}

	spec->vmaster_nid = 0x03;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC_MODEL_AUTO) {
		spec->init_hook = alc_auto_init_std;
#ifdef CONFIG_SND_HDA_POWER_SAVE
		spec->power_hook = alc_power_eapd;
#endif
	}
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc861_loopbacks;
#endif

	return 0;
}

/*
 * ALC861-VD support
 *
 * Based on ALC882
 *
 * In addition, an independent DAC
 */
#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc861vd_loopbacks	alc880_loopbacks
#endif

static int alc861vd_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc861vd_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc861vd_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc861vd_ignore, alc861vd_ssids);
}

enum {
	ALC660VD_FIX_ASUS_GPIO1
};

/* reset GPIO1 */
static const struct alc_fixup alc861vd_fixups[] = {
	[ALC660VD_FIX_ASUS_GPIO1] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
			{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
			{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
			{ }
		}
	},
};

static const struct snd_pci_quirk alc861vd_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS A7-K", ALC660VD_FIX_ASUS_GPIO1),
	{}
};

static const struct hda_verb alc660vd_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/*
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc861vd_quirks.c"
#endif

static int patch_alc861vd(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;

	board_config = alc_board_config(codec, ALC861VD_MODEL_LAST,
					alc861vd_models, alc861vd_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		alc_pick_fixup(codec, NULL, alc861vd_fixup_tbl, alc861vd_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
	}

	if (board_config == ALC_MODEL_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc861vd_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC861VD_3ST;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO)
		setup_preset(codec, &alc861vd_presets[board_config]);

	if (codec->vendor_id == 0x10ec0660) {
		/* always turn on EAPD */
		add_verb(spec, alc660vd_eapd_verbs);
	}

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	if (!spec->no_analog) {
		err = snd_hda_attach_beep_device(codec, 0x23);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
	}

	spec->vmaster_nid = 0x02;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	codec->patch_ops = alc_patch_ops;

	if (board_config == ALC_MODEL_AUTO)
		spec->init_hook = alc_auto_init_std;
	spec->shutup = alc_eapd_shutup;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc861vd_loopbacks;
#endif

	return 0;
}

/*
 * ALC662 support
 *
 * ALC662 is almost identical with ALC880 but has cleaner and more flexible
 * configuration.  Each pin widget can choose any input DACs and a mixer.
 * Each ADC is connected from a mixer of all inputs.  This makes possible
 * 6-channel independent captures.
 *
 * In addition, an independent DAC for the multi-playback (not used in this
 * driver yet).
 */
#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc662_loopbacks	alc880_loopbacks
#endif

/*
 * BIOS auto configuration
 */

static int alc662_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc662_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc663_ssids[] = { 0x15, 0x1b, 0x14, 0x21 };
	static const hda_nid_t alc662_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	const hda_nid_t *ssids;

	if (codec->vendor_id == 0x10ec0272 || codec->vendor_id == 0x10ec0663 ||
	    codec->vendor_id == 0x10ec0665 || codec->vendor_id == 0x10ec0670)
		ssids = alc663_ssids;
	else
		ssids = alc662_ssids;
	return alc_parse_auto_config(codec, alc662_ignore, ssids);
}

static void alc272_fixup_mario(struct hda_codec *codec,
			       const struct alc_fixup *fix, int action)
{
	if (action != ALC_FIXUP_ACT_PROBE)
		return;
	if (snd_hda_override_amp_caps(codec, 0x2, HDA_OUTPUT,
				      (0x3b << AC_AMPCAP_OFFSET_SHIFT) |
				      (0x3b << AC_AMPCAP_NUM_STEPS_SHIFT) |
				      (0x03 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				      (0 << AC_AMPCAP_MUTE_SHIFT)))
		printk(KERN_WARNING
		       "hda_codec: failed to override amp caps for NID 0x2\n");
}

enum {
	ALC662_FIXUP_ASPIRE,
	ALC662_FIXUP_IDEAPAD,
	ALC272_FIXUP_MARIO,
	ALC662_FIXUP_CZC_P10T,
	ALC662_FIXUP_SKU_IGNORE,
	ALC662_FIXUP_HP_RP5800,
};

static const struct alc_fixup alc662_fixups[] = {
	[ALC662_FIXUP_ASPIRE] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x15, 0x99130112 }, /* subwoofer */
			{ }
		}
	},
	[ALC662_FIXUP_IDEAPAD] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x17, 0x99130112 }, /* subwoofer */
			{ }
		}
	},
	[ALC272_FIXUP_MARIO] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc272_fixup_mario,
	},
	[ALC662_FIXUP_CZC_P10T] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x14, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC662_FIXUP_SKU_IGNORE] = {
		.type = ALC_FIXUP_SKU,
		.v.sku = ALC_FIXUP_SKU_IGNORE,
	},
	[ALC662_FIXUP_HP_RP5800] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x0221201f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
};

static const struct snd_pci_quirk alc662_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0308, "Acer Aspire 8942G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x031c, "Gateway NV79", ALC662_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x1025, 0x038b, "Acer Aspire 8943G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x103c, 0x1632, "HP RP5800", ALC662_FIXUP_HP_RP5800),
	SND_PCI_QUIRK(0x144d, 0xc051, "Samsung R720", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x38af, "Lenovo Ideapad Y550P", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Ideapad Y550", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x1b35, 0x2206, "CZC P10T", ALC662_FIXUP_CZC_P10T),
	{}
};

static const struct alc_model_fixup alc662_fixup_models[] = {
	{.id = ALC272_FIXUP_MARIO, .name = "mario"},
	{}
};


/*
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc662_quirks.c"
#endif

static int patch_alc662(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;
	int coef;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;

	alc_auto_parse_customize_define(codec);

	alc_fix_pll_init(codec, 0x20, 0x04, 15);

	coef = alc_read_coef_idx(codec, 0);
	if (coef == 0x8020 || coef == 0x8011)
		alc_codec_rename(codec, "ALC661");
	else if (coef & (1 << 14) &&
		codec->bus->pci->subsystem_vendor == 0x1025 &&
		spec->cdefine.platform_type == 1)
		alc_codec_rename(codec, "ALC272X");
	else if (coef == 0x4011)
		alc_codec_rename(codec, "ALC656");

	board_config = alc_board_config(codec, ALC662_MODEL_LAST,
					alc662_models, alc662_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		alc_pick_fixup(codec, alc662_fixup_models,
			       alc662_fixup_tbl, alc662_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
		/* automatic parse from the BIOS config */
		err = alc662_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC662_3ST_2ch_DIG;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO)
		setup_preset(codec, &alc662_presets[board_config]);

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	if (!spec->no_analog && has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		switch (codec->vendor_id) {
		case 0x10ec0662:
			set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
			break;
		case 0x10ec0272:
		case 0x10ec0663:
		case 0x10ec0665:
			set_beep_amp(spec, 0x0b, 0x04, HDA_INPUT);
			break;
		case 0x10ec0273:
			set_beep_amp(spec, 0x0b, 0x03, HDA_INPUT);
			break;
		}
	}
	spec->vmaster_nid = 0x02;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC_MODEL_AUTO)
		spec->init_hook = alc_auto_init_std;
	spec->shutup = alc_eapd_shutup;

	alc_init_jacks(codec);

#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc662_loopbacks;
#endif

	return 0;
}

static int patch_alc888(struct hda_codec *codec)
{
	if ((alc_read_coef_idx(codec, 0) & 0x00f0)==0x0030){
		kfree(codec->chip_name);
		if (codec->vendor_id == 0x10ec0887)
			codec->chip_name = kstrdup("ALC887-VD", GFP_KERNEL);
		else
			codec->chip_name = kstrdup("ALC888-VD", GFP_KERNEL);
		if (!codec->chip_name) {
			alc_free(codec);
			return -ENOMEM;
		}
		return patch_alc662(codec);
	}
	return patch_alc882(codec);
}

static int patch_alc899(struct hda_codec *codec)
{
	if ((alc_read_coef_idx(codec, 0) & 0x2000) != 0x2000) {
		kfree(codec->chip_name);
		codec->chip_name = kstrdup("ALC898", GFP_KERNEL);
	}
	return patch_alc882(codec);
}

/*
 * ALC680 support
 */

static int alc680_parse_auto_config(struct hda_codec *codec)
{
	return alc_parse_auto_config(codec, NULL, NULL);
}

/*
 */
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
#include "alc680_quirks.c"
#endif

static int patch_alc680(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* ALC680 has no aa-loopback mixer */

	board_config = alc_board_config(codec, ALC680_MODEL_LAST,
					alc680_models, alc680_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC_MODEL_AUTO;
	}

	if (board_config == ALC_MODEL_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc680_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC680_BASE;
		}
#endif
	}

	if (board_config != ALC_MODEL_AUTO) {
		setup_preset(codec, &alc680_presets[board_config]);
#ifdef CONFIG_SND_HDA_ENABLE_REALTEK_QUIRKS
		spec->stream_analog_capture = &alc680_pcm_analog_auto_capture;
#endif
	}

	if (!spec->no_analog && !spec->adc_nids) {
		alc_auto_fill_adc_caps(codec);
		alc_rebuild_imux_for_auto_mic(codec);
		alc_remove_invalid_adc_nids(codec);
	}

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC_MODEL_AUTO)
		spec->init_hook = alc_auto_init_std;

	return 0;
}

/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_realtek[] = {
	{ .id = 0x10ec0221, .name = "ALC221", .patch = patch_alc269 },
	{ .id = 0x10ec0260, .name = "ALC260", .patch = patch_alc260 },
	{ .id = 0x10ec0262, .name = "ALC262", .patch = patch_alc262 },
	{ .id = 0x10ec0267, .name = "ALC267", .patch = patch_alc268 },
	{ .id = 0x10ec0268, .name = "ALC268", .patch = patch_alc268 },
	{ .id = 0x10ec0269, .name = "ALC269", .patch = patch_alc269 },
	{ .id = 0x10ec0270, .name = "ALC270", .patch = patch_alc269 },
	{ .id = 0x10ec0272, .name = "ALC272", .patch = patch_alc662 },
	{ .id = 0x10ec0275, .name = "ALC275", .patch = patch_alc269 },
	{ .id = 0x10ec0276, .name = "ALC276", .patch = patch_alc269 },
	{ .id = 0x10ec0861, .rev = 0x100340, .name = "ALC660",
	  .patch = patch_alc861 },
	{ .id = 0x10ec0660, .name = "ALC660-VD", .patch = patch_alc861vd },
	{ .id = 0x10ec0861, .name = "ALC861", .patch = patch_alc861 },
	{ .id = 0x10ec0862, .name = "ALC861-VD", .patch = patch_alc861vd },
	{ .id = 0x10ec0662, .rev = 0x100002, .name = "ALC662 rev2",
	  .patch = patch_alc882 },
	{ .id = 0x10ec0662, .rev = 0x100101, .name = "ALC662 rev1",
	  .patch = patch_alc662 },
	{ .id = 0x10ec0663, .name = "ALC663", .patch = patch_alc662 },
	{ .id = 0x10ec0665, .name = "ALC665", .patch = patch_alc662 },
	{ .id = 0x10ec0670, .name = "ALC670", .patch = patch_alc662 },
	{ .id = 0x10ec0680, .name = "ALC680", .patch = patch_alc680 },
	{ .id = 0x10ec0880, .name = "ALC880", .patch = patch_alc880 },
	{ .id = 0x10ec0882, .name = "ALC882", .patch = patch_alc882 },
	{ .id = 0x10ec0883, .name = "ALC883", .patch = patch_alc882 },
	{ .id = 0x10ec0885, .rev = 0x100101, .name = "ALC889A",
	  .patch = patch_alc882 },
	{ .id = 0x10ec0885, .rev = 0x100103, .name = "ALC889A",
	  .patch = patch_alc882 },
	{ .id = 0x10ec0885, .name = "ALC885", .patch = patch_alc882 },
	{ .id = 0x10ec0887, .name = "ALC887", .patch = patch_alc888 },
	{ .id = 0x10ec0888, .rev = 0x100101, .name = "ALC1200",
	  .patch = patch_alc882 },
	{ .id = 0x10ec0888, .name = "ALC888", .patch = patch_alc888 },
	{ .id = 0x10ec0889, .name = "ALC889", .patch = patch_alc882 },
	{ .id = 0x10ec0892, .name = "ALC892", .patch = patch_alc662 },
	{ .id = 0x10ec0899, .name = "ALC899", .patch = patch_alc899 },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:10ec*");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek HD-audio codec");

static struct hda_codec_preset_list realtek_list = {
	.preset = snd_hda_preset_realtek,
	.owner = THIS_MODULE,
};

static int __init patch_realtek_init(void)
{
	return snd_hda_add_codec_preset(&realtek_list);
}

static void __exit patch_realtek_exit(void)
{
	snd_hda_delete_codec_preset(&realtek_list);
}

module_init(patch_realtek_init)
module_exit(patch_realtek_exit)
