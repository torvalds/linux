/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_CONNTRACK_SNMP_H
#define _NF_CONNTRACK_SNMP_H

#include <linux/netfilter.h>
#include <linux/skbuff.h>

typedef int
nf_nat_snmp_hook_fn(struct sk_buff *skb,
		    unsigned int protoff,
		    struct nf_conn *ct,
		    enum ip_conntrack_info ctinfo);

extern nf_nat_snmp_hook_fn __rcu *nf_nat_snmp_hook;

#endif /* _NF_CONNTRACK_SNMP_H */
