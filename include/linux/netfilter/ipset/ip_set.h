/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 *                         Martin Josefsson <gandalf@wlug.westbo.se>
 * Copyright (C) 2003-2011 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _IP_SET_H
#define _IP_SET_H

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/stringify.h>
#include <linux/vmalloc.h>
#include <net/netlink.h>
#include <uapi/linux/netfilter/ipset/ip_set.h>

#define _IP_SET_MODULE_DESC(a, b, c)		\
	MODULE_DESCRIPTION(a " type of IP sets, revisions " b "-" c)
#define IP_SET_MODULE_DESC(a, b, c)		\
	_IP_SET_MODULE_DESC(a, __stringify(b), __stringify(c))

/* Set features */
enum ip_set_feature {
	IPSET_TYPE_IP_FLAG = 0,
	IPSET_TYPE_IP = (1 << IPSET_TYPE_IP_FLAG),
	IPSET_TYPE_PORT_FLAG = 1,
	IPSET_TYPE_PORT = (1 << IPSET_TYPE_PORT_FLAG),
	IPSET_TYPE_MAC_FLAG = 2,
	IPSET_TYPE_MAC = (1 << IPSET_TYPE_MAC_FLAG),
	IPSET_TYPE_IP2_FLAG = 3,
	IPSET_TYPE_IP2 = (1 << IPSET_TYPE_IP2_FLAG),
	IPSET_TYPE_NAME_FLAG = 4,
	IPSET_TYPE_NAME = (1 << IPSET_TYPE_NAME_FLAG),
	IPSET_TYPE_IFACE_FLAG = 5,
	IPSET_TYPE_IFACE = (1 << IPSET_TYPE_IFACE_FLAG),
	IPSET_TYPE_NOMATCH_FLAG = 6,
	IPSET_TYPE_NOMATCH = (1 << IPSET_TYPE_NOMATCH_FLAG),
	/* Strictly speaking not a feature, but a flag for dumping:
	 * this settype must be dumped last */
	IPSET_DUMP_LAST_FLAG = 7,
	IPSET_DUMP_LAST = (1 << IPSET_DUMP_LAST_FLAG),
};

struct ip_set;

typedef int (*ipset_adtfn)(struct ip_set *set, void *value,
			   u32 timeout, u32 flags);

/* Kernel API function options */
struct ip_set_adt_opt {
	u8 family;		/* Actual protocol family */
	u8 dim;			/* Dimension of match/target */
	u8 flags;		/* Direction and negation flags */
	u32 cmdflags;		/* Command-like flags */
	u32 timeout;		/* Timeout value */
};

/* Set type, variant-specific part */
struct ip_set_type_variant {
	/* Kernelspace: test/add/del entries
	 *		returns negative error code,
	 *			zero for no match/success to add/delete
	 *			positive for matching element */
	int (*kadt)(struct ip_set *set, const struct sk_buff *skb,
		    const struct xt_action_param *par,
		    enum ipset_adt adt, const struct ip_set_adt_opt *opt);

	/* Userspace: test/add/del entries
	 *		returns negative error code,
	 *			zero for no match/success to add/delete
	 *			positive for matching element */
	int (*uadt)(struct ip_set *set, struct nlattr *tb[],
		    enum ipset_adt adt, u32 *lineno, u32 flags, bool retried);

	/* Low level add/del/test functions */
	ipset_adtfn adt[IPSET_ADT_MAX];

	/* When adding entries and set is full, try to resize the set */
	int (*resize)(struct ip_set *set, bool retried);
	/* Destroy the set */
	void (*destroy)(struct ip_set *set);
	/* Flush the elements */
	void (*flush)(struct ip_set *set);
	/* Expire entries before listing */
	void (*expire)(struct ip_set *set);
	/* List set header data */
	int (*head)(struct ip_set *set, struct sk_buff *skb);
	/* List elements */
	int (*list)(const struct ip_set *set, struct sk_buff *skb,
		    struct netlink_callback *cb);

	/* Return true if "b" set is the same as "a"
	 * according to the create set parameters */
	bool (*same_set)(const struct ip_set *a, const struct ip_set *b);
};

/* The core set type structure */
struct ip_set_type {
	struct list_head list;

	/* Typename */
	char name[IPSET_MAXNAMELEN];
	/* Protocol version */
	u8 protocol;
	/* Set features to control swapping */
	u8 features;
	/* Set type dimension */
	u8 dimension;
	/*
	 * Supported family: may be NFPROTO_UNSPEC for both
	 * NFPROTO_IPV4/NFPROTO_IPV6.
	 */
	u8 family;
	/* Type revisions */
	u8 revision_min, revision_max;

	/* Create set */
	int (*create)(struct ip_set *set, struct nlattr *tb[], u32 flags);

	/* Attribute policies */
	const struct nla_policy create_policy[IPSET_ATTR_CREATE_MAX + 1];
	const struct nla_policy adt_policy[IPSET_ATTR_ADT_MAX + 1];

	/* Set this to THIS_MODULE if you are a module, otherwise NULL */
	struct module *me;
};

/* register and unregister set type */
extern int ip_set_type_register(struct ip_set_type *set_type);
extern void ip_set_type_unregister(struct ip_set_type *set_type);

/* A generic IP set */
struct ip_set {
	/* The name of the set */
	char name[IPSET_MAXNAMELEN];
	/* Lock protecting the set data */
	rwlock_t lock;
	/* References to the set */
	u32 ref;
	/* The core set type */
	struct ip_set_type *type;
	/* The type variant doing the real job */
	const struct ip_set_type_variant *variant;
	/* The actual INET family of the set */
	u8 family;
	/* The type revision */
	u8 revision;
	/* The type specific data */
	void *data;
};

/* register and unregister set references */
extern ip_set_id_t ip_set_get_byname(const char *name, struct ip_set **set);
extern void ip_set_put_byindex(ip_set_id_t index);
extern const char *ip_set_name_byindex(ip_set_id_t index);
extern ip_set_id_t ip_set_nfnl_get(const char *name);
extern ip_set_id_t ip_set_nfnl_get_byindex(ip_set_id_t index);
extern void ip_set_nfnl_put(ip_set_id_t index);

/* API for iptables set match, and SET target */

extern int ip_set_add(ip_set_id_t id, const struct sk_buff *skb,
		      const struct xt_action_param *par,
		      const struct ip_set_adt_opt *opt);
extern int ip_set_del(ip_set_id_t id, const struct sk_buff *skb,
		      const struct xt_action_param *par,
		      const struct ip_set_adt_opt *opt);
extern int ip_set_test(ip_set_id_t id, const struct sk_buff *skb,
		       const struct xt_action_param *par,
		       const struct ip_set_adt_opt *opt);

/* Utility functions */
extern void *ip_set_alloc(size_t size);
extern void ip_set_free(void *members);
extern int ip_set_get_ipaddr4(struct nlattr *nla,  __be32 *ipaddr);
extern int ip_set_get_ipaddr6(struct nlattr *nla, union nf_inet_addr *ipaddr);

static inline int
ip_set_get_hostipaddr4(struct nlattr *nla, u32 *ipaddr)
{
	__be32 ip;
	int ret = ip_set_get_ipaddr4(nla, &ip);

	if (ret)
		return ret;
	*ipaddr = ntohl(ip);
	return 0;
}

/* Ignore IPSET_ERR_EXIST errors if asked to do so? */
static inline bool
ip_set_eexist(int ret, u32 flags)
{
	return ret == -IPSET_ERR_EXIST && (flags & IPSET_FLAG_EXIST);
}

/* Check the NLA_F_NET_BYTEORDER flag */
static inline bool
ip_set_attr_netorder(struct nlattr *tb[], int type)
{
	return tb[type] && (tb[type]->nla_type & NLA_F_NET_BYTEORDER);
}

static inline bool
ip_set_optattr_netorder(struct nlattr *tb[], int type)
{
	return !tb[type] || (tb[type]->nla_type & NLA_F_NET_BYTEORDER);
}

/* Useful converters */
static inline u32
ip_set_get_h32(const struct nlattr *attr)
{
	return ntohl(nla_get_be32(attr));
}

static inline u16
ip_set_get_h16(const struct nlattr *attr)
{
	return ntohs(nla_get_be16(attr));
}

#define ipset_nest_start(skb, attr) nla_nest_start(skb, attr | NLA_F_NESTED)
#define ipset_nest_end(skb, start)  nla_nest_end(skb, start)

static inline int nla_put_ipaddr4(struct sk_buff *skb, int type, __be32 ipaddr)
{
	struct nlattr *__nested = ipset_nest_start(skb, type);
	int ret;

	if (!__nested)
		return -EMSGSIZE;
	ret = nla_put_net32(skb, IPSET_ATTR_IPADDR_IPV4, ipaddr);
	if (!ret)
		ipset_nest_end(skb, __nested);
	return ret;
}

static inline int nla_put_ipaddr6(struct sk_buff *skb, int type,
				  const struct in6_addr *ipaddrptr)
{
	struct nlattr *__nested = ipset_nest_start(skb, type);
	int ret;

	if (!__nested)
		return -EMSGSIZE;
	ret = nla_put(skb, IPSET_ATTR_IPADDR_IPV6,
		      sizeof(struct in6_addr), ipaddrptr);
	if (!ret)
		ipset_nest_end(skb, __nested);
	return ret;
}

/* Get address from skbuff */
static inline __be32
ip4addr(const struct sk_buff *skb, bool src)
{
	return src ? ip_hdr(skb)->saddr : ip_hdr(skb)->daddr;
}

static inline void
ip4addrptr(const struct sk_buff *skb, bool src, __be32 *addr)
{
	*addr = src ? ip_hdr(skb)->saddr : ip_hdr(skb)->daddr;
}

static inline void
ip6addrptr(const struct sk_buff *skb, bool src, struct in6_addr *addr)
{
	memcpy(addr, src ? &ipv6_hdr(skb)->saddr : &ipv6_hdr(skb)->daddr,
	       sizeof(*addr));
}

/* Calculate the bytes required to store the inclusive range of a-b */
static inline int
bitmap_bytes(u32 a, u32 b)
{
	return 4 * ((((b - a + 8) / 8) + 3) / 4);
}

#endif /*_IP_SET_H */
