/*
 * (C) 2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <net/netfilter/nf_conntrack.h>

struct nf_conn *nfqnl_ct_get(struct sk_buff *entskb, size_t *size,
			     enum ip_conntrack_info *ctinfo)
{
	struct nfq_ct_hook *nfq_ct;
	struct nf_conn *ct;

	/* rcu_read_lock()ed by __nf_queue already. */
	nfq_ct = rcu_dereference(nfq_ct_hook);
	if (nfq_ct == NULL)
		return NULL;

	ct = nf_ct_get(entskb, ctinfo);
	if (ct) {
		if (!nf_ct_is_untracked(ct))
			*size += nfq_ct->build_size(ct);
		else
			ct = NULL;
	}
	return ct;
}

struct nf_conn *
nfqnl_ct_parse(const struct sk_buff *skb, const struct nlattr *attr,
	       enum ip_conntrack_info *ctinfo)
{
	struct nfq_ct_hook *nfq_ct;
	struct nf_conn *ct;

	/* rcu_read_lock()ed by __nf_queue already. */
	nfq_ct = rcu_dereference(nfq_ct_hook);
	if (nfq_ct == NULL)
		return NULL;

	ct = nf_ct_get(skb, ctinfo);
	if (ct && !nf_ct_is_untracked(ct))
		nfq_ct->parse(attr, ct);

	return ct;
}

int nfqnl_ct_put(struct sk_buff *skb, struct nf_conn *ct,
		 enum ip_conntrack_info ctinfo)
{
	struct nfq_ct_hook *nfq_ct;
	struct nlattr *nest_parms;
	u_int32_t tmp;

	nfq_ct = rcu_dereference(nfq_ct_hook);
	if (nfq_ct == NULL)
		return 0;

	nest_parms = nla_nest_start(skb, NFQA_CT | NLA_F_NESTED);
	if (!nest_parms)
		goto nla_put_failure;

	if (nfq_ct->build(skb, ct) < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);

	tmp = ctinfo;
	if (nla_put_be32(skb, NFQA_CT_INFO, htonl(tmp)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

void nfqnl_ct_seq_adjust(struct sk_buff *skb, struct nf_conn *ct,
			 enum ip_conntrack_info ctinfo, int diff)
{
	struct nfq_ct_hook *nfq_ct;

	nfq_ct = rcu_dereference(nfq_ct_hook);
	if (nfq_ct == NULL)
		return;

	if ((ct->status & IPS_NAT_MASK) && diff)
		nfq_ct->seq_adjust(skb, ct, ctinfo, diff);
}
