// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	IPv6 virtual tunneling interface
 *
 *	Copyright (C) 2013 secunet Security Networks AG
 *
 *	Author:
 *	Steffen Klassert <steffen.klassert@secunet.com>
 *
 *	Based on:
 *	net/ipv6/ip6_tunnel.c
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

#include <net/icmp.h>
#include <net/ip.h>
#include <net/ip_tunnels.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/ip6_tunnel.h>
#include <net/xfrm.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <linux/etherdevice.h>

#define IP6_VTI_HASH_SIZE_SHIFT  5
#define IP6_VTI_HASH_SIZE (1 << IP6_VTI_HASH_SIZE_SHIFT)

static u32 HASH(const struct in6_addr *addr1, const struct in6_addr *addr2)
{
	u32 hash = ipv6_addr_hash(addr1) ^ ipv6_addr_hash(addr2);

	return hash_32(hash, IP6_VTI_HASH_SIZE_SHIFT);
}

static int vti6_dev_init(struct net_device *dev);
static void vti6_dev_setup(struct net_device *dev);
static struct rtnl_link_ops vti6_link_ops __read_mostly;

static unsigned int vti6_net_id __read_mostly;
struct vti6_net {
	/* the vti6 tunnel fallback device */
	struct net_device *fb_tnl_dev;
	/* lists for storing tunnels in use */
	struct ip6_tnl __rcu *tnls_r_l[IP6_VTI_HASH_SIZE];
	struct ip6_tnl __rcu *tnls_wc[1];
	struct ip6_tnl __rcu **tnls[2];
};

#define for_each_vti6_tunnel_rcu(start) \
	for (t = rcu_dereference(start); t; t = rcu_dereference(t->next))

/**
 * vti6_tnl_lookup - fetch tunnel matching the end-point addresses
 *   @net: network namespace
 *   @remote: the address of the tunnel exit-point
 *   @local: the address of the tunnel entry-point
 *
 * Return:
 *   tunnel matching given end-points if found,
 *   else fallback tunnel if its device is up,
 *   else %NULL
 **/
static struct ip6_tnl *
vti6_tnl_lookup(struct net *net, const struct in6_addr *remote,
		const struct in6_addr *local)
{
	unsigned int hash = HASH(remote, local);
	struct ip6_tnl *t;
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);
	struct in6_addr any;

	for_each_vti6_tunnel_rcu(ip6n->tnls_r_l[hash]) {
		if (ipv6_addr_equal(local, &t->parms.laddr) &&
		    ipv6_addr_equal(remote, &t->parms.raddr) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}

	memset(&any, 0, sizeof(any));
	hash = HASH(&any, local);
	for_each_vti6_tunnel_rcu(ip6n->tnls_r_l[hash]) {
		if (ipv6_addr_equal(local, &t->parms.laddr) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}

	hash = HASH(remote, &any);
	for_each_vti6_tunnel_rcu(ip6n->tnls_r_l[hash]) {
		if (ipv6_addr_equal(remote, &t->parms.raddr) &&
		    (t->dev->flags & IFF_UP))
			return t;
	}

	t = rcu_dereference(ip6n->tnls_wc[0]);
	if (t && (t->dev->flags & IFF_UP))
		return t;

	return NULL;
}

/**
 * vti6_tnl_bucket - get head of list matching given tunnel parameters
 *   @p: parameters containing tunnel end-points
 *
 * Description:
 *   vti6_tnl_bucket() returns the head of the list matching the
 *   &struct in6_addr entries laddr and raddr in @p.
 *
 * Return: head of IPv6 tunnel list
 **/
static struct ip6_tnl __rcu **
vti6_tnl_bucket(struct vti6_net *ip6n, const struct __ip6_tnl_parm *p)
{
	const struct in6_addr *remote = &p->raddr;
	const struct in6_addr *local = &p->laddr;
	unsigned int h = 0;
	int prio = 0;

	if (!ipv6_addr_any(remote) || !ipv6_addr_any(local)) {
		prio = 1;
		h = HASH(remote, local);
	}
	return &ip6n->tnls[prio][h];
}

static void
vti6_tnl_link(struct vti6_net *ip6n, struct ip6_tnl *t)
{
	struct ip6_tnl __rcu **tp = vti6_tnl_bucket(ip6n, &t->parms);

	rcu_assign_pointer(t->next , rtnl_dereference(*tp));
	rcu_assign_pointer(*tp, t);
}

static void
vti6_tnl_unlink(struct vti6_net *ip6n, struct ip6_tnl *t)
{
	struct ip6_tnl __rcu **tp;
	struct ip6_tnl *iter;

	for (tp = vti6_tnl_bucket(ip6n, &t->parms);
	     (iter = rtnl_dereference(*tp)) != NULL;
	     tp = &iter->next) {
		if (t == iter) {
			rcu_assign_pointer(*tp, t->next);
			break;
		}
	}
}

static void vti6_dev_free(struct net_device *dev)
{
	free_percpu(dev->tstats);
}

static int vti6_tnl_create2(struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct net *net = dev_net(dev);
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);
	int err;

	dev->rtnl_link_ops = &vti6_link_ops;
	err = register_netdevice(dev);
	if (err < 0)
		goto out;

	strcpy(t->parms.name, dev->name);

	dev_hold(dev);
	vti6_tnl_link(ip6n, t);

	return 0;

out:
	return err;
}

static struct ip6_tnl *vti6_tnl_create(struct net *net, struct __ip6_tnl_parm *p)
{
	struct net_device *dev;
	struct ip6_tnl *t;
	char name[IFNAMSIZ];
	int err;

	if (p->name[0]) {
		if (!dev_valid_name(p->name))
			goto failed;
		strlcpy(name, p->name, IFNAMSIZ);
	} else {
		sprintf(name, "ip6_vti%%d");
	}

	dev = alloc_netdev(sizeof(*t), name, NET_NAME_UNKNOWN, vti6_dev_setup);
	if (!dev)
		goto failed;

	dev_net_set(dev, net);

	t = netdev_priv(dev);
	t->parms = *p;
	t->net = dev_net(dev);

	err = vti6_tnl_create2(dev);
	if (err < 0)
		goto failed_free;

	return t;

failed_free:
	free_netdev(dev);
failed:
	return NULL;
}

/**
 * vti6_locate - find or create tunnel matching given parameters
 *   @net: network namespace
 *   @p: tunnel parameters
 *   @create: != 0 if allowed to create new tunnel if no match found
 *
 * Description:
 *   vti6_locate() first tries to locate an existing tunnel
 *   based on @parms. If this is unsuccessful, but @create is set a new
 *   tunnel device is created and registered for use.
 *
 * Return:
 *   matching tunnel or NULL
 **/
static struct ip6_tnl *vti6_locate(struct net *net, struct __ip6_tnl_parm *p,
				   int create)
{
	const struct in6_addr *remote = &p->raddr;
	const struct in6_addr *local = &p->laddr;
	struct ip6_tnl __rcu **tp;
	struct ip6_tnl *t;
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);

	for (tp = vti6_tnl_bucket(ip6n, p);
	     (t = rtnl_dereference(*tp)) != NULL;
	     tp = &t->next) {
		if (ipv6_addr_equal(local, &t->parms.laddr) &&
		    ipv6_addr_equal(remote, &t->parms.raddr)) {
			if (create)
				return NULL;

			return t;
		}
	}
	if (!create)
		return NULL;
	return vti6_tnl_create(net, p);
}

/**
 * vti6_dev_uninit - tunnel device uninitializer
 *   @dev: the device to be destroyed
 *
 * Description:
 *   vti6_dev_uninit() removes tunnel from its list
 **/
static void vti6_dev_uninit(struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct vti6_net *ip6n = net_generic(t->net, vti6_net_id);

	if (dev == ip6n->fb_tnl_dev)
		RCU_INIT_POINTER(ip6n->tnls_wc[0], NULL);
	else
		vti6_tnl_unlink(ip6n, t);
	dev_put(dev);
}

static int vti6_rcv(struct sk_buff *skb)
{
	struct ip6_tnl *t;
	const struct ipv6hdr *ipv6h = ipv6_hdr(skb);

	rcu_read_lock();
	t = vti6_tnl_lookup(dev_net(skb->dev), &ipv6h->saddr, &ipv6h->daddr);
	if (t) {
		if (t->parms.proto != IPPROTO_IPV6 && t->parms.proto != 0) {
			rcu_read_unlock();
			goto discard;
		}

		if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb)) {
			rcu_read_unlock();
			return 0;
		}

		ipv6h = ipv6_hdr(skb);
		if (!ip6_tnl_rcv_ctl(t, &ipv6h->daddr, &ipv6h->saddr)) {
			t->dev->stats.rx_dropped++;
			rcu_read_unlock();
			goto discard;
		}

		rcu_read_unlock();

		return xfrm6_rcv_tnl(skb, t);
	}
	rcu_read_unlock();
	return -EINVAL;
discard:
	kfree_skb(skb);
	return 0;
}

static int vti6_rcv_cb(struct sk_buff *skb, int err)
{
	unsigned short family;
	struct net_device *dev;
	struct pcpu_sw_netstats *tstats;
	struct xfrm_state *x;
	const struct xfrm_mode *inner_mode;
	struct ip6_tnl *t = XFRM_TUNNEL_SKB_CB(skb)->tunnel.ip6;
	u32 orig_mark = skb->mark;
	int ret;

	if (!t)
		return 1;

	dev = t->dev;

	if (err) {
		dev->stats.rx_errors++;
		dev->stats.rx_dropped++;

		return 0;
	}

	x = xfrm_input_state(skb);

	inner_mode = &x->inner_mode;

	if (x->sel.family == AF_UNSPEC) {
		inner_mode = xfrm_ip2inner_mode(x, XFRM_MODE_SKB_CB(skb)->protocol);
		if (inner_mode == NULL) {
			XFRM_INC_STATS(dev_net(skb->dev),
				       LINUX_MIB_XFRMINSTATEMODEERROR);
			return -EINVAL;
		}
	}

	family = inner_mode->family;

	skb->mark = be32_to_cpu(t->parms.i_key);
	ret = xfrm_policy_check(NULL, XFRM_POLICY_IN, skb, family);
	skb->mark = orig_mark;

	if (!ret)
		return -EPERM;

	skb_scrub_packet(skb, !net_eq(t->net, dev_net(skb->dev)));
	skb->dev = dev;

	tstats = this_cpu_ptr(dev->tstats);
	u64_stats_update_begin(&tstats->syncp);
	tstats->rx_packets++;
	tstats->rx_bytes += skb->len;
	u64_stats_update_end(&tstats->syncp);

	return 0;
}

/**
 * vti6_addr_conflict - compare packet addresses to tunnel's own
 *   @t: the outgoing tunnel device
 *   @hdr: IPv6 header from the incoming packet
 *
 * Description:
 *   Avoid trivial tunneling loop by checking that tunnel exit-point
 *   doesn't match source of incoming packet.
 *
 * Return:
 *   1 if conflict,
 *   0 else
 **/
static inline bool
vti6_addr_conflict(const struct ip6_tnl *t, const struct ipv6hdr *hdr)
{
	return ipv6_addr_equal(&t->parms.raddr, &hdr->saddr);
}

static bool vti6_state_check(const struct xfrm_state *x,
			     const struct in6_addr *dst,
			     const struct in6_addr *src)
{
	xfrm_address_t *daddr = (xfrm_address_t *)dst;
	xfrm_address_t *saddr = (xfrm_address_t *)src;

	/* if there is no transform then this tunnel is not functional.
	 * Or if the xfrm is not mode tunnel.
	 */
	if (!x || x->props.mode != XFRM_MODE_TUNNEL ||
	    x->props.family != AF_INET6)
		return false;

	if (ipv6_addr_any(dst))
		return xfrm_addr_equal(saddr, &x->props.saddr, AF_INET6);

	if (!xfrm_state_addr_check(x, daddr, saddr, AF_INET6))
		return false;

	return true;
}

/**
 * vti6_xmit - send a packet
 *   @skb: the outgoing socket buffer
 *   @dev: the outgoing tunnel device
 *   @fl: the flow informations for the xfrm_lookup
 **/
static int
vti6_xmit(struct sk_buff *skb, struct net_device *dev, struct flowi *fl)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct net_device_stats *stats = &t->dev->stats;
	struct dst_entry *dst = skb_dst(skb);
	struct net_device *tdev;
	struct xfrm_state *x;
	int pkt_len = skb->len;
	int err = -1;
	int mtu;

	if (!dst) {
		fl->u.ip6.flowi6_oif = dev->ifindex;
		fl->u.ip6.flowi6_flags |= FLOWI_FLAG_ANYSRC;
		dst = ip6_route_output(dev_net(dev), NULL, &fl->u.ip6);
		if (dst->error) {
			dst_release(dst);
			dst = NULL;
			goto tx_err_link_failure;
		}
		skb_dst_set(skb, dst);
	}

	dst_hold(dst);
	dst = xfrm_lookup(t->net, dst, fl, NULL, 0);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		dst = NULL;
		goto tx_err_link_failure;
	}

	x = dst->xfrm;
	if (!vti6_state_check(x, &t->parms.raddr, &t->parms.laddr))
		goto tx_err_link_failure;

	if (!ip6_tnl_xmit_ctl(t, (const struct in6_addr *)&x->props.saddr,
			      (const struct in6_addr *)&x->id.daddr))
		goto tx_err_link_failure;

	tdev = dst->dev;

	if (tdev == dev) {
		stats->collisions++;
		net_warn_ratelimited("%s: Local routing loop detected!\n",
				     t->parms.name);
		goto tx_err_dst_release;
	}

	mtu = dst_mtu(dst);
	if (skb->len > mtu) {
		skb_dst_update_pmtu_no_confirm(skb, mtu);

		if (skb->protocol == htons(ETH_P_IPV6)) {
			if (mtu < IPV6_MIN_MTU)
				mtu = IPV6_MIN_MTU;

			icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu);
		} else {
			icmp_send(skb, ICMP_DEST_UNREACH, ICMP_FRAG_NEEDED,
				  htonl(mtu));
		}

		err = -EMSGSIZE;
		goto tx_err_dst_release;
	}

	skb_scrub_packet(skb, !net_eq(t->net, dev_net(dev)));
	skb_dst_set(skb, dst);
	skb->dev = skb_dst(skb)->dev;

	err = dst_output(t->net, skb->sk, skb);
	if (net_xmit_eval(err) == 0)
		err = pkt_len;
	iptunnel_xmit_stats(dev, err);

	return 0;
tx_err_link_failure:
	stats->tx_carrier_errors++;
	dst_link_failure(skb);
tx_err_dst_release:
	dst_release(dst);
	return err;
}

static netdev_tx_t
vti6_tnl_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct net_device_stats *stats = &t->dev->stats;
	struct flowi fl;
	int ret;

	if (!pskb_inet_may_pull(skb))
		goto tx_err;

	memset(&fl, 0, sizeof(fl));

	switch (skb->protocol) {
	case htons(ETH_P_IPV6):
		if ((t->parms.proto != IPPROTO_IPV6 && t->parms.proto != 0) ||
		    vti6_addr_conflict(t, ipv6_hdr(skb)))
			goto tx_err;

		xfrm_decode_session(skb, &fl, AF_INET6);
		memset(IP6CB(skb), 0, sizeof(*IP6CB(skb)));
		break;
	case htons(ETH_P_IP):
		xfrm_decode_session(skb, &fl, AF_INET);
		memset(IPCB(skb), 0, sizeof(*IPCB(skb)));
		break;
	default:
		goto tx_err;
	}

	/* override mark with tunnel output key */
	fl.flowi_mark = be32_to_cpu(t->parms.o_key);

	ret = vti6_xmit(skb, dev, &fl);
	if (ret < 0)
		goto tx_err;

	return NETDEV_TX_OK;

tx_err:
	stats->tx_errors++;
	stats->tx_dropped++;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int vti6_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		    u8 type, u8 code, int offset, __be32 info)
{
	__be32 spi;
	__u32 mark;
	struct xfrm_state *x;
	struct ip6_tnl *t;
	struct ip_esp_hdr *esph;
	struct ip_auth_hdr *ah;
	struct ip_comp_hdr *ipch;
	struct net *net = dev_net(skb->dev);
	const struct ipv6hdr *iph = (const struct ipv6hdr *)skb->data;
	int protocol = iph->nexthdr;

	t = vti6_tnl_lookup(dev_net(skb->dev), &iph->daddr, &iph->saddr);
	if (!t)
		return -1;

	mark = be32_to_cpu(t->parms.o_key);

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

	x = xfrm_state_lookup(net, mark, (const xfrm_address_t *)&iph->daddr,
			      spi, protocol, AF_INET6);
	if (!x)
		return 0;

	if (type == NDISC_REDIRECT)
		ip6_redirect(skb, net, skb->dev->ifindex, 0,
			     sock_net_uid(net, NULL));
	else
		ip6_update_pmtu(skb, net, info, 0, 0, sock_net_uid(net, NULL));
	xfrm_state_put(x);

	return 0;
}

static void vti6_link_config(struct ip6_tnl *t, bool keep_mtu)
{
	struct net_device *dev = t->dev;
	struct __ip6_tnl_parm *p = &t->parms;
	struct net_device *tdev = NULL;
	int mtu;

	memcpy(dev->dev_addr, &p->laddr, sizeof(struct in6_addr));
	memcpy(dev->broadcast, &p->raddr, sizeof(struct in6_addr));

	p->flags &= ~(IP6_TNL_F_CAP_XMIT | IP6_TNL_F_CAP_RCV |
		      IP6_TNL_F_CAP_PER_PACKET);
	p->flags |= ip6_tnl_get_cap(t, &p->laddr, &p->raddr);

	if (p->flags & IP6_TNL_F_CAP_XMIT && p->flags & IP6_TNL_F_CAP_RCV)
		dev->flags |= IFF_POINTOPOINT;
	else
		dev->flags &= ~IFF_POINTOPOINT;

	if (keep_mtu && dev->mtu) {
		dev->mtu = clamp(dev->mtu, dev->min_mtu, dev->max_mtu);
		return;
	}

	if (p->flags & IP6_TNL_F_CAP_XMIT) {
		int strict = (ipv6_addr_type(&p->raddr) &
			      (IPV6_ADDR_MULTICAST | IPV6_ADDR_LINKLOCAL));
		struct rt6_info *rt = rt6_lookup(t->net,
						 &p->raddr, &p->laddr,
						 p->link, NULL, strict);

		if (rt)
			tdev = rt->dst.dev;
		ip6_rt_put(rt);
	}

	if (!tdev && p->link)
		tdev = __dev_get_by_index(t->net, p->link);

	if (tdev)
		mtu = tdev->mtu - sizeof(struct ipv6hdr);
	else
		mtu = ETH_DATA_LEN - LL_MAX_HEADER - sizeof(struct ipv6hdr);

	dev->mtu = max_t(int, mtu, IPV4_MIN_MTU);
}

/**
 * vti6_tnl_change - update the tunnel parameters
 *   @t: tunnel to be changed
 *   @p: tunnel configuration parameters
 *   @keep_mtu: MTU was set from userspace, don't re-compute it
 *
 * Description:
 *   vti6_tnl_change() updates the tunnel parameters
 **/
static int
vti6_tnl_change(struct ip6_tnl *t, const struct __ip6_tnl_parm *p,
		bool keep_mtu)
{
	t->parms.laddr = p->laddr;
	t->parms.raddr = p->raddr;
	t->parms.link = p->link;
	t->parms.i_key = p->i_key;
	t->parms.o_key = p->o_key;
	t->parms.proto = p->proto;
	t->parms.fwmark = p->fwmark;
	dst_cache_reset(&t->dst_cache);
	vti6_link_config(t, keep_mtu);
	return 0;
}

static int vti6_update(struct ip6_tnl *t, struct __ip6_tnl_parm *p,
		       bool keep_mtu)
{
	struct net *net = dev_net(t->dev);
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);
	int err;

	vti6_tnl_unlink(ip6n, t);
	synchronize_net();
	err = vti6_tnl_change(t, p, keep_mtu);
	vti6_tnl_link(ip6n, t);
	netdev_state_change(t->dev);
	return err;
}

static void
vti6_parm_from_user(struct __ip6_tnl_parm *p, const struct ip6_tnl_parm2 *u)
{
	p->laddr = u->laddr;
	p->raddr = u->raddr;
	p->link = u->link;
	p->i_key = u->i_key;
	p->o_key = u->o_key;
	p->proto = u->proto;

	memcpy(p->name, u->name, sizeof(u->name));
}

static void
vti6_parm_to_user(struct ip6_tnl_parm2 *u, const struct __ip6_tnl_parm *p)
{
	u->laddr = p->laddr;
	u->raddr = p->raddr;
	u->link = p->link;
	u->i_key = p->i_key;
	u->o_key = p->o_key;
	if (u->i_key)
		u->i_flags |= GRE_KEY;
	if (u->o_key)
		u->o_flags |= GRE_KEY;
	u->proto = p->proto;

	memcpy(u->name, p->name, sizeof(u->name));
}

/**
 * vti6_ioctl - configure vti6 tunnels from userspace
 *   @dev: virtual device associated with tunnel
 *   @ifr: parameters passed from userspace
 *   @cmd: command to be performed
 *
 * Description:
 *   vti6_ioctl() is used for managing vti6 tunnels
 *   from userspace.
 *
 *   The possible commands are the following:
 *     %SIOCGETTUNNEL: get tunnel parameters for device
 *     %SIOCADDTUNNEL: add tunnel matching given tunnel parameters
 *     %SIOCCHGTUNNEL: change tunnel parameters to those given
 *     %SIOCDELTUNNEL: delete tunnel
 *
 *   The fallback device "ip6_vti0", created during module
 *   initialization, can be used for creating other tunnel devices.
 *
 * Return:
 *   0 on success,
 *   %-EFAULT if unable to copy data to or from userspace,
 *   %-EPERM if current process hasn't %CAP_NET_ADMIN set
 *   %-EINVAL if passed tunnel parameters are invalid,
 *   %-EEXIST if changing a tunnel's parameters would cause a conflict
 *   %-ENODEV if attempting to change or delete a nonexisting device
 **/
static int
vti6_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int err = 0;
	struct ip6_tnl_parm2 p;
	struct __ip6_tnl_parm p1;
	struct ip6_tnl *t = NULL;
	struct net *net = dev_net(dev);
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);

	switch (cmd) {
	case SIOCGETTUNNEL:
		if (dev == ip6n->fb_tnl_dev) {
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p))) {
				err = -EFAULT;
				break;
			}
			vti6_parm_from_user(&p1, &p);
			t = vti6_locate(net, &p1, 0);
		} else {
			memset(&p, 0, sizeof(p));
		}
		if (!t)
			t = netdev_priv(dev);
		vti6_parm_to_user(&p, &t->parms);
		if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof(p)))
			err = -EFAULT;
		break;
	case SIOCADDTUNNEL:
	case SIOCCHGTUNNEL:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			break;
		err = -EFAULT;
		if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
			break;
		err = -EINVAL;
		if (p.proto != IPPROTO_IPV6  && p.proto != 0)
			break;
		vti6_parm_from_user(&p1, &p);
		t = vti6_locate(net, &p1, cmd == SIOCADDTUNNEL);
		if (dev != ip6n->fb_tnl_dev && cmd == SIOCCHGTUNNEL) {
			if (t) {
				if (t->dev != dev) {
					err = -EEXIST;
					break;
				}
			} else
				t = netdev_priv(dev);

			err = vti6_update(t, &p1, false);
		}
		if (t) {
			err = 0;
			vti6_parm_to_user(&p, &t->parms);
			if (copy_to_user(ifr->ifr_ifru.ifru_data, &p, sizeof(p)))
				err = -EFAULT;

		} else
			err = (cmd == SIOCADDTUNNEL ? -ENOBUFS : -ENOENT);
		break;
	case SIOCDELTUNNEL:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			break;

		if (dev == ip6n->fb_tnl_dev) {
			err = -EFAULT;
			if (copy_from_user(&p, ifr->ifr_ifru.ifru_data, sizeof(p)))
				break;
			err = -ENOENT;
			vti6_parm_from_user(&p1, &p);
			t = vti6_locate(net, &p1, 0);
			if (!t)
				break;
			err = -EPERM;
			if (t->dev == ip6n->fb_tnl_dev)
				break;
			dev = t->dev;
		}
		err = 0;
		unregister_netdevice(dev);
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

static const struct net_device_ops vti6_netdev_ops = {
	.ndo_init	= vti6_dev_init,
	.ndo_uninit	= vti6_dev_uninit,
	.ndo_start_xmit = vti6_tnl_xmit,
	.ndo_do_ioctl	= vti6_ioctl,
	.ndo_get_stats64 = ip_tunnel_get_stats64,
	.ndo_get_iflink = ip6_tnl_get_iflink,
};

/**
 * vti6_dev_setup - setup virtual tunnel device
 *   @dev: virtual device associated with tunnel
 *
 * Description:
 *   Initialize function pointers and device parameters
 **/
static void vti6_dev_setup(struct net_device *dev)
{
	dev->netdev_ops = &vti6_netdev_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = vti6_dev_free;

	dev->type = ARPHRD_TUNNEL6;
	dev->min_mtu = IPV4_MIN_MTU;
	dev->max_mtu = IP_MAX_MTU - sizeof(struct ipv6hdr);
	dev->flags |= IFF_NOARP;
	dev->addr_len = sizeof(struct in6_addr);
	netif_keep_dst(dev);
	/* This perm addr will be used as interface identifier by IPv6 */
	dev->addr_assign_type = NET_ADDR_RANDOM;
	eth_random_addr(dev->perm_addr);
}

/**
 * vti6_dev_init_gen - general initializer for all tunnel devices
 *   @dev: virtual device associated with tunnel
 **/
static inline int vti6_dev_init_gen(struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);

	t->dev = dev;
	t->net = dev_net(dev);
	dev->tstats = netdev_alloc_pcpu_stats(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;
	return 0;
}

/**
 * vti6_dev_init - initializer for all non fallback tunnel devices
 *   @dev: virtual device associated with tunnel
 **/
static int vti6_dev_init(struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	int err = vti6_dev_init_gen(dev);

	if (err)
		return err;
	vti6_link_config(t, true);
	return 0;
}

/**
 * vti6_fb_tnl_dev_init - initializer for fallback tunnel device
 *   @dev: fallback device
 *
 * Return: 0
 **/
static int __net_init vti6_fb_tnl_dev_init(struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct net *net = dev_net(dev);
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);

	t->parms.proto = IPPROTO_IPV6;
	dev_hold(dev);

	rcu_assign_pointer(ip6n->tnls_wc[0], t);
	return 0;
}

static int vti6_validate(struct nlattr *tb[], struct nlattr *data[],
			 struct netlink_ext_ack *extack)
{
	return 0;
}

static void vti6_netlink_parms(struct nlattr *data[],
			       struct __ip6_tnl_parm *parms)
{
	memset(parms, 0, sizeof(*parms));

	if (!data)
		return;

	if (data[IFLA_VTI_LINK])
		parms->link = nla_get_u32(data[IFLA_VTI_LINK]);

	if (data[IFLA_VTI_LOCAL])
		parms->laddr = nla_get_in6_addr(data[IFLA_VTI_LOCAL]);

	if (data[IFLA_VTI_REMOTE])
		parms->raddr = nla_get_in6_addr(data[IFLA_VTI_REMOTE]);

	if (data[IFLA_VTI_IKEY])
		parms->i_key = nla_get_be32(data[IFLA_VTI_IKEY]);

	if (data[IFLA_VTI_OKEY])
		parms->o_key = nla_get_be32(data[IFLA_VTI_OKEY]);

	if (data[IFLA_VTI_FWMARK])
		parms->fwmark = nla_get_u32(data[IFLA_VTI_FWMARK]);
}

static int vti6_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[],
			struct netlink_ext_ack *extack)
{
	struct net *net = dev_net(dev);
	struct ip6_tnl *nt;

	nt = netdev_priv(dev);
	vti6_netlink_parms(data, &nt->parms);

	nt->parms.proto = IPPROTO_IPV6;

	if (vti6_locate(net, &nt->parms, 0))
		return -EEXIST;

	return vti6_tnl_create2(dev);
}

static void vti6_dellink(struct net_device *dev, struct list_head *head)
{
	struct net *net = dev_net(dev);
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);

	if (dev != ip6n->fb_tnl_dev)
		unregister_netdevice_queue(dev, head);
}

static int vti6_changelink(struct net_device *dev, struct nlattr *tb[],
			   struct nlattr *data[],
			   struct netlink_ext_ack *extack)
{
	struct ip6_tnl *t;
	struct __ip6_tnl_parm p;
	struct net *net = dev_net(dev);
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);

	if (dev == ip6n->fb_tnl_dev)
		return -EINVAL;

	vti6_netlink_parms(data, &p);

	t = vti6_locate(net, &p, 0);

	if (t) {
		if (t->dev != dev)
			return -EEXIST;
	} else
		t = netdev_priv(dev);

	return vti6_update(t, &p, tb && tb[IFLA_MTU]);
}

static size_t vti6_get_size(const struct net_device *dev)
{
	return
		/* IFLA_VTI_LINK */
		nla_total_size(4) +
		/* IFLA_VTI_LOCAL */
		nla_total_size(sizeof(struct in6_addr)) +
		/* IFLA_VTI_REMOTE */
		nla_total_size(sizeof(struct in6_addr)) +
		/* IFLA_VTI_IKEY */
		nla_total_size(4) +
		/* IFLA_VTI_OKEY */
		nla_total_size(4) +
		/* IFLA_VTI_FWMARK */
		nla_total_size(4) +
		0;
}

static int vti6_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct ip6_tnl *tunnel = netdev_priv(dev);
	struct __ip6_tnl_parm *parm = &tunnel->parms;

	if (nla_put_u32(skb, IFLA_VTI_LINK, parm->link) ||
	    nla_put_in6_addr(skb, IFLA_VTI_LOCAL, &parm->laddr) ||
	    nla_put_in6_addr(skb, IFLA_VTI_REMOTE, &parm->raddr) ||
	    nla_put_be32(skb, IFLA_VTI_IKEY, parm->i_key) ||
	    nla_put_be32(skb, IFLA_VTI_OKEY, parm->o_key) ||
	    nla_put_u32(skb, IFLA_VTI_FWMARK, parm->fwmark))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static const struct nla_policy vti6_policy[IFLA_VTI_MAX + 1] = {
	[IFLA_VTI_LINK]		= { .type = NLA_U32 },
	[IFLA_VTI_LOCAL]	= { .len = sizeof(struct in6_addr) },
	[IFLA_VTI_REMOTE]	= { .len = sizeof(struct in6_addr) },
	[IFLA_VTI_IKEY]		= { .type = NLA_U32 },
	[IFLA_VTI_OKEY]		= { .type = NLA_U32 },
	[IFLA_VTI_FWMARK]	= { .type = NLA_U32 },
};

static struct rtnl_link_ops vti6_link_ops __read_mostly = {
	.kind		= "vti6",
	.maxtype	= IFLA_VTI_MAX,
	.policy		= vti6_policy,
	.priv_size	= sizeof(struct ip6_tnl),
	.setup		= vti6_dev_setup,
	.validate	= vti6_validate,
	.newlink	= vti6_newlink,
	.dellink	= vti6_dellink,
	.changelink	= vti6_changelink,
	.get_size	= vti6_get_size,
	.fill_info	= vti6_fill_info,
	.get_link_net	= ip6_tnl_get_link_net,
};

static void __net_exit vti6_destroy_tunnels(struct vti6_net *ip6n,
					    struct list_head *list)
{
	int h;
	struct ip6_tnl *t;

	for (h = 0; h < IP6_VTI_HASH_SIZE; h++) {
		t = rtnl_dereference(ip6n->tnls_r_l[h]);
		while (t) {
			unregister_netdevice_queue(t->dev, list);
			t = rtnl_dereference(t->next);
		}
	}

	t = rtnl_dereference(ip6n->tnls_wc[0]);
	if (t)
		unregister_netdevice_queue(t->dev, list);
}

static int __net_init vti6_init_net(struct net *net)
{
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);
	struct ip6_tnl *t = NULL;
	int err;

	ip6n->tnls[0] = ip6n->tnls_wc;
	ip6n->tnls[1] = ip6n->tnls_r_l;

	if (!net_has_fallback_tunnels(net))
		return 0;
	err = -ENOMEM;
	ip6n->fb_tnl_dev = alloc_netdev(sizeof(struct ip6_tnl), "ip6_vti0",
					NET_NAME_UNKNOWN, vti6_dev_setup);

	if (!ip6n->fb_tnl_dev)
		goto err_alloc_dev;
	dev_net_set(ip6n->fb_tnl_dev, net);
	ip6n->fb_tnl_dev->rtnl_link_ops = &vti6_link_ops;

	err = vti6_fb_tnl_dev_init(ip6n->fb_tnl_dev);
	if (err < 0)
		goto err_register;

	err = register_netdev(ip6n->fb_tnl_dev);
	if (err < 0)
		goto err_register;

	t = netdev_priv(ip6n->fb_tnl_dev);

	strcpy(t->parms.name, ip6n->fb_tnl_dev->name);
	return 0;

err_register:
	free_netdev(ip6n->fb_tnl_dev);
err_alloc_dev:
	return err;
}

static void __net_exit vti6_exit_batch_net(struct list_head *net_list)
{
	struct vti6_net *ip6n;
	struct net *net;
	LIST_HEAD(list);

	rtnl_lock();
	list_for_each_entry(net, net_list, exit_list) {
		ip6n = net_generic(net, vti6_net_id);
		vti6_destroy_tunnels(ip6n, &list);
	}
	unregister_netdevice_many(&list);
	rtnl_unlock();
}

static struct pernet_operations vti6_net_ops = {
	.init = vti6_init_net,
	.exit_batch = vti6_exit_batch_net,
	.id   = &vti6_net_id,
	.size = sizeof(struct vti6_net),
};

static struct xfrm6_protocol vti_esp6_protocol __read_mostly = {
	.handler	=	vti6_rcv,
	.cb_handler	=	vti6_rcv_cb,
	.err_handler	=	vti6_err,
	.priority	=	100,
};

static struct xfrm6_protocol vti_ah6_protocol __read_mostly = {
	.handler	=	vti6_rcv,
	.cb_handler	=	vti6_rcv_cb,
	.err_handler	=	vti6_err,
	.priority	=	100,
};

static struct xfrm6_protocol vti_ipcomp6_protocol __read_mostly = {
	.handler	=	vti6_rcv,
	.cb_handler	=	vti6_rcv_cb,
	.err_handler	=	vti6_err,
	.priority	=	100,
};

/**
 * vti6_tunnel_init - register protocol and reserve needed resources
 *
 * Return: 0 on success
 **/
static int __init vti6_tunnel_init(void)
{
	const char *msg;
	int err;

	msg = "tunnel device";
	err = register_pernet_device(&vti6_net_ops);
	if (err < 0)
		goto pernet_dev_failed;

	msg = "tunnel protocols";
	err = xfrm6_protocol_register(&vti_esp6_protocol, IPPROTO_ESP);
	if (err < 0)
		goto xfrm_proto_esp_failed;
	err = xfrm6_protocol_register(&vti_ah6_protocol, IPPROTO_AH);
	if (err < 0)
		goto xfrm_proto_ah_failed;
	err = xfrm6_protocol_register(&vti_ipcomp6_protocol, IPPROTO_COMP);
	if (err < 0)
		goto xfrm_proto_comp_failed;

	msg = "netlink interface";
	err = rtnl_link_register(&vti6_link_ops);
	if (err < 0)
		goto rtnl_link_failed;

	return 0;

rtnl_link_failed:
	xfrm6_protocol_deregister(&vti_ipcomp6_protocol, IPPROTO_COMP);
xfrm_proto_comp_failed:
	xfrm6_protocol_deregister(&vti_ah6_protocol, IPPROTO_AH);
xfrm_proto_ah_failed:
	xfrm6_protocol_deregister(&vti_esp6_protocol, IPPROTO_ESP);
xfrm_proto_esp_failed:
	unregister_pernet_device(&vti6_net_ops);
pernet_dev_failed:
	pr_err("vti6 init: failed to register %s\n", msg);
	return err;
}

/**
 * vti6_tunnel_cleanup - free resources and unregister protocol
 **/
static void __exit vti6_tunnel_cleanup(void)
{
	rtnl_link_unregister(&vti6_link_ops);
	xfrm6_protocol_deregister(&vti_ipcomp6_protocol, IPPROTO_COMP);
	xfrm6_protocol_deregister(&vti_ah6_protocol, IPPROTO_AH);
	xfrm6_protocol_deregister(&vti_esp6_protocol, IPPROTO_ESP);
	unregister_pernet_device(&vti6_net_ops);
}

module_init(vti6_tunnel_init);
module_exit(vti6_tunnel_cleanup);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("vti6");
MODULE_ALIAS_NETDEV("ip6_vti0");
MODULE_AUTHOR("Steffen Klassert");
MODULE_DESCRIPTION("IPv6 virtual tunnel interface");
