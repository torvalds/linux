/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  ebtables
 *
 *	Authors:
 *	Bart De Schuymer		<bdschuym@pandora.be>
 *
 *  ebtables.c,v 2.0, April, 2002
 *
 *  This code is strongly inspired by the iptables code which is
 *  Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 */
#ifndef __LINUX_BRIDGE_EFF_H
#define __LINUX_BRIDGE_EFF_H

#include <linux/if.h>
#include <linux/if_ether.h>
#include <uapi/linux/netfilter_bridge/ebtables.h>

struct ebt_match {
	struct list_head list;
	const char name[EBT_FUNCTION_MAXNAMELEN];
	bool (*match)(const struct sk_buff *skb, const struct net_device *in,
		const struct net_device *out, const struct xt_match *match,
		const void *matchinfo, int offset, unsigned int protoff,
		bool *hotdrop);
	bool (*checkentry)(const char *table, const void *entry,
		const struct xt_match *match, void *matchinfo,
		unsigned int hook_mask);
	void (*destroy)(const struct xt_match *match, void *matchinfo);
	unsigned int matchsize;
	u_int8_t revision;
	u_int8_t family;
	struct module *me;
};

struct ebt_watcher {
	struct list_head list;
	const char name[EBT_FUNCTION_MAXNAMELEN];
	unsigned int (*target)(struct sk_buff *skb,
		const struct net_device *in, const struct net_device *out,
		unsigned int hook_num, const struct xt_target *target,
		const void *targinfo);
	bool (*checkentry)(const char *table, const void *entry,
		const struct xt_target *target, void *targinfo,
		unsigned int hook_mask);
	void (*destroy)(const struct xt_target *target, void *targinfo);
	unsigned int targetsize;
	u_int8_t revision;
	u_int8_t family;
	struct module *me;
};

struct ebt_target {
	struct list_head list;
	const char name[EBT_FUNCTION_MAXNAMELEN];
	/* returns one of the standard EBT_* verdicts */
	unsigned int (*target)(struct sk_buff *skb,
		const struct net_device *in, const struct net_device *out,
		unsigned int hook_num, const struct xt_target *target,
		const void *targinfo);
	bool (*checkentry)(const char *table, const void *entry,
		const struct xt_target *target, void *targinfo,
		unsigned int hook_mask);
	void (*destroy)(const struct xt_target *target, void *targinfo);
	unsigned int targetsize;
	u_int8_t revision;
	u_int8_t family;
	struct module *me;
};

/* used for jumping from and into user defined chains (udc) */
struct ebt_chainstack {
	struct ebt_entries *chaininfo; /* pointer to chain data */
	struct ebt_entry *e; /* pointer to entry data */
	unsigned int n; /* n'th entry */
};

struct ebt_table_info {
	/* total size of the entries */
	unsigned int entries_size;
	unsigned int nentries;
	/* pointers to the start of the chains */
	struct ebt_entries *hook_entry[NF_BR_NUMHOOKS];
	/* room to maintain the stack used for jumping from and into udc */
	struct ebt_chainstack **chainstack;
	char *entries;
	struct ebt_counter counters[] ____cacheline_aligned;
};

struct ebt_table {
	struct list_head list;
	char name[EBT_TABLE_MAXNAMELEN];
	struct ebt_replace_kernel *table;
	unsigned int valid_hooks;
	rwlock_t lock;
	/* the data used by the kernel */
	struct ebt_table_info *private;
	struct nf_hook_ops *ops;
	struct module *me;
};

#define EBT_ALIGN(s) (((s) + (__alignof__(struct _xt_align)-1)) & \
		     ~(__alignof__(struct _xt_align)-1))

extern int ebt_register_table(struct net *net,
			      const struct ebt_table *table,
			      const struct nf_hook_ops *ops);
extern void ebt_unregister_table(struct net *net, const char *tablename);
void ebt_unregister_table_pre_exit(struct net *net, const char *tablename);
extern unsigned int ebt_do_table(void *priv, struct sk_buff *skb,
				 const struct nf_hook_state *state);

/* True if the hook mask denotes that the rule is in a base chain,
 * used in the check() functions */
#define BASE_CHAIN (par->hook_mask & (1 << NF_BR_NUMHOOKS))
/* Clear the bit in the hook mask that tells if the rule is on a base chain */
#define CLEAR_BASE_CHAIN_BIT (par->hook_mask &= ~(1 << NF_BR_NUMHOOKS))

static inline bool ebt_invalid_target(int target)
{
	return (target < -NUM_STANDARD_TARGETS || target >= 0);
}

int ebt_register_template(const struct ebt_table *t, int(*table_init)(struct net *net));
void ebt_unregister_template(const struct ebt_table *t);
#endif
