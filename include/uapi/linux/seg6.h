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

#ifndef _UAPI_LINUX_SEG6_H
#define _UAPI_LINUX_SEG6_H

#include <linux/types.h>
#include <linux/in6.h>		/* For struct in6_addr. */

/*
 * SRH
 */
struct ipv6_sr_hdr {
	__u8	nexthdr;
	__u8	hdrlen;
	__u8	type;
	__u8	segments_left;
	__u8	first_segment;
	__u8	flags;
	__u16	reserved;

	struct in6_addr segments[0];
};

#define SR6_FLAG1_PROTECTED	(1 << 6)
#define SR6_FLAG1_OAM		(1 << 5)
#define SR6_FLAG1_ALERT		(1 << 4)
#define SR6_FLAG1_HMAC		(1 << 3)

#define SR6_TLV_INGRESS		1
#define SR6_TLV_EGRESS		2
#define SR6_TLV_OPAQUE		3
#define SR6_TLV_PADDING		4
#define SR6_TLV_HMAC		5

#define sr_has_hmac(srh) ((srh)->flags & SR6_FLAG1_HMAC)

struct sr6_tlv {
	__u8 type;
	__u8 len;
	__u8 data[0];
};

#endif
