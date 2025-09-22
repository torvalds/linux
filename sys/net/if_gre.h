/*      $OpenBSD: if_gre.h,v 1.17 2025/03/02 21:28:31 bluhm Exp $ */
/*	$NetBSD: if_gre.h,v 1.5 1999/11/19 20:41:19 thorpej Exp $ */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Heiko W.Rupp <hwr@pilhuhn.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NET_IF_GRE_H
#define _NET_IF_GRE_H

struct gre_h {
	u_int16_t flags;	/* GRE flags */
	u_int16_t ptype;	/* protocol type of payload typically
                               Ether protocol type*/
/*
 *  from here on: fields are optional, presence indicated by flags
 *
	u_int_16 checksum	 checksum (one-complements of GRE header
                             and payload
                             Present if (ck_pres | rt_pres == 1).
                             Valid if (ck_pres == 1).
	u_int_16 offset			 offset from start of routing filed to
                             first octet of active SRE (see below).
                             Present if (ck_pres | rt_pres == 1).
                             Valid if (rt_pres == 1).
	u_int_32 key         inserted by encapsulator e.g. for
                             authentication
                             Present if (key_pres ==1 ).
	u_int_32 seq_num     Sequence number to allow for packet order
                             Present if (seq_pres ==1 ).

    struct gre_sre[] routing Routing fields (see below)
                             Present if (rt_pres == 1)
*/
} __packed;

struct greip {
	struct ip gi_i;
	struct gre_h  gi_g;
} __packed;

#define gi_pr           gi_i.ip_p
#define gi_len          gi_i.ip_len
#define gi_src          gi_i.ip_src
#define gi_dst          gi_i.ip_dst
#define gi_ptype	gi_g.ptype
#define gi_flags	gi_g.flags

#define GRE_CP          0x8000  /* Checksum Present */
#define GRE_RP          0x4000  /* Routing Present */
#define GRE_KP          0x2000  /* Key Present */
#define GRE_SP          0x1000  /* Sequence Present */
#define GRE_SS		0x0800	/* Strict Source Route */

/* gre_sre defines a Source route Entry. These are needed if packets
 *  should be routed over more than one tunnel hop by hop
 */

struct gre_sre {
	u_int16_t sre_family;	/* address family */
	u_char  sre_offset;	/* offset to first octet of active entry */
	u_char  sre_length;	/* number of octets in the SRE.
                               sre_lengthl==0 -> last entry. */
	u_char  *sre_rtinfo;	/* the routing information */
};

struct greioctl {
	int unit;
	struct    in_addr addr;
};

/* for mobile encaps */

struct mobile_h {
	u_int16_t proto;		/* protocol and S-bit */
	u_int16_t hcrc;			/* header checksum */
	u_int32_t odst;			/* original destination address */
	u_int32_t osrc;			/* original source addr, if S-bit set */
} __packed;

struct mobip_h {
	struct ip       mi;
	struct mobile_h mh;
} __packed;


#define MOB_H_SIZ_S		(sizeof(struct mobile_h) - sizeof(u_int32_t))
#define MOB_H_SIZ_L		(sizeof(struct mobile_h))
#define MOB_H_SBIT	0x0080

#ifdef _KERNEL
int	gre_sysctl(int *, u_int, void *, size_t *, void *, size_t);
int	gre_input(struct mbuf **, int *, int, int, struct netstack *);
int	gre_input6(struct mbuf **, int *, int, int, struct netstack *);
#endif
#endif /* _NET_IF_GRE_H_ */
