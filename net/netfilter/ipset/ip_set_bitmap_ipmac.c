// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 *			   Martin Josefsson <gandalf@wlug.westbo.se>
 */

/* Kernel module implementing an IP set type: the bitmap:ip,mac type */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/if_ether.h>
#include <linux/netlink.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <net/netlink.h>

#include <linux/netfilter/ipset/pfxlen.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_bitmap.h>

#define IPSET_TYPE_REV_MIN	0
/*				1	   Counter support added */
/*				2	   Comment support added */
#define IPSET_TYPE_REV_MAX	3	/* skbinfo support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@netfilter.org>");
IP_SET_MODULE_DESC("bitmap:ip,mac", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_bitmap:ip,mac");

#define MTYPE		bitmap_ipmac
#define HOST_MASK	32
#define IP_SET_BITMAP_STORED_TIMEOUT

enum {
	MAC_UNSET,		/* element is set, without MAC */
	MAC_FILLED,		/* element is set with MAC */
};

/* Type structure */
struct bitmap_ipmac {
	void *members;		/* the set members */
	u32 first_ip;		/* host byte order, included in range */
	u32 last_ip;		/* host byte order, included in range */
	u32 elements;		/* number of max elements in the set */
	size_t memsize;		/* members size */
	struct timer_list gc;	/* garbage collector */
	struct ip_set *set;	/* attached to this ip_set */
	unsigned char extensions[0]	/* MAC + data extensions */
		__aligned(__alignof__(u64));
};

/* ADT structure for generic function args */
struct bitmap_ipmac_adt_elem {
	unsigned char ether[ETH_ALEN] __aligned(2);
	u16 id;
	u16 add_mac;
};

struct bitmap_ipmac_elem {
	unsigned char ether[ETH_ALEN];
	unsigned char filled;
} __aligned(__alignof__(u64));

static u32
ip_to_id(const struct bitmap_ipmac *m, u32 ip)
{
	return ip - m->first_ip;
}

#define get_elem(extensions, id, dsize)		\
	(struct bitmap_ipmac_elem *)(extensions + (id) * (dsize))

#define get_const_elem(extensions, id, dsize)	\
	(const struct bitmap_ipmac_elem *)(extensions + (id) * (dsize))

/* Common functions */

static int
bitmap_ipmac_do_test(const struct bitmap_ipmac_adt_elem *e,
		     const struct bitmap_ipmac *map, size_t dsize)
{
	const struct bitmap_ipmac_elem *elem;

	if (!test_bit(e->id, map->members))
		return 0;
	elem = get_const_elem(map->extensions, e->id, dsize);
	if (e->add_mac && elem->filled == MAC_FILLED)
		return ether_addr_equal(e->ether, elem->ether);
	/* Trigger kernel to fill out the ethernet address */
	return -EAGAIN;
}

static int
bitmap_ipmac_gc_test(u16 id, const struct bitmap_ipmac *map, size_t dsize)
{
	const struct bitmap_ipmac_elem *elem;

	if (!test_bit(id, map->members))
		return 0;
	elem = get_const_elem(map->extensions, id, dsize);
	/* Timer not started for the incomplete elements */
	return elem->filled == MAC_FILLED;
}

static int
bitmap_ipmac_is_filled(const struct bitmap_ipmac_elem *elem)
{
	return elem->filled == MAC_FILLED;
}

static int
bitmap_ipmac_add_timeout(unsigned long *timeout,
			 const struct bitmap_ipmac_adt_elem *e,
			 const struct ip_set_ext *ext, struct ip_set *set,
			 struct bitmap_ipmac *map, int mode)
{
	u32 t = ext->timeout;

	if (mode == IPSET_ADD_START_STORED_TIMEOUT) {
		if (t == set->timeout)
			/* Timeout was not specified, get stored one */
			t = *timeout;
		ip_set_timeout_set(timeout, t);
	} else {
		/* If MAC is unset yet, we store plain timeout value
		 * because the timer is not activated yet
		 * and we can reuse it later when MAC is filled out,
		 * possibly by the kernel
		 */
		if (e->add_mac)
			ip_set_timeout_set(timeout, t);
		else
			*timeout = t;
	}
	return 0;
}

static int
bitmap_ipmac_do_add(const struct bitmap_ipmac_adt_elem *e,
		    struct bitmap_ipmac *map, u32 flags, size_t dsize)
{
	struct bitmap_ipmac_elem *elem;

	elem = get_elem(map->extensions, e->id, dsize);
	if (test_bit(e->id, map->members)) {
		if (elem->filled == MAC_FILLED) {
			if (e->add_mac &&
			    (flags & IPSET_FLAG_EXIST) &&
			    !ether_addr_equal(e->ether, elem->ether)) {
				/* memcpy isn't atomic */
				clear_bit(e->id, map->members);
				smp_mb__after_atomic();
				ether_addr_copy(elem->ether, e->ether);
			}
			return IPSET_ADD_FAILED;
		} else if (!e->add_mac)
			/* Already added without ethernet address */
			return IPSET_ADD_FAILED;
		/* Fill the MAC address and trigger the timer activation */
		clear_bit(e->id, map->members);
		smp_mb__after_atomic();
		ether_addr_copy(elem->ether, e->ether);
		elem->filled = MAC_FILLED;
		return IPSET_ADD_START_STORED_TIMEOUT;
	} else if (e->add_mac) {
		/* We can store MAC too */
		ether_addr_copy(elem->ether, e->ether);
		elem->filled = MAC_FILLED;
		return 0;
	}
	elem->filled = MAC_UNSET;
	/* MAC is not stored yet, don't start timer */
	return IPSET_ADD_STORE_PLAIN_TIMEOUT;
}

static int
bitmap_ipmac_do_del(const struct bitmap_ipmac_adt_elem *e,
		    struct bitmap_ipmac *map)
{
	return !test_and_clear_bit(e->id, map->members);
}

static int
bitmap_ipmac_do_list(struct sk_buff *skb, const struct bitmap_ipmac *map,
		     u32 id, size_t dsize)
{
	const struct bitmap_ipmac_elem *elem =
		get_const_elem(map->extensions, id, dsize);

	return nla_put_ipaddr4(skb, IPSET_ATTR_IP,
			       htonl(map->first_ip + id)) ||
	       (elem->filled == MAC_FILLED &&
		nla_put(skb, IPSET_ATTR_ETHER, ETH_ALEN, elem->ether));
}

static int
bitmap_ipmac_do_head(struct sk_buff *skb, const struct bitmap_ipmac *map)
{
	return nla_put_ipaddr4(skb, IPSET_ATTR_IP, htonl(map->first_ip)) ||
	       nla_put_ipaddr4(skb, IPSET_ATTR_IP_TO, htonl(map->last_ip));
}

static int
bitmap_ipmac_kadt(struct ip_set *set, const struct sk_buff *skb,
		  const struct xt_action_param *par,
		  enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	struct bitmap_ipmac *map = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct bitmap_ipmac_adt_elem e = { .id = 0, .add_mac = 1 };
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);
	u32 ip;

	ip = ntohl(ip4addr(skb, opt->flags & IPSET_DIM_ONE_SRC));
	if (ip < map->first_ip || ip > map->last_ip)
		return -IPSET_ERR_BITMAP_RANGE;

	/* Backward compatibility: we don't check the second flag */
	if (skb_mac_header(skb) < skb->head ||
	    (skb_mac_header(skb) + ETH_HLEN) > skb->data)
		return -EINVAL;

	e.id = ip_to_id(map, ip);

	if (opt->flags & IPSET_DIM_TWO_SRC)
		ether_addr_copy(e.ether, eth_hdr(skb)->h_source);
	else
		ether_addr_copy(e.ether, eth_hdr(skb)->h_dest);

	if (is_zero_ether_addr(e.ether))
		return -EINVAL;

	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
bitmap_ipmac_uadt(struct ip_set *set, struct nlattr *tb[],
		  enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	const struct bitmap_ipmac *map = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct bitmap_ipmac_adt_elem e = { .id = 0 };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	u32 ip = 0;
	int ret = 0;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	if (unlikely(!tb[IPSET_ATTR_IP]))
		return -IPSET_ERR_PROTOCOL;

	ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP], &ip);
	if (ret)
		return ret;

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	if (ip < map->first_ip || ip > map->last_ip)
		return -IPSET_ERR_BITMAP_RANGE;

	e.id = ip_to_id(map, ip);
	if (tb[IPSET_ATTR_ETHER]) {
		if (nla_len(tb[IPSET_ATTR_ETHER]) != ETH_ALEN)
			return -IPSET_ERR_PROTOCOL;
		memcpy(e.ether, nla_data(tb[IPSET_ATTR_ETHER]), ETH_ALEN);
		e.add_mac = 1;
	}
	ret = adtfn(set, &e, &ext, &ext, flags);

	return ip_set_eexist(ret, flags) ? 0 : ret;
}

static bool
bitmap_ipmac_same_set(const struct ip_set *a, const struct ip_set *b)
{
	const struct bitmap_ipmac *x = a->data;
	const struct bitmap_ipmac *y = b->data;

	return x->first_ip == y->first_ip &&
	       x->last_ip == y->last_ip &&
	       a->timeout == b->timeout &&
	       a->extensions == b->extensions;
}

/* Plain variant */

#include "ip_set_bitmap_gen.h"

/* Create bitmap:ip,mac type of sets */

static bool
init_map_ipmac(struct ip_set *set, struct bitmap_ipmac *map,
	       u32 first_ip, u32 last_ip, u32 elements)
{
	map->members = ip_set_alloc(map->memsize);
	if (!map->members)
		return false;
	map->first_ip = first_ip;
	map->last_ip = last_ip;
	map->elements = elements;
	set->timeout = IPSET_NO_TIMEOUT;

	map->set = set;
	set->data = map;
	set->family = NFPROTO_IPV4;

	return true;
}

static int
bitmap_ipmac_create(struct net *net, struct ip_set *set, struct nlattr *tb[],
		    u32 flags)
{
	u32 first_ip = 0, last_ip = 0;
	u64 elements;
	struct bitmap_ipmac *map;
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
		if (first_ip > last_ip)
			swap(first_ip, last_ip);
	} else if (tb[IPSET_ATTR_CIDR]) {
		u8 cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);

		if (cidr >= HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
		ip_set_mask_from_to(first_ip, last_ip, cidr);
	} else {
		return -IPSET_ERR_PROTOCOL;
	}

	elements = (u64)last_ip - first_ip + 1;

	if (elements > IPSET_BITMAP_MAX_RANGE + 1)
		return -IPSET_ERR_BITMAP_RANGE_SIZE;

	set->dsize = ip_set_elem_len(set, tb,
				     sizeof(struct bitmap_ipmac_elem),
				     __alignof__(struct bitmap_ipmac_elem));
	map = ip_set_alloc(sizeof(*map) + elements * set->dsize);
	if (!map)
		return -ENOMEM;

	map->memsize = bitmap_bytes(0, elements - 1);
	set->variant = &bitmap_ipmac;
	if (!init_map_ipmac(set, map, first_ip, last_ip, elements)) {
		kfree(map);
		return -ENOMEM;
	}
	if (tb[IPSET_ATTR_TIMEOUT]) {
		set->timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
		bitmap_ipmac_gc_init(set, bitmap_ipmac_gc);
	}
	return 0;
}

static struct ip_set_type bitmap_ipmac_type = {
	.name		= "bitmap:ip,mac",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_IP | IPSET_TYPE_MAC,
	.dimension	= IPSET_DIM_TWO,
	.family		= NFPROTO_IPV4,
	.revision_min	= IPSET_TYPE_REV_MIN,
	.revision_max	= IPSET_TYPE_REV_MAX,
	.create		= bitmap_ipmac_create,
	.create_policy	= {
		[IPSET_ATTR_IP]		= { .type = NLA_NESTED },
		[IPSET_ATTR_IP_TO]	= { .type = NLA_NESTED },
		[IPSET_ATTR_CIDR]	= { .type = NLA_U8 },
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
		[IPSET_ATTR_COMMENT]	= { .type = NLA_NUL_STRING,
					    .len  = IPSET_MAX_COMMENT_SIZE },
		[IPSET_ATTR_SKBMARK]	= { .type = NLA_U64 },
		[IPSET_ATTR_SKBPRIO]	= { .type = NLA_U32 },
		[IPSET_ATTR_SKBQUEUE]	= { .type = NLA_U16 },
	},
	.me		= THIS_MODULE,
};

static int __init
bitmap_ipmac_init(void)
{
	return ip_set_type_register(&bitmap_ipmac_type);
}

static void __exit
bitmap_ipmac_fini(void)
{
	rcu_barrier();
	ip_set_type_unregister(&bitmap_ipmac_type);
}

module_init(bitmap_ipmac_init);
module_exit(bitmap_ipmac_fini);
