/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT */
/*
 * Header file for the io_uring query interface.
 */
#ifndef LINUX_IO_URING_QUERY_H
#define LINUX_IO_URING_QUERY_H

#include <linux/types.h>

struct io_uring_query_hdr {
	__u64 next_entry;
	__u64 query_data;
	__u32 query_op;
	__u32 size;
	__s32 result;
	__u32 __resv[3];
};

enum {
	IO_URING_QUERY_OPCODES			= 0,

	__IO_URING_QUERY_MAX,
};

/* Doesn't require a ring */
struct io_uring_query_opcode {
	/* The number of supported IORING_OP_* opcodes */
	__u32	nr_request_opcodes;
	/* The number of supported IORING_[UN]REGISTER_* opcodes */
	__u32	nr_register_opcodes;
	/* Bitmask of all supported IORING_FEAT_* flags */
	__u64	feature_flags;
	/* Bitmask of all supported IORING_SETUP_* flags */
	__u64	ring_setup_flags;
	/* Bitmask of all supported IORING_ENTER_** flags */
	__u64	enter_flags;
	/* Bitmask of all supported IOSQE_* flags */
	__u64	sqe_flags;
	/* The number of available query opcodes */
	__u32	nr_query_opcodes;
	__u32	__pad;
};

#endif
