/*	$OpenBSD: igmp.h,v 1.6 2003/06/02 23:28:13 millert Exp $	*/
/*	$NetBSD: igmp.h,v 1.6 1995/05/31 06:08:21 mycroft Exp $	*/

/*
 * Copyright (c) 1988 Stephen Deering.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Stephen Deering of Stanford University.
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
 *	@(#)igmp.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET_IGMP_H_
#define _NETINET_IGMP_H_

/*
 * Internet Group Management Protocol (IGMP) definitions.
 *
 * MULTICAST 1.3
 */

/*
 * IGMP packet format.
 */
struct igmp {
	u_int8_t	igmp_type;	/* version & type of IGMP message  */
	u_int8_t	igmp_code;	/* code for routing sub-messages   */
	u_int16_t	igmp_cksum;	/* IP-style checksum               */
	struct in_addr	igmp_group;	/* group address being reported    */
};					/*  (zero for queries)             */

#define	IGMP_MINLEN		     8

#define	IGMP_HOST_MEMBERSHIP_QUERY	0x11  /* membership query      */
#define	IGMP_v1_HOST_MEMBERSHIP_REPORT	0x12  /* v1 membership report  */
#define	IGMP_DVMRP			0x13  /* DVMRP routing message */
#define	IGMP_PIM			0x14  /* PIM routing message   */
#define	IGMP_v2_HOST_MEMBERSHIP_REPORT	0x16  /* v2 membership report  */
#define	IGMP_HOST_LEAVE_MESSAGE		0x17  /* leave-group message   */
#define	IGMP_MTRACE_REPLY		0x1e  /* traceroute reply      */
#define	IGMP_MTRACE_QUERY		0x1f  /* traceroute query      */

#define	IGMP_MAX_HOST_REPORT_DELAY	10    /* max delay for response to */
					      /*  query (in seconds)       */

#define	IGMP_TIMER_SCALE		10    /* denominator for igmp_timer */

/*
 * States for the IGMP v2 state table.
 */
#define	IGMP_DELAYING_MEMBER	1
#define	IGMP_IDLE_MEMBER	2
#define	IGMP_LAZY_MEMBER	3
#define	IGMP_SLEEPING_MEMBER	4
#define	IGMP_AWAKENING_MEMBER	5

/*
 * States for IGMP router version cache.
 */
#define	IGMP_v1_ROUTER		1
#define	IGMP_v2_ROUTER		2

/*
 * Revert to v2 if we haven't heard from the router in this amount of time.
 */
#define	IGMP_AGE_THRESHOLD	540

#ifdef _KERNEL
void	rti_delete(struct ifnet *);
#endif /* _KERNEL */
#endif /* _NETINET_IGMP_H_ */
