// SPDX-License-Identifier: GPL-2.0-only
/*
 * IPv6 library code, needed by static components when full IPv6 support is
 * not configured or static.
 */

#include <linux/export.h>
#include <net/ipv6.h>
#include <net/ipv6_stubs.h>
#include <net/ip.h>

/* if ipv6 module registers this function is used by xfrm to force all
 * sockets to relookup their nodes - this is fairly expensive, be
 * careful
 */
void (*__fib6_flush_trees)(struct net *);
EXPORT_SYMBOL(__fib6_flush_trees);

#define IPV6_ADDR_SCOPE_TYPE(scope)	((scope) << 16)

static inline unsigned int ipv6_addr_scope2type(unsigned int scope)
{
	switch (scope) {
	case IPV6_ADDR_SCOPE_NODELOCAL:
		return (IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_NODELOCAL) |
			IPV6_ADDR_LOOPBACK);
	case IPV6_ADDR_SCOPE_LINKLOCAL:
		return (IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_LINKLOCAL) |
			IPV6_ADDR_LINKLOCAL);
	case IPV6_ADDR_SCOPE_SITELOCAL:
		return (IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_SITELOCAL) |
			IPV6_ADDR_SITELOCAL);
	}
	return IPV6_ADDR_SCOPE_TYPE(scope);
}

int __ipv6_addr_type(const struct in6_addr *addr)
{
	__be32 st;

	st = addr->s6_addr32[0];

	/* Consider all addresses with the first three bits different of
	   000 and 111 as unicasts.
	 */
	if ((st & htonl(0xE0000000)) != htonl(0x00000000) &&
	    (st & htonl(0xE0000000)) != htonl(0xE0000000))
		return (IPV6_ADDR_UNICAST |
			IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_GLOBAL));

	if ((st & htonl(0xFF000000)) == htonl(0xFF000000)) {
		/* multicast */
		/* addr-select 3.1 */
		return (IPV6_ADDR_MULTICAST |
			ipv6_addr_scope2type(IPV6_ADDR_MC_SCOPE(addr)));
	}

	if ((st & htonl(0xFFC00000)) == htonl(0xFE800000))
		return (IPV6_ADDR_LINKLOCAL | IPV6_ADDR_UNICAST |
			IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_LINKLOCAL));		/* addr-select 3.1 */
	if ((st & htonl(0xFFC00000)) == htonl(0xFEC00000))
		return (IPV6_ADDR_SITELOCAL | IPV6_ADDR_UNICAST |
			IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_SITELOCAL));		/* addr-select 3.1 */
	if ((st & htonl(0xFE000000)) == htonl(0xFC000000))
		return (IPV6_ADDR_UNICAST |
			IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_GLOBAL));			/* RFC 4193 */

	if ((addr->s6_addr32[0] | addr->s6_addr32[1]) == 0) {
		if (addr->s6_addr32[2] == 0) {
			if (addr->s6_addr32[3] == 0)
				return IPV6_ADDR_ANY;

			if (addr->s6_addr32[3] == htonl(0x00000001))
				return (IPV6_ADDR_LOOPBACK | IPV6_ADDR_UNICAST |
					IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_LINKLOCAL));	/* addr-select 3.4 */

			return (IPV6_ADDR_COMPATv4 | IPV6_ADDR_UNICAST |
				IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_GLOBAL));	/* addr-select 3.3 */
		}

		if (addr->s6_addr32[2] == htonl(0x0000ffff))
			return (IPV6_ADDR_MAPPED |
				IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_GLOBAL));	/* addr-select 3.3 */
	}

	return (IPV6_ADDR_UNICAST |
		IPV6_ADDR_SCOPE_TYPE(IPV6_ADDR_SCOPE_GLOBAL));	/* addr-select 3.4 */
}
EXPORT_SYMBOL(__ipv6_addr_type);

static ATOMIC_NOTIFIER_HEAD(inet6addr_chain);
static BLOCKING_NOTIFIER_HEAD(inet6addr_validator_chain);

int register_inet6addr_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&inet6addr_chain, nb);
}
EXPORT_SYMBOL(register_inet6addr_notifier);

int unregister_inet6addr_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&inet6addr_chain, nb);
}
EXPORT_SYMBOL(unregister_inet6addr_notifier);

int inet6addr_notifier_call_chain(unsigned long val, void *v)
{
	return atomic_notifier_call_chain(&inet6addr_chain, val, v);
}
EXPORT_SYMBOL(inet6addr_notifier_call_chain);

int register_inet6addr_validator_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&inet6addr_validator_chain, nb);
}
EXPORT_SYMBOL(register_inet6addr_validator_notifier);

int unregister_inet6addr_validator_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&inet6addr_validator_chain,
						  nb);
}
EXPORT_SYMBOL(unregister_inet6addr_validator_notifier);

int inet6addr_validator_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&inet6addr_validator_chain, val, v);
}
EXPORT_SYMBOL(inet6addr_validator_notifier_call_chain);

static int eafnosupport_ipv6_dst_lookup(struct net *net, struct sock *u1,
					struct dst_entry **u2,
					struct flowi6 *u3)
{
	return -EAFNOSUPPORT;
}

static int eafnosupport_ipv6_route_input(struct sk_buff *skb)
{
	return -EAFNOSUPPORT;
}

static struct fib6_table *eafnosupport_fib6_get_table(struct net *net, u32 id)
{
	return NULL;
}

static int
eafnosupport_fib6_table_lookup(struct net *net, struct fib6_table *table,
			       int oif, struct flowi6 *fl6,
			       struct fib6_result *res, int flags)
{
	return -EAFNOSUPPORT;
}

static int
eafnosupport_fib6_lookup(struct net *net, int oif, struct flowi6 *fl6,
			 struct fib6_result *res, int flags)
{
	return -EAFNOSUPPORT;
}

static void
eafnosupport_fib6_select_path(const struct net *net, struct fib6_result *res,
			      struct flowi6 *fl6, int oif, bool have_oif_match,
			      const struct sk_buff *skb, int strict)
{
}

static u32
eafnosupport_ip6_mtu_from_fib6(const struct fib6_result *res,
			       const struct in6_addr *daddr,
			       const struct in6_addr *saddr)
{
	return 0;
}

static int eafnosupport_fib6_nh_init(struct net *net, struct fib6_nh *fib6_nh,
				     struct fib6_config *cfg, gfp_t gfp_flags,
				     struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG(extack, "IPv6 support not enabled in kernel");
	return -EAFNOSUPPORT;
}

const struct ipv6_stub *ipv6_stub __read_mostly = &(struct ipv6_stub) {
	.ipv6_dst_lookup   = eafnosupport_ipv6_dst_lookup,
	.ipv6_route_input  = eafnosupport_ipv6_route_input,
	.fib6_get_table    = eafnosupport_fib6_get_table,
	.fib6_table_lookup = eafnosupport_fib6_table_lookup,
	.fib6_lookup       = eafnosupport_fib6_lookup,
	.fib6_select_path  = eafnosupport_fib6_select_path,
	.ip6_mtu_from_fib6 = eafnosupport_ip6_mtu_from_fib6,
	.fib6_nh_init	   = eafnosupport_fib6_nh_init,
};
EXPORT_SYMBOL_GPL(ipv6_stub);

/* IPv6 Wildcard Address and Loopback Address defined by RFC2553 */
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
EXPORT_SYMBOL(in6addr_loopback);
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
EXPORT_SYMBOL(in6addr_any);
const struct in6_addr in6addr_linklocal_allnodes = IN6ADDR_LINKLOCAL_ALLNODES_INIT;
EXPORT_SYMBOL(in6addr_linklocal_allnodes);
const struct in6_addr in6addr_linklocal_allrouters = IN6ADDR_LINKLOCAL_ALLROUTERS_INIT;
EXPORT_SYMBOL(in6addr_linklocal_allrouters);
const struct in6_addr in6addr_interfacelocal_allnodes = IN6ADDR_INTERFACELOCAL_ALLNODES_INIT;
EXPORT_SYMBOL(in6addr_interfacelocal_allnodes);
const struct in6_addr in6addr_interfacelocal_allrouters = IN6ADDR_INTERFACELOCAL_ALLROUTERS_INIT;
EXPORT_SYMBOL(in6addr_interfacelocal_allrouters);
const struct in6_addr in6addr_sitelocal_allrouters = IN6ADDR_SITELOCAL_ALLROUTERS_INIT;
EXPORT_SYMBOL(in6addr_sitelocal_allrouters);

static void snmp6_free_dev(struct inet6_dev *idev)
{
	kfree(idev->stats.icmpv6msgdev);
	kfree(idev->stats.icmpv6dev);
	free_percpu(idev->stats.ipv6);
}

static void in6_dev_finish_destroy_rcu(struct rcu_head *head)
{
	struct inet6_dev *idev = container_of(head, struct inet6_dev, rcu);

	snmp6_free_dev(idev);
	kfree(idev);
}

/* Nobody refers to this device, we may destroy it. */

void in6_dev_finish_destroy(struct inet6_dev *idev)
{
	struct net_device *dev = idev->dev;

	WARN_ON(!list_empty(&idev->addr_list));
	WARN_ON(idev->mc_list);
	WARN_ON(timer_pending(&idev->rs_timer));

#ifdef NET_REFCNT_DEBUG
	pr_debug("%s: %s\n", __func__, dev ? dev->name : "NIL");
#endif
	dev_put(dev);
	if (!idev->dead) {
		pr_warn("Freeing alive inet6 device %p\n", idev);
		return;
	}
	call_rcu(&idev->rcu, in6_dev_finish_destroy_rcu);
}
EXPORT_SYMBOL(in6_dev_finish_destroy);
