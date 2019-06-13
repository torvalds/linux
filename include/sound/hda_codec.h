/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __SOUND_HDA_CODEC_H
#define __SOUND_HDA_CODEC_H

#include <linux/kref.h>
#include <linux/mod_devicetable.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/hwdep.h>
#include <sound/hdaudio.h>
#include <sound/hda_verbs.h>
#include <sound/hda_regmap.h>

#define IS_BXT(pci) ((pci)->vendor == 0x8086 && (pci)->device == 0x5a98)
#define IS_CFL(pci) ((pci)->vendor == 0x8086 && (pci)->device == 0xa348)

/*
 * Structures
 */

struct hda_bus;
struct hda_beep;
struct hda_codec;
struct hda_pcm;
struct hda_pcm_stream;

/*
 * codec bus
 *
 * each controller needs to creata a hda_bus to assign the accessor.
 * A hda_bus contains several codecs in the list codec_list.
 */
struct hda_bus {
	struct hdac_bus core;

	struct snd_card *card;

	struct pci_dev *pci;
	const char *modelname;

	struct mutex prepare_mutex;

	/* assigned PCMs */
	DECLARE_BITMAP(pcm_dev_bits, SNDRV_PCM_DEVICES);

	/* misc op flags */
	unsigned int needs_damn_long_delay :1;
	unsigned int allow_bus_reset:1;	/* allow bus reset at fatal error */
	/* status for codec/controller */
	unsigned int shutdown :1;	/* being unloaded */
	unsigned int response_reset:1;	/* controller was reset */
	unsigned int in_reset:1;	/* during reset operation */
	unsigned int no_response_fallback:1; /* don't fallback at RIRB error */
	unsigned int bus_probing :1;	/* during probing process */

	int primary_dig_out_type;	/* primary digital out PCM type */
	unsigned int mixer_assigned;	/* codec addr for mixer name */
};

/* from hdac_bus to hda_bus */
#define to_hda_bus(bus)		container_of(bus, struct hda_bus, core)

/*
 * codec preset
 *
 * Known codecs have the patch to build and set up the controls/PCMs
 * better than the generic parser.
 */
typedef int (*hda_codec_patch_t)(struct hda_codec *);
	
#define HDA_CODEC_ID_SKIP_PROBE		0x00000001
#define HDA_CODEC_ID_GENERIC_HDMI	0x00000101
#define HDA_CODEC_ID_GENERIC		0x00000201

#define HDA_CODEC_REV_ENTRY(_vid, _rev, _name, _patch) \
	{ .vendor_id = (_vid), .rev_id = (_rev), .name = (_name), \
	  .api_version = HDA_DEV_LEGACY, \
	  .driver_data = (unsigned long)(_patch) }
#define HDA_CODEC_ENTRY(_vid, _name, _patch) \
	HDA_CODEC_REV_ENTRY(_vid, 0, _name, _patch)

struct hda_codec_driver {
	struct hdac_driver core;
	const struct hda_device_id *id;
};

int __hda_codec_driver_register(struct hda_codec_driver *drv, const char *name,
			       struct module *owner);
#define hda_codec_driver_register(drv) \
	__hda_codec_driver_register(drv, KBUILD_MODNAME, THIS_MODULE)
void hda_codec_driver_unregister(struct hda_codec_driver *drv);
#define module_hda_codec_driver(drv) \
	module_driver(drv, hda_codec_driver_register, \
		      hda_codec_driver_unregister)

/* ops set by the preset patch */
struct hda_codec_ops {
	int (*build_controls)(struct hda_codec *codec);
	int (*build_pcms)(struct hda_codec *codec);
	int (*init)(struct hda_codec *codec);
	void (*free)(struct hda_codec *codec);
	void (*unsol_event)(struct hda_codec *codec, unsigned int res);
	void (*set_power_state)(struct hda_codec *codec, hda_nid_t fg,
				unsigned int power_state);
#ifdef CONFIG_PM
	int (*suspend)(struct hda_codec *codec);
	int (*resume)(struct hda_codec *codec);
	int (*check_power_status)(struct hda_codec *codec, hda_nid_t nid);
#endif
	void (*reboot_notify)(struct hda_codec *codec);
	void (*stream_pm)(struct hda_codec *codec, hda_nid_t nid, bool on);
};

/* PCM callbacks */
struct hda_pcm_ops {
	int (*open)(struct hda_pcm_stream *info, struct hda_codec *codec,
		    struct snd_pcm_substream *substream);
	int (*close)(struct hda_pcm_stream *info, struct hda_codec *codec,
		     struct snd_pcm_substream *substream);
	int (*prepare)(struct hda_pcm_stream *info, struct hda_codec *codec,
		       unsigned int stream_tag, unsigned int format,
		       struct snd_pcm_substream *substream);
	int (*cleanup)(struct hda_pcm_stream *info, struct hda_codec *codec,
		       struct snd_pcm_substream *substream);
	unsigned int (*get_delay)(struct hda_pcm_stream *info,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream);
};

/* PCM information for each substream */
struct hda_pcm_stream {
	unsigned int substreams;	/* number of substreams, 0 = not exist*/
	unsigned int channels_min;	/* min. number of channels */
	unsigned int channels_max;	/* max. number of channels */
	hda_nid_t nid;	/* default NID to query rates/formats/bps, or set up */
	u32 rates;	/* supported rates */
	u64 formats;	/* supported formats (SNDRV_PCM_FMTBIT_) */
	unsigned int maxbps;	/* supported max. bit per sample */
	const struct snd_pcm_chmap_elem *chmap; /* chmap to override */
	struct hda_pcm_ops ops;
};

/* PCM types */
enum {
	HDA_PCM_TYPE_AUDIO,
	HDA_PCM_TYPE_SPDIF,
	HDA_PCM_TYPE_HDMI,
	HDA_PCM_TYPE_MODEM,
	HDA_PCM_NTYPES
};

#define SNDRV_PCM_INVALID_DEVICE	(-1)
/* for PCM creation */
struct hda_pcm {
	char *name;
	struct hda_pcm_stream stream[2];
	unsigned int pcm_type;	/* HDA_PCM_TYPE_XXX */
	int device;		/* device number to assign */
	struct snd_pcm *pcm;	/* assigned PCM instance */
	bool own_chmap;		/* codec driver provides own channel maps */
	/* private: */
	struct hda_codec *codec;
	struct kref kref;
	struct list_head list;
};

/* codec information */
struct hda_codec {
	struct hdac_device core;
	struct hda_bus *bus;
	struct snd_card *card;
	unsigned int addr;	/* codec addr*/
	u32 probe_id; /* overridden id for probing */

	/* detected preset */
	const struct hda_device_id *preset;
	const char *modelname;	/* model name for preset */

	/* set by patch */
	struct hda_codec_ops patch_ops;

	/* PCM to create, set by patch_ops.build_pcms callback */
	struct list_head pcm_list_head;

	/* codec specific info */
	void *spec;

	/* beep device */
	struct hda_beep *beep;
	unsigned int beep_mode;

	/* widget capabilities cache */
	u32 *wcaps;

	struct snd_array mixers;	/* list of assigned mixer elements */
	struct snd_array nids;		/* list of mapped mixer elements */

	struct list_head conn_list;	/* linked-list of connection-list */

	struct mutex spdif_mutex;
	struct mutex control_mutex;
	struct snd_array spdif_out;
	unsigned int spdif_in_enable;	/* SPDIF input enable? */
	const hda_nid_t *slave_dig_outs; /* optional digital out slave widgets */
	struct snd_array init_pins;	/* initial (BIOS) pin configurations */
	struct snd_array driver_pins;	/* pin configs set by codec parser */
	struct snd_array cvt_setups;	/* audio convert setups */

	struct mutex user_mutex;
#ifdef CONFIG_SND_HDA_RECONFIG
	struct snd_array init_verbs;	/* additional init verbs */
	struct snd_array hints;		/* additional hints */
	struct snd_array user_pins;	/* default pin configs to override */
#endif

#ifdef CONFIG_SND_HDA_HWDEP
	struct snd_hwdep *hwdep;	/* assigned hwdep device */
#endif

	/* misc flags */
	unsigned int in_freeing:1; /* being released */
	unsigned int registered:1; /* codec was registered */
	unsigned int display_power_control:1; /* needs display power */
	unsigned int spdif_status_reset :1; /* needs to toggle SPDIF for each
					     * status change
					     * (e.g. Realtek codecs)
					     */
	unsigned int pin_amp_workaround:1; /* pin out-amp takes index
					    * (e.g. Conexant codecs)
					    */
	unsigned int single_adc_amp:1; /* adc in-amp takes no index
					* (e.g. CX20549 codec)
					*/
	unsigned int no_sticky_stream:1; /* no sticky-PCM stream assignment */
	unsigned int pins_shutup:1;	/* pins are shut up */
	unsigned int no_trigger_sense:1; /* don't trigger at pin-sensing */
	unsigned int no_jack_detect:1;	/* Machine has no jack-detection */
	unsigned int inv_eapd:1; /* broken h/w: inverted EAPD control */
	unsigned int inv_jack_detect:1;	/* broken h/w: inverted detection bit */
	unsigned int pcm_format_first:1; /* PCM format must be set first */
	unsigned int cached_write:1;	/* write only to caches */
	unsigned int dp_mst:1; /* support DP1.2 Multi-stream transport */
	unsigned int dump_coef:1; /* dump processing coefs in codec proc file */
	unsigned int power_save_node:1; /* advanced PM for each widget */
	unsigned int auto_runtime_pm:1; /* enable automatic codec runtime pm */
	unsigned int force_pin_prefix:1; /* Add location prefix */
	unsigned int link_down_at_suspend:1; /* link down at runtime suspend */
#ifdef CONFIG_PM
	unsigned long power_on_acct;
	unsigned long power_off_acct;
	unsigned long power_jiffies;
#endif

	/* filter the requested power state per nid */
	unsigned int (*power_filter)(struct hda_codec *codec, hda_nid_t nid,
				     unsigned int power_state);

	/* codec-specific additional proc output */
	void (*proc_widget_hook)(struct snd_info_buffer *buffer,
				 struct hda_codec *codec, hda_nid_t nid);

	/* jack detection */
	struct snd_array jacktbl;
	unsigned long jackpoll_interval; /* In jiffies. Zero means no poll, rely on unsol events */
	struct delayed_work jackpoll_work;

	/* jack detection */
	struct snd_array jacks;

	int depop_delay; /* depop delay in ms, -1 for default delay time */

	/* fix-up list */
	int fixup_id;
	const struct hda_fixup *fixup_list;
	const char *fixup_name;

	/* additional init verbs */
	struct snd_array verbs;
};

#define dev_to_hda_codec(_dev)	container_of(_dev, struct hda_codec, core.dev)
#define hda_codec_dev(_dev)	(&(_dev)->core.dev)

#define list_for_each_codec(c, bus) \
	list_for_each_entry(c, &(bus)->core.codec_list, core.list)
#define list_for_each_codec_safe(c, n, bus)				\
	list_for_each_entry_safe(c, n, &(bus)->core.codec_list, core.list)

/* snd_hda_codec_read/write optional flags */
#define HDA_RW_NO_RESPONSE_FALLBACK	(1 << 0)

/*
 * constructors
 */
int snd_hda_codec_new(struct hda_bus *bus, struct snd_card *card,
		      unsigned int codec_addr, struct hda_codec **codecp);
int snd_hda_codec_device_new(struct hda_bus *bus, struct snd_card *card,
		      unsigned int codec_addr, struct hda_codec *codec);
int snd_hda_codec_configure(struct hda_codec *codec);
int snd_hda_codec_update_widgets(struct hda_codec *codec);

/*
 * low level functions
 */
static inline unsigned int
snd_hda_codec_read(struct hda_codec *codec, hda_nid_t nid,
				int flags,
				unsigned int verb, unsigned int parm)
{
	return snd_hdac_codec_read(&codec->core, nid, flags, verb, parm);
}

static inline int
snd_hda_codec_write(struct hda_codec *codec, hda_nid_t nid, int flags,
			unsigned int verb, unsigned int parm)
{
	return snd_hdac_codec_write(&codec->core, nid, flags, verb, parm);
}

#define snd_hda_param_read(codec, nid, param) \
	snd_hdac_read_parm(&(codec)->core, nid, param)
#define snd_hda_get_sub_nodes(codec, nid, start_nid) \
	snd_hdac_get_sub_nodes(&(codec)->core, nid, start_nid)
int snd_hda_get_connections(struct hda_codec *codec, hda_nid_t nid,
			    hda_nid_t *conn_list, int max_conns);
static inline int
snd_hda_get_num_conns(struct hda_codec *codec, hda_nid_t nid)
{
	return snd_hda_get_connections(codec, nid, NULL, 0);
}

#define snd_hda_get_raw_connections(codec, nid, list, max_conns) \
	snd_hdac_get_connections(&(codec)->core, nid, list, max_conns)
#define snd_hda_get_num_raw_conns(codec, nid) \
	snd_hdac_get_connections(&(codec)->core, nid, NULL, 0);

int snd_hda_get_conn_list(struct hda_codec *codec, hda_nid_t nid,
			  const hda_nid_t **listp);
int snd_hda_override_conn_list(struct hda_codec *codec, hda_nid_t nid, int nums,
			  const hda_nid_t *list);
int snd_hda_get_conn_index(struct hda_codec *codec, hda_nid_t mux,
			   hda_nid_t nid, int recursive);
unsigned int snd_hda_get_num_devices(struct hda_codec *codec, hda_nid_t nid);
int snd_hda_get_devices(struct hda_codec *codec, hda_nid_t nid,
			u8 *dev_list, int max_devices);
int snd_hda_get_dev_select(struct hda_codec *codec, hda_nid_t nid);
int snd_hda_set_dev_select(struct hda_codec *codec, hda_nid_t nid, int dev_id);

struct hda_verb {
	hda_nid_t nid;
	u32 verb;
	u32 param;
};

void snd_hda_sequence_write(struct hda_codec *codec,
			    const struct hda_verb *seq);

/* unsolicited event */
static inline void
snd_hda_queue_unsol_event(struct hda_bus *bus, u32 res, u32 res_ex)
{
	snd_hdac_bus_queue_event(&bus->core, res, res_ex);
}

/* cached write */
static inline int
snd_hda_codec_write_cache(struct hda_codec *codec, hda_nid_t nid,
			  int flags, unsigned int verb, unsigned int parm)
{
	return snd_hdac_regmap_write(&codec->core, nid, verb, parm);
}

/* the struct for codec->pin_configs */
struct hda_pincfg {
	hda_nid_t nid;
	unsigned char ctrl;	/* original pin control value */
	unsigned char target;	/* target pin control value */
	unsigned int cfg;	/* default configuration */
};

unsigned int snd_hda_codec_get_pincfg(struct hda_codec *codec, hda_nid_t nid);
int snd_hda_codec_set_pincfg(struct hda_codec *codec, hda_nid_t nid,
			     unsigned int cfg);
int snd_hda_add_pincfg(struct hda_codec *codec, struct snd_array *list,
		       hda_nid_t nid, unsigned int cfg); /* for hwdep */
void snd_hda_shutup_pins(struct hda_codec *codec);

/* SPDIF controls */
struct hda_spdif_out {
	hda_nid_t nid;		/* Converter nid values relate to */
	unsigned int status;	/* IEC958 status bits */
	unsigned short ctls;	/* SPDIF control bits */
};
struct hda_spdif_out *snd_hda_spdif_out_of_nid(struct hda_codec *codec,
					       hda_nid_t nid);
void snd_hda_spdif_ctls_unassign(struct hda_codec *codec, int idx);
void snd_hda_spdif_ctls_assign(struct hda_codec *codec, int idx, hda_nid_t nid);

/*
 * Mixer
 */
int snd_hda_codec_build_controls(struct hda_codec *codec);

/*
 * PCM
 */
int snd_hda_codec_parse_pcms(struct hda_codec *codec);
int snd_hda_codec_build_pcms(struct hda_codec *codec);

__printf(2, 3)
struct hda_pcm *snd_hda_codec_pcm_new(struct hda_codec *codec,
				      const char *fmt, ...);

static inline void snd_hda_codec_pcm_get(struct hda_pcm *pcm)
{
	kref_get(&pcm->kref);
}
void snd_hda_codec_pcm_put(struct hda_pcm *pcm);

int snd_hda_codec_prepare(struct hda_codec *codec,
			  struct hda_pcm_stream *hinfo,
			  unsigned int stream,
			  unsigned int format,
			  struct snd_pcm_substream *substream);
void snd_hda_codec_cleanup(struct hda_codec *codec,
			   struct hda_pcm_stream *hinfo,
			   struct snd_pcm_substream *substream);

void snd_hda_codec_setup_stream(struct hda_codec *codec, hda_nid_t nid,
				u32 stream_tag,
				int channel_id, int format);
void __snd_hda_codec_cleanup_stream(struct hda_codec *codec, hda_nid_t nid,
				    int do_now);
#define snd_hda_codec_cleanup_stream(codec, nid) \
	__snd_hda_codec_cleanup_stream(codec, nid, 0)

#define snd_hda_query_supported_pcm(codec, nid, ratesp, fmtsp, bpsp) \
	snd_hdac_query_supported_pcm(&(codec)->core, nid, ratesp, fmtsp, bpsp)
#define snd_hda_is_supported_format(codec, nid, fmt) \
	snd_hdac_is_supported_format(&(codec)->core, nid, fmt)

extern const struct snd_pcm_chmap_elem snd_pcm_2_1_chmaps[];

int snd_hda_attach_pcm_stream(struct hda_bus *_bus, struct hda_codec *codec,
			      struct hda_pcm *cpcm);

/*
 * Misc
 */
void snd_hda_get_codec_name(struct hda_codec *codec, char *name, int namelen);
void snd_hda_codec_set_power_to_all(struct hda_codec *codec, hda_nid_t fg,
				    unsigned int power_state);

int snd_hda_lock_devices(struct hda_bus *bus);
void snd_hda_unlock_devices(struct hda_bus *bus);
void snd_hda_bus_reset(struct hda_bus *bus);
void snd_hda_bus_reset_codecs(struct hda_bus *bus);

int snd_hda_codec_set_name(struct hda_codec *codec, const char *name);

/*
 * power management
 */
extern const struct dev_pm_ops hda_codec_driver_pm;

static inline
int hda_call_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
#ifdef CONFIG_PM
	if (codec->patch_ops.check_power_status)
		return codec->patch_ops.check_power_status(codec, nid);
#endif
	return 0;
}

/*
 * power saving
 */
#define snd_hda_power_up(codec)		snd_hdac_power_up(&(codec)->core)
#define snd_hda_power_up_pm(codec)	snd_hdac_power_up_pm(&(codec)->core)
#define snd_hda_power_down(codec)	snd_hdac_power_down(&(codec)->core)
#define snd_hda_power_down_pm(codec)	snd_hdac_power_down_pm(&(codec)->core)
#ifdef CONFIG_PM
void snd_hda_set_power_save(struct hda_bus *bus, int delay);
void snd_hda_update_power_acct(struct hda_codec *codec);
#else
static inline void snd_hda_set_power_save(struct hda_bus *bus, int delay) {}
#endif

#ifdef CONFIG_SND_HDA_PATCH_LOADER
/*
 * patch firmware
 */
int snd_hda_load_patch(struct hda_bus *bus, size_t size, const void *buf);
#endif

#ifdef CONFIG_SND_HDA_DSP_LOADER
int snd_hda_codec_load_dsp_prepare(struct hda_codec *codec, unsigned int format,
				   unsigned int size,
				   struct snd_dma_buffer *bufp);
void snd_hda_codec_load_dsp_trigger(struct hda_codec *codec, bool start);
void snd_hda_codec_load_dsp_cleanup(struct hda_codec *codec,
				    struct snd_dma_buffer *dmab);
#else
static inline int
snd_hda_codec_load_dsp_prepare(struct hda_codec *codec, unsigned int format,
				unsigned int size,
				struct snd_dma_buffer *bufp)
{
	return -ENOSYS;
}
static inline void
snd_hda_codec_load_dsp_trigger(struct hda_codec *codec, bool start) {}
static inline void
snd_hda_codec_load_dsp_cleanup(struct hda_codec *codec,
				struct snd_dma_buffer *dmab) {}
#endif

#endif /* __SOUND_HDA_CODEC_H */
