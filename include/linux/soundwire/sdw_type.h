// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SOUNDWIRE_TYPES_H
#define __SOUNDWIRE_TYPES_H

extern struct bus_type sdw_bus_type;

#define drv_to_sdw_driver(_drv) container_of(_drv, struct sdw_driver, driver)

#define sdw_register_driver(drv) \
	__sdw_register_driver(drv, THIS_MODULE)

int __sdw_register_driver(struct sdw_driver *drv, struct module *);
void sdw_unregister_driver(struct sdw_driver *drv);

int sdw_slave_modalias(const struct sdw_slave *slave, char *buf, size_t size);

#endif /* __SOUNDWIRE_TYPES_H */
