/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 * Copyright (C) 2003-2013 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Kernel module implementing an IP set type: the bitmap:ip type */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/netlink.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <net/netlink.h>
#include <net/tcp.h>

#include <linux/netfilter/ipset/pfxlen.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_bitmap.h>

#define IPSET_TYPE_REV_MIN	0
/*				1	   Counter support added */
#define IPSET_TYPE_REV_MAX	2	/* Comment support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
IP_SET_MODULE_DESC("bitmap:ip", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_bitmap:ip");

#define MTYPE		bitmap_ip

/* Type structure */
struct bitmap_ip {
	void *members;		/* the set members */
	void *extensions;	/* data extensions */
	u32 first_ip;		/* host byte order, included in range */
	u32 last_ip;		/* host byte order, included in range */
	u32 elements;		/* number of max elements in the set */
	u32 hosts;		/* number of hosts in a subnet */
	size_t memsize;		/* members size */
	u8 netmask;		/* subnet netmask */
	struct timer_list gc;	/* garbage collection */
};

/* ADT structure for generic function args */
struct bitmap_ip_adt_elem {
	u16 id;
};

static inline u32
ip_to_id(const struct bitmap_ip *m, u32 ip)
{
	return ((ip & ip_set_hostmask(m->netmask)) - m->first_ip)/m->hosts;
}

/* Common functions */

static inline int
bitmap_ip_do_test(const struct bitmap_ip_adt_elem *e,
		  struct bitmap_ip *map, size_t dsize)
{
	return !!test_bit(e->id, map->members);
}

static inline int
bitmap_ip_gc_test(u16 id, const struct bitmap_ip *map, size_t dsize)
{
	return !!test_bit(id, map->members);
}

static inline int
bitmap_ip_do_add(const struct bitmap_ip_adt_elem *e, struct bitmap_ip *map,
		 u32 flags, size_t dsize)
{
	return !!test_and_set_bit(e->id, map->members);
}

static inline int
bitmap_ip_do_del(const struct bitmap_ip_adt_elem *e, struct bitmap_ip *map)
{
	return !test_and_clear_bit(e->id, map->members);
}

static inline int
bitmap_ip_do_list(struct sk_buff *skb, const struct bitmap_ip *map, u32 id,
		  size_t dsize)
{
	return nla_put_ipaddr4(skb, IPSET_ATTR_IP,
			htonl(map->first_ip + id * map->hosts));
}

static inline int
bitmap_ip_do_head(struct sk_buff *skb, const struct bitmap_ip *map)
{
	return nla_put_ipaddr4(skb, IPSET_ATTR_IP, htonl(map->first_ip)) ||
	       nla_put_ipaddr4(skb, IPSET_ATTR_IP_TO, htonl(map->last_ip)) ||
	       (map->netmask != 32 &&
		nla_put_u8(skb, IPSET_ATTR_NETMASK, map->netmask));
}

static int
bitmap_ip_kadt(struct ip_set *set, const struct sk_buff *skb,
	       const struct xt_action_param *par,
	       enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	struct bitmap_ip *map = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct bitmap_ip_adt_elem e = { };
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);
	u32 ip;

	ip = ntohl(ip4addr(skb, opt->flags & IPSET_DIM_ONE_SRC));
	if (ip < map->first_ip || ip > map->last_ip)
		return -IPSET_ERR_BITMAP_RANGE;

	e.id = ip_to_id(map, ip);

	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
bitmap_ip_uadt(struct ip_set *set, struct nlattr *tb[],
	       enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	struct bitmap_ip *map = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	u32 ip = 0, ip_to = 0;
	struct bitmap_ip_adt_elem e = { };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	int ret = 0;

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PACKETS) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_BYTES)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP], &ip) ||
	      ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	if (ip < map->first_ip || ip > map->last_ip)
		return -IPSET_ERR_BITMAP_RANGE;

	if (adt == IPSET_TEST) {
		e.id = ip_to_id(map, ip);
		return adtfn(set, &e, &ext, &ext, flags);
	}

	if (tb[IPSET_ATTR_IP_TO]) {
		ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP_TO], &ip_to);
		if (ret)
			return ret;
		if (ip > ip_to) {
			swap(ip, ip_to);
			if (ip < map->first_ip)
				return -IPSET_ERR_BITMAP_RANGE;
		}
	} else if (tb[IPSET_ATTR_CIDR]) {
		u8 cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);

		if (!cidr || cidr > 32)
			return -IPSET_ERR_INVALID_CIDR;
		ip_set_mask_from_to(ip, ip_to, cidr);
	} else
		ip_to = ip;

	if (ip_to > map->last_ip)
		return -IPSET_ERR_BITMAP_RANGE;

	for (; !before(ip_to, ip); ip += map->hosts) {
		e.id = ip_to_id(map, ip);
		ret = adtfn(set, &e, &ext, &ext, flags);

		if (ret && !ip_set_eexist(ret, flags))
			return ret;
		else
			ret = 0;
	}
	return ret;
}

static bool
bitmap_ip_same_set(const struct ip_set *a, const struct ip_set *b)
{
	const struct bitmap_ip *x = a->data;
	const struct bitmap_ip *y = b->data;

	return x->first_ip == y->first_ip &&
	       x->last_ip == y->last_ip &&
	       x->netmask == y->netmask &&
	       a->timeout == b->timeout &&
	       a->extensions == b->extensions;
}

/* Plain variant */

struct bitmap_ip_elem {
};

#include "ip_set_bitmap_gen.h"

/* Create bitmap:ip type of sets */

static bool
init_map_ip(struct ip_set *set, struct bitmap_ip *map,
	    u32 first_ip, u32 last_ip,
	    u32 elements, u32 hosts, u8 netmask)
{
	map->members = ip_set_alloc(map->memsize);
	if (!map->members)
		return false;
	if (set->dsize) {
		map->extensions = ip_set_alloc(set->dsize * elements);
		if (!map->extensions) {
			kfree(map->members);
			return false;
		}
	}
	map->first_ip = first_ip;
	map->last_ip = last_ip;
	map->elements = elements;
	map->hosts = hosts;
	map->netmask = netmask;
	set->timeout = IPSET_NO_TIMEOUT;

	set->data = map;
	set->family = NFPROTO_IPV4;

	return true;
}

static int
bitmap_ip_create(struct ip_set *set, struct nlattr *tb[], u32 flags)
{
	struct bitmap_ip *map;
	u32 first_ip = 0, last_ip = 0, hosts;
	u64 elements;
	u8 netmask = 32;
	int ret;

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;

	ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP], &first_ip);
	if (ret)
		return ret;

	if (tb[IPSET_ATTR_IP_TO]) {
		ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP_TO], &last_ip);
		if (ret)
			return ret;
		if (first_ip > last_ip) {
			u32 tmp = first_ip;

			first_ip = last_ip;
			last_ip = tmp;
		}
	} else if (tb[IPSET_ATTR_CIDR]) {
		u8 cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);

		if (cidr >= 32)
			return -IPSET_ERR_INVALID_CIDR;
		ip_set_mask_from_to(first_ip, last_ip, cidr);
	} else
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_NETMASK]) {
		netmask = nla_get_u8(tb[IPSET_ATTR_NETMASK]);

		if (netmask > 32)
			return -IPSET_ERR_INVALID_NETMASK;

		first_ip &= ip_set_hostmask(netmask);
		last_ip |= ~ip_set_hostmask(netmask);
	}

	if (netmask == 32) {
		hosts = 1;
		elements = (u64)last_ip - first_ip + 1;
	} else {
		u8 mask_bits;
		u32 mask;

		mask = range_to_mask(first_ip, last_ip, &mask_bits);

		if ((!mask && (first_ip || last_ip != 0xFFFFFFFF)) ||
		    netmask <= mask_bits)
			return -IPSET_ERR_BITMAP_RANGE;

		pr_debug("mask_bits %u, netmask %u\n", mask_bits, netmask);
		hosts = 2 << (32 - netmask - 1);
		elements = 2 << (netmask - mask_bits - 1);
	}
	if (elements > IPSET_BITMAP_MAX_RANGE + 1)
		return -IPSET_ERR_BITMAP_RANGE_SIZE;

	pr_debug("hosts %u, elements %llu\n",
		 hosts, (unsigned long long)elements);

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->memsize = bitmap_bytes(0, elements - 1);
	set->variant = &bitmap_ip;
	set->dsize = ip_set_elem_len(set, tb, 0);
	if (!init_map_ip(set, map, first_ip, last_ip,
			 elements, hosts, netmask)) {
		kfree(map);
		return -ENOMEM;
	}
	if (tb[IPSET_ATTR_TIMEOUT]) {
		set->timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
		bitmap_ip_gc_init(set, bitmap_ip_gc);
	}
	return 0;
}

static struct ip_set_type bitmap_ip_type __read_mostly = {
	.name		= "bitmap:ip",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_IP,
	.dimension	= IPSET_DIM_ONE,
	.family		= NFPROTO_IPV4,
	.revision_min	= IPSET_TYPE_REV_MIN,
	.revision_max	= IPSET_TYPE_REV_MAX,
	.create		= bitmap_ip_create,
	.create_policy	= {
		[IPSET_ATTR_IP]		= { .type = NLA_NESTED },
		[IPSET_ATTR_IP_TO]	= { .type = NLA_NESTED },
		[IPSET_ATTR_CIDR]	= { .type = NLA_U8 },
		[IPSET_ATTR_NETMASK]	= { .type = NLA_U8  },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
	},
	.adt_policy	= {
		[IPSET_ATTR_IP]		= { .type = NLA_NESTED },
		[IPSET_ATTR_IP_TO]	= { .type = NLA_NESTED },
		[IPSET_ATTR_CIDR]	= { .type = NLA_U8 },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_LINENO]	= { .type = NLA_U32 },
		[IPSET_ATTR_BYTES]	= { .type = NLA_U64 },
		[IPSET_ATTR_PACKETS]	= { .type = NLA_U64 },
		[IPSET_ATTR_COMMENT]	= { .type = NLA_NUL_STRING },
	},
	.me		= THIS_MODULE,
};

static int __init
bitmap_ip_init(void)
{
	return ip_set_type_register(&bitmap_ip_type);
}

static void __exit
bitmap_ip_fini(void)
{
	ip_set_type_unregister(&bitmap_ip_type);
}

module_init(bitmap_ip_init);
module_exit(bitmap_ip_fini);
