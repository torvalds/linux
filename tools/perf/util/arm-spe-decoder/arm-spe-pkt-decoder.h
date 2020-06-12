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

#define SPE_ADDR_PKT_HDR_INDEX_INS		(0x0)
#define SPE_ADDR_PKT_HDR_INDEX_BRANCH		(0x1)
#define SPE_ADDR_PKT_HDR_INDEX_DATA_VIRT	(0x2)
#define SPE_ADDR_PKT_HDR_INDEX_DATA_PHYS	(0x3)

#define SPE_ADDR_PKT_NS				BIT(7)
#define SPE_ADDR_PKT_CH				BIT(6)
#define SPE_ADDR_PKT_EL_OFFSET			(5)
#define SPE_ADDR_PKT_EL_MASK			(0x3 << SPE_ADDR_PKT_EL_OFFSET)
#define SPE_ADDR_PKT_EL0			(0)
#define SPE_ADDR_PKT_EL1			(1)
#define SPE_ADDR_PKT_EL2			(2)
#define SPE_ADDR_PKT_EL3			(3)

const char *arm_spe_pkt_name(enum arm_spe_pkt_type);

int arm_spe_get_packet(const unsigned char *buf, size_t len,
		       struct arm_spe_pkt *packet);

int arm_spe_pkt_desc(const struct arm_spe_pkt *packet, char *buf, size_t len);
#endif
