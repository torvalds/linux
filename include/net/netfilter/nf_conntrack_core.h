/*
 * This header is used to share core functionality between the
 * standalone connection tracking module, and the compatibility layer's use
 * of connection tracking.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalize L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack_core.h
 */

#ifndef _NF_CONNTRACK_CORE_H
#define _NF_CONNTRACK_CORE_H

#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_ecache.h>

/* This header is used to share core functionality between the
   standalone connection tracking module, and the compatibility layer's use
   of connection tracking. */
extern unsigned int nf_conntrack_in(int pf,
				    unsigned int hooknum,
				    struct sk_buff *skb);

extern int nf_conntrack_init(void);
extern void nf_conntrack_cleanup(void);

extern int nf_conntrack_proto_init(void);
extern void nf_conntrack_proto_fini(void);

extern bool
nf_ct_get_tuple(const struct sk_buff *skb,
		unsigned int nhoff,
		unsigned int dataoff,
		u_int16_t l3num,
		u_int8_t protonum,
		struct nf_conntrack_tuple *tuple,
		const struct nf_conntrack_l3proto *l3proto,
		const struct nf_conntrack_l4proto *l4proto);

extern bool
nf_ct_invert_tuple(struct nf_conntrack_tuple *inverse,
		   const struct nf_conntrack_tuple *orig,
		   const struct nf_conntrack_l3proto *l3proto,
		   const struct nf_conntrack_l4proto *l4proto);

/* Find a connection corresponding to a tuple. */
extern struct nf_conntrack_tuple_hash *
nf_conntrack_find_get(const struct nf_conntrack_tuple *tuple);

extern int __nf_conntrack_confirm(struct sk_buff *skb);

/* Confirm a connection: returns NF_DROP if packet must be dropped. */
static inline int nf_conntrack_confirm(struct sk_buff *skb)
{
	struct nf_conn *ct = (struct nf_conn *)skb->nfct;
	int ret = NF_ACCEPT;

	if (ct) {
		if (!nf_ct_is_confirmed(ct) && !nf_ct_is_dying(ct))
			ret = __nf_conntrack_confirm(skb);
		nf_ct_deliver_cached_events(ct);
	}
	return ret;
}

int
print_tuple(struct seq_file *s, const struct nf_conntrack_tuple *tuple,
            const struct nf_conntrack_l3proto *l3proto,
            const struct nf_conntrack_l4proto *proto);

extern struct hlist_head *nf_conntrack_hash;
extern spinlock_t nf_conntrack_lock ;
extern struct hlist_head unconfirmed;

#endif /* _NF_CONNTRACK_CORE_H */
