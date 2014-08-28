/* Copyright (C) 2003-2013 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Kernel module implementing an IP set type: the bitmap:port type */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/netlink.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <net/netlink.h>

#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_bitmap.h>
#include <linux/netfilter/ipset/ip_set_getport.h>

#define IPSET_TYPE_REV_MIN	0
/*				1	   Counter support added */
/*				2	   Comment support added */
#define IPSET_TYPE_REV_MAX	3	/* skbinfo support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
IP_SET_MODULE_DESC("bitmap:port", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_bitmap:port");

#define MTYPE		bitmap_port

/* Type structure */
struct bitmap_port {
	void *members;		/* the set members */
	void *extensions;	/* data extensions */
	u16 first_port;		/* host byte order, included in range */
	u16 last_port;		/* host byte order, included in range */
	u32 elements;		/* number of max elements in the set */
	size_t memsize;		/* members size */
	struct timer_list gc;	/* garbage collection */
};

/* ADT structure for generic function args */
struct bitmap_port_adt_elem {
	u16 id;
};

static inline u16
port_to_id(const struct bitmap_port *m, u16 port)
{
	return port - m->first_port;
}

/* Common functions */

static inline int
bitmap_port_do_test(const struct bitmap_port_adt_elem *e,
		    const struct bitmap_port *map, size_t dsize)
{
	return !!test_bit(e->id, map->members);
}

static inline int
bitmap_port_gc_test(u16 id, const struct bitmap_port *map, size_t dsize)
{
	return !!test_bit(id, map->members);
}

static inline int
bitmap_port_do_add(const struct bitmap_port_adt_elem *e,
		   struct bitmap_port *map, u32 flags, size_t dsize)
{
	return !!test_and_set_bit(e->id, map->members);
}

static inline int
bitmap_port_do_del(const struct bitmap_port_adt_elem *e,
		   struct bitmap_port *map)
{
	return !test_and_clear_bit(e->id, map->members);
}

static inline int
bitmap_port_do_list(struct sk_buff *skb, const struct bitmap_port *map, u32 id,
		    size_t dsize)
{
	return nla_put_net16(skb, IPSET_ATTR_PORT,
			     htons(map->first_port + id));
}

static inline int
bitmap_port_do_head(struct sk_buff *skb, const struct bitmap_port *map)
{
	return nla_put_net16(skb, IPSET_ATTR_PORT, htons(map->first_port)) ||
	       nla_put_net16(skb, IPSET_ATTR_PORT_TO, htons(map->last_port));
}

static int
bitmap_port_kadt(struct ip_set *set, const struct sk_buff *skb,
		 const struct xt_action_param *par,
		 enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	struct bitmap_port *map = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct bitmap_port_adt_elem e = { .id = 0 };
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);
	__be16 __port;
	u16 port = 0;

	if (!ip_set_get_ip_port(skb, opt->family,
				opt->flags & IPSET_DIM_ONE_SRC, &__port))
		return -EINVAL;

	port = ntohs(__port);

	if (port < map->first_port || port > map->last_port)
		return -IPSET_ERR_BITMAP_RANGE;

	e.id = port_to_id(map, port);

	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
bitmap_port_uadt(struct ip_set *set, struct nlattr *tb[],
		 enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	struct bitmap_port *map = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct bitmap_port_adt_elem e = { .id = 0 };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	u32 port;	/* wraparound */
	u16 port_to;
	int ret = 0;

	if (unlikely(!ip_set_attr_netorder(tb, IPSET_ATTR_PORT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PORT_TO) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PACKETS) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_BYTES)   ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBMARK) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBPRIO) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_SKBQUEUE)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	port = ip_set_get_h16(tb[IPSET_ATTR_PORT]);
	if (port < map->first_port || port > map->last_port)
		return -IPSET_ERR_BITMAP_RANGE;
	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	if (adt == IPSET_TEST) {
		e.id = port_to_id(map, port);
		return adtfn(set, &e, &ext, &ext, flags);
	}

	if (tb[IPSET_ATTR_PORT_TO]) {
		port_to = ip_set_get_h16(tb[IPSET_ATTR_PORT_TO]);
		if (port > port_to) {
			swap(port, port_to);
			if (port < map->first_port)
				return -IPSET_ERR_BITMAP_RANGE;
		}
	} else
		port_to = port;

	if (port_to > map->last_port)
		return -IPSET_ERR_BITMAP_RANGE;

	for (; port <= port_to; port++) {
		e.id = port_to_id(map, port);
		ret = adtfn(set, &e, &ext, &ext, flags);

		if (ret && !ip_set_eexist(ret, flags))
			return ret;
		else
			ret = 0;
	}
	return ret;
}

static bool
bitmap_port_same_set(const struct ip_set *a, const struct ip_set *b)
{
	const struct bitmap_port *x = a->data;
	const struct bitmap_port *y = b->data;

	return x->first_port == y->first_port &&
	       x->last_port == y->last_port &&
	       a->timeout == b->timeout &&
	       a->extensions == b->extensions;
}

/* Plain variant */

struct bitmap_port_elem {
};

#include "ip_set_bitmap_gen.h"

/* Create bitmap:ip type of sets */

static bool
init_map_port(struct ip_set *set, struct bitmap_port *map,
	      u16 first_port, u16 last_port)
{
	map->members = ip_set_alloc(map->memsize);
	if (!map->members)
		return false;
	if (set->dsize) {
		map->extensions = ip_set_alloc(set->dsize * map->elements);
		if (!map->extensions) {
			kfree(map->members);
			return false;
		}
	}
	map->first_port = first_port;
	map->last_port = last_port;
	set->timeout = IPSET_NO_TIMEOUT;

	set->data = map;
	set->family = NFPROTO_UNSPEC;

	return true;
}

static int
bitmap_port_create(struct net *net, struct ip_set *set, struct nlattr *tb[],
		   u32 flags)
{
	struct bitmap_port *map;
	u16 first_port, last_port;

	if (unlikely(!ip_set_attr_netorder(tb, IPSET_ATTR_PORT) ||
		     !ip_set_attr_netorder(tb, IPSET_ATTR_PORT_TO) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;

	first_port = ip_set_get_h16(tb[IPSET_ATTR_PORT]);
	last_port = ip_set_get_h16(tb[IPSET_ATTR_PORT_TO]);
	if (first_port > last_port) {
		u16 tmp = first_port;

		first_port = last_port;
		last_port = tmp;
	}

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->elements = last_port - first_port + 1;
	map->memsize = bitmap_bytes(0, map->elements);
	set->variant = &bitmap_port;
	set->dsize = ip_set_elem_len(set, tb, 0);
	if (!init_map_port(set, map, first_port, last_port)) {
		kfree(map);
		return -ENOMEM;
	}
	if (tb[IPSET_ATTR_TIMEOUT]) {
		set->timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
		bitmap_port_gc_init(set, bitmap_port_gc);
	}
	return 0;
}

static struct ip_set_type bitmap_port_type = {
	.name		= "bitmap:port",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_PORT,
	.dimension	= IPSET_DIM_ONE,
	.family		= NFPROTO_UNSPEC,
	.revision_min	= IPSET_TYPE_REV_MIN,
	.revision_max	= IPSET_TYPE_REV_MAX,
	.create		= bitmap_port_create,
	.create_policy	= {
		[IPSET_ATTR_PORT]	= { .type = NLA_U16 },
		[IPSET_ATTR_PORT_TO]	= { .type = NLA_U16 },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
	},
	.adt_policy	= {
		[IPSET_ATTR_PORT]	= { .type = NLA_U16 },
		[IPSET_ATTR_PORT_TO]	= { .type = NLA_U16 },
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
bitmap_port_init(void)
{
	return ip_set_type_register(&bitmap_port_type);
}

static void __exit
bitmap_port_fini(void)
{
	ip_set_type_unregister(&bitmap_port_type);
}

module_init(bitmap_port_init);
module_exit(bitmap_port_fini);
