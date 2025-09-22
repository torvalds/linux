/*	$OpenBSD: routed.h,v 1.3 2003/06/02 19:34:12 millert Exp $	*/

/*-
 * Copyright (c) 1983, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)routed.h	8.1 (Berkeley) 6/2/93
 *
 */

#ifndef _ROUTED_H_
#define	_ROUTED_H_

/*
 * Routing Information Protocol
 *
 * Derived from Xerox NS Routing Information Protocol
 * by changing 32-bit net numbers to sockaddr's and
 * padding stuff to 32-bit boundaries.
 */

#define	RIPv1		1
#define	RIPv2		2
#ifndef RIPVERSION
#define	RIPVERSION	RIPv1
#endif

#define RIP_PORT	520

#if RIPVERSION == 1
/* Note that this so called sockaddr has a 2-byte sa_family and no sa_len.
 * It is not a UNIX sockaddr, but the shape of an address as defined
 * in RIPv1.  It is still defined to allow old versions of programs
 * such as `gated` to use this file to define RIPv1.
 */
struct netinfo {
	struct	sockaddr rip_dst;	/* destination net/host */
	u_int32_t   rip_metric;		/* cost of route */
};
#else
struct netinfo {
	u_int16_t   n_family;
#define	    RIP_AF_INET	    htons(AF_INET)
#define	    RIP_AF_UNSPEC   0
#define	    RIP_AF_AUTH	    0xffff
	u_int16_t   n_tag;		/* optional in RIPv2 */
	u_int32_t   n_dst;		/* destination net or host */
#define	    RIP_DEFAULT	    0
	u_int32_t   n_mask;		/* netmask in RIPv2 */
	u_int32_t   n_nhop;		/* optional next hop in RIPv2 */
	u_int32_t   n_metric;		/* cost of route */
};
#endif

/* RIPv2 authentication */
struct netauth {
	u_int16_t   a_type;
#define	    RIP_AUTH_PW	    htons(2)	/* password type */
	union {
#define	    RIP_AUTH_PW_LEN 16
	    int8_t    au_pw[RIP_AUTH_PW_LEN];
	} au;
};

struct rip {
	u_int8_t    rip_cmd;		/* request/response */
	u_int8_t    rip_vers;		/* protocol version # */
	u_int16_t   rip_res1;		/* pad to 32-bit boundary */
	union {				/* variable length... */
	    struct netinfo ru_nets[1];
	    int8_t    ru_tracefile[1];
	    struct netauth ru_auth[1];
	} ripun;
#define	rip_nets	ripun.ru_nets
#define	rip_tracefile	ripun.ru_tracefile
};

/* Packet types.
 */
#define	RIPCMD_REQUEST		1	/* want info */
#define	RIPCMD_RESPONSE		2	/* responding to request */
#define	RIPCMD_TRACEON		3	/* turn tracing on */
#define	RIPCMD_TRACEOFF		4	/* turn it off */

/* Gated extended RIP to include a "poll" command instead of using
 * RIPCMD_REQUEST with (RIP_AF_UNSPEC, RIP_DEFAULT).  RFC 1058 says
 * command 5 is used by Sun Microsystems for its own purposes.
 */
#define RIPCMD_POLL		5

#define	RIPCMD_MAX		6

#ifdef RIPCMDS
char *ripcmds[RIPCMD_MAX] = {
	"#0", "REQUEST", "RESPONSE", "TRACEON", "TRACEOFF"
};
#endif

#define	HOPCNT_INFINITY		16
#define	MAXPACKETSIZE		512	/* max broadcast size */
#define NETS_LEN ((MAXPACKETSIZE-sizeof(struct rip))	\
		      / sizeof(struct netinfo) +1)

#define INADDR_RIP_GROUP __IPADDR(0xe0000009)	/* 224.0.0.9 */


/* Timer values used in managing the routing table.
 *
 * Complete tables are broadcast every SUPPLY_INTERVAL seconds.
 * If changes occur between updates, dynamic updates containing only changes
 * may be sent.  When these are sent, a timer is set for a random value
 * between MIN_WAITTIME and MAX_WAITTIME, and no additional dynamic updates
 * are sent until the timer expires.
 *
 * Every update of a routing entry forces an entry's timer to be reset.
 * After EXPIRE_TIME without updates, the entry is marked invalid,
 * but held onto until GARBAGE_TIME so that others may see it, to
 * "poison" the bad route.
 */
#define	SUPPLY_INTERVAL		30	/* time to supply tables */
#define	MIN_WAITTIME		2	/* min sec until next flash updates */
#define	MAX_WAITTIME		5	/* max sec until flash update */

#define STALE_TIME		90	/* switch to a new gateway */
#define	EXPIRE_TIME		180	/* time to mark entry invalid */
#define	GARBAGE_TIME		240	/* time to garbage collect */

#endif /* !_ROUTED_H_ */
