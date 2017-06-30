#ifndef _BEX_H
#define _BEX_H

#include <linux/device.h>

struct bex_device {
	const char *type;
	int version;
	struct device dev;
};

#define to_bex_device(drv) container_of(dev, struct bex_device, dev)

struct bex_driver {
	const char *type;

	int (*probe)(struct bex_device *dev);
	void (*remove)(struct bex_device *dev);

	struct device_driver driver;
};

#define to_bex_driver(drv) container_of(drv, struct bex_driver, driver)

int bex_register_driver(struct bex_driver *drv);
void bex_unregister_driver(struct bex_driver *drv);

#endif
