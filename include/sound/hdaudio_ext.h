#ifndef __SOUND_HDAUDIO_EXT_H
#define __SOUND_HDAUDIO_EXT_H

#include <sound/hdaudio.h>

/**
 * hdac_ext_bus: HDAC extended bus for extended HDA caps
 *
 * @bus: hdac bus
 * @num_streams: streams supported
 * @ppcap: pp capabilities pointer
 * @spbcap: SPIB capabilities pointer
 * @mlcap: MultiLink capabilities pointer
 * @gtscap: gts capabilities pointer
 * @hlink_list: link list of HDA links
 */
struct hdac_ext_bus {
	struct hdac_bus bus;
	int num_streams;
	int idx;

	void __iomem *ppcap;
	void __iomem *spbcap;
	void __iomem *mlcap;
	void __iomem *gtscap;

	struct list_head hlink_list;
};

int snd_hdac_ext_bus_init(struct hdac_ext_bus *sbus, struct device *dev,
		      const struct hdac_bus_ops *ops,
		      const struct hdac_io_ops *io_ops);

void snd_hdac_ext_bus_exit(struct hdac_ext_bus *sbus);
int snd_hdac_ext_bus_device_init(struct hdac_ext_bus *sbus, int addr);
void snd_hdac_ext_bus_device_exit(struct hdac_device *hdev);

#define ebus_to_hbus(ebus)	(&(ebus)->bus)
#define hbus_to_ebus(_bus) \
	container_of(_bus, struct hdac_ext_bus, bus)

int snd_hdac_ext_bus_parse_capabilities(struct hdac_ext_bus *sbus);
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
 * @decoupled: stream host and link is decoupled
 * @link_locked: link is locked
 * @link_prepared: link is prepared
 * link_substream: link substream
 */
struct hdac_ext_stream {
	struct hdac_stream hstream;

	void __iomem *pphc_addr;
	void __iomem *pplc_addr;

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
	struct list_head list;
};

int snd_hdac_ext_bus_link_power_up(struct hdac_ext_link *link);
int snd_hdac_ext_bus_link_power_down(struct hdac_ext_link *link);
void snd_hdac_ext_link_set_stream_id(struct hdac_ext_link *link,
				 int stream);
void snd_hdac_ext_link_clear_stream_id(struct hdac_ext_link *link,
				 int stream);

/* update register macro */
#define snd_hdac_updatel(addr, reg, mask, val)		\
	writel(((readl(addr + reg) & ~(mask)) | (val)), \
		addr + reg)

#define snd_hdac_updatew(addr, reg, mask, val)		\
	writew(((readw(addr + reg) & ~(mask)) | (val)), \
		addr + reg)

#endif /* __SOUND_HDAUDIO_EXT_H */
