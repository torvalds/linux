#ifndef _LINUX_MEI_CL_BUS_H
#define _LINUX_MEI_CL_BUS_H

#include <linux/device.h>
#include <linux/uuid.h>

struct mei_cl_device;

struct mei_cl_driver {
	struct device_driver driver;
	const char *name;

	const struct mei_cl_device_id *id_table;

	int (*probe)(struct mei_cl_device *dev,
		     const struct mei_cl_device_id *id);
	int (*remove)(struct mei_cl_device *dev);
};

int __mei_cl_driver_register(struct mei_cl_driver *driver,
				struct module *owner);
#define mei_cl_driver_register(driver)             \
	__mei_cl_driver_register(driver, THIS_MODULE)

void mei_cl_driver_unregister(struct mei_cl_driver *driver);

#endif /* _LINUX_MEI_CL_BUS_H */
