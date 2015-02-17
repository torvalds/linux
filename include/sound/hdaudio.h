/*
 * HD-audio core stuff
 */

#ifndef __SOUND_HDAUDIO_H
#define __SOUND_HDAUDIO_H

#include <linux/device.h>

/*
 * exported bus type
 */
extern struct bus_type snd_hda_bus_type;

/*
 * HD-audio codec base device
 */
struct hdac_device {
	struct device dev;
	int type;
};

/* device/driver type used for matching */
enum {
	HDA_DEV_CORE,
	HDA_DEV_LEGACY,
};

#define dev_to_hdac_dev(_dev)	container_of(_dev, struct hdac_device, dev)

/*
 * HD-audio codec base driver
 */
struct hdac_driver {
	struct device_driver driver;
	int type;
	int (*match)(struct hdac_device *dev, struct hdac_driver *drv);
};

#define drv_to_hdac_driver(_drv) container_of(_drv, struct hdac_driver, driver)

#endif /* __SOUND_HDAUDIO_H */
