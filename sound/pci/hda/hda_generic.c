/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Generic widget tree parser
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
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
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/sort.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/tlv.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_beep.h"
#include "hda_generic.h"


/**
 * snd_hda_gen_spec_init - initialize hda_gen_spec struct
 * @spec: hda_gen_spec object to initialize
 *
 * Initialize the given hda_gen_spec object.
 */
int snd_hda_gen_spec_init(struct hda_gen_spec *spec)
{
	snd_array_init(&spec->kctls, sizeof(struct snd_kcontrol_new), 32);
	snd_array_init(&spec->paths, sizeof(struct nid_path), 8);
	snd_array_init(&spec->loopback_list, sizeof(struct hda_amp_list), 8);
	mutex_init(&spec->pcm_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_spec_init);

/**
 * snd_hda_gen_add_kctl - Add a new kctl_new struct from the template
 * @spec: hda_gen_spec object
 * @name: name string to override the template, NULL if unchanged
 * @temp: template for the new kctl
 *
 * Add a new kctl (actually snd_kcontrol_new to be instantiated later)
 * element based on the given snd_kcontrol_new template @temp and the
 * name string @name to the list in @spec.
 * Returns the newly created object or NULL as error.
 */
struct snd_kcontrol_new *
snd_hda_gen_add_kctl(struct hda_gen_spec *spec, const char *name,
		     const struct snd_kcontrol_new *temp)
{
	struct snd_kcontrol_new *knew = snd_array_new(&spec->kctls);
	if (!knew)
		return NULL;
	*knew = *temp;
	if (name)
		knew->name = kstrdup(name, GFP_KERNEL);
	else if (knew->name)
		knew->name = kstrdup(knew->name, GFP_KERNEL);
	if (!knew->name)
		return NULL;
	return knew;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_add_kctl);

static void free_kctls(struct hda_gen_spec *spec)
{
	if (spec->kctls.list) {
		struct snd_kcontrol_new *kctl = spec->kctls.list;
		int i;
		for (i = 0; i < spec->kctls.used; i++)
			kfree(kctl[i].name);
	}
	snd_array_free(&spec->kctls);
}

static void snd_hda_gen_spec_free(struct hda_gen_spec *spec)
{
	if (!spec)
		return;
	free_kctls(spec);
	snd_array_free(&spec->paths);
	snd_array_free(&spec->loopback_list);
}

/*
 * store user hints
 */
static void parse_user_hints(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int val;

	val = snd_hda_get_bool_hint(codec, "jack_detect");
	if (val >= 0)
		codec->no_jack_detect = !val;
	val = snd_hda_get_bool_hint(codec, "inv_jack_detect");
	if (val >= 0)
		codec->inv_jack_detect = !!val;
	val = snd_hda_get_bool_hint(codec, "trigger_sense");
	if (val >= 0)
		codec->no_trigger_sense = !val;
	val = snd_hda_get_bool_hint(codec, "inv_eapd");
	if (val >= 0)
		codec->inv_eapd = !!val;
	val = snd_hda_get_bool_hint(codec, "pcm_format_first");
	if (val >= 0)
		codec->pcm_format_first = !!val;
	val = snd_hda_get_bool_hint(codec, "sticky_stream");
	if (val >= 0)
		codec->no_sticky_stream = !val;
	val = snd_hda_get_bool_hint(codec, "spdif_status_reset");
	if (val >= 0)
		codec->spdif_status_reset = !!val;
	val = snd_hda_get_bool_hint(codec, "pin_amp_workaround");
	if (val >= 0)
		codec->pin_amp_workaround = !!val;
	val = snd_hda_get_bool_hint(codec, "single_adc_amp");
	if (val >= 0)
		codec->single_adc_amp = !!val;

	val = snd_hda_get_bool_hint(codec, "auto_mute");
	if (val >= 0)
		spec->suppress_auto_mute = !val;
	val = snd_hda_get_bool_hint(codec, "auto_mic");
	if (val >= 0)
		spec->suppress_auto_mic = !val;
	val = snd_hda_get_bool_hint(codec, "line_in_auto_switch");
	if (val >= 0)
		spec->line_in_auto_switch = !!val;
	val = snd_hda_get_bool_hint(codec, "auto_mute_via_amp");
	if (val >= 0)
		spec->auto_mute_via_amp = !!val;
	val = snd_hda_get_bool_hint(codec, "need_dac_fix");
	if (val >= 0)
		spec->need_dac_fix = !!val;
	val = snd_hda_get_bool_hint(codec, "primary_hp");
	if (val >= 0)
		spec->no_primary_hp = !val;
	val = snd_hda_get_bool_hint(codec, "multi_io");
	if (val >= 0)
		spec->no_multi_io = !val;
	val = snd_hda_get_bool_hint(codec, "multi_cap_vol");
	if (val >= 0)
		spec->multi_cap_vol = !!val;
	val = snd_hda_get_bool_hint(codec, "inv_dmic_split");
	if (val >= 0)
		spec->inv_dmic_split = !!val;
	val = snd_hda_get_bool_hint(codec, "indep_hp");
	if (val >= 0)
		spec->indep_hp = !!val;
	val = snd_hda_get_bool_hint(codec, "add_stereo_mix_input");
	if (val >= 0)
		spec->add_stereo_mix_input = !!val;
	/* the following two are just for compatibility */
	val = snd_hda_get_bool_hint(codec, "add_out_jack_modes");
	if (val >= 0)
		spec->add_jack_modes = !!val;
	val = snd_hda_get_bool_hint(codec, "add_in_jack_modes");
	if (val >= 0)
		spec->add_jack_modes = !!val;
	val = snd_hda_get_bool_hint(codec, "add_jack_modes");
	if (val >= 0)
		spec->add_jack_modes = !!val;
	val = snd_hda_get_bool_hint(codec, "power_down_unused");
	if (val >= 0)
		spec->power_down_unused = !!val;
	val = snd_hda_get_bool_hint(codec, "add_hp_mic");
	if (val >= 0)
		spec->hp_mic = !!val;
	val = snd_hda_get_bool_hint(codec, "hp_mic_detect");
	if (val >= 0)
		spec->suppress_hp_mic_detect = !val;

	if (!snd_hda_get_int_hint(codec, "mixer_nid", &val))
		spec->mixer_nid = val;
}

/*
 * pin control value accesses
 */

#define update_pin_ctl(codec, pin, val) \
	snd_hda_codec_update_cache(codec, pin, 0, \
				   AC_VERB_SET_PIN_WIDGET_CONTROL, val)

/* restore the pinctl based on the cached value */
static inline void restore_pin_ctl(struct hda_codec *codec, hda_nid_t pin)
{
	update_pin_ctl(codec, pin, snd_hda_codec_get_pin_target(codec, pin));
}

/* set the pinctl target value and write it if requested */
static void set_pin_target(struct hda_codec *codec, hda_nid_t pin,
			   unsigned int val, bool do_write)
{
	if (!pin)
		return;
	val = snd_hda_correct_pin_ctl(codec, pin, val);
	snd_hda_codec_set_pin_target(codec, pin, val);
	if (do_write)
		update_pin_ctl(codec, pin, val);
}

/* set pinctl target values for all given pins */
static void set_pin_targets(struct hda_codec *codec, int num_pins,
			    hda_nid_t *pins, unsigned int val)
{
	int i;
	for (i = 0; i < num_pins; i++)
		set_pin_target(codec, pins[i], val, false);
}

/*
 * parsing paths
 */

/* return the position of NID in the list, or -1 if not found */
static int find_idx_in_nid_list(hda_nid_t nid, const hda_nid_t *list, int nums)
{
	int i;
	for (i = 0; i < nums; i++)
		if (list[i] == nid)
			return i;
	return -1;
}

/* return true if the given NID is contained in the path */
static bool is_nid_contained(struct nid_path *path, hda_nid_t nid)
{
	return find_idx_in_nid_list(nid, path->path, path->depth) >= 0;
}

static struct nid_path *get_nid_path(struct hda_codec *codec,
				     hda_nid_t from_nid, hda_nid_t to_nid,
				     int anchor_nid)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->paths.used; i++) {
		struct nid_path *path = snd_array_elem(&spec->paths, i);
		if (path->depth <= 0)
			continue;
		if ((!from_nid || path->path[0] == from_nid) &&
		    (!to_nid || path->path[path->depth - 1] == to_nid)) {
			if (!anchor_nid ||
			    (anchor_nid > 0 && is_nid_contained(path, anchor_nid)) ||
			    (anchor_nid < 0 && !is_nid_contained(path, anchor_nid)))
				return path;
		}
	}
	return NULL;
}

/**
 * snd_hda_get_nid_path - get the path between the given NIDs
 * @codec: the HDA codec
 * @from_nid: the NID where the path start from
 * @to_nid: the NID where the path ends at
 *
 * Return the found nid_path object or NULL for error.
 * Passing 0 to either @from_nid or @to_nid behaves as a wildcard.
 */
struct nid_path *snd_hda_get_nid_path(struct hda_codec *codec,
				      hda_nid_t from_nid, hda_nid_t to_nid)
{
	return get_nid_path(codec, from_nid, to_nid, 0);
}
EXPORT_SYMBOL_GPL(snd_hda_get_nid_path);

/**
 * snd_hda_get_path_idx - get the index number corresponding to the path
 * instance
 * @codec: the HDA codec
 * @path: nid_path object
 *
 * The returned index starts from 1, i.e. the actual array index with offset 1,
 * and zero is handled as an invalid path
 */
int snd_hda_get_path_idx(struct hda_codec *codec, struct nid_path *path)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *array = spec->paths.list;
	ssize_t idx;

	if (!spec->paths.used)
		return 0;
	idx = path - array;
	if (idx < 0 || idx >= spec->paths.used)
		return 0;
	return idx + 1;
}
EXPORT_SYMBOL_GPL(snd_hda_get_path_idx);

/**
 * snd_hda_get_path_from_idx - get the path instance corresponding to the
 * given index number
 * @codec: the HDA codec
 * @idx: the path index
 */
struct nid_path *snd_hda_get_path_from_idx(struct hda_codec *codec, int idx)
{
	struct hda_gen_spec *spec = codec->spec;

	if (idx <= 0 || idx > spec->paths.used)
		return NULL;
	return snd_array_elem(&spec->paths, idx - 1);
}
EXPORT_SYMBOL_GPL(snd_hda_get_path_from_idx);

/* check whether the given DAC is already found in any existing paths */
static bool is_dac_already_used(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->paths.used; i++) {
		struct nid_path *path = snd_array_elem(&spec->paths, i);
		if (path->path[0] == nid)
			return true;
	}
	return false;
}

/* check whether the given two widgets can be connected */
static bool is_reachable_path(struct hda_codec *codec,
			      hda_nid_t from_nid, hda_nid_t to_nid)
{
	if (!from_nid || !to_nid)
		return false;
	return snd_hda_get_conn_index(codec, to_nid, from_nid, true) >= 0;
}

/* nid, dir and idx */
#define AMP_VAL_COMPARE_MASK	(0xffff | (1U << 18) | (0x0f << 19))

/* check whether the given ctl is already assigned in any path elements */
static bool is_ctl_used(struct hda_codec *codec, unsigned int val, int type)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	val &= AMP_VAL_COMPARE_MASK;
	for (i = 0; i < spec->paths.used; i++) {
		struct nid_path *path = snd_array_elem(&spec->paths, i);
		if ((path->ctls[type] & AMP_VAL_COMPARE_MASK) == val)
			return true;
	}
	return false;
}

/* check whether a control with the given (nid, dir, idx) was assigned */
static bool is_ctl_associated(struct hda_codec *codec, hda_nid_t nid,
			      int dir, int idx, int type)
{
	unsigned int val = HDA_COMPOSE_AMP_VAL(nid, 3, idx, dir);
	return is_ctl_used(codec, val, type);
}

static void print_nid_path(struct hda_codec *codec,
			   const char *pfx, struct nid_path *path)
{
	char buf[40];
	char *pos = buf;
	int i;

	*pos = 0;
	for (i = 0; i < path->depth; i++)
		pos += scnprintf(pos, sizeof(buf) - (pos - buf), "%s%02x",
				 pos != buf ? ":" : "",
				 path->path[i]);

	codec_dbg(codec, "%s path: depth=%d '%s'\n", pfx, path->depth, buf);
}

/* called recursively */
static bool __parse_nid_path(struct hda_codec *codec,
			     hda_nid_t from_nid, hda_nid_t to_nid,
			     int anchor_nid, struct nid_path *path,
			     int depth)
{
	const hda_nid_t *conn;
	int i, nums;

	if (to_nid == anchor_nid)
		anchor_nid = 0; /* anchor passed */
	else if (to_nid == (hda_nid_t)(-anchor_nid))
		return false; /* hit the exclusive nid */

	nums = snd_hda_get_conn_list(codec, to_nid, &conn);
	for (i = 0; i < nums; i++) {
		if (conn[i] != from_nid) {
			/* special case: when from_nid is 0,
			 * try to find an empty DAC
			 */
			if (from_nid ||
			    get_wcaps_type(get_wcaps(codec, conn[i])) != AC_WID_AUD_OUT ||
			    is_dac_already_used(codec, conn[i]))
				continue;
		}
		/* anchor is not requested or already passed? */
		if (anchor_nid <= 0)
			goto found;
	}
	if (depth >= MAX_NID_PATH_DEPTH)
		return false;
	for (i = 0; i < nums; i++) {
		unsigned int type;
		type = get_wcaps_type(get_wcaps(codec, conn[i]));
		if (type == AC_WID_AUD_OUT || type == AC_WID_AUD_IN ||
		    type == AC_WID_PIN)
			continue;
		if (__parse_nid_path(codec, from_nid, conn[i],
				     anchor_nid, path, depth + 1))
			goto found;
	}
	return false;

 found:
	path->path[path->depth] = conn[i];
	path->idx[path->depth + 1] = i;
	if (nums > 1 && get_wcaps_type(get_wcaps(codec, to_nid)) != AC_WID_AUD_MIX)
		path->multi[path->depth + 1] = 1;
	path->depth++;
	return true;
}

/**
 * snd_hda_parse_nid_path - parse the widget path from the given nid to
 * the target nid
 * @codec: the HDA codec
 * @from_nid: the NID where the path start from
 * @to_nid: the NID where the path ends at
 * @anchor_nid: the anchor indication
 * @path: the path object to store the result
 *
 * Returns true if a matching path is found.
 *
 * The parsing behavior depends on parameters:
 * when @from_nid is 0, try to find an empty DAC;
 * when @anchor_nid is set to a positive value, only paths through the widget
 * with the given value are evaluated.
 * when @anchor_nid is set to a negative value, paths through the widget
 * with the negative of given value are excluded, only other paths are chosen.
 * when @anchor_nid is zero, no special handling about path selection.
 */
bool snd_hda_parse_nid_path(struct hda_codec *codec, hda_nid_t from_nid,
			    hda_nid_t to_nid, int anchor_nid,
			    struct nid_path *path)
{
	if (__parse_nid_path(codec, from_nid, to_nid, anchor_nid, path, 1)) {
		path->path[path->depth] = to_nid;
		path->depth++;
		return true;
	}
	return false;
}
EXPORT_SYMBOL_GPL(snd_hda_parse_nid_path);

/**
 * snd_hda_add_new_path - parse the path between the given NIDs and
 * add to the path list
 * @codec: the HDA codec
 * @from_nid: the NID where the path start from
 * @to_nid: the NID where the path ends at
 * @anchor_nid: the anchor indication, see snd_hda_parse_nid_path()
 *
 * If no valid path is found, returns NULL.
 */
struct nid_path *
snd_hda_add_new_path(struct hda_codec *codec, hda_nid_t from_nid,
		     hda_nid_t to_nid, int anchor_nid)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;

	if (from_nid && to_nid && !is_reachable_path(codec, from_nid, to_nid))
		return NULL;

	/* check whether the path has been already added */
	path = get_nid_path(codec, from_nid, to_nid, anchor_nid);
	if (path)
		return path;

	path = snd_array_new(&spec->paths);
	if (!path)
		return NULL;
	memset(path, 0, sizeof(*path));
	if (snd_hda_parse_nid_path(codec, from_nid, to_nid, anchor_nid, path))
		return path;
	/* push back */
	spec->paths.used--;
	return NULL;
}
EXPORT_SYMBOL_GPL(snd_hda_add_new_path);

/* clear the given path as invalid so that it won't be picked up later */
static void invalidate_nid_path(struct hda_codec *codec, int idx)
{
	struct nid_path *path = snd_hda_get_path_from_idx(codec, idx);
	if (!path)
		return;
	memset(path, 0, sizeof(*path));
}

/* return a DAC if paired to the given pin by codec driver */
static hda_nid_t get_preferred_dac(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	const hda_nid_t *list = spec->preferred_dacs;

	if (!list)
		return 0;
	for (; *list; list += 2)
		if (*list == pin)
			return list[1];
	return 0;
}

/* look for an empty DAC slot */
static hda_nid_t look_for_dac(struct hda_codec *codec, hda_nid_t pin,
			      bool is_digital)
{
	struct hda_gen_spec *spec = codec->spec;
	bool cap_digital;
	int i;

	for (i = 0; i < spec->num_all_dacs; i++) {
		hda_nid_t nid = spec->all_dacs[i];
		if (!nid || is_dac_already_used(codec, nid))
			continue;
		cap_digital = !!(get_wcaps(codec, nid) & AC_WCAP_DIGITAL);
		if (is_digital != cap_digital)
			continue;
		if (is_reachable_path(codec, nid, pin))
			return nid;
	}
	return 0;
}

/* replace the channels in the composed amp value with the given number */
static unsigned int amp_val_replace_channels(unsigned int val, unsigned int chs)
{
	val &= ~(0x3U << 16);
	val |= chs << 16;
	return val;
}

static bool same_amp_caps(struct hda_codec *codec, hda_nid_t nid1,
			  hda_nid_t nid2, int dir)
{
	if (!(get_wcaps(codec, nid1) & (1 << (dir + 1))))
		return !(get_wcaps(codec, nid2) & (1 << (dir + 1)));
	return (query_amp_caps(codec, nid1, dir) ==
		query_amp_caps(codec, nid2, dir));
}

/* look for a widget suitable for assigning a mute switch in the path */
static hda_nid_t look_for_out_mute_nid(struct hda_codec *codec,
				       struct nid_path *path)
{
	int i;

	for (i = path->depth - 1; i >= 0; i--) {
		if (nid_has_mute(codec, path->path[i], HDA_OUTPUT))
			return path->path[i];
		if (i != path->depth - 1 && i != 0 &&
		    nid_has_mute(codec, path->path[i], HDA_INPUT))
			return path->path[i];
	}
	return 0;
}

/* look for a widget suitable for assigning a volume ctl in the path */
static hda_nid_t look_for_out_vol_nid(struct hda_codec *codec,
				      struct nid_path *path)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = path->depth - 1; i >= 0; i--) {
		hda_nid_t nid = path->path[i];
		if ((spec->out_vol_mask >> nid) & 1)
			continue;
		if (nid_has_volume(codec, nid, HDA_OUTPUT))
			return nid;
	}
	return 0;
}

/*
 * path activation / deactivation
 */

/* can have the amp-in capability? */
static bool has_amp_in(struct hda_codec *codec, struct nid_path *path, int idx)
{
	hda_nid_t nid = path->path[idx];
	unsigned int caps = get_wcaps(codec, nid);
	unsigned int type = get_wcaps_type(caps);

	if (!(caps & AC_WCAP_IN_AMP))
		return false;
	if (type == AC_WID_PIN && idx > 0) /* only for input pins */
		return false;
	return true;
}

/* can have the amp-out capability? */
static bool has_amp_out(struct hda_codec *codec, struct nid_path *path, int idx)
{
	hda_nid_t nid = path->path[idx];
	unsigned int caps = get_wcaps(codec, nid);
	unsigned int type = get_wcaps_type(caps);

	if (!(caps & AC_WCAP_OUT_AMP))
		return false;
	if (type == AC_WID_PIN && !idx) /* only for output pins */
		return false;
	return true;
}

/* check whether the given (nid,dir,idx) is active */
static bool is_active_nid(struct hda_codec *codec, hda_nid_t nid,
			  unsigned int dir, unsigned int idx)
{
	struct hda_gen_spec *spec = codec->spec;
	int i, n;

	for (n = 0; n < spec->paths.used; n++) {
		struct nid_path *path = snd_array_elem(&spec->paths, n);
		if (!path->active)
			continue;
		for (i = 0; i < path->depth; i++) {
			if (path->path[i] == nid) {
				if (dir == HDA_OUTPUT || path->idx[i] == idx)
					return true;
				break;
			}
		}
	}
	return false;
}

/* check whether the NID is referred by any active paths */
#define is_active_nid_for_any(codec, nid) \
	is_active_nid(codec, nid, HDA_OUTPUT, 0)

/* get the default amp value for the target state */
static int get_amp_val_to_activate(struct hda_codec *codec, hda_nid_t nid,
				   int dir, unsigned int caps, bool enable)
{
	unsigned int val = 0;

	if (caps & AC_AMPCAP_NUM_STEPS) {
		/* set to 0dB */
		if (enable)
			val = (caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT;
	}
	if (caps & (AC_AMPCAP_MUTE | AC_AMPCAP_MIN_MUTE)) {
		if (!enable)
			val |= HDA_AMP_MUTE;
	}
	return val;
}

/* initialize the amp value (only at the first time) */
static void init_amp(struct hda_codec *codec, hda_nid_t nid, int dir, int idx)
{
	unsigned int caps = query_amp_caps(codec, nid, dir);
	int val = get_amp_val_to_activate(codec, nid, dir, caps, false);
	snd_hda_codec_amp_init_stereo(codec, nid, dir, idx, 0xff, val);
}

/* calculate amp value mask we can modify;
 * if the given amp is controlled by mixers, don't touch it
 */
static unsigned int get_amp_mask_to_modify(struct hda_codec *codec,
					   hda_nid_t nid, int dir, int idx,
					   unsigned int caps)
{
	unsigned int mask = 0xff;

	if (caps & (AC_AMPCAP_MUTE | AC_AMPCAP_MIN_MUTE)) {
		if (is_ctl_associated(codec, nid, dir, idx, NID_PATH_MUTE_CTL))
			mask &= ~0x80;
	}
	if (caps & AC_AMPCAP_NUM_STEPS) {
		if (is_ctl_associated(codec, nid, dir, idx, NID_PATH_VOL_CTL) ||
		    is_ctl_associated(codec, nid, dir, idx, NID_PATH_BOOST_CTL))
			mask &= ~0x7f;
	}
	return mask;
}

static void activate_amp(struct hda_codec *codec, hda_nid_t nid, int dir,
			 int idx, int idx_to_check, bool enable)
{
	unsigned int caps;
	unsigned int mask, val;

	if (!enable && is_active_nid(codec, nid, dir, idx_to_check))
		return;

	caps = query_amp_caps(codec, nid, dir);
	val = get_amp_val_to_activate(codec, nid, dir, caps, enable);
	mask = get_amp_mask_to_modify(codec, nid, dir, idx_to_check, caps);
	if (!mask)
		return;

	val &= mask;
	snd_hda_codec_amp_stereo(codec, nid, dir, idx, mask, val);
}

static void activate_amp_out(struct hda_codec *codec, struct nid_path *path,
			     int i, bool enable)
{
	hda_nid_t nid = path->path[i];
	init_amp(codec, nid, HDA_OUTPUT, 0);
	activate_amp(codec, nid, HDA_OUTPUT, 0, 0, enable);
}

static void activate_amp_in(struct hda_codec *codec, struct nid_path *path,
			    int i, bool enable, bool add_aamix)
{
	struct hda_gen_spec *spec = codec->spec;
	const hda_nid_t *conn;
	int n, nums, idx;
	int type;
	hda_nid_t nid = path->path[i];

	nums = snd_hda_get_conn_list(codec, nid, &conn);
	type = get_wcaps_type(get_wcaps(codec, nid));
	if (type == AC_WID_PIN ||
	    (type == AC_WID_AUD_IN && codec->single_adc_amp)) {
		nums = 1;
		idx = 0;
	} else
		idx = path->idx[i];

	for (n = 0; n < nums; n++)
		init_amp(codec, nid, HDA_INPUT, n);

	/* here is a little bit tricky in comparison with activate_amp_out();
	 * when aa-mixer is available, we need to enable the path as well
	 */
	for (n = 0; n < nums; n++) {
		if (n != idx && (!add_aamix || conn[n] != spec->mixer_merge_nid))
			continue;
		activate_amp(codec, nid, HDA_INPUT, n, idx, enable);
	}
}

/**
 * snd_hda_activate_path - activate or deactivate the given path
 * @codec: the HDA codec
 * @path: the path to activate/deactivate
 * @enable: flag to activate or not
 * @add_aamix: enable the input from aamix NID
 *
 * If @add_aamix is set, enable the input from aa-mix NID as well (if any).
 */
void snd_hda_activate_path(struct hda_codec *codec, struct nid_path *path,
			   bool enable, bool add_aamix)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	if (!enable)
		path->active = false;

	for (i = path->depth - 1; i >= 0; i--) {
		hda_nid_t nid = path->path[i];
		if (enable && spec->power_down_unused) {
			/* make sure the widget is powered up */
			if (!snd_hda_check_power_state(codec, nid, AC_PWRST_D0))
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_POWER_STATE,
						    AC_PWRST_D0);
		}
		if (enable && path->multi[i])
			snd_hda_codec_update_cache(codec, nid, 0,
					    AC_VERB_SET_CONNECT_SEL,
					    path->idx[i]);
		if (has_amp_in(codec, path, i))
			activate_amp_in(codec, path, i, enable, add_aamix);
		if (has_amp_out(codec, path, i))
			activate_amp_out(codec, path, i, enable);
	}

	if (enable)
		path->active = true;
}
EXPORT_SYMBOL_GPL(snd_hda_activate_path);

/* if the given path is inactive, put widgets into D3 (only if suitable) */
static void path_power_down_sync(struct hda_codec *codec, struct nid_path *path)
{
	struct hda_gen_spec *spec = codec->spec;
	bool changed = false;
	int i;

	if (!spec->power_down_unused || path->active)
		return;

	for (i = 0; i < path->depth; i++) {
		hda_nid_t nid = path->path[i];
		if (!snd_hda_check_power_state(codec, nid, AC_PWRST_D3) &&
		    !is_active_nid_for_any(codec, nid)) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D3);
			changed = true;
		}
	}

	if (changed) {
		msleep(10);
		snd_hda_codec_read(codec, path->path[0], 0,
				   AC_VERB_GET_POWER_STATE, 0);
	}
}

/* turn on/off EAPD on the given pin */
static void set_pin_eapd(struct hda_codec *codec, hda_nid_t pin, bool enable)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->own_eapd_ctl ||
	    !(snd_hda_query_pin_caps(codec, pin) & AC_PINCAP_EAPD))
		return;
	if (spec->keep_eapd_on && !enable)
		return;
	if (codec->inv_eapd)
		enable = !enable;
	snd_hda_codec_update_cache(codec, pin, 0,
				   AC_VERB_SET_EAPD_BTLENABLE,
				   enable ? 0x02 : 0x00);
}

/* re-initialize the path specified by the given path index */
static void resume_path_from_idx(struct hda_codec *codec, int path_idx)
{
	struct nid_path *path = snd_hda_get_path_from_idx(codec, path_idx);
	if (path)
		snd_hda_activate_path(codec, path, path->active, false);
}


/*
 * Helper functions for creating mixer ctl elements
 */

static int hda_gen_mixer_mute_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol);
static int hda_gen_bind_mute_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol);

enum {
	HDA_CTL_WIDGET_VOL,
	HDA_CTL_WIDGET_MUTE,
	HDA_CTL_BIND_MUTE,
};
static const struct snd_kcontrol_new control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	/* only the put callback is replaced for handling the special mute */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.subdevice = HDA_SUBDEV_AMP_FLAG,
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = hda_gen_mixer_mute_put, /* replaced */
		.private_value = HDA_COMPOSE_AMP_VAL(0, 3, 0, 0),
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_bind_switch_get,
		.put = hda_gen_bind_mute_put, /* replaced */
		.private_value = HDA_COMPOSE_AMP_VAL(0, 3, 0, 0),
	},
};

/* add dynamic controls from template */
static struct snd_kcontrol_new *
add_control(struct hda_gen_spec *spec, int type, const char *name,
		       int cidx, unsigned long val)
{
	struct snd_kcontrol_new *knew;

	knew = snd_hda_gen_add_kctl(spec, name, &control_templates[type]);
	if (!knew)
		return NULL;
	knew->index = cidx;
	if (get_amp_nid_(val))
		knew->subdevice = HDA_SUBDEV_AMP_FLAG;
	knew->private_value = val;
	return knew;
}

static int add_control_with_pfx(struct hda_gen_spec *spec, int type,
				const char *pfx, const char *dir,
				const char *sfx, int cidx, unsigned long val)
{
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	snprintf(name, sizeof(name), "%s %s %s", pfx, dir, sfx);
	if (!add_control(spec, type, name, cidx, val))
		return -ENOMEM;
	return 0;
}

#define add_pb_vol_ctrl(spec, type, pfx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Volume", 0, val)
#define add_pb_sw_ctrl(spec, type, pfx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Switch", 0, val)
#define __add_pb_vol_ctrl(spec, type, pfx, cidx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Volume", cidx, val)
#define __add_pb_sw_ctrl(spec, type, pfx, cidx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Switch", cidx, val)

static int add_vol_ctl(struct hda_codec *codec, const char *pfx, int cidx,
		       unsigned int chs, struct nid_path *path)
{
	unsigned int val;
	if (!path)
		return 0;
	val = path->ctls[NID_PATH_VOL_CTL];
	if (!val)
		return 0;
	val = amp_val_replace_channels(val, chs);
	return __add_pb_vol_ctrl(codec->spec, HDA_CTL_WIDGET_VOL, pfx, cidx, val);
}

/* return the channel bits suitable for the given path->ctls[] */
static int get_default_ch_nums(struct hda_codec *codec, struct nid_path *path,
			       int type)
{
	int chs = 1; /* mono (left only) */
	if (path) {
		hda_nid_t nid = get_amp_nid_(path->ctls[type]);
		if (nid && (get_wcaps(codec, nid) & AC_WCAP_STEREO))
			chs = 3; /* stereo */
	}
	return chs;
}

static int add_stereo_vol(struct hda_codec *codec, const char *pfx, int cidx,
			  struct nid_path *path)
{
	int chs = get_default_ch_nums(codec, path, NID_PATH_VOL_CTL);
	return add_vol_ctl(codec, pfx, cidx, chs, path);
}

/* create a mute-switch for the given mixer widget;
 * if it has multiple sources (e.g. DAC and loopback), create a bind-mute
 */
static int add_sw_ctl(struct hda_codec *codec, const char *pfx, int cidx,
		      unsigned int chs, struct nid_path *path)
{
	unsigned int val;
	int type = HDA_CTL_WIDGET_MUTE;

	if (!path)
		return 0;
	val = path->ctls[NID_PATH_MUTE_CTL];
	if (!val)
		return 0;
	val = amp_val_replace_channels(val, chs);
	if (get_amp_direction_(val) == HDA_INPUT) {
		hda_nid_t nid = get_amp_nid_(val);
		int nums = snd_hda_get_num_conns(codec, nid);
		if (nums > 1) {
			type = HDA_CTL_BIND_MUTE;
			val |= nums << 19;
		}
	}
	return __add_pb_sw_ctrl(codec->spec, type, pfx, cidx, val);
}

static int add_stereo_sw(struct hda_codec *codec, const char *pfx,
				  int cidx, struct nid_path *path)
{
	int chs = get_default_ch_nums(codec, path, NID_PATH_MUTE_CTL);
	return add_sw_ctl(codec, pfx, cidx, chs, path);
}

/* playback mute control with the software mute bit check */
static void sync_auto_mute_bits(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;

	if (spec->auto_mute_via_amp) {
		hda_nid_t nid = get_amp_nid(kcontrol);
		bool enabled = !((spec->mute_bits >> nid) & 1);
		ucontrol->value.integer.value[0] &= enabled;
		ucontrol->value.integer.value[1] &= enabled;
	}
}

static int hda_gen_mixer_mute_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	sync_auto_mute_bits(kcontrol, ucontrol);
	return snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
}

static int hda_gen_bind_mute_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	sync_auto_mute_bits(kcontrol, ucontrol);
	return snd_hda_mixer_bind_switch_put(kcontrol, ucontrol);
}

/* any ctl assigned to the path with the given index? */
static bool path_has_mixer(struct hda_codec *codec, int path_idx, int ctl_type)
{
	struct nid_path *path = snd_hda_get_path_from_idx(codec, path_idx);
	return path && path->ctls[ctl_type];
}

static const char * const channel_name[4] = {
	"Front", "Surround", "CLFE", "Side"
};

/* give some appropriate ctl name prefix for the given line out channel */
static const char *get_line_out_pfx(struct hda_codec *codec, int ch,
				    int *index, int ctl_type)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;

	*index = 0;
	if (cfg->line_outs == 1 && !spec->multi_ios &&
	    !cfg->hp_outs && !cfg->speaker_outs)
		return spec->vmaster_mute.hook ? "PCM" : "Master";

	/* if there is really a single DAC used in the whole output paths,
	 * use it master (or "PCM" if a vmaster hook is present)
	 */
	if (spec->multiout.num_dacs == 1 && !spec->mixer_nid &&
	    !spec->multiout.hp_out_nid[0] && !spec->multiout.extra_out_nid[0])
		return spec->vmaster_mute.hook ? "PCM" : "Master";

	/* multi-io channels */
	if (ch >= cfg->line_outs)
		return channel_name[ch];

	switch (cfg->line_out_type) {
	case AUTO_PIN_SPEAKER_OUT:
		/* if the primary channel vol/mute is shared with HP volume,
		 * don't name it as Speaker
		 */
		if (!ch && cfg->hp_outs &&
		    !path_has_mixer(codec, spec->hp_paths[0], ctl_type))
			break;
		if (cfg->line_outs == 1)
			return "Speaker";
		if (cfg->line_outs == 2)
			return ch ? "Bass Speaker" : "Speaker";
		break;
	case AUTO_PIN_HP_OUT:
		/* if the primary channel vol/mute is shared with spk volume,
		 * don't name it as Headphone
		 */
		if (!ch && cfg->speaker_outs &&
		    !path_has_mixer(codec, spec->speaker_paths[0], ctl_type))
			break;
		/* for multi-io case, only the primary out */
		if (ch && spec->multi_ios)
			break;
		*index = ch;
		return "Headphone";
	case AUTO_PIN_LINE_OUT:
		/* This deals with the case where we have two DACs and
		 * one LO, one HP and one Speaker */
		if (!ch && cfg->speaker_outs && cfg->hp_outs) {
			bool hp_lo_shared = !path_has_mixer(codec, spec->hp_paths[0], ctl_type);
			bool spk_lo_shared = !path_has_mixer(codec, spec->speaker_paths[0], ctl_type);
			if (hp_lo_shared && spk_lo_shared)
				return spec->vmaster_mute.hook ? "PCM" : "Master";
			if (hp_lo_shared)
				return "Headphone+LO";
			if (spk_lo_shared)
				return "Speaker+LO";
		}
	}

	/* for a single channel output, we don't have to name the channel */
	if (cfg->line_outs == 1 && !spec->multi_ios)
		return "Line Out";

	if (ch >= ARRAY_SIZE(channel_name)) {
		snd_BUG();
		return "PCM";
	}

	return channel_name[ch];
}

/*
 * Parse output paths
 */

/* badness definition */
enum {
	/* No primary DAC is found for the main output */
	BAD_NO_PRIMARY_DAC = 0x10000,
	/* No DAC is found for the extra output */
	BAD_NO_DAC = 0x4000,
	/* No possible multi-ios */
	BAD_MULTI_IO = 0x120,
	/* No individual DAC for extra output */
	BAD_NO_EXTRA_DAC = 0x102,
	/* No individual DAC for extra surrounds */
	BAD_NO_EXTRA_SURR_DAC = 0x101,
	/* Primary DAC shared with main surrounds */
	BAD_SHARED_SURROUND = 0x100,
	/* No independent HP possible */
	BAD_NO_INDEP_HP = 0x10,
	/* Primary DAC shared with main CLFE */
	BAD_SHARED_CLFE = 0x10,
	/* Primary DAC shared with extra surrounds */
	BAD_SHARED_EXTRA_SURROUND = 0x10,
	/* Volume widget is shared */
	BAD_SHARED_VOL = 0x10,
};

/* look for widgets in the given path which are appropriate for
 * volume and mute controls, and assign the values to ctls[].
 *
 * When no appropriate widget is found in the path, the badness value
 * is incremented depending on the situation.  The function returns the
 * total badness for both volume and mute controls.
 */
static int assign_out_path_ctls(struct hda_codec *codec, struct nid_path *path)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t nid;
	unsigned int val;
	int badness = 0;

	if (!path)
		return BAD_SHARED_VOL * 2;

	if (path->ctls[NID_PATH_VOL_CTL] ||
	    path->ctls[NID_PATH_MUTE_CTL])
		return 0; /* already evaluated */

	nid = look_for_out_vol_nid(codec, path);
	if (nid) {
		val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
		if (spec->dac_min_mute)
			val |= HDA_AMP_VAL_MIN_MUTE;
		if (is_ctl_used(codec, val, NID_PATH_VOL_CTL))
			badness += BAD_SHARED_VOL;
		else
			path->ctls[NID_PATH_VOL_CTL] = val;
	} else
		badness += BAD_SHARED_VOL;
	nid = look_for_out_mute_nid(codec, path);
	if (nid) {
		unsigned int wid_type = get_wcaps_type(get_wcaps(codec, nid));
		if (wid_type == AC_WID_PIN || wid_type == AC_WID_AUD_OUT ||
		    nid_has_mute(codec, nid, HDA_OUTPUT))
			val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
		else
			val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT);
		if (is_ctl_used(codec, val, NID_PATH_MUTE_CTL))
			badness += BAD_SHARED_VOL;
		else
			path->ctls[NID_PATH_MUTE_CTL] = val;
	} else
		badness += BAD_SHARED_VOL;
	return badness;
}

const struct badness_table hda_main_out_badness = {
	.no_primary_dac = BAD_NO_PRIMARY_DAC,
	.no_dac = BAD_NO_DAC,
	.shared_primary = BAD_NO_PRIMARY_DAC,
	.shared_surr = BAD_SHARED_SURROUND,
	.shared_clfe = BAD_SHARED_CLFE,
	.shared_surr_main = BAD_SHARED_SURROUND,
};
EXPORT_SYMBOL_GPL(hda_main_out_badness);

const struct badness_table hda_extra_out_badness = {
	.no_primary_dac = BAD_NO_DAC,
	.no_dac = BAD_NO_DAC,
	.shared_primary = BAD_NO_EXTRA_DAC,
	.shared_surr = BAD_SHARED_EXTRA_SURROUND,
	.shared_clfe = BAD_SHARED_EXTRA_SURROUND,
	.shared_surr_main = BAD_NO_EXTRA_SURR_DAC,
};
EXPORT_SYMBOL_GPL(hda_extra_out_badness);

/* get the DAC of the primary output corresponding to the given array index */
static hda_nid_t get_primary_out(struct hda_codec *codec, int idx)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;

	if (cfg->line_outs > idx)
		return spec->private_dac_nids[idx];
	idx -= cfg->line_outs;
	if (spec->multi_ios > idx)
		return spec->multi_io[idx].dac;
	return 0;
}

/* return the DAC if it's reachable, otherwise zero */
static inline hda_nid_t try_dac(struct hda_codec *codec,
				hda_nid_t dac, hda_nid_t pin)
{
	return is_reachable_path(codec, dac, pin) ? dac : 0;
}

/* try to assign DACs to pins and return the resultant badness */
static int try_assign_dacs(struct hda_codec *codec, int num_outs,
			   const hda_nid_t *pins, hda_nid_t *dacs,
			   int *path_idx,
			   const struct badness_table *bad)
{
	struct hda_gen_spec *spec = codec->spec;
	int i, j;
	int badness = 0;
	hda_nid_t dac;

	if (!num_outs)
		return 0;

	for (i = 0; i < num_outs; i++) {
		struct nid_path *path;
		hda_nid_t pin = pins[i];

		path = snd_hda_get_path_from_idx(codec, path_idx[i]);
		if (path) {
			badness += assign_out_path_ctls(codec, path);
			continue;
		}

		dacs[i] = get_preferred_dac(codec, pin);
		if (dacs[i]) {
			if (is_dac_already_used(codec, dacs[i]))
				badness += bad->shared_primary;
		}

		if (!dacs[i])
			dacs[i] = look_for_dac(codec, pin, false);
		if (!dacs[i] && !i) {
			/* try to steal the DAC of surrounds for the front */
			for (j = 1; j < num_outs; j++) {
				if (is_reachable_path(codec, dacs[j], pin)) {
					dacs[0] = dacs[j];
					dacs[j] = 0;
					invalidate_nid_path(codec, path_idx[j]);
					path_idx[j] = 0;
					break;
				}
			}
		}
		dac = dacs[i];
		if (!dac) {
			if (num_outs > 2)
				dac = try_dac(codec, get_primary_out(codec, i), pin);
			if (!dac)
				dac = try_dac(codec, dacs[0], pin);
			if (!dac)
				dac = try_dac(codec, get_primary_out(codec, i), pin);
			if (dac) {
				if (!i)
					badness += bad->shared_primary;
				else if (i == 1)
					badness += bad->shared_surr;
				else
					badness += bad->shared_clfe;
			} else if (is_reachable_path(codec, spec->private_dac_nids[0], pin)) {
				dac = spec->private_dac_nids[0];
				badness += bad->shared_surr_main;
			} else if (!i)
				badness += bad->no_primary_dac;
			else
				badness += bad->no_dac;
		}
		if (!dac)
			continue;
		path = snd_hda_add_new_path(codec, dac, pin, -spec->mixer_nid);
		if (!path && !i && spec->mixer_nid) {
			/* try with aamix */
			path = snd_hda_add_new_path(codec, dac, pin, 0);
		}
		if (!path) {
			dac = dacs[i] = 0;
			badness += bad->no_dac;
		} else {
			/* print_nid_path(codec, "output", path); */
			path->active = true;
			path_idx[i] = snd_hda_get_path_idx(codec, path);
			badness += assign_out_path_ctls(codec, path);
		}
	}

	return badness;
}

/* return NID if the given pin has only a single connection to a certain DAC */
static hda_nid_t get_dac_if_single(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	hda_nid_t nid_found = 0;

	for (i = 0; i < spec->num_all_dacs; i++) {
		hda_nid_t nid = spec->all_dacs[i];
		if (!nid || is_dac_already_used(codec, nid))
			continue;
		if (is_reachable_path(codec, nid, pin)) {
			if (nid_found)
				return 0;
			nid_found = nid;
		}
	}
	return nid_found;
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

/* count the number of input pins that are capable to be multi-io */
static int count_multiio_pins(struct hda_codec *codec, hda_nid_t reference_pin)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int defcfg = snd_hda_codec_get_pincfg(codec, reference_pin);
	unsigned int location = get_defcfg_location(defcfg);
	int type, i;
	int num_pins = 0;

	for (type = AUTO_PIN_LINE_IN; type >= AUTO_PIN_MIC; type--) {
		for (i = 0; i < cfg->num_inputs; i++) {
			if (cfg->inputs[i].type != type)
				continue;
			if (can_be_multiio_pin(codec, location,
					       cfg->inputs[i].pin))
				num_pins++;
		}
	}
	return num_pins;
}

/*
 * multi-io helper
 *
 * When hardwired is set, try to fill ony hardwired pins, and returns
 * zero if any pins are filled, non-zero if nothing found.
 * When hardwired is off, try to fill possible input pins, and returns
 * the badness value.
 */
static int fill_multi_ios(struct hda_codec *codec,
			  hda_nid_t reference_pin,
			  bool hardwired)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int type, i, j, num_pins, old_pins;
	unsigned int defcfg = snd_hda_codec_get_pincfg(codec, reference_pin);
	unsigned int location = get_defcfg_location(defcfg);
	int badness = 0;
	struct nid_path *path;

	old_pins = spec->multi_ios;
	if (old_pins >= 2)
		goto end_fill;

	num_pins = count_multiio_pins(codec, reference_pin);
	if (num_pins < 2)
		goto end_fill;

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

			if (hardwired)
				dac = get_dac_if_single(codec, nid);
			else if (!dac)
				dac = look_for_dac(codec, nid, false);
			if (!dac) {
				badness++;
				continue;
			}
			path = snd_hda_add_new_path(codec, dac, nid,
						    -spec->mixer_nid);
			if (!path) {
				badness++;
				continue;
			}
			/* print_nid_path(codec, "multiio", path); */
			spec->multi_io[spec->multi_ios].pin = nid;
			spec->multi_io[spec->multi_ios].dac = dac;
			spec->out_paths[cfg->line_outs + spec->multi_ios] =
				snd_hda_get_path_idx(codec, path);
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
		/* cancel newly assigned paths */
		spec->paths.used -= spec->multi_ios - old_pins;
		spec->multi_ios = old_pins;
		return badness;
	}

	/* assign volume and mute controls */
	for (i = old_pins; i < spec->multi_ios; i++) {
		path = snd_hda_get_path_from_idx(codec, spec->out_paths[cfg->line_outs + i]);
		badness += assign_out_path_ctls(codec, path);
	}

	return badness;
}

/* map DACs for all pins in the list if they are single connections */
static bool map_singles(struct hda_codec *codec, int outs,
			const hda_nid_t *pins, hda_nid_t *dacs, int *path_idx)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	bool found = false;
	for (i = 0; i < outs; i++) {
		struct nid_path *path;
		hda_nid_t dac;
		if (dacs[i])
			continue;
		dac = get_dac_if_single(codec, pins[i]);
		if (!dac)
			continue;
		path = snd_hda_add_new_path(codec, dac, pins[i],
					    -spec->mixer_nid);
		if (!path && !i && spec->mixer_nid)
			path = snd_hda_add_new_path(codec, dac, pins[i], 0);
		if (path) {
			dacs[i] = dac;
			found = true;
			/* print_nid_path(codec, "output", path); */
			path->active = true;
			path_idx[i] = snd_hda_get_path_idx(codec, path);
		}
	}
	return found;
}

/* create a new path including aamix if available, and return its index */
static int check_aamix_out_path(struct hda_codec *codec, int path_idx)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;
	hda_nid_t path_dac, dac, pin;

	path = snd_hda_get_path_from_idx(codec, path_idx);
	if (!path || !path->depth ||
	    is_nid_contained(path, spec->mixer_nid))
		return 0;
	path_dac = path->path[0];
	dac = spec->private_dac_nids[0];
	pin = path->path[path->depth - 1];
	path = snd_hda_add_new_path(codec, dac, pin, spec->mixer_nid);
	if (!path) {
		if (dac != path_dac)
			dac = path_dac;
		else if (spec->multiout.hp_out_nid[0])
			dac = spec->multiout.hp_out_nid[0];
		else if (spec->multiout.extra_out_nid[0])
			dac = spec->multiout.extra_out_nid[0];
		else
			dac = 0;
		if (dac)
			path = snd_hda_add_new_path(codec, dac, pin,
						    spec->mixer_nid);
	}
	if (!path)
		return 0;
	/* print_nid_path(codec, "output-aamix", path); */
	path->active = false; /* unused as default */
	return snd_hda_get_path_idx(codec, path);
}

/* check whether the independent HP is available with the current config */
static bool indep_hp_possible(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct nid_path *path;
	int i, idx;

	if (cfg->line_out_type == AUTO_PIN_HP_OUT)
		idx = spec->out_paths[0];
	else
		idx = spec->hp_paths[0];
	path = snd_hda_get_path_from_idx(codec, idx);
	if (!path)
		return false;

	/* assume no path conflicts unless aamix is involved */
	if (!spec->mixer_nid || !is_nid_contained(path, spec->mixer_nid))
		return true;

	/* check whether output paths contain aamix */
	for (i = 0; i < cfg->line_outs; i++) {
		if (spec->out_paths[i] == idx)
			break;
		path = snd_hda_get_path_from_idx(codec, spec->out_paths[i]);
		if (path && is_nid_contained(path, spec->mixer_nid))
			return false;
	}
	for (i = 0; i < cfg->speaker_outs; i++) {
		path = snd_hda_get_path_from_idx(codec, spec->speaker_paths[i]);
		if (path && is_nid_contained(path, spec->mixer_nid))
			return false;
	}

	return true;
}

/* fill the empty entries in the dac array for speaker/hp with the
 * shared dac pointed by the paths
 */
static void refill_shared_dacs(struct hda_codec *codec, int num_outs,
			       hda_nid_t *dacs, int *path_idx)
{
	struct nid_path *path;
	int i;

	for (i = 0; i < num_outs; i++) {
		if (dacs[i])
			continue;
		path = snd_hda_get_path_from_idx(codec, path_idx[i]);
		if (!path)
			continue;
		dacs[i] = path->path[0];
	}
}

/* fill in the dac_nids table from the parsed pin configuration */
static int fill_and_eval_dacs(struct hda_codec *codec,
			      bool fill_hardwired,
			      bool fill_mio_first)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, err, badness;

	/* set num_dacs once to full for look_for_dac() */
	spec->multiout.num_dacs = cfg->line_outs;
	spec->multiout.dac_nids = spec->private_dac_nids;
	memset(spec->private_dac_nids, 0, sizeof(spec->private_dac_nids));
	memset(spec->multiout.hp_out_nid, 0, sizeof(spec->multiout.hp_out_nid));
	memset(spec->multiout.extra_out_nid, 0, sizeof(spec->multiout.extra_out_nid));
	spec->multi_ios = 0;
	snd_array_free(&spec->paths);

	/* clear path indices */
	memset(spec->out_paths, 0, sizeof(spec->out_paths));
	memset(spec->hp_paths, 0, sizeof(spec->hp_paths));
	memset(spec->speaker_paths, 0, sizeof(spec->speaker_paths));
	memset(spec->aamix_out_paths, 0, sizeof(spec->aamix_out_paths));
	memset(spec->digout_paths, 0, sizeof(spec->digout_paths));
	memset(spec->input_paths, 0, sizeof(spec->input_paths));
	memset(spec->loopback_paths, 0, sizeof(spec->loopback_paths));
	memset(&spec->digin_path, 0, sizeof(spec->digin_path));

	badness = 0;

	/* fill hard-wired DACs first */
	if (fill_hardwired) {
		bool mapped;
		do {
			mapped = map_singles(codec, cfg->line_outs,
					     cfg->line_out_pins,
					     spec->private_dac_nids,
					     spec->out_paths);
			mapped |= map_singles(codec, cfg->hp_outs,
					      cfg->hp_pins,
					      spec->multiout.hp_out_nid,
					      spec->hp_paths);
			mapped |= map_singles(codec, cfg->speaker_outs,
					      cfg->speaker_pins,
					      spec->multiout.extra_out_nid,
					      spec->speaker_paths);
			if (!spec->no_multi_io &&
			    fill_mio_first && cfg->line_outs == 1 &&
			    cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
				err = fill_multi_ios(codec, cfg->line_out_pins[0], true);
				if (!err)
					mapped = true;
			}
		} while (mapped);
	}

	badness += try_assign_dacs(codec, cfg->line_outs, cfg->line_out_pins,
				   spec->private_dac_nids, spec->out_paths,
				   spec->main_out_badness);

	if (!spec->no_multi_io && fill_mio_first &&
	    cfg->line_outs == 1 && cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		/* try to fill multi-io first */
		err = fill_multi_ios(codec, cfg->line_out_pins[0], false);
		if (err < 0)
			return err;
		/* we don't count badness at this stage yet */
	}

	if (cfg->line_out_type != AUTO_PIN_HP_OUT) {
		err = try_assign_dacs(codec, cfg->hp_outs, cfg->hp_pins,
				      spec->multiout.hp_out_nid,
				      spec->hp_paths,
				      spec->extra_out_badness);
		if (err < 0)
			return err;
		badness += err;
	}
	if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		err = try_assign_dacs(codec, cfg->speaker_outs,
				      cfg->speaker_pins,
				      spec->multiout.extra_out_nid,
				      spec->speaker_paths,
				      spec->extra_out_badness);
		if (err < 0)
			return err;
		badness += err;
	}
	if (!spec->no_multi_io &&
	    cfg->line_outs == 1 && cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		err = fill_multi_ios(codec, cfg->line_out_pins[0], false);
		if (err < 0)
			return err;
		badness += err;
	}

	if (spec->mixer_nid) {
		spec->aamix_out_paths[0] =
			check_aamix_out_path(codec, spec->out_paths[0]);
		if (cfg->line_out_type != AUTO_PIN_HP_OUT)
			spec->aamix_out_paths[1] =
				check_aamix_out_path(codec, spec->hp_paths[0]);
		if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT)
			spec->aamix_out_paths[2] =
				check_aamix_out_path(codec, spec->speaker_paths[0]);
	}

	if (!spec->no_multi_io &&
	    cfg->hp_outs && cfg->line_out_type == AUTO_PIN_SPEAKER_OUT)
		if (count_multiio_pins(codec, cfg->hp_pins[0]) >= 2)
			spec->multi_ios = 1; /* give badness */

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

	spec->ext_channel_count = spec->min_channel_count =
		spec->multiout.num_dacs * 2;

	if (spec->multi_ios == 2) {
		for (i = 0; i < 2; i++)
			spec->private_dac_nids[spec->multiout.num_dacs++] =
				spec->multi_io[i].dac;
	} else if (spec->multi_ios) {
		spec->multi_ios = 0;
		badness += BAD_MULTI_IO;
	}

	if (spec->indep_hp && !indep_hp_possible(codec))
		badness += BAD_NO_INDEP_HP;

	/* re-fill the shared DAC for speaker / headphone */
	if (cfg->line_out_type != AUTO_PIN_HP_OUT)
		refill_shared_dacs(codec, cfg->hp_outs,
				   spec->multiout.hp_out_nid,
				   spec->hp_paths);
	if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT)
		refill_shared_dacs(codec, cfg->speaker_outs,
				   spec->multiout.extra_out_nid,
				   spec->speaker_paths);

	return badness;
}

#define DEBUG_BADNESS

#ifdef DEBUG_BADNESS
#define debug_badness(fmt, ...)						\
	codec_dbg(codec, fmt, ##__VA_ARGS__)
#else
#define debug_badness(fmt, ...)						\
	do { if (0) codec_dbg(codec, fmt, ##__VA_ARGS__); } while (0)
#endif

#ifdef DEBUG_BADNESS
static inline void print_nid_path_idx(struct hda_codec *codec,
				      const char *pfx, int idx)
{
	struct nid_path *path;

	path = snd_hda_get_path_from_idx(codec, idx);
	if (path)
		print_nid_path(codec, pfx, path);
}

static void debug_show_configs(struct hda_codec *codec,
			       struct auto_pin_cfg *cfg)
{
	struct hda_gen_spec *spec = codec->spec;
	static const char * const lo_type[3] = { "LO", "SP", "HP" };
	int i;

	debug_badness("multi_outs = %x/%x/%x/%x : %x/%x/%x/%x (type %s)\n",
		      cfg->line_out_pins[0], cfg->line_out_pins[1],
		      cfg->line_out_pins[2], cfg->line_out_pins[3],
		      spec->multiout.dac_nids[0],
		      spec->multiout.dac_nids[1],
		      spec->multiout.dac_nids[2],
		      spec->multiout.dac_nids[3],
		      lo_type[cfg->line_out_type]);
	for (i = 0; i < cfg->line_outs; i++)
		print_nid_path_idx(codec, "  out", spec->out_paths[i]);
	if (spec->multi_ios > 0)
		debug_badness("multi_ios(%d) = %x/%x : %x/%x\n",
			      spec->multi_ios,
			      spec->multi_io[0].pin, spec->multi_io[1].pin,
			      spec->multi_io[0].dac, spec->multi_io[1].dac);
	for (i = 0; i < spec->multi_ios; i++)
		print_nid_path_idx(codec, "  mio",
				   spec->out_paths[cfg->line_outs + i]);
	if (cfg->hp_outs)
		debug_badness("hp_outs = %x/%x/%x/%x : %x/%x/%x/%x\n",
		      cfg->hp_pins[0], cfg->hp_pins[1],
		      cfg->hp_pins[2], cfg->hp_pins[3],
		      spec->multiout.hp_out_nid[0],
		      spec->multiout.hp_out_nid[1],
		      spec->multiout.hp_out_nid[2],
		      spec->multiout.hp_out_nid[3]);
	for (i = 0; i < cfg->hp_outs; i++)
		print_nid_path_idx(codec, "  hp ", spec->hp_paths[i]);
	if (cfg->speaker_outs)
		debug_badness("spk_outs = %x/%x/%x/%x : %x/%x/%x/%x\n",
		      cfg->speaker_pins[0], cfg->speaker_pins[1],
		      cfg->speaker_pins[2], cfg->speaker_pins[3],
		      spec->multiout.extra_out_nid[0],
		      spec->multiout.extra_out_nid[1],
		      spec->multiout.extra_out_nid[2],
		      spec->multiout.extra_out_nid[3]);
	for (i = 0; i < cfg->speaker_outs; i++)
		print_nid_path_idx(codec, "  spk", spec->speaker_paths[i]);
	for (i = 0; i < 3; i++)
		print_nid_path_idx(codec, "  mix", spec->aamix_out_paths[i]);
}
#else
#define debug_show_configs(codec, cfg) /* NOP */
#endif

/* find all available DACs of the codec */
static void fill_all_dac_nids(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	hda_nid_t nid = codec->start_nid;

	spec->num_all_dacs = 0;
	memset(spec->all_dacs, 0, sizeof(spec->all_dacs));
	for (i = 0; i < codec->num_nodes; i++, nid++) {
		if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_AUD_OUT)
			continue;
		if (spec->num_all_dacs >= ARRAY_SIZE(spec->all_dacs)) {
			codec_err(codec, "Too many DACs!\n");
			break;
		}
		spec->all_dacs[spec->num_all_dacs++] = nid;
	}
}

static int parse_output_paths(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct auto_pin_cfg *best_cfg;
	unsigned int val;
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
		debug_show_configs(codec, cfg);
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
		debug_badness("==> restoring best_cfg\n");
		*cfg = *best_cfg;
		fill_and_eval_dacs(codec, best_wired, best_mio);
	}
	debug_badness("==> Best config: lo_type=%d, wired=%d, mio=%d\n",
		      cfg->line_out_type, best_wired, best_mio);
	debug_show_configs(codec, cfg);

	if (cfg->line_out_pins[0]) {
		struct nid_path *path;
		path = snd_hda_get_path_from_idx(codec, spec->out_paths[0]);
		if (path)
			spec->vmaster_nid = look_for_out_vol_nid(codec, path);
		if (spec->vmaster_nid) {
			snd_hda_set_vmaster_tlv(codec, spec->vmaster_nid,
						HDA_OUTPUT, spec->vmaster_tlv);
			if (spec->dac_min_mute)
				spec->vmaster_tlv[3] |= TLV_DB_SCALE_MUTE;
		}
	}

	/* set initial pinctl targets */
	if (spec->prefer_hp_amp || cfg->line_out_type == AUTO_PIN_HP_OUT)
		val = PIN_HP;
	else
		val = PIN_OUT;
	set_pin_targets(codec, cfg->line_outs, cfg->line_out_pins, val);
	if (cfg->line_out_type != AUTO_PIN_HP_OUT)
		set_pin_targets(codec, cfg->hp_outs, cfg->hp_pins, PIN_HP);
	if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		val = spec->prefer_hp_amp ? PIN_HP : PIN_OUT;
		set_pin_targets(codec, cfg->speaker_outs,
				cfg->speaker_pins, val);
	}

	/* clear indep_hp flag if not available */
	if (spec->indep_hp && !indep_hp_possible(codec))
		spec->indep_hp = 0;

	kfree(best_cfg);
	return 0;
}

/* add playback controls from the parsed DAC table */
static int create_multi_out_ctls(struct hda_codec *codec,
				 const struct auto_pin_cfg *cfg)
{
	struct hda_gen_spec *spec = codec->spec;
	int i, err, noutputs;

	noutputs = cfg->line_outs;
	if (spec->multi_ios > 0 && cfg->line_outs < 3)
		noutputs += spec->multi_ios;

	for (i = 0; i < noutputs; i++) {
		const char *name;
		int index;
		struct nid_path *path;

		path = snd_hda_get_path_from_idx(codec, spec->out_paths[i]);
		if (!path)
			continue;

		name = get_line_out_pfx(codec, i, &index, NID_PATH_VOL_CTL);
		if (!name || !strcmp(name, "CLFE")) {
			/* Center/LFE */
			err = add_vol_ctl(codec, "Center", 0, 1, path);
			if (err < 0)
				return err;
			err = add_vol_ctl(codec, "LFE", 0, 2, path);
			if (err < 0)
				return err;
		} else {
			err = add_stereo_vol(codec, name, index, path);
			if (err < 0)
				return err;
		}

		name = get_line_out_pfx(codec, i, &index, NID_PATH_MUTE_CTL);
		if (!name || !strcmp(name, "CLFE")) {
			err = add_sw_ctl(codec, "Center", 0, 1, path);
			if (err < 0)
				return err;
			err = add_sw_ctl(codec, "LFE", 0, 2, path);
			if (err < 0)
				return err;
		} else {
			err = add_stereo_sw(codec, name, index, path);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int create_extra_out(struct hda_codec *codec, int path_idx,
			    const char *pfx, int cidx)
{
	struct nid_path *path;
	int err;

	path = snd_hda_get_path_from_idx(codec, path_idx);
	if (!path)
		return 0;
	err = add_stereo_vol(codec, pfx, cidx, path);
	if (err < 0)
		return err;
	err = add_stereo_sw(codec, pfx, cidx, path);
	if (err < 0)
		return err;
	return 0;
}

/* add playback controls for speaker and HP outputs */
static int create_extra_outs(struct hda_codec *codec, int num_pins,
			     const int *paths, const char *pfx)
{
	int i;

	for (i = 0; i < num_pins; i++) {
		const char *name;
		char tmp[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
		int err, idx = 0;

		if (num_pins == 2 && i == 1 && !strcmp(pfx, "Speaker"))
			name = "Bass Speaker";
		else if (num_pins >= 3) {
			snprintf(tmp, sizeof(tmp), "%s %s",
				 pfx, channel_name[i]);
			name = tmp;
		} else {
			name = pfx;
			idx = i;
		}
		err = create_extra_out(codec, paths[i], name, idx);
		if (err < 0)
			return err;
	}
	return 0;
}

static int create_hp_out_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	return create_extra_outs(codec, spec->autocfg.hp_outs,
				 spec->hp_paths,
				 "Headphone");
}

static int create_speaker_out_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	return create_extra_outs(codec, spec->autocfg.speaker_outs,
				 spec->speaker_paths,
				 "Speaker");
}

/*
 * independent HP controls
 */

static void call_hp_automute(struct hda_codec *codec,
			     struct hda_jack_callback *jack);
static int indep_hp_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	return snd_hda_enum_bool_helper_info(kcontrol, uinfo);
}

static int indep_hp_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = spec->indep_hp_enabled;
	return 0;
}

static void update_aamix_paths(struct hda_codec *codec, bool do_mix,
			       int nomix_path_idx, int mix_path_idx,
			       int out_type);

static int indep_hp_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	unsigned int select = ucontrol->value.enumerated.item[0];
	int ret = 0;

	mutex_lock(&spec->pcm_mutex);
	if (spec->active_streams) {
		ret = -EBUSY;
		goto unlock;
	}

	if (spec->indep_hp_enabled != select) {
		hda_nid_t *dacp;
		if (spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)
			dacp = &spec->private_dac_nids[0];
		else
			dacp = &spec->multiout.hp_out_nid[0];

		/* update HP aamix paths in case it conflicts with indep HP */
		if (spec->have_aamix_ctl) {
			if (spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)
				update_aamix_paths(codec, spec->aamix_mode,
						   spec->out_paths[0],
						   spec->aamix_out_paths[0],
						   spec->autocfg.line_out_type);
			else
				update_aamix_paths(codec, spec->aamix_mode,
						   spec->hp_paths[0],
						   spec->aamix_out_paths[1],
						   AUTO_PIN_HP_OUT);
		}

		spec->indep_hp_enabled = select;
		if (spec->indep_hp_enabled)
			*dacp = 0;
		else
			*dacp = spec->alt_dac_nid;

		call_hp_automute(codec, NULL);
		ret = 1;
	}
 unlock:
	mutex_unlock(&spec->pcm_mutex);
	return ret;
}

static const struct snd_kcontrol_new indep_hp_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Independent HP",
	.info = indep_hp_info,
	.get = indep_hp_get,
	.put = indep_hp_put,
};


static int create_indep_hp_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t dac;

	if (!spec->indep_hp)
		return 0;
	if (spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)
		dac = spec->multiout.dac_nids[0];
	else
		dac = spec->multiout.hp_out_nid[0];
	if (!dac) {
		spec->indep_hp = 0;
		return 0;
	}

	spec->indep_hp_enabled = false;
	spec->alt_dac_nid = dac;
	if (!snd_hda_gen_add_kctl(spec, NULL, &indep_hp_ctl))
		return -ENOMEM;
	return 0;
}

/*
 * channel mode enum control
 */

static int ch_mode_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	int chs;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->multi_ios + 1;
	if (uinfo->value.enumerated.item > spec->multi_ios)
		uinfo->value.enumerated.item = spec->multi_ios;
	chs = uinfo->value.enumerated.item * 2 + spec->min_channel_count;
	sprintf(uinfo->value.enumerated.name, "%dch", chs);
	return 0;
}

static int ch_mode_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] =
		(spec->ext_channel_count - spec->min_channel_count) / 2;
	return 0;
}

static inline struct nid_path *
get_multiio_path(struct hda_codec *codec, int idx)
{
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_get_path_from_idx(codec,
		spec->out_paths[spec->autocfg.line_outs + idx]);
}

static void update_automute_all(struct hda_codec *codec);

/* Default value to be passed as aamix argument for snd_hda_activate_path();
 * used for output paths
 */
static bool aamix_default(struct hda_gen_spec *spec)
{
	return !spec->have_aamix_ctl || spec->aamix_mode;
}

static int set_multi_io(struct hda_codec *codec, int idx, bool output)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t nid = spec->multi_io[idx].pin;
	struct nid_path *path;

	path = get_multiio_path(codec, idx);
	if (!path)
		return -EINVAL;

	if (path->active == output)
		return 0;

	if (output) {
		set_pin_target(codec, nid, PIN_OUT, true);
		snd_hda_activate_path(codec, path, true, aamix_default(spec));
		set_pin_eapd(codec, nid, true);
	} else {
		set_pin_eapd(codec, nid, false);
		snd_hda_activate_path(codec, path, false, aamix_default(spec));
		set_pin_target(codec, nid, spec->multi_io[idx].ctl_in, true);
		path_power_down_sync(codec, path);
	}

	/* update jack retasking in case it modifies any of them */
	update_automute_all(codec);

	return 0;
}

static int ch_mode_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	int i, ch;

	ch = ucontrol->value.enumerated.item[0];
	if (ch < 0 || ch > spec->multi_ios)
		return -EINVAL;
	if (ch == (spec->ext_channel_count - spec->min_channel_count) / 2)
		return 0;
	spec->ext_channel_count = ch * 2 + spec->min_channel_count;
	for (i = 0; i < spec->multi_ios; i++)
		set_multi_io(codec, i, i < ch);
	spec->multiout.max_channels = max(spec->ext_channel_count,
					  spec->const_channel_count);
	if (spec->need_dac_fix)
		spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	return 1;
}

static const struct snd_kcontrol_new channel_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Channel Mode",
	.info = ch_mode_info,
	.get = ch_mode_get,
	.put = ch_mode_put,
};

static int create_multi_channel_mode(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	if (spec->multi_ios > 0) {
		if (!snd_hda_gen_add_kctl(spec, NULL, &channel_mode_enum))
			return -ENOMEM;
	}
	return 0;
}

/*
 * aamix loopback enable/disable switch
 */

#define loopback_mixing_info	indep_hp_info

static int loopback_mixing_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = spec->aamix_mode;
	return 0;
}

static void update_aamix_paths(struct hda_codec *codec, bool do_mix,
			       int nomix_path_idx, int mix_path_idx,
			       int out_type)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *nomix_path, *mix_path;

	nomix_path = snd_hda_get_path_from_idx(codec, nomix_path_idx);
	mix_path = snd_hda_get_path_from_idx(codec, mix_path_idx);
	if (!nomix_path || !mix_path)
		return;

	/* if HP aamix path is driven from a different DAC and the
	 * independent HP mode is ON, can't turn on aamix path
	 */
	if (out_type == AUTO_PIN_HP_OUT && spec->indep_hp_enabled &&
	    mix_path->path[0] != spec->alt_dac_nid)
		do_mix = false;

	if (do_mix) {
		snd_hda_activate_path(codec, nomix_path, false, true);
		snd_hda_activate_path(codec, mix_path, true, true);
		path_power_down_sync(codec, nomix_path);
	} else {
		snd_hda_activate_path(codec, mix_path, false, false);
		snd_hda_activate_path(codec, nomix_path, true, false);
		path_power_down_sync(codec, mix_path);
	}
}

static int loopback_mixing_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	unsigned int val = ucontrol->value.enumerated.item[0];

	if (val == spec->aamix_mode)
		return 0;
	spec->aamix_mode = val;
	update_aamix_paths(codec, val, spec->out_paths[0],
			   spec->aamix_out_paths[0],
			   spec->autocfg.line_out_type);
	update_aamix_paths(codec, val, spec->hp_paths[0],
			   spec->aamix_out_paths[1],
			   AUTO_PIN_HP_OUT);
	update_aamix_paths(codec, val, spec->speaker_paths[0],
			   spec->aamix_out_paths[2],
			   AUTO_PIN_SPEAKER_OUT);
	return 1;
}

static const struct snd_kcontrol_new loopback_mixing_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Loopback Mixing",
	.info = loopback_mixing_info,
	.get = loopback_mixing_get,
	.put = loopback_mixing_put,
};

static int create_loopback_mixing_ctl(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	if (!spec->mixer_nid)
		return 0;
	if (!(spec->aamix_out_paths[0] || spec->aamix_out_paths[1] ||
	      spec->aamix_out_paths[2]))
		return 0;
	if (!snd_hda_gen_add_kctl(spec, NULL, &loopback_mixing_enum))
		return -ENOMEM;
	spec->have_aamix_ctl = 1;
	return 0;
}

/*
 * shared headphone/mic handling
 */

static void call_update_outputs(struct hda_codec *codec);

/* for shared I/O, change the pin-control accordingly */
static void update_hp_mic(struct hda_codec *codec, int adc_mux, bool force)
{
	struct hda_gen_spec *spec = codec->spec;
	bool as_mic;
	unsigned int val;
	hda_nid_t pin;

	pin = spec->hp_mic_pin;
	as_mic = spec->cur_mux[adc_mux] == spec->hp_mic_mux_idx;

	if (!force) {
		val = snd_hda_codec_get_pin_target(codec, pin);
		if (as_mic) {
			if (val & PIN_IN)
				return;
		} else {
			if (val & PIN_OUT)
				return;
		}
	}

	val = snd_hda_get_default_vref(codec, pin);
	/* if the HP pin doesn't support VREF and the codec driver gives an
	 * alternative pin, set up the VREF on that pin instead
	 */
	if (val == AC_PINCTL_VREF_HIZ && spec->shared_mic_vref_pin) {
		const hda_nid_t vref_pin = spec->shared_mic_vref_pin;
		unsigned int vref_val = snd_hda_get_default_vref(codec, vref_pin);
		if (vref_val != AC_PINCTL_VREF_HIZ)
			snd_hda_set_pin_ctl_cache(codec, vref_pin,
						  PIN_IN | (as_mic ? vref_val : 0));
	}

	if (!spec->hp_mic_jack_modes) {
		if (as_mic)
			val |= PIN_IN;
		else
			val = PIN_HP;
		set_pin_target(codec, pin, val, true);
		call_hp_automute(codec, NULL);
	}
}

/* create a shared input with the headphone out */
static int create_hp_mic(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int defcfg;
	hda_nid_t nid;

	if (!spec->hp_mic) {
		if (spec->suppress_hp_mic_detect)
			return 0;
		/* automatic detection: only if no input or a single internal
		 * input pin is found, try to detect the shared hp/mic
		 */
		if (cfg->num_inputs > 1)
			return 0;
		else if (cfg->num_inputs == 1) {
			defcfg = snd_hda_codec_get_pincfg(codec, cfg->inputs[0].pin);
			if (snd_hda_get_input_pin_attr(defcfg) != INPUT_PIN_ATTR_INT)
				return 0;
		}
	}

	spec->hp_mic = 0; /* clear once */
	if (cfg->num_inputs >= AUTO_CFG_MAX_INS)
		return 0;

	nid = 0;
	if (cfg->line_out_type == AUTO_PIN_HP_OUT && cfg->line_outs > 0)
		nid = cfg->line_out_pins[0];
	else if (cfg->hp_outs > 0)
		nid = cfg->hp_pins[0];
	if (!nid)
		return 0;

	if (!(snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_IN))
		return 0; /* no input */

	cfg->inputs[cfg->num_inputs].pin = nid;
	cfg->inputs[cfg->num_inputs].type = AUTO_PIN_MIC;
	cfg->inputs[cfg->num_inputs].is_headphone_mic = 1;
	cfg->num_inputs++;
	spec->hp_mic = 1;
	spec->hp_mic_pin = nid;
	/* we can't handle auto-mic together with HP-mic */
	spec->suppress_auto_mic = 1;
	codec_dbg(codec, "Enable shared I/O jack on NID 0x%x\n", nid);
	return 0;
}

/*
 * output jack mode
 */

static int create_hp_mic_jack_mode(struct hda_codec *codec, hda_nid_t pin);

static const char * const out_jack_texts[] = {
	"Line Out", "Headphone Out",
};

static int out_jack_mode_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	return snd_hda_enum_helper_info(kcontrol, uinfo, 2, out_jack_texts);
}

static int out_jack_mode_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	if (snd_hda_codec_get_pin_target(codec, nid) == PIN_HP)
		ucontrol->value.enumerated.item[0] = 1;
	else
		ucontrol->value.enumerated.item[0] = 0;
	return 0;
}

static int out_jack_mode_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int val;

	val = ucontrol->value.enumerated.item[0] ? PIN_HP : PIN_OUT;
	if (snd_hda_codec_get_pin_target(codec, nid) == val)
		return 0;
	snd_hda_set_pin_ctl_cache(codec, nid, val);
	return 1;
}

static const struct snd_kcontrol_new out_jack_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = out_jack_mode_info,
	.get = out_jack_mode_get,
	.put = out_jack_mode_put,
};

static bool find_kctl_name(struct hda_codec *codec, const char *name, int idx)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->kctls.used; i++) {
		struct snd_kcontrol_new *kctl = snd_array_elem(&spec->kctls, i);
		if (!strcmp(kctl->name, name) && kctl->index == idx)
			return true;
	}
	return false;
}

static void get_jack_mode_name(struct hda_codec *codec, hda_nid_t pin,
			       char *name, size_t name_len)
{
	struct hda_gen_spec *spec = codec->spec;
	int idx = 0;

	snd_hda_get_pin_label(codec, pin, &spec->autocfg, name, name_len, &idx);
	strlcat(name, " Jack Mode", name_len);

	for (; find_kctl_name(codec, name, idx); idx++)
		;
}

static int get_out_jack_num_items(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->add_jack_modes) {
		unsigned int pincap = snd_hda_query_pin_caps(codec, pin);
		if ((pincap & AC_PINCAP_OUT) && (pincap & AC_PINCAP_HP_DRV))
			return 2;
	}
	return 1;
}

static int create_out_jack_modes(struct hda_codec *codec, int num_pins,
				 hda_nid_t *pins)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < num_pins; i++) {
		hda_nid_t pin = pins[i];
		if (pin == spec->hp_mic_pin)
			continue;
		if (get_out_jack_num_items(codec, pin) > 1) {
			struct snd_kcontrol_new *knew;
			char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
			get_jack_mode_name(codec, pin, name, sizeof(name));
			knew = snd_hda_gen_add_kctl(spec, name,
						    &out_jack_mode_enum);
			if (!knew)
				return -ENOMEM;
			knew->private_value = pin;
		}
	}

	return 0;
}

/*
 * input jack mode
 */

/* from AC_PINCTL_VREF_HIZ to AC_PINCTL_VREF_100 */
#define NUM_VREFS	6

static const char * const vref_texts[NUM_VREFS] = {
	"Line In", "Mic 50pc Bias", "Mic 0V Bias",
	"", "Mic 80pc Bias", "Mic 100pc Bias"
};

static unsigned int get_vref_caps(struct hda_codec *codec, hda_nid_t pin)
{
	unsigned int pincap;

	pincap = snd_hda_query_pin_caps(codec, pin);
	pincap = (pincap & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
	/* filter out unusual vrefs */
	pincap &= ~(AC_PINCAP_VREF_GRD | AC_PINCAP_VREF_100);
	return pincap;
}

/* convert from the enum item index to the vref ctl index (0=HIZ, 1=50%...) */
static int get_vref_idx(unsigned int vref_caps, unsigned int item_idx)
{
	unsigned int i, n = 0;

	for (i = 0; i < NUM_VREFS; i++) {
		if (vref_caps & (1 << i)) {
			if (n == item_idx)
				return i;
			n++;
		}
	}
	return 0;
}

/* convert back from the vref ctl index to the enum item index */
static int cvt_from_vref_idx(unsigned int vref_caps, unsigned int idx)
{
	unsigned int i, n = 0;

	for (i = 0; i < NUM_VREFS; i++) {
		if (i == idx)
			return n;
		if (vref_caps & (1 << i))
			n++;
	}
	return 0;
}

static int in_jack_mode_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int vref_caps = get_vref_caps(codec, nid);

	snd_hda_enum_helper_info(kcontrol, uinfo, hweight32(vref_caps),
				 vref_texts);
	/* set the right text */
	strcpy(uinfo->value.enumerated.name,
	       vref_texts[get_vref_idx(vref_caps, uinfo->value.enumerated.item)]);
	return 0;
}

static int in_jack_mode_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int vref_caps = get_vref_caps(codec, nid);
	unsigned int idx;

	idx = snd_hda_codec_get_pin_target(codec, nid) & AC_PINCTL_VREFEN;
	ucontrol->value.enumerated.item[0] = cvt_from_vref_idx(vref_caps, idx);
	return 0;
}

static int in_jack_mode_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int vref_caps = get_vref_caps(codec, nid);
	unsigned int val, idx;

	val = snd_hda_codec_get_pin_target(codec, nid);
	idx = cvt_from_vref_idx(vref_caps, val & AC_PINCTL_VREFEN);
	if (idx == ucontrol->value.enumerated.item[0])
		return 0;

	val &= ~AC_PINCTL_VREFEN;
	val |= get_vref_idx(vref_caps, ucontrol->value.enumerated.item[0]);
	snd_hda_set_pin_ctl_cache(codec, nid, val);
	return 1;
}

static const struct snd_kcontrol_new in_jack_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = in_jack_mode_info,
	.get = in_jack_mode_get,
	.put = in_jack_mode_put,
};

static int get_in_jack_num_items(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	int nitems = 0;
	if (spec->add_jack_modes)
		nitems = hweight32(get_vref_caps(codec, pin));
	return nitems ? nitems : 1;
}

static int create_in_jack_mode(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	unsigned int defcfg;

	if (pin == spec->hp_mic_pin)
		return 0; /* already done in create_out_jack_mode() */

	/* no jack mode for fixed pins */
	defcfg = snd_hda_codec_get_pincfg(codec, pin);
	if (snd_hda_get_input_pin_attr(defcfg) == INPUT_PIN_ATTR_INT)
		return 0;

	/* no multiple vref caps? */
	if (get_in_jack_num_items(codec, pin) <= 1)
		return 0;

	get_jack_mode_name(codec, pin, name, sizeof(name));
	knew = snd_hda_gen_add_kctl(spec, name, &in_jack_mode_enum);
	if (!knew)
		return -ENOMEM;
	knew->private_value = pin;
	return 0;
}

/*
 * HP/mic shared jack mode
 */
static int hp_mic_jack_mode_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	int out_jacks = get_out_jack_num_items(codec, nid);
	int in_jacks = get_in_jack_num_items(codec, nid);
	const char *text = NULL;
	int idx;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = out_jacks + in_jacks;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	idx = uinfo->value.enumerated.item;
	if (idx < out_jacks) {
		if (out_jacks > 1)
			text = out_jack_texts[idx];
		else
			text = "Headphone Out";
	} else {
		idx -= out_jacks;
		if (in_jacks > 1) {
			unsigned int vref_caps = get_vref_caps(codec, nid);
			text = vref_texts[get_vref_idx(vref_caps, idx)];
		} else
			text = "Mic In";
	}

	strcpy(uinfo->value.enumerated.name, text);
	return 0;
}

static int get_cur_hp_mic_jack_mode(struct hda_codec *codec, hda_nid_t nid)
{
	int out_jacks = get_out_jack_num_items(codec, nid);
	int in_jacks = get_in_jack_num_items(codec, nid);
	unsigned int val = snd_hda_codec_get_pin_target(codec, nid);
	int idx = 0;

	if (val & PIN_OUT) {
		if (out_jacks > 1 && val == PIN_HP)
			idx = 1;
	} else if (val & PIN_IN) {
		idx = out_jacks;
		if (in_jacks > 1) {
			unsigned int vref_caps = get_vref_caps(codec, nid);
			val &= AC_PINCTL_VREFEN;
			idx += cvt_from_vref_idx(vref_caps, val);
		}
	}
	return idx;
}

static int hp_mic_jack_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	ucontrol->value.enumerated.item[0] =
		get_cur_hp_mic_jack_mode(codec, nid);
	return 0;
}

static int hp_mic_jack_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	int out_jacks = get_out_jack_num_items(codec, nid);
	int in_jacks = get_in_jack_num_items(codec, nid);
	unsigned int val, oldval, idx;

	oldval = get_cur_hp_mic_jack_mode(codec, nid);
	idx = ucontrol->value.enumerated.item[0];
	if (oldval == idx)
		return 0;

	if (idx < out_jacks) {
		if (out_jacks > 1)
			val = idx ? PIN_HP : PIN_OUT;
		else
			val = PIN_HP;
	} else {
		idx -= out_jacks;
		if (in_jacks > 1) {
			unsigned int vref_caps = get_vref_caps(codec, nid);
			val = snd_hda_codec_get_pin_target(codec, nid);
			val &= ~(AC_PINCTL_VREFEN | PIN_HP);
			val |= get_vref_idx(vref_caps, idx) | PIN_IN;
		} else
			val = snd_hda_get_default_vref(codec, nid) | PIN_IN;
	}
	snd_hda_set_pin_ctl_cache(codec, nid, val);
	call_hp_automute(codec, NULL);

	return 1;
}

static const struct snd_kcontrol_new hp_mic_jack_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = hp_mic_jack_mode_info,
	.get = hp_mic_jack_mode_get,
	.put = hp_mic_jack_mode_put,
};

static int create_hp_mic_jack_mode(struct hda_codec *codec, hda_nid_t pin)
{
	struct hda_gen_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;

	knew = snd_hda_gen_add_kctl(spec, "Headphone Mic Jack Mode",
				    &hp_mic_jack_mode_enum);
	if (!knew)
		return -ENOMEM;
	knew->private_value = pin;
	spec->hp_mic_jack_modes = 1;
	return 0;
}

/*
 * Parse input paths
 */

/* add the powersave loopback-list entry */
static int add_loopback_list(struct hda_gen_spec *spec, hda_nid_t mix, int idx)
{
	struct hda_amp_list *list;

	list = snd_array_new(&spec->loopback_list);
	if (!list)
		return -ENOMEM;
	list->nid = mix;
	list->dir = HDA_INPUT;
	list->idx = idx;
	spec->loopback.amplist = spec->loopback_list.list;
	return 0;
}

/* return true if either a volume or a mute amp is found for the given
 * aamix path; the amp has to be either in the mixer node or its direct leaf
 */
static bool look_for_mix_leaf_ctls(struct hda_codec *codec, hda_nid_t mix_nid,
				   hda_nid_t pin, unsigned int *mix_val,
				   unsigned int *mute_val)
{
	int idx, num_conns;
	const hda_nid_t *list;
	hda_nid_t nid;

	idx = snd_hda_get_conn_index(codec, mix_nid, pin, true);
	if (idx < 0)
		return false;

	*mix_val = *mute_val = 0;
	if (nid_has_volume(codec, mix_nid, HDA_INPUT))
		*mix_val = HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT);
	if (nid_has_mute(codec, mix_nid, HDA_INPUT))
		*mute_val = HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT);
	if (*mix_val && *mute_val)
		return true;

	/* check leaf node */
	num_conns = snd_hda_get_conn_list(codec, mix_nid, &list);
	if (num_conns < idx)
		return false;
	nid = list[idx];
	if (!*mix_val && nid_has_volume(codec, nid, HDA_OUTPUT) &&
	    !is_ctl_associated(codec, nid, HDA_OUTPUT, 0, NID_PATH_VOL_CTL))
		*mix_val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
	if (!*mute_val && nid_has_mute(codec, nid, HDA_OUTPUT) &&
	    !is_ctl_associated(codec, nid, HDA_OUTPUT, 0, NID_PATH_MUTE_CTL))
		*mute_val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);

	return *mix_val || *mute_val;
}

/* create input playback/capture controls for the given pin */
static int new_analog_input(struct hda_codec *codec, int input_idx,
			    hda_nid_t pin, const char *ctlname, int ctlidx,
			    hda_nid_t mix_nid)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;
	unsigned int mix_val, mute_val;
	int err, idx;

	if (!look_for_mix_leaf_ctls(codec, mix_nid, pin, &mix_val, &mute_val))
		return 0;

	path = snd_hda_add_new_path(codec, pin, mix_nid, 0);
	if (!path)
		return -EINVAL;
	print_nid_path(codec, "loopback", path);
	spec->loopback_paths[input_idx] = snd_hda_get_path_idx(codec, path);

	idx = path->idx[path->depth - 1];
	if (mix_val) {
		err = __add_pb_vol_ctrl(spec, HDA_CTL_WIDGET_VOL, ctlname, ctlidx, mix_val);
		if (err < 0)
			return err;
		path->ctls[NID_PATH_VOL_CTL] = mix_val;
	}

	if (mute_val) {
		err = __add_pb_sw_ctrl(spec, HDA_CTL_WIDGET_MUTE, ctlname, ctlidx, mute_val);
		if (err < 0)
			return err;
		path->ctls[NID_PATH_MUTE_CTL] = mute_val;
	}

	path->active = true;
	err = add_loopback_list(spec, mix_nid, idx);
	if (err < 0)
		return err;

	if (spec->mixer_nid != spec->mixer_merge_nid &&
	    !spec->loopback_merge_path) {
		path = snd_hda_add_new_path(codec, spec->mixer_nid,
					    spec->mixer_merge_nid, 0);
		if (path) {
			print_nid_path(codec, "loopback-merge", path);
			path->active = true;
			spec->loopback_merge_path =
				snd_hda_get_path_idx(codec, path);
		}
	}

	return 0;
}

static int is_input_pin(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int pincap = snd_hda_query_pin_caps(codec, nid);
	return (pincap & AC_PINCAP_IN) != 0;
}

/* Parse the codec tree and retrieve ADCs */
static int fill_adc_nids(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t nid;
	hda_nid_t *adc_nids = spec->adc_nids;
	int max_nums = ARRAY_SIZE(spec->adc_nids);
	int i, nums = 0;

	nid = codec->start_nid;
	for (i = 0; i < codec->num_nodes; i++, nid++) {
		unsigned int caps = get_wcaps(codec, nid);
		int type = get_wcaps_type(caps);

		if (type != AC_WID_AUD_IN || (caps & AC_WCAP_DIGITAL))
			continue;
		adc_nids[nums] = nid;
		if (++nums >= max_nums)
			break;
	}
	spec->num_adc_nids = nums;

	/* copy the detected ADCs to all_adcs[] */
	spec->num_all_adcs = nums;
	memcpy(spec->all_adcs, spec->adc_nids, nums * sizeof(hda_nid_t));

	return nums;
}

/* filter out invalid adc_nids that don't give all active input pins;
 * if needed, check whether dynamic ADC-switching is available
 */
static int check_dyn_adc_switch(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	unsigned int ok_bits;
	int i, n, nums;

	nums = 0;
	ok_bits = 0;
	for (n = 0; n < spec->num_adc_nids; n++) {
		for (i = 0; i < imux->num_items; i++) {
			if (!spec->input_paths[i][n])
				break;
		}
		if (i >= imux->num_items) {
			ok_bits |= (1 << n);
			nums++;
		}
	}

	if (!ok_bits) {
		/* check whether ADC-switch is possible */
		for (i = 0; i < imux->num_items; i++) {
			for (n = 0; n < spec->num_adc_nids; n++) {
				if (spec->input_paths[i][n]) {
					spec->dyn_adc_idx[i] = n;
					break;
				}
			}
		}

		codec_dbg(codec, "enabling ADC switching\n");
		spec->dyn_adc_switch = 1;
	} else if (nums != spec->num_adc_nids) {
		/* shrink the invalid adcs and input paths */
		nums = 0;
		for (n = 0; n < spec->num_adc_nids; n++) {
			if (!(ok_bits & (1 << n)))
				continue;
			if (n != nums) {
				spec->adc_nids[nums] = spec->adc_nids[n];
				for (i = 0; i < imux->num_items; i++) {
					invalidate_nid_path(codec,
						spec->input_paths[i][nums]);
					spec->input_paths[i][nums] =
						spec->input_paths[i][n];
				}
			}
			nums++;
		}
		spec->num_adc_nids = nums;
	}

	if (imux->num_items == 1 ||
	    (imux->num_items == 2 && spec->hp_mic)) {
		codec_dbg(codec, "reducing to a single ADC\n");
		spec->num_adc_nids = 1; /* reduce to a single ADC */
	}

	/* single index for individual volumes ctls */
	if (!spec->dyn_adc_switch && spec->multi_cap_vol)
		spec->num_adc_nids = 1;

	return 0;
}

/* parse capture source paths from the given pin and create imux items */
static int parse_capture_source(struct hda_codec *codec, hda_nid_t pin,
				int cfg_idx, int num_adcs,
				const char *label, int anchor)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	int imux_idx = imux->num_items;
	bool imux_added = false;
	int c;

	for (c = 0; c < num_adcs; c++) {
		struct nid_path *path;
		hda_nid_t adc = spec->adc_nids[c];

		if (!is_reachable_path(codec, pin, adc))
			continue;
		path = snd_hda_add_new_path(codec, pin, adc, anchor);
		if (!path)
			continue;
		print_nid_path(codec, "input", path);
		spec->input_paths[imux_idx][c] =
			snd_hda_get_path_idx(codec, path);

		if (!imux_added) {
			if (spec->hp_mic_pin == pin)
				spec->hp_mic_mux_idx = imux->num_items;
			spec->imux_pins[imux->num_items] = pin;
			snd_hda_add_imux_item(codec, imux, label, cfg_idx, NULL);
			imux_added = true;
			if (spec->dyn_adc_switch)
				spec->dyn_adc_idx[imux_idx] = c;
		}
	}

	return 0;
}

/*
 * create playback/capture controls for input pins
 */

/* fill the label for each input at first */
static int fill_input_pin_labels(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin = cfg->inputs[i].pin;
		const char *label;
		int j, idx;

		if (!is_input_pin(codec, pin))
			continue;

		label = hda_get_autocfg_input_label(codec, cfg, i);
		idx = 0;
		for (j = i - 1; j >= 0; j--) {
			if (spec->input_labels[j] &&
			    !strcmp(spec->input_labels[j], label)) {
				idx = spec->input_label_idxs[j] + 1;
				break;
			}
		}

		spec->input_labels[i] = label;
		spec->input_label_idxs[i] = idx;
	}

	return 0;
}

#define CFG_IDX_MIX	99	/* a dummy cfg->input idx for stereo mix */

static int create_input_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	int num_adcs;
	int i, err;
	unsigned int val;

	num_adcs = fill_adc_nids(codec);
	if (num_adcs < 0)
		return 0;

	err = fill_input_pin_labels(codec);
	if (err < 0)
		return err;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin;

		pin = cfg->inputs[i].pin;
		if (!is_input_pin(codec, pin))
			continue;

		val = PIN_IN;
		if (cfg->inputs[i].type == AUTO_PIN_MIC)
			val |= snd_hda_get_default_vref(codec, pin);
		if (pin != spec->hp_mic_pin)
			set_pin_target(codec, pin, val, false);

		if (mixer) {
			if (is_reachable_path(codec, pin, mixer)) {
				err = new_analog_input(codec, i, pin,
						       spec->input_labels[i],
						       spec->input_label_idxs[i],
						       mixer);
				if (err < 0)
					return err;
			}
		}

		err = parse_capture_source(codec, pin, i, num_adcs,
					   spec->input_labels[i], -mixer);
		if (err < 0)
			return err;

		if (spec->add_jack_modes) {
			err = create_in_jack_mode(codec, pin);
			if (err < 0)
				return err;
		}
	}

	/* add stereo mix when explicitly enabled via hint */
	if (mixer && spec->add_stereo_mix_input &&
	    snd_hda_get_bool_hint(codec, "add_stereo_mix_input") > 0) {
		err = parse_capture_source(codec, mixer, CFG_IDX_MIX, num_adcs,
					   "Stereo Mix", 0);
		if (err < 0)
			return err;
		else
			spec->suppress_auto_mic = 1;
	}

	return 0;
}


/*
 * input source mux
 */

/* get the input path specified by the given adc and imux indices */
static struct nid_path *get_input_path(struct hda_codec *codec, int adc_idx, int imux_idx)
{
	struct hda_gen_spec *spec = codec->spec;
	if (imux_idx < 0 || imux_idx >= HDA_MAX_NUM_INPUTS) {
		snd_BUG();
		return NULL;
	}
	if (spec->dyn_adc_switch)
		adc_idx = spec->dyn_adc_idx[imux_idx];
	if (adc_idx < 0 || adc_idx >= AUTO_CFG_MAX_INS) {
		snd_BUG();
		return NULL;
	}
	return snd_hda_get_path_from_idx(codec, spec->input_paths[imux_idx][adc_idx]);
}

static int mux_select(struct hda_codec *codec, unsigned int adc_idx,
		      unsigned int idx);

static int mux_enum_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_input_mux_info(&spec->input_mux, uinfo);
}

static int mux_enum_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	/* the ctls are created at once with multiple counts */
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int mux_enum_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	return mux_select(codec, adc_idx,
			  ucontrol->value.enumerated.item[0]);
}

static const struct snd_kcontrol_new cap_src_temp = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Input Source",
	.info = mux_enum_info,
	.get = mux_enum_get,
	.put = mux_enum_put,
};

/*
 * capture volume and capture switch ctls
 */

typedef int (*put_call_t)(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol);

/* call the given amp update function for all amps in the imux list at once */
static int cap_put_caller(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol,
			  put_call_t func, int type)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	const struct hda_input_mux *imux;
	struct nid_path *path;
	int i, adc_idx, err = 0;

	imux = &spec->input_mux;
	adc_idx = kcontrol->id.index;
	mutex_lock(&codec->control_mutex);
	/* we use the cache-only update at first since multiple input paths
	 * may shared the same amp; by updating only caches, the redundant
	 * writes to hardware can be reduced.
	 */
	codec->cached_write = 1;
	for (i = 0; i < imux->num_items; i++) {
		path = get_input_path(codec, adc_idx, i);
		if (!path || !path->ctls[type])
			continue;
		kcontrol->private_value = path->ctls[type];
		err = func(kcontrol, ucontrol);
		if (err < 0)
			goto error;
	}
 error:
	codec->cached_write = 0;
	mutex_unlock(&codec->control_mutex);
	snd_hda_codec_flush_cache(codec); /* flush the updates */
	if (err >= 0 && spec->cap_sync_hook)
		spec->cap_sync_hook(codec, kcontrol, ucontrol);
	return err;
}

/* capture volume ctl callbacks */
#define cap_vol_info		snd_hda_mixer_amp_volume_info
#define cap_vol_get		snd_hda_mixer_amp_volume_get
#define cap_vol_tlv		snd_hda_mixer_amp_tlv

static int cap_vol_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	return cap_put_caller(kcontrol, ucontrol,
			      snd_hda_mixer_amp_volume_put,
			      NID_PATH_VOL_CTL);
}

static const struct snd_kcontrol_new cap_vol_temp = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Volume",
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
		   SNDRV_CTL_ELEM_ACCESS_TLV_READ |
		   SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK),
	.info = cap_vol_info,
	.get = cap_vol_get,
	.put = cap_vol_put,
	.tlv = { .c = cap_vol_tlv },
};

/* capture switch ctl callbacks */
#define cap_sw_info		snd_ctl_boolean_stereo_info
#define cap_sw_get		snd_hda_mixer_amp_switch_get

static int cap_sw_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	return cap_put_caller(kcontrol, ucontrol,
			      snd_hda_mixer_amp_switch_put,
			      NID_PATH_MUTE_CTL);
}

static const struct snd_kcontrol_new cap_sw_temp = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Switch",
	.info = cap_sw_info,
	.get = cap_sw_get,
	.put = cap_sw_put,
};

static int parse_capvol_in_path(struct hda_codec *codec, struct nid_path *path)
{
	hda_nid_t nid;
	int i, depth;

	path->ctls[NID_PATH_VOL_CTL] = path->ctls[NID_PATH_MUTE_CTL] = 0;
	for (depth = 0; depth < 3; depth++) {
		if (depth >= path->depth)
			return -EINVAL;
		i = path->depth - depth - 1;
		nid = path->path[i];
		if (!path->ctls[NID_PATH_VOL_CTL]) {
			if (nid_has_volume(codec, nid, HDA_OUTPUT))
				path->ctls[NID_PATH_VOL_CTL] =
					HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
			else if (nid_has_volume(codec, nid, HDA_INPUT)) {
				int idx = path->idx[i];
				if (!depth && codec->single_adc_amp)
					idx = 0;
				path->ctls[NID_PATH_VOL_CTL] =
					HDA_COMPOSE_AMP_VAL(nid, 3, idx, HDA_INPUT);
			}
		}
		if (!path->ctls[NID_PATH_MUTE_CTL]) {
			if (nid_has_mute(codec, nid, HDA_OUTPUT))
				path->ctls[NID_PATH_MUTE_CTL] =
					HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
			else if (nid_has_mute(codec, nid, HDA_INPUT)) {
				int idx = path->idx[i];
				if (!depth && codec->single_adc_amp)
					idx = 0;
				path->ctls[NID_PATH_MUTE_CTL] =
					HDA_COMPOSE_AMP_VAL(nid, 3, idx, HDA_INPUT);
			}
		}
	}
	return 0;
}

static bool is_inv_dmic_pin(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int val;
	int i;

	if (!spec->inv_dmic_split)
		return false;
	for (i = 0; i < cfg->num_inputs; i++) {
		if (cfg->inputs[i].pin != nid)
			continue;
		if (cfg->inputs[i].type != AUTO_PIN_MIC)
			return false;
		val = snd_hda_codec_get_pincfg(codec, nid);
		return snd_hda_get_input_pin_attr(val) == INPUT_PIN_ATTR_INT;
	}
	return false;
}

/* capture switch put callback for a single control with hook call */
static int cap_single_sw_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	int ret;

	ret = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	if (spec->cap_sync_hook)
		spec->cap_sync_hook(codec, kcontrol, ucontrol);

	return ret;
}

static int add_single_cap_ctl(struct hda_codec *codec, const char *label,
			      int idx, bool is_switch, unsigned int ctl,
			      bool inv_dmic)
{
	struct hda_gen_spec *spec = codec->spec;
	char tmpname[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	int type = is_switch ? HDA_CTL_WIDGET_MUTE : HDA_CTL_WIDGET_VOL;
	const char *sfx = is_switch ? "Switch" : "Volume";
	unsigned int chs = inv_dmic ? 1 : 3;
	struct snd_kcontrol_new *knew;

	if (!ctl)
		return 0;

	if (label)
		snprintf(tmpname, sizeof(tmpname),
			 "%s Capture %s", label, sfx);
	else
		snprintf(tmpname, sizeof(tmpname),
			 "Capture %s", sfx);
	knew = add_control(spec, type, tmpname, idx,
			   amp_val_replace_channels(ctl, chs));
	if (!knew)
		return -ENOMEM;
	if (is_switch)
		knew->put = cap_single_sw_put;
	if (!inv_dmic)
		return 0;

	/* Make independent right kcontrol */
	if (label)
		snprintf(tmpname, sizeof(tmpname),
			 "Inverted %s Capture %s", label, sfx);
	else
		snprintf(tmpname, sizeof(tmpname),
			 "Inverted Capture %s", sfx);
	knew = add_control(spec, type, tmpname, idx,
			   amp_val_replace_channels(ctl, 2));
	if (!knew)
		return -ENOMEM;
	if (is_switch)
		knew->put = cap_single_sw_put;
	return 0;
}

/* create single (and simple) capture volume and switch controls */
static int create_single_cap_vol_ctl(struct hda_codec *codec, int idx,
				     unsigned int vol_ctl, unsigned int sw_ctl,
				     bool inv_dmic)
{
	int err;
	err = add_single_cap_ctl(codec, NULL, idx, false, vol_ctl, inv_dmic);
	if (err < 0)
		return err;
	err = add_single_cap_ctl(codec, NULL, idx, true, sw_ctl, inv_dmic);
	if (err < 0)
		return err;
	return 0;
}

/* create bound capture volume and switch controls */
static int create_bind_cap_vol_ctl(struct hda_codec *codec, int idx,
				   unsigned int vol_ctl, unsigned int sw_ctl)
{
	struct hda_gen_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;

	if (vol_ctl) {
		knew = snd_hda_gen_add_kctl(spec, NULL, &cap_vol_temp);
		if (!knew)
			return -ENOMEM;
		knew->index = idx;
		knew->private_value = vol_ctl;
		knew->subdevice = HDA_SUBDEV_AMP_FLAG;
	}
	if (sw_ctl) {
		knew = snd_hda_gen_add_kctl(spec, NULL, &cap_sw_temp);
		if (!knew)
			return -ENOMEM;
		knew->index = idx;
		knew->private_value = sw_ctl;
		knew->subdevice = HDA_SUBDEV_AMP_FLAG;
	}
	return 0;
}

/* return the vol ctl when used first in the imux list */
static unsigned int get_first_cap_ctl(struct hda_codec *codec, int idx, int type)
{
	struct nid_path *path;
	unsigned int ctl;
	int i;

	path = get_input_path(codec, 0, idx);
	if (!path)
		return 0;
	ctl = path->ctls[type];
	if (!ctl)
		return 0;
	for (i = 0; i < idx - 1; i++) {
		path = get_input_path(codec, 0, i);
		if (path && path->ctls[type] == ctl)
			return 0;
	}
	return ctl;
}

/* create individual capture volume and switch controls per input */
static int create_multi_cap_vol_ctl(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	int i, err, type;

	for (i = 0; i < imux->num_items; i++) {
		bool inv_dmic;
		int idx;

		idx = imux->items[i].index;
		if (idx >= spec->autocfg.num_inputs)
			continue;
		inv_dmic = is_inv_dmic_pin(codec, spec->imux_pins[i]);

		for (type = 0; type < 2; type++) {
			err = add_single_cap_ctl(codec,
						 spec->input_labels[idx],
						 spec->input_label_idxs[idx],
						 type,
						 get_first_cap_ctl(codec, i, type),
						 inv_dmic);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int create_capture_mixers(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	int i, n, nums, err;

	if (spec->dyn_adc_switch)
		nums = 1;
	else
		nums = spec->num_adc_nids;

	if (!spec->auto_mic && imux->num_items > 1) {
		struct snd_kcontrol_new *knew;
		const char *name;
		name = nums > 1 ? "Input Source" : "Capture Source";
		knew = snd_hda_gen_add_kctl(spec, name, &cap_src_temp);
		if (!knew)
			return -ENOMEM;
		knew->count = nums;
	}

	for (n = 0; n < nums; n++) {
		bool multi = false;
		bool multi_cap_vol = spec->multi_cap_vol;
		bool inv_dmic = false;
		int vol, sw;

		vol = sw = 0;
		for (i = 0; i < imux->num_items; i++) {
			struct nid_path *path;
			path = get_input_path(codec, n, i);
			if (!path)
				continue;
			parse_capvol_in_path(codec, path);
			if (!vol)
				vol = path->ctls[NID_PATH_VOL_CTL];
			else if (vol != path->ctls[NID_PATH_VOL_CTL]) {
				multi = true;
				if (!same_amp_caps(codec, vol,
				    path->ctls[NID_PATH_VOL_CTL], HDA_INPUT))
					multi_cap_vol = true;
			}
			if (!sw)
				sw = path->ctls[NID_PATH_MUTE_CTL];
			else if (sw != path->ctls[NID_PATH_MUTE_CTL]) {
				multi = true;
				if (!same_amp_caps(codec, sw,
				    path->ctls[NID_PATH_MUTE_CTL], HDA_INPUT))
					multi_cap_vol = true;
			}
			if (is_inv_dmic_pin(codec, spec->imux_pins[i]))
				inv_dmic = true;
		}

		if (!multi)
			err = create_single_cap_vol_ctl(codec, n, vol, sw,
							inv_dmic);
		else if (!multi_cap_vol && !inv_dmic)
			err = create_bind_cap_vol_ctl(codec, n, vol, sw);
		else
			err = create_multi_cap_vol_ctl(codec);
		if (err < 0)
			return err;
	}

	return 0;
}

/*
 * add mic boosts if needed
 */

/* check whether the given amp is feasible as a boost volume */
static bool check_boost_vol(struct hda_codec *codec, hda_nid_t nid,
			    int dir, int idx)
{
	unsigned int step;

	if (!nid_has_volume(codec, nid, dir) ||
	    is_ctl_associated(codec, nid, dir, idx, NID_PATH_VOL_CTL) ||
	    is_ctl_associated(codec, nid, dir, idx, NID_PATH_BOOST_CTL))
		return false;

	step = (query_amp_caps(codec, nid, dir) & AC_AMPCAP_STEP_SIZE)
		>> AC_AMPCAP_STEP_SIZE_SHIFT;
	if (step < 0x20)
		return false;
	return true;
}

/* look for a boost amp in a widget close to the pin */
static unsigned int look_for_boost_amp(struct hda_codec *codec,
				       struct nid_path *path)
{
	unsigned int val = 0;
	hda_nid_t nid;
	int depth;

	for (depth = 0; depth < 3; depth++) {
		if (depth >= path->depth - 1)
			break;
		nid = path->path[depth];
		if (depth && check_boost_vol(codec, nid, HDA_OUTPUT, 0)) {
			val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
			break;
		} else if (check_boost_vol(codec, nid, HDA_INPUT,
					   path->idx[depth])) {
			val = HDA_COMPOSE_AMP_VAL(nid, 3, path->idx[depth],
						  HDA_INPUT);
			break;
		}
	}

	return val;
}

static int parse_mic_boost(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct hda_input_mux *imux = &spec->input_mux;
	int i;

	if (!spec->num_adc_nids)
		return 0;

	for (i = 0; i < imux->num_items; i++) {
		struct nid_path *path;
		unsigned int val;
		int idx;
		char boost_label[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

		idx = imux->items[i].index;
		if (idx >= imux->num_items)
			continue;

		/* check only line-in and mic pins */
		if (cfg->inputs[idx].type > AUTO_PIN_LINE_IN)
			continue;

		path = get_input_path(codec, 0, i);
		if (!path)
			continue;

		val = look_for_boost_amp(codec, path);
		if (!val)
			continue;

		/* create a boost control */
		snprintf(boost_label, sizeof(boost_label),
			 "%s Boost Volume", spec->input_labels[idx]);
		if (!add_control(spec, HDA_CTL_WIDGET_VOL, boost_label,
				 spec->input_label_idxs[idx], val))
			return -ENOMEM;

		path->ctls[NID_PATH_BOOST_CTL] = val;
	}
	return 0;
}

/*
 * parse digital I/Os and set up NIDs in BIOS auto-parse mode
 */
static void parse_digital(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;
	int i, nums;
	hda_nid_t dig_nid, pin;

	/* support multiple SPDIFs; the secondary is set up as a slave */
	nums = 0;
	for (i = 0; i < spec->autocfg.dig_outs; i++) {
		pin = spec->autocfg.dig_out_pins[i];
		dig_nid = look_for_dac(codec, pin, true);
		if (!dig_nid)
			continue;
		path = snd_hda_add_new_path(codec, dig_nid, pin, 0);
		if (!path)
			continue;
		print_nid_path(codec, "digout", path);
		path->active = true;
		spec->digout_paths[i] = snd_hda_get_path_idx(codec, path);
		set_pin_target(codec, pin, PIN_OUT, false);
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
		pin = spec->autocfg.dig_in_pin;
		dig_nid = codec->start_nid;
		for (i = 0; i < codec->num_nodes; i++, dig_nid++) {
			unsigned int wcaps = get_wcaps(codec, dig_nid);
			if (get_wcaps_type(wcaps) != AC_WID_AUD_IN)
				continue;
			if (!(wcaps & AC_WCAP_DIGITAL))
				continue;
			path = snd_hda_add_new_path(codec, pin, dig_nid, 0);
			if (path) {
				print_nid_path(codec, "digin", path);
				path->active = true;
				spec->dig_in_nid = dig_nid;
				spec->digin_path = snd_hda_get_path_idx(codec, path);
				set_pin_target(codec, pin, PIN_IN, false);
				break;
			}
		}
	}
}


/*
 * input MUX handling
 */

static bool dyn_adc_pcm_resetup(struct hda_codec *codec, int cur);

/* select the given imux item; either unmute exclusively or select the route */
static int mux_select(struct hda_codec *codec, unsigned int adc_idx,
		      unsigned int idx)
{
	struct hda_gen_spec *spec = codec->spec;
	const struct hda_input_mux *imux;
	struct nid_path *old_path, *path;

	imux = &spec->input_mux;
	if (!imux->num_items)
		return 0;

	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (spec->cur_mux[adc_idx] == idx)
		return 0;

	old_path = get_input_path(codec, adc_idx, spec->cur_mux[adc_idx]);
	if (!old_path)
		return 0;
	if (old_path->active)
		snd_hda_activate_path(codec, old_path, false, false);

	spec->cur_mux[adc_idx] = idx;

	if (spec->hp_mic)
		update_hp_mic(codec, adc_idx, false);

	if (spec->dyn_adc_switch)
		dyn_adc_pcm_resetup(codec, idx);

	path = get_input_path(codec, adc_idx, idx);
	if (!path)
		return 0;
	if (path->active)
		return 0;
	snd_hda_activate_path(codec, path, true, false);
	if (spec->cap_sync_hook)
		spec->cap_sync_hook(codec, NULL, NULL);
	path_power_down_sync(codec, old_path);
	return 1;
}


/*
 * Jack detections for HP auto-mute and mic-switch
 */

/* check each pin in the given array; returns true if any of them is plugged */
static bool detect_jacks(struct hda_codec *codec, int num_pins, hda_nid_t *pins)
{
	int i;
	bool present = false;

	for (i = 0; i < num_pins; i++) {
		hda_nid_t nid = pins[i];
		if (!nid)
			break;
		/* don't detect pins retasked as inputs */
		if (snd_hda_codec_get_pin_target(codec, nid) & AC_PINCTL_IN_EN)
			continue;
		if (snd_hda_jack_detect_state(codec, nid) == HDA_JACK_PRESENT)
			present = true;
	}
	return present;
}

/* standard HP/line-out auto-mute helper */
static void do_automute(struct hda_codec *codec, int num_pins, hda_nid_t *pins,
			int *paths, bool mute)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < num_pins; i++) {
		hda_nid_t nid = pins[i];
		unsigned int val, oldval;
		if (!nid)
			break;

		if (spec->auto_mute_via_amp) {
			struct nid_path *path;
			hda_nid_t mute_nid;

			path = snd_hda_get_path_from_idx(codec, paths[i]);
			if (!path)
				continue;
			mute_nid = get_amp_nid_(path->ctls[NID_PATH_MUTE_CTL]);
			if (!mute_nid)
				continue;
			if (mute)
				spec->mute_bits |= (1ULL << mute_nid);
			else
				spec->mute_bits &= ~(1ULL << mute_nid);
			set_pin_eapd(codec, nid, !mute);
			continue;
		}

		oldval = snd_hda_codec_get_pin_target(codec, nid);
		if (oldval & PIN_IN)
			continue; /* no mute for inputs */
		/* don't reset VREF value in case it's controlling
		 * the amp (see alc861_fixup_asus_amp_vref_0f())
		 */
		if (spec->keep_vref_in_automute)
			val = oldval & ~PIN_HP;
		else
			val = 0;
		if (!mute)
			val |= oldval;
		/* here we call update_pin_ctl() so that the pinctl is changed
		 * without changing the pinctl target value;
		 * the original target value will be still referred at the
		 * init / resume again
		 */
		update_pin_ctl(codec, nid, val);
		set_pin_eapd(codec, nid, !mute);
	}
}

/**
 * snd_hda_gen_update_outputs - Toggle outputs muting
 * @codec: the HDA codec
 *
 * Update the mute status of all outputs based on the current jack states.
 */
void snd_hda_gen_update_outputs(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int *paths;
	int on;

	/* Control HP pins/amps depending on master_mute state;
	 * in general, HP pins/amps control should be enabled in all cases,
	 * but currently set only for master_mute, just to be safe
	 */
	if (spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)
		paths = spec->out_paths;
	else
		paths = spec->hp_paths;
	do_automute(codec, ARRAY_SIZE(spec->autocfg.hp_pins),
		    spec->autocfg.hp_pins, paths, spec->master_mute);

	if (!spec->automute_speaker)
		on = 0;
	else
		on = spec->hp_jack_present | spec->line_jack_present;
	on |= spec->master_mute;
	spec->speaker_muted = on;
	if (spec->autocfg.line_out_type == AUTO_PIN_SPEAKER_OUT)
		paths = spec->out_paths;
	else
		paths = spec->speaker_paths;
	do_automute(codec, ARRAY_SIZE(spec->autocfg.speaker_pins),
		    spec->autocfg.speaker_pins, paths, on);

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
	spec->line_out_muted = on;
	paths = spec->out_paths;
	do_automute(codec, ARRAY_SIZE(spec->autocfg.line_out_pins),
		    spec->autocfg.line_out_pins, paths, on);
}
EXPORT_SYMBOL_GPL(snd_hda_gen_update_outputs);

static void call_update_outputs(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->automute_hook)
		spec->automute_hook(codec);
	else
		snd_hda_gen_update_outputs(codec);

	/* sync the whole vmaster slaves to reflect the new auto-mute status */
	if (spec->auto_mute_via_amp && !codec->bus->shutdown)
		snd_ctl_sync_vmaster(spec->vmaster_mute.sw_kctl, false);
}

/**
 * snd_hda_gen_hp_automute - standard HP-automute helper
 * @codec: the HDA codec
 * @jack: jack object, NULL for the whole
 */
void snd_hda_gen_hp_automute(struct hda_codec *codec,
			     struct hda_jack_callback *jack)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t *pins = spec->autocfg.hp_pins;
	int num_pins = ARRAY_SIZE(spec->autocfg.hp_pins);

	/* No detection for the first HP jack during indep-HP mode */
	if (spec->indep_hp_enabled) {
		pins++;
		num_pins--;
	}

	spec->hp_jack_present = detect_jacks(codec, num_pins, pins);
	if (!spec->detect_hp || (!spec->automute_speaker && !spec->automute_lo))
		return;
	call_update_outputs(codec);
}
EXPORT_SYMBOL_GPL(snd_hda_gen_hp_automute);

/**
 * snd_hda_gen_line_automute - standard line-out-automute helper
 * @codec: the HDA codec
 * @jack: jack object, NULL for the whole
 */
void snd_hda_gen_line_automute(struct hda_codec *codec,
			       struct hda_jack_callback *jack)
{
	struct hda_gen_spec *spec = codec->spec;

	if (spec->autocfg.line_out_type == AUTO_PIN_SPEAKER_OUT)
		return;
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
EXPORT_SYMBOL_GPL(snd_hda_gen_line_automute);

/**
 * snd_hda_gen_mic_autoswitch - standard mic auto-switch helper
 * @codec: the HDA codec
 * @jack: jack object, NULL for the whole
 */
void snd_hda_gen_mic_autoswitch(struct hda_codec *codec,
				struct hda_jack_callback *jack)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	if (!spec->auto_mic)
		return;

	for (i = spec->am_num_entries - 1; i > 0; i--) {
		hda_nid_t pin = spec->am_entry[i].pin;
		/* don't detect pins retasked as outputs */
		if (snd_hda_codec_get_pin_target(codec, pin) & AC_PINCTL_OUT_EN)
			continue;
		if (snd_hda_jack_detect_state(codec, pin) == HDA_JACK_PRESENT) {
			mux_select(codec, 0, spec->am_entry[i].idx);
			return;
		}
	}
	mux_select(codec, 0, spec->am_entry[0].idx);
}
EXPORT_SYMBOL_GPL(snd_hda_gen_mic_autoswitch);

/* call appropriate hooks */
static void call_hp_automute(struct hda_codec *codec,
			     struct hda_jack_callback *jack)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->hp_automute_hook)
		spec->hp_automute_hook(codec, jack);
	else
		snd_hda_gen_hp_automute(codec, jack);
}

static void call_line_automute(struct hda_codec *codec,
			       struct hda_jack_callback *jack)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->line_automute_hook)
		spec->line_automute_hook(codec, jack);
	else
		snd_hda_gen_line_automute(codec, jack);
}

static void call_mic_autoswitch(struct hda_codec *codec,
				struct hda_jack_callback *jack)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->mic_autoswitch_hook)
		spec->mic_autoswitch_hook(codec, jack);
	else
		snd_hda_gen_mic_autoswitch(codec, jack);
}

/* update jack retasking */
static void update_automute_all(struct hda_codec *codec)
{
	call_hp_automute(codec, NULL);
	call_line_automute(codec, NULL);
	call_mic_autoswitch(codec, NULL);
}

/*
 * Auto-Mute mode mixer enum support
 */
static int automute_mode_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	static const char * const texts3[] = {
		"Disabled", "Speaker Only", "Line Out+Speaker"
	};

	if (spec->automute_speaker_possible && spec->automute_lo_possible)
		return snd_hda_enum_helper_info(kcontrol, uinfo, 3, texts3);
	return snd_hda_enum_bool_helper_info(kcontrol, uinfo);
}

static int automute_mode_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	unsigned int val = 0;
	if (spec->automute_speaker)
		val++;
	if (spec->automute_lo)
		val++;

	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int automute_mode_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;

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

static const struct snd_kcontrol_new automute_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Auto-Mute Mode",
	.info = automute_mode_info,
	.get = automute_mode_get,
	.put = automute_mode_put,
};

static int add_automute_mode_enum(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	if (!snd_hda_gen_add_kctl(spec, NULL, &automute_mode_enum))
		return -ENOMEM;
	return 0;
}

/*
 * Check the availability of HP/line-out auto-mute;
 * Set up appropriately if really supported
 */
static int check_auto_mute_availability(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int present = 0;
	int i, err;

	if (spec->suppress_auto_mute)
		return 0;

	if (cfg->hp_pins[0])
		present++;
	if (cfg->line_out_pins[0])
		present++;
	if (cfg->speaker_pins[0])
		present++;
	if (present < 2) /* need two different output types */
		return 0;

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
		codec_dbg(codec, "Enable HP auto-muting on NID 0x%x\n", nid);
		snd_hda_jack_detect_enable_callback(codec, nid,
						    call_hp_automute);
		spec->detect_hp = 1;
	}

	if (cfg->line_out_type == AUTO_PIN_LINE_OUT && cfg->line_outs) {
		if (cfg->speaker_outs)
			for (i = 0; i < cfg->line_outs; i++) {
				hda_nid_t nid = cfg->line_out_pins[i];
				if (!is_jack_detectable(codec, nid))
					continue;
				codec_dbg(codec, "Enable Line-Out auto-muting on NID 0x%x\n", nid);
				snd_hda_jack_detect_enable_callback(codec, nid,
								    call_line_automute);
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
		err = add_automute_mode_enum(codec);
		if (err < 0)
			return err;
	}
	return 0;
}

/* check whether all auto-mic pins are valid; setup indices if OK */
static bool auto_mic_check_imux(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	const struct hda_input_mux *imux;
	int i;

	imux = &spec->input_mux;
	for (i = 0; i < spec->am_num_entries; i++) {
		spec->am_entry[i].idx =
			find_idx_in_nid_list(spec->am_entry[i].pin,
					     spec->imux_pins, imux->num_items);
		if (spec->am_entry[i].idx < 0)
			return false; /* no corresponding imux */
	}

	/* we don't need the jack detection for the first pin */
	for (i = 1; i < spec->am_num_entries; i++)
		snd_hda_jack_detect_enable_callback(codec,
						    spec->am_entry[i].pin,
						    call_mic_autoswitch);
	return true;
}

static int compare_attr(const void *ap, const void *bp)
{
	const struct automic_entry *a = ap;
	const struct automic_entry *b = bp;
	return (int)(a->attr - b->attr);
}

/*
 * Check the availability of auto-mic switch;
 * Set up if really supported
 */
static int check_auto_mic_availability(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int types;
	int i, num_pins;

	if (spec->suppress_auto_mic)
		return 0;

	types = 0;
	num_pins = 0;
	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		unsigned int attr;
		attr = snd_hda_codec_get_pincfg(codec, nid);
		attr = snd_hda_get_input_pin_attr(attr);
		if (types & (1 << attr))
			return 0; /* already occupied */
		switch (attr) {
		case INPUT_PIN_ATTR_INT:
			if (cfg->inputs[i].type != AUTO_PIN_MIC)
				return 0; /* invalid type */
			break;
		case INPUT_PIN_ATTR_UNUSED:
			return 0; /* invalid entry */
		default:
			if (cfg->inputs[i].type > AUTO_PIN_LINE_IN)
				return 0; /* invalid type */
			if (!spec->line_in_auto_switch &&
			    cfg->inputs[i].type != AUTO_PIN_MIC)
				return 0; /* only mic is allowed */
			if (!is_jack_detectable(codec, nid))
				return 0; /* no unsol support */
			break;
		}
		if (num_pins >= MAX_AUTO_MIC_PINS)
			return 0;
		types |= (1 << attr);
		spec->am_entry[num_pins].pin = nid;
		spec->am_entry[num_pins].attr = attr;
		num_pins++;
	}

	if (num_pins < 2)
		return 0;

	spec->am_num_entries = num_pins;
	/* sort the am_entry in the order of attr so that the pin with a
	 * higher attr will be selected when the jack is plugged.
	 */
	sort(spec->am_entry, num_pins, sizeof(spec->am_entry[0]),
	     compare_attr, NULL);

	if (!auto_mic_check_imux(codec))
		return 0;

	spec->auto_mic = 1;
	spec->num_adc_nids = 1;
	spec->cur_mux[0] = spec->am_entry[0].idx;
	codec_dbg(codec, "Enable auto-mic switch on NID 0x%x/0x%x/0x%x\n",
		    spec->am_entry[0].pin,
		    spec->am_entry[1].pin,
		    spec->am_entry[2].pin);

	return 0;
}

/**
 * snd_hda_gen_path_power_filter - power_filter hook to make inactive widgets
 * into power down
 * @codec: the HDA codec
 * @nid: NID to evalute
 * @power_state: target power state
 */
unsigned int snd_hda_gen_path_power_filter(struct hda_codec *codec,
						  hda_nid_t nid,
						  unsigned int power_state)
{
	if (power_state != AC_PWRST_D0 || nid == codec->afg)
		return power_state;
	if (get_wcaps_type(get_wcaps(codec, nid)) >= AC_WID_POWER)
		return power_state;
	if (is_active_nid_for_any(codec, nid))
		return power_state;
	return AC_PWRST_D3;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_path_power_filter);

/* mute all aamix inputs initially; parse up to the first leaves */
static void mute_all_mixer_nid(struct hda_codec *codec, hda_nid_t mix)
{
	int i, nums;
	const hda_nid_t *conn;
	bool has_amp;

	nums = snd_hda_get_conn_list(codec, mix, &conn);
	has_amp = nid_has_mute(codec, mix, HDA_INPUT);
	for (i = 0; i < nums; i++) {
		if (has_amp)
			snd_hda_codec_amp_stereo(codec, mix,
						 HDA_INPUT, i,
						 0xff, HDA_AMP_MUTE);
		else if (nid_has_volume(codec, conn[i], HDA_OUTPUT))
			snd_hda_codec_amp_stereo(codec, conn[i],
						 HDA_OUTPUT, 0,
						 0xff, HDA_AMP_MUTE);
	}
}

/**
 * snd_hda_gen_parse_auto_config - Parse the given BIOS configuration and
 * set up the hda_gen_spec
 * @codec: the HDA codec
 * @cfg: Parsed pin configuration
 *
 * return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
int snd_hda_gen_parse_auto_config(struct hda_codec *codec,
				  struct auto_pin_cfg *cfg)
{
	struct hda_gen_spec *spec = codec->spec;
	int err;

	parse_user_hints(codec);

	if (spec->mixer_nid && !spec->mixer_merge_nid)
		spec->mixer_merge_nid = spec->mixer_nid;

	if (cfg != &spec->autocfg) {
		spec->autocfg = *cfg;
		cfg = &spec->autocfg;
	}

	if (!spec->main_out_badness)
		spec->main_out_badness = &hda_main_out_badness;
	if (!spec->extra_out_badness)
		spec->extra_out_badness = &hda_extra_out_badness;

	fill_all_dac_nids(codec);

	if (!cfg->line_outs) {
		if (cfg->dig_outs || cfg->dig_in_pin) {
			spec->multiout.max_channels = 2;
			spec->no_analog = 1;
			goto dig_only;
		}
		if (!cfg->num_inputs && !cfg->dig_in_pin)
			return 0; /* can't find valid BIOS pin config */
	}

	if (!spec->no_primary_hp &&
	    cfg->line_out_type == AUTO_PIN_SPEAKER_OUT &&
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

	err = parse_output_paths(codec);
	if (err < 0)
		return err;
	err = create_multi_channel_mode(codec);
	if (err < 0)
		return err;
	err = create_multi_out_ctls(codec, cfg);
	if (err < 0)
		return err;
	err = create_hp_out_ctls(codec);
	if (err < 0)
		return err;
	err = create_speaker_out_ctls(codec);
	if (err < 0)
		return err;
	err = create_indep_hp_ctls(codec);
	if (err < 0)
		return err;
	err = create_loopback_mixing_ctl(codec);
	if (err < 0)
		return err;
	err = create_hp_mic(codec);
	if (err < 0)
		return err;
	err = create_input_ctls(codec);
	if (err < 0)
		return err;

	spec->const_channel_count = spec->ext_channel_count;
	/* check the multiple speaker and headphone pins */
	if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT)
		spec->const_channel_count = max(spec->const_channel_count,
						cfg->speaker_outs * 2);
	if (cfg->line_out_type != AUTO_PIN_HP_OUT)
		spec->const_channel_count = max(spec->const_channel_count,
						cfg->hp_outs * 2);
	spec->multiout.max_channels = max(spec->ext_channel_count,
					  spec->const_channel_count);

	err = check_auto_mute_availability(codec);
	if (err < 0)
		return err;

	err = check_dyn_adc_switch(codec);
	if (err < 0)
		return err;

	err = check_auto_mic_availability(codec);
	if (err < 0)
		return err;

	/* add stereo mix if available and not enabled yet */
	if (!spec->auto_mic && spec->mixer_nid &&
	    spec->add_stereo_mix_input &&
	    spec->input_mux.num_items > 1 &&
	    snd_hda_get_bool_hint(codec, "add_stereo_mix_input") < 0) {
		err = parse_capture_source(codec, spec->mixer_nid,
					   CFG_IDX_MIX, spec->num_all_adcs,
					   "Stereo Mix", 0);
		if (err < 0)
			return err;
	}


	err = create_capture_mixers(codec);
	if (err < 0)
		return err;

	err = parse_mic_boost(codec);
	if (err < 0)
		return err;

	/* create "Headphone Mic Jack Mode" if no input selection is
	 * available (or user specifies add_jack_modes hint)
	 */
	if (spec->hp_mic_pin &&
	    (spec->auto_mic || spec->input_mux.num_items == 1 ||
	     spec->add_jack_modes)) {
		err = create_hp_mic_jack_mode(codec, spec->hp_mic_pin);
		if (err < 0)
			return err;
	}

	if (spec->add_jack_modes) {
		if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
			err = create_out_jack_modes(codec, cfg->line_outs,
						    cfg->line_out_pins);
			if (err < 0)
				return err;
		}
		if (cfg->line_out_type != AUTO_PIN_HP_OUT) {
			err = create_out_jack_modes(codec, cfg->hp_outs,
						    cfg->hp_pins);
			if (err < 0)
				return err;
		}
	}

	/* mute all aamix input initially */
	if (spec->mixer_nid)
		mute_all_mixer_nid(codec, spec->mixer_nid);

 dig_only:
	parse_digital(codec);

	if (spec->power_down_unused)
		codec->power_filter = snd_hda_gen_path_power_filter;

	if (!spec->no_analog && spec->beep_nid) {
		err = snd_hda_attach_beep_device(codec, spec->beep_nid);
		if (err < 0)
			return err;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_parse_auto_config);


/*
 * Build control elements
 */

/* slave controls for virtual master */
static const char * const slave_pfxs[] = {
	"Front", "Surround", "Center", "LFE", "Side",
	"Headphone", "Speaker", "Mono", "Line Out",
	"CLFE", "Bass Speaker", "PCM",
	"Speaker Front", "Speaker Surround", "Speaker CLFE", "Speaker Side",
	"Headphone Front", "Headphone Surround", "Headphone CLFE",
	"Headphone Side", "Headphone+LO", "Speaker+LO",
	NULL,
};

/**
 * snd_hda_gen_build_controls - Build controls from the parsed results
 * @codec: the HDA codec
 *
 * Pass this to build_controls patch_ops.
 */
int snd_hda_gen_build_controls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int err;

	if (spec->kctls.used) {
		err = snd_hda_add_new_ctls(codec, spec->kctls.list);
		if (err < 0)
			return err;
	}

	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_dig_out_ctls(codec,
						  spec->multiout.dig_out_nid,
						  spec->multiout.dig_out_nid,
						  spec->pcm_rec[1].pcm_type);
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

	/* if we have no master control, let's create it */
	if (!spec->no_analog &&
	    !snd_hda_find_mixer_ctl(codec, "Master Playback Volume")) {
		err = snd_hda_add_vmaster(codec, "Master Playback Volume",
					  spec->vmaster_tlv, slave_pfxs,
					  "Playback Volume");
		if (err < 0)
			return err;
	}
	if (!spec->no_analog &&
	    !snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = __snd_hda_add_vmaster(codec, "Master Playback Switch",
					    NULL, slave_pfxs,
					    "Playback Switch",
					    true, &spec->vmaster_mute.sw_kctl);
		if (err < 0)
			return err;
		if (spec->vmaster_mute.hook) {
			snd_hda_add_vmaster_hook(codec, &spec->vmaster_mute,
						 spec->vmaster_mute_enum);
			snd_hda_sync_vmaster_hook(&spec->vmaster_mute);
		}
	}

	free_kctls(spec); /* no longer needed */

	err = snd_hda_jack_add_kctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_build_controls);


/*
 * PCM definitions
 */

static void call_pcm_playback_hook(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream,
				   int action)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->pcm_playback_hook)
		spec->pcm_playback_hook(hinfo, codec, substream, action);
}

static void call_pcm_capture_hook(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream,
				  int action)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->pcm_capture_hook)
		spec->pcm_capture_hook(hinfo, codec, substream, action);
}

/*
 * Analog playback callbacks
 */
static int playback_pcm_open(struct hda_pcm_stream *hinfo,
			     struct hda_codec *codec,
			     struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	int err;

	mutex_lock(&spec->pcm_mutex);
	err = snd_hda_multi_out_analog_open(codec,
					    &spec->multiout, substream,
					     hinfo);
	if (!err) {
		spec->active_streams |= 1 << STREAM_MULTI_OUT;
		call_pcm_playback_hook(hinfo, codec, substream,
				       HDA_GEN_PCM_ACT_OPEN);
	}
	mutex_unlock(&spec->pcm_mutex);
	return err;
}

static int playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				struct hda_codec *codec,
				unsigned int stream_tag,
				unsigned int format,
				struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	int err;

	err = snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
					       stream_tag, format, substream);
	if (!err)
		call_pcm_playback_hook(hinfo, codec, substream,
				       HDA_GEN_PCM_ACT_PREPARE);
	return err;
}

static int playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				struct hda_codec *codec,
				struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	int err;

	err = snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
	if (!err)
		call_pcm_playback_hook(hinfo, codec, substream,
				       HDA_GEN_PCM_ACT_CLEANUP);
	return err;
}

static int playback_pcm_close(struct hda_pcm_stream *hinfo,
			      struct hda_codec *codec,
			      struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	mutex_lock(&spec->pcm_mutex);
	spec->active_streams &= ~(1 << STREAM_MULTI_OUT);
	call_pcm_playback_hook(hinfo, codec, substream,
			       HDA_GEN_PCM_ACT_CLOSE);
	mutex_unlock(&spec->pcm_mutex);
	return 0;
}

static int capture_pcm_open(struct hda_pcm_stream *hinfo,
			    struct hda_codec *codec,
			    struct snd_pcm_substream *substream)
{
	call_pcm_capture_hook(hinfo, codec, substream, HDA_GEN_PCM_ACT_OPEN);
	return 0;
}

static int capture_pcm_prepare(struct hda_pcm_stream *hinfo,
			       struct hda_codec *codec,
			       unsigned int stream_tag,
			       unsigned int format,
			       struct snd_pcm_substream *substream)
{
	snd_hda_codec_setup_stream(codec, hinfo->nid, stream_tag, 0, format);
	call_pcm_capture_hook(hinfo, codec, substream,
			      HDA_GEN_PCM_ACT_PREPARE);
	return 0;
}

static int capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
			       struct hda_codec *codec,
			       struct snd_pcm_substream *substream)
{
	snd_hda_codec_cleanup_stream(codec, hinfo->nid);
	call_pcm_capture_hook(hinfo, codec, substream,
			      HDA_GEN_PCM_ACT_CLEANUP);
	return 0;
}

static int capture_pcm_close(struct hda_pcm_stream *hinfo,
			     struct hda_codec *codec,
			     struct snd_pcm_substream *substream)
{
	call_pcm_capture_hook(hinfo, codec, substream, HDA_GEN_PCM_ACT_CLOSE);
	return 0;
}

static int alt_playback_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	int err = 0;

	mutex_lock(&spec->pcm_mutex);
	if (!spec->indep_hp_enabled)
		err = -EBUSY;
	else
		spec->active_streams |= 1 << STREAM_INDEP_HP;
	call_pcm_playback_hook(hinfo, codec, substream,
			       HDA_GEN_PCM_ACT_OPEN);
	mutex_unlock(&spec->pcm_mutex);
	return err;
}

static int alt_playback_pcm_close(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	mutex_lock(&spec->pcm_mutex);
	spec->active_streams &= ~(1 << STREAM_INDEP_HP);
	call_pcm_playback_hook(hinfo, codec, substream,
			       HDA_GEN_PCM_ACT_CLOSE);
	mutex_unlock(&spec->pcm_mutex);
	return 0;
}

static int alt_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    unsigned int stream_tag,
				    unsigned int format,
				    struct snd_pcm_substream *substream)
{
	snd_hda_codec_setup_stream(codec, hinfo->nid, stream_tag, 0, format);
	call_pcm_playback_hook(hinfo, codec, substream,
			       HDA_GEN_PCM_ACT_PREPARE);
	return 0;
}

static int alt_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	snd_hda_codec_cleanup_stream(codec, hinfo->nid);
	call_pcm_playback_hook(hinfo, codec, substream,
			       HDA_GEN_PCM_ACT_CLEANUP);
	return 0;
}

/*
 * Digital out
 */
static int dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    unsigned int stream_tag,
				    unsigned int format,
				    struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

static int dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_cleanup(codec, &spec->multiout);
}

static int dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 * Analog capture
 */
#define alt_capture_pcm_open	capture_pcm_open
#define alt_capture_pcm_close	capture_pcm_close

static int alt_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   unsigned int stream_tag,
				   unsigned int format,
				   struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number + 1],
				   stream_tag, 0, format);
	call_pcm_capture_hook(hinfo, codec, substream,
			      HDA_GEN_PCM_ACT_PREPARE);
	return 0;
}

static int alt_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;

	snd_hda_codec_cleanup_stream(codec,
				     spec->adc_nids[substream->number + 1]);
	call_pcm_capture_hook(hinfo, codec, substream,
			      HDA_GEN_PCM_ACT_CLEANUP);
	return 0;
}

/*
 */
static const struct hda_pcm_stream pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	/* NID is set in build_pcms */
	.ops = {
		.open = playback_pcm_open,
		.close = playback_pcm_close,
		.prepare = playback_pcm_prepare,
		.cleanup = playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in build_pcms */
	.ops = {
		.open = capture_pcm_open,
		.close = capture_pcm_close,
		.prepare = capture_pcm_prepare,
		.cleanup = capture_pcm_cleanup
	},
};

static const struct hda_pcm_stream pcm_analog_alt_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in build_pcms */
	.ops = {
		.open = alt_playback_pcm_open,
		.close = alt_playback_pcm_close,
		.prepare = alt_playback_pcm_prepare,
		.cleanup = alt_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream pcm_analog_alt_capture = {
	.substreams = 2, /* can be overridden */
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in build_pcms */
	.ops = {
		.open = alt_capture_pcm_open,
		.close = alt_capture_pcm_close,
		.prepare = alt_capture_pcm_prepare,
		.cleanup = alt_capture_pcm_cleanup
	},
};

static const struct hda_pcm_stream pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in build_pcms */
	.ops = {
		.open = dig_playback_pcm_open,
		.close = dig_playback_pcm_close,
		.prepare = dig_playback_pcm_prepare,
		.cleanup = dig_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in build_pcms */
};

/* Used by build_pcms to flag that a PCM has no playback stream */
static const struct hda_pcm_stream pcm_null_stream = {
	.substreams = 0,
	.channels_min = 0,
	.channels_max = 0,
};

/*
 * dynamic changing ADC PCM streams
 */
static bool dyn_adc_pcm_resetup(struct hda_codec *codec, int cur)
{
	struct hda_gen_spec *spec = codec->spec;
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

/* analog capture with dynamic dual-adc changes */
static int dyn_adc_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
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
	struct hda_gen_spec *spec = codec->spec;
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

static void fill_pcm_stream_name(char *str, size_t len, const char *sfx,
				 const char *chip_name)
{
	char *p;

	if (*str)
		return;
	strlcpy(str, chip_name, len);

	/* drop non-alnum chars after a space */
	for (p = strchr(str, ' '); p; p = strchr(p + 1, ' ')) {
		if (!isalnum(p[1])) {
			*p = 0;
			break;
		}
	}
	strlcat(str, sfx, len);
}

/**
 * snd_hda_gen_build_pcms - build PCM streams based on the parsed results
 * @codec: the HDA codec
 *
 * Pass this to build_pcms patch_ops.
 */
int snd_hda_gen_build_pcms(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;
	const struct hda_pcm_stream *p;
	bool have_multi_adcs;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	if (spec->no_analog)
		goto skip_analog;

	fill_pcm_stream_name(spec->stream_name_analog,
			     sizeof(spec->stream_name_analog),
			     " Analog", codec->chip_name);
	info->name = spec->stream_name_analog;

	if (spec->multiout.num_dacs > 0) {
		p = spec->stream_analog_playback;
		if (!p)
			p = &pcm_analog_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *p;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dac_nids[0];
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
			spec->multiout.max_channels;
		if (spec->autocfg.line_out_type == AUTO_PIN_SPEAKER_OUT &&
		    spec->autocfg.line_outs == 2)
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].chmap =
				snd_pcm_2_1_chmaps;
	}
	if (spec->num_adc_nids) {
		p = spec->stream_analog_capture;
		if (!p) {
			if (spec->dyn_adc_switch)
				p = &dyn_adc_pcm_analog_capture;
			else
				p = &pcm_analog_capture;
		}
		info->stream[SNDRV_PCM_STREAM_CAPTURE] = *p;
		info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];
	}

 skip_analog:
	/* SPDIF for stream index #1 */
	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		fill_pcm_stream_name(spec->stream_name_digital,
				     sizeof(spec->stream_name_digital),
				     " Digital", codec->chip_name);
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
				p = &pcm_digital_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *p;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid) {
			p = spec->stream_digital_capture;
			if (!p)
				p = &pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = *p;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
	}

	if (spec->no_analog)
		return 0;

	/* If the use of more than one ADC is requested for the current
	 * model, configure a second analog capture-only PCM.
	 */
	have_multi_adcs = (spec->num_adc_nids > 1) &&
		!spec->dyn_adc_switch && !spec->auto_mic;
	/* Additional Analaog capture for index #2 */
	if (spec->alt_dac_nid || have_multi_adcs) {
		fill_pcm_stream_name(spec->stream_name_alt_analog,
				     sizeof(spec->stream_name_alt_analog),
			     " Alt Analog", codec->chip_name);
		codec->num_pcms = 3;
		info = spec->pcm_rec + 2;
		info->name = spec->stream_name_alt_analog;
		if (spec->alt_dac_nid) {
			p = spec->stream_analog_alt_playback;
			if (!p)
				p = &pcm_analog_alt_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *p;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
				spec->alt_dac_nid;
		} else {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				pcm_null_stream;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = 0;
		}
		if (have_multi_adcs) {
			p = spec->stream_analog_alt_capture;
			if (!p)
				p = &pcm_analog_alt_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = *p;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->adc_nids[1];
			info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams =
				spec->num_adc_nids - 1;
		} else {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				pcm_null_stream;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = 0;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_build_pcms);


/*
 * Standard auto-parser initializations
 */

/* configure the given path as a proper output */
static void set_output_and_unmute(struct hda_codec *codec, int path_idx)
{
	struct nid_path *path;
	hda_nid_t pin;

	path = snd_hda_get_path_from_idx(codec, path_idx);
	if (!path || !path->depth)
		return;
	pin = path->path[path->depth - 1];
	restore_pin_ctl(codec, pin);
	snd_hda_activate_path(codec, path, path->active,
			      aamix_default(codec->spec));
	set_pin_eapd(codec, pin, path->active);
}

/* initialize primary output paths */
static void init_multi_out(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->autocfg.line_outs; i++)
		set_output_and_unmute(codec, spec->out_paths[i]);
}


static void __init_extra_out(struct hda_codec *codec, int num_outs, int *paths)
{
	int i;

	for (i = 0; i < num_outs; i++)
		set_output_and_unmute(codec, paths[i]);
}

/* initialize hp and speaker paths */
static void init_extra_out(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	if (spec->autocfg.line_out_type != AUTO_PIN_HP_OUT)
		__init_extra_out(codec, spec->autocfg.hp_outs, spec->hp_paths);
	if (spec->autocfg.line_out_type != AUTO_PIN_SPEAKER_OUT)
		__init_extra_out(codec, spec->autocfg.speaker_outs,
				 spec->speaker_paths);
}

/* initialize multi-io paths */
static void init_multi_io(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->multi_ios; i++) {
		hda_nid_t pin = spec->multi_io[i].pin;
		struct nid_path *path;
		path = get_multiio_path(codec, i);
		if (!path)
			continue;
		if (!spec->multi_io[i].ctl_in)
			spec->multi_io[i].ctl_in =
				snd_hda_codec_get_pin_target(codec, pin);
		snd_hda_activate_path(codec, path, path->active,
				      aamix_default(spec));
	}
}

static void init_aamix_paths(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	if (!spec->have_aamix_ctl)
		return;
	update_aamix_paths(codec, spec->aamix_mode, spec->out_paths[0],
			   spec->aamix_out_paths[0],
			   spec->autocfg.line_out_type);
	update_aamix_paths(codec, spec->aamix_mode, spec->hp_paths[0],
			   spec->aamix_out_paths[1],
			   AUTO_PIN_HP_OUT);
	update_aamix_paths(codec, spec->aamix_mode, spec->speaker_paths[0],
			   spec->aamix_out_paths[2],
			   AUTO_PIN_SPEAKER_OUT);
}

/* set up input pins and loopback paths */
static void init_analog_input(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		if (is_input_pin(codec, nid))
			restore_pin_ctl(codec, nid);

		/* init loopback inputs */
		if (spec->mixer_nid) {
			resume_path_from_idx(codec, spec->loopback_paths[i]);
			resume_path_from_idx(codec, spec->loopback_merge_path);
		}
	}
}

/* initialize ADC paths */
static void init_input_src(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	struct nid_path *path;
	int i, c, nums;

	if (spec->dyn_adc_switch)
		nums = 1;
	else
		nums = spec->num_adc_nids;

	for (c = 0; c < nums; c++) {
		for (i = 0; i < imux->num_items; i++) {
			path = get_input_path(codec, c, i);
			if (path) {
				bool active = path->active;
				if (i == spec->cur_mux[c])
					active = true;
				snd_hda_activate_path(codec, path, active, false);
			}
		}
		if (spec->hp_mic)
			update_hp_mic(codec, c, true);
	}

	if (spec->cap_sync_hook)
		spec->cap_sync_hook(codec, NULL, NULL);
}

/* set right pin controls for digital I/O */
static void init_digital(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	hda_nid_t pin;

	for (i = 0; i < spec->autocfg.dig_outs; i++)
		set_output_and_unmute(codec, spec->digout_paths[i]);
	pin = spec->autocfg.dig_in_pin;
	if (pin) {
		restore_pin_ctl(codec, pin);
		resume_path_from_idx(codec, spec->digin_path);
	}
}

/* clear unsol-event tags on unused pins; Conexant codecs seem to leave
 * invalid unsol tags by some reason
 */
static void clear_unsol_on_unused_pins(struct hda_codec *codec)
{
	int i;

	for (i = 0; i < codec->init_pins.used; i++) {
		struct hda_pincfg *pin = snd_array_elem(&codec->init_pins, i);
		hda_nid_t nid = pin->nid;
		if (is_jack_detectable(codec, nid) &&
		    !snd_hda_jack_tbl_get(codec, nid))
			snd_hda_codec_update_cache(codec, nid, 0,
					AC_VERB_SET_UNSOLICITED_ENABLE, 0);
	}
}

/**
 * snd_hda_gen_init - initialize the generic spec
 * @codec: the HDA codec
 *
 * This can be put as patch_ops init function.
 */
int snd_hda_gen_init(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	if (spec->init_hook)
		spec->init_hook(codec);

	snd_hda_apply_verbs(codec);

	codec->cached_write = 1;

	init_multi_out(codec);
	init_extra_out(codec);
	init_multi_io(codec);
	init_aamix_paths(codec);
	init_analog_input(codec);
	init_input_src(codec);
	init_digital(codec);

	clear_unsol_on_unused_pins(codec);

	/* call init functions of standard auto-mute helpers */
	update_automute_all(codec);

	snd_hda_codec_flush_cache(codec);

	if (spec->vmaster_mute.sw_kctl && spec->vmaster_mute.hook)
		snd_hda_sync_vmaster_hook(&spec->vmaster_mute);

	hda_call_check_power_status(codec, 0x01);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_init);

/**
 * snd_hda_gen_free - free the generic spec
 * @codec: the HDA codec
 *
 * This can be put as patch_ops free function.
 */
void snd_hda_gen_free(struct hda_codec *codec)
{
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_FREE);
	snd_hda_gen_spec_free(codec->spec);
	kfree(codec->spec);
	codec->spec = NULL;
}
EXPORT_SYMBOL_GPL(snd_hda_gen_free);

#ifdef CONFIG_PM
/**
 * snd_hda_gen_check_power_status - check the loopback power save state
 * @codec: the HDA codec
 * @nid: NID to inspect
 *
 * This can be put as patch_ops check_power_status function.
 */
int snd_hda_gen_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
}
EXPORT_SYMBOL_GPL(snd_hda_gen_check_power_status);
#endif


/*
 * the generic codec support
 */

static const struct hda_codec_ops generic_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.free = snd_hda_gen_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.check_power_status = snd_hda_gen_check_power_status,
#endif
};

/**
 * snd_hda_parse_generic_codec - Generic codec parser
 * @codec: the HDA codec
 *
 * This should be called from the HDA codec core.
 */
int snd_hda_parse_generic_codec(struct hda_codec *codec)
{
	struct hda_gen_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	snd_hda_gen_spec_init(spec);
	codec->spec = spec;

	err = snd_hda_parse_pin_defcfg(codec, &spec->autocfg, NULL, 0);
	if (err < 0)
		return err;

	err = snd_hda_gen_parse_auto_config(codec, &spec->autocfg);
	if (err < 0)
		goto error;

	codec->patch_ops = generic_patch_ops;
	return 0;

error:
	snd_hda_gen_free(codec);
	return err;
}
EXPORT_SYMBOL_GPL(snd_hda_parse_generic_codec);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic HD-audio codec parser");
