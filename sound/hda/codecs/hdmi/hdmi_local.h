// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HD-audio HDMI codec driver
 */

#ifndef __HDA_HDMI_LOCAL_H
#define __HDA_HDMI_LOCAL_H

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/hdaudio.h>
#include <sound/hda_i915.h>
#include <sound/hda_chmap.h>
#include <sound/hda_codec.h>
#include "hda_local.h"

struct hdmi_spec_per_cvt {
	hda_nid_t cvt_nid;
	bool assigned;		/* the stream has been assigned */
	bool silent_stream;	/* silent stream activated */
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
	int prev_pcm_idx; /* previously assigned pcm index */
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

/* operations used by generic code that can be overridden by codec drivers */
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

	void (*silent_stream)(struct hda_codec *codec,
			      struct hdmi_spec_per_pin *per_pin,
			      bool enable);
};

struct hdmi_pcm {
	struct hda_pcm *pcm;
	struct snd_jack *jack;
	struct snd_kcontrol *eld_ctl;
};

enum {
	SILENT_STREAM_OFF = 0,
	SILENT_STREAM_KAE,	/* use standard HDA Keep-Alive */
	SILENT_STREAM_I915,	/* Intel i915 extension */
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
	struct hdmi_pcm pcm_rec[8];
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
	bool static_pcm_mapping;
	/* hdmi interrupt trigger control flag for Nvidia codec */
	bool hdmi_intr_trig_ctrl;
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
	int (*port2pin)(struct hda_codec *codec, int port); /* reverse port/pin mapping */

	struct hdac_chmap chmap;
	hda_nid_t vendor_nid;
	const int *port_map;
	int port_num;
	int silent_stream_type;

	const struct snd_pcm_hw_constraint_list *hw_constraints_channels;
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
	DECLARE_FLEX_ARRAY(u8, bytes);
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

/* Generic HDMI codec support */
int snd_hda_hdmi_generic_alloc(struct hda_codec *codec);
int snd_hda_hdmi_parse_codec(struct hda_codec *codec);
int snd_hda_hdmi_generic_probe(struct hda_codec *codec);
void snd_hda_hdmi_generic_remove(struct hda_codec *codec);

int snd_hda_hdmi_generic_build_pcms(struct hda_codec *codec);
int snd_hda_hdmi_generic_build_controls(struct hda_codec *codec);
int snd_hda_hdmi_generic_init(struct hda_codec *codec);
int snd_hda_hdmi_generic_suspend(struct hda_codec *codec);
int snd_hda_hdmi_generic_resume(struct hda_codec *codec);
void snd_hda_hdmi_generic_unsol_event(struct hda_codec *codec, unsigned int res);

int snd_hda_hdmi_pin_id_to_pin_index(struct hda_codec *codec,
				     hda_nid_t pin_nid, int dev_id);
#define pin_id_to_pin_index(codec, pin, dev) \
	snd_hda_hdmi_pin_id_to_pin_index(codec, pin, dev)
int snd_hda_hdmi_generic_init_per_pins(struct hda_codec *codec);
void snd_hda_hdmi_generic_spec_free(struct hda_codec *codec);
int snd_hda_hdmi_setup_stream(struct hda_codec *codec,
			      hda_nid_t cvt_nid,
			      hda_nid_t pin_nid, int dev_id,
			      u32 stream_tag, int format);

int snd_hda_hdmi_generic_pcm_prepare(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     unsigned int stream_tag,
				     unsigned int format,
				     struct snd_pcm_substream *substream);
int snd_hda_hdmi_generic_pcm_cleanup(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream);

void snd_hda_hdmi_check_presence_and_report(struct hda_codec *codec,
					    hda_nid_t nid, int dev_id);
void snd_hda_hdmi_setup_audio_infoframe(struct hda_codec *codec,
					struct hdmi_spec_per_pin *per_pin,
					bool non_pcm);

/* Audio component support */
void snd_hda_hdmi_setup_drm_audio_ops(struct hda_codec *codec,
				      const struct drm_audio_component_audio_ops *ops);
void snd_hda_hdmi_acomp_init(struct hda_codec *codec,
			     const struct drm_audio_component_audio_ops *ops,
			     int (*port2pin)(struct hda_codec *, int));
void snd_hda_hdmi_acomp_pin_eld_notify(void *audio_ptr, int port, int dev_id);
int snd_hda_hdmi_acomp_master_bind(struct device *dev,
				   struct drm_audio_component *acomp);
void snd_hda_hdmi_acomp_master_unbind(struct device *dev,
				      struct drm_audio_component *acomp);

/* Simple / legacy HDMI codec support */
int snd_hda_hdmi_simple_probe(struct hda_codec *codec,
			      hda_nid_t cvt_nid, hda_nid_t pin_nid);
void snd_hda_hdmi_simple_remove(struct hda_codec *codec);

int snd_hda_hdmi_simple_build_pcms(struct hda_codec *codec);
int snd_hda_hdmi_simple_build_controls(struct hda_codec *codec);
int snd_hda_hdmi_simple_init(struct hda_codec *codec);
void snd_hda_hdmi_simple_unsol_event(struct hda_codec *codec,
				     unsigned int res);
int snd_hda_hdmi_simple_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream);

#endif /* __HDA_HDMI_LOCAL_H */
