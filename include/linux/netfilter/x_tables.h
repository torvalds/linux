#ifndef _X_TABLES_H
#define _X_TABLES_H

#define XT_FUNCTION_MAXNAMELEN 30
#define XT_TABLE_MAXNAMELEN 32

struct xt_entry_match
{
	union {
		struct {
			u_int16_t match_size;

			/* Used by userspace */
			char name[XT_FUNCTION_MAXNAMELEN-1];

			u_int8_t revision;
		} user;
		struct {
			u_int16_t match_size;

			/* Used inside the kernel */
			struct xt_match *match;
		} kernel;

		/* Total length */
		u_int16_t match_size;
	} u;

	unsigned char data[0];
};

struct xt_entry_target
{
	union {
		struct {
			u_int16_t target_size;

			/* Used by userspace */
			char name[XT_FUNCTION_MAXNAMELEN-1];

			u_int8_t revision;
		} user;
		struct {
			u_int16_t target_size;

			/* Used inside the kernel */
			struct xt_target *target;
		} kernel;

		/* Total length */
		u_int16_t target_size;
	} u;

	unsigned char data[0];
};

#define XT_TARGET_INIT(__name, __size)					       \
{									       \
	.target.u.user = {						       \
		.target_size	= XT_ALIGN(__size),			       \
		.name		= __name,				       \
	},								       \
}

struct xt_standard_target
{
	struct xt_entry_target target;
	int verdict;
};

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

/* this is a dummy structure to find out the alignment requirement for a struct
 * containing all the fundamental data types that are used in ipt_entry,
 * ip6t_entry and arpt_entry.  This sucks, and it is a hack.  It will be my
 * personal pleasure to remove it -HW
 */
struct _xt_align
{
	u_int8_t u8;
	u_int16_t u16;
	u_int32_t u32;
	u_int64_t u64;
};

#define XT_ALIGN(s) (((s) + (__alignof__(struct _xt_align)-1)) 	\
			& ~(__alignof__(struct _xt_align)-1))

/* Standard return verdict, or do jump. */
#define XT_STANDARD_TARGET ""
/* Error verdict. */
#define XT_ERROR_TARGET "ERROR"

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

/* fn returns 0 to continue iteration */
#define XT_MATCH_ITERATE(type, e, fn, args...)			\
({								\
	unsigned int __i;					\
	int __ret = 0;						\
	struct xt_entry_match *__m;				\
								\
	for (__i = sizeof(type);				\
	     __i < (e)->target_offset;				\
	     __i += __m->u.match_size) {			\
		__m = (void *)e + __i;				\
								\
		__ret = fn(__m , ## args);			\
		if (__ret != 0)					\
			break;					\
	}							\
	__ret;							\
})

/* fn returns 0 to continue iteration */
#define XT_ENTRY_ITERATE_CONTINUE(type, entries, size, n, fn, args...) \
({								\
	unsigned int __i, __n;					\
	int __ret = 0;						\
	type *__entry;						\
								\
	for (__i = 0, __n = 0; __i < (size);			\
	     __i += __entry->next_offset, __n++) { 		\
		__entry = (void *)(entries) + __i;		\
		if (__n < n)					\
			continue;				\
								\
		__ret = fn(__entry , ## args);			\
		if (__ret != 0)					\
			break;					\
	}							\
	__ret;							\
})

/* fn returns 0 to continue iteration */
#define XT_ENTRY_ITERATE(type, entries, size, fn, args...) \
	XT_ENTRY_ITERATE_CONTINUE(type, entries, size, 0, fn, args)

#ifdef __KERNEL__

#include <linux/netdevice.h>

/**
 * struct xt_match_param - parameters for match extensions' match functions
 *
 * @in:		input netdevice
 * @out:	output netdevice
 * @match:	struct xt_match through which this function was invoked
 * @matchinfo:	per-match data
 * @fragoff:	packet is a fragment, this is the data offset
 * @thoff:	position of transport header relative to skb->data
 * @hotdrop:	drop packet if we had inspection problems
 */
struct xt_match_param {
	const struct net_device *in, *out;
	const struct xt_match *match;
	const void *matchinfo;
	int fragoff;
	unsigned int thoff;
	bool *hotdrop;
};

/**
 * struct xt_mtchk_param - parameters for match extensions'
 * checkentry functions
 *
 * @table:	table the rule is tried to be inserted into
 * @entryinfo:	the family-specific rule data
 * 		(struct ipt_ip, ip6t_ip, ebt_entry)
 * @match:	struct xt_match through which this function was invoked
 * @matchinfo:	per-match data
 * @hook_mask:	via which hooks the new rule is reachable
 */
struct xt_mtchk_param {
	const char *table;
	const void *entryinfo;
	const struct xt_match *match;
	void *matchinfo;
	unsigned int hook_mask;
};

/* Match destructor parameters */
struct xt_mtdtor_param {
	const struct xt_match *match;
	void *matchinfo;
};

/**
 * struct xt_target_param - parameters for target extensions' target functions
 *
 * @hooknum:	hook through which this target was invoked
 * @target:	struct xt_target through which this function was invoked
 * @targinfo:	per-target data
 *
 * Other fields see above.
 */
struct xt_target_param {
	const struct net_device *in, *out;
	unsigned int hooknum;
	const struct xt_target *target;
	const void *targinfo;
};

/**
 * struct xt_tgchk_param - parameters for target extensions'
 * checkentry functions
 *
 * @entryinfo:	the family-specific rule data
 * 		(struct ipt_entry, ip6t_entry, arpt_entry, ebt_entry)
 *
 * Other fields see above.
 */
struct xt_tgchk_param {
	const char *table;
	void *entryinfo;
	const struct xt_target *target;
	void *targinfo;
	unsigned int hook_mask;
};

struct xt_match
{
	struct list_head list;

	const char name[XT_FUNCTION_MAXNAMELEN-1];

	/* Return true or false: return FALSE and set *hotdrop = 1 to
           force immediate packet drop. */
	/* Arguments changed since 2.6.9, as this must now handle
	   non-linear skb, using skb_header_pointer and
	   skb_ip_make_writable. */
	bool (*match)(const struct sk_buff *skb,
		      const struct xt_match_param *);

	/* Called when user tries to insert an entry of this type. */
	bool (*checkentry)(const struct xt_mtchk_param *);

	/* Called when entry of this type deleted. */
	void (*destroy)(const struct xt_mtdtor_param *);

	/* Called when userspace align differs from kernel space one */
	void (*compat_from_user)(void *dst, void *src);
	int (*compat_to_user)(void __user *dst, void *src);

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;

	/* Free to use by each match */
	unsigned long data;

	const char *table;
	unsigned int matchsize;
	unsigned int compatsize;
	unsigned int hooks;
	unsigned short proto;

	unsigned short family;
	u_int8_t revision;
};

/* Registration hooks for targets. */
struct xt_target
{
	struct list_head list;

	const char name[XT_FUNCTION_MAXNAMELEN-1];

	/* Returns verdict. Argument order changed since 2.6.9, as this
	   must now handle non-linear skbs, using skb_copy_bits and
	   skb_ip_make_writable. */
	unsigned int (*target)(struct sk_buff *skb,
			       const struct xt_target_param *);

	/* Called when user tries to insert an entry of this type:
           hook_mask is a bitmask of hooks from which it can be
           called. */
	/* Should return true or false. */
	bool (*checkentry)(const struct xt_tgchk_param *);

	/* Called when entry of this type deleted. */
	void (*destroy)(const struct xt_target *target, void *targinfo);

	/* Called when userspace align differs from kernel space one */
	void (*compat_from_user)(void *dst, void *src);
	int (*compat_to_user)(void __user *dst, void *src);

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;

	const char *table;
	unsigned int targetsize;
	unsigned int compatsize;
	unsigned int hooks;
	unsigned short proto;

	unsigned short family;
	u_int8_t revision;
};

/* Furniture shopping... */
struct xt_table
{
	struct list_head list;

	/* A unique name... */
	const char name[XT_TABLE_MAXNAMELEN];

	/* What hooks you will enter on */
	unsigned int valid_hooks;

	/* Lock for the curtain */
	rwlock_t lock;

	/* Man behind the curtain... */
	//struct ip6t_table_info *private;
	void *private;

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;

	u_int8_t af;		/* address/protocol family */
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
	unsigned int hook_entry[NF_INET_NUMHOOKS];
	unsigned int underflow[NF_INET_NUMHOOKS];

	/* ipt_entry tables: one per CPU */
	/* Note : this field MUST be the last one, see XT_TABLE_INFO_SZ */
	char *entries[1];
};

#define XT_TABLE_INFO_SZ (offsetof(struct xt_table_info, entries) \
			  + nr_cpu_ids * sizeof(char *))
extern int xt_register_target(struct xt_target *target);
extern void xt_unregister_target(struct xt_target *target);
extern int xt_register_targets(struct xt_target *target, unsigned int n);
extern void xt_unregister_targets(struct xt_target *target, unsigned int n);

extern int xt_register_match(struct xt_match *target);
extern void xt_unregister_match(struct xt_match *target);
extern int xt_register_matches(struct xt_match *match, unsigned int n);
extern void xt_unregister_matches(struct xt_match *match, unsigned int n);

extern int xt_check_match(struct xt_mtchk_param *, u_int8_t family,
			  unsigned int size, u_int8_t proto, bool inv_proto);
extern int xt_check_target(struct xt_tgchk_param *, u_int8_t family,
			   unsigned int size, u_int8_t proto, bool inv_proto);

extern struct xt_table *xt_register_table(struct net *net,
					  struct xt_table *table,
					  struct xt_table_info *bootstrap,
					  struct xt_table_info *newinfo);
extern void *xt_unregister_table(struct xt_table *table);

extern struct xt_table_info *xt_replace_table(struct xt_table *table,
					      unsigned int num_counters,
					      struct xt_table_info *newinfo,
					      int *error);

extern struct xt_match *xt_find_match(u8 af, const char *name, u8 revision);
extern struct xt_target *xt_find_target(u8 af, const char *name, u8 revision);
extern struct xt_target *xt_request_find_target(u8 af, const char *name,
						u8 revision);
extern int xt_find_revision(u8 af, const char *name, u8 revision,
			    int target, int *err);

extern struct xt_table *xt_find_table_lock(struct net *net, u_int8_t af,
					   const char *name);
extern void xt_table_unlock(struct xt_table *t);

extern int xt_proto_init(struct net *net, u_int8_t af);
extern void xt_proto_fini(struct net *net, u_int8_t af);

extern struct xt_table_info *xt_alloc_table_info(unsigned int size);
extern void xt_free_table_info(struct xt_table_info *info);

#ifdef CONFIG_COMPAT
#include <net/compat.h>

struct compat_xt_entry_match
{
	union {
		struct {
			u_int16_t match_size;
			char name[XT_FUNCTION_MAXNAMELEN - 1];
			u_int8_t revision;
		} user;
		struct {
			u_int16_t match_size;
			compat_uptr_t match;
		} kernel;
		u_int16_t match_size;
	} u;
	unsigned char data[0];
};

struct compat_xt_entry_target
{
	union {
		struct {
			u_int16_t target_size;
			char name[XT_FUNCTION_MAXNAMELEN - 1];
			u_int8_t revision;
		} user;
		struct {
			u_int16_t target_size;
			compat_uptr_t target;
		} kernel;
		u_int16_t target_size;
	} u;
	unsigned char data[0];
};

/* FIXME: this works only on 32 bit tasks
 * need to change whole approach in order to calculate align as function of
 * current task alignment */

struct compat_xt_counters
{
#if defined(CONFIG_X86_64) || defined(CONFIG_IA64)
	u_int32_t cnt[4];
#else
	u_int64_t cnt[2];
#endif
};

struct compat_xt_counters_info
{
	char name[XT_TABLE_MAXNAMELEN];
	compat_uint_t num_counters;
	struct compat_xt_counters counters[0];
};

#define COMPAT_XT_ALIGN(s) (((s) + (__alignof__(struct compat_xt_counters)-1)) \
		& ~(__alignof__(struct compat_xt_counters)-1))

extern void xt_compat_lock(u_int8_t af);
extern void xt_compat_unlock(u_int8_t af);

extern int xt_compat_add_offset(u_int8_t af, unsigned int offset, short delta);
extern void xt_compat_flush_offsets(u_int8_t af);
extern short xt_compat_calc_jump(u_int8_t af, unsigned int offset);

extern int xt_compat_match_offset(const struct xt_match *match);
extern int xt_compat_match_from_user(struct xt_entry_match *m,
				     void **dstptr, unsigned int *size);
extern int xt_compat_match_to_user(struct xt_entry_match *m,
				   void __user **dstptr, unsigned int *size);

extern int xt_compat_target_offset(const struct xt_target *target);
extern void xt_compat_target_from_user(struct xt_entry_target *t,
				       void **dstptr, unsigned int *size);
extern int xt_compat_target_to_user(struct xt_entry_target *t,
				    void __user **dstptr, unsigned int *size);

#endif /* CONFIG_COMPAT */
#endif /* __KERNEL__ */

#endif /* _X_TABLES_H */
