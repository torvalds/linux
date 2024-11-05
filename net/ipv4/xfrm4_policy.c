// SPDX-License-Identifier: GPL-2.0
/*
 * xfrm4_policy.c
 *
 * Changes:
 *	Kazunori MIYAZAWA @USAGI
 * 	YOSHIFUJI Hideaki @USAGI
 *		Split up af-specific portion
 *
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/inetdevice.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <net/l3mdev.h>

static struct dst_entry *__xfrm4_dst_lookup(struct flowi4 *fl4,
					    const struct xfrm_dst_lookup_params *params)
{
	struct rtable *rt;

	memset(fl4, 0, sizeof(*fl4));
	fl4->daddr = params->daddr->a4;
	fl4->flowi4_tos = params->tos;
	fl4->flowi4_l3mdev = l3mdev_master_ifindex_by_index(params->net,
							    params->oif);
	fl4->flowi4_mark = params->mark;
	if (params->saddr)
		fl4->saddr = params->saddr->a4;
	fl4->flowi4_proto = params->ipproto;
	fl4->uli = params->uli;

	rt = __ip_route_output_key(params->net, fl4);
	if (!IS_ERR(rt))
		return &rt->dst;

	return ERR_CAST(rt);
}

static struct dst_entry *xfrm4_dst_lookup(const struct xfrm_dst_lookup_params *params)
{
	struct flowi4 fl4;

	return __xfrm4_dst_lookup(&fl4, params);
}

static int xfrm4_get_saddr(xfrm_address_t *saddr,
			   const struct xfrm_dst_lookup_params *params)
{
	struct dst_entry *dst;
	struct flowi4 fl4;

	dst = __xfrm4_dst_lookup(&fl4, params);
	if (IS_ERR(dst))
		return -EHOSTUNREACH;

	saddr->a4 = fl4.saddr;
	dst_release(dst);
	return 0;
}

static int xfrm4_fill_dst(struct xfrm_dst *xdst, struct net_device *dev,
			  const struct flowi *fl)
{
	struct rtable *rt = dst_rtable(xdst->route);
	const struct flowi4 *fl4 = &fl->u.ip4;

	xdst->u.rt.rt_iif = fl4->flowi4_iif;

	xdst->u.dst.dev = dev;
	netdev_hold(dev, &xdst->u.dst.dev_tracker, GFP_ATOMIC);

	/* Sheit... I remember I did this right. Apparently,
	 * it was magically lost, so this code needs audit */
	xdst->u.rt.rt_is_input = rt->rt_is_input;
	xdst->u.rt.rt_flags = rt->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST |
					      RTCF_LOCAL);
	xdst->u.rt.rt_type = rt->rt_type;
	xdst->u.rt.rt_uses_gateway = rt->rt_uses_gateway;
	xdst->u.rt.rt_gw_family = rt->rt_gw_family;
	if (rt->rt_gw_family == AF_INET)
		xdst->u.rt.rt_gw4 = rt->rt_gw4;
	else if (rt->rt_gw_family == AF_INET6)
		xdst->u.rt.rt_gw6 = rt->rt_gw6;
	xdst->u.rt.rt_pmtu = rt->rt_pmtu;
	xdst->u.rt.rt_mtu_locked = rt->rt_mtu_locked;
	rt_add_uncached_list(&xdst->u.rt);

	return 0;
}

static void xfrm4_update_pmtu(struct dst_entry *dst, struct sock *sk,
			      struct sk_buff *skb, u32 mtu,
			      bool confirm_neigh)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct dst_entry *path = xdst->route;

	path->ops->update_pmtu(path, sk, skb, mtu, confirm_neigh);
}

static void xfrm4_redirect(struct dst_entry *dst, struct sock *sk,
			   struct sk_buff *skb)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct dst_entry *path = xdst->route;

	path->ops->redirect(path, sk, skb);
}

static void xfrm4_dst_destroy(struct dst_entry *dst)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;

	dst_destroy_metrics_generic(dst);
	rt_del_uncached_list(&xdst->u.rt);
	xfrm_dst_destroy(xdst);
}

static struct dst_ops xfrm4_dst_ops_template = {
	.family =		AF_INET,
	.update_pmtu =		xfrm4_update_pmtu,
	.redirect =		xfrm4_redirect,
	.cow_metrics =		dst_cow_metrics_generic,
	.destroy =		xfrm4_dst_destroy,
	.ifdown =		xfrm_dst_ifdown,
	.local_out =		__ip_local_out,
	.gc_thresh =		32768,
};

static const struct xfrm_policy_afinfo xfrm4_policy_afinfo = {
	.dst_ops =		&xfrm4_dst_ops_template,
	.dst_lookup =		xfrm4_dst_lookup,
	.get_saddr =		xfrm4_get_saddr,
	.fill_dst =		xfrm4_fill_dst,
	.blackhole_route =	ipv4_blackhole_route,
};

#ifdef CONFIG_SYSCTL
static struct ctl_table xfrm4_policy_table[] = {
	{
		.procname       = "xfrm4_gc_thresh",
		.data           = &init_net.xfrm.xfrm4_dst_ops.gc_thresh,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
	},
};

static __net_init int xfrm4_net_sysctl_init(struct net *net)
{
	struct ctl_table *table;
	struct ctl_table_header *hdr;

	table = xfrm4_policy_table;
	if (!net_eq(net, &init_net)) {
		table = kmemdup(table, sizeof(xfrm4_policy_table), GFP_KERNEL);
		if (!table)
			goto err_alloc;

		table[0].data = &net->xfrm.xfrm4_dst_ops.gc_thresh;
	}

	hdr = register_net_sysctl_sz(net, "net/ipv4", table,
				     ARRAY_SIZE(xfrm4_policy_table));
	if (!hdr)
		goto err_reg;

	net->ipv4.xfrm4_hdr = hdr;
	return 0;

err_reg:
	if (!net_eq(net, &init_net))
		kfree(table);
err_alloc:
	return -ENOMEM;
}

static __net_exit void xfrm4_net_sysctl_exit(struct net *net)
{
	const struct ctl_table *table;

	if (!net->ipv4.xfrm4_hdr)
		return;

	table = net->ipv4.xfrm4_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv4.xfrm4_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}
#else /* CONFIG_SYSCTL */
static inline int xfrm4_net_sysctl_init(struct net *net)
{
	return 0;
}

static inline void xfrm4_net_sysctl_exit(struct net *net)
{
}
#endif

static int __net_init xfrm4_net_init(struct net *net)
{
	int ret;

	memcpy(&net->xfrm.xfrm4_dst_ops, &xfrm4_dst_ops_template,
	       sizeof(xfrm4_dst_ops_template));
	ret = dst_entries_init(&net->xfrm.xfrm4_dst_ops);
	if (ret)
		return ret;

	ret = xfrm4_net_sysctl_init(net);
	if (ret)
		dst_entries_destroy(&net->xfrm.xfrm4_dst_ops);

	return ret;
}

static void __net_exit xfrm4_net_exit(struct net *net)
{
	xfrm4_net_sysctl_exit(net);
	dst_entries_destroy(&net->xfrm.xfrm4_dst_ops);
}

static struct pernet_operations __net_initdata xfrm4_net_ops = {
	.init	= xfrm4_net_init,
	.exit	= xfrm4_net_exit,
};

static void __init xfrm4_policy_init(void)
{
	xfrm_policy_register_afinfo(&xfrm4_policy_afinfo, AF_INET);
}

void __init xfrm4_init(void)
{
	xfrm4_state_init();
	xfrm4_policy_init();
	xfrm4_protocol_init();
	register_pernet_subsys(&xfrm4_net_ops);
}
