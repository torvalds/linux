// SPDX-License-Identifier: GPL-2.0
/*
 *	XFRM virtual interface
 *
 *	Copyright (C) 2018 secunet Security Networks AG
 *
 *	Author:
 *	Steffen Klassert <steffen.klassert@secunet.com>
 */

#include <linux/module.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/sockios.h>
#include <linux/icmp.h>
#include <linux/if.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_link.h>
#include <linux/if_arp.h>
#include <linux/icmpv6.h>
#include <linux/init.h>
#include <linux/route.h>
#include <linux/rtnetlink.h>
#include <linux/netfilter_ipv6.h>
#include <linux/slab.h>
#include <linux/hash.h>

#include <linux/uaccess.h>
#include <linux/atomic.h>

#include <net/gso.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/ip_tunnels.h>
#include <net/addrconf.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/dst_metadata.h>
#include <net/netns/generic.h>
#include <linux/etherdevice.h>

static int xfrmi_dev_init(struct net_device *dev);
static void xfrmi_dev_setup(struct net_device *dev);
static struct rtnl_link_ops xfrmi_link_ops __read_mostly;
static unsigned int xfrmi_net_id __read_mostly;
static const struct net_device_ops xfrmi_netdev_ops;

#define XFRMI_HASH_BITS	8
#define XFRMI_HASH_SIZE	BIT(XFRMI_HASH_BITS)

struct xfrmi_net {
	/* lists for storing interfaces in use */
	struct xfrm_if __rcu *xfrmi[XFRMI_HASH_SIZE];
	struct xfrm_if __rcu *collect_md_xfrmi;
};

static const struct nla_policy xfrm_lwt_policy[LWT_XFRM_MAX + 1] = {
	[LWT_XFRM_IF_ID]	= NLA_POLICY_MIN(NLA_U32, 1),
	[LWT_XFRM_LINK]		= NLA_POLICY_MIN(NLA_U32, 1),
};

static void xfrmi_destroy_state(struct lwtunnel_state *lwt)
{
}

static int xfrmi_build_state(struct net *net, struct nlattr *nla,
			     unsigned int family, const void *cfg,
			     struct lwtunnel_state **ts,
			     struct netlink_ext_ack *extack)
{
	struct nlattr *tb[LWT_XFRM_MAX + 1];
	struct lwtunnel_state *new_state;
	struct xfrm_md_info *info;
	int ret;

	ret = nla_parse_nested(tb, LWT_XFRM_MAX, nla, xfrm_lwt_policy, extack);
	if (ret < 0)
		return ret;

	if (!tb[LWT_XFRM_IF_ID]) {
		NL_SET_ERR_MSG(extack, "if_id must be set");
		return -EINVAL;
	}

	new_state = lwtunnel_state_alloc(sizeof(*info));
	if (!new_state) {
		NL_SET_ERR_MSG(extack, "failed to create encap info");
		return -ENOMEM;
	}

	new_state->type = LWTUNNEL_ENCAP_XFRM;

	info = lwt_xfrm_info(new_state);

	info->if_id = nla_get_u32(tb[LWT_XFRM_IF_ID]);

	if (tb[LWT_XFRM_LINK])
		info->link = nla_get_u32(tb[LWT_XFRM_LINK]);

	*ts = new_state;
	return 0;
}

static int xfrmi_fill_encap_info(struct sk_buff *skb,
				 struct lwtunnel_state *lwt)
{
	struct xfrm_md_info *info = lwt_xfrm_info(lwt);

	if (nla_put_u32(skb, LWT_XFRM_IF_ID, info->if_id) ||
	    (info->link && nla_put_u32(skb, LWT_XFRM_LINK, info->link)))
		return -EMSGSIZE;

	return 0;
}

static int xfrmi_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	return nla_total_size(sizeof(u32)) + /* LWT_XFRM_IF_ID */
		nla_total_size(sizeof(u32)); /* LWT_XFRM_LINK */
}

static int xfrmi_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct xfrm_md_info *a_info = lwt_xfrm_info(a);
	struct xfrm_md_info *b_info = lwt_xfrm_info(b);

	return memcmp(a_info, b_info, sizeof(*a_info));
}

static const struct lwtunnel_encap_ops xfrmi_encap_ops = {
	.build_state	= xfrmi_build_state,
	.destroy_state	= xfrmi_destroy_state,
	.fill_encap	= xfrmi_fill_encap_info,
	.get_encap_size = xfrmi_encap_nlsize,
	.cmp_encap	= xfrmi_encap_cmp,
	.owner		= THIS_MODULE,
};

#define for_each_xfrmi_rcu(start, xi) \
	for (xi = rcu_dereference(start); xi; xi = rcu_dereference(xi->next))

static u32 xfrmi_hash(u32 if_id)
{
	return hash_32(if_id, XFRMI_HASH_BITS);
}

static struct xfrm_if *xfrmi_lookup(struct net *net, struct xfrm_state *x)
{
	struct xfrmi_net *xfrmn = net_generic(net, xfrmi_net_id);
	struct xfrm_if *xi;

	for_each_xfrmi_rcu(xfrmn->xfrmi[xfrmi_hash(x->if_id)], xi) {
		if (x->if_id == xi->p.if_id &&
		    (xi->dev->flags & IFF_UP))
			return xi;
	}

	xi = rcu_dereference(xfrmn->collect_md_xfrmi);
	if (xi && (xi->dev->flags & IFF_UP))
		return xi;

	return NULL;
}

static bool xfrmi_decode_session(struct sk_buff *skb,
				 unsigned short family,
				 struct xfrm_if_decode_session_result *res)
{
	struct net_device *dev;
	struct xfrm_if *xi;
	int ifindex = 0;

	if (!secpath_exists(skb) || !skb->dev)
		return false;

	switch (family) {
	case AF_INET6:
		ifindex = inet6_sdif(skb);
		break;
	case AF_INET:
		ifindex = inet_sdif(skb);
		break;
	}

	if (ifindex) {
		struct net *net = xs_net(xfrm_input_state(skb));

		dev = dev_get_by_index_rcu(net, ifindex);
	} else {
		dev = skb->dev;
	}

	if (!dev || !(dev->flags & IFF_UP))
		return false;
	if (dev->netdev_ops != &xfrmi_netdev_ops)
		return false;

	xi = netdev_priv(dev);
	res->net = xi->net;

	if (xi->p.collect_md)
		res->if_id = xfrm_input_state(skb)->if_id;
	else
		res->if_id = xi->p.if_id;
	return true;
}

static void xfrmi_link(struct xfrmi_net *xfrmn, struct xfrm_if *xi)
{
	struct xfrm_if __rcu **xip = &xfrmn->xfrmi[xfrmi_hash(xi->p.if_id)];

	rcu_assign_pointer(xi->next , rtnl_dereference(*xip));
	rcu_assign_pointer(*xip, xi);
}

static void xfrmi_unlink(struct xfrmi_net *xfrmn, struct xfrm_if *xi)
{
	struct xfrm_if __rcu **xip;
	struct xfrm_if *iter;

	for (xip = &xfrmn->xfrmi[xfrmi_hash(xi->p.if_id)];
	     (iter = rtnl_dereference(*xip)) != NULL;
	     xip = &iter->next) {
		if (xi == iter) {
			rcu_assign_pointer(*xip, xi->next);
			break;
		}
	}
}

static void xfrmi_dev_free(struct net_device *dev)
{
	struct xfrm_if *xi = netdev_priv(dev);

	gro_cells_destroy(&xi->gro_cells);
}

static int xfrmi_create(struct net_device *dev)
{
	struct xfrm_if *xi = netdev_priv(dev);
	struct net *net = dev_net(dev);
	struct xfrmi_net *xfrmn = net_generic(net, xfrmi_net_id);
	int err;

	dev->rtnl_link_ops = &xfrmi_link_ops;
	err = register_netdevice(dev);
	if (err < 0)
		goto out;

	if (xi->p.collect_md)
		rcu_assign_pointer(xfrmn->collect_md_xfrmi, xi);
	else
		xfrmi_link(xfrmn, xi);

	return 0;

out:
	return err;
}

static struct xfrm_if *xfrmi_locate(struct net *net, struct xfrm_if_parms *p)
{
	struct xfrm_if __rcu **xip;
	struct xfrm_if *xi;
	struct xfrmi_net *xfrmn = net_generic(net, xfrmi_net_id);

	for (xip = &xfrmn->xfrmi[xfrmi_hash(p->if_id)];
	     (xi = rtnl_dereference(*xip)) != NULL;
	     xip = &xi->next)
		if (xi->p.if_id == p->if_id)
			return xi;

	return NULL;
}

static void xfrmi_dev_uninit(struct net_device *dev)
{
	struct xfrm_if *xi = netdev_priv(dev);
	struct xfrmi_net *xfrmn = net_generic(xi->net, xfrmi_net_id);

	if (xi->p.collect_md)
		RCU_INIT_POINTER(xfrmn->collect_md_xfrmi, NULL);
	else
		xfrmi_unlink(xfrmn, xi);
}

static void xfrmi_scrub_packet(struct sk_buff *skb, bool xnet)
{
	skb_clear_tstamp(skb);
	skb->pkt_type = PACKET_HOST;
	skb->skb_iif = 0;
	skb->ignore_df = 0;
	skb_dst_drop(skb);
	nf_reset_ct(skb);
	nf_reset_trace(skb);

	if (!xnet)
		return;

	ipvs_reset(skb);
	secpath_reset(skb);
	skb_orphan(skb);
	skb->mark = 0;
}

static int xfrmi_input(struct sk_buff *skb, int nexthdr, __be32 spi,
		       int encap_type, unsigned short family)
{
	struct sec_path *sp;

	sp = skb_sec_path(skb);
	if (sp && (sp->len || sp->olen) &&
	    !xfrm_policy_check(NULL, XFRM_POLICY_IN, skb, family))
		goto discard;

	XFRM_SPI_SKB_CB(skb)->family = family;
	if (family == AF_INET) {
		XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct iphdr, daddr);
		XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip4 = NULL;
	} else {
		XFRM_SPI_SKB_CB(skb)->daddroff = offsetof(struct ipv6hdr, daddr);
		XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6 = NULL;
	}

	return xfrm_input(skb, nexthdr, spi, encap_type);
discard:
	kfree_skb(skb);
	return 0;
}

static int xfrmi4_rcv(struct sk_buff *skb)
{
	return xfrmi_input(skb, ip_hdr(skb)->protocol, 0, 0, AF_INET);
}

static int xfrmi6_rcv(struct sk_buff *skb)
{
	return xfrmi_input(skb, skb_network_header(skb)[IP6CB(skb)->nhoff],
			   0, 0, AF_INET6);
}

static int xfrmi4_input(struct sk_buff *skb, int nexthdr, __be32 spi, int encap_type)
{
	return xfrmi_input(skb, nexthdr, spi, encap_type, AF_INET);
}

static int xfrmi6_input(struct sk_buff *skb, int nexthdr, __be32 spi, int encap_type)
{
	return xfrmi_input(skb, nexthdr, spi, encap_type, AF_INET6);
}

static int xfrmi_rcv_cb(struct sk_buff *skb, int err)
{
	const struct xfrm_mode *inner_mode;
	struct net_device *dev;
	struct xfrm_state *x;
	struct xfrm_if *xi;
	bool xnet;
	int link;

	if (err && !secpath_exists(skb))
		return 0;

	x = xfrm_input_state(skb);

	xi = xfrmi_lookup(xs_net(x), x);
	if (!xi)
		return 1;

	link = skb->dev->ifindex;
	dev = xi->dev;
	skb->dev = dev;

	if (err) {
		DEV_STATS_INC(dev, rx_errors);
		DEV_STATS_INC(dev, rx_dropped);

		return 0;
	}

	xnet = !net_eq(xi->net, dev_net(skb->dev));

	if (xnet) {
		inner_mode = &x->inner_mode;

		if (x->sel.family == AF_UNSPEC) {
			inner_mode = xfrm_ip2inner_mode(x, XFRM_MODE_SKB_CB(skb)->protocol);
			if (inner_mode == NULL) {
				XFRM_INC_STATS(dev_net(skb->dev),
					       LINUX_MIB_XFRMINSTATEMODEERROR);
				return -EINVAL;
			}
		}

		if (!xfrm_policy_check(NULL, XFRM_POLICY_IN, skb,
				       inner_mode->family))
			return -EPERM;
	}

	xfrmi_scrub_packet(skb, xnet);
	if (xi->p.collect_md) {
		struct metadata_dst *md_dst;

		md_dst = metadata_dst_alloc(0, METADATA_XFRM, GFP_ATOMIC);
		if (!md_dst)
			return -ENOMEM;

		md_dst->u.xfrm_info.if_id = x->if_id;
		md_dst->u.xfrm_info.link = link;
		skb_dst_set(skb, (struct dst_entry *)md_dst);
	}
	dev_sw_netstats_rx_add(dev, skb->len);

	return 0;
}

static int
xfrmi_xmit2(struct sk_buff *skb, struct net_device *dev, struct flowi *fl)
{
	struct xfrm_if *xi = netdev_priv(dev);
	struct dst_entry *dst = skb_dst(skb);
	unsigned int length = skb->len;
	struct net_device *tdev;
	struct xfrm_state *x;
	int err = -1;
	u32 if_id;
	int mtu;

	if (xi->p.collect_md) {
		struct xfrm_md_info *md_info = skb_xfrm_md_info(skb);

		if (unlikely(!md_info))
			return -EINVAL;

		if_id = md_info->if_id;
		fl->flowi_oif = md_info->link;
		if (md_info->dst_orig) {
			struct dst_entry *tmp_dst = dst;

			dst = md_info->dst_orig;
			skb_dst_set(skb, dst);
			md_info->dst_orig = NULL;
			dst_release(tmp_dst);
		}
	} else {
		if_id = xi->p.if_id;
	}

	dst_hold(dst);
	dst = xfrm_lookup_with_ifid(xi->net, dst, fl, NULL, 0, if_id);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		dst = NULL;
		goto tx_err_link_failure;
	}

	x = dst->xfrm;
	if (!x)
		goto tx_err_link_failure;

	if (x->if_id != if_id)
		goto tx_err_link_failure;

	tdev = dst->dev;

	if (tdev == dev) {
		DEV_STATS_INC(dev, collisions);
		net_warn_ratelimited("%s: Local routing loop detected!\n",
				     dev->name);
		goto tx_err_dst_release;
	}

	mtu = dst_mtu(dst);
	if ((!skb_is_gso(skb) && skb->len > mtu) ||
	    (skb_is_gso(skb) && !skb_gso_validate_network_len(skb, mtu))) {
		skb_dst_update_pmtu_no_confirm(skb, mtu);

		if (skb->protocol == htons(ETH_P_IPV6)) {
			if (mtu < IPV6_MIN_MTU)
				mtu = IPV6_MIN_MTU;

			if (skb->len > 1280)
				icmpv6_ndo_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
			else
				goto xmit;
		} else {
			if (!(ip_hdr(skb)->frag_off & htons(IP_DF)))
				goto xmit;
			icmp_ndo_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED,
				      htonl(mtu));
		}

		dst_release(dst);
		return -EMSGSIZE;
	}

xmit:
	xfrmi_scrub_packet(skb, !net_eq(xi->net, dev_net(dev)));
	skb_dst_set(skb, dst);
	skb->dev = tdev;

	err = dst_output(xi->net, skb->sk, skb);
	if (net_xmit_eval(err) == 0) {
		dev_sw_netstats_tx_add(dev, 1, length);
	} else {
		DEV_STATS_INC(dev, tx_errors);
		DEV_STATS_INC(dev, tx_aborted_errors);
	}

	return 0;
tx_err_link_failure:
	DEV_STATS_INC(dev, tx_carrier_errors);
	dst_link_failure(skb);
tx_err_dst_release:
	dst_release(dst);
	return err;
}

static netdev_tx_t xfrmi_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct xfrm_if *xi = netdev_priv(dev);
	struct dst_entry *dst = skb_dst(skb);
	struct flowi fl;
	int ret;

	memset(&fl, 0, sizeof(fl));

	switch (skb->protocol) {
	case htons(ETH_P_IPV6):
		memset(IP6CB(skb), 0, sizeof(*IP6CB(skb)));
		xfrm_decode_session(dev_net(dev), skb, &fl, AF_INET6);
		if (!dst) {
			fl.u.ip6.flowi6_oif = dev->ifindex;
			fl.u.ip6.flowi6_flags |= FLOWI_FLAG_ANYSRC;
			dst = ip6_route_output(dev_net(dev), NULL, &fl.u.ip6);
			if (dst->error) {
				dst_release(dst);
				DEV_STATS_INC(dev, tx_carrier_errors);
				goto tx_err;
			}
			skb_dst_set(skb, dst);
		}
		break;
	case htons(ETH_P_IP):
		memset(IPCB(skb), 0, sizeof(*IPCB(skb)));
		xfrm_decode_session(dev_net(dev), skb, &fl, AF_INET);
		if (!dst) {
			struct rtable *rt;

			fl.u.ip4.flowi4_oif = dev->ifindex;
			fl.u.ip4.flowi4_flags |= FLOWI_FLAG_ANYSRC;
			rt = __ip_route_output_key(dev_net(dev), &fl.u.ip4);
			if (IS_ERR(rt)) {
				DEV_STATS_INC(dev, tx_carrier_errors);
				goto tx_err;
			}
			skb_dst_set(skb, &rt->dst);
		}
		break;
	default:
		goto tx_err;
	}

	fl.flowi_oif = xi->p.link;

	ret = xfrmi_xmit2(skb, dev, &fl);
	if (ret < 0)
		goto tx_err;

	return NETDEV_TX_OK;

tx_err:
	DEV_STATS_INC(dev, tx_errors);
	DEV_STATS_INC(dev, tx_dropped);
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int xfrmi4_err(struct sk_buff *skb, u32 info)
{
	const struct iphdr *iph = (const struct iphdr *)skb->data;
	struct net *net = dev_net(skb->dev);
	int protocol = iph->protocol;
	struct ip_comp_hdr *ipch;
	struct ip_esp_hdr *esph;
	struct ip_auth_hdr *ah ;
	struct xfrm_state *x;
	struct xfrm_if *xi;
	__be32 spi;

	switch (protocol) {
	case IPPROTO_ESP:
		esph = (struct ip_esp_hdr *)(skb->data+(iph->ihl<<2));
		spi = esph->spi;
		break;
	case IPPROTO_AH:
		ah = (struct ip_auth_hdr *)(skb->data+(iph->ihl<<2));
		spi = ah->spi;
		break;
	case IPPROTO_COMP:
		ipch = (struct ip_comp_hdr *)(skb->data+(iph->ihl<<2));
		spi = htonl(ntohs(ipch->cpi));
		break;
	default:
		return 0;
	}

	switch (icmp_hdr(skb)->type) {
	case ICMP_DEST_UNREACH:
		if (icmp_hdr(skb)->code != ICMP_FRAG_NEEDED)
			return 0;
		break;
	case ICMP_REDIRECT:
		break;
	default:
		return 0;
	}

	x = xfrm_state_lookup(net, skb->mark, (const xfrm_address_t *)&iph->daddr,
			      spi, protocol, AF_INET);
	if (!x)
		return 0;

	xi = xfrmi_lookup(net, x);
	if (!xi) {
		xfrm_state_put(x);
		return -1;
	}

	if (icmp_hdr(skb)->type == ICMP_DEST_UNREACH)
		ipv4_update_pmtu(skb, net, info, 0, protocol);
	else
		ipv4_redirect(skb, net, 0, protocol);
	xfrm_state_put(x);

	return 0;
}

static int xfrmi6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		    u8 type, u8 code, int offset, __be32 info)
{
	const struct ipv6hdr *iph = (const struct ipv6hdr *)skb->data;
	struct net *net = dev_net(skb->dev);
	int protocol = iph->nexthdr;
	struct ip_comp_hdr *ipch;
	struct ip_esp_hdr *esph;
	struct ip_auth_hdr *ah;
	struct xfrm_state *x;
	struct xfrm_if *xi;
	__be32 spi;

	switch (protocol) {
	case IPPROTO_ESP:
		esph = (struct ip_esp_hdr *)(skb->data + offset);
		spi = esph->spi;
		break;
	case IPPROTO_AH:
		ah = (struct ip_auth_hdr *)(skb->data + offset);
		spi = ah->spi;
		break;
	case IPPROTO_COMP:
		ipch = (struct ip_comp_hdr *)(skb->data + offset);
		spi = htonl(ntohs(ipch->cpi));
		break;
	default:
		return 0;
	}

	if (type != ICMPV6_PKT_TOOBIG &&
	    type != NDISC_REDIRECT)
		return 0;

	x = xfrm_state_lookup(net, skb->mark, (const xfrm_address_t *)&iph->daddr,
			      spi, protocol, AF_INET6);
	if (!x)
		return 0;

	xi = xfrmi_lookup(net, x);
	if (!xi) {
		xfrm_state_put(x);
		return -1;
	}

	if (type == NDISC_REDIRECT)
		ip6_redirect(skb, net, skb->dev->ifindex, 0,
			     sock_net_uid(net, NULL));
	else
		ip6_update_pmtu(skb, net, info, 0, 0, sock_net_uid(net, NULL));
	xfrm_state_put(x);

	return 0;
}

static int xfrmi_change(struct xfrm_if *xi, const struct xfrm_if_parms *p)
{
	if (xi->p.link != p->link)
		return -EINVAL;

	xi->p.if_id = p->if_id;

	return 0;
}

static int xfrmi_update(struct xfrm_if *xi, struct xfrm_if_parms *p)
{
	struct net *net = xi->net;
	struct xfrmi_net *xfrmn = net_generic(net, xfrmi_net_id);
	int err;

	xfrmi_unlink(xfrmn, xi);
	synchronize_net();
	err = xfrmi_change(xi, p);
	xfrmi_link(xfrmn, xi);
	netdev_state_change(xi->dev);
	return err;
}

static int xfrmi_get_iflink(const struct net_device *dev)
{
	struct xfrm_if *xi = netdev_priv(dev);

	return READ_ONCE(xi->p.link);
}

static const struct net_device_ops xfrmi_netdev_ops = {
	.ndo_init	= xfrmi_dev_init,
	.ndo_uninit	= xfrmi_dev_uninit,
	.ndo_start_xmit = xfrmi_xmit,
	.ndo_get_stats64 = dev_get_tstats64,
	.ndo_get_iflink = xfrmi_get_iflink,
};

static void xfrmi_dev_setup(struct net_device *dev)
{
	dev->netdev_ops 	= &xfrmi_netdev_ops;
	dev->header_ops		= &ip_tunnel_header_ops;
	dev->type		= ARPHRD_NONE;
	dev->mtu		= ETH_DATA_LEN;
	dev->min_mtu		= ETH_MIN_MTU;
	dev->max_mtu		= IP_MAX_MTU;
	dev->flags 		= IFF_NOARP;
	dev->needs_free_netdev	= true;
	dev->priv_destructor	= xfrmi_dev_free;
	dev->pcpu_stat_type	= NETDEV_PCPU_STAT_TSTATS;
	netif_keep_dst(dev);

	eth_broadcast_addr(dev->broadcast);
}

#define XFRMI_FEATURES (NETIF_F_SG |		\
			NETIF_F_FRAGLIST |	\
			NETIF_F_GSO_SOFTWARE |	\
			NETIF_F_HW_CSUM)

static int xfrmi_dev_init(struct net_device *dev)
{
	struct xfrm_if *xi = netdev_priv(dev);
	struct net_device *phydev = __dev_get_by_index(xi->net, xi->p.link);
	int err;

	err = gro_cells_init(&xi->gro_cells, dev);
	if (err)
		return err;

	dev->features |= NETIF_F_LLTX;
	dev->features |= XFRMI_FEATURES;
	dev->hw_features |= XFRMI_FEATURES;

	if (phydev) {
		dev->needed_headroom = phydev->needed_headroom;
		dev->needed_tailroom = phydev->needed_tailroom;

		if (is_zero_ether_addr(dev->dev_addr))
			eth_hw_addr_inherit(dev, phydev);
		if (is_zero_ether_addr(dev->broadcast))
			memcpy(dev->broadcast, phydev->broadcast,
			       dev->addr_len);
	} else {
		eth_hw_addr_random(dev);
		eth_broadcast_addr(dev->broadcast);
	}

	return 0;
}

static int xfrmi_validate(struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	return 0;
}

static void xfrmi_netlink_parms(struct nlattr *data[],
			       struct xfrm_if_parms *parms)
{
	memset(parms, 0, sizeof(*parms));

	if (!data)
		return;

	if (data[IFLA_XFRM_LINK])
		parms->link = nla_get_u32(data[IFLA_XFRM_LINK]);

	if (data[IFLA_XFRM_IF_ID])
		parms->if_id = nla_get_u32(data[IFLA_XFRM_IF_ID]);

	if (data[IFLA_XFRM_COLLECT_METADATA])
		parms->collect_md = true;
}

static int xfrmi_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	struct net *net = dev_net(dev);
	struct xfrm_if_parms p = {};
	struct xfrm_if *xi;
	int err;

	xfrmi_netlink_parms(data, &p);
	if (p.collect_md) {
		struct xfrmi_net *xfrmn = net_generic(net, xfrmi_net_id);

		if (p.link || p.if_id) {
			NL_SET_ERR_MSG(extack, "link and if_id must be zero");
			return -EINVAL;
		}

		if (rtnl_dereference(xfrmn->collect_md_xfrmi))
			return -EEXIST;

	} else {
		if (!p.if_id) {
			NL_SET_ERR_MSG(extack, "if_id must be non zero");
			return -EINVAL;
		}

		xi = xfrmi_locate(net, &p);
		if (xi)
			return -EEXIST;
	}

	xi = netdev_priv(dev);
	xi->p = p;
	xi->net = net;
	xi->dev = dev;

	err = xfrmi_create(dev);
	return err;
}

static void xfrmi_dellink(struct net_device *dev, struct list_head *head)
{
	unregister_netdevice_queue(dev, head);
}

static int xfrmi_changelink(struct net_device *dev, struct nlattr *tb[],
			   struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	struct xfrm_if *xi = netdev_priv(dev);
	struct net *net = xi->net;
	struct xfrm_if_parms p = {};

	xfrmi_netlink_parms(data, &p);
	if (!p.if_id) {
		NL_SET_ERR_MSG(extack, "if_id must be non zero");
		return -EINVAL;
	}

	if (p.collect_md) {
		NL_SET_ERR_MSG(extack, "collect_md can't be changed");
		return -EINVAL;
	}

	xi = xfrmi_locate(net, &p);
	if (!xi) {
		xi = netdev_priv(dev);
	} else {
		if (xi->dev != dev)
			return -EEXIST;
		if (xi->p.collect_md) {
			NL_SET_ERR_MSG(extack,
				       "device can't be changed to collect_md");
			return -EINVAL;
		}
	}

	return xfrmi_update(xi, &p);
}

static size_t xfrmi_get_size(const struct net_device *dev)
{
	return
		/* IFLA_XFRM_LINK */
		nla_total_size(4) +
		/* IFLA_XFRM_IF_ID */
		nla_total_size(4) +
		/* IFLA_XFRM_COLLECT_METADATA */
		nla_total_size(0) +
		0;
}

static int xfrmi_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct xfrm_if *xi = netdev_priv(dev);
	struct xfrm_if_parms *parm = &xi->p;

	if (nla_put_u32(skb, IFLA_XFRM_LINK, parm->link) ||
	    nla_put_u32(skb, IFLA_XFRM_IF_ID, parm->if_id) ||
	    (xi->p.collect_md && nla_put_flag(skb, IFLA_XFRM_COLLECT_METADATA)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static struct net *xfrmi_get_link_net(const struct net_device *dev)
{
	struct xfrm_if *xi = netdev_priv(dev);

	return READ_ONCE(xi->net);
}

static const struct nla_policy xfrmi_policy[IFLA_XFRM_MAX + 1] = {
	[IFLA_XFRM_UNSPEC]		= { .strict_start_type = IFLA_XFRM_COLLECT_METADATA },
	[IFLA_XFRM_LINK]		= { .type = NLA_U32 },
	[IFLA_XFRM_IF_ID]		= { .type = NLA_U32 },
	[IFLA_XFRM_COLLECT_METADATA]	= { .type = NLA_FLAG },
};

static struct rtnl_link_ops xfrmi_link_ops __read_mostly = {
	.kind		= "xfrm",
	.maxtype	= IFLA_XFRM_MAX,
	.policy		= xfrmi_policy,
	.priv_size	= sizeof(struct xfrm_if),
	.setup		= xfrmi_dev_setup,
	.validate	= xfrmi_validate,
	.newlink	= xfrmi_newlink,
	.dellink	= xfrmi_dellink,
	.changelink	= xfrmi_changelink,
	.get_size	= xfrmi_get_size,
	.fill_info	= xfrmi_fill_info,
	.get_link_net	= xfrmi_get_link_net,
};

static void __net_exit xfrmi_exit_batch_rtnl(struct list_head *net_exit_list,
					     struct list_head *dev_to_kill)
{
	struct net *net;

	ASSERT_RTNL();
	list_for_each_entry(net, net_exit_list, exit_list) {
		struct xfrmi_net *xfrmn = net_generic(net, xfrmi_net_id);
		struct xfrm_if __rcu **xip;
		struct xfrm_if *xi;
		int i;

		for (i = 0; i < XFRMI_HASH_SIZE; i++) {
			for (xip = &xfrmn->xfrmi[i];
			     (xi = rtnl_dereference(*xip)) != NULL;
			     xip = &xi->next)
				unregister_netdevice_queue(xi->dev, dev_to_kill);
		}
		xi = rtnl_dereference(xfrmn->collect_md_xfrmi);
		if (xi)
			unregister_netdevice_queue(xi->dev, dev_to_kill);
	}
}

static struct pernet_operations xfrmi_net_ops = {
	.exit_batch_rtnl = xfrmi_exit_batch_rtnl,
	.id   = &xfrmi_net_id,
	.size = sizeof(struct xfrmi_net),
};

static struct xfrm6_protocol xfrmi_esp6_protocol __read_mostly = {
	.handler	=	xfrmi6_rcv,
	.input_handler	=	xfrmi6_input,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi6_err,
	.priority	=	10,
};

static struct xfrm6_protocol xfrmi_ah6_protocol __read_mostly = {
	.handler	=	xfrm6_rcv,
	.input_handler	=	xfrm_input,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi6_err,
	.priority	=	10,
};

static struct xfrm6_protocol xfrmi_ipcomp6_protocol __read_mostly = {
	.handler	=	xfrm6_rcv,
	.input_handler	=	xfrm_input,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi6_err,
	.priority	=	10,
};

#if IS_REACHABLE(CONFIG_INET6_XFRM_TUNNEL)
static int xfrmi6_rcv_tunnel(struct sk_buff *skb)
{
	const xfrm_address_t *saddr;
	__be32 spi;

	saddr = (const xfrm_address_t *)&ipv6_hdr(skb)->saddr;
	spi = xfrm6_tunnel_spi_lookup(dev_net(skb->dev), saddr);

	return xfrm6_rcv_spi(skb, IPPROTO_IPV6, spi, NULL);
}

static struct xfrm6_tunnel xfrmi_ipv6_handler __read_mostly = {
	.handler	=	xfrmi6_rcv_tunnel,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi6_err,
	.priority	=	2,
};

static struct xfrm6_tunnel xfrmi_ip6ip_handler __read_mostly = {
	.handler	=	xfrmi6_rcv_tunnel,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi6_err,
	.priority	=	2,
};
#endif

static struct xfrm4_protocol xfrmi_esp4_protocol __read_mostly = {
	.handler	=	xfrmi4_rcv,
	.input_handler	=	xfrmi4_input,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi4_err,
	.priority	=	10,
};

static struct xfrm4_protocol xfrmi_ah4_protocol __read_mostly = {
	.handler	=	xfrm4_rcv,
	.input_handler	=	xfrm_input,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi4_err,
	.priority	=	10,
};

static struct xfrm4_protocol xfrmi_ipcomp4_protocol __read_mostly = {
	.handler	=	xfrm4_rcv,
	.input_handler	=	xfrm_input,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi4_err,
	.priority	=	10,
};

#if IS_REACHABLE(CONFIG_INET_XFRM_TUNNEL)
static int xfrmi4_rcv_tunnel(struct sk_buff *skb)
{
	return xfrm4_rcv_spi(skb, IPPROTO_IPIP, ip_hdr(skb)->saddr);
}

static struct xfrm_tunnel xfrmi_ipip_handler __read_mostly = {
	.handler	=	xfrmi4_rcv_tunnel,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi4_err,
	.priority	=	3,
};

static struct xfrm_tunnel xfrmi_ipip6_handler __read_mostly = {
	.handler	=	xfrmi4_rcv_tunnel,
	.cb_handler	=	xfrmi_rcv_cb,
	.err_handler	=	xfrmi4_err,
	.priority	=	2,
};
#endif

static int __init xfrmi4_init(void)
{
	int err;

	err = xfrm4_protocol_register(&xfrmi_esp4_protocol, IPPROTO_ESP);
	if (err < 0)
		goto xfrm_proto_esp_failed;
	err = xfrm4_protocol_register(&xfrmi_ah4_protocol, IPPROTO_AH);
	if (err < 0)
		goto xfrm_proto_ah_failed;
	err = xfrm4_protocol_register(&xfrmi_ipcomp4_protocol, IPPROTO_COMP);
	if (err < 0)
		goto xfrm_proto_comp_failed;
#if IS_REACHABLE(CONFIG_INET_XFRM_TUNNEL)
	err = xfrm4_tunnel_register(&xfrmi_ipip_handler, AF_INET);
	if (err < 0)
		goto xfrm_tunnel_ipip_failed;
	err = xfrm4_tunnel_register(&xfrmi_ipip6_handler, AF_INET6);
	if (err < 0)
		goto xfrm_tunnel_ipip6_failed;
#endif

	return 0;

#if IS_REACHABLE(CONFIG_INET_XFRM_TUNNEL)
xfrm_tunnel_ipip6_failed:
	xfrm4_tunnel_deregister(&xfrmi_ipip_handler, AF_INET);
xfrm_tunnel_ipip_failed:
	xfrm4_protocol_deregister(&xfrmi_ipcomp4_protocol, IPPROTO_COMP);
#endif
xfrm_proto_comp_failed:
	xfrm4_protocol_deregister(&xfrmi_ah4_protocol, IPPROTO_AH);
xfrm_proto_ah_failed:
	xfrm4_protocol_deregister(&xfrmi_esp4_protocol, IPPROTO_ESP);
xfrm_proto_esp_failed:
	return err;
}

static void xfrmi4_fini(void)
{
#if IS_REACHABLE(CONFIG_INET_XFRM_TUNNEL)
	xfrm4_tunnel_deregister(&xfrmi_ipip6_handler, AF_INET6);
	xfrm4_tunnel_deregister(&xfrmi_ipip_handler, AF_INET);
#endif
	xfrm4_protocol_deregister(&xfrmi_ipcomp4_protocol, IPPROTO_COMP);
	xfrm4_protocol_deregister(&xfrmi_ah4_protocol, IPPROTO_AH);
	xfrm4_protocol_deregister(&xfrmi_esp4_protocol, IPPROTO_ESP);
}

static int __init xfrmi6_init(void)
{
	int err;

	err = xfrm6_protocol_register(&xfrmi_esp6_protocol, IPPROTO_ESP);
	if (err < 0)
		goto xfrm_proto_esp_failed;
	err = xfrm6_protocol_register(&xfrmi_ah6_protocol, IPPROTO_AH);
	if (err < 0)
		goto xfrm_proto_ah_failed;
	err = xfrm6_protocol_register(&xfrmi_ipcomp6_protocol, IPPROTO_COMP);
	if (err < 0)
		goto xfrm_proto_comp_failed;
#if IS_REACHABLE(CONFIG_INET6_XFRM_TUNNEL)
	err = xfrm6_tunnel_register(&xfrmi_ipv6_handler, AF_INET6);
	if (err < 0)
		goto xfrm_tunnel_ipv6_failed;
	err = xfrm6_tunnel_register(&xfrmi_ip6ip_handler, AF_INET);
	if (err < 0)
		goto xfrm_tunnel_ip6ip_failed;
#endif

	return 0;

#if IS_REACHABLE(CONFIG_INET6_XFRM_TUNNEL)
xfrm_tunnel_ip6ip_failed:
	xfrm6_tunnel_deregister(&xfrmi_ipv6_handler, AF_INET6);
xfrm_tunnel_ipv6_failed:
	xfrm6_protocol_deregister(&xfrmi_ipcomp6_protocol, IPPROTO_COMP);
#endif
xfrm_proto_comp_failed:
	xfrm6_protocol_deregister(&xfrmi_ah6_protocol, IPPROTO_AH);
xfrm_proto_ah_failed:
	xfrm6_protocol_deregister(&xfrmi_esp6_protocol, IPPROTO_ESP);
xfrm_proto_esp_failed:
	return err;
}

static void xfrmi6_fini(void)
{
#if IS_REACHABLE(CONFIG_INET6_XFRM_TUNNEL)
	xfrm6_tunnel_deregister(&xfrmi_ip6ip_handler, AF_INET);
	xfrm6_tunnel_deregister(&xfrmi_ipv6_handler, AF_INET6);
#endif
	xfrm6_protocol_deregister(&xfrmi_ipcomp6_protocol, IPPROTO_COMP);
	xfrm6_protocol_deregister(&xfrmi_ah6_protocol, IPPROTO_AH);
	xfrm6_protocol_deregister(&xfrmi_esp6_protocol, IPPROTO_ESP);
}

static const struct xfrm_if_cb xfrm_if_cb = {
	.decode_session =	xfrmi_decode_session,
};

static int __init xfrmi_init(void)
{
	const char *msg;
	int err;

	pr_info("IPsec XFRM device driver\n");

	msg = "tunnel device";
	err = register_pernet_device(&xfrmi_net_ops);
	if (err < 0)
		goto pernet_dev_failed;

	msg = "xfrm4 protocols";
	err = xfrmi4_init();
	if (err < 0)
		goto xfrmi4_failed;

	msg = "xfrm6 protocols";
	err = xfrmi6_init();
	if (err < 0)
		goto xfrmi6_failed;


	msg = "netlink interface";
	err = rtnl_link_register(&xfrmi_link_ops);
	if (err < 0)
		goto rtnl_link_failed;

	err = register_xfrm_interface_bpf();
	if (err < 0)
		goto kfunc_failed;

	lwtunnel_encap_add_ops(&xfrmi_encap_ops, LWTUNNEL_ENCAP_XFRM);

	xfrm_if_register_cb(&xfrm_if_cb);

	return err;

kfunc_failed:
	rtnl_link_unregister(&xfrmi_link_ops);
rtnl_link_failed:
	xfrmi6_fini();
xfrmi6_failed:
	xfrmi4_fini();
xfrmi4_failed:
	unregister_pernet_device(&xfrmi_net_ops);
pernet_dev_failed:
	pr_err("xfrmi init: failed to register %s\n", msg);
	return err;
}

static void __exit xfrmi_fini(void)
{
	xfrm_if_unregister_cb();
	lwtunnel_encap_del_ops(&xfrmi_encap_ops, LWTUNNEL_ENCAP_XFRM);
	rtnl_link_unregister(&xfrmi_link_ops);
	xfrmi4_fini();
	xfrmi6_fini();
	unregister_pernet_device(&xfrmi_net_ops);
}

module_init(xfrmi_init);
module_exit(xfrmi_fini);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("xfrm");
MODULE_ALIAS_NETDEV("xfrm0");
MODULE_AUTHOR("Steffen Klassert");
MODULE_DESCRIPTION("XFRM virtual interface");
