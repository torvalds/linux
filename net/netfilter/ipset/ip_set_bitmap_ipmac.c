/* Copyright (C) 2000-2002 Joakim Axelsson <gozem@linux.nu>
 *                         Patrick Schaaf <bof@bof.de>
 *			   Martin Josefsson <gandalf@wlug.westbo.se>
 * Copyright (C) 2003-2013 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#define IPSET_TYPE_REV_MAX	1	/* Counter support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
IP_SET_MODULE_DESC("bitmap:ip,mac", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_bitmap:ip,mac");

#define MTYPE		bitmap_ipmac
#define IP_SET_BITMAP_STORED_TIMEOUT

enum {
	MAC_UNSET,		/* element is set, without MAC */
	MAC_FILLED,		/* element is set with MAC */
};

/* Type structure */
struct bitmap_ipmac {
	void *members;		/* the set members */
	void *extensions;	/* MAC + data extensions */
	u32 first_ip;		/* host byte order, included in range */
	u32 last_ip;		/* host byte order, included in range */
	u32 elements;		/* number of max elements in the set */
	size_t memsize;		/* members size */
	struct timer_list gc;	/* garbage collector */
};

/* ADT structure for generic function args */
struct bitmap_ipmac_adt_elem {
	u16 id;
	unsigned char *ether;
};

struct bitmap_ipmac_elem {
	unsigned char ether[ETH_ALEN];
	unsigned char filled;
} __attribute__ ((aligned));

static inline u32
ip_to_id(const struct bitmap_ipmac *m, u32 ip)
{
	return ip - m->first_ip;
}

static inline struct bitmap_ipmac_elem *
get_elem(void *extensions, u16 id, size_t dsize)
{
	return (struct bitmap_ipmac_elem *)(extensions + id * dsize);
}

/* Common functions */

static inline int
bitmap_ipmac_do_test(const struct bitmap_ipmac_adt_elem *e,
		     const struct bitmap_ipmac *map, size_t dsize)
{
	const struct bitmap_ipmac_elem *elem;

	if (!test_bit(e->id, map->members))
		return 0;
	elem = get_elem(map->extensions, e->id, dsize);
	if (elem->filled == MAC_FILLED)
		return e->ether == NULL ||
		       ether_addr_equal(e->ether, elem->ether);
	/* Trigger kernel to fill out the ethernet address */
	return -EAGAIN;
}

static inline int
bitmap_ipmac_gc_test(u16 id, const struct bitmap_ipmac *map, size_t dsize)
{
	const struct bitmap_ipmac_elem *elem;

	if (!test_bit(id, map->members))
		return 0;
	elem = get_elem(map->extensions, id, dsize);
	/* Timer not started for the incomplete elements */
	return elem->filled == MAC_FILLED;
}

static inline int
bitmap_ipmac_is_filled(const struct bitmap_ipmac_elem *elem)
{
	return elem->filled == MAC_FILLED;
}

static inline int
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
		 * possibly by the kernel */
		if (e->ether)
			ip_set_timeout_set(timeout, t);
		else
			*timeout = t;
	}
	return 0;
}

static inline int
bitmap_ipmac_do_add(const struct bitmap_ipmac_adt_elem *e,
		    struct bitmap_ipmac *map, u32 flags, size_t dsize)
{
	struct bitmap_ipmac_elem *elem;

	elem = get_elem(map->extensions, e->id, dsize);
	if (test_and_set_bit(e->id, map->members)) {
		if (elem->filled == MAC_FILLED) {
			if (e->ether && (flags & IPSET_FLAG_EXIST))
				memcpy(elem->ether, e->ether, ETH_ALEN);
			return IPSET_ADD_FAILED;
		} else if (!e->ether)
			/* Already added without ethernet address */
			return IPSET_ADD_FAILED;
		/* Fill the MAC address and trigger the timer activation */
		memcpy(elem->ether, e->ether, ETH_ALEN);
		elem->filled = MAC_FILLED;
		return IPSET_ADD_START_STORED_TIMEOUT;
	} else if (e->ether) {
		/* We can store MAC too */
		memcpy(elem->ether, e->ether, ETH_ALEN);
		elem->filled = MAC_FILLED;
		return 0;
	} else {
		elem->filled = MAC_UNSET;
		/* MAC is not stored yet, don't start timer */
		return IPSET_ADD_STORE_PLAIN_TIMEOUT;
	}
}

static inline int
bitmap_ipmac_do_del(const struct bitmap_ipmac_adt_elem *e,
		    struct bitmap_ipmac *map)
{
	return !test_and_clear_bit(e->id, map->members);
}

static inline unsigned long
ip_set_timeout_stored(struct bitmap_ipmac *map, u32 id, unsigned long *timeout,
		      size_t dsize)
{
	const struct bitmap_ipmac_elem *elem =
		get_elem(map->extensions, id, dsize);

	return elem->filled == MAC_FILLED ? ip_set_timeout_get(timeout) :
					    *timeout;
}

static inline int
bitmap_ipmac_do_list(struct sk_buff *skb, const struct bitmap_ipmac *map,
		     u32 id, size_t dsize)
{
	const struct bitmap_ipmac_elem *elem =
		get_elem(map->extensions, id, dsize);

	return nla_put_ipaddr4(skb, IPSET_ATTR_IP,
			       htonl(map->first_ip + id)) ||
	       (elem->filled == MAC_FILLED &&
		nla_put(skb, IPSET_ATTR_ETHER, ETH_ALEN, elem->ether));
}

static inline int
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
	struct bitmap_ipmac_adt_elem e = {};
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);
	u32 ip;

	/* MAC can be src only */
	if (!(opt->flags & IPSET_DIM_TWO_SRC))
		return 0;

	ip = ntohl(ip4addr(skb, opt->flags & IPSET_DIM_ONE_SRC));
	if (ip < map->first_ip || ip > map->last_ip)
		return -IPSET_ERR_BITMAP_RANGE;

	/* Backward compatibility: we don't check the second flag */
	if (skb_mac_header(skb) < skb->head ||
	    (skb_mac_header(skb) + ETH_HLEN) > skb->data)
		return -EINVAL;

	e.id = ip_to_id(map, ip);
	e.ether = eth_hdr(skb)->h_source;

	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
bitmap_ipmac_uadt(struct ip_set *set, struct nlattr *tb[],
		  enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	const struct bitmap_ipmac *map = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct bitmap_ipmac_adt_elem e = {};
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	u32 ip = 0;
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

	e.id = ip_to_id(map, ip);
	if (tb[IPSET_ATTR_ETHER])
		e.ether = nla_data(tb[IPSET_ATTR_ETHER]);
	else
		e.ether = NULL;

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

/* Timeout variant */

struct bitmap_ipmact_elem {
	struct {
		unsigned char ether[ETH_ALEN];
		unsigned char filled;
	} __attribute__ ((aligned));
	unsigned long timeout;
};

/* Plain variant with counter */

struct bitmap_ipmacc_elem {
	struct {
		unsigned char ether[ETH_ALEN];
		unsigned char filled;
	} __attribute__ ((aligned));
	struct ip_set_counter counter;
};

/* Timeout variant with counter */

struct bitmap_ipmacct_elem {
	struct {
		unsigned char ether[ETH_ALEN];
		unsigned char filled;
	} __attribute__ ((aligned));
	unsigned long timeout;
	struct ip_set_counter counter;
};

#include "ip_set_bitmap_gen.h"

/* Create bitmap:ip,mac type of sets */

static bool
init_map_ipmac(struct ip_set *set, struct bitmap_ipmac *map,
	       u32 first_ip, u32 last_ip, u32 elements)
{
	map->members = ip_set_alloc((last_ip - first_ip + 1) * set->dsize);
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
	set->timeout = IPSET_NO_TIMEOUT;

	set->data = map;
	set->family = NFPROTO_IPV4;

	return true;
}

static int
bitmap_ipmac_create(struct ip_set *set, struct nlattr *tb[],
		    u32 flags)
{
	u32 first_ip = 0, last_ip = 0, cadt_flags = 0;
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

	elements = (u64)last_ip - first_ip + 1;

	if (elements > IPSET_BITMAP_MAX_RANGE + 1)
		return -IPSET_ERR_BITMAP_RANGE_SIZE;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->memsize = bitmap_bytes(0, elements - 1);
	set->variant = &bitmap_ipmac;
	if (tb[IPSET_ATTR_CADT_FLAGS])
		cadt_flags = ip_set_get_h32(tb[IPSET_ATTR_CADT_FLAGS]);
	if (cadt_flags & IPSET_FLAG_WITH_COUNTERS) {
		set->extensions |= IPSET_EXT_COUNTER;
		if (tb[IPSET_ATTR_TIMEOUT]) {
			set->dsize = sizeof(struct bitmap_ipmacct_elem);
			set->offset[IPSET_EXT_ID_TIMEOUT] =
				offsetof(struct bitmap_ipmacct_elem, timeout);
			set->offset[IPSET_EXT_ID_COUNTER] =
				offsetof(struct bitmap_ipmacct_elem, counter);

			if (!init_map_ipmac(set, map, first_ip, last_ip,
					    elements)) {
				kfree(map);
				return -ENOMEM;
			}
			set->timeout = ip_set_timeout_uget(
				tb[IPSET_ATTR_TIMEOUT]);
			set->extensions |= IPSET_EXT_TIMEOUT;
			bitmap_ipmac_gc_init(set, bitmap_ipmac_gc);
		} else {
			set->dsize = sizeof(struct bitmap_ipmacc_elem);
			set->offset[IPSET_EXT_ID_COUNTER] =
				offsetof(struct bitmap_ipmacc_elem, counter);

			if (!init_map_ipmac(set, map, first_ip, last_ip,
					    elements)) {
				kfree(map);
				return -ENOMEM;
			}
		}
	} else if (tb[IPSET_ATTR_TIMEOUT]) {
		set->dsize = sizeof(struct bitmap_ipmact_elem);
		set->offset[IPSET_EXT_ID_TIMEOUT] =
			offsetof(struct bitmap_ipmact_elem, timeout);

		if (!init_map_ipmac(set, map, first_ip, last_ip, elements)) {
			kfree(map);
			return -ENOMEM;
		}
		set->timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
		set->extensions |= IPSET_EXT_TIMEOUT;
		bitmap_ipmac_gc_init(set, bitmap_ipmac_gc);
	} else {
		set->dsize = sizeof(struct bitmap_ipmac_elem);

		if (!init_map_ipmac(set, map, first_ip, last_ip, elements)) {
			kfree(map);
			return -ENOMEM;
		}
		set->variant = &bitmap_ipmac;
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
	ip_set_type_unregister(&bitmap_ipmac_type);
}

module_init(bitmap_ipmac_init);
module_exit(bitmap_ipmac_fini);
