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

#include <linux/netfilter/x_tables.h>

#define IPT_FUNCTION_MAXNAMELEN XT_FUNCTION_MAXNAMELEN
#define IPT_TABLE_MAXNAMELEN XT_FUNCTION_MAXNAMELEN
#define ipt_match xt_match
#define ipt_target xt_target
#define ipt_table xt_table
#define ipt_get_revision xt_get_revision

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

#define ipt_entry_match xt_entry_match
#define ipt_entry_target xt_entry_target
#define ipt_standard_target xt_standard_target

#define ipt_counters xt_counters

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
#define IPT_INV_PROTO		XT_INV_PROTO
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
	struct xt_counters counters;

	/* The matches (if any), then the target. */
	unsigned char elems[0];
};

/*
 * New IP firewall options for [gs]etsockopt at the RAW IP level.
 * Unlike BSD Linux inherits IP options so you don't have to use a raw
 * socket for this. Instead we check rights in the calls. */
#define IPT_BASE_CTL		XT_BASE_CTL

#define IPT_SO_SET_REPLACE	XT_SO_SET_REPLACE
#define IPT_SO_SET_ADD_COUNTERS	XT_SO_SET_ADD_COUNTERS
#define IPT_SO_SET_MAX		XT_SO_SET_MAX

#define IPT_SO_GET_INFO			XT_SO_GET_INFO
#define IPT_SO_GET_ENTRIES		XT_SO_GET_ENTRIES
#define IPT_SO_GET_REVISION_MATCH	XT_SO_GET_REVISION_MATCH
#define IPT_SO_GET_REVISION_TARGET	XT_SO_GET_REVISION_TARGET
#define IPT_SO_GET_MAX			XT_SO_GET_REVISION_TARGET

#define IPT_CONTINUE XT_CONTINUE
#define IPT_RETURN XT_RETURN

#include <linux/netfilter/xt_tcpudp.h>
#define ipt_udp xt_udp
#define ipt_tcp xt_tcp

#define IPT_TCP_INV_SRCPT	XT_TCP_INV_SRCPT
#define IPT_TCP_INV_DSTPT	XT_TCP_INV_DSTPT
#define IPT_TCP_INV_FLAGS	XT_TCP_INV_FLAGS
#define IPT_TCP_INV_OPTION	XT_TCP_INV_OPTION
#define IPT_TCP_INV_MASK	XT_TCP_INV_MASK

#define IPT_UDP_INV_SRCPT	XT_UDP_INV_SRCPT
#define IPT_UDP_INV_DSTPT	XT_UDP_INV_DSTPT
#define IPT_UDP_INV_MASK	XT_UDP_INV_MASK

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
	struct xt_counters __user *counters;

	/* The entries (hang off end: not really an array). */
	struct ipt_entry entries[0];
};

/* The argument to IPT_SO_ADD_COUNTERS. */
#define ipt_counters_info xt_counters_info

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

/* Standard return verdict, or do jump. */
#define IPT_STANDARD_TARGET XT_STANDARD_TARGET
/* Error verdict. */
#define IPT_ERROR_TARGET XT_ERROR_TARGET

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

#define ipt_register_target(tgt) 	\
({	(tgt)->family = AF_INET;	\
 	xt_register_target(tgt); })
#define ipt_unregister_target(tgt) xt_unregister_target(tgt)

#define ipt_register_match(mtch) 	\
({	(mtch)->family = AF_INET;	\
	xt_register_match(mtch); })
#define ipt_unregister_match(mtch) xt_unregister_match(mtch)

//#define ipt_register_table(tbl, repl) xt_register_table(AF_INET, tbl, repl)
//#define ipt_unregister_table(tbl) xt_unregister_table(AF_INET, tbl)

extern int ipt_register_table(struct ipt_table *table,
			      const struct ipt_replace *repl);
extern void ipt_unregister_table(struct ipt_table *table);

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

extern unsigned int ipt_do_table(struct sk_buff **pskb,
				 unsigned int hook,
				 const struct net_device *in,
				 const struct net_device *out,
				 struct ipt_table *table,
				 void *userdata);

#define IPT_ALIGN(s) XT_ALIGN(s)

#ifdef CONFIG_COMPAT
#include <net/compat.h>

struct compat_ipt_entry
{
	struct ipt_ip ip;
	compat_uint_t nfcache;
	u_int16_t target_offset;
	u_int16_t next_offset;
	compat_uint_t comefrom;
	struct compat_xt_counters counters;
	unsigned char elems[0];
};

#define COMPAT_IPT_ALIGN(s) 	COMPAT_XT_ALIGN(s)

#endif /* CONFIG_COMPAT */
#endif /*__KERNEL__*/
#endif /* _IPTABLES_H */
