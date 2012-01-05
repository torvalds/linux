/*
 *	IPv6 Address [auto]configuration
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Janos Farkas			:	delete timer on ifdown
 *	<chexum@bankinf.banki.hu>
 *	Andi Kleen			:	kill double kfree on module
 *						unload.
 *	Maciej W. Rozycki		:	FDDI support
 *	sekiya@USAGI			:	Don't send too many RS
 *						packets.
 *	yoshfuji@USAGI			:       Fixed interval between DAD
 *						packets.
 *	YOSHIFUJI Hideaki @USAGI	:	improved accuracy of
 *						address validation timer.
 *	YOSHIFUJI Hideaki @USAGI	:	Privacy Extensions (RFC3041)
 *						support.
 *	Yuji SEKIYA @USAGI		:	Don't assign a same IPv6
 *						address on a same interface.
 *	YOSHIFUJI Hideaki @USAGI	:	ARCnet support
 *	YOSHIFUJI Hideaki @USAGI	:	convert /proc/net/if_inet6 to
 *						seq_file.
 *	YOSHIFUJI Hideaki @USAGI	:	improved source address
 *						selection; consider scope,
 *						status etc.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_addr.h>
#include <linux/if_arp.h>
#include <linux/if_arcnet.h>
#include <linux/if_infiniband.h>
#include <linux/route.h>
#include <linux/inetdevice.h>
#include <linux/init.h>
#include <linux/slab.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/string.h>

#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/ndisc.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>
#include <net/tcp.h>
#include <net/ip.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <linux/if_tunnel.h>
#include <linux/rtnetlink.h>

#ifdef CONFIG_IPV6_PRIVACY
#include <linux/random.h>
#endif

#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/export.h>

/* Set to 3 to get tracing... */
#define ACONF_DEBUG 2

#if ACONF_DEBUG >= 3
#define ADBG(x) printk x
#else
#define ADBG(x)
#endif

#define	INFINITY_LIFE_TIME	0xFFFFFFFF

static inline u32 cstamp_delta(unsigned long cstamp)
{
	return (cstamp - INITIAL_JIFFIES) * 100UL / HZ;
}

#define ADDRCONF_TIMER_FUZZ_MINUS	(HZ > 50 ? HZ/50 : 1)
#define ADDRCONF_TIMER_FUZZ		(HZ / 4)
#define ADDRCONF_TIMER_FUZZ_MAX		(HZ)

#ifdef CONFIG_SYSCTL
static void addrconf_sysctl_register(struct inet6_dev *idev);
static void addrconf_sysctl_unregister(struct inet6_dev *idev);
#else
static inline void addrconf_sysctl_register(struct inet6_dev *idev)
{
}

static inline void addrconf_sysctl_unregister(struct inet6_dev *idev)
{
}
#endif

#ifdef CONFIG_IPV6_PRIVACY
static int __ipv6_regen_rndid(struct inet6_dev *idev);
static int __ipv6_try_regen_rndid(struct inet6_dev *idev, struct in6_addr *tmpaddr);
static void ipv6_regen_rndid(unsigned long data);
#endif

static int ipv6_generate_eui64(u8 *eui, struct net_device *dev);
static int ipv6_count_addresses(struct inet6_dev *idev);

/*
 *	Configured unicast address hash table
 */
static struct hlist_head inet6_addr_lst[IN6_ADDR_HSIZE];
static DEFINE_SPINLOCK(addrconf_hash_lock);

static void addrconf_verify(unsigned long);

static DEFINE_TIMER(addr_chk_timer, addrconf_verify, 0, 0);
static DEFINE_SPINLOCK(addrconf_verify_lock);

static void addrconf_join_anycast(struct inet6_ifaddr *ifp);
static void addrconf_leave_anycast(struct inet6_ifaddr *ifp);

static void addrconf_type_change(struct net_device *dev,
				 unsigned long event);
static int addrconf_ifdown(struct net_device *dev, int how);

static void addrconf_dad_start(struct inet6_ifaddr *ifp, u32 flags);
static void addrconf_dad_timer(unsigned long data);
static void addrconf_dad_completed(struct inet6_ifaddr *ifp);
static void addrconf_dad_run(struct inet6_dev *idev);
static void addrconf_rs_timer(unsigned long data);
static void __ipv6_ifa_notify(int event, struct inet6_ifaddr *ifa);
static void ipv6_ifa_notify(int event, struct inet6_ifaddr *ifa);

static void inet6_prefix_notify(int event, struct inet6_dev *idev,
				struct prefix_info *pinfo);
static bool ipv6_chk_same_addr(struct net *net, const struct in6_addr *addr,
			       struct net_device *dev);

static ATOMIC_NOTIFIER_HEAD(inet6addr_chain);

static struct ipv6_devconf ipv6_devconf __read_mostly = {
	.forwarding		= 0,
	.hop_limit		= IPV6_DEFAULT_HOPLIMIT,
	.mtu6			= IPV6_MIN_MTU,
	.accept_ra		= 1,
	.accept_redirects	= 1,
	.autoconf		= 1,
	.force_mld_version	= 0,
	.dad_transmits		= 1,
	.rtr_solicits		= MAX_RTR_SOLICITATIONS,
	.rtr_solicit_interval	= RTR_SOLICITATION_INTERVAL,
	.rtr_solicit_delay	= MAX_RTR_SOLICITATION_DELAY,
#ifdef CONFIG_IPV6_PRIVACY
	.use_tempaddr 		= 0,
	.temp_valid_lft		= TEMP_VALID_LIFETIME,
	.temp_prefered_lft	= TEMP_PREFERRED_LIFETIME,
	.regen_max_retry	= REGEN_MAX_RETRY,
	.max_desync_factor	= MAX_DESYNC_FACTOR,
#endif
	.max_addresses		= IPV6_MAX_ADDRESSES,
	.accept_ra_defrtr	= 1,
	.accept_ra_pinfo	= 1,
#ifdef CONFIG_IPV6_ROUTER_PREF
	.accept_ra_rtr_pref	= 1,
	.rtr_probe_interval	= 60 * HZ,
#ifdef CONFIG_IPV6_ROUTE_INFO
	.accept_ra_rt_info_max_plen = 0,
#endif
#endif
	.proxy_ndp		= 0,
	.accept_source_route	= 0,	/* we do not accept RH0 by default. */
	.disable_ipv6		= 0,
	.accept_dad		= 1,
};

static struct ipv6_devconf ipv6_devconf_dflt __read_mostly = {
	.forwarding		= 0,
	.hop_limit		= IPV6_DEFAULT_HOPLIMIT,
	.mtu6			= IPV6_MIN_MTU,
	.accept_ra		= 1,
	.accept_redirects	= 1,
	.autoconf		= 1,
	.dad_transmits		= 1,
	.rtr_solicits		= MAX_RTR_SOLICITATIONS,
	.rtr_solicit_interval	= RTR_SOLICITATION_INTERVAL,
	.rtr_solicit_delay	= MAX_RTR_SOLICITATION_DELAY,
#ifdef CONFIG_IPV6_PRIVACY
	.use_tempaddr		= 0,
	.temp_valid_lft		= TEMP_VALID_LIFETIME,
	.temp_prefered_lft	= TEMP_PREFERRED_LIFETIME,
	.regen_max_retry	= REGEN_MAX_RETRY,
	.max_desync_factor	= MAX_DESYNC_FACTOR,
#endif
	.max_addresses		= IPV6_MAX_ADDRESSES,
	.accept_ra_defrtr	= 1,
	.accept_ra_pinfo	= 1,
#ifdef CONFIG_IPV6_ROUTER_PREF
	.accept_ra_rtr_pref	= 1,
	.rtr_probe_interval	= 60 * HZ,
#ifdef CONFIG_IPV6_ROUTE_INFO
	.accept_ra_rt_info_max_plen = 0,
#endif
#endif
	.proxy_ndp		= 0,
	.accept_source_route	= 0,	/* we do not accept RH0 by default. */
	.disable_ipv6		= 0,
	.accept_dad		= 1,
};

/* IPv6 Wildcard Address and Loopback Address defined by RFC2553 */
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
const struct in6_addr in6addr_linklocal_allnodes = IN6ADDR_LINKLOCAL_ALLNODES_INIT;
const struct in6_addr in6addr_linklocal_allrouters = IN6ADDR_LINKLOCAL_ALLROUTERS_INIT;

/* Check if a valid qdisc is available */
static inline bool addrconf_qdisc_ok(const struct net_device *dev)
{
	return !qdisc_tx_is_noop(dev);
}

/* Check if a route is valid prefix route */
static inline int addrconf_is_prefix_route(const struct rt6_info *rt)
{
	return (rt->rt6i_flags & (RTF_GATEWAY | RTF_DEFAULT)) == 0;
}

static void addrconf_del_timer(struct inet6_ifaddr *ifp)
{
	if (del_timer(&ifp->timer))
		__in6_ifa_put(ifp);
}

enum addrconf_timer_t {
	AC_NONE,
	AC_DAD,
	AC_RS,
};

static void addrconf_mod_timer(struct inet6_ifaddr *ifp,
			       enum addrconf_timer_t what,
			       unsigned long when)
{
	if (!del_timer(&ifp->timer))
		in6_ifa_hold(ifp);

	switch (what) {
	case AC_DAD:
		ifp->timer.function = addrconf_dad_timer;
		break;
	case AC_RS:
		ifp->timer.function = addrconf_rs_timer;
		break;
	default:
		break;
	}
	ifp->timer.expires = jiffies + when;
	add_timer(&ifp->timer);
}

static int snmp6_alloc_dev(struct inet6_dev *idev)
{
	if (snmp_mib_init((void __percpu **)idev->stats.ipv6,
			  sizeof(struct ipstats_mib),
			  __alignof__(struct ipstats_mib)) < 0)
		goto err_ip;
	idev->stats.icmpv6dev = kzalloc(sizeof(struct icmpv6_mib_device),
					GFP_KERNEL);
	if (!idev->stats.icmpv6dev)
		goto err_icmp;
	idev->stats.icmpv6msgdev = kzalloc(sizeof(struct icmpv6msg_mib_device),
					   GFP_KERNEL);
	if (!idev->stats.icmpv6msgdev)
		goto err_icmpmsg;

	return 0;

err_icmpmsg:
	kfree(idev->stats.icmpv6dev);
err_icmp:
	snmp_mib_free((void __percpu **)idev->stats.ipv6);
err_ip:
	return -ENOMEM;
}

static void snmp6_free_dev(struct inet6_dev *idev)
{
	kfree(idev->stats.icmpv6msgdev);
	kfree(idev->stats.icmpv6dev);
	snmp_mib_free((void __percpu **)idev->stats.ipv6);
}

/* Nobody refers to this device, we may destroy it. */

void in6_dev_finish_destroy(struct inet6_dev *idev)
{
	struct net_device *dev = idev->dev;

	WARN_ON(!list_empty(&idev->addr_list));
	WARN_ON(idev->mc_list != NULL);

#ifdef NET_REFCNT_DEBUG
	printk(KERN_DEBUG "in6_dev_finish_destroy: %s\n", dev ? dev->name : "NIL");
#endif
	dev_put(dev);
	if (!idev->dead) {
		pr_warning("Freeing alive inet6 device %p\n", idev);
		return;
	}
	snmp6_free_dev(idev);
	kfree_rcu(idev, rcu);
}

EXPORT_SYMBOL(in6_dev_finish_destroy);

static struct inet6_dev * ipv6_add_dev(struct net_device *dev)
{
	struct inet6_dev *ndev;

	ASSERT_RTNL();

	if (dev->mtu < IPV6_MIN_MTU)
		return NULL;

	ndev = kzalloc(sizeof(struct inet6_dev), GFP_KERNEL);

	if (ndev == NULL)
		return NULL;

	rwlock_init(&ndev->lock);
	ndev->dev = dev;
	INIT_LIST_HEAD(&ndev->addr_list);

	memcpy(&ndev->cnf, dev_net(dev)->ipv6.devconf_dflt, sizeof(ndev->cnf));
	ndev->cnf.mtu6 = dev->mtu;
	ndev->cnf.sysctl = NULL;
	ndev->nd_parms = neigh_parms_alloc(dev, &nd_tbl);
	if (ndev->nd_parms == NULL) {
		kfree(ndev);
		return NULL;
	}
	if (ndev->cnf.forwarding)
		dev_disable_lro(dev);
	/* We refer to the device */
	dev_hold(dev);

	if (snmp6_alloc_dev(ndev) < 0) {
		ADBG((KERN_WARNING
			"%s(): cannot allocate memory for statistics; dev=%s.\n",
			__func__, dev->name));
		neigh_parms_release(&nd_tbl, ndev->nd_parms);
		dev_put(dev);
		kfree(ndev);
		return NULL;
	}

	if (snmp6_register_dev(ndev) < 0) {
		ADBG((KERN_WARNING
			"%s(): cannot create /proc/net/dev_snmp6/%s\n",
			__func__, dev->name));
		neigh_parms_release(&nd_tbl, ndev->nd_parms);
		ndev->dead = 1;
		in6_dev_finish_destroy(ndev);
		return NULL;
	}

	/* One reference from device.  We must do this before
	 * we invoke __ipv6_regen_rndid().
	 */
	in6_dev_hold(ndev);

	if (dev->flags & (IFF_NOARP | IFF_LOOPBACK))
		ndev->cnf.accept_dad = -1;

#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
	if (dev->type == ARPHRD_SIT && (dev->priv_flags & IFF_ISATAP)) {
		printk(KERN_INFO
		       "%s: Disabled Multicast RS\n",
		       dev->name);
		ndev->cnf.rtr_solicits = 0;
	}
#endif

#ifdef CONFIG_IPV6_PRIVACY
	INIT_LIST_HEAD(&ndev->tempaddr_list);
	setup_timer(&ndev->regen_timer, ipv6_regen_rndid, (unsigned long)ndev);
	if ((dev->flags&IFF_LOOPBACK) ||
	    dev->type == ARPHRD_TUNNEL ||
	    dev->type == ARPHRD_TUNNEL6 ||
	    dev->type == ARPHRD_SIT ||
	    dev->type == ARPHRD_NONE) {
		ndev->cnf.use_tempaddr = -1;
	} else {
		in6_dev_hold(ndev);
		ipv6_regen_rndid((unsigned long) ndev);
	}
#endif

	if (netif_running(dev) && addrconf_qdisc_ok(dev))
		ndev->if_flags |= IF_READY;

	ipv6_mc_init_dev(ndev);
	ndev->tstamp = jiffies;
	addrconf_sysctl_register(ndev);
	/* protected by rtnl_lock */
	RCU_INIT_POINTER(dev->ip6_ptr, ndev);

	/* Join all-node multicast group */
	ipv6_dev_mc_inc(dev, &in6addr_linklocal_allnodes);

	return ndev;
}

static struct inet6_dev * ipv6_find_idev(struct net_device *dev)
{
	struct inet6_dev *idev;

	ASSERT_RTNL();

	idev = __in6_dev_get(dev);
	if (!idev) {
		idev = ipv6_add_dev(dev);
		if (!idev)
			return NULL;
	}

	if (dev->flags&IFF_UP)
		ipv6_mc_up(idev);
	return idev;
}

#ifdef CONFIG_SYSCTL
static void dev_forward_change(struct inet6_dev *idev)
{
	struct net_device *dev;
	struct inet6_ifaddr *ifa;

	if (!idev)
		return;
	dev = idev->dev;
	if (idev->cnf.forwarding)
		dev_disable_lro(dev);
	if (dev && (dev->flags & IFF_MULTICAST)) {
		if (idev->cnf.forwarding)
			ipv6_dev_mc_inc(dev, &in6addr_linklocal_allrouters);
		else
			ipv6_dev_mc_dec(dev, &in6addr_linklocal_allrouters);
	}

	list_for_each_entry(ifa, &idev->addr_list, if_list) {
		if (ifa->flags&IFA_F_TENTATIVE)
			continue;
		if (idev->cnf.forwarding)
			addrconf_join_anycast(ifa);
		else
			addrconf_leave_anycast(ifa);
	}
}


static void addrconf_forward_change(struct net *net, __s32 newf)
{
	struct net_device *dev;
	struct inet6_dev *idev;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		idev = __in6_dev_get(dev);
		if (idev) {
			int changed = (!idev->cnf.forwarding) ^ (!newf);
			idev->cnf.forwarding = newf;
			if (changed)
				dev_forward_change(idev);
		}
	}
	rcu_read_unlock();
}

static int addrconf_fixup_forwarding(struct ctl_table *table, int *p, int old)
{
	struct net *net;

	net = (struct net *)table->extra2;
	if (p == &net->ipv6.devconf_dflt->forwarding)
		return 0;

	if (!rtnl_trylock()) {
		/* Restore the original values before restarting */
		*p = old;
		return restart_syscall();
	}

	if (p == &net->ipv6.devconf_all->forwarding) {
		__s32 newf = net->ipv6.devconf_all->forwarding;
		net->ipv6.devconf_dflt->forwarding = newf;
		addrconf_forward_change(net, newf);
	} else if ((!*p) ^ (!old))
		dev_forward_change((struct inet6_dev *)table->extra1);
	rtnl_unlock();

	if (*p)
		rt6_purge_dflt_routers(net);
	return 1;
}
#endif

/* Nobody refers to this ifaddr, destroy it */
void inet6_ifa_finish_destroy(struct inet6_ifaddr *ifp)
{
	WARN_ON(!hlist_unhashed(&ifp->addr_lst));

#ifdef NET_REFCNT_DEBUG
	printk(KERN_DEBUG "inet6_ifa_finish_destroy\n");
#endif

	in6_dev_put(ifp->idev);

	if (del_timer(&ifp->timer))
		pr_notice("Timer is still running, when freeing ifa=%p\n", ifp);

	if (ifp->state != INET6_IFADDR_STATE_DEAD) {
		pr_warning("Freeing alive inet6 address %p\n", ifp);
		return;
	}
	dst_release(&ifp->rt->dst);

	kfree_rcu(ifp, rcu);
}

static void
ipv6_link_dev_addr(struct inet6_dev *idev, struct inet6_ifaddr *ifp)
{
	struct list_head *p;
	int ifp_scope = ipv6_addr_src_scope(&ifp->addr);

	/*
	 * Each device address list is sorted in order of scope -
	 * global before linklocal.
	 */
	list_for_each(p, &idev->addr_list) {
		struct inet6_ifaddr *ifa
			= list_entry(p, struct inet6_ifaddr, if_list);
		if (ifp_scope >= ipv6_addr_src_scope(&ifa->addr))
			break;
	}

	list_add_tail(&ifp->if_list, p);
}

static u32 ipv6_addr_hash(const struct in6_addr *addr)
{
	/*
	 * We perform the hash function over the last 64 bits of the address
	 * This will include the IEEE address token on links that support it.
	 */
	return jhash_2words((__force u32)addr->s6_addr32[2],
			    (__force u32)addr->s6_addr32[3], 0)
		& (IN6_ADDR_HSIZE - 1);
}

/* On success it returns ifp with increased reference count */

static struct inet6_ifaddr *
ipv6_add_addr(struct inet6_dev *idev, const struct in6_addr *addr, int pfxlen,
	      int scope, u32 flags)
{
	struct inet6_ifaddr *ifa = NULL;
	struct rt6_info *rt;
	unsigned int hash;
	int err = 0;
	int addr_type = ipv6_addr_type(addr);

	if (addr_type == IPV6_ADDR_ANY ||
	    addr_type & IPV6_ADDR_MULTICAST ||
	    (!(idev->dev->flags & IFF_LOOPBACK) &&
	     addr_type & IPV6_ADDR_LOOPBACK))
		return ERR_PTR(-EADDRNOTAVAIL);

	rcu_read_lock_bh();
	if (idev->dead) {
		err = -ENODEV;			/*XXX*/
		goto out2;
	}

	if (idev->cnf.disable_ipv6) {
		err = -EACCES;
		goto out2;
	}

	spin_lock(&addrconf_hash_lock);

	/* Ignore adding duplicate addresses on an interface */
	if (ipv6_chk_same_addr(dev_net(idev->dev), addr, idev->dev)) {
		ADBG(("ipv6_add_addr: already assigned\n"));
		err = -EEXIST;
		goto out;
	}

	ifa = kzalloc(sizeof(struct inet6_ifaddr), GFP_ATOMIC);

	if (ifa == NULL) {
		ADBG(("ipv6_add_addr: malloc failed\n"));
		err = -ENOBUFS;
		goto out;
	}

	rt = addrconf_dst_alloc(idev, addr, 0);
	if (IS_ERR(rt)) {
		err = PTR_ERR(rt);
		goto out;
	}

	ipv6_addr_copy(&ifa->addr, addr);

	spin_lock_init(&ifa->lock);
	spin_lock_init(&ifa->state_lock);
	init_timer(&ifa->timer);
	INIT_HLIST_NODE(&ifa->addr_lst);
	ifa->timer.data = (unsigned long) ifa;
	ifa->scope = scope;
	ifa->prefix_len = pfxlen;
	ifa->flags = flags | IFA_F_TENTATIVE;
	ifa->cstamp = ifa->tstamp = jiffies;

	ifa->rt = rt;

	/*
	 * part one of RFC 4429, section 3.3
	 * We should not configure an address as
	 * optimistic if we do not yet know the link
	 * layer address of our nexhop router
	 */

	if (dst_get_neighbour_raw(&rt->dst) == NULL)
		ifa->flags &= ~IFA_F_OPTIMISTIC;

	ifa->idev = idev;
	in6_dev_hold(idev);
	/* For caller */
	in6_ifa_hold(ifa);

	/* Add to big hash table */
	hash = ipv6_addr_hash(addr);

	hlist_add_head_rcu(&ifa->addr_lst, &inet6_addr_lst[hash]);
	spin_unlock(&addrconf_hash_lock);

	write_lock(&idev->lock);
	/* Add to inet6_dev unicast addr list. */
	ipv6_link_dev_addr(idev, ifa);

#ifdef CONFIG_IPV6_PRIVACY
	if (ifa->flags&IFA_F_TEMPORARY) {
		list_add(&ifa->tmp_list, &idev->tempaddr_list);
		in6_ifa_hold(ifa);
	}
#endif

	in6_ifa_hold(ifa);
	write_unlock(&idev->lock);
out2:
	rcu_read_unlock_bh();

	if (likely(err == 0))
		atomic_notifier_call_chain(&inet6addr_chain, NETDEV_UP, ifa);
	else {
		kfree(ifa);
		ifa = ERR_PTR(err);
	}

	return ifa;
out:
	spin_unlock(&addrconf_hash_lock);
	goto out2;
}

/* This function wants to get referenced ifp and releases it before return */

static void ipv6_del_addr(struct inet6_ifaddr *ifp)
{
	struct inet6_ifaddr *ifa, *ifn;
	struct inet6_dev *idev = ifp->idev;
	int state;
	int deleted = 0, onlink = 0;
	unsigned long expires = jiffies;

	spin_lock_bh(&ifp->state_lock);
	state = ifp->state;
	ifp->state = INET6_IFADDR_STATE_DEAD;
	spin_unlock_bh(&ifp->state_lock);

	if (state == INET6_IFADDR_STATE_DEAD)
		goto out;

	spin_lock_bh(&addrconf_hash_lock);
	hlist_del_init_rcu(&ifp->addr_lst);
	spin_unlock_bh(&addrconf_hash_lock);

	write_lock_bh(&idev->lock);
#ifdef CONFIG_IPV6_PRIVACY
	if (ifp->flags&IFA_F_TEMPORARY) {
		list_del(&ifp->tmp_list);
		if (ifp->ifpub) {
			in6_ifa_put(ifp->ifpub);
			ifp->ifpub = NULL;
		}
		__in6_ifa_put(ifp);
	}
#endif

	list_for_each_entry_safe(ifa, ifn, &idev->addr_list, if_list) {
		if (ifa == ifp) {
			list_del_init(&ifp->if_list);
			__in6_ifa_put(ifp);

			if (!(ifp->flags & IFA_F_PERMANENT) || onlink > 0)
				break;
			deleted = 1;
			continue;
		} else if (ifp->flags & IFA_F_PERMANENT) {
			if (ipv6_prefix_equal(&ifa->addr, &ifp->addr,
					      ifp->prefix_len)) {
				if (ifa->flags & IFA_F_PERMANENT) {
					onlink = 1;
					if (deleted)
						break;
				} else {
					unsigned long lifetime;

					if (!onlink)
						onlink = -1;

					spin_lock(&ifa->lock);

					lifetime = addrconf_timeout_fixup(ifa->valid_lft, HZ);
					/*
					 * Note: Because this address is
					 * not permanent, lifetime <
					 * LONG_MAX / HZ here.
					 */
					if (time_before(expires,
							ifa->tstamp + lifetime * HZ))
						expires = ifa->tstamp + lifetime * HZ;
					spin_unlock(&ifa->lock);
				}
			}
		}
	}
	write_unlock_bh(&idev->lock);

	addrconf_del_timer(ifp);

	ipv6_ifa_notify(RTM_DELADDR, ifp);

	atomic_notifier_call_chain(&inet6addr_chain, NETDEV_DOWN, ifp);

	/*
	 * Purge or update corresponding prefix
	 *
	 * 1) we don't purge prefix here if address was not permanent.
	 *    prefix is managed by its own lifetime.
	 * 2) if there're no addresses, delete prefix.
	 * 3) if there're still other permanent address(es),
	 *    corresponding prefix is still permanent.
	 * 4) otherwise, update prefix lifetime to the
	 *    longest valid lifetime among the corresponding
	 *    addresses on the device.
	 *    Note: subsequent RA will update lifetime.
	 *
	 * --yoshfuji
	 */
	if ((ifp->flags & IFA_F_PERMANENT) && onlink < 1) {
		struct in6_addr prefix;
		struct rt6_info *rt;
		struct net *net = dev_net(ifp->idev->dev);
		ipv6_addr_prefix(&prefix, &ifp->addr, ifp->prefix_len);
		rt = rt6_lookup(net, &prefix, NULL, ifp->idev->dev->ifindex, 1);

		if (rt && addrconf_is_prefix_route(rt)) {
			if (onlink == 0) {
				ip6_del_rt(rt);
				rt = NULL;
			} else if (!(rt->rt6i_flags & RTF_EXPIRES)) {
				rt->rt6i_expires = expires;
				rt->rt6i_flags |= RTF_EXPIRES;
			}
		}
		dst_release(&rt->dst);
	}

	/* clean up prefsrc entries */
	rt6_remove_prefsrc(ifp);
out:
	in6_ifa_put(ifp);
}

#ifdef CONFIG_IPV6_PRIVACY
static int ipv6_create_tempaddr(struct inet6_ifaddr *ifp, struct inet6_ifaddr *ift)
{
	struct inet6_dev *idev = ifp->idev;
	struct in6_addr addr, *tmpaddr;
	unsigned long tmp_prefered_lft, tmp_valid_lft, tmp_tstamp, age;
	unsigned long regen_advance;
	int tmp_plen;
	int ret = 0;
	int max_addresses;
	u32 addr_flags;
	unsigned long now = jiffies;

	write_lock(&idev->lock);
	if (ift) {
		spin_lock_bh(&ift->lock);
		memcpy(&addr.s6_addr[8], &ift->addr.s6_addr[8], 8);
		spin_unlock_bh(&ift->lock);
		tmpaddr = &addr;
	} else {
		tmpaddr = NULL;
	}
retry:
	in6_dev_hold(idev);
	if (idev->cnf.use_tempaddr <= 0) {
		write_unlock(&idev->lock);
		printk(KERN_INFO
			"ipv6_create_tempaddr(): use_tempaddr is disabled.\n");
		in6_dev_put(idev);
		ret = -1;
		goto out;
	}
	spin_lock_bh(&ifp->lock);
	if (ifp->regen_count++ >= idev->cnf.regen_max_retry) {
		idev->cnf.use_tempaddr = -1;	/*XXX*/
		spin_unlock_bh(&ifp->lock);
		write_unlock(&idev->lock);
		printk(KERN_WARNING
			"ipv6_create_tempaddr(): regeneration time exceeded. disabled temporary address support.\n");
		in6_dev_put(idev);
		ret = -1;
		goto out;
	}
	in6_ifa_hold(ifp);
	memcpy(addr.s6_addr, ifp->addr.s6_addr, 8);
	if (__ipv6_try_regen_rndid(idev, tmpaddr) < 0) {
		spin_unlock_bh(&ifp->lock);
		write_unlock(&idev->lock);
		printk(KERN_WARNING
			"ipv6_create_tempaddr(): regeneration of randomized interface id failed.\n");
		in6_ifa_put(ifp);
		in6_dev_put(idev);
		ret = -1;
		goto out;
	}
	memcpy(&addr.s6_addr[8], idev->rndid, 8);
	age = (now - ifp->tstamp) / HZ;
	tmp_valid_lft = min_t(__u32,
			      ifp->valid_lft,
			      idev->cnf.temp_valid_lft + age);
	tmp_prefered_lft = min_t(__u32,
				 ifp->prefered_lft,
				 idev->cnf.temp_prefered_lft + age -
				 idev->cnf.max_desync_factor);
	tmp_plen = ifp->prefix_len;
	max_addresses = idev->cnf.max_addresses;
	tmp_tstamp = ifp->tstamp;
	spin_unlock_bh(&ifp->lock);

	regen_advance = idev->cnf.regen_max_retry *
	                idev->cnf.dad_transmits *
	                idev->nd_parms->retrans_time / HZ;
	write_unlock(&idev->lock);

	/* A temporary address is created only if this calculated Preferred
	 * Lifetime is greater than REGEN_ADVANCE time units.  In particular,
	 * an implementation must not create a temporary address with a zero
	 * Preferred Lifetime.
	 */
	if (tmp_prefered_lft <= regen_advance) {
		in6_ifa_put(ifp);
		in6_dev_put(idev);
		ret = -1;
		goto out;
	}

	addr_flags = IFA_F_TEMPORARY;
	/* set in addrconf_prefix_rcv() */
	if (ifp->flags & IFA_F_OPTIMISTIC)
		addr_flags |= IFA_F_OPTIMISTIC;

	ift = !max_addresses ||
	      ipv6_count_addresses(idev) < max_addresses ?
		ipv6_add_addr(idev, &addr, tmp_plen,
			      ipv6_addr_type(&addr)&IPV6_ADDR_SCOPE_MASK,
			      addr_flags) : NULL;
	if (!ift || IS_ERR(ift)) {
		in6_ifa_put(ifp);
		in6_dev_put(idev);
		printk(KERN_INFO
			"ipv6_create_tempaddr(): retry temporary address regeneration.\n");
		tmpaddr = &addr;
		write_lock(&idev->lock);
		goto retry;
	}

	spin_lock_bh(&ift->lock);
	ift->ifpub = ifp;
	ift->valid_lft = tmp_valid_lft;
	ift->prefered_lft = tmp_prefered_lft;
	ift->cstamp = now;
	ift->tstamp = tmp_tstamp;
	spin_unlock_bh(&ift->lock);

	addrconf_dad_start(ift, 0);
	in6_ifa_put(ift);
	in6_dev_put(idev);
out:
	return ret;
}
#endif

/*
 *	Choose an appropriate source address (RFC3484)
 */
enum {
	IPV6_SADDR_RULE_INIT = 0,
	IPV6_SADDR_RULE_LOCAL,
	IPV6_SADDR_RULE_SCOPE,
	IPV6_SADDR_RULE_PREFERRED,
#ifdef CONFIG_IPV6_MIP6
	IPV6_SADDR_RULE_HOA,
#endif
	IPV6_SADDR_RULE_OIF,
	IPV6_SADDR_RULE_LABEL,
#ifdef CONFIG_IPV6_PRIVACY
	IPV6_SADDR_RULE_PRIVACY,
#endif
	IPV6_SADDR_RULE_ORCHID,
	IPV6_SADDR_RULE_PREFIX,
	IPV6_SADDR_RULE_MAX
};

struct ipv6_saddr_score {
	int			rule;
	int			addr_type;
	struct inet6_ifaddr	*ifa;
	DECLARE_BITMAP(scorebits, IPV6_SADDR_RULE_MAX);
	int			scopedist;
	int			matchlen;
};

struct ipv6_saddr_dst {
	const struct in6_addr *addr;
	int ifindex;
	int scope;
	int label;
	unsigned int prefs;
};

static inline int ipv6_saddr_preferred(int type)
{
	if (type & (IPV6_ADDR_MAPPED|IPV6_ADDR_COMPATv4|IPV6_ADDR_LOOPBACK))
		return 1;
	return 0;
}

static int ipv6_get_saddr_eval(struct net *net,
			       struct ipv6_saddr_score *score,
			       struct ipv6_saddr_dst *dst,
			       int i)
{
	int ret;

	if (i <= score->rule) {
		switch (i) {
		case IPV6_SADDR_RULE_SCOPE:
			ret = score->scopedist;
			break;
		case IPV6_SADDR_RULE_PREFIX:
			ret = score->matchlen;
			break;
		default:
			ret = !!test_bit(i, score->scorebits);
		}
		goto out;
	}

	switch (i) {
	case IPV6_SADDR_RULE_INIT:
		/* Rule 0: remember if hiscore is not ready yet */
		ret = !!score->ifa;
		break;
	case IPV6_SADDR_RULE_LOCAL:
		/* Rule 1: Prefer same address */
		ret = ipv6_addr_equal(&score->ifa->addr, dst->addr);
		break;
	case IPV6_SADDR_RULE_SCOPE:
		/* Rule 2: Prefer appropriate scope
		 *
		 *      ret
		 *       ^
		 *    -1 |  d 15
		 *    ---+--+-+---> scope
		 *       |
		 *       |             d is scope of the destination.
		 *  B-d  |  \
		 *       |   \      <- smaller scope is better if
		 *  B-15 |    \        if scope is enough for destinaion.
		 *       |             ret = B - scope (-1 <= scope >= d <= 15).
		 * d-C-1 | /
		 *       |/         <- greater is better
		 *   -C  /             if scope is not enough for destination.
		 *      /|             ret = scope - C (-1 <= d < scope <= 15).
		 *
		 * d - C - 1 < B -15 (for all -1 <= d <= 15).
		 * C > d + 14 - B >= 15 + 14 - B = 29 - B.
		 * Assume B = 0 and we get C > 29.
		 */
		ret = __ipv6_addr_src_scope(score->addr_type);
		if (ret >= dst->scope)
			ret = -ret;
		else
			ret -= 128;	/* 30 is enough */
		score->scopedist = ret;
		break;
	case IPV6_SADDR_RULE_PREFERRED:
		/* Rule 3: Avoid deprecated and optimistic addresses */
		ret = ipv6_saddr_preferred(score->addr_type) ||
		      !(score->ifa->flags & (IFA_F_DEPRECATED|IFA_F_OPTIMISTIC));
		break;
#ifdef CONFIG_IPV6_MIP6
	case IPV6_SADDR_RULE_HOA:
	    {
		/* Rule 4: Prefer home address */
		int prefhome = !(dst->prefs & IPV6_PREFER_SRC_COA);
		ret = !(score->ifa->flags & IFA_F_HOMEADDRESS) ^ prefhome;
		break;
	    }
#endif
	case IPV6_SADDR_RULE_OIF:
		/* Rule 5: Prefer outgoing interface */
		ret = (!dst->ifindex ||
		       dst->ifindex == score->ifa->idev->dev->ifindex);
		break;
	case IPV6_SADDR_RULE_LABEL:
		/* Rule 6: Prefer matching label */
		ret = ipv6_addr_label(net,
				      &score->ifa->addr, score->addr_type,
				      score->ifa->idev->dev->ifindex) == dst->label;
		break;
#ifdef CONFIG_IPV6_PRIVACY
	case IPV6_SADDR_RULE_PRIVACY:
	    {
		/* Rule 7: Prefer public address
		 * Note: prefer temporary address if use_tempaddr >= 2
		 */
		int preftmp = dst->prefs & (IPV6_PREFER_SRC_PUBLIC|IPV6_PREFER_SRC_TMP) ?
				!!(dst->prefs & IPV6_PREFER_SRC_TMP) :
				score->ifa->idev->cnf.use_tempaddr >= 2;
		ret = (!(score->ifa->flags & IFA_F_TEMPORARY)) ^ preftmp;
		break;
	    }
#endif
	case IPV6_SADDR_RULE_ORCHID:
		/* Rule 8-: Prefer ORCHID vs ORCHID or
		 *	    non-ORCHID vs non-ORCHID
		 */
		ret = !(ipv6_addr_orchid(&score->ifa->addr) ^
			ipv6_addr_orchid(dst->addr));
		break;
	case IPV6_SADDR_RULE_PREFIX:
		/* Rule 8: Use longest matching prefix */
		score->matchlen = ret = ipv6_addr_diff(&score->ifa->addr,
						       dst->addr);
		break;
	default:
		ret = 0;
	}

	if (ret)
		__set_bit(i, score->scorebits);
	score->rule = i;
out:
	return ret;
}

int ipv6_dev_get_saddr(struct net *net, struct net_device *dst_dev,
		       const struct in6_addr *daddr, unsigned int prefs,
		       struct in6_addr *saddr)
{
	struct ipv6_saddr_score scores[2],
				*score = &scores[0], *hiscore = &scores[1];
	struct ipv6_saddr_dst dst;
	struct net_device *dev;
	int dst_type;

	dst_type = __ipv6_addr_type(daddr);
	dst.addr = daddr;
	dst.ifindex = dst_dev ? dst_dev->ifindex : 0;
	dst.scope = __ipv6_addr_src_scope(dst_type);
	dst.label = ipv6_addr_label(net, daddr, dst_type, dst.ifindex);
	dst.prefs = prefs;

	hiscore->rule = -1;
	hiscore->ifa = NULL;

	rcu_read_lock();

	for_each_netdev_rcu(net, dev) {
		struct inet6_dev *idev;

		/* Candidate Source Address (section 4)
		 *  - multicast and link-local destination address,
		 *    the set of candidate source address MUST only
		 *    include addresses assigned to interfaces
		 *    belonging to the same link as the outgoing
		 *    interface.
		 * (- For site-local destination addresses, the
		 *    set of candidate source addresses MUST only
		 *    include addresses assigned to interfaces
		 *    belonging to the same site as the outgoing
		 *    interface.)
		 */
		if (((dst_type & IPV6_ADDR_MULTICAST) ||
		     dst.scope <= IPV6_ADDR_SCOPE_LINKLOCAL) &&
		    dst.ifindex && dev->ifindex != dst.ifindex)
			continue;

		idev = __in6_dev_get(dev);
		if (!idev)
			continue;

		read_lock_bh(&idev->lock);
		list_for_each_entry(score->ifa, &idev->addr_list, if_list) {
			int i;

			/*
			 * - Tentative Address (RFC2462 section 5.4)
			 *  - A tentative address is not considered
			 *    "assigned to an interface" in the traditional
			 *    sense, unless it is also flagged as optimistic.
			 * - Candidate Source Address (section 4)
			 *  - In any case, anycast addresses, multicast
			 *    addresses, and the unspecified address MUST
			 *    NOT be included in a candidate set.
			 */
			if ((score->ifa->flags & IFA_F_TENTATIVE) &&
			    (!(score->ifa->flags & IFA_F_OPTIMISTIC)))
				continue;

			score->addr_type = __ipv6_addr_type(&score->ifa->addr);

			if (unlikely(score->addr_type == IPV6_ADDR_ANY ||
				     score->addr_type & IPV6_ADDR_MULTICAST)) {
				LIMIT_NETDEBUG(KERN_DEBUG
					       "ADDRCONF: unspecified / multicast address "
					       "assigned as unicast address on %s",
					       dev->name);
				continue;
			}

			score->rule = -1;
			bitmap_zero(score->scorebits, IPV6_SADDR_RULE_MAX);

			for (i = 0; i < IPV6_SADDR_RULE_MAX; i++) {
				int minihiscore, miniscore;

				minihiscore = ipv6_get_saddr_eval(net, hiscore, &dst, i);
				miniscore = ipv6_get_saddr_eval(net, score, &dst, i);

				if (minihiscore > miniscore) {
					if (i == IPV6_SADDR_RULE_SCOPE &&
					    score->scopedist > 0) {
						/*
						 * special case:
						 * each remaining entry
						 * has too small (not enough)
						 * scope, because ifa entries
						 * are sorted by their scope
						 * values.
						 */
						goto try_nextdev;
					}
					break;
				} else if (minihiscore < miniscore) {
					if (hiscore->ifa)
						in6_ifa_put(hiscore->ifa);

					in6_ifa_hold(score->ifa);

					swap(hiscore, score);

					/* restore our iterator */
					score->ifa = hiscore->ifa;

					break;
				}
			}
		}
try_nextdev:
		read_unlock_bh(&idev->lock);
	}
	rcu_read_unlock();

	if (!hiscore->ifa)
		return -EADDRNOTAVAIL;

	ipv6_addr_copy(saddr, &hiscore->ifa->addr);
	in6_ifa_put(hiscore->ifa);
	return 0;
}
EXPORT_SYMBOL(ipv6_dev_get_saddr);

int ipv6_get_lladdr(struct net_device *dev, struct in6_addr *addr,
		    unsigned char banned_flags)
{
	struct inet6_dev *idev;
	int err = -EADDRNOTAVAIL;

	rcu_read_lock();
	idev = __in6_dev_get(dev);
	if (idev) {
		struct inet6_ifaddr *ifp;

		read_lock_bh(&idev->lock);
		list_for_each_entry(ifp, &idev->addr_list, if_list) {
			if (ifp->scope == IFA_LINK &&
			    !(ifp->flags & banned_flags)) {
				ipv6_addr_copy(addr, &ifp->addr);
				err = 0;
				break;
			}
		}
		read_unlock_bh(&idev->lock);
	}
	rcu_read_unlock();
	return err;
}

static int ipv6_count_addresses(struct inet6_dev *idev)
{
	int cnt = 0;
	struct inet6_ifaddr *ifp;

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifp, &idev->addr_list, if_list)
		cnt++;
	read_unlock_bh(&idev->lock);
	return cnt;
}

int ipv6_chk_addr(struct net *net, const struct in6_addr *addr,
		  struct net_device *dev, int strict)
{
	struct inet6_ifaddr *ifp;
	struct hlist_node *node;
	unsigned int hash = ipv6_addr_hash(addr);

	rcu_read_lock_bh();
	hlist_for_each_entry_rcu(ifp, node, &inet6_addr_lst[hash], addr_lst) {
		if (!net_eq(dev_net(ifp->idev->dev), net))
			continue;
		if (ipv6_addr_equal(&ifp->addr, addr) &&
		    !(ifp->flags&IFA_F_TENTATIVE) &&
		    (dev == NULL || ifp->idev->dev == dev ||
		     !(ifp->scope&(IFA_LINK|IFA_HOST) || strict))) {
			rcu_read_unlock_bh();
			return 1;
		}
	}

	rcu_read_unlock_bh();
	return 0;
}
EXPORT_SYMBOL(ipv6_chk_addr);

static bool ipv6_chk_same_addr(struct net *net, const struct in6_addr *addr,
			       struct net_device *dev)
{
	unsigned int hash = ipv6_addr_hash(addr);
	struct inet6_ifaddr *ifp;
	struct hlist_node *node;

	hlist_for_each_entry(ifp, node, &inet6_addr_lst[hash], addr_lst) {
		if (!net_eq(dev_net(ifp->idev->dev), net))
			continue;
		if (ipv6_addr_equal(&ifp->addr, addr)) {
			if (dev == NULL || ifp->idev->dev == dev)
				return true;
		}
	}
	return false;
}

int ipv6_chk_prefix(const struct in6_addr *addr, struct net_device *dev)
{
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifa;
	int	onlink;

	onlink = 0;
	rcu_read_lock();
	idev = __in6_dev_get(dev);
	if (idev) {
		read_lock_bh(&idev->lock);
		list_for_each_entry(ifa, &idev->addr_list, if_list) {
			onlink = ipv6_prefix_equal(addr, &ifa->addr,
						   ifa->prefix_len);
			if (onlink)
				break;
		}
		read_unlock_bh(&idev->lock);
	}
	rcu_read_unlock();
	return onlink;
}

EXPORT_SYMBOL(ipv6_chk_prefix);

struct inet6_ifaddr *ipv6_get_ifaddr(struct net *net, const struct in6_addr *addr,
				     struct net_device *dev, int strict)
{
	struct inet6_ifaddr *ifp, *result = NULL;
	unsigned int hash = ipv6_addr_hash(addr);
	struct hlist_node *node;

	rcu_read_lock_bh();
	hlist_for_each_entry_rcu_bh(ifp, node, &inet6_addr_lst[hash], addr_lst) {
		if (!net_eq(dev_net(ifp->idev->dev), net))
			continue;
		if (ipv6_addr_equal(&ifp->addr, addr)) {
			if (dev == NULL || ifp->idev->dev == dev ||
			    !(ifp->scope&(IFA_LINK|IFA_HOST) || strict)) {
				result = ifp;
				in6_ifa_hold(ifp);
				break;
			}
		}
	}
	rcu_read_unlock_bh();

	return result;
}

/* Gets referenced address, destroys ifaddr */

static void addrconf_dad_stop(struct inet6_ifaddr *ifp, int dad_failed)
{
	if (ifp->flags&IFA_F_PERMANENT) {
		spin_lock_bh(&ifp->lock);
		addrconf_del_timer(ifp);
		ifp->flags |= IFA_F_TENTATIVE;
		if (dad_failed)
			ifp->flags |= IFA_F_DADFAILED;
		spin_unlock_bh(&ifp->lock);
		if (dad_failed)
			ipv6_ifa_notify(0, ifp);
		in6_ifa_put(ifp);
#ifdef CONFIG_IPV6_PRIVACY
	} else if (ifp->flags&IFA_F_TEMPORARY) {
		struct inet6_ifaddr *ifpub;
		spin_lock_bh(&ifp->lock);
		ifpub = ifp->ifpub;
		if (ifpub) {
			in6_ifa_hold(ifpub);
			spin_unlock_bh(&ifp->lock);
			ipv6_create_tempaddr(ifpub, ifp);
			in6_ifa_put(ifpub);
		} else {
			spin_unlock_bh(&ifp->lock);
		}
		ipv6_del_addr(ifp);
#endif
	} else
		ipv6_del_addr(ifp);
}

static int addrconf_dad_end(struct inet6_ifaddr *ifp)
{
	int err = -ENOENT;

	spin_lock(&ifp->state_lock);
	if (ifp->state == INET6_IFADDR_STATE_DAD) {
		ifp->state = INET6_IFADDR_STATE_POSTDAD;
		err = 0;
	}
	spin_unlock(&ifp->state_lock);

	return err;
}

void addrconf_dad_failure(struct inet6_ifaddr *ifp)
{
	struct inet6_dev *idev = ifp->idev;

	if (addrconf_dad_end(ifp)) {
		in6_ifa_put(ifp);
		return;
	}

	if (net_ratelimit())
		printk(KERN_INFO "%s: IPv6 duplicate address %pI6c detected!\n",
			ifp->idev->dev->name, &ifp->addr);

	if (idev->cnf.accept_dad > 1 && !idev->cnf.disable_ipv6) {
		struct in6_addr addr;

		addr.s6_addr32[0] = htonl(0xfe800000);
		addr.s6_addr32[1] = 0;

		if (!ipv6_generate_eui64(addr.s6_addr + 8, idev->dev) &&
		    ipv6_addr_equal(&ifp->addr, &addr)) {
			/* DAD failed for link-local based on MAC address */
			idev->cnf.disable_ipv6 = 1;

			printk(KERN_INFO "%s: IPv6 being disabled!\n",
				ifp->idev->dev->name);
		}
	}

	addrconf_dad_stop(ifp, 1);
}

/* Join to solicited addr multicast group. */

void addrconf_join_solict(struct net_device *dev, const struct in6_addr *addr)
{
	struct in6_addr maddr;

	if (dev->flags&(IFF_LOOPBACK|IFF_NOARP))
		return;

	addrconf_addr_solict_mult(addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);
}

void addrconf_leave_solict(struct inet6_dev *idev, const struct in6_addr *addr)
{
	struct in6_addr maddr;

	if (idev->dev->flags&(IFF_LOOPBACK|IFF_NOARP))
		return;

	addrconf_addr_solict_mult(addr, &maddr);
	__ipv6_dev_mc_dec(idev, &maddr);
}

static void addrconf_join_anycast(struct inet6_ifaddr *ifp)
{
	struct in6_addr addr;
	if (ifp->prefix_len == 127) /* RFC 6164 */
		return;
	ipv6_addr_prefix(&addr, &ifp->addr, ifp->prefix_len);
	if (ipv6_addr_any(&addr))
		return;
	ipv6_dev_ac_inc(ifp->idev->dev, &addr);
}

static void addrconf_leave_anycast(struct inet6_ifaddr *ifp)
{
	struct in6_addr addr;
	if (ifp->prefix_len == 127) /* RFC 6164 */
		return;
	ipv6_addr_prefix(&addr, &ifp->addr, ifp->prefix_len);
	if (ipv6_addr_any(&addr))
		return;
	__ipv6_dev_ac_dec(ifp->idev, &addr);
}

static int addrconf_ifid_eui48(u8 *eui, struct net_device *dev)
{
	if (dev->addr_len != ETH_ALEN)
		return -1;
	memcpy(eui, dev->dev_addr, 3);
	memcpy(eui + 5, dev->dev_addr + 3, 3);

	/*
	 * The zSeries OSA network cards can be shared among various
	 * OS instances, but the OSA cards have only one MAC address.
	 * This leads to duplicate address conflicts in conjunction
	 * with IPv6 if more than one instance uses the same card.
	 *
	 * The driver for these cards can deliver a unique 16-bit
	 * identifier for each instance sharing the same card.  It is
	 * placed instead of 0xFFFE in the interface identifier.  The
	 * "u" bit of the interface identifier is not inverted in this
	 * case.  Hence the resulting interface identifier has local
	 * scope according to RFC2373.
	 */
	if (dev->dev_id) {
		eui[3] = (dev->dev_id >> 8) & 0xFF;
		eui[4] = dev->dev_id & 0xFF;
	} else {
		eui[3] = 0xFF;
		eui[4] = 0xFE;
		eui[0] ^= 2;
	}
	return 0;
}

static int addrconf_ifid_arcnet(u8 *eui, struct net_device *dev)
{
	/* XXX: inherit EUI-64 from other interface -- yoshfuji */
	if (dev->addr_len != ARCNET_ALEN)
		return -1;
	memset(eui, 0, 7);
	eui[7] = *(u8*)dev->dev_addr;
	return 0;
}

static int addrconf_ifid_infiniband(u8 *eui, struct net_device *dev)
{
	if (dev->addr_len != INFINIBAND_ALEN)
		return -1;
	memcpy(eui, dev->dev_addr + 12, 8);
	eui[0] |= 2;
	return 0;
}

static int __ipv6_isatap_ifid(u8 *eui, __be32 addr)
{
	if (addr == 0)
		return -1;
	eui[0] = (ipv4_is_zeronet(addr) || ipv4_is_private_10(addr) ||
		  ipv4_is_loopback(addr) || ipv4_is_linklocal_169(addr) ||
		  ipv4_is_private_172(addr) || ipv4_is_test_192(addr) ||
		  ipv4_is_anycast_6to4(addr) || ipv4_is_private_192(addr) ||
		  ipv4_is_test_198(addr) || ipv4_is_multicast(addr) ||
		  ipv4_is_lbcast(addr)) ? 0x00 : 0x02;
	eui[1] = 0;
	eui[2] = 0x5E;
	eui[3] = 0xFE;
	memcpy(eui + 4, &addr, 4);
	return 0;
}

static int addrconf_ifid_sit(u8 *eui, struct net_device *dev)
{
	if (dev->priv_flags & IFF_ISATAP)
		return __ipv6_isatap_ifid(eui, *(__be32 *)dev->dev_addr);
	return -1;
}

static int addrconf_ifid_gre(u8 *eui, struct net_device *dev)
{
	return __ipv6_isatap_ifid(eui, *(__be32 *)dev->dev_addr);
}

static int ipv6_generate_eui64(u8 *eui, struct net_device *dev)
{
	switch (dev->type) {
	case ARPHRD_ETHER:
	case ARPHRD_FDDI:
	case ARPHRD_IEEE802_TR:
		return addrconf_ifid_eui48(eui, dev);
	case ARPHRD_ARCNET:
		return addrconf_ifid_arcnet(eui, dev);
	case ARPHRD_INFINIBAND:
		return addrconf_ifid_infiniband(eui, dev);
	case ARPHRD_SIT:
		return addrconf_ifid_sit(eui, dev);
	case ARPHRD_IPGRE:
		return addrconf_ifid_gre(eui, dev);
	}
	return -1;
}

static int ipv6_inherit_eui64(u8 *eui, struct inet6_dev *idev)
{
	int err = -1;
	struct inet6_ifaddr *ifp;

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifp, &idev->addr_list, if_list) {
		if (ifp->scope == IFA_LINK && !(ifp->flags&IFA_F_TENTATIVE)) {
			memcpy(eui, ifp->addr.s6_addr+8, 8);
			err = 0;
			break;
		}
	}
	read_unlock_bh(&idev->lock);
	return err;
}

#ifdef CONFIG_IPV6_PRIVACY
/* (re)generation of randomized interface identifier (RFC 3041 3.2, 3.5) */
static int __ipv6_regen_rndid(struct inet6_dev *idev)
{
regen:
	get_random_bytes(idev->rndid, sizeof(idev->rndid));
	idev->rndid[0] &= ~0x02;

	/*
	 * <draft-ietf-ipngwg-temp-addresses-v2-00.txt>:
	 * check if generated address is not inappropriate
	 *
	 *  - Reserved subnet anycast (RFC 2526)
	 *	11111101 11....11 1xxxxxxx
	 *  - ISATAP (RFC4214) 6.1
	 *	00-00-5E-FE-xx-xx-xx-xx
	 *  - value 0
	 *  - XXX: already assigned to an address on the device
	 */
	if (idev->rndid[0] == 0xfd &&
	    (idev->rndid[1]&idev->rndid[2]&idev->rndid[3]&idev->rndid[4]&idev->rndid[5]&idev->rndid[6]) == 0xff &&
	    (idev->rndid[7]&0x80))
		goto regen;
	if ((idev->rndid[0]|idev->rndid[1]) == 0) {
		if (idev->rndid[2] == 0x5e && idev->rndid[3] == 0xfe)
			goto regen;
		if ((idev->rndid[2]|idev->rndid[3]|idev->rndid[4]|idev->rndid[5]|idev->rndid[6]|idev->rndid[7]) == 0x00)
			goto regen;
	}

	return 0;
}

static void ipv6_regen_rndid(unsigned long data)
{
	struct inet6_dev *idev = (struct inet6_dev *) data;
	unsigned long expires;

	rcu_read_lock_bh();
	write_lock_bh(&idev->lock);

	if (idev->dead)
		goto out;

	if (__ipv6_regen_rndid(idev) < 0)
		goto out;

	expires = jiffies +
		idev->cnf.temp_prefered_lft * HZ -
		idev->cnf.regen_max_retry * idev->cnf.dad_transmits * idev->nd_parms->retrans_time -
		idev->cnf.max_desync_factor * HZ;
	if (time_before(expires, jiffies)) {
		printk(KERN_WARNING
			"ipv6_regen_rndid(): too short regeneration interval; timer disabled for %s.\n",
			idev->dev->name);
		goto out;
	}

	if (!mod_timer(&idev->regen_timer, expires))
		in6_dev_hold(idev);

out:
	write_unlock_bh(&idev->lock);
	rcu_read_unlock_bh();
	in6_dev_put(idev);
}

static int __ipv6_try_regen_rndid(struct inet6_dev *idev, struct in6_addr *tmpaddr) {
	int ret = 0;

	if (tmpaddr && memcmp(idev->rndid, &tmpaddr->s6_addr[8], 8) == 0)
		ret = __ipv6_regen_rndid(idev);
	return ret;
}
#endif

/*
 *	Add prefix route.
 */

static void
addrconf_prefix_route(struct in6_addr *pfx, int plen, struct net_device *dev,
		      unsigned long expires, u32 flags)
{
	struct fib6_config cfg = {
		.fc_table = RT6_TABLE_PREFIX,
		.fc_metric = IP6_RT_PRIO_ADDRCONF,
		.fc_ifindex = dev->ifindex,
		.fc_expires = expires,
		.fc_dst_len = plen,
		.fc_flags = RTF_UP | flags,
		.fc_nlinfo.nl_net = dev_net(dev),
		.fc_protocol = RTPROT_KERNEL,
	};

	ipv6_addr_copy(&cfg.fc_dst, pfx);

	/* Prevent useless cloning on PtP SIT.
	   This thing is done here expecting that the whole
	   class of non-broadcast devices need not cloning.
	 */
#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
	if (dev->type == ARPHRD_SIT && (dev->flags & IFF_POINTOPOINT))
		cfg.fc_flags |= RTF_NONEXTHOP;
#endif

	ip6_route_add(&cfg);
}


static struct rt6_info *addrconf_get_prefix_route(const struct in6_addr *pfx,
						  int plen,
						  const struct net_device *dev,
						  u32 flags, u32 noflags)
{
	struct fib6_node *fn;
	struct rt6_info *rt = NULL;
	struct fib6_table *table;

	table = fib6_get_table(dev_net(dev), RT6_TABLE_PREFIX);
	if (table == NULL)
		return NULL;

	write_lock_bh(&table->tb6_lock);
	fn = fib6_locate(&table->tb6_root, pfx, plen, NULL, 0);
	if (!fn)
		goto out;
	for (rt = fn->leaf; rt; rt = rt->dst.rt6_next) {
		if (rt->rt6i_dev->ifindex != dev->ifindex)
			continue;
		if ((rt->rt6i_flags & flags) != flags)
			continue;
		if ((noflags != 0) && ((rt->rt6i_flags & flags) != 0))
			continue;
		dst_hold(&rt->dst);
		break;
	}
out:
	write_unlock_bh(&table->tb6_lock);
	return rt;
}


/* Create "default" multicast route to the interface */

static void addrconf_add_mroute(struct net_device *dev)
{
	struct fib6_config cfg = {
		.fc_table = RT6_TABLE_LOCAL,
		.fc_metric = IP6_RT_PRIO_ADDRCONF,
		.fc_ifindex = dev->ifindex,
		.fc_dst_len = 8,
		.fc_flags = RTF_UP,
		.fc_nlinfo.nl_net = dev_net(dev),
	};

	ipv6_addr_set(&cfg.fc_dst, htonl(0xFF000000), 0, 0, 0);

	ip6_route_add(&cfg);
}

#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
static void sit_route_add(struct net_device *dev)
{
	struct fib6_config cfg = {
		.fc_table = RT6_TABLE_MAIN,
		.fc_metric = IP6_RT_PRIO_ADDRCONF,
		.fc_ifindex = dev->ifindex,
		.fc_dst_len = 96,
		.fc_flags = RTF_UP | RTF_NONEXTHOP,
		.fc_nlinfo.nl_net = dev_net(dev),
	};

	/* prefix length - 96 bits "::d.d.d.d" */
	ip6_route_add(&cfg);
}
#endif

static void addrconf_add_lroute(struct net_device *dev)
{
	struct in6_addr addr;

	ipv6_addr_set(&addr,  htonl(0xFE800000), 0, 0, 0);
	addrconf_prefix_route(&addr, 64, dev, 0, 0);
}

static struct inet6_dev *addrconf_add_dev(struct net_device *dev)
{
	struct inet6_dev *idev;

	ASSERT_RTNL();

	idev = ipv6_find_idev(dev);
	if (!idev)
		return ERR_PTR(-ENOBUFS);

	if (idev->cnf.disable_ipv6)
		return ERR_PTR(-EACCES);

	/* Add default multicast route */
	if (!(dev->flags & IFF_LOOPBACK))
		addrconf_add_mroute(dev);

	/* Add link local route */
	addrconf_add_lroute(dev);
	return idev;
}

void addrconf_prefix_rcv(struct net_device *dev, u8 *opt, int len)
{
	struct prefix_info *pinfo;
	__u32 valid_lft;
	__u32 prefered_lft;
	int addr_type;
	struct inet6_dev *in6_dev;
	struct net *net = dev_net(dev);

	pinfo = (struct prefix_info *) opt;

	if (len < sizeof(struct prefix_info)) {
		ADBG(("addrconf: prefix option too short\n"));
		return;
	}

	/*
	 *	Validation checks ([ADDRCONF], page 19)
	 */

	addr_type = ipv6_addr_type(&pinfo->prefix);

	if (addr_type & (IPV6_ADDR_MULTICAST|IPV6_ADDR_LINKLOCAL))
		return;

	valid_lft = ntohl(pinfo->valid);
	prefered_lft = ntohl(pinfo->prefered);

	if (prefered_lft > valid_lft) {
		if (net_ratelimit())
			printk(KERN_WARNING "addrconf: prefix option has invalid lifetime\n");
		return;
	}

	in6_dev = in6_dev_get(dev);

	if (in6_dev == NULL) {
		if (net_ratelimit())
			printk(KERN_DEBUG "addrconf: device %s not configured\n", dev->name);
		return;
	}

	/*
	 *	Two things going on here:
	 *	1) Add routes for on-link prefixes
	 *	2) Configure prefixes with the auto flag set
	 */

	if (pinfo->onlink) {
		struct rt6_info *rt;
		unsigned long rt_expires;

		/* Avoid arithmetic overflow. Really, we could
		 * save rt_expires in seconds, likely valid_lft,
		 * but it would require division in fib gc, that it
		 * not good.
		 */
		if (HZ > USER_HZ)
			rt_expires = addrconf_timeout_fixup(valid_lft, HZ);
		else
			rt_expires = addrconf_timeout_fixup(valid_lft, USER_HZ);

		if (addrconf_finite_timeout(rt_expires))
			rt_expires *= HZ;

		rt = addrconf_get_prefix_route(&pinfo->prefix,
					       pinfo->prefix_len,
					       dev,
					       RTF_ADDRCONF | RTF_PREFIX_RT,
					       RTF_GATEWAY | RTF_DEFAULT);

		if (rt) {
			/* Autoconf prefix route */
			if (valid_lft == 0) {
				ip6_del_rt(rt);
				rt = NULL;
			} else if (addrconf_finite_timeout(rt_expires)) {
				/* not infinity */
				rt->rt6i_expires = jiffies + rt_expires;
				rt->rt6i_flags |= RTF_EXPIRES;
			} else {
				rt->rt6i_flags &= ~RTF_EXPIRES;
				rt->rt6i_expires = 0;
			}
		} else if (valid_lft) {
			clock_t expires = 0;
			int flags = RTF_ADDRCONF | RTF_PREFIX_RT;
			if (addrconf_finite_timeout(rt_expires)) {
				/* not infinity */
				flags |= RTF_EXPIRES;
				expires = jiffies_to_clock_t(rt_expires);
			}
			addrconf_prefix_route(&pinfo->prefix, pinfo->prefix_len,
					      dev, expires, flags);
		}
		if (rt)
			dst_release(&rt->dst);
	}

	/* Try to figure out our local address for this prefix */

	if (pinfo->autoconf && in6_dev->cnf.autoconf) {
		struct inet6_ifaddr * ifp;
		struct in6_addr addr;
		int create = 0, update_lft = 0;

		if (pinfo->prefix_len == 64) {
			memcpy(&addr, &pinfo->prefix, 8);
			if (ipv6_generate_eui64(addr.s6_addr + 8, dev) &&
			    ipv6_inherit_eui64(addr.s6_addr + 8, in6_dev)) {
				in6_dev_put(in6_dev);
				return;
			}
			goto ok;
		}
		if (net_ratelimit())
			printk(KERN_DEBUG "IPv6 addrconf: prefix with wrong length %d\n",
			       pinfo->prefix_len);
		in6_dev_put(in6_dev);
		return;

ok:

		ifp = ipv6_get_ifaddr(net, &addr, dev, 1);

		if (ifp == NULL && valid_lft) {
			int max_addresses = in6_dev->cnf.max_addresses;
			u32 addr_flags = 0;

#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
			if (in6_dev->cnf.optimistic_dad &&
			    !net->ipv6.devconf_all->forwarding)
				addr_flags = IFA_F_OPTIMISTIC;
#endif

			/* Do not allow to create too much of autoconfigured
			 * addresses; this would be too easy way to crash kernel.
			 */
			if (!max_addresses ||
			    ipv6_count_addresses(in6_dev) < max_addresses)
				ifp = ipv6_add_addr(in6_dev, &addr, pinfo->prefix_len,
						    addr_type&IPV6_ADDR_SCOPE_MASK,
						    addr_flags);

			if (!ifp || IS_ERR(ifp)) {
				in6_dev_put(in6_dev);
				return;
			}

			update_lft = create = 1;
			ifp->cstamp = jiffies;
			addrconf_dad_start(ifp, RTF_ADDRCONF|RTF_PREFIX_RT);
		}

		if (ifp) {
			int flags;
			unsigned long now;
#ifdef CONFIG_IPV6_PRIVACY
			struct inet6_ifaddr *ift;
#endif
			u32 stored_lft;

			/* update lifetime (RFC2462 5.5.3 e) */
			spin_lock(&ifp->lock);
			now = jiffies;
			if (ifp->valid_lft > (now - ifp->tstamp) / HZ)
				stored_lft = ifp->valid_lft - (now - ifp->tstamp) / HZ;
			else
				stored_lft = 0;
			if (!update_lft && stored_lft) {
				if (valid_lft > MIN_VALID_LIFETIME ||
				    valid_lft > stored_lft)
					update_lft = 1;
				else if (stored_lft <= MIN_VALID_LIFETIME) {
					/* valid_lft <= stored_lft is always true */
					/*
					 * RFC 4862 Section 5.5.3e:
					 * "Note that the preferred lifetime of
					 *  the corresponding address is always
					 *  reset to the Preferred Lifetime in
					 *  the received Prefix Information
					 *  option, regardless of whether the
					 *  valid lifetime is also reset or
					 *  ignored."
					 *
					 *  So if the preferred lifetime in
					 *  this advertisement is different
					 *  than what we have stored, but the
					 *  valid lifetime is invalid, just
					 *  reset prefered_lft.
					 *
					 *  We must set the valid lifetime
					 *  to the stored lifetime since we'll
					 *  be updating the timestamp below,
					 *  else we'll set it back to the
					 *  minimum.
					 */
					if (prefered_lft != ifp->prefered_lft) {
						valid_lft = stored_lft;
						update_lft = 1;
					}
				} else {
					valid_lft = MIN_VALID_LIFETIME;
					if (valid_lft < prefered_lft)
						prefered_lft = valid_lft;
					update_lft = 1;
				}
			}

			if (update_lft) {
				ifp->valid_lft = valid_lft;
				ifp->prefered_lft = prefered_lft;
				ifp->tstamp = now;
				flags = ifp->flags;
				ifp->flags &= ~IFA_F_DEPRECATED;
				spin_unlock(&ifp->lock);

				if (!(flags&IFA_F_TENTATIVE))
					ipv6_ifa_notify(0, ifp);
			} else
				spin_unlock(&ifp->lock);

#ifdef CONFIG_IPV6_PRIVACY
			read_lock_bh(&in6_dev->lock);
			/* update all temporary addresses in the list */
			list_for_each_entry(ift, &in6_dev->tempaddr_list,
					    tmp_list) {
				int age, max_valid, max_prefered;

				if (ifp != ift->ifpub)
					continue;

				/*
				 * RFC 4941 section 3.3:
				 * If a received option will extend the lifetime
				 * of a public address, the lifetimes of
				 * temporary addresses should be extended,
				 * subject to the overall constraint that no
				 * temporary addresses should ever remain
				 * "valid" or "preferred" for a time longer than
				 * (TEMP_VALID_LIFETIME) or
				 * (TEMP_PREFERRED_LIFETIME - DESYNC_FACTOR),
				 * respectively.
				 */
				age = (now - ift->cstamp) / HZ;
				max_valid = in6_dev->cnf.temp_valid_lft - age;
				if (max_valid < 0)
					max_valid = 0;

				max_prefered = in6_dev->cnf.temp_prefered_lft -
					       in6_dev->cnf.max_desync_factor -
					       age;
				if (max_prefered < 0)
					max_prefered = 0;

				if (valid_lft > max_valid)
					valid_lft = max_valid;

				if (prefered_lft > max_prefered)
					prefered_lft = max_prefered;

				spin_lock(&ift->lock);
				flags = ift->flags;
				ift->valid_lft = valid_lft;
				ift->prefered_lft = prefered_lft;
				ift->tstamp = now;
				if (prefered_lft > 0)
					ift->flags &= ~IFA_F_DEPRECATED;

				spin_unlock(&ift->lock);
				if (!(flags&IFA_F_TENTATIVE))
					ipv6_ifa_notify(0, ift);
			}

			if ((create || list_empty(&in6_dev->tempaddr_list)) && in6_dev->cnf.use_tempaddr > 0) {
				/*
				 * When a new public address is created as
				 * described in [ADDRCONF], also create a new
				 * temporary address. Also create a temporary
				 * address if it's enabled but no temporary
				 * address currently exists.
				 */
				read_unlock_bh(&in6_dev->lock);
				ipv6_create_tempaddr(ifp, NULL);
			} else {
				read_unlock_bh(&in6_dev->lock);
			}
#endif
			in6_ifa_put(ifp);
			addrconf_verify(0);
		}
	}
	inet6_prefix_notify(RTM_NEWPREFIX, in6_dev, pinfo);
	in6_dev_put(in6_dev);
}

/*
 *	Set destination address.
 *	Special case for SIT interfaces where we create a new "virtual"
 *	device.
 */
int addrconf_set_dstaddr(struct net *net, void __user *arg)
{
	struct in6_ifreq ireq;
	struct net_device *dev;
	int err = -EINVAL;

	rtnl_lock();

	err = -EFAULT;
	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		goto err_exit;

	dev = __dev_get_by_index(net, ireq.ifr6_ifindex);

	err = -ENODEV;
	if (dev == NULL)
		goto err_exit;

#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
	if (dev->type == ARPHRD_SIT) {
		const struct net_device_ops *ops = dev->netdev_ops;
		struct ifreq ifr;
		struct ip_tunnel_parm p;

		err = -EADDRNOTAVAIL;
		if (!(ipv6_addr_type(&ireq.ifr6_addr) & IPV6_ADDR_COMPATv4))
			goto err_exit;

		memset(&p, 0, sizeof(p));
		p.iph.daddr = ireq.ifr6_addr.s6_addr32[3];
		p.iph.saddr = 0;
		p.iph.version = 4;
		p.iph.ihl = 5;
		p.iph.protocol = IPPROTO_IPV6;
		p.iph.ttl = 64;
		ifr.ifr_ifru.ifru_data = (__force void __user *)&p;

		if (ops->ndo_do_ioctl) {
			mm_segment_t oldfs = get_fs();

			set_fs(KERNEL_DS);
			err = ops->ndo_do_ioctl(dev, &ifr, SIOCADDTUNNEL);
			set_fs(oldfs);
		} else
			err = -EOPNOTSUPP;

		if (err == 0) {
			err = -ENOBUFS;
			dev = __dev_get_by_name(net, p.name);
			if (!dev)
				goto err_exit;
			err = dev_open(dev);
		}
	}
#endif

err_exit:
	rtnl_unlock();
	return err;
}

/*
 *	Manual configuration of address on an interface
 */
static int inet6_addr_add(struct net *net, int ifindex, const struct in6_addr *pfx,
			  unsigned int plen, __u8 ifa_flags, __u32 prefered_lft,
			  __u32 valid_lft)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *idev;
	struct net_device *dev;
	int scope;
	u32 flags;
	clock_t expires;
	unsigned long timeout;

	ASSERT_RTNL();

	if (plen > 128)
		return -EINVAL;

	/* check the lifetime */
	if (!valid_lft || prefered_lft > valid_lft)
		return -EINVAL;

	dev = __dev_get_by_index(net, ifindex);
	if (!dev)
		return -ENODEV;

	idev = addrconf_add_dev(dev);
	if (IS_ERR(idev))
		return PTR_ERR(idev);

	scope = ipv6_addr_scope(pfx);

	timeout = addrconf_timeout_fixup(valid_lft, HZ);
	if (addrconf_finite_timeout(timeout)) {
		expires = jiffies_to_clock_t(timeout * HZ);
		valid_lft = timeout;
		flags = RTF_EXPIRES;
	} else {
		expires = 0;
		flags = 0;
		ifa_flags |= IFA_F_PERMANENT;
	}

	timeout = addrconf_timeout_fixup(prefered_lft, HZ);
	if (addrconf_finite_timeout(timeout)) {
		if (timeout == 0)
			ifa_flags |= IFA_F_DEPRECATED;
		prefered_lft = timeout;
	}

	ifp = ipv6_add_addr(idev, pfx, plen, scope, ifa_flags);

	if (!IS_ERR(ifp)) {
		spin_lock_bh(&ifp->lock);
		ifp->valid_lft = valid_lft;
		ifp->prefered_lft = prefered_lft;
		ifp->tstamp = jiffies;
		spin_unlock_bh(&ifp->lock);

		addrconf_prefix_route(&ifp->addr, ifp->prefix_len, dev,
				      expires, flags);
		/*
		 * Note that section 3.1 of RFC 4429 indicates
		 * that the Optimistic flag should not be set for
		 * manually configured addresses
		 */
		addrconf_dad_start(ifp, 0);
		in6_ifa_put(ifp);
		addrconf_verify(0);
		return 0;
	}

	return PTR_ERR(ifp);
}

static int inet6_addr_del(struct net *net, int ifindex, const struct in6_addr *pfx,
			  unsigned int plen)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *idev;
	struct net_device *dev;

	if (plen > 128)
		return -EINVAL;

	dev = __dev_get_by_index(net, ifindex);
	if (!dev)
		return -ENODEV;

	if ((idev = __in6_dev_get(dev)) == NULL)
		return -ENXIO;

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifp, &idev->addr_list, if_list) {
		if (ifp->prefix_len == plen &&
		    ipv6_addr_equal(pfx, &ifp->addr)) {
			in6_ifa_hold(ifp);
			read_unlock_bh(&idev->lock);

			ipv6_del_addr(ifp);

			/* If the last address is deleted administratively,
			   disable IPv6 on this interface.
			 */
			if (list_empty(&idev->addr_list))
				addrconf_ifdown(idev->dev, 1);
			return 0;
		}
	}
	read_unlock_bh(&idev->lock);
	return -EADDRNOTAVAIL;
}


int addrconf_add_ifaddr(struct net *net, void __user *arg)
{
	struct in6_ifreq ireq;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		return -EFAULT;

	rtnl_lock();
	err = inet6_addr_add(net, ireq.ifr6_ifindex, &ireq.ifr6_addr,
			     ireq.ifr6_prefixlen, IFA_F_PERMANENT,
			     INFINITY_LIFE_TIME, INFINITY_LIFE_TIME);
	rtnl_unlock();
	return err;
}

int addrconf_del_ifaddr(struct net *net, void __user *arg)
{
	struct in6_ifreq ireq;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		return -EFAULT;

	rtnl_lock();
	err = inet6_addr_del(net, ireq.ifr6_ifindex, &ireq.ifr6_addr,
			     ireq.ifr6_prefixlen);
	rtnl_unlock();
	return err;
}

static void add_addr(struct inet6_dev *idev, const struct in6_addr *addr,
		     int plen, int scope)
{
	struct inet6_ifaddr *ifp;

	ifp = ipv6_add_addr(idev, addr, plen, scope, IFA_F_PERMANENT);
	if (!IS_ERR(ifp)) {
		spin_lock_bh(&ifp->lock);
		ifp->flags &= ~IFA_F_TENTATIVE;
		spin_unlock_bh(&ifp->lock);
		ipv6_ifa_notify(RTM_NEWADDR, ifp);
		in6_ifa_put(ifp);
	}
}

#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
static void sit_add_v4_addrs(struct inet6_dev *idev)
{
	struct in6_addr addr;
	struct net_device *dev;
	struct net *net = dev_net(idev->dev);
	int scope;

	ASSERT_RTNL();

	memset(&addr, 0, sizeof(struct in6_addr));
	memcpy(&addr.s6_addr32[3], idev->dev->dev_addr, 4);

	if (idev->dev->flags&IFF_POINTOPOINT) {
		addr.s6_addr32[0] = htonl(0xfe800000);
		scope = IFA_LINK;
	} else {
		scope = IPV6_ADDR_COMPATv4;
	}

	if (addr.s6_addr32[3]) {
		add_addr(idev, &addr, 128, scope);
		return;
	}

	for_each_netdev(net, dev) {
		struct in_device * in_dev = __in_dev_get_rtnl(dev);
		if (in_dev && (dev->flags & IFF_UP)) {
			struct in_ifaddr * ifa;

			int flag = scope;

			for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
				int plen;

				addr.s6_addr32[3] = ifa->ifa_local;

				if (ifa->ifa_scope == RT_SCOPE_LINK)
					continue;
				if (ifa->ifa_scope >= RT_SCOPE_HOST) {
					if (idev->dev->flags&IFF_POINTOPOINT)
						continue;
					flag |= IFA_HOST;
				}
				if (idev->dev->flags&IFF_POINTOPOINT)
					plen = 64;
				else
					plen = 96;

				add_addr(idev, &addr, plen, flag);
			}
		}
	}
}
#endif

static void init_loopback(struct net_device *dev)
{
	struct inet6_dev  *idev;

	/* ::1 */

	ASSERT_RTNL();

	if ((idev = ipv6_find_idev(dev)) == NULL) {
		printk(KERN_DEBUG "init loopback: add_dev failed\n");
		return;
	}

	add_addr(idev, &in6addr_loopback, 128, IFA_HOST);
}

static void addrconf_add_linklocal(struct inet6_dev *idev, const struct in6_addr *addr)
{
	struct inet6_ifaddr * ifp;
	u32 addr_flags = IFA_F_PERMANENT;

#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
	if (idev->cnf.optimistic_dad &&
	    !dev_net(idev->dev)->ipv6.devconf_all->forwarding)
		addr_flags |= IFA_F_OPTIMISTIC;
#endif


	ifp = ipv6_add_addr(idev, addr, 64, IFA_LINK, addr_flags);
	if (!IS_ERR(ifp)) {
		addrconf_prefix_route(&ifp->addr, ifp->prefix_len, idev->dev, 0, 0);
		addrconf_dad_start(ifp, 0);
		in6_ifa_put(ifp);
	}
}

static void addrconf_dev_config(struct net_device *dev)
{
	struct in6_addr addr;
	struct inet6_dev    * idev;

	ASSERT_RTNL();

	if ((dev->type != ARPHRD_ETHER) &&
	    (dev->type != ARPHRD_FDDI) &&
	    (dev->type != ARPHRD_IEEE802_TR) &&
	    (dev->type != ARPHRD_ARCNET) &&
	    (dev->type != ARPHRD_INFINIBAND)) {
		/* Alas, we support only Ethernet autoconfiguration. */
		return;
	}

	idev = addrconf_add_dev(dev);
	if (IS_ERR(idev))
		return;

	memset(&addr, 0, sizeof(struct in6_addr));
	addr.s6_addr32[0] = htonl(0xFE800000);

	if (ipv6_generate_eui64(addr.s6_addr + 8, dev) == 0)
		addrconf_add_linklocal(idev, &addr);
}

#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
static void addrconf_sit_config(struct net_device *dev)
{
	struct inet6_dev *idev;

	ASSERT_RTNL();

	/*
	 * Configure the tunnel with one of our IPv4
	 * addresses... we should configure all of
	 * our v4 addrs in the tunnel
	 */

	if ((idev = ipv6_find_idev(dev)) == NULL) {
		printk(KERN_DEBUG "init sit: add_dev failed\n");
		return;
	}

	if (dev->priv_flags & IFF_ISATAP) {
		struct in6_addr addr;

		ipv6_addr_set(&addr,  htonl(0xFE800000), 0, 0, 0);
		addrconf_prefix_route(&addr, 64, dev, 0, 0);
		if (!ipv6_generate_eui64(addr.s6_addr + 8, dev))
			addrconf_add_linklocal(idev, &addr);
		return;
	}

	sit_add_v4_addrs(idev);

	if (dev->flags&IFF_POINTOPOINT) {
		addrconf_add_mroute(dev);
		addrconf_add_lroute(dev);
	} else
		sit_route_add(dev);
}
#endif

#if defined(CONFIG_NET_IPGRE) || defined(CONFIG_NET_IPGRE_MODULE)
static void addrconf_gre_config(struct net_device *dev)
{
	struct inet6_dev *idev;
	struct in6_addr addr;

	pr_info("ipv6: addrconf_gre_config(%s)\n", dev->name);

	ASSERT_RTNL();

	if ((idev = ipv6_find_idev(dev)) == NULL) {
		printk(KERN_DEBUG "init gre: add_dev failed\n");
		return;
	}

	ipv6_addr_set(&addr,  htonl(0xFE800000), 0, 0, 0);
	addrconf_prefix_route(&addr, 64, dev, 0, 0);

	if (!ipv6_generate_eui64(addr.s6_addr + 8, dev))
		addrconf_add_linklocal(idev, &addr);
}
#endif

static inline int
ipv6_inherit_linklocal(struct inet6_dev *idev, struct net_device *link_dev)
{
	struct in6_addr lladdr;

	if (!ipv6_get_lladdr(link_dev, &lladdr, IFA_F_TENTATIVE)) {
		addrconf_add_linklocal(idev, &lladdr);
		return 0;
	}
	return -1;
}

static void ip6_tnl_add_linklocal(struct inet6_dev *idev)
{
	struct net_device *link_dev;
	struct net *net = dev_net(idev->dev);

	/* first try to inherit the link-local address from the link device */
	if (idev->dev->iflink &&
	    (link_dev = __dev_get_by_index(net, idev->dev->iflink))) {
		if (!ipv6_inherit_linklocal(idev, link_dev))
			return;
	}
	/* then try to inherit it from any device */
	for_each_netdev(net, link_dev) {
		if (!ipv6_inherit_linklocal(idev, link_dev))
			return;
	}
	printk(KERN_DEBUG "init ip6-ip6: add_linklocal failed\n");
}

/*
 * Autoconfigure tunnel with a link-local address so routing protocols,
 * DHCPv6, MLD etc. can be run over the virtual link
 */

static void addrconf_ip6_tnl_config(struct net_device *dev)
{
	struct inet6_dev *idev;

	ASSERT_RTNL();

	idev = addrconf_add_dev(dev);
	if (IS_ERR(idev)) {
		printk(KERN_DEBUG "init ip6-ip6: add_dev failed\n");
		return;
	}
	ip6_tnl_add_linklocal(idev);
}

static int addrconf_notify(struct notifier_block *this, unsigned long event,
			   void * data)
{
	struct net_device *dev = (struct net_device *) data;
	struct inet6_dev *idev = __in6_dev_get(dev);
	int run_pending = 0;
	int err;

	switch (event) {
	case NETDEV_REGISTER:
		if (!idev && dev->mtu >= IPV6_MIN_MTU) {
			idev = ipv6_add_dev(dev);
			if (!idev)
				return notifier_from_errno(-ENOMEM);
		}
		break;

	case NETDEV_UP:
	case NETDEV_CHANGE:
		if (dev->flags & IFF_SLAVE)
			break;

		if (event == NETDEV_UP) {
			if (!addrconf_qdisc_ok(dev)) {
				/* device is not ready yet. */
				printk(KERN_INFO
					"ADDRCONF(NETDEV_UP): %s: "
					"link is not ready\n",
					dev->name);
				break;
			}

			if (!idev && dev->mtu >= IPV6_MIN_MTU)
				idev = ipv6_add_dev(dev);

			if (idev) {
				idev->if_flags |= IF_READY;
				run_pending = 1;
			}
		} else {
			if (!addrconf_qdisc_ok(dev)) {
				/* device is still not ready. */
				break;
			}

			if (idev) {
				if (idev->if_flags & IF_READY)
					/* device is already configured. */
					break;
				idev->if_flags |= IF_READY;
			}

			printk(KERN_INFO
					"ADDRCONF(NETDEV_CHANGE): %s: "
					"link becomes ready\n",
					dev->name);

			run_pending = 1;
		}

		switch (dev->type) {
#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
		case ARPHRD_SIT:
			addrconf_sit_config(dev);
			break;
#endif
#if defined(CONFIG_NET_IPGRE) || defined(CONFIG_NET_IPGRE_MODULE)
		case ARPHRD_IPGRE:
			addrconf_gre_config(dev);
			break;
#endif
		case ARPHRD_TUNNEL6:
			addrconf_ip6_tnl_config(dev);
			break;
		case ARPHRD_LOOPBACK:
			init_loopback(dev);
			break;

		default:
			addrconf_dev_config(dev);
			break;
		}

		if (idev) {
			if (run_pending)
				addrconf_dad_run(idev);

			/*
			 * If the MTU changed during the interface down,
			 * when the interface up, the changed MTU must be
			 * reflected in the idev as well as routers.
			 */
			if (idev->cnf.mtu6 != dev->mtu &&
			    dev->mtu >= IPV6_MIN_MTU) {
				rt6_mtu_change(dev, dev->mtu);
				idev->cnf.mtu6 = dev->mtu;
			}
			idev->tstamp = jiffies;
			inet6_ifinfo_notify(RTM_NEWLINK, idev);

			/*
			 * If the changed mtu during down is lower than
			 * IPV6_MIN_MTU stop IPv6 on this interface.
			 */
			if (dev->mtu < IPV6_MIN_MTU)
				addrconf_ifdown(dev, 1);
		}
		break;

	case NETDEV_CHANGEMTU:
		if (idev && dev->mtu >= IPV6_MIN_MTU) {
			rt6_mtu_change(dev, dev->mtu);
			idev->cnf.mtu6 = dev->mtu;
			break;
		}

		if (!idev && dev->mtu >= IPV6_MIN_MTU) {
			idev = ipv6_add_dev(dev);
			if (idev)
				break;
		}

		/*
		 * MTU falled under IPV6_MIN_MTU.
		 * Stop IPv6 on this interface.
		 */

	case NETDEV_DOWN:
	case NETDEV_UNREGISTER:
		/*
		 *	Remove all addresses from this interface.
		 */
		addrconf_ifdown(dev, event != NETDEV_DOWN);
		break;

	case NETDEV_CHANGENAME:
		if (idev) {
			snmp6_unregister_dev(idev);
			addrconf_sysctl_unregister(idev);
			addrconf_sysctl_register(idev);
			err = snmp6_register_dev(idev);
			if (err)
				return notifier_from_errno(err);
		}
		break;

	case NETDEV_PRE_TYPE_CHANGE:
	case NETDEV_POST_TYPE_CHANGE:
		addrconf_type_change(dev, event);
		break;
	}

	return NOTIFY_OK;
}

/*
 *	addrconf module should be notified of a device going up
 */
static struct notifier_block ipv6_dev_notf = {
	.notifier_call = addrconf_notify,
};

static void addrconf_type_change(struct net_device *dev, unsigned long event)
{
	struct inet6_dev *idev;
	ASSERT_RTNL();

	idev = __in6_dev_get(dev);

	if (event == NETDEV_POST_TYPE_CHANGE)
		ipv6_mc_remap(idev);
	else if (event == NETDEV_PRE_TYPE_CHANGE)
		ipv6_mc_unmap(idev);
}

static int addrconf_ifdown(struct net_device *dev, int how)
{
	struct net *net = dev_net(dev);
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifa;
	int state, i;

	ASSERT_RTNL();

	rt6_ifdown(net, dev);
	neigh_ifdown(&nd_tbl, dev);

	idev = __in6_dev_get(dev);
	if (idev == NULL)
		return -ENODEV;

	/*
	 * Step 1: remove reference to ipv6 device from parent device.
	 *	   Do not dev_put!
	 */
	if (how) {
		idev->dead = 1;

		/* protected by rtnl_lock */
		RCU_INIT_POINTER(dev->ip6_ptr, NULL);

		/* Step 1.5: remove snmp6 entry */
		snmp6_unregister_dev(idev);

	}

	/* Step 2: clear hash table */
	for (i = 0; i < IN6_ADDR_HSIZE; i++) {
		struct hlist_head *h = &inet6_addr_lst[i];
		struct hlist_node *n;

		spin_lock_bh(&addrconf_hash_lock);
	restart:
		hlist_for_each_entry_rcu(ifa, n, h, addr_lst) {
			if (ifa->idev == idev) {
				hlist_del_init_rcu(&ifa->addr_lst);
				addrconf_del_timer(ifa);
				goto restart;
			}
		}
		spin_unlock_bh(&addrconf_hash_lock);
	}

	write_lock_bh(&idev->lock);

	/* Step 2: clear flags for stateless addrconf */
	if (!how)
		idev->if_flags &= ~(IF_RS_SENT|IF_RA_RCVD|IF_READY);

#ifdef CONFIG_IPV6_PRIVACY
	if (how && del_timer(&idev->regen_timer))
		in6_dev_put(idev);

	/* Step 3: clear tempaddr list */
	while (!list_empty(&idev->tempaddr_list)) {
		ifa = list_first_entry(&idev->tempaddr_list,
				       struct inet6_ifaddr, tmp_list);
		list_del(&ifa->tmp_list);
		write_unlock_bh(&idev->lock);
		spin_lock_bh(&ifa->lock);

		if (ifa->ifpub) {
			in6_ifa_put(ifa->ifpub);
			ifa->ifpub = NULL;
		}
		spin_unlock_bh(&ifa->lock);
		in6_ifa_put(ifa);
		write_lock_bh(&idev->lock);
	}
#endif

	while (!list_empty(&idev->addr_list)) {
		ifa = list_first_entry(&idev->addr_list,
				       struct inet6_ifaddr, if_list);
		addrconf_del_timer(ifa);

		list_del(&ifa->if_list);

		write_unlock_bh(&idev->lock);

		spin_lock_bh(&ifa->state_lock);
		state = ifa->state;
		ifa->state = INET6_IFADDR_STATE_DEAD;
		spin_unlock_bh(&ifa->state_lock);

		if (state != INET6_IFADDR_STATE_DEAD) {
			__ipv6_ifa_notify(RTM_DELADDR, ifa);
			atomic_notifier_call_chain(&inet6addr_chain, NETDEV_DOWN, ifa);
		}
		in6_ifa_put(ifa);

		write_lock_bh(&idev->lock);
	}

	write_unlock_bh(&idev->lock);

	/* Step 5: Discard multicast list */
	if (how)
		ipv6_mc_destroy_dev(idev);
	else
		ipv6_mc_down(idev);

	idev->tstamp = jiffies;

	/* Last: Shot the device (if unregistered) */
	if (how) {
		addrconf_sysctl_unregister(idev);
		neigh_parms_release(&nd_tbl, idev->nd_parms);
		neigh_ifdown(&nd_tbl, dev);
		in6_dev_put(idev);
	}
	return 0;
}

static void addrconf_rs_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp = (struct inet6_ifaddr *) data;
	struct inet6_dev *idev = ifp->idev;

	read_lock(&idev->lock);
	if (idev->dead || !(idev->if_flags & IF_READY))
		goto out;

	if (idev->cnf.forwarding)
		goto out;

	/* Announcement received after solicitation was sent */
	if (idev->if_flags & IF_RA_RCVD)
		goto out;

	spin_lock(&ifp->lock);
	if (ifp->probes++ < idev->cnf.rtr_solicits) {
		/* The wait after the last probe can be shorter */
		addrconf_mod_timer(ifp, AC_RS,
				   (ifp->probes == idev->cnf.rtr_solicits) ?
				   idev->cnf.rtr_solicit_delay :
				   idev->cnf.rtr_solicit_interval);
		spin_unlock(&ifp->lock);

		ndisc_send_rs(idev->dev, &ifp->addr, &in6addr_linklocal_allrouters);
	} else {
		spin_unlock(&ifp->lock);
		/*
		 * Note: we do not support deprecated "all on-link"
		 * assumption any longer.
		 */
		printk(KERN_DEBUG "%s: no IPv6 routers present\n",
		       idev->dev->name);
	}

out:
	read_unlock(&idev->lock);
	in6_ifa_put(ifp);
}

/*
 *	Duplicate Address Detection
 */
static void addrconf_dad_kick(struct inet6_ifaddr *ifp)
{
	unsigned long rand_num;
	struct inet6_dev *idev = ifp->idev;

	if (ifp->flags & IFA_F_OPTIMISTIC)
		rand_num = 0;
	else
		rand_num = net_random() % (idev->cnf.rtr_solicit_delay ? : 1);

	ifp->probes = idev->cnf.dad_transmits;
	addrconf_mod_timer(ifp, AC_DAD, rand_num);
}

static void addrconf_dad_start(struct inet6_ifaddr *ifp, u32 flags)
{
	struct inet6_dev *idev = ifp->idev;
	struct net_device *dev = idev->dev;

	addrconf_join_solict(dev, &ifp->addr);

	net_srandom(ifp->addr.s6_addr32[3]);

	read_lock_bh(&idev->lock);
	spin_lock(&ifp->lock);
	if (ifp->state == INET6_IFADDR_STATE_DEAD)
		goto out;

	if (dev->flags&(IFF_NOARP|IFF_LOOPBACK) ||
	    idev->cnf.accept_dad < 1 ||
	    !(ifp->flags&IFA_F_TENTATIVE) ||
	    ifp->flags & IFA_F_NODAD) {
		ifp->flags &= ~(IFA_F_TENTATIVE|IFA_F_OPTIMISTIC|IFA_F_DADFAILED);
		spin_unlock(&ifp->lock);
		read_unlock_bh(&idev->lock);

		addrconf_dad_completed(ifp);
		return;
	}

	if (!(idev->if_flags & IF_READY)) {
		spin_unlock(&ifp->lock);
		read_unlock_bh(&idev->lock);
		/*
		 * If the device is not ready:
		 * - keep it tentative if it is a permanent address.
		 * - otherwise, kill it.
		 */
		in6_ifa_hold(ifp);
		addrconf_dad_stop(ifp, 0);
		return;
	}

	/*
	 * Optimistic nodes can start receiving
	 * Frames right away
	 */
	if (ifp->flags & IFA_F_OPTIMISTIC)
		ip6_ins_rt(ifp->rt);

	addrconf_dad_kick(ifp);
out:
	spin_unlock(&ifp->lock);
	read_unlock_bh(&idev->lock);
}

static void addrconf_dad_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp = (struct inet6_ifaddr *) data;
	struct inet6_dev *idev = ifp->idev;
	struct in6_addr mcaddr;

	if (!ifp->probes && addrconf_dad_end(ifp))
		goto out;

	read_lock(&idev->lock);
	if (idev->dead || !(idev->if_flags & IF_READY)) {
		read_unlock(&idev->lock);
		goto out;
	}

	spin_lock(&ifp->lock);
	if (ifp->state == INET6_IFADDR_STATE_DEAD) {
		spin_unlock(&ifp->lock);
		read_unlock(&idev->lock);
		goto out;
	}

	if (ifp->probes == 0) {
		/*
		 * DAD was successful
		 */

		ifp->flags &= ~(IFA_F_TENTATIVE|IFA_F_OPTIMISTIC|IFA_F_DADFAILED);
		spin_unlock(&ifp->lock);
		read_unlock(&idev->lock);

		addrconf_dad_completed(ifp);

		goto out;
	}

	ifp->probes--;
	addrconf_mod_timer(ifp, AC_DAD, ifp->idev->nd_parms->retrans_time);
	spin_unlock(&ifp->lock);
	read_unlock(&idev->lock);

	/* send a neighbour solicitation for our addr */
	addrconf_addr_solict_mult(&ifp->addr, &mcaddr);
	ndisc_send_ns(ifp->idev->dev, NULL, &ifp->addr, &mcaddr, &in6addr_any);
out:
	in6_ifa_put(ifp);
}

static void addrconf_dad_completed(struct inet6_ifaddr *ifp)
{
	struct net_device *dev = ifp->idev->dev;

	/*
	 *	Configure the address for reception. Now it is valid.
	 */

	ipv6_ifa_notify(RTM_NEWADDR, ifp);

	/* If added prefix is link local and we are prepared to process
	   router advertisements, start sending router solicitations.
	 */

	if (((ifp->idev->cnf.accept_ra == 1 && !ifp->idev->cnf.forwarding) ||
	     ifp->idev->cnf.accept_ra == 2) &&
	    ifp->idev->cnf.rtr_solicits > 0 &&
	    (dev->flags&IFF_LOOPBACK) == 0 &&
	    (ipv6_addr_type(&ifp->addr) & IPV6_ADDR_LINKLOCAL)) {
		/*
		 *	If a host as already performed a random delay
		 *	[...] as part of DAD [...] there is no need
		 *	to delay again before sending the first RS
		 */
		ndisc_send_rs(ifp->idev->dev, &ifp->addr, &in6addr_linklocal_allrouters);

		spin_lock_bh(&ifp->lock);
		ifp->probes = 1;
		ifp->idev->if_flags |= IF_RS_SENT;
		addrconf_mod_timer(ifp, AC_RS, ifp->idev->cnf.rtr_solicit_interval);
		spin_unlock_bh(&ifp->lock);
	}
}

static void addrconf_dad_run(struct inet6_dev *idev)
{
	struct inet6_ifaddr *ifp;

	read_lock_bh(&idev->lock);
	list_for_each_entry(ifp, &idev->addr_list, if_list) {
		spin_lock(&ifp->lock);
		if (ifp->flags & IFA_F_TENTATIVE &&
		    ifp->state == INET6_IFADDR_STATE_DAD)
			addrconf_dad_kick(ifp);
		spin_unlock(&ifp->lock);
	}
	read_unlock_bh(&idev->lock);
}

#ifdef CONFIG_PROC_FS
struct if6_iter_state {
	struct seq_net_private p;
	int bucket;
};

static struct inet6_ifaddr *if6_get_first(struct seq_file *seq)
{
	struct inet6_ifaddr *ifa = NULL;
	struct if6_iter_state *state = seq->private;
	struct net *net = seq_file_net(seq);

	for (state->bucket = 0; state->bucket < IN6_ADDR_HSIZE; ++state->bucket) {
		struct hlist_node *n;
		hlist_for_each_entry_rcu_bh(ifa, n, &inet6_addr_lst[state->bucket],
					 addr_lst)
			if (net_eq(dev_net(ifa->idev->dev), net))
				return ifa;
	}
	return NULL;
}

static struct inet6_ifaddr *if6_get_next(struct seq_file *seq,
					 struct inet6_ifaddr *ifa)
{
	struct if6_iter_state *state = seq->private;
	struct net *net = seq_file_net(seq);
	struct hlist_node *n = &ifa->addr_lst;

	hlist_for_each_entry_continue_rcu_bh(ifa, n, addr_lst)
		if (net_eq(dev_net(ifa->idev->dev), net))
			return ifa;

	while (++state->bucket < IN6_ADDR_HSIZE) {
		hlist_for_each_entry_rcu_bh(ifa, n,
				     &inet6_addr_lst[state->bucket], addr_lst) {
			if (net_eq(dev_net(ifa->idev->dev), net))
				return ifa;
		}
	}

	return NULL;
}

static struct inet6_ifaddr *if6_get_idx(struct seq_file *seq, loff_t pos)
{
	struct inet6_ifaddr *ifa = if6_get_first(seq);

	if (ifa)
		while (pos && (ifa = if6_get_next(seq, ifa)) != NULL)
			--pos;
	return pos ? NULL : ifa;
}

static void *if6_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(rcu_bh)
{
	rcu_read_lock_bh();
	return if6_get_idx(seq, *pos);
}

static void *if6_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct inet6_ifaddr *ifa;

	ifa = if6_get_next(seq, v);
	++*pos;
	return ifa;
}

static void if6_seq_stop(struct seq_file *seq, void *v)
	__releases(rcu_bh)
{
	rcu_read_unlock_bh();
}

static int if6_seq_show(struct seq_file *seq, void *v)
{
	struct inet6_ifaddr *ifp = (struct inet6_ifaddr *)v;
	seq_printf(seq, "%pi6 %02x %02x %02x %02x %8s\n",
		   &ifp->addr,
		   ifp->idev->dev->ifindex,
		   ifp->prefix_len,
		   ifp->scope,
		   ifp->flags,
		   ifp->idev->dev->name);
	return 0;
}

static const struct seq_operations if6_seq_ops = {
	.start	= if6_seq_start,
	.next	= if6_seq_next,
	.show	= if6_seq_show,
	.stop	= if6_seq_stop,
};

static int if6_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &if6_seq_ops,
			    sizeof(struct if6_iter_state));
}

static const struct file_operations if6_fops = {
	.owner		= THIS_MODULE,
	.open		= if6_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_net,
};

static int __net_init if6_proc_net_init(struct net *net)
{
	if (!proc_net_fops_create(net, "if_inet6", S_IRUGO, &if6_fops))
		return -ENOMEM;
	return 0;
}

static void __net_exit if6_proc_net_exit(struct net *net)
{
       proc_net_remove(net, "if_inet6");
}

static struct pernet_operations if6_proc_net_ops = {
       .init = if6_proc_net_init,
       .exit = if6_proc_net_exit,
};

int __init if6_proc_init(void)
{
	return register_pernet_subsys(&if6_proc_net_ops);
}

void if6_proc_exit(void)
{
	unregister_pernet_subsys(&if6_proc_net_ops);
}
#endif	/* CONFIG_PROC_FS */

#if defined(CONFIG_IPV6_MIP6) || defined(CONFIG_IPV6_MIP6_MODULE)
/* Check if address is a home address configured on any interface. */
int ipv6_chk_home_addr(struct net *net, const struct in6_addr *addr)
{
	int ret = 0;
	struct inet6_ifaddr *ifp = NULL;
	struct hlist_node *n;
	unsigned int hash = ipv6_addr_hash(addr);

	rcu_read_lock_bh();
	hlist_for_each_entry_rcu_bh(ifp, n, &inet6_addr_lst[hash], addr_lst) {
		if (!net_eq(dev_net(ifp->idev->dev), net))
			continue;
		if (ipv6_addr_equal(&ifp->addr, addr) &&
		    (ifp->flags & IFA_F_HOMEADDRESS)) {
			ret = 1;
			break;
		}
	}
	rcu_read_unlock_bh();
	return ret;
}
#endif

/*
 *	Periodic address status verification
 */

static void addrconf_verify(unsigned long foo)
{
	unsigned long now, next, next_sec, next_sched;
	struct inet6_ifaddr *ifp;
	struct hlist_node *node;
	int i;

	rcu_read_lock_bh();
	spin_lock(&addrconf_verify_lock);
	now = jiffies;
	next = round_jiffies_up(now + ADDR_CHECK_FREQUENCY);

	del_timer(&addr_chk_timer);

	for (i = 0; i < IN6_ADDR_HSIZE; i++) {
restart:
		hlist_for_each_entry_rcu_bh(ifp, node,
					 &inet6_addr_lst[i], addr_lst) {
			unsigned long age;

			if (ifp->flags & IFA_F_PERMANENT)
				continue;

			spin_lock(&ifp->lock);
			/* We try to batch several events at once. */
			age = (now - ifp->tstamp + ADDRCONF_TIMER_FUZZ_MINUS) / HZ;

			if (ifp->valid_lft != INFINITY_LIFE_TIME &&
			    age >= ifp->valid_lft) {
				spin_unlock(&ifp->lock);
				in6_ifa_hold(ifp);
				ipv6_del_addr(ifp);
				goto restart;
			} else if (ifp->prefered_lft == INFINITY_LIFE_TIME) {
				spin_unlock(&ifp->lock);
				continue;
			} else if (age >= ifp->prefered_lft) {
				/* jiffies - ifp->tstamp > age >= ifp->prefered_lft */
				int deprecate = 0;

				if (!(ifp->flags&IFA_F_DEPRECATED)) {
					deprecate = 1;
					ifp->flags |= IFA_F_DEPRECATED;
				}

				if (time_before(ifp->tstamp + ifp->valid_lft * HZ, next))
					next = ifp->tstamp + ifp->valid_lft * HZ;

				spin_unlock(&ifp->lock);

				if (deprecate) {
					in6_ifa_hold(ifp);

					ipv6_ifa_notify(0, ifp);
					in6_ifa_put(ifp);
					goto restart;
				}
#ifdef CONFIG_IPV6_PRIVACY
			} else if ((ifp->flags&IFA_F_TEMPORARY) &&
				   !(ifp->flags&IFA_F_TENTATIVE)) {
				unsigned long regen_advance = ifp->idev->cnf.regen_max_retry *
					ifp->idev->cnf.dad_transmits *
					ifp->idev->nd_parms->retrans_time / HZ;

				if (age >= ifp->prefered_lft - regen_advance) {
					struct inet6_ifaddr *ifpub = ifp->ifpub;
					if (time_before(ifp->tstamp + ifp->prefered_lft * HZ, next))
						next = ifp->tstamp + ifp->prefered_lft * HZ;
					if (!ifp->regen_count && ifpub) {
						ifp->regen_count++;
						in6_ifa_hold(ifp);
						in6_ifa_hold(ifpub);
						spin_unlock(&ifp->lock);

						spin_lock(&ifpub->lock);
						ifpub->regen_count = 0;
						spin_unlock(&ifpub->lock);
						ipv6_create_tempaddr(ifpub, ifp);
						in6_ifa_put(ifpub);
						in6_ifa_put(ifp);
						goto restart;
					}
				} else if (time_before(ifp->tstamp + ifp->prefered_lft * HZ - regen_advance * HZ, next))
					next = ifp->tstamp + ifp->prefered_lft * HZ - regen_advance * HZ;
				spin_unlock(&ifp->lock);
#endif
			} else {
				/* ifp->prefered_lft <= ifp->valid_lft */
				if (time_before(ifp->tstamp + ifp->prefered_lft * HZ, next))
					next = ifp->tstamp + ifp->prefered_lft * HZ;
				spin_unlock(&ifp->lock);
			}
		}
	}

	next_sec = round_jiffies_up(next);
	next_sched = next;

	/* If rounded timeout is accurate enough, accept it. */
	if (time_before(next_sec, next + ADDRCONF_TIMER_FUZZ))
		next_sched = next_sec;

	/* And minimum interval is ADDRCONF_TIMER_FUZZ_MAX. */
	if (time_before(next_sched, jiffies + ADDRCONF_TIMER_FUZZ_MAX))
		next_sched = jiffies + ADDRCONF_TIMER_FUZZ_MAX;

	ADBG((KERN_DEBUG "now = %lu, schedule = %lu, rounded schedule = %lu => %lu\n",
	      now, next, next_sec, next_sched));

	addr_chk_timer.expires = next_sched;
	add_timer(&addr_chk_timer);
	spin_unlock(&addrconf_verify_lock);
	rcu_read_unlock_bh();
}

static struct in6_addr *extract_addr(struct nlattr *addr, struct nlattr *local)
{
	struct in6_addr *pfx = NULL;

	if (addr)
		pfx = nla_data(addr);

	if (local) {
		if (pfx && nla_memcmp(local, pfx, sizeof(*pfx)))
			pfx = NULL;
		else
			pfx = nla_data(local);
	}

	return pfx;
}

static const struct nla_policy ifa_ipv6_policy[IFA_MAX+1] = {
	[IFA_ADDRESS]		= { .len = sizeof(struct in6_addr) },
	[IFA_LOCAL]		= { .len = sizeof(struct in6_addr) },
	[IFA_CACHEINFO]		= { .len = sizeof(struct ifa_cacheinfo) },
};

static int
inet6_rtm_deladdr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct ifaddrmsg *ifm;
	struct nlattr *tb[IFA_MAX+1];
	struct in6_addr *pfx;
	int err;

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, ifa_ipv6_policy);
	if (err < 0)
		return err;

	ifm = nlmsg_data(nlh);
	pfx = extract_addr(tb[IFA_ADDRESS], tb[IFA_LOCAL]);
	if (pfx == NULL)
		return -EINVAL;

	return inet6_addr_del(net, ifm->ifa_index, pfx, ifm->ifa_prefixlen);
}

static int inet6_addr_modify(struct inet6_ifaddr *ifp, u8 ifa_flags,
			     u32 prefered_lft, u32 valid_lft)
{
	u32 flags;
	clock_t expires;
	unsigned long timeout;

	if (!valid_lft || (prefered_lft > valid_lft))
		return -EINVAL;

	timeout = addrconf_timeout_fixup(valid_lft, HZ);
	if (addrconf_finite_timeout(timeout)) {
		expires = jiffies_to_clock_t(timeout * HZ);
		valid_lft = timeout;
		flags = RTF_EXPIRES;
	} else {
		expires = 0;
		flags = 0;
		ifa_flags |= IFA_F_PERMANENT;
	}

	timeout = addrconf_timeout_fixup(prefered_lft, HZ);
	if (addrconf_finite_timeout(timeout)) {
		if (timeout == 0)
			ifa_flags |= IFA_F_DEPRECATED;
		prefered_lft = timeout;
	}

	spin_lock_bh(&ifp->lock);
	ifp->flags = (ifp->flags & ~(IFA_F_DEPRECATED | IFA_F_PERMANENT | IFA_F_NODAD | IFA_F_HOMEADDRESS)) | ifa_flags;
	ifp->tstamp = jiffies;
	ifp->valid_lft = valid_lft;
	ifp->prefered_lft = prefered_lft;

	spin_unlock_bh(&ifp->lock);
	if (!(ifp->flags&IFA_F_TENTATIVE))
		ipv6_ifa_notify(0, ifp);

	addrconf_prefix_route(&ifp->addr, ifp->prefix_len, ifp->idev->dev,
			      expires, flags);
	addrconf_verify(0);

	return 0;
}

static int
inet6_rtm_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct ifaddrmsg *ifm;
	struct nlattr *tb[IFA_MAX+1];
	struct in6_addr *pfx;
	struct inet6_ifaddr *ifa;
	struct net_device *dev;
	u32 valid_lft = INFINITY_LIFE_TIME, preferred_lft = INFINITY_LIFE_TIME;
	u8 ifa_flags;
	int err;

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, ifa_ipv6_policy);
	if (err < 0)
		return err;

	ifm = nlmsg_data(nlh);
	pfx = extract_addr(tb[IFA_ADDRESS], tb[IFA_LOCAL]);
	if (pfx == NULL)
		return -EINVAL;

	if (tb[IFA_CACHEINFO]) {
		struct ifa_cacheinfo *ci;

		ci = nla_data(tb[IFA_CACHEINFO]);
		valid_lft = ci->ifa_valid;
		preferred_lft = ci->ifa_prefered;
	} else {
		preferred_lft = INFINITY_LIFE_TIME;
		valid_lft = INFINITY_LIFE_TIME;
	}

	dev =  __dev_get_by_index(net, ifm->ifa_index);
	if (dev == NULL)
		return -ENODEV;

	/* We ignore other flags so far. */
	ifa_flags = ifm->ifa_flags & (IFA_F_NODAD | IFA_F_HOMEADDRESS);

	ifa = ipv6_get_ifaddr(net, pfx, dev, 1);
	if (ifa == NULL) {
		/*
		 * It would be best to check for !NLM_F_CREATE here but
		 * userspace alreay relies on not having to provide this.
		 */
		return inet6_addr_add(net, ifm->ifa_index, pfx,
				      ifm->ifa_prefixlen, ifa_flags,
				      preferred_lft, valid_lft);
	}

	if (nlh->nlmsg_flags & NLM_F_EXCL ||
	    !(nlh->nlmsg_flags & NLM_F_REPLACE))
		err = -EEXIST;
	else
		err = inet6_addr_modify(ifa, ifa_flags, preferred_lft, valid_lft);

	in6_ifa_put(ifa);

	return err;
}

static void put_ifaddrmsg(struct nlmsghdr *nlh, u8 prefixlen, u8 flags,
			  u8 scope, int ifindex)
{
	struct ifaddrmsg *ifm;

	ifm = nlmsg_data(nlh);
	ifm->ifa_family = AF_INET6;
	ifm->ifa_prefixlen = prefixlen;
	ifm->ifa_flags = flags;
	ifm->ifa_scope = scope;
	ifm->ifa_index = ifindex;
}

static int put_cacheinfo(struct sk_buff *skb, unsigned long cstamp,
			 unsigned long tstamp, u32 preferred, u32 valid)
{
	struct ifa_cacheinfo ci;

	ci.cstamp = cstamp_delta(cstamp);
	ci.tstamp = cstamp_delta(tstamp);
	ci.ifa_prefered = preferred;
	ci.ifa_valid = valid;

	return nla_put(skb, IFA_CACHEINFO, sizeof(ci), &ci);
}

static inline int rt_scope(int ifa_scope)
{
	if (ifa_scope & IFA_HOST)
		return RT_SCOPE_HOST;
	else if (ifa_scope & IFA_LINK)
		return RT_SCOPE_LINK;
	else if (ifa_scope & IFA_SITE)
		return RT_SCOPE_SITE;
	else
		return RT_SCOPE_UNIVERSE;
}

static inline int inet6_ifaddr_msgsize(void)
{
	return NLMSG_ALIGN(sizeof(struct ifaddrmsg))
	       + nla_total_size(16) /* IFA_ADDRESS */
	       + nla_total_size(sizeof(struct ifa_cacheinfo));
}

static int inet6_fill_ifaddr(struct sk_buff *skb, struct inet6_ifaddr *ifa,
			     u32 pid, u32 seq, int event, unsigned int flags)
{
	struct nlmsghdr  *nlh;
	u32 preferred, valid;

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(struct ifaddrmsg), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	put_ifaddrmsg(nlh, ifa->prefix_len, ifa->flags, rt_scope(ifa->scope),
		      ifa->idev->dev->ifindex);

	if (!(ifa->flags&IFA_F_PERMANENT)) {
		preferred = ifa->prefered_lft;
		valid = ifa->valid_lft;
		if (preferred != INFINITY_LIFE_TIME) {
			long tval = (jiffies - ifa->tstamp)/HZ;
			if (preferred > tval)
				preferred -= tval;
			else
				preferred = 0;
			if (valid != INFINITY_LIFE_TIME) {
				if (valid > tval)
					valid -= tval;
				else
					valid = 0;
			}
		}
	} else {
		preferred = INFINITY_LIFE_TIME;
		valid = INFINITY_LIFE_TIME;
	}

	if (nla_put(skb, IFA_ADDRESS, 16, &ifa->addr) < 0 ||
	    put_cacheinfo(skb, ifa->cstamp, ifa->tstamp, preferred, valid) < 0) {
		nlmsg_cancel(skb, nlh);
		return -EMSGSIZE;
	}

	return nlmsg_end(skb, nlh);
}

static int inet6_fill_ifmcaddr(struct sk_buff *skb, struct ifmcaddr6 *ifmca,
				u32 pid, u32 seq, int event, u16 flags)
{
	struct nlmsghdr  *nlh;
	u8 scope = RT_SCOPE_UNIVERSE;
	int ifindex = ifmca->idev->dev->ifindex;

	if (ipv6_addr_scope(&ifmca->mca_addr) & IFA_SITE)
		scope = RT_SCOPE_SITE;

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(struct ifaddrmsg), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	put_ifaddrmsg(nlh, 128, IFA_F_PERMANENT, scope, ifindex);
	if (nla_put(skb, IFA_MULTICAST, 16, &ifmca->mca_addr) < 0 ||
	    put_cacheinfo(skb, ifmca->mca_cstamp, ifmca->mca_tstamp,
			  INFINITY_LIFE_TIME, INFINITY_LIFE_TIME) < 0) {
		nlmsg_cancel(skb, nlh);
		return -EMSGSIZE;
	}

	return nlmsg_end(skb, nlh);
}

static int inet6_fill_ifacaddr(struct sk_buff *skb, struct ifacaddr6 *ifaca,
				u32 pid, u32 seq, int event, unsigned int flags)
{
	struct nlmsghdr  *nlh;
	u8 scope = RT_SCOPE_UNIVERSE;
	int ifindex = ifaca->aca_idev->dev->ifindex;

	if (ipv6_addr_scope(&ifaca->aca_addr) & IFA_SITE)
		scope = RT_SCOPE_SITE;

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(struct ifaddrmsg), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	put_ifaddrmsg(nlh, 128, IFA_F_PERMANENT, scope, ifindex);
	if (nla_put(skb, IFA_ANYCAST, 16, &ifaca->aca_addr) < 0 ||
	    put_cacheinfo(skb, ifaca->aca_cstamp, ifaca->aca_tstamp,
			  INFINITY_LIFE_TIME, INFINITY_LIFE_TIME) < 0) {
		nlmsg_cancel(skb, nlh);
		return -EMSGSIZE;
	}

	return nlmsg_end(skb, nlh);
}

enum addr_type_t {
	UNICAST_ADDR,
	MULTICAST_ADDR,
	ANYCAST_ADDR,
};

/* called with rcu_read_lock() */
static int in6_dump_addrs(struct inet6_dev *idev, struct sk_buff *skb,
			  struct netlink_callback *cb, enum addr_type_t type,
			  int s_ip_idx, int *p_ip_idx)
{
	struct ifmcaddr6 *ifmca;
	struct ifacaddr6 *ifaca;
	int err = 1;
	int ip_idx = *p_ip_idx;

	read_lock_bh(&idev->lock);
	switch (type) {
	case UNICAST_ADDR: {
		struct inet6_ifaddr *ifa;

		/* unicast address incl. temp addr */
		list_for_each_entry(ifa, &idev->addr_list, if_list) {
			if (++ip_idx < s_ip_idx)
				continue;
			err = inet6_fill_ifaddr(skb, ifa,
						NETLINK_CB(cb->skb).pid,
						cb->nlh->nlmsg_seq,
						RTM_NEWADDR,
						NLM_F_MULTI);
			if (err <= 0)
				break;
		}
		break;
	}
	case MULTICAST_ADDR:
		/* multicast address */
		for (ifmca = idev->mc_list; ifmca;
		     ifmca = ifmca->next, ip_idx++) {
			if (ip_idx < s_ip_idx)
				continue;
			err = inet6_fill_ifmcaddr(skb, ifmca,
						  NETLINK_CB(cb->skb).pid,
						  cb->nlh->nlmsg_seq,
						  RTM_GETMULTICAST,
						  NLM_F_MULTI);
			if (err <= 0)
				break;
		}
		break;
	case ANYCAST_ADDR:
		/* anycast address */
		for (ifaca = idev->ac_list; ifaca;
		     ifaca = ifaca->aca_next, ip_idx++) {
			if (ip_idx < s_ip_idx)
				continue;
			err = inet6_fill_ifacaddr(skb, ifaca,
						  NETLINK_CB(cb->skb).pid,
						  cb->nlh->nlmsg_seq,
						  RTM_GETANYCAST,
						  NLM_F_MULTI);
			if (err <= 0)
				break;
		}
		break;
	default:
		break;
	}
	read_unlock_bh(&idev->lock);
	*p_ip_idx = ip_idx;
	return err;
}

static int inet6_dump_addr(struct sk_buff *skb, struct netlink_callback *cb,
			   enum addr_type_t type)
{
	struct net *net = sock_net(skb->sk);
	int h, s_h;
	int idx, ip_idx;
	int s_idx, s_ip_idx;
	struct net_device *dev;
	struct inet6_dev *idev;
	struct hlist_head *head;
	struct hlist_node *node;

	s_h = cb->args[0];
	s_idx = idx = cb->args[1];
	s_ip_idx = ip_idx = cb->args[2];

	rcu_read_lock();
	for (h = s_h; h < NETDEV_HASHENTRIES; h++, s_idx = 0) {
		idx = 0;
		head = &net->dev_index_head[h];
		hlist_for_each_entry_rcu(dev, node, head, index_hlist) {
			if (idx < s_idx)
				goto cont;
			if (h > s_h || idx > s_idx)
				s_ip_idx = 0;
			ip_idx = 0;
			idev = __in6_dev_get(dev);
			if (!idev)
				goto cont;

			if (in6_dump_addrs(idev, skb, cb, type,
					   s_ip_idx, &ip_idx) <= 0)
				goto done;
cont:
			idx++;
		}
	}
done:
	rcu_read_unlock();
	cb->args[0] = h;
	cb->args[1] = idx;
	cb->args[2] = ip_idx;

	return skb->len;
}

static int inet6_dump_ifaddr(struct sk_buff *skb, struct netlink_callback *cb)
{
	enum addr_type_t type = UNICAST_ADDR;

	return inet6_dump_addr(skb, cb, type);
}

static int inet6_dump_ifmcaddr(struct sk_buff *skb, struct netlink_callback *cb)
{
	enum addr_type_t type = MULTICAST_ADDR;

	return inet6_dump_addr(skb, cb, type);
}


static int inet6_dump_ifacaddr(struct sk_buff *skb, struct netlink_callback *cb)
{
	enum addr_type_t type = ANYCAST_ADDR;

	return inet6_dump_addr(skb, cb, type);
}

static int inet6_rtm_getaddr(struct sk_buff *in_skb, struct nlmsghdr* nlh,
			     void *arg)
{
	struct net *net = sock_net(in_skb->sk);
	struct ifaddrmsg *ifm;
	struct nlattr *tb[IFA_MAX+1];
	struct in6_addr *addr = NULL;
	struct net_device *dev = NULL;
	struct inet6_ifaddr *ifa;
	struct sk_buff *skb;
	int err;

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, ifa_ipv6_policy);
	if (err < 0)
		goto errout;

	addr = extract_addr(tb[IFA_ADDRESS], tb[IFA_LOCAL]);
	if (addr == NULL) {
		err = -EINVAL;
		goto errout;
	}

	ifm = nlmsg_data(nlh);
	if (ifm->ifa_index)
		dev = __dev_get_by_index(net, ifm->ifa_index);

	ifa = ipv6_get_ifaddr(net, addr, dev, 1);
	if (!ifa) {
		err = -EADDRNOTAVAIL;
		goto errout;
	}

	skb = nlmsg_new(inet6_ifaddr_msgsize(), GFP_KERNEL);
	if (!skb) {
		err = -ENOBUFS;
		goto errout_ifa;
	}

	err = inet6_fill_ifaddr(skb, ifa, NETLINK_CB(in_skb).pid,
				nlh->nlmsg_seq, RTM_NEWADDR, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in inet6_ifaddr_msgsize() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout_ifa;
	}
	err = rtnl_unicast(skb, net, NETLINK_CB(in_skb).pid);
errout_ifa:
	in6_ifa_put(ifa);
errout:
	return err;
}

static void inet6_ifa_notify(int event, struct inet6_ifaddr *ifa)
{
	struct sk_buff *skb;
	struct net *net = dev_net(ifa->idev->dev);
	int err = -ENOBUFS;

	skb = nlmsg_new(inet6_ifaddr_msgsize(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = inet6_fill_ifaddr(skb, ifa, 0, 0, event, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in inet6_ifaddr_msgsize() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, 0, RTNLGRP_IPV6_IFADDR, NULL, GFP_ATOMIC);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_IPV6_IFADDR, err);
}

static inline void ipv6_store_devconf(struct ipv6_devconf *cnf,
				__s32 *array, int bytes)
{
	BUG_ON(bytes < (DEVCONF_MAX * 4));

	memset(array, 0, bytes);
	array[DEVCONF_FORWARDING] = cnf->forwarding;
	array[DEVCONF_HOPLIMIT] = cnf->hop_limit;
	array[DEVCONF_MTU6] = cnf->mtu6;
	array[DEVCONF_ACCEPT_RA] = cnf->accept_ra;
	array[DEVCONF_ACCEPT_REDIRECTS] = cnf->accept_redirects;
	array[DEVCONF_AUTOCONF] = cnf->autoconf;
	array[DEVCONF_DAD_TRANSMITS] = cnf->dad_transmits;
	array[DEVCONF_RTR_SOLICITS] = cnf->rtr_solicits;
	array[DEVCONF_RTR_SOLICIT_INTERVAL] =
		jiffies_to_msecs(cnf->rtr_solicit_interval);
	array[DEVCONF_RTR_SOLICIT_DELAY] =
		jiffies_to_msecs(cnf->rtr_solicit_delay);
	array[DEVCONF_FORCE_MLD_VERSION] = cnf->force_mld_version;
#ifdef CONFIG_IPV6_PRIVACY
	array[DEVCONF_USE_TEMPADDR] = cnf->use_tempaddr;
	array[DEVCONF_TEMP_VALID_LFT] = cnf->temp_valid_lft;
	array[DEVCONF_TEMP_PREFERED_LFT] = cnf->temp_prefered_lft;
	array[DEVCONF_REGEN_MAX_RETRY] = cnf->regen_max_retry;
	array[DEVCONF_MAX_DESYNC_FACTOR] = cnf->max_desync_factor;
#endif
	array[DEVCONF_MAX_ADDRESSES] = cnf->max_addresses;
	array[DEVCONF_ACCEPT_RA_DEFRTR] = cnf->accept_ra_defrtr;
	array[DEVCONF_ACCEPT_RA_PINFO] = cnf->accept_ra_pinfo;
#ifdef CONFIG_IPV6_ROUTER_PREF
	array[DEVCONF_ACCEPT_RA_RTR_PREF] = cnf->accept_ra_rtr_pref;
	array[DEVCONF_RTR_PROBE_INTERVAL] =
		jiffies_to_msecs(cnf->rtr_probe_interval);
#ifdef CONFIG_IPV6_ROUTE_INFO
	array[DEVCONF_ACCEPT_RA_RT_INFO_MAX_PLEN] = cnf->accept_ra_rt_info_max_plen;
#endif
#endif
	array[DEVCONF_PROXY_NDP] = cnf->proxy_ndp;
	array[DEVCONF_ACCEPT_SOURCE_ROUTE] = cnf->accept_source_route;
#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
	array[DEVCONF_OPTIMISTIC_DAD] = cnf->optimistic_dad;
#endif
#ifdef CONFIG_IPV6_MROUTE
	array[DEVCONF_MC_FORWARDING] = cnf->mc_forwarding;
#endif
	array[DEVCONF_DISABLE_IPV6] = cnf->disable_ipv6;
	array[DEVCONF_ACCEPT_DAD] = cnf->accept_dad;
	array[DEVCONF_FORCE_TLLAO] = cnf->force_tllao;
}

static inline size_t inet6_ifla6_size(void)
{
	return nla_total_size(4) /* IFLA_INET6_FLAGS */
	     + nla_total_size(sizeof(struct ifla_cacheinfo))
	     + nla_total_size(DEVCONF_MAX * 4) /* IFLA_INET6_CONF */
	     + nla_total_size(IPSTATS_MIB_MAX * 8) /* IFLA_INET6_STATS */
	     + nla_total_size(ICMP6_MIB_MAX * 8); /* IFLA_INET6_ICMP6STATS */
}

static inline size_t inet6_if_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ifinfomsg))
	       + nla_total_size(IFNAMSIZ) /* IFLA_IFNAME */
	       + nla_total_size(MAX_ADDR_LEN) /* IFLA_ADDRESS */
	       + nla_total_size(4) /* IFLA_MTU */
	       + nla_total_size(4) /* IFLA_LINK */
	       + nla_total_size(inet6_ifla6_size()); /* IFLA_PROTINFO */
}

static inline void __snmp6_fill_statsdev(u64 *stats, atomic_long_t *mib,
				      int items, int bytes)
{
	int i;
	int pad = bytes - sizeof(u64) * items;
	BUG_ON(pad < 0);

	/* Use put_unaligned() because stats may not be aligned for u64. */
	put_unaligned(items, &stats[0]);
	for (i = 1; i < items; i++)
		put_unaligned(atomic_long_read(&mib[i]), &stats[i]);

	memset(&stats[items], 0, pad);
}

static inline void __snmp6_fill_stats64(u64 *stats, void __percpu **mib,
				      int items, int bytes, size_t syncpoff)
{
	int i;
	int pad = bytes - sizeof(u64) * items;
	BUG_ON(pad < 0);

	/* Use put_unaligned() because stats may not be aligned for u64. */
	put_unaligned(items, &stats[0]);
	for (i = 1; i < items; i++)
		put_unaligned(snmp_fold_field64(mib, i, syncpoff), &stats[i]);

	memset(&stats[items], 0, pad);
}

static void snmp6_fill_stats(u64 *stats, struct inet6_dev *idev, int attrtype,
			     int bytes)
{
	switch (attrtype) {
	case IFLA_INET6_STATS:
		__snmp6_fill_stats64(stats, (void __percpu **)idev->stats.ipv6,
				     IPSTATS_MIB_MAX, bytes, offsetof(struct ipstats_mib, syncp));
		break;
	case IFLA_INET6_ICMP6STATS:
		__snmp6_fill_statsdev(stats, idev->stats.icmpv6dev->mibs, ICMP6_MIB_MAX, bytes);
		break;
	}
}

static int inet6_fill_ifla6_attrs(struct sk_buff *skb, struct inet6_dev *idev)
{
	struct nlattr *nla;
	struct ifla_cacheinfo ci;

	NLA_PUT_U32(skb, IFLA_INET6_FLAGS, idev->if_flags);

	ci.max_reasm_len = IPV6_MAXPLEN;
	ci.tstamp = cstamp_delta(idev->tstamp);
	ci.reachable_time = jiffies_to_msecs(idev->nd_parms->reachable_time);
	ci.retrans_time = jiffies_to_msecs(idev->nd_parms->retrans_time);
	NLA_PUT(skb, IFLA_INET6_CACHEINFO, sizeof(ci), &ci);

	nla = nla_reserve(skb, IFLA_INET6_CONF, DEVCONF_MAX * sizeof(s32));
	if (nla == NULL)
		goto nla_put_failure;
	ipv6_store_devconf(&idev->cnf, nla_data(nla), nla_len(nla));

	/* XXX - MC not implemented */

	nla = nla_reserve(skb, IFLA_INET6_STATS, IPSTATS_MIB_MAX * sizeof(u64));
	if (nla == NULL)
		goto nla_put_failure;
	snmp6_fill_stats(nla_data(nla), idev, IFLA_INET6_STATS, nla_len(nla));

	nla = nla_reserve(skb, IFLA_INET6_ICMP6STATS, ICMP6_MIB_MAX * sizeof(u64));
	if (nla == NULL)
		goto nla_put_failure;
	snmp6_fill_stats(nla_data(nla), idev, IFLA_INET6_ICMP6STATS, nla_len(nla));

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static size_t inet6_get_link_af_size(const struct net_device *dev)
{
	if (!__in6_dev_get(dev))
		return 0;

	return inet6_ifla6_size();
}

static int inet6_fill_link_af(struct sk_buff *skb, const struct net_device *dev)
{
	struct inet6_dev *idev = __in6_dev_get(dev);

	if (!idev)
		return -ENODATA;

	if (inet6_fill_ifla6_attrs(skb, idev) < 0)
		return -EMSGSIZE;

	return 0;
}

static int inet6_fill_ifinfo(struct sk_buff *skb, struct inet6_dev *idev,
			     u32 pid, u32 seq, int event, unsigned int flags)
{
	struct net_device *dev = idev->dev;
	struct ifinfomsg *hdr;
	struct nlmsghdr *nlh;
	void *protoinfo;

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(*hdr), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	hdr = nlmsg_data(nlh);
	hdr->ifi_family = AF_INET6;
	hdr->__ifi_pad = 0;
	hdr->ifi_type = dev->type;
	hdr->ifi_index = dev->ifindex;
	hdr->ifi_flags = dev_get_flags(dev);
	hdr->ifi_change = 0;

	NLA_PUT_STRING(skb, IFLA_IFNAME, dev->name);

	if (dev->addr_len)
		NLA_PUT(skb, IFLA_ADDRESS, dev->addr_len, dev->dev_addr);

	NLA_PUT_U32(skb, IFLA_MTU, dev->mtu);
	if (dev->ifindex != dev->iflink)
		NLA_PUT_U32(skb, IFLA_LINK, dev->iflink);

	protoinfo = nla_nest_start(skb, IFLA_PROTINFO);
	if (protoinfo == NULL)
		goto nla_put_failure;

	if (inet6_fill_ifla6_attrs(skb, idev) < 0)
		goto nla_put_failure;

	nla_nest_end(skb, protoinfo);
	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int inet6_dump_ifinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	int h, s_h;
	int idx = 0, s_idx;
	struct net_device *dev;
	struct inet6_dev *idev;
	struct hlist_head *head;
	struct hlist_node *node;

	s_h = cb->args[0];
	s_idx = cb->args[1];

	rcu_read_lock();
	for (h = s_h; h < NETDEV_HASHENTRIES; h++, s_idx = 0) {
		idx = 0;
		head = &net->dev_index_head[h];
		hlist_for_each_entry_rcu(dev, node, head, index_hlist) {
			if (idx < s_idx)
				goto cont;
			idev = __in6_dev_get(dev);
			if (!idev)
				goto cont;
			if (inet6_fill_ifinfo(skb, idev,
					      NETLINK_CB(cb->skb).pid,
					      cb->nlh->nlmsg_seq,
					      RTM_NEWLINK, NLM_F_MULTI) <= 0)
				goto out;
cont:
			idx++;
		}
	}
out:
	rcu_read_unlock();
	cb->args[1] = idx;
	cb->args[0] = h;

	return skb->len;
}

void inet6_ifinfo_notify(int event, struct inet6_dev *idev)
{
	struct sk_buff *skb;
	struct net *net = dev_net(idev->dev);
	int err = -ENOBUFS;

	skb = nlmsg_new(inet6_if_nlmsg_size(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = inet6_fill_ifinfo(skb, idev, 0, 0, event, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in inet6_if_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, 0, RTNLGRP_IPV6_IFINFO, NULL, GFP_ATOMIC);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_IPV6_IFINFO, err);
}

static inline size_t inet6_prefix_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct prefixmsg))
	       + nla_total_size(sizeof(struct in6_addr))
	       + nla_total_size(sizeof(struct prefix_cacheinfo));
}

static int inet6_fill_prefix(struct sk_buff *skb, struct inet6_dev *idev,
			     struct prefix_info *pinfo, u32 pid, u32 seq,
			     int event, unsigned int flags)
{
	struct prefixmsg *pmsg;
	struct nlmsghdr *nlh;
	struct prefix_cacheinfo	ci;

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(*pmsg), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	pmsg = nlmsg_data(nlh);
	pmsg->prefix_family = AF_INET6;
	pmsg->prefix_pad1 = 0;
	pmsg->prefix_pad2 = 0;
	pmsg->prefix_ifindex = idev->dev->ifindex;
	pmsg->prefix_len = pinfo->prefix_len;
	pmsg->prefix_type = pinfo->type;
	pmsg->prefix_pad3 = 0;
	pmsg->prefix_flags = 0;
	if (pinfo->onlink)
		pmsg->prefix_flags |= IF_PREFIX_ONLINK;
	if (pinfo->autoconf)
		pmsg->prefix_flags |= IF_PREFIX_AUTOCONF;

	NLA_PUT(skb, PREFIX_ADDRESS, sizeof(pinfo->prefix), &pinfo->prefix);

	ci.preferred_time = ntohl(pinfo->prefered);
	ci.valid_time = ntohl(pinfo->valid);
	NLA_PUT(skb, PREFIX_CACHEINFO, sizeof(ci), &ci);

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static void inet6_prefix_notify(int event, struct inet6_dev *idev,
			 struct prefix_info *pinfo)
{
	struct sk_buff *skb;
	struct net *net = dev_net(idev->dev);
	int err = -ENOBUFS;

	skb = nlmsg_new(inet6_prefix_nlmsg_size(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = inet6_fill_prefix(skb, idev, pinfo, 0, 0, event, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in inet6_prefix_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, 0, RTNLGRP_IPV6_PREFIX, NULL, GFP_ATOMIC);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_IPV6_PREFIX, err);
}

static void __ipv6_ifa_notify(int event, struct inet6_ifaddr *ifp)
{
	inet6_ifa_notify(event ? : RTM_NEWADDR, ifp);

	switch (event) {
	case RTM_NEWADDR:
		/*
		 * If the address was optimistic
		 * we inserted the route at the start of
		 * our DAD process, so we don't need
		 * to do it again
		 */
		if (!(ifp->rt->rt6i_node))
			ip6_ins_rt(ifp->rt);
		if (ifp->idev->cnf.forwarding)
			addrconf_join_anycast(ifp);
		break;
	case RTM_DELADDR:
		if (ifp->idev->cnf.forwarding)
			addrconf_leave_anycast(ifp);
		addrconf_leave_solict(ifp->idev, &ifp->addr);
		dst_hold(&ifp->rt->dst);

		if (ip6_del_rt(ifp->rt))
			dst_free(&ifp->rt->dst);
		break;
	}
}

static void ipv6_ifa_notify(int event, struct inet6_ifaddr *ifp)
{
	rcu_read_lock_bh();
	if (likely(ifp->idev->dead == 0))
		__ipv6_ifa_notify(event, ifp);
	rcu_read_unlock_bh();
}

#ifdef CONFIG_SYSCTL

static
int addrconf_sysctl_forward(ctl_table *ctl, int write,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *valp = ctl->data;
	int val = *valp;
	loff_t pos = *ppos;
	int ret;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);

	if (write)
		ret = addrconf_fixup_forwarding(ctl, valp, val);
	if (ret)
		*ppos = pos;
	return ret;
}

static void dev_disable_change(struct inet6_dev *idev)
{
	if (!idev || !idev->dev)
		return;

	if (idev->cnf.disable_ipv6)
		addrconf_notify(NULL, NETDEV_DOWN, idev->dev);
	else
		addrconf_notify(NULL, NETDEV_UP, idev->dev);
}

static void addrconf_disable_change(struct net *net, __s32 newf)
{
	struct net_device *dev;
	struct inet6_dev *idev;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		idev = __in6_dev_get(dev);
		if (idev) {
			int changed = (!idev->cnf.disable_ipv6) ^ (!newf);
			idev->cnf.disable_ipv6 = newf;
			if (changed)
				dev_disable_change(idev);
		}
	}
	rcu_read_unlock();
}

static int addrconf_disable_ipv6(struct ctl_table *table, int *p, int old)
{
	struct net *net;

	net = (struct net *)table->extra2;

	if (p == &net->ipv6.devconf_dflt->disable_ipv6)
		return 0;

	if (!rtnl_trylock()) {
		/* Restore the original values before restarting */
		*p = old;
		return restart_syscall();
	}

	if (p == &net->ipv6.devconf_all->disable_ipv6) {
		__s32 newf = net->ipv6.devconf_all->disable_ipv6;
		net->ipv6.devconf_dflt->disable_ipv6 = newf;
		addrconf_disable_change(net, newf);
	} else if ((!*p) ^ (!old))
		dev_disable_change((struct inet6_dev *)table->extra1);

	rtnl_unlock();
	return 0;
}

static
int addrconf_sysctl_disable(ctl_table *ctl, int write,
			    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *valp = ctl->data;
	int val = *valp;
	loff_t pos = *ppos;
	int ret;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);

	if (write)
		ret = addrconf_disable_ipv6(ctl, valp, val);
	if (ret)
		*ppos = pos;
	return ret;
}

static struct addrconf_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	ctl_table addrconf_vars[DEVCONF_MAX+1];
	char *dev_name;
} addrconf_sysctl __read_mostly = {
	.sysctl_header = NULL,
	.addrconf_vars = {
		{
			.procname	= "forwarding",
			.data		= &ipv6_devconf.forwarding,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= addrconf_sysctl_forward,
		},
		{
			.procname	= "hop_limit",
			.data		= &ipv6_devconf.hop_limit,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "mtu",
			.data		= &ipv6_devconf.mtu6,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "accept_ra",
			.data		= &ipv6_devconf.accept_ra,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "accept_redirects",
			.data		= &ipv6_devconf.accept_redirects,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "autoconf",
			.data		= &ipv6_devconf.autoconf,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "dad_transmits",
			.data		= &ipv6_devconf.dad_transmits,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "router_solicitations",
			.data		= &ipv6_devconf.rtr_solicits,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "router_solicitation_interval",
			.data		= &ipv6_devconf.rtr_solicit_interval,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec_jiffies,
		},
		{
			.procname	= "router_solicitation_delay",
			.data		= &ipv6_devconf.rtr_solicit_delay,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec_jiffies,
		},
		{
			.procname	= "force_mld_version",
			.data		= &ipv6_devconf.force_mld_version,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
#ifdef CONFIG_IPV6_PRIVACY
		{
			.procname	= "use_tempaddr",
			.data		= &ipv6_devconf.use_tempaddr,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "temp_valid_lft",
			.data		= &ipv6_devconf.temp_valid_lft,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "temp_prefered_lft",
			.data		= &ipv6_devconf.temp_prefered_lft,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "regen_max_retry",
			.data		= &ipv6_devconf.regen_max_retry,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "max_desync_factor",
			.data		= &ipv6_devconf.max_desync_factor,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
#endif
		{
			.procname	= "max_addresses",
			.data		= &ipv6_devconf.max_addresses,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "accept_ra_defrtr",
			.data		= &ipv6_devconf.accept_ra_defrtr,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "accept_ra_pinfo",
			.data		= &ipv6_devconf.accept_ra_pinfo,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
#ifdef CONFIG_IPV6_ROUTER_PREF
		{
			.procname	= "accept_ra_rtr_pref",
			.data		= &ipv6_devconf.accept_ra_rtr_pref,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "router_probe_interval",
			.data		= &ipv6_devconf.rtr_probe_interval,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec_jiffies,
		},
#ifdef CONFIG_IPV6_ROUTE_INFO
		{
			.procname	= "accept_ra_rt_info_max_plen",
			.data		= &ipv6_devconf.accept_ra_rt_info_max_plen,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
#endif
#endif
		{
			.procname	= "proxy_ndp",
			.data		= &ipv6_devconf.proxy_ndp,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname	= "accept_source_route",
			.data		= &ipv6_devconf.accept_source_route,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
		{
			.procname       = "optimistic_dad",
			.data           = &ipv6_devconf.optimistic_dad,
			.maxlen         = sizeof(int),
			.mode           = 0644,
			.proc_handler   = proc_dointvec,

		},
#endif
#ifdef CONFIG_IPV6_MROUTE
		{
			.procname	= "mc_forwarding",
			.data		= &ipv6_devconf.mc_forwarding,
			.maxlen		= sizeof(int),
			.mode		= 0444,
			.proc_handler	= proc_dointvec,
		},
#endif
		{
			.procname	= "disable_ipv6",
			.data		= &ipv6_devconf.disable_ipv6,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= addrconf_sysctl_disable,
		},
		{
			.procname	= "accept_dad",
			.data		= &ipv6_devconf.accept_dad,
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec,
		},
		{
			.procname       = "force_tllao",
			.data           = &ipv6_devconf.force_tllao,
			.maxlen         = sizeof(int),
			.mode           = 0644,
			.proc_handler   = proc_dointvec
		},
		{
			/* sentinel */
		}
	},
};

static int __addrconf_sysctl_register(struct net *net, char *dev_name,
		struct inet6_dev *idev, struct ipv6_devconf *p)
{
	int i;
	struct addrconf_sysctl_table *t;

#define ADDRCONF_CTL_PATH_DEV	3

	struct ctl_path addrconf_ctl_path[] = {
		{ .procname = "net", },
		{ .procname = "ipv6", },
		{ .procname = "conf", },
		{ /* to be set */ },
		{ },
	};


	t = kmemdup(&addrconf_sysctl, sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		goto out;

	for (i = 0; t->addrconf_vars[i].data; i++) {
		t->addrconf_vars[i].data += (char *)p - (char *)&ipv6_devconf;
		t->addrconf_vars[i].extra1 = idev; /* embedded; no ref */
		t->addrconf_vars[i].extra2 = net;
	}

	/*
	 * Make a copy of dev_name, because '.procname' is regarded as const
	 * by sysctl and we wouldn't want anyone to change it under our feet
	 * (see SIOCSIFNAME).
	 */
	t->dev_name = kstrdup(dev_name, GFP_KERNEL);
	if (!t->dev_name)
		goto free;

	addrconf_ctl_path[ADDRCONF_CTL_PATH_DEV].procname = t->dev_name;

	t->sysctl_header = register_net_sysctl_table(net, addrconf_ctl_path,
			t->addrconf_vars);
	if (t->sysctl_header == NULL)
		goto free_procname;

	p->sysctl = t;
	return 0;

free_procname:
	kfree(t->dev_name);
free:
	kfree(t);
out:
	return -ENOBUFS;
}

static void __addrconf_sysctl_unregister(struct ipv6_devconf *p)
{
	struct addrconf_sysctl_table *t;

	if (p->sysctl == NULL)
		return;

	t = p->sysctl;
	p->sysctl = NULL;
	unregister_net_sysctl_table(t->sysctl_header);
	kfree(t->dev_name);
	kfree(t);
}

static void addrconf_sysctl_register(struct inet6_dev *idev)
{
	neigh_sysctl_register(idev->dev, idev->nd_parms, "ipv6",
			      &ndisc_ifinfo_sysctl_change);
	__addrconf_sysctl_register(dev_net(idev->dev), idev->dev->name,
					idev, &idev->cnf);
}

static void addrconf_sysctl_unregister(struct inet6_dev *idev)
{
	__addrconf_sysctl_unregister(&idev->cnf);
	neigh_sysctl_unregister(idev->nd_parms);
}


#endif

static int __net_init addrconf_init_net(struct net *net)
{
	int err;
	struct ipv6_devconf *all, *dflt;

	err = -ENOMEM;
	all = &ipv6_devconf;
	dflt = &ipv6_devconf_dflt;

	if (!net_eq(net, &init_net)) {
		all = kmemdup(all, sizeof(ipv6_devconf), GFP_KERNEL);
		if (all == NULL)
			goto err_alloc_all;

		dflt = kmemdup(dflt, sizeof(ipv6_devconf_dflt), GFP_KERNEL);
		if (dflt == NULL)
			goto err_alloc_dflt;
	} else {
		/* these will be inherited by all namespaces */
		dflt->autoconf = ipv6_defaults.autoconf;
		dflt->disable_ipv6 = ipv6_defaults.disable_ipv6;
	}

	net->ipv6.devconf_all = all;
	net->ipv6.devconf_dflt = dflt;

#ifdef CONFIG_SYSCTL
	err = __addrconf_sysctl_register(net, "all", NULL, all);
	if (err < 0)
		goto err_reg_all;

	err = __addrconf_sysctl_register(net, "default", NULL, dflt);
	if (err < 0)
		goto err_reg_dflt;
#endif
	return 0;

#ifdef CONFIG_SYSCTL
err_reg_dflt:
	__addrconf_sysctl_unregister(all);
err_reg_all:
	kfree(dflt);
#endif
err_alloc_dflt:
	kfree(all);
err_alloc_all:
	return err;
}

static void __net_exit addrconf_exit_net(struct net *net)
{
#ifdef CONFIG_SYSCTL
	__addrconf_sysctl_unregister(net->ipv6.devconf_dflt);
	__addrconf_sysctl_unregister(net->ipv6.devconf_all);
#endif
	if (!net_eq(net, &init_net)) {
		kfree(net->ipv6.devconf_dflt);
		kfree(net->ipv6.devconf_all);
	}
}

static struct pernet_operations addrconf_ops = {
	.init = addrconf_init_net,
	.exit = addrconf_exit_net,
};

/*
 *      Device notifier
 */

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

static struct rtnl_af_ops inet6_ops = {
	.family		  = AF_INET6,
	.fill_link_af	  = inet6_fill_link_af,
	.get_link_af_size = inet6_get_link_af_size,
};

/*
 *	Init / cleanup code
 */

int __init addrconf_init(void)
{
	int i, err;

	err = ipv6_addr_label_init();
	if (err < 0) {
		printk(KERN_CRIT "IPv6 Addrconf:"
		       " cannot initialize default policy table: %d.\n", err);
		goto out;
	}

	err = register_pernet_subsys(&addrconf_ops);
	if (err < 0)
		goto out_addrlabel;

	/* The addrconf netdev notifier requires that loopback_dev
	 * has it's ipv6 private information allocated and setup
	 * before it can bring up and give link-local addresses
	 * to other devices which are up.
	 *
	 * Unfortunately, loopback_dev is not necessarily the first
	 * entry in the global dev_base list of net devices.  In fact,
	 * it is likely to be the very last entry on that list.
	 * So this causes the notifier registry below to try and
	 * give link-local addresses to all devices besides loopback_dev
	 * first, then loopback_dev, which cases all the non-loopback_dev
	 * devices to fail to get a link-local address.
	 *
	 * So, as a temporary fix, allocate the ipv6 structure for
	 * loopback_dev first by hand.
	 * Longer term, all of the dependencies ipv6 has upon the loopback
	 * device and it being up should be removed.
	 */
	rtnl_lock();
	if (!ipv6_add_dev(init_net.loopback_dev))
		err = -ENOMEM;
	rtnl_unlock();
	if (err)
		goto errlo;

	for (i = 0; i < IN6_ADDR_HSIZE; i++)
		INIT_HLIST_HEAD(&inet6_addr_lst[i]);

	register_netdevice_notifier(&ipv6_dev_notf);

	addrconf_verify(0);

	err = rtnl_af_register(&inet6_ops);
	if (err < 0)
		goto errout_af;

	err = __rtnl_register(PF_INET6, RTM_GETLINK, NULL, inet6_dump_ifinfo,
			      NULL);
	if (err < 0)
		goto errout;

	/* Only the first call to __rtnl_register can fail */
	__rtnl_register(PF_INET6, RTM_NEWADDR, inet6_rtm_newaddr, NULL, NULL);
	__rtnl_register(PF_INET6, RTM_DELADDR, inet6_rtm_deladdr, NULL, NULL);
	__rtnl_register(PF_INET6, RTM_GETADDR, inet6_rtm_getaddr,
			inet6_dump_ifaddr, NULL);
	__rtnl_register(PF_INET6, RTM_GETMULTICAST, NULL,
			inet6_dump_ifmcaddr, NULL);
	__rtnl_register(PF_INET6, RTM_GETANYCAST, NULL,
			inet6_dump_ifacaddr, NULL);

	ipv6_addr_label_rtnl_register();

	return 0;
errout:
	rtnl_af_unregister(&inet6_ops);
errout_af:
	unregister_netdevice_notifier(&ipv6_dev_notf);
errlo:
	unregister_pernet_subsys(&addrconf_ops);
out_addrlabel:
	ipv6_addr_label_cleanup();
out:
	return err;
}

void addrconf_cleanup(void)
{
	struct net_device *dev;
	int i;

	unregister_netdevice_notifier(&ipv6_dev_notf);
	unregister_pernet_subsys(&addrconf_ops);
	ipv6_addr_label_cleanup();

	rtnl_lock();

	__rtnl_af_unregister(&inet6_ops);

	/* clean dev list */
	for_each_netdev(&init_net, dev) {
		if (__in6_dev_get(dev) == NULL)
			continue;
		addrconf_ifdown(dev, 1);
	}
	addrconf_ifdown(init_net.loopback_dev, 2);

	/*
	 *	Check hash table.
	 */
	spin_lock_bh(&addrconf_hash_lock);
	for (i = 0; i < IN6_ADDR_HSIZE; i++)
		WARN_ON(!hlist_empty(&inet6_addr_lst[i]));
	spin_unlock_bh(&addrconf_hash_lock);

	del_timer(&addr_chk_timer);
	rtnl_unlock();
}
