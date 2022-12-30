/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) */
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#ifndef MANA_ABI_USER_H
#define MANA_ABI_USER_H

#include <linux/types.h>
#include <rdma/ib_user_ioctl_verbs.h>

/*
 * Increment this value if any changes that break userspace ABI
 * compatibility are made.
 */

#define MANA_IB_UVERBS_ABI_VERSION 1

struct mana_ib_create_cq {
	__aligned_u64 buf_addr;
};

struct mana_ib_create_qp {
	__aligned_u64 sq_buf_addr;
	__u32 sq_buf_size;
	__u32 port;
};

struct mana_ib_create_qp_resp {
	__u32 sqid;
	__u32 cqid;
	__u32 tx_vp_offset;
	__u32 reserved;
};

struct mana_ib_create_wq {
	__aligned_u64 wq_buf_addr;
	__u32 wq_buf_size;
	__u32 reserved;
};

/* RX Hash function flags */
enum mana_ib_rx_hash_function_flags {
	MANA_IB_RX_HASH_FUNC_TOEPLITZ = 1 << 0,
};

struct mana_ib_create_qp_rss {
	__aligned_u64 rx_hash_fields_mask;
	__u8 rx_hash_function;
	__u8 reserved[7];
	__u32 rx_hash_key_len;
	__u8 rx_hash_key[40];
	__u32 port;
};

struct rss_resp_entry {
	__u32 cqid;
	__u32 wqid;
};

struct mana_ib_create_qp_rss_resp {
	__aligned_u64 num_entries;
	struct rss_resp_entry entries[64];
};

#endif
