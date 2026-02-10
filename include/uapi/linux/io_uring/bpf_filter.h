/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT */
/*
 * Header file for the io_uring BPF filters.
 */
#ifndef LINUX_IO_URING_BPF_FILTER_H
#define LINUX_IO_URING_BPF_FILTER_H

#include <linux/types.h>

/*
 * Struct passed to filters.
 */
struct io_uring_bpf_ctx {
	__u64	user_data;
	__u8	opcode;
	__u8	sqe_flags;
	__u8	pdu_size;	/* size of aux data for filter */
	__u8	pad[5];
	union {
		struct {
			__u32	family;
			__u32	type;
			__u32	protocol;
		} socket;
		struct {
			__u64	flags;
			__u64	mode;
			__u64	resolve;
		} open;
	};
};

enum {
	/*
	 * If set, any currently unset opcode will have a deny filter attached
	 */
	IO_URING_BPF_FILTER_DENY_REST	= 1,
};

struct io_uring_bpf_filter {
	__u32	opcode;		/* io_uring opcode to filter */
	__u32	flags;
	__u32	filter_len;	/* number of BPF instructions */
	__u32	resv;
	__u64	filter_ptr;	/* pointer to BPF filter */
	__u64	resv2[5];
};

enum {
	IO_URING_BPF_CMD_FILTER	= 1,
};

struct io_uring_bpf {
	__u16	cmd_type;	/* IO_URING_BPF_* values */
	__u16	cmd_flags;	/* none so far */
	__u32	resv;
	union {
		struct io_uring_bpf_filter	filter;
	};
};

#endif
