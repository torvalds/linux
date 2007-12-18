/*
 * 25-Jul-1998 Major changes to allow for ip chain table
 *
 * 3-Jan-2000 Named tables to allow packet selection for different uses.
 */

/*
 * 	Format of an IP6 firewall descriptor
 *
 * 	src, dst, src_mask, dst_mask are always stored in network byte order.
 * 	flags are stored in host byte order (of course).
 * 	Port numbers are stored in HOST byte order.
 */

#ifndef _IP6_TABLES_H
#define _IP6_TABLES_H

#ifdef __KERNEL__
#include <linux/if.h>
#include <linux/types.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>
#endif
#include <linux/compiler.h>
#include <linux/netfilter_ipv6.h>

#include <linux/netfilter/x_tables.h>

#define IP6T_FUNCTION_MAXNAMELEN XT_FUNCTION_MAXNAMELEN
#define IP6T_TABLE_MAXNAMELEN XT_TABLE_MAXNAMELEN

#define ip6t_match xt_match
#define ip6t_target xt_target
#define ip6t_table xt_table
#define ip6t_get_revision xt_get_revision

/* Yes, Virginia, you have to zero the padding. */
struct ip6t_ip6 {
	/* Source and destination IP6 addr */
	struct in6_addr src, dst;		
	/* Mask for src and dest IP6 addr */
	struct in6_addr smsk, dmsk;
	char iniface[IFNAMSIZ], outiface[IFNAMSIZ];
	unsigned char iniface_mask[IFNAMSIZ], outiface_mask[IFNAMSIZ];

	/* Upper protocol number
	 * - The allowed value is 0 (any) or protocol number of last parsable
	 *   header, which is 50 (ESP), 59 (No Next Header), 135 (MH), or
	 *   the non IPv6 extension headers.
	 * - The protocol numbers of IPv6 extension headers except of ESP and
	 *   MH do not match any packets.
	 * - You also need to set IP6T_FLAGS_PROTO to "flags" to check protocol.
	 */
	u_int16_t proto;
	/* TOS to match iff flags & IP6T_F_TOS */
	u_int8_t tos;

	/* Flags word */
	u_int8_t flags;
	/* Inverse flags */
	u_int8_t invflags;
};

#define ip6t_entry_match xt_entry_match
#define ip6t_entry_target xt_entry_target
#define ip6t_standard_target xt_standard_target

#define ip6t_counters	xt_counters

/* Values for "flag" field in struct ip6t_ip6 (general ip6 structure). */
#define IP6T_F_PROTO		0x01	/* Set if rule cares about upper 
					   protocols */
#define IP6T_F_TOS		0x02	/* Match the TOS. */
#define IP6T_F_GOTO		0x04	/* Set if jump is a goto */
#define IP6T_F_MASK		0x07	/* All possible flag bits mask. */

/* Values for "inv" field in struct ip6t_ip6. */
#define IP6T_INV_VIA_IN		0x01	/* Invert the sense of IN IFACE. */
#define IP6T_INV_VIA_OUT		0x02	/* Invert the sense of OUT IFACE */
#define IP6T_INV_TOS		0x04	/* Invert the sense of TOS. */
#define IP6T_INV_SRCIP		0x08	/* Invert the sense of SRC IP. */
#define IP6T_INV_DSTIP		0x10	/* Invert the sense of DST OP. */
#define IP6T_INV_FRAG		0x20	/* Invert the sense of FRAG. */
#define IP6T_INV_PROTO		XT_INV_PROTO
#define IP6T_INV_MASK		0x7F	/* All possible flag bits mask. */

/* This structure defines each of the firewall rules.  Consists of 3
   parts which are 1) general IP header stuff 2) match specific
   stuff 3) the target to perform if the rule matches */
struct ip6t_entry
{
	struct ip6t_ip6 ipv6;

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

/* Standard entry */
struct ip6t_standard
{
	struct ip6t_entry entry;
	struct ip6t_standard_target target;
};

struct ip6t_error_target
{
	struct ip6t_entry_target target;
	char errorname[IP6T_FUNCTION_MAXNAMELEN];
};

struct ip6t_error
{
	struct ip6t_entry entry;
	struct ip6t_error_target target;
};

#define IP6T_ENTRY_INIT(__size)						       \
{									       \
	.target_offset	= sizeof(struct ip6t_entry),			       \
	.next_offset	= (__size),					       \
}

#define IP6T_STANDARD_INIT(__verdict)					       \
{									       \
	.entry		= IP6T_ENTRY_INIT(sizeof(struct ip6t_standard)),       \
	.target		= XT_TARGET_INIT(IP6T_STANDARD_TARGET,		       \
					 sizeof(struct ip6t_standard_target)), \
	.target.verdict	= -(__verdict) - 1,				       \
}

#define IP6T_ERROR_INIT							       \
{									       \
	.entry		= IP6T_ENTRY_INIT(sizeof(struct ip6t_error)),	       \
	.target		= XT_TARGET_INIT(IP6T_ERROR_TARGET,		       \
					 sizeof(struct ip6t_error_target)),    \
	.target.errorname = "ERROR",					       \
}

/*
 * New IP firewall options for [gs]etsockopt at the RAW IP level.
 * Unlike BSD Linux inherits IP options so you don't have to use
 * a raw socket for this. Instead we check rights in the calls.
 *
 * ATTENTION: check linux/in6.h before adding new number here.
 */
#define IP6T_BASE_CTL			64

#define IP6T_SO_SET_REPLACE		(IP6T_BASE_CTL)
#define IP6T_SO_SET_ADD_COUNTERS	(IP6T_BASE_CTL + 1)
#define IP6T_SO_SET_MAX			IP6T_SO_SET_ADD_COUNTERS

#define IP6T_SO_GET_INFO		(IP6T_BASE_CTL)
#define IP6T_SO_GET_ENTRIES		(IP6T_BASE_CTL + 1)
#define IP6T_SO_GET_REVISION_MATCH	(IP6T_BASE_CTL + 4)
#define IP6T_SO_GET_REVISION_TARGET	(IP6T_BASE_CTL + 5)
#define IP6T_SO_GET_MAX			IP6T_SO_GET_REVISION_TARGET

/* CONTINUE verdict for targets */
#define IP6T_CONTINUE XT_CONTINUE

/* For standard target */
#define IP6T_RETURN XT_RETURN

/* TCP/UDP matching stuff */
#include <linux/netfilter/xt_tcpudp.h>

#define ip6t_tcp xt_tcp
#define ip6t_udp xt_udp

/* Values for "inv" field in struct ipt_tcp. */
#define IP6T_TCP_INV_SRCPT	XT_TCP_INV_SRCPT
#define IP6T_TCP_INV_DSTPT	XT_TCP_INV_DSTPT
#define IP6T_TCP_INV_FLAGS	XT_TCP_INV_FLAGS
#define IP6T_TCP_INV_OPTION	XT_TCP_INV_OPTION
#define IP6T_TCP_INV_MASK	XT_TCP_INV_MASK

/* Values for "invflags" field in struct ipt_udp. */
#define IP6T_UDP_INV_SRCPT	XT_UDP_INV_SRCPT
#define IP6T_UDP_INV_DSTPT	XT_UDP_INV_DSTPT
#define IP6T_UDP_INV_MASK	XT_UDP_INV_MASK

/* ICMP matching stuff */
struct ip6t_icmp
{
	u_int8_t type;				/* type to match */
	u_int8_t code[2];			/* range of code */
	u_int8_t invflags;			/* Inverse flags */
};

/* Values for "inv" field for struct ipt_icmp. */
#define IP6T_ICMP_INV	0x01	/* Invert the sense of type/code test */

/* The argument to IP6T_SO_GET_INFO */
struct ip6t_getinfo
{
	/* Which table: caller fills this in. */
	char name[IP6T_TABLE_MAXNAMELEN];

	/* Kernel fills these in. */
	/* Which hook entry points are valid: bitmask */
	unsigned int valid_hooks;

	/* Hook entry points: one per netfilter hook. */
	unsigned int hook_entry[NF_INET_NUMHOOKS];

	/* Underflow points. */
	unsigned int underflow[NF_INET_NUMHOOKS];

	/* Number of entries */
	unsigned int num_entries;

	/* Size of entries. */
	unsigned int size;
};

/* The argument to IP6T_SO_SET_REPLACE. */
struct ip6t_replace
{
	/* Which table. */
	char name[IP6T_TABLE_MAXNAMELEN];

	/* Which hook entry points are valid: bitmask.  You can't
           change this. */
	unsigned int valid_hooks;

	/* Number of entries */
	unsigned int num_entries;

	/* Total size of new entries */
	unsigned int size;

	/* Hook entry points. */
	unsigned int hook_entry[NF_INET_NUMHOOKS];

	/* Underflow points. */
	unsigned int underflow[NF_INET_NUMHOOKS];

	/* Information about old entries: */
	/* Number of counters (must be equal to current number of entries). */
	unsigned int num_counters;
	/* The old entries' counters. */
	struct xt_counters __user *counters;

	/* The entries (hang off end: not really an array). */
	struct ip6t_entry entries[0];
};

/* The argument to IP6T_SO_ADD_COUNTERS. */
#define ip6t_counters_info xt_counters_info

/* The argument to IP6T_SO_GET_ENTRIES. */
struct ip6t_get_entries
{
	/* Which table: user fills this in. */
	char name[IP6T_TABLE_MAXNAMELEN];

	/* User fills this in: total entry size. */
	unsigned int size;

	/* The entries. */
	struct ip6t_entry entrytable[0];
};

/* Standard return verdict, or do jump. */
#define IP6T_STANDARD_TARGET XT_STANDARD_TARGET
/* Error verdict. */
#define IP6T_ERROR_TARGET XT_ERROR_TARGET

/* Helper functions */
static __inline__ struct ip6t_entry_target *
ip6t_get_target(struct ip6t_entry *e)
{
	return (void *)e + e->target_offset;
}

/* fn returns 0 to continue iteration */
#define IP6T_MATCH_ITERATE(e, fn, args...) \
	XT_MATCH_ITERATE(struct ip6t_entry, e, fn, ## args)

/* fn returns 0 to continue iteration */
#define IP6T_ENTRY_ITERATE(entries, size, fn, args...) \
	XT_ENTRY_ITERATE(struct ip6t_entry, entries, size, fn, ## args)

/*
 *	Main firewall chains definitions and global var's definitions.
 */

#ifdef __KERNEL__

#include <linux/init.h>
extern void ip6t_init(void) __init;

extern int ip6t_register_table(struct xt_table *table,
			       const struct ip6t_replace *repl);
extern void ip6t_unregister_table(struct xt_table *table);
extern unsigned int ip6t_do_table(struct sk_buff *skb,
				  unsigned int hook,
				  const struct net_device *in,
				  const struct net_device *out,
				  struct xt_table *table);

/* Check for an extension */
extern int ip6t_ext_hdr(u8 nexthdr);
/* find specified header and get offset to it */
extern int ipv6_find_hdr(const struct sk_buff *skb, unsigned int *offset,
			 int target, unsigned short *fragoff);

extern int ip6_masked_addrcmp(const struct in6_addr *addr1,
			      const struct in6_addr *mask,
			      const struct in6_addr *addr2);

#define IP6T_ALIGN(s) XT_ALIGN(s)

#ifdef CONFIG_COMPAT
#include <net/compat.h>

struct compat_ip6t_entry
{
	struct ip6t_ip6 ipv6;
	compat_uint_t nfcache;
	u_int16_t target_offset;
	u_int16_t next_offset;
	compat_uint_t comefrom;
	struct compat_xt_counters counters;
	unsigned char elems[0];
};

static inline struct ip6t_entry_target *
compat_ip6t_get_target(struct compat_ip6t_entry *e)
{
	return (void *)e + e->target_offset;
}

#define COMPAT_IP6T_ALIGN(s)	COMPAT_XT_ALIGN(s)

/* fn returns 0 to continue iteration */
#define COMPAT_IP6T_MATCH_ITERATE(e, fn, args...) \
	XT_MATCH_ITERATE(struct compat_ip6t_entry, e, fn, ## args)

/* fn returns 0 to continue iteration */
#define COMPAT_IP6T_ENTRY_ITERATE(entries, size, fn, args...) \
	XT_ENTRY_ITERATE(struct compat_ip6t_entry, entries, size, fn, ## args)

#define COMPAT_IP6T_ENTRY_ITERATE_CONTINUE(entries, size, n, fn, args...) \
	XT_ENTRY_ITERATE_CONTINUE(struct compat_ip6t_entry, entries, size, n, \
				  fn, ## args)

#endif /* CONFIG_COMPAT */
#endif /*__KERNEL__*/
#endif /* _IP6_TABLES_H */
