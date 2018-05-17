/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SOUND_HDAUDIO_EXT_H
#define __SOUND_HDAUDIO_EXT_H

#include <sound/hdaudio.h>

/**
 * hdac_ext_bus: HDAC extended bus for extended HDA caps
 *
 * @bus: hdac bus
 * @num_streams: streams supported
 * @hlink_list: link list of HDA links
 * @lock: lock for link mgmt
 * @cmd_dma_state: state of cmd DMAs: CORB and RIRB
 */
struct hdac_ext_bus {
	struct hdac_bus bus;
	int num_streams;
	int idx;

	struct list_head hlink_list;

	struct mutex lock;
	bool cmd_dma_state;
};

int snd_hdac_ext_bus_init(struct hdac_ext_bus *sbus, struct device *dev,
		      const struct hdac_bus_ops *ops,
		      const struct hdac_io_ops *io_ops);

void snd_hdac_ext_bus_exit(struct hdac_ext_bus *sbus);
int snd_hdac_ext_bus_device_init(struct hdac_ext_bus *sbus, int addr);
void snd_hdac_ext_bus_device_exit(struct hdac_device *hdev);
void snd_hdac_ext_bus_device_remove(struct hdac_ext_bus *ebus);

#define ebus_to_hbus(ebus)	(&(ebus)->bus)
#define hbus_to_ebus(_bus) \
	container_of(_bus, struct hdac_ext_bus, bus)

#define HDA_CODEC_REV_EXT_ENTRY(_vid, _rev, _name, drv_data) \
	{ .vendor_id = (_vid), .rev_id = (_rev), .name = (_name), \
	  .api_version = HDA_DEV_ASOC, \
	  .driver_data = (unsigned long)(drv_data) }
#define HDA_CODEC_EXT_ENTRY(_vid, _revid, _name, _drv_data) \
	HDA_CODEC_REV_EXT_ENTRY(_vid, _revid, _name, _drv_data)

void snd_hdac_ext_bus_ppcap_enable(struct hdac_ext_bus *chip, bool enable);
void snd_hdac_ext_bus_ppcap_int_enable(struct hdac_ext_bus *chip, bool enable);

void snd_hdac_ext_stream_spbcap_enable(struct hdac_ext_bus *chip,
				 bool enable, int index);

int snd_hdac_ext_bus_get_ml_capabilities(struct hdac_ext_bus *bus);
struct hdac_ext_link *snd_hdac_ext_bus_get_link(struct hdac_ext_bus *bus,
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
 * @spib_addr: software position in buffers stream pointer
 * @fifo_addr: software position Max fifos stream pointer
 * @dpibr_addr: DMA position in buffer resume pointer
 * @dpib: DMA position in buffer
 * @lpib: Linear position in buffer
 * @decoupled: stream host and link is decoupled
 * @link_locked: link is locked
 * @link_prepared: link is prepared
 * link_substream: link substream
 */
struct hdac_ext_stream {
	struct hdac_stream hstream;

	void __iomem *pphc_addr;
	void __iomem *pplc_addr;

	void __iomem *spib_addr;
	void __iomem *fifo_addr;

	void __iomem *dpibr_addr;

	u32 dpib;
	u32 lpib;
	bool decoupled:1;
	bool link_locked:1;
	bool link_prepared;

	struct snd_pcm_substream *link_substream;
};

#define hdac_stream(s)		(&(s)->hstream)
#define stream_to_hdac_ext_stream(s) \
	container_of(s, struct hdac_ext_stream, hstream)

void snd_hdac_ext_stream_init(struct hdac_ext_bus *bus,
				struct hdac_ext_stream *stream, int idx,
				int direction, int tag);
int snd_hdac_ext_stream_init_all(struct hdac_ext_bus *ebus, int start_idx,
		int num_stream, int dir);
void snd_hdac_stream_free_all(struct hdac_ext_bus *ebus);
void snd_hdac_link_free_all(struct hdac_ext_bus *ebus);
struct hdac_ext_stream *snd_hdac_ext_stream_assign(struct hdac_ext_bus *bus,
					   struct snd_pcm_substream *substream,
					   int type);
void snd_hdac_ext_stream_release(struct hdac_ext_stream *azx_dev, int type);
void snd_hdac_ext_stream_decouple(struct hdac_ext_bus *bus,
				struct hdac_ext_stream *azx_dev, bool decouple);
void snd_hdac_ext_stop_streams(struct hdac_ext_bus *sbus);

int snd_hdac_ext_stream_set_spib(struct hdac_ext_bus *ebus,
				 struct hdac_ext_stream *stream, u32 value);
int snd_hdac_ext_stream_get_spbmaxfifo(struct hdac_ext_bus *ebus,
				 struct hdac_ext_stream *stream);
void snd_hdac_ext_stream_drsm_enable(struct hdac_ext_bus *ebus,
				bool enable, int index);
int snd_hdac_ext_stream_set_dpibr(struct hdac_ext_bus *ebus,
				struct hdac_ext_stream *stream, u32 value);
int snd_hdac_ext_stream_set_lpib(struct hdac_ext_stream *stream, u32 value);

void snd_hdac_ext_link_stream_start(struct hdac_ext_stream *hstream);
void snd_hdac_ext_link_stream_clear(struct hdac_ext_stream *hstream);
void snd_hdac_ext_link_stream_reset(struct hdac_ext_stream *hstream);
int snd_hdac_ext_link_stream_setup(struct hdac_ext_stream *stream, int fmt);

struct hdac_ext_link {
	struct hdac_bus *bus;
	int index;
	void __iomem *ml_addr; /* link output stream reg pointer */
	u32 lcaps;   /* link capablities */
	u16 lsdiid;  /* link sdi identifier */

	int ref_count;

	struct list_head list;
};

int snd_hdac_ext_bus_link_power_up(struct hdac_ext_link *link);
int snd_hdac_ext_bus_link_power_down(struct hdac_ext_link *link);
int snd_hdac_ext_bus_link_power_up_all(struct hdac_ext_bus *ebus);
int snd_hdac_ext_bus_link_power_down_all(struct hdac_ext_bus *ebus);
void snd_hdac_ext_link_set_stream_id(struct hdac_ext_link *link,
				 int stream);
void snd_hdac_ext_link_clear_stream_id(struct hdac_ext_link *link,
				 int stream);

int snd_hdac_ext_bus_link_get(struct hdac_ext_bus *ebus,
				struct hdac_ext_link *link);
int snd_hdac_ext_bus_link_put(struct hdac_ext_bus *ebus,
				struct hdac_ext_link *link);

/* update register macro */
#define snd_hdac_updatel(addr, reg, mask, val)		\
	writel(((readl(addr + reg) & ~(mask)) | (val)), \
		addr + reg)

#define snd_hdac_updatew(addr, reg, mask, val)		\
	writew(((readw(addr + reg) & ~(mask)) | (val)), \
		addr + reg)


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

#define HDA_MAX_NIDS 16

/**
 * struct hdac_ext_device - HDAC Ext device
 *
 * @hdac: hdac core device
 * @nid_list - the dai map which matches the dai-name with the nid
 * @map_cur_idx - the idx in use in dai_map
 * @ops - the hda codec ops common to all codec drivers
 * @pvt_data - private data, for asoc contains asoc codec object
 */
struct hdac_ext_device {
	struct hdac_device hdev;
	struct hdac_ext_bus *ebus;

	/* soc-dai to nid map */
	struct hda_dai_map nid_list[HDA_MAX_NIDS];
	unsigned int map_cur_idx;

	/* codec ops */
	struct hdac_ext_codec_ops ops;

	struct snd_card *card;
	void *scodec;
	void *private_data;
};

struct hdac_ext_dma_params {
	u32 format;
	u8 stream_tag;
};
#define to_ehdac_device(dev) (container_of((dev), \
				 struct hdac_ext_device, hdev))
/*
 * HD-audio codec base driver
 */
struct hdac_ext_driver {
	struct hdac_driver hdac;

	int	(*probe)(struct hdac_ext_device *dev);
	int	(*remove)(struct hdac_ext_device *dev);
	void	(*shutdown)(struct hdac_ext_device *dev);
};

int snd_hda_ext_driver_register(struct hdac_ext_driver *drv);
void snd_hda_ext_driver_unregister(struct hdac_ext_driver *drv);

#define to_ehdac_driver(_drv) container_of(_drv, struct hdac_ext_driver, hdac)

#endif /* __SOUND_HDAUDIO_EXT_H */
