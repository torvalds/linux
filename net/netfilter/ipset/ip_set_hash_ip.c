/* Copyright (C) 2003-2011 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Kernel module implementing an IP set type: the hash:ip type */

#include <linux/jhash.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/netlink.h>
#include <net/tcp.h>

#include <linux/netfilter.h>
#include <linux/netfilter/ipset/pfxlen.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_timeout.h>
#include <linux/netfilter/ipset/ip_set_hash.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
MODULE_DESCRIPTION("hash:ip type of IP sets");
MODULE_ALIAS("ip_set_hash:ip");

/* Type specific function prefix */
#define TYPE		hash_ip

static bool
hash_ip_same_set(const struct ip_set *a, const struct ip_set *b);

#define hash_ip4_same_set	hash_ip_same_set
#define hash_ip6_same_set	hash_ip_same_set

/* The type variant functions: IPv4 */

/* Member elements without timeout */
struct hash_ip4_elem {
	__be32 ip;
};

/* Member elements with timeout support */
struct hash_ip4_telem {
	__be32 ip;
	unsigned long timeout;
};

static inline bool
hash_ip4_data_equal(const struct hash_ip4_elem *ip1,
		    const struct hash_ip4_elem *ip2)
{
	return ip1->ip == ip2->ip;
}

static inline bool
hash_ip4_data_isnull(const struct hash_ip4_elem *elem)
{
	return elem->ip == 0;
}

static inline void
hash_ip4_data_copy(struct hash_ip4_elem *dst, const struct hash_ip4_elem *src)
{
	dst->ip = src->ip;
}

/* Zero valued IP addresses cannot be stored */
static inline void
hash_ip4_data_zero_out(struct hash_ip4_elem *elem)
{
	elem->ip = 0;
}

static inline bool
hash_ip4_data_list(struct sk_buff *skb, const struct hash_ip4_elem *data)
{
	NLA_PUT_IPADDR4(skb, IPSET_ATTR_IP, data->ip);
	return 0;

nla_put_failure:
	return 1;
}

static bool
hash_ip4_data_tlist(struct sk_buff *skb, const struct hash_ip4_elem *data)
{
	const struct hash_ip4_telem *tdata =
		(const struct hash_ip4_telem *)data;

	NLA_PUT_IPADDR4(skb, IPSET_ATTR_IP, tdata->ip);
	NLA_PUT_NET32(skb, IPSET_ATTR_TIMEOUT,
		      htonl(ip_set_timeout_get(tdata->timeout)));

	return 0;

nla_put_failure:
	return 1;
}

#define IP_SET_HASH_WITH_NETMASK
#define PF		4
#define HOST_MASK	32
#include <linux/netfilter/ipset/ip_set_ahash.h>

static inline void
hash_ip4_data_next(struct ip_set_hash *h, const struct hash_ip4_elem *d)
{
	h->next.ip = ntohl(d->ip);
}

static int
hash_ip4_kadt(struct ip_set *set, const struct sk_buff *skb,
	      enum ipset_adt adt, const struct ip_set_adt_opt *opt)
{
	const struct ip_set_hash *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	__be32 ip;

	ip4addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &ip);
	ip &= ip_set_netmask(h->netmask);
	if (ip == 0)
		return -EINVAL;

	return adtfn(set, &ip, opt_timeout(opt, h), opt->cmdflags);
}

static int
hash_ip4_uadt(struct ip_set *set, struct nlattr *tb[],
	      enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	const struct ip_set_hash *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	u32 ip, ip_to, hosts, timeout = h->timeout;
	__be32 nip;
	int ret = 0;

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP], &ip);
	if (ret)
		return ret;

	ip &= ip_set_hostmask(h->netmask);

	if (tb[IPSET_ATTR_TIMEOUT]) {
		if (!with_timeout(h->timeout))
			return -IPSET_ERR_TIMEOUT;
		timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
	}

	if (adt == IPSET_TEST) {
		nip = htonl(ip);
		if (nip == 0)
			return -IPSET_ERR_HASH_ELEM;
		return adtfn(set, &nip, timeout, flags);
	}

	if (tb[IPSET_ATTR_IP_TO]) {
		ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP_TO], &ip_to);
		if (ret)
			return ret;
		if (ip > ip_to)
			swap(ip, ip_to);
	} else if (tb[IPSET_ATTR_CIDR]) {
		u8 cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);

		if (cidr > 32)
			return -IPSET_ERR_INVALID_CIDR;
		ip_set_mask_from_to(ip, ip_to, cidr);
	} else
		ip_to = ip;

	hosts = h->netmask == 32 ? 1 : 2 << (32 - h->netmask - 1);

	if (retried)
		ip = h->next.ip;
	for (; !before(ip_to, ip); ip += hosts) {
		nip = htonl(ip);
		if (nip == 0)
			return -IPSET_ERR_HASH_ELEM;
		ret = adtfn(set, &nip, timeout, flags);

		if (ret && !ip_set_eexist(ret, flags))
			return ret;
		else
			ret = 0;
	}
	return ret;
}

static bool
hash_ip_same_set(const struct ip_set *a, const struct ip_set *b)
{
	const struct ip_set_hash *x = a->data;
	const struct ip_set_hash *y = b->data;

	/* Resizing changes htable_bits, so we ignore it */
	return x->maxelem == y->maxelem &&
	       x->timeout == y->timeout &&
	       x->netmask == y->netmask;
}

/* The type variant functions: IPv6 */

struct hash_ip6_elem {
	union nf_inet_addr ip;
};

struct hash_ip6_telem {
	union nf_inet_addr ip;
	unsigned long timeout;
};

static inline bool
hash_ip6_data_equal(const struct hash_ip6_elem *ip1,
		    const struct hash_ip6_elem *ip2)
{
	return ipv6_addr_cmp(&ip1->ip.in6, &ip2->ip.in6) == 0;
}

static inline bool
hash_ip6_data_isnull(const struct hash_ip6_elem *elem)
{
	return ipv6_addr_any(&elem->ip.in6);
}

static inline void
hash_ip6_data_copy(struct hash_ip6_elem *dst, const struct hash_ip6_elem *src)
{
	ipv6_addr_copy(&dst->ip.in6, &src->ip.in6);
}

static inline void
hash_ip6_data_zero_out(struct hash_ip6_elem *elem)
{
	ipv6_addr_set(&elem->ip.in6, 0, 0, 0, 0);
}

static inline void
ip6_netmask(union nf_inet_addr *ip, u8 prefix)
{
	ip->ip6[0] &= ip_set_netmask6(prefix)[0];
	ip->ip6[1] &= ip_set_netmask6(prefix)[1];
	ip->ip6[2] &= ip_set_netmask6(prefix)[2];
	ip->ip6[3] &= ip_set_netmask6(prefix)[3];
}

static bool
hash_ip6_data_list(struct sk_buff *skb, const struct hash_ip6_elem *data)
{
	NLA_PUT_IPADDR6(skb, IPSET_ATTR_IP, &data->ip);
	return 0;

nla_put_failure:
	return 1;
}

static bool
hash_ip6_data_tlist(struct sk_buff *skb, const struct hash_ip6_elem *data)
{
	const struct hash_ip6_telem *e =
		(const struct hash_ip6_telem *)data;

	NLA_PUT_IPADDR6(skb, IPSET_ATTR_IP, &e->ip);
	NLA_PUT_NET32(skb, IPSET_ATTR_TIMEOUT,
		      htonl(ip_set_timeout_get(e->timeout)));
	return 0;

nla_put_failure:
	return 1;
}

#undef PF
#undef HOST_MASK

#define PF		6
#define HOST_MASK	128
#include <linux/netfilter/ipset/ip_set_ahash.h>

static inline void
hash_ip6_data_next(struct ip_set_hash *h, const struct hash_ip6_elem *d)
{
}

static int
hash_ip6_kadt(struct ip_set *set, const struct sk_buff *skb,
	      enum ipset_adt adt, const struct ip_set_adt_opt *opt)
{
	const struct ip_set_hash *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	union nf_inet_addr ip;

	ip6addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &ip.in6);
	ip6_netmask(&ip, h->netmask);
	if (ipv6_addr_any(&ip.in6))
		return -EINVAL;

	return adtfn(set, &ip, opt_timeout(opt, h), opt->cmdflags);
}

static const struct nla_policy hash_ip6_adt_policy[IPSET_ATTR_ADT_MAX + 1] = {
	[IPSET_ATTR_IP]		= { .type = NLA_NESTED },
	[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
	[IPSET_ATTR_LINENO]	= { .type = NLA_U32 },
};

static int
hash_ip6_uadt(struct ip_set *set, struct nlattr *tb[],
	      enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	const struct ip_set_hash *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	union nf_inet_addr ip;
	u32 timeout = h->timeout;
	int ret;

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     tb[IPSET_ATTR_IP_TO] ||
		     tb[IPSET_ATTR_CIDR]))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	ret = ip_set_get_ipaddr6(tb[IPSET_ATTR_IP], &ip);
	if (ret)
		return ret;

	ip6_netmask(&ip, h->netmask);
	if (ipv6_addr_any(&ip.in6))
		return -IPSET_ERR_HASH_ELEM;

	if (tb[IPSET_ATTR_TIMEOUT]) {
		if (!with_timeout(h->timeout))
			return -IPSET_ERR_TIMEOUT;
		timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
	}

	ret = adtfn(set, &ip, timeout, flags);

	return ip_set_eexist(ret, flags) ? 0 : ret;
}

/* Create hash:ip type of sets */

static int
hash_ip_create(struct ip_set *set, struct nlattr *tb[], u32 flags)
{
	u32 hashsize = IPSET_DEFAULT_HASHSIZE, maxelem = IPSET_DEFAULT_MAXELEM;
	u8 netmask, hbits;
	struct ip_set_hash *h;

	if (!(set->family == AF_INET || set->family == AF_INET6))
		return -IPSET_ERR_INVALID_FAMILY;
	netmask = set->family == AF_INET ? 32 : 128;
	pr_debug("Create set %s with family %s\n",
		 set->name, set->family == AF_INET ? "inet" : "inet6");

	if (unlikely(!ip_set_optattr_netorder(tb, IPSET_ATTR_HASHSIZE) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_MAXELEM) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_HASHSIZE]) {
		hashsize = ip_set_get_h32(tb[IPSET_ATTR_HASHSIZE]);
		if (hashsize < IPSET_MIMINAL_HASHSIZE)
			hashsize = IPSET_MIMINAL_HASHSIZE;
	}

	if (tb[IPSET_ATTR_MAXELEM])
		maxelem = ip_set_get_h32(tb[IPSET_ATTR_MAXELEM]);

	if (tb[IPSET_ATTR_NETMASK]) {
		netmask = nla_get_u8(tb[IPSET_ATTR_NETMASK]);

		if ((set->family == AF_INET && netmask > 32) ||
		    (set->family == AF_INET6 && netmask > 128) ||
		    netmask == 0)
			return -IPSET_ERR_INVALID_NETMASK;
	}

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	h->maxelem = maxelem;
	h->netmask = netmask;
	get_random_bytes(&h->initval, sizeof(h->initval));
	h->timeout = IPSET_NO_TIMEOUT;

	hbits = htable_bits(hashsize);
	h->table = ip_set_alloc(
			sizeof(struct htable)
			+ jhash_size(hbits) * sizeof(struct hbucket));
	if (!h->table) {
		kfree(h);
		return -ENOMEM;
	}
	h->table->htable_bits = hbits;

	set->data = h;

	if (tb[IPSET_ATTR_TIMEOUT]) {
		h->timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);

		set->variant = set->family == AF_INET
			? &hash_ip4_tvariant : &hash_ip6_tvariant;

		if (set->family == AF_INET)
			hash_ip4_gc_init(set);
		else
			hash_ip6_gc_init(set);
	} else {
		set->variant = set->family == AF_INET
			? &hash_ip4_variant : &hash_ip6_variant;
	}

	pr_debug("create %s hashsize %u (%u) maxelem %u: %p(%p)\n",
		 set->name, jhash_size(h->table->htable_bits),
		 h->table->htable_bits, h->maxelem, set->data, h->table);

	return 0;
}

static struct ip_set_type hash_ip_type __read_mostly = {
	.name		= "hash:ip",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_IP,
	.dimension	= IPSET_DIM_ONE,
	.family		= AF_UNSPEC,
	.revision_min	= 0,
	.revision_max	= 0,
	.create		= hash_ip_create,
	.create_policy	= {
		[IPSET_ATTR_HASHSIZE]	= { .type = NLA_U32 },
		[IPSET_ATTR_MAXELEM]	= { .type = NLA_U32 },
		[IPSET_ATTR_PROBES]	= { .type = NLA_U8 },
		[IPSET_ATTR_RESIZE]	= { .type = NLA_U8  },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_NETMASK]	= { .type = NLA_U8  },
	},
	.adt_policy	= {
		[IPSET_ATTR_IP]		= { .type = NLA_NESTED },
		[IPSET_ATTR_IP_TO]	= { .type = NLA_NESTED },
		[IPSET_ATTR_CIDR]	= { .type = NLA_U8 },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_LINENO]	= { .type = NLA_U32 },
	},
	.me		= THIS_MODULE,
};

static int __init
hash_ip_init(void)
{
	return ip_set_type_register(&hash_ip_type);
}

static void __exit
hash_ip_fini(void)
{
	ip_set_type_unregister(&hash_ip_type);
}

module_init(hash_ip_init);
module_exit(hash_ip_fini);
