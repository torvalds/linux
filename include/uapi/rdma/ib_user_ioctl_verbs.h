/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
 * Copyright (c) 2017-2018, Mellanox Technologies inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef IB_USER_IOCTL_VERBS_H
#define IB_USER_IOCTL_VERBS_H

#include <linux/types.h>

#ifndef RDMA_UAPI_PTR
#define RDMA_UAPI_PTR(_type, _name)	__aligned_u64 _name
#endif

enum ib_uverbs_flow_action_esp_keymat {
	IB_UVERBS_FLOW_ACTION_ESP_KEYMAT_AES_GCM,
};

enum ib_uverbs_flow_action_esp_keymat_aes_gcm_iv_algo {
	IB_UVERBS_FLOW_ACTION_IV_ALGO_SEQ,
};

struct ib_uverbs_flow_action_esp_keymat_aes_gcm {
	__aligned_u64	iv;
	__u32		iv_algo; /* Use enum ib_uverbs_flow_action_esp_keymat_aes_gcm_iv_algo */

	__u32		salt;
	__u32		icv_len;

	__u32		key_len;
	__u32		aes_key[256 / 32];
};

enum ib_uverbs_flow_action_esp_replay {
	IB_UVERBS_FLOW_ACTION_ESP_REPLAY_NONE,
	IB_UVERBS_FLOW_ACTION_ESP_REPLAY_BMP,
};

struct ib_uverbs_flow_action_esp_replay_bmp {
	__u32	size;
};

enum ib_uverbs_flow_action_esp_flags {
	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_INLINE_CRYPTO	= 0UL << 0,	/* Default */
	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_FULL_OFFLOAD	= 1UL << 0,

	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_TUNNEL		= 0UL << 1,	/* Default */
	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_TRANSPORT	= 1UL << 1,

	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_DECRYPT		= 0UL << 2,	/* Default */
	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ENCRYPT		= 1UL << 2,

	IB_UVERBS_FLOW_ACTION_ESP_FLAGS_ESN_NEW_WINDOW	= 1UL << 3,
};

struct ib_uverbs_flow_action_esp_encap {
	/* This struct represents a list of pointers to flow_xxxx_filter that
	 * encapsulates the payload in ESP tunnel mode.
	 */
	RDMA_UAPI_PTR(void *, val_ptr); /* pointer to a flow_xxxx_filter */
	RDMA_UAPI_PTR(struct ib_uverbs_flow_action_esp_encap *, next_ptr);
	__u16	len;		/* Len of the filter struct val_ptr points to */
	__u16	type;		/* Use flow_spec_type enum */
};

struct ib_uverbs_flow_action_esp {
	__u32		spi;
	__u32		seq;
	__u32		tfc_pad;
	__u32		flags;
	__aligned_u64	hard_limit_pkts;
};

#endif
