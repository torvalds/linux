// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_pt_pkt_decoder.c: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 */

#include <stdio.h>
#include <string.h>
#include <endian.h>
#include <byteswap.h>
#include <linux/compiler.h>

#include "intel-pt-pkt-decoder.h"

#define BIT(n)		(1 << (n))

#define BIT63		((uint64_t)1 << 63)

#define NR_FLAG		BIT63

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
	[INTEL_PT_TMA]		= "TMA",
	[INTEL_PT_MODE_EXEC]	= "MODE.Exec",
	[INTEL_PT_MODE_TSX]	= "MODE.TSX",
	[INTEL_PT_MTC]		= "MTC",
	[INTEL_PT_TIP]		= "TIP",
	[INTEL_PT_FUP]		= "FUP",
	[INTEL_PT_CYC]		= "CYC",
	[INTEL_PT_VMCS]		= "VMCS",
	[INTEL_PT_PSB]		= "PSB",
	[INTEL_PT_PSBEND]	= "PSBEND",
	[INTEL_PT_CBR]		= "CBR",
	[INTEL_PT_TRACESTOP]	= "TraceSTOP",
	[INTEL_PT_PIP]		= "PIP",
	[INTEL_PT_OVF]		= "OVF",
	[INTEL_PT_MNT]		= "MNT",
	[INTEL_PT_PTWRITE]	= "PTWRITE",
	[INTEL_PT_PTWRITE_IP]	= "PTWRITE",
	[INTEL_PT_EXSTOP]	= "EXSTOP",
	[INTEL_PT_EXSTOP_IP]	= "EXSTOP",
	[INTEL_PT_MWAIT]	= "MWAIT",
	[INTEL_PT_PWRE]		= "PWRE",
	[INTEL_PT_PWRX]		= "PWRX",
	[INTEL_PT_BBP]		= "BBP",
	[INTEL_PT_BIP]		= "BIP",
	[INTEL_PT_BEP]		= "BEP",
	[INTEL_PT_BEP_IP]	= "BEP",
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
	if (payload & 1)
		packet->payload |= NR_FLAG;

	return 8;
}

static int intel_pt_get_tracestop(struct intel_pt_pkt *packet)
{
	packet->type = INTEL_PT_TRACESTOP;
	return 2;
}

static int intel_pt_get_cbr(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	if (len < 4)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_CBR;
	packet->payload = le16_to_cpu(*(uint16_t *)(buf + 2));
	return 4;
}

static int intel_pt_get_vmcs(const unsigned char *buf, size_t len,
			     struct intel_pt_pkt *packet)
{
	unsigned int count = (52 - 5) >> 3;

	if (count < 1 || count > 7)
		return INTEL_PT_BAD_PACKET;

	if (len < count + 2)
		return INTEL_PT_NEED_MORE_BYTES;

	packet->type = INTEL_PT_VMCS;
	packet->count = count;
	memcpy_le64(&packet->payload, buf + 2, count);

	return count + 2;
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

static int intel_pt_get_tma(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	if (len < 7)
		return INTEL_PT_NEED_MORE_BYTES;

	packet->type = INTEL_PT_TMA;
	packet->payload = buf[2] | (buf[3] << 8);
	packet->count = buf[5] | ((buf[6] & BIT(0)) << 8);
	return 7;
}

static int intel_pt_get_pad(struct intel_pt_pkt *packet)
{
	packet->type = INTEL_PT_PAD;
	return 1;
}

static int intel_pt_get_mnt(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	if (len < 11)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_MNT;
	memcpy_le64(&packet->payload, buf + 3, 8);
	return 11
;
}

static int intel_pt_get_3byte(const unsigned char *buf, size_t len,
			      struct intel_pt_pkt *packet)
{
	if (len < 3)
		return INTEL_PT_NEED_MORE_BYTES;

	switch (buf[2]) {
	case 0x88: /* MNT */
		return intel_pt_get_mnt(buf, len, packet);
	default:
		return INTEL_PT_BAD_PACKET;
	}
}

static int intel_pt_get_ptwrite(const unsigned char *buf, size_t len,
				struct intel_pt_pkt *packet)
{
	packet->count = (buf[1] >> 5) & 0x3;
	packet->type = buf[1] & BIT(7) ? INTEL_PT_PTWRITE_IP :
					 INTEL_PT_PTWRITE;

	switch (packet->count) {
	case 0:
		if (len < 6)
			return INTEL_PT_NEED_MORE_BYTES;
		packet->payload = le32_to_cpu(*(uint32_t *)(buf + 2));
		return 6;
	case 1:
		if (len < 10)
			return INTEL_PT_NEED_MORE_BYTES;
		packet->payload = le64_to_cpu(*(uint64_t *)(buf + 2));
		return 10;
	default:
		return INTEL_PT_BAD_PACKET;
	}
}

static int intel_pt_get_exstop(struct intel_pt_pkt *packet)
{
	packet->type = INTEL_PT_EXSTOP;
	return 2;
}

static int intel_pt_get_exstop_ip(struct intel_pt_pkt *packet)
{
	packet->type = INTEL_PT_EXSTOP_IP;
	return 2;
}

static int intel_pt_get_mwait(const unsigned char *buf, size_t len,
			      struct intel_pt_pkt *packet)
{
	if (len < 10)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_MWAIT;
	packet->payload = le64_to_cpu(*(uint64_t *)(buf + 2));
	return 10;
}

static int intel_pt_get_pwre(const unsigned char *buf, size_t len,
			     struct intel_pt_pkt *packet)
{
	if (len < 4)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_PWRE;
	memcpy_le64(&packet->payload, buf + 2, 2);
	return 4;
}

static int intel_pt_get_pwrx(const unsigned char *buf, size_t len,
			     struct intel_pt_pkt *packet)
{
	if (len < 7)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_PWRX;
	memcpy_le64(&packet->payload, buf + 2, 5);
	return 7;
}

static int intel_pt_get_bbp(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	if (len < 3)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_BBP;
	packet->count = buf[2] >> 7;
	packet->payload = buf[2] & 0x1f;
	return 3;
}

static int intel_pt_get_bip_4(const unsigned char *buf, size_t len,
			      struct intel_pt_pkt *packet)
{
	if (len < 5)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_BIP;
	packet->count = buf[0] >> 3;
	memcpy_le64(&packet->payload, buf + 1, 4);
	return 5;
}

static int intel_pt_get_bip_8(const unsigned char *buf, size_t len,
			      struct intel_pt_pkt *packet)
{
	if (len < 9)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_BIP;
	packet->count = buf[0] >> 3;
	memcpy_le64(&packet->payload, buf + 1, 8);
	return 9;
}

static int intel_pt_get_bep(size_t len, struct intel_pt_pkt *packet)
{
	if (len < 2)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_BEP;
	return 2;
}

static int intel_pt_get_bep_ip(size_t len, struct intel_pt_pkt *packet)
{
	if (len < 2)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_BEP_IP;
	return 2;
}

static int intel_pt_get_ext(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	if (len < 2)
		return INTEL_PT_NEED_MORE_BYTES;

	if ((buf[1] & 0x1f) == 0x12)
		return intel_pt_get_ptwrite(buf, len, packet);

	switch (buf[1]) {
	case 0xa3: /* Long TNT */
		return intel_pt_get_long_tnt(buf, len, packet);
	case 0x43: /* PIP */
		return intel_pt_get_pip(buf, len, packet);
	case 0x83: /* TraceStop */
		return intel_pt_get_tracestop(packet);
	case 0x03: /* CBR */
		return intel_pt_get_cbr(buf, len, packet);
	case 0xc8: /* VMCS */
		return intel_pt_get_vmcs(buf, len, packet);
	case 0xf3: /* OVF */
		return intel_pt_get_ovf(packet);
	case 0x82: /* PSB */
		return intel_pt_get_psb(buf, len, packet);
	case 0x23: /* PSBEND */
		return intel_pt_get_psbend(packet);
	case 0x73: /* TMA */
		return intel_pt_get_tma(buf, len, packet);
	case 0xC3: /* 3-byte header */
		return intel_pt_get_3byte(buf, len, packet);
	case 0x62: /* EXSTOP no IP */
		return intel_pt_get_exstop(packet);
	case 0xE2: /* EXSTOP with IP */
		return intel_pt_get_exstop_ip(packet);
	case 0xC2: /* MWAIT */
		return intel_pt_get_mwait(buf, len, packet);
	case 0x22: /* PWRE */
		return intel_pt_get_pwre(buf, len, packet);
	case 0xA2: /* PWRX */
		return intel_pt_get_pwrx(buf, len, packet);
	case 0x63: /* BBP */
		return intel_pt_get_bbp(buf, len, packet);
	case 0x33: /* BEP no IP */
		return intel_pt_get_bep(len, packet);
	case 0xb3: /* BEP with IP */
		return intel_pt_get_bep_ip(len, packet);
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

static int intel_pt_get_cyc(unsigned int byte, const unsigned char *buf,
			    size_t len, struct intel_pt_pkt *packet)
{
	unsigned int offs = 1, shift;
	uint64_t payload = byte >> 3;

	byte >>= 2;
	len -= 1;
	for (shift = 5; byte & 1; shift += 7) {
		if (offs > 9)
			return INTEL_PT_BAD_PACKET;
		if (len < offs)
			return INTEL_PT_NEED_MORE_BYTES;
		byte = buf[offs++];
		payload |= ((uint64_t)byte >> 1) << shift;
	}

	packet->type = INTEL_PT_CYC;
	packet->payload = payload;
	return offs;
}

static int intel_pt_get_ip(enum intel_pt_pkt_type type, unsigned int byte,
			   const unsigned char *buf, size_t len,
			   struct intel_pt_pkt *packet)
{
	int ip_len;

	packet->count = byte >> 5;

	switch (packet->count) {
	case 0:
		ip_len = 0;
		break;
	case 1:
		if (len < 3)
			return INTEL_PT_NEED_MORE_BYTES;
		ip_len = 2;
		packet->payload = le16_to_cpu(*(uint16_t *)(buf + 1));
		break;
	case 2:
		if (len < 5)
			return INTEL_PT_NEED_MORE_BYTES;
		ip_len = 4;
		packet->payload = le32_to_cpu(*(uint32_t *)(buf + 1));
		break;
	case 3:
	case 4:
		if (len < 7)
			return INTEL_PT_NEED_MORE_BYTES;
		ip_len = 6;
		memcpy_le64(&packet->payload, buf + 1, 6);
		break;
	case 6:
		if (len < 9)
			return INTEL_PT_NEED_MORE_BYTES;
		ip_len = 8;
		packet->payload = le64_to_cpu(*(uint64_t *)(buf + 1));
		break;
	default:
		return INTEL_PT_BAD_PACKET;
	}

	packet->type = type;

	return ip_len + 1;
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

static int intel_pt_get_mtc(const unsigned char *buf, size_t len,
			    struct intel_pt_pkt *packet)
{
	if (len < 2)
		return INTEL_PT_NEED_MORE_BYTES;
	packet->type = INTEL_PT_MTC;
	packet->payload = buf[1];
	return 2;
}

static int intel_pt_do_get_packet(const unsigned char *buf, size_t len,
				  struct intel_pt_pkt *packet,
				  enum intel_pt_pkt_ctx ctx)
{
	unsigned int byte;

	memset(packet, 0, sizeof(struct intel_pt_pkt));

	if (!len)
		return INTEL_PT_NEED_MORE_BYTES;

	byte = buf[0];

	switch (ctx) {
	case INTEL_PT_NO_CTX:
		break;
	case INTEL_PT_BLK_4_CTX:
		if ((byte & 0x7) == 4)
			return intel_pt_get_bip_4(buf, len, packet);
		break;
	case INTEL_PT_BLK_8_CTX:
		if ((byte & 0x7) == 4)
			return intel_pt_get_bip_8(buf, len, packet);
		break;
	default:
		break;
	}

	if (!(byte & BIT(0))) {
		if (byte == 0)
			return intel_pt_get_pad(packet);
		if (byte == 2)
			return intel_pt_get_ext(buf, len, packet);
		return intel_pt_get_short_tnt(byte, packet);
	}

	if ((byte & 2))
		return intel_pt_get_cyc(byte, buf, len, packet);

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
		case 0x59:
			return intel_pt_get_mtc(buf, len, packet);
		default:
			return INTEL_PT_BAD_PACKET;
		}
	default:
		return INTEL_PT_BAD_PACKET;
	}
}

void intel_pt_upd_pkt_ctx(const struct intel_pt_pkt *packet,
			  enum intel_pt_pkt_ctx *ctx)
{
	switch (packet->type) {
	case INTEL_PT_BAD:
	case INTEL_PT_PAD:
	case INTEL_PT_TSC:
	case INTEL_PT_TMA:
	case INTEL_PT_MTC:
	case INTEL_PT_FUP:
	case INTEL_PT_CYC:
	case INTEL_PT_CBR:
	case INTEL_PT_MNT:
	case INTEL_PT_EXSTOP:
	case INTEL_PT_EXSTOP_IP:
	case INTEL_PT_PWRE:
	case INTEL_PT_PWRX:
	case INTEL_PT_BIP:
		break;
	case INTEL_PT_TNT:
	case INTEL_PT_TIP:
	case INTEL_PT_TIP_PGD:
	case INTEL_PT_TIP_PGE:
	case INTEL_PT_MODE_EXEC:
	case INTEL_PT_MODE_TSX:
	case INTEL_PT_PIP:
	case INTEL_PT_OVF:
	case INTEL_PT_VMCS:
	case INTEL_PT_TRACESTOP:
	case INTEL_PT_PSB:
	case INTEL_PT_PSBEND:
	case INTEL_PT_PTWRITE:
	case INTEL_PT_PTWRITE_IP:
	case INTEL_PT_MWAIT:
	case INTEL_PT_BEP:
	case INTEL_PT_BEP_IP:
		*ctx = INTEL_PT_NO_CTX;
		break;
	case INTEL_PT_BBP:
		if (packet->count)
			*ctx = INTEL_PT_BLK_4_CTX;
		else
			*ctx = INTEL_PT_BLK_8_CTX;
		break;
	default:
		break;
	}
}

int intel_pt_get_packet(const unsigned char *buf, size_t len,
			struct intel_pt_pkt *packet, enum intel_pt_pkt_ctx *ctx)
{
	int ret;

	ret = intel_pt_do_get_packet(buf, len, packet, *ctx);
	if (ret > 0) {
		while (ret < 8 && len > (size_t)ret && !buf[ret])
			ret += 1;
		intel_pt_upd_pkt_ctx(packet, ctx);
	}
	return ret;
}

int intel_pt_pkt_desc(const struct intel_pt_pkt *packet, char *buf,
		      size_t buf_len)
{
	int ret, i, nr;
	unsigned long long payload = packet->payload;
	const char *name = intel_pt_pkt_name(packet->type);

	switch (packet->type) {
	case INTEL_PT_BAD:
	case INTEL_PT_PAD:
	case INTEL_PT_PSB:
	case INTEL_PT_PSBEND:
	case INTEL_PT_TRACESTOP:
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
		__fallthrough;
	case INTEL_PT_CYC:
	case INTEL_PT_VMCS:
	case INTEL_PT_MTC:
	case INTEL_PT_MNT:
	case INTEL_PT_CBR:
	case INTEL_PT_TSC:
		return snprintf(buf, buf_len, "%s 0x%llx", name, payload);
	case INTEL_PT_TMA:
		return snprintf(buf, buf_len, "%s CTC 0x%x FC 0x%x", name,
				(unsigned)payload, packet->count);
	case INTEL_PT_MODE_EXEC:
		return snprintf(buf, buf_len, "%s %lld", name, payload);
	case INTEL_PT_MODE_TSX:
		return snprintf(buf, buf_len, "%s TXAbort:%u InTX:%u",
				name, (unsigned)(payload >> 1) & 1,
				(unsigned)payload & 1);
	case INTEL_PT_PIP:
		nr = packet->payload & NR_FLAG ? 1 : 0;
		payload &= ~NR_FLAG;
		ret = snprintf(buf, buf_len, "%s 0x%llx (NR=%d)",
			       name, payload, nr);
		return ret;
	case INTEL_PT_PTWRITE:
		return snprintf(buf, buf_len, "%s 0x%llx IP:0", name, payload);
	case INTEL_PT_PTWRITE_IP:
		return snprintf(buf, buf_len, "%s 0x%llx IP:1", name, payload);
	case INTEL_PT_BEP:
	case INTEL_PT_EXSTOP:
		return snprintf(buf, buf_len, "%s IP:0", name);
	case INTEL_PT_BEP_IP:
	case INTEL_PT_EXSTOP_IP:
		return snprintf(buf, buf_len, "%s IP:1", name);
	case INTEL_PT_MWAIT:
		return snprintf(buf, buf_len, "%s 0x%llx Hints 0x%x Extensions 0x%x",
				name, payload, (unsigned int)(payload & 0xff),
				(unsigned int)((payload >> 32) & 0x3));
	case INTEL_PT_PWRE:
		return snprintf(buf, buf_len, "%s 0x%llx HW:%u CState:%u Sub-CState:%u",
				name, payload, !!(payload & 0x80),
				(unsigned int)((payload >> 12) & 0xf),
				(unsigned int)((payload >> 8) & 0xf));
	case INTEL_PT_PWRX:
		return snprintf(buf, buf_len, "%s 0x%llx Last CState:%u Deepest CState:%u Wake Reason 0x%x",
				name, payload,
				(unsigned int)((payload >> 4) & 0xf),
				(unsigned int)(payload & 0xf),
				(unsigned int)((payload >> 8) & 0xf));
	case INTEL_PT_BBP:
		return snprintf(buf, buf_len, "%s SZ %s-byte Type 0x%llx",
				name, packet->count ? "4" : "8", payload);
	case INTEL_PT_BIP:
		return snprintf(buf, buf_len, "%s ID 0x%02x Value 0x%llx",
				name, packet->count, payload);
	default:
		break;
	}
	return snprintf(buf, buf_len, "%s 0x%llx (%d)",
			name, payload, packet->count);
}
