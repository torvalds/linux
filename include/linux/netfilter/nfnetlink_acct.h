/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NFNL_ACCT_H_
#define _NFNL_ACCT_H_

#include <uapi/linux/netfilter/nfnetlink_acct.h>
#include <net/net_namespace.h>

enum {
	NFACCT_NO_QUOTA		= -1,
	NFACCT_UNDERQUOTA,
	NFACCT_OVERQUOTA,
};

struct nf_acct;

struct nf_acct *nfnl_acct_find_get(struct net *net, const char *filter_name);
void nfnl_acct_put(struct nf_acct *acct);
void nfnl_acct_update(const struct sk_buff *skb, struct nf_acct *nfacct);
int nfnl_acct_overquota(struct net *net, struct nf_acct *nfacct);
#endif /* _NFNL_ACCT_H */
