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
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_beep.h"
#include "hda_jack.h"

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

#define MAX_VOL_NIDS	0x40

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
	DECLARE_BITMAP(vol_ctls, MAX_VOL_NIDS << 1);
	DECLARE_BITMAP(sw_ctls, MAX_VOL_NIDS << 1);

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
	void (*automute_hook)(struct hda_codec *codec);

	/* for pin sensing */
	unsigned int hp_jack_present:1;
	unsigned int line_jack_present:1;
	unsigned int master_mute:1;
	unsigned int auto_mic:1;
	unsigned int auto_mic_valid_imux:1;	/* valid imux for auto-mic */
	unsigned int automute_speaker:1; /* automute speaker outputs */
	unsigned int automute_lo:1; /* automute LO outputs */
	unsigned int detect_hp:1;	/* Headphone detection enabled */
	unsigned int detect_lo:1;	/* Line-out detection enabled */
	unsigned int automute_speaker_possible:1; /* there are speakers and either LO or HP */
	unsigned int automute_lo_possible:1;	  /* there are line outs and HP */
	unsigned int keep_vref_in_automute:1; /* Don't clear VREF in automute */

	/* other flags */
	unsigned int no_analog :1; /* digital I/O only */
	unsigned int dyn_adc_switch:1; /* switch ADCs (for ALC275) */
	unsigned int single_input_src:1;
	unsigned int vol_in_capsrc:1; /* use capsrc volume (ADC has no vol) */
	unsigned int parse_flags; /* passed to snd_hda_parse_pin_defcfg() */
	unsigned int shared_mic_hp:1; /* HP/Mic-in sharing */

	/* auto-mute control */
	int automute_mode;
	hda_nid_t automute_mixer_nid[AUTO_CFG_MAX_OUTS];

	int init_amp;
	int codec_variant;	/* flag for other variants */

	/* for virtual master */
	hda_nid_t vmaster_nid;
	struct hda_vmaster_mute_hook vmaster_mute;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_loopback_check loopback;
	int num_loopbacks;
	struct hda_amp_list loopback_list[8];
#endif

	/* for PLL fix */
	hda_nid_t pll_nid;
	unsigned int pll_coef_idx, pll_coef_bit;
	unsigned int coef0;

	/* fix-up list */
	int fixup_id;
	const struct alc_fixup *fixup_list;
	const char *fixup_name;

	/* multi-io */
	int multi_ios;
	struct alc_multi_io multi_io[4];

	/* bind volumes */
	struct snd_array bind_ctls;
};

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

static inline hda_nid_t get_capsrc(struct alc_spec *spec, int idx)
{
	return spec->capsrc_nids ?
		spec->capsrc_nids[idx] : spec->adc_nids[idx];
}

static void call_update_outputs(struct hda_codec *codec);

/* select the given imux item; either unmute exclusively or select the route */
static int alc_mux_select(struct hda_codec *codec, unsigned int adc_idx,
			  unsigned int idx, bool force)
{
	struct alc_spec *spec = codec->spec;
	const struct hda_input_mux *imux;
	unsigned int mux_idx;
	int i, type, num_conns;
	hda_nid_t nid;

	if (!spec->input_mux)
		return 0;

	mux_idx = adc_idx >= spec->num_mux_defs ? 0 : adc_idx;
	imux = &spec->input_mux[mux_idx];
	if (!imux->num_items && mux_idx > 0)
		imux = &spec->input_mux[0];
	if (!imux->num_items)
		return 0;

	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (spec->cur_mux[adc_idx] == idx && !force)
		return 0;
	spec->cur_mux[adc_idx] = idx;

	/* for shared I/O, change the pin-control accordingly */
	if (spec->shared_mic_hp) {
		/* NOTE: this assumes that there are only two inputs, the
		 * first is the real internal mic and the second is HP jack.
		 */
		snd_hda_codec_write(codec, spec->autocfg.inputs[1].pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    spec->cur_mux[adc_idx] ?
				    PIN_VREF80 : PIN_HP);
		spec->automute_speaker = !spec->cur_mux[adc_idx];
		call_update_outputs(codec);
	}

	if (spec->dyn_adc_switch) {
		alc_dyn_adc_pcm_resetup(codec, idx);
		adc_idx = spec->dyn_adc_idx[idx];
	}

	nid = get_capsrc(spec, adc_idx);

	/* no selection? */
	num_conns = snd_hda_get_conn_list(codec, nid, NULL);
	if (num_conns <= 1)
		return 1;

	type = get_wcaps_type(get_wcaps(codec, nid));
	if (type == AC_WID_AUD_MIX) {
		/* Matrix-mixer style (e.g. ALC882) */
		int active = imux->items[idx].index;
		for (i = 0; i < num_conns; i++) {
			unsigned int v = (i == active) ? 0 : HDA_AMP_MUTE;
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT, i,
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
		unsigned int val;
		if (!nid)
			break;
		switch (spec->automute_mode) {
		case ALC_AUTOMUTE_PIN:
			/* don't reset VREF value in case it's controlling
			 * the amp (see alc861_fixup_asus_amp_vref_0f())
			 */
			if (spec->keep_vref_in_automute) {
				val = snd_hda_codec_read(codec, nid, 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
				val &= ~PIN_HP;
			} else
				val = 0;
			val |= pin_bits;
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    val);
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

/* Toggle outputs muting */
static void update_outputs(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int on;

	/* Control HP pins/amps depending on master_mute state;
	 * in general, HP pins/amps control should be enabled in all cases,
	 * but currently set only for master_mute, just to be safe
	 */
	if (!spec->shared_mic_hp) /* don't change HP-pin when shared with mic */
		do_automute(codec, ARRAY_SIZE(spec->autocfg.hp_pins),
		    spec->autocfg.hp_pins, spec->master_mute, true);

	if (!spec->automute_speaker)
		on = 0;
	else
		on = spec->hp_jack_present | spec->line_jack_present;
	on |= spec->master_mute;
	do_automute(codec, ARRAY_SIZE(spec->autocfg.speaker_pins),
		    spec->autocfg.speaker_pins, on, false);

	/* toggle line-out mutes if needed, too */
	/* if LO is a copy of either HP or Speaker, don't need to handle it */
	if (spec->autocfg.line_out_pins[0] == spec->autocfg.hp_pins[0] ||
	    spec->autocfg.line_out_pins[0] == spec->autocfg.speaker_pins[0])
		return;
	if (!spec->automute_lo)
		on = 0;
	else
		on = spec->hp_jack_present;
	on |= spec->master_mute;
	do_automute(codec, ARRAY_SIZE(spec->autocfg.line_out_pins),
		    spec->autocfg.line_out_pins, on, false);
}

static void call_update_outputs(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	if (spec->automute_hook)
		spec->automute_hook(codec);
	else
		update_outputs(codec);
}

/* standard HP-automute helper */
static void alc_hp_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->hp_jack_present =
		detect_jacks(codec, ARRAY_SIZE(spec->autocfg.hp_pins),
			     spec->autocfg.hp_pins);
	if (!spec->detect_hp || (!spec->automute_speaker && !spec->automute_lo))
		return;
	call_update_outputs(codec);
}

/* standard line-out-automute helper */
static void alc_line_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	/* check LO jack only when it's different from HP */
	if (spec->autocfg.line_out_pins[0] == spec->autocfg.hp_pins[0])
		return;

	spec->line_jack_present =
		detect_jacks(codec, ARRAY_SIZE(spec->autocfg.line_out_pins),
			     spec->autocfg.line_out_pins);
	if (!spec->automute_speaker || !spec->detect_lo)
		return;
	call_update_outputs(codec);
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
}

/* handle the specified unsol action (ALC_XXX_EVENT) */
static void alc_exec_unsol_event(struct hda_codec *codec, int action)
{
	switch (action) {
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
	snd_hda_jack_report_sync(codec);
}

/* update the master volume per volume-knob's unsol event */
static void alc_update_knob_master(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int val;
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_value *uctl;

	kctl = snd_hda_find_mixer_ctl(codec, "Master Playback Volume");
	if (!kctl)
		return;
	uctl = kzalloc(sizeof(*uctl), GFP_KERNEL);
	if (!uctl)
		return;
	val = snd_hda_codec_read(codec, nid, 0,
				 AC_VERB_GET_VOLUME_KNOB_CONTROL, 0);
	val &= HDA_AMP_VOLMASK;
	uctl->value.integer.value[0] = val;
	uctl->value.integer.value[1] = val;
	kctl->put(kctl, uctl);
	kfree(uctl);
}

/* unsolicited event for HP jack sensing */
static void alc_sku_unsol_event(struct hda_codec *codec, unsigned int res)
{
	int action;

	if (codec->vendor_id == 0x10ec0880)
		res >>= 28;
	else
		res >>= 26;
	action = snd_hda_jack_get_action(codec, res);
	if (action == ALC_DCVOL_EVENT) {
		/* Execute the dc-vol event here as it requires the NID
		 * but we don't pass NID to alc_exec_unsol_event().
		 * Once when we convert all static quirks to the auto-parser,
		 * this can be integerated into there.
		 */
		struct hda_jack_tbl *jack;
		jack = snd_hda_jack_tbl_get_from_tag(codec, res);
		if (jack)
			alc_update_knob_master(codec, jack->nid);
		return;
	}
	alc_exec_unsol_event(codec, action);
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
		"Disabled", "Speaker Only", "Line Out+Speaker"
	};
	const char * const *texts;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	if (spec->automute_speaker_possible && spec->automute_lo_possible) {
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
	unsigned int val = 0;
	if (spec->automute_speaker)
		val++;
	if (spec->automute_lo)
		val++;

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
		if (!spec->automute_speaker && !spec->automute_lo)
			return 0;
		spec->automute_speaker = 0;
		spec->automute_lo = 0;
		break;
	case 1:
		if (spec->automute_speaker_possible) {
			if (!spec->automute_lo && spec->automute_speaker)
				return 0;
			spec->automute_speaker = 1;
			spec->automute_lo = 0;
		} else if (spec->automute_lo_possible) {
			if (spec->automute_lo)
				return 0;
			spec->automute_lo = 1;
		} else
			return -EINVAL;
		break;
	case 2:
		if (!spec->automute_lo_possible || !spec->automute_speaker_possible)
			return -EINVAL;
		if (spec->automute_speaker && spec->automute_lo)
			return 0;
		spec->automute_speaker = 1;
		spec->automute_lo = 1;
		break;
	default:
		return -EINVAL;
	}
	call_update_outputs(codec);
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
static void alc_init_automute(struct hda_codec *codec)
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

	spec->automute_mode = ALC_AUTOMUTE_PIN;

	for (i = 0; i < cfg->hp_outs; i++) {
		hda_nid_t nid = cfg->hp_pins[i];
		if (!is_jack_detectable(codec, nid))
			continue;
		snd_printdd("realtek: Enable HP auto-muting on NID 0x%x\n",
			    nid);
		snd_hda_jack_detect_enable(codec, nid, ALC_HP_EVENT);
		spec->detect_hp = 1;
	}

	if (cfg->line_out_type == AUTO_PIN_LINE_OUT && cfg->line_outs) {
		if (cfg->speaker_outs)
			for (i = 0; i < cfg->line_outs; i++) {
				hda_nid_t nid = cfg->line_out_pins[i];
				if (!is_jack_detectable(codec, nid))
					continue;
				snd_printdd("realtek: Enable Line-Out "
					    "auto-muting on NID 0x%x\n", nid);
				snd_hda_jack_detect_enable(codec, nid,
							   ALC_FRONT_EVENT);
				spec->detect_lo = 1;
		}
		spec->automute_lo_possible = spec->detect_hp;
	}

	spec->automute_speaker_possible = cfg->speaker_outs &&
		(spec->detect_hp || spec->detect_lo);

	spec->automute_lo = spec->automute_lo_possible;
	spec->automute_speaker = spec->automute_speaker_possible;

	if (spec->automute_speaker_possible || spec->automute_lo_possible) {
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

	snd_hda_jack_detect_enable(codec, spec->ext_mic_pin, ALC_MIC_EVENT);
	if (spec->dock_mic_pin)
		snd_hda_jack_detect_enable(codec, spec->dock_mic_pin,
					   ALC_MIC_EVENT);

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

	if (spec->shared_mic_hp)
		return; /* no auto-mic for the shared I/O */

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
	alc_init_automute(codec);
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
	if (!spec->autocfg.hp_pins[0] &&
	    !(spec->autocfg.line_out_pins[0] &&
	      spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)) {
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
	ALC_FIXUP_ACT_BUILD,
};

static void alc_apply_pincfgs(struct hda_codec *codec,
			      const struct alc_pincfg *cfg)
{
	for (; cfg->nid; cfg++)
		snd_hda_codec_set_pincfg(codec, cfg->nid, cfg->val);
}

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
				break;
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
			alc_apply_pincfgs(codec, cfg);
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
	const struct snd_pci_quirk *q;
	int id = -1;
	const char *name = NULL;

	/* when model=nofixup is given, don't pick up any fixups */
	if (codec->modelname && !strcmp(codec->modelname, "nofixup")) {
		spec->fixup_list = NULL;
		spec->fixup_id = -1;
		return;
	}

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
		q = snd_pci_quirk_lookup(codec->bus->pci, quirk);
		if (q) {
			id = q->value;
#ifdef CONFIG_SND_DEBUG_VERBOSE
			name = q->name;
#endif
		}
	}
	if (id < 0) {
		for (q = quirk; q->subvendor; q++) {
			unsigned int vendorid =
				q->subdevice | (q->subvendor << 16);
			if (vendorid == codec->subsystem_id) {
				id = q->value;
#ifdef CONFIG_SND_DEBUG_VERBOSE
				name = q->name;
#endif
				break;
			}
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

/* a special bypass for COEF 0; read the cached value at the second time */
static unsigned int alc_get_coef0(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	if (!spec->coef0)
		spec->coef0 = alc_read_coef_idx(codec, 0);
	return spec->coef0;
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
	int i, err, nums;
	hda_nid_t dig_nid;

	/* support multiple SPDIFs; the secondary is set up as a slave */
	nums = 0;
	for (i = 0; i < spec->autocfg.dig_outs; i++) {
		hda_nid_t conn[4];
		err = snd_hda_get_connections(codec,
					      spec->autocfg.dig_out_pins[i],
					      conn, ARRAY_SIZE(conn));
		if (err <= 0)
			continue;
		dig_nid = conn[0]; /* assume the first element is audio-out */
		if (!nums) {
			spec->multiout.dig_out_nid = dig_nid;
			spec->dig_out_type = spec->autocfg.dig_out_type[0];
		} else {
			spec->multiout.slave_dig_outs = spec->slave_dig_outs;
			if (nums >= ARRAY_SIZE(spec->slave_dig_outs) - 1)
				break;
			spec->slave_dig_outs[nums - 1] = dig_nid;
		}
		nums++;
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
static const char * const alc_slave_pfxs[] = {
	"Front", "Surround", "Center", "LFE", "Side",
	"Headphone", "Speaker", "Mono", "Line Out",
	"CLFE", "Bass Speaker", "PCM",
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

static int __alc_build_controls(struct hda_codec *codec)
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
					  vmaster_tlv, alc_slave_pfxs,
					  "Playback Volume");
		if (err < 0)
			return err;
	}
	if (!spec->no_analog &&
	    !snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = __snd_hda_add_vmaster(codec, "Master Playback Switch",
					    NULL, alc_slave_pfxs,
					    "Playback Switch",
					    true, &spec->vmaster_mute.sw_kctl);
		if (err < 0)
			return err;
	}

	/* assign Capture Source enums to NID */
	if (spec->capsrc_nids || spec->adc_nids) {
		kctl = snd_hda_find_mixer_ctl(codec, "Capture Source");
		if (!kctl)
			kctl = snd_hda_find_mixer_ctl(codec, "Input Source");
		for (i = 0; kctl && i < kctl->count; i++) {
			err = snd_hda_add_nid(codec, kctl, i,
					      get_capsrc(spec, i));
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

static int alc_build_controls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err = __alc_build_controls(codec);
	if (err < 0)
		return err;
	err = snd_hda_jack_add_kctls(codec, &spec->autocfg);
	if (err < 0)
		return err;
	alc_apply_fixup(codec, ALC_FIXUP_ACT_BUILD);
	return 0;
}


/*
 * Common callbacks
 */

static void alc_init_special_input_src(struct hda_codec *codec);
static void alc_auto_init_std(struct hda_codec *codec);

static int alc_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int i;

	if (spec->init_hook)
		spec->init_hook(codec);

	alc_fix_pll(codec);
	alc_auto_init_amp(codec, spec->init_amp);

	for (i = 0; i < spec->num_init_verbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);
	alc_init_special_input_src(codec);
	alc_auto_init_std(codec);

	alc_apply_fixup(codec, ALC_FIXUP_ACT_INIT);

	snd_hda_jack_report_sync(codec);

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
	bool have_multi_adcs;
	int i;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	if (spec->no_analog)
		goto skip_analog;

	snprintf(spec->stream_name_analog, sizeof(spec->stream_name_analog),
		 "%s Analog", codec->chip_name);
	info->name = spec->stream_name_analog;

	if (spec->multiout.num_dacs > 0) {
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
	have_multi_adcs = (spec->num_adc_nids > 1) &&
		!spec->dyn_adc_switch && !spec->auto_mic &&
		(!spec->input_mux || spec->input_mux->num_items > 1);
	/* Additional Analaog capture for index #2 */
	if (spec->alt_dac_nid || have_multi_adcs) {
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
		if (have_multi_adcs) {
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

static void alc_free_bind_ctls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	if (spec->bind_ctls.list) {
		struct hda_bind_ctls **ctl = spec->bind_ctls.list;
		int i;
		for (i = 0; i < spec->bind_ctls.used; i++)
			kfree(ctl[i]);
	}
	snd_array_free(&spec->bind_ctls);
}

static void alc_free(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec)
		return;

	alc_shutup(codec);
	alc_free_kctls(codec);
	alc_free_bind_ctls(codec);
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
 * Rename codecs appropriately from COEF value
 */
struct alc_codec_rename_table {
	unsigned int vendor_id;
	unsigned short coef_mask;
	unsigned short coef_bits;
	const char *name;
};

static struct alc_codec_rename_table rename_tbl[] = {
	{ 0x10ec0269, 0xfff0, 0x3010, "ALC277" },
	{ 0x10ec0269, 0xf0f0, 0x2010, "ALC259" },
	{ 0x10ec0269, 0xf0f0, 0x3010, "ALC258" },
	{ 0x10ec0269, 0x00f0, 0x0010, "ALC269VB" },
	{ 0x10ec0269, 0xffff, 0xa023, "ALC259" },
	{ 0x10ec0269, 0xffff, 0x6023, "ALC281X" },
	{ 0x10ec0269, 0x00f0, 0x0020, "ALC269VC" },
	{ 0x10ec0887, 0x00f0, 0x0030, "ALC887-VD" },
	{ 0x10ec0888, 0x00f0, 0x0030, "ALC888-VD" },
	{ 0x10ec0888, 0xf0f0, 0x3020, "ALC886" },
	{ 0x10ec0899, 0x2000, 0x2000, "ALC899" },
	{ 0x10ec0892, 0xffff, 0x8020, "ALC661" },
	{ 0x10ec0892, 0xffff, 0x8011, "ALC661" },
	{ 0x10ec0892, 0xffff, 0x4011, "ALC656" },
	{ } /* terminator */
};

static int alc_codec_rename_from_preset(struct hda_codec *codec)
{
	const struct alc_codec_rename_table *p;

	for (p = rename_tbl; p->vendor_id; p++) {
		if (p->vendor_id != codec->vendor_id)
			continue;
		if ((alc_get_coef0(codec) & p->coef_mask) == p->coef_bits)
			return alc_codec_rename(codec, p->name);
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
	ALC_CTL_BIND_VOL,
	ALC_CTL_BIND_SW,
};
static const struct snd_kcontrol_new alc_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	HDA_BIND_MUTE(NULL, 0, 0, 0),
	HDA_BIND_VOL(NULL, 0),
	HDA_BIND_SW(NULL, 0),
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

static const char * const channel_name[4] = {
	"Front", "Surround", "CLFE", "Side"
};

static const char *alc_get_line_out_pfx(struct alc_spec *spec, int ch,
					bool can_be_master, int *index)
{
	struct auto_pin_cfg *cfg = &spec->autocfg;

	*index = 0;
	if (cfg->line_outs == 1 && !spec->multi_ios &&
	    !cfg->hp_outs && !cfg->speaker_outs && can_be_master)
		return "Master";

	switch (cfg->line_out_type) {
	case AUTO_PIN_SPEAKER_OUT:
		if (cfg->line_outs == 1)
			return "Speaker";
		if (cfg->line_outs == 2)
			return ch ? "Bass Speaker" : "Speaker";
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
	if (snd_BUG_ON(ch >= ARRAY_SIZE(channel_name)))
		return "PCM";

	return channel_name[ch];
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
/* add the powersave loopback-list entry */
static void add_loopback_list(struct alc_spec *spec, hda_nid_t mix, int idx)
{
	struct hda_amp_list *list;

	if (spec->num_loopbacks >= ARRAY_SIZE(spec->loopback_list) - 1)
		return;
	list = spec->loopback_list + spec->num_loopbacks;
	list->nid = mix;
	list->dir = HDA_INPUT;
	list->idx = idx;
	spec->num_loopbacks++;
	spec->loopback.amplist = spec->loopback_list;
}
#else
#define add_loopback_list(spec, mix, idx) /* NOP */
#endif

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
	add_loopback_list(spec, mix_nid, idx);
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
				break;
			}
			n = snd_hda_get_conn_list(codec, src, &list);
			if (n > 1) {
				cap_nids[nums] = src;
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
		if (spec->shared_mic_hp && !strcmp(label, "Misc"))
			label = "Headphone Mic";
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
			hda_nid_t cap = get_capsrc(spec, c);
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

/* create a shared input with the headphone out */
static int alc_auto_create_shared_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int defcfg;
	hda_nid_t nid;

	/* only one internal input pin? */
	if (cfg->num_inputs != 1)
		return 0;
	defcfg = snd_hda_codec_get_pincfg(codec, cfg->inputs[0].pin);
	if (snd_hda_get_input_pin_attr(defcfg) != INPUT_PIN_ATTR_INT)
		return 0;

	if (cfg->hp_outs == 1 && cfg->line_out_type == AUTO_PIN_SPEAKER_OUT)
		nid = cfg->hp_pins[0]; /* OK, we have a single HP-out */
	else if (cfg->line_outs == 1 && cfg->line_out_type == AUTO_PIN_HP_OUT)
		nid = cfg->line_out_pins[0]; /* OK, we have a single line-out */
	else
		return 0; /* both not available */

	if (!(snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_IN))
		return 0; /* no input */

	cfg->inputs[1].pin = nid;
	cfg->inputs[1].type = AUTO_PIN_MIC;
	cfg->num_inputs = 2;
	spec->shared_mic_hp = 1;
	snd_printdd("realtek: Enable shared I/O jack on NID 0x%x\n", nid);
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

static bool alc_is_dac_already_used(struct hda_codec *codec, hda_nid_t nid)
{
	struct alc_spec *spec = codec->spec;
	int i;
	if (found_in_nid_list(nid, spec->multiout.dac_nids,
			      ARRAY_SIZE(spec->private_dac_nids)) ||
	    found_in_nid_list(nid, spec->multiout.hp_out_nid,
			      ARRAY_SIZE(spec->multiout.hp_out_nid)) ||
	    found_in_nid_list(nid, spec->multiout.extra_out_nid,
			      ARRAY_SIZE(spec->multiout.extra_out_nid)))
		return true;
	for (i = 0; i < spec->multi_ios; i++) {
		if (spec->multi_io[i].dac == nid)
			return true;
	}
	return false;
}

/* look for an empty DAC slot */
static hda_nid_t alc_auto_look_for_dac(struct hda_codec *codec, hda_nid_t pin)
{
	hda_nid_t srcs[5];
	int i, num;

	pin = alc_go_down_to_selector(codec, pin);
	num = snd_hda_get_connections(codec, pin, srcs, ARRAY_SIZE(srcs));
	for (i = 0; i < num; i++) {
		hda_nid_t nid = alc_auto_mix_to_dac(codec, srcs[i]);
		if (!nid)
			continue;
		if (!alc_is_dac_already_used(codec, nid))
			return nid;
	}
	return 0;
}

/* check whether the DAC is reachable from the pin */
static bool alc_auto_is_dac_reachable(struct hda_codec *codec,
				      hda_nid_t pin, hda_nid_t dac)
{
	hda_nid_t srcs[5];
	int i, num;

	if (!pin || !dac)
		return false;
	pin = alc_go_down_to_selector(codec, pin);
	num = snd_hda_get_connections(codec, pin, srcs, ARRAY_SIZE(srcs));
	for (i = 0; i < num; i++) {
		hda_nid_t nid = alc_auto_mix_to_dac(codec, srcs[i]);
		if (nid == dac)
			return true;
	}
	return false;
}

static hda_nid_t get_dac_if_single(struct hda_codec *codec, hda_nid_t pin)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t sel = alc_go_down_to_selector(codec, pin);
	hda_nid_t nid, nid_found, srcs[5];
	int i, num = snd_hda_get_connections(codec, sel, srcs,
					  ARRAY_SIZE(srcs));
	if (num == 1)
		return alc_auto_look_for_dac(codec, pin);
	nid_found = 0;
	for (i = 0; i < num; i++) {
		if (srcs[i] == spec->mixer_nid)
			continue;
		nid = alc_auto_mix_to_dac(codec, srcs[i]);
		if (nid && !alc_is_dac_already_used(codec, nid)) {
			if (nid_found)
				return 0;
			nid_found = nid;
		}
	}
	return nid_found;
}

/* mark up volume and mute control NIDs: used during badness parsing and
 * at creating actual controls
 */
static inline unsigned int get_ctl_pos(unsigned int data)
{
	hda_nid_t nid = get_amp_nid_(data);
	unsigned int dir;
	if (snd_BUG_ON(nid >= MAX_VOL_NIDS))
		return 0;
	dir = get_amp_direction_(data);
	return (nid << 1) | dir;
}

#define is_ctl_used(bits, data) \
	test_bit(get_ctl_pos(data), bits)
#define mark_ctl_usage(bits, data) \
	set_bit(get_ctl_pos(data), bits)

static void clear_vol_marks(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	memset(spec->vol_ctls, 0, sizeof(spec->vol_ctls));
	memset(spec->sw_ctls, 0, sizeof(spec->sw_ctls));
}

/* badness definition */
enum {
	/* No primary DAC is found for the main output */
	BAD_NO_PRIMARY_DAC = 0x10000,
	/* No DAC is found for the extra output */
	BAD_NO_DAC = 0x4000,
	/* No possible multi-ios */
	BAD_MULTI_IO = 0x103,
	/* No individual DAC for extra output */
	BAD_NO_EXTRA_DAC = 0x102,
	/* No individual DAC for extra surrounds */
	BAD_NO_EXTRA_SURR_DAC = 0x101,
	/* Primary DAC shared with main surrounds */
	BAD_SHARED_SURROUND = 0x100,
	/* Primary DAC shared with main CLFE */
	BAD_SHARED_CLFE = 0x10,
	/* Primary DAC shared with extra surrounds */
	BAD_SHARED_EXTRA_SURROUND = 0x10,
	/* Volume widget is shared */
	BAD_SHARED_VOL = 0x10,
};

static hda_nid_t alc_look_for_out_mute_nid(struct hda_codec *codec,
					   hda_nid_t pin, hda_nid_t dac);
static hda_nid_t alc_look_for_out_vol_nid(struct hda_codec *codec,
					  hda_nid_t pin, hda_nid_t dac);

static int eval_shared_vol_badness(struct hda_codec *codec, hda_nid_t pin,
				   hda_nid_t dac)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid;
	unsigned int val;
	int badness = 0;

	nid = alc_look_for_out_vol_nid(codec, pin, dac);
	if (nid) {
		val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
		if (is_ctl_used(spec->vol_ctls, nid))
			badness += BAD_SHARED_VOL;
		else
			mark_ctl_usage(spec->vol_ctls, val);
	} else
		badness += BAD_SHARED_VOL;
	nid = alc_look_for_out_mute_nid(codec, pin, dac);
	if (nid) {
		unsigned int wid_type = get_wcaps_type(get_wcaps(codec, nid));
		if (wid_type == AC_WID_PIN || wid_type == AC_WID_AUD_OUT)
			val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
		else
			val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT);
		if (is_ctl_used(spec->sw_ctls, val))
			badness += BAD_SHARED_VOL;
		else
			mark_ctl_usage(spec->sw_ctls, val);
	} else
		badness += BAD_SHARED_VOL;
	return badness;
}

struct badness_table {
	int no_primary_dac;	/* no primary DAC */
	int no_dac;		/* no secondary DACs */
	int shared_primary;	/* primary DAC is shared with main output */
	int shared_surr;	/* secondary DAC shared with main or primary */
	int shared_clfe;	/* third DAC shared with main or primary */
	int shared_surr_main;	/* secondary DAC sahred with main/DAC0 */
};

static struct badness_table main_out_badness = {
	.no_primary_dac = BAD_NO_PRIMARY_DAC,
	.no_dac = BAD_NO_DAC,
	.shared_primary = BAD_NO_PRIMARY_DAC,
	.shared_surr = BAD_SHARED_SURROUND,
	.shared_clfe = BAD_SHARED_CLFE,
	.shared_surr_main = BAD_SHARED_SURROUND,
};

static struct badness_table extra_out_badness = {
	.no_primary_dac = BAD_NO_DAC,
	.no_dac = BAD_NO_DAC,
	.shared_primary = BAD_NO_EXTRA_DAC,
	.shared_surr = BAD_SHARED_EXTRA_SURROUND,
	.shared_clfe = BAD_SHARED_EXTRA_SURROUND,
	.shared_surr_main = BAD_NO_EXTRA_SURR_DAC,
};

/* try to assign DACs to pins and return the resultant badness */
static int alc_auto_fill_dacs(struct hda_codec *codec, int num_outs,
			      const hda_nid_t *pins, hda_nid_t *dacs,
			      const struct badness_table *bad)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, j;
	int badness = 0;
	hda_nid_t dac;

	if (!num_outs)
		return 0;

	for (i = 0; i < num_outs; i++) {
		hda_nid_t pin = pins[i];
		if (!dacs[i])
			dacs[i] = alc_auto_look_for_dac(codec, pin);
		if (!dacs[i] && !i) {
			for (j = 1; j < num_outs; j++) {
				if (alc_auto_is_dac_reachable(codec, pin, dacs[j])) {
					dacs[0] = dacs[j];
					dacs[j] = 0;
					break;
				}
			}
		}
		dac = dacs[i];
		if (!dac) {
			if (alc_auto_is_dac_reachable(codec, pin, dacs[0]))
				dac = dacs[0];
			else if (cfg->line_outs > i &&
				 alc_auto_is_dac_reachable(codec, pin,
					spec->private_dac_nids[i]))
				dac = spec->private_dac_nids[i];
			if (dac) {
				if (!i)
					badness += bad->shared_primary;
				else if (i == 1)
					badness += bad->shared_surr;
				else
					badness += bad->shared_clfe;
			} else if (alc_auto_is_dac_reachable(codec, pin,
					spec->private_dac_nids[0])) {
				dac = spec->private_dac_nids[0];
				badness += bad->shared_surr_main;
			} else if (!i)
				badness += bad->no_primary_dac;
			else
				badness += bad->no_dac;
		}
		if (dac)
			badness += eval_shared_vol_badness(codec, pin, dac);
	}

	return badness;
}

static int alc_auto_fill_multi_ios(struct hda_codec *codec,
				   hda_nid_t reference_pin,
				   bool hardwired, int offset);

static bool alc_map_singles(struct hda_codec *codec, int outs,
			    const hda_nid_t *pins, hda_nid_t *dacs)
{
	int i;
	bool found = false;
	for (i = 0; i < outs; i++) {
		if (dacs[i])
			continue;
		dacs[i] = get_dac_if_single(codec, pins[i]);
		if (dacs[i])
			found = true;
	}
	return found;
}

/* fill in the dac_nids table from the parsed pin configuration */
static int fill_and_eval_dacs(struct hda_codec *codec,
			      bool fill_hardwired,
			      bool fill_mio_first)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, err, badness;

	/* set num_dacs once to full for alc_auto_look_for_dac() */
	spec->multiout.num_dacs = cfg->line_outs;
	spec->multiout.dac_nids = spec->private_dac_nids;
	memset(spec->private_dac_nids, 0, sizeof(spec->private_dac_nids));
	memset(spec->multiout.hp_out_nid, 0, sizeof(spec->multiout.hp_out_nid));
	memset(spec->multiout.extra_out_nid, 0, sizeof(spec->multiout.extra_out_nid));
	spec->multi_ios = 0;
	clear_vol_marks(codec);
	badness = 0;

	/* fill hard-wired DACs first */
	if (fill_hardwired) {
		bool mapped;
		do {
			mapped = alc_map_singles(codec, cfg->line_outs,
						 cfg->line_out_pins,
						 spec->private_dac_nids);
			mapped |= alc_map_singles(codec, cfg->hp_outs,
						  cfg->hp_pins,
						  spec->multiout.hp_out_nid);
			mapped |= alc_map_singles(codec, cfg->speaker_outs,
						  cfg->speaker_pins,
						  spec->multiout.extra_out_nid);
			if (fill_mio_first && cfg->line_outs == 1 &&
			    cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
				err = alc_auto_fill_multi_ios(codec, cfg->line_out_pins[0], true, 0);
				if (!err)
					mapped = true;
			}
		} while (mapped);
	}

	badness += alc_auto_fill_dacs(codec, cfg->line_outs, cfg->line_out_pins,
				      spec->private_dac_nids,
				      &main_out_badness);

	/* re-count num_dacs and squash invalid entries */
	spec->multiout.num_dacs = 0;
	for (i = 0; i < cfg->line_outs; i++) {
		if (spec->private_dac_nids[i])
			spec->multiout.num_dacs++;
		else {
			memmove(spec->private_dac_nids + i,
				spec->private_dac_nids + i + 1,
				sizeof(hda_nid_t) * (cfg->line_outs - i - 1));
			spec->private_dac_nids[cfg->line_outs - 1] = 0;
		}
	}

	if (fill_mio_first &&
	    cfg->line_outs == 1 && cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		/* try to fill multi-io first */
		err = alc_auto_fill_multi_ios(codec, cfg->line_out_pins[0], false, 0);
		if (err < 0)
			return err;
		/* we don't count badness at this stage yet */
	}

	if (cfg->line_out_type != AUTO_PIN_HP_OUT) {
		err = alc_auto_fill_dacs(codec, cfg->hp_outs, cfg->hp_pins,
					 spec->multiout.hp_out_nid,
					 &extra_out_badness);
		if (err < 0)
			return err;
		badness += err;
	}
	if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		err = alc_auto_fill_dacs(codec, cfg->speaker_outs,
					 cfg->speaker_pins,
					 spec->multiout.extra_out_nid,
					 &extra_out_badness);
		if (err < 0)
			return err;
		badness += err;
	}
	if (cfg->line_outs == 1 && cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		err = alc_auto_fill_multi_ios(codec, cfg->line_out_pins[0], false, 0);
		if (err < 0)
			return err;
		badness += err;
	}
	if (cfg->hp_outs && cfg->line_out_type == AUTO_PIN_SPEAKER_OUT) {
		/* try multi-ios with HP + inputs */
		int offset = 0;
		if (cfg->line_outs >= 3)
			offset = 1;
		err = alc_auto_fill_multi_ios(codec, cfg->hp_pins[0], false,
					      offset);
		if (err < 0)
			return err;
		badness += err;
	}

	if (spec->multi_ios == 2) {
		for (i = 0; i < 2; i++)
			spec->private_dac_nids[spec->multiout.num_dacs++] =
				spec->multi_io[i].dac;
		spec->ext_channel_count = 2;
	} else if (spec->multi_ios) {
		spec->multi_ios = 0;
		badness += BAD_MULTI_IO;
	}

	return badness;
}

#define DEBUG_BADNESS

#ifdef DEBUG_BADNESS
#define debug_badness	snd_printdd
#else
#define debug_badness(...)
#endif

static void debug_show_configs(struct alc_spec *spec, struct auto_pin_cfg *cfg)
{
	debug_badness("multi_outs = %x/%x/%x/%x : %x/%x/%x/%x\n",
		      cfg->line_out_pins[0], cfg->line_out_pins[1],
		      cfg->line_out_pins[2], cfg->line_out_pins[2],
		      spec->multiout.dac_nids[0],
		      spec->multiout.dac_nids[1],
		      spec->multiout.dac_nids[2],
		      spec->multiout.dac_nids[3]);
	if (spec->multi_ios > 0)
		debug_badness("multi_ios(%d) = %x/%x : %x/%x\n",
			      spec->multi_ios,
			      spec->multi_io[0].pin, spec->multi_io[1].pin,
			      spec->multi_io[0].dac, spec->multi_io[1].dac);
	debug_badness("hp_outs = %x/%x/%x/%x : %x/%x/%x/%x\n",
		      cfg->hp_pins[0], cfg->hp_pins[1],
		      cfg->hp_pins[2], cfg->hp_pins[2],
		      spec->multiout.hp_out_nid[0],
		      spec->multiout.hp_out_nid[1],
		      spec->multiout.hp_out_nid[2],
		      spec->multiout.hp_out_nid[3]);
	debug_badness("spk_outs = %x/%x/%x/%x : %x/%x/%x/%x\n",
		      cfg->speaker_pins[0], cfg->speaker_pins[1],
		      cfg->speaker_pins[2], cfg->speaker_pins[3],
		      spec->multiout.extra_out_nid[0],
		      spec->multiout.extra_out_nid[1],
		      spec->multiout.extra_out_nid[2],
		      spec->multiout.extra_out_nid[3]);
}

static int alc_auto_fill_dac_nids(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct auto_pin_cfg *best_cfg;
	int best_badness = INT_MAX;
	int badness;
	bool fill_hardwired = true, fill_mio_first = true;
	bool best_wired = true, best_mio = true;
	bool hp_spk_swapped = false;

	best_cfg = kmalloc(sizeof(*best_cfg), GFP_KERNEL);
	if (!best_cfg)
		return -ENOMEM;
	*best_cfg = *cfg;

	for (;;) {
		badness = fill_and_eval_dacs(codec, fill_hardwired,
					     fill_mio_first);
		if (badness < 0) {
			kfree(best_cfg);
			return badness;
		}
		debug_badness("==> lo_type=%d, wired=%d, mio=%d, badness=0x%x\n",
			      cfg->line_out_type, fill_hardwired, fill_mio_first,
			      badness);
		debug_show_configs(spec, cfg);
		if (badness < best_badness) {
			best_badness = badness;
			*best_cfg = *cfg;
			best_wired = fill_hardwired;
			best_mio = fill_mio_first;
		}
		if (!badness)
			break;
		fill_mio_first = !fill_mio_first;
		if (!fill_mio_first)
			continue;
		fill_hardwired = !fill_hardwired;
		if (!fill_hardwired)
			continue;
		if (hp_spk_swapped)
			break;
		hp_spk_swapped = true;
		if (cfg->speaker_outs > 0 &&
		    cfg->line_out_type == AUTO_PIN_HP_OUT) {
			cfg->hp_outs = cfg->line_outs;
			memcpy(cfg->hp_pins, cfg->line_out_pins,
			       sizeof(cfg->hp_pins));
			cfg->line_outs = cfg->speaker_outs;
			memcpy(cfg->line_out_pins, cfg->speaker_pins,
			       sizeof(cfg->speaker_pins));
			cfg->speaker_outs = 0;
			memset(cfg->speaker_pins, 0, sizeof(cfg->speaker_pins));
			cfg->line_out_type = AUTO_PIN_SPEAKER_OUT;
			fill_hardwired = true;
			continue;
		}
		if (cfg->hp_outs > 0 &&
		    cfg->line_out_type == AUTO_PIN_SPEAKER_OUT) {
			cfg->speaker_outs = cfg->line_outs;
			memcpy(cfg->speaker_pins, cfg->line_out_pins,
			       sizeof(cfg->speaker_pins));
			cfg->line_outs = cfg->hp_outs;
			memcpy(cfg->line_out_pins, cfg->hp_pins,
			       sizeof(cfg->hp_pins));
			cfg->hp_outs = 0;
			memset(cfg->hp_pins, 0, sizeof(cfg->hp_pins));
			cfg->line_out_type = AUTO_PIN_HP_OUT;
			fill_hardwired = true;
			continue;
		}
		break;
	}

	if (badness) {
		*cfg = *best_cfg;
		fill_and_eval_dacs(codec, best_wired, best_mio);
	}
	debug_badness("==> Best config: lo_type=%d, wired=%d, mio=%d\n",
		      cfg->line_out_type, best_wired, best_mio);
	debug_show_configs(spec, cfg);

	if (cfg->line_out_pins[0])
		spec->vmaster_nid =
			alc_look_for_out_vol_nid(codec, cfg->line_out_pins[0],
						 spec->multiout.dac_nids[0]);

	/* clear the bitmap flags for creating controls */
	clear_vol_marks(codec);
	kfree(best_cfg);
	return 0;
}

static int alc_auto_add_vol_ctl(struct hda_codec *codec,
			      const char *pfx, int cidx,
			      hda_nid_t nid, unsigned int chs)
{
	struct alc_spec *spec = codec->spec;
	unsigned int val;
	if (!nid)
		return 0;
	val = HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT);
	if (is_ctl_used(spec->vol_ctls, val) && chs != 2) /* exclude LFE */
		return 0;
	mark_ctl_usage(spec->vol_ctls, val);
	return __add_pb_vol_ctrl(codec->spec, ALC_CTL_WIDGET_VOL, pfx, cidx,
				 val);
}

static int alc_auto_add_stereo_vol(struct hda_codec *codec,
				   const char *pfx, int cidx,
				   hda_nid_t nid)
{
	int chs = 1;
	if (get_wcaps(codec, nid) & AC_WCAP_STEREO)
		chs = 3;
	return alc_auto_add_vol_ctl(codec, pfx, cidx, nid, chs);
}

/* create a mute-switch for the given mixer widget;
 * if it has multiple sources (e.g. DAC and loopback), create a bind-mute
 */
static int alc_auto_add_sw_ctl(struct hda_codec *codec,
			     const char *pfx, int cidx,
			     hda_nid_t nid, unsigned int chs)
{
	struct alc_spec *spec = codec->spec;
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
	if (is_ctl_used(spec->sw_ctls, val) && chs != 2) /* exclude LFE */
		return 0;
	mark_ctl_usage(spec->sw_ctls, val);
	return __add_pb_sw_ctrl(codec->spec, type, pfx, cidx, val);
}

static int alc_auto_add_stereo_sw(struct hda_codec *codec, const char *pfx,
				  int cidx, hda_nid_t nid)
{
	int chs = 1;
	if (get_wcaps(codec, nid) & AC_WCAP_STEREO)
		chs = 3;
	return alc_auto_add_sw_ctl(codec, pfx, cidx, nid, chs);
}

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
	if (spec->multi_ios > 0 && cfg->line_outs < 3)
		noutputs += spec->multi_ios;

	for (i = 0; i < noutputs; i++) {
		const char *name;
		int index;
		hda_nid_t dac, pin;
		hda_nid_t sw, vol;

		dac = spec->multiout.dac_nids[i];
		if (!dac)
			continue;
		if (i >= cfg->line_outs) {
			pin = spec->multi_io[i - 1].pin;
			index = 0;
			name = channel_name[i];
		} else {
			pin = cfg->line_out_pins[i];
			name = alc_get_line_out_pfx(spec, i, true, &index);
		}

		sw = alc_look_for_out_mute_nid(codec, pin, dac);
		vol = alc_look_for_out_vol_nid(codec, pin, dac);
		if (!name || !strcmp(name, "CLFE")) {
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

static int alc_auto_create_extra_out(struct hda_codec *codec, hda_nid_t pin,
				     hda_nid_t dac, const char *pfx,
				     int cidx)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t sw, vol;
	int err;

	if (!dac) {
		unsigned int val;
		/* the corresponding DAC is already occupied */
		if (!(get_wcaps(codec, pin) & AC_WCAP_OUT_AMP))
			return 0; /* no way */
		/* create a switch only */
		val = HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT);
		if (is_ctl_used(spec->sw_ctls, val))
			return 0; /* already created */
		mark_ctl_usage(spec->sw_ctls, val);
		return __add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, pfx, cidx, val);
	}

	sw = alc_look_for_out_mute_nid(codec, pin, dac);
	vol = alc_look_for_out_vol_nid(codec, pin, dac);
	err = alc_auto_add_stereo_vol(codec, pfx, cidx, vol);
	if (err < 0)
		return err;
	err = alc_auto_add_stereo_sw(codec, pfx, cidx, sw);
	if (err < 0)
		return err;
	return 0;
}

static struct hda_bind_ctls *new_bind_ctl(struct hda_codec *codec,
					  unsigned int nums,
					  struct hda_ctl_ops *ops)
{
	struct alc_spec *spec = codec->spec;
	struct hda_bind_ctls **ctlp, *ctl;
	snd_array_init(&spec->bind_ctls, sizeof(ctl), 8);
	ctlp = snd_array_new(&spec->bind_ctls);
	if (!ctlp)
		return NULL;
	ctl = kzalloc(sizeof(*ctl) + sizeof(long) * (nums + 1), GFP_KERNEL);
	*ctlp = ctl;
	if (ctl)
		ctl->ops = ops;
	return ctl;
}

/* add playback controls for speaker and HP outputs */
static int alc_auto_create_extra_outs(struct hda_codec *codec, int num_pins,
				      const hda_nid_t *pins,
				      const hda_nid_t *dacs,
				      const char *pfx)
{
	struct alc_spec *spec = codec->spec;
	struct hda_bind_ctls *ctl;
	char name[32];
	int i, n, err;

	if (!num_pins || !pins[0])
		return 0;

	if (num_pins == 1) {
		hda_nid_t dac = *dacs;
		if (!dac)
			dac = spec->multiout.dac_nids[0];
		return alc_auto_create_extra_out(codec, *pins, dac, pfx, 0);
	}

	for (i = 0; i < num_pins; i++) {
		hda_nid_t dac;
		if (dacs[num_pins - 1])
			dac = dacs[i]; /* with individual volumes */
		else
			dac = 0;
		if (num_pins == 2 && i == 1 && !strcmp(pfx, "Speaker")) {
			err = alc_auto_create_extra_out(codec, pins[i], dac,
							"Bass Speaker", 0);
		} else if (num_pins >= 3) {
			snprintf(name, sizeof(name), "%s %s",
				 pfx, channel_name[i]);
			err = alc_auto_create_extra_out(codec, pins[i], dac,
							name, 0);
		} else {
			err = alc_auto_create_extra_out(codec, pins[i], dac,
							pfx, i);
		}
		if (err < 0)
			return err;
	}
	if (dacs[num_pins - 1])
		return 0;

	/* Let's create a bind-controls for volumes */
	ctl = new_bind_ctl(codec, num_pins, &snd_hda_bind_vol);
	if (!ctl)
		return -ENOMEM;
	n = 0;
	for (i = 0; i < num_pins; i++) {
		hda_nid_t vol;
		if (!pins[i] || !dacs[i])
			continue;
		vol = alc_look_for_out_vol_nid(codec, pins[i], dacs[i]);
		if (vol)
			ctl->values[n++] =
				HDA_COMPOSE_AMP_VAL(vol, 3, 0, HDA_OUTPUT);
	}
	if (n) {
		snprintf(name, sizeof(name), "%s Playback Volume", pfx);
		err = add_control(spec, ALC_CTL_BIND_VOL, name, 0, (long)ctl);
		if (err < 0)
			return err;
	}
	return 0;
}

static int alc_auto_create_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	return alc_auto_create_extra_outs(codec, spec->autocfg.hp_outs,
					  spec->autocfg.hp_pins,
					  spec->multiout.hp_out_nid,
					  "Headphone");
}

static int alc_auto_create_speaker_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	return alc_auto_create_extra_outs(codec, spec->autocfg.speaker_outs,
					  spec->autocfg.speaker_pins,
					  spec->multiout.extra_out_nid,
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

	/* unmute DAC if it's not assigned to a mixer */
	nid = alc_look_for_out_mute_nid(codec, pin, dac);
	if (nid == mix && nid_has_mute(codec, dac, HDA_OUTPUT))
		snd_hda_codec_write(codec, dac, 0, AC_VERB_SET_AMP_GAIN_MUTE,
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
	int i;
	hda_nid_t pin, dac;

	for (i = 0; i < spec->autocfg.hp_outs; i++) {
		if (spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)
			break;
		pin = spec->autocfg.hp_pins[i];
		if (!pin)
			break;
		dac = spec->multiout.hp_out_nid[i];
		if (!dac) {
			if (i > 0 && spec->multiout.hp_out_nid[0])
				dac = spec->multiout.hp_out_nid[0];
			else
				dac = spec->multiout.dac_nids[0];
		}
		alc_auto_set_output_and_unmute(codec, pin, PIN_HP, dac);
	}
	for (i = 0; i < spec->autocfg.speaker_outs; i++) {
		if (spec->autocfg.line_out_type == AUTO_PIN_SPEAKER_OUT)
			break;
		pin = spec->autocfg.speaker_pins[i];
		if (!pin)
			break;
		dac = spec->multiout.extra_out_nid[i];
		if (!dac) {
			if (i > 0 && spec->multiout.extra_out_nid[0])
				dac = spec->multiout.extra_out_nid[0];
			else
				dac = spec->multiout.dac_nids[0];
		}
		alc_auto_set_output_and_unmute(codec, pin, PIN_OUT, dac);
	}
}

/* check whether the given pin can be a multi-io pin */
static bool can_be_multiio_pin(struct hda_codec *codec,
			       unsigned int location, hda_nid_t nid)
{
	unsigned int defcfg, caps;

	defcfg = snd_hda_codec_get_pincfg(codec, nid);
	if (get_defcfg_connect(defcfg) != AC_JACK_PORT_COMPLEX)
		return false;
	if (location && get_defcfg_location(defcfg) != location)
		return false;
	caps = snd_hda_query_pin_caps(codec, nid);
	if (!(caps & AC_PINCAP_OUT))
		return false;
	return true;
}

/*
 * multi-io helper
 *
 * When hardwired is set, try to fill ony hardwired pins, and returns
 * zero if any pins are filled, non-zero if nothing found.
 * When hardwired is off, try to fill possible input pins, and returns
 * the badness value.
 */
static int alc_auto_fill_multi_ios(struct hda_codec *codec,
				   hda_nid_t reference_pin,
				   bool hardwired, int offset)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int type, i, j, dacs, num_pins, old_pins;
	unsigned int defcfg = snd_hda_codec_get_pincfg(codec, reference_pin);
	unsigned int location = get_defcfg_location(defcfg);
	int badness = 0;

	old_pins = spec->multi_ios;
	if (old_pins >= 2)
		goto end_fill;

	num_pins = 0;
	for (type = AUTO_PIN_LINE_IN; type >= AUTO_PIN_MIC; type--) {
		for (i = 0; i < cfg->num_inputs; i++) {
			if (cfg->inputs[i].type != type)
				continue;
			if (can_be_multiio_pin(codec, location,
					       cfg->inputs[i].pin))
				num_pins++;
		}
	}
	if (num_pins < 2)
		goto end_fill;

	dacs = spec->multiout.num_dacs;
	for (type = AUTO_PIN_LINE_IN; type >= AUTO_PIN_MIC; type--) {
		for (i = 0; i < cfg->num_inputs; i++) {
			hda_nid_t nid = cfg->inputs[i].pin;
			hda_nid_t dac = 0;

			if (cfg->inputs[i].type != type)
				continue;
			if (!can_be_multiio_pin(codec, location, nid))
				continue;
			for (j = 0; j < spec->multi_ios; j++) {
				if (nid == spec->multi_io[j].pin)
					break;
			}
			if (j < spec->multi_ios)
				continue;

			if (offset && offset + spec->multi_ios < dacs) {
				dac = spec->private_dac_nids[offset + spec->multi_ios];
				if (!alc_auto_is_dac_reachable(codec, nid, dac))
					dac = 0;
			}
			if (hardwired)
				dac = get_dac_if_single(codec, nid);
			else if (!dac)
				dac = alc_auto_look_for_dac(codec, nid);
			if (!dac) {
				badness++;
				continue;
			}
			spec->multi_io[spec->multi_ios].pin = nid;
			spec->multi_io[spec->multi_ios].dac = dac;
			spec->multi_ios++;
			if (spec->multi_ios >= 2)
				break;
		}
	}
 end_fill:
	if (badness)
		badness = BAD_MULTI_IO;
	if (old_pins == spec->multi_ios) {
		if (hardwired)
			return 1; /* nothing found */
		else
			return badness; /* no badness if nothing found */
	}
	if (!hardwired && spec->multi_ios < 2) {
		spec->multi_ios = old_pins;
		return badness;
	}

	return 0;
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

static int alc_auto_add_multi_channel_mode(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->multi_ios > 0) {
		struct snd_kcontrol_new *knew;

		knew = alc_kcontrol_new(spec);
		if (!knew)
			return -ENOMEM;
		*knew = alc_auto_channel_mode_enum;
		knew->name = kstrdup("Channel Mode", GFP_KERNEL);
		if (!knew->name)
			return -ENOMEM;
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

 again:
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
			if (spec->shared_mic_hp) {
				spec->shared_mic_hp = 0;
				spec->private_imux[0].num_items = 1;
				goto again;
			}
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
	else if (spec->input_mux->num_items == 1 || spec->shared_mic_hp)
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
		alc_mux_select(codec, c, spec->cur_mux[c], true);
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
			if (spec->shared_mic_hp && !strcmp(label, "Misc"))
				label = "Headphone Mic";
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
		hda_nid_t cap = get_capsrc(spec, i);
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
	SND_PCI_QUIRK(0x1458, 0xa002, "GA-MA790X", 1),
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
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int err;

	err = snd_hda_parse_pin_defcfg(codec, cfg, ignore_nids,
				       spec->parse_flags);
	if (err < 0)
		return err;
	if (!cfg->line_outs) {
		if (cfg->dig_outs || cfg->dig_in_pin) {
			spec->multiout.max_channels = 2;
			spec->no_analog = 1;
			goto dig_only;
		}
		return 0; /* can't find valid BIOS pin config */
	}

	if (cfg->line_out_type == AUTO_PIN_SPEAKER_OUT &&
	    cfg->line_outs <= cfg->hp_outs) {
		/* use HP as primary out */
		cfg->speaker_outs = cfg->line_outs;
		memcpy(cfg->speaker_pins, cfg->line_out_pins,
		       sizeof(cfg->speaker_pins));
		cfg->line_outs = cfg->hp_outs;
		memcpy(cfg->line_out_pins, cfg->hp_pins, sizeof(cfg->hp_pins));
		cfg->hp_outs = 0;
		memset(cfg->hp_pins, 0, sizeof(cfg->hp_pins));
		cfg->line_out_type = AUTO_PIN_HP_OUT;
	}

	err = alc_auto_fill_dac_nids(codec);
	if (err < 0)
		return err;
	err = alc_auto_add_multi_channel_mode(codec);
	if (err < 0)
		return err;
	err = alc_auto_create_multi_out_ctls(codec, cfg);
	if (err < 0)
		return err;
	err = alc_auto_create_hp_out(codec);
	if (err < 0)
		return err;
	err = alc_auto_create_speaker_out(codec);
	if (err < 0)
		return err;
	err = alc_auto_create_shared_input(codec);
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

	if (!spec->no_analog && !spec->cap_mixer)
		set_capture_mixer(codec);

	return 1;
}

static int alc880_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc880_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc880_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc880_ignore, alc880_ssids);
}

/*
 * ALC880 fix-ups
 */
enum {
	ALC880_FIXUP_GPIO1,
	ALC880_FIXUP_GPIO2,
	ALC880_FIXUP_MEDION_RIM,
	ALC880_FIXUP_LG,
	ALC880_FIXUP_W810,
	ALC880_FIXUP_EAPD_COEF,
	ALC880_FIXUP_TCL_S700,
	ALC880_FIXUP_VOL_KNOB,
	ALC880_FIXUP_FUJITSU,
	ALC880_FIXUP_F1734,
	ALC880_FIXUP_UNIWILL,
	ALC880_FIXUP_UNIWILL_DIG,
	ALC880_FIXUP_Z71V,
	ALC880_FIXUP_3ST_BASE,
	ALC880_FIXUP_3ST,
	ALC880_FIXUP_3ST_DIG,
	ALC880_FIXUP_5ST_BASE,
	ALC880_FIXUP_5ST,
	ALC880_FIXUP_5ST_DIG,
	ALC880_FIXUP_6ST_BASE,
	ALC880_FIXUP_6ST,
	ALC880_FIXUP_6ST_DIG,
};

/* enable the volume-knob widget support on NID 0x21 */
static void alc880_fixup_vol_knob(struct hda_codec *codec,
				  const struct alc_fixup *fix, int action)
{
	if (action == ALC_FIXUP_ACT_PROBE)
		snd_hda_jack_detect_enable(codec, 0x21, ALC_DCVOL_EVENT);
}

static const struct alc_fixup alc880_fixups[] = {
	[ALC880_FIXUP_GPIO1] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = alc_gpio1_init_verbs,
	},
	[ALC880_FIXUP_GPIO2] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = alc_gpio2_init_verbs,
	},
	[ALC880_FIXUP_MEDION_RIM] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3060 },
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_LG] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			/* disable bogus unused pins */
			{ 0x16, 0x411111f0 },
			{ 0x18, 0x411111f0 },
			{ 0x1a, 0x411111f0 },
			{ }
		}
	},
	[ALC880_FIXUP_W810] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			/* disable bogus unused pins */
			{ 0x17, 0x411111f0 },
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_EAPD_COEF] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3060 },
			{}
		},
	},
	[ALC880_FIXUP_TCL_S700] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3070 },
			{}
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_GPIO2,
	},
	[ALC880_FIXUP_VOL_KNOB] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc880_fixup_vol_knob,
	},
	[ALC880_FIXUP_FUJITSU] = {
		/* override all pins as BIOS on old Amilo is broken */
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x0121411f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x99030130 }, /* bass speaker */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x411111f0 }, /* N/A */
			{ 0x19, 0x01a19950 }, /* mic-in */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x01454140 }, /* SPDIF out */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_VOL_KNOB,
	},
	[ALC880_FIXUP_F1734] = {
		/* almost compatible with FUJITSU, but no bass and SPDIF */
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x0121411f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x411111f0 }, /* N/A */
			{ 0x19, 0x01a19950 }, /* mic-in */
			{ 0x1a, 0x411111f0 }, /* N/A */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_VOL_KNOB,
	},
	[ALC880_FIXUP_UNIWILL] = {
		/* need to fix HP and speaker pins to be parsed correctly */
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x0121411f }, /* HP */
			{ 0x15, 0x99030120 }, /* speaker */
			{ 0x16, 0x99030130 }, /* bass speaker */
			{ }
		},
	},
	[ALC880_FIXUP_UNIWILL_DIG] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			/* disable bogus unused pins */
			{ 0x17, 0x411111f0 },
			{ 0x19, 0x411111f0 },
			{ 0x1b, 0x411111f0 },
			{ 0x1f, 0x411111f0 },
			{ }
		}
	},
	[ALC880_FIXUP_Z71V] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			/* set up the whole pins as BIOS is utterly broken */
			{ 0x14, 0x99030120 }, /* speaker */
			{ 0x15, 0x0121411f }, /* HP */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x01a19950 }, /* mic-in */
			{ 0x19, 0x411111f0 }, /* N/A */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x411111f0 }, /* N/A */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		}
	},
	[ALC880_FIXUP_3ST_BASE] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x01014010 }, /* line-out */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x411111f0 }, /* N/A */
			{ 0x17, 0x411111f0 }, /* N/A */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x0121411f }, /* HP */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x02a19c40 }, /* front-mic */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_3ST] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_3ST_BASE,
	},
	[ALC880_FIXUP_3ST_DIG] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_3ST_BASE,
	},
	[ALC880_FIXUP_5ST_BASE] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x01014010 }, /* front */
			{ 0x15, 0x411111f0 }, /* N/A */
			{ 0x16, 0x01011411 }, /* CLFE */
			{ 0x17, 0x01016412 }, /* surr */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x0121411f }, /* HP */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x02a19c40 }, /* front-mic */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_5ST] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_5ST_BASE,
	},
	[ALC880_FIXUP_5ST_DIG] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_5ST_BASE,
	},
	[ALC880_FIXUP_6ST_BASE] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x01014010 }, /* front */
			{ 0x15, 0x01016412 }, /* surr */
			{ 0x16, 0x01011411 }, /* CLFE */
			{ 0x17, 0x01012414 }, /* side */
			{ 0x18, 0x01a19c30 }, /* mic-in */
			{ 0x19, 0x02a19c40 }, /* front-mic */
			{ 0x1a, 0x01813031 }, /* line-in */
			{ 0x1b, 0x0121411f }, /* HP */
			{ 0x1c, 0x411111f0 }, /* N/A */
			{ 0x1d, 0x411111f0 }, /* N/A */
			/* 0x1e is filled in below */
			{ 0x1f, 0x411111f0 }, /* N/A */
			{ }
		}
	},
	[ALC880_FIXUP_6ST] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x1e, 0x411111f0 }, /* N/A */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
	[ALC880_FIXUP_6ST_DIG] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x1e, 0x0144111e }, /* SPDIF */
			{ }
		},
		.chained = true,
		.chain_id = ALC880_FIXUP_6ST_BASE,
	},
};

static const struct snd_pci_quirk alc880_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x0f69, "Coeus G610P", ALC880_FIXUP_W810),
	SND_PCI_QUIRK(0x1043, 0x1964, "ASUS Z71V", ALC880_FIXUP_Z71V),
	SND_PCI_QUIRK_VENDOR(0x1043, "ASUS", ALC880_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x1558, 0x5401, "Clevo GPIO2", ALC880_FIXUP_GPIO2),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo", ALC880_FIXUP_EAPD_COEF),
	SND_PCI_QUIRK(0x1584, 0x9050, "Uniwill", ALC880_FIXUP_UNIWILL_DIG),
	SND_PCI_QUIRK(0x1584, 0x9054, "Uniwill", ALC880_FIXUP_F1734),
	SND_PCI_QUIRK(0x1584, 0x9070, "Uniwill", ALC880_FIXUP_UNIWILL),
	SND_PCI_QUIRK(0x1584, 0x9077, "Uniwill P53", ALC880_FIXUP_VOL_KNOB),
	SND_PCI_QUIRK(0x161f, 0x203d, "W810", ALC880_FIXUP_W810),
	SND_PCI_QUIRK(0x161f, 0x205d, "Medion Rim 2150", ALC880_FIXUP_MEDION_RIM),
	SND_PCI_QUIRK(0x1734, 0x107c, "FSC F1734", ALC880_FIXUP_F1734),
	SND_PCI_QUIRK(0x1734, 0x1094, "FSC Amilo M1451G", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x10ac, "FSC AMILO Xi 1526", ALC880_FIXUP_F1734),
	SND_PCI_QUIRK(0x1734, 0x10b0, "FSC Amilo Pi1556", ALC880_FIXUP_FUJITSU),
	SND_PCI_QUIRK(0x1854, 0x003b, "LG", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x005f, "LG P1 Express", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x1854, 0x0068, "LG w1", ALC880_FIXUP_LG),
	SND_PCI_QUIRK(0x19db, 0x4188, "TCL S700", ALC880_FIXUP_TCL_S700),

	/* Below is the copied entries from alc880_quirks.c.
	 * It's not quite sure whether BIOS sets the correct pin-config table
	 * on these machines, thus they are kept to be compatible with
	 * the old static quirks.  Once when it's confirmed to work without
	 * these overrides, it'd be better to remove.
	 */
	SND_PCI_QUIRK(0x1019, 0xa880, "ECS", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1019, 0xa884, "Acer APFV", ALC880_FIXUP_6ST),
	SND_PCI_QUIRK(0x1025, 0x0070, "ULI", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0077, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0078, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0087, "ULI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe309, "ULI", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe310, "ULI", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x1039, 0x1234, NULL, ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x104d, 0x81a0, "Sony", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x104d, 0x81d6, "Sony", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0x107b, 0x3032, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x107b, 0x3033, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x107b, 0x4039, "Gateway", ALC880_FIXUP_5ST),
	SND_PCI_QUIRK(0x1297, 0xc790, "Shuttle ST20G5", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1458, 0xa102, "Gigabyte K8", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x1150, "MSI", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1509, 0x925d, "FIC P4M", ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x1565, 0x8202, "Biostar", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x400d, "EPoX", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x4012, "EPox EP-5LDA", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x2668, 0x8086, NULL, ALC880_FIXUP_6ST_DIG), /* broken BIOS */
	SND_PCI_QUIRK(0x8086, 0x2668, NULL, ALC880_FIXUP_6ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xa100, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd400, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd401, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd402, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe224, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe305, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe308, "Intel mobo", ALC880_FIXUP_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe400, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe401, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe402, "Intel mobo", ALC880_FIXUP_5ST_DIG),
	/* default Intel */
	SND_PCI_QUIRK_VENDOR(0x8086, "Intel mobo", ALC880_FIXUP_3ST),
	SND_PCI_QUIRK(0xa0a0, 0x0560, "AOpen i915GMm-HFS", ALC880_FIXUP_5ST_DIG),
	SND_PCI_QUIRK(0xe803, 0x1019, NULL, ALC880_FIXUP_6ST_DIG),
	{}
};

static const struct alc_model_fixup alc880_fixup_models[] = {
	{.id = ALC880_FIXUP_3ST, .name = "3stack"},
	{.id = ALC880_FIXUP_3ST_DIG, .name = "3stack-digout"},
	{.id = ALC880_FIXUP_5ST, .name = "5stack"},
	{.id = ALC880_FIXUP_5ST_DIG, .name = "5stack-digout"},
	{.id = ALC880_FIXUP_6ST, .name = "6stack"},
	{.id = ALC880_FIXUP_6ST_DIG, .name = "6stack-digout"},
	{}
};


/*
 * OK, here we have finally the patch for ALC880
 */
static int patch_alc880(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;
	spec->need_dac_fix = 1;

	alc_pick_fixup(codec, alc880_fixup_models, alc880_fixup_tbl,
		       alc880_fixups);
	alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc880_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->no_analog) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0)
			goto error;
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
	}

	codec->patch_ops = alc_patch_ops;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
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

/*
 * Pin config fixes
 */
enum {
	ALC260_FIXUP_HP_DC5750,
	ALC260_FIXUP_HP_PIN_0F,
	ALC260_FIXUP_COEF,
	ALC260_FIXUP_GPIO1,
	ALC260_FIXUP_GPIO1_TOGGLE,
	ALC260_FIXUP_REPLACER,
	ALC260_FIXUP_HP_B1900,
	ALC260_FIXUP_KN1,
};

static void alc260_gpio1_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
			    spec->hp_jack_present);
}

static void alc260_fixup_gpio1_toggle(struct hda_codec *codec,
				      const struct alc_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action == ALC_FIXUP_ACT_PROBE) {
		/* although the machine has only one output pin, we need to
		 * toggle GPIO1 according to the jack state
		 */
		spec->automute_hook = alc260_gpio1_automute;
		spec->detect_hp = 1;
		spec->automute_speaker = 1;
		spec->autocfg.hp_pins[0] = 0x0f; /* copy it for automute */
		snd_hda_jack_detect_enable(codec, 0x0f, ALC_HP_EVENT);
		spec->unsol_event = alc_sku_unsol_event;
		add_verb(codec->spec, alc_gpio1_init_verbs);
	}
}

static void alc260_fixup_kn1(struct hda_codec *codec,
			     const struct alc_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static const struct alc_pincfg pincfgs[] = {
		{ 0x0f, 0x02214000 }, /* HP/speaker */
		{ 0x12, 0x90a60160 }, /* int mic */
		{ 0x13, 0x02a19000 }, /* ext mic */
		{ 0x18, 0x01446000 }, /* SPDIF out */
		/* disable bogus I/O pins */
		{ 0x10, 0x411111f0 },
		{ 0x11, 0x411111f0 },
		{ 0x14, 0x411111f0 },
		{ 0x15, 0x411111f0 },
		{ 0x16, 0x411111f0 },
		{ 0x17, 0x411111f0 },
		{ 0x19, 0x411111f0 },
		{ }
	};

	switch (action) {
	case ALC_FIXUP_ACT_PRE_PROBE:
		alc_apply_pincfgs(codec, pincfgs);
		break;
	case ALC_FIXUP_ACT_PROBE:
		spec->init_amp = ALC_INIT_NONE;
		break;
	}
}

static const struct alc_fixup alc260_fixups[] = {
	[ALC260_FIXUP_HP_DC5750] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x11, 0x90130110 }, /* speaker */
			{ }
		}
	},
	[ALC260_FIXUP_HP_PIN_0F] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x0f, 0x01214000 }, /* HP */
			{ }
		}
	},
	[ALC260_FIXUP_COEF] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3040 },
			{ }
		},
		.chained = true,
		.chain_id = ALC260_FIXUP_HP_PIN_0F,
	},
	[ALC260_FIXUP_GPIO1] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = alc_gpio1_init_verbs,
	},
	[ALC260_FIXUP_GPIO1_TOGGLE] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc260_fixup_gpio1_toggle,
		.chained = true,
		.chain_id = ALC260_FIXUP_HP_PIN_0F,
	},
	[ALC260_FIXUP_REPLACER] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x3050 },
			{ }
		},
		.chained = true,
		.chain_id = ALC260_FIXUP_GPIO1_TOGGLE,
	},
	[ALC260_FIXUP_HP_B1900] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc260_fixup_gpio1_toggle,
		.chained = true,
		.chain_id = ALC260_FIXUP_COEF,
	},
	[ALC260_FIXUP_KN1] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc260_fixup_kn1,
	},
};

static const struct snd_pci_quirk alc260_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x007b, "Acer C20x", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x1025, 0x007f, "Acer Aspire 9500", ALC260_FIXUP_COEF),
	SND_PCI_QUIRK(0x1025, 0x008f, "Acer", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x103c, 0x280a, "HP dc5750", ALC260_FIXUP_HP_DC5750),
	SND_PCI_QUIRK(0x103c, 0x30ba, "HP Presario B1900", ALC260_FIXUP_HP_B1900),
	SND_PCI_QUIRK(0x1509, 0x4540, "Favorit 100XS", ALC260_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x152d, 0x0729, "Quanta KN1", ALC260_FIXUP_KN1),
	SND_PCI_QUIRK(0x161f, 0x2057, "Replacer 672V", ALC260_FIXUP_REPLACER),
	SND_PCI_QUIRK(0x1631, 0xc017, "PB V7900", ALC260_FIXUP_COEF),
	{}
};

/*
 */
static int patch_alc260(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x07;

	alc_pick_fixup(codec, NULL, alc260_fixup_tbl, alc260_fixups);
	alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc260_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->no_analog) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0)
			goto error;
		set_beep_amp(spec, 0x07, 0x05, HDA_INPUT);
	}

	codec->patch_ops = alc_patch_ops;
	spec->shutup = alc_eapd_shutup;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
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

/*
 * Pin config fixes
 */
enum {
	ALC882_FIXUP_ABIT_AW9D_MAX,
	ALC882_FIXUP_LENOVO_Y530,
	ALC882_FIXUP_PB_M5210,
	ALC882_FIXUP_ACER_ASPIRE_7736,
	ALC882_FIXUP_ASUS_W90V,
	ALC889_FIXUP_CD,
	ALC889_FIXUP_VAIO_TT,
	ALC888_FIXUP_EEE1601,
	ALC882_FIXUP_EAPD,
	ALC883_FIXUP_EAPD,
	ALC883_FIXUP_ACER_EAPD,
	ALC882_FIXUP_GPIO1,
	ALC882_FIXUP_GPIO2,
	ALC882_FIXUP_GPIO3,
	ALC889_FIXUP_COEF,
	ALC882_FIXUP_ASUS_W2JC,
	ALC882_FIXUP_ACER_ASPIRE_4930G,
	ALC882_FIXUP_ACER_ASPIRE_8930G,
	ALC882_FIXUP_ASPIRE_8930G_VERBS,
	ALC885_FIXUP_MACPRO_GPIO,
	ALC889_FIXUP_DAC_ROUTE,
	ALC889_FIXUP_MBP_VREF,
	ALC889_FIXUP_IMAC91_VREF,
};

static void alc889_fixup_coef(struct hda_codec *codec,
			      const struct alc_fixup *fix, int action)
{
	if (action != ALC_FIXUP_ACT_INIT)
		return;
	alc889_coef_init(codec);
}

/* toggle speaker-output according to the hp-jack state */
static void alc882_gpio_mute(struct hda_codec *codec, int pin, int muted)
{
	unsigned int gpiostate, gpiomask, gpiodir;

	gpiostate = snd_hda_codec_read(codec, codec->afg, 0,
				       AC_VERB_GET_GPIO_DATA, 0);

	if (!muted)
		gpiostate |= (1 << pin);
	else
		gpiostate &= ~(1 << pin);

	gpiomask = snd_hda_codec_read(codec, codec->afg, 0,
				      AC_VERB_GET_GPIO_MASK, 0);
	gpiomask |= (1 << pin);

	gpiodir = snd_hda_codec_read(codec, codec->afg, 0,
				     AC_VERB_GET_GPIO_DIRECTION, 0);
	gpiodir |= (1 << pin);


	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_MASK, gpiomask);
	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_DIRECTION, gpiodir);

	msleep(1);

	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_DATA, gpiostate);
}

/* set up GPIO at initialization */
static void alc885_fixup_macpro_gpio(struct hda_codec *codec,
				     const struct alc_fixup *fix, int action)
{
	if (action != ALC_FIXUP_ACT_INIT)
		return;
	alc882_gpio_mute(codec, 0, 0);
	alc882_gpio_mute(codec, 1, 0);
}

/* Fix the connection of some pins for ALC889:
 * At least, Acer Aspire 5935 shows the connections to DAC3/4 don't
 * work correctly (bko#42740)
 */
static void alc889_fixup_dac_route(struct hda_codec *codec,
				   const struct alc_fixup *fix, int action)
{
	if (action == ALC_FIXUP_ACT_PRE_PROBE) {
		/* fake the connections during parsing the tree */
		hda_nid_t conn1[2] = { 0x0c, 0x0d };
		hda_nid_t conn2[2] = { 0x0e, 0x0f };
		snd_hda_override_conn_list(codec, 0x14, 2, conn1);
		snd_hda_override_conn_list(codec, 0x15, 2, conn1);
		snd_hda_override_conn_list(codec, 0x18, 2, conn2);
		snd_hda_override_conn_list(codec, 0x1a, 2, conn2);
	} else if (action == ALC_FIXUP_ACT_PROBE) {
		/* restore the connections */
		hda_nid_t conn[5] = { 0x0c, 0x0d, 0x0e, 0x0f, 0x26 };
		snd_hda_override_conn_list(codec, 0x14, 5, conn);
		snd_hda_override_conn_list(codec, 0x15, 5, conn);
		snd_hda_override_conn_list(codec, 0x18, 5, conn);
		snd_hda_override_conn_list(codec, 0x1a, 5, conn);
	}
}

/* Set VREF on HP pin */
static void alc889_fixup_mbp_vref(struct hda_codec *codec,
				  const struct alc_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static hda_nid_t nids[2] = { 0x14, 0x15 };
	int i;

	if (action != ALC_FIXUP_ACT_INIT)
		return;
	for (i = 0; i < ARRAY_SIZE(nids); i++) {
		unsigned int val = snd_hda_codec_get_pincfg(codec, nids[i]);
		if (get_defcfg_device(val) != AC_JACK_HP_OUT)
			continue;
		val = snd_hda_codec_read(codec, nids[i], 0,
					 AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		val |= AC_PINCTL_VREF_80;
		snd_hda_codec_write(codec, nids[i], 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, val);
		spec->keep_vref_in_automute = 1;
		break;
	}
}

/* Set VREF on speaker pins on imac91 */
static void alc889_fixup_imac91_vref(struct hda_codec *codec,
				     const struct alc_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	static hda_nid_t nids[2] = { 0x18, 0x1a };
	int i;

	if (action != ALC_FIXUP_ACT_INIT)
		return;
	for (i = 0; i < ARRAY_SIZE(nids); i++) {
		unsigned int val;
		val = snd_hda_codec_read(codec, nids[i], 0,
					 AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		val |= AC_PINCTL_VREF_50;
		snd_hda_codec_write(codec, nids[i], 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, val);
	}
	spec->keep_vref_in_automute = 1;
}

static const struct alc_fixup alc882_fixups[] = {
	[ALC882_FIXUP_ABIT_AW9D_MAX] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x15, 0x01080104 }, /* side */
			{ 0x16, 0x01011012 }, /* rear */
			{ 0x17, 0x01016011 }, /* clfe */
			{ }
		}
	},
	[ALC882_FIXUP_LENOVO_Y530] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x15, 0x99130112 }, /* rear int speakers */
			{ 0x16, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[ALC882_FIXUP_PB_M5210] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50 },
			{}
		}
	},
	[ALC882_FIXUP_ACER_ASPIRE_7736] = {
		.type = ALC_FIXUP_SKU,
		.v.sku = ALC_FIXUP_SKU_IGNORE,
	},
	[ALC882_FIXUP_ASUS_W90V] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x16, 0x99130110 }, /* fix sequence for CLFE */
			{ }
		}
	},
	[ALC889_FIXUP_CD] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x1c, 0x993301f0 }, /* CD */
			{ }
		}
	},
	[ALC889_FIXUP_VAIO_TT] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x17, 0x90170111 }, /* hidden surround speaker */
			{ }
		}
	},
	[ALC888_FIXUP_EEE1601] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0b },
			{ 0x20, AC_VERB_SET_PROC_COEF,  0x0838 },
			{ }
		}
	},
	[ALC882_FIXUP_EAPD] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3060 },
			{ }
		}
	},
	[ALC883_FIXUP_EAPD] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* change to EAPD mode */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3070 },
			{ }
		}
	},
	[ALC883_FIXUP_ACER_EAPD] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* eanable EAPD on Acer laptops */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{ }
		}
	},
	[ALC882_FIXUP_GPIO1] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = alc_gpio1_init_verbs,
	},
	[ALC882_FIXUP_GPIO2] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = alc_gpio2_init_verbs,
	},
	[ALC882_FIXUP_GPIO3] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = alc_gpio3_init_verbs,
	},
	[ALC882_FIXUP_ASUS_W2JC] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = alc_gpio1_init_verbs,
		.chained = true,
		.chain_id = ALC882_FIXUP_EAPD,
	},
	[ALC889_FIXUP_COEF] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc889_fixup_coef,
	},
	[ALC882_FIXUP_ACER_ASPIRE_4930G] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x16, 0x99130111 }, /* CLFE speaker */
			{ 0x17, 0x99130112 }, /* surround speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC882_FIXUP_ACER_ASPIRE_8930G] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x16, 0x99130111 }, /* CLFE speaker */
			{ 0x1b, 0x99130112 }, /* surround speaker */
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_ASPIRE_8930G_VERBS,
	},
	[ALC882_FIXUP_ASPIRE_8930G_VERBS] = {
		/* additional init verbs for Acer Aspire 8930G */
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enable all DACs */
			/* DAC DISABLE/MUTE 1? */
			/*  setting bits 1-5 disables DAC nids 0x02-0x06
			 *  apparently. Init=0x38 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x03 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0000 },
			/* DAC DISABLE/MUTE 2? */
			/*  some bit here disables the other DACs.
			 *  Init=0x4900 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x08 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0000 },
			/* DMIC fix
			 * This laptop has a stereo digital microphone.
			 * The mics are only 1cm apart which makes the stereo
			 * useless. However, either the mic or the ALC889
			 * makes the signal become a difference/sum signal
			 * instead of standard stereo, which is annoying.
			 * So instead we flip this bit which makes the
			 * codec replicate the sum signal to both channels,
			 * turning it into a normal mono mic.
			 */
			/* DMIC_CONTROL? Init value = 0x0001 */
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x0b },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x0003 },
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{ }
		},
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC885_FIXUP_MACPRO_GPIO] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc885_fixup_macpro_gpio,
	},
	[ALC889_FIXUP_DAC_ROUTE] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc889_fixup_dac_route,
	},
	[ALC889_FIXUP_MBP_VREF] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc889_fixup_mbp_vref,
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
	[ALC889_FIXUP_IMAC91_VREF] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc889_fixup_imac91_vref,
		.chained = true,
		.chain_id = ALC882_FIXUP_GPIO1,
	},
};

static const struct snd_pci_quirk alc882_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x006c, "Acer Aspire 9810", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0090, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x010a, "Acer Ferrari 5000", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0110, "Acer Aspire", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0112, "Acer Aspire 9303", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x0121, "Acer Aspire 5920G", ALC883_FIXUP_ACER_EAPD),
	SND_PCI_QUIRK(0x1025, 0x013e, "Acer Aspire 4930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x013f, "Acer Aspire 5930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0145, "Acer Aspire 8930G",
		      ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0146, "Acer Aspire 6935G",
		      ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x015e, "Acer Aspire 6930G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0166, "Acer Aspire 6530G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0142, "Acer Aspire 7730G",
		      ALC882_FIXUP_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0155, "Packard-Bell M5120", ALC882_FIXUP_PB_M5210),
	SND_PCI_QUIRK(0x1025, 0x0259, "Acer Aspire 5935", ALC889_FIXUP_DAC_ROUTE),
	SND_PCI_QUIRK(0x1025, 0x026b, "Acer Aspire 8940G", ALC882_FIXUP_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0296, "Acer Aspire 7736z", ALC882_FIXUP_ACER_ASPIRE_7736),
	SND_PCI_QUIRK(0x1043, 0x13c2, "Asus A7M", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1873, "ASUS W90V", ALC882_FIXUP_ASUS_W90V),
	SND_PCI_QUIRK(0x1043, 0x1971, "Asus W2JC", ALC882_FIXUP_ASUS_W2JC),
	SND_PCI_QUIRK(0x1043, 0x835f, "Asus Eee 1601", ALC888_FIXUP_EEE1601),
	SND_PCI_QUIRK(0x104d, 0x9047, "Sony Vaio TT", ALC889_FIXUP_VAIO_TT),

	/* All Apple entries are in codec SSIDs */
	SND_PCI_QUIRK(0x106b, 0x00a0, "MacBookPro 3,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x00a1, "Macbook", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x00a4, "MacbookPro 4,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x0c00, "Mac Pro", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x1000, "iMac 24", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x2800, "AppleTV", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x2c00, "MacbookPro rev3", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3000, "iMac", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3200, "iMac 7,1 Aluminum", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x106b, 0x3400, "MacBookAir 1,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3500, "MacBookAir 2,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3600, "Macbook 3,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3800, "MacbookPro 4,1", ALC889_FIXUP_MBP_VREF),
	SND_PCI_QUIRK(0x106b, 0x3e00, "iMac 24 Aluminum", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x3f00, "Macbook 5,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4000, "MacbookPro 5,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4100, "Macmini 3,1", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4200, "Mac Pro 5,1", ALC885_FIXUP_MACPRO_GPIO),
	SND_PCI_QUIRK(0x106b, 0x4600, "MacbookPro 5,2", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4900, "iMac 9,1 Aluminum", ALC889_FIXUP_IMAC91_VREF),
	SND_PCI_QUIRK(0x106b, 0x4a00, "Macbook 5,2", ALC889_FIXUP_IMAC91_VREF),

	SND_PCI_QUIRK(0x1071, 0x8258, "Evesham Voyaeger", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK_VENDOR(0x1462, "MSI", ALC882_FIXUP_GPIO3),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte EP45-DS3", ALC889_FIXUP_CD),
	SND_PCI_QUIRK(0x147b, 0x107a, "Abit AW9D-MAX", ALC882_FIXUP_ABIT_AW9D_MAX),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo laptop", ALC882_FIXUP_EAPD),
	SND_PCI_QUIRK(0x161f, 0x2054, "Medion laptop", ALC883_FIXUP_EAPD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Y530", ALC882_FIXUP_LENOVO_Y530),
	SND_PCI_QUIRK(0x8086, 0x0022, "DX58SO", ALC889_FIXUP_COEF),
	{}
};

static const struct alc_model_fixup alc882_fixup_models[] = {
	{.id = ALC882_FIXUP_ACER_ASPIRE_4930G, .name = "acer-aspire-4930g"},
	{.id = ALC882_FIXUP_ACER_ASPIRE_8930G, .name = "acer-aspire-8930g"},
	{.id = ALC883_FIXUP_ACER_EAPD, .name = "acer-aspire"},
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
static int patch_alc882(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

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

	err = alc_codec_rename_from_preset(codec);
	if (err < 0)
		goto error;

	alc_pick_fixup(codec, alc882_fixup_models, alc882_fixup_tbl,
		       alc882_fixups);
	alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);

	alc_auto_parse_customize_define(codec);

	/* automatic parse from the BIOS config */
	err = alc882_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->no_analog && has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0)
			goto error;
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
	}

	codec->patch_ops = alc_patch_ops;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
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
	ALC262_FIXUP_FSC_H270,
	ALC262_FIXUP_HP_Z200,
	ALC262_FIXUP_TYAN,
	ALC262_FIXUP_LENOVO_3000,
	ALC262_FIXUP_BENQ,
	ALC262_FIXUP_BENQ_T31,
};

static const struct alc_fixup alc262_fixups[] = {
	[ALC262_FIXUP_FSC_H270] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0221142f }, /* front HP */
			{ 0x1b, 0x0121141f }, /* rear HP */
			{ }
		}
	},
	[ALC262_FIXUP_HP_Z200] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x16, 0x99130120 }, /* internal speaker */
			{ }
		}
	},
	[ALC262_FIXUP_TYAN] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x1993e1f0 }, /* int AUX */
			{ }
		}
	},
	[ALC262_FIXUP_LENOVO_3000] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50 },
			{}
		},
		.chained = true,
		.chain_id = ALC262_FIXUP_BENQ,
	},
	[ALC262_FIXUP_BENQ] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3070 },
			{}
		}
	},
	[ALC262_FIXUP_BENQ_T31] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x20, AC_VERB_SET_COEF_INDEX, 0x07 },
			{ 0x20, AC_VERB_SET_PROC_COEF, 0x3050 },
			{}
		}
	},
};

static const struct snd_pci_quirk alc262_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x170b, "HP Z200", ALC262_FIXUP_HP_Z200),
	SND_PCI_QUIRK(0x10cf, 0x1397, "Fujitsu", ALC262_FIXUP_BENQ),
	SND_PCI_QUIRK(0x10cf, 0x142d, "Fujitsu Lifebook E8410", ALC262_FIXUP_BENQ),
	SND_PCI_QUIRK(0x10f1, 0x2915, "Tyan Thunder n6650W", ALC262_FIXUP_TYAN),
	SND_PCI_QUIRK(0x1734, 0x1147, "FSC Celsius H270", ALC262_FIXUP_FSC_H270),
	SND_PCI_QUIRK(0x17aa, 0x384e, "Lenovo 3000", ALC262_FIXUP_LENOVO_3000),
	SND_PCI_QUIRK(0x17ff, 0x0560, "Benq ED8", ALC262_FIXUP_BENQ),
	SND_PCI_QUIRK(0x17ff, 0x058d, "Benq T31-16", ALC262_FIXUP_BENQ_T31),
	{}
};


/*
 */
static int patch_alc262(struct hda_codec *codec)
{
	struct alc_spec *spec;
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

	alc_pick_fixup(codec, NULL, alc262_fixup_tbl, alc262_fixups);
	alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc262_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->no_analog && has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0)
			goto error;
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
	}

	codec->patch_ops = alc_patch_ops;
	spec->shutup = alc_eapd_shutup;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
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
static int patch_alc268(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int i, has_beep, err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* ALC268 has no aa-loopback mixer */

	/* automatic parse from the BIOS config */
	err = alc268_parse_auto_config(codec);
	if (err < 0)
		goto error;

	has_beep = 0;
	for (i = 0; i < spec->num_mixers; i++) {
		if (spec->mixers[i] == alc268_beep_mixer) {
			has_beep = 1;
			break;
		}
	}

	if (has_beep) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0)
			goto error;
		if (!query_amp_caps(codec, 0x1d, HDA_INPUT))
			/* override the amp caps for beep generator */
			snd_hda_override_amp_caps(codec, 0x1d, HDA_INPUT,
					  (0x0c << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x0c << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x07 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (0 << AC_AMPCAP_MUTE_SHIFT));
	}

	codec->patch_ops = alc_patch_ops;
	spec->shutup = alc_eapd_shutup;

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC269
 */
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
	if ((alc_get_coef0(codec) & 0x00ff) == 0x017)
		alc269_toggle_power_output(codec, 0);
	if ((alc_get_coef0(codec) & 0x00ff) == 0x018) {
		alc269_toggle_power_output(codec, 0);
		msleep(150);
	}
}

#ifdef CONFIG_PM
static int alc269_resume(struct hda_codec *codec)
{
	if ((alc_get_coef0(codec) & 0x00ff) == 0x018) {
		alc269_toggle_power_output(codec, 0);
		msleep(150);
	}

	codec->patch_ops.init(codec);

	if ((alc_get_coef0(codec) & 0x00ff) == 0x017) {
		alc269_toggle_power_output(codec, 1);
		msleep(200);
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x018)
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

static void alc269_quanta_automute(struct hda_codec *codec)
{
	update_outputs(codec);

	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_COEF_INDEX, 0x0c);
	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_PROC_COEF, 0x680);

	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_COEF_INDEX, 0x0c);
	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_PROC_COEF, 0x480);
}

static void alc269_fixup_quanta_mute(struct hda_codec *codec,
				     const struct alc_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	if (action != ALC_FIXUP_ACT_PROBE)
		return;
	spec->automute_hook = alc269_quanta_automute;
}

/* update mute-LED according to the speaker mute state via mic2 VREF pin */
static void alc269_fixup_mic2_mute_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	unsigned int pinval = enabled ? 0x20 : 0x24;
	snd_hda_codec_update_cache(codec, 0x19, 0,
				   AC_VERB_SET_PIN_WIDGET_CONTROL,
				   pinval);
}

static void alc269_fixup_mic2_mute(struct hda_codec *codec,
				   const struct alc_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	switch (action) {
	case ALC_FIXUP_ACT_BUILD:
		spec->vmaster_mute.hook = alc269_fixup_mic2_mute_hook;
		snd_hda_add_vmaster_hook(codec, &spec->vmaster_mute, true);
		/* fallthru */
	case ALC_FIXUP_ACT_INIT:
		snd_hda_sync_vmaster_hook(&spec->vmaster_mute);
		break;
	}
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
	ALC269_FIXUP_QUANTA_MUTE,
	ALC269_FIXUP_LIFEBOOK,
	ALC269_FIXUP_AMIC,
	ALC269_FIXUP_DMIC,
	ALC269VB_FIXUP_AMIC,
	ALC269VB_FIXUP_DMIC,
	ALC269_FIXUP_MIC2_MUTE_LED,
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
	[ALC269_FIXUP_QUANTA_MUTE] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc269_fixup_quanta_mute,
	},
	[ALC269_FIXUP_LIFEBOOK] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x1a, 0x2101103f }, /* dock line-out */
			{ 0x1b, 0x23a11040 }, /* dock mic-in */
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_QUANTA_MUTE
	},
	[ALC269_FIXUP_AMIC] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121401f }, /* HP out */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ }
		},
	},
	[ALC269_FIXUP_DMIC] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121401f }, /* HP out */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ }
		},
	},
	[ALC269VB_FIXUP_AMIC] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
	},
	[ALC269VB_FIXUP_DMIC] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x12, 0x99a3092f }, /* int-mic */
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
	},
	[ALC269_FIXUP_MIC2_MUTE_LED] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc269_fixup_mic2_mute,
	},
};

static const struct snd_pci_quirk alc269_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x1586, "HP", ALC269_FIXUP_MIC2_MUTE_LED),
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
	SND_PCI_QUIRK(0x10cf, 0x1475, "Lifebook", ALC269_FIXUP_LIFEBOOK),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Thinkpad SL410/510", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x215e, "Thinkpad L512", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21b8, "Thinkpad Edge 14", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21ca, "Thinkpad L412", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21e9, "Thinkpad Edge 15", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_FIXUP_QUANTA_MUTE),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Lenovo Ideapd", ALC269_FIXUP_PCM_44K),
	SND_PCI_QUIRK(0x17aa, 0x9e54, "LENOVO NB", ALC269_FIXUP_LENOVO_EAPD),

#if 0
	/* Below is a quirk table taken from the old code.
	 * Basically the device should work as is without the fixup table.
	 * If BIOS doesn't give a proper info, enable the corresponding
	 * fixup entry.
	 */
	SND_PCI_QUIRK(0x1043, 0x8330, "ASUS Eeepc P703 P900A",
		      ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1013, "ASUS N61Da", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1143, "ASUS B53f", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1133, "ASUS UJ20ft", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1183, "ASUS K72DR", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x11b3, "ASUS K52DR", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x11e3, "ASUS U33Jc", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1273, "ASUS UL80Jt", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1283, "ASUS U53Jc", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x12b3, "ASUS N82JV", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x12d3, "ASUS N61Jv", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x13a3, "ASUS UL30Vt", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1373, "ASUS G73JX", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1383, "ASUS UJ30Jc", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x13d3, "ASUS N61JA", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1413, "ASUS UL50", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1443, "ASUS UL30", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1453, "ASUS M60Jv", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1483, "ASUS UL80", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x14f3, "ASUS F83Vf", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x14e3, "ASUS UL20", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1513, "ASUS UX30", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1593, "ASUS N51Vn", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15a3, "ASUS N60Jv", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15b3, "ASUS N60Dp", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15c3, "ASUS N70De", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15e3, "ASUS F83T", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1643, "ASUS M60J", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1653, "ASUS U50", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1693, "ASUS F50N", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x16a3, "ASUS F5Q", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1723, "ASUS P80", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1743, "ASUS U80", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1773, "ASUS U20A", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1883, "ASUS F81Se", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x152d, 0x1778, "Quanta ON1", ALC269_FIXUP_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x3be9, "Quanta Wistron", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_FIXUP_AMIC),
	SND_PCI_QUIRK(0x17ff, 0x059a, "Quanta EL3", ALC269_FIXUP_DMIC),
	SND_PCI_QUIRK(0x17ff, 0x059b, "Quanta JR1", ALC269_FIXUP_DMIC),
#endif
	{}
};

static const struct alc_model_fixup alc269_fixup_models[] = {
	{.id = ALC269_FIXUP_AMIC, .name = "laptop-amic"},
	{.id = ALC269_FIXUP_DMIC, .name = "laptop-dmic"},
	{}
};


static void alc269_fill_coef(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int val;

	if (spec->codec_variant != ALC269_TYPE_ALC269VB)
		return;

	if ((alc_get_coef0(codec) & 0x00ff) < 0x015) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8817);
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x016) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8814);
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x017) {
		val = alc_read_coef_idx(codec, 0x04);
		/* Power up output pin */
		alc_write_coef_idx(codec, 0x04, val | (1<<11));
	}

	if ((alc_get_coef0(codec) & 0x00ff) == 0x018) {
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
}

/*
 */
static int patch_alc269(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err = 0;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;

	alc_auto_parse_customize_define(codec);

	err = alc_codec_rename_from_preset(codec);
	if (err < 0)
		goto error;

	if (codec->vendor_id == 0x10ec0269) {
		spec->codec_variant = ALC269_TYPE_ALC269VA;
		switch (alc_get_coef0(codec) & 0x00f0) {
		case 0x0010:
			if (codec->bus->pci->subsystem_vendor == 0x1025 &&
			    spec->cdefine.platform_type == 1)
				err = alc_codec_rename(codec, "ALC271X");
			spec->codec_variant = ALC269_TYPE_ALC269VB;
			break;
		case 0x0020:
			if (codec->bus->pci->subsystem_vendor == 0x17aa &&
			    codec->bus->pci->subsystem_device == 0x21f3)
				err = alc_codec_rename(codec, "ALC3202");
			spec->codec_variant = ALC269_TYPE_ALC269VC;
			break;
		default:
			alc_fix_pll_init(codec, 0x20, 0x04, 15);
		}
		if (err < 0)
			goto error;
		spec->init_hook = alc269_fill_coef;
		alc269_fill_coef(codec);
	}

	alc_pick_fixup(codec, alc269_fixup_models,
		       alc269_fixup_tbl, alc269_fixups);
	alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc269_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->no_analog && has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0)
			goto error;
		set_beep_amp(spec, 0x0b, 0x04, HDA_INPUT);
	}

	codec->patch_ops = alc_patch_ops;
#ifdef CONFIG_PM
	codec->patch_ops.resume = alc269_resume;
#endif
	spec->shutup = alc269_shutup;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
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

/* Pin config fixes */
enum {
	ALC861_FIXUP_FSC_AMILO_PI1505,
	ALC861_FIXUP_AMP_VREF_0F,
	ALC861_FIXUP_NO_JACK_DETECT,
	ALC861_FIXUP_ASUS_A6RP,
};

/* On some laptops, VREF of pin 0x0f is abused for controlling the main amp */
static void alc861_fixup_asus_amp_vref_0f(struct hda_codec *codec,
			const struct alc_fixup *fix, int action)
{
	struct alc_spec *spec = codec->spec;
	unsigned int val;

	if (action != ALC_FIXUP_ACT_INIT)
		return;
	val = snd_hda_codec_read(codec, 0x0f, 0,
				 AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	if (!(val & (AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN)))
		val |= AC_PINCTL_IN_EN;
	val |= AC_PINCTL_VREF_50;
	snd_hda_codec_write(codec, 0x0f, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, val);
	spec->keep_vref_in_automute = 1;
}

/* suppress the jack-detection */
static void alc_fixup_no_jack_detect(struct hda_codec *codec,
				     const struct alc_fixup *fix, int action)
{
	if (action == ALC_FIXUP_ACT_PRE_PROBE)
		codec->no_jack_detect = 1;
}

static const struct alc_fixup alc861_fixups[] = {
	[ALC861_FIXUP_FSC_AMILO_PI1505] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x0b, 0x0221101f }, /* HP */
			{ 0x0f, 0x90170310 }, /* speaker */
			{ }
		}
	},
	[ALC861_FIXUP_AMP_VREF_0F] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc861_fixup_asus_amp_vref_0f,
	},
	[ALC861_FIXUP_NO_JACK_DETECT] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc_fixup_no_jack_detect,
	},
	[ALC861_FIXUP_ASUS_A6RP] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc861_fixup_asus_amp_vref_0f,
		.chained = true,
		.chain_id = ALC861_FIXUP_NO_JACK_DETECT,
	}
};

static const struct snd_pci_quirk alc861_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1393, "ASUS A6Rp", ALC861_FIXUP_ASUS_A6RP),
	SND_PCI_QUIRK_VENDOR(0x1043, "ASUS laptop", ALC861_FIXUP_AMP_VREF_0F),
	SND_PCI_QUIRK(0x1462, 0x7254, "HP DX2200", ALC861_FIXUP_NO_JACK_DETECT),
	SND_PCI_QUIRK(0x1584, 0x2b01, "Haier W18", ALC861_FIXUP_AMP_VREF_0F),
	SND_PCI_QUIRK(0x1584, 0x0000, "Uniwill ECS M31EI", ALC861_FIXUP_AMP_VREF_0F),
	SND_PCI_QUIRK(0x1734, 0x10c7, "FSC Amilo Pi1505", ALC861_FIXUP_FSC_AMILO_PI1505),
	{}
};

/*
 */
static int patch_alc861(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x15;

	alc_pick_fixup(codec, NULL, alc861_fixup_tbl, alc861_fixups);
	alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc861_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->no_analog) {
		err = snd_hda_attach_beep_device(codec, 0x23);
		if (err < 0)
			goto error;
		set_beep_amp(spec, 0x23, 0, HDA_OUTPUT);
	}

	codec->patch_ops = alc_patch_ops;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->power_hook = alc_power_eapd;
#endif

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
}

/*
 * ALC861-VD support
 *
 * Based on ALC882
 *
 * In addition, an independent DAC
 */
static int alc861vd_parse_auto_config(struct hda_codec *codec)
{
	static const hda_nid_t alc861vd_ignore[] = { 0x1d, 0 };
	static const hda_nid_t alc861vd_ssids[] = { 0x15, 0x1b, 0x14, 0 };
	return alc_parse_auto_config(codec, alc861vd_ignore, alc861vd_ssids);
}

enum {
	ALC660VD_FIX_ASUS_GPIO1,
	ALC861VD_FIX_DALLAS,
};

/* exclude VREF80 */
static void alc861vd_fixup_dallas(struct hda_codec *codec,
				  const struct alc_fixup *fix, int action)
{
	if (action == ALC_FIXUP_ACT_PRE_PROBE) {
		snd_hda_override_pin_caps(codec, 0x18, 0x00001714);
		snd_hda_override_pin_caps(codec, 0x19, 0x0000171c);
	}
}

static const struct alc_fixup alc861vd_fixups[] = {
	[ALC660VD_FIX_ASUS_GPIO1] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* reset GPIO1 */
			{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
			{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
			{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
			{ }
		}
	},
	[ALC861VD_FIX_DALLAS] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc861vd_fixup_dallas,
	},
};

static const struct snd_pci_quirk alc861vd_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30bf, "HP TX1000", ALC861VD_FIX_DALLAS),
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS A7-K", ALC660VD_FIX_ASUS_GPIO1),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba L30-149", ALC861VD_FIX_DALLAS),
	{}
};

static const struct hda_verb alc660vd_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/*
 */
static int patch_alc861vd(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;

	alc_pick_fixup(codec, NULL, alc861vd_fixup_tbl, alc861vd_fixups);
	alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = alc861vd_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (codec->vendor_id == 0x10ec0660) {
		/* always turn on EAPD */
		add_verb(spec, alc660vd_eapd_verbs);
	}

	if (!spec->no_analog) {
		err = snd_hda_attach_beep_device(codec, 0x23);
		if (err < 0)
			goto error;
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
	}

	codec->patch_ops = alc_patch_ops;

	spec->shutup = alc_eapd_shutup;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
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
	ALC662_FIXUP_ASUS_MODE1,
	ALC662_FIXUP_ASUS_MODE2,
	ALC662_FIXUP_ASUS_MODE3,
	ALC662_FIXUP_ASUS_MODE4,
	ALC662_FIXUP_ASUS_MODE5,
	ALC662_FIXUP_ASUS_MODE6,
	ALC662_FIXUP_ASUS_MODE7,
	ALC662_FIXUP_ASUS_MODE8,
	ALC662_FIXUP_NO_JACK_DETECT,
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
	[ALC662_FIXUP_ASUS_MODE1] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19c20 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ 0x21, 0x0121401f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE2] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x18, 0x01a19820 }, /* mic */
			{ 0x19, 0x99a3092f }, /* int-mic */
			{ 0x1b, 0x0121401f }, /* HP out */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE3] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121441f }, /* HP */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x21, 0x01211420 }, /* HP2 */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE4] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x16, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x21, 0x0121441f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE5] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0121441f }, /* HP */
			{ 0x16, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE6] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x01211420 }, /* HP2 */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x1b, 0x0121441f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE7] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x17, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x19, 0x99a3094f }, /* int-mic */
			{ 0x1b, 0x01214020 }, /* HP */
			{ 0x21, 0x0121401f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_ASUS_MODE8] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x12, 0x99a30970 }, /* int-mic */
			{ 0x15, 0x01214020 }, /* HP */
			{ 0x17, 0x99130111 }, /* speaker */
			{ 0x18, 0x01a19840 }, /* mic */
			{ 0x21, 0x0121401f }, /* HP */
			{ }
		},
		.chained = true,
		.chain_id = ALC662_FIXUP_SKU_IGNORE
	},
	[ALC662_FIXUP_NO_JACK_DETECT] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc_fixup_no_jack_detect,
	},
};

static const struct snd_pci_quirk alc662_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x9087, "ECS", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1025, 0x0308, "Acer Aspire 8942G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x031c, "Gateway NV79", ALC662_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x1025, 0x038b, "Acer Aspire 8943G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x103c, 0x1632, "HP RP5800", ALC662_FIXUP_HP_RP5800),
	SND_PCI_QUIRK(0x1043, 0x8469, "ASUS mobo", ALC662_FIXUP_NO_JACK_DETECT),
	SND_PCI_QUIRK(0x105b, 0x0cd6, "Foxconn", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x144d, 0xc051, "Samsung R720", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x38af, "Lenovo Ideapad Y550P", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Ideapad Y550", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x1b35, 0x2206, "CZC P10T", ALC662_FIXUP_CZC_P10T),

#if 0
	/* Below is a quirk table taken from the old code.
	 * Basically the device should work as is without the fixup table.
	 * If BIOS doesn't give a proper info, enable the corresponding
	 * fixup entry.
	 */
	SND_PCI_QUIRK(0x1043, 0x1000, "ASUS N50Vm", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1092, "ASUS NB", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1173, "ASUS K73Jn", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x11c3, "ASUS M70V", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x11d3, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x11f3, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1203, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1303, "ASUS G60J", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1333, "ASUS G60Jx", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x13e3, "ASUS N71JA", ALC662_FIXUP_ASUS_MODE7),
	SND_PCI_QUIRK(0x1043, 0x1463, "ASUS N71", ALC662_FIXUP_ASUS_MODE7),
	SND_PCI_QUIRK(0x1043, 0x14d3, "ASUS G72", ALC662_FIXUP_ASUS_MODE8),
	SND_PCI_QUIRK(0x1043, 0x1563, "ASUS N90", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x15d3, "ASUS N50SF F50SF", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x16c3, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x16f3, "ASUS K40C K50C", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1733, "ASUS N81De", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1753, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1763, "ASUS NB", ALC662_FIXUP_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1765, "ASUS NB", ALC662_FIXUP_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1783, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1793, "ASUS F50GX", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x17b3, "ASUS F70SL", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x17f3, "ASUS X58LE", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1813, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1823, "ASUS NB", ALC662_FIXUP_ASUS_MODE5),
	SND_PCI_QUIRK(0x1043, 0x1833, "ASUS NB", ALC662_FIXUP_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1843, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1853, "ASUS F50Z", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1864, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1876, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1893, "ASUS M50Vm", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1894, "ASUS X55", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x18b3, "ASUS N80Vc", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18c3, "ASUS VX5", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18d3, "ASUS N81Te", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18f3, "ASUS N505Tp", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1903, "ASUS F5GL", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1913, "ASUS NB", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1933, "ASUS F80Q", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1943, "ASUS Vx3V", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1953, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1963, "ASUS X71C", ALC662_FIXUP_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1983, "ASUS N5051A", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1993, "ASUS N20", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19b3, "ASUS F7Z", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19c3, "ASUS F5Z/F6x", ALC662_FIXUP_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x19e3, "ASUS NB", ALC662_FIXUP_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19f3, "ASUS NB", ALC662_FIXUP_ASUS_MODE4),
#endif
	{}
};

static const struct alc_model_fixup alc662_fixup_models[] = {
	{.id = ALC272_FIXUP_MARIO, .name = "mario"},
	{.id = ALC662_FIXUP_ASUS_MODE1, .name = "asus-mode1"},
	{.id = ALC662_FIXUP_ASUS_MODE2, .name = "asus-mode2"},
	{.id = ALC662_FIXUP_ASUS_MODE3, .name = "asus-mode3"},
	{.id = ALC662_FIXUP_ASUS_MODE4, .name = "asus-mode4"},
	{.id = ALC662_FIXUP_ASUS_MODE5, .name = "asus-mode5"},
	{.id = ALC662_FIXUP_ASUS_MODE6, .name = "asus-mode6"},
	{.id = ALC662_FIXUP_ASUS_MODE7, .name = "asus-mode7"},
	{.id = ALC662_FIXUP_ASUS_MODE8, .name = "asus-mode8"},
	{}
};


/*
 */
static int patch_alc662(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err = 0;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	codec->spec = spec;

	spec->mixer_nid = 0x0b;

	/* handle multiple HPs as is */
	spec->parse_flags = HDA_PINCFG_NO_HP_FIXUP;

	alc_auto_parse_customize_define(codec);

	alc_fix_pll_init(codec, 0x20, 0x04, 15);

	err = alc_codec_rename_from_preset(codec);
	if (err < 0)
		goto error;

	if ((alc_get_coef0(codec) & (1 << 14)) &&
	    codec->bus->pci->subsystem_vendor == 0x1025 &&
	    spec->cdefine.platform_type == 1) {
		if (alc_codec_rename(codec, "ALC272X") < 0)
			goto error;
	}

	alc_pick_fixup(codec, alc662_fixup_models,
		       alc662_fixup_tbl, alc662_fixups);
	alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
	/* automatic parse from the BIOS config */
	err = alc662_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!spec->no_analog && has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0)
			goto error;
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

	codec->patch_ops = alc_patch_ops;
	spec->shutup = alc_eapd_shutup;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	return 0;

 error:
	alc_free(codec);
	return err;
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
static int patch_alc680(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* ALC680 has no aa-loopback mixer */

	/* automatic parse from the BIOS config */
	err = alc680_parse_auto_config(codec);
	if (err < 0) {
		alc_free(codec);
		return err;
	}

	codec->patch_ops = alc_patch_ops;

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
	{ .id = 0x10ec0662, .rev = 0x100300, .name = "ALC662 rev3",
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
	{ .id = 0x10ec0887, .name = "ALC887", .patch = patch_alc882 },
	{ .id = 0x10ec0888, .rev = 0x100101, .name = "ALC1200",
	  .patch = patch_alc882 },
	{ .id = 0x10ec0888, .name = "ALC888", .patch = patch_alc882 },
	{ .id = 0x10ec0889, .name = "ALC889", .patch = patch_alc882 },
	{ .id = 0x10ec0892, .name = "ALC892", .patch = patch_alc662 },
	{ .id = 0x10ec0899, .name = "ALC898", .patch = patch_alc882 },
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
