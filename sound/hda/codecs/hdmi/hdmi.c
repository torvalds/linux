// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  hdmi.c - routines for HDMI/DisplayPort codecs
 *
 *  Copyright(c) 2008-2010 Intel Corporation
 *  Copyright (c) 2006 ATI Technologies Inc.
 *  Copyright (c) 2008 NVIDIA Corp.  All rights reserved.
 *  Copyright (c) 2008 Wei Ni <wni@nvidia.com>
 *  Copyright (c) 2013 Anssi Hannula <anssi.hannula@iki.fi>
 *
 *  Authors:
 *			Wu Fengguang <wfg@linux.intel.com>
 *
 *  Maintained by:
 *			Wu Fengguang <wfg@linux.intel.com>
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/asoundef.h>
#include <sound/tlv.h>
#include <sound/hdaudio.h>
#include <sound/hda_i915.h>
#include <sound/hda_chmap.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_jack.h"
#include "hda_controller.h"
#include "hdmi_local.h"

static bool static_hdmi_pcm;
module_param(static_hdmi_pcm, bool, 0644);
MODULE_PARM_DESC(static_hdmi_pcm, "Don't restrict PCM parameters per ELD info");

static bool enable_acomp = true;
module_param(enable_acomp, bool, 0444);
MODULE_PARM_DESC(enable_acomp, "Enable audio component binding (default=yes)");

static bool enable_all_pins;
module_param(enable_all_pins, bool, 0444);
MODULE_PARM_DESC(enable_all_pins, "Forcibly enable all pins");

int snd_hda_hdmi_pin_id_to_pin_index(struct hda_codec *codec,
				     hda_nid_t pin_nid, int dev_id)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;
	struct hdmi_spec_per_pin *per_pin;

	/*
	 * (dev_id == -1) means it is NON-MST pin
	 * return the first virtual pin on this port
	 */
	if (dev_id == -1)
		dev_id = 0;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		per_pin = get_pin(spec, pin_idx);
		if ((per_pin->pin_nid == pin_nid) &&
			(per_pin->dev_id == dev_id))
			return pin_idx;
	}

	codec_warn(codec, "HDMI: pin NID 0x%x not registered\n", pin_nid);
	return -EINVAL;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_pin_id_to_pin_index, "SND_HDA_CODEC_HDMI");

static int hinfo_to_pcm_index(struct hda_codec *codec,
			struct hda_pcm_stream *hinfo)
{
	struct hdmi_spec *spec = codec->spec;
	int pcm_idx;

	for (pcm_idx = 0; pcm_idx < spec->pcm_used; pcm_idx++)
		if (get_pcm_rec(spec, pcm_idx)->stream == hinfo)
			return pcm_idx;

	codec_warn(codec, "HDMI: hinfo %p not tied to a PCM\n", hinfo);
	return -EINVAL;
}

static int hinfo_to_pin_index(struct hda_codec *codec,
			      struct hda_pcm_stream *hinfo)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin;
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		per_pin = get_pin(spec, pin_idx);
		if (per_pin->pcm &&
			per_pin->pcm->pcm->stream == hinfo)
			return pin_idx;
	}

	codec_dbg(codec, "HDMI: hinfo %p (pcm %d) not registered\n", hinfo,
		  hinfo_to_pcm_index(codec, hinfo));
	return -EINVAL;
}

static struct hdmi_spec_per_pin *pcm_idx_to_pin(struct hdmi_spec *spec,
						int pcm_idx)
{
	int i;
	struct hdmi_spec_per_pin *per_pin;

	for (i = 0; i < spec->num_pins; i++) {
		per_pin = get_pin(spec, i);
		if (per_pin->pcm_idx == pcm_idx)
			return per_pin;
	}
	return NULL;
}

static int cvt_nid_to_cvt_index(struct hda_codec *codec, hda_nid_t cvt_nid)
{
	struct hdmi_spec *spec = codec->spec;
	int cvt_idx;

	for (cvt_idx = 0; cvt_idx < spec->num_cvts; cvt_idx++)
		if (get_cvt(spec, cvt_idx)->cvt_nid == cvt_nid)
			return cvt_idx;

	codec_warn(codec, "HDMI: cvt NID 0x%x not registered\n", cvt_nid);
	return -EINVAL;
}

static int hdmi_eld_ctl_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin;
	struct hdmi_eld *eld;
	int pcm_idx;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;

	pcm_idx = kcontrol->private_value;
	guard(mutex)(&spec->pcm_lock);
	per_pin = pcm_idx_to_pin(spec, pcm_idx);
	if (!per_pin) {
		/* no pin is bound to the pcm */
		uinfo->count = 0;
		return 0;
	}
	eld = &per_pin->sink_eld;
	uinfo->count = eld->eld_valid ? eld->eld_size : 0;
	return 0;
}

static int hdmi_eld_ctl_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin;
	struct hdmi_eld *eld;
	int pcm_idx;

	pcm_idx = kcontrol->private_value;
	guard(mutex)(&spec->pcm_lock);
	per_pin = pcm_idx_to_pin(spec, pcm_idx);
	if (!per_pin) {
		/* no pin is bound to the pcm */
		memset(ucontrol->value.bytes.data, 0,
		       ARRAY_SIZE(ucontrol->value.bytes.data));
		return 0;
	}

	eld = &per_pin->sink_eld;
	if (eld->eld_size > ARRAY_SIZE(ucontrol->value.bytes.data) ||
	    eld->eld_size > ELD_MAX_SIZE) {
		snd_BUG();
		return -EINVAL;
	}

	memset(ucontrol->value.bytes.data, 0,
	       ARRAY_SIZE(ucontrol->value.bytes.data));
	if (eld->eld_valid)
		memcpy(ucontrol->value.bytes.data, eld->eld_buffer,
		       eld->eld_size);
	return 0;
}

static const struct snd_kcontrol_new eld_bytes_ctl = {
	.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE |
		SNDRV_CTL_ELEM_ACCESS_SKIP_CHECK,
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = "ELD",
	.info = hdmi_eld_ctl_info,
	.get = hdmi_eld_ctl_get,
};

static int hdmi_create_eld_ctl(struct hda_codec *codec, int pcm_idx,
			int device)
{
	struct snd_kcontrol *kctl;
	struct hdmi_spec *spec = codec->spec;
	int err;

	kctl = snd_ctl_new1(&eld_bytes_ctl, codec);
	if (!kctl)
		return -ENOMEM;
	kctl->private_value = pcm_idx;
	kctl->id.device = device;

	/* no pin nid is associated with the kctl now
	 * tbd: associate pin nid to eld ctl later
	 */
	err = snd_hda_ctl_add(codec, 0, kctl);
	if (err < 0)
		return err;

	get_hdmi_pcm(spec, pcm_idx)->eld_ctl = kctl;
	return 0;
}

#ifdef BE_PARANOID
static void hdmi_get_dip_index(struct hda_codec *codec, hda_nid_t pin_nid,
				int *packet_index, int *byte_index)
{
	int val;

	val = snd_hda_codec_read(codec, pin_nid, 0,
				 AC_VERB_GET_HDMI_DIP_INDEX, 0);

	*packet_index = val >> 5;
	*byte_index = val & 0x1f;
}
#endif

static void hdmi_set_dip_index(struct hda_codec *codec, hda_nid_t pin_nid,
				int packet_index, int byte_index)
{
	int val;

	val = (packet_index << 5) | (byte_index & 0x1f);

	snd_hda_codec_write(codec, pin_nid, 0, AC_VERB_SET_HDMI_DIP_INDEX, val);
}

static void hdmi_write_dip_byte(struct hda_codec *codec, hda_nid_t pin_nid,
				unsigned char val)
{
	snd_hda_codec_write(codec, pin_nid, 0, AC_VERB_SET_HDMI_DIP_DATA, val);
}

static void hdmi_init_pin(struct hda_codec *codec, hda_nid_t pin_nid)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_out;

	/* Unmute */
	if (get_wcaps(codec, pin_nid) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, pin_nid, 0,
				AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);

	if (spec->dyn_pin_out)
		/* Disable pin out until stream is active */
		pin_out = 0;
	else
		/* Enable pin out: some machines with GM965 gets broken output
		 * when the pin is disabled or changed while using with HDMI
		 */
		pin_out = PIN_OUT;

	snd_hda_codec_write(codec, pin_nid, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, pin_out);
}

/*
 * ELD proc files
 */

#ifdef CONFIG_SND_PROC_FS
static void print_eld_info(struct snd_info_entry *entry,
			   struct snd_info_buffer *buffer)
{
	struct hdmi_spec_per_pin *per_pin = entry->private_data;

	guard(mutex)(&per_pin->lock);
	snd_hdmi_print_eld_info(&per_pin->sink_eld, buffer, per_pin->pin_nid,
				per_pin->dev_id, per_pin->cvt_nid);
}

static void write_eld_info(struct snd_info_entry *entry,
			   struct snd_info_buffer *buffer)
{
	struct hdmi_spec_per_pin *per_pin = entry->private_data;

	guard(mutex)(&per_pin->lock);
	snd_hdmi_write_eld_info(&per_pin->sink_eld, buffer);
}

static int eld_proc_new(struct hdmi_spec_per_pin *per_pin, int index)
{
	char name[32];
	struct hda_codec *codec = per_pin->codec;
	struct snd_info_entry *entry;
	int err;

	snprintf(name, sizeof(name), "eld#%d.%d", codec->addr, index);
	err = snd_card_proc_new(codec->card, name, &entry);
	if (err < 0)
		return err;

	snd_info_set_text_ops(entry, per_pin, print_eld_info);
	entry->c.text.write = write_eld_info;
	entry->mode |= 0200;
	per_pin->proc_entry = entry;

	return 0;
}

static void eld_proc_free(struct hdmi_spec_per_pin *per_pin)
{
	if (!per_pin->codec->bus->shutdown) {
		snd_info_free_entry(per_pin->proc_entry);
		per_pin->proc_entry = NULL;
	}
}
#else
static inline int eld_proc_new(struct hdmi_spec_per_pin *per_pin,
			       int index)
{
	return 0;
}
static inline void eld_proc_free(struct hdmi_spec_per_pin *per_pin)
{
}
#endif

/*
 * Audio InfoFrame routines
 */

/*
 * Enable Audio InfoFrame Transmission
 */
static void hdmi_start_infoframe_trans(struct hda_codec *codec,
				       hda_nid_t pin_nid)
{
	hdmi_set_dip_index(codec, pin_nid, 0x0, 0x0);
	snd_hda_codec_write(codec, pin_nid, 0, AC_VERB_SET_HDMI_DIP_XMIT,
						AC_DIPXMIT_BEST);
}

/*
 * Disable Audio InfoFrame Transmission
 */
static void hdmi_stop_infoframe_trans(struct hda_codec *codec,
				      hda_nid_t pin_nid)
{
	hdmi_set_dip_index(codec, pin_nid, 0x0, 0x0);
	snd_hda_codec_write(codec, pin_nid, 0, AC_VERB_SET_HDMI_DIP_XMIT,
						AC_DIPXMIT_DISABLE);
}

static void hdmi_debug_dip_size(struct hda_codec *codec, hda_nid_t pin_nid)
{
#ifdef CONFIG_SND_DEBUG_VERBOSE
	int i;
	int size;

	size = snd_hdmi_get_eld_size(codec, pin_nid);
	codec_dbg(codec, "HDMI: ELD buf size is %d\n", size);

	for (i = 0; i < 8; i++) {
		size = snd_hda_codec_read(codec, pin_nid, 0,
						AC_VERB_GET_HDMI_DIP_SIZE, i);
		codec_dbg(codec, "HDMI: DIP GP[%d] buf size is %d\n", i, size);
	}
#endif
}

static void hdmi_clear_dip_buffers(struct hda_codec *codec, hda_nid_t pin_nid)
{
#ifdef BE_PARANOID
	int i, j;
	int size;
	int pi, bi;
	for (i = 0; i < 8; i++) {
		size = snd_hda_codec_read(codec, pin_nid, 0,
						AC_VERB_GET_HDMI_DIP_SIZE, i);
		if (size == 0)
			continue;

		hdmi_set_dip_index(codec, pin_nid, i, 0x0);
		for (j = 1; j < 1000; j++) {
			hdmi_write_dip_byte(codec, pin_nid, 0x0);
			hdmi_get_dip_index(codec, pin_nid, &pi, &bi);
			if (pi != i)
				codec_dbg(codec, "dip index %d: %d != %d\n",
						bi, pi, i);
			if (bi == 0) /* byte index wrapped around */
				break;
		}
		codec_dbg(codec,
			"HDMI: DIP GP[%d] buf reported size=%d, written=%d\n",
			i, size, j);
	}
#endif
}

static void hdmi_checksum_audio_infoframe(struct hdmi_audio_infoframe *hdmi_ai)
{
	u8 *bytes = (u8 *)hdmi_ai;
	u8 sum = 0;
	int i;

	hdmi_ai->checksum = 0;

	for (i = 0; i < sizeof(*hdmi_ai); i++)
		sum += bytes[i];

	hdmi_ai->checksum = -sum;
}

static void hdmi_fill_audio_infoframe(struct hda_codec *codec,
				      hda_nid_t pin_nid,
				      u8 *dip, int size)
{
	int i;

	hdmi_debug_dip_size(codec, pin_nid);
	hdmi_clear_dip_buffers(codec, pin_nid); /* be paranoid */

	hdmi_set_dip_index(codec, pin_nid, 0x0, 0x0);
	for (i = 0; i < size; i++)
		hdmi_write_dip_byte(codec, pin_nid, dip[i]);
}

static bool hdmi_infoframe_uptodate(struct hda_codec *codec, hda_nid_t pin_nid,
				    u8 *dip, int size)
{
	u8 val;
	int i;

	hdmi_set_dip_index(codec, pin_nid, 0x0, 0x0);
	if (snd_hda_codec_read(codec, pin_nid, 0, AC_VERB_GET_HDMI_DIP_XMIT, 0)
							    != AC_DIPXMIT_BEST)
		return false;

	for (i = 0; i < size; i++) {
		val = snd_hda_codec_read(codec, pin_nid, 0,
					 AC_VERB_GET_HDMI_DIP_DATA, 0);
		if (val != dip[i])
			return false;
	}

	return true;
}

static int hdmi_pin_get_eld(struct hda_codec *codec, hda_nid_t nid,
			    int dev_id, unsigned char *buf, int *eld_size)
{
	snd_hda_set_dev_select(codec, nid, dev_id);

	return snd_hdmi_get_eld(codec, nid, buf, eld_size);
}

static void hdmi_pin_setup_infoframe(struct hda_codec *codec,
				     hda_nid_t pin_nid, int dev_id,
				     int ca, int active_channels,
				     int conn_type)
{
	struct hdmi_spec *spec = codec->spec;
	union audio_infoframe ai;

	memset(&ai, 0, sizeof(ai));
	if ((conn_type == 0) || /* HDMI */
		/* Nvidia DisplayPort: Nvidia HW expects same layout as HDMI */
		(conn_type == 1 && spec->nv_dp_workaround)) {
		struct hdmi_audio_infoframe *hdmi_ai = &ai.hdmi;

		if (conn_type == 0) { /* HDMI */
			hdmi_ai->type		= 0x84;
			hdmi_ai->ver		= 0x01;
			hdmi_ai->len		= 0x0a;
		} else {/* Nvidia DP */
			hdmi_ai->type		= 0x84;
			hdmi_ai->ver		= 0x1b;
			hdmi_ai->len		= 0x11 << 2;
		}
		hdmi_ai->CC02_CT47	= active_channels - 1;
		hdmi_ai->CA		= ca;
		hdmi_checksum_audio_infoframe(hdmi_ai);
	} else if (conn_type == 1) { /* DisplayPort */
		struct dp_audio_infoframe *dp_ai = &ai.dp;

		dp_ai->type		= 0x84;
		dp_ai->len		= 0x1b;
		dp_ai->ver		= 0x11 << 2;
		dp_ai->CC02_CT47	= active_channels - 1;
		dp_ai->CA		= ca;
	} else {
		codec_dbg(codec, "HDMI: unknown connection type at pin NID 0x%x\n", pin_nid);
		return;
	}

	snd_hda_set_dev_select(codec, pin_nid, dev_id);

	/*
	 * sizeof(ai) is used instead of sizeof(*hdmi_ai) or
	 * sizeof(*dp_ai) to avoid partial match/update problems when
	 * the user switches between HDMI/DP monitors.
	 */
	if (!hdmi_infoframe_uptodate(codec, pin_nid, ai.bytes,
					sizeof(ai))) {
		codec_dbg(codec, "%s: pin NID=0x%x channels=%d ca=0x%02x\n",
			  __func__, pin_nid, active_channels, ca);
		hdmi_stop_infoframe_trans(codec, pin_nid);
		hdmi_fill_audio_infoframe(codec, pin_nid,
					    ai.bytes, sizeof(ai));
		hdmi_start_infoframe_trans(codec, pin_nid);
	}
}

void snd_hda_hdmi_setup_audio_infoframe(struct hda_codec *codec,
					struct hdmi_spec_per_pin *per_pin,
					bool non_pcm)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdac_chmap *chmap = &spec->chmap;
	hda_nid_t pin_nid = per_pin->pin_nid;
	int dev_id = per_pin->dev_id;
	int channels = per_pin->channels;
	int active_channels;
	struct hdmi_eld *eld;
	int ca;

	if (!channels)
		return;

	snd_hda_set_dev_select(codec, pin_nid, dev_id);

	/* some HW (e.g. HSW+) needs reprogramming the amp at each time */
	if (get_wcaps(codec, pin_nid) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, pin_nid, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_OUT_UNMUTE);

	eld = &per_pin->sink_eld;

	ca = snd_hdac_channel_allocation(&codec->core,
			eld->info.spk_alloc, channels,
			per_pin->chmap_set, non_pcm, per_pin->chmap);

	active_channels = snd_hdac_get_active_channels(ca);

	chmap->ops.set_channel_count(&codec->core, per_pin->cvt_nid,
						active_channels);

	/*
	 * always configure channel mapping, it may have been changed by the
	 * user in the meantime
	 */
	snd_hdac_setup_channel_mapping(&spec->chmap,
				pin_nid, non_pcm, ca, channels,
				per_pin->chmap, per_pin->chmap_set);

	spec->ops.pin_setup_infoframe(codec, pin_nid, dev_id,
				      ca, active_channels, eld->info.conn_type);

	per_pin->non_pcm = non_pcm;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_setup_audio_infoframe, "SND_HDA_CODEC_HDMI");

/*
 * Unsolicited events
 */

static void hdmi_present_sense(struct hdmi_spec_per_pin *per_pin, int repoll);

void snd_hda_hdmi_check_presence_and_report(struct hda_codec *codec,
					    hda_nid_t nid, int dev_id)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx = pin_id_to_pin_index(codec, nid, dev_id);

	if (pin_idx < 0)
		return;
	guard(mutex)(&spec->pcm_lock);
	hdmi_present_sense(get_pin(spec, pin_idx), 1);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_check_presence_and_report,
		     "SND_HDA_CODEC_HDMI");

static void jack_callback(struct hda_codec *codec,
			  struct hda_jack_callback *jack)
{
	/* stop polling when notification is enabled */
	if (codec_has_acomp(codec))
		return;

	snd_hda_hdmi_check_presence_and_report(codec, jack->nid, jack->dev_id);
}

static void hdmi_intrinsic_event(struct hda_codec *codec, unsigned int res,
				 struct hda_jack_tbl *jack)
{
	jack->jack_dirty = 1;

	codec_dbg(codec,
		"HDMI hot plug event: Codec=%d NID=0x%x Device=%d Inactive=%d Presence_Detect=%d ELD_Valid=%d\n",
		codec->addr, jack->nid, jack->dev_id, !!(res & AC_UNSOL_RES_IA),
		!!(res & AC_UNSOL_RES_PD), !!(res & AC_UNSOL_RES_ELDV));

	snd_hda_hdmi_check_presence_and_report(codec, jack->nid, jack->dev_id);
}

static void hdmi_non_intrinsic_event(struct hda_codec *codec, unsigned int res)
{
	int tag = res >> AC_UNSOL_RES_TAG_SHIFT;
	int subtag = (res & AC_UNSOL_RES_SUBTAG) >> AC_UNSOL_RES_SUBTAG_SHIFT;
	int cp_state = !!(res & AC_UNSOL_RES_CP_STATE);
	int cp_ready = !!(res & AC_UNSOL_RES_CP_READY);

	codec_info(codec,
		"HDMI CP event: CODEC=%d TAG=%d SUBTAG=0x%x CP_STATE=%d CP_READY=%d\n",
		codec->addr,
		tag,
		subtag,
		cp_state,
		cp_ready);

	/* TODO */
	if (cp_state) {
		;
	}
	if (cp_ready) {
		;
	}
}

void snd_hda_hdmi_generic_unsol_event(struct hda_codec *codec, unsigned int res)
{
	int tag = res >> AC_UNSOL_RES_TAG_SHIFT;
	int subtag = (res & AC_UNSOL_RES_SUBTAG) >> AC_UNSOL_RES_SUBTAG_SHIFT;
	struct hda_jack_tbl *jack;

	if (codec_has_acomp(codec))
		return;

	if (codec->dp_mst) {
		int dev_entry =
			(res & AC_UNSOL_RES_DE) >> AC_UNSOL_RES_DE_SHIFT;

		jack = snd_hda_jack_tbl_get_from_tag(codec, tag, dev_entry);
	} else {
		jack = snd_hda_jack_tbl_get_from_tag(codec, tag, 0);
	}

	if (!jack) {
		codec_dbg(codec, "Unexpected HDMI event tag 0x%x\n", tag);
		return;
	}

	if (subtag == 0)
		hdmi_intrinsic_event(codec, res, jack);
	else
		hdmi_non_intrinsic_event(codec, res);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_unsol_event, "SND_HDA_CODEC_HDMI");

/*
 * Callbacks
 */

/* HBR should be Non-PCM, 8 channels */
#define is_hbr_format(format) \
	((format & AC_FMT_TYPE_NON_PCM) && (format & AC_FMT_CHAN_MASK) == 7)

static int hdmi_pin_hbr_setup(struct hda_codec *codec, hda_nid_t pin_nid,
			      int dev_id, bool hbr)
{
	int pinctl, new_pinctl;

	if (snd_hda_query_pin_caps(codec, pin_nid) & AC_PINCAP_HBR) {
		snd_hda_set_dev_select(codec, pin_nid, dev_id);
		pinctl = snd_hda_codec_read(codec, pin_nid, 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);

		if (pinctl < 0)
			return hbr ? -EINVAL : 0;

		new_pinctl = pinctl & ~AC_PINCTL_EPT;
		if (hbr)
			new_pinctl |= AC_PINCTL_EPT_HBR;
		else
			new_pinctl |= AC_PINCTL_EPT_NATIVE;

		codec_dbg(codec,
			  "hdmi_pin_hbr_setup: NID=0x%x, %spinctl=0x%x\n",
			    pin_nid,
			    pinctl == new_pinctl ? "" : "new-",
			    new_pinctl);

		if (pinctl != new_pinctl)
			snd_hda_codec_write(codec, pin_nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    new_pinctl);
	} else if (hbr)
		return -EINVAL;

	return 0;
}

int snd_hda_hdmi_setup_stream(struct hda_codec *codec,
			      hda_nid_t cvt_nid,
			      hda_nid_t pin_nid, int dev_id,
			      u32 stream_tag, int format)
{
	struct hdmi_spec *spec = codec->spec;
	unsigned int param;
	int err;

	err = spec->ops.pin_hbr_setup(codec, pin_nid, dev_id,
				      is_hbr_format(format));

	if (err) {
		codec_dbg(codec, "hdmi_setup_stream: HBR is not supported\n");
		return err;
	}

	if (spec->intel_hsw_fixup) {

		/*
		 * on recent platforms IEC Coding Type is required for HBR
		 * support, read current Digital Converter settings and set
		 * ICT bitfield if needed.
		 */
		param = snd_hda_codec_read(codec, cvt_nid, 0,
					   AC_VERB_GET_DIGI_CONVERT_1, 0);

		param = (param >> 16) & ~(AC_DIG3_ICT);

		/* on recent platforms ICT mode is required for HBR support */
		if (is_hbr_format(format))
			param |= 0x1;

		snd_hda_codec_write(codec, cvt_nid, 0,
				    AC_VERB_SET_DIGI_CONVERT_3, param);
	}

	snd_hda_codec_setup_stream(codec, cvt_nid, stream_tag, 0, format);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_setup_stream, "SND_HDA_CODEC_HDMI");

/* Try to find an available converter
 * If pin_idx is less then zero, just try to find an available converter.
 * Otherwise, try to find an available converter and get the cvt mux index
 * of the pin.
 */
static int hdmi_choose_cvt(struct hda_codec *codec,
			   int pin_idx, int *cvt_id,
			   bool silent)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin;
	struct hdmi_spec_per_cvt *per_cvt = NULL;
	int cvt_idx, mux_idx = 0;

	/* pin_idx < 0 means no pin will be bound to the converter */
	if (pin_idx < 0)
		per_pin = NULL;
	else
		per_pin = get_pin(spec, pin_idx);

	if (per_pin && per_pin->silent_stream) {
		cvt_idx = cvt_nid_to_cvt_index(codec, per_pin->cvt_nid);
		per_cvt = get_cvt(spec, cvt_idx);
		if (per_cvt->assigned && !silent)
			return -EBUSY;
		if (cvt_id)
			*cvt_id = cvt_idx;
		return 0;
	}

	/* Dynamically assign converter to stream */
	for (cvt_idx = 0; cvt_idx < spec->num_cvts; cvt_idx++) {
		per_cvt = get_cvt(spec, cvt_idx);

		/* Must not already be assigned */
		if (per_cvt->assigned || per_cvt->silent_stream)
			continue;
		if (per_pin == NULL)
			break;
		/* Must be in pin's mux's list of converters */
		for (mux_idx = 0; mux_idx < per_pin->num_mux_nids; mux_idx++)
			if (per_pin->mux_nids[mux_idx] == per_cvt->cvt_nid)
				break;
		/* Not in mux list */
		if (mux_idx == per_pin->num_mux_nids)
			continue;
		break;
	}

	/* No free converters */
	if (cvt_idx == spec->num_cvts)
		return -EBUSY;

	if (per_pin != NULL)
		per_pin->mux_idx = mux_idx;

	if (cvt_id)
		*cvt_id = cvt_idx;

	return 0;
}

/* skeleton caller of pin_cvt_fixup ops */
static void pin_cvt_fixup(struct hda_codec *codec,
			  struct hdmi_spec_per_pin *per_pin,
			  hda_nid_t cvt_nid)
{
	struct hdmi_spec *spec = codec->spec;

	if (spec->ops.pin_cvt_fixup)
		spec->ops.pin_cvt_fixup(codec, per_pin, cvt_nid);
}

/* called in hdmi_pcm_open when no pin is assigned to the PCM */
static int hdmi_pcm_open_no_pin(struct hda_pcm_stream *hinfo,
			 struct hda_codec *codec,
			 struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int cvt_idx, pcm_idx;
	struct hdmi_spec_per_cvt *per_cvt = NULL;
	int err;

	pcm_idx = hinfo_to_pcm_index(codec, hinfo);
	if (pcm_idx < 0)
		return -EINVAL;

	err = hdmi_choose_cvt(codec, -1, &cvt_idx, false);
	if (err)
		return err;

	per_cvt = get_cvt(spec, cvt_idx);
	per_cvt->assigned = true;
	hinfo->nid = per_cvt->cvt_nid;

	pin_cvt_fixup(codec, NULL, per_cvt->cvt_nid);

	set_bit(pcm_idx, &spec->pcm_in_use);
	/* todo: setup spdif ctls assign */

	/* Initially set the converter's capabilities */
	hinfo->channels_min = per_cvt->channels_min;
	hinfo->channels_max = per_cvt->channels_max;
	hinfo->rates = per_cvt->rates;
	hinfo->formats = per_cvt->formats;
	hinfo->maxbps = per_cvt->maxbps;

	/* Store the updated parameters */
	runtime->hw.channels_min = hinfo->channels_min;
	runtime->hw.channels_max = hinfo->channels_max;
	runtime->hw.formats = hinfo->formats;
	runtime->hw.rates = hinfo->rates;

	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	return 0;
}

/*
 * HDA PCM callbacks
 */
static int hdmi_pcm_open(struct hda_pcm_stream *hinfo,
			 struct hda_codec *codec,
			 struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int pin_idx, cvt_idx, pcm_idx;
	struct hdmi_spec_per_pin *per_pin;
	struct hdmi_eld *eld;
	struct hdmi_spec_per_cvt *per_cvt = NULL;
	int err;

	/* Validate hinfo */
	pcm_idx = hinfo_to_pcm_index(codec, hinfo);
	if (pcm_idx < 0)
		return -EINVAL;

	guard(mutex)(&spec->pcm_lock);
	pin_idx = hinfo_to_pin_index(codec, hinfo);
	/* no pin is assigned to the PCM
	 * PA need pcm open successfully when probe
	 */
	if (pin_idx < 0)
		return hdmi_pcm_open_no_pin(hinfo, codec, substream);

	err = hdmi_choose_cvt(codec, pin_idx, &cvt_idx, false);
	if (err < 0)
		return err;

	per_cvt = get_cvt(spec, cvt_idx);
	/* Claim converter */
	per_cvt->assigned = true;

	set_bit(pcm_idx, &spec->pcm_in_use);
	per_pin = get_pin(spec, pin_idx);
	per_pin->cvt_nid = per_cvt->cvt_nid;
	hinfo->nid = per_cvt->cvt_nid;

	/* flip stripe flag for the assigned stream if supported */
	if (get_wcaps(codec, per_cvt->cvt_nid) & AC_WCAP_STRIPE)
		azx_stream(get_azx_dev(substream))->stripe = 1;

	snd_hda_set_dev_select(codec, per_pin->pin_nid, per_pin->dev_id);
	snd_hda_codec_write_cache(codec, per_pin->pin_nid, 0,
			    AC_VERB_SET_CONNECT_SEL,
			    per_pin->mux_idx);

	/* configure unused pins to choose other converters */
	pin_cvt_fixup(codec, per_pin, 0);

	snd_hda_spdif_ctls_assign(codec, pcm_idx, per_cvt->cvt_nid);

	/* Initially set the converter's capabilities */
	hinfo->channels_min = per_cvt->channels_min;
	hinfo->channels_max = per_cvt->channels_max;
	hinfo->rates = per_cvt->rates;
	hinfo->formats = per_cvt->formats;
	hinfo->maxbps = per_cvt->maxbps;

	eld = &per_pin->sink_eld;
	/* Restrict capabilities by ELD if this isn't disabled */
	if (!static_hdmi_pcm && eld->eld_valid) {
		snd_hdmi_eld_update_pcm_info(&eld->info, hinfo);
		if (hinfo->channels_min > hinfo->channels_max ||
		    !hinfo->rates || !hinfo->formats) {
			per_cvt->assigned = false;
			hinfo->nid = 0;
			snd_hda_spdif_ctls_unassign(codec, pcm_idx);
			return -ENODEV;
		}
	}

	/* Store the updated parameters */
	runtime->hw.channels_min = hinfo->channels_min;
	runtime->hw.channels_max = hinfo->channels_max;
	runtime->hw.formats = hinfo->formats;
	runtime->hw.rates = hinfo->rates;

	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	return 0;
}

/*
 * HDA/HDMI auto parsing
 */
static int hdmi_read_pin_conn(struct hda_codec *codec, int pin_idx)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);
	hda_nid_t pin_nid = per_pin->pin_nid;
	int dev_id = per_pin->dev_id;
	int conns;

	if (!(get_wcaps(codec, pin_nid) & AC_WCAP_CONN_LIST)) {
		codec_warn(codec,
			   "HDMI: pin NID 0x%x wcaps %#x does not support connection list\n",
			   pin_nid, get_wcaps(codec, pin_nid));
		return -EINVAL;
	}

	snd_hda_set_dev_select(codec, pin_nid, dev_id);

	if (spec->intel_hsw_fixup) {
		conns = spec->num_cvts;
		memcpy(per_pin->mux_nids, spec->cvt_nids,
		       sizeof(hda_nid_t) * conns);
	} else {
		conns = snd_hda_get_raw_connections(codec, pin_nid,
						    per_pin->mux_nids,
						    HDA_MAX_CONNECTIONS);
	}

	/* all the device entries on the same pin have the same conn list */
	per_pin->num_mux_nids = conns;

	return 0;
}

static int hdmi_find_pcm_slot(struct hdmi_spec *spec,
			      struct hdmi_spec_per_pin *per_pin)
{
	int i;

	for (i = 0; i < spec->pcm_used; i++) {
		if (!test_bit(i, &spec->pcm_bitmap))
			return i;
	}
	return -EBUSY;
}

static void hdmi_attach_hda_pcm(struct hdmi_spec *spec,
				struct hdmi_spec_per_pin *per_pin)
{
	int idx;

	/* pcm already be attached to the pin */
	if (per_pin->pcm)
		return;
	/* try the previously used slot at first */
	idx = per_pin->prev_pcm_idx;
	if (idx >= 0) {
		if (!test_bit(idx, &spec->pcm_bitmap))
			goto found;
		per_pin->prev_pcm_idx = -1; /* no longer valid, clear it */
	}
	idx = hdmi_find_pcm_slot(spec, per_pin);
	if (idx == -EBUSY)
		return;
 found:
	per_pin->pcm_idx = idx;
	per_pin->pcm = get_hdmi_pcm(spec, idx);
	set_bit(idx, &spec->pcm_bitmap);
}

static void hdmi_detach_hda_pcm(struct hdmi_spec *spec,
				struct hdmi_spec_per_pin *per_pin)
{
	int idx;

	/* pcm already be detached from the pin */
	if (!per_pin->pcm)
		return;
	idx = per_pin->pcm_idx;
	per_pin->pcm_idx = -1;
	per_pin->prev_pcm_idx = idx; /* remember the previous index */
	per_pin->pcm = NULL;
	if (idx >= 0 && idx < spec->pcm_used)
		clear_bit(idx, &spec->pcm_bitmap);
}

static int hdmi_get_pin_cvt_mux(struct hdmi_spec *spec,
		struct hdmi_spec_per_pin *per_pin, hda_nid_t cvt_nid)
{
	int mux_idx;

	for (mux_idx = 0; mux_idx < per_pin->num_mux_nids; mux_idx++)
		if (per_pin->mux_nids[mux_idx] == cvt_nid)
			break;
	return mux_idx;
}

static bool check_non_pcm_per_cvt(struct hda_codec *codec, hda_nid_t cvt_nid);

static void hdmi_pcm_setup_pin(struct hdmi_spec *spec,
			   struct hdmi_spec_per_pin *per_pin)
{
	struct hda_codec *codec = per_pin->codec;
	struct hda_pcm *pcm;
	struct hda_pcm_stream *hinfo;
	struct snd_pcm_substream *substream;
	int mux_idx;
	bool non_pcm;

	if (per_pin->pcm_idx < 0 || per_pin->pcm_idx >= spec->pcm_used)
		return;
	pcm = get_pcm_rec(spec, per_pin->pcm_idx);
	if (!pcm->pcm)
		return;
	if (!test_bit(per_pin->pcm_idx, &spec->pcm_in_use))
		return;

	/* hdmi audio only uses playback and one substream */
	hinfo = pcm->stream;
	substream = pcm->pcm->streams[0].substream;

	per_pin->cvt_nid = hinfo->nid;

	mux_idx = hdmi_get_pin_cvt_mux(spec, per_pin, hinfo->nid);
	if (mux_idx < per_pin->num_mux_nids) {
		snd_hda_set_dev_select(codec, per_pin->pin_nid,
				   per_pin->dev_id);
		snd_hda_codec_write_cache(codec, per_pin->pin_nid, 0,
				AC_VERB_SET_CONNECT_SEL,
				mux_idx);
	}
	snd_hda_spdif_ctls_assign(codec, per_pin->pcm_idx, hinfo->nid);

	non_pcm = check_non_pcm_per_cvt(codec, hinfo->nid);
	if (substream->runtime)
		per_pin->channels = substream->runtime->channels;
	per_pin->setup = true;
	per_pin->mux_idx = mux_idx;

	snd_hda_hdmi_setup_audio_infoframe(codec, per_pin, non_pcm);
}

static void hdmi_pcm_reset_pin(struct hdmi_spec *spec,
			   struct hdmi_spec_per_pin *per_pin)
{
	if (per_pin->pcm_idx >= 0 && per_pin->pcm_idx < spec->pcm_used)
		snd_hda_spdif_ctls_unassign(per_pin->codec, per_pin->pcm_idx);

	per_pin->chmap_set = false;
	memset(per_pin->chmap, 0, sizeof(per_pin->chmap));

	per_pin->setup = false;
	per_pin->channels = 0;
}

static struct snd_jack *pin_idx_to_pcm_jack(struct hda_codec *codec,
					    struct hdmi_spec_per_pin *per_pin)
{
	struct hdmi_spec *spec = codec->spec;

	if (per_pin->pcm_idx >= 0)
		return spec->pcm_rec[per_pin->pcm_idx].jack;
	else
		return NULL;
}

/* update per_pin ELD from the given new ELD;
 * setup info frame and notification accordingly
 * also notify ELD kctl and report jack status changes
 */
static void update_eld(struct hda_codec *codec,
		       struct hdmi_spec_per_pin *per_pin,
		       struct hdmi_eld *eld,
		       int repoll)
{
	struct hdmi_eld *pin_eld = &per_pin->sink_eld;
	struct hdmi_spec *spec = codec->spec;
	struct snd_jack *pcm_jack;
	bool old_eld_valid = pin_eld->eld_valid;
	bool eld_changed;
	int pcm_idx;

	if (eld->eld_valid) {
		if (eld->eld_size <= 0 ||
		    snd_parse_eld(hda_codec_dev(codec), &eld->info,
				  eld->eld_buffer, eld->eld_size) < 0) {
			eld->eld_valid = false;
			if (repoll) {
				schedule_delayed_work(&per_pin->work,
						      msecs_to_jiffies(300));
				return;
			}
		}
	}

	if (!eld->eld_valid || eld->eld_size <= 0 || eld->info.sad_count <= 0) {
		eld->eld_valid = false;
		eld->eld_size = 0;
	}

	/* for monitor disconnection, save pcm_idx firstly */
	pcm_idx = per_pin->pcm_idx;

	/*
	 * pcm_idx >=0 before update_eld() means it is in monitor
	 * disconnected event. Jack must be fetched before update_eld().
	 */
	pcm_jack = pin_idx_to_pcm_jack(codec, per_pin);

	if (!spec->static_pcm_mapping) {
		if (eld->eld_valid) {
			hdmi_attach_hda_pcm(spec, per_pin);
			hdmi_pcm_setup_pin(spec, per_pin);
		} else {
			hdmi_pcm_reset_pin(spec, per_pin);
			hdmi_detach_hda_pcm(spec, per_pin);
		}
	}

	/* if pcm_idx == -1, it means this is in monitor connection event
	 * we can get the correct pcm_idx now.
	 */
	if (pcm_idx == -1)
		pcm_idx = per_pin->pcm_idx;
	if (!pcm_jack)
		pcm_jack = pin_idx_to_pcm_jack(codec, per_pin);

	if (eld->eld_valid)
		snd_show_eld(hda_codec_dev(codec), &eld->info);

	eld_changed = (pin_eld->eld_valid != eld->eld_valid);
	eld_changed |= (pin_eld->monitor_present != eld->monitor_present);
	if (!eld_changed && eld->eld_valid && pin_eld->eld_valid)
		if (pin_eld->eld_size != eld->eld_size ||
		    memcmp(pin_eld->eld_buffer, eld->eld_buffer,
			   eld->eld_size) != 0)
			eld_changed = true;

	if (eld_changed) {
		pin_eld->monitor_present = eld->monitor_present;
		pin_eld->eld_valid = eld->eld_valid;
		pin_eld->eld_size = eld->eld_size;
		if (eld->eld_valid)
			memcpy(pin_eld->eld_buffer, eld->eld_buffer,
			       eld->eld_size);
		pin_eld->info = eld->info;
	}

	/*
	 * Re-setup pin and infoframe. This is needed e.g. when
	 * - sink is first plugged-in
	 * - transcoder can change during stream playback on Haswell
	 *   and this can make HW reset converter selection on a pin.
	 */
	if (eld->eld_valid && !old_eld_valid && per_pin->setup) {
		pin_cvt_fixup(codec, per_pin, 0);
		snd_hda_hdmi_setup_audio_infoframe(codec, per_pin, per_pin->non_pcm);
	}

	if (eld_changed && pcm_idx >= 0)
		snd_ctl_notify(codec->card,
			       SNDRV_CTL_EVENT_MASK_VALUE |
			       SNDRV_CTL_EVENT_MASK_INFO,
			       &get_hdmi_pcm(spec, pcm_idx)->eld_ctl->id);

	if (eld_changed && pcm_jack)
		snd_jack_report(pcm_jack,
				(eld->monitor_present && eld->eld_valid) ?
				SND_JACK_AVOUT : 0);
}

/* update ELD and jack state via HD-audio verbs */
static void hdmi_present_sense_via_verbs(struct hdmi_spec_per_pin *per_pin,
					 int repoll)
{
	struct hda_codec *codec = per_pin->codec;
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_eld *eld = &spec->temp_eld;
	struct device *dev = hda_codec_dev(codec);
	hda_nid_t pin_nid = per_pin->pin_nid;
	int dev_id = per_pin->dev_id;
	/*
	 * Always execute a GetPinSense verb here, even when called from
	 * hdmi_intrinsic_event; for some NVIDIA HW, the unsolicited
	 * response's PD bit is not the real PD value, but indicates that
	 * the real PD value changed. An older version of the HD-audio
	 * specification worked this way. Hence, we just ignore the data in
	 * the unsolicited response to avoid custom WARs.
	 */
	int present;

#ifdef	CONFIG_PM
	if (dev->power.runtime_status == RPM_SUSPENDING)
		return;
#endif

	CLASS(snd_hda_power_pm, pm)(codec);
	if (pm.err < 0 && pm_runtime_suspended(dev))
		return;

	present = snd_hda_jack_pin_sense(codec, pin_nid, dev_id);

	guard(mutex)(&per_pin->lock);
	eld->monitor_present = !!(present & AC_PINSENSE_PRESENCE);
	if (eld->monitor_present)
		eld->eld_valid  = !!(present & AC_PINSENSE_ELDV);
	else
		eld->eld_valid = false;

	codec_dbg(codec,
		"HDMI status: Codec=%d NID=0x%x Presence_Detect=%d ELD_Valid=%d\n",
		codec->addr, pin_nid, eld->monitor_present, eld->eld_valid);

	if (eld->eld_valid) {
		if (spec->ops.pin_get_eld(codec, pin_nid, dev_id,
					  eld->eld_buffer, &eld->eld_size) < 0)
			eld->eld_valid = false;
	}

	update_eld(codec, per_pin, eld, repoll);
}

static void silent_stream_enable(struct hda_codec *codec,
				 struct hdmi_spec_per_pin *per_pin)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_cvt *per_cvt;
	int cvt_idx, pin_idx, err;

	/*
	 * Power-up will call hdmi_present_sense, so the PM calls
	 * have to be done without mutex held.
	 */

	CLASS(snd_hda_power_pm, pm)(codec);
	if (pm.err < 0 && pm.err != -EACCES) {
		codec_err(codec,
			  "Failed to power up codec for silent stream enable ret=[%d]\n", pm.err);
		return;
	}

	guard(mutex)(&per_pin->lock);

	if (per_pin->setup) {
		codec_dbg(codec, "hdmi: PCM already open, no silent stream\n");
		return;
	}

	pin_idx = pin_id_to_pin_index(codec, per_pin->pin_nid, per_pin->dev_id);
	err = hdmi_choose_cvt(codec, pin_idx, &cvt_idx, true);
	if (err) {
		codec_err(codec, "hdmi: no free converter to enable silent mode\n");
		return;
	}

	per_cvt = get_cvt(spec, cvt_idx);
	per_cvt->silent_stream = true;
	per_pin->cvt_nid = per_cvt->cvt_nid;
	per_pin->silent_stream = true;

	codec_dbg(codec, "hdmi: enabling silent stream pin-NID=0x%x cvt-NID=0x%x\n",
		  per_pin->pin_nid, per_cvt->cvt_nid);

	snd_hda_set_dev_select(codec, per_pin->pin_nid, per_pin->dev_id);
	snd_hda_codec_write_cache(codec, per_pin->pin_nid, 0,
				  AC_VERB_SET_CONNECT_SEL,
				  per_pin->mux_idx);

	/* configure unused pins to choose other converters */
	pin_cvt_fixup(codec, per_pin, 0);

	spec->ops.silent_stream(codec, per_pin, true);
}

static void silent_stream_disable(struct hda_codec *codec,
				  struct hdmi_spec_per_pin *per_pin)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_cvt *per_cvt;
	int cvt_idx;

	CLASS(snd_hda_power_pm, pm)(codec);
	if (pm.err < 0 && pm.err != -EACCES) {
		codec_err(codec,
			  "Failed to power up codec for silent stream disable ret=[%d]\n",
			  pm.err);
		return;
	}

	guard(mutex)(&per_pin->lock);
	if (!per_pin->silent_stream)
		return;

	codec_dbg(codec, "HDMI: disable silent stream on pin-NID=0x%x cvt-NID=0x%x\n",
		  per_pin->pin_nid, per_pin->cvt_nid);

	cvt_idx = cvt_nid_to_cvt_index(codec, per_pin->cvt_nid);
	if (cvt_idx >= 0 && cvt_idx < spec->num_cvts) {
		per_cvt = get_cvt(spec, cvt_idx);
		per_cvt->silent_stream = false;
	}

	spec->ops.silent_stream(codec, per_pin, false);

	per_pin->cvt_nid = 0;
	per_pin->silent_stream = false;
}

/* update ELD and jack state via audio component */
static void sync_eld_via_acomp(struct hda_codec *codec,
			       struct hdmi_spec_per_pin *per_pin)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_eld *eld = &spec->temp_eld;
	bool monitor_prev, monitor_next;

	scoped_guard(mutex, &per_pin->lock) {
		eld->monitor_present = false;
		monitor_prev = per_pin->sink_eld.monitor_present;
		eld->eld_size = snd_hdac_acomp_get_eld(&codec->core, per_pin->pin_nid,
						       per_pin->dev_id, &eld->monitor_present,
						       eld->eld_buffer, ELD_MAX_SIZE);
		eld->eld_valid = (eld->eld_size > 0);
		update_eld(codec, per_pin, eld, 0);
		monitor_next = per_pin->sink_eld.monitor_present;
	}

	if (spec->silent_stream_type) {
		if (!monitor_prev && monitor_next)
			silent_stream_enable(codec, per_pin);
		else if (monitor_prev && !monitor_next)
			silent_stream_disable(codec, per_pin);
	}
}

static void hdmi_present_sense(struct hdmi_spec_per_pin *per_pin, int repoll)
{
	struct hda_codec *codec = per_pin->codec;

	if (!codec_has_acomp(codec))
		hdmi_present_sense_via_verbs(per_pin, repoll);
	else
		sync_eld_via_acomp(codec, per_pin);
}

static void hdmi_repoll_eld(struct work_struct *work)
{
	struct hdmi_spec_per_pin *per_pin =
	container_of(to_delayed_work(work), struct hdmi_spec_per_pin, work);
	struct hda_codec *codec = per_pin->codec;
	struct hdmi_spec *spec = codec->spec;
	struct hda_jack_tbl *jack;

	jack = snd_hda_jack_tbl_get_mst(codec, per_pin->pin_nid,
					per_pin->dev_id);
	if (jack)
		jack->jack_dirty = 1;

	if (per_pin->repoll_count++ > 6)
		per_pin->repoll_count = 0;

	guard(mutex)(&spec->pcm_lock);
	hdmi_present_sense(per_pin, per_pin->repoll_count);
}

static int hdmi_add_pin(struct hda_codec *codec, hda_nid_t pin_nid)
{
	struct hdmi_spec *spec = codec->spec;
	unsigned int caps, config;
	int pin_idx;
	struct hdmi_spec_per_pin *per_pin;
	int err;
	int dev_num, i;

	caps = snd_hda_query_pin_caps(codec, pin_nid);
	if (!(caps & (AC_PINCAP_HDMI | AC_PINCAP_DP)))
		return 0;

	/*
	 * For DP MST audio, Configuration Default is the same for
	 * all device entries on the same pin
	 */
	config = snd_hda_codec_get_pincfg(codec, pin_nid);
	if (get_defcfg_connect(config) == AC_JACK_PORT_NONE &&
	    !spec->force_connect)
		return 0;

	/*
	 * To simplify the implementation, malloc all
	 * the virtual pins in the initialization statically
	 */
	if (spec->intel_hsw_fixup) {
		/*
		 * On Intel platforms, device entries count returned
		 * by AC_PAR_DEVLIST_LEN is dynamic, and depends on
		 * the type of receiver that is connected. Allocate pin
		 * structures based on worst case.
		 */
		dev_num = spec->dev_num;
	} else if (codec->dp_mst) {
		dev_num = snd_hda_get_num_devices(codec, pin_nid) + 1;
		/*
		 * spec->dev_num is the maxinum number of device entries
		 * among all the pins
		 */
		spec->dev_num = (spec->dev_num > dev_num) ?
			spec->dev_num : dev_num;
	} else {
		/*
		 * If the platform doesn't support DP MST,
		 * manually set dev_num to 1. This means
		 * the pin has only one device entry.
		 */
		dev_num = 1;
		spec->dev_num = 1;
	}

	for (i = 0; i < dev_num; i++) {
		pin_idx = spec->num_pins;
		per_pin = snd_array_new(&spec->pins);

		if (!per_pin)
			return -ENOMEM;

		per_pin->pcm = NULL;
		per_pin->pcm_idx = -1;
		per_pin->prev_pcm_idx = -1;
		per_pin->pin_nid = pin_nid;
		per_pin->pin_nid_idx = spec->num_nids;
		per_pin->dev_id = i;
		per_pin->non_pcm = false;
		snd_hda_set_dev_select(codec, pin_nid, i);
		err = hdmi_read_pin_conn(codec, pin_idx);
		if (err < 0)
			return err;
		if (!is_jack_detectable(codec, pin_nid))
			codec_warn(codec, "HDMI: pin NID 0x%x - jack not detectable\n", pin_nid);
		spec->num_pins++;
	}
	spec->num_nids++;

	return 0;
}

static int hdmi_add_cvt(struct hda_codec *codec, hda_nid_t cvt_nid)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_cvt *per_cvt;
	unsigned int chans;
	int err;

	chans = get_wcaps(codec, cvt_nid);
	chans = get_wcaps_channels(chans);

	per_cvt = snd_array_new(&spec->cvts);
	if (!per_cvt)
		return -ENOMEM;

	per_cvt->cvt_nid = cvt_nid;
	per_cvt->channels_min = 2;
	if (chans <= 16) {
		per_cvt->channels_max = chans;
		if (chans > spec->chmap.channels_max)
			spec->chmap.channels_max = chans;
	}

	err = snd_hda_query_supported_pcm(codec, cvt_nid,
					  &per_cvt->rates,
					  &per_cvt->formats,
					  NULL,
					  &per_cvt->maxbps);
	if (err < 0)
		return err;

	if (spec->num_cvts < ARRAY_SIZE(spec->cvt_nids))
		spec->cvt_nids[spec->num_cvts] = cvt_nid;
	spec->num_cvts++;

	return 0;
}

static const struct snd_pci_quirk force_connect_list[] = {
	SND_PCI_QUIRK(0x103c, 0x83e2, "HP EliteDesk 800 G4", 1),
	SND_PCI_QUIRK(0x103c, 0x83ef, "HP MP9 G4 Retail System AMS", 1),
	SND_PCI_QUIRK(0x103c, 0x845a, "HP EliteDesk 800 G4 DM 65W", 1),
	SND_PCI_QUIRK(0x103c, 0x83f3, "HP ProDesk 400", 1),
	SND_PCI_QUIRK(0x103c, 0x870f, "HP", 1),
	SND_PCI_QUIRK(0x103c, 0x871a, "HP", 1),
	SND_PCI_QUIRK(0x103c, 0x8711, "HP", 1),
	SND_PCI_QUIRK(0x103c, 0x8715, "HP", 1),
	SND_PCI_QUIRK(0x1043, 0x86ae, "ASUS", 1),  /* Z170 PRO */
	SND_PCI_QUIRK(0x1043, 0x86c7, "ASUS", 1),  /* Z170M PLUS */
	SND_PCI_QUIRK(0x1462, 0xec94, "MS-7C94", 1),
	SND_PCI_QUIRK(0x8086, 0x2060, "Intel NUC5CPYB", 1),
	SND_PCI_QUIRK(0x8086, 0x2081, "Intel NUC 10", 1),
	{}
};

int snd_hda_hdmi_parse_codec(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	hda_nid_t start_nid;
	unsigned int caps;
	int i, nodes;
	const struct snd_pci_quirk *q;

	nodes = snd_hda_get_sub_nodes(codec, codec->core.afg, &start_nid);
	if (!start_nid || nodes < 0) {
		codec_warn(codec, "HDMI: failed to get afg sub nodes\n");
		return -EINVAL;
	}

	if (enable_all_pins)
		spec->force_connect = true;

	q = snd_pci_quirk_lookup(codec->bus->pci, force_connect_list);

	if (q && q->value)
		spec->force_connect = true;

	/*
	 * hdmi_add_pin() assumes total amount of converters to
	 * be known, so first discover all converters
	 */
	for (i = 0; i < nodes; i++) {
		hda_nid_t nid = start_nid + i;

		caps = get_wcaps(codec, nid);

		if (!(caps & AC_WCAP_DIGITAL))
			continue;

		if (get_wcaps_type(caps) == AC_WID_AUD_OUT)
			hdmi_add_cvt(codec, nid);
	}

	/* discover audio pins */
	for (i = 0; i < nodes; i++) {
		hda_nid_t nid = start_nid + i;

		caps = get_wcaps(codec, nid);

		if (!(caps & AC_WCAP_DIGITAL))
			continue;

		if (get_wcaps_type(caps) == AC_WID_PIN)
			hdmi_add_pin(codec, nid);
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_parse_codec, "SND_HDA_CODEC_HDMI");

/*
 */
static bool check_non_pcm_per_cvt(struct hda_codec *codec, hda_nid_t cvt_nid)
{
	struct hda_spdif_out *spdif;

	guard(mutex)(&codec->spdif_mutex);
	spdif = snd_hda_spdif_out_of_nid(codec, cvt_nid);
	/* Add sanity check to pass klockwork check.
	 * This should never happen.
	 */
	if (WARN_ON(spdif == NULL))
		return true;
	return !!(spdif->status & IEC958_AES0_NONAUDIO);
}

/*
 * HDMI callbacks
 */

int snd_hda_hdmi_generic_pcm_prepare(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream)
{
	hda_nid_t cvt_nid = hinfo->nid;
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;
	struct hdmi_spec_per_pin *per_pin;
	struct snd_pcm_runtime *runtime = substream->runtime;
	bool non_pcm;
	int pinctl, stripe;

	guard(mutex)(&spec->pcm_lock);
	pin_idx = hinfo_to_pin_index(codec, hinfo);
	if (pin_idx < 0) {
		/* when pcm is not bound to a pin skip pin setup and return 0
		 * to make audio playback be ongoing
		 */
		pin_cvt_fixup(codec, NULL, cvt_nid);
		snd_hda_codec_setup_stream(codec, cvt_nid,
					stream_tag, 0, format);
		return 0;
	}

	per_pin = get_pin(spec, pin_idx);

	/* Verify pin:cvt selections to avoid silent audio after S3.
	 * After S3, the audio driver restores pin:cvt selections
	 * but this can happen before gfx is ready and such selection
	 * is overlooked by HW. Thus multiple pins can share a same
	 * default convertor and mute control will affect each other,
	 * which can cause a resumed audio playback become silent
	 * after S3.
	 */
	pin_cvt_fixup(codec, per_pin, 0);

	/* Call sync_audio_rate to set the N/CTS/M manually if necessary */
	/* Todo: add DP1.2 MST audio support later */
	if (codec_has_acomp(codec))
		snd_hdac_sync_audio_rate(&codec->core, per_pin->pin_nid,
					 per_pin->dev_id, runtime->rate);

	non_pcm = check_non_pcm_per_cvt(codec, cvt_nid);
	scoped_guard(mutex, &per_pin->lock) {
		per_pin->channels = substream->runtime->channels;
		per_pin->setup = true;

		if (get_wcaps(codec, cvt_nid) & AC_WCAP_STRIPE) {
			stripe = snd_hdac_get_stream_stripe_ctl(&codec->bus->core,
								substream);
			snd_hda_codec_write(codec, cvt_nid, 0,
					    AC_VERB_SET_STRIPE_CONTROL,
					    stripe);
		}

		snd_hda_hdmi_setup_audio_infoframe(codec, per_pin, non_pcm);
	}
	if (spec->dyn_pin_out) {
		snd_hda_set_dev_select(codec, per_pin->pin_nid,
				       per_pin->dev_id);
		pinctl = snd_hda_codec_read(codec, per_pin->pin_nid, 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_codec_write(codec, per_pin->pin_nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    pinctl | PIN_OUT);
	}

	/* snd_hda_set_dev_select() has been called before */
	return spec->ops.setup_stream(codec, cvt_nid, per_pin->pin_nid,
				      per_pin->dev_id, stream_tag, format);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_pcm_prepare, "SND_HDA_CODEC_HDMI");

int snd_hda_hdmi_generic_pcm_cleanup(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	snd_hda_codec_cleanup_stream(codec, hinfo->nid);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_pcm_cleanup, "SND_HDA_CODEC_HDMI");

static int hdmi_pcm_close(struct hda_pcm_stream *hinfo,
			  struct hda_codec *codec,
			  struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	int cvt_idx, pin_idx, pcm_idx;
	struct hdmi_spec_per_cvt *per_cvt;
	struct hdmi_spec_per_pin *per_pin;
	int pinctl;

	guard(mutex)(&spec->pcm_lock);
	if (hinfo->nid) {
		pcm_idx = hinfo_to_pcm_index(codec, hinfo);
		if (snd_BUG_ON(pcm_idx < 0))
			return -EINVAL;
		cvt_idx = cvt_nid_to_cvt_index(codec, hinfo->nid);
		if (snd_BUG_ON(cvt_idx < 0))
			return -EINVAL;
		per_cvt = get_cvt(spec, cvt_idx);
		per_cvt->assigned = false;
		hinfo->nid = 0;

		azx_stream(get_azx_dev(substream))->stripe = 0;

		snd_hda_spdif_ctls_unassign(codec, pcm_idx);
		clear_bit(pcm_idx, &spec->pcm_in_use);
		pin_idx = hinfo_to_pin_index(codec, hinfo);
		/*
		 * In such a case, return 0 to match the behavior in
		 * hdmi_pcm_open()
		 */
		if (pin_idx < 0)
			return 0;

		per_pin = get_pin(spec, pin_idx);

		if (spec->dyn_pin_out) {
			snd_hda_set_dev_select(codec, per_pin->pin_nid,
					       per_pin->dev_id);
			pinctl = snd_hda_codec_read(codec, per_pin->pin_nid, 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
			snd_hda_codec_write(codec, per_pin->pin_nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    pinctl & ~PIN_OUT);
		}

		guard(mutex)(&per_pin->lock);
		per_pin->chmap_set = false;
		memset(per_pin->chmap, 0, sizeof(per_pin->chmap));

		per_pin->setup = false;
		per_pin->channels = 0;
	}

	return 0;
}

static const struct hda_pcm_ops generic_ops = {
	.open = hdmi_pcm_open,
	.close = hdmi_pcm_close,
	.prepare = snd_hda_hdmi_generic_pcm_prepare,
	.cleanup = snd_hda_hdmi_generic_pcm_cleanup,
};

static int hdmi_get_spk_alloc(struct hdac_device *hdac, int pcm_idx)
{
	struct hda_codec *codec = hdac_to_hda_codec(hdac);
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = pcm_idx_to_pin(spec, pcm_idx);

	if (!per_pin)
		return 0;

	return per_pin->sink_eld.info.spk_alloc;
}

static void hdmi_get_chmap(struct hdac_device *hdac, int pcm_idx,
					unsigned char *chmap)
{
	struct hda_codec *codec = hdac_to_hda_codec(hdac);
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = pcm_idx_to_pin(spec, pcm_idx);

	/* chmap is already set to 0 in caller */
	if (!per_pin)
		return;

	memcpy(chmap, per_pin->chmap, ARRAY_SIZE(per_pin->chmap));
}

static void hdmi_set_chmap(struct hdac_device *hdac, int pcm_idx,
				unsigned char *chmap, int prepared)
{
	struct hda_codec *codec = hdac_to_hda_codec(hdac);
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = pcm_idx_to_pin(spec, pcm_idx);

	if (!per_pin)
		return;
	guard(mutex)(&per_pin->lock);
	per_pin->chmap_set = true;
	memcpy(per_pin->chmap, chmap, ARRAY_SIZE(per_pin->chmap));
	if (prepared)
		snd_hda_hdmi_setup_audio_infoframe(codec, per_pin, per_pin->non_pcm);
}

static bool is_hdmi_pcm_attached(struct hdac_device *hdac, int pcm_idx)
{
	struct hda_codec *codec = hdac_to_hda_codec(hdac);
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = pcm_idx_to_pin(spec, pcm_idx);

	return per_pin ? true:false;
}

int snd_hda_hdmi_generic_build_pcms(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int idx, pcm_num;

	/* limit the PCM devices to the codec converters or available PINs */
	pcm_num = min(spec->num_cvts, spec->num_pins);
	codec_dbg(codec, "hdmi: pcm_num set to %d\n", pcm_num);

	for (idx = 0; idx < pcm_num; idx++) {
		struct hdmi_spec_per_cvt *per_cvt;
		struct hda_pcm *info;
		struct hda_pcm_stream *pstr;

		info = snd_hda_codec_pcm_new(codec, "HDMI %d", idx);
		if (!info)
			return -ENOMEM;

		spec->pcm_rec[idx].pcm = info;
		spec->pcm_used++;
		info->pcm_type = HDA_PCM_TYPE_HDMI;
		info->own_chmap = true;

		pstr = &info->stream[SNDRV_PCM_STREAM_PLAYBACK];
		pstr->substreams = 1;
		pstr->ops = generic_ops;

		per_cvt = get_cvt(spec, 0);
		pstr->channels_min = per_cvt->channels_min;
		pstr->channels_max = per_cvt->channels_max;

		/* pcm number is less than pcm_rec array size */
		if (spec->pcm_used >= ARRAY_SIZE(spec->pcm_rec))
			break;
		/* other pstr fields are set in open */
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_build_pcms, "SND_HDA_CODEC_HDMI");

static void free_hdmi_jack_priv(struct snd_jack *jack)
{
	struct hdmi_pcm *pcm = jack->private_data;

	pcm->jack = NULL;
}

static int generic_hdmi_build_jack(struct hda_codec *codec, int pcm_idx)
{
	char hdmi_str[32] = "HDMI/DP";
	struct hdmi_spec *spec = codec->spec;
	struct snd_jack *jack;
	int pcmdev = get_pcm_rec(spec, pcm_idx)->device;
	int err;

	if (pcmdev > 0)
		sprintf(hdmi_str + strlen(hdmi_str), ",pcm=%d", pcmdev);

	err = snd_jack_new(codec->card, hdmi_str, SND_JACK_AVOUT, &jack,
			   true, false);
	if (err < 0)
		return err;

	spec->pcm_rec[pcm_idx].jack = jack;
	jack->private_data = &spec->pcm_rec[pcm_idx];
	jack->private_free = free_hdmi_jack_priv;
	return 0;
}

int snd_hda_hdmi_generic_build_controls(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int dev, err;
	int pin_idx, pcm_idx;

	for (pcm_idx = 0; pcm_idx < spec->pcm_used; pcm_idx++) {
		if (!get_pcm_rec(spec, pcm_idx)->pcm) {
			/* no PCM: mark this for skipping permanently */
			set_bit(pcm_idx, &spec->pcm_bitmap);
			continue;
		}

		err = generic_hdmi_build_jack(codec, pcm_idx);
		if (err < 0)
			return err;

		/* create the spdif for each pcm
		 * pin will be bound when monitor is connected
		 */
		err = snd_hda_create_dig_out_ctls(codec,
					  0, spec->cvt_nids[0],
					  HDA_PCM_TYPE_HDMI);
		if (err < 0)
			return err;
		snd_hda_spdif_ctls_unassign(codec, pcm_idx);

		dev = get_pcm_rec(spec, pcm_idx)->device;
		if (dev != SNDRV_PCM_INVALID_DEVICE) {
			/* add control for ELD Bytes */
			err = hdmi_create_eld_ctl(codec, pcm_idx, dev);
			if (err < 0)
				return err;
		}
	}

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);
		struct hdmi_eld *pin_eld = &per_pin->sink_eld;

		if (spec->static_pcm_mapping) {
			hdmi_attach_hda_pcm(spec, per_pin);
			hdmi_pcm_setup_pin(spec, per_pin);
		}

		pin_eld->eld_valid = false;
		hdmi_present_sense(per_pin, 0);
	}

	/* add channel maps */
	for (pcm_idx = 0; pcm_idx < spec->pcm_used; pcm_idx++) {
		struct hda_pcm *pcm;

		pcm = get_pcm_rec(spec, pcm_idx);
		if (!pcm || !pcm->pcm)
			break;
		err = snd_hdac_add_chmap_ctls(pcm->pcm, pcm_idx, &spec->chmap);
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_build_controls, "SND_HDA_CODEC_HDMI");

int snd_hda_hdmi_generic_init_per_pins(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);

		per_pin->codec = codec;
		mutex_init(&per_pin->lock);
		INIT_DELAYED_WORK(&per_pin->work, hdmi_repoll_eld);
		eld_proc_new(per_pin, pin_idx);
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_init_per_pins, "SND_HDA_CODEC_HDMI");

int snd_hda_hdmi_generic_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	guard(mutex)(&spec->bind_lock);
	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);
		hda_nid_t pin_nid = per_pin->pin_nid;
		int dev_id = per_pin->dev_id;

		snd_hda_set_dev_select(codec, pin_nid, dev_id);
		hdmi_init_pin(codec, pin_nid);
		if (codec_has_acomp(codec))
			continue;
		snd_hda_jack_detect_enable_callback_mst(codec, pin_nid, dev_id,
							jack_callback);
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_init, "SND_HDA_CODEC_HDMI");

static void hdmi_array_init(struct hdmi_spec *spec, int nums)
{
	snd_array_init(&spec->pins, sizeof(struct hdmi_spec_per_pin), nums);
	snd_array_init(&spec->cvts, sizeof(struct hdmi_spec_per_cvt), nums);
}

static void hdmi_array_free(struct hdmi_spec *spec)
{
	snd_array_free(&spec->pins);
	snd_array_free(&spec->cvts);
}

void snd_hda_hdmi_generic_spec_free(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;

	if (spec) {
		hdmi_array_free(spec);
		kfree(spec);
		codec->spec = NULL;
	}
	codec->dp_mst = false;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_spec_free, "SND_HDA_CODEC_HDMI");

void snd_hda_hdmi_generic_remove(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx, pcm_idx;

	if (spec->acomp_registered) {
		snd_hdac_acomp_exit(&codec->bus->core);
	} else if (codec_has_acomp(codec)) {
		snd_hdac_acomp_register_notifier(&codec->bus->core, NULL);
	}
	codec->relaxed_resume = 0;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);
		cancel_delayed_work_sync(&per_pin->work);
		eld_proc_free(per_pin);
	}

	for (pcm_idx = 0; pcm_idx < spec->pcm_used; pcm_idx++) {
		if (spec->pcm_rec[pcm_idx].jack == NULL)
			continue;
		snd_device_free(codec->card, spec->pcm_rec[pcm_idx].jack);
	}

	snd_hda_hdmi_generic_spec_free(codec);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_remove, "SND_HDA_CODEC_HDMI");

int snd_hda_hdmi_generic_suspend(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);
		cancel_delayed_work_sync(&per_pin->work);
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_suspend, "SND_HDA_CODEC_HDMI");

int snd_hda_hdmi_generic_resume(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	snd_hda_codec_init(codec);
	snd_hda_regmap_sync(codec);

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);
		hdmi_present_sense(per_pin, 1);
	}
	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_resume, "SND_HDA_CODEC_HDMI");

static const struct hdmi_ops generic_standard_hdmi_ops = {
	.pin_get_eld				= hdmi_pin_get_eld,
	.pin_setup_infoframe			= hdmi_pin_setup_infoframe,
	.pin_hbr_setup				= hdmi_pin_hbr_setup,
	.setup_stream				= snd_hda_hdmi_setup_stream,
};

/* allocate codec->spec and assign/initialize generic parser ops */
int snd_hda_hdmi_generic_alloc(struct hda_codec *codec)
{
	struct hdmi_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	spec->codec = codec;
	spec->ops = generic_standard_hdmi_ops;
	spec->dev_num = 1;	/* initialize to 1 */
	mutex_init(&spec->pcm_lock);
	mutex_init(&spec->bind_lock);
	snd_hdac_register_chmap_ops(&codec->core, &spec->chmap);

	spec->chmap.ops.get_chmap = hdmi_get_chmap;
	spec->chmap.ops.set_chmap = hdmi_set_chmap;
	spec->chmap.ops.is_pcm_attached = is_hdmi_pcm_attached;
	spec->chmap.ops.get_spk_alloc = hdmi_get_spk_alloc;

	codec->spec = spec;
	hdmi_array_init(spec, 4);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_alloc, "SND_HDA_CODEC_HDMI");

/* generic HDMI parser */
int snd_hda_hdmi_generic_probe(struct hda_codec *codec)
{
	int err;

	err = snd_hda_hdmi_generic_alloc(codec);
	if (err < 0)
		return err;

	err = snd_hda_hdmi_parse_codec(codec);
	if (err < 0) {
		snd_hda_hdmi_generic_spec_free(codec);
		return err;
	}

	snd_hda_hdmi_generic_init_per_pins(codec);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_generic_probe, "SND_HDA_CODEC_HDMI");

/*
 * generic audio component binding
 */

/* turn on / off the unsol event jack detection dynamically */
static void reprogram_jack_detect(struct hda_codec *codec, hda_nid_t nid,
				  int dev_id, bool use_acomp)
{
	struct hda_jack_tbl *tbl;

	tbl = snd_hda_jack_tbl_get_mst(codec, nid, dev_id);
	if (tbl) {
		/* clear unsol even if component notifier is used, or re-enable
		 * if notifier is cleared
		 */
		unsigned int val = use_acomp ? 0 : (AC_USRSP_EN | tbl->tag);
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_UNSOLICITED_ENABLE, val);
	}
}

/* set up / clear component notifier dynamically */
static void generic_acomp_notifier_set(struct drm_audio_component *acomp,
				       bool use_acomp)
{
	struct hdmi_spec *spec;
	int i;

	spec = container_of(acomp->audio_ops, struct hdmi_spec, drm_audio_ops);
	guard(mutex)(&spec->bind_lock);
	spec->use_acomp_notifier = use_acomp;
	spec->codec->relaxed_resume = use_acomp;
	spec->codec->bus->keep_power = 0;
	/* reprogram each jack detection logic depending on the notifier */
	for (i = 0; i < spec->num_pins; i++)
		reprogram_jack_detect(spec->codec,
				      get_pin(spec, i)->pin_nid,
				      get_pin(spec, i)->dev_id,
				      use_acomp);
}

/* enable / disable the notifier via master bind / unbind */
int snd_hda_hdmi_acomp_master_bind(struct device *dev,
				   struct drm_audio_component *acomp)
{
	generic_acomp_notifier_set(acomp, true);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_acomp_master_bind, "SND_HDA_CODEC_HDMI");

void snd_hda_hdmi_acomp_master_unbind(struct device *dev,
				      struct drm_audio_component *acomp)
{
	generic_acomp_notifier_set(acomp, false);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_acomp_master_unbind, "SND_HDA_CODEC_HDMI");

/* check whether both HD-audio and DRM PCI devices belong to the same bus */
static int match_bound_vga(struct device *dev, int subtype, void *data)
{
	struct hdac_bus *bus = data;
	struct pci_dev *pci, *master;

	if (!dev_is_pci(dev) || !dev_is_pci(bus->dev))
		return 0;
	master = to_pci_dev(bus->dev);
	pci = to_pci_dev(dev);
	return master->bus == pci->bus;
}

/* audio component notifier for AMD/Nvidia HDMI codecs */
void snd_hda_hdmi_acomp_pin_eld_notify(void *audio_ptr, int port, int dev_id)
{
	struct hda_codec *codec = audio_ptr;
	struct hdmi_spec *spec = codec->spec;
	hda_nid_t pin_nid = spec->port2pin(codec, port);

	if (!pin_nid)
		return;
	if (get_wcaps_type(get_wcaps(codec, pin_nid)) != AC_WID_PIN)
		return;
	/* skip notification during system suspend (but not in runtime PM);
	 * the state will be updated at resume
	 */
	if (codec->core.dev.power.power_state.event == PM_EVENT_SUSPEND)
		return;

	snd_hda_hdmi_check_presence_and_report(codec, pin_nid, dev_id);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_acomp_pin_eld_notify, "SND_HDA_CODEC_HDMI");

/* set up the private drm_audio_ops from the template */
void snd_hda_hdmi_setup_drm_audio_ops(struct hda_codec *codec,
				      const struct drm_audio_component_audio_ops *ops)
{
	struct hdmi_spec *spec = codec->spec;

	spec->drm_audio_ops.audio_ptr = codec;
	/* intel_audio_codec_enable() or intel_audio_codec_disable()
	 * will call pin_eld_notify with using audio_ptr pointer
	 * We need make sure audio_ptr is really setup
	 */
	wmb();
	spec->drm_audio_ops.pin2port = ops->pin2port;
	spec->drm_audio_ops.pin_eld_notify = ops->pin_eld_notify;
	spec->drm_audio_ops.master_bind = ops->master_bind;
	spec->drm_audio_ops.master_unbind = ops->master_unbind;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_setup_drm_audio_ops, "SND_HDA_CODEC_HDMI");

/* initialize the generic HDMI audio component */
void snd_hda_hdmi_acomp_init(struct hda_codec *codec,
			     const struct drm_audio_component_audio_ops *ops,
			     int (*port2pin)(struct hda_codec *, int))
{
	struct hdmi_spec *spec = codec->spec;

	if (!enable_acomp) {
		codec_info(codec, "audio component disabled by module option\n");
		return;
	}

	spec->port2pin = port2pin;
	snd_hda_hdmi_setup_drm_audio_ops(codec, ops);
	if (!snd_hdac_acomp_init(&codec->bus->core, &spec->drm_audio_ops,
				 match_bound_vga, 0)) {
		spec->acomp_registered = true;
	}
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_acomp_init, "SND_HDA_CODEC_HDMI");

/*
 */

enum {
	MODEL_GENERIC,
	MODEL_GF,
};

static int generichdmi_probe(struct hda_codec *codec,
			     const struct hda_device_id *id)
{
	int err;

	err = snd_hda_hdmi_generic_probe(codec);
	if (err < 0)
		return err;
	/*
	 * Glenfly GPUs have two codecs, stream switches from one codec to
	 * another, need to do actual clean-ups in codec_cleanup_stream
	 */
	if (id->driver_data == MODEL_GF)
		codec->no_sticky_stream = 1;

	return 0;
}

static const struct hda_codec_ops generichdmi_codec_ops = {
	.probe = generichdmi_probe,
	.remove = snd_hda_hdmi_generic_remove,
	.init = snd_hda_hdmi_generic_init,
	.build_pcms = snd_hda_hdmi_generic_build_pcms,
	.build_controls = snd_hda_hdmi_generic_build_controls,
	.unsol_event = snd_hda_hdmi_generic_unsol_event,
	.suspend = snd_hda_hdmi_generic_suspend,
	.resume	 = snd_hda_hdmi_generic_resume,
};

/*
 */
static const struct hda_device_id snd_hda_id_generichdmi[] = {
	HDA_CODEC_ID_MODEL(0x00147a47, "Loongson HDMI",		MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10951390, "SiI1390 HDMI",		MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x10951392, "SiI1392 HDMI",		MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x11069f84, "VX11 HDMI/DP",		MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x11069f85, "VX11 HDMI/DP",		MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x17e80047, "Chrontel HDMI",		MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x1d179f86, "ZX-100S HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f87, "ZX-100S HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f88, "KX-5000 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f89, "KX-5000 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f8a, "KX-6000 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f8b, "KX-6000 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f8c, "KX-6000G HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f8d, "KX-6000G HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f8e, "KX-7000 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f8f, "KX-7000 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x1d179f90, "KX-7000 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x67663d82, "Arise 82 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x67663d83, "Arise 83 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x67663d84, "Arise 84 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x67663d85, "Arise 85 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x67663d86, "Arise 86 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x67663d87, "Arise 87 HDMI/DP",	MODEL_GF),
	HDA_CODEC_ID_MODEL(0x80862801, "Bearlake HDMI",		MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x80862802, "Cantiga HDMI",		MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x80862803, "Eaglelake HDMI",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x80862880, "CedarTrail HDMI",	MODEL_GENERIC),
	HDA_CODEC_ID_MODEL(0x808629fb, "Crestline HDMI",	MODEL_GENERIC),
	/* special ID for generic HDMI */
	HDA_CODEC_ID_MODEL(HDA_CODEC_ID_GENERIC_HDMI, "Generic HDMI", MODEL_GENERIC),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_generichdmi);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic HDMI HD-audio codec");

static struct hda_codec_driver generichdmi_driver = {
	.id = snd_hda_id_generichdmi,
	.ops = &generichdmi_codec_ops,
};

module_hda_codec_driver(generichdmi_driver);
