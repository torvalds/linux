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
#include <net/netfilter/nf_conntrack_protocol.h>

/* This header is used to share core functionality between the
   standalone connection tracking module, and the compatibility layer's use
   of connection tracking. */
extern unsigned int nf_conntrack_in(int pf,
				    unsigned int hooknum,
				    struct sk_buff **pskb);

extern int nf_conntrack_init(void);
extern void nf_conntrack_cleanup(void);

struct nf_conntrack_l3proto;
extern struct nf_conntrack_l3proto *nf_ct_find_l3proto(u_int16_t pf);
/* Like above, but you already have conntrack read lock. */
extern struct nf_conntrack_l3proto *__nf_ct_find_l3proto(u_int16_t l3proto);

struct nf_conntrack_protocol;

extern int
nf_ct_get_tuple(const struct sk_buff *skb,
		unsigned int nhoff,
		unsigned int dataoff,
		u_int16_t l3num,
		u_int8_t protonum,
		struct nf_conntrack_tuple *tuple,
		const struct nf_conntrack_l3proto *l3proto,
		const struct nf_conntrack_protocol *protocol);

extern int
nf_ct_invert_tuple(struct nf_conntrack_tuple *inverse,
		   const struct nf_conntrack_tuple *orig,
		   const struct nf_conntrack_l3proto *l3proto,
		   const struct nf_conntrack_protocol *protocol);

/* Find a connection corresponding to a tuple. */
extern struct nf_conntrack_tuple_hash *
nf_conntrack_find_get(const struct nf_conntrack_tuple *tuple,
		      const struct nf_conn *ignored_conntrack);

extern int __nf_conntrack_confirm(struct sk_buff **pskb);

/* Confirm a connection: returns NF_DROP if packet must be dropped. */
static inline int nf_conntrack_confirm(struct sk_buff **pskb)
{
	struct nf_conn *ct = (struct nf_conn *)(*pskb)->nfct;
	int ret = NF_ACCEPT;

	if (ct) {
		if (!nf_ct_is_confirmed(ct))
			ret = __nf_conntrack_confirm(pskb);
		nf_ct_deliver_cached_events(ct);
	}
	return ret;
}

extern void __nf_conntrack_attach(struct sk_buff *nskb, struct sk_buff *skb);

int
print_tuple(struct seq_file *s, const struct nf_conntrack_tuple *tuple,
	    struct nf_conntrack_l3proto *l3proto,
	    struct nf_conntrack_protocol *proto);

extern struct list_head *nf_conntrack_hash;
extern struct list_head nf_conntrack_expect_list;
extern rwlock_t nf_conntrack_lock ;
extern struct list_head unconfirmed;

#endif /* _NF_CONNTRACK_CORE_H */
