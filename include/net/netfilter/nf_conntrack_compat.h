#ifndef _NF_CONNTRACK_COMPAT_H
#define _NF_CONNTRACK_COMPAT_H

#ifdef __KERNEL__

#if defined(CONFIG_IP_NF_CONNTRACK) || defined(CONFIG_IP_NF_CONNTRACK_MODULE)

#include <linux/netfilter_ipv4/ip_conntrack.h>

#ifdef CONFIG_IP_NF_CONNTRACK_MARK
static inline u_int32_t *nf_ct_get_mark(const struct sk_buff *skb,
					u_int32_t *ctinfo)
{
	struct ip_conntrack *ct = ip_conntrack_get(skb, ctinfo);

	if (ct)
		return &ct->mark;
	else
		return NULL;
}
#endif /* CONFIG_IP_NF_CONNTRACK_MARK */

#ifdef CONFIG_IP_NF_CT_ACCT
static inline struct ip_conntrack_counter *
nf_ct_get_counters(const struct sk_buff *skb)
{
	enum ip_conntrack_info ctinfo;
	struct ip_conntrack *ct = ip_conntrack_get(skb, &ctinfo);

	if (ct)
		return ct->counters;
	else
		return NULL;
}
#endif /* CONFIG_IP_NF_CT_ACCT */

static inline int nf_ct_is_untracked(const struct sk_buff *skb)
{
	return (skb->nfct == &ip_conntrack_untracked.ct_general);
}

static inline void nf_ct_untrack(struct sk_buff *skb)
{
	skb->nfct = &ip_conntrack_untracked.ct_general;
}

static inline int nf_ct_get_ctinfo(const struct sk_buff *skb,
				   enum ip_conntrack_info *ctinfo)
{
	struct ip_conntrack *ct = ip_conntrack_get(skb, ctinfo);
	return (ct != NULL);
}

#else /* CONFIG_IP_NF_CONNTRACK */

#include <net/netfilter/ipv4/nf_conntrack_ipv4.h>
#include <net/netfilter/nf_conntrack.h>

#ifdef CONFIG_NF_CONNTRACK_MARK

static inline u_int32_t *nf_ct_get_mark(const struct sk_buff *skb,
					u_int32_t *ctinfo)
{
	struct nf_conn *ct = nf_ct_get(skb, ctinfo);

	if (ct)
		return &ct->mark;
	else
		return NULL;
}
#endif /* CONFIG_NF_CONNTRACK_MARK */

#ifdef CONFIG_NF_CT_ACCT
static inline struct ip_conntrack_counter *
nf_ct_get_counters(const struct sk_buff *skb)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);

	if (ct)
		return ct->counters;
	else
		return NULL;
}
#endif /* CONFIG_NF_CT_ACCT */

static inline int nf_ct_is_untracked(const struct sk_buff *skb)
{
	return (skb->nfct == &nf_conntrack_untracked.ct_general);
}

static inline void nf_ct_untrack(struct sk_buff *skb)
{
	skb->nfct = &nf_conntrack_untracked.ct_general;
}

static inline int nf_ct_get_ctinfo(const struct sk_buff *skb,
				   enum ip_conntrack_info *ctinfo)
{
	struct nf_conn *ct = nf_ct_get(skb, ctinfo);
	return (ct != NULL);
}

#endif /* CONFIG_IP_NF_CONNTRACK */

#endif /* __KERNEL__ */

#endif /* _NF_CONNTRACK_COMPAT_H */
