/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_PLATFORM_H
#define LINUX_DEVICE_ID_PLATFORM_H

#ifdef __KERNEL__
typedef unsigned long kernel_ulong_t;
#endif

#define PLATFORM_NAME_SIZE	24
#define PLATFORM_MODULE_PREFIX	"platform:"

struct platform_device_id {
	char name[PLATFORM_NAME_SIZE];
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_PLATFORM_H */
