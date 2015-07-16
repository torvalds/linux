#ifndef _NFNL_ACCT_H_
#define _NFNL_ACCT_H_

#include <uapi/linux/netfilter/nfnetlink_acct.h>

enum {
	NFACCT_NO_QUOTA		= -1,
	NFACCT_UNDERQUOTA,
	NFACCT_OVERQUOTA,
};

struct nf_acct;

struct nf_acct *nfnl_acct_find_get(const char *filter_name);
void nfnl_acct_put(struct nf_acct *acct);
void nfnl_acct_update(const struct sk_buff *skb, struct nf_acct *nfacct);
extern int nfnl_acct_overquota(const struct sk_buff *skb,
			      struct nf_acct *nfacct);
#endif /* _NFNL_ACCT_H */
