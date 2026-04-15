/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_AMANDA_H
#define _NF_CONNTRACK_AMANDA_H
/* AMANDA tracking. */

#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <net/netfilter/nf_conntrack_expect.h>

typedef unsigned int
nf_nat_amanda_hook_fn(struct sk_buff *skb,
		      enum ip_conntrack_info ctinfo,
		      unsigned int protoff,
		      unsigned int matchoff,
		      unsigned int matchlen,
		      struct nf_conntrack_expect *exp);

extern nf_nat_amanda_hook_fn __rcu *nf_nat_amanda_hook;
#endif /* _NF_CONNTRACK_AMANDA_H */
