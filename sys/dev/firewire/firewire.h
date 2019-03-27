/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003 Hidetoshi Shimokawa
 * Copyright (c) 1998-2002 Katsushi Kobayashi and Hidetoshi Shimokawa
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the acknowledgement as bellow:
 *
 *    This product includes software developed by K. Kobayashi and H. Shimokawa
 *
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef _FIREWIRE_H
#define _FIREWIRE_H 1

#define	DEV_DEF  0
#define	DEV_DV   2

struct fw_isochreq {
	unsigned char ch:6;
	unsigned char tag:2;
};

struct fw_isobufreq {
	struct fw_bufspec {
		unsigned int nchunk;
		unsigned int npacket;
		unsigned int psize;
	} tx, rx;
};

struct fw_addr {
	uint32_t hi;
	uint32_t lo;
};

struct fw_asybindreq {
	struct fw_addr start;
	unsigned long len;
};

struct fw_reg_req_t {
	uint32_t addr;
	uint32_t data;
};

#define MAXREC(x)	(2 << (x))
#define FWPMAX_S400 (2048 + 20)	/* MAXREC plus space for control data */
#define FWMAXQUEUE 128

#define	FWLOCALBUS	0xffc0

#define FWTCODE_WREQQ	0
#define FWTCODE_WREQB	1
#define FWTCODE_WRES	2
#define FWTCODE_RREQQ	4
#define FWTCODE_RREQB	5
#define FWTCODE_RRESQ	6
#define FWTCODE_RRESB	7
#define FWTCODE_CYCS	8
#define FWTCODE_LREQ	9
#define FWTCODE_STREAM	0xa
#define FWTCODE_LRES	0xb
#define FWTCODE_PHY	0xe

#define	FWRETRY_1	0
#define	FWRETRY_X	1
#define	FWRETRY_A	2
#define	FWRETRY_B	3

#define FWRCODE_COMPLETE	0
#define FWRCODE_ER_CONFL	4
#define FWRCODE_ER_DATA		5
#define FWRCODE_ER_TYPE		6
#define FWRCODE_ER_ADDR		7

/*
 * Defined 1394a-2000
 * Table 5B-1
 */
#define FWSPD_S100	0
#define FWSPD_S200	1
#define FWSPD_S400	2
#define FWSPD_S800	3
#define FWSPD_S1600	4
#define FWSPD_S3200	5

#define	FWP_TL_VALID (1 << 7)

struct fw_isohdr {
	uint32_t hdr[1];
};

struct fw_asyhdr {
	uint32_t hdr[4];
};

#if BYTE_ORDER == BIG_ENDIAN
#define BIT4x2(x,y)	 uint8_t  x:4, y:4
#define BIT16x2(x,y)	uint32_t x:16, y:16
#else
#define BIT4x2(x,y)	 uint8_t  y:4, x:4
#define BIT16x2(x,y)	uint32_t y:16, x:16
#endif


#if BYTE_ORDER == BIG_ENDIAN
#define COMMON_HDR(a,b,c,d)	uint32_t a:16,b:8,c:4,d:4
#define COMMON_RES(a,b,c,d)	uint32_t a:16,b:4,c:4,d:8
#else
#define COMMON_HDR(a,b,c,d)	uint32_t d:4,c:4,b:8,a:16
#define COMMON_RES(a,b,c,d)	uint32_t d:8,c:4,b:4,a:16
#endif

struct fw_pkt {
	union {
		uint32_t ld[0];
		struct {
			COMMON_HDR(, , tcode, );
		} common;
		struct {
			COMMON_HDR(len, chtag, tcode, sy);
			uint32_t payload[0];
		} stream;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			BIT16x2(src, );
		} hdr;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			BIT16x2(src, dest_hi);
			uint32_t dest_lo;
		} rreqq;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			COMMON_RES(src, rtcode, , );
			uint32_t :32;
		} wres;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			BIT16x2(src, dest_hi);
			uint32_t dest_lo;
			BIT16x2(len, extcode);
		} rreqb;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			BIT16x2(src, dest_hi);
			uint32_t dest_lo;
			uint32_t data;
		} wreqq;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			BIT16x2(src, dest_hi);
			uint32_t dest_lo;
			uint32_t data;
		} cyc;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			COMMON_RES(src, rtcode, , );
			uint32_t :32;
			uint32_t data;
		} rresq;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			BIT16x2(src, dest_hi);
			uint32_t dest_lo;
			BIT16x2(len, extcode);
			uint32_t payload[0];
		} wreqb;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			BIT16x2(src, dest_hi);
			uint32_t dest_lo;
			BIT16x2(len, extcode);
			uint32_t payload[0];
		} lreq;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			COMMON_RES(src, rtcode, , );
			uint32_t :32;
			BIT16x2(len, extcode);
			uint32_t payload[0];
		} rresb;
		struct {
			COMMON_HDR(dst, tlrt, tcode, pri);
			COMMON_RES(src, rtcode, , );
			uint32_t :32;
			BIT16x2(len, extcode);
			uint32_t payload[0];
		} lres;
	} mode;
};

/*
 * Response code (rtcode)
 */
/* The node has successfully completed the command. */
#define	RESP_CMP		0
/* A resource conflict was detected. The request may be retried. */
#define	RESP_CONFLICT_ERROR	4
/* Hardware error, data is unavailable. */
#define	RESP_DATA_ERROR		5
/* A field in the request packet header was set to an unsupported or incorrect
 * value, or an invalid transaction was attempted (e.g., a write to a read-only
 * address). */
#define	RESP_TYPE_ERROR		6
/* The destination offset field in the request was set to an address not
 * accessible in the destination node. */
#define	RESP_ADDRESS_ERROR	7

/*
 * Extended transaction code (extcode)
 */
#define EXTCODE_MASK_SWAP	1
#define EXTCODE_CMP_SWAP	2
#define EXTCODE_FETCH_ADD	3
#define EXTCODE_LITTLE_ADD	4
#define EXTCODE_BOUNDED_ADD	5
#define EXTCODE_WRAP_ADD	6

struct fw_eui64 {
	uint32_t hi, lo;
};
#define FW_EUI64_BYTE(eui, x) \
	((((x) < 4)?				\
		((eui)->hi >> (8 * (3 - (x)))): \
		((eui)->lo >> (8 * (7 - (x))))	\
	) & 0xff)
#define FW_EUI64_EQUAL(x, y) \
	((x).hi == (y).hi && (x).lo == (y).lo)

struct fw_asyreq {
	struct fw_asyreq_t {
		unsigned char sped;
		unsigned int type;
#define FWASREQNODE	0
#define FWASREQEUI	1
#define FWASRESTL	2
#define FWASREQSTREAM	3
		unsigned short len;
		union {
			struct fw_eui64 eui;
		} dst;
	} req;
	struct fw_pkt pkt;
	uint32_t data[512];
};

struct fw_devinfo {
	struct fw_eui64 eui;
	uint16_t dst;
	uint16_t status;
};

#define FW_MAX_DEVLST 70
struct fw_devlstreq {
	uint16_t n;
	uint16_t info_len;
	struct fw_devinfo dev[FW_MAX_DEVLST];
};

/*
 * Defined in IEEE 1394a-2000
 * 4.3.4.1
 */
#define FW_SELF_ID_PORT_CONNECTED_TO_CHILD 3
#define FW_SELF_ID_PORT_CONNECTED_TO_PARENT 2
#define FW_SELF_ID_PORT_NOT_CONNECTED 1
#define FW_SELF_ID_PORT_NOT_EXISTS 0

#define FW_SELF_ID_PAGE0 0
#define FW_SELF_ID_PAGE1 1

#if BYTE_ORDER == BIG_ENDIAN
union fw_self_id {
	struct {
		uint32_t  id:2,
			  phy_id:6,
			  sequel:1,
			  link_active:1,
			  gap_count:6,
			  phy_speed:2,
			  reserved:2,
			  contender:1,
			  power_class:3,
			  port0:2,
			  port1:2,
			  port2:2,
			  initiated_reset:1,
			  more_packets:1;
	} p0;
	struct {
		uint32_t
			  id:2,
			  phy_id:6,
			  sequel:1,
			  sequence_num:3,
			  reserved2:2,
			  port3:2,
			  port4:2,
			  port5:2,
			  port6:2,
			  port7:2,
			  port8:2,
			  port9:2,
			  port10:2,
			  reserved1:1,
			  more_packets:1;
	} p1;
	struct {
		uint32_t
			  id:2,
			  phy_id:6,
			  sequel:1,
			  sequence_num:3,
			  :2,
			  port11:2,
			  port12:2,
			  port13:2,
			  port14:2,
			  port15:2,
			  :8;
	} p2;
};
#else
union fw_self_id {
	struct {
		uint32_t  more_packets:1,
			  initiated_reset:1,
			  port2:2,
			  port1:2,
			  port0:2,
			  power_class:3,
			  contender:1,
			  reserved:2,
			  phy_speed:2,
			  gap_count:6,
			  link_active:1,
			  sequel:1,
			  phy_id:6,
			  id:2;
	} p0;
	struct {
		uint32_t  more_packets:1,
			  reserved1:1,
			  port10:2,
			  port9:2,
			  port8:2,
			  port7:2,
			  port6:2,
			  port5:2,
			  port4:2,
			  port3:2,
			  reserved2:2,
			  sequence_num:3,
			  sequel:1,
			  phy_id:6,
			  id:2;
	} p1;
	struct {
		uint32_t
			  reserved3:8,
			  port15:2,
			  port14:2,
			  port13:2,
			  port12:2,
			  port11:2,
			  reserved4:2,
			  sequence_num:3,
			  sequel:1,
			  phy_id:6,
			  id:2;
	} p2;
};
#endif


struct fw_topology_map {
	uint32_t crc:16,
		 crc_len:16;
	uint32_t generation;
	uint32_t self_id_count:16,
		 node_count:16;
	union fw_self_id self_id[4 * 64];
};

struct fw_speed_map {
	uint32_t crc:16,
		 crc_len:16;
	uint32_t generation;
	uint8_t  speed[64][64];
};

struct fw_crom_buf {
	struct fw_eui64 eui;
	uint32_t len;
	void *ptr;
};

/*
 * FireWire specific system requests.
 */
#define	FW_SSTBUF	_IOWR('S', 86, struct fw_isobufreq)
#define	FW_GSTBUF	_IOWR('S', 87, struct fw_isobufreq)
#define	FW_SRSTREAM	_IOWR('S', 88, struct fw_isochreq)
#define	FW_GRSTREAM	_IOWR('S', 89, struct fw_isochreq)
#define	FW_STSTREAM	_IOWR('S', 90, struct fw_isochreq)
#define	FW_GTSTREAM	_IOWR('S', 91, struct fw_isochreq)

#define	FW_ASYREQ	_IOWR('S', 92, struct fw_asyreq)
#define FW_IBUSRST	_IOR('S', 1, unsigned int)
#define FW_GDEVLST	_IOWR('S', 2, struct fw_devlstreq)
#define	FW_SBINDADDR	_IOWR('S', 3, struct fw_asybindreq)
#define	FW_CBINDADDR	_IOWR('S', 4, struct fw_asybindreq)
#define	FW_GTPMAP	_IOR('S', 5, struct fw_topology_map)
#define	FW_GCROM	_IOWR('S', 7, struct fw_crom_buf)

#define	FW_SDEUI64	_IOW('S', 20, struct fw_eui64)
#define	FW_GDEUI64	_IOR('S', 21, struct fw_eui64)

#define FWOHCI_RDREG	_IOWR('S', 80, struct fw_reg_req_t)
#define FWOHCI_WRREG	_IOWR('S', 81, struct fw_reg_req_t)
#define FWOHCI_RDPHYREG	_IOWR('S', 82, struct fw_reg_req_t)
#define FWOHCI_WRPHYREG	_IOWR('S', 83, struct fw_reg_req_t)

#define DUMPDMA		_IOWR('S', 82, uint32_t)

#ifdef _KERNEL

#define FWMAXNDMA 0x100 /* 8 bits DMA channel id. in device No. */

#define MAKEMINOR(f, u, s)	\
	((f) | (((u) & 0xff) << 8) | (s & 0xff))
#define DEV2UNIT(x)	((dev2unit(x) & 0xff00) >> 8)
#define DEV2SUB(x)	(dev2unit(x) & 0xff)

#define FWMEM_FLAG	0x10000
#define DEV_FWMEM(x)	(dev2unit(x) & FWMEM_FLAG)
#endif
#endif
