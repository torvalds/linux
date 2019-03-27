/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1992 The University of Utah and the Center
 *	for Software Science (CSS).
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Center for Software Science of the University of Utah Computer
 * Science Department.  CSS requests users of this software to return
 * to css-dist@cs.utah.edu any improvements that they make and grant
 * CSS redistribution rights.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)rmp.h	8.1 (Berkeley) 6/4/93
 *
 * From: Utah Hdr: rmp.h 3.1 92/07/06
 * Author: Jeff Forys, University of Utah CSS
 *
 * $FreeBSD$
 */

/*
 *  Define MIN/MAX sizes of RMP (ethernet) packet.
 *  For ease of computation, the 4 octet CRC field is not included.
 *
 *  MCLBYTES is for bpfwrite(); it is adamant about using a cluster.
 */

#define	RMP_MAX_PACKET	MIN(1514,MCLBYTES)
#define	RMP_MIN_PACKET	60

/*
 *  Define RMP/Ethernet Multicast address (9:0:9:0:0:4) and its length.
 */
#define	RMP_ADDR	{ 0x9, 0x0, 0x9, 0x0, 0x0, 0x4 }
#define	RMP_ADDRLEN	6

/*
 *  Define IEEE802.2 (Logical Link Control) information.
 */
#define	IEEE_DSAP_HP	0xF8	/* Destination Service Access Point */
#define	IEEE_SSAP_HP	0xF8	/* Source Service Access Point */
#define	IEEE_CNTL_HP	0x0300	/* Type 1 / I format control information */

#define	HPEXT_DXSAP	0x608	/* HP Destination Service Access Point */
#define	HPEXT_SXSAP	0x609	/* HP Source Service Access Point */

/*
 *  802.3-style "Ethernet" header.
 */

struct hp_hdr {
	u_int8_t	daddr[RMP_ADDRLEN];
	u_int8_t	saddr[RMP_ADDRLEN];
	u_int16_t	len;
};

/*
 * HP uses 802.2 LLC with their own local extensions.  This struct makes
 * sense out of this data (encapsulated in the above 802.3 packet).
 */

struct hp_llc {
	u_int8_t	dsap;		/* 802.2 DSAP */
	u_int8_t	ssap;		/* 802.2 SSAP */
	u_int16_t	cntrl;		/* 802.2 control field */
	u_int16_t	filler;		/* HP filler (must be zero) */
	u_int16_t	dxsap;		/* HP extended DSAP */
	u_int16_t	sxsap;		/* HP extended SSAP */
};
