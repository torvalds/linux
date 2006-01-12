#ifndef _X_TABLES_H
#define _X_TABLES_H

#define XT_FUNCTION_MAXNAMELEN 30
#define XT_TABLE_MAXNAMELEN 32

/* The argument to IPT_SO_GET_REVISION_*.  Returns highest revision
 * kernel supports, if >= revision. */
struct xt_get_revision
{
	char name[XT_FUNCTION_MAXNAMELEN-1];

	u_int8_t revision;
};

/* CONTINUE verdict for targets */
#define XT_CONTINUE 0xFFFFFFFF

/* For standard target */
#define XT_RETURN (-NF_REPEAT - 1)

#define XT_ALIGN(s) (((s) + (__alignof__(void *)-1)) & ~(__alignof__(void *)-1))

/* Standard return verdict, or do jump. */
#define XT_STANDARD_TARGET ""
/* Error verdict. */
#define XT_ERROR_TARGET "ERROR"

/*
 * New IP firewall options for [gs]etsockopt at the RAW IP level.
 * Unlike BSD Linux inherits IP options so you don't have to use a raw
 * socket for this. Instead we check rights in the calls. */
#define XT_BASE_CTL		64	/* base for firewall socket options */

#define XT_SO_SET_REPLACE	(XT_BASE_CTL)
#define XT_SO_SET_ADD_COUNTERS	(XT_BASE_CTL + 1)
#define XT_SO_SET_MAX		XT_SO_SET_ADD_COUNTERS

#define XT_SO_GET_INFO			(XT_BASE_CTL)
#define XT_SO_GET_ENTRIES		(XT_BASE_CTL + 1)
#define XT_SO_GET_REVISION_MATCH	(XT_BASE_CTL + 2)
#define XT_SO_GET_REVISION_TARGET	(XT_BASE_CTL + 3)
#define XT_SO_GET_MAX			XT_SO_GET_REVISION_TARGET

#define SET_COUNTER(c,b,p) do { (c).bcnt = (b); (c).pcnt = (p); } while(0)
#define ADD_COUNTER(c,b,p) do { (c).bcnt += (b); (c).pcnt += (p); } while(0)

struct xt_counters
{
	u_int64_t pcnt, bcnt;			/* Packet and byte counters */
};

/* The argument to IPT_SO_ADD_COUNTERS. */
struct xt_counters_info
{
	/* Which table. */
	char name[XT_TABLE_MAXNAMELEN];

	unsigned int num_counters;

	/* The counters (actually `number' of these). */
	struct xt_counters counters[0];
};

#define XT_INV_PROTO		0x40	/* Invert the sense of PROTO. */

#ifdef __KERNEL__

#include <linux/netdevice.h>

#define ASSERT_READ_LOCK(x)
#define ASSERT_WRITE_LOCK(x)
#include <linux/netfilter_ipv4/listhelp.h>

struct xt_match
{
	struct list_head list;

	const char name[XT_FUNCTION_MAXNAMELEN-1];

	u_int8_t revision;

	/* Return true or false: return FALSE and set *hotdrop = 1 to
           force immediate packet drop. */
	/* Arguments changed since 2.6.9, as this must now handle
	   non-linear skb, using skb_header_pointer and
	   skb_ip_make_writable. */
	int (*match)(const struct sk_buff *skb,
		     const struct net_device *in,
		     const struct net_device *out,
		     const void *matchinfo,
		     int offset,
		     unsigned int protoff,
		     int *hotdrop);

	/* Called when user tries to insert an entry of this type. */
	/* Should return true or false. */
	int (*checkentry)(const char *tablename,
			  const void *ip,
			  void *matchinfo,
			  unsigned int matchinfosize,
			  unsigned int hook_mask);

	/* Called when entry of this type deleted. */
	void (*destroy)(void *matchinfo, unsigned int matchinfosize);

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;
};

/* Registration hooks for targets. */
struct xt_target
{
	struct list_head list;

	const char name[XT_FUNCTION_MAXNAMELEN-1];

	u_int8_t revision;

	/* Returns verdict. Argument order changed since 2.6.9, as this
	   must now handle non-linear skbs, using skb_copy_bits and
	   skb_ip_make_writable. */
	unsigned int (*target)(struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
			       unsigned int hooknum,
			       const void *targinfo,
			       void *userdata);

	/* Called when user tries to insert an entry of this type:
           hook_mask is a bitmask of hooks from which it can be
           called. */
	/* Should return true or false. */
	int (*checkentry)(const char *tablename,
			  const void *entry,
			  void *targinfo,
			  unsigned int targinfosize,
			  unsigned int hook_mask);

	/* Called when entry of this type deleted. */
	void (*destroy)(void *targinfo, unsigned int targinfosize);

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;
};

/* Furniture shopping... */
struct xt_table
{
	struct list_head list;

	/* A unique name... */
	char name[XT_TABLE_MAXNAMELEN];

	/* What hooks you will enter on */
	unsigned int valid_hooks;

	/* Lock for the curtain */
	rwlock_t lock;

	/* Man behind the curtain... */
	//struct ip6t_table_info *private;
	void *private;

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;

	int af;		/* address/protocol family */
};

#include <linux/netfilter_ipv4.h>

/* The table itself */
struct xt_table_info
{
	/* Size per table */
	unsigned int size;
	/* Number of entries: FIXME. --RR */
	unsigned int number;
	/* Initial number of entries. Needed for module usage count */
	unsigned int initial_entries;

	/* Entry points and underflows */
	unsigned int hook_entry[NF_IP_NUMHOOKS];
	unsigned int underflow[NF_IP_NUMHOOKS];

	/* ipt_entry tables: one per CPU */
	char *entries[NR_CPUS];
};

extern int xt_register_target(int af, struct xt_target *target);
extern void xt_unregister_target(int af, struct xt_target *target);
extern int xt_register_match(int af, struct xt_match *target);
extern void xt_unregister_match(int af, struct xt_match *target);

extern int xt_register_table(struct xt_table *table,
			     struct xt_table_info *bootstrap,
			     struct xt_table_info *newinfo);
extern void *xt_unregister_table(struct xt_table *table);

extern struct xt_table_info *xt_replace_table(struct xt_table *table,
					      unsigned int num_counters,
					      struct xt_table_info *newinfo,
					      int *error);

extern struct xt_match *xt_find_match(int af, const char *name, u8 revision);
extern struct xt_target *xt_find_target(int af, const char *name, u8 revision);
extern struct xt_target *xt_request_find_target(int af, const char *name, 
						u8 revision);
extern int xt_find_revision(int af, const char *name, u8 revision, int target,
			    int *err);

extern struct xt_table *xt_find_table_lock(int af, const char *name);
extern void xt_table_unlock(struct xt_table *t);

extern int xt_proto_init(int af);
extern void xt_proto_fini(int af);

extern struct xt_table_info *xt_alloc_table_info(unsigned int size);
extern void xt_free_table_info(struct xt_table_info *info);

#endif /* __KERNEL__ */

#endif /* _X_TABLES_H */
