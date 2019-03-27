/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
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
 *
 * $FreeBSD$
 */

#ifndef IB_PACK_H
#define IB_PACK_H

#include <rdma/ib_verbs.h>

enum {
	IB_LRH_BYTES  = 8,
	IB_ETH_BYTES  = 14,
	IB_VLAN_BYTES = 4,
	IB_GRH_BYTES  = 40,
	IB_IP4_BYTES  = 20,
	IB_UDP_BYTES  = 8,
	IB_BTH_BYTES  = 12,
	IB_DETH_BYTES = 8
};

struct ib_field {
	size_t struct_offset_bytes;
	size_t struct_size_bytes;
	int    offset_words;
	int    offset_bits;
	int    size_bits;
	char  *field_name;
};

#define RESERVED \
	.field_name          = "reserved"

/*
 * This macro cleans up the definitions of constants for BTH opcodes.
 * It is used to define constants such as IB_OPCODE_UD_SEND_ONLY,
 * which becomes IB_OPCODE_UD + IB_OPCODE_SEND_ONLY, and this gives
 * the correct value.
 *
 * In short, user code should use the constants defined using the
 * macro rather than worrying about adding together other constants.
*/
#define IB_OPCODE(transport, op) \
	IB_OPCODE_ ## transport ## _ ## op = \
		IB_OPCODE_ ## transport + IB_OPCODE_ ## op

enum {
	/* transport types -- just used to define real constants */
	IB_OPCODE_RC                                = 0x00,
	IB_OPCODE_UC                                = 0x20,
	IB_OPCODE_RD                                = 0x40,
	IB_OPCODE_UD                                = 0x60,
	/* per IBTA 1.3 vol 1 Table 38, A10.3.2 */
	IB_OPCODE_CNP                               = 0x80,

	/* operations -- just used to define real constants */
	IB_OPCODE_SEND_FIRST                        = 0x00,
	IB_OPCODE_SEND_MIDDLE                       = 0x01,
	IB_OPCODE_SEND_LAST                         = 0x02,
	IB_OPCODE_SEND_LAST_WITH_IMMEDIATE          = 0x03,
	IB_OPCODE_SEND_ONLY                         = 0x04,
	IB_OPCODE_SEND_ONLY_WITH_IMMEDIATE          = 0x05,
	IB_OPCODE_RDMA_WRITE_FIRST                  = 0x06,
	IB_OPCODE_RDMA_WRITE_MIDDLE                 = 0x07,
	IB_OPCODE_RDMA_WRITE_LAST                   = 0x08,
	IB_OPCODE_RDMA_WRITE_LAST_WITH_IMMEDIATE    = 0x09,
	IB_OPCODE_RDMA_WRITE_ONLY                   = 0x0a,
	IB_OPCODE_RDMA_WRITE_ONLY_WITH_IMMEDIATE    = 0x0b,
	IB_OPCODE_RDMA_READ_REQUEST                 = 0x0c,
	IB_OPCODE_RDMA_READ_RESPONSE_FIRST          = 0x0d,
	IB_OPCODE_RDMA_READ_RESPONSE_MIDDLE         = 0x0e,
	IB_OPCODE_RDMA_READ_RESPONSE_LAST           = 0x0f,
	IB_OPCODE_RDMA_READ_RESPONSE_ONLY           = 0x10,
	IB_OPCODE_ACKNOWLEDGE                       = 0x11,
	IB_OPCODE_ATOMIC_ACKNOWLEDGE                = 0x12,
	IB_OPCODE_COMPARE_SWAP                      = 0x13,
	IB_OPCODE_FETCH_ADD                         = 0x14,
	/* opcode 0x15 is reserved */
	IB_OPCODE_SEND_LAST_WITH_INVALIDATE         = 0x16,
	IB_OPCODE_SEND_ONLY_WITH_INVALIDATE         = 0x17,

	/* real constants follow -- see comment about above IB_OPCODE()
	   macro for more details */

	/* RC */
	IB_OPCODE(RC, SEND_FIRST),
	IB_OPCODE(RC, SEND_MIDDLE),
	IB_OPCODE(RC, SEND_LAST),
	IB_OPCODE(RC, SEND_LAST_WITH_IMMEDIATE),
	IB_OPCODE(RC, SEND_ONLY),
	IB_OPCODE(RC, SEND_ONLY_WITH_IMMEDIATE),
	IB_OPCODE(RC, RDMA_WRITE_FIRST),
	IB_OPCODE(RC, RDMA_WRITE_MIDDLE),
	IB_OPCODE(RC, RDMA_WRITE_LAST),
	IB_OPCODE(RC, RDMA_WRITE_LAST_WITH_IMMEDIATE),
	IB_OPCODE(RC, RDMA_WRITE_ONLY),
	IB_OPCODE(RC, RDMA_WRITE_ONLY_WITH_IMMEDIATE),
	IB_OPCODE(RC, RDMA_READ_REQUEST),
	IB_OPCODE(RC, RDMA_READ_RESPONSE_FIRST),
	IB_OPCODE(RC, RDMA_READ_RESPONSE_MIDDLE),
	IB_OPCODE(RC, RDMA_READ_RESPONSE_LAST),
	IB_OPCODE(RC, RDMA_READ_RESPONSE_ONLY),
	IB_OPCODE(RC, ACKNOWLEDGE),
	IB_OPCODE(RC, ATOMIC_ACKNOWLEDGE),
	IB_OPCODE(RC, COMPARE_SWAP),
	IB_OPCODE(RC, FETCH_ADD),
	IB_OPCODE(RC, SEND_LAST_WITH_INVALIDATE),
	IB_OPCODE(RC, SEND_ONLY_WITH_INVALIDATE),

	/* UC */
	IB_OPCODE(UC, SEND_FIRST),
	IB_OPCODE(UC, SEND_MIDDLE),
	IB_OPCODE(UC, SEND_LAST),
	IB_OPCODE(UC, SEND_LAST_WITH_IMMEDIATE),
	IB_OPCODE(UC, SEND_ONLY),
	IB_OPCODE(UC, SEND_ONLY_WITH_IMMEDIATE),
	IB_OPCODE(UC, RDMA_WRITE_FIRST),
	IB_OPCODE(UC, RDMA_WRITE_MIDDLE),
	IB_OPCODE(UC, RDMA_WRITE_LAST),
	IB_OPCODE(UC, RDMA_WRITE_LAST_WITH_IMMEDIATE),
	IB_OPCODE(UC, RDMA_WRITE_ONLY),
	IB_OPCODE(UC, RDMA_WRITE_ONLY_WITH_IMMEDIATE),

	/* RD */
	IB_OPCODE(RD, SEND_FIRST),
	IB_OPCODE(RD, SEND_MIDDLE),
	IB_OPCODE(RD, SEND_LAST),
	IB_OPCODE(RD, SEND_LAST_WITH_IMMEDIATE),
	IB_OPCODE(RD, SEND_ONLY),
	IB_OPCODE(RD, SEND_ONLY_WITH_IMMEDIATE),
	IB_OPCODE(RD, RDMA_WRITE_FIRST),
	IB_OPCODE(RD, RDMA_WRITE_MIDDLE),
	IB_OPCODE(RD, RDMA_WRITE_LAST),
	IB_OPCODE(RD, RDMA_WRITE_LAST_WITH_IMMEDIATE),
	IB_OPCODE(RD, RDMA_WRITE_ONLY),
	IB_OPCODE(RD, RDMA_WRITE_ONLY_WITH_IMMEDIATE),
	IB_OPCODE(RD, RDMA_READ_REQUEST),
	IB_OPCODE(RD, RDMA_READ_RESPONSE_FIRST),
	IB_OPCODE(RD, RDMA_READ_RESPONSE_MIDDLE),
	IB_OPCODE(RD, RDMA_READ_RESPONSE_LAST),
	IB_OPCODE(RD, RDMA_READ_RESPONSE_ONLY),
	IB_OPCODE(RD, ACKNOWLEDGE),
	IB_OPCODE(RD, ATOMIC_ACKNOWLEDGE),
	IB_OPCODE(RD, COMPARE_SWAP),
	IB_OPCODE(RD, FETCH_ADD),

	/* UD */
	IB_OPCODE(UD, SEND_ONLY),
	IB_OPCODE(UD, SEND_ONLY_WITH_IMMEDIATE)
};

enum {
	IB_LNH_RAW        = 0,
	IB_LNH_IP         = 1,
	IB_LNH_IBA_LOCAL  = 2,
	IB_LNH_IBA_GLOBAL = 3
};

struct ib_unpacked_lrh {
	u8        virtual_lane;
	u8        link_version;
	u8        service_level;
	u8        link_next_header;
	__be16    destination_lid;
	__be16    packet_length;
	__be16    source_lid;
};

struct ib_unpacked_grh {
	u8    	     ip_version;
	u8    	     traffic_class;
	__be32 	     flow_label;
	__be16       payload_length;
	u8    	     next_header;
	u8    	     hop_limit;
	union ib_gid source_gid;
	union ib_gid destination_gid;
};

struct ib_unpacked_bth {
	u8           opcode;
	u8           solicited_event;
	u8           mig_req;
	u8           pad_count;
	u8           transport_header_version;
	__be16       pkey;
	__be32       destination_qpn;
	u8           ack_req;
	__be32       psn;
};

struct ib_unpacked_deth {
	__be32       qkey;
	__be32       source_qpn;
};

struct ib_unpacked_eth {
	u8	dmac_h[4];
	u8	dmac_l[2];
	u8	smac_h[2];
	u8	smac_l[4];
	__be16	type;
};

struct ib_unpacked_ip4 {
	u8	ver;
	u8	hdr_len;
	u8	tos;
	__be16	tot_len;
	__be16	id;
	__be16	frag_off;
	u8	ttl;
	u8	protocol;
	__sum16	check;
	__be32	saddr;
	__be32	daddr;
};

struct ib_unpacked_udp {
	__be16	sport;
	__be16	dport;
	__be16	length;
	__be16	csum;
};

struct ib_unpacked_vlan {
	__be16  tag;
	__be16  type;
};

struct ib_ud_header {
	int                     lrh_present;
	struct ib_unpacked_lrh  lrh;
	int			eth_present;
	struct ib_unpacked_eth	eth;
	int                     vlan_present;
	struct ib_unpacked_vlan vlan;
	int			grh_present;
	struct ib_unpacked_grh	grh;
	int			ipv4_present;
	struct ib_unpacked_ip4	ip4;
	int			udp_present;
	struct ib_unpacked_udp	udp;
	struct ib_unpacked_bth	bth;
	struct ib_unpacked_deth deth;
	int			immediate_present;
	__be32			immediate_data;
};

void ib_pack(const struct ib_field        *desc,
	     int                           desc_len,
	     void                         *structure,
	     void                         *buf);

void ib_unpack(const struct ib_field        *desc,
	       int                           desc_len,
	       void                         *buf,
	       void                         *structure);

__sum16 ib_ud_ip4_csum(struct ib_ud_header *header);

int ib_ud_header_init(int		    payload_bytes,
		      int		    lrh_present,
		      int		    eth_present,
		      int		    vlan_present,
		      int		    grh_present,
		      int		    ip_version,
		      int		    udp_present,
		      int		    immediate_present,
		      struct ib_ud_header *header);

int ib_ud_header_pack(struct ib_ud_header *header,
		      void                *buf);

int ib_ud_header_unpack(void                *buf,
			struct ib_ud_header *header);

#endif /* IB_PACK_H */
