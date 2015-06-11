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

#endif /* __SOUND_HDAUDIO_EXT_H */
