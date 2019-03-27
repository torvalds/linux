/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996-2000
 * University of Southern California/Information Sciences Institute.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NETINET_PIM_H_
#define _NETINET_PIM_H_

/*
 * Protocol Independent Multicast (PIM) definitions.
 * RFC 2362, June 1998.
 *
 * Written by Ahmed Helmy, USC/SGI, July 1996.
 * Modified by George Edmond Eddy (Rusty), ISI, February 1998.
 * Modified by Pavlin Radoslavov, USC/ISI, May 1998, October 2000.
 */

#include <sys/types.h>

#ifndef _PIM_VT
#ifndef BYTE_ORDER
# error BYTE_ORDER is not defined!
#endif
#if (BYTE_ORDER != BIG_ENDIAN) && (BYTE_ORDER != LITTLE_ENDIAN)
# error BYTE_ORDER must be defined to either BIG_ENDIAN or LITTLE_ENDIAN
#endif
#endif /* ! _PIM_VT */

/*
 * PIM packet header
 */
struct pim {
#ifdef _PIM_VT
	uint8_t		pim_vt;		/* PIM version and message type	*/
#else /* ! _PIM_VT   */
#if BYTE_ORDER == BIG_ENDIAN
	u_int		pim_vers:4,	/* PIM protocol version		*/
			pim_type:4;	/* PIM message type		*/
#endif
#if BYTE_ORDER == LITTLE_ENDIAN
	u_int		pim_type:4,	/* PIM message type		*/
			pim_vers:4;	/* PIM protocol version		*/
#endif
#endif /* ! _PIM_VT  */
	uint8_t		pim_reserved;	/* Reserved			*/
	uint16_t	pim_cksum;	/* IP-style checksum		*/
};
/* KAME-related name backward compatibility */
#define pim_ver pim_vers
#define pim_rsv pim_reserved

#ifdef _PIM_VT
#define PIM_MAKE_VT(v, t)	(0xff & (((v) << 4) | (0x0f & (t))))
#define PIM_VT_V(x)		(((x) >> 4) & 0x0f)
#define PIM_VT_T(x)		((x) & 0x0f)
#endif /* _PIM_VT */

#define PIM_VERSION		2
#define PIM_MINLEN		8	/* PIM message min. length	*/
#define PIM_REG_MINLEN	(PIM_MINLEN+20)	/* PIM Register hdr + inner IPv4 hdr */
#define PIM6_REG_MINLEN	(PIM_MINLEN+40)	/* PIM Register hdr + inner IPv6 hdr */

/*
 * PIM message types
 */
#define PIM_HELLO		0x0	/* PIM-SM and PIM-DM		*/
#define PIM_REGISTER		0x1	/* PIM-SM only			*/
#define PIM_REGISTER_STOP	0x2	/* PIM-SM only			*/
#define PIM_JOIN_PRUNE		0x3	/* PIM-SM and PIM-DM		*/
#define PIM_BOOTSTRAP		0x4	/* PIM-SM only			*/
#define PIM_ASSERT		0x5	/* PIM-SM and PIM-DM		*/
#define PIM_GRAFT		0x6	/* PIM-DM only			*/
#define PIM_GRAFT_ACK		0x7	/* PIM-DM only			*/
#define PIM_CAND_RP_ADV		0x8	/* PIM-SM only			*/
#define PIM_ALL_DF_ELECTION	0xa	/* Bidir-PIM-SM only		*/

/*
 * PIM-Register message flags
 */
#define PIM_BORDER_REGISTER 0x80000000U	/* The Border bit (host-order)	*/
#define PIM_NULL_REGISTER   0x40000000U	/* The Null-Register bit (host-order)*/

/*
 * All-PIM-Routers IPv4 and IPv6 multicast addresses
 */
#define INADDR_ALLPIM_ROUTERS_GROUP	(uint32_t)0xe000000dU  /* 224.0.0.13 */
#define IN6ADDR_LINKLOCAL_ALLPIM_ROUTERS	"ff02::d"
#define IN6ADDR_LINKLOCAL_ALLPIM_ROUTERS_INIT				\
	{{{ 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		\
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d }}}

#endif /* _NETINET_PIM_H_ */
