// SPDX-License-Identifier: GPL-2.0
/*
 * Arm Statistical Profiling Extensions (SPE) support
 * Copyright (c) 2017-2018, Arm Ltd.
 */

#include <stdio.h>
#include <string.h>
#include <endian.h>
#include <byteswap.h>

#include "arm-spe-pkt-decoder.h"

#define BIT(n)		(1ULL << (n))

#define NS_FLAG		BIT(63)
#define EL_FLAG		(BIT(62) | BIT(61))

#define SPE_HEADER0_PAD			0x0
#define SPE_HEADER0_END			0x1
#define SPE_HEADER0_ADDRESS		0x30 /* address packet (short) */
#define SPE_HEADER0_ADDRESS_MASK	0x38
#define SPE_HEADER0_COUNTER		0x18 /* counter packet (short) */
#define SPE_HEADER0_COUNTER_MASK	0x38
#define SPE_HEADER0_TIMESTAMP		0x71
#define SPE_HEADER0_TIMESTAMP		0x71
#define SPE_HEADER0_EVENTS		0x2
#define SPE_HEADER0_EVENTS_MASK		0xf
#define SPE_HEADER0_SOURCE		0x3
#define SPE_HEADER0_SOURCE_MASK		0xf
#define SPE_HEADER0_CONTEXT		0x24
#define SPE_HEADER0_CONTEXT_MASK	0x3c
#define SPE_HEADER0_OP_TYPE		0x8
#define SPE_HEADER0_OP_TYPE_MASK	0x3c
#define SPE_HEADER1_ALIGNMENT		0x0
#define SPE_HEADER1_ADDRESS		0xb0 /* address packet (extended) */
#define SPE_HEADER1_ADDRESS_MASK	0xf8
#define SPE_HEADER1_COUNTER		0x98 /* counter packet (extended) */
#define SPE_HEADER1_COUNTER_MASK	0xf8

#if __BYTE_ORDER == __BIG_ENDIAN
#define le16_to_cpu bswap_16
#define le32_to_cpu bswap_32
#define le64_to_cpu bswap_64
#define memcpy_le64(d, s, n) do { \
	memcpy((d), (s), (n));    \
	*(d) = le64_to_cpu(*(d)); \
} while (0)
#else
#define le16_to_cpu
#define le32_to_cpu
#define le64_to_cpu
#define memcpy_le64 memcpy
#endif

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

/* return ARM SPE payload size from its encoding,
 * which is in bits 5:4 of the byte.
 * 00 : byte
 * 01 : halfword (2)
 * 10 : word (4)
 * 11 : doubleword (8)
 */
static int payloadlen(unsigned char byte)
{
	return 1 << ((byte & 0x30) >> 4);
}

static int arm_spe_get_payload(const unsigned char *buf, size_t len,
			       struct arm_spe_pkt *packet)
{
	size_t payload_len = payloadlen(buf[0]);

	if (len < 1 + payload_len)
		return ARM_SPE_NEED_MORE_BYTES;

	buf++;

	switch (payload_len) {
	case 1: packet->payload = *(uint8_t *)buf; break;
	case 2: packet->payload = le16_to_cpu(*(uint16_t *)buf); break;
	case 4: packet->payload = le32_to_cpu(*(uint32_t *)buf); break;
	case 8: packet->payload = le64_to_cpu(*(uint64_t *)buf); break;
	default: return ARM_SPE_BAD_PACKET;
	}

	return 1 + payload_len;
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
	return arm_spe_get_payload(buf, len, packet);
}

static int arm_spe_get_events(const unsigned char *buf, size_t len,
			      struct arm_spe_pkt *packet)
{
	int ret = arm_spe_get_payload(buf, len, packet);

	packet->type = ARM_SPE_EVENTS;

	/* we use index to identify Events with a less number of
	 * comparisons in arm_spe_pkt_desc(): E.g., the LLC-ACCESS,
	 * LLC-REFILL, and REMOTE-ACCESS events are identified iff
	 * index > 1.
	 */
	packet->index = ret - 1;

	return ret;
}

static int arm_spe_get_data_source(const unsigned char *buf, size_t len,
				   struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_DATA_SOURCE;
	return arm_spe_get_payload(buf, len, packet);
}

static int arm_spe_get_context(const unsigned char *buf, size_t len,
			       struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_CONTEXT;
	packet->index = buf[0] & 0x3;

	return arm_spe_get_payload(buf, len, packet);
}

static int arm_spe_get_op_type(const unsigned char *buf, size_t len,
			       struct arm_spe_pkt *packet)
{
	packet->type = ARM_SPE_OP_TYPE;
	packet->index = buf[0] & 0x3;
	return arm_spe_get_payload(buf, len, packet);
}

static int arm_spe_get_counter(const unsigned char *buf, size_t len,
			       const unsigned char ext_hdr, struct arm_spe_pkt *packet)
{
	if (len < 2)
		return ARM_SPE_NEED_MORE_BYTES;

	packet->type = ARM_SPE_COUNTER;
	if (ext_hdr)
		packet->index = ((buf[0] & 0x3) << 3) | (buf[1] & 0x7);
	else
		packet->index = buf[0] & 0x7;

	packet->payload = le16_to_cpu(*(uint16_t *)(buf + 1));

	return 1 + ext_hdr + 2;
}

static int arm_spe_get_addr(const unsigned char *buf, size_t len,
			    const unsigned char ext_hdr, struct arm_spe_pkt *packet)
{
	if (len < 8)
		return ARM_SPE_NEED_MORE_BYTES;

	packet->type = ARM_SPE_ADDRESS;
	if (ext_hdr)
		packet->index = ((buf[0] & 0x3) << 3) | (buf[1] & 0x7);
	else
		packet->index = buf[0] & 0x7;

	memcpy_le64(&packet->payload, buf + 1, 8);

	return 1 + ext_hdr + 8;
}

static int arm_spe_do_get_packet(const unsigned char *buf, size_t len,
				 struct arm_spe_pkt *packet)
{
	unsigned int byte;

	memset(packet, 0, sizeof(struct arm_spe_pkt));

	if (!len)
		return ARM_SPE_NEED_MORE_BYTES;

	byte = buf[0];
	if (byte == SPE_HEADER0_PAD)
		return arm_spe_get_pad(packet);
	else if (byte == SPE_HEADER0_END) /* no timestamp at end of record */
		return arm_spe_get_end(packet);
	else if (byte & 0xc0 /* 0y11xxxxxx */) {
		if (byte & 0x80) {
			if ((byte & SPE_HEADER0_ADDRESS_MASK) == SPE_HEADER0_ADDRESS)
				return arm_spe_get_addr(buf, len, 0, packet);
			if ((byte & SPE_HEADER0_COUNTER_MASK) == SPE_HEADER0_COUNTER)
				return arm_spe_get_counter(buf, len, 0, packet);
		} else
			if (byte == SPE_HEADER0_TIMESTAMP)
				return arm_spe_get_timestamp(buf, len, packet);
			else if ((byte & SPE_HEADER0_EVENTS_MASK) == SPE_HEADER0_EVENTS)
				return arm_spe_get_events(buf, len, packet);
			else if ((byte & SPE_HEADER0_SOURCE_MASK) == SPE_HEADER0_SOURCE)
				return arm_spe_get_data_source(buf, len, packet);
			else if ((byte & SPE_HEADER0_CONTEXT_MASK) == SPE_HEADER0_CONTEXT)
				return arm_spe_get_context(buf, len, packet);
			else if ((byte & SPE_HEADER0_OP_TYPE_MASK) == SPE_HEADER0_OP_TYPE)
				return arm_spe_get_op_type(buf, len, packet);
	} else if ((byte & 0xe0) == 0x20 /* 0y001xxxxx */) {
		/* 16-bit header */
		byte = buf[1];
		if (byte == SPE_HEADER1_ALIGNMENT)
			return arm_spe_get_alignment(buf, len, packet);
		else if ((byte & SPE_HEADER1_ADDRESS_MASK) == SPE_HEADER1_ADDRESS)
			return arm_spe_get_addr(buf, len, 1, packet);
		else if ((byte & SPE_HEADER1_COUNTER_MASK) == SPE_HEADER1_COUNTER)
			return arm_spe_get_counter(buf, len, 1, packet);
	}

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

int arm_spe_pkt_desc(const struct arm_spe_pkt *packet, char *buf,
		     size_t buf_len)
{
	int ret, ns, el, idx = packet->index;
	unsigned long long payload = packet->payload;
	const char *name = arm_spe_pkt_name(packet->type);

	switch (packet->type) {
	case ARM_SPE_BAD:
	case ARM_SPE_PAD:
	case ARM_SPE_END:
		return snprintf(buf, buf_len, "%s", name);
	case ARM_SPE_EVENTS: {
		size_t blen = buf_len;

		ret = 0;
		ret = snprintf(buf, buf_len, "EV");
		buf += ret;
		blen -= ret;
		if (payload & 0x1) {
			ret = snprintf(buf, buf_len, " EXCEPTION-GEN");
			buf += ret;
			blen -= ret;
		}
		if (payload & 0x2) {
			ret = snprintf(buf, buf_len, " RETIRED");
			buf += ret;
			blen -= ret;
		}
		if (payload & 0x4) {
			ret = snprintf(buf, buf_len, " L1D-ACCESS");
			buf += ret;
			blen -= ret;
		}
		if (payload & 0x8) {
			ret = snprintf(buf, buf_len, " L1D-REFILL");
			buf += ret;
			blen -= ret;
		}
		if (payload & 0x10) {
			ret = snprintf(buf, buf_len, " TLB-ACCESS");
			buf += ret;
			blen -= ret;
		}
		if (payload & 0x20) {
			ret = snprintf(buf, buf_len, " TLB-REFILL");
			buf += ret;
			blen -= ret;
		}
		if (payload & 0x40) {
			ret = snprintf(buf, buf_len, " NOT-TAKEN");
			buf += ret;
			blen -= ret;
		}
		if (payload & 0x80) {
			ret = snprintf(buf, buf_len, " MISPRED");
			buf += ret;
			blen -= ret;
		}
		if (idx > 1) {
			if (payload & 0x100) {
				ret = snprintf(buf, buf_len, " LLC-ACCESS");
				buf += ret;
				blen -= ret;
			}
			if (payload & 0x200) {
				ret = snprintf(buf, buf_len, " LLC-REFILL");
				buf += ret;
				blen -= ret;
			}
			if (payload & 0x400) {
				ret = snprintf(buf, buf_len, " REMOTE-ACCESS");
				buf += ret;
				blen -= ret;
			}
		}
		if (ret < 0)
			return ret;
		blen -= ret;
		return buf_len - blen;
	}
	case ARM_SPE_OP_TYPE:
		switch (idx) {
		case 0:	return snprintf(buf, buf_len, "%s", payload & 0x1 ?
					"COND-SELECT" : "INSN-OTHER");
		case 1:	{
			size_t blen = buf_len;

			if (payload & 0x1)
				ret = snprintf(buf, buf_len, "ST");
			else
				ret = snprintf(buf, buf_len, "LD");
			buf += ret;
			blen -= ret;
			if (payload & 0x2) {
				if (payload & 0x4) {
					ret = snprintf(buf, buf_len, " AT");
					buf += ret;
					blen -= ret;
				}
				if (payload & 0x8) {
					ret = snprintf(buf, buf_len, " EXCL");
					buf += ret;
					blen -= ret;
				}
				if (payload & 0x10) {
					ret = snprintf(buf, buf_len, " AR");
					buf += ret;
					blen -= ret;
				}
			} else if (payload & 0x4) {
				ret = snprintf(buf, buf_len, " SIMD-FP");
				buf += ret;
				blen -= ret;
			}
			if (ret < 0)
				return ret;
			blen -= ret;
			return buf_len - blen;
		}
		case 2:	{
			size_t blen = buf_len;

			ret = snprintf(buf, buf_len, "B");
			buf += ret;
			blen -= ret;
			if (payload & 0x1) {
				ret = snprintf(buf, buf_len, " COND");
				buf += ret;
				blen -= ret;
			}
			if (payload & 0x2) {
				ret = snprintf(buf, buf_len, " IND");
				buf += ret;
				blen -= ret;
			}
			if (ret < 0)
				return ret;
			blen -= ret;
			return buf_len - blen;
			}
		default: return 0;
		}
	case ARM_SPE_DATA_SOURCE:
	case ARM_SPE_TIMESTAMP:
		return snprintf(buf, buf_len, "%s %lld", name, payload);
	case ARM_SPE_ADDRESS:
		switch (idx) {
		case 0:
		case 1: ns = !!(packet->payload & NS_FLAG);
			el = (packet->payload & EL_FLAG) >> 61;
			payload &= ~(0xffULL << 56);
			return snprintf(buf, buf_len, "%s 0x%llx el%d ns=%d",
				        (idx == 1) ? "TGT" : "PC", payload, el, ns);
		case 2:	return snprintf(buf, buf_len, "VA 0x%llx", payload);
		case 3:	ns = !!(packet->payload & NS_FLAG);
			payload &= ~(0xffULL << 56);
			return snprintf(buf, buf_len, "PA 0x%llx ns=%d",
					payload, ns);
		default: return 0;
		}
	case ARM_SPE_CONTEXT:
		return snprintf(buf, buf_len, "%s 0x%lx el%d", name,
				(unsigned long)payload, idx + 1);
	case ARM_SPE_COUNTER: {
		size_t blen = buf_len;

		ret = snprintf(buf, buf_len, "%s %d ", name,
			       (unsigned short)payload);
		buf += ret;
		blen -= ret;
		switch (idx) {
		case 0:	ret = snprintf(buf, buf_len, "TOT"); break;
		case 1:	ret = snprintf(buf, buf_len, "ISSUE"); break;
		case 2:	ret = snprintf(buf, buf_len, "XLAT"); break;
		default: ret = 0;
		}
		if (ret < 0)
			return ret;
		blen -= ret;
		return buf_len - blen;
	}
	default:
		break;
	}

	return snprintf(buf, buf_len, "%s 0x%llx (%d)",
			name, payload, packet->index);
}
