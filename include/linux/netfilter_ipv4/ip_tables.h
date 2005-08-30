/*
 * 25-Jul-1998 Major changes to allow for ip chain table
 *
 * 3-Jan-2000 Named tables to allow packet selection for different uses.
 */

/*
 * 	Format of an IP firewall descriptor
 *
 * 	src, dst, src_mask, dst_mask are always stored in network byte order.
 * 	flags are stored in host byte order (of course).
 * 	Port numbers are stored in HOST byte order.
 */

#ifndef _IPTABLES_H
#define _IPTABLES_H

#ifdef __KERNEL__
#include <linux/if.h>
#include <linux/types.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#endif
#include <linux/compiler.h>
#include <linux/netfilter_ipv4.h>

#define IPT_FUNCTION_MAXNAMELEN 30
#define IPT_TABLE_MAXNAMELEN 32

/* Yes, Virginia, you have to zero the padding. */
struct ipt_ip {
	/* Source and destination IP addr */
	struct in_addr src, dst;
	/* Mask for src and dest IP addr */
	struct in_addr smsk, dmsk;
	char iniface[IFNAMSIZ], outiface[IFNAMSIZ];
	unsigned char iniface_mask[IFNAMSIZ], outiface_mask[IFNAMSIZ];

	/* Protocol, 0 = ANY */
	u_int16_t proto;

	/* Flags word */
	u_int8_t flags;
	/* Inverse flags */
	u_int8_t invflags;
};

struct ipt_entry_match
{
	union {
		struct {
			u_int16_t match_size;

			/* Used by userspace */
			char name[IPT_FUNCTION_MAXNAMELEN-1];

			u_int8_t revision;
		} user;
		struct {
			u_int16_t match_size;

			/* Used inside the kernel */
			struct ipt_match *match;
		} kernel;

		/* Total length */
		u_int16_t match_size;
	} u;

	unsigned char data[0];
};

struct ipt_entry_target
{
	union {
		struct {
			u_int16_t target_size;

			/* Used by userspace */
			char name[IPT_FUNCTION_MAXNAMELEN-1];

			u_int8_t revision;
		} user;
		struct {
			u_int16_t target_size;

			/* Used inside the kernel */
			struct ipt_target *target;
		} kernel;

		/* Total length */
		u_int16_t target_size;
	} u;

	unsigned char data[0];
};

struct ipt_standard_target
{
	struct ipt_entry_target target;
	int verdict;
};

struct ipt_counters
{
	u_int64_t pcnt, bcnt;			/* Packet and byte counters */
};

/* Values for "flag" field in struct ipt_ip (general ip structure). */
#define IPT_F_FRAG		0x01	/* Set if rule is a fragment rule */
#define IPT_F_GOTO		0x02	/* Set if jump is a goto */
#define IPT_F_MASK		0x03	/* All possible flag bits mask. */

/* Values for "inv" field in struct ipt_ip. */
#define IPT_INV_VIA_IN		0x01	/* Invert the sense of IN IFACE. */
#define IPT_INV_VIA_OUT		0x02	/* Invert the sense of OUT IFACE */
#define IPT_INV_TOS		0x04	/* Invert the sense of TOS. */
#define IPT_INV_SRCIP		0x08	/* Invert the sense of SRC IP. */
#define IPT_INV_DSTIP		0x10	/* Invert the sense of DST OP. */
#define IPT_INV_FRAG		0x20	/* Invert the sense of FRAG. */
#define IPT_INV_PROTO		0x40	/* Invert the sense of PROTO. */
#define IPT_INV_MASK		0x7F	/* All possible flag bits mask. */

/* This structure defines each of the firewall rules.  Consists of 3
   parts which are 1) general IP header stuff 2) match specific
   stuff 3) the target to perform if the rule matches */
struct ipt_entry
{
	struct ipt_ip ip;

	/* Mark with fields that we care about. */
	unsigned int nfcache;

	/* Size of ipt_entry + matches */
	u_int16_t target_offset;
	/* Size of ipt_entry + matches + target */
	u_int16_t next_offset;

	/* Back pointer */
	unsigned int comefrom;

	/* Packet and byte counters. */
	struct ipt_counters counters;

	/* The matches (if any), then the target. */
	unsigned char elems[0];
};

/*
 * New IP firewall options for [gs]etsockopt at the RAW IP level.
 * Unlike BSD Linux inherits IP options so you don't have to use a raw
 * socket for this. Instead we check rights in the calls. */
#define IPT_BASE_CTL		64	/* base for firewall socket options */

#define IPT_SO_SET_REPLACE	(IPT_BASE_CTL)
#define IPT_SO_SET_ADD_COUNTERS	(IPT_BASE_CTL + 1)
#define IPT_SO_SET_MAX		IPT_SO_SET_ADD_COUNTERS

#define IPT_SO_GET_INFO			(IPT_BASE_CTL)
#define IPT_SO_GET_ENTRIES		(IPT_BASE_CTL + 1)
#define IPT_SO_GET_REVISION_MATCH	(IPT_BASE_CTL + 2)
#define IPT_SO_GET_REVISION_TARGET	(IPT_BASE_CTL + 3)
#define IPT_SO_GET_MAX			IPT_SO_GET_REVISION_TARGET

/* CONTINUE verdict for targets */
#define IPT_CONTINUE 0xFFFFFFFF

/* For standard target */
#define IPT_RETURN (-NF_REPEAT - 1)

/* TCP matching stuff */
struct ipt_tcp
{
	u_int16_t spts[2];			/* Source port range. */
	u_int16_t dpts[2];			/* Destination port range. */
	u_int8_t option;			/* TCP Option iff non-zero*/
	u_int8_t flg_mask;			/* TCP flags mask byte */
	u_int8_t flg_cmp;			/* TCP flags compare byte */
	u_int8_t invflags;			/* Inverse flags */
};

/* Values for "inv" field in struct ipt_tcp. */
#define IPT_TCP_INV_SRCPT	0x01	/* Invert the sense of source ports. */
#define IPT_TCP_INV_DSTPT	0x02	/* Invert the sense of dest ports. */
#define IPT_TCP_INV_FLAGS	0x04	/* Invert the sense of TCP flags. */
#define IPT_TCP_INV_OPTION	0x08	/* Invert the sense of option test. */
#define IPT_TCP_INV_MASK	0x0F	/* All possible flags. */

/* UDP matching stuff */
struct ipt_udp
{
	u_int16_t spts[2];			/* Source port range. */
	u_int16_t dpts[2];			/* Destination port range. */
	u_int8_t invflags;			/* Inverse flags */
};

/* Values for "invflags" field in struct ipt_udp. */
#define IPT_UDP_INV_SRCPT	0x01	/* Invert the sense of source ports. */
#define IPT_UDP_INV_DSTPT	0x02	/* Invert the sense of dest ports. */
#define IPT_UDP_INV_MASK	0x03	/* All possible flags. */

/* ICMP matching stuff */
struct ipt_icmp
{
	u_int8_t type;				/* type to match */
	u_int8_t code[2];			/* range of code */
	u_int8_t invflags;			/* Inverse flags */
};

/* Values for "inv" field for struct ipt_icmp. */
#define IPT_ICMP_INV	0x01	/* Invert the sense of type/code test */

/* The argument to IPT_SO_GET_INFO */
struct ipt_getinfo
{
	/* Which table: caller fills this in. */
	char name[IPT_TABLE_MAXNAMELEN];

	/* Kernel fills these in. */
	/* Which hook entry points are valid: bitmask */
	unsigned int valid_hooks;

	/* Hook entry points: one per netfilter hook. */
	unsigned int hook_entry[NF_IP_NUMHOOKS];

	/* Underflow points. */
	unsigned int underflow[NF_IP_NUMHOOKS];

	/* Number of entries */
	unsigned int num_entries;

	/* Size of entries. */
	unsigned int size;
};

/* The argument to IPT_SO_SET_REPLACE. */
struct ipt_replace
{
	/* Which table. */
	char name[IPT_TABLE_MAXNAMELEN];

	/* Which hook entry points are valid: bitmask.  You can't
           change this. */
	unsigned int valid_hooks;

	/* Number of entries */
	unsigned int num_entries;

	/* Total size of new entries */
	unsigned int size;

	/* Hook entry points. */
	unsigned int hook_entry[NF_IP_NUMHOOKS];

	/* Underflow points. */
	unsigned int underflow[NF_IP_NUMHOOKS];

	/* Information about old entries: */
	/* Number of counters (must be equal to current number of entries). */
	unsigned int num_counters;
	/* The old entries' counters. */
	struct ipt_counters __user *counters;

	/* The entries (hang off end: not really an array). */
	struct ipt_entry entries[0];
};

/* The argument to IPT_SO_ADD_COUNTERS. */
struct ipt_counters_info
{
	/* Which table. */
	char name[IPT_TABLE_MAXNAMELEN];

	unsigned int num_counters;

	/* The counters (actually `number' of these). */
	struct ipt_counters counters[0];
};

/* The argument to IPT_SO_GET_ENTRIES. */
struct ipt_get_entries
{
	/* Which table: user fills this in. */
	char name[IPT_TABLE_MAXNAMELEN];

	/* User fills this in: total entry size. */
	unsigned int size;

	/* The entries. */
	struct ipt_entry entrytable[0];
};

/* The argument to IPT_SO_GET_REVISION_*.  Returns highest revision
 * kernel supports, if >= revision. */
struct ipt_get_revision
{
	char name[IPT_FUNCTION_MAXNAMELEN-1];

	u_int8_t revision;
};

/* Standard return verdict, or do jump. */
#define IPT_STANDARD_TARGET ""
/* Error verdict. */
#define IPT_ERROR_TARGET "ERROR"

/* Helper functions */
static __inline__ struct ipt_entry_target *
ipt_get_target(struct ipt_entry *e)
{
	return (void *)e + e->target_offset;
}

/* fn returns 0 to continue iteration */
#define IPT_MATCH_ITERATE(e, fn, args...)	\
({						\
	unsigned int __i;			\
	int __ret = 0;				\
	struct ipt_entry_match *__match;	\
						\
	for (__i = sizeof(struct ipt_entry);	\
	     __i < (e)->target_offset;		\
	     __i += __match->u.match_size) {	\
		__match = (void *)(e) + __i;	\
						\
		__ret = fn(__match , ## args);	\
		if (__ret != 0)			\
			break;			\
	}					\
	__ret;					\
})

/* fn returns 0 to continue iteration */
#define IPT_ENTRY_ITERATE(entries, size, fn, args...)		\
({								\
	unsigned int __i;					\
	int __ret = 0;						\
	struct ipt_entry *__entry;				\
								\
	for (__i = 0; __i < (size); __i += __entry->next_offset) { \
		__entry = (void *)(entries) + __i;		\
								\
		__ret = fn(__entry , ## args);			\
		if (__ret != 0)					\
			break;					\
	}							\
	__ret;							\
})

/*
 *	Main firewall chains definitions and global var's definitions.
 */
#ifdef __KERNEL__

#include <linux/init.h>
extern void ipt_init(void) __init;

struct ipt_match
{
	struct list_head list;

	const char name[IPT_FUNCTION_MAXNAMELEN-1];

	u_int8_t revision;

	/* Return true or false: return FALSE and set *hotdrop = 1 to
           force immediate packet drop. */
	/* Arguments changed since 2.4, as this must now handle
           non-linear skbs, using skb_copy_bits and
           skb_ip_make_writable. */
	int (*match)(const struct sk_buff *skb,
		     const struct net_device *in,
		     const struct net_device *out,
		     const void *matchinfo,
		     int offset,
		     int *hotdrop);

	/* Called when user tries to insert an entry of this type. */
	/* Should return true or false. */
	int (*checkentry)(const char *tablename,
			  const struct ipt_ip *ip,
			  void *matchinfo,
			  unsigned int matchinfosize,
			  unsigned int hook_mask);

	/* Called when entry of this type deleted. */
	void (*destroy)(void *matchinfo, unsigned int matchinfosize);

	/* Set this to THIS_MODULE. */
	struct module *me;
};

/* Registration hooks for targets. */
struct ipt_target
{
	struct list_head list;

	const char name[IPT_FUNCTION_MAXNAMELEN-1];

	u_int8_t revision;

	/* Called when user tries to insert an entry of this type:
           hook_mask is a bitmask of hooks from which it can be
           called. */
	/* Should return true or false. */
	int (*checkentry)(const char *tablename,
			  const struct ipt_entry *e,
			  void *targinfo,
			  unsigned int targinfosize,
			  unsigned int hook_mask);

	/* Called when entry of this type deleted. */
	void (*destroy)(void *targinfo, unsigned int targinfosize);

	/* Returns verdict.  Argument order changed since 2.4, as this
           must now handle non-linear skbs, using skb_copy_bits and
           skb_ip_make_writable. */
	unsigned int (*target)(struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
			       unsigned int hooknum,
			       const void *targinfo,
			       void *userdata);

	/* Set this to THIS_MODULE. */
	struct module *me;
};

extern int ipt_register_target(struct ipt_target *target);
extern void ipt_unregister_target(struct ipt_target *target);

extern int ipt_register_match(struct ipt_match *match);
extern void ipt_unregister_match(struct ipt_match *match);

/* Furniture shopping... */
struct ipt_table
{
	struct list_head list;

	/* A unique name... */
	char name[IPT_TABLE_MAXNAMELEN];

	/* What hooks you will enter on */
	unsigned int valid_hooks;

	/* Lock for the curtain */
	rwlock_t lock;

	/* Man behind the curtain... */
	struct ipt_table_info *private;

	/* Set to THIS_MODULE. */
	struct module *me;
};

/* net/sched/ipt.c: Gimme access to your targets!  Gets target->me. */
extern struct ipt_target *ipt_find_target(const char *name, u8 revision);

/* Standard entry. */
struct ipt_standard
{
	struct ipt_entry entry;
	struct ipt_standard_target target;
};

struct ipt_error_target
{
	struct ipt_entry_target target;
	char errorname[IPT_FUNCTION_MAXNAMELEN];
};

struct ipt_error
{
	struct ipt_entry entry;
	struct ipt_error_target target;
};

extern int ipt_register_table(struct ipt_table *table,
			      const struct ipt_replace *repl);
extern void ipt_unregister_table(struct ipt_table *table);
extern unsigned int ipt_do_table(struct sk_buff **pskb,
				 unsigned int hook,
				 const struct net_device *in,
				 const struct net_device *out,
				 struct ipt_table *table,
				 void *userdata);

#define IPT_ALIGN(s) (((s) + (__alignof__(struct ipt_entry)-1)) & ~(__alignof__(struct ipt_entry)-1))
#endif /*__KERNEL__*/
#endif /* _IPTABLES_H */
