/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_TYPEC_H
#define LINUX_DEVICE_ID_TYPEC_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

/* USB Type-C Alternate Modes */

#define TYPEC_ANY_MODE	0x7

/**
 * struct typec_device_id - USB Type-C alternate mode identifiers
 * @svid: Standard or Vendor ID
 * @mode: Mode index
 * @driver_data: Driver specific data
 */
struct typec_device_id {
	__u16 svid;
	__u8 mode;
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_TYPEC_H */
