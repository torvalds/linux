/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2021-2022, Microsoft Corporation.
 *
 * Authors:
 *   Beau Belgrave <beaub@linux.microsoft.com>
 */
#ifndef _UAPI_LINUX_USER_EVENTS_H
#define _UAPI_LINUX_USER_EVENTS_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define USER_EVENTS_SYSTEM "user_events"
#define USER_EVENTS_MULTI_SYSTEM "user_events_multi"
#define USER_EVENTS_PREFIX "u:"

/* Create dynamic location entry within a 32-bit value */
#define DYN_LOC(offset, size) ((size) << 16 | (offset))

/* List of supported registration flags */
enum user_reg_flag {
	/* Event will not delete upon last reference closing */
	USER_EVENT_REG_PERSIST		= 1U << 0,

	/* Event will be allowed to have multiple formats */
	USER_EVENT_REG_MULTI_FORMAT	= 1U << 1,

	/* This value or above is currently non-ABI */
	USER_EVENT_REG_MAX		= 1U << 2,
};

/*
 * Describes an event registration and stores the results of the registration.
 * This structure is passed to the DIAG_IOCSREG ioctl, callers at a minimum
 * must set the size and name_args before invocation.
 */
struct user_reg {

	/* Input: Size of the user_reg structure being used */
	__u32	size;

	/* Input: Bit in enable address to use */
	__u8	enable_bit;

	/* Input: Enable size in bytes at address */
	__u8	enable_size;

	/* Input: Flags to use, if any */
	__u16	flags;

	/* Input: Address to update when enabled */
	__u64	enable_addr;

	/* Input: Pointer to string with event name, description and flags */
	__u64	name_args;

	/* Output: Index of the event to use when writing data */
	__u32	write_index;
} __attribute__((__packed__));

/*
 * Describes an event unregister, callers must set the size, address and bit.
 * This structure is passed to the DIAG_IOCSUNREG ioctl to disable bit updates.
 */
struct user_unreg {
	/* Input: Size of the user_unreg structure being used */
	__u32	size;

	/* Input: Bit to unregister */
	__u8	disable_bit;

	/* Input: Reserved, set to 0 */
	__u8	__reserved;

	/* Input: Reserved, set to 0 */
	__u16	__reserved2;

	/* Input: Address to unregister */
	__u64	disable_addr;
} __attribute__((__packed__));

#define DIAG_IOC_MAGIC '*'

/* Request to register a user_event */
#define DIAG_IOCSREG _IOWR(DIAG_IOC_MAGIC, 0, struct user_reg *)

/* Request to delete a user_event */
#define DIAG_IOCSDEL _IOW(DIAG_IOC_MAGIC, 1, char *)

/* Requests to unregister a user_event */
#define DIAG_IOCSUNREG _IOW(DIAG_IOC_MAGIC, 2, struct user_unreg*)

#endif /* _UAPI_LINUX_USER_EVENTS_H */
