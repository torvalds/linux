/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_ISHTP_H
#define LINUX_DEVICE_ID_ISHTP_H

#ifdef __KERNEL__
#include <linux/uuid.h>
typedef unsigned long kernel_ulong_t;
#endif

/* ISHTP (Integrated Sensor Hub Transport Protocol) */

#define ISHTP_MODULE_PREFIX	"ishtp:"

/**
 * struct ishtp_device_id - ISHTP device identifier
 * @guid: GUID of the device.
 * @driver_data: pointer to driver specific data
 */
struct ishtp_device_id {
	guid_t guid;
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_ISHTP_H */
