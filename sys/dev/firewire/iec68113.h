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

#define		DV_BROADCAST_ON (1<<30)
#define		oMPR		0x900
#define		oPCR		0x904
#define		iMPR		0x980
#define		iPCR		0x984

struct ciphdr {
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t eoh0:1,		/* 0 */
		form0:1,	/* 0 */
		src:6;
#else
	uint8_t src:6,
		form0:1,	/* 0 */
		eoh0:1;		/* 0 */
#endif
	uint8_t len;
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t fn:2,
		qpc:3,
		sph:1,
		:2;
#else
	uint8_t :2,
		sph:1,
		qpc:3,
		fn:2;
#endif
	uint8_t dbc;
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t eoh1:1,		/* 1 */
		form1:1,	/* 0 */
		fmt:6;
#else
	uint8_t fmt:6,
		form1:1,	/* 0 */
		eoh1:1;		/* 1 */
#endif
#define CIP_FMT_DVCR	0
#define CIP_FMT_MPEG	(1<<5)
	union {
		struct {
#if BYTE_ORDER == BIG_ENDIAN
			uint8_t fs:1,		/* 50/60 field system
								NTSC/PAL */
				stype:5,
				:2;
#else
			uint8_t :2,
				stype:5,
		  		fs:1;		/* 50/60 field system
								NTSC/PAL */
#endif
#define	CIP_STYPE_SD	0
#define	CIP_STYPE_SDL	1
#define	CIP_STYPE_HD	2
	  		uint16_t cyc:16;	/* take care of byte order! */
		} __attribute__ ((packed)) dv;
		uint8_t bytes[3];
	} fdf;

};
struct dvdbc {
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t sct:3,		/* Section type */
		:1,		/* Reserved */
		arb:4;		/* Arbitrary bit */
#else
	uint8_t arb:4,		/* Arbitrary bit */
		:1,		/* Reserved */
		sct:3;		/* Section type */
#endif
#define	DV_SCT_HEADER	0
#define	DV_SCT_SUBCODE	1
#define	DV_SCT_VAUX	2
#define	DV_SCT_AUDIO	3
#define	DV_SCT_VIDEO	4
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t dseq:4,		/* DIF sequence number */
		fsc:1,		/* ID of a DIF block in each channel */
		:3;
#else
	uint8_t :3,
		fsc:1,		/* ID of a DIF block in each channel */
		dseq:4;		/* DIF sequence number */
#endif
	uint8_t dbn;		/* DIF block number */
	uint8_t payload[77];
#define	DV_DSF_12	0x80	/* PAL: payload[0] in Header DIF */
};
