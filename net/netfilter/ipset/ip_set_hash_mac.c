/* Copyright (C) 2014 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Kernel module implementing an IP set type: the hash:mac type */

#include <linux/jhash.h>
#include <linux/module.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/if_ether.h>
#include <net/netlink.h>

#include <linux/netfilter.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_hash.h>

#define IPSET_TYPE_REV_MIN	0
#define IPSET_TYPE_REV_MAX	0

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
IP_SET_MODULE_DESC("hash:mac", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_hash:mac");

/* Type specific function prefix */
#define HTYPE		hash_mac

/* Member elements */
struct hash_mac4_elem {
	/* Zero valued IP addresses cannot be stored */
	union {
		unsigned char ether[ETH_ALEN];
		__be32 foo[2];
	};
};

/* Common functions */

static inline bool
hash_mac4_data_equal(const struct hash_mac4_elem *e1,
		     const struct hash_mac4_elem *e2,
		     u32 *multi)
{
	return ether_addr_equal(e1->ether, e2->ether);
}

static inline bool
hash_mac4_data_list(struct sk_buff *skb, const struct hash_mac4_elem *e)
{
	return nla_put(skb, IPSET_ATTR_ETHER, ETH_ALEN, e->ether);
}

static inline void
hash_mac4_data_next(struct hash_mac4_elem *next,
		    const struct hash_mac4_elem *e)
{
}

#define MTYPE		hash_mac4
#define PF		4
#define HOST_MASK	32
#define IP_SET_EMIT_CREATE
#define IP_SET_PROTO_UNDEF
#include "ip_set_hash_gen.h"

/* Zero valued element is not supported */
static const unsigned char invalid_ether[ETH_ALEN] = { 0 };

static int
hash_mac4_kadt(struct ip_set *set, const struct sk_buff *skb,
	       const struct xt_action_param *par,
	       enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_mac4_elem e = { { .foo[0] = 0, .foo[1] = 0 } };
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);

	 /* MAC can be src only */
	if (!(opt->flags & IPSET_DIM_ONE_SRC))
		return 0;

	if (skb_mac_header(skb) < skb->head ||
	     (skb_mac_header(skb) + ETH_HLEN) > skb->data)
		return -EINVAL;

	memcpy(e.ether, eth_hdr(skb)->h_source, ETH_ALEN);
	if (memcmp(e.ether, invalid_ether, ETH_ALEN) == 0)
		return -EINVAL;
	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
hash_mac4_uadt(struct ip_set *set, struct nlattr *tb[],
	       enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_mac4_elem e = { { .foo[0] = 0, .foo[1] = 0 } };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	int ret;

	if (unlikely(!tb[IPSET_ATTR_ETHER] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PACKETS) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_BYTES)   ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBMARK) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBPRIO) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBQUEUE)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;
	memcpy(e.ether, nla_data(tb[IPSET_ATTR_ETHER]), ETH_ALEN);
	if (memcmp(e.ether, invalid_ether, ETH_ALEN) == 0)
		return -IPSET_ERR_HASH_ELEM;

	return adtfn(set, &e, &ext, &ext, flags);
}

static struct ip_set_type hash_mac_type __read_mostly = {
	.name		= "hash:mac",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_MAC,
	.dimension	= IPSET_DIM_ONE,
	.family		= NFPROTO_UNSPEC,
	.revision_min	= IPSET_TYPE_REV_MIN,
	.revision_max	= IPSET_TYPE_REV_MAX,
	.create		= hash_mac_create,
	.create_policy	= {
		[IPSET_ATTR_HASHSIZE]	= { .type = NLA_U32 },
		[IPSET_ATTR_MAXELEM]	= { .type = NLA_U32 },
		[IPSET_ATTR_PROBES]	= { .type = NLA_U8 },
		[IPSET_ATTR_RESIZE]	= { .type = NLA_U8  },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
	},
	.adt_policy	= {
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
hash_mac_init(void)
{
	return ip_set_type_register(&hash_mac_type);
}

static void __exit
hash_mac_fini(void)
{
	ip_set_type_unregister(&hash_mac_type);
}

module_init(hash_mac_init);
module_exit(hash_mac_fini);
