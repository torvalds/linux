/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOUND_HDAUDIO_EXT_H
#define __SOUND_HDAUDIO_EXT_H

#include <sound/hdaudio.h>

int snd_hdac_ext_bus_init(struct hdac_bus *bus, struct device *dev,
		      const struct hdac_bus_ops *ops,
		      const struct hdac_ext_bus_ops *ext_ops);

void snd_hdac_ext_bus_exit(struct hdac_bus *bus);
void snd_hdac_ext_bus_device_remove(struct hdac_bus *bus);

#define HDA_CODEC_REV_EXT_ENTRY(_vid, _rev, _name, drv_data) \
	{ .vendor_id = (_vid), .rev_id = (_rev), .name = (_name), \
	  .api_version = HDA_DEV_ASOC, \
	  .driver_data = (unsigned long)(drv_data) }
#define HDA_CODEC_EXT_ENTRY(_vid, _revid, _name, _drv_data) \
	HDA_CODEC_REV_EXT_ENTRY(_vid, _revid, _name, _drv_data)

void snd_hdac_ext_bus_ppcap_enable(struct hdac_bus *chip, bool enable);
void snd_hdac_ext_bus_ppcap_int_enable(struct hdac_bus *chip, bool enable);

int snd_hdac_ext_bus_get_ml_capabilities(struct hdac_bus *bus);
struct hdac_ext_link *snd_hdac_ext_bus_get_hlink_by_addr(struct hdac_bus *bus, int addr);
struct hdac_ext_link *snd_hdac_ext_bus_get_hlink_by_name(struct hdac_bus *bus,
							 const char *codec_name);

enum hdac_ext_stream_type {
	HDAC_EXT_STREAM_TYPE_COUPLED = 0,
	HDAC_EXT_STREAM_TYPE_HOST,
	HDAC_EXT_STREAM_TYPE_LINK
};

/**
 * hdac_ext_stream: HDAC extended stream for extended HDA caps
 *
 * @hstream: hdac_stream
 * @pphc_addr: processing pipe host stream pointer
 * @pplc_addr: processing pipe link stream pointer
 * @decoupled: stream host and link is decoupled
 * @link_locked: link is locked
 * @link_prepared: link is prepared
 * @link_substream: link substream
 */
struct hdac_ext_stream {
	struct hdac_stream hstream;

	void __iomem *pphc_addr;
	void __iomem *pplc_addr;

	u32 pphcllpl;
	u32 pphcllpu;
	u32 pphcldpl;
	u32 pphcldpu;

	u32 pplcllpl;
	u32 pplcllpu;

	bool decoupled:1;
	bool link_locked:1;
	bool link_prepared;

	int (*host_setup)(struct hdac_stream *, bool);

	struct snd_pcm_substream *link_substream;
};

#define hdac_stream(s)		(&(s)->hstream)
#define stream_to_hdac_ext_stream(s) \
	container_of(s, struct hdac_ext_stream, hstream)

int snd_hdac_ext_stream_init_all(struct hdac_bus *bus, int start_idx,
				 int num_stream, int dir);
void snd_hdac_ext_stream_free_all(struct hdac_bus *bus);
void snd_hdac_ext_link_free_all(struct hdac_bus *bus);
struct hdac_ext_stream *snd_hdac_ext_stream_assign(struct hdac_bus *bus,
					   struct snd_pcm_substream *substream,
					   int type);
void snd_hdac_ext_stream_release(struct hdac_ext_stream *hext_stream, int type);
struct hdac_ext_stream *snd_hdac_ext_cstream_assign(struct hdac_bus *bus,
						    struct snd_compr_stream *cstream);
void snd_hdac_ext_stream_decouple_locked(struct hdac_bus *bus,
					 struct hdac_ext_stream *hext_stream, bool decouple);
void snd_hdac_ext_stream_decouple(struct hdac_bus *bus,
				struct hdac_ext_stream *azx_dev, bool decouple);

void snd_hdac_ext_stream_start(struct hdac_ext_stream *hext_stream);
void snd_hdac_ext_stream_clear(struct hdac_ext_stream *hext_stream);
void snd_hdac_ext_stream_reset(struct hdac_ext_stream *hext_stream);
int snd_hdac_ext_stream_setup(struct hdac_ext_stream *hext_stream, int fmt);
int snd_hdac_ext_host_stream_setup(struct hdac_ext_stream *hext_stream, bool code_loading);

struct hdac_ext_link {
	struct hdac_bus *bus;
	int index;
	void __iomem *ml_addr; /* link output stream reg pointer */
	u32 lcaps;   /* link capablities */
	u16 lsdiid;  /* link sdi identifier */

	int ref_count;

	struct list_head list;
};

int snd_hdac_ext_bus_link_power_up(struct hdac_ext_link *hlink);
int snd_hdac_ext_bus_link_power_down(struct hdac_ext_link *hlink);
int snd_hdac_ext_bus_link_power_up_all(struct hdac_bus *bus);
int snd_hdac_ext_bus_link_power_down_all(struct hdac_bus *bus);
void snd_hdac_ext_bus_link_set_stream_id(struct hdac_ext_link *hlink,
					 int stream);
void snd_hdac_ext_bus_link_clear_stream_id(struct hdac_ext_link *hlink,
					   int stream);

int snd_hdac_ext_bus_link_get(struct hdac_bus *bus, struct hdac_ext_link *hlink);
int snd_hdac_ext_bus_link_put(struct hdac_bus *bus, struct hdac_ext_link *hlink);

void snd_hdac_ext_bus_link_power(struct hdac_device *codec, bool enable);

struct hdac_ext_device;

/* ops common to all codec drivers */
struct hdac_ext_codec_ops {
	int (*build_controls)(struct hdac_ext_device *dev);
	int (*init)(struct hdac_ext_device *dev);
	void (*free)(struct hdac_ext_device *dev);
};

struct hda_dai_map {
	char *dai_name;
	hda_nid_t nid;
	u32	maxbps;
};

struct hdac_ext_dma_params {
	u32 format;
	u8 stream_tag;
};

int snd_hda_ext_driver_register(struct hdac_driver *drv);
void snd_hda_ext_driver_unregister(struct hdac_driver *drv);

#endif /* __SOUND_HDAUDIO_EXT_H */
