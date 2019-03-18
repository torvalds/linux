/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel ISH client Interface definitions
 *
 * Copyright (c) 2019, Intel Corporation.
 */

#ifndef _INTEL_ISH_CLIENT_IF_H_
#define _INTEL_ISH_CLIENT_IF_H_

struct ishtp_cl_device;

/**
 * struct ishtp_cl_device - ISHTP device handle
 * @driver:	driver instance on a bus
 * @name:	Name of the device for probe
 * @probe:	driver callback for device probe
 * @remove:	driver callback on device removal
 *
 * Client drivers defines to get probed/removed for ISHTP client device.
 */
struct ishtp_cl_driver {
	struct device_driver driver;
	const char *name;
	const guid_t *guid;
	int (*probe)(struct ishtp_cl_device *dev);
	int (*remove)(struct ishtp_cl_device *dev);
	int (*reset)(struct ishtp_cl_device *dev);
	const struct dev_pm_ops *pm;
};

int ishtp_cl_driver_register(struct ishtp_cl_driver *driver,
			     struct module *owner);
void ishtp_cl_driver_unregister(struct ishtp_cl_driver *driver);
int ishtp_register_event_cb(struct ishtp_cl_device *device,
			    void (*read_cb)(struct ishtp_cl_device *));

/* Get the device * from ishtp device instance */
struct device *ishtp_device(struct ishtp_cl_device *cl_device);
/* Trace interface for clients */
void *ishtp_trace_callback(struct ishtp_cl_device *cl_device);

#endif /* _INTEL_ISH_CLIENT_IF_H_ */
