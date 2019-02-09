/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 	Format of an ARP firewall descriptor
 *
 * 	src, tgt, src_mask, tgt_mask, arpop, arpop_mask are always stored in
 *	network byte order.
 * 	flags are stored in host byte order (of course).
 */
#ifndef _ARPTABLES_H
#define _ARPTABLES_H

#include <linux/if.h>
#include <linux/in.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <uapi/linux/netfilter_arp/arp_tables.h>

/* Standard entry. */
struct arpt_standard {
	struct arpt_entry entry;
	struct xt_standard_target target;
};

struct arpt_error {
	struct arpt_entry entry;
	struct xt_error_target target;
};

#define ARPT_ENTRY_INIT(__size)						       \
{									       \
	.target_offset	= sizeof(struct arpt_entry),			       \
	.next_offset	= (__size),					       \
}

#define ARPT_STANDARD_INIT(__verdict)					       \
{									       \
	.entry		= ARPT_ENTRY_INIT(sizeof(struct arpt_standard)),       \
	.target		= XT_TARGET_INIT(XT_STANDARD_TARGET,		       \
					 sizeof(struct xt_standard_target)), \
	.target.verdict	= -(__verdict) - 1,				       \
}

#define ARPT_ERROR_INIT							       \
{									       \
	.entry		= ARPT_ENTRY_INIT(sizeof(struct arpt_error)),	       \
	.target		= XT_TARGET_INIT(XT_ERROR_TARGET,		       \
					 sizeof(struct xt_error_target)),      \
	.target.errorname = "ERROR",					       \
}

extern void *arpt_alloc_initial_table(const struct xt_table *);
int arpt_register_table(struct net *net, const struct xt_table *table,
			const struct arpt_replace *repl,
			const struct nf_hook_ops *ops, struct xt_table **res);
void arpt_unregister_table(struct net *net, struct xt_table *table,
			   const struct nf_hook_ops *ops);
extern unsigned int arpt_do_table(struct sk_buff *skb,
				  const struct nf_hook_state *state,
				  struct xt_table *table);

#ifdef CONFIG_COMPAT
#include <net/compat.h>

struct compat_arpt_entry {
	struct arpt_arp arp;
	__u16 target_offset;
	__u16 next_offset;
	compat_uint_t comefrom;
	struct compat_xt_counters counters;
	unsigned char elems[0];
};

static inline struct xt_entry_target *
compat_arpt_get_target(struct compat_arpt_entry *e)
{
	return (void *)e + e->target_offset;
}

#endif /* CONFIG_COMPAT */
#endif /* _ARPTABLES_H */
