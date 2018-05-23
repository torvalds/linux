/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_NAT_CORE_H
#define _NF_NAT_CORE_H
#include <linux/list.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat.h>

/* This header used to share core functionality between the standalone
   NAT module, and the compatibility layer's use of NAT for masquerading. */

unsigned int nf_nat_packet(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			   unsigned int hooknum, struct sk_buff *skb);

unsigned int
nf_nat_inet_fn(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state);

int nf_xfrm_me_harder(struct net *net, struct sk_buff *skb, unsigned int family);

static inline int nf_nat_initialized(struct nf_conn *ct,
				     enum nf_nat_manip_type manip)
{
	if (manip == NF_NAT_MANIP_SRC)
		return ct->status & IPS_SRC_NAT_DONE;
	else
		return ct->status & IPS_DST_NAT_DONE;
}

#endif /* _NF_NAT_CORE_H */
