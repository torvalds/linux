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
int snd_hdac_ext_bus_map_codec_to_link(struct hdac_ext_bus *bus, int addr);
struct hdac_ext_link *snd_hdac_ext_bus_get_link(struct hdac_ext_bus *bus,
						const char *codec_name);

enum hdac_ext_stream_type {
	HDAC_EXT_STREAM_TYPE_COUPLED = 0,
	HDAC_EXT_STREAM_TYPE_HOST,
	HDAC_EXT_STREAM_TYPE_LINK
};

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

#endif /* __SOUND_HDAUDIO_EXT_H */
