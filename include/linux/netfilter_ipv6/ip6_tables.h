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

#define IP6T_FUNCTION_MAXNAMELEN 30
#define IP6T_TABLE_MAXNAMELEN 32

/* Yes, Virginia, you have to zero the padding. */
struct ip6t_ip6 {
	/* Source and destination IP6 addr */
	struct in6_addr src, dst;		
	/* Mask for src and dest IP6 addr */
	struct in6_addr smsk, dmsk;
	char iniface[IFNAMSIZ], outiface[IFNAMSIZ];
	unsigned char iniface_mask[IFNAMSIZ], outiface_mask[IFNAMSIZ];

	/* ARGH, HopByHop uses 0, so can't do 0 = ANY,
	   instead IP6T_F_NOPROTO must be set */
	u_int16_t proto;
	/* TOS to match iff flags & IP6T_F_TOS */
	u_int8_t tos;

	/* Flags word */
	u_int8_t flags;
	/* Inverse flags */
	u_int8_t invflags;
};

/* FIXME: If alignment in kernel different from userspace? --RR */
struct ip6t_entry_match
{
	union {
		struct {
			u_int16_t match_size;

			/* Used by userspace */
			char name[IP6T_FUNCTION_MAXNAMELEN];
		} user;
		struct {
			u_int16_t match_size;

			/* Used inside the kernel */
			struct ip6t_match *match;
		} kernel;

		/* Total length */
		u_int16_t match_size;
	} u;

	unsigned char data[0];
};

struct ip6t_entry_target
{
	union {
		struct {
			u_int16_t target_size;

			/* Used by userspace */
			char name[IP6T_FUNCTION_MAXNAMELEN];
		} user;
		struct {
			u_int16_t target_size;

			/* Used inside the kernel */
			struct ip6t_target *target;
		} kernel;

		/* Total length */
		u_int16_t target_size;
	} u;

	unsigned char data[0];
};

struct ip6t_standard_target
{
	struct ip6t_entry_target target;
	int verdict;
};

struct ip6t_counters
{
	u_int64_t pcnt, bcnt;			/* Packet and byte counters */
};

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
#define IP6T_INV_PROTO		0x40	/* Invert the sense of PROTO. */
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
	struct ip6t_counters counters;

	/* The matches (if any), then the target. */
	unsigned char elems[0];
};

/*
 * New IP firewall options for [gs]etsockopt at the RAW IP level.
 * Unlike BSD Linux inherits IP options so you don't have to use
 * a raw socket for this. Instead we check rights in the calls. */
#define IP6T_BASE_CTL			64	/* base for firewall socket options */

#define IP6T_SO_SET_REPLACE		(IP6T_BASE_CTL)
#define IP6T_SO_SET_ADD_COUNTERS	(IP6T_BASE_CTL + 1)
#define IP6T_SO_SET_MAX			IP6T_SO_SET_ADD_COUNTERS

#define IP6T_SO_GET_INFO		(IP6T_BASE_CTL)
#define IP6T_SO_GET_ENTRIES		(IP6T_BASE_CTL + 1)
#define IP6T_SO_GET_MAX			IP6T_SO_GET_ENTRIES

/* CONTINUE verdict for targets */
#define IP6T_CONTINUE 0xFFFFFFFF

/* For standard target */
#define IP6T_RETURN (-NF_REPEAT - 1)

/* TCP matching stuff */
struct ip6t_tcp
{
	u_int16_t spts[2];			/* Source port range. */
	u_int16_t dpts[2];			/* Destination port range. */
	u_int8_t option;			/* TCP Option iff non-zero*/
	u_int8_t flg_mask;			/* TCP flags mask byte */
	u_int8_t flg_cmp;			/* TCP flags compare byte */
	u_int8_t invflags;			/* Inverse flags */
};

/* Values for "inv" field in struct ipt_tcp. */
#define IP6T_TCP_INV_SRCPT	0x01	/* Invert the sense of source ports. */
#define IP6T_TCP_INV_DSTPT	0x02	/* Invert the sense of dest ports. */
#define IP6T_TCP_INV_FLAGS	0x04	/* Invert the sense of TCP flags. */
#define IP6T_TCP_INV_OPTION	0x08	/* Invert the sense of option test. */
#define IP6T_TCP_INV_MASK	0x0F	/* All possible flags. */

/* UDP matching stuff */
struct ip6t_udp
{
	u_int16_t spts[2];			/* Source port range. */
	u_int16_t dpts[2];			/* Destination port range. */
	u_int8_t invflags;			/* Inverse flags */
};

/* Values for "invflags" field in struct ipt_udp. */
#define IP6T_UDP_INV_SRCPT	0x01	/* Invert the sense of source ports. */
#define IP6T_UDP_INV_DSTPT	0x02	/* Invert the sense of dest ports. */
#define IP6T_UDP_INV_MASK	0x03	/* All possible flags. */

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
	unsigned int hook_entry[NF_IP6_NUMHOOKS];

	/* Underflow points. */
	unsigned int underflow[NF_IP6_NUMHOOKS];

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
	unsigned int hook_entry[NF_IP6_NUMHOOKS];

	/* Underflow points. */
	unsigned int underflow[NF_IP6_NUMHOOKS];

	/* Information about old entries: */
	/* Number of counters (must be equal to current number of entries). */
	unsigned int num_counters;
	/* The old entries' counters. */
	struct ip6t_counters __user *counters;

	/* The entries (hang off end: not really an array). */
	struct ip6t_entry entries[0];
};

/* The argument to IP6T_SO_ADD_COUNTERS. */
struct ip6t_counters_info
{
	/* Which table. */
	char name[IP6T_TABLE_MAXNAMELEN];

	unsigned int num_counters;

	/* The counters (actually `number' of these). */
	struct ip6t_counters counters[0];
};

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
#define IP6T_STANDARD_TARGET ""
/* Error verdict. */
#define IP6T_ERROR_TARGET "ERROR"

/* Helper functions */
static __inline__ struct ip6t_entry_target *
ip6t_get_target(struct ip6t_entry *e)
{
	return (void *)e + e->target_offset;
}

/* fn returns 0 to continue iteration */
#define IP6T_MATCH_ITERATE(e, fn, args...)	\
({						\
	unsigned int __i;			\
	int __ret = 0;				\
	struct ip6t_entry_match *__m;		\
						\
	for (__i = sizeof(struct ip6t_entry);	\
	     __i < (e)->target_offset;		\
	     __i += __m->u.match_size) {	\
		__m = (void *)(e) + __i;	\
						\
		__ret = fn(__m , ## args);	\
		if (__ret != 0)			\
			break;			\
	}					\
	__ret;					\
})

/* fn returns 0 to continue iteration */
#define IP6T_ENTRY_ITERATE(entries, size, fn, args...)		\
({								\
	unsigned int __i;					\
	int __ret = 0;						\
	struct ip6t_entry *__e;					\
								\
	for (__i = 0; __i < (size); __i += __e->next_offset) {	\
		__e = (void *)(entries) + __i;			\
								\
		__ret = fn(__e , ## args);			\
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
extern void ip6t_init(void) __init;

struct ip6t_match
{
	struct list_head list;

	const char name[IP6T_FUNCTION_MAXNAMELEN];

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
			  const struct ip6t_ip6 *ip,
			  void *matchinfo,
			  unsigned int matchinfosize,
			  unsigned int hook_mask);

	/* Called when entry of this type deleted. */
	void (*destroy)(void *matchinfo, unsigned int matchinfosize);

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;
};

/* Registration hooks for targets. */
struct ip6t_target
{
	struct list_head list;

	const char name[IP6T_FUNCTION_MAXNAMELEN];

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
			  const struct ip6t_entry *e,
			  void *targinfo,
			  unsigned int targinfosize,
			  unsigned int hook_mask);

	/* Called when entry of this type deleted. */
	void (*destroy)(void *targinfo, unsigned int targinfosize);

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;
};

extern int ip6t_register_target(struct ip6t_target *target);
extern void ip6t_unregister_target(struct ip6t_target *target);

extern int ip6t_register_match(struct ip6t_match *match);
extern void ip6t_unregister_match(struct ip6t_match *match);

/* Furniture shopping... */
struct ip6t_table
{
	struct list_head list;

	/* A unique name... */
	char name[IP6T_TABLE_MAXNAMELEN];

	/* What hooks you will enter on */
	unsigned int valid_hooks;

	/* Lock for the curtain */
	rwlock_t lock;

	/* Man behind the curtain... */
	struct ip6t_table_info *private;

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;
};

extern int ip6t_register_table(struct ip6t_table *table,
			       const struct ip6t_replace *repl);
extern void ip6t_unregister_table(struct ip6t_table *table);
extern unsigned int ip6t_do_table(struct sk_buff **pskb,
				  unsigned int hook,
				  const struct net_device *in,
				  const struct net_device *out,
				  struct ip6t_table *table,
				  void *userdata);

/* Check for an extension */
extern int ip6t_ext_hdr(u8 nexthdr);
/* find specified header and get offset to it */
extern int ipv6_find_hdr(const struct sk_buff *skb, unsigned int *offset,
			 u8 target);

#define IP6T_ALIGN(s) (((s) + (__alignof__(struct ip6t_entry)-1)) & ~(__alignof__(struct ip6t_entry)-1))

#endif /*__KERNEL__*/
#endif /* _IP6_TABLES_H */
