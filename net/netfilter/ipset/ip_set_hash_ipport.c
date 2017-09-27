/* Copyright (C) 2003-2013 Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Kernel module implementing an IP set type: the hash:ip,port type */

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
#include <linux/netfilter/ipset/ip_set_getport.h>
#include <linux/netfilter/ipset/ip_set_hash.h>

#define IPSET_TYPE_REV_MIN	0
/*				1    SCTP and UDPLITE support added */
/*				2    Counters support added */
/*				3    Comments support added */
/*				4    Forceadd support added */
#define IPSET_TYPE_REV_MAX	5 /* skbinfo support added */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>");
IP_SET_MODULE_DESC("hash:ip,port", IPSET_TYPE_REV_MIN, IPSET_TYPE_REV_MAX);
MODULE_ALIAS("ip_set_hash:ip,port");

/* Type specific function prefix */
#define HTYPE		hash_ipport

/* IPv4 variant */

/* Member elements */
struct hash_ipport4_elem {
	__be32 ip;
	__be16 port;
	u8 proto;
	u8 padding;
};

/* Common functions */

static inline bool
hash_ipport4_data_equal(const struct hash_ipport4_elem *ip1,
			const struct hash_ipport4_elem *ip2,
			u32 *multi)
{
	return ip1->ip == ip2->ip &&
	       ip1->port == ip2->port &&
	       ip1->proto == ip2->proto;
}

static bool
hash_ipport4_data_list(struct sk_buff *skb,
		       const struct hash_ipport4_elem *data)
{
	if (nla_put_ipaddr4(skb, IPSET_ATTR_IP, data->ip) ||
	    nla_put_net16(skb, IPSET_ATTR_PORT, data->port) ||
	    nla_put_u8(skb, IPSET_ATTR_PROTO, data->proto))
		goto nla_put_failure;
	return false;

nla_put_failure:
	return true;
}

static inline void
hash_ipport4_data_next(struct hash_ipport4_elem *next,
		       const struct hash_ipport4_elem *d)
{
	next->ip = d->ip;
	next->port = d->port;
}

#define MTYPE		hash_ipport4
#define HOST_MASK	32
#include "ip_set_hash_gen.h"

static int
hash_ipport4_kadt(struct ip_set *set, const struct sk_buff *skb,
		  const struct xt_action_param *par,
		  enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_ipport4_elem e = { .ip = 0 };
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);

	if (!ip_set_get_ip4_port(skb, opt->flags & IPSET_DIM_TWO_SRC,
				 &e.port, &e.proto))
		return -EINVAL;

	ip4addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &e.ip);
	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
hash_ipport4_uadt(struct ip_set *set, struct nlattr *tb[],
		  enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	const struct hash_ipport4 *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_ipport4_elem e = { .ip = 0 };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	u32 ip, ip_to = 0, p = 0, port, port_to;
	bool with_ports = false;
	int ret;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !ip_set_attr_netorder(tb, IPSET_ATTR_PORT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PORT_TO)))
		return -IPSET_ERR_PROTOCOL;

	ret = ip_set_get_ipaddr4(tb[IPSET_ATTR_IP], &e.ip);
	if (ret)
		return ret;

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	e.port = nla_get_be16(tb[IPSET_ATTR_PORT]);

	if (tb[IPSET_ATTR_PROTO]) {
		e.proto = nla_get_u8(tb[IPSET_ATTR_PROTO]);
		with_ports = ip_set_proto_with_ports(e.proto);

		if (e.proto == 0)
			return -IPSET_ERR_INVALID_PROTO;
	} else {
		return -IPSET_ERR_MISSING_PROTO;
	}

	if (!(with_ports || e.proto == IPPROTO_ICMP))
		e.port = 0;

	if (adt == IPSET_TEST ||
	    !(tb[IPSET_ATTR_IP_TO] || tb[IPSET_ATTR_CIDR] ||
	      tb[IPSET_ATTR_PORT_TO])) {
		ret = adtfn(set, &e, &ext, &ext, flags);
		return ip_set_eexist(ret, flags) ? 0 : ret;
	}

	ip_to = ip = ntohl(e.ip);
	if (tb[IPSET_ATTR_IP_TO]) {
		ret = ip_set_get_hostipaddr4(tb[IPSET_ATTR_IP_TO], &ip_to);
		if (ret)
			return ret;
		if (ip > ip_to)
			swap(ip, ip_to);
	} else if (tb[IPSET_ATTR_CIDR]) {
		u8 cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);

		if (!cidr || cidr > HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
		ip_set_mask_from_to(ip, ip_to, cidr);
	}

	port_to = port = ntohs(e.port);
	if (with_ports && tb[IPSET_ATTR_PORT_TO]) {
		port_to = ip_set_get_h16(tb[IPSET_ATTR_PORT_TO]);
		if (port > port_to)
			swap(port, port_to);
	}

	if (retried)
		ip = ntohl(h->next.ip);
	for (; !before(ip_to, ip); ip++) {
		p = retried && ip == ntohl(h->next.ip) ? ntohs(h->next.port)
						       : port;
		for (; p <= port_to; p++) {
			e.ip = htonl(ip);
			e.port = htons(p);
			ret = adtfn(set, &e, &ext, &ext, flags);

			if (ret && !ip_set_eexist(ret, flags))
				return ret;

			ret = 0;
		}
	}
	return ret;
}

/* IPv6 variant */

struct hash_ipport6_elem {
	union nf_inet_addr ip;
	__be16 port;
	u8 proto;
	u8 padding;
};

/* Common functions */

static inline bool
hash_ipport6_data_equal(const struct hash_ipport6_elem *ip1,
			const struct hash_ipport6_elem *ip2,
			u32 *multi)
{
	return ipv6_addr_equal(&ip1->ip.in6, &ip2->ip.in6) &&
	       ip1->port == ip2->port &&
	       ip1->proto == ip2->proto;
}

static bool
hash_ipport6_data_list(struct sk_buff *skb,
		       const struct hash_ipport6_elem *data)
{
	if (nla_put_ipaddr6(skb, IPSET_ATTR_IP, &data->ip.in6) ||
	    nla_put_net16(skb, IPSET_ATTR_PORT, data->port) ||
	    nla_put_u8(skb, IPSET_ATTR_PROTO, data->proto))
		goto nla_put_failure;
	return false;

nla_put_failure:
	return true;
}

static inline void
hash_ipport6_data_next(struct hash_ipport6_elem *next,
		       const struct hash_ipport6_elem *d)
{
	next->port = d->port;
}

#undef MTYPE
#undef HOST_MASK

#define MTYPE		hash_ipport6
#define HOST_MASK	128
#define IP_SET_EMIT_CREATE
#include "ip_set_hash_gen.h"

static int
hash_ipport6_kadt(struct ip_set *set, const struct sk_buff *skb,
		  const struct xt_action_param *par,
		  enum ipset_adt adt, struct ip_set_adt_opt *opt)
{
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_ipport6_elem e = { .ip = { .all = { 0 } } };
	struct ip_set_ext ext = IP_SET_INIT_KEXT(skb, opt, set);

	if (!ip_set_get_ip6_port(skb, opt->flags & IPSET_DIM_TWO_SRC,
				 &e.port, &e.proto))
		return -EINVAL;

	ip6addrptr(skb, opt->flags & IPSET_DIM_ONE_SRC, &e.ip.in6);
	return adtfn(set, &e, &ext, &opt->ext, opt->cmdflags);
}

static int
hash_ipport6_uadt(struct ip_set *set, struct nlattr *tb[],
		  enum ipset_adt adt, u32 *lineno, u32 flags, bool retried)
{
	const struct hash_ipport6 *h = set->data;
	ipset_adtfn adtfn = set->variant->adt[adt];
	struct hash_ipport6_elem e = { .ip = { .all = { 0 } } };
	struct ip_set_ext ext = IP_SET_INIT_UEXT(set);
	u32 port, port_to;
	bool with_ports = false;
	int ret;

	if (tb[IPSET_ATTR_LINENO])
		*lineno = nla_get_u32(tb[IPSET_ATTR_LINENO]);

	if (unlikely(!tb[IPSET_ATTR_IP] ||
		     !ip_set_attr_netorder(tb, IPSET_ATTR_PORT) ||
		     !ip_set_optattr_netorder(tb, IPSET_ATTR_PORT_TO)))
		return -IPSET_ERR_PROTOCOL;
	if (unlikely(tb[IPSET_ATTR_IP_TO]))
		return -IPSET_ERR_HASH_RANGE_UNSUPPORTED;
	if (unlikely(tb[IPSET_ATTR_CIDR])) {
		u8 cidr = nla_get_u8(tb[IPSET_ATTR_CIDR]);

		if (cidr != HOST_MASK)
			return -IPSET_ERR_INVALID_CIDR;
	}

	ret = ip_set_get_ipaddr6(tb[IPSET_ATTR_IP], &e.ip);
	if (ret)
		return ret;

	ret = ip_set_get_extensions(set, tb, &ext);
	if (ret)
		return ret;

	e.port = nla_get_be16(tb[IPSET_ATTR_PORT]);

	if (tb[IPSET_ATTR_PROTO]) {
		e.proto = nla_get_u8(tb[IPSET_ATTR_PROTO]);
		with_ports = ip_set_proto_with_ports(e.proto);

		if (e.proto == 0)
			return -IPSET_ERR_INVALID_PROTO;
	} else {
		return -IPSET_ERR_MISSING_PROTO;
	}

	if (!(with_ports || e.proto == IPPROTO_ICMPV6))
		e.port = 0;

	if (adt == IPSET_TEST || !with_ports || !tb[IPSET_ATTR_PORT_TO]) {
		ret = adtfn(set, &e, &ext, &ext, flags);
		return ip_set_eexist(ret, flags) ? 0 : ret;
	}

	port = ntohs(e.port);
	port_to = ip_set_get_h16(tb[IPSET_ATTR_PORT_TO]);
	if (port > port_to)
		swap(port, port_to);

	if (retried)
		port = ntohs(h->next.port);
	for (; port <= port_to; port++) {
		e.port = htons(port);
		ret = adtfn(set, &e, &ext, &ext, flags);

		if (ret && !ip_set_eexist(ret, flags))
			return ret;

		ret = 0;
	}
	return ret;
}

static struct ip_set_type hash_ipport_type __read_mostly = {
	.name		= "hash:ip,port",
	.protocol	= IPSET_PROTOCOL,
	.features	= IPSET_TYPE_IP | IPSET_TYPE_PORT,
	.dimension	= IPSET_DIM_TWO,
	.family		= NFPROTO_UNSPEC,
	.revision_min	= IPSET_TYPE_REV_MIN,
	.revision_max	= IPSET_TYPE_REV_MAX,
	.create		= hash_ipport_create,
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
		[IPSET_ATTR_PORT]	= { .type = NLA_U16 },
		[IPSET_ATTR_PORT_TO]	= { .type = NLA_U16 },
		[IPSET_ATTR_CIDR]	= { .type = NLA_U8 },
		[IPSET_ATTR_PROTO]	= { .type = NLA_U8 },
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
hash_ipport_init(void)
{
	return ip_set_type_register(&hash_ipport_type);
}

static void __exit
hash_ipport_fini(void)
{
	rcu_barrier();
	ip_set_type_unregister(&hash_ipport_type);
}

module_init(hash_ipport_init);
module_exit(hash_ipport_fini);
