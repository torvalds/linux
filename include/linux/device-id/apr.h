/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_APR_H
#define LINUX_DEVICE_ID_APR_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

#define APR_NAME_SIZE	32
#define APR_MODULE_PREFIX "apr:"

struct apr_device_id {
	char name[APR_NAME_SIZE];
	__u32 domain_id;
	__u32 svc_id;
	__u32 svc_version;
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif /* ifndef LINUX_DEVICE_ID_APR_H */
