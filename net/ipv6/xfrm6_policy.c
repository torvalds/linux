// SPDX-License-Identifier: GPL-2.0
/*
 * xfrm6_policy.c: based on xfrm4_policy.c
 *
 * Authors:
 *	Mitsuru KANDA @USAGI
 *	Kazunori MIYAZAWA @USAGI
 *	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 *		IPv6 support
 *	YOSHIFUJI Hideaki
 *		Split up af-specific portion
 *
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <net/addrconf.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/l3mdev.h>

static struct dst_entry *xfrm6_dst_lookup(const struct xfrm_dst_lookup_params *params)
{
	struct flowi6 fl6;
	struct dst_entry *dst;
	int err;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_l3mdev = l3mdev_master_ifindex_by_index(params->net,
							   params->oif);
	fl6.flowi6_mark = params->mark;
	memcpy(&fl6.daddr, params->daddr, sizeof(fl6.daddr));
	if (params->saddr)
		memcpy(&fl6.saddr, params->saddr, sizeof(fl6.saddr));

	fl6.flowi4_proto = params->ipproto;
	fl6.uli = params->uli;

	dst = ip6_route_output(params->net, NULL, &fl6);

	err = dst->error;
	if (dst->error) {
		dst_release(dst);
		dst = ERR_PTR(err);
	}

	return dst;
}

static int xfrm6_get_saddr(xfrm_address_t *saddr,
			   const struct xfrm_dst_lookup_params *params)
{
	struct dst_entry *dst;
	struct net_device *dev;
	struct inet6_dev *idev;

	dst = xfrm6_dst_lookup(params);
	if (IS_ERR(dst))
		return -EHOSTUNREACH;

	idev = ip6_dst_idev(dst);
	if (!idev) {
		dst_release(dst);
		return -EHOSTUNREACH;
	}
	dev = idev->dev;
	ipv6_dev_get_saddr(dev_net(dev), dev, &params->daddr->in6, 0,
			   &saddr->in6);
	dst_release(dst);
	return 0;
}

static int xfrm6_fill_dst(struct xfrm_dst *xdst, struct net_device *dev,
			  const struct flowi *fl)
{
	struct rt6_info *rt = dst_rt6_info(xdst->route);

	xdst->u.dst.dev = dev;
	netdev_hold(dev, &xdst->u.dst.dev_tracker, GFP_ATOMIC);

	xdst->u.rt6.rt6i_idev = in6_dev_get(dev);
	if (!xdst->u.rt6.rt6i_idev) {
		netdev_put(dev, &xdst->u.dst.dev_tracker);
		return -ENODEV;
	}

	/* Sheit... I remember I did this right. Apparently,
	 * it was magically lost, so this code needs audit */
	xdst->u.rt6.rt6i_flags = rt->rt6i_flags & (RTF_ANYCAST |
						   RTF_LOCAL);
	xdst->route_cookie = rt6_get_cookie(rt);
	xdst->u.rt6.rt6i_gateway = rt->rt6i_gateway;
	xdst->u.rt6.rt6i_dst = rt->rt6i_dst;
	xdst->u.rt6.rt6i_src = rt->rt6i_src;
	rt6_uncached_list_add(&xdst->u.rt6);

	return 0;
}

static void xfrm6_update_pmtu(struct dst_entry *dst, struct sock *sk,
			      struct sk_buff *skb, u32 mtu,
			      bool confirm_neigh)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct dst_entry *path = xdst->route;

	path->ops->update_pmtu(path, sk, skb, mtu, confirm_neigh);
}

static void xfrm6_redirect(struct dst_entry *dst, struct sock *sk,
			   struct sk_buff *skb)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct dst_entry *path = xdst->route;

	path->ops->redirect(path, sk, skb);
}

static void xfrm6_dst_destroy(struct dst_entry *dst)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;

	dst_destroy_metrics_generic(dst);
	rt6_uncached_list_del(&xdst->u.rt6);
	if (likely(xdst->u.rt6.rt6i_idev))
		in6_dev_put(xdst->u.rt6.rt6i_idev);
	xfrm_dst_destroy(xdst);
}

static void xfrm6_dst_ifdown(struct dst_entry *dst, struct net_device *dev)
{
	struct xfrm_dst *xdst;

	xdst = (struct xfrm_dst *)dst;
	if (xdst->u.rt6.rt6i_idev->dev == dev) {
		struct inet6_dev *loopback_idev =
			in6_dev_get(dev_net(dev)->loopback_dev);

		do {
			in6_dev_put(xdst->u.rt6.rt6i_idev);
			xdst->u.rt6.rt6i_idev = loopback_idev;
			in6_dev_hold(loopback_idev);
			xdst = (struct xfrm_dst *)xfrm_dst_child(&xdst->u.dst);
		} while (xdst->u.dst.xfrm);

		__in6_dev_put(loopback_idev);
	}

	xfrm_dst_ifdown(dst, dev);
}

static struct dst_ops xfrm6_dst_ops_template = {
	.family =		AF_INET6,
	.update_pmtu =		xfrm6_update_pmtu,
	.redirect =		xfrm6_redirect,
	.cow_metrics =		dst_cow_metrics_generic,
	.destroy =		xfrm6_dst_destroy,
	.ifdown =		xfrm6_dst_ifdown,
	.local_out =		__ip6_local_out,
	.gc_thresh =		32768,
};

static const struct xfrm_policy_afinfo xfrm6_policy_afinfo = {
	.dst_ops =		&xfrm6_dst_ops_template,
	.dst_lookup =		xfrm6_dst_lookup,
	.get_saddr =		xfrm6_get_saddr,
	.fill_dst =		xfrm6_fill_dst,
	.blackhole_route =	ip6_blackhole_route,
};

static int __init xfrm6_policy_init(void)
{
	return xfrm_policy_register_afinfo(&xfrm6_policy_afinfo, AF_INET6);
}

static void xfrm6_policy_fini(void)
{
	xfrm_policy_unregister_afinfo(&xfrm6_policy_afinfo);
}

#ifdef CONFIG_SYSCTL
static struct ctl_table xfrm6_policy_table[] = {
	{
		.procname       = "xfrm6_gc_thresh",
		.data		= &init_net.xfrm.xfrm6_dst_ops.gc_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler   = proc_dointvec,
	},
};

static int __net_init xfrm6_net_sysctl_init(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = xfrm6_policy_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(xfrm6_policy_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;

		table[0].data = &net->xfrm.xfrm6_dst_ops.gc_thresh;
	}

	hdr = register_net_sysctl_sz(net, "net/ipv6", table,
				     ARRAY_SIZE(xfrm6_policy_table));
	if (!hdr)
		goto err_reg;

	net->ipv6.sysctl.xfrm6_hdr = hdr;
	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static void __net_exit xfrm6_net_sysctl_exit(struct net *net)
{
	const struct ctl_table *table;

	if (!net->ipv6.sysctl.xfrm6_hdr)
		return;

	table = net->ipv6.sysctl.xfrm6_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv6.sysctl.xfrm6_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}
#else /* CONFIG_SYSCTL */
static inline int xfrm6_net_sysctl_init(struct net *net)
{
	return 0;
}

static inline void xfrm6_net_sysctl_exit(struct net *net)
{
}
#endif

static int __net_init xfrm6_net_init(struct net *net)
{
	int ret;

	memcpy(&net->xfrm.xfrm6_dst_ops, &xfrm6_dst_ops_template,
	       sizeof(xfrm6_dst_ops_template));
	ret = dst_entries_init(&net->xfrm.xfrm6_dst_ops);
	if (ret)
		return ret;

	ret = xfrm6_net_sysctl_init(net);
	if (ret)
		dst_entries_destroy(&net->xfrm.xfrm6_dst_ops);

	return ret;
}

static void __net_exit xfrm6_net_exit(struct net *net)
{
	xfrm6_net_sysctl_exit(net);
	dst_entries_destroy(&net->xfrm.xfrm6_dst_ops);
}

static struct pernet_operations xfrm6_net_ops = {
	.init	= xfrm6_net_init,
	.exit	= xfrm6_net_exit,
};

int __init xfrm6_init(void)
{
	int ret;

	ret = xfrm6_policy_init();
	if (ret)
		goto out;
	ret = xfrm6_state_init();
	if (ret)
		goto out_policy;

	ret = xfrm6_protocol_init();
	if (ret)
		goto out_state;

	ret = register_pernet_subsys(&xfrm6_net_ops);
	if (ret)
		goto out_protocol;

	ret = xfrm_nat_keepalive_init(AF_INET6);
	if (ret)
		goto out_nat_keepalive;
out:
	return ret;
out_nat_keepalive:
	unregister_pernet_subsys(&xfrm6_net_ops);
out_protocol:
	xfrm6_protocol_fini();
out_state:
	xfrm6_state_fini();
out_policy:
	xfrm6_policy_fini();
	goto out;
}

void xfrm6_fini(void)
{
	xfrm_nat_keepalive_fini(AF_INET6);
	unregister_pernet_subsys(&xfrm6_net_ops);
	xfrm6_protocol_fini();
	xfrm6_policy_fini();
	xfrm6_state_fini();
}
