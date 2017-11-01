/*
 *  User API methods for ACPI-WMI mapping driver
 *
 *  Copyright (C) 2017 Dell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _UAPI_LINUX_WMI_H
#define _UAPI_LINUX_WMI_H

#include <linux/types.h>

/* WMI bus will filter all WMI vendor driver requests through this IOC */
#define WMI_IOC 'W'

/* All ioctl requests through WMI should declare their size followed by
 * relevant data objects
 */
struct wmi_ioctl_buffer {
	__u64	length;
	__u8	data[];
};

#endif
