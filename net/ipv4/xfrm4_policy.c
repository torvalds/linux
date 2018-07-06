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
#include <linux/if_tunnel.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <net/ip.h>
#include <net/l3mdev.h>

static struct xfrm_policy_afinfo xfrm4_policy_afinfo;

static struct dst_entry *__xfrm4_dst_lookup(struct net *net, struct flowi4 *fl4,
					    int tos, int oif,
					    const xfrm_address_t *saddr,
					    const xfrm_address_t *daddr,
					    u32 mark)
{
	struct rtable *rt;

	memset(fl4, 0, sizeof(*fl4));
	fl4->daddr = daddr->a4;
	fl4->flowi4_tos = tos;
	fl4->flowi4_oif = l3mdev_master_ifindex_by_index(net, oif);
	fl4->flowi4_mark = mark;
	if (saddr)
		fl4->saddr = saddr->a4;

	fl4->flowi4_flags = FLOWI_FLAG_SKIP_NH_OIF;

	rt = __ip_route_output_key(net, fl4);
	if (!IS_ERR(rt))
		return &rt->dst;

	return ERR_CAST(rt);
}

static struct dst_entry *xfrm4_dst_lookup(struct net *net, int tos, int oif,
					  const xfrm_address_t *saddr,
					  const xfrm_address_t *daddr,
					  u32 mark)
{
	struct flowi4 fl4;

	return __xfrm4_dst_lookup(net, &fl4, tos, oif, saddr, daddr, mark);
}

static int xfrm4_get_saddr(struct net *net, int oif,
			   xfrm_address_t *saddr, xfrm_address_t *daddr,
			   u32 mark)
{
	struct dst_entry *dst;
	struct flowi4 fl4;

	dst = __xfrm4_dst_lookup(net, &fl4, 0, oif, NULL, daddr, mark);
	if (IS_ERR(dst))
		return -EHOSTUNREACH;

	saddr->a4 = fl4.saddr;
	dst_release(dst);
	return 0;
}

static int xfrm4_get_tos(const struct flowi *fl)
{
	return IPTOS_RT_MASK & fl->u.ip4.flowi4_tos; /* Strip ECN bits */
}

static int xfrm4_init_path(struct xfrm_dst *path, struct dst_entry *dst,
			   int nfheader_len)
{
	return 0;
}

static int xfrm4_fill_dst(struct xfrm_dst *xdst, struct net_device *dev,
			  const struct flowi *fl)
{
	struct rtable *rt = (struct rtable *)xdst->route;
	const struct flowi4 *fl4 = &fl->u.ip4;

	xdst->u.rt.rt_iif = fl4->flowi4_iif;

	xdst->u.dst.dev = dev;
	dev_hold(dev);

	/* Sheit... I remember I did this right. Apparently,
	 * it was magically lost, so this code needs audit */
	xdst->u.rt.rt_is_input = rt->rt_is_input;
	xdst->u.rt.rt_flags = rt->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST |
					      RTCF_LOCAL);
	xdst->u.rt.rt_type = rt->rt_type;
	xdst->u.rt.rt_gateway = rt->rt_gateway;
	xdst->u.rt.rt_uses_gateway = rt->rt_uses_gateway;
	xdst->u.rt.rt_pmtu = rt->rt_pmtu;
	xdst->u.rt.rt_mtu_locked = rt->rt_mtu_locked;
	xdst->u.rt.rt_table_id = rt->rt_table_id;
	INIT_LIST_HEAD(&xdst->u.rt.rt_uncached);

	return 0;
}

static void
_decode_session4(struct sk_buff *skb, struct flowi *fl, int reverse)
{
	const struct iphdr *iph = ip_hdr(skb);
	u8 *xprth = skb_network_header(skb) + iph->ihl * 4;
	struct flowi4 *fl4 = &fl->u.ip4;
	int oif = 0;

	if (skb_dst(skb))
		oif = l3mdev_fib_oif(skb_dst(skb)->dev);

	memset(fl4, 0, sizeof(struct flowi4));
	fl4->flowi4_mark = skb->mark;
	fl4->flowi4_oif = reverse ? skb->skb_iif : oif;

	if (!ip_is_fragment(iph)) {
		switch (iph->protocol) {
		case IPPROTO_UDP:
		case IPPROTO_UDPLITE:
		case IPPROTO_TCP:
		case IPPROTO_SCTP:
		case IPPROTO_DCCP:
			if (xprth + 4 < skb->data ||
			    pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be16 *ports;

				xprth = skb_network_header(skb) + iph->ihl * 4;
				ports = (__be16 *)xprth;

				fl4->fl4_sport = ports[!!reverse];
				fl4->fl4_dport = ports[!reverse];
			}
			break;

		case IPPROTO_ICMP:
			if (xprth + 2 < skb->data ||
			    pskb_may_pull(skb, xprth + 2 - skb->data)) {
				u8 *icmp;

				xprth = skb_network_header(skb) + iph->ihl * 4;
				icmp = xprth;

				fl4->fl4_icmp_type = icmp[0];
				fl4->fl4_icmp_code = icmp[1];
			}
			break;

		case IPPROTO_ESP:
			if (xprth + 4 < skb->data ||
			    pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be32 *ehdr;

				xprth = skb_network_header(skb) + iph->ihl * 4;
				ehdr = (__be32 *)xprth;

				fl4->fl4_ipsec_spi = ehdr[0];
			}
			break;

		case IPPROTO_AH:
			if (xprth + 8 < skb->data ||
			    pskb_may_pull(skb, xprth + 8 - skb->data)) {
				__be32 *ah_hdr;

				xprth = skb_network_header(skb) + iph->ihl * 4;
				ah_hdr = (__be32 *)xprth;

				fl4->fl4_ipsec_spi = ah_hdr[1];
			}
			break;

		case IPPROTO_COMP:
			if (xprth + 4 < skb->data ||
			    pskb_may_pull(skb, xprth + 4 - skb->data)) {
				__be16 *ipcomp_hdr;

				xprth = skb_network_header(skb) + iph->ihl * 4;
				ipcomp_hdr = (__be16 *)xprth;

				fl4->fl4_ipsec_spi = htonl(ntohs(ipcomp_hdr[1]));
			}
			break;

		case IPPROTO_GRE:
			if (xprth + 12 < skb->data ||
			    pskb_may_pull(skb, xprth + 12 - skb->data)) {
				__be16 *greflags;
				__be32 *gre_hdr;

				xprth = skb_network_header(skb) + iph->ihl * 4;
				greflags = (__be16 *)xprth;
				gre_hdr = (__be32 *)xprth;

				if (greflags[0] & GRE_KEY) {
					if (greflags[0] & GRE_CSUM)
						gre_hdr++;
					fl4->fl4_gre_key = gre_hdr[1];
				}
			}
			break;

		default:
			fl4->fl4_ipsec_spi = 0;
			break;
		}
	}
	fl4->flowi4_proto = iph->protocol;
	fl4->daddr = reverse ? iph->saddr : iph->daddr;
	fl4->saddr = reverse ? iph->daddr : iph->saddr;
	fl4->flowi4_tos = iph->tos;
}

static inline int xfrm4_garbage_collect(struct dst_ops *ops)
{
	struct net *net = container_of(ops, struct net, xfrm.xfrm4_dst_ops);

	xfrm4_policy_afinfo.garbage_collect(net);
	return (dst_entries_get_slow(ops) > ops->gc_thresh * 2);
}

static void xfrm4_update_pmtu(struct dst_entry *dst, struct sock *sk,
			      struct sk_buff *skb, u32 mtu)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct dst_entry *path = xdst->route;

	path->ops->update_pmtu(path, sk, skb, mtu);
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

	xfrm_dst_destroy(xdst);
}

static void xfrm4_dst_ifdown(struct dst_entry *dst, struct net_device *dev,
			     int unregister)
{
	if (!unregister)
		return;

	xfrm_dst_ifdown(dst, dev);
}

static struct dst_ops xfrm4_dst_ops_template = {
	.family =		AF_INET,
	.gc =			xfrm4_garbage_collect,
	.update_pmtu =		xfrm4_update_pmtu,
	.redirect =		xfrm4_redirect,
	.cow_metrics =		dst_cow_metrics_generic,
	.destroy =		xfrm4_dst_destroy,
	.ifdown =		xfrm4_dst_ifdown,
	.local_out =		__ip_local_out,
	.gc_thresh =		INT_MAX,
};

static struct xfrm_policy_afinfo xfrm4_policy_afinfo = {
	.family = 		AF_INET,
	.dst_ops =		&xfrm4_dst_ops_template,
	.dst_lookup =		xfrm4_dst_lookup,
	.get_saddr =		xfrm4_get_saddr,
	.decode_session =	_decode_session4,
	.get_tos =		xfrm4_get_tos,
	.init_path =		xfrm4_init_path,
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
	{ }
};

static int __net_init xfrm4_net_sysctl_init(struct net *net)
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

	hdr = register_net_sysctl(net, "net/ipv4", table);
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

static void __net_exit xfrm4_net_sysctl_exit(struct net *net)
{
	struct ctl_table *table;

	if (!net->ipv4.xfrm4_hdr)
		return;

	table = net->ipv4.xfrm4_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->ipv4.xfrm4_hdr);
	if (!net_eq(net, &init_net))
		kfree(table);
}
#else /* CONFIG_SYSCTL */
static int inline xfrm4_net_sysctl_init(struct net *net)
{
	return 0;
}

static void inline xfrm4_net_sysctl_exit(struct net *net)
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
	xfrm_policy_register_afinfo(&xfrm4_policy_afinfo);
}

void __init xfrm4_init(void)
{
	xfrm4_state_init();
	xfrm4_policy_init();
	xfrm4_protocol_init();
	register_pernet_subsys(&xfrm4_net_ops);
}

