/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *	Linux NET3:	Internet Group Management Protocol  [IGMP]
 *
 *	Authors:
 *		Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	Extended to talk the BSD extended IGMP protocol of mrouted 3.6
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#ifndef _UAPI_LINUX_IGMP_H
#define _UAPI_LINUX_IGMP_H

#include <linux/types.h>
#include <asm/byteorder.h>

/*
 *	IGMP protocol structures
 */

/*
 *	Header in on cable format
 */

struct igmphdr {
	__u8 type;
	__u8 code;		/* For newer IGMP */
	__sum16 csum;
	__be32 group;
};

/* V3 group record types [grec_type] */
#define IGMPV3_MODE_IS_INCLUDE		1
#define IGMPV3_MODE_IS_EXCLUDE		2
#define IGMPV3_CHANGE_TO_INCLUDE	3
#define IGMPV3_CHANGE_TO_EXCLUDE	4
#define IGMPV3_ALLOW_NEW_SOURCES	5
#define IGMPV3_BLOCK_OLD_SOURCES	6

struct igmpv3_grec {
	__u8	grec_type;
	__u8	grec_auxwords;
	__be16	grec_nsrcs;
	__be32	grec_mca;
	__be32	grec_src[0];
};

struct igmpv3_report {
	__u8 type;
	__u8 resv1;
	__sum16 csum;
	__be16 resv2;
	__be16 ngrec;
	struct igmpv3_grec grec[0];
};

struct igmpv3_query {
	__u8 type;
	__u8 code;
	__sum16 csum;
	__be32 group;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 qrv:3,
	     suppress:1,
	     resv:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8 resv:4,
	     suppress:1,
	     qrv:3;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	__u8 qqic;
	__be16 nsrcs;
	__be32 srcs[0];
};

#define IGMP_HOST_MEMBERSHIP_QUERY	0x11	/* From RFC1112 */
#define IGMP_HOST_MEMBERSHIP_REPORT	0x12	/* Ditto */
#define IGMP_DVMRP			0x13	/* DVMRP routing */
#define IGMP_PIM			0x14	/* PIM routing */
#define IGMP_TRACE			0x15
#define IGMPV2_HOST_MEMBERSHIP_REPORT	0x16	/* V2 version of 0x12 */
#define IGMP_HOST_LEAVE_MESSAGE 	0x17
#define IGMPV3_HOST_MEMBERSHIP_REPORT	0x22	/* V3 version of 0x12 */

#define IGMP_MTRACE_RESP		0x1e
#define IGMP_MTRACE			0x1f

#define IGMP_MRDISC_ADV			0x30	/* From RFC4286 */

/*
 *	Use the BSD names for these for compatibility
 */

#define IGMP_DELAYING_MEMBER		0x01
#define IGMP_IDLE_MEMBER		0x02
#define IGMP_LAZY_MEMBER		0x03
#define IGMP_SLEEPING_MEMBER		0x04
#define IGMP_AWAKENING_MEMBER		0x05

#define IGMP_MINLEN			8

#define IGMP_MAX_HOST_REPORT_DELAY	10	/* max delay for response to */
						/* query (in seconds)	*/

#define IGMP_TIMER_SCALE		10	/* denotes that the igmphdr->timer field */
						/* specifies time in 10th of seconds	 */

#define IGMP_AGE_THRESHOLD		400	/* If this host don't hear any IGMP V1	*/
						/* message in this period of time,	*/
						/* revert to IGMP v2 router.		*/

#define IGMP_ALL_HOSTS		htonl(0xE0000001L)
#define IGMP_ALL_ROUTER 	htonl(0xE0000002L)
#define IGMPV3_ALL_MCR	 	htonl(0xE0000016L)
#define IGMP_LOCAL_GROUP	htonl(0xE0000000L)
#define IGMP_LOCAL_GROUP_MASK	htonl(0xFFFFFF00L)

/*
 * struct for keeping the multicast list in
 */

#endif /* _UAPI_LINUX_IGMP_H */
