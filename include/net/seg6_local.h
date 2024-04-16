/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  SR-IPv6 implementation
 *
 *  Authors:
 *  David Lebrun <david.lebrun@uclouvain.be>
 *  eBPF support: Mathieu Xhonneux <m.xhonneux@gmail.com>
 */

#ifndef _NET_SEG6_LOCAL_H
#define _NET_SEG6_LOCAL_H

#include <linux/percpu.h>
#include <linux/net.h>
#include <linux/ipv6.h>

extern int seg6_lookup_nexthop(struct sk_buff *skb, struct in6_addr *nhaddr,
			       u32 tbl_id);
extern bool seg6_bpf_has_valid_srh(struct sk_buff *skb);

struct seg6_bpf_srh_state {
	struct ipv6_sr_hdr *srh;
	u16 hdrlen;
	bool valid;
};

DECLARE_PER_CPU(struct seg6_bpf_srh_state, seg6_bpf_srh_states);

#endif
