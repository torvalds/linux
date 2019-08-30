// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2011-2013 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 */

/* Kernel module implementing an IP set type: the hash:net,iface type */

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
#include <linux/netfilter_bridge.h>
#include <linux/netfilter/ipset/pfxlen.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_hash.h>

#define IPSET_TYPE_REV_MIN	0
/*				1    nomatch flag support added */
/*				2    /0 support added */
/*				3    Counters support added */
/*				4    Comments support added */
/*				5    Forceadd support added */
#define IPSET_TYPE_REV_MAX	6 /* skbinfo support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
IP_SET_MODULE_DESC("hash:net,iface", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_hash:net,iface");

/* Type specific function prefix */
#define HTYPE		hash_netiface
#define IP_SET_HASH_WITH_NETS
#define IP_SET_HASH_WITH_MULTI
#define IP_SET_HASH_WITH_NET0

#define STRLCPY(a, b)	strlcpy(a, b, IFNAMSIZ)

/* IPv4 variant */

struct hash_netiface4_elem_hashed {
	__be32 ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
};

/* Member elements */
struct hash_netiface4_elem {
	__be32 ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
	char iface[IFNAMSIZ];
};

/* Common functions */

static inline bool
hash_netiface4_data_equal(const struct hash_netiface4_elem *ip1,
			  const struct hash_netiface4_elem *ip2,
			  u32 *multi)
{
	return ip1->ip == ip2->ip &&
	       ip1->cidr == ip2->cidr &&
	       (++*multi) &&
	       ip1->physdev == ip2->physdev &&
	       strcmp(ip1->iface, ip2->iface) == 0;
}

static inline int
hash_netiface4_do_data_match(const struct hash_netiface4_elem *elem)
{
	return elem->nomatch ? -ENOTEMPTY : 1;
}

static inline void
hash_netiface4_data_set_flags(struct hash_netiface4_elem *elem, u32 flags)
{
	elem->nomatch = (flags >> 16) & IPSET_FLAG_NOMATCH;
}

static inline void
hash_netiface4_data_reset_flags(struct hash_netiface4_elem *elem, u8 *flags)
{
	swap(*flags, elem->nomatch);
}

static inline void
hash_netiface4_data_netmask(struct hash_netiface4_elem *elem, u8 cidr)
{
	elem->ip &= ip_set_netmask(cidr);
	elem->cidr = cidr;
}

static bool
hash_netiface4_data_list(struct sk_buff *skb,
			 const struct hash_netiface4_elem *data)
{
	u32 flags = data->physdev ? IPSET_FLAG_PHYSDEV : 0;

	if (data->nomatch)
		flags |= IPSET_FLAG_NOMATCH;
	if (nla_put_ipaddr4(skb, IPSET_ATTR_IP, data->ip) ||
	    nla_put_u8(skb, IPSET_ATTR_CIDR, data->cidr) ||
	    nla_put_string(skb, IPSET_ATTR_IFACE, data->iface) ||
	    (flags &&
	     nla_put_net32(skb, IPSET_ATTR_CADT_FLAGS, htonl(flags))))
		goto nla_put_failure;
	return false;

nla_put_failure:
	return true;
}

static inline void
hash_netiface4_data_next(struct hash_netiface4_elem *next,
			 const struct hash_netiface4_elem *d)
{
	next->ip = d->ip;
}

#define MTYPE		hash_netiface4
#define HOST_MASK	32
#define HKEY_DATALEN	sizeof(struct hash_netiface4_elem_hashed)
#include "ip_set_hash_gen.h"

#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
static const char *get_physindev_name(const struct sk_buff *skb)
{
	struct net_device *dev = nf_bridge_get_physindev(skb);

	return dev ? dev->name : NULL;
}

static const char *get_physoutdev_name(const struct sk_buff *skb)
{
	struct net_device *dev = nf_bridge_get_physoutdev(skb);

	return dev ? dev->name : NULL;
}
#endif

static int
hash_netiface4_kadt(struct ip_set *set, const struct sk_buff *skb,
		    const struct xt_action_param *par,
		    enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	struct hash_netiface4 *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netiface4_elem e = {
		.cidr = INIT_CIDR(h->nets[0].cidr[0], HOST_MASK),
		.elem = 1,
	};
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);

	if (adt == IPSET_TEST)
		e.cidr = HOST_MASK;

	ip4addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &e.ip);
	e.ip &= ip_set_netmask(e.cidr);

#define IFACE(dir)	(par->state->dir ? par->state->dir->name : "")
#define SRCDIR		(opt->flags & IPSET_DIM_TWO_SRC)

	if (opt->cmdflags & IPSET_FLAG_PHYSDEV) {
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
		const char *eiface = SRCDIR ? get_physindev_name(skb) :
					      get_physoutdev_name(skb);

		if (!eiface)
			return -EINVAL;
		STRLCPY(e.iface, eiface);
		e.physdev = 1;
#endif
	} else {
		STRLCPY(e.iface, SRCDIR ? IFACE(in) : IFACE(out));
	}

	if (strlen(e.iface) == 0)
		return -EINVAL;
	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
hash_netiface4_uadt(struct ip_set *set, struct nlattr *tb[],
		    enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	struct hash_netiface4 *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netiface4_elem e = { .cidr = HOST_MASK, .elem = 1 };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	u32 ip = 0, ip_to = 0;
	int ret;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !tb[IPSET_ATTR_IFACE] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;

	ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP], &ip);
	if (ret)
		return ret;

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	if (tb[IPSET_ATTR_CIDR]) {
		e.cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);
		if (e.cidr > HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
	}
	nla_strlcpy(e.iface, tb[IPSET_ATTR_IFACE], IFNAMSIZ);

	if (tb[IPSET_ATTR_CADT_FLAGS]) {
		u32 cadt_flags = ip_set_get_h32(tb[IPSET_ATTR_CADT_FLAGS]);

		if (cadt_flags & IPSET_FLAG_PHYSDEV)
			e.physdev = 1;
		if (cadt_flags & IPSET_FLAG_NOMATCH)
			flags |= (IPSET_FLAG_NOMATCH << 16);
	}
	if (adt == IPSET_TEST || !tb[IPSET_ATTR_IP_TO]) {
		e.ip = htonl(ip & ip_set_hostmask(e.cidr));
		ret = adtfn(set, &e, &ext, &ext, flags);
		return ip_set_enomatch(ret, flags, adt, set) ? -ret :
		       ip_set_eexist(ret, flags) ? 0 : ret;
	}

	if (tb[IPSET_ATTR_IP_TO]) {
		ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP_TO], &ip_to);
		if (ret)
			return ret;
		if (ip_to < ip)
			swap(ip, ip_to);
		if (ip + UINT_MAX == ip_to)
			return -IPSET_ERR_HASH_RANGE;
	} else {
		ip_set_mask_from_to(ip, ip_to, e.cidr);
	}

	if (retried)
		ip = ntohl(h->next.ip);
	do {
		e.ip = htonl(ip);
		ip = ip_set_range_to_cidr(ip, ip_to, &e.cidr);
		ret = adtfn(set, &e, &ext, &ext, flags);

		if (ret && !ip_set_eexist(ret, flags))
			return ret;

		ret = 0;
	} while (ip++ < ip_to);
	return ret;
}

/* IPv6 variant */

struct hash_netiface6_elem_hashed {
	union nf_inet_addr ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
};

struct hash_netiface6_elem {
	union nf_inet_addr ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
	char iface[IFNAMSIZ];
};

/* Common functions */

static inline bool
hash_netiface6_data_equal(const struct hash_netiface6_elem *ip1,
			  const struct hash_netiface6_elem *ip2,
			  u32 *multi)
{
	return ipv6_addr_equal(&ip1->ip.in6, &ip2->ip.in6) &&
	       ip1->cidr == ip2->cidr &&
	       (++*multi) &&
	       ip1->physdev == ip2->physdev &&
	       strcmp(ip1->iface, ip2->iface) == 0;
}

static inline int
hash_netiface6_do_data_match(const struct hash_netiface6_elem *elem)
{
	return elem->nomatch ? -ENOTEMPTY : 1;
}

static inline void
hash_netiface6_data_set_flags(struct hash_netiface6_elem *elem, u32 flags)
{
	elem->nomatch = (flags >> 16) & IPSET_FLAG_NOMATCH;
}

static inline void
hash_netiface6_data_reset_flags(struct hash_netiface6_elem *elem, u8 *flags)
{
	swap(*flags, elem->nomatch);
}

static inline void
hash_netiface6_data_netmask(struct hash_netiface6_elem *elem, u8 cidr)
{
	ip6_netmask(&elem->ip, cidr);
	elem->cidr = cidr;
}

static bool
hash_netiface6_data_list(struct sk_buff *skb,
			 const struct hash_netiface6_elem *data)
{
	u32 flags = data->physdev ? IPSET_FLAG_PHYSDEV : 0;

	if (data->nomatch)
		flags |= IPSET_FLAG_NOMATCH;
	if (nla_put_ipaddr6(skb, IPSET_ATTR_IP, &data->ip.in6) ||
	    nla_put_u8(skb, IPSET_ATTR_CIDR, data->cidr) ||
	    nla_put_string(skb, IPSET_ATTR_IFACE, data->iface) ||
	    (flags &&
	     nla_put_net32(skb, IPSET_ATTR_CADT_FLAGS, htonl(flags))))
		goto nla_put_failure;
	return false;

nla_put_failure:
	return true;
}

static inline void
hash_netiface6_data_next(struct hash_netiface6_elem *next,
			 const struct hash_netiface6_elem *d)
{
}

#undef MTYPE
#undef HOST_MASK

#define MTYPE		hash_netiface6
#define HOST_MASK	128
#define HKEY_DATALEN	sizeof(struct hash_netiface6_elem_hashed)
#define IP_SET_EMIT_CREATE
#include "ip_set_hash_gen.h"

static int
hash_netiface6_kadt(struct ip_set *set, const struct sk_buff *skb,
		    const struct xt_action_param *par,
		    enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	struct hash_netiface6 *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netiface6_elem e = {
		.cidr = INIT_CIDR(h->nets[0].cidr[0], HOST_MASK),
		.elem = 1,
	};
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);

	if (adt == IPSET_TEST)
		e.cidr = HOST_MASK;

	ip6addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &e.ip.in6);
	ip6_netmask(&e.ip, e.cidr);

	if (opt->cmdflags & IPSET_FLAG_PHYSDEV) {
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
		const char *eiface = SRCDIR ? get_physindev_name(skb) :
					      get_physoutdev_name(skb);

		if (!eiface)
			return -EINVAL;
		STRLCPY(e.iface, eiface);
		e.physdev = 1;
#endif
	} else {
		STRLCPY(e.iface, SRCDIR ? IFACE(in) : IFACE(out));
	}

	if (strlen(e.iface) == 0)
		return -EINVAL;

	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
hash_netiface6_uadt(struct ip_set *set, struct nlattr *tb[],
		    enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netiface6_elem e = { .cidr = HOST_MASK, .elem = 1 };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	int ret;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !tb[IPSET_ATTR_IFACE] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;
	if (unlikely(tb[IPSET_ATTR_IP_TO]))
		return -IPSET_ERR_HASH_RANGE_UNSUPPORTED;

	ret = ip_set_get_ipaddr6(tb[IPSET_ATTR_IP], &e.ip);
	if (ret)
		return ret;

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	if (tb[IPSET_ATTR_CIDR]) {
		e.cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);
		if (e.cidr > HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
	}

	ip6_netmask(&e.ip, e.cidr);

	nla_strlcpy(e.iface, tb[IPSET_ATTR_IFACE], IFNAMSIZ);

	if (tb[IPSET_ATTR_CADT_FLAGS]) {
		u32 cadt_flags = ip_set_get_h32(tb[IPSET_ATTR_CADT_FLAGS]);

		if (cadt_flags & IPSET_FLAG_PHYSDEV)
			e.physdev = 1;
		if (cadt_flags & IPSET_FLAG_NOMATCH)
			flags |= (IPSET_FLAG_NOMATCH << 16);
	}

	ret = adtfn(set, &e, &ext, &ext, flags);

	return ip_set_enomatch(ret, flags, adt, set) ? -ret :
	       ip_set_eexist(ret, flags) ? 0 : ret;
}

static struct ip_set_type hash_netiface_type __read_mostly = {
	.name		= "hash:net,iface",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_IP | IPSET_TYPE_IFACE |
			  IPSET_TYPE_NOMATCH,
	.dimension	= IPSET_DIM_TWO,
	.family		= NFPROTO_UNSPEC,
	.revision_min	= IPSET_TYPE_REV_MIN,
	.revision_max	= IPSET_TYPE_REV_MAX,
	.create		= hash_netiface_create,
	.create_policy	= {
		[IPSET_ATTR_HASHSIZE]	= { .type = NLA_U32 },
		[IPSET_ATTR_MAXELEM]	= { .type = NLA_U32 },
		[IPSET_ATTR_PROBES]	= { .type = NLA_U8 },
		[IPSET_ATTR_RESIZE]	= { .type = NLA_U8  },
		[IPSET_ATTR_PROTO]	= { .type = NLA_U8 },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
	},
	.adt_policy	= {
		[IPSET_ATTR_IP]		= { .type = NLA_NESTED },
		[IPSET_ATTR_IP_TO]	= { .type = NLA_NESTED },
		[IPSET_ATTR_IFACE]	= { .type = NLA_NUL_STRING,
					    .len  = IFNAMSIZ - 1 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
		[IPSET_ATTR_CIDR]	= { .type = NLA_U8 },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_LINENO]	= { .type = NLA_U32 },
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
hash_netiface_init(void)
{
	return ip_set_type_register(&hash_netiface_type);
}

static void __exit
hash_netiface_fini(void)
{
	rcu_barrier();
	ip_set_type_unregister(&hash_netiface_type);
}

module_init(hash_netiface_init);
module_exit(hash_netiface_fini);
