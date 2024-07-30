// SPDX-License-Identifier: GPL-2.0
/*
 * Arm Statistical Profiling Extensions (SPE) support
 * Copyright (c) 2017-2018, Arm Ltd.
 */

#include <stdio.h>
#include <string.h>
#include <endian.h>
#include <byteswap.h>
#include <linux/bitops.h>
#include <stdarg.h>
#include <linux/kernel.h>
#include <asm-generic/unaligned.h>

#include "arm-spe-pkt-decoder.h"

static const char * const arm_spe_packet_name[] = {
	[ARM_SPE_PAD]		= "PAD",
	[ARM_SPE_END]		= "END",
	[ARM_SPE_TIMESTAMP]	= "TS",
	[ARM_SPE_ADDRESS]	= "ADDR",
	[ARM_SPE_COUNTER]	= "LAT",
	[ARM_SPE_CONTEXT]	= "CONTEXT",
	[ARM_SPE_OP_TYPE]	= "OP-TYPE",
	[ARM_SPE_EVENTS]	= "EVENTS",
	[ARM_SPE_DATA_SOURCE]	= "DATA-SOURCE",
};

const char *arm_spe_pkt_name(enum arm_spe_pkt_type type)
{
	return arm_spe_packet_name[type];
}

/*
 * Extracts the field "sz" from header bits and converts to bytes:
 *   00 : byte (1)
 *   01 : halfword (2)
 *   10 : word (4)
 *   11 : doubleword (8)
 */
static unsigned int arm_spe_payload_len(unsigned char hdr)
{
	return 1U << ((hdr & GENMASK_ULL(5, 4)) >> 4);
}

static int arm_spe_get_payload(const unsigned char *buf, size_t len,
			       unsigned char ext_hdr,
			       struct arm_spe_pkt *packet)
{
	size_t payload_len = arm_spe_payload_len(buf[ext_hdr]);

	if (len < 1 + ext_hdr + payload_len)
		return ARM_SPE_NEED_MORE_BYTES;

	buf += 1 + ext_hdr;

	switch (payload_len) {
	case 1: packet->payload = *(uint8_t *)buf; break;
	case 2: packet->payload = get_unaligned_le16(buf); break;
	case 4: packet->payload = get_unaligned_le32(buf); break;
	case 8: packet->payload = get_unaligned_le64(buf); break;
	default: return ARM_SPE_BAD_PACKET;
	}

	return 1 + ext_hdr + payload_len;
}

static int arm_spe_get_pad(struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_PAD;
	return 1;
}

static int arm_spe_get_alignment(const unsigned char *buf, size_t len,
				 struct arm_spe_pkt *packet)
{
	unsigned int alignment = 1 << ((buf[0] & 0xf) + 1);

	if (len < alignment)
		return ARM_SPE_NEED_MORE_BYTES;

	packet->type = ARM_SPE_PAD;
	return alignment - (((uintptr_t)buf) & (alignment - 1));
}

static int arm_spe_get_end(struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_END;
	return 1;
}

static int arm_spe_get_timestamp(const unsigned char *buf, size_t len,
				 struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_TIMESTAMP;
	return arm_spe_get_payload(buf, len, 0, packet);
}

static int arm_spe_get_events(const unsigned char *buf, size_t len,
			      struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_EVENTS;

	/* we use index to identify Events with a less number of
	 * comparisons in arm_spe_pkt_desc(): E.g., the LLC-ACCESS,
	 * LLC-REFILL, and REMOTE-ACCESS events are identified if
	 * index > 1.
	 */
	packet->index = arm_spe_payload_len(buf[0]);

	return arm_spe_get_payload(buf, len, 0, packet);
}

static int arm_spe_get_data_source(const unsigned char *buf, size_t len,
				   struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_DATA_SOURCE;
	return arm_spe_get_payload(buf, len, 0, packet);
}

static int arm_spe_get_context(const unsigned char *buf, size_t len,
			       struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_CONTEXT;
	packet->index = SPE_CTX_PKT_HDR_INDEX(buf[0]);
	return arm_spe_get_payload(buf, len, 0, packet);
}

static int arm_spe_get_op_type(const unsigned char *buf, size_t len,
			       struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_OP_TYPE;
	packet->index = SPE_OP_PKT_HDR_CLASS(buf[0]);
	return arm_spe_get_payload(buf, len, 0, packet);
}

static int arm_spe_get_counter(const unsigned char *buf, size_t len,
			       const unsigned char ext_hdr, struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_COUNTER;

	if (ext_hdr)
		packet->index = SPE_HDR_EXTENDED_INDEX(buf[0], buf[1]);
	else
		packet->index = SPE_HDR_SHORT_INDEX(buf[0]);

	return arm_spe_get_payload(buf, len, ext_hdr, packet);
}

static int arm_spe_get_addr(const unsigned char *buf, size_t len,
			    const unsigned char ext_hdr, struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_ADDRESS;

	if (ext_hdr)
		packet->index = SPE_HDR_EXTENDED_INDEX(buf[0], buf[1]);
	else
		packet->index = SPE_HDR_SHORT_INDEX(buf[0]);

	return arm_spe_get_payload(buf, len, ext_hdr, packet);
}

static int arm_spe_do_get_packet(const unsigned char *buf, size_t len,
				 struct arm_spe_pkt *packet)
{
	unsigned int hdr;
	unsigned char ext_hdr = 0;

	memset(packet, 0, sizeof(struct arm_spe_pkt));

	if (!len)
		return ARM_SPE_NEED_MORE_BYTES;

	hdr = buf[0];

	if (hdr == SPE_HEADER0_PAD)
		return arm_spe_get_pad(packet);

	if (hdr == SPE_HEADER0_END) /* no timestamp at end of record */
		return arm_spe_get_end(packet);

	if (hdr == SPE_HEADER0_TIMESTAMP)
		return arm_spe_get_timestamp(buf, len, packet);

	if ((hdr & SPE_HEADER0_MASK1) == SPE_HEADER0_EVENTS)
		return arm_spe_get_events(buf, len, packet);

	if ((hdr & SPE_HEADER0_MASK1) == SPE_HEADER0_SOURCE)
		return arm_spe_get_data_source(buf, len, packet);

	if ((hdr & SPE_HEADER0_MASK2) == SPE_HEADER0_CONTEXT)
		return arm_spe_get_context(buf, len, packet);

	if ((hdr & SPE_HEADER0_MASK2) == SPE_HEADER0_OP_TYPE)
		return arm_spe_get_op_type(buf, len, packet);

	if ((hdr & SPE_HEADER0_MASK2) == SPE_HEADER0_EXTENDED) {
		/* 16-bit extended format header */
		if (len == 1)
			return ARM_SPE_BAD_PACKET;

		ext_hdr = 1;
		hdr = buf[1];
		if (hdr == SPE_HEADER1_ALIGNMENT)
			return arm_spe_get_alignment(buf, len, packet);
	}

	/*
	 * The short format header's byte 0 or the extended format header's
	 * byte 1 has been assigned to 'hdr', which uses the same encoding for
	 * address packet and counter packet, so don't need to distinguish if
	 * it's short format or extended format and handle in once.
	 */
	if ((hdr & SPE_HEADER0_MASK3) == SPE_HEADER0_ADDRESS)
		return arm_spe_get_addr(buf, len, ext_hdr, packet);

	if ((hdr & SPE_HEADER0_MASK3) == SPE_HEADER0_COUNTER)
		return arm_spe_get_counter(buf, len, ext_hdr, packet);

	return ARM_SPE_BAD_PACKET;
}

int arm_spe_get_packet(const unsigned char *buf, size_t len,
		       struct arm_spe_pkt *packet)
{
	int ret;

	ret = arm_spe_do_get_packet(buf, len, packet);
	/* put multiple consecutive PADs on the same line, up to
	 * the fixed-width output format of 16 bytes per line.
	 */
	if (ret > 0 && packet->type == ARM_SPE_PAD) {
		while (ret < 16 && len > (size_t)ret && !buf[ret])
			ret += 1;
	}
	return ret;
}

static int arm_spe_pkt_out_string(int *err, char **buf_p, size_t *blen,
				  const char *fmt, ...)
{
	va_list ap;
	int ret;

	/* Bail out if any error occurred */
	if (err && *err)
		return *err;

	va_start(ap, fmt);
	ret = vsnprintf(*buf_p, *blen, fmt, ap);
	va_end(ap);

	if (ret < 0) {
		if (err && !*err)
			*err = ret;

	/*
	 * A return value of *blen or more means that the output was
	 * truncated and the buffer is overrun.
	 */
	} else if ((size_t)ret >= *blen) {
		(*buf_p)[*blen - 1] = '\0';

		/*
		 * Set *err to 'ret' to avoid overflow if tries to
		 * fill this buffer sequentially.
		 */
		if (err && !*err)
			*err = ret;
	} else {
		*buf_p += ret;
		*blen -= ret;
	}

	return ret;
}

static int arm_spe_pkt_desc_event(const struct arm_spe_pkt *packet,
				  char *buf, size_t buf_len)
{
	u64 payload = packet->payload;
	int err = 0;

	arm_spe_pkt_out_string(&err, &buf, &buf_len, "EV");

	if (payload & BIT(EV_EXCEPTION_GEN))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " EXCEPTION-GEN");
	if (payload & BIT(EV_RETIRED))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " RETIRED");
	if (payload & BIT(EV_L1D_ACCESS))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " L1D-ACCESS");
	if (payload & BIT(EV_L1D_REFILL))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " L1D-REFILL");
	if (payload & BIT(EV_TLB_ACCESS))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " TLB-ACCESS");
	if (payload & BIT(EV_TLB_WALK))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " TLB-REFILL");
	if (payload & BIT(EV_NOT_TAKEN))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " NOT-TAKEN");
	if (payload & BIT(EV_MISPRED))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " MISPRED");
	if (payload & BIT(EV_LLC_ACCESS))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " LLC-ACCESS");
	if (payload & BIT(EV_LLC_MISS))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " LLC-REFILL");
	if (payload & BIT(EV_REMOTE_ACCESS))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " REMOTE-ACCESS");
	if (payload & BIT(EV_ALIGNMENT))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " ALIGNMENT");
	if (payload & BIT(EV_PARTIAL_PREDICATE))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " SVE-PARTIAL-PRED");
	if (payload & BIT(EV_EMPTY_PREDICATE))
		arm_spe_pkt_out_string(&err, &buf, &buf_len, " SVE-EMPTY-PRED");

	return err;
}

static int arm_spe_pkt_desc_op_type(const struct arm_spe_pkt *packet,
				    char *buf, size_t buf_len)
{
	u64 payload = packet->payload;
	int err = 0;

	switch (packet->index) {
	case SPE_OP_PKT_HDR_CLASS_OTHER:
		if (SPE_OP_PKT_IS_OTHER_SVE_OP(payload)) {
			arm_spe_pkt_out_string(&err, &buf, &buf_len, "SVE-OTHER");

			/* SVE effective vector length */
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " EVLEN %d",
					       SPE_OP_PKG_SVE_EVL(payload));

			if (payload & SPE_OP_PKT_SVE_FP)
				arm_spe_pkt_out_string(&err, &buf, &buf_len, " FP");
			if (payload & SPE_OP_PKT_SVE_PRED)
				arm_spe_pkt_out_string(&err, &buf, &buf_len, " PRED");
		} else {
			arm_spe_pkt_out_string(&err, &buf, &buf_len, "OTHER");
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " %s",
					       payload & SPE_OP_PKT_COND ?
					       "COND-SELECT" : "INSN-OTHER");
		}
		break;
	case SPE_OP_PKT_HDR_CLASS_LD_ST_ATOMIC:
		arm_spe_pkt_out_string(&err, &buf, &buf_len,
				       payload & 0x1 ? "ST" : "LD");

		if (SPE_OP_PKT_IS_LDST_ATOMIC(payload)) {
			if (payload & SPE_OP_PKT_AT)
				arm_spe_pkt_out_string(&err, &buf, &buf_len, " AT");
			if (payload & SPE_OP_PKT_EXCL)
				arm_spe_pkt_out_string(&err, &buf, &buf_len, " EXCL");
			if (payload & SPE_OP_PKT_AR)
				arm_spe_pkt_out_string(&err, &buf, &buf_len, " AR");
		}

		switch (SPE_OP_PKT_LDST_SUBCLASS_GET(payload)) {
		case SPE_OP_PKT_LDST_SUBCLASS_SIMD_FP:
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " SIMD-FP");
			break;
		case SPE_OP_PKT_LDST_SUBCLASS_GP_REG:
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " GP-REG");
			break;
		case SPE_OP_PKT_LDST_SUBCLASS_UNSPEC_REG:
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " UNSPEC-REG");
			break;
		case SPE_OP_PKT_LDST_SUBCLASS_NV_SYSREG:
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " NV-SYSREG");
			break;
		case SPE_OP_PKT_LDST_SUBCLASS_MTE_TAG:
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " MTE-TAG");
			break;
		case SPE_OP_PKT_LDST_SUBCLASS_MEMCPY:
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " MEMCPY");
			break;
		case SPE_OP_PKT_LDST_SUBCLASS_MEMSET:
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " MEMSET");
			break;
		default:
			break;
		}

		if (SPE_OP_PKT_IS_LDST_SVE(payload)) {
			/* SVE effective vector length */
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " EVLEN %d",
					       SPE_OP_PKG_SVE_EVL(payload));

			if (payload & SPE_OP_PKT_SVE_PRED)
				arm_spe_pkt_out_string(&err, &buf, &buf_len, " PRED");
			if (payload & SPE_OP_PKT_SVE_SG)
				arm_spe_pkt_out_string(&err, &buf, &buf_len, " SG");
		}
		break;
	case SPE_OP_PKT_HDR_CLASS_BR_ERET:
		arm_spe_pkt_out_string(&err, &buf, &buf_len, "B");

		if (payload & SPE_OP_PKT_COND)
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " COND");

		if (SPE_OP_PKT_IS_INDIRECT_BRANCH(payload))
			arm_spe_pkt_out_string(&err, &buf, &buf_len, " IND");

		break;
	default:
		/* Unknown index */
		err = -1;
		break;
	}

	return err;
}

static int arm_spe_pkt_desc_addr(const struct arm_spe_pkt *packet,
				 char *buf, size_t buf_len)
{
	int ns, el, idx = packet->index;
	int ch, pat;
	u64 payload = packet->payload;
	int err = 0;
	static const char *idx_name[] = {"PC", "TGT", "VA", "PA", "PBT"};

	switch (idx) {
	case SPE_ADDR_PKT_HDR_INDEX_INS:
	case SPE_ADDR_PKT_HDR_INDEX_BRANCH:
	case SPE_ADDR_PKT_HDR_INDEX_PREV_BRANCH:
		ns = !!SPE_ADDR_PKT_GET_NS(payload);
		el = SPE_ADDR_PKT_GET_EL(payload);
		payload = SPE_ADDR_PKT_ADDR_GET_BYTES_0_6(payload);
		arm_spe_pkt_out_string(&err, &buf, &buf_len,
				"%s 0x%llx el%d ns=%d",
				idx_name[idx], payload, el, ns);
		break;
	case SPE_ADDR_PKT_HDR_INDEX_DATA_VIRT:
		arm_spe_pkt_out_string(&err, &buf, &buf_len,
				       "VA 0x%llx", payload);
		break;
	case SPE_ADDR_PKT_HDR_INDEX_DATA_PHYS:
		ns = !!SPE_ADDR_PKT_GET_NS(payload);
		ch = !!SPE_ADDR_PKT_GET_CH(payload);
		pat = SPE_ADDR_PKT_GET_PAT(payload);
		payload = SPE_ADDR_PKT_ADDR_GET_BYTES_0_6(payload);
		arm_spe_pkt_out_string(&err, &buf, &buf_len,
				       "PA 0x%llx ns=%d ch=%d pat=%x",
				       payload, ns, ch, pat);
		break;
	default:
		/* Unknown index */
		err = -1;
		break;
	}

	return err;
}

static int arm_spe_pkt_desc_counter(const struct arm_spe_pkt *packet,
				    char *buf, size_t buf_len)
{
	u64 payload = packet->payload;
	const char *name = arm_spe_pkt_name(packet->type);
	int err = 0;

	arm_spe_pkt_out_string(&err, &buf, &buf_len, "%s %d ", name,
			       (unsigned short)payload);

	switch (packet->index) {
	case SPE_CNT_PKT_HDR_INDEX_TOTAL_LAT:
		arm_spe_pkt_out_string(&err, &buf, &buf_len, "TOT");
		break;
	case SPE_CNT_PKT_HDR_INDEX_ISSUE_LAT:
		arm_spe_pkt_out_string(&err, &buf, &buf_len, "ISSUE");
		break;
	case SPE_CNT_PKT_HDR_INDEX_TRANS_LAT:
		arm_spe_pkt_out_string(&err, &buf, &buf_len, "XLAT");
		break;
	default:
		break;
	}

	return err;
}

int arm_spe_pkt_desc(const struct arm_spe_pkt *packet, char *buf,
		     size_t buf_len)
{
	int idx = packet->index;
	unsigned long long payload = packet->payload;
	const char *name = arm_spe_pkt_name(packet->type);
	char *buf_orig = buf;
	size_t blen = buf_len;
	int err = 0;

	switch (packet->type) {
	case ARM_SPE_BAD:
	case ARM_SPE_PAD:
	case ARM_SPE_END:
		arm_spe_pkt_out_string(&err, &buf, &blen, "%s", name);
		break;
	case ARM_SPE_EVENTS:
		err = arm_spe_pkt_desc_event(packet, buf, buf_len);
		break;
	case ARM_SPE_OP_TYPE:
		err = arm_spe_pkt_desc_op_type(packet, buf, buf_len);
		break;
	case ARM_SPE_DATA_SOURCE:
	case ARM_SPE_TIMESTAMP:
		arm_spe_pkt_out_string(&err, &buf, &blen, "%s %lld", name, payload);
		break;
	case ARM_SPE_ADDRESS:
		err = arm_spe_pkt_desc_addr(packet, buf, buf_len);
		break;
	case ARM_SPE_CONTEXT:
		arm_spe_pkt_out_string(&err, &buf, &blen, "%s 0x%lx el%d",
				       name, (unsigned long)payload, idx + 1);
		break;
	case ARM_SPE_COUNTER:
		err = arm_spe_pkt_desc_counter(packet, buf, buf_len);
		break;
	default:
		/* Unknown packet type */
		err = -1;
		break;
	}

	/* Output raw data if detect any error */
	if (err) {
		err = 0;
		arm_spe_pkt_out_string(&err, &buf_orig, &buf_len, "%s 0x%llx (%d)",
				       name, payload, packet->index);
	}

	return err;
}
