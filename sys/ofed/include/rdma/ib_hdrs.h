/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef IB_HDRS_H
#define IB_HDRS_H

#include <linux/types.h>
#include <asm/unaligned.h>
#include <rdma/ib_verbs.h>

#define IB_SEQ_NAK	(3 << 29)

/* AETH NAK opcode values */
#define IB_RNR_NAK                      0x20
#define IB_NAK_PSN_ERROR                0x60
#define IB_NAK_INVALID_REQUEST          0x61
#define IB_NAK_REMOTE_ACCESS_ERROR      0x62
#define IB_NAK_REMOTE_OPERATIONAL_ERROR 0x63
#define IB_NAK_INVALID_RD_REQUEST       0x64

#define IB_BTH_REQ_ACK		BIT(31)
#define IB_BTH_SOLICITED	BIT(23)
#define IB_BTH_MIG_REQ		BIT(22)

#define IB_GRH_VERSION		6
#define IB_GRH_VERSION_MASK	0xF
#define IB_GRH_VERSION_SHIFT	28
#define IB_GRH_TCLASS_MASK	0xFF
#define IB_GRH_TCLASS_SHIFT	20
#define IB_GRH_FLOW_MASK	0xFFFFF
#define IB_GRH_FLOW_SHIFT	0
#define IB_GRH_NEXT_HDR		0x1B

struct ib_reth {
	__be64 vaddr;        /* potentially unaligned */
	__be32 rkey;
	__be32 length;
} __packed;

struct ib_atomic_eth {
	__be64 vaddr;        /* potentially unaligned */
	__be32 rkey;
	__be64 swap_data;    /* potentially unaligned */
	__be64 compare_data; /* potentially unaligned */
} __packed;

union ib_ehdrs {
	struct {
		__be32 deth[2];
		__be32 imm_data;
	} ud;
	struct {
		struct ib_reth reth;
		__be32 imm_data;
	} rc;
	struct {
		__be32 aeth;
		__be64 atomic_ack_eth; /* potentially unaligned */
	} __packed at;
	__be32 imm_data;
	__be32 aeth;
	__be32 ieth;
	struct ib_atomic_eth atomic_eth;
}  __packed;

struct ib_other_headers {
	__be32 bth[3];
	union ib_ehdrs u;
} __packed;

struct ib_header {
	__be16 lrh[4];
	union {
		struct {
			struct ib_grh grh;
			struct ib_other_headers oth;
		} l;
		struct ib_other_headers oth;
	} u;
} __packed;

/* accessors for unaligned __be64 items */

static inline u64 ib_u64_get(__be64 *p)
{
	return get_unaligned_be64(p);
}

static inline void ib_u64_put(u64 val, __be64 *p)
{
	put_unaligned_be64(val, p);
}

static inline u64 get_ib_reth_vaddr(struct ib_reth *reth)
{
	return ib_u64_get(&reth->vaddr);
}

static inline void put_ib_reth_vaddr(u64 val, struct ib_reth *reth)
{
	ib_u64_put(val, &reth->vaddr);
}

static inline u64 get_ib_ateth_vaddr(struct ib_atomic_eth *ateth)
{
	return ib_u64_get(&ateth->vaddr);
}

static inline void put_ib_ateth_vaddr(u64 val, struct ib_atomic_eth *ateth)
{
	ib_u64_put(val, &ateth->vaddr);
}

static inline u64 get_ib_ateth_swap(struct ib_atomic_eth *ateth)
{
	return ib_u64_get(&ateth->swap_data);
}

static inline void put_ib_ateth_swap(u64 val, struct ib_atomic_eth *ateth)
{
	ib_u64_put(val, &ateth->swap_data);
}

static inline u64 get_ib_ateth_compare(struct ib_atomic_eth *ateth)
{
	return ib_u64_get(&ateth->compare_data);
}

static inline void put_ib_ateth_compare(u64 val, struct ib_atomic_eth *ateth)
{
	ib_u64_put(val, &ateth->compare_data);
}

#endif                          /* IB_HDRS_H */
