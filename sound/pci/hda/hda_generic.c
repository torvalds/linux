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
#include <linux/ctype.h>
#include <linux/string.h>
#include <sound/core.h>
#include <sound/jack.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include "hda_generic.h"


/* initialize hda_gen_spec struct */
int snd_hda_gen_spec_init(struct hda_gen_spec *spec)
{
	snd_array_init(&spec->kctls, sizeof(struct snd_kcontrol_new), 32);
	snd_array_init(&spec->bind_ctls, sizeof(struct hda_bind_ctls *), 8);
	snd_array_init(&spec->paths, sizeof(struct nid_path), 8);
	mutex_init(&spec->pcm_mutex);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_gen_spec_init);

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
EXPORT_SYMBOL_HDA(snd_hda_gen_add_kctl);

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

static struct hda_bind_ctls *new_bind_ctl(struct hda_codec *codec,
					  unsigned int nums,
					  struct hda_ctl_ops *ops)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_bind_ctls **ctlp, *ctl;
	ctlp = snd_array_new(&spec->bind_ctls);
	if (!ctlp)
		return NULL;
	ctl = kzalloc(sizeof(*ctl) + sizeof(long) * (nums + 1), GFP_KERNEL);
	*ctlp = ctl;
	if (ctl)
		ctl->ops = ops;
	return ctl;
}

static void free_bind_ctls(struct hda_gen_spec *spec)
{
	if (spec->bind_ctls.list) {
		struct hda_bind_ctls **ctl = spec->bind_ctls.list;
		int i;
		for (i = 0; i < spec->bind_ctls.used; i++)
			kfree(ctl[i]);
	}
	snd_array_free(&spec->bind_ctls);
}

void snd_hda_gen_spec_free(struct hda_gen_spec *spec)
{
	if (!spec)
		return;
	free_kctls(spec);
	free_bind_ctls(spec);
	snd_array_free(&spec->paths);
}
EXPORT_SYMBOL_HDA(snd_hda_gen_spec_free);

/*
 * parsing paths
 */

/* get the path between the given NIDs;
 * passing 0 to either @pin or @dac behaves as a wildcard
 */
struct nid_path *snd_hda_get_nid_path(struct hda_codec *codec,
				      hda_nid_t from_nid, hda_nid_t to_nid)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->paths.used; i++) {
		struct nid_path *path = snd_array_elem(&spec->paths, i);
		if (path->depth <= 0)
			continue;
		if ((!from_nid || path->path[0] == from_nid) &&
		    (!to_nid || path->path[path->depth - 1] == to_nid))
			return path;
	}
	return NULL;
}
EXPORT_SYMBOL_HDA(snd_hda_get_nid_path);

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
			      int dir, int idx)
{
	unsigned int val = HDA_COMPOSE_AMP_VAL(nid, 3, idx, dir);
	return is_ctl_used(codec, val, NID_PATH_VOL_CTL) ||
		is_ctl_used(codec, val, NID_PATH_MUTE_CTL);
}

static void print_nid_path(const char *pfx, struct nid_path *path)
{
	char buf[40];
	int i;


	buf[0] = 0;
	for (i = 0; i < path->depth; i++) {
		char tmp[4];
		sprintf(tmp, ":%02x", path->path[i]);
		strlcat(buf, tmp, sizeof(buf));
	}
	snd_printdd("%s path: depth=%d %s\n", pfx, path->depth, buf);
}

/* called recursively */
static bool __parse_nid_path(struct hda_codec *codec,
			     hda_nid_t from_nid, hda_nid_t to_nid,
			     int with_aa_mix, struct nid_path *path, int depth)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t conn[16];
	int i, nums;

	if (to_nid == spec->mixer_nid) {
		if (with_aa_mix == HDA_PARSE_NO_AAMIX)
			return false;
		with_aa_mix = HDA_PARSE_ALL; /* mark aa-mix is included */
	}

	nums = snd_hda_get_connections(codec, to_nid, conn, ARRAY_SIZE(conn));
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
		/* aa-mix is requested but not included? */
		if (!(spec->mixer_nid && with_aa_mix == HDA_PARSE_ONLY_AAMIX))
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
				     with_aa_mix, path, depth + 1))
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

/* parse the widget path from the given nid to the target nid;
 * when @from_nid is 0, try to find an empty DAC;
 * when @with_aa_mix is HDA_PARSE_NO_AAMIX, paths with spec->mixer_nid are
 * excluded, only the paths that don't go through the mixer will be chosen.
 * when @with_aa_mix is HDA_PARSE_ONLY_AAMIX, only the paths going through
 * spec->mixer_nid will be chosen.
 * when @with_aa_mix is HDA_PARSE_ALL, no special handling about mixer widget.
 */
bool snd_hda_parse_nid_path(struct hda_codec *codec, hda_nid_t from_nid,
			    hda_nid_t to_nid, int with_aa_mix,
			    struct nid_path *path)
{
	if (__parse_nid_path(codec, from_nid, to_nid, with_aa_mix, path, 1)) {
		path->path[path->depth] = to_nid;
		path->depth++;
		return true;
	}
	return false;
}
EXPORT_SYMBOL_HDA(snd_hda_parse_nid_path);

/*
 * parse the path between the given NIDs and add to the path list.
 * if no valid path is found, return NULL
 */
struct nid_path *
snd_hda_add_new_path(struct hda_codec *codec, hda_nid_t from_nid,
		     hda_nid_t to_nid, int with_aa_mix)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;

	if (from_nid && to_nid && !is_reachable_path(codec, from_nid, to_nid))
		return NULL;

	path = snd_array_new(&spec->paths);
	if (!path)
		return NULL;
	memset(path, 0, sizeof(*path));
	if (snd_hda_parse_nid_path(codec, from_nid, to_nid, with_aa_mix, path))
		return path;
	/* push back */
	spec->paths.used--;
	return NULL;
}
EXPORT_SYMBOL_HDA(snd_hda_add_new_path);

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

/* check whether the widget has the given amp capability for the direction */
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
	int i;

	for (i = path->depth - 1; i >= 0; i--) {
		if (nid_has_volume(codec, path->path[i], HDA_OUTPUT))
			return path->path[i];
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
			  unsigned int idx, unsigned int dir)
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

/* get the default amp value for the target state */
static int get_amp_val_to_activate(struct hda_codec *codec, hda_nid_t nid,
				   int dir, bool enable)
{
	unsigned int caps;
	unsigned int val = 0;

	caps = query_amp_caps(codec, nid, dir);
	if (caps & AC_AMPCAP_NUM_STEPS) {
		/* set to 0dB */
		if (enable)
			val = (caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT;
	}
	if (caps & AC_AMPCAP_MUTE) {
		if (!enable)
			val |= HDA_AMP_MUTE;
	}
	return val;
}

/* initialize the amp value (only at the first time) */
static void init_amp(struct hda_codec *codec, hda_nid_t nid, int dir, int idx)
{
	int val = get_amp_val_to_activate(codec, nid, dir, false);
	snd_hda_codec_amp_init_stereo(codec, nid, dir, idx, 0xff, val);
}

static void activate_amp(struct hda_codec *codec, hda_nid_t nid, int dir,
			 int idx, bool enable)
{
	int val;
	if (is_ctl_associated(codec, nid, dir, idx) ||
	    is_active_nid(codec, nid, dir, idx))
		return;
	val = get_amp_val_to_activate(codec, nid, dir, enable);
	snd_hda_codec_amp_stereo(codec, nid, dir, idx, 0xff, val);
}

static void activate_amp_out(struct hda_codec *codec, struct nid_path *path,
			     int i, bool enable)
{
	hda_nid_t nid = path->path[i];
	init_amp(codec, nid, HDA_OUTPUT, 0);
	activate_amp(codec, nid, HDA_OUTPUT, 0, enable);
}

static void activate_amp_in(struct hda_codec *codec, struct nid_path *path,
			    int i, bool enable, bool add_aamix)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t conn[16];
	int n, nums, idx;
	int type;
	hda_nid_t nid = path->path[i];

	nums = snd_hda_get_connections(codec, nid, conn, ARRAY_SIZE(conn));
	type = get_wcaps_type(get_wcaps(codec, nid));
	if (type == AC_WID_PIN ||
	    (type == AC_WID_AUD_IN && codec->single_adc_amp)) {
		nums = 1;
		idx = 0;
	} else
		idx = path->idx[i];

	for (n = 0; n < nums; n++)
		init_amp(codec, nid, HDA_INPUT, n);

	if (is_ctl_associated(codec, nid, HDA_INPUT, idx))
		return;

	/* here is a little bit tricky in comparison with activate_amp_out();
	 * when aa-mixer is available, we need to enable the path as well
	 */
	for (n = 0; n < nums; n++) {
		if (n != idx && (!add_aamix || conn[n] != spec->mixer_nid))
			continue;
		activate_amp(codec, nid, HDA_INPUT, n, enable);
	}
}

/* activate or deactivate the given path
 * if @add_aamix is set, enable the input from aa-mix NID as well (if any)
 */
void snd_hda_activate_path(struct hda_codec *codec, struct nid_path *path,
			   bool enable, bool add_aamix)
{
	int i;

	if (!enable)
		path->active = false;

	for (i = path->depth - 1; i >= 0; i--) {
		if (enable && path->multi[i])
			snd_hda_codec_write_cache(codec, path->path[i], 0,
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
EXPORT_SYMBOL_HDA(snd_hda_activate_path);

/* turn on/off EAPD on the given pin */
static void set_pin_eapd(struct hda_codec *codec, hda_nid_t pin, bool enable)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->own_eapd_ctl ||
	    !(snd_hda_query_pin_caps(codec, pin) & AC_PINCAP_EAPD))
		return;
	if (codec->inv_eapd)
		enable = !enable;
	snd_hda_codec_update_cache(codec, pin, 0,
				   AC_VERB_SET_EAPD_BTLENABLE,
				   enable ? 0x02 : 0x00);
}


/*
 * Helper functions for creating mixer ctl elements
 */

enum {
	HDA_CTL_WIDGET_VOL,
	HDA_CTL_WIDGET_MUTE,
	HDA_CTL_BIND_MUTE,
	HDA_CTL_BIND_VOL,
	HDA_CTL_BIND_SW,
};
static const struct snd_kcontrol_new control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	HDA_BIND_MUTE(NULL, 0, 0, 0),
	HDA_BIND_VOL(NULL, 0),
	HDA_BIND_SW(NULL, 0),
};

/* add dynamic controls from template */
static int add_control(struct hda_gen_spec *spec, int type, const char *name,
		       int cidx, unsigned long val)
{
	struct snd_kcontrol_new *knew;

	knew = snd_hda_gen_add_kctl(spec, name, &control_templates[type]);
	if (!knew)
		return -ENOMEM;
	knew->index = cidx;
	if (get_amp_nid_(val))
		knew->subdevice = HDA_SUBDEV_AMP_FLAG;
	knew->private_value = val;
	return 0;
}

static int add_control_with_pfx(struct hda_gen_spec *spec, int type,
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

static const char * const channel_name[4] = {
	"Front", "Surround", "CLFE", "Side"
};

/* give some appropriate ctl name prefix for the given line out channel */
static const char *get_line_out_pfx(struct hda_gen_spec *spec, int ch,
				    bool can_be_master, int *index)
{
	struct auto_pin_cfg *cfg = &spec->autocfg;

	*index = 0;
	if (cfg->line_outs == 1 && !spec->multi_ios &&
	    !cfg->hp_outs && !cfg->speaker_outs && can_be_master)
		return spec->vmaster_mute.hook ? "PCM" : "Master";

	/* if there is really a single DAC used in the whole output paths,
	 * use it master (or "PCM" if a vmaster hook is present)
	 */
	if (spec->multiout.num_dacs == 1 && !spec->mixer_nid &&
	    !spec->multiout.hp_out_nid[0] && !spec->multiout.extra_out_nid[0])
		return spec->vmaster_mute.hook ? "PCM" : "Master";

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

/* look for widgets in the path between the given NIDs appropriate for
 * volume and mute controls, and assign the values to ctls[].
 *
 * When no appropriate widget is found in the path, the badness value
 * is incremented depending on the situation.  The function returns the
 * total badness for both volume and mute controls.
 */
static int assign_out_path_ctls(struct hda_codec *codec, hda_nid_t pin,
				hda_nid_t dac)
{
	struct nid_path *path = snd_hda_get_nid_path(codec, dac, pin);
	hda_nid_t nid;
	unsigned int val;
	int badness = 0;

	if (!path)
		return BAD_SHARED_VOL * 2;
	nid = look_for_out_vol_nid(codec, path);
	if (nid) {
		val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
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
static int try_assign_dacs(struct hda_codec *codec, int num_outs,
			   const hda_nid_t *pins, hda_nid_t *dacs,
			   const struct badness_table *bad)
{
	struct hda_gen_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, j;
	int badness = 0;
	hda_nid_t dac;

	if (!num_outs)
		return 0;

	for (i = 0; i < num_outs; i++) {
		struct nid_path *path;
		hda_nid_t pin = pins[i];
		if (!dacs[i])
			dacs[i] = look_for_dac(codec, pin, false);
		if (!dacs[i] && !i) {
			for (j = 1; j < num_outs; j++) {
				if (is_reachable_path(codec, dacs[j], pin)) {
					dacs[0] = dacs[j];
					dacs[j] = 0;
					break;
				}
			}
		}
		dac = dacs[i];
		if (!dac) {
			if (is_reachable_path(codec, dacs[0], pin))
				dac = dacs[0];
			else if (cfg->line_outs > i &&
				 is_reachable_path(codec, spec->private_dac_nids[i], pin))
				dac = spec->private_dac_nids[i];
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
		path = snd_hda_add_new_path(codec, dac, pin, HDA_PARSE_NO_AAMIX);
		if (!path && i > 0 && spec->mixer_nid) {
			/* try with aamix */
			path = snd_hda_add_new_path(codec, dac, pin, HDA_PARSE_ALL);
		}
		if (!path)
			dac = dacs[i] = 0;
		else
			print_nid_path("output", path);
		if (dac)
			badness += assign_out_path_ctls(codec, pin, dac);
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
			  bool hardwired, int offset)
{
	struct hda_gen_spec *spec = codec->spec;
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
			struct nid_path *path;
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
				if (!is_reachable_path(codec, dac, nid))
					dac = 0;
			}
			if (hardwired)
				dac = get_dac_if_single(codec, nid);
			else if (!dac)
				dac = look_for_dac(codec, nid, false);
			if (!dac) {
				badness++;
				continue;
			}
			path = snd_hda_add_new_path(codec, dac, nid, HDA_PARSE_NO_AAMIX);
			if (!path) {
				badness++;
				continue;
			}
			print_nid_path("multiio", path);
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
		/* cancel newly assigned paths */
		spec->paths.used -= spec->multi_ios - old_pins;
		spec->multi_ios = old_pins;
		return badness;
	}

	/* assign volume and mute controls */
	for (i = old_pins; i < spec->multi_ios; i++)
		badness += assign_out_path_ctls(codec, spec->multi_io[i].pin,
						spec->multi_io[i].dac);

	return badness;
}

/* map DACs for all pins in the list if they are single connections */
static bool map_singles(struct hda_codec *codec, int outs,
			const hda_nid_t *pins, hda_nid_t *dacs)
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
		path = snd_hda_add_new_path(codec, dac, pins[i], HDA_PARSE_NO_AAMIX);
		if (!path && i > 0 && spec->mixer_nid)
			path = snd_hda_add_new_path(codec, dac, pins[i], HDA_PARSE_ALL);
		if (path) {
			dacs[i] = dac;
			found = true;
			print_nid_path("output", path);
		}
	}
	return found;
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
	badness = 0;

	/* fill hard-wired DACs first */
	if (fill_hardwired) {
		bool mapped;
		do {
			mapped = map_singles(codec, cfg->line_outs,
					     cfg->line_out_pins,
					     spec->private_dac_nids);
			mapped |= map_singles(codec, cfg->hp_outs,
					      cfg->hp_pins,
					      spec->multiout.hp_out_nid);
			mapped |= map_singles(codec, cfg->speaker_outs,
					      cfg->speaker_pins,
					      spec->multiout.extra_out_nid);
			if (fill_mio_first && cfg->line_outs == 1 &&
			    cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
				err = fill_multi_ios(codec, cfg->line_out_pins[0], true, 0);
				if (!err)
					mapped = true;
			}
		} while (mapped);
	}

	badness += try_assign_dacs(codec, cfg->line_outs, cfg->line_out_pins,
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
		err = fill_multi_ios(codec, cfg->line_out_pins[0], false, 0);
		if (err < 0)
			return err;
		/* we don't count badness at this stage yet */
	}

	if (cfg->line_out_type != AUTO_PIN_HP_OUT) {
		err = try_assign_dacs(codec, cfg->hp_outs, cfg->hp_pins,
				      spec->multiout.hp_out_nid,
				      &extra_out_badness);
		if (err < 0)
			return err;
		badness += err;
	}
	if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		err = try_assign_dacs(codec, cfg->speaker_outs,
				      cfg->speaker_pins,
				      spec->multiout.extra_out_nid,
					 &extra_out_badness);
		if (err < 0)
			return err;
		badness += err;
	}
	if (cfg->line_outs == 1 && cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
		err = fill_multi_ios(codec, cfg->line_out_pins[0], false, 0);
		if (err < 0)
			return err;
		badness += err;
	}
	if (cfg->hp_outs && cfg->line_out_type == AUTO_PIN_SPEAKER_OUT) {
		/* try multi-ios with HP + inputs */
		int offset = 0;
		if (cfg->line_outs >= 3)
			offset = 1;
		err = fill_multi_ios(codec, cfg->hp_pins[0], false, offset);
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

static void debug_show_configs(struct hda_gen_spec *spec, struct auto_pin_cfg *cfg)
{
	debug_badness("multi_outs = %x/%x/%x/%x : %x/%x/%x/%x\n",
		      cfg->line_out_pins[0], cfg->line_out_pins[1],
		      cfg->line_out_pins[2], cfg->line_out_pins[3],
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
		      cfg->hp_pins[2], cfg->hp_pins[3],
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
			snd_printk(KERN_ERR "hda: Too many DACs!\n");
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
	int best_badness = INT_MAX;
	int badness;
	bool fill_hardwired = true, fill_mio_first = true;
	bool best_wired = true, best_mio = true;
	bool hp_spk_swapped = false;

	fill_all_dac_nids(codec);

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
		debug_badness("==> restoring best_cfg\n");
		*cfg = *best_cfg;
		fill_and_eval_dacs(codec, best_wired, best_mio);
	}
	debug_badness("==> Best config: lo_type=%d, wired=%d, mio=%d\n",
		      cfg->line_out_type, best_wired, best_mio);
	debug_show_configs(spec, cfg);

	if (cfg->line_out_pins[0]) {
		struct nid_path *path;
		path = snd_hda_get_nid_path(codec,
					    spec->multiout.dac_nids[0],
					    cfg->line_out_pins[0]);
		if (path)
			spec->vmaster_nid = look_for_out_vol_nid(codec, path);
	}

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
		hda_nid_t dac, pin;
		struct nid_path *path;

		dac = spec->multiout.dac_nids[i];
		if (!dac)
			continue;
		if (i >= cfg->line_outs) {
			pin = spec->multi_io[i - 1].pin;
			index = 0;
			name = channel_name[i];
		} else {
			pin = cfg->line_out_pins[i];
			name = get_line_out_pfx(spec, i, true, &index);
		}

		path = snd_hda_get_nid_path(codec, dac, pin);
		if (!path)
			continue;
		if (!name || !strcmp(name, "CLFE")) {
			/* Center/LFE */
			err = add_vol_ctl(codec, "Center", 0, 1, path);
			if (err < 0)
				return err;
			err = add_vol_ctl(codec, "LFE", 0, 2, path);
			if (err < 0)
				return err;
			err = add_sw_ctl(codec, "Center", 0, 1, path);
			if (err < 0)
				return err;
			err = add_sw_ctl(codec, "LFE", 0, 2, path);
			if (err < 0)
				return err;
		} else {
			err = add_stereo_vol(codec, name, index, path);
			if (err < 0)
				return err;
			err = add_stereo_sw(codec, name, index, path);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int create_extra_out(struct hda_codec *codec, hda_nid_t pin,
			    hda_nid_t dac, const char *pfx, int cidx)
{
	struct nid_path *path;
	int err;

	path = snd_hda_get_nid_path(codec, dac, pin);
	if (!path)
		return 0;
	/* bind volume control will be created in the case of dac = 0 */
	if (dac) {
		err = add_stereo_vol(codec, pfx, cidx, path);
		if (err < 0)
			return err;
	}
	err = add_stereo_sw(codec, pfx, cidx, path);
	if (err < 0)
		return err;
	return 0;
}

/* add playback controls for speaker and HP outputs */
static int create_extra_outs(struct hda_codec *codec, int num_pins,
			     const hda_nid_t *pins, const hda_nid_t *dacs,
			     const char *pfx)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_bind_ctls *ctl;
	char name[32];
	int i, n, err;

	if (!num_pins || !pins[0])
		return 0;

	if (num_pins == 1) {
		hda_nid_t dac = *dacs;
		if (!dac)
			dac = spec->multiout.dac_nids[0];
		return create_extra_out(codec, *pins, dac, pfx, 0);
	}

	for (i = 0; i < num_pins; i++) {
		hda_nid_t dac;
		if (dacs[num_pins - 1])
			dac = dacs[i]; /* with individual volumes */
		else
			dac = 0;
		if (num_pins == 2 && i == 1 && !strcmp(pfx, "Speaker")) {
			err = create_extra_out(codec, pins[i], dac,
					       "Bass Speaker", 0);
		} else if (num_pins >= 3) {
			snprintf(name, sizeof(name), "%s %s",
				 pfx, channel_name[i]);
			err = create_extra_out(codec, pins[i], dac, name, 0);
		} else {
			err = create_extra_out(codec, pins[i], dac, pfx, i);
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
		struct nid_path *path;
		if (!pins[i] || !dacs[i])
			continue;
		path = snd_hda_get_nid_path(codec, dacs[i], pins[i]);
		if (!path)
			continue;
		vol = look_for_out_vol_nid(codec, path);
		if (vol)
			ctl->values[n++] =
				HDA_COMPOSE_AMP_VAL(vol, 3, 0, HDA_OUTPUT);
	}
	if (n) {
		snprintf(name, sizeof(name), "%s Playback Volume", pfx);
		err = add_control(spec, HDA_CTL_BIND_VOL, name, 0, (long)ctl);
		if (err < 0)
			return err;
	}
	return 0;
}

static int create_hp_out_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	return create_extra_outs(codec, spec->autocfg.hp_outs,
				 spec->autocfg.hp_pins,
				 spec->multiout.hp_out_nid,
				 "Headphone");
}

static int create_speaker_out_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	return create_extra_outs(codec, spec->autocfg.speaker_outs,
				 spec->autocfg.speaker_pins,
				 spec->multiout.extra_out_nid,
				 "Speaker");
}

/*
 * independent HP controls
 */

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
		spec->indep_hp_enabled = select;
		if (spec->indep_hp_enabled)
			spec->multiout.hp_out_nid[0] = 0;
		else
			spec->multiout.hp_out_nid[0] = spec->alt_dac_nid;
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

	if (!spec->indep_hp)
		return 0;
	if (!spec->multiout.hp_out_nid[0]) {
		spec->indep_hp = 0;
		return 0;
	}

	spec->indep_hp_enabled = false;
	spec->alt_dac_nid = spec->multiout.hp_out_nid[0];
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

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->multi_ios + 1;
	if (uinfo->value.enumerated.item > spec->multi_ios)
		uinfo->value.enumerated.item = spec->multi_ios;
	sprintf(uinfo->value.enumerated.name, "%dch",
		(uinfo->value.enumerated.item + 1) * 2);
	return 0;
}

static int ch_mode_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gen_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = (spec->ext_channel_count - 1) / 2;
	return 0;
}

static int set_multi_io(struct hda_codec *codec, int idx, bool output)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t nid = spec->multi_io[idx].pin;
	struct nid_path *path;

	path = snd_hda_get_nid_path(codec, spec->multi_io[idx].dac, nid);
	if (!path)
		return -EINVAL;

	if (path->active == output)
		return 0;

	if (output) {
		snd_hda_set_pin_ctl_cache(codec, nid, PIN_OUT);
		snd_hda_activate_path(codec, path, true, true);
		set_pin_eapd(codec, nid, true);
	} else {
		set_pin_eapd(codec, nid, false);
		snd_hda_activate_path(codec, path, false, true);
		snd_hda_set_pin_ctl_cache(codec, nid,
					  spec->multi_io[idx].ctl_in);
	}
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
	if (ch == (spec->ext_channel_count - 1) / 2)
		return 0;
	spec->ext_channel_count = (ch + 1) * 2;
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
 * shared headphone/mic handling
 */

static void call_update_outputs(struct hda_codec *codec);

/* for shared I/O, change the pin-control accordingly */
static void update_shared_mic_hp(struct hda_codec *codec, bool set_as_mic)
{
	struct hda_gen_spec *spec = codec->spec;
	unsigned int val;
	hda_nid_t pin = spec->autocfg.inputs[1].pin;
	/* NOTE: this assumes that there are only two inputs, the
	 * first is the real internal mic and the second is HP/mic jack.
	 */

	val = snd_hda_get_default_vref(codec, pin);

	/* This pin does not have vref caps - let's enable vref on pin 0x18
	   instead, as suggested by Realtek */
	if (val == AC_PINCTL_VREF_HIZ && spec->shared_mic_vref_pin) {
		const hda_nid_t vref_pin = spec->shared_mic_vref_pin;
		unsigned int vref_val = snd_hda_get_default_vref(codec, vref_pin);
		if (vref_val != AC_PINCTL_VREF_HIZ)
			snd_hda_set_pin_ctl_cache(codec, vref_pin,
					PIN_IN | (set_as_mic ? vref_val : 0));
	}

	val = set_as_mic ? val | PIN_IN : PIN_HP;
	snd_hda_set_pin_ctl_cache(codec, pin, val);

	spec->automute_speaker = !set_as_mic;
	call_update_outputs(codec);
}

/* create a shared input with the headphone out */
static int create_shared_input(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
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
	snd_printdd("hda-codec: Enable shared I/O jack on NID 0x%x\n", nid);
	return 0;
}


/*
 * Parse input paths
 */

#ifdef CONFIG_PM
/* add the powersave loopback-list entry */
static void add_loopback_list(struct hda_gen_spec *spec, hda_nid_t mix, int idx)
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
static int new_analog_input(struct hda_codec *codec, hda_nid_t pin,
			    const char *ctlname, int ctlidx,
			    hda_nid_t mix_nid)
{
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;
	unsigned int val;
	int err, idx;

	if (!nid_has_volume(codec, mix_nid, HDA_INPUT) &&
	    !nid_has_mute(codec, mix_nid, HDA_INPUT))
		return 0; /* no need for analog loopback */

	path = snd_hda_add_new_path(codec, pin, mix_nid, HDA_PARSE_ALL);
	if (!path)
		return -EINVAL;
	print_nid_path("loopback", path);

	idx = path->idx[path->depth - 1];
	if (nid_has_volume(codec, mix_nid, HDA_INPUT)) {
		val = HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT);
		err = __add_pb_vol_ctrl(spec, HDA_CTL_WIDGET_VOL, ctlname, ctlidx, val);
		if (err < 0)
			return err;
		path->ctls[NID_PATH_VOL_CTL] = val;
	}

	if (nid_has_mute(codec, mix_nid, HDA_INPUT)) {
		val = HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT);
		err = __add_pb_sw_ctrl(spec, HDA_CTL_WIDGET_MUTE, ctlname, ctlidx, val);
		if (err < 0)
			return err;
		path->ctls[NID_PATH_MUTE_CTL] = val;
	}

	path->active = true;
	add_loopback_list(spec, mix_nid, idx);
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
	return nums;
}

/* filter out invalid adc_nids that don't give all active input pins;
 * if needed, check whether dynamic ADC-switching is available
 */
static int check_dyn_adc_switch(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->input_mux;
	hda_nid_t adc_nids[ARRAY_SIZE(spec->adc_nids)];
	int i, n, nums;
	hda_nid_t pin, adc;

 again:
	nums = 0;
	for (n = 0; n < spec->num_adc_nids; n++) {
		adc = spec->adc_nids[n];
		for (i = 0; i < imux->num_items; i++) {
			pin = spec->imux_pins[i];
			if (!is_reachable_path(codec, pin, adc))
				break;
		}
		if (i >= imux->num_items)
			adc_nids[nums++] = adc;
	}

	if (!nums) {
		if (spec->shared_mic_hp) {
			spec->shared_mic_hp = 0;
			imux->num_items = 1;
			goto again;
		}

		/* check whether ADC-switch is possible */
		for (i = 0; i < imux->num_items; i++) {
			pin = spec->imux_pins[i];
			for (n = 0; n < spec->num_adc_nids; n++) {
				adc = spec->adc_nids[n];
				if (is_reachable_path(codec, pin, adc)) {
					spec->dyn_adc_idx[i] = n;
					break;
				}
			}
		}

		snd_printdd("hda-codec: enabling ADC switching\n");
		spec->dyn_adc_switch = 1;
	} else if (nums != spec->num_adc_nids) {
		memcpy(spec->adc_nids, adc_nids, nums * sizeof(hda_nid_t));
		spec->num_adc_nids = nums;
	}

	if (imux->num_items == 1 || spec->shared_mic_hp) {
		snd_printdd("hda-codec: reducing to a single ADC\n");
		spec->num_adc_nids = 1; /* reduce to a single ADC */
	}

	/* single index for individual volumes ctls */
	if (!spec->dyn_adc_switch && spec->multi_cap_vol)
		spec->num_adc_nids = 1;

	return 0;
}

/*
 * create playback/capture controls for input pins
 */
static int create_input_ctls(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	const struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t mixer = spec->mixer_nid;
	struct hda_input_mux *imux = &spec->input_mux;
	int num_adcs;
	int i, c, err, type_idx = 0;
	const char *prev_label = NULL;

	num_adcs = fill_adc_nids(codec);
	if (num_adcs < 0)
		return 0;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin;
		const char *label;
		bool imux_added;

		pin = cfg->inputs[i].pin;
		if (!is_input_pin(codec, pin))
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
			if (is_reachable_path(codec, pin, mixer)) {
				err = new_analog_input(codec, pin,
						       label, type_idx, mixer);
				if (err < 0)
					return err;
			}
		}

		imux_added = false;
		for (c = 0; c < num_adcs; c++) {
			struct nid_path *path;
			hda_nid_t adc = spec->adc_nids[c];

			if (!is_reachable_path(codec, pin, adc))
				continue;
			path = snd_array_new(&spec->paths);
			if (!path)
				return -ENOMEM;
			memset(path, 0, sizeof(*path));
			if (!snd_hda_parse_nid_path(codec, pin, adc, HDA_PARSE_ALL, path)) {
				snd_printd(KERN_ERR
					   "invalid input path 0x%x -> 0x%x\n",
					   pin, adc);
				spec->paths.used--;
				continue;
			}
			print_nid_path("input", path);

			if (!imux_added) {
				spec->imux_pins[imux->num_items] = pin;
				snd_hda_add_imux_item(imux, label,
						      imux->num_items, NULL);
				imux_added = true;
			}
		}
	}

	return 0;
}


/*
 * input source mux
 */

/* get the ADC NID corresponding to the given index */
static hda_nid_t get_adc_nid(struct hda_codec *codec, int adc_idx, int imux_idx)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->dyn_adc_switch)
		adc_idx = spec->dyn_adc_idx[imux_idx];
	return spec->adc_nids[adc_idx];
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
	adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	mutex_lock(&codec->control_mutex);
	/* we use the cache-only update at first since multiple input paths
	 * may shared the same amp; by updating only caches, the redundant
	 * writes to hardware can be reduced.
	 */
	codec->cached_write = 1;
	for (i = 0; i < imux->num_items; i++) {
		path = snd_hda_get_nid_path(codec, spec->imux_pins[i],
					    get_adc_nid(codec, adc_idx, i));
		if (!path->ctls[type])
			continue;
		kcontrol->private_value = path->ctls[type];
		err = func(kcontrol, ucontrol);
		if (err < 0)
			goto error;
	}
 error:
	codec->cached_write = 0;
	mutex_unlock(&codec->control_mutex);
	snd_hda_codec_flush_amp_cache(codec); /* flush the updates */
	if (err >= 0 && spec->cap_sync_hook)
		spec->cap_sync_hook(codec);
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

static int add_single_cap_ctl(struct hda_codec *codec, const char *label,
			      int idx, bool is_switch, unsigned int ctl,
			      bool inv_dmic)
{
	struct hda_gen_spec *spec = codec->spec;
	char tmpname[44];
	int type = is_switch ? HDA_CTL_WIDGET_MUTE : HDA_CTL_WIDGET_VOL;
	const char *sfx = is_switch ? "Switch" : "Volume";
	unsigned int chs = inv_dmic ? 1 : 3;
	int err;

	if (!ctl)
		return 0;

	if (label)
		snprintf(tmpname, sizeof(tmpname),
			 "%s Capture %s", label, sfx);
	else
		snprintf(tmpname, sizeof(tmpname),
			 "Capture %s", sfx);
	err = add_control(spec, type, tmpname, idx,
			  amp_val_replace_channels(ctl, chs));
	if (err < 0 || !inv_dmic)
		return err;

	/* Make independent right kcontrol */
	if (label)
		snprintf(tmpname, sizeof(tmpname),
			 "Inverted %s Capture %s", label, sfx);
	else
		snprintf(tmpname, sizeof(tmpname),
			 "Inverted Capture %s", sfx);
	return add_control(spec, type, tmpname, idx,
			   amp_val_replace_channels(ctl, 2));
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
	struct hda_gen_spec *spec = codec->spec;
	struct nid_path *path;
	unsigned int ctl;
	int i;

	path = snd_hda_get_nid_path(codec, spec->imux_pins[idx],
				    get_adc_nid(codec, 0, idx));
	if (!path)
		return 0;
	ctl = path->ctls[type];
	if (!ctl)
		return 0;
	for (i = 0; i < idx - 1; i++) {
		path = snd_hda_get_nid_path(codec, spec->imux_pins[i],
					    get_adc_nid(codec, 0, i));
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
	int i, err, type, type_idx = 0;
	const char *prev_label = NULL;

	for (i = 0; i < imux->num_items; i++) {
		const char *label;
		bool inv_dmic;
		label = hda_get_autocfg_input_label(codec, &spec->autocfg, i);
		if (prev_label && !strcmp(label, prev_label))
			type_idx++;
		else
			type_idx = 0;
		prev_label = label;
		inv_dmic = is_inv_dmic_pin(codec, spec->imux_pins[i]);

		for (type = 0; type < 2; type++) {
			err = add_single_cap_ctl(codec, label, type_idx, type,
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
		bool inv_dmic = false;
		int vol, sw;

		vol = sw = 0;
		for (i = 0; i < imux->num_items; i++) {
			struct nid_path *path;
			path = snd_hda_get_nid_path(codec, spec->imux_pins[i],
						    get_adc_nid(codec, n, i));
			if (!path)
				continue;
			parse_capvol_in_path(codec, path);
			if (!vol)
				vol = path->ctls[NID_PATH_VOL_CTL];
			else if (vol != path->ctls[NID_PATH_VOL_CTL])
				multi = true;
			if (!sw)
				sw = path->ctls[NID_PATH_MUTE_CTL];
			else if (sw != path->ctls[NID_PATH_MUTE_CTL])
				multi = true;
			if (is_inv_dmic_pin(codec, spec->imux_pins[i]))
				inv_dmic = true;
		}

		if (!multi)
			err = create_single_cap_vol_ctl(codec, n, vol, sw,
							inv_dmic);
		else if (!spec->multi_cap_vol)
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
static int parse_mic_boost(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
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
			struct nid_path *path;
			unsigned int val;

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
			val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT);
			err = add_control(spec, HDA_CTL_WIDGET_VOL,
					  boost_label, type_idx, val);
			if (err < 0)
				return err;

			path = snd_hda_get_nid_path(codec, nid, 0);
			if (path)
				path->ctls[NID_PATH_BOOST_CTL] = val;
		}
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
	hda_nid_t dig_nid;

	/* support multiple SPDIFs; the secondary is set up as a slave */
	nums = 0;
	for (i = 0; i < spec->autocfg.dig_outs; i++) {
		hda_nid_t pin = spec->autocfg.dig_out_pins[i];
		dig_nid = look_for_dac(codec, pin, true);
		if (!dig_nid)
			continue;
		path = snd_hda_add_new_path(codec, dig_nid, pin, HDA_PARSE_ALL);
		if (!path)
			continue;
		print_nid_path("digout", path);
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
			path = snd_hda_add_new_path(codec,
						    spec->autocfg.dig_in_pin,
						    dig_nid, HDA_PARSE_ALL);
			if (path) {
				print_nid_path("digin", path);
				path->active = true;
				spec->dig_in_nid = dig_nid;
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
	struct nid_path *path;

	imux = &spec->input_mux;
	if (!imux->num_items)
		return 0;

	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (spec->cur_mux[adc_idx] == idx)
		return 0;

	path = snd_hda_get_nid_path(codec,
				    spec->imux_pins[spec->cur_mux[adc_idx]],
				    spec->adc_nids[adc_idx]);
	if (!path)
		return 0;
	if (path->active)
		snd_hda_activate_path(codec, path, false, false);

	spec->cur_mux[adc_idx] = idx;

	if (spec->shared_mic_hp)
		update_shared_mic_hp(codec, spec->cur_mux[adc_idx]);

	if (spec->dyn_adc_switch)
		dyn_adc_pcm_resetup(codec, idx);

	path = snd_hda_get_nid_path(codec, spec->imux_pins[idx],
				    get_adc_nid(codec, adc_idx, idx));
	if (!path)
		return 0;
	if (path->active)
		return 0;
	snd_hda_activate_path(codec, path, true, false);
	if (spec->cap_sync_hook)
		spec->cap_sync_hook(codec);
	return 1;
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
	struct hda_gen_spec *spec = codec->spec;
	unsigned int pin_bits = mute ? 0 : (hp_out ? PIN_HP : PIN_OUT);
	int i;

	for (i = 0; i < num_pins; i++) {
		hda_nid_t nid = pins[i];
		unsigned int val;
		if (!nid)
			break;
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
		snd_hda_set_pin_ctl_cache(codec, nid, val);
		set_pin_eapd(codec, nid, !mute);
	}
}

/* Toggle outputs muting */
void snd_hda_gen_update_outputs(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
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
EXPORT_SYMBOL_HDA(snd_hda_gen_update_outputs);

static void call_update_outputs(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	if (spec->automute_hook)
		spec->automute_hook(codec);
	else
		snd_hda_gen_update_outputs(codec);
}

/* standard HP-automute helper */
void snd_hda_gen_hp_automute(struct hda_codec *codec, struct hda_jack_tbl *jack)
{
	struct hda_gen_spec *spec = codec->spec;

	spec->hp_jack_present =
		detect_jacks(codec, ARRAY_SIZE(spec->autocfg.hp_pins),
			     spec->autocfg.hp_pins);
	if (!spec->detect_hp || (!spec->automute_speaker && !spec->automute_lo))
		return;
	call_update_outputs(codec);
}
EXPORT_SYMBOL_HDA(snd_hda_gen_hp_automute);

/* standard line-out-automute helper */
void snd_hda_gen_line_automute(struct hda_codec *codec, struct hda_jack_tbl *jack)
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
EXPORT_SYMBOL_HDA(snd_hda_gen_line_automute);

/* standard mic auto-switch helper */
void snd_hda_gen_mic_autoswitch(struct hda_codec *codec, struct hda_jack_tbl *jack)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	if (!spec->auto_mic)
		return;

	for (i = spec->am_num_entries - 1; i > 0; i--) {
		if (snd_hda_jack_detect(codec, spec->am_entry[i].pin)) {
			mux_select(codec, 0, spec->am_entry[i].idx);
			return;
		}
	}
	mux_select(codec, 0, spec->am_entry[0].idx);
}
EXPORT_SYMBOL_HDA(snd_hda_gen_mic_autoswitch);

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
		snd_printdd("hda-codec: Enable HP auto-muting on NID 0x%x\n",
			    nid);
		snd_hda_jack_detect_enable_callback(codec, nid, HDA_GEN_HP_EVENT,
						    snd_hda_gen_hp_automute);
		spec->detect_hp = 1;
	}

	if (cfg->line_out_type == AUTO_PIN_LINE_OUT && cfg->line_outs) {
		if (cfg->speaker_outs)
			for (i = 0; i < cfg->line_outs; i++) {
				hda_nid_t nid = cfg->line_out_pins[i];
				if (!is_jack_detectable(codec, nid))
					continue;
				snd_printdd("hda-codec: Enable Line-Out auto-muting on NID 0x%x\n", nid);
				snd_hda_jack_detect_enable_callback(codec, nid,
								    HDA_GEN_FRONT_EVENT,
								    snd_hda_gen_line_automute);
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

/* return the position of NID in the list, or -1 if not found */
static int find_idx_in_nid_list(hda_nid_t nid, const hda_nid_t *list, int nums)
{
	int i;
	for (i = 0; i < nums; i++)
		if (list[i] == nid)
			return i;
	return -1;
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
						    HDA_GEN_MIC_EVENT,
						    snd_hda_gen_mic_autoswitch);
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
	snd_printdd("hda-codec: Enable auto-mic switch on NID 0x%x/0x%x/0x%x\n",
		    spec->am_entry[0].pin,
		    spec->am_entry[1].pin,
		    spec->am_entry[2].pin);

	return 0;
}


/*
 * Parse the given BIOS configuration and set up the hda_gen_spec
 *
 * return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
int snd_hda_gen_parse_auto_config(struct hda_codec *codec,
				  struct auto_pin_cfg *cfg)
{
	struct hda_gen_spec *spec = codec->spec;
	int err;

	if (cfg != &spec->autocfg) {
		spec->autocfg = *cfg;
		cfg = &spec->autocfg;
	}

	if (!cfg->line_outs) {
		if (cfg->dig_outs || cfg->dig_in_pin) {
			spec->multiout.max_channels = 2;
			spec->no_analog = 1;
			goto dig_only;
		}
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
	err = create_shared_input(codec);
	if (err < 0)
		return err;
	err = create_input_ctls(codec);
	if (err < 0)
		return err;

	/* check the multiple speaker pins */
	if (cfg->line_out_type == AUTO_PIN_SPEAKER_OUT)
		spec->const_channel_count = cfg->line_outs * 2;
	else
		spec->const_channel_count = cfg->speaker_outs * 2;

	if (spec->multi_ios > 0)
		spec->multiout.max_channels = max(spec->ext_channel_count,
						  spec->const_channel_count);
	else
		spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	err = check_auto_mute_availability(codec);
	if (err < 0)
		return err;

	err = check_dyn_adc_switch(codec);
	if (err < 0)
		return err;

	if (!spec->shared_mic_hp) {
		err = check_auto_mic_availability(codec);
		if (err < 0)
			return err;
	}

	err = create_capture_mixers(codec);
	if (err < 0)
		return err;

	err = parse_mic_boost(codec);
	if (err < 0)
		return err;

 dig_only:
	parse_digital(codec);

	return 1;
}
EXPORT_SYMBOL_HDA(snd_hda_gen_parse_auto_config);


/*
 * Build control elements
 */

/* slave controls for virtual master */
static const char * const slave_pfxs[] = {
	"Front", "Surround", "Center", "LFE", "Side",
	"Headphone", "Speaker", "Mono", "Line Out",
	"CLFE", "Bass Speaker", "PCM",
	NULL,
};

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
		unsigned int vmaster_tlv[4];
		snd_hda_set_vmaster_tlv(codec, spec->vmaster_nid,
					HDA_OUTPUT, vmaster_tlv);
		err = snd_hda_add_vmaster(codec, "Master Playback Volume",
					  vmaster_tlv, slave_pfxs,
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
		if (spec->vmaster_mute.hook)
			snd_hda_add_vmaster_hook(codec, &spec->vmaster_mute,
						 spec->vmaster_mute_enum);
	}

	free_kctls(spec); /* no longer needed */

	if (spec->shared_mic_hp) {
		int err;
		int nid = spec->autocfg.inputs[1].pin;
		err = snd_hda_jack_add_kctl(codec, nid, "Headphone Mic", 0);
		if (err < 0)
			return err;
		err = snd_hda_jack_detect_enable(codec, nid, 0);
		if (err < 0)
			return err;
	}

	err = snd_hda_jack_add_kctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_gen_build_controls);


/*
 * PCM definitions
 */

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
	if (!err)
		spec->active_streams |= 1 << STREAM_MULTI_OUT;
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
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag, format, substream);
}

static int playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				struct hda_codec *codec,
				struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

static int playback_pcm_close(struct hda_pcm_stream *hinfo,
			      struct hda_codec *codec,
			      struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;
	mutex_lock(&spec->pcm_mutex);
	spec->active_streams &= ~(1 << STREAM_MULTI_OUT);
	mutex_unlock(&spec->pcm_mutex);
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
	mutex_unlock(&spec->pcm_mutex);
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
static int alt_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   unsigned int stream_tag,
				   unsigned int format,
				   struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number + 1],
				   stream_tag, 0, format);
	return 0;
}

static int alt_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	struct hda_gen_spec *spec = codec->spec;

	snd_hda_codec_cleanup_stream(codec,
				     spec->adc_nids[substream->number + 1]);
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
};

static const struct hda_pcm_stream pcm_analog_alt_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in build_pcms */
	.ops = {
		.open = alt_playback_pcm_open,
		.close = alt_playback_pcm_close
	},
};

static const struct hda_pcm_stream pcm_analog_alt_capture = {
	.substreams = 2, /* can be overridden */
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in build_pcms */
	.ops = {
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

/* build PCM streams based on the parsed results */
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
		codec->num_pcms = 3;
		info = spec->pcm_rec + 2;
		info->name = spec->stream_name_analog;
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
EXPORT_SYMBOL_HDA(snd_hda_gen_build_pcms);


/*
 * Standard auto-parser initializations
 */

/* configure the path from the given dac to the pin as the proper output */
static void set_output_and_unmute(struct hda_codec *codec, hda_nid_t pin,
				  int pin_type, hda_nid_t dac)
{
	struct nid_path *path;

	snd_hda_set_pin_ctl_cache(codec, pin, pin_type);
	path = snd_hda_get_nid_path(codec, dac, pin);
	if (!path)
		return;
	if (path->active)
		return;
	snd_hda_activate_path(codec, path, true, true);
	set_pin_eapd(codec, pin, true);
}

/* initialize primary output paths */
static void init_multi_out(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	hda_nid_t nid, dac;
	int pin_type;
	int i;

	if (spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)
		pin_type = PIN_HP;
	else
		pin_type = PIN_OUT;

	for (i = 0; i < spec->autocfg.line_outs; i++) {
		nid = spec->autocfg.line_out_pins[i];
		if (nid) {
			dac = spec->multiout.dac_nids[i];
			if (!dac)
				dac = spec->multiout.dac_nids[0];
			set_output_and_unmute(codec, nid, pin_type, dac);
		}
	}
}


static void __init_extra_out(struct hda_codec *codec, int num_outs,
			     hda_nid_t *pins, hda_nid_t *dacs, int type)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	hda_nid_t pin, dac;

	for (i = 0; i < num_outs; i++) {
		pin = pins[i];
		if (!pin)
			break;
		dac = dacs[i];
		if (!dac) {
			if (i > 0 && dacs[0])
				dac = dacs[0];
			else
				dac = spec->multiout.dac_nids[0];
		}
		set_output_and_unmute(codec, pin, type, dac);
	}
}

/* initialize hp and speaker paths */
static void init_extra_out(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;

	if (spec->autocfg.line_out_type != AUTO_PIN_HP_OUT)
		__init_extra_out(codec, spec->autocfg.hp_outs,
				 spec->autocfg.hp_pins,
				 spec->multiout.hp_out_nid, PIN_HP);
	if (spec->autocfg.line_out_type != AUTO_PIN_SPEAKER_OUT)
		__init_extra_out(codec, spec->autocfg.speaker_outs,
				 spec->autocfg.speaker_pins,
				 spec->multiout.extra_out_nid, PIN_OUT);
}

/* initialize multi-io paths */
static void init_multi_io(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->multi_ios; i++) {
		hda_nid_t pin = spec->multi_io[i].pin;
		struct nid_path *path;
		path = snd_hda_get_nid_path(codec, spec->multi_io[i].dac, pin);
		if (!path)
			continue;
		if (!spec->multi_io[i].ctl_in)
			spec->multi_io[i].ctl_in =
				snd_hda_codec_update_cache(codec, pin, 0,
					   AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_activate_path(codec, path, path->active, true);
	}
}

/* set up the input pin config, depending on the given auto-pin type */
static void set_input_pin(struct hda_codec *codec, hda_nid_t nid,
			  int auto_pin_type)
{
	unsigned int val = PIN_IN;
	if (auto_pin_type == AUTO_PIN_MIC)
		val |= snd_hda_get_default_vref(codec, nid);
	snd_hda_set_pin_ctl_cache(codec, nid, val);
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
			set_input_pin(codec, nid, cfg->inputs[i].type);

		/* init loopback inputs */
		if (spec->mixer_nid) {
			struct nid_path *path;
			path = snd_hda_get_nid_path(codec, nid, spec->mixer_nid);
			if (path)
				snd_hda_activate_path(codec, path,
						      path->active, false);
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
			path = snd_hda_get_nid_path(codec, spec->imux_pins[i],
						    get_adc_nid(codec, c, i));
			if (path) {
				bool active = path->active;
				if (i == spec->cur_mux[c])
					active = true;
				snd_hda_activate_path(codec, path, active, false);
			}
		}
	}

	if (spec->shared_mic_hp)
		update_shared_mic_hp(codec, spec->cur_mux[0]);

	if (spec->cap_sync_hook)
		spec->cap_sync_hook(codec);
}

/* set right pin controls for digital I/O */
static void init_digital(struct hda_codec *codec)
{
	struct hda_gen_spec *spec = codec->spec;
	int i;
	hda_nid_t pin;

	for (i = 0; i < spec->autocfg.dig_outs; i++) {
		pin = spec->autocfg.dig_out_pins[i];
		if (!pin)
			continue;
		set_output_and_unmute(codec, pin, PIN_OUT, 0);
	}
	pin = spec->autocfg.dig_in_pin;
	if (pin)
		snd_hda_set_pin_ctl_cache(codec, pin, PIN_IN);
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
	init_analog_input(codec);
	init_input_src(codec);
	init_digital(codec);

	clear_unsol_on_unused_pins(codec);

	/* call init functions of standard auto-mute helpers */
	snd_hda_gen_hp_automute(codec, NULL);
	snd_hda_gen_line_automute(codec, NULL);
	snd_hda_gen_mic_autoswitch(codec, NULL);

	snd_hda_codec_flush_amp_cache(codec);
	snd_hda_codec_flush_cmd_cache(codec);

	if (spec->vmaster_mute.sw_kctl && spec->vmaster_mute.hook)
		snd_hda_sync_vmaster_hook(&spec->vmaster_mute);

	hda_call_check_power_status(codec, 0x01);
	return 0;
}
EXPORT_SYMBOL(snd_hda_gen_init);


/*
 * the generic codec support
 */

#ifdef CONFIG_PM
static int generic_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_gen_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
}
#endif

static void generic_free(struct hda_codec *codec)
{
	snd_hda_gen_spec_free(codec->spec);
	kfree(codec->spec);
	codec->spec = NULL;
}

static const struct hda_codec_ops generic_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.free = generic_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.check_power_status = generic_check_power_status,
#endif
};

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
	generic_free(codec);
	return err;
}
EXPORT_SYMBOL(snd_hda_parse_generic_codec);
