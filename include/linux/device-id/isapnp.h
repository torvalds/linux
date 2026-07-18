/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_ISAPNP_H
#define LINUX_DEVICE_ID_ISAPNP_H

#ifdef __KERNEL__
typedef unsigned long kernel_ulong_t;
#endif

#define ISAPNP_ANY_ID		0xffff
struct isapnp_device_id {
	unsigned short card_vendor, card_device;
	unsigned short vendor, function;
	kernel_ulong_t driver_data;	/* data private to the driver */
};

#endif /* ifndef LINUX_DEVICE_ID_ISAPNP_H */
