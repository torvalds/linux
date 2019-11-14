/* Copyright (C) 2016 Tomasz Chilinski <tomasz.chilinski@chilan.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Kernel module implementing an IP set type: the hash:ip,mac type */

#include <linux/jhash.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/if_ether.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/netlink.h>
#include <net/tcp.h>

#include <linux/netfilter.h>
#include <linux/netfilter/ipset/pfxlen.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_hash.h>

#define IPSET_TYPE_REV_MIN	0
#define IPSET_TYPE_REV_MAX	0

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tomasz Chilinski <tomasz.chilinski@chilan.com>");
IP_SET_MODULE_DESC("hash:ip,mac", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_hash:ip,mac");

/* Type specific function prefix */
#define HTYPE		hash_ipmac

/* IPv4 variant */

/* Member elements */
struct hash_ipmac4_elem {
	/* Zero valued IP addresses cannot be stored */
	__be32 ip;
	union {
		unsigned char ether[ETH_ALEN];
		__be32 foo[2];
	};
};

/* Common functions */

static inline bool
hash_ipmac4_data_equal(const struct hash_ipmac4_elem *e1,
		       const struct hash_ipmac4_elem *e2,
		       u32 *multi)
{
	return e1->ip == e2->ip && ether_addr_equal(e1->ether, e2->ether);
}

static bool
hash_ipmac4_data_list(struct sk_buff *skb, const struct hash_ipmac4_elem *e)
{
	if (nla_put_ipaddr4(skb, IPSET_ATTR_IP, e->ip) ||
	    nla_put(skb, IPSET_ATTR_ETHER, ETH_ALEN, e->ether))
		goto nla_put_failure;
	return false;

nla_put_failure:
	return true;
}

static inline void
hash_ipmac4_data_next(struct hash_ipmac4_elem *next,
		      const struct hash_ipmac4_elem *e)
{
	next->ip = e->ip;
}

#define MTYPE		hash_ipmac4
#define PF		4
#define HOST_MASK	32
#define HKEY_DATALEN	sizeof(struct hash_ipmac4_elem)
#include "ip_set_hash_gen.h"

static int
hash_ipmac4_kadt(struct ip_set *set, const struct sk_buff *skb,
		 const struct xt_action_param *par,
		 enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_ipmac4_elem e = { .ip = 0, { .foo[0] = 0, .foo[1] = 0 } };
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);

	if (skb_mac_header(skb) < skb->head ||
	    (skb_mac_header(skb) + ETH_HLEN) > skb->data)
		return -EINVAL;

	if (opt->flags & IPSET_DIM_TWO_SRC)
		ether_addr_copy(e.ether, eth_hdr(skb)->h_source);
	else
		ether_addr_copy(e.ether, eth_hdr(skb)->h_dest);

	if (is_zero_ether_addr(e.ether))
		return -EINVAL;

	ip4addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &e.ip);

	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
hash_ipmac4_uadt(struct ip_set *set, struct nlattr *tb[],
		 enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_ipmac4_elem e = { .ip = 0, { .foo[0] = 0, .foo[1] = 0 } };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	int ret;

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !tb[IPSET_ATTR_ETHER] ||
		     nla_len(tb[IPSET_ATTR_ETHER]) != ETH_ALEN ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PACKETS) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_BYTES)   ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBMARK) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBPRIO) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBQUEUE)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	ret = ip_set_get_ipaddr4(tb[IPSET_ATTR_IP], &e.ip) ||
		ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;
	memcpy(e.ether, nla_data(tb[IPSET_ATTR_ETHER]), ETH_ALEN);
	if (is_zero_ether_addr(e.ether))
		return -IPSET_ERR_HASH_ELEM;

	return adtfn(set, &e, &ext, &ext, flags);
}

/* IPv6 variant */

/* Member elements */
struct hash_ipmac6_elem {
	/* Zero valued IP addresses cannot be stored */
	union nf_inet_addr ip;
	union {
		unsigned char ether[ETH_ALEN];
		__be32 foo[2];
	};
};

/* Common functions */

static inline bool
hash_ipmac6_data_equal(const struct hash_ipmac6_elem *e1,
		       const struct hash_ipmac6_elem *e2,
		       u32 *multi)
{
	return ipv6_addr_equal(&e1->ip.in6, &e2->ip.in6) &&
		ether_addr_equal(e1->ether, e2->ether);
}

static bool
hash_ipmac6_data_list(struct sk_buff *skb, const struct hash_ipmac6_elem *e)
{
	if (nla_put_ipaddr6(skb, IPSET_ATTR_IP, &e->ip.in6) ||
	    nla_put(skb, IPSET_ATTR_ETHER, ETH_ALEN, e->ether))
		goto nla_put_failure;
	return false;

nla_put_failure:
	return true;
}

static inline void
hash_ipmac6_data_next(struct hash_ipmac6_elem *next,
		      const struct hash_ipmac6_elem *e)
{
}

#undef MTYPE
#undef PF
#undef HOST_MASK
#undef HKEY_DATALEN

#define MTYPE		hash_ipmac6
#define PF		6
#define HOST_MASK	128
#define HKEY_DATALEN	sizeof(struct hash_ipmac6_elem)
#define IP_SET_EMIT_CREATE
#include "ip_set_hash_gen.h"

static int
hash_ipmac6_kadt(struct ip_set *set, const struct sk_buff *skb,
		 const struct xt_action_param *par,
		 enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_ipmac6_elem e = {
		{ .all = { 0 } },
		{ .foo[0] = 0, .foo[1] = 0 }
	};
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);

	if (skb_mac_header(skb) < skb->head ||
	    (skb_mac_header(skb) + ETH_HLEN) > skb->data)
		return -EINVAL;

	if (opt->flags & IPSET_DIM_TWO_SRC)
		ether_addr_copy(e.ether, eth_hdr(skb)->h_source);
	else
		ether_addr_copy(e.ether, eth_hdr(skb)->h_dest);

	if (is_zero_ether_addr(e.ether))
		return -EINVAL;

	ip6addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &e.ip.in6);

	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
hash_ipmac6_uadt(struct ip_set *set, struct nlattr *tb[],
		 enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_ipmac6_elem e = {
		{ .all = { 0 } },
		{ .foo[0] = 0, .foo[1] = 0 }
	};
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	int ret;

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !tb[IPSET_ATTR_ETHER] ||
		     nla_len(tb[IPSET_ATTR_ETHER]) != ETH_ALEN ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PACKETS) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_BYTES)   ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBMARK) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBPRIO) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBQUEUE)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	ret = ip_set_get_ipaddr6(tb[IPSET_ATTR_IP], &e.ip) ||
		ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	memcpy(e.ether, nla_data(tb[IPSET_ATTR_ETHER]), ETH_ALEN);
	if (is_zero_ether_addr(e.ether))
		return -IPSET_ERR_HASH_ELEM;

	return adtfn(set, &e, &ext, &ext, flags);
}

static struct ip_set_type hash_ipmac_type __read_mostly = {
	.name		= "hash:ip,mac",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_IP | IPSET_TYPE_MAC,
	.dimension	= IPSET_DIM_TWO,
	.family		= NFPROTO_UNSPEC,
	.revision_min	= IPSET_TYPE_REV_MIN,
	.revision_max	= IPSET_TYPE_REV_MAX,
	.create		= hash_ipmac_create,
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
		[IPSET_ATTR_ETHER]	= { .type = NLA_BINARY,
				.len  = ETH_ALEN },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_LINENO]	= { .type = NLA_U32 },
		[IPSET_ATTR_BYTES]	= { .type = NLA_U64 },
		[IPSET_ATTR_PACKETS]	= { .type = NLA_U64 },
		[IPSET_ATTR_COMMENT]	= { .type = NLA_NUL_STRING },
		[IPSET_ATTR_SKBMARK]	= { .type = NLA_U64 },
		[IPSET_ATTR_SKBPRIO]	= { .type = NLA_U32 },
		[IPSET_ATTR_SKBQUEUE]	= { .type = NLA_U16 },
	},
	.me		= THIS_MODULE,
};

static int __init
hash_ipmac_init(void)
{
	return ip_set_type_register(&hash_ipmac_type);
}

static void __exit
hash_ipmac_fini(void)
{
	ip_set_type_unregister(&hash_ipmac_type);
}

module_init(hash_ipmac_init);
module_exit(hash_ipmac_fini);
