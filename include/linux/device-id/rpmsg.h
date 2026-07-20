/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_RPMSG_H
#define LINUX_DEVICE_ID_RPMSG_H

#ifdef __KERNEL__
typedef unsigned long kernel_ulong_t;
#endif

/* rpmsg */

#define RPMSG_NAME_SIZE			32
#define RPMSG_DEVICE_MODALIAS_FMT	"rpmsg:%s"

struct rpmsg_device_id {
	char name[RPMSG_NAME_SIZE];
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_RPMSG_H */
