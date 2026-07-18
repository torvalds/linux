/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CONNTRACK_PROTO_GRE_H
#define _CONNTRACK_PROTO_GRE_H

struct nf_ct_gre {
	unsigned int stream_timeout;
	unsigned int timeout;
};

#include <net/netfilter/nf_conntrack_tuple.h>

struct nf_conn;

/* structure for original <-> reply keymap */
struct nf_ct_gre_keymap {
	struct list_head list;
	struct nf_conntrack_tuple tuple;
	struct rcu_head rcu;
};

/* add tuple->key_reply pairs to keymap */
bool nf_ct_gre_keymap_add(struct nf_conn *ct,
			  const struct nf_conntrack_tuple *orig,
			  const struct nf_conntrack_tuple *repl);

/* delete keymap entries */
void nf_ct_gre_keymap_destroy(struct nf_conn *ct);

bool gre_pkt_to_tuple(const struct sk_buff *skb, unsigned int dataoff,
		      struct net *net, struct nf_conntrack_tuple *tuple);
#endif /* _CONNTRACK_PROTO_GRE_H */
