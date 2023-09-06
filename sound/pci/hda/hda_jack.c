// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Jack-detection handling for HD-audio
 *
 * Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/jack.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"

/**
 * is_jack_detectable - Check whether the given pin is jack-detectable
 * @codec: the HDA codec
 * @nid: pin NID
 *
 * Check whether the given pin is capable to report the jack detection.
 * The jack detection might not work by various reasons, e.g. the jack
 * detection is prohibited in the codec level, the pin config has
 * AC_DEFCFG_MISC_NO_PRESENCE bit, no unsol support, etc.
 */
bool is_jack_detectable(struct hda_codec *codec, hda_nid_t nid)
{
	if (codec->no_jack_detect)
		return false;
	if (!(snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_PRES_DETECT))
		return false;
	if (get_defcfg_misc(snd_hda_codec_get_pincfg(codec, nid)) &
	     AC_DEFCFG_MISC_NO_PRESENCE)
		return false;
	if (!(get_wcaps(codec, nid) & AC_WCAP_UNSOL_CAP) &&
	    !codec->jackpoll_interval)
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(is_jack_detectable);

/* execute pin sense measurement */
static u32 read_pin_sense(struct hda_codec *codec, hda_nid_t nid, int dev_id)
{
	u32 pincap;
	u32 val;

	if (!codec->no_trigger_sense) {
		pincap = snd_hda_query_pin_caps(codec, nid);
		if (pincap & AC_PINCAP_TRIG_REQ) /* need trigger? */
			snd_hda_codec_read(codec, nid, 0,
					AC_VERB_SET_PIN_SENSE, 0);
	}
	val = snd_hda_codec_read(codec, nid, 0,
				  AC_VERB_GET_PIN_SENSE, dev_id);
	if (codec->inv_jack_detect)
		val ^= AC_PINSENSE_PRESENCE;
	return val;
}

/**
 * snd_hda_jack_tbl_get_mst - query the jack-table entry for the given NID
 * @codec: the HDA codec
 * @nid: pin NID to refer to
 * @dev_id: pin device entry id
 */
struct hda_jack_tbl *
snd_hda_jack_tbl_get_mst(struct hda_codec *codec, hda_nid_t nid, int dev_id)
{
	struct hda_jack_tbl *jack = codec->jacktbl.list;
	int i;

	if (!nid || !jack)
		return NULL;
	for (i = 0; i < codec->jacktbl.used; i++, jack++)
		if (jack->nid == nid && jack->dev_id == dev_id)
			return jack;
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_tbl_get_mst);

/**
 * snd_hda_jack_tbl_get_from_tag - query the jack-table entry for the given tag
 * @codec: the HDA codec
 * @tag: tag value to refer to
 * @dev_id: pin device entry id
 */
struct hda_jack_tbl *
snd_hda_jack_tbl_get_from_tag(struct hda_codec *codec,
			      unsigned char tag, int dev_id)
{
	struct hda_jack_tbl *jack = codec->jacktbl.list;
	int i;

	if (!tag || !jack)
		return NULL;
	for (i = 0; i < codec->jacktbl.used; i++, jack++)
		if (jack->tag == tag && jack->dev_id == dev_id)
			return jack;
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_tbl_get_from_tag);

static struct hda_jack_tbl *
any_jack_tbl_get_from_nid(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_jack_tbl *jack = codec->jacktbl.list;
	int i;

	if (!nid || !jack)
		return NULL;
	for (i = 0; i < codec->jacktbl.used; i++, jack++)
		if (jack->nid == nid)
			return jack;
	return NULL;
}

/**
 * snd_hda_jack_tbl_new - create a jack-table entry for the given NID
 * @codec: the HDA codec
 * @nid: pin NID to assign
 * @dev_id: pin device entry id
 */
static struct hda_jack_tbl *
snd_hda_jack_tbl_new(struct hda_codec *codec, hda_nid_t nid, int dev_id)
{
	struct hda_jack_tbl *jack =
		snd_hda_jack_tbl_get_mst(codec, nid, dev_id);
	struct hda_jack_tbl *existing_nid_jack =
		any_jack_tbl_get_from_nid(codec, nid);

	WARN_ON(dev_id != 0 && !codec->dp_mst);

	if (jack)
		return jack;
	jack = snd_array_new(&codec->jacktbl);
	if (!jack)
		return NULL;
	jack->nid = nid;
	jack->dev_id = dev_id;
	jack->jack_dirty = 1;
	if (existing_nid_jack) {
		jack->tag = existing_nid_jack->tag;

		/*
		 * Copy jack_detect from existing_nid_jack to avoid
		 * snd_hda_jack_detect_enable_callback_mst() making multiple
		 * SET_UNSOLICITED_ENABLE calls on the same pin.
		 */
		jack->jack_detect = existing_nid_jack->jack_detect;
	} else {
		jack->tag = codec->jacktbl.used;
	}

	return jack;
}

void snd_hda_jack_tbl_disconnect(struct hda_codec *codec)
{
	struct hda_jack_tbl *jack = codec->jacktbl.list;
	int i;

	for (i = 0; i < codec->jacktbl.used; i++, jack++) {
		if (!codec->bus->shutdown && jack->jack)
			snd_device_disconnect(codec->card, jack->jack);
	}
}

void snd_hda_jack_tbl_clear(struct hda_codec *codec)
{
	struct hda_jack_tbl *jack = codec->jacktbl.list;
	int i;

	for (i = 0; i < codec->jacktbl.used; i++, jack++) {
		struct hda_jack_callback *cb, *next;

		/* free jack instances manually when clearing/reconfiguring */
		if (!codec->bus->shutdown && jack->jack)
			snd_device_free(codec->card, jack->jack);

		for (cb = jack->callback; cb; cb = next) {
			next = cb->next;
			kfree(cb);
		}
	}
	snd_array_free(&codec->jacktbl);
}

#define get_jack_plug_state(sense) !!(sense & AC_PINSENSE_PRESENCE)

/* update the cached value and notification flag if needed */
static void jack_detect_update(struct hda_codec *codec,
			       struct hda_jack_tbl *jack)
{
	if (!jack->jack_dirty)
		return;

	if (jack->phantom_jack)
		jack->pin_sense = AC_PINSENSE_PRESENCE;
	else
		jack->pin_sense = read_pin_sense(codec, jack->nid,
						 jack->dev_id);

	/* A gating jack indicates the jack is invalid if gating is unplugged */
	if (jack->gating_jack &&
	    !snd_hda_jack_detect_mst(codec, jack->gating_jack, jack->dev_id))
		jack->pin_sense &= ~AC_PINSENSE_PRESENCE;

	jack->jack_dirty = 0;

	/* If a jack is gated by this one update it. */
	if (jack->gated_jack) {
		struct hda_jack_tbl *gated =
			snd_hda_jack_tbl_get_mst(codec, jack->gated_jack,
						 jack->dev_id);
		if (gated) {
			gated->jack_dirty = 1;
			jack_detect_update(codec, gated);
		}
	}
}

/**
 * snd_hda_jack_set_dirty_all - Mark all the cached as dirty
 * @codec: the HDA codec
 *
 * This function sets the dirty flag to all entries of jack table.
 * It's called from the resume path in hda_codec.c.
 */
void snd_hda_jack_set_dirty_all(struct hda_codec *codec)
{
	struct hda_jack_tbl *jack = codec->jacktbl.list;
	int i;

	for (i = 0; i < codec->jacktbl.used; i++, jack++)
		if (jack->nid)
			jack->jack_dirty = 1;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_set_dirty_all);

/**
 * snd_hda_jack_pin_sense - execute pin sense measurement
 * @codec: the CODEC to sense
 * @nid: the pin NID to sense
 * @dev_id: pin device entry id
 *
 * Execute necessary pin sense measurement and return its Presence Detect,
 * Impedance, ELD Valid etc. status bits.
 */
u32 snd_hda_jack_pin_sense(struct hda_codec *codec, hda_nid_t nid, int dev_id)
{
	struct hda_jack_tbl *jack =
		snd_hda_jack_tbl_get_mst(codec, nid, dev_id);
	if (jack) {
		jack_detect_update(codec, jack);
		return jack->pin_sense;
	}
	return read_pin_sense(codec, nid, dev_id);
}
EXPORT_SYMBOL_GPL(snd_hda_jack_pin_sense);

/**
 * snd_hda_jack_detect_state_mst - query pin Presence Detect status
 * @codec: the CODEC to sense
 * @nid: the pin NID to sense
 * @dev_id: pin device entry id
 *
 * Query and return the pin's Presence Detect status, as either
 * HDA_JACK_NOT_PRESENT, HDA_JACK_PRESENT or HDA_JACK_PHANTOM.
 */
int snd_hda_jack_detect_state_mst(struct hda_codec *codec,
				  hda_nid_t nid, int dev_id)
{
	struct hda_jack_tbl *jack =
		snd_hda_jack_tbl_get_mst(codec, nid, dev_id);
	if (jack && jack->phantom_jack)
		return HDA_JACK_PHANTOM;
	else if (snd_hda_jack_pin_sense(codec, nid, dev_id) &
		 AC_PINSENSE_PRESENCE)
		return HDA_JACK_PRESENT;
	else
		return HDA_JACK_NOT_PRESENT;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_detect_state_mst);

static struct hda_jack_callback *
find_callback_from_list(struct hda_jack_tbl *jack,
			hda_jack_callback_fn func)
{
	struct hda_jack_callback *cb;

	if (!func)
		return NULL;

	for (cb = jack->callback; cb; cb = cb->next) {
		if (cb->func == func)
			return cb;
	}

	return NULL;
}

/**
 * snd_hda_jack_detect_enable_callback_mst - enable the jack-detection
 * @codec: the HDA codec
 * @nid: pin NID to enable
 * @func: callback function to register
 * @dev_id: pin device entry id
 *
 * In the case of error, the return value will be a pointer embedded with
 * errno.  Check and handle the return value appropriately with standard
 * macros such as @IS_ERR() and @PTR_ERR().
 */
struct hda_jack_callback *
snd_hda_jack_detect_enable_callback_mst(struct hda_codec *codec, hda_nid_t nid,
					int dev_id, hda_jack_callback_fn func)
{
	struct hda_jack_tbl *jack;
	struct hda_jack_callback *callback = NULL;
	int err;

	jack = snd_hda_jack_tbl_new(codec, nid, dev_id);
	if (!jack)
		return ERR_PTR(-ENOMEM);

	callback = find_callback_from_list(jack, func);

	if (func && !callback) {
		callback = kzalloc(sizeof(*callback), GFP_KERNEL);
		if (!callback)
			return ERR_PTR(-ENOMEM);
		callback->func = func;
		callback->nid = jack->nid;
		callback->dev_id = jack->dev_id;
		callback->next = jack->callback;
		jack->callback = callback;
	}

	if (jack->jack_detect)
		return callback; /* already registered */
	jack->jack_detect = 1;
	if (codec->jackpoll_interval > 0)
		return callback; /* No unsol if we're polling instead */
	err = snd_hda_codec_write_cache(codec, nid, 0,
					 AC_VERB_SET_UNSOLICITED_ENABLE,
					 AC_USRSP_EN | jack->tag);
	if (err < 0)
		return ERR_PTR(err);
	return callback;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_detect_enable_callback_mst);

/**
 * snd_hda_jack_detect_enable - Enable the jack detection on the given pin
 * @codec: the HDA codec
 * @nid: pin NID to enable jack detection
 * @dev_id: pin device entry id
 *
 * Enable the jack detection with the default callback.  Returns zero if
 * successful or a negative error code.
 */
int snd_hda_jack_detect_enable(struct hda_codec *codec, hda_nid_t nid,
			       int dev_id)
{
	return PTR_ERR_OR_ZERO(snd_hda_jack_detect_enable_callback_mst(codec,
								       nid,
								       dev_id,
								       NULL));
}
EXPORT_SYMBOL_GPL(snd_hda_jack_detect_enable);

/**
 * snd_hda_jack_set_gating_jack - Set gating jack.
 * @codec: the HDA codec
 * @gated_nid: gated pin NID
 * @gating_nid: gating pin NID
 *
 * Indicates the gated jack is only valid when the gating jack is plugged.
 */
int snd_hda_jack_set_gating_jack(struct hda_codec *codec, hda_nid_t gated_nid,
				 hda_nid_t gating_nid)
{
	struct hda_jack_tbl *gated = snd_hda_jack_tbl_new(codec, gated_nid, 0);
	struct hda_jack_tbl *gating =
		snd_hda_jack_tbl_new(codec, gating_nid, 0);

	WARN_ON(codec->dp_mst);

	if (!gated || !gating)
		return -EINVAL;

	gated->gating_jack = gating_nid;
	gating->gated_jack = gated_nid;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_set_gating_jack);

/**
 * snd_hda_jack_bind_keymap - bind keys generated from one NID to another jack.
 * @codec: the HDA codec
 * @key_nid: key event is generated by this pin NID
 * @keymap: map of key type and key code
 * @jack_nid: key reports to the jack of this pin NID
 *
 * This function is used in the case of key is generated from one NID while is
 * reported to the jack of another NID.
 */
int snd_hda_jack_bind_keymap(struct hda_codec *codec, hda_nid_t key_nid,
			     const struct hda_jack_keymap *keymap,
			     hda_nid_t jack_nid)
{
	const struct hda_jack_keymap *map;
	struct hda_jack_tbl *key_gen = snd_hda_jack_tbl_get(codec, key_nid);
	struct hda_jack_tbl *report_to = snd_hda_jack_tbl_get(codec, jack_nid);

	WARN_ON(codec->dp_mst);

	if (!key_gen || !report_to || !report_to->jack)
		return -EINVAL;

	key_gen->key_report_jack = jack_nid;

	if (keymap)
		for (map = keymap; map->type; map++)
			snd_jack_set_key(report_to->jack, map->type, map->key);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_bind_keymap);

/**
 * snd_hda_jack_set_button_state - report button event to the hda_jack_tbl button_state.
 * @codec: the HDA codec
 * @jack_nid: the button event reports to the jack_tbl of this NID
 * @button_state: the button event captured by codec
 *
 * Codec driver calls this function to report the button event.
 */
void snd_hda_jack_set_button_state(struct hda_codec *codec, hda_nid_t jack_nid,
				   int button_state)
{
	struct hda_jack_tbl *jack = snd_hda_jack_tbl_get(codec, jack_nid);

	if (!jack)
		return;

	if (jack->key_report_jack) {
		struct hda_jack_tbl *report_to =
			snd_hda_jack_tbl_get(codec, jack->key_report_jack);

		if (report_to) {
			report_to->button_state = button_state;
			return;
		}
	}

	jack->button_state = button_state;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_set_button_state);

/**
 * snd_hda_jack_report_sync - sync the states of all jacks and report if changed
 * @codec: the HDA codec
 */
void snd_hda_jack_report_sync(struct hda_codec *codec)
{
	struct hda_jack_tbl *jack;
	int i, state;

	/* update all jacks at first */
	jack = codec->jacktbl.list;
	for (i = 0; i < codec->jacktbl.used; i++, jack++)
		if (jack->nid)
			jack_detect_update(codec, jack);

	/* report the updated jacks; it's done after updating all jacks
	 * to make sure that all gating jacks properly have been set
	 */
	jack = codec->jacktbl.list;
	for (i = 0; i < codec->jacktbl.used; i++, jack++)
		if (jack->nid) {
			if (!jack->jack || jack->block_report)
				continue;
			state = jack->button_state;
			if (get_jack_plug_state(jack->pin_sense))
				state |= jack->type;
			snd_jack_report(jack->jack, state);
			if (jack->button_state) {
				snd_jack_report(jack->jack,
						state & ~jack->button_state);
				jack->button_state = 0; /* button released */
			}
		}
}
EXPORT_SYMBOL_GPL(snd_hda_jack_report_sync);

/* guess the jack type from the pin-config */
static int get_input_jack_type(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int def_conf = snd_hda_codec_get_pincfg(codec, nid);
	switch (get_defcfg_device(def_conf)) {
	case AC_JACK_LINE_OUT:
	case AC_JACK_SPEAKER:
		return SND_JACK_LINEOUT;
	case AC_JACK_HP_OUT:
		return SND_JACK_HEADPHONE;
	case AC_JACK_SPDIF_OUT:
	case AC_JACK_DIG_OTHER_OUT:
		return SND_JACK_AVOUT;
	case AC_JACK_MIC_IN:
		return SND_JACK_MICROPHONE;
	default:
		return SND_JACK_LINEIN;
	}
}

static void hda_free_jack_priv(struct snd_jack *jack)
{
	struct hda_jack_tbl *jacks = jack->private_data;
	jacks->nid = 0;
	jacks->jack = NULL;
}

/**
 * snd_hda_jack_add_kctl_mst - Add a kctl for the given pin
 * @codec: the HDA codec
 * @nid: pin NID to assign
 * @dev_id : pin device entry id
 * @name: string name for the jack
 * @phantom_jack: flag to deal as a phantom jack
 * @type: jack type bits to be reported, 0 for guessing from pincfg
 * @keymap: optional jack / key mapping
 *
 * This assigns a jack-detection kctl to the given pin.  The kcontrol
 * will have the given name and index.
 */
int snd_hda_jack_add_kctl_mst(struct hda_codec *codec, hda_nid_t nid,
			      int dev_id, const char *name, bool phantom_jack,
			      int type, const struct hda_jack_keymap *keymap)
{
	struct hda_jack_tbl *jack;
	const struct hda_jack_keymap *map;
	int err, state, buttons;

	jack = snd_hda_jack_tbl_new(codec, nid, dev_id);
	if (!jack)
		return 0;
	if (jack->jack)
		return 0; /* already created */

	if (!type)
		type = get_input_jack_type(codec, nid);

	buttons = 0;
	if (keymap) {
		for (map = keymap; map->type; map++)
			buttons |= map->type;
	}

	err = snd_jack_new(codec->card, name, type | buttons,
			   &jack->jack, true, phantom_jack);
	if (err < 0)
		return err;

	jack->phantom_jack = !!phantom_jack;
	jack->type = type;
	jack->button_state = 0;
	jack->jack->private_data = jack;
	jack->jack->private_free = hda_free_jack_priv;
	if (keymap) {
		for (map = keymap; map->type; map++)
			snd_jack_set_key(jack->jack, map->type, map->key);
	}

	state = snd_hda_jack_detect_mst(codec, nid, dev_id);
	snd_jack_report(jack->jack, state ? jack->type : 0);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_add_kctl_mst);

static int add_jack_kctl(struct hda_codec *codec, hda_nid_t nid,
			 const struct auto_pin_cfg *cfg,
			 const char *base_name)
{
	unsigned int def_conf, conn;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	int err;
	bool phantom_jack;

	WARN_ON(codec->dp_mst);

	if (!nid)
		return 0;
	def_conf = snd_hda_codec_get_pincfg(codec, nid);
	conn = get_defcfg_connect(def_conf);
	if (conn == AC_JACK_PORT_NONE)
		return 0;
	phantom_jack = (conn != AC_JACK_PORT_COMPLEX) ||
		       !is_jack_detectable(codec, nid);

	if (base_name)
		strscpy(name, base_name, sizeof(name));
	else
		snd_hda_get_pin_label(codec, nid, cfg, name, sizeof(name), NULL);
	if (phantom_jack)
		/* Example final name: "Internal Mic Phantom Jack" */
		strncat(name, " Phantom", sizeof(name) - strlen(name) - 1);
	err = snd_hda_jack_add_kctl(codec, nid, name, phantom_jack, 0, NULL);
	if (err < 0)
		return err;

	if (!phantom_jack)
		return snd_hda_jack_detect_enable(codec, nid, 0);
	return 0;
}

/**
 * snd_hda_jack_add_kctls - Add kctls for all pins included in the given pincfg
 * @codec: the HDA codec
 * @cfg: pin config table to parse
 */
int snd_hda_jack_add_kctls(struct hda_codec *codec,
			   const struct auto_pin_cfg *cfg)
{
	const hda_nid_t *p;
	int i, err;

	for (i = 0; i < cfg->num_inputs; i++) {
		/* If we have headphone mics; make sure they get the right name
		   before grabbed by output pins */
		if (cfg->inputs[i].is_headphone_mic) {
			if (auto_cfg_hp_outs(cfg) == 1)
				err = add_jack_kctl(codec, auto_cfg_hp_pins(cfg)[0],
						    cfg, "Headphone Mic");
			else
				err = add_jack_kctl(codec, cfg->inputs[i].pin,
						    cfg, "Headphone Mic");
		} else
			err = add_jack_kctl(codec, cfg->inputs[i].pin, cfg,
					    NULL);
		if (err < 0)
			return err;
	}

	for (i = 0, p = cfg->line_out_pins; i < cfg->line_outs; i++, p++) {
		err = add_jack_kctl(codec, *p, cfg, NULL);
		if (err < 0)
			return err;
	}
	for (i = 0, p = cfg->hp_pins; i < cfg->hp_outs; i++, p++) {
		if (*p == *cfg->line_out_pins) /* might be duplicated */
			break;
		err = add_jack_kctl(codec, *p, cfg, NULL);
		if (err < 0)
			return err;
	}
	for (i = 0, p = cfg->speaker_pins; i < cfg->speaker_outs; i++, p++) {
		if (*p == *cfg->line_out_pins) /* might be duplicated */
			break;
		err = add_jack_kctl(codec, *p, cfg, NULL);
		if (err < 0)
			return err;
	}
	for (i = 0, p = cfg->dig_out_pins; i < cfg->dig_outs; i++, p++) {
		err = add_jack_kctl(codec, *p, cfg, NULL);
		if (err < 0)
			return err;
	}
	err = add_jack_kctl(codec, cfg->dig_in_pin, cfg, NULL);
	if (err < 0)
		return err;
	err = add_jack_kctl(codec, cfg->mono_out_pin, cfg, NULL);
	if (err < 0)
		return err;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_jack_add_kctls);

static void call_jack_callback(struct hda_codec *codec, unsigned int res,
			       struct hda_jack_tbl *jack)
{
	struct hda_jack_callback *cb;

	for (cb = jack->callback; cb; cb = cb->next) {
		cb->jack = jack;
		cb->unsol_res = res;
		cb->func(codec, cb);
	}
	if (jack->gated_jack) {
		struct hda_jack_tbl *gated =
			snd_hda_jack_tbl_get_mst(codec, jack->gated_jack,
						 jack->dev_id);
		if (gated) {
			for (cb = gated->callback; cb; cb = cb->next) {
				cb->jack = gated;
				cb->unsol_res = res;
				cb->func(codec, cb);
			}
		}
	}
}

/**
 * snd_hda_jack_unsol_event - Handle an unsolicited event
 * @codec: the HDA codec
 * @res: the unsolicited event data
 */
void snd_hda_jack_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct hda_jack_tbl *event;
	int tag = (res & AC_UNSOL_RES_TAG) >> AC_UNSOL_RES_TAG_SHIFT;

	if (codec->dp_mst) {
		int dev_entry =
			(res & AC_UNSOL_RES_DE) >> AC_UNSOL_RES_DE_SHIFT;

		event = snd_hda_jack_tbl_get_from_tag(codec, tag, dev_entry);
	} else {
		event = snd_hda_jack_tbl_get_from_tag(codec, tag, 0);
	}
	if (!event)
		return;

	if (event->key_report_jack) {
		struct hda_jack_tbl *report_to =
			snd_hda_jack_tbl_get_mst(codec, event->key_report_jack,
						 event->dev_id);
		if (report_to)
			report_to->jack_dirty = 1;
	} else
		event->jack_dirty = 1;

	call_jack_callback(codec, res, event);
	snd_hda_jack_report_sync(codec);
}
EXPORT_SYMBOL_GPL(snd_hda_jack_unsol_event);

/**
 * snd_hda_jack_poll_all - Poll all jacks
 * @codec: the HDA codec
 *
 * Poll all detectable jacks with dirty flag, update the status, call
 * callbacks and call snd_hda_jack_report_sync() if any changes are found.
 */
void snd_hda_jack_poll_all(struct hda_codec *codec)
{
	struct hda_jack_tbl *jack = codec->jacktbl.list;
	int i, changes = 0;

	for (i = 0; i < codec->jacktbl.used; i++, jack++) {
		unsigned int old_sense;
		if (!jack->nid || !jack->jack_dirty || jack->phantom_jack)
			continue;
		old_sense = get_jack_plug_state(jack->pin_sense);
		jack_detect_update(codec, jack);
		if (old_sense == get_jack_plug_state(jack->pin_sense))
			continue;
		changes = 1;
		call_jack_callback(codec, 0, jack);
	}
	if (changes)
		snd_hda_jack_report_sync(codec);
}
EXPORT_SYMBOL_GPL(snd_hda_jack_poll_all);

