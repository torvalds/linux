/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_HV_VMBUS_H
#define LINUX_DEVICE_ID_HV_VMBUS_H

#ifdef __KERNEL__
#include <linux/uuid.h>
typedef unsigned long kernel_ulong_t;
#endif

/*
 * For Hyper-V devices we use the device guid as the id.
 */
struct hv_vmbus_device_id {
	guid_t guid;
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif /* ifndef LINUX_DEVICE_ID_HV_VMBUS_H */
