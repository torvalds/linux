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
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
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

#define HASH_SIZE_SHIFT  5
#define HASH_SIZE (1 << HASH_SIZE_SHIFT)

static u32 HASH(const struct in6_addr *addr1, const struct in6_addr *addr2)
{
	u32 hash = ipv6_addr_hash(addr1) ^ ipv6_addr_hash(addr2);

	return hash_32(hash, HASH_SIZE_SHIFT);
}

static int vti6_dev_init(struct net_device *dev);
static void vti6_dev_setup(struct net_device *dev);
static struct rtnl_link_ops vti6_link_ops __read_mostly;

static int vti6_net_id __read_mostly;
struct vti6_net {
	/* the vti6 tunnel fallback device */
	struct net_device *fb_tnl_dev;
	/* lists for storing tunnels in use */
	struct ip6_tnl __rcu *tnls_r_l[HASH_SIZE];
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

	for_each_vti6_tunnel_rcu(ip6n->tnls_r_l[hash]) {
		if (ipv6_addr_equal(local, &t->parms.laddr) &&
		    ipv6_addr_equal(remote, &t->parms.raddr) &&
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
	free_netdev(dev);
}

static int vti6_tnl_create2(struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct net *net = dev_net(dev);
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);
	int err;

	err = vti6_dev_init(dev);
	if (err < 0)
		goto out;

	err = register_netdevice(dev);
	if (err < 0)
		goto out;

	strcpy(t->parms.name, dev->name);
	dev->rtnl_link_ops = &vti6_link_ops;

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

	if (p->name[0])
		strlcpy(name, p->name, IFNAMSIZ);
	else
		sprintf(name, "ip6_vti%%d");

	dev = alloc_netdev(sizeof(*t), name, vti6_dev_setup);
	if (dev == NULL)
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
	vti6_dev_free(dev);
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
		    ipv6_addr_equal(remote, &t->parms.raddr))
			return t;
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
	struct net *net = dev_net(dev);
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);

	if (dev == ip6n->fb_tnl_dev)
		RCU_INIT_POINTER(ip6n->tnls_wc[0], NULL);
	else
		vti6_tnl_unlink(ip6n, t);
	ip6_tnl_dst_reset(t);
	dev_put(dev);
}

static int vti6_rcv(struct sk_buff *skb)
{
	struct ip6_tnl *t;
	const struct ipv6hdr *ipv6h = ipv6_hdr(skb);

	rcu_read_lock();

	if ((t = vti6_tnl_lookup(dev_net(skb->dev), &ipv6h->saddr,
				 &ipv6h->daddr)) != NULL) {
		struct pcpu_sw_netstats *tstats;

		if (t->parms.proto != IPPROTO_IPV6 && t->parms.proto != 0) {
			rcu_read_unlock();
			goto discard;
		}

		if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb)) {
			rcu_read_unlock();
			return 0;
		}

		if (!ip6_tnl_rcv_ctl(t, &ipv6h->daddr, &ipv6h->saddr)) {
			t->dev->stats.rx_dropped++;
			rcu_read_unlock();
			goto discard;
		}

		tstats = this_cpu_ptr(t->dev->tstats);
		u64_stats_update_begin(&tstats->syncp);
		tstats->rx_packets++;
		tstats->rx_bytes += skb->len;
		u64_stats_update_end(&tstats->syncp);

		skb->mark = 0;
		secpath_reset(skb);
		skb->dev = t->dev;

		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();
	return 1;

discard:
	kfree_skb(skb);
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

/**
 * vti6_xmit - send a packet
 *   @skb: the outgoing socket buffer
 *   @dev: the outgoing tunnel device
 **/
static int vti6_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct ip6_tnl *t = netdev_priv(dev);
	struct net_device_stats *stats = &t->dev->stats;
	struct dst_entry *dst = NULL, *ndst = NULL;
	struct flowi6 fl6;
	struct ipv6hdr *ipv6h = ipv6_hdr(skb);
	struct net_device *tdev;
	int err = -1;

	if ((t->parms.proto != IPPROTO_IPV6 && t->parms.proto != 0) ||
	    !ip6_tnl_xmit_ctl(t) || vti6_addr_conflict(t, ipv6h))
		return err;

	dst = ip6_tnl_dst_check(t);
	if (!dst) {
		memcpy(&fl6, &t->fl.u.ip6, sizeof(fl6));

		ndst = ip6_route_output(net, NULL, &fl6);

		if (ndst->error)
			goto tx_err_link_failure;
		ndst = xfrm_lookup(net, ndst, flowi6_to_flowi(&fl6), NULL, 0);
		if (IS_ERR(ndst)) {
			err = PTR_ERR(ndst);
			ndst = NULL;
			goto tx_err_link_failure;
		}
		dst = ndst;
	}

	if (!dst->xfrm || dst->xfrm->props.mode != XFRM_MODE_TUNNEL)
		goto tx_err_link_failure;

	tdev = dst->dev;

	if (tdev == dev) {
		stats->collisions++;
		net_warn_ratelimited("%s: Local routing loop detected!\n",
				     t->parms.name);
		goto tx_err_dst_release;
	}


	skb_dst_drop(skb);
	skb_dst_set_noref(skb, dst);

	ip6tunnel_xmit(skb, dev);
	if (ndst) {
		dev->mtu = dst_mtu(ndst);
		ip6_tnl_dst_store(t, ndst);
	}

	return 0;
tx_err_link_failure:
	stats->tx_carrier_errors++;
	dst_link_failure(skb);
tx_err_dst_release:
	dst_release(ndst);
	return err;
}

static netdev_tx_t
vti6_tnl_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	struct net_device_stats *stats = &t->dev->stats;
	int ret;

	switch (skb->protocol) {
	case htons(ETH_P_IPV6):
		ret = vti6_xmit(skb, dev);
		break;
	default:
		goto tx_err;
	}

	if (ret < 0)
		goto tx_err;

	return NETDEV_TX_OK;

tx_err:
	stats->tx_errors++;
	stats->tx_dropped++;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static void vti6_link_config(struct ip6_tnl *t)
{
	struct dst_entry *dst;
	struct net_device *dev = t->dev;
	struct __ip6_tnl_parm *p = &t->parms;
	struct flowi6 *fl6 = &t->fl.u.ip6;

	memcpy(dev->dev_addr, &p->laddr, sizeof(struct in6_addr));
	memcpy(dev->broadcast, &p->raddr, sizeof(struct in6_addr));

	/* Set up flowi template */
	fl6->saddr = p->laddr;
	fl6->daddr = p->raddr;
	fl6->flowi6_oif = p->link;
	fl6->flowi6_mark = be32_to_cpu(p->i_key);
	fl6->flowi6_proto = p->proto;
	fl6->flowlabel = 0;

	p->flags &= ~(IP6_TNL_F_CAP_XMIT | IP6_TNL_F_CAP_RCV |
		      IP6_TNL_F_CAP_PER_PACKET);
	p->flags |= ip6_tnl_get_cap(t, &p->laddr, &p->raddr);

	if (p->flags & IP6_TNL_F_CAP_XMIT && p->flags & IP6_TNL_F_CAP_RCV)
		dev->flags |= IFF_POINTOPOINT;
	else
		dev->flags &= ~IFF_POINTOPOINT;

	dev->iflink = p->link;

	if (p->flags & IP6_TNL_F_CAP_XMIT) {

		dst = ip6_route_output(dev_net(dev), NULL, fl6);
		if (dst->error)
			return;

		dst = xfrm_lookup(dev_net(dev), dst, flowi6_to_flowi(fl6),
				  NULL, 0);
		if (IS_ERR(dst))
			return;

		if (dst->dev) {
			dev->hard_header_len = dst->dev->hard_header_len;

			dev->mtu = dst_mtu(dst);

			if (dev->mtu < IPV6_MIN_MTU)
				dev->mtu = IPV6_MIN_MTU;
		}
		dst_release(dst);
	}
}

/**
 * vti6_tnl_change - update the tunnel parameters
 *   @t: tunnel to be changed
 *   @p: tunnel configuration parameters
 *
 * Description:
 *   vti6_tnl_change() updates the tunnel parameters
 **/
static int
vti6_tnl_change(struct ip6_tnl *t, const struct __ip6_tnl_parm *p)
{
	t->parms.laddr = p->laddr;
	t->parms.raddr = p->raddr;
	t->parms.link = p->link;
	t->parms.i_key = p->i_key;
	t->parms.o_key = p->o_key;
	t->parms.proto = p->proto;
	ip6_tnl_dst_reset(t);
	vti6_link_config(t);
	return 0;
}

static int vti6_update(struct ip6_tnl *t, struct __ip6_tnl_parm *p)
{
	struct net *net = dev_net(t->dev);
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);
	int err;

	vti6_tnl_unlink(ip6n, t);
	synchronize_net();
	err = vti6_tnl_change(t, p);
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
	u->proto = p->proto;

	memcpy(u->name, p->name, sizeof(u->name));
}

/**
 * vti6_tnl_ioctl - configure vti6 tunnels from userspace
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
		if (t == NULL)
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
			if (t != NULL) {
				if (t->dev != dev) {
					err = -EEXIST;
					break;
				}
			} else
				t = netdev_priv(dev);

			err = vti6_update(t, &p1);
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
			if (t == NULL)
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

/**
 * vti6_tnl_change_mtu - change mtu manually for tunnel device
 *   @dev: virtual device associated with tunnel
 *   @new_mtu: the new mtu
 *
 * Return:
 *   0 on success,
 *   %-EINVAL if mtu too small
 **/
static int vti6_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < IPV6_MIN_MTU)
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops vti6_netdev_ops = {
	.ndo_uninit	= vti6_dev_uninit,
	.ndo_start_xmit = vti6_tnl_xmit,
	.ndo_do_ioctl	= vti6_ioctl,
	.ndo_change_mtu = vti6_change_mtu,
	.ndo_get_stats64 = ip_tunnel_get_stats64,
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
	struct ip6_tnl *t;

	dev->netdev_ops = &vti6_netdev_ops;
	dev->destructor = vti6_dev_free;

	dev->type = ARPHRD_TUNNEL6;
	dev->hard_header_len = LL_MAX_HEADER + sizeof(struct ipv6hdr);
	dev->mtu = ETH_DATA_LEN;
	t = netdev_priv(dev);
	dev->flags |= IFF_NOARP;
	dev->addr_len = sizeof(struct in6_addr);
	dev->features |= NETIF_F_NETNS_LOCAL;
	dev->priv_flags &= ~IFF_XMIT_DST_RELEASE;
}

/**
 * vti6_dev_init_gen - general initializer for all tunnel devices
 *   @dev: virtual device associated with tunnel
 **/
static inline int vti6_dev_init_gen(struct net_device *dev)
{
	struct ip6_tnl *t = netdev_priv(dev);
	int i;

	t->dev = dev;
	t->net = dev_net(dev);
	dev->tstats = alloc_percpu(struct pcpu_sw_netstats);
	if (!dev->tstats)
		return -ENOMEM;
	for_each_possible_cpu(i) {
		struct pcpu_sw_netstats *stats;
		stats = per_cpu_ptr(dev->tstats, i);
		u64_stats_init(&stats->syncp);
	}
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
	vti6_link_config(t);
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
	int err = vti6_dev_init_gen(dev);

	if (err)
		return err;

	t->parms.proto = IPPROTO_IPV6;
	dev_hold(dev);

	vti6_link_config(t);

	rcu_assign_pointer(ip6n->tnls_wc[0], t);
	return 0;
}

static int vti6_validate(struct nlattr *tb[], struct nlattr *data[])
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
		nla_memcpy(&parms->laddr, data[IFLA_VTI_LOCAL],
			   sizeof(struct in6_addr));

	if (data[IFLA_VTI_REMOTE])
		nla_memcpy(&parms->raddr, data[IFLA_VTI_REMOTE],
			   sizeof(struct in6_addr));

	if (data[IFLA_VTI_IKEY])
		parms->i_key = nla_get_be32(data[IFLA_VTI_IKEY]);

	if (data[IFLA_VTI_OKEY])
		parms->o_key = nla_get_be32(data[IFLA_VTI_OKEY]);
}

static int vti6_newlink(struct net *src_net, struct net_device *dev,
			struct nlattr *tb[], struct nlattr *data[])
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

static int vti6_changelink(struct net_device *dev, struct nlattr *tb[],
			   struct nlattr *data[])
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

	return vti6_update(t, &p);
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
		0;
}

static int vti6_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct ip6_tnl *tunnel = netdev_priv(dev);
	struct __ip6_tnl_parm *parm = &tunnel->parms;

	if (nla_put_u32(skb, IFLA_VTI_LINK, parm->link) ||
	    nla_put(skb, IFLA_VTI_LOCAL, sizeof(struct in6_addr),
		    &parm->laddr) ||
	    nla_put(skb, IFLA_VTI_REMOTE, sizeof(struct in6_addr),
		    &parm->raddr) ||
	    nla_put_be32(skb, IFLA_VTI_IKEY, parm->i_key) ||
	    nla_put_be32(skb, IFLA_VTI_OKEY, parm->o_key))
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
};

static struct rtnl_link_ops vti6_link_ops __read_mostly = {
	.kind		= "vti6",
	.maxtype	= IFLA_VTI_MAX,
	.policy		= vti6_policy,
	.priv_size	= sizeof(struct ip6_tnl),
	.setup		= vti6_dev_setup,
	.validate	= vti6_validate,
	.newlink	= vti6_newlink,
	.changelink	= vti6_changelink,
	.get_size	= vti6_get_size,
	.fill_info	= vti6_fill_info,
};

static struct xfrm_tunnel_notifier vti6_handler __read_mostly = {
	.handler	= vti6_rcv,
	.priority	=	1,
};

static void __net_exit vti6_destroy_tunnels(struct vti6_net *ip6n)
{
	int h;
	struct ip6_tnl *t;
	LIST_HEAD(list);

	for (h = 0; h < HASH_SIZE; h++) {
		t = rtnl_dereference(ip6n->tnls_r_l[h]);
		while (t != NULL) {
			unregister_netdevice_queue(t->dev, &list);
			t = rtnl_dereference(t->next);
		}
	}

	t = rtnl_dereference(ip6n->tnls_wc[0]);
	unregister_netdevice_queue(t->dev, &list);
	unregister_netdevice_many(&list);
}

static int __net_init vti6_init_net(struct net *net)
{
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);
	struct ip6_tnl *t = NULL;
	int err;

	ip6n->tnls[0] = ip6n->tnls_wc;
	ip6n->tnls[1] = ip6n->tnls_r_l;

	err = -ENOMEM;
	ip6n->fb_tnl_dev = alloc_netdev(sizeof(struct ip6_tnl), "ip6_vti0",
					vti6_dev_setup);

	if (!ip6n->fb_tnl_dev)
		goto err_alloc_dev;
	dev_net_set(ip6n->fb_tnl_dev, net);

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
	vti6_dev_free(ip6n->fb_tnl_dev);
err_alloc_dev:
	return err;
}

static void __net_exit vti6_exit_net(struct net *net)
{
	struct vti6_net *ip6n = net_generic(net, vti6_net_id);

	rtnl_lock();
	vti6_destroy_tunnels(ip6n);
	rtnl_unlock();
}

static struct pernet_operations vti6_net_ops = {
	.init = vti6_init_net,
	.exit = vti6_exit_net,
	.id   = &vti6_net_id,
	.size = sizeof(struct vti6_net),
};

/**
 * vti6_tunnel_init - register protocol and reserve needed resources
 *
 * Return: 0 on success
 **/
static int __init vti6_tunnel_init(void)
{
	int  err;

	err = register_pernet_device(&vti6_net_ops);
	if (err < 0)
		goto out_pernet;

	err = xfrm6_mode_tunnel_input_register(&vti6_handler);
	if (err < 0) {
		pr_err("%s: can't register vti6\n", __func__);
		goto out;
	}
	err = rtnl_link_register(&vti6_link_ops);
	if (err < 0)
		goto rtnl_link_failed;

	return 0;

rtnl_link_failed:
	xfrm6_mode_tunnel_input_deregister(&vti6_handler);
out:
	unregister_pernet_device(&vti6_net_ops);
out_pernet:
	return err;
}

/**
 * vti6_tunnel_cleanup - free resources and unregister protocol
 **/
static void __exit vti6_tunnel_cleanup(void)
{
	rtnl_link_unregister(&vti6_link_ops);
	if (xfrm6_mode_tunnel_input_deregister(&vti6_handler))
		pr_info("%s: can't deregister vti6\n", __func__);

	unregister_pernet_device(&vti6_net_ops);
}

module_init(vti6_tunnel_init);
module_exit(vti6_tunnel_cleanup);
MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("vti6");
MODULE_ALIAS_NETDEV("ip6_vti0");
MODULE_AUTHOR("Steffen Klassert");
MODULE_DESCRIPTION("IPv6 virtual tunnel interface");
