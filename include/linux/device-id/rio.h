/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_RIO_H
#define LINUX_DEVICE_ID_RIO_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

/* RapidIO */

#define RIO_ANY_ID	0xffff

/**
 * struct rio_device_id - RIO device identifier
 * @did: RapidIO device ID
 * @vid: RapidIO vendor ID
 * @asm_did: RapidIO assembly device ID
 * @asm_vid: RapidIO assembly vendor ID
 *
 * Identifies a RapidIO device based on both the device/vendor IDs and
 * the assembly device/vendor IDs.
 */
struct rio_device_id {
	__u16 did, vid;
	__u16 asm_did, asm_vid;
};

#endif /* ifndef LINUX_DEVICE_ID_RIO_H */
