/*	$OpenBSD: ppp_defs.h,v 1.13 2002/09/13 00:12:07 deraadt Exp $	*/
/*	$NetBSD: ppp_defs.h,v 1.1 1995/07/04 06:28:26 paulus Exp $	*/

/*
 * ppp_defs.h - PPP definitions.
 *
 * Copyright (c) 1989-2002 Paul Mackerras. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name(s) of the authors of this software must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission.
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Paul Mackerras
 *     <paulus@samba.org>".
 *
 * THE AUTHORS OF THIS SOFTWARE DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _PPP_DEFS_H_
#define _PPP_DEFS_H_

/*
 * The basic PPP frame.
 */
#define PPP_HDRLEN	4	/* octets for standard ppp header */
#define PPP_FCSLEN	2	/* octets for FCS */
#define PPP_MRU		1500	/* default MRU = max length of info field */

#define PPP_ADDRESS(p)	(((u_char *)(p))[0])
#define PPP_CONTROL(p)	(((u_char *)(p))[1])
#define PPP_PROTOCOL(p)	((((u_char *)(p))[2] << 8) + ((u_char *)(p))[3])

/*
 * Significant octet values.
 */
#define PPP_ALLSTATIONS	0xff	/* All-Stations broadcast address */
#define PPP_UI		0x03	/* Unnumbered Information */
#define PPP_FLAG	0x7e	/* Flag Sequence */
#define PPP_ESCAPE	0x7d	/* Asynchronous Control Escape */
#define PPP_TRANS	0x20	/* Asynchronous transparency modifier */

/*
 * Protocol field values.
 */
#define PPP_IP		0x21	/* Internet Protocol */
#define PPP_XNS		0x25	/* Xerox NS */
#define PPP_AT		0x29	/* AppleTalk Protocol */
#define PPP_IPX		0x2b	/* Internetwork Packet Exchange */
#define PPP_VJC_COMP	0x2d	/* VJ compressed TCP */
#define PPP_VJC_UNCOMP	0x2f	/* VJ uncompressed TCP */
#define PPP_IPV6	0x57	/* Internet Protocol Version 6 */
#define PPP_COMP	0xfd	/* compressed packet */
#define PPP_IPCP	0x8021	/* IP Control Protocol */
#define PPP_ATCP	0x8029	/* AppleTalk Control Protocol */
#define PPP_IPXCP	0x802b	/* IPX Control Protocol */
#define PPP_IPV6CP	0x8057	/* IPv6 Control Protocol */
#define PPP_CCP		0x80fd	/* Compression Control Protocol */
#define PPP_LCP		0xc021	/* Link Control Protocol */
#define PPP_PAP		0xc023	/* Password Authentication Protocol */
#define PPP_LQR		0xc025	/* Link Quality Report protocol */
#define PPP_CHAP	0xc223	/* Cryptographic Handshake Auth. Protocol */
#define PPP_CBCP	0xc029	/* Callback Control Protocol */

/*
 * Values for FCS calculations.
 */
#define PPP_INITFCS	0xffff	/* Initial FCS value */
#define PPP_GOODFCS	0xf0b8	/* Good final FCS value */
#define PPP_FCS(fcs, c)	(((fcs) >> 8) ^ fcstab[((fcs) ^ (c)) & 0xff])

/*
 * A 32-bit unsigned integral type.
 */
#ifndef __BIT_TYPES_DEFINED__
#ifdef	UINT32_T
typedef UINT32_T	u_int32_t;
#else
typedef unsigned int	u_int32_t;
typedef unsigned short	u_int16_t;
#endif
#endif

/*
 * Extended asyncmap - allows any character to be escaped.
 */
typedef u_int32_t	ext_accm[8];

/*
 * What to do with network protocol (NP) packets.
 */
enum NPmode {
    NPMODE_PASS,		/* pass the packet through */
    NPMODE_DROP,		/* silently drop the packet */
    NPMODE_ERROR,		/* return an error */
    NPMODE_QUEUE		/* save it up for later. */
};

/*
 * Statistics.
 */
struct pppstat	{
    u_int	ppp_ibytes;	/* bytes received */
    u_int	ppp_ipackets;	/* packets received */
    u_int	ppp_ierrors;	/* receive errors */
    u_int	ppp_obytes;	/* bytes sent */
    u_int	ppp_opackets;	/* packets sent */
    u_int	ppp_oerrors;	/* transmit errors */
};

struct vjstat {
    u_int	vjs_packets;	/* outbound packets */
    u_int	vjs_compressed;	/* outbound compressed packets */
    u_int	vjs_searches;	/* searches for connection state */
    u_int	vjs_misses;	/* times couldn't find conn. state */
    u_int	vjs_uncompressedin; /* inbound uncompressed packets */
    u_int	vjs_compressedin;   /* inbound compressed packets */
    u_int	vjs_errorin;	/* inbound unknown type packets */
    u_int	vjs_tossed;	/* inbound packets tossed because of error */
};

struct ppp_stats {
    struct pppstat	p;	/* basic PPP statistics */
    struct vjstat	vj;	/* VJ header compression statistics */
};

struct compstat {
    u_int	unc_bytes;	/* total uncompressed bytes */
    u_int	unc_packets;	/* total uncompressed packets */
    u_int	comp_bytes;	/* compressed bytes */
    u_int	comp_packets;	/* compressed packets */
    u_int	inc_bytes;	/* incompressible bytes */
    u_int	inc_packets;	/* incompressible packets */
    u_int	ratio;		/* recent compression ratio << 8 */
};

struct ppp_comp_stats {
    struct compstat	c;	/* packet compression statistics */
    struct compstat	d;	/* packet decompression statistics */
};

/*
 * The following structure records the time in seconds since
 * the last NP packet was sent or received.
 */
struct ppp_idle {
    time_t xmit_idle;		/* time since last NP packet sent */
    time_t recv_idle;		/* time since last NP packet received */
};

#endif /* _PPP_DEFS_H_ */
