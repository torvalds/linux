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
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/hwdep.h>
#include <sound/hda_verbs.h>

/*
 * generic arrays
 */
struct snd_array {
	unsigned int used;
	unsigned int alloced;
	unsigned int elem_size;
	unsigned int alloc_align;
	void *list;
};

void *snd_array_new(struct snd_array *array);
void snd_array_free(struct snd_array *array);
static inline void snd_array_init(struct snd_array *array, unsigned int size,
				  unsigned int align)
{
	array->elem_size = size;
	array->alloc_align = align;
}

static inline void *snd_array_elem(struct snd_array *array, unsigned int idx)
{
	return array->list + idx * array->elem_size;
}

static inline unsigned int snd_array_index(struct snd_array *array, void *ptr)
{
	return (unsigned long)(ptr - array->list) / array->elem_size;
}

/*
 * Structures
 */

struct hda_bus;
struct hda_beep;
struct hda_codec;
struct hda_pcm;
struct hda_pcm_stream;

/* NID type */
typedef u16 hda_nid_t;

/* bus operators */
struct hda_bus_ops {
	/* send a single command */
	int (*command)(struct hda_bus *bus, unsigned int cmd);
	/* get a response from the last command */
	unsigned int (*get_response)(struct hda_bus *bus, unsigned int addr);
	/* free the private data */
	void (*private_free)(struct hda_bus *);
	/* attach a PCM stream */
	int (*attach_pcm)(struct hda_bus *bus, struct hda_codec *codec,
			  struct hda_pcm *pcm);
	/* reset bus for retry verb */
	void (*bus_reset)(struct hda_bus *bus);
#ifdef CONFIG_SND_HDA_DSP_LOADER
	/* prepare DSP transfer */
	int (*load_dsp_prepare)(struct hda_bus *bus, unsigned int format,
				unsigned int byte_size,
				struct snd_dma_buffer *bufp);
	/* start/stop DSP transfer */
	void (*load_dsp_trigger)(struct hda_bus *bus, bool start);
	/* clean up DSP transfer */
	void (*load_dsp_cleanup)(struct hda_bus *bus,
				 struct snd_dma_buffer *dmab);
#endif
};

/* unsolicited event handler */
#define HDA_UNSOL_QUEUE_SIZE	64
struct hda_bus_unsolicited {
	/* ring buffer */
	u32 queue[HDA_UNSOL_QUEUE_SIZE * 2];
	unsigned int rp, wp;
	/* workqueue */
	struct work_struct work;
};

/*
 * codec bus
 *
 * each controller needs to creata a hda_bus to assign the accessor.
 * A hda_bus contains several codecs in the list codec_list.
 */
struct hda_bus {
	struct snd_card *card;

	void *private_data;
	struct pci_dev *pci;
	const char *modelname;
	struct hda_bus_ops ops;

	/* codec linked list */
	struct list_head codec_list;
	unsigned int num_codecs;
	/* link caddr -> codec */
	struct hda_codec *caddr_tbl[HDA_MAX_CODEC_ADDRESS + 1];

	struct mutex cmd_mutex;
	struct mutex prepare_mutex;

	/* unsolicited event queue */
	struct hda_bus_unsolicited unsol;

	/* assigned PCMs */
	DECLARE_BITMAP(pcm_dev_bits, SNDRV_PCM_DEVICES);

	/* misc op flags */
	unsigned int needs_damn_long_delay :1;
	unsigned int allow_bus_reset:1;	/* allow bus reset at fatal error */
	unsigned int sync_write:1;	/* sync after verb write */
	/* status for codec/controller */
	unsigned int shutdown :1;	/* being unloaded */
	unsigned int rirb_error:1;	/* error in codec communication */
	unsigned int response_reset:1;	/* controller was reset */
	unsigned int in_reset:1;	/* during reset operation */
	unsigned int no_response_fallback:1; /* don't fallback at RIRB error */

	int primary_dig_out_type;	/* primary digital out PCM type */
	unsigned long codec_powered;	/* bit flags of powered codecs */
};

/*
 * codec preset
 *
 * Known codecs have the patch to build and set up the controls/PCMs
 * better than the generic parser.
 */
struct hda_codec_preset {
	unsigned int id;
	unsigned int mask;
	unsigned int subs;
	unsigned int subs_mask;
	unsigned int rev;
	hda_nid_t afg, mfg;
	const char *name;
	int (*patch)(struct hda_codec *codec);
};
	
#define HDA_CODEC_ID_GENERIC_HDMI	0x00000101
#define HDA_CODEC_ID_GENERIC		0x00000201

struct hda_codec_driver {
	struct device_driver driver;
	const struct hda_codec_preset *preset;
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

/* record for amp information cache */
struct hda_cache_head {
	u32 key:31;		/* hash key */
	u32 dirty:1;
	u16 val;		/* assigned value */
	u16 next;
};

struct hda_amp_info {
	struct hda_cache_head head;
	u32 amp_caps;		/* amp capabilities */
	u16 vol[2];		/* current volume & mute */
};

struct hda_cache_rec {
	u16 hash[64];			/* hash table for index */
	struct snd_array buf;		/* record entries */
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
	struct device dev;
	struct hda_bus *bus;
	struct snd_card *card;
	unsigned int addr;	/* codec addr*/
	struct list_head list;	/* list point */

	hda_nid_t afg;	/* AFG node id */
	hda_nid_t mfg;	/* MFG node id */

	/* ids */
	u8 afg_function_id;
	u8 mfg_function_id;
	u8 afg_unsol;
	u8 mfg_unsol;
	u32 vendor_id;
	u32 subsystem_id;
	u32 revision_id;
	u32 probe_id; /* overridden id for probing */

	/* detected preset */
	const struct hda_codec_preset *preset;
	const char *vendor_name;	/* codec vendor name */
	const char *chip_name;		/* codec chip name */
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
	unsigned int num_nodes;
	hda_nid_t start_nid;
	u32 *wcaps;

	struct snd_array mixers;	/* list of assigned mixer elements */
	struct snd_array nids;		/* list of mapped mixer elements */

	struct hda_cache_rec amp_cache;	/* cache for amp access */
	struct hda_cache_rec cmd_cache;	/* cache for other commands */

	struct list_head conn_list;	/* linked-list of connection-list */

	struct mutex spdif_mutex;
	struct mutex control_mutex;
	struct mutex hash_mutex;
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
	unsigned int epss:1;		/* supporting EPSS? */
	unsigned int cached_write:1;	/* write only to caches */
	unsigned int dp_mst:1; /* support DP1.2 Multi-stream transport */
	unsigned int dump_coef:1; /* dump processing coefs in codec proc file */
	unsigned int power_save_node:1; /* advanced PM for each widget */
#ifdef CONFIG_PM
	unsigned int d3_stop_clk:1;	/* support D3 operation without BCLK */
	atomic_t in_pm;		/* suspend/resume being performed */
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

#ifdef CONFIG_SND_HDA_INPUT_JACK
	/* jack detection */
	struct snd_array jacks;
#endif

	int depop_delay; /* depop delay in ms, -1 for default delay time */

	/* fix-up list */
	int fixup_id;
	const struct hda_fixup *fixup_list;
	const char *fixup_name;

	/* additional init verbs */
	struct snd_array verbs;
};

#define dev_to_hda_codec(_dev)	container_of(_dev, struct hda_codec, dev)
#define hda_codec_dev(_dev)	(&(_dev)->dev)

extern struct bus_type snd_hda_bus_type;

/* direction */
enum {
	HDA_INPUT, HDA_OUTPUT
};

/* snd_hda_codec_read/write optional flags */
#define HDA_RW_NO_RESPONSE_FALLBACK	(1 << 0)

/*
 * constructors
 */
int snd_hda_bus_new(struct snd_card *card, struct hda_bus **busp);
int snd_hda_codec_new(struct hda_bus *bus, struct snd_card *card,
		      unsigned int codec_addr, struct hda_codec **codecp);
int snd_hda_codec_configure(struct hda_codec *codec);
int snd_hda_codec_update_widgets(struct hda_codec *codec);

/*
 * low level functions
 */
unsigned int snd_hda_codec_read(struct hda_codec *codec, hda_nid_t nid,
				int flags,
				unsigned int verb, unsigned int parm);
int snd_hda_codec_write(struct hda_codec *codec, hda_nid_t nid, int flags,
			unsigned int verb, unsigned int parm);
#define snd_hda_param_read(codec, nid, param) \
	snd_hda_codec_read(codec, nid, 0, AC_VERB_PARAMETERS, param)
int snd_hda_get_sub_nodes(struct hda_codec *codec, hda_nid_t nid,
			  hda_nid_t *start_id);
int snd_hda_get_connections(struct hda_codec *codec, hda_nid_t nid,
			    hda_nid_t *conn_list, int max_conns);
static inline int
snd_hda_get_num_conns(struct hda_codec *codec, hda_nid_t nid)
{
	return snd_hda_get_connections(codec, nid, NULL, 0);
}
int snd_hda_get_num_raw_conns(struct hda_codec *codec, hda_nid_t nid);
int snd_hda_get_raw_connections(struct hda_codec *codec, hda_nid_t nid,
			    hda_nid_t *conn_list, int max_conns);
int snd_hda_get_conn_list(struct hda_codec *codec, hda_nid_t nid,
			  const hda_nid_t **listp);
int snd_hda_override_conn_list(struct hda_codec *codec, hda_nid_t nid, int nums,
			  const hda_nid_t *list);
int snd_hda_get_conn_index(struct hda_codec *codec, hda_nid_t mux,
			   hda_nid_t nid, int recursive);
int snd_hda_get_devices(struct hda_codec *codec, hda_nid_t nid,
			u8 *dev_list, int max_devices);
int snd_hda_query_supported_pcm(struct hda_codec *codec, hda_nid_t nid,
				u32 *ratesp, u64 *formatsp, unsigned int *bpsp);

struct hda_verb {
	hda_nid_t nid;
	u32 verb;
	u32 param;
};

void snd_hda_sequence_write(struct hda_codec *codec,
			    const struct hda_verb *seq);

/* unsolicited event */
int snd_hda_queue_unsol_event(struct hda_bus *bus, u32 res, u32 res_ex);

/* cached write */
int snd_hda_codec_write_cache(struct hda_codec *codec, hda_nid_t nid,
			      int flags, unsigned int verb, unsigned int parm);
void snd_hda_sequence_write_cache(struct hda_codec *codec,
				  const struct hda_verb *seq);
int snd_hda_codec_update_cache(struct hda_codec *codec, hda_nid_t nid,
			      int flags, unsigned int verb, unsigned int parm);
void snd_hda_codec_resume_cache(struct hda_codec *codec);
/* both for cmd & amp caches */
void snd_hda_codec_flush_cache(struct hda_codec *codec);

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
unsigned int snd_hda_calc_stream_format(struct hda_codec *codec,
					unsigned int rate,
					unsigned int channels,
					unsigned int format,
					unsigned int maxbps,
					unsigned short spdif_ctls);
int snd_hda_is_supported_format(struct hda_codec *codec, hda_nid_t nid,
				unsigned int format);

extern const struct snd_pcm_chmap_elem snd_pcm_2_1_chmaps[];

/*
 * Misc
 */
void snd_hda_get_codec_name(struct hda_codec *codec, char *name, int namelen);
void snd_hda_codec_set_power_to_all(struct hda_codec *codec, hda_nid_t fg,
				    unsigned int power_state);

int snd_hda_lock_devices(struct hda_bus *bus);
void snd_hda_unlock_devices(struct hda_bus *bus);
void snd_hda_bus_reset(struct hda_bus *bus);

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
 * get widget information
 */
const char *snd_hda_get_jack_connectivity(u32 cfg);
const char *snd_hda_get_jack_type(u32 cfg);
const char *snd_hda_get_jack_location(u32 cfg);

/*
 * power saving
 */
#ifdef CONFIG_PM
void snd_hda_power_up(struct hda_codec *codec);
void snd_hda_power_down(struct hda_codec *codec);
void snd_hda_set_power_save(struct hda_bus *bus, int delay);
void snd_hda_update_power_acct(struct hda_codec *codec);
#else
static inline void snd_hda_power_up(struct hda_codec *codec) {}
static inline void snd_hda_power_down(struct hda_codec *codec) {}
static inline void snd_hda_set_power_save(struct hda_bus *bus, int delay) {}
#endif

#ifdef CONFIG_SND_HDA_PATCH_LOADER
/*
 * patch firmware
 */
int snd_hda_load_patch(struct hda_bus *bus, size_t size, const void *buf);
#endif

#ifdef CONFIG_SND_HDA_DSP_LOADER
static inline int
snd_hda_codec_load_dsp_prepare(struct hda_codec *codec, unsigned int format,
				unsigned int size,
				struct snd_dma_buffer *bufp)
{
	return codec->bus->ops.load_dsp_prepare(codec->bus, format, size, bufp);
}
static inline void
snd_hda_codec_load_dsp_trigger(struct hda_codec *codec, bool start)
{
	return codec->bus->ops.load_dsp_trigger(codec->bus, start);
}
static inline void
snd_hda_codec_load_dsp_cleanup(struct hda_codec *codec,
				struct snd_dma_buffer *dmab)
{
	return codec->bus->ops.load_dsp_cleanup(codec->bus, dmab);
}
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
