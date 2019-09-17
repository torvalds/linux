// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2003-2013 Jozsef Kadlecsik <kadlec@netfilter.org>
 * Copyright (C) 2013 Oliver Smith <oliver@8.c.9.b.0.7.4.0.1.0.0.2.ip6.arpa>
 */

/* Kernel module implementing an IP set type: the hash:net type */

#include <linux/jhash.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/netlink.h>

#include <linux/netfilter.h>
#include <linux/netfilter/ipset/pfxlen.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_hash.h>

#define IPSET_TYPE_REV_MIN	0
/*				1	   Forceadd support added */
#define IPSET_TYPE_REV_MAX	2	/* skbinfo support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Oliver Smith <oliver@8.c.9.b.0.7.4.0.1.0.0.2.ip6.arpa>");
IP_SET_MODULE_DESC("hash:net,net", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_hash:net,net");

/* Type specific function prefix */
#define HTYPE		hash_netnet
#define IP_SET_HASH_WITH_NETS
#define IPSET_NET_COUNT 2

/* IPv4 variants */

/* Member elements  */
struct hash_netnet4_elem {
	union {
		__be32 ip[2];
		__be64 ipcmp;
	};
	u8 nomatch;
	u8 padding;
	union {
		u8 cidr[2];
		u16 ccmp;
	};
};

/* Common functions */

static inline bool
hash_netnet4_data_equal(const struct hash_netnet4_elem *ip1,
			const struct hash_netnet4_elem *ip2,
			u32 *multi)
{
	return ip1->ipcmp == ip2->ipcmp &&
	       ip1->ccmp == ip2->ccmp;
}

static inline int
hash_netnet4_do_data_match(const struct hash_netnet4_elem *elem)
{
	return elem->nomatch ? -ENOTEMPTY : 1;
}

static inline void
hash_netnet4_data_set_flags(struct hash_netnet4_elem *elem, u32 flags)
{
	elem->nomatch = (flags >> 16) & IPSET_FLAG_NOMATCH;
}

static inline void
hash_netnet4_data_reset_flags(struct hash_netnet4_elem *elem, u8 *flags)
{
	swap(*flags, elem->nomatch);
}

static inline void
hash_netnet4_data_reset_elem(struct hash_netnet4_elem *elem,
			     struct hash_netnet4_elem *orig)
{
	elem->ip[1] = orig->ip[1];
}

static inline void
hash_netnet4_data_netmask(struct hash_netnet4_elem *elem, u8 cidr, bool inner)
{
	if (inner) {
		elem->ip[1] &= ip_set_netmask(cidr);
		elem->cidr[1] = cidr;
	} else {
		elem->ip[0] &= ip_set_netmask(cidr);
		elem->cidr[0] = cidr;
	}
}

static bool
hash_netnet4_data_list(struct sk_buff *skb,
		       const struct hash_netnet4_elem *data)
{
	u32 flags = data->nomatch ? IPSET_FLAG_NOMATCH : 0;

	if (nla_put_ipaddr4(skb, IPSET_ATTR_IP, data->ip[0]) ||
	    nla_put_ipaddr4(skb, IPSET_ATTR_IP2, data->ip[1]) ||
	    nla_put_u8(skb, IPSET_ATTR_CIDR, data->cidr[0]) ||
	    nla_put_u8(skb, IPSET_ATTR_CIDR2, data->cidr[1]) ||
	    (flags &&
	     nla_put_net32(skb, IPSET_ATTR_CADT_FLAGS, htonl(flags))))
		goto nla_put_failure;
	return false;

nla_put_failure:
	return true;
}

static inline void
hash_netnet4_data_next(struct hash_netnet4_elem *next,
		       const struct hash_netnet4_elem *d)
{
	next->ipcmp = d->ipcmp;
}

#define MTYPE		hash_netnet4
#define HOST_MASK	32
#include "ip_set_hash_gen.h"

static void
hash_netnet4_init(struct hash_netnet4_elem *e)
{
	e->cidr[0] = HOST_MASK;
	e->cidr[1] = HOST_MASK;
}

static int
hash_netnet4_kadt(struct ip_set *set, const struct sk_buff *skb,
		  const struct xt_action_param *par,
		  enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	const struct hash_netnet4 *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netnet4_elem e = { };
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);

	e.cidr[0] = INIT_CIDR(h->nets[0].cidr[0], HOST_MASK);
	e.cidr[1] = INIT_CIDR(h->nets[0].cidr[1], HOST_MASK);
	if (adt == IPSET_TEST)
		e.ccmp = (HOST_MASK << (sizeof(e.cidr[0]) * 8)) | HOST_MASK;

	ip4addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &e.ip[0]);
	ip4addrptr(skb, opt->flags & IPSET_DIM_TWO_SRC, &e.ip[1]);
	e.ip[0] &= ip_set_netmask(e.cidr[0]);
	e.ip[1] &= ip_set_netmask(e.cidr[1]);

	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
hash_netnet4_uadt(struct ip_set *set, struct nlattr *tb[],
		  enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	const struct hash_netnet4 *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netnet4_elem e = { };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	u32 ip = 0, ip_to = 0;
	u32 ip2 = 0, ip2_from = 0, ip2_to = 0;
	int ret;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	hash_netnet4_init(&e);
	if (unlikely(!tb[IPSET_ATTR_IP] || !tb[IPSET_ATTR_IP2] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;

	ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP], &ip);
	if (ret)
		return ret;

	ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP2], &ip2_from);
	if (ret)
		return ret;

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	if (tb[IPSET_ATTR_CIDR]) {
		e.cidr[0] = nla_get_u8(tb[IPSET_ATTR_CIDR]);
		if (!e.cidr[0] || e.cidr[0] > HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
	}

	if (tb[IPSET_ATTR_CIDR2]) {
		e.cidr[1] = nla_get_u8(tb[IPSET_ATTR_CIDR2]);
		if (!e.cidr[1] || e.cidr[1] > HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
	}

	if (tb[IPSET_ATTR_CADT_FLAGS]) {
		u32 cadt_flags = ip_set_get_h32(tb[IPSET_ATTR_CADT_FLAGS]);

		if (cadt_flags & IPSET_FLAG_NOMATCH)
			flags |= (IPSET_FLAG_NOMATCH << 16);
	}

	if (adt == IPSET_TEST || !(tb[IPSET_ATTR_IP_TO] ||
				   tb[IPSET_ATTR_IP2_TO])) {
		e.ip[0] = htonl(ip & ip_set_hostmask(e.cidr[0]));
		e.ip[1] = htonl(ip2_from & ip_set_hostmask(e.cidr[1]));
		ret = adtfn(set, &e, &ext, &ext, flags);
		return ip_set_enomatch(ret, flags, adt, set) ? -ret :
		       ip_set_eexist(ret, flags) ? 0 : ret;
	}

	ip_to = ip;
	if (tb[IPSET_ATTR_IP_TO]) {
		ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP_TO], &ip_to);
		if (ret)
			return ret;
		if (ip_to < ip)
			swap(ip, ip_to);
		if (unlikely(ip + UINT_MAX == ip_to))
			return -IPSET_ERR_HASH_RANGE;
	} else {
		ip_set_mask_from_to(ip, ip_to, e.cidr[0]);
	}

	ip2_to = ip2_from;
	if (tb[IPSET_ATTR_IP2_TO]) {
		ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP2_TO], &ip2_to);
		if (ret)
			return ret;
		if (ip2_to < ip2_from)
			swap(ip2_from, ip2_to);
		if (unlikely(ip2_from + UINT_MAX == ip2_to))
			return -IPSET_ERR_HASH_RANGE;
	} else {
		ip_set_mask_from_to(ip2_from, ip2_to, e.cidr[1]);
	}

	if (retried) {
		ip = ntohl(h->next.ip[0]);
		ip2 = ntohl(h->next.ip[1]);
	} else {
		ip2 = ip2_from;
	}

	do {
		e.ip[0] = htonl(ip);
		ip = ip_set_range_to_cidr(ip, ip_to, &e.cidr[0]);
		do {
			e.ip[1] = htonl(ip2);
			ip2 = ip_set_range_to_cidr(ip2, ip2_to, &e.cidr[1]);
			ret = adtfn(set, &e, &ext, &ext, flags);
			if (ret && !ip_set_eexist(ret, flags))
				return ret;

			ret = 0;
		} while (ip2++ < ip2_to);
		ip2 = ip2_from;
	} while (ip++ < ip_to);
	return ret;
}

/* IPv6 variants */

struct hash_netnet6_elem {
	union nf_inet_addr ip[2];
	u8 nomatch;
	u8 padding;
	union {
		u8 cidr[2];
		u16 ccmp;
	};
};

/* Common functions */

static inline bool
hash_netnet6_data_equal(const struct hash_netnet6_elem *ip1,
			const struct hash_netnet6_elem *ip2,
			u32 *multi)
{
	return ipv6_addr_equal(&ip1->ip[0].in6, &ip2->ip[0].in6) &&
	       ipv6_addr_equal(&ip1->ip[1].in6, &ip2->ip[1].in6) &&
	       ip1->ccmp == ip2->ccmp;
}

static inline int
hash_netnet6_do_data_match(const struct hash_netnet6_elem *elem)
{
	return elem->nomatch ? -ENOTEMPTY : 1;
}

static inline void
hash_netnet6_data_set_flags(struct hash_netnet6_elem *elem, u32 flags)
{
	elem->nomatch = (flags >> 16) & IPSET_FLAG_NOMATCH;
}

static inline void
hash_netnet6_data_reset_flags(struct hash_netnet6_elem *elem, u8 *flags)
{
	swap(*flags, elem->nomatch);
}

static inline void
hash_netnet6_data_reset_elem(struct hash_netnet6_elem *elem,
			     struct hash_netnet6_elem *orig)
{
	elem->ip[1] = orig->ip[1];
}

static inline void
hash_netnet6_data_netmask(struct hash_netnet6_elem *elem, u8 cidr, bool inner)
{
	if (inner) {
		ip6_netmask(&elem->ip[1], cidr);
		elem->cidr[1] = cidr;
	} else {
		ip6_netmask(&elem->ip[0], cidr);
		elem->cidr[0] = cidr;
	}
}

static bool
hash_netnet6_data_list(struct sk_buff *skb,
		       const struct hash_netnet6_elem *data)
{
	u32 flags = data->nomatch ? IPSET_FLAG_NOMATCH : 0;

	if (nla_put_ipaddr6(skb, IPSET_ATTR_IP, &data->ip[0].in6) ||
	    nla_put_ipaddr6(skb, IPSET_ATTR_IP2, &data->ip[1].in6) ||
	    nla_put_u8(skb, IPSET_ATTR_CIDR, data->cidr[0]) ||
	    nla_put_u8(skb, IPSET_ATTR_CIDR2, data->cidr[1]) ||
	    (flags &&
	     nla_put_net32(skb, IPSET_ATTR_CADT_FLAGS, htonl(flags))))
		goto nla_put_failure;
	return false;

nla_put_failure:
	return true;
}

static inline void
hash_netnet6_data_next(struct hash_netnet6_elem *next,
		       const struct hash_netnet6_elem *d)
{
}

#undef MTYPE
#undef HOST_MASK

#define MTYPE		hash_netnet6
#define HOST_MASK	128
#define IP_SET_EMIT_CREATE
#include "ip_set_hash_gen.h"

static void
hash_netnet6_init(struct hash_netnet6_elem *e)
{
	e->cidr[0] = HOST_MASK;
	e->cidr[1] = HOST_MASK;
}

static int
hash_netnet6_kadt(struct ip_set *set, const struct sk_buff *skb,
		  const struct xt_action_param *par,
		  enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	const struct hash_netnet6 *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netnet6_elem e = { };
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);

	e.cidr[0] = INIT_CIDR(h->nets[0].cidr[0], HOST_MASK);
	e.cidr[1] = INIT_CIDR(h->nets[0].cidr[1], HOST_MASK);
	if (adt == IPSET_TEST)
		e.ccmp = (HOST_MASK << (sizeof(u8) * 8)) | HOST_MASK;

	ip6addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &e.ip[0].in6);
	ip6addrptr(skb, opt->flags & IPSET_DIM_TWO_SRC, &e.ip[1].in6);
	ip6_netmask(&e.ip[0], e.cidr[0]);
	ip6_netmask(&e.ip[1], e.cidr[1]);

	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
hash_netnet6_uadt(struct ip_set *set, struct nlattr *tb[],
		  enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netnet6_elem e = { };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	int ret;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	hash_netnet6_init(&e);
	if (unlikely(!tb[IPSET_ATTR_IP] || !tb[IPSET_ATTR_IP2] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;
	if (unlikely(tb[IPSET_ATTR_IP_TO] || tb[IPSET_ATTR_IP2_TO]))
		return -IPSET_ERR_HASH_RANGE_UNSUPPORTED;

	ret = ip_set_get_ipaddr6(tb[IPSET_ATTR_IP], &e.ip[0]);
	if (ret)
		return ret;

	ret = ip_set_get_ipaddr6(tb[IPSET_ATTR_IP2], &e.ip[1]);
	if (ret)
		return ret;

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	if (tb[IPSET_ATTR_CIDR]) {
		e.cidr[0] = nla_get_u8(tb[IPSET_ATTR_CIDR]);
		if (!e.cidr[0] || e.cidr[0] > HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
	}

	if (tb[IPSET_ATTR_CIDR2]) {
		e.cidr[1] = nla_get_u8(tb[IPSET_ATTR_CIDR2]);
		if (!e.cidr[1] || e.cidr[1] > HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
	}

	ip6_netmask(&e.ip[0], e.cidr[0]);
	ip6_netmask(&e.ip[1], e.cidr[1]);

	if (tb[IPSET_ATTR_CADT_FLAGS]) {
		u32 cadt_flags = ip_set_get_h32(tb[IPSET_ATTR_CADT_FLAGS]);

		if (cadt_flags & IPSET_FLAG_NOMATCH)
			flags |= (IPSET_FLAG_NOMATCH << 16);
	}

	ret = adtfn(set, &e, &ext, &ext, flags);

	return ip_set_enomatch(ret, flags, adt, set) ? -ret :
	       ip_set_eexist(ret, flags) ? 0 : ret;
}

static struct ip_set_type hash_netnet_type __read_mostly = {
	.name		= "hash:net,net",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_IP | IPSET_TYPE_IP2 | IPSET_TYPE_NOMATCH,
	.dimension	= IPSET_DIM_TWO,
	.family		= NFPROTO_UNSPEC,
	.revision_min	= IPSET_TYPE_REV_MIN,
	.revision_max	= IPSET_TYPE_REV_MAX,
	.create		= hash_netnet_create,
	.create_policy	= {
		[IPSET_ATTR_HASHSIZE]	= { .type = NLA_U32 },
		[IPSET_ATTR_MAXELEM]	= { .type = NLA_U32 },
		[IPSET_ATTR_PROBES]	= { .type = NLA_U8 },
		[IPSET_ATTR_RESIZE]	= { .type = NLA_U8  },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
	},
	.adt_policy	= {
		[IPSET_ATTR_IP]		= { .type = NLA_NESTED },
		[IPSET_ATTR_IP_TO]	= { .type = NLA_NESTED },
		[IPSET_ATTR_IP2]	= { .type = NLA_NESTED },
		[IPSET_ATTR_IP2_TO]	= { .type = NLA_NESTED },
		[IPSET_ATTR_CIDR]	= { .type = NLA_U8 },
		[IPSET_ATTR_CIDR2]	= { .type = NLA_U8 },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
		[IPSET_ATTR_BYTES]	= { .type = NLA_U64 },
		[IPSET_ATTR_PACKETS]	= { .type = NLA_U64 },
		[IPSET_ATTR_COMMENT]	= { .type = NLA_NUL_STRING,
					    .len  = IPSET_MAX_COMMENT_SIZE },
		[IPSET_ATTR_SKBMARK]	= { .type = NLA_U64 },
		[IPSET_ATTR_SKBPRIO]	= { .type = NLA_U32 },
		[IPSET_ATTR_SKBQUEUE]	= { .type = NLA_U16 },
	},
	.me		= THIS_MODULE,
};

static int __init
hash_netnet_init(void)
{
	return ip_set_type_register(&hash_netnet_type);
}

static void __exit
hash_netnet_fini(void)
{
	rcu_barrier();
	ip_set_type_unregister(&hash_netnet_type);
}

module_init(hash_netnet_init);
module_exit(hash_netnet_fini);
