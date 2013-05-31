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
#include <linux/module.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"

/* Pin Widget NID */
#define VT1708_HP_PIN_NID	0x20
#define VT1708_CD_PIN_NID	0x24

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
	VT1802,
	VT1705CF,
	VT1808,
	CODEC_TYPES,
};

#define VT2002P_COMPATIBLE(spec) \
	((spec)->codec_type == VT2002P ||\
	 (spec)->codec_type == VT1812 ||\
	 (spec)->codec_type == VT1802)

struct via_spec {
	struct hda_gen_spec gen;

	/* codec parameterization */
	const struct snd_kcontrol_new *mixers[6];
	unsigned int num_mixers;

	const struct hda_verb *init_verbs[5];
	unsigned int num_iverbs;

	/* HP mode source */
	unsigned int dmic_enabled;
	unsigned int no_pin_power_ctl;
	enum VIA_HDA_CODEC codec_type;

	/* analog low-power control */
	bool alc_mode;

	/* work to check hp jack state */
	int hp_work_active;
	int vt1708_jack_detect;

	void (*set_widgets_power_state)(struct hda_codec *codec);
	unsigned int dac_stream_tag[4];
};

static enum VIA_HDA_CODEC get_codec_type(struct hda_codec *codec);
static void via_playback_pcm_hook(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream,
				  int action);
static void via_hp_automute(struct hda_codec *codec, struct hda_jack_tbl *tbl);

static struct via_spec *via_new_spec(struct hda_codec *codec)
{
	struct via_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return NULL;

	codec->spec = spec;
	snd_hda_gen_spec_init(&spec->gen);
	spec->codec_type = get_codec_type(codec);
	/* VT1708BCE & VT1708S are almost same */
	if (spec->codec_type == VT1708BCE)
		spec->codec_type = VT1708S;
	spec->no_pin_power_ctl = 1;
	spec->gen.indep_hp = 1;
	spec->gen.pcm_playback_hook = via_playback_pcm_hook;
	return spec;
}

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
	else if ((dev_id & 0xfff) == 0x446)
		codec_type = VT1802;
	else if (dev_id == 0x4760)
		codec_type = VT1705CF;
	else if (dev_id == 0x4761 || dev_id == 0x4762)
		codec_type = VT1808;
	else
		codec_type = UNKNOWN;
	return codec_type;
};

static void analog_low_current_mode(struct hda_codec *codec);
static bool is_aa_path_mute(struct hda_codec *codec);

#define hp_detect_with_aa(codec) \
	(snd_hda_get_bool_hint(codec, "analog_loopback_hp_detect") == 1 && \
	 !is_aa_path_mute(codec))

static void vt1708_stop_hp_work(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	if (spec->codec_type != VT1708 || !spec->gen.autocfg.hp_outs)
		return;
	if (spec->hp_work_active) {
		snd_hda_codec_write(codec, 0x1, 0, 0xf81, 1);
		cancel_delayed_work_sync(&codec->jackpoll_work);
		spec->hp_work_active = false;
		codec->jackpoll_interval = 0;
	}
}

static void vt1708_update_hp_work(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	if (spec->codec_type != VT1708 || !spec->gen.autocfg.hp_outs)
		return;
	if (spec->vt1708_jack_detect) {
		if (!spec->hp_work_active) {
			codec->jackpoll_interval = msecs_to_jiffies(100);
			snd_hda_codec_write(codec, 0x1, 0, 0xf81, 0);
			queue_delayed_work(codec->bus->workq,
					   &codec->jackpoll_work, 0);
			spec->hp_work_active = true;
		}
	} else if (!hp_detect_with_aa(codec))
		vt1708_stop_hp_work(codec);
}

static void set_widgets_power_state(struct hda_codec *codec)
{
#if 0 /* FIXME: the assumed connections don't match always with the
       * actual routes by the generic parser, so better to disable
       * the control for safety.
       */
	struct via_spec *spec = codec->spec;
	if (spec->set_widgets_power_state)
		spec->set_widgets_power_state(codec);
#endif
}

static void update_power_state(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int parm)
{
	if (snd_hda_check_power_state(codec, nid, parm))
		return;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE, parm);
}

static void update_conv_power_state(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int parm, unsigned int index)
{
	struct via_spec *spec = codec->spec;
	unsigned int format;

	if (snd_hda_check_power_state(codec, nid, parm))
		return;
	format = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONV, 0);
	if (format && (spec->dac_stream_tag[index] != format))
		spec->dac_stream_tag[index] = format;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE, parm);
	if (parm == AC_PWRST_D0) {
		format = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONV, 0);
		if (!format && (spec->dac_stream_tag[index] != format))
			snd_hda_codec_write(codec, nid, 0,
						  AC_VERB_SET_CHANNEL_STREAMID,
						  spec->dac_stream_tag[index]);
	}
}

static bool smart51_enabled(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	return spec->gen.ext_channel_count > 2;
}

static bool is_smart51_pins(struct hda_codec *codec, hda_nid_t pin)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->gen.multi_ios; i++)
		if (spec->gen.multi_io[i].pin == pin)
			return true;
	return false;
}

static void set_pin_power_state(struct hda_codec *codec, hda_nid_t nid,
				unsigned int *affected_parm)
{
	unsigned parm;
	unsigned def_conf = snd_hda_codec_get_pincfg(codec, nid);
	unsigned no_presence = (def_conf & AC_DEFCFG_MISC)
		>> AC_DEFCFG_MISC_SHIFT
		& AC_DEFCFG_MISC_NO_PRESENCE; /* do not support pin sense */
	struct via_spec *spec = codec->spec;
	unsigned present = 0;

	no_presence |= spec->no_pin_power_ctl;
	if (!no_presence)
		present = snd_hda_jack_detect(codec, nid);
	if ((smart51_enabled(codec) && is_smart51_pins(codec, nid))
	    || ((no_presence || present)
		&& get_defcfg_connect(def_conf) != AC_JACK_PORT_NONE)) {
		*affected_parm = AC_PWRST_D0; /* if it's connected */
		parm = AC_PWRST_D0;
	} else
		parm = AC_PWRST_D3;

	update_power_state(codec, nid, parm);
}

static int via_pin_power_ctl_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	return snd_hda_enum_bool_helper_info(kcontrol, uinfo);
}

static int via_pin_power_ctl_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = !spec->no_pin_power_ctl;
	return 0;
}

static int via_pin_power_ctl_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	unsigned int val = !ucontrol->value.enumerated.item[0];

	if (val == spec->no_pin_power_ctl)
		return 0;
	spec->no_pin_power_ctl = val;
	set_widgets_power_state(codec);
	analog_low_current_mode(codec);
	return 1;
}

static const struct snd_kcontrol_new via_pin_power_ctl_enum[] = {
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Dynamic Power-Control",
	.info = via_pin_power_ctl_info,
	.get = via_pin_power_ctl_get,
	.put = via_pin_power_ctl_put,
	},
	{} /* terminator */
};


/* check AA path's mute status */
static bool is_aa_path_mute(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	const struct hda_amp_list *p;
	int ch, v;

	p = spec->gen.loopback.amplist;
	if (!p)
		return true;
	for (; p->nid; p++) {
		for (ch = 0; ch < 2; ch++) {
			v = snd_hda_codec_amp_read(codec, p->nid, ch, p->dir,
						   p->idx);
			if (!(v & HDA_AMP_MUTE) && v > 0)
				return false;
		}
	}
	return true;
}

/* enter/exit analog low-current mode */
static void __analog_low_current_mode(struct hda_codec *codec, bool force)
{
	struct via_spec *spec = codec->spec;
	bool enable;
	unsigned int verb, parm;

	if (spec->no_pin_power_ctl)
		enable = false;
	else
		enable = is_aa_path_mute(codec) && !spec->gen.active_streams;
	if (enable == spec->alc_mode && !force)
		return;
	spec->alc_mode = enable;

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
	case VT1802:
		verb = 0xf93;
		parm = enable ? 0x00 : 0xe0; /* 0x00: 4/40x, 0xe0: 1x */
		break;
	case VT1705CF:
	case VT1808:
		verb = 0xf82;
		parm = enable ? 0x00 : 0xe0;  /* 0x00: 4/40x, 0xe0: 1x */
		break;
	default:
		return;		/* other codecs are not supported */
	}
	/* send verb */
	snd_hda_codec_write(codec, codec->afg, 0, verb, parm);
}

static void analog_low_current_mode(struct hda_codec *codec)
{
	return __analog_low_current_mode(codec, false);
}

static int via_build_controls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err, i;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;

	if (spec->set_widgets_power_state)
		spec->mixers[spec->num_mixers++] = via_pin_power_ctl_enum;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}

	return 0;
}

static void via_playback_pcm_hook(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream,
				  int action)
{
	analog_low_current_mode(codec);
	vt1708_update_hp_work(codec);
}

static void via_free(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;

	if (!spec)
		return;

	vt1708_stop_hp_work(codec);
	snd_hda_gen_spec_free(&spec->gen);
	kfree(spec);
}

#ifdef CONFIG_PM
static int via_suspend(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	vt1708_stop_hp_work(codec);

	if (spec->codec_type == VT1802) {
		/* Fix pop noise on headphones */
		int i;
		for (i = 0; i < spec->gen.autocfg.hp_outs; i++)
			snd_hda_set_pin_ctl(codec, spec->gen.autocfg.hp_pins[i], 0);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM
static int via_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct via_spec *spec = codec->spec;
	set_widgets_power_state(codec);
	analog_low_current_mode(codec);
	vt1708_update_hp_work(codec);
	return snd_hda_check_amp_list_power(codec, &spec->gen.loopback, nid);
}
#endif

/*
 */

static int via_init(struct hda_codec *codec);

static const struct hda_codec_ops via_patch_ops = {
	.build_controls = via_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = via_init,
	.free = via_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = via_suspend,
	.check_power_status = via_check_power_status,
#endif
};


static const struct hda_verb vt1708_init_verbs[] = {
	/* power down jack detect function */
	{0x1, 0xf81, 0x1},
	{ }
};
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

static int vt1708_jack_detect_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;

	if (spec->codec_type != VT1708)
		return 0;
	ucontrol->value.integer.value[0] = spec->vt1708_jack_detect;
	return 0;
}

static int vt1708_jack_detect_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int val;

	if (spec->codec_type != VT1708)
		return 0;
	val = !!ucontrol->value.integer.value[0];
	if (spec->vt1708_jack_detect == val)
		return 0;
	spec->vt1708_jack_detect = val;
	vt1708_update_hp_work(codec);
	return 1;
}

static const struct snd_kcontrol_new vt1708_jack_detect_ctl[] = {
	{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Jack Detect",
	.count = 1,
	.info = snd_ctl_boolean_mono_info,
	.get = vt1708_jack_detect_get,
	.put = vt1708_jack_detect_put,
	},
	{} /* terminator */
};

static void via_hp_automute(struct hda_codec *codec, struct hda_jack_tbl *tbl)
{
	set_widgets_power_state(codec);
	snd_hda_gen_hp_automute(codec, tbl);
}

static void via_line_automute(struct hda_codec *codec, struct hda_jack_tbl *tbl)
{
	set_widgets_power_state(codec);
	snd_hda_gen_line_automute(codec, tbl);
}

static void via_jack_powerstate_event(struct hda_codec *codec, struct hda_jack_tbl *tbl)
{
	set_widgets_power_state(codec);
}

#define VIA_JACK_EVENT	(HDA_GEN_LAST_EVENT + 1)

static void via_set_jack_unsol_events(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	hda_nid_t pin;
	int i;

	spec->gen.hp_automute_hook = via_hp_automute;
	if (cfg->speaker_pins[0])
		spec->gen.line_automute_hook = via_line_automute;

	for (i = 0; i < cfg->line_outs; i++) {
		pin = cfg->line_out_pins[i];
		if (pin && !snd_hda_jack_tbl_get(codec, pin) &&
		    is_jack_detectable(codec, pin))
			snd_hda_jack_detect_enable_callback(codec, pin,
							    VIA_JACK_EVENT,
							    via_jack_powerstate_event);
	}

	for (i = 0; i < cfg->num_inputs; i++) {
		pin = cfg->line_out_pins[i];
		if (pin && !snd_hda_jack_tbl_get(codec, pin) &&
		    is_jack_detectable(codec, pin))
			snd_hda_jack_detect_enable_callback(codec, pin,
							    VIA_JACK_EVENT,
							    via_jack_powerstate_event);
	}
}

static const struct badness_table via_main_out_badness = {
	.no_primary_dac = 0x10000,
	.no_dac = 0x4000,
	.shared_primary = 0x10000,
	.shared_surr = 0x20,
	.shared_clfe = 0x20,
	.shared_surr_main = 0x20,
};
static const struct badness_table via_extra_out_badness = {
	.no_primary_dac = 0x4000,
	.no_dac = 0x4000,
	.shared_primary = 0x12,
	.shared_surr = 0x20,
	.shared_clfe = 0x20,
	.shared_surr_main = 0x10,
};

static int via_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	spec->gen.main_out_badness = &via_main_out_badness;
	spec->gen.extra_out_badness = &via_extra_out_badness;

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL, 0);
	if (err < 0)
		return err;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		return err;

	via_set_jack_unsol_events(codec);
	return 0;
}

static int via_init(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->num_iverbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);

	/* init power states */
	set_widgets_power_state(codec);
	__analog_low_current_mode(codec, true);

	snd_hda_gen_init(codec);

	vt1708_update_hp_work(codec);

	return 0;
}

static int vt1708_build_controls(struct hda_codec *codec)
{
	/* In order not to create "Phantom Jack" controls,
	   temporary enable jackpoll */
	int err;
	int old_interval = codec->jackpoll_interval;
	codec->jackpoll_interval = msecs_to_jiffies(100);
	err = via_build_controls(codec);
	codec->jackpoll_interval = old_interval;
	return err;
}

static int vt1708_build_pcms(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i, err;

	err = snd_hda_gen_build_pcms(codec);
	if (err < 0 || codec->vendor_id != 0x11061708)
		return err;

	/* We got noisy outputs on the right channel on VT1708 when
	 * 24bit samples are used.  Until any workaround is found,
	 * disable the 24bit format, so far.
	 */
	for (i = 0; i < codec->num_pcms; i++) {
		struct hda_pcm *info = &spec->gen.pcm_rec[i];
		if (!info->stream[SNDRV_PCM_STREAM_PLAYBACK].substreams ||
		    info->pcm_type != HDA_PCM_TYPE_AUDIO)
			continue;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].formats =
			SNDRV_PCM_FMTBIT_S16_LE;
	}

	return 0;
}

static int patch_vt1708(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x17;

	/* set jackpoll_interval while parsing the codec */
	codec->jackpoll_interval = msecs_to_jiffies(100);
	spec->vt1708_jack_detect = 1;

	/* don't support the input jack switching due to lack of unsol event */
	/* (it may work with polling, though, but it needs testing) */
	spec->gen.suppress_auto_mic = 1;

	/* Add HP and CD pin config connect bit re-config action */
	vt1708_set_pinconfig_connect(codec, VT1708_HP_PIN_NID);
	vt1708_set_pinconfig_connect(codec, VT1708_CD_PIN_NID);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	/* add jack detect on/off control */
	spec->mixers[spec->num_mixers++] = vt1708_jack_detect_ctl;

	spec->init_verbs[spec->num_iverbs++] = vt1708_init_verbs;

	codec->patch_ops = via_patch_ops;
	codec->patch_ops.build_controls = vt1708_build_controls;
	codec->patch_ops.build_pcms = vt1708_build_pcms;

	/* clear jackpoll_interval again; it's set dynamically */
	codec->jackpoll_interval = 0;

	return 0;
}

static int patch_vt1709(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x18;

	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	codec->patch_ops = via_patch_ops;

	return 0;
}

static void set_widgets_power_state_vt1708B(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int imux_is_smixer;
	unsigned int parm;
	int is_8ch = 0;
	if ((spec->codec_type != VT1708B_4CH) &&
	    (codec->vendor_id != 0x11064397))
		is_8ch = 1;

	/* SW0 (17h) = stereo mixer */
	imux_is_smixer =
	(snd_hda_codec_read(codec, 0x17, 0, AC_VERB_GET_CONNECT_SEL, 0x00)
	 == ((spec->codec_type == VT1708S) ? 5 : 0));
	/* inputs */
	/* PW 1/2/5 (1ah/1bh/1eh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x1a, &parm);
	set_pin_power_state(codec, 0x1b, &parm);
	set_pin_power_state(codec, 0x1e, &parm);
	if (imux_is_smixer)
		parm = AC_PWRST_D0;
	/* SW0 (17h), AIW 0/1 (13h/14h) */
	update_power_state(codec, 0x17, parm);
	update_power_state(codec, 0x13, parm);
	update_power_state(codec, 0x14, parm);

	/* outputs */
	/* PW0 (19h), SW1 (18h), AOW1 (11h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x19, &parm);
	if (smart51_enabled(codec))
		set_pin_power_state(codec, 0x1b, &parm);
	update_power_state(codec, 0x18, parm);
	update_power_state(codec, 0x11, parm);

	/* PW6 (22h), SW2 (26h), AOW2 (24h) */
	if (is_8ch) {
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x22, &parm);
		if (smart51_enabled(codec))
			set_pin_power_state(codec, 0x1a, &parm);
		update_power_state(codec, 0x26, parm);
		update_power_state(codec, 0x24, parm);
	} else if (codec->vendor_id == 0x11064397) {
		/* PW7(23h), SW2(27h), AOW2(25h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x23, &parm);
		if (smart51_enabled(codec))
			set_pin_power_state(codec, 0x1a, &parm);
		update_power_state(codec, 0x27, parm);
		update_power_state(codec, 0x25, parm);
	}

	/* PW 3/4/7 (1ch/1dh/23h) */
	parm = AC_PWRST_D3;
	/* force to D0 for internal Speaker */
	set_pin_power_state(codec, 0x1c, &parm);
	set_pin_power_state(codec, 0x1d, &parm);
	if (is_8ch)
		set_pin_power_state(codec, 0x23, &parm);

	/* MW0 (16h), Sw3 (27h), AOW 0/3 (10h/25h) */
	update_power_state(codec, 0x16, imux_is_smixer ? AC_PWRST_D0 : parm);
	update_power_state(codec, 0x10, parm);
	if (is_8ch) {
		update_power_state(codec, 0x25, parm);
		update_power_state(codec, 0x27, parm);
	} else if (codec->vendor_id == 0x11064397 && spec->gen.indep_hp_enabled)
		update_power_state(codec, 0x25, parm);
}

static int patch_vt1708S(struct hda_codec *codec);
static int patch_vt1708B(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	if (get_codec_type(codec) == VT1708BCE)
		return patch_vt1708S(codec);

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x16;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	codec->patch_ops = via_patch_ops;

	spec->set_widgets_power_state =  set_widgets_power_state_vt1708B;

	return 0;
}

/* Patch for VT1708S */
static const struct hda_verb vt1708S_init_verbs[] = {
	/* Enable Mic Boost Volume backdoor */
	{0x1, 0xf98, 0x1},
	/* don't bybass mixer */
	{0x1, 0xf88, 0xc0},
	{ }
};

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
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x16;
	override_mic_boost(codec, 0x1a, 0, 3, 40);
	override_mic_boost(codec, 0x1e, 0, 3, 40);

	/* correct names for VT1708BCE */
	if (get_codec_type(codec) == VT1708BCE)	{
		kfree(codec->chip_name);
		codec->chip_name = kstrdup("VT1708BCE", GFP_KERNEL);
		snprintf(codec->bus->card->mixername,
			 sizeof(codec->bus->card->mixername),
			 "%s %s", codec->vendor_name, codec->chip_name);
	}
	/* correct names for VT1705 */
	if (codec->vendor_id == 0x11064397)	{
		kfree(codec->chip_name);
		codec->chip_name = kstrdup("VT1705", GFP_KERNEL);
		snprintf(codec->bus->card->mixername,
			 sizeof(codec->bus->card->mixername),
			 "%s %s", codec->vendor_name, codec->chip_name);
	}

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++] = vt1708S_init_verbs;

	codec->patch_ops = via_patch_ops;

	spec->set_widgets_power_state =  set_widgets_power_state_vt1708B;
	return 0;
}

/* Patch for VT1702 */

static const struct hda_verb vt1702_init_verbs[] = {
	/* mixer enable */
	{0x1, 0xF88, 0x3},
	/* GPIO 0~2 */
	{0x1, 0xF82, 0x3F},
	{ }
};

static void set_widgets_power_state_vt1702(struct hda_codec *codec)
{
	int imux_is_smixer =
	snd_hda_codec_read(codec, 0x13, 0, AC_VERB_GET_CONNECT_SEL, 0x00) == 3;
	unsigned int parm;
	/* inputs */
	/* PW 1/2/5 (14h/15h/18h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x14, &parm);
	set_pin_power_state(codec, 0x15, &parm);
	set_pin_power_state(codec, 0x18, &parm);
	if (imux_is_smixer)
		parm = AC_PWRST_D0; /* SW0 (13h) = stereo mixer (idx 3) */
	/* SW0 (13h), AIW 0/1/2 (12h/1fh/20h) */
	update_power_state(codec, 0x13, parm);
	update_power_state(codec, 0x12, parm);
	update_power_state(codec, 0x1f, parm);
	update_power_state(codec, 0x20, parm);

	/* outputs */
	/* PW 3/4 (16h/17h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x17, &parm);
	set_pin_power_state(codec, 0x16, &parm);
	/* MW0 (1ah), AOW 0/1 (10h/1dh) */
	update_power_state(codec, 0x1a, imux_is_smixer ? AC_PWRST_D0 : parm);
	update_power_state(codec, 0x10, parm);
	update_power_state(codec, 0x1d, parm);
}

static int patch_vt1702(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x1a;

	/* limit AA path volume to 0 dB */
	snd_hda_override_amp_caps(codec, 0x1A, HDA_INPUT,
				  (0x17 << AC_AMPCAP_OFFSET_SHIFT) |
				  (0x17 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x5 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (1 << AC_AMPCAP_MUTE_SHIFT));

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++] = vt1702_init_verbs;

	codec->patch_ops = via_patch_ops;

	spec->set_widgets_power_state =  set_widgets_power_state_vt1702;
	return 0;
}

/* Patch for VT1718S */

static const struct hda_verb vt1718S_init_verbs[] = {
	/* Enable MW0 adjust Gain 5 */
	{0x1, 0xfb2, 0x10},
	/* Enable Boost Volume backdoor */
	{0x1, 0xf88, 0x8},

	{ }
};

static void set_widgets_power_state_vt1718S(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int imux_is_smixer;
	unsigned int parm, parm2;
	/* MUX6 (1eh) = stereo mixer */
	imux_is_smixer =
	snd_hda_codec_read(codec, 0x1e, 0, AC_VERB_GET_CONNECT_SEL, 0x00) == 5;
	/* inputs */
	/* PW 5/6/7 (29h/2ah/2bh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x29, &parm);
	set_pin_power_state(codec, 0x2a, &parm);
	set_pin_power_state(codec, 0x2b, &parm);
	if (imux_is_smixer)
		parm = AC_PWRST_D0;
	/* MUX6/7 (1eh/1fh), AIW 0/1 (10h/11h) */
	update_power_state(codec, 0x1e, parm);
	update_power_state(codec, 0x1f, parm);
	update_power_state(codec, 0x10, parm);
	update_power_state(codec, 0x11, parm);

	/* outputs */
	/* PW3 (27h), MW2 (1ah), AOW3 (bh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x27, &parm);
	update_power_state(codec, 0x1a, parm);
	parm2 = parm; /* for pin 0x0b */

	/* PW2 (26h), AOW2 (ah) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x26, &parm);
	if (smart51_enabled(codec))
		set_pin_power_state(codec, 0x2b, &parm);
	update_power_state(codec, 0xa, parm);

	/* PW0 (24h), AOW0 (8h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x24, &parm);
	if (!spec->gen.indep_hp_enabled) /* check for redirected HP */
		set_pin_power_state(codec, 0x28, &parm);
	update_power_state(codec, 0x8, parm);
	if (!spec->gen.indep_hp_enabled && parm2 != AC_PWRST_D3)
		parm = parm2;
	update_power_state(codec, 0xb, parm);
	/* MW9 (21h), Mw2 (1ah), AOW0 (8h) */
	update_power_state(codec, 0x21, imux_is_smixer ? AC_PWRST_D0 : parm);

	/* PW1 (25h), AOW1 (9h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x25, &parm);
	if (smart51_enabled(codec))
		set_pin_power_state(codec, 0x2a, &parm);
	update_power_state(codec, 0x9, parm);

	if (spec->gen.indep_hp_enabled) {
		/* PW4 (28h), MW3 (1bh), MUX1(34h), AOW4 (ch) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x28, &parm);
		update_power_state(codec, 0x1b, parm);
		update_power_state(codec, 0x34, parm);
		update_power_state(codec, 0xc, parm);
	}
}

/* Add a connection to the primary DAC from AA-mixer for some codecs
 * This isn't listed from the raw info, but the chip has a secret connection.
 */
static int add_secret_dac_path(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i, nums;
	hda_nid_t conn[8];
	hda_nid_t nid;

	if (!spec->gen.mixer_nid)
		return 0;
	nums = snd_hda_get_connections(codec, spec->gen.mixer_nid, conn,
				       ARRAY_SIZE(conn) - 1);
	for (i = 0; i < nums; i++) {
		if (get_wcaps_type(get_wcaps(codec, conn[i])) == AC_WID_AUD_OUT)
			return 0;
	}

	/* find the primary DAC and add to the connection list */
	nid = codec->start_nid;
	for (i = 0; i < codec->num_nodes; i++, nid++) {
		unsigned int caps = get_wcaps(codec, nid);
		if (get_wcaps_type(caps) == AC_WID_AUD_OUT &&
		    !(caps & AC_WCAP_DIGITAL)) {
			conn[nums++] = nid;
			return snd_hda_override_conn_list(codec,
							  spec->gen.mixer_nid,
							  nums, conn);
		}
	}
	return 0;
}


static int patch_vt1718S(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x21;
	override_mic_boost(codec, 0x2b, 0, 3, 40);
	override_mic_boost(codec, 0x29, 0, 3, 40);
	add_secret_dac_path(codec);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++] = vt1718S_init_verbs;

	codec->patch_ops = via_patch_ops;

	spec->set_widgets_power_state =  set_widgets_power_state_vt1718S;

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
	set_widgets_power_state(codec);
	return 1;
}

static const struct snd_kcontrol_new vt1716s_dmic_mixer[] = {
	HDA_CODEC_VOLUME("Digital Mic Capture Volume", 0x22, 0x0, HDA_INPUT),
	{
	 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name = "Digital Mic Capture Switch",
	 .subdevice = HDA_SUBDEV_NID_FLAG | 0x26,
	 .count = 1,
	 .info = vt1716s_dmic_info,
	 .get = vt1716s_dmic_get,
	 .put = vt1716s_dmic_put,
	 },
	{}			/* end */
};


/* mono-out mixer elements */
static const struct snd_kcontrol_new vt1716S_mono_out_mixer[] = {
	HDA_CODEC_MUTE("Mono Playback Switch", 0x2a, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static const struct hda_verb vt1716S_init_verbs[] = {
	/* Enable Boost Volume backdoor */
	{0x1, 0xf8a, 0x80},
	/* don't bybass mixer */
	{0x1, 0xf88, 0xc0},
	/* Enable mono output */
	{0x1, 0xf90, 0x08},
	{ }
};

static void set_widgets_power_state_vt1716S(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int imux_is_smixer;
	unsigned int parm;
	unsigned int mono_out, present;
	/* SW0 (17h) = stereo mixer */
	imux_is_smixer =
	(snd_hda_codec_read(codec, 0x17, 0,
			    AC_VERB_GET_CONNECT_SEL, 0x00) ==  5);
	/* inputs */
	/* PW 1/2/5 (1ah/1bh/1eh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x1a, &parm);
	set_pin_power_state(codec, 0x1b, &parm);
	set_pin_power_state(codec, 0x1e, &parm);
	if (imux_is_smixer)
		parm = AC_PWRST_D0;
	/* SW0 (17h), AIW0(13h) */
	update_power_state(codec, 0x17, parm);
	update_power_state(codec, 0x13, parm);

	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x1e, &parm);
	/* PW11 (22h) */
	if (spec->dmic_enabled)
		set_pin_power_state(codec, 0x22, &parm);
	else
		update_power_state(codec, 0x22, AC_PWRST_D3);

	/* SW2(26h), AIW1(14h) */
	update_power_state(codec, 0x26, parm);
	update_power_state(codec, 0x14, parm);

	/* outputs */
	/* PW0 (19h), SW1 (18h), AOW1 (11h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x19, &parm);
	/* Smart 5.1 PW2(1bh) */
	if (smart51_enabled(codec))
		set_pin_power_state(codec, 0x1b, &parm);
	update_power_state(codec, 0x18, parm);
	update_power_state(codec, 0x11, parm);

	/* PW7 (23h), SW3 (27h), AOW3 (25h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x23, &parm);
	/* Smart 5.1 PW1(1ah) */
	if (smart51_enabled(codec))
		set_pin_power_state(codec, 0x1a, &parm);
	update_power_state(codec, 0x27, parm);

	/* Smart 5.1 PW5(1eh) */
	if (smart51_enabled(codec))
		set_pin_power_state(codec, 0x1e, &parm);
	update_power_state(codec, 0x25, parm);

	/* Mono out */
	/* SW4(28h)->MW1(29h)-> PW12 (2ah)*/
	present = snd_hda_jack_detect(codec, 0x1c);

	if (present)
		mono_out = 0;
	else {
		present = snd_hda_jack_detect(codec, 0x1d);
		if (!spec->gen.indep_hp_enabled && present)
			mono_out = 0;
		else
			mono_out = 1;
	}
	parm = mono_out ? AC_PWRST_D0 : AC_PWRST_D3;
	update_power_state(codec, 0x28, parm);
	update_power_state(codec, 0x29, parm);
	update_power_state(codec, 0x2a, parm);

	/* PW 3/4 (1ch/1dh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x1c, &parm);
	set_pin_power_state(codec, 0x1d, &parm);
	/* HP Independent Mode, power on AOW3 */
	if (spec->gen.indep_hp_enabled)
		update_power_state(codec, 0x25, parm);

	/* force to D0 for internal Speaker */
	/* MW0 (16h), AOW0 (10h) */
	update_power_state(codec, 0x16, imux_is_smixer ? AC_PWRST_D0 : parm);
	update_power_state(codec, 0x10, mono_out ? AC_PWRST_D0 : parm);
}

static int patch_vt1716S(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x16;
	override_mic_boost(codec, 0x1a, 0, 3, 40);
	override_mic_boost(codec, 0x1e, 0, 3, 40);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++]  = vt1716S_init_verbs;

	spec->mixers[spec->num_mixers++] = vt1716s_dmic_mixer;
	spec->mixers[spec->num_mixers++] = vt1716S_mono_out_mixer;

	codec->patch_ops = via_patch_ops;

	spec->set_widgets_power_state = set_widgets_power_state_vt1716S;
	return 0;
}

/* for vt2002P */

static const struct hda_verb vt2002P_init_verbs[] = {
	/* Class-D speaker related verbs */
	{0x1, 0xfe0, 0x4},
	{0x1, 0xfe9, 0x80},
	{0x1, 0xfe2, 0x22},
	/* Enable Boost Volume backdoor */
	{0x1, 0xfb9, 0x24},
	/* Enable AOW0 to MW9 */
	{0x1, 0xfb8, 0x88},
	{ }
};

static const struct hda_verb vt1802_init_verbs[] = {
	/* Enable Boost Volume backdoor */
	{0x1, 0xfb9, 0x24},
	/* Enable AOW0 to MW9 */
	{0x1, 0xfb8, 0x88},
	{ }
};

static void set_widgets_power_state_vt2002P(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int imux_is_smixer;
	unsigned int parm;
	unsigned int present;
	/* MUX9 (1eh) = stereo mixer */
	imux_is_smixer =
	snd_hda_codec_read(codec, 0x1e, 0, AC_VERB_GET_CONNECT_SEL, 0x00) == 3;
	/* inputs */
	/* PW 5/6/7 (29h/2ah/2bh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x29, &parm);
	set_pin_power_state(codec, 0x2a, &parm);
	set_pin_power_state(codec, 0x2b, &parm);
	parm = AC_PWRST_D0;
	/* MUX9/10 (1eh/1fh), AIW 0/1 (10h/11h) */
	update_power_state(codec, 0x1e, parm);
	update_power_state(codec, 0x1f, parm);
	update_power_state(codec, 0x10, parm);
	update_power_state(codec, 0x11, parm);

	/* outputs */
	/* AOW0 (8h)*/
	update_power_state(codec, 0x8, parm);

	if (spec->codec_type == VT1802) {
		/* PW4 (28h), MW4 (18h), MUX4(38h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x28, &parm);
		update_power_state(codec, 0x18, parm);
		update_power_state(codec, 0x38, parm);
	} else {
		/* PW4 (26h), MW4 (1ch), MUX4(37h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x26, &parm);
		update_power_state(codec, 0x1c, parm);
		update_power_state(codec, 0x37, parm);
	}

	if (spec->codec_type == VT1802) {
		/* PW1 (25h), MW1 (15h), MUX1(35h), AOW1 (9h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x25, &parm);
		update_power_state(codec, 0x15, parm);
		update_power_state(codec, 0x35, parm);
	} else {
		/* PW1 (25h), MW1 (19h), MUX1(35h), AOW1 (9h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x25, &parm);
		update_power_state(codec, 0x19, parm);
		update_power_state(codec, 0x35, parm);
	}

	if (spec->gen.indep_hp_enabled)
		update_power_state(codec, 0x9, AC_PWRST_D0);

	/* Class-D */
	/* PW0 (24h), MW0(18h/14h), MUX0(34h) */
	present = snd_hda_jack_detect(codec, 0x25);

	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x24, &parm);
	parm = present ? AC_PWRST_D3 : AC_PWRST_D0;
	if (spec->codec_type == VT1802)
		update_power_state(codec, 0x14, parm);
	else
		update_power_state(codec, 0x18, parm);
	update_power_state(codec, 0x34, parm);

	/* Mono Out */
	present = snd_hda_jack_detect(codec, 0x26);

	parm = present ? AC_PWRST_D3 : AC_PWRST_D0;
	if (spec->codec_type == VT1802) {
		/* PW15 (33h), MW8(1ch), MUX8(3ch) */
		update_power_state(codec, 0x33, parm);
		update_power_state(codec, 0x1c, parm);
		update_power_state(codec, 0x3c, parm);
	} else {
		/* PW15 (31h), MW8(17h), MUX8(3bh) */
		update_power_state(codec, 0x31, parm);
		update_power_state(codec, 0x17, parm);
		update_power_state(codec, 0x3b, parm);
	}
	/* MW9 (21h) */
	if (imux_is_smixer || !is_aa_path_mute(codec))
		update_power_state(codec, 0x21, AC_PWRST_D0);
	else
		update_power_state(codec, 0x21, AC_PWRST_D3);
}

/*
 * pin fix-up
 */
enum {
	VIA_FIXUP_INTMIC_BOOST,
	VIA_FIXUP_ASUS_G75,
};

static void via_fixup_intmic_boost(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		override_mic_boost(codec, 0x30, 0, 2, 40);
}

static const struct hda_fixup via_fixups[] = {
	[VIA_FIXUP_INTMIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = via_fixup_intmic_boost,
	},
	[VIA_FIXUP_ASUS_G75] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* set 0x24 and 0x33 as speakers */
			{ 0x24, 0x991301f0 },
			{ 0x33, 0x991301f1 }, /* subwoofer */
			{ }
		}
	},
};

static const struct snd_pci_quirk vt2002p_fixups[] = {
	SND_PCI_QUIRK(0x1043, 0x1487, "Asus G75", VIA_FIXUP_ASUS_G75),
	SND_PCI_QUIRK(0x1043, 0x8532, "Asus X202E", VIA_FIXUP_INTMIC_BOOST),
	{}
};

/* NIDs 0x24 and 0x33 on VT1802 have connections to non-existing NID 0x3e
 * Replace this with mixer NID 0x1c
 */
static void fix_vt1802_connections(struct hda_codec *codec)
{
	static hda_nid_t conn_24[] = { 0x14, 0x1c };
	static hda_nid_t conn_33[] = { 0x1c };

	snd_hda_override_conn_list(codec, 0x24, ARRAY_SIZE(conn_24), conn_24);
	snd_hda_override_conn_list(codec, 0x33, ARRAY_SIZE(conn_33), conn_33);
}

/* patch for vt2002P */
static int patch_vt2002P(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x21;
	override_mic_boost(codec, 0x2b, 0, 3, 40);
	override_mic_boost(codec, 0x29, 0, 3, 40);
	if (spec->codec_type == VT1802)
		fix_vt1802_connections(codec);
	add_secret_dac_path(codec);

	snd_hda_pick_fixup(codec, NULL, vt2002p_fixups, via_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	if (spec->codec_type == VT1802)
		spec->init_verbs[spec->num_iverbs++] = vt1802_init_verbs;
	else
		spec->init_verbs[spec->num_iverbs++] = vt2002P_init_verbs;

	codec->patch_ops = via_patch_ops;

	spec->set_widgets_power_state =  set_widgets_power_state_vt2002P;
	return 0;
}

/* for vt1812 */

static const struct hda_verb vt1812_init_verbs[] = {
	/* Enable Boost Volume backdoor */
	{0x1, 0xfb9, 0x24},
	/* Enable AOW0 to MW9 */
	{0x1, 0xfb8, 0xa8},
	{ }
};

static void set_widgets_power_state_vt1812(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	unsigned int parm;
	unsigned int present;
	/* inputs */
	/* PW 5/6/7 (29h/2ah/2bh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x29, &parm);
	set_pin_power_state(codec, 0x2a, &parm);
	set_pin_power_state(codec, 0x2b, &parm);
	parm = AC_PWRST_D0;
	/* MUX10/11 (1eh/1fh), AIW 0/1 (10h/11h) */
	update_power_state(codec, 0x1e, parm);
	update_power_state(codec, 0x1f, parm);
	update_power_state(codec, 0x10, parm);
	update_power_state(codec, 0x11, parm);

	/* outputs */
	/* AOW0 (8h)*/
	update_power_state(codec, 0x8, AC_PWRST_D0);

	/* PW4 (28h), MW4 (18h), MUX4(38h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x28, &parm);
	update_power_state(codec, 0x18, parm);
	update_power_state(codec, 0x38, parm);

	/* PW1 (25h), MW1 (15h), MUX1(35h), AOW1 (9h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x25, &parm);
	update_power_state(codec, 0x15, parm);
	update_power_state(codec, 0x35, parm);
	if (spec->gen.indep_hp_enabled)
		update_power_state(codec, 0x9, AC_PWRST_D0);

	/* Internal Speaker */
	/* PW0 (24h), MW0(14h), MUX0(34h) */
	present = snd_hda_jack_detect(codec, 0x25);

	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x24, &parm);
	if (present) {
		update_power_state(codec, 0x14, AC_PWRST_D3);
		update_power_state(codec, 0x34, AC_PWRST_D3);
	} else {
		update_power_state(codec, 0x14, AC_PWRST_D0);
		update_power_state(codec, 0x34, AC_PWRST_D0);
	}


	/* Mono Out */
	/* PW13 (31h), MW13(1ch), MUX13(3ch), MW14(3eh) */
	present = snd_hda_jack_detect(codec, 0x28);

	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x31, &parm);
	if (present) {
		update_power_state(codec, 0x1c, AC_PWRST_D3);
		update_power_state(codec, 0x3c, AC_PWRST_D3);
		update_power_state(codec, 0x3e, AC_PWRST_D3);
	} else {
		update_power_state(codec, 0x1c, AC_PWRST_D0);
		update_power_state(codec, 0x3c, AC_PWRST_D0);
		update_power_state(codec, 0x3e, AC_PWRST_D0);
	}

	/* PW15 (33h), MW15 (1dh), MUX15(3dh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x33, &parm);
	update_power_state(codec, 0x1d, parm);
	update_power_state(codec, 0x3d, parm);

}

/* patch for vt1812 */
static int patch_vt1812(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x21;
	override_mic_boost(codec, 0x2b, 0, 3, 40);
	override_mic_boost(codec, 0x29, 0, 3, 40);
	add_secret_dac_path(codec);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++]  = vt1812_init_verbs;

	codec->patch_ops = via_patch_ops;

	spec->set_widgets_power_state =  set_widgets_power_state_vt1812;
	return 0;
}

/* patch for vt3476 */

static const struct hda_verb vt3476_init_verbs[] = {
	/* Enable DMic 8/16/32K */
	{0x1, 0xF7B, 0x30},
	/* Enable Boost Volume backdoor */
	{0x1, 0xFB9, 0x20},
	/* Enable AOW-MW9 path */
	{0x1, 0xFB8, 0x10},
	{ }
};

static void set_widgets_power_state_vt3476(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int imux_is_smixer;
	unsigned int parm, parm2;
	/* MUX10 (1eh) = stereo mixer */
	imux_is_smixer =
	snd_hda_codec_read(codec, 0x1e, 0, AC_VERB_GET_CONNECT_SEL, 0x00) == 4;
	/* inputs */
	/* PW 5/6/7 (29h/2ah/2bh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x29, &parm);
	set_pin_power_state(codec, 0x2a, &parm);
	set_pin_power_state(codec, 0x2b, &parm);
	if (imux_is_smixer)
		parm = AC_PWRST_D0;
	/* MUX10/11 (1eh/1fh), AIW 0/1 (10h/11h) */
	update_power_state(codec, 0x1e, parm);
	update_power_state(codec, 0x1f, parm);
	update_power_state(codec, 0x10, parm);
	update_power_state(codec, 0x11, parm);

	/* outputs */
	/* PW3 (27h), MW3(37h), AOW3 (bh) */
	if (spec->codec_type == VT1705CF) {
		parm = AC_PWRST_D3;
		update_power_state(codec, 0x27, parm);
		update_power_state(codec, 0x37, parm);
	}	else {
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x27, &parm);
		update_power_state(codec, 0x37, parm);
	}

	/* PW2 (26h), MW2(36h), AOW2 (ah) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x26, &parm);
	update_power_state(codec, 0x36, parm);
	if (smart51_enabled(codec)) {
		/* PW7(2bh), MW7(3bh), MUX7(1Bh) */
		set_pin_power_state(codec, 0x2b, &parm);
		update_power_state(codec, 0x3b, parm);
		update_power_state(codec, 0x1b, parm);
	}
	update_conv_power_state(codec, 0xa, parm, 2);

	/* PW1 (25h), MW1(35h), AOW1 (9h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x25, &parm);
	update_power_state(codec, 0x35, parm);
	if (smart51_enabled(codec)) {
		/* PW6(2ah), MW6(3ah), MUX6(1ah) */
		set_pin_power_state(codec, 0x2a, &parm);
		update_power_state(codec, 0x3a, parm);
		update_power_state(codec, 0x1a, parm);
	}
	update_conv_power_state(codec, 0x9, parm, 1);

	/* PW4 (28h), MW4 (38h), MUX4(18h), AOW3(bh)/AOW0(8h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x28, &parm);
	update_power_state(codec, 0x38, parm);
	update_power_state(codec, 0x18, parm);
	if (spec->gen.indep_hp_enabled)
		update_conv_power_state(codec, 0xb, parm, 3);
	parm2 = parm; /* for pin 0x0b */

	/* PW0 (24h), MW0(34h), MW9(3fh), AOW0 (8h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x24, &parm);
	update_power_state(codec, 0x34, parm);
	if (!spec->gen.indep_hp_enabled && parm2 != AC_PWRST_D3)
		parm = parm2;
	update_conv_power_state(codec, 0x8, parm, 0);
	/* MW9 (21h), Mw2 (1ah), AOW0 (8h) */
	update_power_state(codec, 0x3f, imux_is_smixer ? AC_PWRST_D0 : parm);
}

static int patch_vt3476(struct hda_codec *codec)
{
	struct via_spec *spec;
	int err;

	/* create a codec specific record */
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->gen.mixer_nid = 0x3f;
	add_secret_dac_path(codec);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++] = vt3476_init_verbs;

	codec->patch_ops = via_patch_ops;

	spec->set_widgets_power_state = set_widgets_power_state_vt3476;

	return 0;
}

/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_via[] = {
	{ .id = 0x11061708, .name = "VT1708", .patch = patch_vt1708},
	{ .id = 0x11061709, .name = "VT1708", .patch = patch_vt1708},
	{ .id = 0x1106170a, .name = "VT1708", .patch = patch_vt1708},
	{ .id = 0x1106170b, .name = "VT1708", .patch = patch_vt1708},
	{ .id = 0x1106e710, .name = "VT1709 10-Ch",
	  .patch = patch_vt1709},
	{ .id = 0x1106e711, .name = "VT1709 10-Ch",
	  .patch = patch_vt1709},
	{ .id = 0x1106e712, .name = "VT1709 10-Ch",
	  .patch = patch_vt1709},
	{ .id = 0x1106e713, .name = "VT1709 10-Ch",
	  .patch = patch_vt1709},
	{ .id = 0x1106e714, .name = "VT1709 6-Ch",
	  .patch = patch_vt1709},
	{ .id = 0x1106e715, .name = "VT1709 6-Ch",
	  .patch = patch_vt1709},
	{ .id = 0x1106e716, .name = "VT1709 6-Ch",
	  .patch = patch_vt1709},
	{ .id = 0x1106e717, .name = "VT1709 6-Ch",
	  .patch = patch_vt1709},
	{ .id = 0x1106e720, .name = "VT1708B 8-Ch",
	  .patch = patch_vt1708B},
	{ .id = 0x1106e721, .name = "VT1708B 8-Ch",
	  .patch = patch_vt1708B},
	{ .id = 0x1106e722, .name = "VT1708B 8-Ch",
	  .patch = patch_vt1708B},
	{ .id = 0x1106e723, .name = "VT1708B 8-Ch",
	  .patch = patch_vt1708B},
	{ .id = 0x1106e724, .name = "VT1708B 4-Ch",
	  .patch = patch_vt1708B},
	{ .id = 0x1106e725, .name = "VT1708B 4-Ch",
	  .patch = patch_vt1708B},
	{ .id = 0x1106e726, .name = "VT1708B 4-Ch",
	  .patch = patch_vt1708B},
	{ .id = 0x1106e727, .name = "VT1708B 4-Ch",
	  .patch = patch_vt1708B},
	{ .id = 0x11060397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11061397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11062397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11063397, .name = "VT1708S",
	  .patch = patch_vt1708S},
	{ .id = 0x11064397, .name = "VT1705",
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
	{ .id = 0x11060446, .name = "VT1802",
		.patch = patch_vt2002P},
	{ .id = 0x11068446, .name = "VT1802",
		.patch = patch_vt2002P},
	{ .id = 0x11064760, .name = "VT1705CF",
		.patch = patch_vt3476},
	{ .id = 0x11064761, .name = "VT1708SCE",
		.patch = patch_vt3476},
	{ .id = 0x11064762, .name = "VT1808",
		.patch = patch_vt3476},
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
