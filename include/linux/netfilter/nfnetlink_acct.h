#ifndef _NFNL_ACCT_H_
#define _NFNL_ACCT_H_

#ifndef NFACCT_NAME_MAX
#define NFACCT_NAME_MAX		32
#endif

enum nfnl_acct_msg_types {
	NFNL_MSG_ACCT_NEW,
	NFNL_MSG_ACCT_GET,
	NFNL_MSG_ACCT_GET_CTRZERO,
	NFNL_MSG_ACCT_DEL,
	NFNL_MSG_ACCT_MAX
};

enum nfnl_acct_type {
	NFACCT_UNSPEC,
	NFACCT_NAME,
	NFACCT_PKTS,
	NFACCT_BYTES,
	NFACCT_USE,
	__NFACCT_MAX
};
#define NFACCT_MAX (__NFACCT_MAX - 1)

#ifdef __KERNEL__

struct nf_acct;

extern struct nf_acct *nfnl_acct_find_get(const char *filter_name);
extern void nfnl_acct_put(struct nf_acct *acct);
extern void nfnl_acct_update(const struct sk_buff *skb, struct nf_acct *nfacct);

#endif /* __KERNEL__ */

#endif /* _NFNL_ACCT_H */
