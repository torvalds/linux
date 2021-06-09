/*
 *  SR-IPv6 implementation
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _UAPI_LINUX_SEG6_LOCAL_H
#define _UAPI_LINUX_SEG6_LOCAL_H

#include <linux/seg6.h>

enum {
	SEG6_LOCAL_UNSPEC,
	SEG6_LOCAL_ACTION,
	SEG6_LOCAL_SRH,
	SEG6_LOCAL_TABLE,
	SEG6_LOCAL_NH4,
	SEG6_LOCAL_NH6,
	SEG6_LOCAL_IIF,
	SEG6_LOCAL_OIF,
	SEG6_LOCAL_BPF,
	SEG6_LOCAL_VRFTABLE,
	SEG6_LOCAL_COUNTERS,
	__SEG6_LOCAL_MAX,
};
#define SEG6_LOCAL_MAX (__SEG6_LOCAL_MAX - 1)

enum {
	SEG6_LOCAL_ACTION_UNSPEC	= 0,
	/* node segment */
	SEG6_LOCAL_ACTION_END		= 1,
	/* adjacency segment (IPv6 cross-connect) */
	SEG6_LOCAL_ACTION_END_X		= 2,
	/* lookup of next seg NH in table */
	SEG6_LOCAL_ACTION_END_T		= 3,
	/* decap and L2 cross-connect */
	SEG6_LOCAL_ACTION_END_DX2	= 4,
	/* decap and IPv6 cross-connect */
	SEG6_LOCAL_ACTION_END_DX6	= 5,
	/* decap and IPv4 cross-connect */
	SEG6_LOCAL_ACTION_END_DX4	= 6,
	/* decap and lookup of DA in v6 table */
	SEG6_LOCAL_ACTION_END_DT6	= 7,
	/* decap and lookup of DA in v4 table */
	SEG6_LOCAL_ACTION_END_DT4	= 8,
	/* binding segment with insertion */
	SEG6_LOCAL_ACTION_END_B6	= 9,
	/* binding segment with encapsulation */
	SEG6_LOCAL_ACTION_END_B6_ENCAP	= 10,
	/* binding segment with MPLS encap */
	SEG6_LOCAL_ACTION_END_BM	= 11,
	/* lookup last seg in table */
	SEG6_LOCAL_ACTION_END_S		= 12,
	/* forward to SR-unaware VNF with static proxy */
	SEG6_LOCAL_ACTION_END_AS	= 13,
	/* forward to SR-unaware VNF with masquerading */
	SEG6_LOCAL_ACTION_END_AM	= 14,
	/* custom BPF action */
	SEG6_LOCAL_ACTION_END_BPF	= 15,

	__SEG6_LOCAL_ACTION_MAX,
};

#define SEG6_LOCAL_ACTION_MAX (__SEG6_LOCAL_ACTION_MAX - 1)

enum {
	SEG6_LOCAL_BPF_PROG_UNSPEC,
	SEG6_LOCAL_BPF_PROG,
	SEG6_LOCAL_BPF_PROG_NAME,
	__SEG6_LOCAL_BPF_PROG_MAX,
};

#define SEG6_LOCAL_BPF_PROG_MAX (__SEG6_LOCAL_BPF_PROG_MAX - 1)

/* SRv6 Behavior counters are encoded as netlink attributes guaranteeing the
 * correct alignment.
 * Each counter is identified by a different attribute type (i.e.
 * SEG6_LOCAL_CNT_PACKETS).
 *
 * - SEG6_LOCAL_CNT_PACKETS: identifies a counter that counts the number of
 *   packets that have been CORRECTLY processed by an SRv6 Behavior instance
 *   (i.e., packets that generate errors or are dropped are NOT counted).
 *
 * - SEG6_LOCAL_CNT_BYTES: identifies a counter that counts the total amount
 *   of traffic in bytes of all packets that have been CORRECTLY processed by
 *   an SRv6 Behavior instance (i.e., packets that generate errors or are
 *   dropped are NOT counted).
 *
 * - SEG6_LOCAL_CNT_ERRORS: identifies a counter that counts the number of
 *   packets that have NOT been properly processed by an SRv6 Behavior instance
 *   (i.e., packets that generate errors or are dropped).
 */
enum {
	SEG6_LOCAL_CNT_UNSPEC,
	SEG6_LOCAL_CNT_PAD,		/* pad for 64 bits values */
	SEG6_LOCAL_CNT_PACKETS,
	SEG6_LOCAL_CNT_BYTES,
	SEG6_LOCAL_CNT_ERRORS,
	__SEG6_LOCAL_CNT_MAX,
};

#define SEG6_LOCAL_CNT_MAX (__SEG6_LOCAL_CNT_MAX - 1)

#endif
