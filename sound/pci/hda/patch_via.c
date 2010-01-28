/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for VIA VT17xx/VT18xx/VT20xx codec
 *
 *  (C) 2006-2009 VIA Technology, Inc.
 *  (C) 2006-2008 Takashi Iwai <tiwai@suse.de>
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
/*									     */
/* 2006-03-03  Lydia Wang  Create the basic patch to support VT1708 codec    */
/* 2006-03-14  Lydia Wang  Modify hard code for some pin widget nid	     */
/* 2006-08-02  Lydia Wang  Add support to VT1709 codec			     */
/* 2006-09-08  Lydia Wang  Fix internal loopback recording source select bug */
/* 2007-09-12  Lydia Wang  Add EAPD enable during driver initialization	     */
/* 2007-09-17  Lydia Wang  Add VT1708B codec support			    */
/* 2007-11-14  Lydia Wang  Add VT1708A codec HP and CD pin connect config    */
/* 2008-02-03  Lydia Wang  Fix Rear channels and Back channels inverse issue */
/* 2008-03-06  Lydia Wang  Add VT1702 codec and VT1708S codec support	     */
/* 2008-04-09  Lydia Wang  Add mute front speaker when HP plugin	     */
/* 2008-04-09  Lydia Wang  Add Independent HP feature			     */
/* 2008-05-28  Lydia Wang  Add second S/PDIF Out support for VT1702	     */
/* 2008-09-15  Logan Li	   Add VT1708S Mic Boost workaround/backdoor	     */
/* 2009-02-16  Logan Li	   Add support for VT1718S			     */
/* 2009-03-13  Logan Li	   Add support for VT1716S			     */
/* 2009-04-14  Lydai Wang  Add support for VT1828S and VT2020		     */
/* 2009-07-08  Lydia Wang  Add support for VT2002P			     */
/* 2009-07-21  Lydia Wang  Add support for VT1812			     */
/* 2009-09-19  Lydia Wang  Add support for VT1818S			     */
/*									     */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include "hda_codec.h"
#include "hda_local.h"

/* amp values */
#define AMP_VAL_IDX_SHIFT	19
#define AMP_VAL_IDX_MASK	(0x0f<<19)

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

enum VIA_HDA_CODEC {
	UNKNOWN = -1,
	VT1708,
	VT1709_10CH,
	VT1709_6CH,
	VT1708B_8CH,
	VT1708B_4CH,
	VT1708S,
	VT1708BCE,
	VT1702,
	VT1718S,
	VT1716S,
	VT2002P,
	VT1812,
	CODEC_TYPES,
};

struct via_spec {
	/* codec parameterization */
	struct snd_kcontrol_new *mixers[6];
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
	hda_nid_t slave_dig_outs[2];

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t mux_nids[3];
	hda_nid_t dig_in_nid;
	hda_nid_t dig_in_pin;

	/* capture source */
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];

	/* PCM information */
	struct hda_pcm pcm_rec[3];

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	struct snd_array kctls;
	struct hda_input_mux private_imux[2];
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];

	/* HP mode source */
	const struct hda_input_mux *hp_mux;
	unsigned int hp_independent_mode;
	unsigned int hp_independent_mode_index;
	unsigned int smart51_enabled;
	unsigned int dmic_enabled;
	enum VIA_HDA_CODEC codec_type;

	/* work to check hp jack state */
	struct hda_codec *codec;
	struct delayed_work vt1708_hp_work;
	int vt1708_jack_detectect;
	int vt1708_hp_present;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_loopback_check loopback;
#endif
};

static enum VIA_HDA_CODEC get_codec_type(struct hda_codec *codec)
{
	u32 vendor_id = codec->vendor_id;
	u16 ven_id = vendor_id >> 16;
	u16 dev_id = vendor_id & 0xffff;
	enum VIA_HDA_CODEC codec_type;

	/* get codec type */
	if (ven_id != 0x1106)
		codec_type = UNKNOWN;
	else if (dev_id >= 0x1708 && dev_id <= 0x170b)
		codec_type = VT1708;
	else if (dev_id >= 0xe710 && dev_id <= 0xe713)
		codec_type = VT1709_10CH;
	else if (dev_id >= 0xe714 && dev_id <= 0xe717)
		codec_type = VT1709_6CH;
	else if (dev_id >= 0xe720 && dev_id <= 0xe723) {
		codec_type = VT1708B_8CH;
		if (snd_hda_param_read(codec, 0x16, AC_PAR_CONNLIST_LEN) == 0x7)
			codec_type = VT1708BCE;
	} else if (dev_id >= 0xe724 && dev_id <= 0xe727)
		codec_type = VT1708B_4CH;
	else if ((dev_id & 0xfff) == 0x397
		 && (dev_id >> 12) < 8)
		codec_type = VT1708S;
	else if ((dev_id & 0xfff) == 0x398
		 && (dev_id >> 12) < 8)
		codec_type = VT1702;
	else if ((dev_id & 0xfff) == 0x428
		 && (dev_id >> 12) < 8)
		codec_type = VT1718S;
	else if (dev_id == 0x0433 || dev_id == 0xa721)
		codec_type = VT1716S;
	else if (dev_id == 0x0441 || dev_id == 0x4441)
		codec_type = VT1718S;
	else if (dev_id == 0x0438 || dev_id == 0x4438)
		codec_type = VT2002P;
	else if (dev_id == 0x0448)
		codec_type = VT1812;
	else if (dev_id == 0x0440)
		codec_type = VT1708S;
	else
		codec_type = UNKNOWN;
	return codec_type;
};

#define VIA_HP_EVENT		0x01
#define VIA_GPIO_EVENT		0x02
#define VIA_JACK_EVENT		0x04
#define VIA_MONO_EVENT		0x08
#define VIA_SPEAKER_EVENT	0x10
#define VIA_BIND_HP_EVENT	0x20

enum {
	VIA_CTL_WIDGET_VOL,
	VIA_CTL_WIDGET_MUTE,
	VIA_CTL_WIDGET_ANALOG_MUTE,
	VIA_CTL_WIDGET_BIND_PIN_MUTE,
};

enum {
	AUTO_SEQ_FRONT = 0,
	AUTO_SEQ_SURROUND,
	AUTO_SEQ_CENLFE,
	AUTO_SEQ_SIDE
};

static void analog_low_current_mode(struct hda_codec *codec, int stream_idle);
static void set_jack_power_state(struct hda_codec *codec);
static int is_aa_path_mute(struct hda_codec *codec);

static void vt1708_start_hp_work(struct via_spec *spec)
{
	if (spec->codec_type != VT1708 || spec->autocfg.hp_pins[0] == 0)
		return;
	snd_hda_codec_write(spec->codec, 0x1, 0, 0xf81,
			    !spec->vt1708_jack_detectect);
	if (!delayed_work_pending(&spec->vt1708_hp_work))
		schedule_delayed_work(&spec->vt1708_hp_work,
				      msecs_to_jiffies(100));
}

static void vt1708_stop_hp_work(struct via_spec *spec)
{
	if (spec->codec_type != VT1708 || spec->autocfg.hp_pins[0] == 0)
		return;
	if (snd_hda_get_bool_hint(spec->codec, "analog_loopback_hp_detect") == 1
	    && !is_aa_path_mute(spec->codec))
		return;
	snd_hda_codec_write(spec->codec, 0x1, 0, 0xf81,
			    !spec->vt1708_jack_detectect);
	cancel_delayed_work(&spec->vt1708_hp_work);
	flush_scheduled_work();
}


static int analog_input_switch_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	int change = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	set_jack_power_state(codec);
	analog_low_current_mode(snd_kcontrol_chip(kcontrol), -1);
	if (snd_hda_get_bool_hint(codec, "analog_loopback_hp_detect") == 1) {
		if (is_aa_path_mute(codec))
			vt1708_start_hp_work(codec->spec);
		else
			vt1708_stop_hp_work(codec->spec);
	}
	return change;
}

/* modify .put = snd_hda_mixer_amp_switch_put */
#define ANALOG_INPUT_MUTE						\
	{		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,		\
			.name = NULL,					\
			.index = 0,					\
			.info = snd_hda_mixer_amp_switch_info,		\
			.get = snd_hda_mixer_amp_switch_get,		\
			.put = analog_input_switch_put,			\
			.private_value = HDA_COMPOSE_AMP_VAL(0, 3, 0, 0) }

static void via_hp_bind_automute(struct hda_codec *codec);

static int bind_pin_switch_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int i;
	int change = 0;

	long *valp = ucontrol->value.integer.value;
	int lmute, rmute;
	if (strstr(kcontrol->id.name, "Switch") == NULL) {
		snd_printd("Invalid control!\n");
		return change;
	}
	change = snd_hda_mixer_amp_switch_put(kcontrol,
					      ucontrol);
	/* Get mute value */
	lmute = *valp ? 0 : HDA_AMP_MUTE;
	valp++;
	rmute = *valp ? 0 : HDA_AMP_MUTE;

	/* Set hp pins */
	if (!spec->hp_independent_mode) {
		for (i = 0; i < spec->autocfg.hp_outs; i++) {
			snd_hda_codec_amp_update(
				codec, spec->autocfg.hp_pins[i],
				0, HDA_OUTPUT, 0, HDA_AMP_MUTE,
				lmute);
			snd_hda_codec_amp_update(
				codec, spec->autocfg.hp_pins[i],
				1, HDA_OUTPUT, 0, HDA_AMP_MUTE,
				rmute);
		}
	}

	if (!lmute && !rmute) {
		/* Line Outs */
		for (i = 0; i < spec->autocfg.line_outs; i++)
			snd_hda_codec_amp_stereo(
				codec, spec->autocfg.line_out_pins[i],
				HDA_OUTPUT, 0, HDA_AMP_MUTE, 0);
		/* Speakers */
		for (i = 0; i < spec->autocfg.speaker_outs; i++)
			snd_hda_codec_amp_stereo(
				codec, spec->autocfg.speaker_pins[i],
				HDA_OUTPUT, 0, HDA_AMP_MUTE, 0);
		/* unmute */
		via_hp_bind_automute(codec);

	} else {
		if (lmute) {
			/* Mute all left channels */
			for (i = 1; i < spec->autocfg.line_outs; i++)
				snd_hda_codec_amp_update(
					codec,
					spec->autocfg.line_out_pins[i],
					0, HDA_OUTPUT, 0, HDA_AMP_MUTE,
					lmute);
			for (i = 0; i < spec->autocfg.speaker_outs; i++)
				snd_hda_codec_amp_update(
					codec,
					spec->autocfg.speaker_pins[i],
					0, HDA_OUTPUT, 0, HDA_AMP_MUTE,
					lmute);
		}
		if (rmute) {
			/* mute all right channels */
			for (i = 1; i < spec->autocfg.line_outs; i++)
				snd_hda_codec_amp_update(
					codec,
					spec->autocfg.line_out_pins[i],
					1, HDA_OUTPUT, 0, HDA_AMP_MUTE,
					rmute);
			for (i = 0; i < spec->autocfg.speaker_outs; i++)
				snd_hda_codec_amp_update(
					codec,
					spec->autocfg.speaker_pins[i],
					1, HDA_OUTPUT, 0, HDA_AMP_MUTE,
					rmute);
		}
	}
	return change;
}

#define BIND_PIN_MUTE							\
	{		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,		\
			.name = NULL,					\
			.index = 0,					\
			.info = snd_hda_mixer_amp_switch_info,		\
			.get = snd_hda_mixer_amp_switch_get,		\
			.put = bind_pin_switch_put,			\
			.private_value = HDA_COMPOSE_AMP_VAL(0, 3, 0, 0) }

static struct snd_kcontrol_new via_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	ANALOG_INPUT_MUTE,
	BIND_PIN_MUTE,
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

static hda_nid_t vt1718S_adc_nids[2] = {
	/* ADC1-2 */
	0x10, 0x11
};

static hda_nid_t vt1716S_adc_nids[2] = {
	/* ADC1-2 */
	0x13, 0x14
};

static hda_nid_t vt2002P_adc_nids[2] = {
	/* ADC1-2 */
	0x10, 0x11
};

static hda_nid_t vt1812_adc_nids[2] = {
	/* ADC1-2 */
	0x10, 0x11
};


/* add dynamic controls */
static int via_add_control(struct via_spec *spec, int type, const char *name,
			   unsigned long val)
{
	struct snd_kcontrol_new *knew;

	snd_array_init(&spec->kctls, sizeof(*knew), 32);
	knew = snd_array_new(&spec->kctls);
	if (!knew)
		return -ENOMEM;
	*knew = via_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);
	if (!knew->name)
		return -ENOMEM;
	if (get_amp_nid_(val))
		knew->subdevice = HDA_SUBDEV_NID_FLAG | get_amp_nid_(val);
	knew->private_value = val;
	return 0;
}

static void via_free_kctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;

	if (spec->kctls.list) {
		struct snd_kcontrol_new *kctl = spec->kctls.list;
		int i;
		for (i = 0; i < spec->kctls.used; i++)
			kfree(kctl[i].name);
	}
	snd_array_free(&spec->kctls);
}

/* create input playback/capture controls for the given pin */
static int via_new_analog_input(struct via_spec *spec, const char *ctlname,
				int idx, int mix_nid)
{
	char name[32];
	int err;

	sprintf(name, "%s Playback Volume", ctlname);
	err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
			      HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	sprintf(name, "%s Playback Switch", ctlname);
	err = via_add_control(spec, VIA_CTL_WIDGET_ANALOG_MUTE, name,
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
	if (snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_EAPD)
		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_EAPD_BTLENABLE, 0x02);
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
	int i;

	for (i = 0; i < spec->autocfg.hp_outs; i++) {
		pin = spec->autocfg.hp_pins[i];
		if (pin) /* connect to front */
			via_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	}
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

static int is_smart51_pins(struct via_spec *spec, hda_nid_t pin);

static void set_pin_power_state(struct hda_codec *codec, hda_nid_t nid,
				unsigned int *affected_parm)
{
	unsigned parm;
	unsigned def_conf = snd_hda_codec_get_pincfg(codec, nid);
	unsigned no_presence = (def_conf & AC_DEFCFG_MISC)
		>> AC_DEFCFG_MISC_SHIFT
		& AC_DEFCFG_MISC_NO_PRESENCE; /* do not support pin sense */
	unsigned present = snd_hda_jack_detect(codec, nid);
	struct via_spec *spec = codec->spec;
	if ((spec->smart51_enabled && is_smart51_pins(spec, nid))
	    || ((no_presence || present)
		&& get_defcfg_connect(def_conf) != AC_JACK_PORT_NONE)) {
		*affected_parm = AC_PWRST_D0; /* if it's connected */
		parm = AC_PWRST_D0;
	} else
		parm = AC_PWRST_D3;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE, parm);
}

static void set_jack_power_state(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int imux_is_smixer;
	unsigned int parm;

	if (spec->codec_type == VT1702) {
		imux_is_smixer = snd_hda_codec_read(
			codec, 0x13, 0, AC_VERB_GET_CONNECT_SEL, 0x00) == 3;
		/* inputs */
		/* PW 1/2/5 (14h/15h/18h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x14, &parm);
		set_pin_power_state(codec, 0x15, &parm);
		set_pin_power_state(codec, 0x18, &parm);
		if (imux_is_smixer)
			parm = AC_PWRST_D0; /* SW0 = stereo mixer (idx 3) */
		/* SW0 (13h), AIW 0/1/2 (12h/1fh/20h) */
		snd_hda_codec_write(codec, 0x13, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x12, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x1f, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* outputs */
		/* PW 3/4 (16h/17h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x16, &parm);
		set_pin_power_state(codec, 0x17, &parm);
		/* MW0 (1ah), AOW 0/1 (10h/1dh) */
		snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_POWER_STATE,
				    imux_is_smixer ? AC_PWRST_D0 : parm);
		snd_hda_codec_write(codec, 0x10, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x1d, 0, AC_VERB_SET_POWER_STATE,
				    parm);
	} else if (spec->codec_type == VT1708B_8CH
		   || spec->codec_type == VT1708B_4CH
		   || spec->codec_type == VT1708S) {
		/* SW0 (17h) = stereo mixer */
		int is_8ch = spec->codec_type != VT1708B_4CH;
		imux_is_smixer = snd_hda_codec_read(
			codec, 0x17, 0, AC_VERB_GET_CONNECT_SEL, 0x00)
			== ((spec->codec_type == VT1708S)  ? 5 : 0);
		/* inputs */
		/* PW 1/2/5 (1ah/1bh/1eh) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x1a, &parm);
		set_pin_power_state(codec, 0x1b, &parm);
		set_pin_power_state(codec, 0x1e, &parm);
		if (imux_is_smixer)
			parm = AC_PWRST_D0;
		/* SW0 (17h), AIW 0/1 (13h/14h) */
		snd_hda_codec_write(codec, 0x17, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x13, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x14, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* outputs */
		/* PW0 (19h), SW1 (18h), AOW1 (11h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x19, &parm);
		snd_hda_codec_write(codec, 0x18, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x11, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* PW6 (22h), SW2 (26h), AOW2 (24h) */
		if (is_8ch) {
			parm = AC_PWRST_D3;
			set_pin_power_state(codec, 0x22, &parm);
			snd_hda_codec_write(codec, 0x26, 0,
					    AC_VERB_SET_POWER_STATE, parm);
			snd_hda_codec_write(codec, 0x24, 0,
					    AC_VERB_SET_POWER_STATE, parm);
		}

		/* PW 3/4/7 (1ch/1dh/23h) */
		parm = AC_PWRST_D3;
		/* force to D0 for internal Speaker */
		set_pin_power_state(codec, 0x1c, &parm);
		set_pin_power_state(codec, 0x1d, &parm);
		if (is_8ch)
			set_pin_power_state(codec, 0x23, &parm);
		/* MW0 (16h), Sw3 (27h), AOW 0/3 (10h/25h) */
		snd_hda_codec_write(codec, 0x16, 0, AC_VERB_SET_POWER_STATE,
				    imux_is_smixer ? AC_PWRST_D0 : parm);
		snd_hda_codec_write(codec, 0x10, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		if (is_8ch) {
			snd_hda_codec_write(codec, 0x25, 0,
					    AC_VERB_SET_POWER_STATE, parm);
			snd_hda_codec_write(codec, 0x27, 0,
					    AC_VERB_SET_POWER_STATE, parm);
		}
	}  else if (spec->codec_type == VT1718S) {
		/* MUX6 (1eh) = stereo mixer */
		imux_is_smixer = snd_hda_codec_read(
			codec, 0x1e, 0, AC_VERB_GET_CONNECT_SEL, 0x00) == 5;
		/* inputs */
		/* PW 5/6/7 (29h/2ah/2bh) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x29, &parm);
		set_pin_power_state(codec, 0x2a, &parm);
		set_pin_power_state(codec, 0x2b, &parm);
		if (imux_is_smixer)
			parm = AC_PWRST_D0;
		/* MUX6/7 (1eh/1fh), AIW 0/1 (10h/11h) */
		snd_hda_codec_write(codec, 0x1e, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x1f, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x10, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x11, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* outputs */
		/* PW3 (27h), MW2 (1ah), AOW3 (bh) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x27, &parm);
		snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0xb, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* PW2 (26h), AOW2 (ah) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x26, &parm);
		snd_hda_codec_write(codec, 0xa, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* PW0/1 (24h/25h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x24, &parm);
		set_pin_power_state(codec, 0x25, &parm);
		if (!spec->hp_independent_mode) /* check for redirected HP */
			set_pin_power_state(codec, 0x28, &parm);
		snd_hda_codec_write(codec, 0x8, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x9, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		/* MW9 (21h), Mw2 (1ah), AOW0 (8h) */
		snd_hda_codec_write(codec, 0x21, 0, AC_VERB_SET_POWER_STATE,
				    imux_is_smixer ? AC_PWRST_D0 : parm);
		if (spec->hp_independent_mode) {
			/* PW4 (28h), MW3 (1bh), MUX1(34h), AOW4 (ch) */
			parm = AC_PWRST_D3;
			set_pin_power_state(codec, 0x28, &parm);
			snd_hda_codec_write(codec, 0x1b, 0,
					    AC_VERB_SET_POWER_STATE, parm);
			snd_hda_codec_write(codec, 0x34, 0,
					    AC_VERB_SET_POWER_STATE, parm);
			snd_hda_codec_write(codec, 0xc, 0,
					    AC_VERB_SET_POWER_STATE, parm);
		}
	} else if (spec->codec_type == VT1716S) {
		unsigned int mono_out, present;
		/* SW0 (17h) = stereo mixer */
		imux_is_smixer = snd_hda_codec_read(
			codec, 0x17, 0, AC_VERB_GET_CONNECT_SEL, 0x00) ==  5;
		/* inputs */
		/* PW 1/2/5 (1ah/1bh/1eh) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x1a, &parm);
		set_pin_power_state(codec, 0x1b, &parm);
		set_pin_power_state(codec, 0x1e, &parm);
		if (imux_is_smixer)
			parm = AC_PWRST_D0;
		/* SW0 (17h), AIW0(13h) */
		snd_hda_codec_write(codec, 0x17, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x13, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x1e, &parm);
		/* PW11 (22h) */
		if (spec->dmic_enabled)
			set_pin_power_state(codec, 0x22, &parm);
		else
			snd_hda_codec_write(
				codec, 0x22, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D3);

		/* SW2(26h), AIW1(14h) */
		snd_hda_codec_write(codec, 0x26, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x14, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* outputs */
		/* PW0 (19h), SW1 (18h), AOW1 (11h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x19, &parm);
		/* Smart 5.1 PW2(1bh) */
		if (spec->smart51_enabled)
			set_pin_power_state(codec, 0x1b, &parm);
		snd_hda_codec_write(codec, 0x18, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x11, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* PW7 (23h), SW3 (27h), AOW3 (25h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x23, &parm);
		/* Smart 5.1 PW1(1ah) */
		if (spec->smart51_enabled)
			set_pin_power_state(codec, 0x1a, &parm);
		snd_hda_codec_write(codec, 0x27, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* Smart 5.1 PW5(1eh) */
		if (spec->smart51_enabled)
			set_pin_power_state(codec, 0x1e, &parm);
		snd_hda_codec_write(codec, 0x25, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* Mono out */
		/* SW4(28h)->MW1(29h)-> PW12 (2ah)*/
		present = snd_hda_jack_detect(codec, 0x1c);
		if (present)
			mono_out = 0;
		else {
			present = snd_hda_jack_detect(codec, 0x1d);
			if (!spec->hp_independent_mode && present)
				mono_out = 0;
			else
				mono_out = 1;
		}
		parm = mono_out ? AC_PWRST_D0 : AC_PWRST_D3;
		snd_hda_codec_write(codec, 0x28, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x29, 0, AC_VERB_SET_POWER_STATE,
				    parm);
		snd_hda_codec_write(codec, 0x2a, 0, AC_VERB_SET_POWER_STATE,
				    parm);

		/* PW 3/4 (1ch/1dh) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x1c, &parm);
		set_pin_power_state(codec, 0x1d, &parm);
		/* HP Independent Mode, power on AOW3 */
		if (spec->hp_independent_mode)
			snd_hda_codec_write(codec, 0x25, 0,
					    AC_VERB_SET_POWER_STATE, parm);

		/* force to D0 for internal Speaker */
		/* MW0 (16h), AOW0 (10h) */
		snd_hda_codec_write(codec, 0x16, 0, AC_VERB_SET_POWER_STATE,
				    imux_is_smixer ? AC_PWRST_D0 : parm);
		snd_hda_codec_write(codec, 0x10, 0, AC_VERB_SET_POWER_STATE,
				    mono_out ? AC_PWRST_D0 : parm);
	} else if (spec->codec_type == VT2002P) {
		unsigned int present;
		/* MUX9 (1eh) = stereo mixer */
		imux_is_smixer = snd_hda_codec_read(
			codec, 0x1e, 0, AC_VERB_GET_CONNECT_SEL, 0x00) == 3;
		/* inputs */
		/* PW 5/6/7 (29h/2ah/2bh) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x29, &parm);
		set_pin_power_state(codec, 0x2a, &parm);
		set_pin_power_state(codec, 0x2b, &parm);
		if (imux_is_smixer)
			parm = AC_PWRST_D0;
		/* MUX9/10 (1eh/1fh), AIW 0/1 (10h/11h) */
		snd_hda_codec_write(codec, 0x1e, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x1f, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x10, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x11, 0,
				    AC_VERB_SET_POWER_STATE, parm);

		/* outputs */
		/* AOW0 (8h)*/
		snd_hda_codec_write(codec, 0x8, 0,
				    AC_VERB_SET_POWER_STATE, AC_PWRST_D0);

		/* PW4 (26h), MW4 (1ch), MUX4(37h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x26, &parm);
		snd_hda_codec_write(codec, 0x1c, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x37,
				    0, AC_VERB_SET_POWER_STATE, parm);

		/* PW1 (25h), MW1 (19h), MUX1(35h), AOW1 (9h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x25, &parm);
		snd_hda_codec_write(codec, 0x19, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x35, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		if (spec->hp_independent_mode)	{
			snd_hda_codec_write(codec, 0x9, 0,
					    AC_VERB_SET_POWER_STATE, parm);
		}

		/* Class-D */
		/* PW0 (24h), MW0(18h), MUX0(34h) */
		present = snd_hda_jack_detect(codec, 0x25);
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x24, &parm);
		if (present) {
			snd_hda_codec_write(
				codec, 0x18, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
			snd_hda_codec_write(
				codec, 0x34, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
		} else {
			snd_hda_codec_write(
				codec, 0x18, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
			snd_hda_codec_write(
				codec, 0x34, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
		}

		/* Mono Out */
		/* PW15 (31h), MW8(17h), MUX8(3bh) */
		present = snd_hda_jack_detect(codec, 0x26);
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x31, &parm);
		if (present) {
			snd_hda_codec_write(
				codec, 0x17, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
			snd_hda_codec_write(
				codec, 0x3b, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
		} else {
			snd_hda_codec_write(
				codec, 0x17, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
			snd_hda_codec_write(
				codec, 0x3b, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
		}

		/* MW9 (21h) */
		if (imux_is_smixer || !is_aa_path_mute(codec))
			snd_hda_codec_write(
				codec, 0x21, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
		else
			snd_hda_codec_write(
				codec, 0x21, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
	} else if (spec->codec_type == VT1812) {
		unsigned int present;
		/* MUX10 (1eh) = stereo mixer */
		imux_is_smixer = snd_hda_codec_read(
			codec, 0x1e, 0, AC_VERB_GET_CONNECT_SEL, 0x00) == 5;
		/* inputs */
		/* PW 5/6/7 (29h/2ah/2bh) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x29, &parm);
		set_pin_power_state(codec, 0x2a, &parm);
		set_pin_power_state(codec, 0x2b, &parm);
		if (imux_is_smixer)
			parm = AC_PWRST_D0;
		/* MUX10/11 (1eh/1fh), AIW 0/1 (10h/11h) */
		snd_hda_codec_write(codec, 0x1e, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x1f, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x10, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x11, 0,
				    AC_VERB_SET_POWER_STATE, parm);

		/* outputs */
		/* AOW0 (8h)*/
		snd_hda_codec_write(codec, 0x8, 0,
				    AC_VERB_SET_POWER_STATE, AC_PWRST_D0);

		/* PW4 (28h), MW4 (18h), MUX4(38h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x28, &parm);
		snd_hda_codec_write(codec, 0x18, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x38, 0,
				    AC_VERB_SET_POWER_STATE, parm);

		/* PW1 (25h), MW1 (15h), MUX1(35h), AOW1 (9h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x25, &parm);
		snd_hda_codec_write(codec, 0x15, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x35, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		if (spec->hp_independent_mode)	{
			snd_hda_codec_write(codec, 0x9, 0,
					    AC_VERB_SET_POWER_STATE, parm);
		}

		/* Internal Speaker */
		/* PW0 (24h), MW0(14h), MUX0(34h) */
		present = snd_hda_jack_detect(codec, 0x25);
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x24, &parm);
		if (present) {
			snd_hda_codec_write(codec, 0x14, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D3);
			snd_hda_codec_write(codec, 0x34, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D3);
		} else {
			snd_hda_codec_write(codec, 0x14, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D0);
			snd_hda_codec_write(codec, 0x34, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D0);
		}
		/* Mono Out */
		/* PW13 (31h), MW13(1ch), MUX13(3ch), MW14(3eh) */
		present = snd_hda_jack_detect(codec, 0x28);
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x31, &parm);
		if (present) {
			snd_hda_codec_write(codec, 0x1c, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D3);
			snd_hda_codec_write(codec, 0x3c, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D3);
			snd_hda_codec_write(codec, 0x3e, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D3);
		} else {
			snd_hda_codec_write(codec, 0x1c, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D0);
			snd_hda_codec_write(codec, 0x3c, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D0);
			snd_hda_codec_write(codec, 0x3e, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D0);
		}

		/* PW15 (33h), MW15 (1dh), MUX15(3dh) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x33, &parm);
		snd_hda_codec_write(codec, 0x1d, 0,
				    AC_VERB_SET_POWER_STATE, parm);
		snd_hda_codec_write(codec, 0x3d, 0,
				    AC_VERB_SET_POWER_STATE, parm);

		/* MW9 (21h) */
		if (imux_is_smixer || !is_aa_path_mute(codec))
			snd_hda_codec_write(
				codec, 0x21, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
		else
			snd_hda_codec_write(
				codec, 0x21, 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
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

	if (!spec->mux_nids[adc_idx])
		return -EINVAL;
	/* switch to D0 beofre change index */
	if (snd_hda_codec_read(codec, spec->mux_nids[adc_idx], 0,
			       AC_VERB_GET_POWER_STATE, 0x00) != AC_PWRST_D0)
		snd_hda_codec_write(codec, spec->mux_nids[adc_idx], 0,
				    AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
	/* update jack power state */
	set_jack_power_state(codec);

	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->mux_nids[adc_idx],
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
	hda_nid_t nid;
	unsigned int pinsel;

	switch (spec->codec_type) {
	case VT1718S:
		nid = 0x34;
		break;
	case VT2002P:
		nid = 0x35;
		break;
	case VT1812:
		nid = 0x3d;
		break;
	default:
		nid = spec->autocfg.hp_pins[0];
		break;
	}
	/* use !! to translate conn sel 2 for VT1718S */
	pinsel = !!snd_hda_codec_read(codec, nid, 0,
				      AC_VERB_GET_CONNECT_SEL,
				      0x00);
	ucontrol->value.enumerated.item[0] = pinsel;

	return 0;
}

static void activate_ctl(struct hda_codec *codec, const char *name, int active)
{
	struct snd_kcontrol *ctl = snd_hda_find_mixer_ctl(codec, name);
	if (ctl) {
		ctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		ctl->vd[0].access |= active
			? 0 : SNDRV_CTL_ELEM_ACCESS_INACTIVE;
		snd_ctl_notify(codec->bus->card,
			       SNDRV_CTL_EVENT_MASK_VALUE, &ctl->id);
	}
}

static int update_side_mute_status(struct hda_codec *codec)
{
	/* mute side channel */
	struct via_spec *spec = codec->spec;
	unsigned int parm = spec->hp_independent_mode
		? AMP_OUT_MUTE : AMP_OUT_UNMUTE;
	hda_nid_t sw3;

	switch (spec->codec_type) {
	case VT1708:
		sw3 = 0x1b;
		break;
	case VT1709_10CH:
		sw3 = 0x29;
		break;
	case VT1708B_8CH:
	case VT1708S:
		sw3 = 0x27;
		break;
	default:
		sw3 = 0;
		break;
	}

	if (sw3)
		snd_hda_codec_write(codec, sw3, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    parm);
	return 0;
}

static int via_independent_hp_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	hda_nid_t nid = spec->autocfg.hp_pins[0];
	unsigned int pinsel = ucontrol->value.enumerated.item[0];
	/* Get Independent Mode index of headphone pin widget */
	spec->hp_independent_mode = spec->hp_independent_mode_index == pinsel
		? 1 : 0;

	switch (spec->codec_type) {
	case VT1718S:
		nid = 0x34;
		pinsel = pinsel ? 2 : 0; /* indep HP use AOW4 (index 2) */
		spec->multiout.num_dacs = 4;
		break;
	case VT2002P:
		nid = 0x35;
		break;
	case VT1812:
		nid = 0x3d;
		break;
	default:
		nid = spec->autocfg.hp_pins[0];
		break;
	}
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CONNECT_SEL, pinsel);

	if (spec->multiout.hp_nid && spec->multiout.hp_nid
	    != spec->multiout.dac_nids[HDA_FRONT])
		snd_hda_codec_setup_stream(codec, spec->multiout.hp_nid,
					   0, 0, 0);

	update_side_mute_status(codec);
	/* update HP volume/swtich active state */
	if (spec->codec_type == VT1708S
	    || spec->codec_type == VT1702
	    || spec->codec_type == VT1718S
	    || spec->codec_type == VT1716S
	    || spec->codec_type == VT2002P
	    || spec->codec_type == VT1812) {
		activate_ctl(codec, "Headphone Playback Volume",
			     spec->hp_independent_mode);
		activate_ctl(codec, "Headphone Playback Switch",
			     spec->hp_independent_mode);
	}
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

static void notify_aa_path_ctls(struct hda_codec *codec)
{
	int i;
	struct snd_ctl_elem_id id;
	const char *labels[] = {"Mic", "Front Mic", "Line"};

	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	for (i = 0; i < ARRAY_SIZE(labels); i++) {
		sprintf(id.name, "%s Playback Volume", labels[i]);
		snd_ctl_notify(codec->bus->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &id);
	}
}

static void mute_aa_path(struct hda_codec *codec, int mute)
{
	struct via_spec *spec = codec->spec;
	hda_nid_t  nid_mixer;
	int start_idx;
	int end_idx;
	int i;
	/* get nid of MW0 and start & end index */
	switch (spec->codec_type) {
	case VT1708:
		nid_mixer = 0x17;
		start_idx = 2;
		end_idx = 4;
		break;
	case VT1709_10CH:
	case VT1709_6CH:
		nid_mixer = 0x18;
		start_idx = 2;
		end_idx = 4;
		break;
	case VT1708B_8CH:
	case VT1708B_4CH:
	case VT1708S:
	case VT1716S:
		nid_mixer = 0x16;
		start_idx = 2;
		end_idx = 4;
		break;
	default:
		return;
	}
	/* check AA path's mute status */
	for (i = start_idx; i <= end_idx; i++) {
		int val = mute ? HDA_AMP_MUTE : HDA_AMP_UNMUTE;
		snd_hda_codec_amp_stereo(codec, nid_mixer, HDA_INPUT, i,
					 HDA_AMP_MUTE, val);
	}
}
static int is_smart51_pins(struct via_spec *spec, hda_nid_t pin)
{
	int res = 0;
	int index;
	for (index = AUTO_PIN_MIC; index < AUTO_PIN_FRONT_LINE; index++) {
		if (pin == spec->autocfg.input_pins[index]) {
			res = 1;
			break;
		}
	}
	return res;
}

static int via_smart51_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int via_smart51_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int index[] = { AUTO_PIN_MIC, AUTO_PIN_FRONT_MIC, AUTO_PIN_LINE };
	int on = 1;
	int i;

	for (i = 0; i < ARRAY_SIZE(index); i++) {
		hda_nid_t nid = spec->autocfg.input_pins[index[i]];
		if (nid) {
			int ctl =
			    snd_hda_codec_read(codec, nid, 0,
					       AC_VERB_GET_PIN_WIDGET_CONTROL,
					       0);
			if (i == AUTO_PIN_FRONT_MIC
			    && spec->hp_independent_mode
			    && spec->codec_type != VT1718S)
				continue; /* ignore FMic for independent HP */
			if (ctl & AC_PINCTL_IN_EN
			    && !(ctl & AC_PINCTL_OUT_EN))
				on = 0;
		}
	}
	*ucontrol->value.integer.value = on;
	return 0;
}

static int via_smart51_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int out_in = *ucontrol->value.integer.value
		? AC_PINCTL_OUT_EN : AC_PINCTL_IN_EN;
	int index[] = { AUTO_PIN_MIC, AUTO_PIN_FRONT_MIC, AUTO_PIN_LINE };
	int i;

	for (i = 0; i < ARRAY_SIZE(index); i++) {
		hda_nid_t nid = spec->autocfg.input_pins[index[i]];
		if (i == AUTO_PIN_FRONT_MIC
		    && spec->hp_independent_mode
		    && spec->codec_type != VT1718S)
			continue; /* don't retask FMic for independent HP */
		if (nid) {
			unsigned int parm = snd_hda_codec_read(
				codec, nid, 0,
				AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
			parm &= ~(AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN);
			parm |= out_in;
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    parm);
			if (out_in == AC_PINCTL_OUT_EN) {
				mute_aa_path(codec, 1);
				notify_aa_path_ctls(codec);
			}
			if (spec->codec_type == VT1718S)
				snd_hda_codec_amp_stereo(
					codec, nid, HDA_OUTPUT, 0, HDA_AMP_MUTE,
					HDA_AMP_UNMUTE);
		}
		if (i == AUTO_PIN_FRONT_MIC) {
			if (spec->codec_type == VT1708S
			    || spec->codec_type == VT1716S) {
				/* input = index 1 (AOW3) */
				snd_hda_codec_write(
					codec, nid, 0,
					AC_VERB_SET_CONNECT_SEL, 1);
				snd_hda_codec_amp_stereo(
					codec, nid, HDA_OUTPUT,
					0, HDA_AMP_MUTE, HDA_AMP_UNMUTE);
			}
		}
	}
	spec->smart51_enabled = *ucontrol->value.integer.value;
	set_jack_power_state(codec);
	return 1;
}

static struct snd_kcontrol_new via_smart51_mixer[] = {
	{
	 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name = "Smart 5.1",
	 .count = 1,
	 .info = via_smart51_info,
	 .get = via_smart51_get,
	 .put = via_smart51_put,
	 },
	{}			/* end */
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

/* check AA path's mute statue */
static int is_aa_path_mute(struct hda_codec *codec)
{
	int mute = 1;
	hda_nid_t  nid_mixer;
	int start_idx;
	int end_idx;
	int i;
	struct via_spec *spec = codec->spec;
	/* get nid of MW0 and start & end index */
	switch (spec->codec_type) {
	case VT1708B_8CH:
	case VT1708B_4CH:
	case VT1708S:
	case VT1716S:
		nid_mixer = 0x16;
		start_idx = 2;
		end_idx = 4;
		break;
	case VT1702:
		nid_mixer = 0x1a;
		start_idx = 1;
		end_idx = 3;
		break;
	case VT1718S:
		nid_mixer = 0x21;
		start_idx = 1;
		end_idx = 3;
		break;
	case VT2002P:
	case VT1812:
		nid_mixer = 0x21;
		start_idx = 0;
		end_idx = 2;
		break;
	default:
		return 0;
	}
	/* check AA path's mute status */
	for (i = start_idx; i <= end_idx; i++) {
		unsigned int con_list = snd_hda_codec_read(
			codec, nid_mixer, 0, AC_VERB_GET_CONNECT_LIST, i/4*4);
		int shift = 8 * (i % 4);
		hda_nid_t nid_pin = (con_list & (0xff << shift)) >> shift;
		unsigned int defconf = snd_hda_codec_get_pincfg(codec, nid_pin);
		if (get_defcfg_connect(defconf) == AC_JACK_PORT_COMPLEX) {
			/* check mute status while the pin is connected */
			int mute_l = snd_hda_codec_amp_read(codec, nid_mixer, 0,
							    HDA_INPUT, i) >> 7;
			int mute_r = snd_hda_codec_amp_read(codec, nid_mixer, 1,
							    HDA_INPUT, i) >> 7;
			if (!mute_l || !mute_r) {
				mute = 0;
				break;
			}
		}
	}
	return mute;
}

/* enter/exit analog low-current mode */
static void analog_low_current_mode(struct hda_codec *codec, int stream_idle)
{
	struct via_spec *spec = codec->spec;
	static int saved_stream_idle = 1; /* saved stream idle status */
	int enable = is_aa_path_mute(codec);
	unsigned int verb = 0;
	unsigned int parm = 0;

	if (stream_idle == -1)	/* stream status did not change */
		enable = enable && saved_stream_idle;
	else {
		enable = enable && stream_idle;
		saved_stream_idle = stream_idle;
	}

	/* decide low current mode's verb & parameter */
	switch (spec->codec_type) {
	case VT1708B_8CH:
	case VT1708B_4CH:
		verb = 0xf70;
		parm = enable ? 0x02 : 0x00; /* 0x02: 2/3x, 0x00: 1x */
		break;
	case VT1708S:
	case VT1718S:
	case VT1716S:
		verb = 0xf73;
		parm = enable ? 0x51 : 0xe1; /* 0x51: 4/28x, 0xe1: 1x */
		break;
	case VT1702:
		verb = 0xf73;
		parm = enable ? 0x01 : 0x1d; /* 0x01: 4/40x, 0x1d: 1x */
		break;
	case VT2002P:
	case VT1812:
		verb = 0xf93;
		parm = enable ? 0x00 : 0xe0; /* 0x00: 4/40x, 0xe0: 1x */
		break;
	default:
		return;		/* other codecs are not supported */
	}
	/* send verb */
	snd_hda_codec_write(codec, codec->afg, 0, verb, parm);
}

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

	/* Setup default input MW0 to PW4 */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0},
	/* PW9 Output enable */
	{0x25, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{ }
};

static int via_playback_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	int idle = substream->pstr->substream_opened == 1
		&& substream->ref_count == 0;
	analog_low_current_mode(codec, idle);
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
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

	if (mout->hp_nid && mout->hp_nid != nids[HDA_FRONT]
	    && !spec->hp_independent_mode)
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
	vt1708_start_hp_work(spec);
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
	vt1708_stop_hp_work(spec);
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

static int via_dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	snd_hda_multi_out_dig_cleanup(codec, &spec->multiout);
	return 0;
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
	.substreams = 2,
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
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup
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
		.prepare = via_dig_playback_pcm_prepare,
		.cleanup = via_dig_playback_pcm_cleanup
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

	/* init power states */
	set_jack_power_state(codec);
	analog_low_current_mode(codec, 1);

	via_free_kctls(codec); /* no longer needed */
	return 0;
}

static int via_build_pcms(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = spec->stream_name_analog;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
		*(spec->stream_analog_playback);
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
		spec->multiout.dac_nids[0];
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

	if (!spec)
		return;

	via_free_kctls(codec);
	vt1708_stop_hp_work(spec);
	kfree(codec->spec);
}

/* mute internal speaker if HP is plugged */
static void via_hp_automute(struct hda_codec *codec)
{
	unsigned int present = 0;
	struct via_spec *spec = codec->spec;

	present = snd_hda_jack_detect(codec, spec->autocfg.hp_pins[0]);

	if (!spec->hp_independent_mode) {
		struct snd_ctl_elem_id id;
		/* auto mute */
		snd_hda_codec_amp_stereo(
			codec, spec->autocfg.line_out_pins[0], HDA_OUTPUT, 0,
			HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
		/* notify change */
		memset(&id, 0, sizeof(id));
		id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		strcpy(id.name, "Front Playback Switch");
		snd_ctl_notify(codec->bus->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &id);
	}
}

/* mute mono out if HP or Line out is plugged */
static void via_mono_automute(struct hda_codec *codec)
{
	unsigned int hp_present, lineout_present;
	struct via_spec *spec = codec->spec;

	if (spec->codec_type != VT1716S)
		return;

	lineout_present = snd_hda_jack_detect(codec,
					      spec->autocfg.line_out_pins[0]);

	/* Mute Mono Out if Line Out is plugged */
	if (lineout_present) {
		snd_hda_codec_amp_stereo(
			codec, 0x2A, HDA_OUTPUT, 0, HDA_AMP_MUTE, HDA_AMP_MUTE);
		return;
	}

	hp_present = snd_hda_jack_detect(codec, spec->autocfg.hp_pins[0]);

	if (!spec->hp_independent_mode)
		snd_hda_codec_amp_stereo(
			codec, 0x2A, HDA_OUTPUT, 0, HDA_AMP_MUTE,
			hp_present ? HDA_AMP_MUTE : 0);
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

/* mute Internal-Speaker if HP is plugged */
static void via_speaker_automute(struct hda_codec *codec)
{
	unsigned int hp_present;
	struct via_spec *spec = codec->spec;

	if (spec->codec_type != VT2002P && spec->codec_type != VT1812)
		return;

	hp_present = snd_hda_jack_detect(codec, spec->autocfg.hp_pins[0]);

	if (!spec->hp_independent_mode) {
		struct snd_ctl_elem_id id;
		snd_hda_codec_amp_stereo(
			codec, spec->autocfg.speaker_pins[0], HDA_OUTPUT, 0,
			HDA_AMP_MUTE, hp_present ? HDA_AMP_MUTE : 0);
		/* notify change */
		memset(&id, 0, sizeof(id));
		id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		strcpy(id.name, "Speaker Playback Switch");
		snd_ctl_notify(codec->bus->card, SNDRV_CTL_EVENT_MASK_VALUE,
			       &id);
	}
}

/* mute line-out and internal speaker if HP is plugged */
static void via_hp_bind_automute(struct hda_codec *codec)
{
	/* use long instead of int below just to avoid an internal compiler
	 * error with gcc 4.0.x
	 */
	unsigned long hp_present, present = 0;
	struct via_spec *spec = codec->spec;
	int i;

	if (!spec->autocfg.hp_pins[0] || !spec->autocfg.line_out_pins[0])
		return;

	hp_present = snd_hda_jack_detect(codec, spec->autocfg.hp_pins[0]);

	present = snd_hda_jack_detect(codec, spec->autocfg.line_out_pins[0]);

	if (!spec->hp_independent_mode) {
		/* Mute Line-Outs */
		for (i = 0; i < spec->autocfg.line_outs; i++)
			snd_hda_codec_amp_stereo(
				codec, spec->autocfg.line_out_pins[i],
				HDA_OUTPUT, 0,
				HDA_AMP_MUTE, hp_present ? HDA_AMP_MUTE : 0);
		if (hp_present)
			present = hp_present;
	}
	/* Speakers */
	for (i = 0; i < spec->autocfg.speaker_outs; i++)
		snd_hda_codec_amp_stereo(
			codec, spec->autocfg.speaker_pins[i], HDA_OUTPUT, 0,
			HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}


/* unsolicited event for jack sensing */
static void via_unsol_event(struct hda_codec *codec,
				  unsigned int res)
{
	res >>= 26;
	if (res & VIA_HP_EVENT)
		via_hp_automute(codec);
	if (res & VIA_GPIO_EVENT)
		via_gpio_control(codec);
	if (res & VIA_JACK_EVENT)
		set_jack_power_state(codec);
	if (res & VIA_MONO_EVENT)
		via_mono_automute(codec);
	if (res & VIA_SPEAKER_EVENT)
		via_speaker_automute(codec);
	if (res & VIA_BIND_HP_EVENT)
		via_hp_bind_automute(codec);
}

static int via_init(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;
	for (i = 0; i < spec->num_iverbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);

	spec->codec_type = get_codec_type(codec);
	if (spec->codec_type == VT1708BCE)
		spec->codec_type = VT1708S; /* VT1708BCE & VT1708S are almost
					       same */
	/* Lydia Add for EAPD enable */
	if (!spec->dig_in_nid) { /* No Digital In connection */
		if (spec->dig_in_pin) {
			snd_hda_codec_write(codec, spec->dig_in_pin, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    PIN_OUT);
			snd_hda_codec_write(codec, spec->dig_in_pin, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 0x02);
		}
	} else /* enable SPDIF-input pin */
		snd_hda_codec_write(codec, spec->autocfg.dig_in_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN);

	/* assign slave outs */
	if (spec->slave_dig_outs[0])
		codec->slave_dig_outs = spec->slave_dig_outs;

	return 0;
}

#ifdef SND_HDA_NEEDS_RESUME
static int via_suspend(struct hda_codec *codec, pm_message_t state)
{
	struct via_spec *spec = codec->spec;
	vt1708_stop_hp_work(spec);
	return 0;
}
#endif

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
#ifdef SND_HDA_NEEDS_RESUME
	.suspend = via_suspend,
#endif
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

	for (i = 0; i < 4; i++) {
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
	hda_nid_t nid, nid_vol, nid_vols[] = {0x17, 0x19, 0x1a, 0x1b};
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
	spec->hp_independent_mode_index = 1;

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
		err = via_new_analog_input(spec, labels[i], idx, 0x17);
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

	def_conf = snd_hda_codec_get_pincfg(codec, nid);
	seqassoc = (unsigned char) get_defcfg_association(def_conf);
	seqassoc = (seqassoc << 4) | get_defcfg_sequence(def_conf);
	if (get_defcfg_connect(def_conf) == AC_JACK_PORT_NONE
	    && (seqassoc == 0xf0 || seqassoc == 0xff)) {
		def_conf = def_conf & (~(AC_JACK_PORT_BOTH << 30));
		snd_hda_codec_set_pincfg(codec, nid, def_conf);
	}

	return;
}

static int vt1708_jack_detectect_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;

	if (spec->codec_type != VT1708)
		return 0;
	spec->vt1708_jack_detectect =
		!((snd_hda_codec_read(codec, 0x1, 0, 0xf84, 0) >> 8) & 0x1);
	ucontrol->value.integer.value[0] = spec->vt1708_jack_detectect;
	return 0;
}

static int vt1708_jack_detectect_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int change;

	if (spec->codec_type != VT1708)
		return 0;
	spec->vt1708_jack_detectect = ucontrol->value.integer.value[0];
	change = (0x1 & (snd_hda_codec_read(codec, 0x1, 0, 0xf84, 0) >> 8))
		== !spec->vt1708_jack_detectect;
	if (spec->vt1708_jack_detectect) {
		mute_aa_path(codec, 1);
		notify_aa_path_ctls(codec);
	}
	return change;
}

static struct snd_kcontrol_new vt1708_jack_detectect[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Jack Detect",
		.count = 1,
		.info = snd_ctl_boolean_mono_info,
		.get = vt1708_jack_detectect_get,
		.put = vt1708_jack_detectect_put,
	},
	{} /* end */
};

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
	/* add jack detect on/off control */
	err = snd_hda_add_new_ctls(codec, vt1708_jack_detectect);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_outs)
		spec->multiout.dig_out_nid = VT1708_DIGOUT_NID;
	spec->dig_in_pin = VT1708_DIGIN_PIN;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = VT1708_DIGIN_NID;

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->init_verbs[spec->num_iverbs++] = vt1708_volume_init_verbs;

	spec->input_mux = &spec->private_imux[0];

	if (spec->hp_mux)
		spec->mixers[spec->num_mixers++] = via_hp_mixer;

	spec->mixers[spec->num_mixers++] = via_smart51_mixer;
	return 1;
}

/* init callback for auto-configuration model -- overriding the default init */
static int via_auto_init(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;

	via_init(codec);
	via_auto_init_multi_out(codec);
	via_auto_init_hp_out(codec);
	via_auto_init_analog_input(codec);
	if (spec->codec_type == VT2002P || spec->codec_type == VT1812) {
		via_hp_bind_automute(codec);
	} else {
		via_hp_automute(codec);
		via_speaker_automute(codec);
	}

	return 0;
}

static void vt1708_update_hp_jack_state(struct work_struct *work)
{
	struct via_spec *spec = container_of(work, struct via_spec,
					     vt1708_hp_work.work);
	if (spec->codec_type != VT1708)
		return;
	/* if jack state toggled */
	if (spec->vt1708_hp_present
	    != snd_hda_jack_detect(spec->codec, spec->autocfg.hp_pins[0])) {
		spec->vt1708_hp_present ^= 1;
		via_hp_automute(spec->codec);
	}
	vt1708_start_hp_work(spec);
}

static int get_mux_nids(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	hda_nid_t nid, conn[8];
	unsigned int type;
	int i, n;

	for (i = 0; i < spec->num_adc_nids; i++) {
		nid = spec->adc_nids[i];
		while (nid) {
			type = get_wcaps_type(get_wcaps(codec, nid));
			if (type == AC_WID_PIN)
				break;
			n = snd_hda_get_connections(codec, nid, conn,
						    ARRAY_SIZE(conn));
			if (n <= 0)
				break;
			if (n > 1) {
				spec->mux_nids[i] = nid;
				break;
			}
			nid = conn[0];
		}
	}
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
		get_mux_nids(codec);
		spec->mixers[spec->num_mixers] = vt1708_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1708_loopbacks;
#endif
	spec->codec = codec;
	INIT_DELAYED_WORK(&spec->vt1708_hp_work, vt1708_update_hp_jack_state);
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
	{0x20, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_HP_EVENT | VIA_JACK_EVENT},
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

	/* Set input of PW4 as MW0 */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0},
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
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup,
	},
};

static struct hda_pcm_stream vt1709_6ch_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 6,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup,
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
	hda_nid_t nid, nid_vol, nid_vols[] = {0x18, 0x1a, 0x1b, 0x29};
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
			/* ADD control to mixer index 0 */
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
		} else if (i == AUTO_SEQ_SURROUND) {
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
		} else if (i == AUTO_SEQ_SIDE) {
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

static int vt1709_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	if (spec->multiout.num_dacs == 5) /* 10 channels */
		spec->multiout.hp_nid = VT1709_HP_DAC_NID;
	else if (spec->multiout.num_dacs == 3) /* 6 channels */
		spec->multiout.hp_nid = 0;
	spec->hp_independent_mode_index = 1;

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
		err = via_new_analog_input(spec, labels[i], idx, 0x18);
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

	if (spec->autocfg.dig_outs)
		spec->multiout.dig_out_nid = VT1709_DIGOUT_NID;
	spec->dig_in_pin = VT1709_DIGIN_PIN;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = VT1709_DIGIN_NID;

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->input_mux = &spec->private_imux[0];

	if (spec->hp_mux)
		spec->mixers[spec->num_mixers++] = via_hp_mixer;

	spec->mixers[spec->num_mixers++] = via_smart51_mixer;
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
		get_mux_nids(codec);
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
		get_mux_nids(codec);
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
	{0x1d, AC_VERB_SET_CONNECT_SEL, 0},
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
	{0x1d, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_HP_EVENT | VIA_JACK_EVENT},
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1c, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1e, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x22, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x23, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{ }
};

static int via_pcm_open_close(struct hda_pcm_stream *hinfo,
			      struct hda_codec *codec,
			      struct snd_pcm_substream *substream)
{
	int idle = substream->pstr->substream_opened == 1
		&& substream->ref_count == 0;

	analog_low_current_mode(codec, idle);
	return 0;
}

static struct hda_pcm_stream vt1708B_8ch_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup,
		.close = via_pcm_open_close
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
		.open = via_pcm_open_close,
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup,
		.close = via_pcm_open_close
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
		.prepare = via_dig_playback_pcm_prepare,
		.cleanup = via_dig_playback_pcm_cleanup
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
	spec->hp_independent_mode_index = 1;

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
		err = via_new_analog_input(spec, labels[i], idx, 0x16);
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

	if (spec->autocfg.dig_outs)
		spec->multiout.dig_out_nid = VT1708B_DIGOUT_NID;
	spec->dig_in_pin = VT1708B_DIGIN_PIN;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = VT1708B_DIGIN_NID;

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->input_mux = &spec->private_imux[0];

	if (spec->hp_mux)
		spec->mixers[spec->num_mixers++] = via_hp_mixer;

	spec->mixers[spec->num_mixers++] = via_smart51_mixer;
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
static int patch_vt1708S(struct hda_codec *codec);
static int patch_vt1708B_8ch(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	if (get_codec_type(codec) == VT1708BCE)
		return patch_vt1708S(codec);
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
		get_mux_nids(codec);
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
		get_mux_nids(codec);
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
	HDA_CODEC_VOLUME("Mic Boost Capture Volume", 0x1A, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Capture Volume", 0x1E, 0x0,
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
	/* PW9, PW10  Output enable */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Enable Mic Boost Volume backdoor */
	{0x1, 0xf98, 0x1},
	/* don't bybass mixer */
	{0x1, 0xf88, 0xc0},
	{ }
};

static struct hda_verb vt1708S_uniwill_init_verbs[] = {
	{0x1d, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_HP_EVENT | VIA_JACK_EVENT},
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1c, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1e, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x22, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x23, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{ }
};

static struct hda_pcm_stream vt1708S_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup,
		.close = via_pcm_open_close
	},
};

static struct hda_pcm_stream vt1708S_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x13, /* NID to query formats and rates */
	.ops = {
		.open = via_pcm_open_close,
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup,
		.close = via_pcm_open_close
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
		.prepare = via_dig_playback_pcm_prepare,
		.cleanup = via_dig_playback_pcm_cleanup
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
	spec->hp_independent_mode_index = 1;

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
		err = via_new_analog_input(spec, labels[i], idx, 0x16);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx-1;
		imux->num_items++;
	}
	return 0;
}

/* fill out digital output widgets; one for master and one for slave outputs */
static void fill_dig_outs(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->autocfg.dig_outs; i++) {
		hda_nid_t nid;
		int conn;

		nid = spec->autocfg.dig_out_pins[i];
		if (!nid)
			continue;
		conn = snd_hda_get_connections(codec, nid, &nid, 1);
		if (conn < 1)
			continue;
		if (!spec->multiout.dig_out_nid)
			spec->multiout.dig_out_nid = nid;
		else {
			spec->slave_dig_outs[0] = nid;
			break; /* at most two dig outs */
		}
	}
}

static int vt1708S_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
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

	fill_dig_outs(codec);

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->input_mux = &spec->private_imux[0];

	if (spec->hp_mux)
		spec->mixers[spec->num_mixers++] = via_hp_mixer;

	spec->mixers[spec->num_mixers++] = via_smart51_mixer;
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

static void override_mic_boost(struct hda_codec *codec, hda_nid_t pin,
			       int offset, int num_steps, int step_size)
{
	snd_hda_override_amp_caps(codec, pin, HDA_INPUT,
				  (offset << AC_AMPCAP_OFFSET_SHIFT) |
				  (num_steps << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (step_size << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (0 << AC_AMPCAP_MUTE_SHIFT));
}

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

	if (codec->vendor_id == 0x11060440)
		spec->stream_name_analog = "VT1818S Analog";
	else
		spec->stream_name_analog = "VT1708S Analog";
	spec->stream_analog_playback = &vt1708S_pcm_analog_playback;
	spec->stream_analog_capture = &vt1708S_pcm_analog_capture;

	if (codec->vendor_id == 0x11060440)
		spec->stream_name_digital = "VT1818S Digital";
	else
		spec->stream_name_digital = "VT1708S Digital";
	spec->stream_digital_playback = &vt1708S_pcm_digital_playback;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1708S_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1708S_adc_nids);
		get_mux_nids(codec);
		override_mic_boost(codec, 0x1a, 0, 3, 40);
		override_mic_boost(codec, 0x1e, 0, 3, 40);
		spec->mixers[spec->num_mixers] = vt1708S_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1708S_loopbacks;
#endif

	/* correct names for VT1708BCE */
	if (get_codec_type(codec) == VT1708BCE)	{
		kfree(codec->chip_name);
		codec->chip_name = kstrdup("VT1708BCE", GFP_KERNEL);
		snprintf(codec->bus->card->mixername,
			 sizeof(codec->bus->card->mixername),
			 "%s %s", codec->vendor_name, codec->chip_name);
		spec->stream_name_analog = "VT1708BCE Analog";
		spec->stream_name_digital = "VT1708BCE Digital";
	}
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
	/* mixer enable */
	{0x1, 0xF88, 0x3},
	/* GPIO 0~2 */
	{0x1, 0xF82, 0x3F},
	{ }
};

static struct hda_verb vt1702_uniwill_init_verbs[] = {
	{0x17, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_HP_EVENT | VIA_JACK_EVENT},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x16, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
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
		.cleanup = via_playback_multi_pcm_cleanup,
		.close = via_pcm_open_close
	},
};

static struct hda_pcm_stream vt1702_pcm_analog_capture = {
	.substreams = 3,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x12, /* NID to query formats and rates */
	.ops = {
		.open = via_pcm_open_close,
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup,
		.close = via_pcm_open_close
	},
};

static struct hda_pcm_stream vt1702_pcm_digital_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare,
		.cleanup = via_dig_playback_pcm_cleanup
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
	int err, i;
	struct hda_input_mux *imux;
	static const char *texts[] = { "ON", "OFF", NULL};
	if (!pin)
		return 0;
	spec->multiout.hp_nid = 0x1D;
	spec->hp_independent_mode_index = 0;

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

	imux = &spec->private_imux[1];

	/* for hp mode select */
	i = 0;
	while (texts[i] != NULL)	{
		imux->items[imux->num_items].label =  texts[i];
		imux->items[imux->num_items].index = i;
		imux->num_items++;
		i++;
	}

	spec->hp_mux = &spec->private_imux[1];
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
		err = via_new_analog_input(spec, labels[i], idx, 0x1A);
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

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
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
	/* limit AA path volume to 0 dB */
	snd_hda_override_amp_caps(codec, 0x1A, HDA_INPUT,
				  (0x17 << AC_AMPCAP_OFFSET_SHIFT) |
				  (0x17 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x5 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (1 << AC_AMPCAP_MUTE_SHIFT));
	err = vt1702_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	fill_dig_outs(codec);

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->input_mux = &spec->private_imux[0];

	if (spec->hp_mux)
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
		get_mux_nids(codec);
		spec->mixers[spec->num_mixers] = vt1702_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1702_loopbacks;
#endif

	return 0;
}

/* Patch for VT1718S */

/* capture mixer elements */
static struct snd_kcontrol_new vt1718S_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x10, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x10, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x11, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x11, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Capture Volume", 0x2b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Capture Volume", 0x29, 0x0,
			 HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		.name = "Input Source",
		.count = 2,
		.info = via_mux_enum_info,
		.get = via_mux_enum_get,
		.put = via_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_verb vt1718S_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},

	/* Setup default input of Front HP to MW9 */
	{0x28, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* PW9 PW10 Output enable */
	{0x2d, AC_VERB_SET_PIN_WIDGET_CONTROL, AC_PINCTL_OUT_EN},
	{0x2e, AC_VERB_SET_PIN_WIDGET_CONTROL, AC_PINCTL_OUT_EN},
	/* PW11 Input enable */
	{0x2f, AC_VERB_SET_PIN_WIDGET_CONTROL, AC_PINCTL_IN_EN},
	/* Enable Boost Volume backdoor */
	{0x1, 0xf88, 0x8},
	/* MW0/1/2/3/4: un-mute index 0 (AOWx), mute index 1 (MW9) */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* set MUX1 = 2 (AOW4), MUX2 = 1 (AOW3) */
	{0x34, AC_VERB_SET_CONNECT_SEL, 0x2},
	{0x35, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* Unmute MW4's index 0 */
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ }
};


static struct hda_verb vt1718S_uniwill_init_verbs[] = {
	{0x28, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_HP_EVENT | VIA_JACK_EVENT},
	{0x24, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x25, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x26, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x27, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x29, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x2a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x2b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{ }
};

static struct hda_pcm_stream vt1718S_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 10,
	.nid = 0x8, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup,
		.close = via_pcm_open_close,
	},
};

static struct hda_pcm_stream vt1718S_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_pcm_open_close,
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup,
		.close = via_pcm_open_close,
	},
};

static struct hda_pcm_stream vt1718S_pcm_digital_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare,
		.cleanup = via_dig_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream vt1718S_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

/* fill in the dac_nids table from the parsed pin configuration */
static int vt1718S_auto_fill_dac_nids(struct via_spec *spec,
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
				spec->multiout.dac_nids[i] = 0x8;
				break;
			case AUTO_SEQ_CENLFE:
				spec->multiout.dac_nids[i] = 0xa;
				break;
			case AUTO_SEQ_SURROUND:
				spec->multiout.dac_nids[i] = 0x9;
				break;
			case AUTO_SEQ_SIDE:
				spec->multiout.dac_nids[i] = 0xb;
				break;
			}
		}
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1718S_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", "C/LFE", "Side" };
	hda_nid_t nid_vols[] = {0x8, 0x9, 0xa, 0xb};
	hda_nid_t nid_mutes[] = {0x24, 0x25, 0x26, 0x27};
	hda_nid_t nid, nid_vol, nid_mute = 0;
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
			err = via_add_control(
				spec, VIA_CTL_WIDGET_MUTE,
				"Center Playback Switch",
				HDA_COMPOSE_AMP_VAL(nid_mute, 1, 0,
						    HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(
				spec, VIA_CTL_WIDGET_MUTE,
				"LFE Playback Switch",
				HDA_COMPOSE_AMP_VAL(nid_mute, 2, 0,
						    HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT) {
			/* Front */
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(
				spec, VIA_CTL_WIDGET_VOL, name,
				HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(
				spec, VIA_CTL_WIDGET_MUTE, name,
				HDA_COMPOSE_AMP_VAL(nid_mute, 3, 0,
						    HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(
				spec, VIA_CTL_WIDGET_VOL, name,
				HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(
				spec, VIA_CTL_WIDGET_MUTE, name,
				HDA_COMPOSE_AMP_VAL(nid_mute, 3, 0,
						    HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int vt1718S_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = 0xc; /* AOW4 */
	spec->hp_independent_mode_index = 1;

	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(0xc, 3, 0, HDA_OUTPUT));
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
static int vt1718S_auto_create_analog_input_ctls(struct via_spec *spec,
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
		case 0x2b: /* Mic */
			idx = 1;
			break;

		case 0x2a: /* Line In */
			idx = 2;
			break;

		case 0x29: /* Front Mic */
			idx = 3;
			break;

		case 0x2c: /* CD */
			idx = 0;
			break;
		}
		err = via_new_analog_input(spec, labels[i], idx, 0x21);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx;
		imux->num_items++;
	}
	return 0;
}

static int vt1718S_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);

	if (err < 0)
		return err;
	err = vt1718S_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1718S_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1718S_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1718S_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	fill_dig_outs(codec);

	if (spec->autocfg.dig_in_pin && codec->vendor_id == 0x11060428)
		spec->dig_in_nid = 0x13;

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->input_mux = &spec->private_imux[0];

	if (spec->hp_mux)
		spec->mixers[spec->num_mixers++] = via_hp_mixer;

	spec->mixers[spec->num_mixers++] = via_smart51_mixer;

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1718S_loopbacks[] = {
	{ 0x21, HDA_INPUT, 1 },
	{ 0x21, HDA_INPUT, 2 },
	{ 0x21, HDA_INPUT, 3 },
	{ 0x21, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static int patch_vt1718S(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1718S_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	spec->init_verbs[spec->num_iverbs++] = vt1718S_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt1718S_uniwill_init_verbs;

	if (codec->vendor_id == 0x11060441)
		spec->stream_name_analog = "VT2020 Analog";
	else if (codec->vendor_id == 0x11064441)
		spec->stream_name_analog = "VT1828S Analog";
	else
		spec->stream_name_analog = "VT1718S Analog";
	spec->stream_analog_playback = &vt1718S_pcm_analog_playback;
	spec->stream_analog_capture = &vt1718S_pcm_analog_capture;

	if (codec->vendor_id == 0x11060441)
		spec->stream_name_digital = "VT2020 Digital";
	else if (codec->vendor_id == 0x11064441)
		spec->stream_name_digital = "VT1828S Digital";
	else
		spec->stream_name_digital = "VT1718S Digital";
	spec->stream_digital_playback = &vt1718S_pcm_digital_playback;
	if (codec->vendor_id == 0x11060428 || codec->vendor_id == 0x11060441)
		spec->stream_digital_capture = &vt1718S_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1718S_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1718S_adc_nids);
		get_mux_nids(codec);
		override_mic_boost(codec, 0x2b, 0, 3, 40);
		override_mic_boost(codec, 0x29, 0, 3, 40);
		spec->mixers[spec->num_mixers] = vt1718S_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;

#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1718S_loopbacks;
#endif

	return 0;
}

/* Patch for VT1716S */

static int vt1716s_dmic_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int vt1716s_dmic_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	int index = 0;

	index = snd_hda_codec_read(codec, 0x26, 0,
					       AC_VERB_GET_CONNECT_SEL, 0);
	if (index != -1)
		*ucontrol->value.integer.value = index;

	return 0;
}

static int vt1716s_dmic_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int index = *ucontrol->value.integer.value;

	snd_hda_codec_write(codec, 0x26, 0,
					       AC_VERB_SET_CONNECT_SEL, index);
	spec->dmic_enabled = index;
	set_jack_power_state(codec);

	return 1;
}

/* capture mixer elements */
static struct snd_kcontrol_new vt1716S_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x13, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x13, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x14, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Capture Volume", 0x1A, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Capture Volume", 0x1E, 0x0,
			 HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.count = 1,
		.info = via_mux_enum_info,
		.get = via_mux_enum_get,
		.put = via_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new vt1716s_dmic_mixer[] = {
	HDA_CODEC_VOLUME("Digital Mic Capture Volume", 0x22, 0x0, HDA_INPUT),
	{
	 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name = "Digital Mic Capture Switch",
	 .count = 1,
	 .info = vt1716s_dmic_info,
	 .get = vt1716s_dmic_get,
	 .put = vt1716s_dmic_put,
	 },
	{}			/* end */
};


/* mono-out mixer elements */
static struct snd_kcontrol_new vt1716S_mono_out_mixer[] = {
	HDA_CODEC_MUTE("Mono Playback Switch", 0x2a, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct hda_verb vt1716S_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/* MUX Indices: Stereo Mixer = 5 */
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x5},

	/* Setup default input of PW4 to MW0 */
	{0x1d, AC_VERB_SET_CONNECT_SEL, 0x0},

	/* Setup default input of SW1 as MW0 */
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x1},

	/* Setup default input of SW4 as AOW0 */
	{0x28, AC_VERB_SET_CONNECT_SEL, 0x1},

	/* PW9 PW10 Output enable */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},

	/* Unmute SW1, PW12 */
	{0x29, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x2a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* PW12 Output enable */
	{0x2a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Enable Boost Volume backdoor */
	{0x1, 0xf8a, 0x80},
	/* don't bybass mixer */
	{0x1, 0xf88, 0xc0},
	/* Enable mono output */
	{0x1, 0xf90, 0x08},
	{ }
};


static struct hda_verb vt1716S_uniwill_init_verbs[] = {
	{0x1d, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_HP_EVENT | VIA_JACK_EVENT},
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x1c, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_MONO_EVENT | VIA_JACK_EVENT},
	{0x1e, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x23, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{ }
};

static struct hda_pcm_stream vt1716S_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 6,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup,
		.close = via_pcm_open_close,
	},
};

static struct hda_pcm_stream vt1716S_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x13, /* NID to query formats and rates */
	.ops = {
		.open = via_pcm_open_close,
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup,
		.close = via_pcm_open_close,
	},
};

static struct hda_pcm_stream vt1716S_pcm_digital_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare,
		.cleanup = via_dig_playback_pcm_cleanup
	},
};

/* fill in the dac_nids table from the parsed pin configuration */
static int vt1716S_auto_fill_dac_nids(struct via_spec *spec,
				      const struct auto_pin_cfg *cfg)
{	int i;
	hda_nid_t nid;

	spec->multiout.num_dacs = cfg->line_outs;

	spec->multiout.dac_nids = spec->private_dac_nids;

	for (i = 0; i < 3; i++) {
		nid = cfg->line_out_pins[i];
		if (nid) {
			/* config dac list */
			switch (i) {
			case AUTO_SEQ_FRONT:
				spec->multiout.dac_nids[i] = 0x10;
				break;
			case AUTO_SEQ_CENLFE:
				spec->multiout.dac_nids[i] = 0x25;
				break;
			case AUTO_SEQ_SURROUND:
				spec->multiout.dac_nids[i] = 0x11;
				break;
			}
		}
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt1716S_auto_create_multi_out_ctls(struct via_spec *spec,
					      const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[3] = { "Front", "Surround", "C/LFE" };
	hda_nid_t nid_vols[] = {0x10, 0x11, 0x25};
	hda_nid_t nid_mutes[] = {0x1C, 0x18, 0x27};
	hda_nid_t nid, nid_vol, nid_mute;
	int i, err;

	for (i = 0; i <= AUTO_SEQ_CENLFE; i++) {
		nid = cfg->line_out_pins[i];

		if (!nid)
			continue;

		nid_vol = nid_vols[i];
		nid_mute = nid_mutes[i];

		if (i == AUTO_SEQ_CENLFE) {
			err = via_add_control(
				spec, VIA_CTL_WIDGET_VOL,
				"Center Playback Volume",
				HDA_COMPOSE_AMP_VAL(nid_vol, 1, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(
				spec, VIA_CTL_WIDGET_VOL,
				"LFE Playback Volume",
				HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(
				spec, VIA_CTL_WIDGET_MUTE,
				"Center Playback Switch",
				HDA_COMPOSE_AMP_VAL(nid_mute, 1, 0,
						    HDA_OUTPUT));
			if (err < 0)
				return err;
			err = via_add_control(
				spec, VIA_CTL_WIDGET_MUTE,
				"LFE Playback Switch",
				HDA_COMPOSE_AMP_VAL(nid_mute, 2, 0,
						    HDA_OUTPUT));
			if (err < 0)
				return err;
		} else if (i == AUTO_SEQ_FRONT) {

			err = via_add_control(
				spec, VIA_CTL_WIDGET_VOL,
				"Master Front Playback Volume",
				HDA_COMPOSE_AMP_VAL(0x16, 3, 0, HDA_INPUT));
			if (err < 0)
				return err;
			err = via_add_control(
				spec, VIA_CTL_WIDGET_MUTE,
				"Master Front Playback Switch",
				HDA_COMPOSE_AMP_VAL(0x16, 3, 0, HDA_INPUT));
			if (err < 0)
				return err;

			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(
				spec, VIA_CTL_WIDGET_VOL, name,
				HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(
				spec, VIA_CTL_WIDGET_MUTE, name,
				HDA_COMPOSE_AMP_VAL(nid_mute, 3, 0,
						    HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = via_add_control(
				spec, VIA_CTL_WIDGET_VOL, name,
				HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = via_add_control(
				spec, VIA_CTL_WIDGET_MUTE, name,
				HDA_COMPOSE_AMP_VAL(nid_mute, 3, 0,
						    HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int vt1716S_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = 0x25; /* AOW3 */
	spec->hp_independent_mode_index = 1;

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
static int vt1716S_auto_create_analog_input_ctls(struct via_spec *spec,
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
		err = via_new_analog_input(spec, labels[i], idx, 0x16);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx-1;
		imux->num_items++;
	}
	return 0;
}

static int vt1716S_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	err = vt1716S_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt1716S_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1716S_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1716S_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	fill_dig_outs(codec);

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->input_mux = &spec->private_imux[0];

	if (spec->hp_mux)
		spec->mixers[spec->num_mixers++] = via_hp_mixer;

	spec->mixers[spec->num_mixers++] = via_smart51_mixer;

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1716S_loopbacks[] = {
	{ 0x16, HDA_INPUT, 1 },
	{ 0x16, HDA_INPUT, 2 },
	{ 0x16, HDA_INPUT, 3 },
	{ 0x16, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

static int patch_vt1716S(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1716S_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	spec->init_verbs[spec->num_iverbs++]  = vt1716S_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt1716S_uniwill_init_verbs;

	spec->stream_name_analog = "VT1716S Analog";
	spec->stream_analog_playback = &vt1716S_pcm_analog_playback;
	spec->stream_analog_capture = &vt1716S_pcm_analog_capture;

	spec->stream_name_digital = "VT1716S Digital";
	spec->stream_digital_playback = &vt1716S_pcm_digital_playback;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1716S_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1716S_adc_nids);
		get_mux_nids(codec);
		override_mic_boost(codec, 0x1a, 0, 3, 40);
		override_mic_boost(codec, 0x1e, 0, 3, 40);
		spec->mixers[spec->num_mixers] = vt1716S_capture_mixer;
		spec->num_mixers++;
	}

	spec->mixers[spec->num_mixers] = vt1716s_dmic_mixer;
	spec->num_mixers++;

	spec->mixers[spec->num_mixers++] = vt1716S_mono_out_mixer;

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;

#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1716S_loopbacks;
#endif

	return 0;
}

/* for vt2002P */

/* capture mixer elements */
static struct snd_kcontrol_new vt2002P_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x10, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x10, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x11, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x11, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Capture Volume", 0x2b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Capture Volume", 0x29, 0x0,
			 HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = via_mux_enum_info,
		.get = via_mux_enum_get,
		.put = via_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_verb vt2002P_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x8, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x9, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/* MUX Indices: Mic = 0 */
	{0x1e, AC_VERB_SET_CONNECT_SEL, 0},
	{0x1f, AC_VERB_SET_CONNECT_SEL, 0},

	/* PW9 Output enable */
	{0x2d, AC_VERB_SET_PIN_WIDGET_CONTROL, AC_PINCTL_OUT_EN},

	/* Enable Boost Volume backdoor */
	{0x1, 0xfb9, 0x24},

	/* MW0/1/4/8: un-mute index 0 (MUXx), un-mute index 1 (MW9) */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* set MUX0/1/4/8 = 0 (AOW0) */
	{0x34, AC_VERB_SET_CONNECT_SEL, 0},
	{0x35, AC_VERB_SET_CONNECT_SEL, 0},
	{0x37, AC_VERB_SET_CONNECT_SEL, 0},
	{0x3b, AC_VERB_SET_CONNECT_SEL, 0},

	/* set PW0 index=0 (MW0) */
	{0x24, AC_VERB_SET_CONNECT_SEL, 0},

	/* Enable AOW0 to MW9 */
	{0x1, 0xfb8, 0x88},
	{ }
};


static struct hda_verb vt2002P_uniwill_init_verbs[] = {
	{0x25, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_JACK_EVENT | VIA_BIND_HP_EVENT},
	{0x26, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_JACK_EVENT | VIA_BIND_HP_EVENT},
	{0x29, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x2a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x2b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{ }
};

static struct hda_pcm_stream vt2002P_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x8, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup,
		.close = via_pcm_open_close,
	},
};

static struct hda_pcm_stream vt2002P_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_pcm_open_close,
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup,
		.close = via_pcm_open_close,
	},
};

static struct hda_pcm_stream vt2002P_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare,
		.cleanup = via_dig_playback_pcm_cleanup
	},
};

/* fill in the dac_nids table from the parsed pin configuration */
static int vt2002P_auto_fill_dac_nids(struct via_spec *spec,
				      const struct auto_pin_cfg *cfg)
{
	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = spec->private_dac_nids;
	if (cfg->line_out_pins[0])
		spec->multiout.dac_nids[0] = 0x8;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int vt2002P_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	int err;

	if (!cfg->line_out_pins[0])
		return -1;


	/* Line-Out: PortE */
	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Master Front Playback Volume",
			      HDA_COMPOSE_AMP_VAL(0x8, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	err = via_add_control(spec, VIA_CTL_WIDGET_BIND_PIN_MUTE,
			      "Master Front Playback Switch",
			      HDA_COMPOSE_AMP_VAL(0x26, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	return 0;
}

static int vt2002P_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = 0x9;
	spec->hp_independent_mode_index = 1;

	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(
				      spec->multiout.hp_nid, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	err = via_add_control(spec, VIA_CTL_WIDGET_MUTE,
			      "Headphone Playback Switch",
			      HDA_COMPOSE_AMP_VAL(0x25, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	create_hp_imux(spec);
	return 0;
}

/* create playback/capture controls for input pins */
static int vt2002P_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux[0];
	int i, err, idx = 0;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x2b: /* Mic */
			idx = 0;
			break;

		case 0x2a: /* Line In */
			idx = 1;
			break;

		case 0x29: /* Front Mic */
			idx = 2;
			break;
		}
		err = via_new_analog_input(spec, labels[i], idx, 0x21);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx;
		imux->num_items++;
	}

	/* build volume/mute control of loopback */
	err = via_new_analog_input(spec, "Stereo Mixer", 3, 0x21);
	if (err < 0)
		return err;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = 3;
	imux->num_items++;

	/* for digital mic select */
	imux->items[imux->num_items].label = "Digital Mic";
	imux->items[imux->num_items].index = 4;
	imux->num_items++;

	return 0;
}

static int vt2002P_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;


	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;

	err = vt2002P_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;

	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return 0; /* can't find valid BIOS pin config */

	err = vt2002P_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt2002P_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt2002P_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	fill_dig_outs(codec);

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->input_mux = &spec->private_imux[0];

	if (spec->hp_mux)
		spec->mixers[spec->num_mixers++] = via_hp_mixer;

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt2002P_loopbacks[] = {
	{ 0x21, HDA_INPUT, 0 },
	{ 0x21, HDA_INPUT, 1 },
	{ 0x21, HDA_INPUT, 2 },
	{ } /* end */
};
#endif


/* patch for vt2002P */
static int patch_vt2002P(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt2002P_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}

	spec->init_verbs[spec->num_iverbs++]  = vt2002P_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt2002P_uniwill_init_verbs;

	spec->stream_name_analog = "VT2002P Analog";
	spec->stream_analog_playback = &vt2002P_pcm_analog_playback;
	spec->stream_analog_capture = &vt2002P_pcm_analog_capture;

	spec->stream_name_digital = "VT2002P Digital";
	spec->stream_digital_playback = &vt2002P_pcm_digital_playback;

	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt2002P_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt2002P_adc_nids);
		get_mux_nids(codec);
		override_mic_boost(codec, 0x2b, 0, 3, 40);
		override_mic_boost(codec, 0x29, 0, 3, 40);
		spec->mixers[spec->num_mixers] = vt2002P_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;

#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt2002P_loopbacks;
#endif

	return 0;
}

/* for vt1812 */

/* capture mixer elements */
static struct snd_kcontrol_new vt1812_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x10, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x10, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x11, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x11, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Boost Capture Volume", 0x2b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Boost Capture Volume", 0x29, 0x0,
		       HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		.name = "Input Source",
		.count = 2,
		.info = via_mux_enum_info,
		.get = via_mux_enum_get,
		.put = via_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_verb vt1812_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x8, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x9, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},


	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 */
	/* Amp Indices: CD = 1, Mic1 = 2, Line = 3, Mic2 = 4 */
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/* MUX Indices: Mic = 0 */
	{0x1e, AC_VERB_SET_CONNECT_SEL, 0},
	{0x1f, AC_VERB_SET_CONNECT_SEL, 0},

	/* PW9 Output enable */
	{0x2d, AC_VERB_SET_PIN_WIDGET_CONTROL, AC_PINCTL_OUT_EN},

	/* Enable Boost Volume backdoor */
	{0x1, 0xfb9, 0x24},

	/* MW0/1/4/13/15: un-mute index 0 (MUXx), un-mute index 1 (MW9) */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* set MUX0/1/4/13/15 = 0 (AOW0) */
	{0x34, AC_VERB_SET_CONNECT_SEL, 0},
	{0x35, AC_VERB_SET_CONNECT_SEL, 0},
	{0x38, AC_VERB_SET_CONNECT_SEL, 0},
	{0x3c, AC_VERB_SET_CONNECT_SEL, 0},
	{0x3d, AC_VERB_SET_CONNECT_SEL, 0},

	/* Enable AOW0 to MW9 */
	{0x1, 0xfb8, 0xa8},
	{ }
};


static struct hda_verb vt1812_uniwill_init_verbs[] = {
	{0x33, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_JACK_EVENT | VIA_BIND_HP_EVENT},
	{0x25, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT },
	{0x28, AC_VERB_SET_UNSOLICITED_ENABLE,
	 AC_USRSP_EN | VIA_JACK_EVENT | VIA_BIND_HP_EVENT},
	{0x29, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x2a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{0x2b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | VIA_JACK_EVENT},
	{ }
};

static struct hda_pcm_stream vt1812_pcm_analog_playback = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x8, /* NID to query formats and rates */
	.ops = {
		.open = via_playback_pcm_open,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup,
		.close = via_pcm_open_close,
	},
};

static struct hda_pcm_stream vt1812_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x10, /* NID to query formats and rates */
	.ops = {
		.open = via_pcm_open_close,
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup,
		.close = via_pcm_open_close,
	},
};

static struct hda_pcm_stream vt1812_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_dig_playback_pcm_open,
		.close = via_dig_playback_pcm_close,
		.prepare = via_dig_playback_pcm_prepare,
		.cleanup = via_dig_playback_pcm_cleanup
	},
};
/* fill in the dac_nids table from the parsed pin configuration */
static int vt1812_auto_fill_dac_nids(struct via_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = spec->private_dac_nids;
	if (cfg->line_out_pins[0])
		spec->multiout.dac_nids[0] = 0x8;
	return 0;
}


/* add playback controls from the parsed DAC table */
static int vt1812_auto_create_multi_out_ctls(struct via_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	int err;

	if (!cfg->line_out_pins[0])
		return -1;

	/* Line-Out: PortE */
	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Master Front Playback Volume",
			      HDA_COMPOSE_AMP_VAL(0x8, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	err = via_add_control(spec, VIA_CTL_WIDGET_BIND_PIN_MUTE,
			      "Master Front Playback Switch",
			      HDA_COMPOSE_AMP_VAL(0x28, 3, 0, HDA_OUTPUT));
	if (err < 0)
		return err;

	return 0;
}

static int vt1812_auto_create_hp_ctls(struct via_spec *spec, hda_nid_t pin)
{
	int err;

	if (!pin)
		return 0;

	spec->multiout.hp_nid = 0x9;
	spec->hp_independent_mode_index = 1;


	err = via_add_control(spec, VIA_CTL_WIDGET_VOL,
			      "Headphone Playback Volume",
			      HDA_COMPOSE_AMP_VAL(
				      spec->multiout.hp_nid, 3, 0, HDA_OUTPUT));
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
static int vt1812_auto_create_analog_input_ctls(struct via_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	static char *labels[] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux", NULL
	};
	struct hda_input_mux *imux = &spec->private_imux[0];
	int i, err, idx = 0;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!cfg->input_pins[i])
			continue;

		switch (cfg->input_pins[i]) {
		case 0x2b: /* Mic */
			idx = 0;
			break;

		case 0x2a: /* Line In */
			idx = 1;
			break;

		case 0x29: /* Front Mic */
			idx = 2;
			break;
		}
		err = via_new_analog_input(spec, labels[i], idx, 0x21);
		if (err < 0)
			return err;
		imux->items[imux->num_items].label = labels[i];
		imux->items[imux->num_items].index = idx;
		imux->num_items++;
	}
	/* build volume/mute control of loopback */
	err = via_new_analog_input(spec, "Stereo Mixer", 5, 0x21);
	if (err < 0)
		return err;

	/* for internal loopback recording select */
	imux->items[imux->num_items].label = "Stereo Mixer";
	imux->items[imux->num_items].index = 5;
	imux->num_items++;

	/* for digital mic select */
	imux->items[imux->num_items].label = "Digital Mic";
	imux->items[imux->num_items].index = 6;
	imux->num_items++;

	return 0;
}

static int vt1812_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;


	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	fill_dig_outs(codec);
	err = vt1812_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;

	if (!spec->autocfg.line_outs && !spec->autocfg.hp_outs)
		return 0; /* can't find valid BIOS pin config */

	err = vt1812_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = vt1812_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = vt1812_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	fill_dig_outs(codec);

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->input_mux = &spec->private_imux[0];

	if (spec->hp_mux)
		spec->mixers[spec->num_mixers++] = via_hp_mixer;

	return 1;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list vt1812_loopbacks[] = {
	{ 0x21, HDA_INPUT, 0 },
	{ 0x21, HDA_INPUT, 1 },
	{ 0x21, HDA_INPUT, 2 },
	{ } /* end */
};
#endif


/* patch for vt1812 */
static int patch_vt1812(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	/* automatic parse from the BIOS config */
	err = vt1812_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	} else if (!err) {
		printk(KERN_INFO "hda_codec: Cannot set up configuration "
		       "from BIOS.  Using genenic mode...\n");
	}


	spec->init_verbs[spec->num_iverbs++]  = vt1812_volume_init_verbs;
	spec->init_verbs[spec->num_iverbs++] = vt1812_uniwill_init_verbs;

	spec->stream_name_analog = "VT1812 Analog";
	spec->stream_analog_playback = &vt1812_pcm_analog_playback;
	spec->stream_analog_capture = &vt1812_pcm_analog_capture;

	spec->stream_name_digital = "VT1812 Digital";
	spec->stream_digital_playback = &vt1812_pcm_digital_playback;


	if (!spec->adc_nids && spec->input_mux) {
		spec->adc_nids = vt1812_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(vt1812_adc_nids);
		get_mux_nids(codec);
		override_mic_boost(codec, 0x2b, 0, 3, 40);
		override_mic_boost(codec, 0x29, 0, 3, 40);
		spec->mixers[spec->num_mixers] = vt1812_capture_mixer;
		spec->num_mixers++;
	}

	codec->patch_ops = via_patch_ops;

	codec->patch_ops.init = via_auto_init;
	codec->patch_ops.unsol_event = via_unsol_event;

#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = vt1812_loopbacks;
#endif

	return 0;
}

/*
 * patch entries
 */
static struct hda_codec_preset snd_hda_preset_via[] = {
	{ .id = 0x11061708, .name = "VT1708", .patch = patch_vt1708},
	{ .id = 0x11061709, .name = "VT1708", .patch = patch_vt1708},
	{ .id = 0x1106170a, .name = "VT1708", .patch = patch_vt1708},
	{ .id = 0x1106170b, .name = "VT1708", .patch = patch_vt1708},
	{ .id = 0x1106e710, .name = "VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106e711, .name = "VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106e712, .name = "VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106e713, .name = "VT1709 10-Ch",
	  .patch = patch_vt1709_10ch},
	{ .id = 0x1106e714, .name = "VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106e715, .name = "VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106e716, .name = "VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106e717, .name = "VT1709 6-Ch",
	  .patch = patch_vt1709_6ch},
	{ .id = 0x1106e720, .name = "VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106e721, .name = "VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106e722, .name = "VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106e723, .name = "VT1708B 8-Ch",
	  .patch = patch_vt1708B_8ch},
	{ .id = 0x1106e724, .name = "VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x1106e725, .name = "VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x1106e726, .name = "VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x1106e727, .name = "VT1708B 4-Ch",
	  .patch = patch_vt1708B_4ch},
	{ .id = 0x11060397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11061397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11062397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11063397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11064397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11065397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11066397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11067397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11060398, .name = "VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11061398, .name = "VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11062398, .name = "VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11063398, .name = "VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11064398, .name = "VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11065398, .name = "VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11066398, .name = "VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11067398, .name = "VT1702",
	  .patch = patch_vt1702},
	{ .id = 0x11060428, .name = "VT1718S",
	  .patch = patch_vt1718S},
	{ .id = 0x11064428, .name = "VT1718S",
	  .patch = patch_vt1718S},
	{ .id = 0x11060441, .name = "VT2020",
	  .patch = patch_vt1718S},
	{ .id = 0x11064441, .name = "VT1828S",
	  .patch = patch_vt1718S},
	{ .id = 0x11060433, .name = "VT1716S",
	  .patch = patch_vt1716S},
	{ .id = 0x1106a721, .name = "VT1716S",
	  .patch = patch_vt1716S},
	{ .id = 0x11060438, .name = "VT2002P", .patch = patch_vt2002P},
	{ .id = 0x11064438, .name = "VT2002P", .patch = patch_vt2002P},
	{ .id = 0x11060448, .name = "VT1812", .patch = patch_vt1812},
	{ .id = 0x11060440, .name = "VT1818S",
	  .patch = patch_vt1708S},
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:1106*");

static struct hda_codec_preset_list via_list = {
	.preset = snd_hda_preset_via,
	.owner = THIS_MODULE,
};

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VIA HD-audio codec");

static int __init patch_via_init(void)
{
	return snd_hda_add_codec_preset(&via_list);
}

static void __exit patch_via_exit(void)
{
	snd_hda_delete_codec_preset(&via_list);
}

module_init(patch_via_init)
module_exit(patch_via_exit)
