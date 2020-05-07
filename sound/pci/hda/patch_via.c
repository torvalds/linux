// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for VIA VT17xx/VT18xx/VT20xx codec
 *
 *  (C) 2006-2009 VIA Technology, Inc.
 *  (C) 2006-2008 Takashi Iwai <tiwai@suse.de>
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
#include <sound/hda_codec.h>
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

	/* HP mode source */
	unsigned int dmic_enabled;
	enum VIA_HDA_CODEC codec_type;

	/* analog low-power control */
	bool alc_mode;

	/* work to check hp jack state */
	int hp_work_active;
	int vt1708_jack_detect;
};

static enum VIA_HDA_CODEC get_codec_type(struct hda_codec *codec);
static void via_playback_pcm_hook(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream,
				  int action);

static const struct hda_codec_ops via_patch_ops; /* defined below */

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
	spec->gen.indep_hp = 1;
	spec->gen.keep_eapd_on = 1;
	spec->gen.pcm_playback_hook = via_playback_pcm_hook;
	spec->gen.add_stereo_mix_input = HDA_HINT_STEREO_MIX_AUTO;
	codec->power_save_node = 1;
	spec->gen.power_down_unused = 1;
	codec->patch_ops = via_patch_ops;
	return spec;
}

static enum VIA_HDA_CODEC get_codec_type(struct hda_codec *codec)
{
	u32 vendor_id = codec->core.vendor_id;
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
		codec->jackpoll_interval = 0;
		cancel_delayed_work_sync(&codec->jackpoll_work);
		spec->hp_work_active = false;
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
			schedule_delayed_work(&codec->jackpoll_work, 0);
			spec->hp_work_active = true;
		}
	} else if (!hp_detect_with_aa(codec))
		vt1708_stop_hp_work(codec);
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

	ucontrol->value.enumerated.item[0] = spec->gen.power_down_unused;
	return 0;
}

static int via_pin_power_ctl_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	bool val = !!ucontrol->value.enumerated.item[0];

	if (val == spec->gen.power_down_unused)
		return 0;
	/* codec->power_save_node = val; */ /* widget PM seems yet broken */
	spec->gen.power_down_unused = val;
	analog_low_current_mode(codec);
	return 1;
}

static const struct snd_kcontrol_new via_pin_power_ctl_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Dynamic Power-Control",
	.info = via_pin_power_ctl_info,
	.get = via_pin_power_ctl_get,
	.put = via_pin_power_ctl_put,
};

#ifdef CONFIG_SND_HDA_INPUT_BEEP
/* additional beep mixers; the actual parameters are overwritten at build */
static const struct snd_kcontrol_new via_beep_mixer[] = {
	HDA_CODEC_VOLUME_MONO("Beep Playback Volume", 0, 1, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_BEEP_MONO("Beep Playback Switch", 0, 1, 0, HDA_OUTPUT),
};

static int set_beep_amp(struct via_spec *spec, hda_nid_t nid,
			int idx, int dir)
{
	struct snd_kcontrol_new *knew;
	unsigned int beep_amp = HDA_COMPOSE_AMP_VAL(nid, 1, idx, dir);
	int i;

	spec->gen.beep_nid = nid;
	for (i = 0; i < ARRAY_SIZE(via_beep_mixer); i++) {
		knew = snd_hda_gen_add_kctl(&spec->gen, NULL,
					    &via_beep_mixer[i]);
		if (!knew)
			return -ENOMEM;
		knew->private_value = beep_amp;
	}
	return 0;
}

static int auto_parse_beep(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	hda_nid_t nid;

	for_each_hda_codec_node(nid, codec)
		if (get_wcaps_type(get_wcaps(codec, nid)) == AC_WID_BEEP)
			return set_beep_amp(spec, nid, 0, HDA_OUTPUT);
	return 0;
}
#else
#define auto_parse_beep(codec)	0
#endif

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

	if (!codec->power_save_node)
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
	snd_hda_codec_write(codec, codec->core.afg, 0, verb, parm);
}

static void analog_low_current_mode(struct hda_codec *codec)
{
	return __analog_low_current_mode(codec, false);
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
	vt1708_stop_hp_work(codec);
	snd_hda_gen_free(codec);
}

#ifdef CONFIG_PM
static int via_suspend(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	vt1708_stop_hp_work(codec);

	/* Fix pop noise on headphones */
	if (spec->codec_type == VT1802)
		snd_hda_shutup_pins(codec);

	return 0;
}

static int via_resume(struct hda_codec *codec)
{
	/* some delay here to make jack detection working (bko#98921) */
	msleep(10);
	codec->patch_ops.init(codec);
	snd_hda_regmap_sync(codec);
	return 0;
}
#endif

#ifdef CONFIG_PM
static int via_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct via_spec *spec = codec->spec;
	analog_low_current_mode(codec);
	vt1708_update_hp_work(codec);
	return snd_hda_check_amp_list_power(codec, &spec->gen.loopback, nid);
}
#endif

/*
 */

static int via_init(struct hda_codec *codec);

static const struct hda_codec_ops via_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = via_init,
	.free = via_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = via_suspend,
	.resume = via_resume,
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

static const struct snd_kcontrol_new vt1708_jack_detect_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Jack Detect",
	.count = 1,
	.info = snd_ctl_boolean_mono_info,
	.get = vt1708_jack_detect_get,
	.put = vt1708_jack_detect_put,
};

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

	err = auto_parse_beep(codec);
	if (err < 0)
		return err;

	if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &via_pin_power_ctl_enum))
		return -ENOMEM;

	/* disable widget PM at start for compatibility */
	codec->power_save_node = 0;
	spec->gen.power_down_unused = 0;
	return 0;
}

static int via_init(struct hda_codec *codec)
{
	/* init power states */
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
	err = snd_hda_gen_build_controls(codec);
	codec->jackpoll_interval = old_interval;
	return err;
}

static int vt1708_build_pcms(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i, err;

	err = snd_hda_gen_build_pcms(codec);
	if (err < 0 || codec->core.vendor_id != 0x11061708)
		return err;

	/* We got noisy outputs on the right channel on VT1708 when
	 * 24bit samples are used.  Until any workaround is found,
	 * disable the 24bit format, so far.
	 */
	for (i = 0; i < ARRAY_SIZE(spec->gen.pcm_rec); i++) {
		struct hda_pcm *info = spec->gen.pcm_rec[i];
		if (!info)
			continue;
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

	/* override some patch_ops */
	codec->patch_ops.build_controls = vt1708_build_controls;
	codec->patch_ops.build_pcms = vt1708_build_pcms;
	spec->gen.mixer_nid = 0x17;

	/* set jackpoll_interval while parsing the codec */
	codec->jackpoll_interval = msecs_to_jiffies(100);
	spec->vt1708_jack_detect = 1;

	/* don't support the input jack switching due to lack of unsol event */
	/* (it may work with polling, though, but it needs testing) */
	spec->gen.suppress_auto_mic = 1;
	/* Some machines show the broken speaker mute */
	spec->gen.auto_mute_via_amp = 1;

	/* Add HP and CD pin config connect bit re-config action */
	vt1708_set_pinconfig_connect(codec, VT1708_HP_PIN_NID);
	vt1708_set_pinconfig_connect(codec, VT1708_CD_PIN_NID);

	err = snd_hda_add_verbs(codec, vt1708_init_verbs);
	if (err < 0)
		goto error;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0)
		goto error;

	/* add jack detect on/off control */
	if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &vt1708_jack_detect_ctl)) {
		err = -ENOMEM;
		goto error;
	}

	/* clear jackpoll_interval again; it's set dynamically */
	codec->jackpoll_interval = 0;

	return 0;

 error:
	via_free(codec);
	return err;
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
	if (err < 0)
		goto error;

	return 0;

 error:
	via_free(codec);
	return err;
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
	if (err < 0)
		goto error;

	return 0;

 error:
	via_free(codec);
	return err;
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
	snd_hda_override_wcaps(codec, pin,
			       get_wcaps(codec, pin) | AC_WCAP_IN_AMP);
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
	if (get_codec_type(codec) == VT1708BCE)
		snd_hda_codec_set_name(codec, "VT1708BCE");
	/* correct names for VT1705 */
	if (codec->core.vendor_id == 0x11064397)
		snd_hda_codec_set_name(codec, "VT1705");

	err = snd_hda_add_verbs(codec, vt1708S_init_verbs);
	if (err < 0)
		goto error;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0)
		goto error;

	return 0;

 error:
	via_free(codec);
	return err;
}

/* Patch for VT1702 */

static const struct hda_verb vt1702_init_verbs[] = {
	/* mixer enable */
	{0x1, 0xF88, 0x3},
	/* GPIO 0~2 */
	{0x1, 0xF82, 0x3F},
	{ }
};

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

	err = snd_hda_add_verbs(codec, vt1702_init_verbs);
	if (err < 0)
		goto error;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0)
		goto error;

	return 0;

 error:
	via_free(codec);
	return err;
}

/* Patch for VT1718S */

static const struct hda_verb vt1718S_init_verbs[] = {
	/* Enable MW0 adjust Gain 5 */
	{0x1, 0xfb2, 0x10},
	/* Enable Boost Volume backdoor */
	{0x1, 0xf88, 0x8},

	{ }
};

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
	for_each_hda_codec_node(nid, codec) {
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

	err = snd_hda_add_verbs(codec, vt1718S_init_verbs);
	if (err < 0)
		goto error;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0)
		goto error;

	return 0;

 error:
	via_free(codec);
	return err;
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
	return 1;
}

static const struct snd_kcontrol_new vt1716s_dmic_mixer_vol =
	HDA_CODEC_VOLUME("Digital Mic Capture Volume", 0x22, 0x0, HDA_INPUT);
static const struct snd_kcontrol_new vt1716s_dmic_mixer_sw = {
	 .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name = "Digital Mic Capture Switch",
	 .subdevice = HDA_SUBDEV_NID_FLAG | 0x26,
	 .count = 1,
	 .info = vt1716s_dmic_info,
	 .get = vt1716s_dmic_get,
	 .put = vt1716s_dmic_put,
};


/* mono-out mixer elements */
static const struct snd_kcontrol_new vt1716S_mono_out_mixer =
	HDA_CODEC_MUTE("Mono Playback Switch", 0x2a, 0x0, HDA_OUTPUT);

static const struct hda_verb vt1716S_init_verbs[] = {
	/* Enable Boost Volume backdoor */
	{0x1, 0xf8a, 0x80},
	/* don't bybass mixer */
	{0x1, 0xf88, 0xc0},
	/* Enable mono output */
	{0x1, 0xf90, 0x08},
	{ }
};

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

	err = snd_hda_add_verbs(codec, vt1716S_init_verbs);
	if (err < 0)
		goto error;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0)
		goto error;

	if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &vt1716s_dmic_mixer_vol) ||
	    !snd_hda_gen_add_kctl(&spec->gen, NULL, &vt1716s_dmic_mixer_sw) ||
	    !snd_hda_gen_add_kctl(&spec->gen, NULL, &vt1716S_mono_out_mixer)) {
		err = -ENOMEM;
		goto error;
	}

	return 0;

 error:
	via_free(codec);
	return err;
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
	static const hda_nid_t conn_24[] = { 0x14, 0x1c };
	static const hda_nid_t conn_33[] = { 0x1c };

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

	if (spec->codec_type == VT1802)
		err = snd_hda_add_verbs(codec, vt1802_init_verbs);
	else
		err = snd_hda_add_verbs(codec, vt2002P_init_verbs);
	if (err < 0)
		goto error;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0)
		goto error;

	return 0;

 error:
	via_free(codec);
	return err;
}

/* for vt1812 */

static const struct hda_verb vt1812_init_verbs[] = {
	/* Enable Boost Volume backdoor */
	{0x1, 0xfb9, 0x24},
	/* Enable AOW0 to MW9 */
	{0x1, 0xfb8, 0xa8},
	{ }
};

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

	err = snd_hda_add_verbs(codec, vt1812_init_verbs);
	if (err < 0)
		goto error;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0)
		goto error;

	return 0;

 error:
	via_free(codec);
	return err;
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

	err = snd_hda_add_verbs(codec, vt3476_init_verbs);
	if (err < 0)
		goto error;

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0)
		goto error;

	return 0;

 error:
	via_free(codec);
	return err;
}

/*
 * patch entries
 */
static const struct hda_device_id snd_hda_id_via[] = {
	HDA_CODEC_ENTRY(0x11061708, "VT1708", patch_vt1708),
	HDA_CODEC_ENTRY(0x11061709, "VT1708", patch_vt1708),
	HDA_CODEC_ENTRY(0x1106170a, "VT1708", patch_vt1708),
	HDA_CODEC_ENTRY(0x1106170b, "VT1708", patch_vt1708),
	HDA_CODEC_ENTRY(0x1106e710, "VT1709 10-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e711, "VT1709 10-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e712, "VT1709 10-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e713, "VT1709 10-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e714, "VT1709 6-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e715, "VT1709 6-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e716, "VT1709 6-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e717, "VT1709 6-Ch", patch_vt1709),
	HDA_CODEC_ENTRY(0x1106e720, "VT1708B 8-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e721, "VT1708B 8-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e722, "VT1708B 8-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e723, "VT1708B 8-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e724, "VT1708B 4-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e725, "VT1708B 4-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e726, "VT1708B 4-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x1106e727, "VT1708B 4-Ch", patch_vt1708B),
	HDA_CODEC_ENTRY(0x11060397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11061397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11062397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11063397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11064397, "VT1705", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11065397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11066397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11067397, "VT1708S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11060398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11061398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11062398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11063398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11064398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11065398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11066398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11067398, "VT1702", patch_vt1702),
	HDA_CODEC_ENTRY(0x11060428, "VT1718S", patch_vt1718S),
	HDA_CODEC_ENTRY(0x11064428, "VT1718S", patch_vt1718S),
	HDA_CODEC_ENTRY(0x11060441, "VT2020", patch_vt1718S),
	HDA_CODEC_ENTRY(0x11064441, "VT1828S", patch_vt1718S),
	HDA_CODEC_ENTRY(0x11060433, "VT1716S", patch_vt1716S),
	HDA_CODEC_ENTRY(0x1106a721, "VT1716S", patch_vt1716S),
	HDA_CODEC_ENTRY(0x11060438, "VT2002P", patch_vt2002P),
	HDA_CODEC_ENTRY(0x11064438, "VT2002P", patch_vt2002P),
	HDA_CODEC_ENTRY(0x11060448, "VT1812", patch_vt1812),
	HDA_CODEC_ENTRY(0x11060440, "VT1818S", patch_vt1708S),
	HDA_CODEC_ENTRY(0x11060446, "VT1802", patch_vt2002P),
	HDA_CODEC_ENTRY(0x11068446, "VT1802", patch_vt2002P),
	HDA_CODEC_ENTRY(0x11064760, "VT1705CF", patch_vt3476),
	HDA_CODEC_ENTRY(0x11064761, "VT1708SCE", patch_vt3476),
	HDA_CODEC_ENTRY(0x11064762, "VT1808", patch_vt3476),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_via);

static struct hda_codec_driver via_driver = {
	.id = snd_hda_id_via,
};

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("VIA HD-audio codec");

module_hda_codec_driver(via_driver);
