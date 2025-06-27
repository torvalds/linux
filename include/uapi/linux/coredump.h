/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_LINUX_COREDUMP_H
#define _UAPI_LINUX_COREDUMP_H

#include <linux/types.h>

/**
 * coredump_{req,ack} flags
 * @COREDUMP_KERNEL: kernel writes coredump
 * @COREDUMP_USERSPACE: userspace writes coredump
 * @COREDUMP_REJECT: don't generate coredump
 * @COREDUMP_WAIT: wait for coredump server
 */
enum {
	COREDUMP_KERNEL		= (1ULL << 0),
	COREDUMP_USERSPACE	= (1ULL << 1),
	COREDUMP_REJECT		= (1ULL << 2),
	COREDUMP_WAIT		= (1ULL << 3),
};

/**
 * struct coredump_req - message kernel sends to userspace
 * @size: size of struct coredump_req
 * @size_ack: known size of struct coredump_ack on this kernel
 * @mask: supported features
 *
 * When a coredump happens the kernel will connect to the coredump
 * socket and send a coredump request to the coredump server. The @size
 * member is set to the size of struct coredump_req and provides a hint
 * to userspace how much data can be read. Userspace may use MSG_PEEK to
 * peek the size of struct coredump_req and then choose to consume it in
 * one go. Userspace may also simply read a COREDUMP_ACK_SIZE_VER0
 * request. If the size the kernel sends is larger userspace simply
 * discards any remaining data.
 *
 * The coredump_req->mask member is set to the currently know features.
 * Userspace may only set coredump_ack->mask to the bits raised by the
 * kernel in coredump_req->mask.
 *
 * The coredump_req->size_ack member is set by the kernel to the size of
 * struct coredump_ack the kernel knows. Userspace may only send up to
 * coredump_req->size_ack bytes to the kernel and must set
 * coredump_ack->size accordingly.
 */
struct coredump_req {
	__u32 size;
	__u32 size_ack;
	__u64 mask;
};

enum {
	COREDUMP_REQ_SIZE_VER0 = 16U, /* size of first published struct */
};

/**
 * struct coredump_ack - message userspace sends to kernel
 * @size: size of the struct
 * @spare: unused
 * @mask: features kernel is supposed to use
 *
 * The @size member must be set to the size of struct coredump_ack. It
 * may never exceed what the kernel returned in coredump_req->size_ack
 * but it may of course be smaller (>= COREDUMP_ACK_SIZE_VER0 and <=
 * coredump_req->size_ack).
 *
 * The @mask member must be set to the features the coredump server
 * wants the kernel to use. Only bits the kernel returned in
 * coredump_req->mask may be set.
 */
struct coredump_ack {
	__u32 size;
	__u32 spare;
	__u64 mask;
};

enum {
	COREDUMP_ACK_SIZE_VER0 = 16U, /* size of first published struct */
};

/**
 * enum coredump_mark - Markers for the coredump socket
 *
 * The kernel will place a single byte on the coredump socket. The
 * markers notify userspace whether the coredump ack succeeded or
 * failed.
 *
 * @COREDUMP_MARK_MINSIZE: the provided coredump_ack size was too small
 * @COREDUMP_MARK_MAXSIZE: the provided coredump_ack size was too big
 * @COREDUMP_MARK_UNSUPPORTED: the provided coredump_ack mask was invalid
 * @COREDUMP_MARK_CONFLICTING: the provided coredump_ack mask has conflicting options
 * @COREDUMP_MARK_REQACK: the coredump request and ack was successful
 * @__COREDUMP_MARK_MAX: the maximum coredump mark value
 */
enum coredump_mark {
	COREDUMP_MARK_REQACK		= 0U,
	COREDUMP_MARK_MINSIZE		= 1U,
	COREDUMP_MARK_MAXSIZE		= 2U,
	COREDUMP_MARK_UNSUPPORTED	= 3U,
	COREDUMP_MARK_CONFLICTING	= 4U,
	__COREDUMP_MARK_MAX		= (1U << 31),
};

#endif /* _UAPI_LINUX_COREDUMP_H */
