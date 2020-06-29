// SPDX-License-Identifier: GPL-2.0
/*
 * arm_spe_decoder.c: ARM SPE support
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include <linux/zalloc.h>

#include "../auxtrace.h"
#include "../debug.h"
#include "../util.h"

#include "arm-spe-decoder.h"

#ifndef BIT
#define BIT(n)		(1UL << (n))
#endif

static u64 arm_spe_calc_ip(int index, u64 payload)
{
	u8 *addr = (u8 *)&payload;
	int ns, el;

	/* Instruction virtual address or Branch target address */
	if (index == SPE_ADDR_PKT_HDR_INDEX_INS ||
	    index == SPE_ADDR_PKT_HDR_INDEX_BRANCH) {
		ns = addr[7] & SPE_ADDR_PKT_NS;
		el = (addr[7] & SPE_ADDR_PKT_EL_MASK) >> SPE_ADDR_PKT_EL_OFFSET;

		/* Fill highest byte for EL1 or EL2 (VHE) mode */
		if (ns && (el == SPE_ADDR_PKT_EL1 || el == SPE_ADDR_PKT_EL2))
			addr[7] = 0xff;
		/* Clean highest byte for other cases */
		else
			addr[7] = 0x0;

	/* Data access virtual address */
	} else if (index == SPE_ADDR_PKT_HDR_INDEX_DATA_VIRT) {

		/* Fill highest byte if bits [48..55] is 0xff */
		if (addr[6] == 0xff)
			addr[7] = 0xff;
		/* Otherwise, cleanup tags */
		else
			addr[7] = 0x0;

	/* Data access physical address */
	} else if (index == SPE_ADDR_PKT_HDR_INDEX_DATA_PHYS) {
		/* Cleanup byte 7 */
		addr[7] = 0x0;
	} else {
		pr_err("unsupported address packet index: 0x%x\n", index);
	}

	return payload;
}

struct arm_spe_decoder *arm_spe_decoder_new(struct arm_spe_params *params)
{
	struct arm_spe_decoder *decoder;

	if (!params->get_trace)
		return NULL;

	decoder = zalloc(sizeof(struct arm_spe_decoder));
	if (!decoder)
		return NULL;

	decoder->get_trace = params->get_trace;
	decoder->data = params->data;

	return decoder;
}

void arm_spe_decoder_free(struct arm_spe_decoder *decoder)
{
	free(decoder);
}

static int arm_spe_get_data(struct arm_spe_decoder *decoder)
{
	struct arm_spe_buffer buffer = { .buf = 0, };
	int ret;

	pr_debug("Getting more data\n");
	ret = decoder->get_trace(&buffer, decoder->data);
	if (ret < 0)
		return ret;

	decoder->buf = buffer.buf;
	decoder->len = buffer.len;

	if (!decoder->len)
		pr_debug("No more data\n");

	return decoder->len;
}

static int arm_spe_get_next_packet(struct arm_spe_decoder *decoder)
{
	int ret;

	do {
		if (!decoder->len) {
			ret = arm_spe_get_data(decoder);

			/* Failed to read out trace data */
			if (ret <= 0)
				return ret;
		}

		ret = arm_spe_get_packet(decoder->buf, decoder->len,
					 &decoder->packet);
		if (ret <= 0) {
			/* Move forward for 1 byte */
			decoder->buf += 1;
			decoder->len -= 1;
			return -EBADMSG;
		}

		decoder->buf += ret;
		decoder->len -= ret;
	} while (decoder->packet.type == ARM_SPE_PAD);

	return 1;
}

static int arm_spe_read_record(struct arm_spe_decoder *decoder)
{
	int err;
	int idx;
	u64 payload, ip;

	memset(&decoder->record, 0x0, sizeof(decoder->record));

	while (1) {
		err = arm_spe_get_next_packet(decoder);
		if (err <= 0)
			return err;

		idx = decoder->packet.index;
		payload = decoder->packet.payload;

		switch (decoder->packet.type) {
		case ARM_SPE_TIMESTAMP:
			decoder->record.timestamp = payload;
			return 1;
		case ARM_SPE_END:
			return 1;
		case ARM_SPE_ADDRESS:
			ip = arm_spe_calc_ip(idx, payload);
			if (idx == SPE_ADDR_PKT_HDR_INDEX_INS)
				decoder->record.from_ip = ip;
			else if (idx == SPE_ADDR_PKT_HDR_INDEX_BRANCH)
				decoder->record.to_ip = ip;
			break;
		case ARM_SPE_COUNTER:
			break;
		case ARM_SPE_CONTEXT:
			break;
		case ARM_SPE_OP_TYPE:
			break;
		case ARM_SPE_EVENTS:
			if (payload & BIT(EV_L1D_REFILL))
				decoder->record.type |= ARM_SPE_L1D_MISS;

			if (payload & BIT(EV_L1D_ACCESS))
				decoder->record.type |= ARM_SPE_L1D_ACCESS;

			if (payload & BIT(EV_TLB_WALK))
				decoder->record.type |= ARM_SPE_TLB_MISS;

			if (payload & BIT(EV_TLB_ACCESS))
				decoder->record.type |= ARM_SPE_TLB_ACCESS;

			if ((idx == 1 || idx == 2 || idx == 3) &&
			    (payload & BIT(EV_LLC_MISS)))
				decoder->record.type |= ARM_SPE_LLC_MISS;

			if ((idx == 1 || idx == 2 || idx == 3) &&
			    (payload & BIT(EV_LLC_ACCESS)))
				decoder->record.type |= ARM_SPE_LLC_ACCESS;

			if ((idx == 1 || idx == 2 || idx == 3) &&
			    (payload & BIT(EV_REMOTE_ACCESS)))
				decoder->record.type |= ARM_SPE_REMOTE_ACCESS;

			if (payload & BIT(EV_MISPRED))
				decoder->record.type |= ARM_SPE_BRANCH_MISS;

			break;
		case ARM_SPE_DATA_SOURCE:
			break;
		case ARM_SPE_BAD:
			break;
		case ARM_SPE_PAD:
			break;
		default:
			pr_err("Get packet error!\n");
			return -1;
		}
	}

	return 0;
}

int arm_spe_decode(struct arm_spe_decoder *decoder)
{
	return arm_spe_read_record(decoder);
}
