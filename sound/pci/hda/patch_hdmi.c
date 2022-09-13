// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  patch_hdmi.c - routines for HDMI/DisplayPort codecs
 *
 *  Copyright(c) 2008-2010 Intel Corporation. All rights reserved.
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

static bool static_hdmi_pcm;
module_param(static_hdmi_pcm, bool, 0644);
MODULE_PARM_DESC(static_hdmi_pcm, "Don't restrict PCM parameters per ELD info");

static bool enable_acomp = true;
module_param(enable_acomp, bool, 0444);
MODULE_PARM_DESC(enable_acomp, "Enable audio component binding (default=yes)");

static bool enable_silent_stream =
IS_ENABLED(CONFIG_SND_HDA_INTEL_HDMI_SILENT_STREAM);
module_param(enable_silent_stream, bool, 0644);
MODULE_PARM_DESC(enable_silent_stream, "Enable Silent Stream for HDMI devices");

struct hdmi_spec_per_cvt {
	hda_nid_t cvt_nid;
	int assigned;
	unsigned int channels_min;
	unsigned int channels_max;
	u32 rates;
	u64 formats;
	unsigned int maxbps;
};

/* max. connections to a widget */
#define HDA_MAX_CONNECTIONS	32

struct hdmi_spec_per_pin {
	hda_nid_t pin_nid;
	int dev_id;
	/* pin idx, different device entries on the same pin use the same idx */
	int pin_nid_idx;
	int num_mux_nids;
	hda_nid_t mux_nids[HDA_MAX_CONNECTIONS];
	int mux_idx;
	hda_nid_t cvt_nid;

	struct hda_codec *codec;
	struct hdmi_eld sink_eld;
	struct mutex lock;
	struct delayed_work work;
	struct hdmi_pcm *pcm; /* pointer to spec->pcm_rec[n] dynamically*/
	int pcm_idx; /* which pcm is attached. -1 means no pcm is attached */
	int repoll_count;
	bool setup; /* the stream has been set up by prepare callback */
	bool silent_stream;
	int channels; /* current number of channels */
	bool non_pcm;
	bool chmap_set;		/* channel-map override by ALSA API? */
	unsigned char chmap[8]; /* ALSA API channel-map */
#ifdef CONFIG_SND_PROC_FS
	struct snd_info_entry *proc_entry;
#endif
};

/* operations used by generic code that can be overridden by patches */
struct hdmi_ops {
	int (*pin_get_eld)(struct hda_codec *codec, hda_nid_t pin_nid,
			   int dev_id, unsigned char *buf, int *eld_size);

	void (*pin_setup_infoframe)(struct hda_codec *codec, hda_nid_t pin_nid,
				    int dev_id,
				    int ca, int active_channels, int conn_type);

	/* enable/disable HBR (HD passthrough) */
	int (*pin_hbr_setup)(struct hda_codec *codec, hda_nid_t pin_nid,
			     int dev_id, bool hbr);

	int (*setup_stream)(struct hda_codec *codec, hda_nid_t cvt_nid,
			    hda_nid_t pin_nid, int dev_id, u32 stream_tag,
			    int format);

	void (*pin_cvt_fixup)(struct hda_codec *codec,
			      struct hdmi_spec_per_pin *per_pin,
			      hda_nid_t cvt_nid);
};

struct hdmi_pcm {
	struct hda_pcm *pcm;
	struct snd_jack *jack;
	struct snd_kcontrol *eld_ctl;
};

struct hdmi_spec {
	struct hda_codec *codec;
	int num_cvts;
	struct snd_array cvts; /* struct hdmi_spec_per_cvt */
	hda_nid_t cvt_nids[4]; /* only for haswell fix */

	/*
	 * num_pins is the number of virtual pins
	 * for example, there are 3 pins, and each pin
	 * has 4 device entries, then the num_pins is 12
	 */
	int num_pins;
	/*
	 * num_nids is the number of real pins
	 * In the above example, num_nids is 3
	 */
	int num_nids;
	/*
	 * dev_num is the number of device entries
	 * on each pin.
	 * In the above example, dev_num is 4
	 */
	int dev_num;
	struct snd_array pins; /* struct hdmi_spec_per_pin */
	struct hdmi_pcm pcm_rec[16];
	struct mutex pcm_lock;
	struct mutex bind_lock; /* for audio component binding */
	/* pcm_bitmap means which pcms have been assigned to pins*/
	unsigned long pcm_bitmap;
	int pcm_used;	/* counter of pcm_rec[] */
	/* bitmap shows whether the pcm is opened in user space
	 * bit 0 means the first playback PCM (PCM3);
	 * bit 1 means the second playback PCM, and so on.
	 */
	unsigned long pcm_in_use;

	struct hdmi_eld temp_eld;
	struct hdmi_ops ops;

	bool dyn_pin_out;
	bool dyn_pcm_assign;
	bool dyn_pcm_no_legacy;
	bool nv_dp_workaround; /* workaround DP audio infoframe for Nvidia */

	bool intel_hsw_fixup;	/* apply Intel platform-specific fixups */
	/*
	 * Non-generic VIA/NVIDIA specific
	 */
	struct hda_multi_out multiout;
	struct hda_pcm_stream pcm_playback;

	bool use_acomp_notifier; /* use eld_notify callback for hotplug */
	bool acomp_registered; /* audio component registered in this driver */
	bool force_connect; /* force connectivity */
	struct drm_audio_component_audio_ops drm_audio_ops;
	int (*port2pin)(struct hda_codec *, int); /* reverse port/pin mapping */

	struct hdac_chmap chmap;
	hda_nid_t vendor_nid;
	const int *port_map;
	int port_num;
	bool send_silent_stream; /* Flag to enable silent stream feature */
};

#ifdef CONFIG_SND_HDA_COMPONENT
static inline bool codec_has_acomp(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	return spec->use_acomp_notifier;
}
#else
#define codec_has_acomp(codec)	false
#endif

struct hdmi_audio_infoframe {
	u8 type; /* 0x84 */
	u8 ver;  /* 0x01 */
	u8 len;  /* 0x0a */

	u8 checksum;

	u8 CC02_CT47;	/* CC in bits 0:2, CT in 4:7 */
	u8 SS01_SF24;
	u8 CXT04;
	u8 CA;
	u8 LFEPBL01_LSV36_DM_INH7;
};

struct dp_audio_infoframe {
	u8 type; /* 0x84 */
	u8 len;  /* 0x1b */
	u8 ver;  /* 0x11 << 2 */

	u8 CC02_CT47;	/* match with HDMI infoframe from this on */
	u8 SS01_SF24;
	u8 CXT04;
	u8 CA;
	u8 LFEPBL01_LSV36_DM_INH7;
};

union audio_infoframe {
	struct hdmi_audio_infoframe hdmi;
	struct dp_audio_infoframe dp;
	u8 bytes[0];
};

/*
 * HDMI routines
 */

#define get_pin(spec, idx) \
	((struct hdmi_spec_per_pin *)snd_array_elem(&spec->pins, idx))
#define get_cvt(spec, idx) \
	((struct hdmi_spec_per_cvt  *)snd_array_elem(&spec->cvts, idx))
/* obtain hdmi_pcm object assigned to idx */
#define get_hdmi_pcm(spec, idx)	(&(spec)->pcm_rec[idx])
/* obtain hda_pcm object assigned to idx */
#define get_pcm_rec(spec, idx)	(get_hdmi_pcm(spec, idx)->pcm)

static int pin_id_to_pin_index(struct hda_codec *codec,
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

	codec_warn(codec, "HDMI: pin nid %d not registered\n", pin_nid);
	return -EINVAL;
}

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

	codec_warn(codec, "HDMI: cvt nid %d not registered\n", cvt_nid);
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
	mutex_lock(&spec->pcm_lock);
	per_pin = pcm_idx_to_pin(spec, pcm_idx);
	if (!per_pin) {
		/* no pin is bound to the pcm */
		uinfo->count = 0;
		goto unlock;
	}
	eld = &per_pin->sink_eld;
	uinfo->count = eld->eld_valid ? eld->eld_size : 0;

 unlock:
	mutex_unlock(&spec->pcm_lock);
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
	int err = 0;

	pcm_idx = kcontrol->private_value;
	mutex_lock(&spec->pcm_lock);
	per_pin = pcm_idx_to_pin(spec, pcm_idx);
	if (!per_pin) {
		/* no pin is bound to the pcm */
		memset(ucontrol->value.bytes.data, 0,
		       ARRAY_SIZE(ucontrol->value.bytes.data));
		goto unlock;
	}

	eld = &per_pin->sink_eld;
	if (eld->eld_size > ARRAY_SIZE(ucontrol->value.bytes.data) ||
	    eld->eld_size > ELD_MAX_SIZE) {
		snd_BUG();
		err = -EINVAL;
		goto unlock;
	}

	memset(ucontrol->value.bytes.data, 0,
	       ARRAY_SIZE(ucontrol->value.bytes.data));
	if (eld->eld_valid)
		memcpy(ucontrol->value.bytes.data, eld->eld_buffer,
		       eld->eld_size);

 unlock:
	mutex_unlock(&spec->pcm_lock);
	return err;
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

	mutex_lock(&per_pin->lock);
	snd_hdmi_print_eld_info(&per_pin->sink_eld, buffer);
	mutex_unlock(&per_pin->lock);
}

static void write_eld_info(struct snd_info_entry *entry,
			   struct snd_info_buffer *buffer)
{
	struct hdmi_spec_per_pin *per_pin = entry->private_data;

	mutex_lock(&per_pin->lock);
	snd_hdmi_write_eld_info(&per_pin->sink_eld, buffer);
	mutex_unlock(&per_pin->lock);
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

	if (snd_hda_codec_read(codec, pin_nid, 0, AC_VERB_GET_HDMI_DIP_XMIT, 0)
							    != AC_DIPXMIT_BEST)
		return false;

	hdmi_set_dip_index(codec, pin_nid, 0x0, 0x0);
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
		codec_dbg(codec, "HDMI: unknown connection type at pin %d\n",
			    pin_nid);
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
		codec_dbg(codec,
			  "hdmi_pin_setup_infoframe: pin=%d channels=%d ca=0x%02x\n",
			    pin_nid,
			    active_channels, ca);
		hdmi_stop_infoframe_trans(codec, pin_nid);
		hdmi_fill_audio_infoframe(codec, pin_nid,
					    ai.bytes, sizeof(ai));
		hdmi_start_infoframe_trans(codec, pin_nid);
	}
}

static void hdmi_setup_audio_infoframe(struct hda_codec *codec,
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

/*
 * Unsolicited events
 */

static void hdmi_present_sense(struct hdmi_spec_per_pin *per_pin, int repoll);

static void check_presence_and_report(struct hda_codec *codec, hda_nid_t nid,
				      int dev_id)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx = pin_id_to_pin_index(codec, nid, dev_id);

	if (pin_idx < 0)
		return;
	mutex_lock(&spec->pcm_lock);
	hdmi_present_sense(get_pin(spec, pin_idx), 1);
	mutex_unlock(&spec->pcm_lock);
}

static void jack_callback(struct hda_codec *codec,
			  struct hda_jack_callback *jack)
{
	/* stop polling when notification is enabled */
	if (codec_has_acomp(codec))
		return;

	check_presence_and_report(codec, jack->nid, jack->dev_id);
}

static void hdmi_intrinsic_event(struct hda_codec *codec, unsigned int res,
				 struct hda_jack_tbl *jack)
{
	jack->jack_dirty = 1;

	codec_dbg(codec,
		"HDMI hot plug event: Codec=%d Pin=%d Device=%d Inactive=%d Presence_Detect=%d ELD_Valid=%d\n",
		codec->addr, jack->nid, jack->dev_id, !!(res & AC_UNSOL_RES_IA),
		!!(res & AC_UNSOL_RES_PD), !!(res & AC_UNSOL_RES_ELDV));

	check_presence_and_report(codec, jack->nid, jack->dev_id);
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


static void hdmi_unsol_event(struct hda_codec *codec, unsigned int res)
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

static void haswell_verify_D0(struct hda_codec *codec,
		hda_nid_t cvt_nid, hda_nid_t nid)
{
	int pwr;

	/* For Haswell, the converter 1/2 may keep in D3 state after bootup,
	 * thus pins could only choose converter 0 for use. Make sure the
	 * converters are in correct power state */
	if (!snd_hda_check_power_state(codec, cvt_nid, AC_PWRST_D0))
		snd_hda_codec_write(codec, cvt_nid, 0, AC_VERB_SET_POWER_STATE, AC_PWRST_D0);

	if (!snd_hda_check_power_state(codec, nid, AC_PWRST_D0)) {
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_POWER_STATE,
				    AC_PWRST_D0);
		msleep(40);
		pwr = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_POWER_STATE, 0);
		pwr = (pwr & AC_PWRST_ACTUAL) >> AC_PWRST_ACTUAL_SHIFT;
		codec_dbg(codec, "Haswell HDMI audio: Power for pin 0x%x is now D%d\n", nid, pwr);
	}
}

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

static int hdmi_setup_stream(struct hda_codec *codec, hda_nid_t cvt_nid,
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

/* Try to find an available converter
 * If pin_idx is less then zero, just try to find an available converter.
 * Otherwise, try to find an available converter and get the cvt mux index
 * of the pin.
 */
static int hdmi_choose_cvt(struct hda_codec *codec,
			   int pin_idx, int *cvt_id)
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
		if (cvt_id)
			*cvt_id = cvt_idx;
		return 0;
	}

	/* Dynamically assign converter to stream */
	for (cvt_idx = 0; cvt_idx < spec->num_cvts; cvt_idx++) {
		per_cvt = get_cvt(spec, cvt_idx);

		/* Must not already be assigned */
		if (per_cvt->assigned)
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

/* Assure the pin select the right convetor */
static void intel_verify_pin_cvt_connect(struct hda_codec *codec,
			struct hdmi_spec_per_pin *per_pin)
{
	hda_nid_t pin_nid = per_pin->pin_nid;
	int mux_idx, curr;

	mux_idx = per_pin->mux_idx;
	curr = snd_hda_codec_read(codec, pin_nid, 0,
					  AC_VERB_GET_CONNECT_SEL, 0);
	if (curr != mux_idx)
		snd_hda_codec_write_cache(codec, pin_nid, 0,
					    AC_VERB_SET_CONNECT_SEL,
					    mux_idx);
}

/* get the mux index for the converter of the pins
 * converter's mux index is the same for all pins on Intel platform
 */
static int intel_cvt_id_to_mux_idx(struct hdmi_spec *spec,
			hda_nid_t cvt_nid)
{
	int i;

	for (i = 0; i < spec->num_cvts; i++)
		if (spec->cvt_nids[i] == cvt_nid)
			return i;
	return -EINVAL;
}

/* Intel HDMI workaround to fix audio routing issue:
 * For some Intel display codecs, pins share the same connection list.
 * So a conveter can be selected by multiple pins and playback on any of these
 * pins will generate sound on the external display, because audio flows from
 * the same converter to the display pipeline. Also muting one pin may make
 * other pins have no sound output.
 * So this function assures that an assigned converter for a pin is not selected
 * by any other pins.
 */
static void intel_not_share_assigned_cvt(struct hda_codec *codec,
					 hda_nid_t pin_nid,
					 int dev_id, int mux_idx)
{
	struct hdmi_spec *spec = codec->spec;
	hda_nid_t nid;
	int cvt_idx, curr;
	struct hdmi_spec_per_cvt *per_cvt;
	struct hdmi_spec_per_pin *per_pin;
	int pin_idx;

	/* configure the pins connections */
	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		int dev_id_saved;
		int dev_num;

		per_pin = get_pin(spec, pin_idx);
		/*
		 * pin not connected to monitor
		 * no need to operate on it
		 */
		if (!per_pin->pcm)
			continue;

		if ((per_pin->pin_nid == pin_nid) &&
			(per_pin->dev_id == dev_id))
			continue;

		/*
		 * if per_pin->dev_id >= dev_num,
		 * snd_hda_get_dev_select() will fail,
		 * and the following operation is unpredictable.
		 * So skip this situation.
		 */
		dev_num = snd_hda_get_num_devices(codec, per_pin->pin_nid) + 1;
		if (per_pin->dev_id >= dev_num)
			continue;

		nid = per_pin->pin_nid;

		/*
		 * Calling this function should not impact
		 * on the device entry selection
		 * So let's save the dev id for each pin,
		 * and restore it when return
		 */
		dev_id_saved = snd_hda_get_dev_select(codec, nid);
		snd_hda_set_dev_select(codec, nid, per_pin->dev_id);
		curr = snd_hda_codec_read(codec, nid, 0,
					  AC_VERB_GET_CONNECT_SEL, 0);
		if (curr != mux_idx) {
			snd_hda_set_dev_select(codec, nid, dev_id_saved);
			continue;
		}


		/* choose an unassigned converter. The conveters in the
		 * connection list are in the same order as in the codec.
		 */
		for (cvt_idx = 0; cvt_idx < spec->num_cvts; cvt_idx++) {
			per_cvt = get_cvt(spec, cvt_idx);
			if (!per_cvt->assigned) {
				codec_dbg(codec,
					  "choose cvt %d for pin nid %d\n",
					cvt_idx, nid);
				snd_hda_codec_write_cache(codec, nid, 0,
					    AC_VERB_SET_CONNECT_SEL,
					    cvt_idx);
				break;
			}
		}
		snd_hda_set_dev_select(codec, nid, dev_id_saved);
	}
}

/* A wrapper of intel_not_share_asigned_cvt() */
static void intel_not_share_assigned_cvt_nid(struct hda_codec *codec,
			hda_nid_t pin_nid, int dev_id, hda_nid_t cvt_nid)
{
	int mux_idx;
	struct hdmi_spec *spec = codec->spec;

	/* On Intel platform, the mapping of converter nid to
	 * mux index of the pins are always the same.
	 * The pin nid may be 0, this means all pins will not
	 * share the converter.
	 */
	mux_idx = intel_cvt_id_to_mux_idx(spec, cvt_nid);
	if (mux_idx >= 0)
		intel_not_share_assigned_cvt(codec, pin_nid, dev_id, mux_idx);
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

/* called in hdmi_pcm_open when no pin is assigned to the PCM
 * in dyn_pcm_assign mode.
 */
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

	err = hdmi_choose_cvt(codec, -1, &cvt_idx);
	if (err)
		return err;

	per_cvt = get_cvt(spec, cvt_idx);
	per_cvt->assigned = 1;
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

	mutex_lock(&spec->pcm_lock);
	pin_idx = hinfo_to_pin_index(codec, hinfo);
	if (!spec->dyn_pcm_assign) {
		if (snd_BUG_ON(pin_idx < 0)) {
			err = -EINVAL;
			goto unlock;
		}
	} else {
		/* no pin is assigned to the PCM
		 * PA need pcm open successfully when probe
		 */
		if (pin_idx < 0) {
			err = hdmi_pcm_open_no_pin(hinfo, codec, substream);
			goto unlock;
		}
	}

	err = hdmi_choose_cvt(codec, pin_idx, &cvt_idx);
	if (err < 0)
		goto unlock;

	per_cvt = get_cvt(spec, cvt_idx);
	/* Claim converter */
	per_cvt->assigned = 1;

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
			per_cvt->assigned = 0;
			hinfo->nid = 0;
			snd_hda_spdif_ctls_unassign(codec, pcm_idx);
			err = -ENODEV;
			goto unlock;
		}
	}

	/* Store the updated parameters */
	runtime->hw.channels_min = hinfo->channels_min;
	runtime->hw.channels_max = hinfo->channels_max;
	runtime->hw.formats = hinfo->formats;
	runtime->hw.rates = hinfo->rates;

	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_CHANNELS, 2);
 unlock:
	mutex_unlock(&spec->pcm_lock);
	return err;
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
			   "HDMI: pin %d wcaps %#x does not support connection list\n",
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

	/* on the new machines, try to assign the pcm slot dynamically,
	 * not use the preferred fixed map (legacy way) anymore.
	 */
	if (spec->dyn_pcm_no_legacy)
		goto last_try;

	/*
	 * generic_hdmi_build_pcms() may allocate extra PCMs on some
	 * platforms (with maximum of 'num_nids + dev_num - 1')
	 *
	 * The per_pin of pin_nid_idx=n and dev_id=m prefers to get pcm-n
	 * if m==0. This guarantees that dynamic pcm assignments are compatible
	 * with the legacy static per_pin-pcm assignment that existed in the
	 * days before DP-MST.
	 *
	 * Intel DP-MST prefers this legacy behavior for compatibility, too.
	 *
	 * per_pin of m!=0 prefers to get pcm=(num_nids + (m - 1)).
	 */

	if (per_pin->dev_id == 0 || spec->intel_hsw_fixup) {
		if (!test_bit(per_pin->pin_nid_idx, &spec->pcm_bitmap))
			return per_pin->pin_nid_idx;
	} else {
		i = spec->num_nids + (per_pin->dev_id - 1);
		if (i < spec->pcm_used && !(test_bit(i, &spec->pcm_bitmap)))
			return i;
	}

	/* have a second try; check the area over num_nids */
	for (i = spec->num_nids; i < spec->pcm_used; i++) {
		if (!test_bit(i, &spec->pcm_bitmap))
			return i;
	}

 last_try:
	/* the last try; check the empty slots in pins */
	for (i = 0; i < spec->num_nids; i++) {
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
	idx = hdmi_find_pcm_slot(spec, per_pin);
	if (idx == -EBUSY)
		return;
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

	if (per_pin->pcm_idx >= 0 && per_pin->pcm_idx < spec->pcm_used)
		pcm = get_pcm_rec(spec, per_pin->pcm_idx);
	else
		return;
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

	hdmi_setup_audio_infoframe(codec, per_pin, non_pcm);
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
		    snd_hdmi_parse_eld(codec, &eld->info, eld->eld_buffer,
				       eld->eld_size) < 0) {
			eld->eld_valid = false;
			if (repoll) {
				schedule_delayed_work(&per_pin->work,
						      msecs_to_jiffies(300));
				return;
			}
		}
	}

	if (!eld->eld_valid || eld->eld_size <= 0) {
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

	if (spec->dyn_pcm_assign) {
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
		snd_hdmi_show_eld(codec, &eld->info);

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
		hdmi_setup_audio_infoframe(codec, per_pin, per_pin->non_pcm);
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
	int ret;

#ifdef	CONFIG_PM
	if (dev->power.runtime_status == RPM_SUSPENDING)
		return;
#endif

	ret = snd_hda_power_up_pm(codec);
	if (ret < 0 && pm_runtime_suspended(dev))
		goto out;

	present = snd_hda_jack_pin_sense(codec, pin_nid, dev_id);

	mutex_lock(&per_pin->lock);
	eld->monitor_present = !!(present & AC_PINSENSE_PRESENCE);
	if (eld->monitor_present)
		eld->eld_valid  = !!(present & AC_PINSENSE_ELDV);
	else
		eld->eld_valid = false;

	codec_dbg(codec,
		"HDMI status: Codec=%d Pin=%d Presence_Detect=%d ELD_Valid=%d\n",
		codec->addr, pin_nid, eld->monitor_present, eld->eld_valid);

	if (eld->eld_valid) {
		if (spec->ops.pin_get_eld(codec, pin_nid, dev_id,
					  eld->eld_buffer, &eld->eld_size) < 0)
			eld->eld_valid = false;
	}

	update_eld(codec, per_pin, eld, repoll);
	mutex_unlock(&per_pin->lock);
 out:
	snd_hda_power_down_pm(codec);
}

#define I915_SILENT_RATE		48000
#define I915_SILENT_CHANNELS		2
#define I915_SILENT_FORMAT		SNDRV_PCM_FORMAT_S16_LE
#define I915_SILENT_FORMAT_BITS	16
#define I915_SILENT_FMT_MASK		0xf

static void silent_stream_enable(struct hda_codec *codec,
				 struct hdmi_spec_per_pin *per_pin)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_cvt *per_cvt;
	int cvt_idx, pin_idx, err;
	unsigned int format;

	mutex_lock(&per_pin->lock);

	if (per_pin->setup) {
		codec_dbg(codec, "hdmi: PCM already open, no silent stream\n");
		goto unlock_out;
	}

	pin_idx = pin_id_to_pin_index(codec, per_pin->pin_nid, per_pin->dev_id);
	err = hdmi_choose_cvt(codec, pin_idx, &cvt_idx);
	if (err) {
		codec_err(codec, "hdmi: no free converter to enable silent mode\n");
		goto unlock_out;
	}

	per_cvt = get_cvt(spec, cvt_idx);
	per_cvt->assigned = 1;
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

	snd_hdac_sync_audio_rate(&codec->core, per_pin->pin_nid,
				 per_pin->dev_id, I915_SILENT_RATE);

	/* trigger silent stream generation in hw */
	format = snd_hdac_calc_stream_format(I915_SILENT_RATE, I915_SILENT_CHANNELS,
					     I915_SILENT_FORMAT, I915_SILENT_FORMAT_BITS, 0);
	snd_hda_codec_setup_stream(codec, per_pin->cvt_nid,
				   I915_SILENT_FMT_MASK, I915_SILENT_FMT_MASK, format);
	usleep_range(100, 200);
	snd_hda_codec_setup_stream(codec, per_pin->cvt_nid, I915_SILENT_FMT_MASK, 0, format);

	per_pin->channels = I915_SILENT_CHANNELS;
	hdmi_setup_audio_infoframe(codec, per_pin, per_pin->non_pcm);

 unlock_out:
	mutex_unlock(&per_pin->lock);
}

static void silent_stream_disable(struct hda_codec *codec,
				  struct hdmi_spec_per_pin *per_pin)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_cvt *per_cvt;
	int cvt_idx;

	mutex_lock(&per_pin->lock);
	if (!per_pin->silent_stream)
		goto unlock_out;

	codec_dbg(codec, "HDMI: disable silent stream on pin-NID=0x%x cvt-NID=0x%x\n",
		  per_pin->pin_nid, per_pin->cvt_nid);

	cvt_idx = cvt_nid_to_cvt_index(codec, per_pin->cvt_nid);
	if (cvt_idx >= 0 && cvt_idx < spec->num_cvts) {
		per_cvt = get_cvt(spec, cvt_idx);
		per_cvt->assigned = 0;
	}

	per_pin->cvt_nid = 0;
	per_pin->silent_stream = false;

 unlock_out:
	mutex_unlock(&per_pin->lock);
}

/* update ELD and jack state via audio component */
static void sync_eld_via_acomp(struct hda_codec *codec,
			       struct hdmi_spec_per_pin *per_pin)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_eld *eld = &spec->temp_eld;
	bool monitor_prev, monitor_next;

	mutex_lock(&per_pin->lock);
	eld->monitor_present = false;
	monitor_prev = per_pin->sink_eld.monitor_present;
	eld->eld_size = snd_hdac_acomp_get_eld(&codec->core, per_pin->pin_nid,
				      per_pin->dev_id, &eld->monitor_present,
				      eld->eld_buffer, ELD_MAX_SIZE);
	eld->eld_valid = (eld->eld_size > 0);
	update_eld(codec, per_pin, eld, 0);
	monitor_next = per_pin->sink_eld.monitor_present;
	mutex_unlock(&per_pin->lock);

	/*
	 * Power-up will call hdmi_present_sense, so the PM calls
	 * have to be done without mutex held.
	 */

	if (spec->send_silent_stream) {
		int pm_ret;

		if (!monitor_prev && monitor_next) {
			pm_ret = snd_hda_power_up_pm(codec);
			if (pm_ret < 0)
				codec_err(codec,
				"Monitor plugged-in, Failed to power up codec ret=[%d]\n",
				pm_ret);
			silent_stream_enable(codec, per_pin);
		} else if (monitor_prev && !monitor_next) {
			silent_stream_disable(codec, per_pin);
			pm_ret = snd_hda_power_down_pm(codec);
			if (pm_ret < 0)
				codec_err(codec,
				"Monitor plugged-out, Failed to power down codec ret=[%d]\n",
				pm_ret);
		}
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

	mutex_lock(&spec->pcm_lock);
	hdmi_present_sense(per_pin, per_pin->repoll_count);
	mutex_unlock(&spec->pcm_lock);
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
		 * On Intel platforms, device entries number is
		 * changed dynamically. If there is a DP MST
		 * hub connected, the device entries number is 3.
		 * Otherwise, it is 1.
		 * Here we manually set dev_num to 3, so that
		 * we can initialize all the device entries when
		 * bootup statically.
		 */
		dev_num = 3;
		spec->dev_num = 3;
	} else if (spec->dyn_pcm_assign && codec->dp_mst) {
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

		if (spec->dyn_pcm_assign) {
			per_pin->pcm = NULL;
			per_pin->pcm_idx = -1;
		} else {
			per_pin->pcm = get_hdmi_pcm(spec, pin_idx);
			per_pin->pcm_idx = pin_idx;
		}
		per_pin->pin_nid = pin_nid;
		per_pin->pin_nid_idx = spec->num_nids;
		per_pin->dev_id = i;
		per_pin->non_pcm = false;
		snd_hda_set_dev_select(codec, pin_nid, i);
		err = hdmi_read_pin_conn(codec, pin_idx);
		if (err < 0)
			return err;
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
					  &per_cvt->maxbps);
	if (err < 0)
		return err;

	if (spec->num_cvts < ARRAY_SIZE(spec->cvt_nids))
		spec->cvt_nids[spec->num_cvts] = cvt_nid;
	spec->num_cvts++;

	return 0;
}

static const struct snd_pci_quirk force_connect_list[] = {
	SND_PCI_QUIRK(0x103c, 0x870f, "HP", 1),
	SND_PCI_QUIRK(0x103c, 0x871a, "HP", 1),
	SND_PCI_QUIRK(0x1462, 0xec94, "MS-7C94", 1),
	{}
};

static int hdmi_parse_codec(struct hda_codec *codec)
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

/*
 */
static bool check_non_pcm_per_cvt(struct hda_codec *codec, hda_nid_t cvt_nid)
{
	struct hda_spdif_out *spdif;
	bool non_pcm;

	mutex_lock(&codec->spdif_mutex);
	spdif = snd_hda_spdif_out_of_nid(codec, cvt_nid);
	/* Add sanity check to pass klockwork check.
	 * This should never happen.
	 */
	if (WARN_ON(spdif == NULL)) {
		mutex_unlock(&codec->spdif_mutex);
		return true;
	}
	non_pcm = !!(spdif->status & IEC958_AES0_NONAUDIO);
	mutex_unlock(&codec->spdif_mutex);
	return non_pcm;
}

/*
 * HDMI callbacks
 */

static int generic_hdmi_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
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
	int err = 0;

	mutex_lock(&spec->pcm_lock);
	pin_idx = hinfo_to_pin_index(codec, hinfo);
	if (spec->dyn_pcm_assign && pin_idx < 0) {
		/* when dyn_pcm_assign and pcm is not bound to a pin
		 * skip pin setup and return 0 to make audio playback
		 * be ongoing
		 */
		pin_cvt_fixup(codec, NULL, cvt_nid);
		snd_hda_codec_setup_stream(codec, cvt_nid,
					stream_tag, 0, format);
		goto unlock;
	}

	if (snd_BUG_ON(pin_idx < 0)) {
		err = -EINVAL;
		goto unlock;
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
	mutex_lock(&per_pin->lock);
	per_pin->channels = substream->runtime->channels;
	per_pin->setup = true;

	if (get_wcaps(codec, cvt_nid) & AC_WCAP_STRIPE) {
		stripe = snd_hdac_get_stream_stripe_ctl(&codec->bus->core,
							substream);
		snd_hda_codec_write(codec, cvt_nid, 0,
				    AC_VERB_SET_STRIPE_CONTROL,
				    stripe);
	}

	hdmi_setup_audio_infoframe(codec, per_pin, non_pcm);
	mutex_unlock(&per_pin->lock);
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
	err = spec->ops.setup_stream(codec, cvt_nid, per_pin->pin_nid,
				     per_pin->dev_id, stream_tag, format);
 unlock:
	mutex_unlock(&spec->pcm_lock);
	return err;
}

static int generic_hdmi_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					     struct hda_codec *codec,
					     struct snd_pcm_substream *substream)
{
	snd_hda_codec_cleanup_stream(codec, hinfo->nid);
	return 0;
}

static int hdmi_pcm_close(struct hda_pcm_stream *hinfo,
			  struct hda_codec *codec,
			  struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	int cvt_idx, pin_idx, pcm_idx;
	struct hdmi_spec_per_cvt *per_cvt;
	struct hdmi_spec_per_pin *per_pin;
	int pinctl;
	int err = 0;

	mutex_lock(&spec->pcm_lock);
	if (hinfo->nid) {
		pcm_idx = hinfo_to_pcm_index(codec, hinfo);
		if (snd_BUG_ON(pcm_idx < 0)) {
			err = -EINVAL;
			goto unlock;
		}
		cvt_idx = cvt_nid_to_cvt_index(codec, hinfo->nid);
		if (snd_BUG_ON(cvt_idx < 0)) {
			err = -EINVAL;
			goto unlock;
		}
		per_cvt = get_cvt(spec, cvt_idx);
		per_cvt->assigned = 0;
		hinfo->nid = 0;

		azx_stream(get_azx_dev(substream))->stripe = 0;

		snd_hda_spdif_ctls_unassign(codec, pcm_idx);
		clear_bit(pcm_idx, &spec->pcm_in_use);
		pin_idx = hinfo_to_pin_index(codec, hinfo);
		if (spec->dyn_pcm_assign && pin_idx < 0)
			goto unlock;

		if (snd_BUG_ON(pin_idx < 0)) {
			err = -EINVAL;
			goto unlock;
		}
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

		mutex_lock(&per_pin->lock);
		per_pin->chmap_set = false;
		memset(per_pin->chmap, 0, sizeof(per_pin->chmap));

		per_pin->setup = false;
		per_pin->channels = 0;
		mutex_unlock(&per_pin->lock);
	}

unlock:
	mutex_unlock(&spec->pcm_lock);

	return err;
}

static const struct hda_pcm_ops generic_ops = {
	.open = hdmi_pcm_open,
	.close = hdmi_pcm_close,
	.prepare = generic_hdmi_playback_pcm_prepare,
	.cleanup = generic_hdmi_playback_pcm_cleanup,
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
	mutex_lock(&per_pin->lock);
	per_pin->chmap_set = true;
	memcpy(per_pin->chmap, chmap, ARRAY_SIZE(per_pin->chmap));
	if (prepared)
		hdmi_setup_audio_infoframe(codec, per_pin, per_pin->non_pcm);
	mutex_unlock(&per_pin->lock);
}

static bool is_hdmi_pcm_attached(struct hdac_device *hdac, int pcm_idx)
{
	struct hda_codec *codec = hdac_to_hda_codec(hdac);
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = pcm_idx_to_pin(spec, pcm_idx);

	return per_pin ? true:false;
}

static int generic_hdmi_build_pcms(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int idx, pcm_num;

	/*
	 * for non-mst mode, pcm number is the same as before
	 * for DP MST mode without extra PCM, pcm number is same
	 * for DP MST mode with extra PCMs, pcm number is
	 *  (nid number + dev_num - 1)
	 * dev_num is the device entry number in a pin
	 */

	if (codec->mst_no_extra_pcms)
		pcm_num = spec->num_nids;
	else
		pcm_num = spec->num_nids + spec->dev_num - 1;

	codec_dbg(codec, "hdmi: pcm_num set to %d\n", pcm_num);

	for (idx = 0; idx < pcm_num; idx++) {
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
		/* pcm number is less than 16 */
		if (spec->pcm_used >= 16)
			break;
		/* other pstr fields are set in open */
	}

	return 0;
}

static void free_hdmi_jack_priv(struct snd_jack *jack)
{
	struct hdmi_pcm *pcm = jack->private_data;

	pcm->jack = NULL;
}

static int generic_hdmi_build_jack(struct hda_codec *codec, int pcm_idx)
{
	char hdmi_str[32] = "HDMI/DP";
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = get_pin(spec, pcm_idx);
	struct snd_jack *jack;
	int pcmdev = get_pcm_rec(spec, pcm_idx)->device;
	int err;

	if (pcmdev > 0)
		sprintf(hdmi_str + strlen(hdmi_str), ",pcm=%d", pcmdev);
	if (!spec->dyn_pcm_assign &&
	    !is_jack_detectable(codec, per_pin->pin_nid))
		strncat(hdmi_str, " Phantom",
			sizeof(hdmi_str) - strlen(hdmi_str) - 1);

	err = snd_jack_new(codec->card, hdmi_str, SND_JACK_AVOUT, &jack,
			   true, false);
	if (err < 0)
		return err;

	spec->pcm_rec[pcm_idx].jack = jack;
	jack->private_data = &spec->pcm_rec[pcm_idx];
	jack->private_free = free_hdmi_jack_priv;
	return 0;
}

static int generic_hdmi_build_controls(struct hda_codec *codec)
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
		if (spec->dyn_pcm_assign)
			err = snd_hda_create_dig_out_ctls(codec,
					  0, spec->cvt_nids[0],
					  HDA_PCM_TYPE_HDMI);
		else {
			struct hdmi_spec_per_pin *per_pin =
				get_pin(spec, pcm_idx);
			err = snd_hda_create_dig_out_ctls(codec,
						  per_pin->pin_nid,
						  per_pin->mux_nids[0],
						  HDA_PCM_TYPE_HDMI);
		}
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

static int generic_hdmi_init_per_pins(struct hda_codec *codec)
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

static int generic_hdmi_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	mutex_lock(&spec->bind_lock);
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
	mutex_unlock(&spec->bind_lock);
	return 0;
}

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

static void generic_spec_free(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;

	if (spec) {
		hdmi_array_free(spec);
		kfree(spec);
		codec->spec = NULL;
	}
	codec->dp_mst = false;
}

static void generic_hdmi_free(struct hda_codec *codec)
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
		if (spec->dyn_pcm_assign)
			snd_device_free(codec->card,
					spec->pcm_rec[pcm_idx].jack);
		else
			spec->pcm_rec[pcm_idx].jack = NULL;
	}

	generic_spec_free(codec);
}

#ifdef CONFIG_PM
static int generic_hdmi_suspend(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);
		cancel_delayed_work_sync(&per_pin->work);
	}
	return 0;
}

static int generic_hdmi_resume(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx;

	codec->patch_ops.init(codec);
	snd_hda_regmap_sync(codec);

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);
		hdmi_present_sense(per_pin, 1);
	}
	return 0;
}
#endif

static const struct hda_codec_ops generic_hdmi_patch_ops = {
	.init			= generic_hdmi_init,
	.free			= generic_hdmi_free,
	.build_pcms		= generic_hdmi_build_pcms,
	.build_controls		= generic_hdmi_build_controls,
	.unsol_event		= hdmi_unsol_event,
#ifdef CONFIG_PM
	.suspend		= generic_hdmi_suspend,
	.resume			= generic_hdmi_resume,
#endif
};

static const struct hdmi_ops generic_standard_hdmi_ops = {
	.pin_get_eld				= hdmi_pin_get_eld,
	.pin_setup_infoframe			= hdmi_pin_setup_infoframe,
	.pin_hbr_setup				= hdmi_pin_hbr_setup,
	.setup_stream				= hdmi_setup_stream,
};

/* allocate codec->spec and assign/initialize generic parser ops */
static int alloc_generic_hdmi(struct hda_codec *codec)
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

	codec->patch_ops = generic_hdmi_patch_ops;

	return 0;
}

/* generic HDMI parser */
static int patch_generic_hdmi(struct hda_codec *codec)
{
	int err;

	err = alloc_generic_hdmi(codec);
	if (err < 0)
		return err;

	err = hdmi_parse_codec(codec);
	if (err < 0) {
		generic_spec_free(codec);
		return err;
	}

	generic_hdmi_init_per_pins(codec);
	return 0;
}

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
	mutex_lock(&spec->bind_lock);
	spec->use_acomp_notifier = use_acomp;
	spec->codec->relaxed_resume = use_acomp;
	spec->codec->bus->keep_power = 0;
	/* reprogram each jack detection logic depending on the notifier */
	for (i = 0; i < spec->num_pins; i++)
		reprogram_jack_detect(spec->codec,
				      get_pin(spec, i)->pin_nid,
				      get_pin(spec, i)->dev_id,
				      use_acomp);
	mutex_unlock(&spec->bind_lock);
}

/* enable / disable the notifier via master bind / unbind */
static int generic_acomp_master_bind(struct device *dev,
				     struct drm_audio_component *acomp)
{
	generic_acomp_notifier_set(acomp, true);
	return 0;
}

static void generic_acomp_master_unbind(struct device *dev,
					struct drm_audio_component *acomp)
{
	generic_acomp_notifier_set(acomp, false);
}

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
static void generic_acomp_pin_eld_notify(void *audio_ptr, int port, int dev_id)
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
	/* ditto during suspend/resume process itself */
	if (snd_hdac_is_in_pm(&codec->core))
		return;

	check_presence_and_report(codec, pin_nid, dev_id);
}

/* set up the private drm_audio_ops from the template */
static void setup_drm_audio_ops(struct hda_codec *codec,
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

/* initialize the generic HDMI audio component */
static void generic_acomp_init(struct hda_codec *codec,
			       const struct drm_audio_component_audio_ops *ops,
			       int (*port2pin)(struct hda_codec *, int))
{
	struct hdmi_spec *spec = codec->spec;

	if (!enable_acomp) {
		codec_info(codec, "audio component disabled by module option\n");
		return;
	}

	spec->port2pin = port2pin;
	setup_drm_audio_ops(codec, ops);
	if (!snd_hdac_acomp_init(&codec->bus->core, &spec->drm_audio_ops,
				 match_bound_vga, 0)) {
		spec->acomp_registered = true;
	}
}

/*
 * Intel codec parsers and helpers
 */

#define INTEL_GET_VENDOR_VERB	0xf81
#define INTEL_SET_VENDOR_VERB	0x781
#define INTEL_EN_DP12		0x02	/* enable DP 1.2 features */
#define INTEL_EN_ALL_PIN_CVTS	0x01	/* enable 2nd & 3rd pins and convertors */

static void intel_haswell_enable_all_pins(struct hda_codec *codec,
					  bool update_tree)
{
	unsigned int vendor_param;
	struct hdmi_spec *spec = codec->spec;

	vendor_param = snd_hda_codec_read(codec, spec->vendor_nid, 0,
				INTEL_GET_VENDOR_VERB, 0);
	if (vendor_param == -1 || vendor_param & INTEL_EN_ALL_PIN_CVTS)
		return;

	vendor_param |= INTEL_EN_ALL_PIN_CVTS;
	vendor_param = snd_hda_codec_read(codec, spec->vendor_nid, 0,
				INTEL_SET_VENDOR_VERB, vendor_param);
	if (vendor_param == -1)
		return;

	if (update_tree)
		snd_hda_codec_update_widgets(codec);
}

static void intel_haswell_fixup_enable_dp12(struct hda_codec *codec)
{
	unsigned int vendor_param;
	struct hdmi_spec *spec = codec->spec;

	vendor_param = snd_hda_codec_read(codec, spec->vendor_nid, 0,
				INTEL_GET_VENDOR_VERB, 0);
	if (vendor_param == -1 || vendor_param & INTEL_EN_DP12)
		return;

	/* enable DP1.2 mode */
	vendor_param |= INTEL_EN_DP12;
	snd_hdac_regmap_add_vendor_verb(&codec->core, INTEL_SET_VENDOR_VERB);
	snd_hda_codec_write_cache(codec, spec->vendor_nid, 0,
				INTEL_SET_VENDOR_VERB, vendor_param);
}

/* Haswell needs to re-issue the vendor-specific verbs before turning to D0.
 * Otherwise you may get severe h/w communication errors.
 */
static void haswell_set_power_state(struct hda_codec *codec, hda_nid_t fg,
				unsigned int power_state)
{
	if (power_state == AC_PWRST_D0) {
		intel_haswell_enable_all_pins(codec, false);
		intel_haswell_fixup_enable_dp12(codec);
	}

	snd_hda_codec_read(codec, fg, 0, AC_VERB_SET_POWER_STATE, power_state);
	snd_hda_codec_set_power_to_all(codec, fg, power_state);
}

/* There is a fixed mapping between audio pin node and display port.
 * on SNB, IVY, HSW, BSW, SKL, BXT, KBL:
 * Pin Widget 5 - PORT B (port = 1 in i915 driver)
 * Pin Widget 6 - PORT C (port = 2 in i915 driver)
 * Pin Widget 7 - PORT D (port = 3 in i915 driver)
 *
 * on VLV, ILK:
 * Pin Widget 4 - PORT B (port = 1 in i915 driver)
 * Pin Widget 5 - PORT C (port = 2 in i915 driver)
 * Pin Widget 6 - PORT D (port = 3 in i915 driver)
 */
static int intel_base_nid(struct hda_codec *codec)
{
	switch (codec->core.vendor_id) {
	case 0x80860054: /* ILK */
	case 0x80862804: /* ILK */
	case 0x80862882: /* VLV */
		return 4;
	default:
		return 5;
	}
}

static int intel_pin2port(void *audio_ptr, int pin_nid)
{
	struct hda_codec *codec = audio_ptr;
	struct hdmi_spec *spec = codec->spec;
	int base_nid, i;

	if (!spec->port_num) {
		base_nid = intel_base_nid(codec);
		if (WARN_ON(pin_nid < base_nid || pin_nid >= base_nid + 3))
			return -1;
		return pin_nid - base_nid + 1;
	}

	/*
	 * looking for the pin number in the mapping table and return
	 * the index which indicate the port number
	 */
	for (i = 0; i < spec->port_num; i++) {
		if (pin_nid == spec->port_map[i])
			return i;
	}

	codec_info(codec, "Can't find the HDMI/DP port for pin %d\n", pin_nid);
	return -1;
}

static int intel_port2pin(struct hda_codec *codec, int port)
{
	struct hdmi_spec *spec = codec->spec;

	if (!spec->port_num) {
		/* we assume only from port-B to port-D */
		if (port < 1 || port > 3)
			return 0;
		return port + intel_base_nid(codec) - 1;
	}

	if (port < 0 || port >= spec->port_num)
		return 0;
	return spec->port_map[port];
}

static void intel_pin_eld_notify(void *audio_ptr, int port, int pipe)
{
	struct hda_codec *codec = audio_ptr;
	int pin_nid;
	int dev_id = pipe;

	pin_nid = intel_port2pin(codec, port);
	if (!pin_nid)
		return;
	/* skip notification during system suspend (but not in runtime PM);
	 * the state will be updated at resume
	 */
	if (codec->core.dev.power.power_state.event == PM_EVENT_SUSPEND)
		return;
	/* ditto during suspend/resume process itself */
	if (snd_hdac_is_in_pm(&codec->core))
		return;

	snd_hdac_i915_set_bclk(&codec->bus->core);
	check_presence_and_report(codec, pin_nid, dev_id);
}

static const struct drm_audio_component_audio_ops intel_audio_ops = {
	.pin2port = intel_pin2port,
	.pin_eld_notify = intel_pin_eld_notify,
};

/* register i915 component pin_eld_notify callback */
static void register_i915_notifier(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;

	spec->use_acomp_notifier = true;
	spec->port2pin = intel_port2pin;
	setup_drm_audio_ops(codec, &intel_audio_ops);
	snd_hdac_acomp_register_notifier(&codec->bus->core,
					&spec->drm_audio_ops);
	/* no need for forcible resume for jack check thanks to notifier */
	codec->relaxed_resume = 1;
}

/* setup_stream ops override for HSW+ */
static int i915_hsw_setup_stream(struct hda_codec *codec, hda_nid_t cvt_nid,
				 hda_nid_t pin_nid, int dev_id, u32 stream_tag,
				 int format)
{
	haswell_verify_D0(codec, cvt_nid, pin_nid);
	return hdmi_setup_stream(codec, cvt_nid, pin_nid, dev_id,
				 stream_tag, format);
}

/* pin_cvt_fixup ops override for HSW+ and VLV+ */
static void i915_pin_cvt_fixup(struct hda_codec *codec,
			       struct hdmi_spec_per_pin *per_pin,
			       hda_nid_t cvt_nid)
{
	if (per_pin) {
		haswell_verify_D0(codec, per_pin->cvt_nid, per_pin->pin_nid);
		snd_hda_set_dev_select(codec, per_pin->pin_nid,
			       per_pin->dev_id);
		intel_verify_pin_cvt_connect(codec, per_pin);
		intel_not_share_assigned_cvt(codec, per_pin->pin_nid,
				     per_pin->dev_id, per_pin->mux_idx);
	} else {
		intel_not_share_assigned_cvt_nid(codec, 0, 0, cvt_nid);
	}
}

/* precondition and allocation for Intel codecs */
static int alloc_intel_hdmi(struct hda_codec *codec)
{
	int err;

	/* requires i915 binding */
	if (!codec->bus->core.audio_component) {
		codec_info(codec, "No i915 binding for Intel HDMI/DP codec\n");
		/* set probe_id here to prevent generic fallback binding */
		codec->probe_id = HDA_CODEC_ID_SKIP_PROBE;
		return -ENODEV;
	}

	err = alloc_generic_hdmi(codec);
	if (err < 0)
		return err;
	/* no need to handle unsol events */
	codec->patch_ops.unsol_event = NULL;
	return 0;
}

/* parse and post-process for Intel codecs */
static int parse_intel_hdmi(struct hda_codec *codec)
{
	int err, retries = 3;

	do {
		err = hdmi_parse_codec(codec);
	} while (err < 0 && retries--);

	if (err < 0) {
		generic_spec_free(codec);
		return err;
	}

	generic_hdmi_init_per_pins(codec);
	register_i915_notifier(codec);
	return 0;
}

/* Intel Haswell and onwards; audio component with eld notifier */
static int intel_hsw_common_init(struct hda_codec *codec, hda_nid_t vendor_nid,
				 const int *port_map, int port_num)
{
	struct hdmi_spec *spec;
	int err;

	err = alloc_intel_hdmi(codec);
	if (err < 0)
		return err;
	spec = codec->spec;
	codec->dp_mst = true;
	spec->dyn_pcm_assign = true;
	spec->vendor_nid = vendor_nid;
	spec->port_map = port_map;
	spec->port_num = port_num;
	spec->intel_hsw_fixup = true;

	intel_haswell_enable_all_pins(codec, true);
	intel_haswell_fixup_enable_dp12(codec);

	codec->display_power_control = 1;

	codec->patch_ops.set_power_state = haswell_set_power_state;
	codec->depop_delay = 0;
	codec->auto_runtime_pm = 1;

	spec->ops.setup_stream = i915_hsw_setup_stream;
	spec->ops.pin_cvt_fixup = i915_pin_cvt_fixup;

	/*
	 * Enable silent stream feature, if it is enabled via
	 * module param or Kconfig option
	 */
	if (enable_silent_stream)
		spec->send_silent_stream = true;

	return parse_intel_hdmi(codec);
}

static int patch_i915_hsw_hdmi(struct hda_codec *codec)
{
	return intel_hsw_common_init(codec, 0x08, NULL, 0);
}

static int patch_i915_glk_hdmi(struct hda_codec *codec)
{
	return intel_hsw_common_init(codec, 0x0b, NULL, 0);
}

static int patch_i915_icl_hdmi(struct hda_codec *codec)
{
	/*
	 * pin to port mapping table where the value indicate the pin number and
	 * the index indicate the port number.
	 */
	static const int map[] = {0x0, 0x4, 0x6, 0x8, 0xa, 0xb};

	return intel_hsw_common_init(codec, 0x02, map, ARRAY_SIZE(map));
}

static int patch_i915_tgl_hdmi(struct hda_codec *codec)
{
	/*
	 * pin to port mapping table where the value indicate the pin number and
	 * the index indicate the port number.
	 */
	static const int map[] = {0x4, 0x6, 0x8, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
	int ret;

	ret = intel_hsw_common_init(codec, 0x02, map, ARRAY_SIZE(map));
	if (!ret) {
		struct hdmi_spec *spec = codec->spec;

		spec->dyn_pcm_no_legacy = true;
	}

	return ret;
}

/* Intel Baytrail and Braswell; with eld notifier */
static int patch_i915_byt_hdmi(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err;

	err = alloc_intel_hdmi(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	/* For Valleyview/Cherryview, only the display codec is in the display
	 * power well and can use link_power ops to request/release the power.
	 */
	codec->display_power_control = 1;

	codec->depop_delay = 0;
	codec->auto_runtime_pm = 1;

	spec->ops.pin_cvt_fixup = i915_pin_cvt_fixup;

	return parse_intel_hdmi(codec);
}

/* Intel IronLake, SandyBridge and IvyBridge; with eld notifier */
static int patch_i915_cpt_hdmi(struct hda_codec *codec)
{
	int err;

	err = alloc_intel_hdmi(codec);
	if (err < 0)
		return err;
	return parse_intel_hdmi(codec);
}

/*
 * Shared non-generic implementations
 */

static int simple_playback_build_pcms(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hda_pcm *info;
	unsigned int chans;
	struct hda_pcm_stream *pstr;
	struct hdmi_spec_per_cvt *per_cvt;

	per_cvt = get_cvt(spec, 0);
	chans = get_wcaps(codec, per_cvt->cvt_nid);
	chans = get_wcaps_channels(chans);

	info = snd_hda_codec_pcm_new(codec, "HDMI 0");
	if (!info)
		return -ENOMEM;
	spec->pcm_rec[0].pcm = info;
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	pstr = &info->stream[SNDRV_PCM_STREAM_PLAYBACK];
	*pstr = spec->pcm_playback;
	pstr->nid = per_cvt->cvt_nid;
	if (pstr->channels_max <= 2 && chans && chans <= 16)
		pstr->channels_max = chans;

	return 0;
}

/* unsolicited event for jack sensing */
static void simple_hdmi_unsol_event(struct hda_codec *codec,
				    unsigned int res)
{
	snd_hda_jack_set_dirty_all(codec);
	snd_hda_jack_report_sync(codec);
}

/* generic_hdmi_build_jack can be used for simple_hdmi, too,
 * as long as spec->pins[] is set correctly
 */
#define simple_hdmi_build_jack	generic_hdmi_build_jack

static int simple_playback_build_controls(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_cvt *per_cvt;
	int err;

	per_cvt = get_cvt(spec, 0);
	err = snd_hda_create_dig_out_ctls(codec, per_cvt->cvt_nid,
					  per_cvt->cvt_nid,
					  HDA_PCM_TYPE_HDMI);
	if (err < 0)
		return err;
	return simple_hdmi_build_jack(codec, 0);
}

static int simple_playback_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = get_pin(spec, 0);
	hda_nid_t pin = per_pin->pin_nid;

	snd_hda_codec_write(codec, pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
	/* some codecs require to unmute the pin */
	if (get_wcaps(codec, pin) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, pin, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_OUT_UNMUTE);
	snd_hda_jack_detect_enable(codec, pin, per_pin->dev_id);
	return 0;
}

static void simple_playback_free(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;

	hdmi_array_free(spec);
	kfree(spec);
}

/*
 * Nvidia specific implementations
 */

#define Nv_VERB_SET_Channel_Allocation          0xF79
#define Nv_VERB_SET_Info_Frame_Checksum         0xF7A
#define Nv_VERB_SET_Audio_Protection_On         0xF98
#define Nv_VERB_SET_Audio_Protection_Off        0xF99

#define nvhdmi_master_con_nid_7x	0x04
#define nvhdmi_master_pin_nid_7x	0x05

static const hda_nid_t nvhdmi_con_nids_7x[4] = {
	/*front, rear, clfe, rear_surr */
	0x6, 0x8, 0xa, 0xc,
};

static const struct hda_verb nvhdmi_basic_init_7x_2ch[] = {
	/* set audio protect on */
	{ 0x1, Nv_VERB_SET_Audio_Protection_On, 0x1},
	/* enable digital output on pin widget */
	{ 0x5, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{} /* terminator */
};

static const struct hda_verb nvhdmi_basic_init_7x_8ch[] = {
	/* set audio protect on */
	{ 0x1, Nv_VERB_SET_Audio_Protection_On, 0x1},
	/* enable digital output on pin widget */
	{ 0x5, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0x7, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0x9, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0xb, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{ 0xd, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x5 },
	{} /* terminator */
};

#ifdef LIMITED_RATE_FMT_SUPPORT
/* support only the safe format and rate */
#define SUPPORTED_RATES		SNDRV_PCM_RATE_48000
#define SUPPORTED_MAXBPS	16
#define SUPPORTED_FORMATS	SNDRV_PCM_FMTBIT_S16_LE
#else
/* support all rates and formats */
#define SUPPORTED_RATES \
	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |\
	SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |\
	 SNDRV_PCM_RATE_192000)
#define SUPPORTED_MAXBPS	24
#define SUPPORTED_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE)
#endif

static int nvhdmi_7x_init_2ch(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, nvhdmi_basic_init_7x_2ch);
	return 0;
}

static int nvhdmi_7x_init_8ch(struct hda_codec *codec)
{
	snd_hda_sequence_write(codec, nvhdmi_basic_init_7x_8ch);
	return 0;
}

static const unsigned int channels_2_6_8[] = {
	2, 6, 8
};

static const unsigned int channels_2_8[] = {
	2, 8
};

static const struct snd_pcm_hw_constraint_list hw_constraints_2_6_8_channels = {
	.count = ARRAY_SIZE(channels_2_6_8),
	.list = channels_2_6_8,
	.mask = 0,
};

static const struct snd_pcm_hw_constraint_list hw_constraints_2_8_channels = {
	.count = ARRAY_SIZE(channels_2_8),
	.list = channels_2_8,
	.mask = 0,
};

static int simple_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	const struct snd_pcm_hw_constraint_list *hw_constraints_channels = NULL;

	switch (codec->preset->vendor_id) {
	case 0x10de0002:
	case 0x10de0003:
	case 0x10de0005:
	case 0x10de0006:
		hw_constraints_channels = &hw_constraints_2_8_channels;
		break;
	case 0x10de0007:
		hw_constraints_channels = &hw_constraints_2_6_8_channels;
		break;
	default:
		break;
	}

	if (hw_constraints_channels != NULL) {
		snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				hw_constraints_channels);
	} else {
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	}

	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int simple_playback_pcm_close(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int simple_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

static const struct hda_pcm_stream simple_pcm_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.open = simple_playback_pcm_open,
		.close = simple_playback_pcm_close,
		.prepare = simple_playback_pcm_prepare
	},
};

static const struct hda_codec_ops simple_hdmi_patch_ops = {
	.build_controls = simple_playback_build_controls,
	.build_pcms = simple_playback_build_pcms,
	.init = simple_playback_init,
	.free = simple_playback_free,
	.unsol_event = simple_hdmi_unsol_event,
};

static int patch_simple_hdmi(struct hda_codec *codec,
			     hda_nid_t cvt_nid, hda_nid_t pin_nid)
{
	struct hdmi_spec *spec;
	struct hdmi_spec_per_cvt *per_cvt;
	struct hdmi_spec_per_pin *per_pin;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	spec->codec = codec;
	codec->spec = spec;
	hdmi_array_init(spec, 1);

	spec->multiout.num_dacs = 0;  /* no analog */
	spec->multiout.max_channels = 2;
	spec->multiout.dig_out_nid = cvt_nid;
	spec->num_cvts = 1;
	spec->num_pins = 1;
	per_pin = snd_array_new(&spec->pins);
	per_cvt = snd_array_new(&spec->cvts);
	if (!per_pin || !per_cvt) {
		simple_playback_free(codec);
		return -ENOMEM;
	}
	per_cvt->cvt_nid = cvt_nid;
	per_pin->pin_nid = pin_nid;
	spec->pcm_playback = simple_pcm_playback;

	codec->patch_ops = simple_hdmi_patch_ops;

	return 0;
}

static void nvhdmi_8ch_7x_set_info_frame_parameters(struct hda_codec *codec,
						    int channels)
{
	unsigned int chanmask;
	int chan = channels ? (channels - 1) : 1;

	switch (channels) {
	default:
	case 0:
	case 2:
		chanmask = 0x00;
		break;
	case 4:
		chanmask = 0x08;
		break;
	case 6:
		chanmask = 0x0b;
		break;
	case 8:
		chanmask = 0x13;
		break;
	}

	/* Set the audio infoframe channel allocation and checksum fields.  The
	 * channel count is computed implicitly by the hardware. */
	snd_hda_codec_write(codec, 0x1, 0,
			Nv_VERB_SET_Channel_Allocation, chanmask);

	snd_hda_codec_write(codec, 0x1, 0,
			Nv_VERB_SET_Info_Frame_Checksum,
			(0x71 - chan - chanmask));
}

static int nvhdmi_8ch_7x_pcm_close(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;
	int i;

	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x,
			0, AC_VERB_SET_CHANNEL_STREAMID, 0);
	for (i = 0; i < 4; i++) {
		/* set the stream id */
		snd_hda_codec_write(codec, nvhdmi_con_nids_7x[i], 0,
				AC_VERB_SET_CHANNEL_STREAMID, 0);
		/* set the stream format */
		snd_hda_codec_write(codec, nvhdmi_con_nids_7x[i], 0,
				AC_VERB_SET_STREAM_FORMAT, 0);
	}

	/* The audio hardware sends a channel count of 0x7 (8ch) when all the
	 * streams are disabled. */
	nvhdmi_8ch_7x_set_info_frame_parameters(codec, 8);

	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int nvhdmi_8ch_7x_pcm_prepare(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream)
{
	int chs;
	unsigned int dataDCC2, channel_id;
	int i;
	struct hdmi_spec *spec = codec->spec;
	struct hda_spdif_out *spdif;
	struct hdmi_spec_per_cvt *per_cvt;

	mutex_lock(&codec->spdif_mutex);
	per_cvt = get_cvt(spec, 0);
	spdif = snd_hda_spdif_out_of_nid(codec, per_cvt->cvt_nid);

	chs = substream->runtime->channels;

	dataDCC2 = 0x2;

	/* turn off SPDIF once; otherwise the IEC958 bits won't be updated */
	if (codec->spdif_status_reset && (spdif->ctls & AC_DIG1_ENABLE))
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				spdif->ctls & ~AC_DIG1_ENABLE & 0xff);

	/* set the stream id */
	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x, 0,
			AC_VERB_SET_CHANNEL_STREAMID, (stream_tag << 4) | 0x0);

	/* set the stream format */
	snd_hda_codec_write(codec, nvhdmi_master_con_nid_7x, 0,
			AC_VERB_SET_STREAM_FORMAT, format);

	/* turn on again (if needed) */
	/* enable and set the channel status audio/data flag */
	if (codec->spdif_status_reset && (spdif->ctls & AC_DIG1_ENABLE)) {
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				spdif->ctls & 0xff);
		snd_hda_codec_write(codec,
				nvhdmi_master_con_nid_7x,
				0,
				AC_VERB_SET_DIGI_CONVERT_2, dataDCC2);
	}

	for (i = 0; i < 4; i++) {
		if (chs == 2)
			channel_id = 0;
		else
			channel_id = i * 2;

		/* turn off SPDIF once;
		 *otherwise the IEC958 bits won't be updated
		 */
		if (codec->spdif_status_reset &&
		(spdif->ctls & AC_DIG1_ENABLE))
			snd_hda_codec_write(codec,
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_DIGI_CONVERT_1,
				spdif->ctls & ~AC_DIG1_ENABLE & 0xff);
		/* set the stream id */
		snd_hda_codec_write(codec,
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_CHANNEL_STREAMID,
				(stream_tag << 4) | channel_id);
		/* set the stream format */
		snd_hda_codec_write(codec,
				nvhdmi_con_nids_7x[i],
				0,
				AC_VERB_SET_STREAM_FORMAT,
				format);
		/* turn on again (if needed) */
		/* enable and set the channel status audio/data flag */
		if (codec->spdif_status_reset &&
		(spdif->ctls & AC_DIG1_ENABLE)) {
			snd_hda_codec_write(codec,
					nvhdmi_con_nids_7x[i],
					0,
					AC_VERB_SET_DIGI_CONVERT_1,
					spdif->ctls & 0xff);
			snd_hda_codec_write(codec,
					nvhdmi_con_nids_7x[i],
					0,
					AC_VERB_SET_DIGI_CONVERT_2, dataDCC2);
		}
	}

	nvhdmi_8ch_7x_set_info_frame_parameters(codec, chs);

	mutex_unlock(&codec->spdif_mutex);
	return 0;
}

static const struct hda_pcm_stream nvhdmi_pcm_playback_8ch_7x = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = nvhdmi_master_con_nid_7x,
	.rates = SUPPORTED_RATES,
	.maxbps = SUPPORTED_MAXBPS,
	.formats = SUPPORTED_FORMATS,
	.ops = {
		.open = simple_playback_pcm_open,
		.close = nvhdmi_8ch_7x_pcm_close,
		.prepare = nvhdmi_8ch_7x_pcm_prepare
	},
};

static int patch_nvhdmi_2ch(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err = patch_simple_hdmi(codec, nvhdmi_master_con_nid_7x,
				    nvhdmi_master_pin_nid_7x);
	if (err < 0)
		return err;

	codec->patch_ops.init = nvhdmi_7x_init_2ch;
	/* override the PCM rates, etc, as the codec doesn't give full list */
	spec = codec->spec;
	spec->pcm_playback.rates = SUPPORTED_RATES;
	spec->pcm_playback.maxbps = SUPPORTED_MAXBPS;
	spec->pcm_playback.formats = SUPPORTED_FORMATS;
	spec->nv_dp_workaround = true;
	return 0;
}

static int nvhdmi_7x_8ch_build_pcms(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int err = simple_playback_build_pcms(codec);
	if (!err) {
		struct hda_pcm *info = get_pcm_rec(spec, 0);
		info->own_chmap = true;
	}
	return err;
}

static int nvhdmi_7x_8ch_build_controls(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hda_pcm *info;
	struct snd_pcm_chmap *chmap;
	int err;

	err = simple_playback_build_controls(codec);
	if (err < 0)
		return err;

	/* add channel maps */
	info = get_pcm_rec(spec, 0);
	err = snd_pcm_add_chmap_ctls(info->pcm,
				     SNDRV_PCM_STREAM_PLAYBACK,
				     snd_pcm_alt_chmaps, 8, 0, &chmap);
	if (err < 0)
		return err;
	switch (codec->preset->vendor_id) {
	case 0x10de0002:
	case 0x10de0003:
	case 0x10de0005:
	case 0x10de0006:
		chmap->channel_mask = (1U << 2) | (1U << 8);
		break;
	case 0x10de0007:
		chmap->channel_mask = (1U << 2) | (1U << 6) | (1U << 8);
	}
	return 0;
}

static int patch_nvhdmi_8ch_7x(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err = patch_nvhdmi_2ch(codec);
	if (err < 0)
		return err;
	spec = codec->spec;
	spec->multiout.max_channels = 8;
	spec->pcm_playback = nvhdmi_pcm_playback_8ch_7x;
	codec->patch_ops.init = nvhdmi_7x_init_8ch;
	codec->patch_ops.build_pcms = nvhdmi_7x_8ch_build_pcms;
	codec->patch_ops.build_controls = nvhdmi_7x_8ch_build_controls;

	/* Initialize the audio infoframe channel mask and checksum to something
	 * valid */
	nvhdmi_8ch_7x_set_info_frame_parameters(codec, 8);

	return 0;
}

/*
 * NVIDIA codecs ignore ASP mapping for 2ch - confirmed on:
 * - 0x10de0015
 * - 0x10de0040
 */
static int nvhdmi_chmap_cea_alloc_validate_get_type(struct hdac_chmap *chmap,
		struct hdac_cea_channel_speaker_allocation *cap, int channels)
{
	if (cap->ca_index == 0x00 && channels == 2)
		return SNDRV_CTL_TLVT_CHMAP_FIXED;

	/* If the speaker allocation matches the channel count, it is OK. */
	if (cap->channels != channels)
		return -1;

	/* all channels are remappable freely */
	return SNDRV_CTL_TLVT_CHMAP_VAR;
}

static int nvhdmi_chmap_validate(struct hdac_chmap *chmap,
		int ca, int chs, unsigned char *map)
{
	if (ca == 0x00 && (map[0] != SNDRV_CHMAP_FL || map[1] != SNDRV_CHMAP_FR))
		return -EINVAL;

	return 0;
}

/* map from pin NID to port; port is 0-based */
/* for Nvidia: assume widget NID starting from 4, with step 1 (4, 5, 6, ...) */
static int nvhdmi_pin2port(void *audio_ptr, int pin_nid)
{
	return pin_nid - 4;
}

/* reverse-map from port to pin NID: see above */
static int nvhdmi_port2pin(struct hda_codec *codec, int port)
{
	return port + 4;
}

static const struct drm_audio_component_audio_ops nvhdmi_audio_ops = {
	.pin2port = nvhdmi_pin2port,
	.pin_eld_notify = generic_acomp_pin_eld_notify,
	.master_bind = generic_acomp_master_bind,
	.master_unbind = generic_acomp_master_unbind,
};

static int patch_nvhdmi(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err;

	err = alloc_generic_hdmi(codec);
	if (err < 0)
		return err;
	codec->dp_mst = true;

	spec = codec->spec;
	spec->dyn_pcm_assign = true;

	err = hdmi_parse_codec(codec);
	if (err < 0) {
		generic_spec_free(codec);
		return err;
	}

	generic_hdmi_init_per_pins(codec);

	spec->dyn_pin_out = true;

	spec->chmap.ops.chmap_cea_alloc_validate_get_type =
		nvhdmi_chmap_cea_alloc_validate_get_type;
	spec->chmap.ops.chmap_validate = nvhdmi_chmap_validate;
	spec->nv_dp_workaround = true;

	codec->link_down_at_suspend = 1;

	generic_acomp_init(codec, &nvhdmi_audio_ops, nvhdmi_port2pin);

	return 0;
}

static int patch_nvhdmi_legacy(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err;

	err = patch_generic_hdmi(codec);
	if (err)
		return err;

	spec = codec->spec;
	spec->dyn_pin_out = true;

	spec->chmap.ops.chmap_cea_alloc_validate_get_type =
		nvhdmi_chmap_cea_alloc_validate_get_type;
	spec->chmap.ops.chmap_validate = nvhdmi_chmap_validate;
	spec->nv_dp_workaround = true;

	codec->link_down_at_suspend = 1;

	return 0;
}

/*
 * The HDA codec on NVIDIA Tegra contains two scratch registers that are
 * accessed using vendor-defined verbs. These registers can be used for
 * interoperability between the HDA and HDMI drivers.
 */

/* Audio Function Group node */
#define NVIDIA_AFG_NID 0x01

/*
 * The SCRATCH0 register is used to notify the HDMI codec of changes in audio
 * format. On Tegra, bit 31 is used as a trigger that causes an interrupt to
 * be raised in the HDMI codec. The remainder of the bits is arbitrary. This
 * implementation stores the HDA format (see AC_FMT_*) in bits [15:0] and an
 * additional bit (at position 30) to signal the validity of the format.
 *
 * | 31      | 30    | 29  16 | 15   0 |
 * +---------+-------+--------+--------+
 * | TRIGGER | VALID | UNUSED | FORMAT |
 * +-----------------------------------|
 *
 * Note that for the trigger bit to take effect it needs to change value
 * (i.e. it needs to be toggled).
 */
#define NVIDIA_GET_SCRATCH0		0xfa6
#define NVIDIA_SET_SCRATCH0_BYTE0	0xfa7
#define NVIDIA_SET_SCRATCH0_BYTE1	0xfa8
#define NVIDIA_SET_SCRATCH0_BYTE2	0xfa9
#define NVIDIA_SET_SCRATCH0_BYTE3	0xfaa
#define NVIDIA_SCRATCH_TRIGGER (1 << 7)
#define NVIDIA_SCRATCH_VALID   (1 << 6)

#define NVIDIA_GET_SCRATCH1		0xfab
#define NVIDIA_SET_SCRATCH1_BYTE0	0xfac
#define NVIDIA_SET_SCRATCH1_BYTE1	0xfad
#define NVIDIA_SET_SCRATCH1_BYTE2	0xfae
#define NVIDIA_SET_SCRATCH1_BYTE3	0xfaf

/*
 * The format parameter is the HDA audio format (see AC_FMT_*). If set to 0,
 * the format is invalidated so that the HDMI codec can be disabled.
 */
static void tegra_hdmi_set_format(struct hda_codec *codec, unsigned int format)
{
	unsigned int value;

	/* bits [31:30] contain the trigger and valid bits */
	value = snd_hda_codec_read(codec, NVIDIA_AFG_NID, 0,
				   NVIDIA_GET_SCRATCH0, 0);
	value = (value >> 24) & 0xff;

	/* bits [15:0] are used to store the HDA format */
	snd_hda_codec_write(codec, NVIDIA_AFG_NID, 0,
			    NVIDIA_SET_SCRATCH0_BYTE0,
			    (format >> 0) & 0xff);
	snd_hda_codec_write(codec, NVIDIA_AFG_NID, 0,
			    NVIDIA_SET_SCRATCH0_BYTE1,
			    (format >> 8) & 0xff);

	/* bits [16:24] are unused */
	snd_hda_codec_write(codec, NVIDIA_AFG_NID, 0,
			    NVIDIA_SET_SCRATCH0_BYTE2, 0);

	/*
	 * Bit 30 signals that the data is valid and hence that HDMI audio can
	 * be enabled.
	 */
	if (format == 0)
		value &= ~NVIDIA_SCRATCH_VALID;
	else
		value |= NVIDIA_SCRATCH_VALID;

	/*
	 * Whenever the trigger bit is toggled, an interrupt is raised in the
	 * HDMI codec. The HDMI driver will use that as trigger to update its
	 * configuration.
	 */
	value ^= NVIDIA_SCRATCH_TRIGGER;

	snd_hda_codec_write(codec, NVIDIA_AFG_NID, 0,
			    NVIDIA_SET_SCRATCH0_BYTE3, value);
}

static int tegra_hdmi_pcm_prepare(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  unsigned int stream_tag,
				  unsigned int format,
				  struct snd_pcm_substream *substream)
{
	int err;

	err = generic_hdmi_playback_pcm_prepare(hinfo, codec, stream_tag,
						format, substream);
	if (err < 0)
		return err;

	/* notify the HDMI codec of the format change */
	tegra_hdmi_set_format(codec, format);

	return 0;
}

static int tegra_hdmi_pcm_cleanup(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream)
{
	/* invalidate the format in the HDMI codec */
	tegra_hdmi_set_format(codec, 0);

	return generic_hdmi_playback_pcm_cleanup(hinfo, codec, substream);
}

static struct hda_pcm *hda_find_pcm_by_type(struct hda_codec *codec, int type)
{
	struct hdmi_spec *spec = codec->spec;
	unsigned int i;

	for (i = 0; i < spec->num_pins; i++) {
		struct hda_pcm *pcm = get_pcm_rec(spec, i);

		if (pcm->pcm_type == type)
			return pcm;
	}

	return NULL;
}

static int tegra_hdmi_build_pcms(struct hda_codec *codec)
{
	struct hda_pcm_stream *stream;
	struct hda_pcm *pcm;
	int err;

	err = generic_hdmi_build_pcms(codec);
	if (err < 0)
		return err;

	pcm = hda_find_pcm_by_type(codec, HDA_PCM_TYPE_HDMI);
	if (!pcm)
		return -ENODEV;

	/*
	 * Override ->prepare() and ->cleanup() operations to notify the HDMI
	 * codec about format changes.
	 */
	stream = &pcm->stream[SNDRV_PCM_STREAM_PLAYBACK];
	stream->ops.prepare = tegra_hdmi_pcm_prepare;
	stream->ops.cleanup = tegra_hdmi_pcm_cleanup;

	return 0;
}

static int patch_tegra_hdmi(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	int err;

	err = patch_generic_hdmi(codec);
	if (err)
		return err;

	codec->depop_delay = 10;
	codec->patch_ops.build_pcms = tegra_hdmi_build_pcms;
	spec = codec->spec;
	spec->chmap.ops.chmap_cea_alloc_validate_get_type =
		nvhdmi_chmap_cea_alloc_validate_get_type;
	spec->chmap.ops.chmap_validate = nvhdmi_chmap_validate;
	spec->nv_dp_workaround = true;

	return 0;
}

/*
 * ATI/AMD-specific implementations
 */

#define is_amdhdmi_rev3_or_later(codec) \
	((codec)->core.vendor_id == 0x1002aa01 && \
	 ((codec)->core.revision_id & 0xff00) >= 0x0300)
#define has_amd_full_remap_support(codec) is_amdhdmi_rev3_or_later(codec)

/* ATI/AMD specific HDA pin verbs, see the AMD HDA Verbs specification */
#define ATI_VERB_SET_CHANNEL_ALLOCATION	0x771
#define ATI_VERB_SET_DOWNMIX_INFO	0x772
#define ATI_VERB_SET_MULTICHANNEL_01	0x777
#define ATI_VERB_SET_MULTICHANNEL_23	0x778
#define ATI_VERB_SET_MULTICHANNEL_45	0x779
#define ATI_VERB_SET_MULTICHANNEL_67	0x77a
#define ATI_VERB_SET_HBR_CONTROL	0x77c
#define ATI_VERB_SET_MULTICHANNEL_1	0x785
#define ATI_VERB_SET_MULTICHANNEL_3	0x786
#define ATI_VERB_SET_MULTICHANNEL_5	0x787
#define ATI_VERB_SET_MULTICHANNEL_7	0x788
#define ATI_VERB_SET_MULTICHANNEL_MODE	0x789
#define ATI_VERB_GET_CHANNEL_ALLOCATION	0xf71
#define ATI_VERB_GET_DOWNMIX_INFO	0xf72
#define ATI_VERB_GET_MULTICHANNEL_01	0xf77
#define ATI_VERB_GET_MULTICHANNEL_23	0xf78
#define ATI_VERB_GET_MULTICHANNEL_45	0xf79
#define ATI_VERB_GET_MULTICHANNEL_67	0xf7a
#define ATI_VERB_GET_HBR_CONTROL	0xf7c
#define ATI_VERB_GET_MULTICHANNEL_1	0xf85
#define ATI_VERB_GET_MULTICHANNEL_3	0xf86
#define ATI_VERB_GET_MULTICHANNEL_5	0xf87
#define ATI_VERB_GET_MULTICHANNEL_7	0xf88
#define ATI_VERB_GET_MULTICHANNEL_MODE	0xf89

/* AMD specific HDA cvt verbs */
#define ATI_VERB_SET_RAMP_RATE		0x770
#define ATI_VERB_GET_RAMP_RATE		0xf70

#define ATI_OUT_ENABLE 0x1

#define ATI_MULTICHANNEL_MODE_PAIRED	0
#define ATI_MULTICHANNEL_MODE_SINGLE	1

#define ATI_HBR_CAPABLE 0x01
#define ATI_HBR_ENABLE 0x10

static int atihdmi_pin_get_eld(struct hda_codec *codec, hda_nid_t nid,
			       int dev_id, unsigned char *buf, int *eld_size)
{
	WARN_ON(dev_id != 0);
	/* call hda_eld.c ATI/AMD-specific function */
	return snd_hdmi_get_eld_ati(codec, nid, buf, eld_size,
				    is_amdhdmi_rev3_or_later(codec));
}

static void atihdmi_pin_setup_infoframe(struct hda_codec *codec,
					hda_nid_t pin_nid, int dev_id, int ca,
					int active_channels, int conn_type)
{
	WARN_ON(dev_id != 0);
	snd_hda_codec_write(codec, pin_nid, 0, ATI_VERB_SET_CHANNEL_ALLOCATION, ca);
}

static int atihdmi_paired_swap_fc_lfe(int pos)
{
	/*
	 * ATI/AMD have automatic FC/LFE swap built-in
	 * when in pairwise mapping mode.
	 */

	switch (pos) {
		/* see channel_allocations[].speakers[] */
		case 2: return 3;
		case 3: return 2;
		default: break;
	}

	return pos;
}

static int atihdmi_paired_chmap_validate(struct hdac_chmap *chmap,
			int ca, int chs, unsigned char *map)
{
	struct hdac_cea_channel_speaker_allocation *cap;
	int i, j;

	/* check that only channel pairs need to be remapped on old pre-rev3 ATI/AMD */

	cap = snd_hdac_get_ch_alloc_from_ca(ca);
	for (i = 0; i < chs; ++i) {
		int mask = snd_hdac_chmap_to_spk_mask(map[i]);
		bool ok = false;
		bool companion_ok = false;

		if (!mask)
			continue;

		for (j = 0 + i % 2; j < 8; j += 2) {
			int chan_idx = 7 - atihdmi_paired_swap_fc_lfe(j);
			if (cap->speakers[chan_idx] == mask) {
				/* channel is in a supported position */
				ok = true;

				if (i % 2 == 0 && i + 1 < chs) {
					/* even channel, check the odd companion */
					int comp_chan_idx = 7 - atihdmi_paired_swap_fc_lfe(j + 1);
					int comp_mask_req = snd_hdac_chmap_to_spk_mask(map[i+1]);
					int comp_mask_act = cap->speakers[comp_chan_idx];

					if (comp_mask_req == comp_mask_act)
						companion_ok = true;
					else
						return -EINVAL;
				}
				break;
			}
		}

		if (!ok)
			return -EINVAL;

		if (companion_ok)
			i++; /* companion channel already checked */
	}

	return 0;
}

static int atihdmi_pin_set_slot_channel(struct hdac_device *hdac,
		hda_nid_t pin_nid, int hdmi_slot, int stream_channel)
{
	struct hda_codec *codec = hdac_to_hda_codec(hdac);
	int verb;
	int ati_channel_setup = 0;

	if (hdmi_slot > 7)
		return -EINVAL;

	if (!has_amd_full_remap_support(codec)) {
		hdmi_slot = atihdmi_paired_swap_fc_lfe(hdmi_slot);

		/* In case this is an odd slot but without stream channel, do not
		 * disable the slot since the corresponding even slot could have a
		 * channel. In case neither have a channel, the slot pair will be
		 * disabled when this function is called for the even slot. */
		if (hdmi_slot % 2 != 0 && stream_channel == 0xf)
			return 0;

		hdmi_slot -= hdmi_slot % 2;

		if (stream_channel != 0xf)
			stream_channel -= stream_channel % 2;
	}

	verb = ATI_VERB_SET_MULTICHANNEL_01 + hdmi_slot/2 + (hdmi_slot % 2) * 0x00e;

	/* ati_channel_setup format: [7..4] = stream_channel_id, [1] = mute, [0] = enable */

	if (stream_channel != 0xf)
		ati_channel_setup = (stream_channel << 4) | ATI_OUT_ENABLE;

	return snd_hda_codec_write(codec, pin_nid, 0, verb, ati_channel_setup);
}

static int atihdmi_pin_get_slot_channel(struct hdac_device *hdac,
				hda_nid_t pin_nid, int asp_slot)
{
	struct hda_codec *codec = hdac_to_hda_codec(hdac);
	bool was_odd = false;
	int ati_asp_slot = asp_slot;
	int verb;
	int ati_channel_setup;

	if (asp_slot > 7)
		return -EINVAL;

	if (!has_amd_full_remap_support(codec)) {
		ati_asp_slot = atihdmi_paired_swap_fc_lfe(asp_slot);
		if (ati_asp_slot % 2 != 0) {
			ati_asp_slot -= 1;
			was_odd = true;
		}
	}

	verb = ATI_VERB_GET_MULTICHANNEL_01 + ati_asp_slot/2 + (ati_asp_slot % 2) * 0x00e;

	ati_channel_setup = snd_hda_codec_read(codec, pin_nid, 0, verb, 0);

	if (!(ati_channel_setup & ATI_OUT_ENABLE))
		return 0xf;

	return ((ati_channel_setup & 0xf0) >> 4) + !!was_odd;
}

static int atihdmi_paired_chmap_cea_alloc_validate_get_type(
		struct hdac_chmap *chmap,
		struct hdac_cea_channel_speaker_allocation *cap,
		int channels)
{
	int c;

	/*
	 * Pre-rev3 ATI/AMD codecs operate in a paired channel mode, so
	 * we need to take that into account (a single channel may take 2
	 * channel slots if we need to carry a silent channel next to it).
	 * On Rev3+ AMD codecs this function is not used.
	 */
	int chanpairs = 0;

	/* We only produce even-numbered channel count TLVs */
	if ((channels % 2) != 0)
		return -1;

	for (c = 0; c < 7; c += 2) {
		if (cap->speakers[c] || cap->speakers[c+1])
			chanpairs++;
	}

	if (chanpairs * 2 != channels)
		return -1;

	return SNDRV_CTL_TLVT_CHMAP_PAIRED;
}

static void atihdmi_paired_cea_alloc_to_tlv_chmap(struct hdac_chmap *hchmap,
		struct hdac_cea_channel_speaker_allocation *cap,
		unsigned int *chmap, int channels)
{
	/* produce paired maps for pre-rev3 ATI/AMD codecs */
	int count = 0;
	int c;

	for (c = 7; c >= 0; c--) {
		int chan = 7 - atihdmi_paired_swap_fc_lfe(7 - c);
		int spk = cap->speakers[chan];
		if (!spk) {
			/* add N/A channel if the companion channel is occupied */
			if (cap->speakers[chan + (chan % 2 ? -1 : 1)])
				chmap[count++] = SNDRV_CHMAP_NA;

			continue;
		}

		chmap[count++] = snd_hdac_spk_to_chmap(spk);
	}

	WARN_ON(count != channels);
}

static int atihdmi_pin_hbr_setup(struct hda_codec *codec, hda_nid_t pin_nid,
				 int dev_id, bool hbr)
{
	int hbr_ctl, hbr_ctl_new;

	WARN_ON(dev_id != 0);

	hbr_ctl = snd_hda_codec_read(codec, pin_nid, 0, ATI_VERB_GET_HBR_CONTROL, 0);
	if (hbr_ctl >= 0 && (hbr_ctl & ATI_HBR_CAPABLE)) {
		if (hbr)
			hbr_ctl_new = hbr_ctl | ATI_HBR_ENABLE;
		else
			hbr_ctl_new = hbr_ctl & ~ATI_HBR_ENABLE;

		codec_dbg(codec,
			  "atihdmi_pin_hbr_setup: NID=0x%x, %shbr-ctl=0x%x\n",
				pin_nid,
				hbr_ctl == hbr_ctl_new ? "" : "new-",
				hbr_ctl_new);

		if (hbr_ctl != hbr_ctl_new)
			snd_hda_codec_write(codec, pin_nid, 0,
						ATI_VERB_SET_HBR_CONTROL,
						hbr_ctl_new);

	} else if (hbr)
		return -EINVAL;

	return 0;
}

static int atihdmi_setup_stream(struct hda_codec *codec, hda_nid_t cvt_nid,
				hda_nid_t pin_nid, int dev_id,
				u32 stream_tag, int format)
{
	if (is_amdhdmi_rev3_or_later(codec)) {
		int ramp_rate = 180; /* default as per AMD spec */
		/* disable ramp-up/down for non-pcm as per AMD spec */
		if (format & AC_FMT_TYPE_NON_PCM)
			ramp_rate = 0;

		snd_hda_codec_write(codec, cvt_nid, 0, ATI_VERB_SET_RAMP_RATE, ramp_rate);
	}

	return hdmi_setup_stream(codec, cvt_nid, pin_nid, dev_id,
				 stream_tag, format);
}


static int atihdmi_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	int pin_idx, err;

	err = generic_hdmi_init(codec);

	if (err)
		return err;

	for (pin_idx = 0; pin_idx < spec->num_pins; pin_idx++) {
		struct hdmi_spec_per_pin *per_pin = get_pin(spec, pin_idx);

		/* make sure downmix information in infoframe is zero */
		snd_hda_codec_write(codec, per_pin->pin_nid, 0, ATI_VERB_SET_DOWNMIX_INFO, 0);

		/* enable channel-wise remap mode if supported */
		if (has_amd_full_remap_support(codec))
			snd_hda_codec_write(codec, per_pin->pin_nid, 0,
					    ATI_VERB_SET_MULTICHANNEL_MODE,
					    ATI_MULTICHANNEL_MODE_SINGLE);
	}
	codec->auto_runtime_pm = 1;

	return 0;
}

/* map from pin NID to port; port is 0-based */
/* for AMD: assume widget NID starting from 3, with step 2 (3, 5, 7, ...) */
static int atihdmi_pin2port(void *audio_ptr, int pin_nid)
{
	return pin_nid / 2 - 1;
}

/* reverse-map from port to pin NID: see above */
static int atihdmi_port2pin(struct hda_codec *codec, int port)
{
	return port * 2 + 3;
}

static const struct drm_audio_component_audio_ops atihdmi_audio_ops = {
	.pin2port = atihdmi_pin2port,
	.pin_eld_notify = generic_acomp_pin_eld_notify,
	.master_bind = generic_acomp_master_bind,
	.master_unbind = generic_acomp_master_unbind,
};

static int patch_atihdmi(struct hda_codec *codec)
{
	struct hdmi_spec *spec;
	struct hdmi_spec_per_cvt *per_cvt;
	int err, cvt_idx;

	err = patch_generic_hdmi(codec);

	if (err)
		return err;

	codec->patch_ops.init = atihdmi_init;

	spec = codec->spec;

	spec->ops.pin_get_eld = atihdmi_pin_get_eld;
	spec->ops.pin_setup_infoframe = atihdmi_pin_setup_infoframe;
	spec->ops.pin_hbr_setup = atihdmi_pin_hbr_setup;
	spec->ops.setup_stream = atihdmi_setup_stream;

	spec->chmap.ops.pin_get_slot_channel = atihdmi_pin_get_slot_channel;
	spec->chmap.ops.pin_set_slot_channel = atihdmi_pin_set_slot_channel;

	if (!has_amd_full_remap_support(codec)) {
		/* override to ATI/AMD-specific versions with pairwise mapping */
		spec->chmap.ops.chmap_cea_alloc_validate_get_type =
			atihdmi_paired_chmap_cea_alloc_validate_get_type;
		spec->chmap.ops.cea_alloc_to_tlv_chmap =
				atihdmi_paired_cea_alloc_to_tlv_chmap;
		spec->chmap.ops.chmap_validate = atihdmi_paired_chmap_validate;
	}

	/* ATI/AMD converters do not advertise all of their capabilities */
	for (cvt_idx = 0; cvt_idx < spec->num_cvts; cvt_idx++) {
		per_cvt = get_cvt(spec, cvt_idx);
		per_cvt->channels_max = max(per_cvt->channels_max, 8u);
		per_cvt->rates |= SUPPORTED_RATES;
		per_cvt->formats |= SUPPORTED_FORMATS;
		per_cvt->maxbps = max(per_cvt->maxbps, 24u);
	}

	spec->chmap.channels_max = max(spec->chmap.channels_max, 8u);

	/* AMD GPUs have neither EPSS nor CLKSTOP bits, hence preventing
	 * the link-down as is.  Tell the core to allow it.
	 */
	codec->link_down_at_suspend = 1;

	generic_acomp_init(codec, &atihdmi_audio_ops, atihdmi_port2pin);

	return 0;
}

/* VIA HDMI Implementation */
#define VIAHDMI_CVT_NID	0x02	/* audio converter1 */
#define VIAHDMI_PIN_NID	0x03	/* HDMI output pin1 */

static int patch_via_hdmi(struct hda_codec *codec)
{
	return patch_simple_hdmi(codec, VIAHDMI_CVT_NID, VIAHDMI_PIN_NID);
}

/*
 * patch entries
 */
static const struct hda_device_id snd_hda_id_hdmi[] = {
HDA_CODEC_ENTRY(0x1002793c, "RS600 HDMI",	patch_atihdmi),
HDA_CODEC_ENTRY(0x10027919, "RS600 HDMI",	patch_atihdmi),
HDA_CODEC_ENTRY(0x1002791a, "RS690/780 HDMI",	patch_atihdmi),
HDA_CODEC_ENTRY(0x1002aa01, "R6xx HDMI",	patch_atihdmi),
HDA_CODEC_ENTRY(0x10951390, "SiI1390 HDMI",	patch_generic_hdmi),
HDA_CODEC_ENTRY(0x10951392, "SiI1392 HDMI",	patch_generic_hdmi),
HDA_CODEC_ENTRY(0x17e80047, "Chrontel HDMI",	patch_generic_hdmi),
HDA_CODEC_ENTRY(0x10de0001, "MCP73 HDMI",	patch_nvhdmi_2ch),
HDA_CODEC_ENTRY(0x10de0002, "MCP77/78 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0003, "MCP77/78 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0004, "GPU 04 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0005, "MCP77/78 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0006, "MCP77/78 HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0007, "MCP79/7A HDMI",	patch_nvhdmi_8ch_7x),
HDA_CODEC_ENTRY(0x10de0008, "GPU 08 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0009, "GPU 09 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de000a, "GPU 0a HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de000b, "GPU 0b HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de000c, "MCP89 HDMI",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de000d, "GPU 0d HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0010, "GPU 10 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0011, "GPU 11 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0012, "GPU 12 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0013, "GPU 13 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0014, "GPU 14 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0015, "GPU 15 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0016, "GPU 16 HDMI/DP",	patch_nvhdmi_legacy),
/* 17 is known to be absent */
HDA_CODEC_ENTRY(0x10de0018, "GPU 18 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0019, "GPU 19 HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de001a, "GPU 1a HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de001b, "GPU 1b HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de001c, "GPU 1c HDMI/DP",	patch_nvhdmi_legacy),
HDA_CODEC_ENTRY(0x10de0020, "Tegra30 HDMI",	patch_tegra_hdmi),
HDA_CODEC_ENTRY(0x10de0022, "Tegra114 HDMI",	patch_tegra_hdmi),
HDA_CODEC_ENTRY(0x10de0028, "Tegra124 HDMI",	patch_tegra_hdmi),
HDA_CODEC_ENTRY(0x10de0029, "Tegra210 HDMI/DP",	patch_tegra_hdmi),
HDA_CODEC_ENTRY(0x10de002d, "Tegra186 HDMI/DP0", patch_tegra_hdmi),
HDA_CODEC_ENTRY(0x10de002e, "Tegra186 HDMI/DP1", patch_tegra_hdmi),
HDA_CODEC_ENTRY(0x10de002f, "Tegra194 HDMI/DP2", patch_tegra_hdmi),
HDA_CODEC_ENTRY(0x10de0030, "Tegra194 HDMI/DP3", patch_tegra_hdmi),
HDA_CODEC_ENTRY(0x10de0040, "GPU 40 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0041, "GPU 41 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0042, "GPU 42 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0043, "GPU 43 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0044, "GPU 44 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0045, "GPU 45 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0050, "GPU 50 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0051, "GPU 51 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0052, "GPU 52 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0060, "GPU 60 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0061, "GPU 61 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0062, "GPU 62 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0067, "MCP67 HDMI",	patch_nvhdmi_2ch),
HDA_CODEC_ENTRY(0x10de0070, "GPU 70 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0071, "GPU 71 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0072, "GPU 72 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0073, "GPU 73 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0074, "GPU 74 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0076, "GPU 76 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de007b, "GPU 7b HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de007c, "GPU 7c HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de007d, "GPU 7d HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de007e, "GPU 7e HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0080, "GPU 80 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0081, "GPU 81 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0082, "GPU 82 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0083, "GPU 83 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0084, "GPU 84 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0090, "GPU 90 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0091, "GPU 91 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0092, "GPU 92 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0093, "GPU 93 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0094, "GPU 94 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0095, "GPU 95 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0097, "GPU 97 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0098, "GPU 98 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de0099, "GPU 99 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de009a, "GPU 9a HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de009d, "GPU 9d HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de009e, "GPU 9e HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de009f, "GPU 9f HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de00a0, "GPU a0 HDMI/DP",	patch_nvhdmi),
HDA_CODEC_ENTRY(0x10de8001, "MCP73 HDMI",	patch_nvhdmi_2ch),
HDA_CODEC_ENTRY(0x10de8067, "MCP67/68 HDMI",	patch_nvhdmi_2ch),
HDA_CODEC_ENTRY(0x11069f80, "VX900 HDMI/DP",	patch_via_hdmi),
HDA_CODEC_ENTRY(0x11069f81, "VX900 HDMI/DP",	patch_via_hdmi),
HDA_CODEC_ENTRY(0x11069f84, "VX11 HDMI/DP",	patch_generic_hdmi),
HDA_CODEC_ENTRY(0x11069f85, "VX11 HDMI/DP",	patch_generic_hdmi),
HDA_CODEC_ENTRY(0x80860054, "IbexPeak HDMI",	patch_i915_cpt_hdmi),
HDA_CODEC_ENTRY(0x80862800, "Geminilake HDMI",	patch_i915_glk_hdmi),
HDA_CODEC_ENTRY(0x80862801, "Bearlake HDMI",	patch_generic_hdmi),
HDA_CODEC_ENTRY(0x80862802, "Cantiga HDMI",	patch_generic_hdmi),
HDA_CODEC_ENTRY(0x80862803, "Eaglelake HDMI",	patch_generic_hdmi),
HDA_CODEC_ENTRY(0x80862804, "IbexPeak HDMI",	patch_i915_cpt_hdmi),
HDA_CODEC_ENTRY(0x80862805, "CougarPoint HDMI",	patch_i915_cpt_hdmi),
HDA_CODEC_ENTRY(0x80862806, "PantherPoint HDMI", patch_i915_cpt_hdmi),
HDA_CODEC_ENTRY(0x80862807, "Haswell HDMI",	patch_i915_hsw_hdmi),
HDA_CODEC_ENTRY(0x80862808, "Broadwell HDMI",	patch_i915_hsw_hdmi),
HDA_CODEC_ENTRY(0x80862809, "Skylake HDMI",	patch_i915_hsw_hdmi),
HDA_CODEC_ENTRY(0x8086280a, "Broxton HDMI",	patch_i915_hsw_hdmi),
HDA_CODEC_ENTRY(0x8086280b, "Kabylake HDMI",	patch_i915_hsw_hdmi),
HDA_CODEC_ENTRY(0x8086280c, "Cannonlake HDMI",	patch_i915_glk_hdmi),
HDA_CODEC_ENTRY(0x8086280d, "Geminilake HDMI",	patch_i915_glk_hdmi),
HDA_CODEC_ENTRY(0x8086280f, "Icelake HDMI",	patch_i915_icl_hdmi),
HDA_CODEC_ENTRY(0x80862812, "Tigerlake HDMI",	patch_i915_tgl_hdmi),
HDA_CODEC_ENTRY(0x80862814, "DG1 HDMI",	patch_i915_tgl_hdmi),
HDA_CODEC_ENTRY(0x80862815, "Alderlake HDMI",	patch_i915_tgl_hdmi),
HDA_CODEC_ENTRY(0x80862816, "Rocketlake HDMI",	patch_i915_tgl_hdmi),
HDA_CODEC_ENTRY(0x80862819, "DG2 HDMI",	patch_i915_tgl_hdmi),
HDA_CODEC_ENTRY(0x8086281a, "Jasperlake HDMI",	patch_i915_icl_hdmi),
HDA_CODEC_ENTRY(0x8086281b, "Elkhartlake HDMI",	patch_i915_icl_hdmi),
HDA_CODEC_ENTRY(0x8086281c, "Alderlake-P HDMI", patch_i915_tgl_hdmi),
HDA_CODEC_ENTRY(0x80862880, "CedarTrail HDMI",	patch_generic_hdmi),
HDA_CODEC_ENTRY(0x80862882, "Valleyview2 HDMI",	patch_i915_byt_hdmi),
HDA_CODEC_ENTRY(0x80862883, "Braswell HDMI",	patch_i915_byt_hdmi),
HDA_CODEC_ENTRY(0x808629fb, "Crestline HDMI",	patch_generic_hdmi),
/* special ID for generic HDMI */
HDA_CODEC_ENTRY(HDA_CODEC_ID_GENERIC_HDMI, "Generic HDMI", patch_generic_hdmi),
{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_hdmi);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HDMI HD-audio codec");
MODULE_ALIAS("snd-hda-codec-intelhdmi");
MODULE_ALIAS("snd-hda-codec-nvhdmi");
MODULE_ALIAS("snd-hda-codec-atihdmi");

static struct hda_codec_driver hdmi_driver = {
	.id = snd_hda_id_hdmi,
};

module_hda_codec_driver(hdmi_driver);
