/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_IPCOMP_H
#define _NET_IPCOMP_H

#include <linux/skbuff.h>

struct ip_comp_hdr;
struct netlink_ext_ack;
struct xfrm_state;

int ipcomp_input(struct xfrm_state *x, struct sk_buff *skb);
int ipcomp_output(struct xfrm_state *x, struct sk_buff *skb);
void ipcomp_destroy(struct xfrm_state *x);
int ipcomp_init_state(struct xfrm_state *x, struct netlink_ext_ack *extack);

static inline struct ip_comp_hdr *ip_comp_hdr(const struct sk_buff *skb)
{
	return (struct ip_comp_hdr *)skb_transport_header(skb);
}

#endif
