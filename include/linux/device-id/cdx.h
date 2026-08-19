/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_CDX_H
#define LINUX_DEVICE_ID_CDX_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define CDX_ANY_ID (0xFFFF)

enum {
	CDX_ID_F_VFIO_DRIVER_OVERRIDE = 1,
};

/**
 * struct cdx_device_id - CDX device identifier
 * @vendor: Vendor ID
 * @device: Device ID
 * @subvendor: Subsystem vendor ID (or CDX_ANY_ID)
 * @subdevice: Subsystem device ID (or CDX_ANY_ID)
 * @class: Device class
 *         Most drivers do not need to specify class/class_mask
 *         as vendor/device is normally sufficient.
 * @class_mask: Limit which sub-fields of the class field are compared.
 * @override_only: Match only when dev->driver_override is this driver.
 *
 * Type of entries in the "device Id" table for CDX devices supported by
 * a CDX device driver.
 */
struct cdx_device_id {
	__u16 vendor;
	__u16 device;
	__u16 subvendor;
	__u16 subdevice;
	__u32 class;
	__u32 class_mask;
	__u32 override_only;
};

#endif /* ifndef LINUX_DEVICE_ID_CDX_H */
