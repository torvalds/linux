/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_SLIM_H
#define LINUX_DEVICE_ID_SLIM_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

/* SLIMbus */

#define SLIMBUS_NAME_SIZE	32
#define SLIMBUS_MODULE_PREFIX	"slim:"

struct slim_device_id {
	__u16 manf_id, prod_code;
	__u16 dev_index, instance;

	/* Data private to the driver */
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_SLIM_H */
