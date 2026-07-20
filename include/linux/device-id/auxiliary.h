/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_AUXILIARY_H
#define LINUX_DEVICE_ID_AUXILIARY_H

#ifdef __KERNEL__
typedef unsigned long kernel_ulong_t;
#endif

#define AUXILIARY_NAME_SIZE 40
#define AUXILIARY_MODULE_PREFIX "auxiliary:"

struct auxiliary_device_id {
	char name[AUXILIARY_NAME_SIZE];
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_AUXILIARY_H */
