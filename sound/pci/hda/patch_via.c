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

#define MAX_NID_PATH_DEPTH	5

/* output-path: DAC -> ... -> pin
 * idx[] contains the source index number of the next widget;
 * e.g. idx[0] is the index of the DAC selected by path[1] widget
 * multi[] indicates whether it's a selector widget with multi-connectors
 * (i.e. the connection selection is mandatory)
 * vol_ctl and mute_ctl contains the NIDs for the assigned mixers
 */
struct nid_path {
	int depth;
	hda_nid_t path[MAX_NID_PATH_DEPTH];
	unsigned char idx[MAX_NID_PATH_DEPTH];
	unsigned char multi[MAX_NID_PATH_DEPTH];
	unsigned int vol_ctl;
	unsigned int mute_ctl;
};

/* input-path */
struct via_input {
	hda_nid_t pin;	/* input-pin or aa-mix */
	int adc_idx;	/* ADC index to be used */
	int mux_idx;	/* MUX index (if any) */
	const char *label;	/* input-source label */
};

#define VIA_MAX_ADCS	3

enum {
	STREAM_MULTI_OUT = (1 << 0),
	STREAM_INDEP_HP = (1 << 1),
};

struct via_spec {
	struct hda_gen_spec gen;

	/* codec parameterization */
	const struct snd_kcontrol_new *mixers[6];
	unsigned int num_mixers;

	const struct hda_verb *init_verbs[5];
	unsigned int num_iverbs;

	char stream_name_analog[32];
	char stream_name_hp[32];
	const struct hda_pcm_stream *stream_analog_playback;
	const struct hda_pcm_stream *stream_analog_capture;

	char stream_name_digital[32];
	const struct hda_pcm_stream *stream_digital_playback;
	const struct hda_pcm_stream *stream_digital_capture;

	/* playback */
	struct hda_multi_out multiout;
	hda_nid_t slave_dig_outs[2];
	hda_nid_t hp_dac_nid;
	hda_nid_t speaker_dac_nid;
	int hp_indep_shared;	/* indep HP-DAC is shared with side ch */
	int opened_streams;	/* STREAM_* bits */
	int active_streams;	/* STREAM_* bits */
	int aamix_mode;		/* loopback is enabled for output-path? */

	/* Output-paths:
	 * There are different output-paths depending on the setup.
	 * out_path, hp_path and speaker_path are primary paths.  If both
	 * direct DAC and aa-loopback routes are available, these contain
	 * the former paths.  Meanwhile *_mix_path contain the paths with
	 * loopback mixer.  (Since the loopback is only for front channel,
	 * no out_mix_path for surround channels.)
	 * The HP output has another path, hp_indep_path, which is used in
	 * the independent-HP mode.
	 */
	struct nid_path out_path[HDA_SIDE + 1];
	struct nid_path out_mix_path;
	struct nid_path hp_path;
	struct nid_path hp_mix_path;
	struct nid_path hp_indep_path;
	struct nid_path speaker_path;
	struct nid_path speaker_mix_path;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t adc_nids[VIA_MAX_ADCS];
	hda_nid_t mux_nids[VIA_MAX_ADCS];
	hda_nid_t aa_mix_nid;
	hda_nid_t dig_in_nid;

	/* capture source */
	bool dyn_adc_switch;
	int num_inputs;
	struct via_input inputs[AUTO_CFG_MAX_INS + 1];
	unsigned int cur_mux[VIA_MAX_ADCS];

	/* dynamic DAC switching */
	unsigned int cur_dac_stream_tag;
	unsigned int cur_dac_format;
	unsigned int cur_hp_stream_tag;
	unsigned int cur_hp_format;

	/* dynamic ADC switching */
	hda_nid_t cur_adc;
	unsigned int cur_adc_stream_tag;
	unsigned int cur_adc_format;

	/* PCM information */
	struct hda_pcm pcm_rec[3];

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	struct snd_array kctls;
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];

	/* HP mode source */
	unsigned int hp_independent_mode;
	unsigned int dmic_enabled;
	unsigned int no_pin_power_ctl;
	enum VIA_HDA_CODEC codec_type;

	/* analog low-power control */
	bool alc_mode;

	/* smart51 setup */
	unsigned int smart51_nums;
	hda_nid_t smart51_pins[2];
	int smart51_idxs[2];
	const char *smart51_labels[2];
	unsigned int smart51_enabled;

	/* work to check hp jack state */
	struct hda_codec *codec;
	struct delayed_work vt1708_hp_work;
	int hp_work_active;
	int vt1708_jack_detect;
	int vt1708_hp_present;

	void (*set_widgets_power_state)(struct hda_codec *codec);
	unsigned int dac_stream_tag[4];

	struct hda_loopback_check loopback;
	int num_loopbacks;
	struct hda_amp_list loopback_list[8];

	/* bind capture-volume */
	struct hda_bind_ctls *bind_cap_vol;
	struct hda_bind_ctls *bind_cap_sw;

	struct mutex config_mutex;
};

static enum VIA_HDA_CODEC get_codec_type(struct hda_codec *codec);
static struct via_spec * via_new_spec(struct hda_codec *codec)
{
	struct via_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return NULL;

	snd_array_init(&spec->kctls, sizeof(struct snd_kcontrol_new), 32);
	mutex_init(&spec->config_mutex);
	codec->spec = spec;
	spec->codec = codec;
	spec->codec_type = get_codec_type(codec);
	/* VT1708BCE & VT1708S are almost same */
	if (spec->codec_type == VT1708BCE)
		spec->codec_type = VT1708S;
	snd_hda_gen_init(&spec->gen);
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

#define VIA_JACK_EVENT		0x20
#define VIA_HP_EVENT		0x01
#define VIA_LINE_EVENT		0x03

enum {
	VIA_CTL_WIDGET_VOL,
	VIA_CTL_WIDGET_MUTE,
	VIA_CTL_WIDGET_ANALOG_MUTE,
};

static void analog_low_current_mode(struct hda_codec *codec);
static bool is_aa_path_mute(struct hda_codec *codec);

#define hp_detect_with_aa(codec) \
	(snd_hda_get_bool_hint(codec, "analog_loopback_hp_detect") == 1 && \
	 !is_aa_path_mute(codec))

static void vt1708_stop_hp_work(struct via_spec *spec)
{
	if (spec->codec_type != VT1708 || spec->autocfg.hp_pins[0] == 0)
		return;
	if (spec->hp_work_active) {
		snd_hda_codec_write(spec->codec, 0x1, 0, 0xf81, 1);
		cancel_delayed_work_sync(&spec->vt1708_hp_work);
		spec->hp_work_active = 0;
	}
}

static void vt1708_update_hp_work(struct via_spec *spec)
{
	if (spec->codec_type != VT1708 || spec->autocfg.hp_pins[0] == 0)
		return;
	if (spec->vt1708_jack_detect &&
	    (spec->active_streams || hp_detect_with_aa(spec->codec))) {
		if (!spec->hp_work_active) {
			snd_hda_codec_write(spec->codec, 0x1, 0, 0xf81, 0);
			schedule_delayed_work(&spec->vt1708_hp_work,
					      msecs_to_jiffies(100));
			spec->hp_work_active = 1;
		}
	} else if (!hp_detect_with_aa(spec->codec))
		vt1708_stop_hp_work(spec);
}

static void set_widgets_power_state(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	if (spec->set_widgets_power_state)
		spec->set_widgets_power_state(codec);
}

static int analog_input_switch_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	int change = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	set_widgets_power_state(codec);
	analog_low_current_mode(snd_kcontrol_chip(kcontrol));
	vt1708_update_hp_work(codec->spec);
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

static const struct snd_kcontrol_new via_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	ANALOG_INPUT_MUTE,
};


/* add dynamic controls */
static struct snd_kcontrol_new *__via_clone_ctl(struct via_spec *spec,
				const struct snd_kcontrol_new *tmpl,
				const char *name)
{
	struct snd_kcontrol_new *knew;

	knew = snd_array_new(&spec->kctls);
	if (!knew)
		return NULL;
	*knew = *tmpl;
	if (!name)
		name = tmpl->name;
	if (name) {
		knew->name = kstrdup(name, GFP_KERNEL);
		if (!knew->name)
			return NULL;
	}
	return knew;
}

static int __via_add_control(struct via_spec *spec, int type, const char *name,
			     int idx, unsigned long val)
{
	struct snd_kcontrol_new *knew;

	knew = __via_clone_ctl(spec, &via_control_templates[type], name);
	if (!knew)
		return -ENOMEM;
	knew->index = idx;
	if (get_amp_nid_(val))
		knew->subdevice = HDA_SUBDEV_AMP_FLAG;
	knew->private_value = val;
	return 0;
}

#define via_add_control(spec, type, name, val) \
	__via_add_control(spec, type, name, 0, val)

#define via_clone_control(spec, tmpl) __via_clone_ctl(spec, tmpl, NULL)

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
				int type_idx, int idx, int mix_nid)
{
	char name[32];
	int err;

	sprintf(name, "%s Playback Volume", ctlname);
	err = __via_add_control(spec, VIA_CTL_WIDGET_VOL, name, type_idx,
			      HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	sprintf(name, "%s Playback Switch", ctlname);
	err = __via_add_control(spec, VIA_CTL_WIDGET_ANALOG_MUTE, name, type_idx,
			      HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	return 0;
}

#define get_connection_index(codec, mux, nid) \
	snd_hda_get_conn_index(codec, mux, nid, 0)

static bool check_amp_caps(struct hda_codec *codec, hda_nid_t nid, int dir,
			   unsigned int mask)
{
	unsigned int caps;
	if (!nid)
		return false;
	caps = get_wcaps(codec, nid);
	if (dir == HDA_INPUT)
		caps &= AC_WCAP_IN_AMP;
	else
		caps &= AC_WCAP_OUT_AMP;
	if (!caps)
		return false;
	if (query_amp_caps(codec, nid, dir) & mask)
		return true;
	return false;
}

#define have_mute(codec, nid, dir) \
	check_amp_caps(codec, nid, dir, AC_AMPCAP_MUTE)

/* enable/disable the output-route mixers */
static void activate_output_mix(struct hda_codec *codec, struct nid_path *path,
				hda_nid_t mix_nid, int idx, bool enable)
{
	int i, num, val;

	if (!path)
		return;
	num = snd_hda_get_num_conns(codec, mix_nid);
	for (i = 0; i < num; i++) {
		if (i == idx)
			val = AMP_IN_UNMUTE(i);
		else
			val = AMP_IN_MUTE(i);
		snd_hda_codec_write(codec, mix_nid, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, val);
	}
}

/* enable/disable the output-route */
static void activate_output_path(struct hda_codec *codec, struct nid_path *path,
				 bool enable, bool force)
{
	struct via_spec *spec = codec->spec;
	int i;
	for (i = 0; i < path->depth; i++) {
		hda_nid_t src, dst;
		int idx = path->idx[i];
		src = path->path[i];			
		if (i < path->depth - 1)
			dst = path->path[i + 1];
		else
			dst = 0;
		if (enable && path->multi[i])
			snd_hda_codec_write(codec, dst, 0,
					    AC_VERB_SET_CONNECT_SEL, idx);
		if (!force && (dst == spec->aa_mix_nid))
			continue;
		if (have_mute(codec, dst, HDA_INPUT))
			activate_output_mix(codec, path, dst, idx, enable);
		if (!force && (src == path->vol_ctl || src == path->mute_ctl))
			continue;
		if (have_mute(codec, src, HDA_OUTPUT)) {
			int val = enable ? AMP_OUT_UNMUTE : AMP_OUT_MUTE;
			snd_hda_codec_write(codec, src, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE, val);
		}
	}
}

/* set the given pin as output */
static void init_output_pin(struct hda_codec *codec, hda_nid_t pin,
			    int pin_type)
{
	if (!pin)
		return;
	snd_hda_set_pin_ctl(codec, pin, pin_type);
	if (snd_hda_query_pin_caps(codec, pin) & AC_PINCAP_EAPD)
		snd_hda_codec_write(codec, pin, 0,
				    AC_VERB_SET_EAPD_BTLENABLE, 0x02);
}

static void via_auto_init_output(struct hda_codec *codec,
				 struct nid_path *path, int pin_type)
{
	unsigned int caps;
	hda_nid_t pin;

	if (!path->depth)
		return;
	pin = path->path[path->depth - 1];

	init_output_pin(codec, pin, pin_type);
	if (get_wcaps(codec, pin) & AC_WCAP_OUT_AMP)
		caps = query_amp_caps(codec, pin, HDA_OUTPUT);
	else
		caps = 0;
	if (caps & AC_AMPCAP_MUTE) {
		unsigned int val;
		val = (caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT;
		snd_hda_codec_write(codec, pin, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_OUT_MUTE | val);
	}
	activate_output_path(codec, path, true, true); /* force on */
}

static void via_auto_init_multi_out(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct nid_path *path;
	int i;

	for (i = 0; i < spec->autocfg.line_outs + spec->smart51_nums; i++) {
		path = &spec->out_path[i];
		if (!i && spec->aamix_mode && spec->out_mix_path.depth)
			path = &spec->out_mix_path;
		via_auto_init_output(codec, path, PIN_OUT);
	}
}

/* deactivate the inactive headphone-paths */
static void deactivate_hp_paths(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int shared = spec->hp_indep_shared;

	if (spec->hp_independent_mode) {
		activate_output_path(codec, &spec->hp_path, false, false);
		activate_output_path(codec, &spec->hp_mix_path, false, false);
		if (shared)
			activate_output_path(codec, &spec->out_path[shared],
					     false, false);
	} else if (spec->aamix_mode || !spec->hp_path.depth) {
		activate_output_path(codec, &spec->hp_indep_path, false, false);
		activate_output_path(codec, &spec->hp_path, false, false);
	} else {
		activate_output_path(codec, &spec->hp_indep_path, false, false);
		activate_output_path(codec, &spec->hp_mix_path, false, false);
	}
}

static void via_auto_init_hp_out(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;

	if (!spec->hp_path.depth) {
		via_auto_init_output(codec, &spec->hp_mix_path, PIN_HP);
		return;
	}
	deactivate_hp_paths(codec);
	if (spec->hp_independent_mode)
		via_auto_init_output(codec, &spec->hp_indep_path, PIN_HP);
	else if (spec->aamix_mode)
		via_auto_init_output(codec, &spec->hp_mix_path, PIN_HP);
	else
		via_auto_init_output(codec, &spec->hp_path, PIN_HP);
}

static void via_auto_init_speaker_out(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;

	if (!spec->autocfg.speaker_outs)
		return;
	if (!spec->speaker_path.depth) {
		via_auto_init_output(codec, &spec->speaker_mix_path, PIN_OUT);
		return;
	}
	if (!spec->aamix_mode) {
		activate_output_path(codec, &spec->speaker_mix_path,
				     false, false);
		via_auto_init_output(codec, &spec->speaker_path, PIN_OUT);
	} else {
		activate_output_path(codec, &spec->speaker_path, false, false);
		via_auto_init_output(codec, &spec->speaker_mix_path, PIN_OUT);
	}
}

static bool is_smart51_pins(struct hda_codec *codec, hda_nid_t pin);
static void via_hp_automute(struct hda_codec *codec);

static void via_auto_init_analog_input(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t conn[HDA_MAX_CONNECTIONS];
	unsigned int ctl;
	int i, num_conns;

	/* init ADCs */
	for (i = 0; i < spec->num_adc_nids; i++) {
		hda_nid_t nid = spec->adc_nids[i];
		if (!(get_wcaps(codec, nid) & AC_WCAP_IN_AMP) ||
		    !(query_amp_caps(codec, nid, HDA_INPUT) & AC_AMPCAP_MUTE))
			continue;
		snd_hda_codec_write(codec, spec->adc_nids[i], 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(0));
	}

	/* init pins */
	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		if (spec->smart51_enabled && is_smart51_pins(codec, nid))
			ctl = PIN_OUT;
		else {
			ctl = PIN_IN;
			if (cfg->inputs[i].type == AUTO_PIN_MIC)
				ctl |= snd_hda_get_default_vref(codec, nid);
		}
		snd_hda_set_pin_ctl(codec, nid, ctl);
	}

	/* init input-src */
	for (i = 0; i < spec->num_adc_nids; i++) {
		int adc_idx = spec->inputs[spec->cur_mux[i]].adc_idx;
		/* secondary ADCs must have the unique MUX */
		if (i > 0 && !spec->mux_nids[i])
			break;
		if (spec->mux_nids[adc_idx]) {
			int mux_idx = spec->inputs[spec->cur_mux[i]].mux_idx;
			snd_hda_codec_write(codec, spec->mux_nids[adc_idx], 0,
					    AC_VERB_SET_CONNECT_SEL,
					    mux_idx);
		}
		if (spec->dyn_adc_switch)
			break; /* only one input-src */
	}

	/* init aa-mixer */
	if (!spec->aa_mix_nid)
		return;
	num_conns = snd_hda_get_connections(codec, spec->aa_mix_nid, conn,
					    ARRAY_SIZE(conn));
	for (i = 0; i < num_conns; i++) {
		unsigned int caps = get_wcaps(codec, conn[i]);
		if (get_wcaps_type(caps) == AC_WID_PIN)
			snd_hda_codec_write(codec, spec->aa_mix_nid, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_IN_MUTE(i));
	}
}

static void update_power_state(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int parm)
{
	if (snd_hda_codec_read(codec, nid, 0,
			       AC_VERB_GET_POWER_STATE, 0) == parm)
		return;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE, parm);
}

static void update_conv_power_state(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int parm, unsigned int index)
{
	struct via_spec *spec = codec->spec;
	unsigned int format;
	if (snd_hda_codec_read(codec, nid, 0,
			       AC_VERB_GET_POWER_STATE, 0) == parm)
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
	if ((spec->smart51_enabled && is_smart51_pins(codec, nid))
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

static const struct snd_kcontrol_new via_pin_power_ctl_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Dynamic Power-Control",
	.info = via_pin_power_ctl_info,
	.get = via_pin_power_ctl_get,
	.put = via_pin_power_ctl_put,
};


static int via_independent_hp_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = { "OFF", "ON" };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item >= 2)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int via_independent_hp_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->hp_independent_mode;
	return 0;
}

/* adjust spec->multiout setup according to the current flags */
static void setup_playback_multi_pcm(struct via_spec *spec)
{
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	spec->multiout.num_dacs = cfg->line_outs + spec->smart51_nums;
	spec->multiout.hp_nid = 0;
	if (!spec->hp_independent_mode) {
		if (!spec->hp_indep_shared)
			spec->multiout.hp_nid = spec->hp_dac_nid;
	} else {
		if (spec->hp_indep_shared)
			spec->multiout.num_dacs = cfg->line_outs - 1;
	}
}

/* update DAC setups according to indep-HP switch;
 * this function is called only when indep-HP is modified
 */
static void switch_indep_hp_dacs(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int shared = spec->hp_indep_shared;
	hda_nid_t shared_dac, hp_dac;

	if (!spec->opened_streams)
		return;

	shared_dac = shared ? spec->multiout.dac_nids[shared] : 0;
	hp_dac = spec->hp_dac_nid;
	if (spec->hp_independent_mode) {
		/* switch to indep-HP mode */
		if (spec->active_streams & STREAM_MULTI_OUT) {
			__snd_hda_codec_cleanup_stream(codec, hp_dac, 1);
			__snd_hda_codec_cleanup_stream(codec, shared_dac, 1);
		}
		if (spec->active_streams & STREAM_INDEP_HP)
			snd_hda_codec_setup_stream(codec, hp_dac,
						   spec->cur_hp_stream_tag, 0,
						   spec->cur_hp_format);
	} else {
		/* back to HP or shared-DAC */
		if (spec->active_streams & STREAM_INDEP_HP)
			__snd_hda_codec_cleanup_stream(codec, hp_dac, 1);
		if (spec->active_streams & STREAM_MULTI_OUT) {
			hda_nid_t dac;
			int ch;
			if (shared_dac) { /* reset mutli-ch DAC */
				dac = shared_dac;
				ch = shared * 2;
			} else { /* reset HP DAC */
				dac = hp_dac;
				ch = 0;
			}
			snd_hda_codec_setup_stream(codec, dac,
						   spec->cur_dac_stream_tag, ch,
						   spec->cur_dac_format);
		}
	}
	setup_playback_multi_pcm(spec);
}

static int via_independent_hp_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int cur, shared;

	mutex_lock(&spec->config_mutex);
	cur = !!ucontrol->value.enumerated.item[0];
	if (spec->hp_independent_mode == cur) {
		mutex_unlock(&spec->config_mutex);
		return 0;
	}
	spec->hp_independent_mode = cur;
	shared = spec->hp_indep_shared;
	deactivate_hp_paths(codec);
	if (cur)
		activate_output_path(codec, &spec->hp_indep_path, true, false);
	else {
		if (shared)
			activate_output_path(codec, &spec->out_path[shared],
					     true, false);
		if (spec->aamix_mode || !spec->hp_path.depth)
			activate_output_path(codec, &spec->hp_mix_path,
					     true, false);
		else
			activate_output_path(codec, &spec->hp_path,
					     true, false);
	}

	switch_indep_hp_dacs(codec);
	mutex_unlock(&spec->config_mutex);

	/* update jack power state */
	set_widgets_power_state(codec);
	via_hp_automute(codec);
	return 1;
}

static const struct snd_kcontrol_new via_hp_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Independent HP",
	.info = via_independent_hp_info,
	.get = via_independent_hp_get,
	.put = via_independent_hp_put,
};

static int via_hp_build(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;
	hda_nid_t nid;

	nid = spec->autocfg.hp_pins[0];
	knew = via_clone_control(spec, &via_hp_mixer);
	if (knew == NULL)
		return -ENOMEM;

	knew->subdevice = HDA_SUBDEV_NID_FLAG | nid;

	return 0;
}

static void notify_aa_path_ctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->smart51_nums; i++) {
		struct snd_kcontrol *ctl;
		struct snd_ctl_elem_id id;
		memset(&id, 0, sizeof(id));
		id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		sprintf(id.name, "%s Playback Volume", spec->smart51_labels[i]);
		ctl = snd_hda_find_mixer_ctl(codec, id.name);
		if (ctl)
			snd_ctl_notify(codec->bus->card,
					SNDRV_CTL_EVENT_MASK_VALUE,
					&ctl->id);
	}
}

static void mute_aa_path(struct hda_codec *codec, int mute)
{
	struct via_spec *spec = codec->spec;
	int val = mute ? HDA_AMP_MUTE : HDA_AMP_UNMUTE;
	int i;

	/* check AA path's mute status */
	for (i = 0; i < spec->smart51_nums; i++) {
		if (spec->smart51_idxs[i] < 0)
			continue;
		snd_hda_codec_amp_stereo(codec, spec->aa_mix_nid,
					 HDA_INPUT, spec->smart51_idxs[i],
					 HDA_AMP_MUTE, val);
	}
}

static bool is_smart51_pins(struct hda_codec *codec, hda_nid_t pin)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->smart51_nums; i++)
		if (spec->smart51_pins[i] == pin)
			return true;
	return false;
}

static int via_smart51_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;

	*ucontrol->value.integer.value = spec->smart51_enabled;
	return 0;
}

static int via_smart51_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	int out_in = *ucontrol->value.integer.value
		? AC_PINCTL_OUT_EN : AC_PINCTL_IN_EN;
	int i;

	for (i = 0; i < spec->smart51_nums; i++) {
		hda_nid_t nid = spec->smart51_pins[i];
		unsigned int parm;

		parm = snd_hda_codec_read(codec, nid, 0,
					  AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		parm &= ~(AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN);
		parm |= out_in;
		snd_hda_set_pin_ctl(codec, nid, parm);
		if (out_in == AC_PINCTL_OUT_EN) {
			mute_aa_path(codec, 1);
			notify_aa_path_ctls(codec);
		}
	}
	spec->smart51_enabled = *ucontrol->value.integer.value;
	set_widgets_power_state(codec);
	return 1;
}

static const struct snd_kcontrol_new via_smart51_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Smart 5.1",
	.count = 1,
	.info = snd_ctl_boolean_mono_info,
	.get = via_smart51_get,
	.put = via_smart51_put,
};

static int via_smart51_build(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;

	if (!spec->smart51_nums)
		return 0;
	if (!via_clone_control(spec, &via_smart51_mixer))
		return -ENOMEM;
	return 0;
}

/* check AA path's mute status */
static bool is_aa_path_mute(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	const struct hda_amp_list *p;
	int i, ch, v;

	for (i = 0; i < spec->num_loopbacks; i++) {
		p = &spec->loopback_list[i];
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
		enable = is_aa_path_mute(codec) && !spec->opened_streams;
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

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb vt1708_init_verbs[] = {
	/* power down jack detect function */
	{0x1, 0xf81, 0x1},
	{ }
};

static void set_stream_open(struct hda_codec *codec, int bit, bool active)
{
	struct via_spec *spec = codec->spec;

	if (active)
		spec->opened_streams |= bit;
	else
		spec->opened_streams &= ~bit;
	analog_low_current_mode(codec);
}

static int via_playback_multi_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	int err;

	spec->multiout.num_dacs = cfg->line_outs + spec->smart51_nums;
	spec->multiout.max_channels = spec->multiout.num_dacs * 2;
	set_stream_open(codec, STREAM_MULTI_OUT, true);
	err = snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					    hinfo);
	if (err < 0) {
		set_stream_open(codec, STREAM_MULTI_OUT, false);
		return err;
	}
	return 0;
}

static int via_playback_multi_pcm_close(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream)
{
	set_stream_open(codec, STREAM_MULTI_OUT, false);
	return 0;
}

static int via_playback_hp_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;

	if (snd_BUG_ON(!spec->hp_dac_nid))
		return -EINVAL;
	set_stream_open(codec, STREAM_INDEP_HP, true);
	return 0;
}

static int via_playback_hp_pcm_close(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	set_stream_open(codec, STREAM_INDEP_HP, false);
	return 0;
}

static int via_playback_multi_pcm_prepare(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  unsigned int stream_tag,
					  unsigned int format,
					  struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;

	mutex_lock(&spec->config_mutex);
	setup_playback_multi_pcm(spec);
	snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag,
					 format, substream);
	/* remember for dynamic DAC switch with indep-HP */
	spec->active_streams |= STREAM_MULTI_OUT;
	spec->cur_dac_stream_tag = stream_tag;
	spec->cur_dac_format = format;
	mutex_unlock(&spec->config_mutex);
	vt1708_update_hp_work(spec);
	return 0;
}

static int via_playback_hp_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;

	mutex_lock(&spec->config_mutex);
	if (spec->hp_independent_mode)
		snd_hda_codec_setup_stream(codec, spec->hp_dac_nid,
					   stream_tag, 0, format);
	spec->active_streams |= STREAM_INDEP_HP;
	spec->cur_hp_stream_tag = stream_tag;
	spec->cur_hp_format = format;
	mutex_unlock(&spec->config_mutex);
	vt1708_update_hp_work(spec);
	return 0;
}

static int via_playback_multi_pcm_cleanup(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;

	mutex_lock(&spec->config_mutex);
	snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
	spec->active_streams &= ~STREAM_MULTI_OUT;
	mutex_unlock(&spec->config_mutex);
	vt1708_update_hp_work(spec);
	return 0;
}

static int via_playback_hp_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;

	mutex_lock(&spec->config_mutex);
	if (spec->hp_independent_mode)
		snd_hda_codec_setup_stream(codec, spec->hp_dac_nid, 0, 0, 0);
	spec->active_streams &= ~STREAM_INDEP_HP;
	mutex_unlock(&spec->config_mutex);
	vt1708_update_hp_work(spec);
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

/* analog capture with dynamic ADC switching */
static int via_dyn_adc_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;
	int adc_idx = spec->inputs[spec->cur_mux[0]].adc_idx;

	mutex_lock(&spec->config_mutex);
	spec->cur_adc = spec->adc_nids[adc_idx];
	spec->cur_adc_stream_tag = stream_tag;
	spec->cur_adc_format = format;
	snd_hda_codec_setup_stream(codec, spec->cur_adc, stream_tag, 0, format);
	mutex_unlock(&spec->config_mutex);
	return 0;
}

static int via_dyn_adc_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   struct snd_pcm_substream *substream)
{
	struct via_spec *spec = codec->spec;

	mutex_lock(&spec->config_mutex);
	snd_hda_codec_cleanup_stream(codec, spec->cur_adc);
	spec->cur_adc = 0;
	mutex_unlock(&spec->config_mutex);
	return 0;
}

/* re-setup the stream if running; called from input-src put */
static bool via_dyn_adc_pcm_resetup(struct hda_codec *codec, int cur)
{
	struct via_spec *spec = codec->spec;
	int adc_idx = spec->inputs[cur].adc_idx;
	hda_nid_t adc = spec->adc_nids[adc_idx];
	bool ret = false;

	mutex_lock(&spec->config_mutex);
	if (spec->cur_adc && spec->cur_adc != adc) {
		/* stream is running, let's swap the current ADC */
		__snd_hda_codec_cleanup_stream(codec, spec->cur_adc, 1);
		spec->cur_adc = adc;
		snd_hda_codec_setup_stream(codec, adc,
					   spec->cur_adc_stream_tag, 0,
					   spec->cur_adc_format);
		ret = true;
	}
	mutex_unlock(&spec->config_mutex);
	return ret;
}

static const struct hda_pcm_stream via_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_playback_multi_pcm_open,
		.close = via_playback_multi_pcm_close,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup
	},
};

static const struct hda_pcm_stream via_pcm_hp_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.open = via_playback_hp_pcm_open,
		.close = via_playback_hp_pcm_close,
		.prepare = via_playback_hp_pcm_prepare,
		.cleanup = via_playback_hp_pcm_cleanup
	},
};

static const struct hda_pcm_stream vt1708_pcm_analog_s16_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	/* NID is set in via_build_pcms */
	/* We got noisy outputs on the right channel on VT1708 when
	 * 24bit samples are used.  Until any workaround is found,
	 * disable the 24bit format, so far.
	 */
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.ops = {
		.open = via_playback_multi_pcm_open,
		.close = via_playback_multi_pcm_close,
		.prepare = via_playback_multi_pcm_prepare,
		.cleanup = via_playback_multi_pcm_cleanup
	},
};

static const struct hda_pcm_stream via_pcm_analog_capture = {
	.substreams = 1, /* will be changed in via_build_pcms() */
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.prepare = via_capture_pcm_prepare,
		.cleanup = via_capture_pcm_cleanup
	},
};

static const struct hda_pcm_stream via_pcm_dyn_adc_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in via_build_pcms */
	.ops = {
		.prepare = via_dyn_adc_capture_pcm_prepare,
		.cleanup = via_dyn_adc_capture_pcm_cleanup,
	},
};

static const struct hda_pcm_stream via_pcm_digital_playback = {
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

static const struct hda_pcm_stream via_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

/*
 * slave controls for virtual master
 */
static const char * const via_slave_pfxs[] = {
	"Front", "Surround", "Center", "LFE", "Side",
	"Headphone", "Speaker", "Bass Speaker",
	NULL,
};

static int via_build_controls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct snd_kcontrol *kctl;
	int err, i;

	spec->no_pin_power_ctl = 1;
	if (spec->set_widgets_power_state)
		if (!via_clone_control(spec, &via_pin_power_ctl_enum))
			return -ENOMEM;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}

	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec,
						    spec->multiout.dig_out_nid,
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

	/* if we have no master control, let's create it */
	if (!snd_hda_find_mixer_ctl(codec, "Master Playback Volume")) {
		unsigned int vmaster_tlv[4];
		snd_hda_set_vmaster_tlv(codec, spec->multiout.dac_nids[0],
					HDA_OUTPUT, vmaster_tlv);
		err = snd_hda_add_vmaster(codec, "Master Playback Volume",
					  vmaster_tlv, via_slave_pfxs,
					  "Playback Volume");
		if (err < 0)
			return err;
	}
	if (!snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = snd_hda_add_vmaster(codec, "Master Playback Switch",
					  NULL, via_slave_pfxs,
					  "Playback Switch");
		if (err < 0)
			return err;
	}

	/* assign Capture Source enums to NID */
	kctl = snd_hda_find_mixer_ctl(codec, "Input Source");
	for (i = 0; kctl && i < kctl->count; i++) {
		if (!spec->mux_nids[i])
			continue;
		err = snd_hda_add_nid(codec, kctl, i, spec->mux_nids[i]);
		if (err < 0)
			return err;
	}

	via_free_kctls(codec); /* no longer needed */

	err = snd_hda_jack_add_kctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	return 0;
}

static int via_build_pcms(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 0;
	codec->pcm_info = info;

	if (spec->multiout.num_dacs || spec->num_adc_nids) {
		snprintf(spec->stream_name_analog,
			 sizeof(spec->stream_name_analog),
			 "%s Analog", codec->chip_name);
		info->name = spec->stream_name_analog;

		if (spec->multiout.num_dacs) {
			if (!spec->stream_analog_playback)
				spec->stream_analog_playback =
					&via_pcm_analog_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				*spec->stream_analog_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
				spec->multiout.dac_nids[0];
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
				spec->multiout.max_channels;
			if (spec->autocfg.line_out_type == AUTO_PIN_SPEAKER_OUT
			    && spec->autocfg.line_outs == 2)
				info->stream[SNDRV_PCM_STREAM_PLAYBACK].chmap =
					snd_pcm_2_1_chmaps;
		}

		if (!spec->stream_analog_capture) {
			if (spec->dyn_adc_switch)
				spec->stream_analog_capture =
					&via_pcm_dyn_adc_analog_capture;
			else
				spec->stream_analog_capture =
					&via_pcm_analog_capture;
		}
		if (spec->num_adc_nids) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				*spec->stream_analog_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->adc_nids[0];
			if (!spec->dyn_adc_switch)
				info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams =
					spec->num_adc_nids;
		}
		codec->num_pcms++;
		info++;
	}

	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		snprintf(spec->stream_name_digital,
			 sizeof(spec->stream_name_digital),
			 "%s Digital", codec->chip_name);
		info->name = spec->stream_name_digital;
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
		if (spec->multiout.dig_out_nid) {
			if (!spec->stream_digital_playback)
				spec->stream_digital_playback =
					&via_pcm_digital_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				*spec->stream_digital_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
				spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid) {
			if (!spec->stream_digital_capture)
				spec->stream_digital_capture =
					&via_pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				*spec->stream_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->dig_in_nid;
		}
		codec->num_pcms++;
		info++;
	}

	if (spec->hp_dac_nid) {
		snprintf(spec->stream_name_hp, sizeof(spec->stream_name_hp),
			 "%s HP", codec->chip_name);
		info->name = spec->stream_name_hp;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] = via_pcm_hp_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
			spec->hp_dac_nid;
		codec->num_pcms++;
		info++;
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
	kfree(spec->bind_cap_vol);
	kfree(spec->bind_cap_sw);
	snd_hda_gen_free(&spec->gen);
	kfree(spec);
}

/* mute/unmute outputs */
static void toggle_output_mutes(struct hda_codec *codec, int num_pins,
				hda_nid_t *pins, bool mute)
{
	int i;
	for (i = 0; i < num_pins; i++) {
		unsigned int parm = snd_hda_codec_read(codec, pins[i], 0,
					  AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		if (parm & AC_PINCTL_IN_EN)
			continue;
		if (mute)
			parm &= ~AC_PINCTL_OUT_EN;
		else
			parm |= AC_PINCTL_OUT_EN;
		snd_hda_set_pin_ctl(codec, pins[i], parm);
	}
}

/* mute internal speaker if line-out is plugged */
static void via_line_automute(struct hda_codec *codec, int present)
{
	struct via_spec *spec = codec->spec;

	if (!spec->autocfg.speaker_outs)
		return;
	if (!present)
		present = snd_hda_jack_detect(codec,
					      spec->autocfg.line_out_pins[0]);
	toggle_output_mutes(codec, spec->autocfg.speaker_outs,
			    spec->autocfg.speaker_pins,
			    present);
}

/* mute internal speaker if HP is plugged */
static void via_hp_automute(struct hda_codec *codec)
{
	int present = 0;
	int nums;
	struct via_spec *spec = codec->spec;

	if (!spec->hp_independent_mode && spec->autocfg.hp_pins[0] &&
	    (spec->codec_type != VT1708 || spec->vt1708_jack_detect) &&
	    is_jack_detectable(codec, spec->autocfg.hp_pins[0]))
		present = snd_hda_jack_detect(codec, spec->autocfg.hp_pins[0]);

	if (spec->smart51_enabled)
		nums = spec->autocfg.line_outs + spec->smart51_nums;
	else
		nums = spec->autocfg.line_outs;
	toggle_output_mutes(codec, nums, spec->autocfg.line_out_pins, present);

	via_line_automute(codec, present);
}

#ifdef CONFIG_PM
static int via_suspend(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	vt1708_stop_hp_work(spec);

	if (spec->codec_type == VT1802) {
		/* Fix pop noise on headphones */
		int i;
		for (i = 0; i < spec->autocfg.hp_outs; i++)
			snd_hda_set_pin_ctl(codec, spec->autocfg.hp_pins[i], 0);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM
static int via_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct via_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
}
#endif

/*
 */

static int via_init(struct hda_codec *codec);

static const struct hda_codec_ops via_patch_ops = {
	.build_controls = via_build_controls,
	.build_pcms = via_build_pcms,
	.init = via_init,
	.free = via_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = via_suspend,
	.check_power_status = via_check_power_status,
#endif
};

static bool is_empty_dac(struct hda_codec *codec, hda_nid_t dac)
{
	struct via_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->multiout.num_dacs; i++) {
		if (spec->multiout.dac_nids[i] == dac)
			return false;
	}
	if (spec->hp_dac_nid == dac)
		return false;
	return true;
}

static bool __parse_output_path(struct hda_codec *codec, hda_nid_t nid,
				hda_nid_t target_dac, int with_aa_mix,
				struct nid_path *path, int depth)
{
	struct via_spec *spec = codec->spec;
	hda_nid_t conn[8];
	int i, nums;

	if (nid == spec->aa_mix_nid) {
		if (!with_aa_mix)
			return false;
		with_aa_mix = 2; /* mark aa-mix is included */
	}

	nums = snd_hda_get_connections(codec, nid, conn, ARRAY_SIZE(conn));
	for (i = 0; i < nums; i++) {
		if (get_wcaps_type(get_wcaps(codec, conn[i])) != AC_WID_AUD_OUT)
			continue;
		if (conn[i] == target_dac || is_empty_dac(codec, conn[i])) {
			/* aa-mix is requested but not included? */
			if (!(spec->aa_mix_nid && with_aa_mix == 1))
				goto found;
		}
	}
	if (depth >= MAX_NID_PATH_DEPTH)
		return false;
	for (i = 0; i < nums; i++) {
		unsigned int type;
		type = get_wcaps_type(get_wcaps(codec, conn[i]));
		if (type == AC_WID_AUD_OUT)
			continue;
		if (__parse_output_path(codec, conn[i], target_dac,
					with_aa_mix, path, depth + 1))
			goto found;
	}
	return false;

 found:
	path->path[path->depth] = conn[i];
	path->idx[path->depth] = i;
	if (nums > 1 && get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_AUD_MIX)
		path->multi[path->depth] = 1;
	path->depth++;
	return true;
}

static bool parse_output_path(struct hda_codec *codec, hda_nid_t nid,
			      hda_nid_t target_dac, int with_aa_mix,
			      struct nid_path *path)
{
	if (__parse_output_path(codec, nid, target_dac, with_aa_mix, path, 1)) {
		path->path[path->depth] = nid;
		path->depth++;
		snd_printdd("output-path: depth=%d, %02x/%02x/%02x/%02x/%02x\n",
			    path->depth, path->path[0], path->path[1],
			    path->path[2], path->path[3], path->path[4]);
		return true;
	}
	return false;
}

static int via_auto_fill_dac_nids(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;
	hda_nid_t nid;

	spec->multiout.num_dacs = 0;
	spec->multiout.dac_nids = spec->private_dac_nids;
	for (i = 0; i < cfg->line_outs; i++) {
		hda_nid_t dac = 0;
		nid = cfg->line_out_pins[i];
		if (!nid)
			continue;
		if (parse_output_path(codec, nid, 0, 0, &spec->out_path[i]))
			dac = spec->out_path[i].path[0];
		if (!i && parse_output_path(codec, nid, dac, 1,
					    &spec->out_mix_path))
			dac = spec->out_mix_path.path[0];
		if (dac)
			spec->private_dac_nids[spec->multiout.num_dacs++] = dac;
	}
	if (!spec->out_path[0].depth && spec->out_mix_path.depth) {
		spec->out_path[0] = spec->out_mix_path;
		spec->out_mix_path.depth = 0;
	}
	return 0;
}

static int create_ch_ctls(struct hda_codec *codec, const char *pfx,
			  int chs, bool check_dac, struct nid_path *path)
{
	struct via_spec *spec = codec->spec;
	char name[32];
	hda_nid_t dac, pin, sel, nid;
	int err;

	dac = check_dac ? path->path[0] : 0;
	pin = path->path[path->depth - 1];
	sel = path->depth > 1 ? path->path[1] : 0;

	if (dac && check_amp_caps(codec, dac, HDA_OUTPUT, AC_AMPCAP_NUM_STEPS))
		nid = dac;
	else if (check_amp_caps(codec, pin, HDA_OUTPUT, AC_AMPCAP_NUM_STEPS))
		nid = pin;
	else if (check_amp_caps(codec, sel, HDA_OUTPUT, AC_AMPCAP_NUM_STEPS))
		nid = sel;
	else
		nid = 0;
	if (nid) {
		sprintf(name, "%s Playback Volume", pfx);
		err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
			      HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		path->vol_ctl = nid;
	}

	if (dac && check_amp_caps(codec, dac, HDA_OUTPUT, AC_AMPCAP_MUTE))
		nid = dac;
	else if (check_amp_caps(codec, pin, HDA_OUTPUT, AC_AMPCAP_MUTE))
		nid = pin;
	else if (check_amp_caps(codec, sel, HDA_OUTPUT, AC_AMPCAP_MUTE))
		nid = sel;
	else
		nid = 0;
	if (nid) {
		sprintf(name, "%s Playback Switch", pfx);
		err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
			      HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		path->mute_ctl = nid;
	}
	return 0;
}

static void mangle_smart51(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct auto_pin_cfg_item *ins = cfg->inputs;
	int i, j, nums, attr;
	int pins[AUTO_CFG_MAX_INS];

	for (attr = INPUT_PIN_ATTR_REAR; attr >= INPUT_PIN_ATTR_NORMAL; attr--) {
		nums = 0;
		for (i = 0; i < cfg->num_inputs; i++) {
			unsigned int def;
			if (ins[i].type > AUTO_PIN_LINE_IN)
				continue;
			def = snd_hda_codec_get_pincfg(codec, ins[i].pin);
			if (snd_hda_get_input_pin_attr(def) != attr)
				continue;
			for (j = 0; j < nums; j++)
				if (ins[pins[j]].type < ins[i].type) {
					memmove(pins + j + 1, pins + j,
						(nums - j) * sizeof(int));
					break;
				}
			pins[j] = i;
			nums++;
		}
		if (cfg->line_outs + nums < 3)
			continue;
		for (i = 0; i < nums; i++) {
			hda_nid_t pin = ins[pins[i]].pin;
			spec->smart51_pins[spec->smart51_nums++] = pin;
			cfg->line_out_pins[cfg->line_outs++] = pin;
			if (cfg->line_outs == 3)
				break;
		}
		return;
	}
}

static void copy_path_mixer_ctls(struct nid_path *dst, struct nid_path *src)
{
	dst->vol_ctl = src->vol_ctl;
	dst->mute_ctl = src->mute_ctl;
}

/* add playback controls from the parsed DAC table */
static int via_auto_create_multi_out_ctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct nid_path *path;
	static const char * const chname[4] = {
		"Front", "Surround", NULL /* "CLFE" */, "Side"
	};
	int i, idx, err;
	int old_line_outs;

	/* check smart51 */
	old_line_outs = cfg->line_outs;
	if (cfg->line_outs == 1)
		mangle_smart51(codec);

	err = via_auto_fill_dac_nids(codec);
	if (err < 0)
		return err;

	if (spec->multiout.num_dacs < 3) {
		spec->smart51_nums = 0;
		cfg->line_outs = old_line_outs;
	}
	for (i = 0; i < cfg->line_outs; i++) {
		hda_nid_t pin, dac;
		pin = cfg->line_out_pins[i];
		dac = spec->multiout.dac_nids[i];
		if (!pin || !dac)
			continue;
		path = spec->out_path + i;
		if (i == HDA_CLFE) {
			err = create_ch_ctls(codec, "Center", 1, true, path);
			if (err < 0)
				return err;
			err = create_ch_ctls(codec, "LFE", 2, true, path);
			if (err < 0)
				return err;
		} else {
			const char *pfx = chname[i];
			if (cfg->line_out_type == AUTO_PIN_SPEAKER_OUT &&
			    cfg->line_outs <= 2)
				pfx = i ? "Bass Speaker" : "Speaker";
			err = create_ch_ctls(codec, pfx, 3, true, path);
			if (err < 0)
				return err;
		}
		if (path != spec->out_path + i)
			copy_path_mixer_ctls(&spec->out_path[i], path);
		if (path == spec->out_path && spec->out_mix_path.depth)
			copy_path_mixer_ctls(&spec->out_mix_path, path);
	}

	idx = get_connection_index(codec, spec->aa_mix_nid,
				   spec->multiout.dac_nids[0]);
	if (idx >= 0) {
		/* add control to mixer */
		const char *name;
		name = spec->out_mix_path.depth ?
			"PCM Loopback Playback Volume" : "PCM Playback Volume";
		err = via_add_control(spec, VIA_CTL_WIDGET_VOL, name,
				      HDA_COMPOSE_AMP_VAL(spec->aa_mix_nid, 3,
							  idx, HDA_INPUT));
		if (err < 0)
			return err;
		name = spec->out_mix_path.depth ?
			"PCM Loopback Playback Switch" : "PCM Playback Switch";
		err = via_add_control(spec, VIA_CTL_WIDGET_MUTE, name,
				      HDA_COMPOSE_AMP_VAL(spec->aa_mix_nid, 3,
							  idx, HDA_INPUT));
		if (err < 0)
			return err;
	}

	cfg->line_outs = old_line_outs;

	return 0;
}

static int via_auto_create_hp_ctls(struct hda_codec *codec, hda_nid_t pin)
{
	struct via_spec *spec = codec->spec;
	struct nid_path *path;
	bool check_dac;
	int i, err;

	if (!pin)
		return 0;

	if (!parse_output_path(codec, pin, 0, 0, &spec->hp_indep_path)) {
		for (i = HDA_SIDE; i >= HDA_CLFE; i--) {
			if (i < spec->multiout.num_dacs &&
			    parse_output_path(codec, pin,
					      spec->multiout.dac_nids[i], 0,
					      &spec->hp_indep_path)) {
				spec->hp_indep_shared = i;
				break;
			}
		}
	}
	if (spec->hp_indep_path.depth) {
		spec->hp_dac_nid = spec->hp_indep_path.path[0];
		if (!spec->hp_indep_shared)
			spec->hp_path = spec->hp_indep_path;
	}
	/* optionally check front-path w/o AA-mix */
	if (!spec->hp_path.depth)
		parse_output_path(codec, pin,
				  spec->multiout.dac_nids[HDA_FRONT], 0,
				  &spec->hp_path);

	if (!parse_output_path(codec, pin, spec->multiout.dac_nids[HDA_FRONT],
			       1, &spec->hp_mix_path) && !spec->hp_path.depth)
		return 0;

	if (spec->hp_path.depth) {
		path = &spec->hp_path;
		check_dac = true;
	} else {
		path = &spec->hp_mix_path;
		check_dac = false;
	}
	err = create_ch_ctls(codec, "Headphone", 3, check_dac, path);
	if (err < 0)
		return err;
	if (check_dac)
		copy_path_mixer_ctls(&spec->hp_mix_path, path);
	else
		copy_path_mixer_ctls(&spec->hp_path, path);
	if (spec->hp_indep_path.depth)
		copy_path_mixer_ctls(&spec->hp_indep_path, path);
	return 0;
}

static int via_auto_create_speaker_ctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct nid_path *path;
	bool check_dac;
	hda_nid_t pin, dac = 0;
	int err;

	pin = spec->autocfg.speaker_pins[0];
	if (!spec->autocfg.speaker_outs || !pin)
		return 0;

	if (parse_output_path(codec, pin, 0, 0, &spec->speaker_path))
		dac = spec->speaker_path.path[0];
	if (!dac)
		parse_output_path(codec, pin,
				  spec->multiout.dac_nids[HDA_FRONT], 0,
				  &spec->speaker_path);
	if (!parse_output_path(codec, pin, spec->multiout.dac_nids[HDA_FRONT],
			       1, &spec->speaker_mix_path) && !dac)
		return 0;

	/* no AA-path for front? */
	if (!spec->out_mix_path.depth && spec->speaker_mix_path.depth)
		dac = 0;

	spec->speaker_dac_nid = dac;
	spec->multiout.extra_out_nid[0] = dac;
	if (dac) {
		path = &spec->speaker_path;
		check_dac = true;
	} else {
		path = &spec->speaker_mix_path;
		check_dac = false;
	}
	err = create_ch_ctls(codec, "Speaker", 3, check_dac, path);
	if (err < 0)
		return err;
	if (check_dac)
		copy_path_mixer_ctls(&spec->speaker_mix_path, path);
	else
		copy_path_mixer_ctls(&spec->speaker_path, path);
	return 0;
}

#define via_aamix_ctl_info	via_pin_power_ctl_info

static int via_aamix_ctl_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = spec->aamix_mode;
	return 0;
}

static void update_aamix_paths(struct hda_codec *codec, int do_mix,
			       struct nid_path *nomix, struct nid_path *mix)
{
	if (do_mix) {
		activate_output_path(codec, nomix, false, false);
		activate_output_path(codec, mix, true, false);
	} else {
		activate_output_path(codec, mix, false, false);
		activate_output_path(codec, nomix, true, false);
	}
}

static int via_aamix_ctl_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	unsigned int val = ucontrol->value.enumerated.item[0];

	if (val == spec->aamix_mode)
		return 0;
	spec->aamix_mode = val;
	/* update front path */
	update_aamix_paths(codec, val, &spec->out_path[0], &spec->out_mix_path);
	/* update HP path */
	if (!spec->hp_independent_mode) {
		update_aamix_paths(codec, val, &spec->hp_path,
				   &spec->hp_mix_path);
	}
	/* update speaker path */
	update_aamix_paths(codec, val, &spec->speaker_path,
			   &spec->speaker_mix_path);
	return 1;
}

static const struct snd_kcontrol_new via_aamix_ctl_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Loopback Mixing",
	.info = via_aamix_ctl_info,
	.get = via_aamix_ctl_get,
	.put = via_aamix_ctl_put,
};

static int via_auto_create_loopback_switch(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;

	if (!spec->aa_mix_nid)
		return 0; /* no loopback switching available */
	if (!(spec->out_mix_path.depth || spec->hp_mix_path.depth ||
	      spec->speaker_path.depth))
		return 0; /* no loopback switching available */
	if (!via_clone_control(spec, &via_aamix_ctl_enum))
		return -ENOMEM;
	return 0;
}

/* look for ADCs */
static int via_fill_adcs(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	hda_nid_t nid = codec->start_nid;
	int i;

	for (i = 0; i < codec->num_nodes; i++, nid++) {
		unsigned int wcaps = get_wcaps(codec, nid);
		if (get_wcaps_type(wcaps) != AC_WID_AUD_IN)
			continue;
		if (wcaps & AC_WCAP_DIGITAL)
			continue;
		if (!(wcaps & AC_WCAP_CONN_LIST))
			continue;
		if (spec->num_adc_nids >= ARRAY_SIZE(spec->adc_nids))
			return -ENOMEM;
		spec->adc_nids[spec->num_adc_nids++] = nid;
	}
	return 0;
}

/* input-src control */
static int via_mux_enum_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->num_inputs;
	if (uinfo->value.enumerated.item >= spec->num_inputs)
		uinfo->value.enumerated.item = spec->num_inputs - 1;
	strcpy(uinfo->value.enumerated.name,
	       spec->inputs[uinfo->value.enumerated.item].label);
	return 0;
}

static int via_mux_enum_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[idx];
	return 0;
}

static int via_mux_enum_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct via_spec *spec = codec->spec;
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	hda_nid_t mux;
	int cur;

	cur = ucontrol->value.enumerated.item[0];
	if (cur < 0 || cur >= spec->num_inputs)
		return -EINVAL;
	if (spec->cur_mux[idx] == cur)
		return 0;
	spec->cur_mux[idx] = cur;
	if (spec->dyn_adc_switch) {
		int adc_idx = spec->inputs[cur].adc_idx;
		mux = spec->mux_nids[adc_idx];
		via_dyn_adc_pcm_resetup(codec, cur);
	} else {
		mux = spec->mux_nids[idx];
		if (snd_BUG_ON(!mux))
			return -EINVAL;
	}

	if (mux) {
		/* switch to D0 beofre change index */
		update_power_state(codec, mux, AC_PWRST_D0);
		snd_hda_codec_write(codec, mux, 0,
				    AC_VERB_SET_CONNECT_SEL,
				    spec->inputs[cur].mux_idx);
	}

	/* update jack power state */
	set_widgets_power_state(codec);
	return 0;
}

static const struct snd_kcontrol_new via_input_src_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	/* The multiple "Capture Source" controls confuse alsamixer
	 * So call somewhat different..
	 */
	/* .name = "Capture Source", */
	.name = "Input Source",
	.info = via_mux_enum_info,
	.get = via_mux_enum_get,
	.put = via_mux_enum_put,
};

static int create_input_src_ctls(struct hda_codec *codec, int count)
{
	struct via_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;

	if (spec->num_inputs <= 1 || !count)
		return 0; /* no need for single src */

	knew = via_clone_control(spec, &via_input_src_ctl);
	if (!knew)
		return -ENOMEM;
	knew->count = count;
	return 0;
}

/* add the powersave loopback-list entry */
static void add_loopback_list(struct via_spec *spec, hda_nid_t mix, int idx)
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

static bool is_reachable_nid(struct hda_codec *codec, hda_nid_t src,
			     hda_nid_t dst)
{
	return snd_hda_get_conn_index(codec, src, dst, 1) >= 0;
}

/* add the input-route to the given pin */
static bool add_input_route(struct hda_codec *codec, hda_nid_t pin)
{
	struct via_spec *spec = codec->spec;
	int c, idx;

	spec->inputs[spec->num_inputs].adc_idx = -1;
	spec->inputs[spec->num_inputs].pin = pin;
	for (c = 0; c < spec->num_adc_nids; c++) {
		if (spec->mux_nids[c]) {
			idx = get_connection_index(codec, spec->mux_nids[c],
						   pin);
			if (idx < 0)
				continue;
			spec->inputs[spec->num_inputs].mux_idx = idx;
		} else {
			if (!is_reachable_nid(codec, spec->adc_nids[c], pin))
				continue;
		}
		spec->inputs[spec->num_inputs].adc_idx = c;
		/* Can primary ADC satisfy all inputs? */
		if (!spec->dyn_adc_switch &&
		    spec->num_inputs > 0 && spec->inputs[0].adc_idx != c) {
			snd_printd(KERN_INFO
				   "via: dynamic ADC switching enabled\n");
			spec->dyn_adc_switch = 1;
		}
		return true;
	}
	return false;
}

static int get_mux_nids(struct hda_codec *codec);

/* parse input-routes; fill ADCs, MUXs and input-src entries */
static int parse_analog_inputs(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, err;

	err = via_fill_adcs(codec);
	if (err < 0)
		return err;
	err = get_mux_nids(codec);
	if (err < 0)
		return err;

	/* fill all input-routes */
	for (i = 0; i < cfg->num_inputs; i++) {
		if (add_input_route(codec, cfg->inputs[i].pin))
			spec->inputs[spec->num_inputs++].label =
				hda_get_autocfg_input_label(codec, cfg, i);
	}

	/* check for internal loopback recording */
	if (spec->aa_mix_nid &&
	    add_input_route(codec, spec->aa_mix_nid))
		spec->inputs[spec->num_inputs++].label = "Stereo Mixer";

	return 0;
}

/* create analog-loopback volume/switch controls */
static int create_loopback_ctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	const char *prev_label = NULL;
	int type_idx = 0;
	int i, j, err, idx;

	if (!spec->aa_mix_nid)
		return 0;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin = cfg->inputs[i].pin;
		const char *label = hda_get_autocfg_input_label(codec, cfg, i);

		if (prev_label && !strcmp(label, prev_label))
			type_idx++;
		else
			type_idx = 0;
		prev_label = label;
		idx = get_connection_index(codec, spec->aa_mix_nid, pin);
		if (idx >= 0) {
			err = via_new_analog_input(spec, label, type_idx,
						   idx, spec->aa_mix_nid);
			if (err < 0)
				return err;
			add_loopback_list(spec, spec->aa_mix_nid, idx);
		}

		/* remember the label for smart51 control */
		for (j = 0; j < spec->smart51_nums; j++) {
			if (spec->smart51_pins[j] == pin) {
				spec->smart51_idxs[j] = idx;
				spec->smart51_labels[j] = label;
				break;
			}
		}
	}
	return 0;
}

/* create mic-boost controls (if present) */
static int create_mic_boost_ctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	const char *prev_label = NULL;
	int type_idx = 0;
	int i, err;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin = cfg->inputs[i].pin;
		unsigned int caps;
		const char *label;
		char name[32];

		if (cfg->inputs[i].type != AUTO_PIN_MIC)
			continue;
		caps = query_amp_caps(codec, pin, HDA_INPUT);
		if (caps == -1 || !(caps & AC_AMPCAP_NUM_STEPS))
			continue;
		label = hda_get_autocfg_input_label(codec, cfg, i);
		if (prev_label && !strcmp(label, prev_label))
			type_idx++;
		else
			type_idx = 0;
		prev_label = label;
		snprintf(name, sizeof(name), "%s Boost Volume", label);
		err = __via_add_control(spec, VIA_CTL_WIDGET_VOL, name, type_idx,
			      HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_INPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* create capture and input-src controls for multiple streams */
static int create_multi_adc_ctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int i, err;

	/* create capture mixer elements */
	for (i = 0; i < spec->num_adc_nids; i++) {
		hda_nid_t adc = spec->adc_nids[i];
		err = __via_add_control(spec, VIA_CTL_WIDGET_VOL,
					"Capture Volume", i,
					HDA_COMPOSE_AMP_VAL(adc, 3, 0,
							    HDA_INPUT));
		if (err < 0)
			return err;
		err = __via_add_control(spec, VIA_CTL_WIDGET_MUTE,
					"Capture Switch", i,
					HDA_COMPOSE_AMP_VAL(adc, 3, 0,
							    HDA_INPUT));
		if (err < 0)
			return err;
	}

	/* input-source control */
	for (i = 0; i < spec->num_adc_nids; i++)
		if (!spec->mux_nids[i])
			break;
	err = create_input_src_ctls(codec, i);
	if (err < 0)
		return err;
	return 0;
}

/* bind capture volume/switch */
static struct snd_kcontrol_new via_bind_cap_vol_ctl =
	HDA_BIND_VOL("Capture Volume", 0);
static struct snd_kcontrol_new via_bind_cap_sw_ctl =
	HDA_BIND_SW("Capture Switch", 0);

static int init_bind_ctl(struct via_spec *spec, struct hda_bind_ctls **ctl_ret,
			 struct hda_ctl_ops *ops)
{
	struct hda_bind_ctls *ctl;
	int i;

	ctl = kzalloc(sizeof(*ctl) + sizeof(long) * 4, GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;
	ctl->ops = ops;
	for (i = 0; i < spec->num_adc_nids; i++)
		ctl->values[i] =
			HDA_COMPOSE_AMP_VAL(spec->adc_nids[i], 3, 0, HDA_INPUT);
	*ctl_ret = ctl;
	return 0;
}

/* create capture and input-src controls for dynamic ADC-switch case */
static int create_dyn_adc_ctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;
	int err;

	/* set up the bind capture ctls */
	err = init_bind_ctl(spec, &spec->bind_cap_vol, &snd_hda_bind_vol);
	if (err < 0)
		return err;
	err = init_bind_ctl(spec, &spec->bind_cap_sw, &snd_hda_bind_sw);
	if (err < 0)
		return err;

	/* create capture mixer elements */
	knew = via_clone_control(spec, &via_bind_cap_vol_ctl);
	if (!knew)
		return -ENOMEM;
	knew->private_value = (long)spec->bind_cap_vol;

	knew = via_clone_control(spec, &via_bind_cap_sw_ctl);
	if (!knew)
		return -ENOMEM;
	knew->private_value = (long)spec->bind_cap_sw;

	/* input-source control */
	err = create_input_src_ctls(codec, 1);
	if (err < 0)
		return err;
	return 0;
}

/* parse and create capture-related stuff */
static int via_auto_create_analog_input_ctls(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = parse_analog_inputs(codec);
	if (err < 0)
		return err;
	if (spec->dyn_adc_switch)
		err = create_dyn_adc_ctls(codec);
	else
		err = create_multi_adc_ctls(codec);
	if (err < 0)
		return err;
	err = create_loopback_ctls(codec);
	if (err < 0)
		return err;
	err = create_mic_boost_ctls(codec);
	if (err < 0)
		return err;
	return 0;
}

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
	if (spec->vt1708_jack_detect &&
	    snd_hda_get_bool_hint(codec, "analog_loopback_hp_detect") != 1) {
		mute_aa_path(codec, 1);
		notify_aa_path_ctls(codec);
	}
	via_hp_automute(codec);
	vt1708_update_hp_work(spec);
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

static void fill_dig_outs(struct hda_codec *codec);
static void fill_dig_in(struct hda_codec *codec);

static int via_parse_auto_config(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs && !spec->autocfg.hp_pins[0])
		return -EINVAL;

	err = via_auto_create_multi_out_ctls(codec);
	if (err < 0)
		return err;
	err = via_auto_create_hp_ctls(codec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = via_auto_create_speaker_ctls(codec);
	if (err < 0)
		return err;
	err = via_auto_create_loopback_switch(codec);
	if (err < 0)
		return err;
	err = via_auto_create_analog_input_ctls(codec);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	fill_dig_outs(codec);
	fill_dig_in(codec);

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;


	if (spec->hp_dac_nid && spec->hp_mix_path.depth) {
		err = via_hp_build(codec);
		if (err < 0)
			return err;
	}

	err = via_smart51_build(codec);
	if (err < 0)
		return err;

	/* assign slave outs */
	if (spec->slave_dig_outs[0])
		codec->slave_dig_outs = spec->slave_dig_outs;

	return 1;
}

static void via_auto_init_dig_outs(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	if (spec->multiout.dig_out_nid)
		init_output_pin(codec, spec->autocfg.dig_out_pins[0], PIN_OUT);
	if (spec->slave_dig_outs[0])
		init_output_pin(codec, spec->autocfg.dig_out_pins[1], PIN_OUT);
}

static void via_auto_init_dig_in(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	if (!spec->dig_in_nid)
		return;
	snd_hda_set_pin_ctl(codec, spec->autocfg.dig_in_pin, PIN_IN);
}

static void via_jack_output_event(struct hda_codec *codec, struct hda_jack_tbl *tbl)
{
	set_widgets_power_state(codec);
	via_hp_automute(codec);
}

static void via_jack_powerstate_event(struct hda_codec *codec, struct hda_jack_tbl *tbl)
{
	set_widgets_power_state(codec);
}

/* initialize the unsolicited events */
static void via_auto_init_unsol_event(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int ev;
	int i;
	hda_jack_callback cb;

	if (cfg->hp_pins[0] && is_jack_detectable(codec, cfg->hp_pins[0]))
		snd_hda_jack_detect_enable_callback(codec, cfg->hp_pins[0],
						    VIA_HP_EVENT | VIA_JACK_EVENT,
						    via_jack_output_event);

	if (cfg->speaker_pins[0])
		ev = VIA_LINE_EVENT;
	else
		ev = 0;
	cb = ev ? via_jack_output_event : via_jack_powerstate_event;

	for (i = 0; i < cfg->line_outs; i++) {
		if (cfg->line_out_pins[i] &&
		    is_jack_detectable(codec, cfg->line_out_pins[i]))
			snd_hda_jack_detect_enable_callback(codec, cfg->line_out_pins[i],
							    ev | VIA_JACK_EVENT, cb);
	}

	for (i = 0; i < cfg->num_inputs; i++) {
		if (is_jack_detectable(codec, cfg->inputs[i].pin))
			snd_hda_jack_detect_enable_callback(codec, cfg->inputs[i].pin,
							    VIA_JACK_EVENT,
							    via_jack_powerstate_event);
	}
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

	via_auto_init_multi_out(codec);
	via_auto_init_hp_out(codec);
	via_auto_init_speaker_out(codec);
	via_auto_init_analog_input(codec);
	via_auto_init_dig_outs(codec);
	via_auto_init_dig_in(codec);

	via_auto_init_unsol_event(codec);

	via_hp_automute(codec);
	vt1708_update_hp_work(spec);

	return 0;
}

static void vt1708_update_hp_jack_state(struct work_struct *work)
{
	struct via_spec *spec = container_of(work, struct via_spec,
					     vt1708_hp_work.work);
	if (spec->codec_type != VT1708)
		return;
	snd_hda_jack_set_dirty_all(spec->codec);
	/* if jack state toggled */
	if (spec->vt1708_hp_present
	    != snd_hda_jack_detect(spec->codec, spec->autocfg.hp_pins[0])) {
		spec->vt1708_hp_present ^= 1;
		via_hp_automute(spec->codec);
	}
	if (spec->vt1708_jack_detect)
		schedule_delayed_work(&spec->vt1708_hp_work,
				      msecs_to_jiffies(100));
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
	spec = via_new_spec(codec);
	if (spec == NULL)
		return -ENOMEM;

	spec->aa_mix_nid = 0x17;

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
	if (!via_clone_control(spec, &vt1708_jack_detect_ctl))
		return -ENOMEM;

	/* disable 32bit format on VT1708 */
	if (codec->vendor_id == 0x11061708)
		spec->stream_analog_playback = &vt1708_pcm_analog_s16_playback;

	spec->init_verbs[spec->num_iverbs++] = vt1708_init_verbs;

	codec->patch_ops = via_patch_ops;

	INIT_DELAYED_WORK(&spec->vt1708_hp_work, vt1708_update_hp_jack_state);
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

	spec->aa_mix_nid = 0x18;

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
	if (spec->smart51_enabled)
		set_pin_power_state(codec, 0x1b, &parm);
	update_power_state(codec, 0x18, parm);
	update_power_state(codec, 0x11, parm);

	/* PW6 (22h), SW2 (26h), AOW2 (24h) */
	if (is_8ch) {
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x22, &parm);
		if (spec->smart51_enabled)
			set_pin_power_state(codec, 0x1a, &parm);
		update_power_state(codec, 0x26, parm);
		update_power_state(codec, 0x24, parm);
	} else if (codec->vendor_id == 0x11064397) {
		/* PW7(23h), SW2(27h), AOW2(25h) */
		parm = AC_PWRST_D3;
		set_pin_power_state(codec, 0x23, &parm);
		if (spec->smart51_enabled)
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
	} else if (codec->vendor_id == 0x11064397 && spec->hp_independent_mode)
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

	spec->aa_mix_nid = 0x16;

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

static void fill_dig_in(struct hda_codec *codec)
{
	struct via_spec *spec = codec->spec;
	hda_nid_t dig_nid;
	int i, err;

	if (!spec->autocfg.dig_in_pin)
		return;

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

	spec->aa_mix_nid = 0x16;
	override_mic_boost(codec, 0x1a, 0, 3, 40);
	override_mic_boost(codec, 0x1e, 0, 3, 40);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++] = vt1708S_init_verbs;

	codec->patch_ops = via_patch_ops;

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

	spec->aa_mix_nid = 0x1a;

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
	if (spec->smart51_enabled)
		set_pin_power_state(codec, 0x2b, &parm);
	update_power_state(codec, 0xa, parm);

	/* PW0 (24h), AOW0 (8h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x24, &parm);
	if (!spec->hp_independent_mode) /* check for redirected HP */
		set_pin_power_state(codec, 0x28, &parm);
	update_power_state(codec, 0x8, parm);
	if (!spec->hp_independent_mode && parm2 != AC_PWRST_D3)
		parm = parm2;
	update_power_state(codec, 0xb, parm);
	/* MW9 (21h), Mw2 (1ah), AOW0 (8h) */
	update_power_state(codec, 0x21, imux_is_smixer ? AC_PWRST_D0 : parm);

	/* PW1 (25h), AOW1 (9h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x25, &parm);
	if (spec->smart51_enabled)
		set_pin_power_state(codec, 0x2a, &parm);
	update_power_state(codec, 0x9, parm);

	if (spec->hp_independent_mode) {
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

	if (!spec->aa_mix_nid)
		return 0;
	nums = snd_hda_get_connections(codec, spec->aa_mix_nid, conn,
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
							  spec->aa_mix_nid,
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

	spec->aa_mix_nid = 0x21;
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
	if (spec->smart51_enabled)
		set_pin_power_state(codec, 0x1b, &parm);
	update_power_state(codec, 0x18, parm);
	update_power_state(codec, 0x11, parm);

	/* PW7 (23h), SW3 (27h), AOW3 (25h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x23, &parm);
	/* Smart 5.1 PW1(1ah) */
	if (spec->smart51_enabled)
		set_pin_power_state(codec, 0x1a, &parm);
	update_power_state(codec, 0x27, parm);

	/* Smart 5.1 PW5(1eh) */
	if (spec->smart51_enabled)
		set_pin_power_state(codec, 0x1e, &parm);
	update_power_state(codec, 0x25, parm);

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
	update_power_state(codec, 0x28, parm);
	update_power_state(codec, 0x29, parm);
	update_power_state(codec, 0x2a, parm);

	/* PW 3/4 (1ch/1dh) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x1c, &parm);
	set_pin_power_state(codec, 0x1d, &parm);
	/* HP Independent Mode, power on AOW3 */
	if (spec->hp_independent_mode)
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

	spec->aa_mix_nid = 0x16;
	override_mic_boost(codec, 0x1a, 0, 3, 40);
	override_mic_boost(codec, 0x1e, 0, 3, 40);

	/* automatic parse from the BIOS config */
	err = via_parse_auto_config(codec);
	if (err < 0) {
		via_free(codec);
		return err;
	}

	spec->init_verbs[spec->num_iverbs++]  = vt1716S_init_verbs;

	spec->mixers[spec->num_mixers] = vt1716s_dmic_mixer;
	spec->num_mixers++;

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

	if (spec->hp_independent_mode)
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

	spec->aa_mix_nid = 0x21;
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
	if (spec->hp_independent_mode)
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

	spec->aa_mix_nid = 0x21;
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
	if (spec->smart51_enabled) {
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
	if (spec->smart51_enabled) {
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
	if (spec->hp_independent_mode)
		update_conv_power_state(codec, 0xb, parm, 3);
	parm2 = parm; /* for pin 0x0b */

	/* PW0 (24h), MW0(34h), MW9(3fh), AOW0 (8h) */
	parm = AC_PWRST_D3;
	set_pin_power_state(codec, 0x24, &parm);
	update_power_state(codec, 0x34, parm);
	if (!spec->hp_independent_mode && parm2 != AC_PWRST_D3)
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

	spec->aa_mix_nid = 0x3f;
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
