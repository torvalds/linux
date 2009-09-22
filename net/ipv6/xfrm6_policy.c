/*
 * xfrm6_policy.c: based on xfrm4_policy.c
 *
 * Authors:
 *	Mitsuru KANDA @USAGI
 * 	Kazunori MIYAZAWA @USAGI
 * 	Kunihiro Ishiguro <kunihiro@ipinfusion.com>
 * 		IPv6 support
 * 	YOSHIFUJI Hideaki
 * 		Split up af-specific portion
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
#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
#include <net/mip6.h>
#endif

static struct dst_ops xfrm6_dst_ops;
static struct xfrm_policy_afinfo xfrm6_policy_afinfo;

static struct dst_entry *xfrm6_dst_lookup(struct net *net, int tos,
					  xfrm_address_t *saddr,
					  xfrm_address_t *daddr)
{
	struct flowi fl = {};
	struct dst_entry *dst;
	int err;

	memcpy(&fl.fl6_dst, daddr, sizeof(fl.fl6_dst));
	if (saddr)
		memcpy(&fl.fl6_src, saddr, sizeof(fl.fl6_src));

	dst = ip6_route_output(net, NULL, &fl);

	err = dst->error;
	if (dst->error) {
		dst_release(dst);
		dst = ERR_PTR(err);
	}

	return dst;
}

static int xfrm6_get_saddr(struct net *net,
			   xfrm_address_t *saddr, xfrm_address_t *daddr)
{
	struct dst_entry *dst;
	struct net_device *dev;

	dst = xfrm6_dst_lookup(net, 0, NULL, daddr);
	if (IS_ERR(dst))
		return -EHOSTUNREACH;

	dev = ip6_dst_idev(dst)->dev;
	ipv6_dev_get_saddr(dev_net(dev), dev,
			   (struct in6_addr *)&daddr->a6, 0,
			   (struct in6_addr *)&saddr->a6);
	dst_release(dst);
	return 0;
}

static struct dst_entry *
__xfrm6_find_bundle(struct flowi *fl, struct xfrm_policy *policy)
{
	struct dst_entry *dst;

	/* Still not clear if we should set fl->fl6_{src,dst}... */
	read_lock_bh(&policy->lock);
	for (dst = policy->bundles; dst; dst = dst->next) {
		struct xfrm_dst *xdst = (struct xfrm_dst*)dst;
		struct in6_addr fl_dst_prefix, fl_src_prefix;

		ipv6_addr_prefix(&fl_dst_prefix,
				 &fl->fl6_dst,
				 xdst->u.rt6.rt6i_dst.plen);
		ipv6_addr_prefix(&fl_src_prefix,
				 &fl->fl6_src,
				 xdst->u.rt6.rt6i_src.plen);
		if (ipv6_addr_equal(&xdst->u.rt6.rt6i_dst.addr, &fl_dst_prefix) &&
		    ipv6_addr_equal(&xdst->u.rt6.rt6i_src.addr, &fl_src_prefix) &&
		    xfrm_bundle_ok(policy, xdst, fl, AF_INET6,
				   (xdst->u.rt6.rt6i_dst.plen != 128 ||
				    xdst->u.rt6.rt6i_src.plen != 128))) {
			dst_clone(dst);
			break;
		}
	}
	read_unlock_bh(&policy->lock);
	return dst;
}

static int xfrm6_get_tos(struct flowi *fl)
{
	return 0;
}

static int xfrm6_init_path(struct xfrm_dst *path, struct dst_entry *dst,
			   int nfheader_len)
{
	if (dst->ops->family == AF_INET6) {
		struct rt6_info *rt = (struct rt6_info*)dst;
		if (rt->rt6i_node)
			path->path_cookie = rt->rt6i_node->fn_sernum;
	}

	path->u.rt6.rt6i_nfheader_len = nfheader_len;

	return 0;
}

static int xfrm6_fill_dst(struct xfrm_dst *xdst, struct net_device *dev)
{
	struct rt6_info *rt = (struct rt6_info*)xdst->route;

	xdst->u.dst.dev = dev;
	dev_hold(dev);

	xdst->u.rt6.rt6i_idev = in6_dev_get(rt->u.dst.dev);
	if (!xdst->u.rt6.rt6i_idev)
		return -ENODEV;

	/* Sheit... I remember I did this right. Apparently,
	 * it was magically lost, so this code needs audit */
	xdst->u.rt6.rt6i_flags = rt->rt6i_flags & (RTF_ANYCAST |
						   RTF_LOCAL);
	xdst->u.rt6.rt6i_metric = rt->rt6i_metric;
	xdst->u.rt6.rt6i_node = rt->rt6i_node;
	if (rt->rt6i_node)
		xdst->route_cookie = rt->rt6i_node->fn_sernum;
	xdst->u.rt6.rt6i_gateway = rt->rt6i_gateway;
	xdst->u.rt6.rt6i_dst = rt->rt6i_dst;
	xdst->u.rt6.rt6i_src = rt->rt6i_src;

	return 0;
}

static inline void
_decode_session6(struct sk_buff *skb, struct flowi *fl, int reverse)
{
	int onlyproto = 0;
	u16 offset = skb_network_header_len(skb);
	struct ipv6hdr *hdr = ipv6_hdr(skb);
	struct ipv6_opt_hdr *exthdr;
	const unsigned char *nh = skb_network_header(skb);
	u8 nexthdr = nh[IP6CB(skb)->nhoff];

	memset(fl, 0, sizeof(struct flowi));
	ipv6_addr_copy(&fl->fl6_dst, reverse ? &hdr->saddr : &hdr->daddr);
	ipv6_addr_copy(&fl->fl6_src, reverse ? &hdr->daddr : &hdr->saddr);

	while (nh + offset + 1 < skb->data ||
	       pskb_may_pull(skb, nh + offset + 1 - skb->data)) {
		nh = skb_network_header(skb);
		exthdr = (struct ipv6_opt_hdr *)(nh + offset);

		switch (nexthdr) {
		case NEXTHDR_FRAGMENT:
			onlyproto = 1;
		case NEXTHDR_ROUTING:
		case NEXTHDR_HOP:
		case NEXTHDR_DEST:
			offset += ipv6_optlen(exthdr);
			nexthdr = exthdr->nexthdr;
			exthdr = (struct ipv6_opt_hdr *)(nh + offset);
			break;

		case IPPROTO_UDP:
		case IPPROTO_UDPLITE:
		case IPPROTO_TCP:
		case IPPROTO_SCTP:
		case IPPROTO_DCCP:
			if (!onlyproto && (nh + offset + 4 < skb->data ||
			     pskb_may_pull(skb, nh + offset + 4 - skb->data))) {
				__be16 *ports = (__be16 *)exthdr;

				fl->fl_ip_sport = ports[!!reverse];
				fl->fl_ip_dport = ports[!reverse];
			}
			fl->proto = nexthdr;
			return;

		case IPPROTO_ICMPV6:
			if (!onlyproto && pskb_may_pull(skb, nh + offset + 2 - skb->data)) {
				u8 *icmp = (u8 *)exthdr;

				fl->fl_icmp_type = icmp[0];
				fl->fl_icmp_code = icmp[1];
			}
			fl->proto = nexthdr;
			return;

#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
		case IPPROTO_MH:
			if (!onlyproto && pskb_may_pull(skb, nh + offset + 3 - skb->data)) {
				struct ip6_mh *mh;
				mh = (struct ip6_mh *)exthdr;

				fl->fl_mh_type = mh->ip6mh_type;
			}
			fl->proto = nexthdr;
			return;
#endif

		/* XXX Why are there these headers? */
		case IPPROTO_AH:
		case IPPROTO_ESP:
		case IPPROTO_COMP:
		default:
			fl->fl_ipsec_spi = 0;
			fl->proto = nexthdr;
			return;
		}
	}
}

static inline int xfrm6_garbage_collect(struct dst_ops *ops)
{
	xfrm6_policy_afinfo.garbage_collect(&init_net);
	return (atomic_read(&xfrm6_dst_ops.entries) > xfrm6_dst_ops.gc_thresh*2);
}

static void xfrm6_update_pmtu(struct dst_entry *dst, u32 mtu)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;
	struct dst_entry *path = xdst->route;

	path->ops->update_pmtu(path, mtu);
}

static void xfrm6_dst_destroy(struct dst_entry *dst)
{
	struct xfrm_dst *xdst = (struct xfrm_dst *)dst;

	if (likely(xdst->u.rt6.rt6i_idev))
		in6_dev_put(xdst->u.rt6.rt6i_idev);
	xfrm_dst_destroy(xdst);
}

static void xfrm6_dst_ifdown(struct dst_entry *dst, struct net_device *dev,
			     int unregister)
{
	struct xfrm_dst *xdst;

	if (!unregister)
		return;

	xdst = (struct xfrm_dst *)dst;
	if (xdst->u.rt6.rt6i_idev->dev == dev) {
		struct inet6_dev *loopback_idev =
			in6_dev_get(dev_net(dev)->loopback_dev);
		BUG_ON(!loopback_idev);

		do {
			in6_dev_put(xdst->u.rt6.rt6i_idev);
			xdst->u.rt6.rt6i_idev = loopback_idev;
			in6_dev_hold(loopback_idev);
			xdst = (struct xfrm_dst *)xdst->u.dst.child;
		} while (xdst->u.dst.xfrm);

		__in6_dev_put(loopback_idev);
	}

	xfrm_dst_ifdown(dst, dev);
}

static struct dst_ops xfrm6_dst_ops = {
	.family =		AF_INET6,
	.protocol =		cpu_to_be16(ETH_P_IPV6),
	.gc =			xfrm6_garbage_collect,
	.update_pmtu =		xfrm6_update_pmtu,
	.destroy =		xfrm6_dst_destroy,
	.ifdown =		xfrm6_dst_ifdown,
	.local_out =		__ip6_local_out,
	.gc_thresh =		1024,
	.entries =		ATOMIC_INIT(0),
};

static struct xfrm_policy_afinfo xfrm6_policy_afinfo = {
	.family =		AF_INET6,
	.dst_ops =		&xfrm6_dst_ops,
	.dst_lookup =		xfrm6_dst_lookup,
	.get_saddr = 		xfrm6_get_saddr,
	.find_bundle =		__xfrm6_find_bundle,
	.decode_session =	_decode_session6,
	.get_tos =		xfrm6_get_tos,
	.init_path =		xfrm6_init_path,
	.fill_dst =		xfrm6_fill_dst,
};

static int __init xfrm6_policy_init(void)
{
	return xfrm_policy_register_afinfo(&xfrm6_policy_afinfo);
}

static void xfrm6_policy_fini(void)
{
	xfrm_policy_unregister_afinfo(&xfrm6_policy_afinfo);
}

#ifdef CONFIG_SYSCTL
static struct ctl_table xfrm6_policy_table[] = {
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "xfrm6_gc_thresh",
		.data	   	= &xfrm6_dst_ops.gc_thresh,
		.maxlen	 	= sizeof(int),
		.mode	   	= 0644,
		.proc_handler   = proc_dointvec,
	},
	{ }
};

static struct ctl_table_header *sysctl_hdr;
#endif

int __init xfrm6_init(void)
{
	int ret;
	unsigned int gc_thresh;

	ret = xfrm6_policy_init();
	if (ret)
		goto out;

	ret = xfrm6_state_init();
	if (ret)
		goto out_policy;
	/*
	 * We need a good default value for the xfrm6 gc threshold.
	 * In ipv4 we set it to the route hash table size * 8, which
	 * is half the size of the maximaum route cache for ipv4.  It
	 * would be good to do the same thing for v6, except the table is
	 * constructed differently here.  Here each table for a net namespace
	 * can have FIB_TABLE_HASHSZ entries, so lets go with the same
	 * computation that we used for ipv4 here.  Also, lets keep the initial
	 * gc_thresh to a minimum of 1024, since, the ipv6 route cache defaults
	 * to that as a minimum as well
	 */
	gc_thresh = FIB6_TABLE_HASHSZ * 8;
	xfrm6_dst_ops.gc_thresh = (gc_thresh < 1024) ? 1024 : gc_thresh;
#ifdef CONFIG_SYSCTL
	sysctl_hdr = register_net_sysctl_table(&init_net, net_ipv6_ctl_path,
						xfrm6_policy_table);
#endif
out:
	return ret;
out_policy:
	xfrm6_policy_fini();
	goto out;
}

void xfrm6_fini(void)
{
#ifdef CONFIG_SYSCTL
	if (sysctl_hdr)
		unregister_net_sysctl_table(sysctl_hdr);
#endif
	//xfrm6_input_fini();
	xfrm6_policy_fini();
	xfrm6_state_fini();
}
