/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Arm Statistical Profiling Extensions (SPE) support
 * Copyright (c) 2017-2018, Arm Ltd.
 */

#ifndef INCLUDE__ARM_SPE_PKT_DECODER_H__
#define INCLUDE__ARM_SPE_PKT_DECODER_H__

#include <stddef.h>
#include <stdint.h>

#define ARM_SPE_PKT_DESC_MAX		256

#define ARM_SPE_NEED_MORE_BYTES		-1
#define ARM_SPE_BAD_PACKET		-2

#define ARM_SPE_PKT_MAX_SZ		16

enum arm_spe_pkt_type {
	ARM_SPE_BAD,
	ARM_SPE_PAD,
	ARM_SPE_END,
	ARM_SPE_TIMESTAMP,
	ARM_SPE_ADDRESS,
	ARM_SPE_COUNTER,
	ARM_SPE_CONTEXT,
	ARM_SPE_OP_TYPE,
	ARM_SPE_EVENTS,
	ARM_SPE_DATA_SOURCE,
};

struct arm_spe_pkt {
	enum arm_spe_pkt_type	type;
	unsigned char		index;
	uint64_t		payload;
};

/* Short header (HEADER0) and extended header (HEADER1) */
#define SPE_HEADER0_PAD				0x0
#define SPE_HEADER0_END				0x1
#define SPE_HEADER0_TIMESTAMP			0x71
/* Mask for event & data source */
#define SPE_HEADER0_MASK1			(GENMASK_ULL(7, 6) | GENMASK_ULL(3, 0))
#define SPE_HEADER0_EVENTS			0x42
#define SPE_HEADER0_SOURCE			0x43
/* Mask for context & operation */
#define SPE_HEADER0_MASK2			GENMASK_ULL(7, 2)
#define SPE_HEADER0_CONTEXT			0x64
#define SPE_HEADER0_OP_TYPE			0x48
/* Mask for extended format */
#define SPE_HEADER0_EXTENDED			0x20
/* Mask for address & counter */
#define SPE_HEADER0_MASK3			GENMASK_ULL(7, 3)
#define SPE_HEADER0_ADDRESS			0xb0
#define SPE_HEADER0_COUNTER			0x98
#define SPE_HEADER1_ALIGNMENT			0x0

#define SPE_HDR_SHORT_INDEX(h)			((h) & GENMASK_ULL(2, 0))
#define SPE_HDR_EXTENDED_INDEX(h0, h1)		(((h0) & GENMASK_ULL(1, 0)) << 3 | \
						 SPE_HDR_SHORT_INDEX(h1))

/* Address packet header */
#define SPE_ADDR_PKT_HDR_INDEX_INS		0x0
#define SPE_ADDR_PKT_HDR_INDEX_BRANCH		0x1
#define SPE_ADDR_PKT_HDR_INDEX_DATA_VIRT	0x2
#define SPE_ADDR_PKT_HDR_INDEX_DATA_PHYS	0x3

/* Address packet payload */
#define SPE_ADDR_PKT_ADDR_BYTE7_SHIFT		56
#define SPE_ADDR_PKT_ADDR_GET_BYTES_0_6(v)	((v) & GENMASK_ULL(55, 0))
#define SPE_ADDR_PKT_ADDR_GET_BYTE_6(v)		(((v) & GENMASK_ULL(55, 48)) >> 48)

#define SPE_ADDR_PKT_GET_NS(v)			(((v) & BIT_ULL(63)) >> 63)
#define SPE_ADDR_PKT_GET_EL(v)			(((v) & GENMASK_ULL(62, 61)) >> 61)

#define SPE_ADDR_PKT_EL0			0
#define SPE_ADDR_PKT_EL1			1
#define SPE_ADDR_PKT_EL2			2
#define SPE_ADDR_PKT_EL3			3

const char *arm_spe_pkt_name(enum arm_spe_pkt_type);

int arm_spe_get_packet(const unsigned char *buf, size_t len,
		       struct arm_spe_pkt *packet);

int arm_spe_pkt_desc(const struct arm_spe_pkt *packet, char *buf, size_t len);
#endif
