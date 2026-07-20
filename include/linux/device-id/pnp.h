/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_PNP_H
#define LINUX_DEVICE_ID_PNP_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

#define PNP_ID_LEN	8
#define PNP_MAX_DEVICES	8

struct pnp_device_id {
	__u8 id[PNP_ID_LEN];
	kernel_ulong_t driver_data;
};

struct pnp_card_device_id {
	__u8 id[PNP_ID_LEN];
	kernel_ulong_t driver_data;
	struct {
		__u8 id[PNP_ID_LEN];
	} devs[PNP_MAX_DEVICES];
};

#endif /* ifndef LINUX_DEVICE_ID_PNP_H */
