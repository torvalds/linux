/*
 *  SR-IPv6 implementation
 *
 *  Authors:
 *  David Lebrun <david.lebrun@uclouvain.be>
 *  eBPF support: Mathieu Xhonneux <m.xhonneux@gmail.com>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _NET_SEG6_LOCAL_H
#define _NET_SEG6_LOCAL_H

#include <linux/percpu.h>
#include <linux/net.h>
#include <linux/ipv6.h>

extern int seg6_lookup_nexthop(struct sk_buff *skb, struct in6_addr *nhaddr,
			       u32 tbl_id);

struct seg6_bpf_srh_state {
	bool valid;
	u16 hdrlen;
};

DECLARE_PER_CPU(struct seg6_bpf_srh_state, seg6_bpf_srh_states);

#endif
