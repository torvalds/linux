/*
 *  ebtables
 *
 *	Authors:
 *	Bart De Schuymer		<bdschuym@pandora.be>
 *
 *  ebtables.c,v 2.0, April, 2002
 *
 *  This code is stongly inspired on the iptables code which is
 *  Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 */

#ifndef __LINUX_BRIDGE_EFF_H
#define __LINUX_BRIDGE_EFF_H
#include <linux/if.h>
#include <linux/netfilter_bridge.h>
#include <linux/if_ether.h>

#define EBT_TABLE_MAXNAMELEN 32
#define EBT_CHAIN_MAXNAMELEN EBT_TABLE_MAXNAMELEN
#define EBT_FUNCTION_MAXNAMELEN EBT_TABLE_MAXNAMELEN

/* verdicts >0 are "branches" */
#define EBT_ACCEPT   -1
#define EBT_DROP     -2
#define EBT_CONTINUE -3
#define EBT_RETURN   -4
#define NUM_STANDARD_TARGETS   4
/* ebtables target modules store the verdict inside an int. We can
 * reclaim a part of this int for backwards compatible extensions.
 * The 4 lsb are more than enough to store the verdict. */
#define EBT_VERDICT_BITS 0x0000000F

struct xt_match;
struct xt_target;

struct ebt_counter
{
	uint64_t pcnt;
	uint64_t bcnt;
};

struct ebt_replace
{
	char name[EBT_TABLE_MAXNAMELEN];
	unsigned int valid_hooks;
	/* nr of rules in the table */
	unsigned int nentries;
	/* total size of the entries */
	unsigned int entries_size;
	/* start of the chains */
	struct ebt_entries __user *hook_entry[NF_BR_NUMHOOKS];
	/* nr of counters userspace expects back */
	unsigned int num_counters;
	/* where the kernel will put the old counters */
	struct ebt_counter __user *counters;
	char __user *entries;
};

struct ebt_replace_kernel
{
	char name[EBT_TABLE_MAXNAMELEN];
	unsigned int valid_hooks;
	/* nr of rules in the table */
	unsigned int nentries;
	/* total size of the entries */
	unsigned int entries_size;
	/* start of the chains */
	struct ebt_entries *hook_entry[NF_BR_NUMHOOKS];
	/* nr of counters userspace expects back */
	unsigned int num_counters;
	/* where the kernel will put the old counters */
	struct ebt_counter *counters;
	char *entries;
};

struct ebt_entries {
	/* this field is always set to zero
	 * See EBT_ENTRY_OR_ENTRIES.
	 * Must be same size as ebt_entry.bitmask */
	unsigned int distinguisher;
	/* the chain name */
	char name[EBT_CHAIN_MAXNAMELEN];
	/* counter offset for this chain */
	unsigned int counter_offset;
	/* one standard (accept, drop, return) per hook */
	int policy;
	/* nr. of entries */
	unsigned int nentries;
	/* entry list */
	char data[0] __attribute__ ((aligned (__alignof__(struct ebt_replace))));
};

/* used for the bitmask of struct ebt_entry */

/* This is a hack to make a difference between an ebt_entry struct and an
 * ebt_entries struct when traversing the entries from start to end.
 * Using this simplifies the code alot, while still being able to use
 * ebt_entries.
 * Contrary, iptables doesn't use something like ebt_entries and therefore uses
 * different techniques for naming the policy and such. So, iptables doesn't
 * need a hack like this.
 */
#define EBT_ENTRY_OR_ENTRIES 0x01
/* these are the normal masks */
#define EBT_NOPROTO 0x02
#define EBT_802_3 0x04
#define EBT_SOURCEMAC 0x08
#define EBT_DESTMAC 0x10
#define EBT_F_MASK (EBT_NOPROTO | EBT_802_3 | EBT_SOURCEMAC | EBT_DESTMAC \
   | EBT_ENTRY_OR_ENTRIES)

#define EBT_IPROTO 0x01
#define EBT_IIN 0x02
#define EBT_IOUT 0x04
#define EBT_ISOURCE 0x8
#define EBT_IDEST 0x10
#define EBT_ILOGICALIN 0x20
#define EBT_ILOGICALOUT 0x40
#define EBT_INV_MASK (EBT_IPROTO | EBT_IIN | EBT_IOUT | EBT_ILOGICALIN \
   | EBT_ILOGICALOUT | EBT_ISOURCE | EBT_IDEST)

struct ebt_entry_match
{
	union {
		char name[EBT_FUNCTION_MAXNAMELEN];
		struct xt_match *match;
	} u;
	/* size of data */
	unsigned int match_size;
	unsigned char data[0] __attribute__ ((aligned (__alignof__(struct ebt_replace))));
};

struct ebt_entry_watcher
{
	union {
		char name[EBT_FUNCTION_MAXNAMELEN];
		struct xt_target *watcher;
	} u;
	/* size of data */
	unsigned int watcher_size;
	unsigned char data[0] __attribute__ ((aligned (__alignof__(struct ebt_replace))));
};

struct ebt_entry_target
{
	union {
		char name[EBT_FUNCTION_MAXNAMELEN];
		struct xt_target *target;
	} u;
	/* size of data */
	unsigned int target_size;
	unsigned char data[0] __attribute__ ((aligned (__alignof__(struct ebt_replace))));
};

#define EBT_STANDARD_TARGET "standard"
struct ebt_standard_target
{
	struct ebt_entry_target target;
	int verdict;
};

/* one entry */
struct ebt_entry {
	/* this needs to be the first field */
	unsigned int bitmask;
	unsigned int invflags;
	__be16 ethproto;
	/* the physical in-dev */
	char in[IFNAMSIZ];
	/* the logical in-dev */
	char logical_in[IFNAMSIZ];
	/* the physical out-dev */
	char out[IFNAMSIZ];
	/* the logical out-dev */
	char logical_out[IFNAMSIZ];
	unsigned char sourcemac[ETH_ALEN];
	unsigned char sourcemsk[ETH_ALEN];
	unsigned char destmac[ETH_ALEN];
	unsigned char destmsk[ETH_ALEN];
	/* sizeof ebt_entry + matches */
	unsigned int watchers_offset;
	/* sizeof ebt_entry + matches + watchers */
	unsigned int target_offset;
	/* sizeof ebt_entry + matches + watchers + target */
	unsigned int next_offset;
	unsigned char elems[0] __attribute__ ((aligned (__alignof__(struct ebt_replace))));
};

/* {g,s}etsockopt numbers */
#define EBT_BASE_CTL            128

#define EBT_SO_SET_ENTRIES      (EBT_BASE_CTL)
#define EBT_SO_SET_COUNTERS     (EBT_SO_SET_ENTRIES+1)
#define EBT_SO_SET_MAX          (EBT_SO_SET_COUNTERS+1)

#define EBT_SO_GET_INFO         (EBT_BASE_CTL)
#define EBT_SO_GET_ENTRIES      (EBT_SO_GET_INFO+1)
#define EBT_SO_GET_INIT_INFO    (EBT_SO_GET_ENTRIES+1)
#define EBT_SO_GET_INIT_ENTRIES (EBT_SO_GET_INIT_INFO+1)
#define EBT_SO_GET_MAX          (EBT_SO_GET_INIT_ENTRIES+1)

#ifdef __KERNEL__

/* return values for match() functions */
#define EBT_MATCH 0
#define EBT_NOMATCH 1

struct ebt_match
{
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

struct ebt_watcher
{
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

struct ebt_target
{
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
struct ebt_chainstack
{
	struct ebt_entries *chaininfo; /* pointer to chain data */
	struct ebt_entry *e; /* pointer to entry data */
	unsigned int n; /* n'th entry */
};

struct ebt_table_info
{
	/* total size of the entries */
	unsigned int entries_size;
	unsigned int nentries;
	/* pointers to the start of the chains */
	struct ebt_entries *hook_entry[NF_BR_NUMHOOKS];
	/* room to maintain the stack used for jumping from and into udc */
	struct ebt_chainstack **chainstack;
	char *entries;
	struct ebt_counter counters[0] ____cacheline_aligned;
};

struct ebt_table
{
	struct list_head list;
	char name[EBT_TABLE_MAXNAMELEN];
	struct ebt_replace_kernel *table;
	unsigned int valid_hooks;
	rwlock_t lock;
	/* e.g. could be the table explicitly only allows certain
	 * matches, targets, ... 0 == let it in */
	int (*check)(const struct ebt_table_info *info,
	   unsigned int valid_hooks);
	/* the data used by the kernel */
	struct ebt_table_info *private;
	struct module *me;
};

#define EBT_ALIGN(s) (((s) + (__alignof__(struct ebt_replace)-1)) & \
		     ~(__alignof__(struct ebt_replace)-1))
extern struct ebt_table *ebt_register_table(struct net *net,
					    const struct ebt_table *table);
extern void ebt_unregister_table(struct ebt_table *table);
extern unsigned int ebt_do_table(unsigned int hook, struct sk_buff *skb,
   const struct net_device *in, const struct net_device *out,
   struct ebt_table *table);

/* Used in the kernel match() functions */
#define FWINV(bool,invflg) ((bool) ^ !!(info->invflags & invflg))
/* True if the hook mask denotes that the rule is in a base chain,
 * used in the check() functions */
#define BASE_CHAIN (par->hook_mask & (1 << NF_BR_NUMHOOKS))
/* Clear the bit in the hook mask that tells if the rule is on a base chain */
#define CLEAR_BASE_CHAIN_BIT (par->hook_mask &= ~(1 << NF_BR_NUMHOOKS))
/* True if the target is not a standard target */
#define INVALID_TARGET (info->target < -NUM_STANDARD_TARGETS || info->target >= 0)

#endif /* __KERNEL__ */

/* blatently stolen from ip_tables.h
 * fn returns 0 to continue iteration */
#define EBT_MATCH_ITERATE(e, fn, args...)                   \
({                                                          \
	unsigned int __i;                                   \
	int __ret = 0;                                      \
	struct ebt_entry_match *__match;                    \
	                                                    \
	for (__i = sizeof(struct ebt_entry);                \
	     __i < (e)->watchers_offset;                    \
	     __i += __match->match_size +                   \
	     sizeof(struct ebt_entry_match)) {              \
		__match = (void *)(e) + __i;                \
		                                            \
		__ret = fn(__match , ## args);              \
		if (__ret != 0)                             \
			break;                              \
	}                                                   \
	if (__ret == 0) {                                   \
		if (__i != (e)->watchers_offset)            \
			__ret = -EINVAL;                    \
	}                                                   \
	__ret;                                              \
})

#define EBT_WATCHER_ITERATE(e, fn, args...)                 \
({                                                          \
	unsigned int __i;                                   \
	int __ret = 0;                                      \
	struct ebt_entry_watcher *__watcher;                \
	                                                    \
	for (__i = e->watchers_offset;                      \
	     __i < (e)->target_offset;                      \
	     __i += __watcher->watcher_size +               \
	     sizeof(struct ebt_entry_watcher)) {            \
		__watcher = (void *)(e) + __i;              \
		                                            \
		__ret = fn(__watcher , ## args);            \
		if (__ret != 0)                             \
			break;                              \
	}                                                   \
	if (__ret == 0) {                                   \
		if (__i != (e)->target_offset)              \
			__ret = -EINVAL;                    \
	}                                                   \
	__ret;                                              \
})

#define EBT_ENTRY_ITERATE(entries, size, fn, args...)       \
({                                                          \
	unsigned int __i;                                   \
	int __ret = 0;                                      \
	struct ebt_entry *__entry;                          \
	                                                    \
	for (__i = 0; __i < (size);) {                      \
		__entry = (void *)(entries) + __i;          \
		__ret = fn(__entry , ## args);              \
		if (__ret != 0)                             \
			break;                              \
		if (__entry->bitmask != 0)                  \
			__i += __entry->next_offset;        \
		else                                        \
			__i += sizeof(struct ebt_entries);  \
	}                                                   \
	if (__ret == 0) {                                   \
		if (__i != (size))                          \
			__ret = -EINVAL;                    \
	}                                                   \
	__ret;                                              \
})

#endif
