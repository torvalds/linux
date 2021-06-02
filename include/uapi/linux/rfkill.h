/*
 * Copyright (C) 2006 - 2007 Ivo van Doorn
 * Copyright (C) 2007 Dmitry Torokhov
 * Copyright 2009 Johannes Berg <johannes@sipsolutions.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef _UAPI__RFKILL_H
#define _UAPI__RFKILL_H


#include <linux/types.h>

/* define userspace visible states */
#define RFKILL_STATE_SOFT_BLOCKED	0
#define RFKILL_STATE_UNBLOCKED		1
#define RFKILL_STATE_HARD_BLOCKED	2

/**
 * enum rfkill_type - type of rfkill switch.
 *
 * @RFKILL_TYPE_ALL: toggles all switches (requests only - not a switch type)
 * @RFKILL_TYPE_WLAN: switch is on a 802.11 wireless network device.
 * @RFKILL_TYPE_BLUETOOTH: switch is on a bluetooth device.
 * @RFKILL_TYPE_UWB: switch is on a ultra wideband device.
 * @RFKILL_TYPE_WIMAX: switch is on a WiMAX device.
 * @RFKILL_TYPE_WWAN: switch is on a wireless WAN device.
 * @RFKILL_TYPE_GPS: switch is on a GPS device.
 * @RFKILL_TYPE_FM: switch is on a FM radio device.
 * @RFKILL_TYPE_NFC: switch is on an NFC device.
 * @NUM_RFKILL_TYPES: number of defined rfkill types
 */
enum rfkill_type {
	RFKILL_TYPE_ALL = 0,
	RFKILL_TYPE_WLAN,
	RFKILL_TYPE_BLUETOOTH,
	RFKILL_TYPE_UWB,
	RFKILL_TYPE_WIMAX,
	RFKILL_TYPE_WWAN,
	RFKILL_TYPE_GPS,
	RFKILL_TYPE_FM,
	RFKILL_TYPE_NFC,
	NUM_RFKILL_TYPES,
};

/**
 * enum rfkill_operation - operation types
 * @RFKILL_OP_ADD: a device was added
 * @RFKILL_OP_DEL: a device was removed
 * @RFKILL_OP_CHANGE: a device's state changed -- userspace changes one device
 * @RFKILL_OP_CHANGE_ALL: userspace changes all devices (of a type, or all)
 *	into a state, also updating the default state used for devices that
 *	are hot-plugged later.
 */
enum rfkill_operation {
	RFKILL_OP_ADD = 0,
	RFKILL_OP_DEL,
	RFKILL_OP_CHANGE,
	RFKILL_OP_CHANGE_ALL,
};

/**
 * enum rfkill_hard_block_reasons - hard block reasons
 * @RFKILL_HARD_BLOCK_SIGNAL: the hardware rfkill signal is active
 * @RFKILL_HARD_BLOCK_NOT_OWNER: the NIC is not owned by the host
 */
enum rfkill_hard_block_reasons {
	RFKILL_HARD_BLOCK_SIGNAL	= 1 << 0,
	RFKILL_HARD_BLOCK_NOT_OWNER	= 1 << 1,
};

/**
 * struct rfkill_event - events for userspace on /dev/rfkill
 * @idx: index of dev rfkill
 * @type: type of the rfkill struct
 * @op: operation code
 * @hard: hard state (0/1)
 * @soft: soft state (0/1)
 *
 * Structure used for userspace communication on /dev/rfkill,
 * used for events from the kernel and control to the kernel.
 */
struct rfkill_event {
	__u32 idx;
	__u8  type;
	__u8  op;
	__u8  soft;
	__u8  hard;
} __attribute__((packed));

/**
 * struct rfkill_event_ext - events for userspace on /dev/rfkill
 * @idx: index of dev rfkill
 * @type: type of the rfkill struct
 * @op: operation code
 * @hard: hard state (0/1)
 * @soft: soft state (0/1)
 * @hard_block_reasons: valid if hard is set. One or several reasons from
 *	&enum rfkill_hard_block_reasons.
 *
 * Structure used for userspace communication on /dev/rfkill,
 * used for events from the kernel and control to the kernel.
 *
 * See the extensibility docs below.
 */
struct rfkill_event_ext {
	__u32 idx;
	__u8  type;
	__u8  op;
	__u8  soft;
	__u8  hard;

	/*
	 * older kernels will accept/send only up to this point,
	 * and if extended further up to any chunk marked below
	 */

	__u8  hard_block_reasons;
} __attribute__((packed));

/**
 * DOC: Extensibility
 *
 * Originally, we had planned to allow backward and forward compatible
 * changes by just adding fields at the end of the structure that are
 * then not reported on older kernels on read(), and not written to by
 * older kernels on write(), with the kernel reporting the size it did
 * accept as the result.
 *
 * This would have allowed userspace to detect on read() and write()
 * which kernel structure version it was dealing with, and if was just
 * recompiled it would have gotten the new fields, but obviously not
 * accessed them, but things should've continued to work.
 *
 * Unfortunately, while actually exercising this mechanism to add the
 * hard block reasons field, we found that userspace (notably systemd)
 * did all kinds of fun things not in line with this scheme:
 *
 * 1. treat the (expected) short writes as an error;
 * 2. ask to read sizeof(struct rfkill_event) but then compare the
 *    actual return value to RFKILL_EVENT_SIZE_V1 and treat any
 *    mismatch as an error.
 *
 * As a consequence, just recompiling with a new struct version caused
 * things to no longer work correctly on old and new kernels.
 *
 * Hence, we've rolled back &struct rfkill_event to the original version
 * and added &struct rfkill_event_ext. This effectively reverts to the
 * old behaviour for all userspace, unless it explicitly opts in to the
 * rules outlined here by using the new &struct rfkill_event_ext.
 *
 * Userspace using &struct rfkill_event_ext must adhere to the following
 * rules
 *
 * 1. accept short writes, optionally using them to detect that it's
 *    running on an older kernel;
 * 2. accept short reads, knowing that this means it's running on an
 *    older kernel;
 * 3. treat reads that are as long as requested as acceptable, not
 *    checking against RFKILL_EVENT_SIZE_V1 or such.
 */
#define RFKILL_EVENT_SIZE_V1	sizeof(struct rfkill_event)

/* ioctl for turning off rfkill-input (if present) */
#define RFKILL_IOC_MAGIC	'R'
#define RFKILL_IOC_NOINPUT	1
#define RFKILL_IOCTL_NOINPUT	_IO(RFKILL_IOC_MAGIC, RFKILL_IOC_NOINPUT)

/* and that's all userspace gets */

#endif /* _UAPI__RFKILL_H */
