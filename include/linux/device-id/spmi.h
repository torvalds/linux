/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_SPMI_H
#define LINUX_DEVICE_ID_SPMI_H

#ifdef __KERNEL__
typedef unsigned long kernel_ulong_t;
#endif

#define SPMI_NAME_SIZE	32
#define SPMI_MODULE_PREFIX "spmi:"

struct spmi_device_id {
	char name[SPMI_NAME_SIZE];
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif /* ifndef LINUX_DEVICE_ID_SPMI_H */
