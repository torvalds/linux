/* Copyright (C) 2011 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Kernel module implementing an IP set type: the hash:net,iface type */

#include <linux/jhash.h>
#include <linux/module.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/rbtree.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/netlink.h>

#include <linux/netfilter.h>
#include <linux/netfilter/ipset/pfxlen.h>
#include <linux/netfilter/ipset/ip_set.h>
#include <linux/netfilter/ipset/ip_set_timeout.h>
#include <linux/netfilter/ipset/ip_set_hash.h>

#define REVISION_MIN	0
/*			1    nomatch flag support added */
#define REVISION_MAX	2 /* /0 support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
IP_SET_MODULE_DESC("hash:net,iface", REVISION_MIN, REVISION_MAX);
MODULE_ALIAS("ip_set_hash:net,iface");

/* Interface name rbtree */

struct iface_node {
	struct rb_node node;
	char iface[IFNAMSIZ];
};

#define iface_data(n)	(rb_entry(n, struct iface_node, node)->iface)

static void
rbtree_destroy(struct rb_root *root)
{
	struct rb_node *p, *n = root->rb_node;
	struct iface_node *node;

	/* Non-recursive destroy, like in ext3 */
	while (n) {
		if (n->rb_left) {
			n = n->rb_left;
			continue;
		}
		if (n->rb_right) {
			n = n->rb_right;
			continue;
		}
		p = rb_parent(n);
		node = rb_entry(n, struct iface_node, node);
		if (!p)
			*root = RB_ROOT;
		else if (p->rb_left == n)
			p->rb_left = NULL;
		else if (p->rb_right == n)
			p->rb_right = NULL;

		kfree(node);
		n = p;
	}
}

static int
iface_test(struct rb_root *root, const char **iface)
{
	struct rb_node *n = root->rb_node;

	while (n) {
		const char *d = iface_data(n);
		int res = strcmp(*iface, d);

		if (res < 0)
			n = n->rb_left;
		else if (res > 0)
			n = n->rb_right;
		else {
			*iface = d;
			return 1;
		}
	}
	return 0;
}

static int
iface_add(struct rb_root *root, const char **iface)
{
	struct rb_node **n = &(root->rb_node), *p = NULL;
	struct iface_node *d;

	while (*n) {
		char *ifname = iface_data(*n);
		int res = strcmp(*iface, ifname);

		p = *n;
		if (res < 0)
			n = &((*n)->rb_left);
		else if (res > 0)
			n = &((*n)->rb_right);
		else {
			*iface = ifname;
			return 0;
		}
	}

	d = kzalloc(sizeof(*d), GFP_ATOMIC);
	if (!d)
		return -ENOMEM;
	strcpy(d->iface, *iface);

	rb_link_node(&d->node, p, n);
	rb_insert_color(&d->node, root);

	*iface = d->iface;
	return 0;
}

/* Type specific function prefix */
#define TYPE		hash_netiface

static bool
hash_netiface_same_set(const struct ip_set *a, const struct ip_set *b);

#define hash_netiface4_same_set	hash_netiface_same_set
#define hash_netiface6_same_set	hash_netiface_same_set

#define STREQ(a, b)	(strcmp(a, b) == 0)

/* The type variant functions: IPv4 */

struct hash_netiface4_elem_hashed {
	__be32 ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
};

#define HKEY_DATALEN	sizeof(struct hash_netiface4_elem_hashed)

/* Member elements without timeout */
struct hash_netiface4_elem {
	__be32 ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
	const char *iface;
};

/* Member elements with timeout support */
struct hash_netiface4_telem {
	__be32 ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
	const char *iface;
	unsigned long timeout;
};

static inline bool
hash_netiface4_data_equal(const struct hash_netiface4_elem *ip1,
			  const struct hash_netiface4_elem *ip2,
			  u32 *multi)
{
	return ip1->ip == ip2->ip &&
	       ip1->cidr == ip2->cidr &&
	       (++*multi) &&
	       ip1->physdev == ip2->physdev &&
	       ip1->iface == ip2->iface;
}

static inline bool
hash_netiface4_data_isnull(const struct hash_netiface4_elem *elem)
{
	return elem->elem == 0;
}

static inline void
hash_netiface4_data_copy(struct hash_netiface4_elem *dst,
			 const struct hash_netiface4_elem *src)
{
	memcpy(dst, src, sizeof(*dst));
}

static inline void
hash_netiface4_data_flags(struct hash_netiface4_elem *dst, u32 flags)
{
	dst->nomatch = flags & IPSET_FLAG_NOMATCH;
}

static inline int
hash_netiface4_data_match(const struct hash_netiface4_elem *elem)
{
	return elem->nomatch ? -ENOTEMPTY : 1;
}

static inline void
hash_netiface4_data_netmask(struct hash_netiface4_elem *elem, u8 cidr)
{
	elem->ip &= ip_set_netmask(cidr);
	elem->cidr = cidr;
}

static inline void
hash_netiface4_data_zero_out(struct hash_netiface4_elem *elem)
{
	elem->elem = 0;
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
	return 0;

nla_put_failure:
	return 1;
}

static bool
hash_netiface4_data_tlist(struct sk_buff *skb,
			  const struct hash_netiface4_elem *data)
{
	const struct hash_netiface4_telem *tdata =
		(const struct hash_netiface4_telem *)data;
	u32 flags = data->physdev ? IPSET_FLAG_PHYSDEV : 0;

	if (data->nomatch)
		flags |= IPSET_FLAG_NOMATCH;
	if (nla_put_ipaddr4(skb, IPSET_ATTR_IP, data->ip) ||
	    nla_put_u8(skb, IPSET_ATTR_CIDR, data->cidr) ||
	    nla_put_string(skb, IPSET_ATTR_IFACE, data->iface) ||
	    (flags &&
	     nla_put_net32(skb, IPSET_ATTR_CADT_FLAGS, htonl(flags))) ||
	    nla_put_net32(skb, IPSET_ATTR_TIMEOUT,
			  htonl(ip_set_timeout_get(tdata->timeout))))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return 1;
}

#define IP_SET_HASH_WITH_NETS
#define IP_SET_HASH_WITH_RBTREE
#define IP_SET_HASH_WITH_MULTI

#define PF		4
#define HOST_MASK	32
#include <linux/netfilter/ipset/ip_set_ahash.h>

static inline void
hash_netiface4_data_next(struct ip_set_hash *h,
			 const struct hash_netiface4_elem *d)
{
	h->next.ip = d->ip;
}

static int
hash_netiface4_kadt(struct ip_set *set, const struct sk_buff *skb,
		    const struct xt_action_param *par,
		    enum ipset_adt adt, const struct ip_set_adt_opt *opt)
{
	struct ip_set_hash *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netiface4_elem data = {
		.cidr = h->nets[0].cidr ? h->nets[0].cidr : HOST_MASK,
		.elem = 1,
	};
	int ret;

	if (data.cidr == 0)
		return -EINVAL;
	if (adt == IPSET_TEST)
		data.cidr = HOST_MASK;

	ip4addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &data.ip);
	data.ip &= ip_set_netmask(data.cidr);

#define IFACE(dir)	(par->dir ? par->dir->name : NULL)
#define PHYSDEV(dir)	(nf_bridge->dir ? nf_bridge->dir->name : NULL)
#define SRCDIR		(opt->flags & IPSET_DIM_TWO_SRC)

	if (opt->cmdflags & IPSET_FLAG_PHYSDEV) {
#ifdef CONFIG_BRIDGE_NETFILTER
		const struct nf_bridge_info *nf_bridge = skb->nf_bridge;

		if (!nf_bridge)
			return -EINVAL;
		data.iface = SRCDIR ? PHYSDEV(physindev) : PHYSDEV(physoutdev);
		data.physdev = 1;
#else
		data.iface = NULL;
#endif
	} else
		data.iface = SRCDIR ? IFACE(in) : IFACE(out);

	if (!data.iface)
		return -EINVAL;
	ret = iface_test(&h->rbtree, &data.iface);
	if (adt == IPSET_ADD) {
		if (!ret) {
			ret = iface_add(&h->rbtree, &data.iface);
			if (ret)
				return ret;
		}
	} else if (!ret)
		return ret;

	return adtfn(set, &data, opt_timeout(opt, h), opt->cmdflags);
}

static int
hash_netiface4_uadt(struct ip_set *set, struct nlattr *tb[],
		    enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	struct ip_set_hash *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netiface4_elem data = { .cidr = HOST_MASK, .elem = 1 };
	u32 ip = 0, ip_to, last;
	u32 timeout = h->timeout;
	char iface[IFNAMSIZ];
	int ret;

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !tb[IPSET_ATTR_IFACE] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP], &ip);
	if (ret)
		return ret;

	if (tb[IPSET_ATTR_CIDR]) {
		data.cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);
		if (data.cidr > HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
	}

	if (tb[IPSET_ATTR_TIMEOUT]) {
		if (!with_timeout(h->timeout))
			return -IPSET_ERR_TIMEOUT;
		timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
	}

	strcpy(iface, nla_data(tb[IPSET_ATTR_IFACE]));
	data.iface = iface;
	ret = iface_test(&h->rbtree, &data.iface);
	if (adt == IPSET_ADD) {
		if (!ret) {
			ret = iface_add(&h->rbtree, &data.iface);
			if (ret)
				return ret;
		}
	} else if (!ret)
		return ret;

	if (tb[IPSET_ATTR_CADT_FLAGS]) {
		u32 cadt_flags = ip_set_get_h32(tb[IPSET_ATTR_CADT_FLAGS]);
		if (cadt_flags & IPSET_FLAG_PHYSDEV)
			data.physdev = 1;
		if (adt == IPSET_ADD && (cadt_flags & IPSET_FLAG_NOMATCH))
			flags |= (cadt_flags << 16);
	}
	if (adt == IPSET_TEST || !tb[IPSET_ATTR_IP_TO]) {
		data.ip = htonl(ip & ip_set_hostmask(data.cidr));
		ret = adtfn(set, &data, timeout, flags);
		return ip_set_eexist(ret, flags) ? 0 : ret;
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
		ip_set_mask_from_to(ip, ip_to, data.cidr);
	}

	if (retried)
		ip = ntohl(h->next.ip);
	while (!after(ip, ip_to)) {
		data.ip = htonl(ip);
		last = ip_set_range_to_cidr(ip, ip_to, &data.cidr);
		ret = adtfn(set, &data, timeout, flags);

		if (ret && !ip_set_eexist(ret, flags))
			return ret;
		else
			ret = 0;
		ip = last + 1;
	}
	return ret;
}

static bool
hash_netiface_same_set(const struct ip_set *a, const struct ip_set *b)
{
	const struct ip_set_hash *x = a->data;
	const struct ip_set_hash *y = b->data;

	/* Resizing changes htable_bits, so we ignore it */
	return x->maxelem == y->maxelem &&
	       x->timeout == y->timeout;
}

/* The type variant functions: IPv6 */

struct hash_netiface6_elem_hashed {
	union nf_inet_addr ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
};

#define HKEY_DATALEN	sizeof(struct hash_netiface6_elem_hashed)

struct hash_netiface6_elem {
	union nf_inet_addr ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
	const char *iface;
};

struct hash_netiface6_telem {
	union nf_inet_addr ip;
	u8 physdev;
	u8 cidr;
	u8 nomatch;
	u8 elem;
	const char *iface;
	unsigned long timeout;
};

static inline bool
hash_netiface6_data_equal(const struct hash_netiface6_elem *ip1,
			  const struct hash_netiface6_elem *ip2,
			  u32 *multi)
{
	return ipv6_addr_cmp(&ip1->ip.in6, &ip2->ip.in6) == 0 &&
	       ip1->cidr == ip2->cidr &&
	       (++*multi) &&
	       ip1->physdev == ip2->physdev &&
	       ip1->iface == ip2->iface;
}

static inline bool
hash_netiface6_data_isnull(const struct hash_netiface6_elem *elem)
{
	return elem->elem == 0;
}

static inline void
hash_netiface6_data_copy(struct hash_netiface6_elem *dst,
			 const struct hash_netiface6_elem *src)
{
	memcpy(dst, src, sizeof(*dst));
}

static inline void
hash_netiface6_data_flags(struct hash_netiface6_elem *dst, u32 flags)
{
	dst->nomatch = flags & IPSET_FLAG_NOMATCH;
}

static inline int
hash_netiface6_data_match(const struct hash_netiface6_elem *elem)
{
	return elem->nomatch ? -ENOTEMPTY : 1;
}

static inline void
hash_netiface6_data_zero_out(struct hash_netiface6_elem *elem)
{
	elem->elem = 0;
}

static inline void
ip6_netmask(union nf_inet_addr *ip, u8 prefix)
{
	ip->ip6[0] &= ip_set_netmask6(prefix)[0];
	ip->ip6[1] &= ip_set_netmask6(prefix)[1];
	ip->ip6[2] &= ip_set_netmask6(prefix)[2];
	ip->ip6[3] &= ip_set_netmask6(prefix)[3];
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
	return 0;

nla_put_failure:
	return 1;
}

static bool
hash_netiface6_data_tlist(struct sk_buff *skb,
			  const struct hash_netiface6_elem *data)
{
	const struct hash_netiface6_telem *e =
		(const struct hash_netiface6_telem *)data;
	u32 flags = data->physdev ? IPSET_FLAG_PHYSDEV : 0;

	if (data->nomatch)
		flags |= IPSET_FLAG_NOMATCH;
	if (nla_put_ipaddr6(skb, IPSET_ATTR_IP, &e->ip.in6) ||
	    nla_put_u8(skb, IPSET_ATTR_CIDR, data->cidr) ||
	    nla_put_string(skb, IPSET_ATTR_IFACE, data->iface) ||
	    (flags &&
	     nla_put_net32(skb, IPSET_ATTR_CADT_FLAGS, htonl(flags))) ||
	    nla_put_net32(skb, IPSET_ATTR_TIMEOUT,
			  htonl(ip_set_timeout_get(e->timeout))))
		goto nla_put_failure;
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
hash_netiface6_data_next(struct ip_set_hash *h,
			 const struct hash_netiface6_elem *d)
{
}

static int
hash_netiface6_kadt(struct ip_set *set, const struct sk_buff *skb,
		    const struct xt_action_param *par,
		    enum ipset_adt adt, const struct ip_set_adt_opt *opt)
{
	struct ip_set_hash *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netiface6_elem data = {
		.cidr = h->nets[0].cidr ? h->nets[0].cidr : HOST_MASK,
		.elem = 1,
	};
	int ret;

	if (data.cidr == 0)
		return -EINVAL;
	if (adt == IPSET_TEST)
		data.cidr = HOST_MASK;

	ip6addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &data.ip.in6);
	ip6_netmask(&data.ip, data.cidr);

	if (opt->cmdflags & IPSET_FLAG_PHYSDEV) {
#ifdef CONFIG_BRIDGE_NETFILTER
		const struct nf_bridge_info *nf_bridge = skb->nf_bridge;

		if (!nf_bridge)
			return -EINVAL;
		data.iface = SRCDIR ? PHYSDEV(physindev) : PHYSDEV(physoutdev);
		data.physdev = 1;
#else
		data.iface = NULL;
#endif
	} else
		data.iface = SRCDIR ? IFACE(in) : IFACE(out);

	if (!data.iface)
		return -EINVAL;
	ret = iface_test(&h->rbtree, &data.iface);
	if (adt == IPSET_ADD) {
		if (!ret) {
			ret = iface_add(&h->rbtree, &data.iface);
			if (ret)
				return ret;
		}
	} else if (!ret)
		return ret;

	return adtfn(set, &data, opt_timeout(opt, h), opt->cmdflags);
}

static int
hash_netiface6_uadt(struct ip_set *set, struct nlattr *tb[],
		   enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	struct ip_set_hash *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_netiface6_elem data = { .cidr = HOST_MASK, .elem = 1 };
	u32 timeout = h->timeout;
	char iface[IFNAMSIZ];
	int ret;

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !tb[IPSET_ATTR_IFACE] ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_TIMEOUT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_CADT_FLAGS)))
		return -IPSET_ERR_PROTOCOL;
	if (unlikely(tb[IPSET_ATTR_IP_TO]))
		return -IPSET_ERR_HASH_RANGE_UNSUPPORTED;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	ret = ip_set_get_ipaddr6(tb[IPSET_ATTR_IP], &data.ip);
	if (ret)
		return ret;

	if (tb[IPSET_ATTR_CIDR])
		data.cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);
	if (data.cidr > HOST_MASK)
		return -IPSET_ERR_INVALID_CIDR;
	ip6_netmask(&data.ip, data.cidr);

	if (tb[IPSET_ATTR_TIMEOUT]) {
		if (!with_timeout(h->timeout))
			return -IPSET_ERR_TIMEOUT;
		timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);
	}

	strcpy(iface, nla_data(tb[IPSET_ATTR_IFACE]));
	data.iface = iface;
	ret = iface_test(&h->rbtree, &data.iface);
	if (adt == IPSET_ADD) {
		if (!ret) {
			ret = iface_add(&h->rbtree, &data.iface);
			if (ret)
				return ret;
		}
	} else if (!ret)
		return ret;

	if (tb[IPSET_ATTR_CADT_FLAGS]) {
		u32 cadt_flags = ip_set_get_h32(tb[IPSET_ATTR_CADT_FLAGS]);
		if (cadt_flags & IPSET_FLAG_PHYSDEV)
			data.physdev = 1;
		if (adt == IPSET_ADD && (cadt_flags & IPSET_FLAG_NOMATCH))
			flags |= (cadt_flags << 16);
	}

	ret = adtfn(set, &data, timeout, flags);

	return ip_set_eexist(ret, flags) ? 0 : ret;
}

/* Create hash:ip type of sets */

static int
hash_netiface_create(struct ip_set *set, struct nlattr *tb[], u32 flags)
{
	struct ip_set_hash *h;
	u32 hashsize = IPSET_DEFAULT_HASHSIZE, maxelem = IPSET_DEFAULT_MAXELEM;
	u8 hbits;
	size_t hsize;

	if (!(set->family == NFPROTO_IPV4 || set->family == NFPROTO_IPV6))
		return -IPSET_ERR_INVALID_FAMILY;

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

	h = kzalloc(sizeof(*h)
		    + sizeof(struct ip_set_hash_nets)
		      * (set->family == NFPROTO_IPV4 ? 32 : 128), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	h->maxelem = maxelem;
	get_random_bytes(&h->initval, sizeof(h->initval));
	h->timeout = IPSET_NO_TIMEOUT;
	h->ahash_max = AHASH_MAX_SIZE;

	hbits = htable_bits(hashsize);
	hsize = htable_size(hbits);
	if (hsize == 0) {
		kfree(h);
		return -ENOMEM;
	}
	h->table = ip_set_alloc(hsize);
	if (!h->table) {
		kfree(h);
		return -ENOMEM;
	}
	h->table->htable_bits = hbits;
	h->rbtree = RB_ROOT;

	set->data = h;

	if (tb[IPSET_ATTR_TIMEOUT]) {
		h->timeout = ip_set_timeout_uget(tb[IPSET_ATTR_TIMEOUT]);

		set->variant = set->family == NFPROTO_IPV4
			? &hash_netiface4_tvariant : &hash_netiface6_tvariant;

		if (set->family == NFPROTO_IPV4)
			hash_netiface4_gc_init(set);
		else
			hash_netiface6_gc_init(set);
	} else {
		set->variant = set->family == NFPROTO_IPV4
			? &hash_netiface4_variant : &hash_netiface6_variant;
	}

	pr_debug("create %s hashsize %u (%u) maxelem %u: %p(%p)\n",
		 set->name, jhash_size(h->table->htable_bits),
		 h->table->htable_bits, h->maxelem, set->data, h->table);

	return 0;
}

static struct ip_set_type hash_netiface_type __read_mostly = {
	.name		= "hash:net,iface",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_IP | IPSET_TYPE_IFACE |
			  IPSET_TYPE_NOMATCH,
	.dimension	= IPSET_DIM_TWO,
	.family		= NFPROTO_UNSPEC,
	.revision_min	= REVISION_MIN,
	.revision_max	= REVISION_MAX,
	.create		= hash_netiface_create,
	.create_policy	= {
		[IPSET_ATTR_HASHSIZE]	= { .type = NLA_U32 },
		[IPSET_ATTR_MAXELEM]	= { .type = NLA_U32 },
		[IPSET_ATTR_PROBES]	= { .type = NLA_U8 },
		[IPSET_ATTR_RESIZE]	= { .type = NLA_U8  },
		[IPSET_ATTR_PROTO]	= { .type = NLA_U8 },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
	},
	.adt_policy	= {
		[IPSET_ATTR_IP]		= { .type = NLA_NESTED },
		[IPSET_ATTR_IP_TO]	= { .type = NLA_NESTED },
		[IPSET_ATTR_IFACE]	= { .type = NLA_NUL_STRING,
					    .len = IPSET_MAXNAMELEN - 1 },
		[IPSET_ATTR_CADT_FLAGS]	= { .type = NLA_U32 },
		[IPSET_ATTR_CIDR]	= { .type = NLA_U8 },
		[IPSET_ATTR_TIMEOUT]	= { .type = NLA_U32 },
		[IPSET_ATTR_LINENO]	= { .type = NLA_U32 },
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
	ip_set_type_unregister(&hash_netiface_type);
}

module_init(hash_netiface_init);
module_exit(hash_netiface_fini);
