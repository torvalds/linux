/*
 * intel_pt_pkt_decoder.c: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <stdio.h>
#include <string.h>
#include <endian.h>
#include <byteswap.h>

#include "intel-pt-pkt-decoder.h"

#define BIT(n)		(1 << (n))

#define BIT63		((uint64_t)1 << 63)

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

static const char * const packet_name[] = {
	[INTEL_PT_BAD]		= "Bad Packet!",
	[INTEL_PT_PAD]		= "PAD",
	[INTEL_PT_TNT]		= "TNT",
	[INTEL_PT_TIP_PGD]	= "TIP.PGD",
	[INTEL_PT_TIP_PGE]	= "TIP.PGE",
	[INTEL_PT_TSC]		= "TSC",
	[INTEL_PT_MODE_EXEC]	= "MODE.Exec",
	[INTEL_PT_MODE_TSX]	= "MODE.TSX",
	[INTEL_PT_TIP]		= "TIP",
	[INTEL_PT_FUP]		= "FUP",
	[INTEL_PT_PSB]		= "PSB",
	[INTEL_PT_PSBEND]	= "PSBEND",
	[INTEL_PT_CBR]		= "CBR",
	[INTEL_PT_PIP]		= "PIP",
	[INTEL_PT_OVF]		= "OVF",
};

const char *intel_pt_pkt_name(enum intel_pt_pkt_type type)
{
	return packet_name[type];
}

static int intel_pt_get_long_tnt(const unsigned char *buf, size_t len,
				 struct intel_pt_pkt *packet)
{
	uint64_t payload;
	int count;

	if (len < 8)
		return INTEL_PT_NEED_MORE_BYTES;

	payload = le64_to_cpu(*(uint64_t *)buf);

	for (count = 47; count; count--) {
		if (payload & BIT63)
			break;
		payload <<= 1;
	}

	packet->type = INTEL_PT_TNT;
	packet->count = count;
	packet->payload = payload << 1;
	return 8;
}

static int intel_pt_get_pip(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	uint64_t payload = 0;

	if (len < 8)
		return INTEL_PT_NEED_MORE_BYTES;

	packet->type = INTEL_PT_PIP;
	memcpy_le64(&payload, buf + 2, 6);
	packet->payload = payload >> 1;

	return 8;
}

static int intel_pt_get_cbr(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	if (len < 4)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_CBR;
	packet->payload = buf[2];
	return 4;
}

static int intel_pt_get_ovf(struct intel_pt_pkt *packet)
{
	packet->type = INTEL_PT_OVF;
	return 2;
}

static int intel_pt_get_psb(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	int i;

	if (len < 16)
		return INTEL_PT_NEED_MORE_BYTES;

	for (i = 2; i < 16; i += 2) {
		if (buf[i] != 2 || buf[i + 1] != 0x82)
			return INTEL_PT_BAD_PACKET;
	}

	packet->type = INTEL_PT_PSB;
	return 16;
}

static int intel_pt_get_psbend(struct intel_pt_pkt *packet)
{
	packet->type = INTEL_PT_PSBEND;
	return 2;
}

static int intel_pt_get_pad(struct intel_pt_pkt *packet)
{
	packet->type = INTEL_PT_PAD;
	return 1;
}

static int intel_pt_get_ext(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	if (len < 2)
		return INTEL_PT_NEED_MORE_BYTES;

	switch (buf[1]) {
	case 0xa3: /* Long TNT */
		return intel_pt_get_long_tnt(buf, len, packet);
	case 0x43: /* PIP */
		return intel_pt_get_pip(buf, len, packet);
	case 0x03: /* CBR */
		return intel_pt_get_cbr(buf, len, packet);
	case 0xf3: /* OVF */
		return intel_pt_get_ovf(packet);
	case 0x82: /* PSB */
		return intel_pt_get_psb(buf, len, packet);
	case 0x23: /* PSBEND */
		return intel_pt_get_psbend(packet);
	default:
		return INTEL_PT_BAD_PACKET;
	}
}

static int intel_pt_get_short_tnt(unsigned int byte,
				  struct intel_pt_pkt *packet)
{
	int count;

	for (count = 6; count; count--) {
		if (byte & BIT(7))
			break;
		byte <<= 1;
	}

	packet->type = INTEL_PT_TNT;
	packet->count = count;
	packet->payload = (uint64_t)byte << 57;

	return 1;
}

static int intel_pt_get_ip(enum intel_pt_pkt_type type, unsigned int byte,
			   const unsigned char *buf, size_t len,
			   struct intel_pt_pkt *packet)
{
	switch (byte >> 5) {
	case 0:
		packet->count = 0;
		break;
	case 1:
		if (len < 3)
			return INTEL_PT_NEED_MORE_BYTES;
		packet->count = 2;
		packet->payload = le16_to_cpu(*(uint16_t *)(buf + 1));
		break;
	case 2:
		if (len < 5)
			return INTEL_PT_NEED_MORE_BYTES;
		packet->count = 4;
		packet->payload = le32_to_cpu(*(uint32_t *)(buf + 1));
		break;
	case 3:
	case 6:
		if (len < 7)
			return INTEL_PT_NEED_MORE_BYTES;
		packet->count = 6;
		memcpy_le64(&packet->payload, buf + 1, 6);
		break;
	default:
		return INTEL_PT_BAD_PACKET;
	}

	packet->type = type;

	return packet->count + 1;
}

static int intel_pt_get_mode(const unsigned char *buf, size_t len,
			     struct intel_pt_pkt *packet)
{
	if (len < 2)
		return INTEL_PT_NEED_MORE_BYTES;

	switch (buf[1] >> 5) {
	case 0:
		packet->type = INTEL_PT_MODE_EXEC;
		switch (buf[1] & 3) {
		case 0:
			packet->payload = 16;
			break;
		case 1:
			packet->payload = 64;
			break;
		case 2:
			packet->payload = 32;
			break;
		default:
			return INTEL_PT_BAD_PACKET;
		}
		break;
	case 1:
		packet->type = INTEL_PT_MODE_TSX;
		if ((buf[1] & 3) == 3)
			return INTEL_PT_BAD_PACKET;
		packet->payload = buf[1] & 3;
		break;
	default:
		return INTEL_PT_BAD_PACKET;
	}

	return 2;
}

static int intel_pt_get_tsc(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	if (len < 8)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_TSC;
	memcpy_le64(&packet->payload, buf + 1, 7);
	return 8;
}

static int intel_pt_do_get_packet(const unsigned char *buf, size_t len,
				  struct intel_pt_pkt *packet)
{
	unsigned int byte;

	memset(packet, 0, sizeof(struct intel_pt_pkt));

	if (!len)
		return INTEL_PT_NEED_MORE_BYTES;

	byte = buf[0];
	if (!(byte & BIT(0))) {
		if (byte == 0)
			return intel_pt_get_pad(packet);
		if (byte == 2)
			return intel_pt_get_ext(buf, len, packet);
		return intel_pt_get_short_tnt(byte, packet);
	}

	switch (byte & 0x1f) {
	case 0x0D:
		return intel_pt_get_ip(INTEL_PT_TIP, byte, buf, len, packet);
	case 0x11:
		return intel_pt_get_ip(INTEL_PT_TIP_PGE, byte, buf, len,
				       packet);
	case 0x01:
		return intel_pt_get_ip(INTEL_PT_TIP_PGD, byte, buf, len,
				       packet);
	case 0x1D:
		return intel_pt_get_ip(INTEL_PT_FUP, byte, buf, len, packet);
	case 0x19:
		switch (byte) {
		case 0x99:
			return intel_pt_get_mode(buf, len, packet);
		case 0x19:
			return intel_pt_get_tsc(buf, len, packet);
		default:
			return INTEL_PT_BAD_PACKET;
		}
	default:
		return INTEL_PT_BAD_PACKET;
	}
}

int intel_pt_get_packet(const unsigned char *buf, size_t len,
			struct intel_pt_pkt *packet)
{
	int ret;

	ret = intel_pt_do_get_packet(buf, len, packet);
	if (ret > 0) {
		while (ret < 8 && len > (size_t)ret && !buf[ret])
			ret += 1;
	}
	return ret;
}

int intel_pt_pkt_desc(const struct intel_pt_pkt *packet, char *buf,
		      size_t buf_len)
{
	int ret, i;
	unsigned long long payload = packet->payload;
	const char *name = intel_pt_pkt_name(packet->type);

	switch (packet->type) {
	case INTEL_PT_BAD:
	case INTEL_PT_PAD:
	case INTEL_PT_PSB:
	case INTEL_PT_PSBEND:
	case INTEL_PT_OVF:
		return snprintf(buf, buf_len, "%s", name);
	case INTEL_PT_TNT: {
		size_t blen = buf_len;

		ret = snprintf(buf, blen, "%s ", name);
		if (ret < 0)
			return ret;
		buf += ret;
		blen -= ret;
		for (i = 0; i < packet->count; i++) {
			if (payload & BIT63)
				ret = snprintf(buf, blen, "T");
			else
				ret = snprintf(buf, blen, "N");
			if (ret < 0)
				return ret;
			buf += ret;
			blen -= ret;
			payload <<= 1;
		}
		ret = snprintf(buf, blen, " (%d)", packet->count);
		if (ret < 0)
			return ret;
		blen -= ret;
		return buf_len - blen;
	}
	case INTEL_PT_TIP_PGD:
	case INTEL_PT_TIP_PGE:
	case INTEL_PT_TIP:
	case INTEL_PT_FUP:
		if (!(packet->count))
			return snprintf(buf, buf_len, "%s no ip", name);
	case INTEL_PT_CBR:
		return snprintf(buf, buf_len, "%s 0x%llx", name, payload);
	case INTEL_PT_TSC:
		if (packet->count)
			return snprintf(buf, buf_len,
					"%s 0x%llx CTC 0x%x FC 0x%x",
					name, payload, packet->count & 0xffff,
					(packet->count >> 16) & 0x1ff);
		else
			return snprintf(buf, buf_len, "%s 0x%llx",
					name, payload);
	case INTEL_PT_MODE_EXEC:
		return snprintf(buf, buf_len, "%s %lld", name, payload);
	case INTEL_PT_MODE_TSX:
		return snprintf(buf, buf_len, "%s TXAbort:%u InTX:%u",
				name, (unsigned)(payload >> 1) & 1,
				(unsigned)payload & 1);
	case INTEL_PT_PIP:
		ret = snprintf(buf, buf_len, "%s 0x%llx",
			       name, payload);
		return ret;
	default:
		break;
	}
	return snprintf(buf, buf_len, "%s 0x%llx (%d)",
			name, payload, packet->count);
}
