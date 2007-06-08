/*
 *	IPv6 Address [auto]configuration
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 *
 *	$Id: addrconf.c,v 1.69 2001/10/31 21:55:54 davem Exp $
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
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/string.h>

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
#include <linux/if_tunnel.h>
#include <linux/rtnetlink.h>

#ifdef CONFIG_IPV6_PRIVACY
#include <linux/random.h>
#endif

#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

/* Set to 3 to get tracing... */
#define ACONF_DEBUG 2

#if ACONF_DEBUG >= 3
#define ADBG(x) printk x
#else
#define ADBG(x)
#endif

#define	INFINITY_LIFE_TIME	0xFFFFFFFF
#define TIME_DELTA(a,b) ((unsigned long)((long)(a) - (long)(b)))

#ifdef CONFIG_SYSCTL
static void addrconf_sysctl_register(struct inet6_dev *idev, struct ipv6_devconf *p);
static void addrconf_sysctl_unregister(struct ipv6_devconf *p);
#endif

#ifdef CONFIG_IPV6_PRIVACY
static int __ipv6_regen_rndid(struct inet6_dev *idev);
static int __ipv6_try_regen_rndid(struct inet6_dev *idev, struct in6_addr *tmpaddr);
static void ipv6_regen_rndid(unsigned long data);

static int desync_factor = MAX_DESYNC_FACTOR * HZ;
#endif

static int ipv6_count_addresses(struct inet6_dev *idev);

/*
 *	Configured unicast address hash table
 */
static struct inet6_ifaddr		*inet6_addr_lst[IN6_ADDR_HSIZE];
static DEFINE_RWLOCK(addrconf_hash_lock);

static void addrconf_verify(unsigned long);

static DEFINE_TIMER(addr_chk_timer, addrconf_verify, 0, 0);
static DEFINE_SPINLOCK(addrconf_verify_lock);

static void addrconf_join_anycast(struct inet6_ifaddr *ifp);
static void addrconf_leave_anycast(struct inet6_ifaddr *ifp);

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
static int ipv6_chk_same_addr(const struct in6_addr *addr, struct net_device *dev);

static ATOMIC_NOTIFIER_HEAD(inet6addr_chain);

struct ipv6_devconf ipv6_devconf __read_mostly = {
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
};

/* IPv6 Wildcard Address and Loopback Address defined by RFC2553 */
const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;

static void addrconf_del_timer(struct inet6_ifaddr *ifp)
{
	if (del_timer(&ifp->timer))
		__in6_ifa_put(ifp);
}

enum addrconf_timer_t
{
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
	default:;
	}
	ifp->timer.expires = jiffies + when;
	add_timer(&ifp->timer);
}

static int snmp6_alloc_dev(struct inet6_dev *idev)
{
	int err = -ENOMEM;

	if (!idev || !idev->dev)
		return -EINVAL;

	if (snmp_mib_init((void **)idev->stats.ipv6,
			  sizeof(struct ipstats_mib),
			  __alignof__(struct ipstats_mib)) < 0)
		goto err_ip;
	if (snmp_mib_init((void **)idev->stats.icmpv6,
			  sizeof(struct icmpv6_mib),
			  __alignof__(struct icmpv6_mib)) < 0)
		goto err_icmp;

	return 0;

err_icmp:
	snmp_mib_free((void **)idev->stats.ipv6);
err_ip:
	return err;
}

static int snmp6_free_dev(struct inet6_dev *idev)
{
	snmp_mib_free((void **)idev->stats.icmpv6);
	snmp_mib_free((void **)idev->stats.ipv6);
	return 0;
}

/* Nobody refers to this device, we may destroy it. */

static void in6_dev_finish_destroy_rcu(struct rcu_head *head)
{
	struct inet6_dev *idev = container_of(head, struct inet6_dev, rcu);
	kfree(idev);
}

void in6_dev_finish_destroy(struct inet6_dev *idev)
{
	struct net_device *dev = idev->dev;
	BUG_TRAP(idev->addr_list==NULL);
	BUG_TRAP(idev->mc_list==NULL);
#ifdef NET_REFCNT_DEBUG
	printk(KERN_DEBUG "in6_dev_finish_destroy: %s\n", dev ? dev->name : "NIL");
#endif
	dev_put(dev);
	if (!idev->dead) {
		printk("Freeing alive inet6 device %p\n", idev);
		return;
	}
	snmp6_free_dev(idev);
	call_rcu(&idev->rcu, in6_dev_finish_destroy_rcu);
}

EXPORT_SYMBOL(in6_dev_finish_destroy);

static struct inet6_dev * ipv6_add_dev(struct net_device *dev)
{
	struct inet6_dev *ndev;
	struct in6_addr maddr;

	ASSERT_RTNL();

	if (dev->mtu < IPV6_MIN_MTU)
		return NULL;

	ndev = kzalloc(sizeof(struct inet6_dev), GFP_KERNEL);

	if (ndev == NULL)
		return NULL;

	rwlock_init(&ndev->lock);
	ndev->dev = dev;
	memcpy(&ndev->cnf, &ipv6_devconf_dflt, sizeof(ndev->cnf));
	ndev->cnf.mtu6 = dev->mtu;
	ndev->cnf.sysctl = NULL;
	ndev->nd_parms = neigh_parms_alloc(dev, &nd_tbl);
	if (ndev->nd_parms == NULL) {
		kfree(ndev);
		return NULL;
	}
	/* We refer to the device */
	dev_hold(dev);

	if (snmp6_alloc_dev(ndev) < 0) {
		ADBG((KERN_WARNING
			"%s(): cannot allocate memory for statistics; dev=%s.\n",
			__FUNCTION__, dev->name));
		neigh_parms_release(&nd_tbl, ndev->nd_parms);
		ndev->dead = 1;
		in6_dev_finish_destroy(ndev);
		return NULL;
	}

	if (snmp6_register_dev(ndev) < 0) {
		ADBG((KERN_WARNING
			"%s(): cannot create /proc/net/dev_snmp6/%s\n",
			__FUNCTION__, dev->name));
		neigh_parms_release(&nd_tbl, ndev->nd_parms);
		ndev->dead = 1;
		in6_dev_finish_destroy(ndev);
		return NULL;
	}

	/* One reference from device.  We must do this before
	 * we invoke __ipv6_regen_rndid().
	 */
	in6_dev_hold(ndev);

#ifdef CONFIG_IPV6_PRIVACY
	init_timer(&ndev->regen_timer);
	ndev->regen_timer.function = ipv6_regen_rndid;
	ndev->regen_timer.data = (unsigned long) ndev;
	if ((dev->flags&IFF_LOOPBACK) ||
	    dev->type == ARPHRD_TUNNEL ||
#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
	    dev->type == ARPHRD_SIT ||
#endif
	    dev->type == ARPHRD_NONE) {
		printk(KERN_INFO
		       "%s: Disabled Privacy Extensions\n",
		       dev->name);
		ndev->cnf.use_tempaddr = -1;
	} else {
		in6_dev_hold(ndev);
		ipv6_regen_rndid((unsigned long) ndev);
	}
#endif

	if (netif_running(dev) && netif_carrier_ok(dev))
		ndev->if_flags |= IF_READY;

	ipv6_mc_init_dev(ndev);
	ndev->tstamp = jiffies;
#ifdef CONFIG_SYSCTL
	neigh_sysctl_register(dev, ndev->nd_parms, NET_IPV6,
			      NET_IPV6_NEIGH, "ipv6",
			      &ndisc_ifinfo_sysctl_change,
			      NULL);
	addrconf_sysctl_register(ndev, &ndev->cnf);
#endif
	/* protected by rtnl_lock */
	rcu_assign_pointer(dev->ip6_ptr, ndev);

	/* Join all-node multicast group */
	ipv6_addr_all_nodes(&maddr);
	ipv6_dev_mc_inc(dev, &maddr);

	return ndev;
}

static struct inet6_dev * ipv6_find_idev(struct net_device *dev)
{
	struct inet6_dev *idev;

	ASSERT_RTNL();

	if ((idev = __in6_dev_get(dev)) == NULL) {
		if ((idev = ipv6_add_dev(dev)) == NULL)
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
	struct in6_addr addr;

	if (!idev)
		return;
	dev = idev->dev;
	if (dev && (dev->flags & IFF_MULTICAST)) {
		ipv6_addr_all_routers(&addr);

		if (idev->cnf.forwarding)
			ipv6_dev_mc_inc(dev, &addr);
		else
			ipv6_dev_mc_dec(dev, &addr);
	}
	for (ifa=idev->addr_list; ifa; ifa=ifa->if_next) {
		if (ifa->flags&IFA_F_TENTATIVE)
			continue;
		if (idev->cnf.forwarding)
			addrconf_join_anycast(ifa);
		else
			addrconf_leave_anycast(ifa);
	}
}


static void addrconf_forward_change(void)
{
	struct net_device *dev;
	struct inet6_dev *idev;

	read_lock(&dev_base_lock);
	for_each_netdev(dev) {
		rcu_read_lock();
		idev = __in6_dev_get(dev);
		if (idev) {
			int changed = (!idev->cnf.forwarding) ^ (!ipv6_devconf.forwarding);
			idev->cnf.forwarding = ipv6_devconf.forwarding;
			if (changed)
				dev_forward_change(idev);
		}
		rcu_read_unlock();
	}
	read_unlock(&dev_base_lock);
}
#endif

/* Nobody refers to this ifaddr, destroy it */

void inet6_ifa_finish_destroy(struct inet6_ifaddr *ifp)
{
	BUG_TRAP(ifp->if_next==NULL);
	BUG_TRAP(ifp->lst_next==NULL);
#ifdef NET_REFCNT_DEBUG
	printk(KERN_DEBUG "inet6_ifa_finish_destroy\n");
#endif

	in6_dev_put(ifp->idev);

	if (del_timer(&ifp->timer))
		printk("Timer is still running, when freeing ifa=%p\n", ifp);

	if (!ifp->dead) {
		printk("Freeing alive inet6 address %p\n", ifp);
		return;
	}
	dst_release(&ifp->rt->u.dst);

	kfree(ifp);
}

static void
ipv6_link_dev_addr(struct inet6_dev *idev, struct inet6_ifaddr *ifp)
{
	struct inet6_ifaddr *ifa, **ifap;
	int ifp_scope = ipv6_addr_src_scope(&ifp->addr);

	/*
	 * Each device address list is sorted in order of scope -
	 * global before linklocal.
	 */
	for (ifap = &idev->addr_list; (ifa = *ifap) != NULL;
	     ifap = &ifa->if_next) {
		if (ifp_scope >= ipv6_addr_src_scope(&ifa->addr))
			break;
	}

	ifp->if_next = *ifap;
	*ifap = ifp;
}

/* On success it returns ifp with increased reference count */

static struct inet6_ifaddr *
ipv6_add_addr(struct inet6_dev *idev, const struct in6_addr *addr, int pfxlen,
	      int scope, u32 flags)
{
	struct inet6_ifaddr *ifa = NULL;
	struct rt6_info *rt;
	int hash;
	int err = 0;

	rcu_read_lock_bh();
	if (idev->dead) {
		err = -ENODEV;			/*XXX*/
		goto out2;
	}

	write_lock(&addrconf_hash_lock);

	/* Ignore adding duplicate addresses on an interface */
	if (ipv6_chk_same_addr(addr, idev->dev)) {
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
	init_timer(&ifa->timer);
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

	if (rt->rt6i_nexthop == NULL)
		ifa->flags &= ~IFA_F_OPTIMISTIC;

	ifa->idev = idev;
	in6_dev_hold(idev);
	/* For caller */
	in6_ifa_hold(ifa);

	/* Add to big hash table */
	hash = ipv6_addr_hash(addr);

	ifa->lst_next = inet6_addr_lst[hash];
	inet6_addr_lst[hash] = ifa;
	in6_ifa_hold(ifa);
	write_unlock(&addrconf_hash_lock);

	write_lock(&idev->lock);
	/* Add to inet6_dev unicast addr list. */
	ipv6_link_dev_addr(idev, ifa);

#ifdef CONFIG_IPV6_PRIVACY
	if (ifa->flags&IFA_F_TEMPORARY) {
		ifa->tmp_next = idev->tempaddr_list;
		idev->tempaddr_list = ifa;
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
	write_unlock(&addrconf_hash_lock);
	goto out2;
}

/* This function wants to get referenced ifp and releases it before return */

static void ipv6_del_addr(struct inet6_ifaddr *ifp)
{
	struct inet6_ifaddr *ifa, **ifap;
	struct inet6_dev *idev = ifp->idev;
	int hash;
	int deleted = 0, onlink = 0;
	unsigned long expires = jiffies;

	hash = ipv6_addr_hash(&ifp->addr);

	ifp->dead = 1;

	write_lock_bh(&addrconf_hash_lock);
	for (ifap = &inet6_addr_lst[hash]; (ifa=*ifap) != NULL;
	     ifap = &ifa->lst_next) {
		if (ifa == ifp) {
			*ifap = ifa->lst_next;
			__in6_ifa_put(ifp);
			ifa->lst_next = NULL;
			break;
		}
	}
	write_unlock_bh(&addrconf_hash_lock);

	write_lock_bh(&idev->lock);
#ifdef CONFIG_IPV6_PRIVACY
	if (ifp->flags&IFA_F_TEMPORARY) {
		for (ifap = &idev->tempaddr_list; (ifa=*ifap) != NULL;
		     ifap = &ifa->tmp_next) {
			if (ifa == ifp) {
				*ifap = ifa->tmp_next;
				if (ifp->ifpub) {
					in6_ifa_put(ifp->ifpub);
					ifp->ifpub = NULL;
				}
				__in6_ifa_put(ifp);
				ifa->tmp_next = NULL;
				break;
			}
		}
	}
#endif

	for (ifap = &idev->addr_list; (ifa=*ifap) != NULL;) {
		if (ifa == ifp) {
			*ifap = ifa->if_next;
			__in6_ifa_put(ifp);
			ifa->if_next = NULL;
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
					lifetime = min_t(unsigned long,
							 ifa->valid_lft, 0x7fffffffUL/HZ);
					if (time_before(expires,
							ifa->tstamp + lifetime * HZ))
						expires = ifa->tstamp + lifetime * HZ;
					spin_unlock(&ifa->lock);
				}
			}
		}
		ifap = &ifa->if_next;
	}
	write_unlock_bh(&idev->lock);

	ipv6_ifa_notify(RTM_DELADDR, ifp);

	atomic_notifier_call_chain(&inet6addr_chain, NETDEV_DOWN, ifp);

	addrconf_del_timer(ifp);

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

		ipv6_addr_prefix(&prefix, &ifp->addr, ifp->prefix_len);
		rt = rt6_lookup(&prefix, NULL, ifp->idev->dev->ifindex, 1);

		if (rt && ((rt->rt6i_flags & (RTF_GATEWAY | RTF_DEFAULT)) == 0)) {
			if (onlink == 0) {
				ip6_del_rt(rt);
				rt = NULL;
			} else if (!(rt->rt6i_flags & RTF_EXPIRES)) {
				rt->rt6i_expires = expires;
				rt->rt6i_flags |= RTF_EXPIRES;
			}
		}
		dst_release(&rt->u.dst);
	}

	in6_ifa_put(ifp);
}

#ifdef CONFIG_IPV6_PRIVACY
static int ipv6_create_tempaddr(struct inet6_ifaddr *ifp, struct inet6_ifaddr *ift)
{
	struct inet6_dev *idev = ifp->idev;
	struct in6_addr addr, *tmpaddr;
	unsigned long tmp_prefered_lft, tmp_valid_lft, tmp_cstamp, tmp_tstamp;
	int tmp_plen;
	int ret = 0;
	int max_addresses;
	u32 addr_flags;

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
	tmp_valid_lft = min_t(__u32,
			      ifp->valid_lft,
			      idev->cnf.temp_valid_lft);
	tmp_prefered_lft = min_t(__u32,
				 ifp->prefered_lft,
				 idev->cnf.temp_prefered_lft - desync_factor / HZ);
	tmp_plen = ifp->prefix_len;
	max_addresses = idev->cnf.max_addresses;
	tmp_cstamp = ifp->cstamp;
	tmp_tstamp = ifp->tstamp;
	spin_unlock_bh(&ifp->lock);

	write_unlock(&idev->lock);

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
	ift->cstamp = tmp_cstamp;
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
struct ipv6_saddr_score {
	int		addr_type;
	unsigned int	attrs;
	int		matchlen;
	int		scope;
	unsigned int	rule;
};

#define IPV6_SADDR_SCORE_LOCAL		0x0001
#define IPV6_SADDR_SCORE_PREFERRED	0x0004
#define IPV6_SADDR_SCORE_HOA		0x0008
#define IPV6_SADDR_SCORE_OIF		0x0010
#define IPV6_SADDR_SCORE_LABEL		0x0020
#define IPV6_SADDR_SCORE_PRIVACY	0x0040

static inline int ipv6_saddr_preferred(int type)
{
	if (type & (IPV6_ADDR_MAPPED|IPV6_ADDR_COMPATv4|
		    IPV6_ADDR_LOOPBACK|IPV6_ADDR_RESERVED))
		return 1;
	return 0;
}

/* static matching label */
static inline int ipv6_saddr_label(const struct in6_addr *addr, int type)
{
 /*
  * 	prefix (longest match)	label
  * 	-----------------------------
  * 	::1/128			0
  * 	::/0			1
  * 	2002::/16		2
  * 	::/96			3
  * 	::ffff:0:0/96		4
  *	fc00::/7		5
  * 	2001::/32		6
  */
	if (type & IPV6_ADDR_LOOPBACK)
		return 0;
	else if (type & IPV6_ADDR_COMPATv4)
		return 3;
	else if (type & IPV6_ADDR_MAPPED)
		return 4;
	else if (addr->s6_addr32[0] == htonl(0x20010000))
		return 6;
	else if (addr->s6_addr16[0] == htons(0x2002))
		return 2;
	else if ((addr->s6_addr[0] & 0xfe) == 0xfc)
		return 5;
	return 1;
}

int ipv6_dev_get_saddr(struct net_device *daddr_dev,
		       struct in6_addr *daddr, struct in6_addr *saddr)
{
	struct ipv6_saddr_score hiscore;
	struct inet6_ifaddr *ifa_result = NULL;
	int daddr_type = __ipv6_addr_type(daddr);
	int daddr_scope = __ipv6_addr_src_scope(daddr_type);
	u32 daddr_label = ipv6_saddr_label(daddr, daddr_type);
	struct net_device *dev;

	memset(&hiscore, 0, sizeof(hiscore));

	read_lock(&dev_base_lock);
	rcu_read_lock();

	for_each_netdev(dev) {
		struct inet6_dev *idev;
		struct inet6_ifaddr *ifa;

		/* Rule 0: Candidate Source Address (section 4)
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
		if ((daddr_type & IPV6_ADDR_MULTICAST ||
		     daddr_scope <= IPV6_ADDR_SCOPE_LINKLOCAL) &&
		    daddr_dev && dev != daddr_dev)
			continue;

		idev = __in6_dev_get(dev);
		if (!idev)
			continue;

		read_lock_bh(&idev->lock);
		for (ifa = idev->addr_list; ifa; ifa = ifa->if_next) {
			struct ipv6_saddr_score score;

			score.addr_type = __ipv6_addr_type(&ifa->addr);

			/* Rule 0:
			 * - Tentative Address (RFC2462 section 5.4)
			 *  - A tentative address is not considered
			 *    "assigned to an interface" in the traditional
			 *    sense, unless it is also flagged as optimistic.
			 * - Candidate Source Address (section 4)
			 *  - In any case, anycast addresses, multicast
			 *    addresses, and the unspecified address MUST
			 *    NOT be included in a candidate set.
			 */
			if ((ifa->flags & IFA_F_TENTATIVE) &&
			    (!(ifa->flags & IFA_F_OPTIMISTIC)))
				continue;
			if (unlikely(score.addr_type == IPV6_ADDR_ANY ||
				     score.addr_type & IPV6_ADDR_MULTICAST)) {
				LIMIT_NETDEBUG(KERN_DEBUG
					       "ADDRCONF: unspecified / multicast address"
					       "assigned as unicast address on %s",
					       dev->name);
				continue;
			}

			score.attrs = 0;
			score.matchlen = 0;
			score.scope = 0;
			score.rule = 0;

			if (ifa_result == NULL) {
				/* record it if the first available entry */
				goto record_it;
			}

			/* Rule 1: Prefer same address */
			if (hiscore.rule < 1) {
				if (ipv6_addr_equal(&ifa_result->addr, daddr))
					hiscore.attrs |= IPV6_SADDR_SCORE_LOCAL;
				hiscore.rule++;
			}
			if (ipv6_addr_equal(&ifa->addr, daddr)) {
				score.attrs |= IPV6_SADDR_SCORE_LOCAL;
				if (!(hiscore.attrs & IPV6_SADDR_SCORE_LOCAL)) {
					score.rule = 1;
					goto record_it;
				}
			} else {
				if (hiscore.attrs & IPV6_SADDR_SCORE_LOCAL)
					continue;
			}

			/* Rule 2: Prefer appropriate scope */
			if (hiscore.rule < 2) {
				hiscore.scope = __ipv6_addr_src_scope(hiscore.addr_type);
				hiscore.rule++;
			}
			score.scope = __ipv6_addr_src_scope(score.addr_type);
			if (hiscore.scope < score.scope) {
				if (hiscore.scope < daddr_scope) {
					score.rule = 2;
					goto record_it;
				} else
					continue;
			} else if (score.scope < hiscore.scope) {
				if (score.scope < daddr_scope)
					break; /* addresses sorted by scope */
				else {
					score.rule = 2;
					goto record_it;
				}
			}

			/* Rule 3: Avoid deprecated and optimistic addresses */
			if (hiscore.rule < 3) {
				if (ipv6_saddr_preferred(hiscore.addr_type) ||
				   (((ifa_result->flags &
				    (IFA_F_DEPRECATED|IFA_F_OPTIMISTIC)) == 0)))
					hiscore.attrs |= IPV6_SADDR_SCORE_PREFERRED;
				hiscore.rule++;
			}
			if (ipv6_saddr_preferred(score.addr_type) ||
			   (((ifa_result->flags &
			    (IFA_F_DEPRECATED|IFA_F_OPTIMISTIC)) == 0))) {
				score.attrs |= IPV6_SADDR_SCORE_PREFERRED;
				if (!(hiscore.attrs & IPV6_SADDR_SCORE_PREFERRED)) {
					score.rule = 3;
					goto record_it;
				}
			} else {
				if (hiscore.attrs & IPV6_SADDR_SCORE_PREFERRED)
					continue;
			}

			/* Rule 4: Prefer home address */
#ifdef CONFIG_IPV6_MIP6
			if (hiscore.rule < 4) {
				if (ifa_result->flags & IFA_F_HOMEADDRESS)
					hiscore.attrs |= IPV6_SADDR_SCORE_HOA;
				hiscore.rule++;
			}
			if (ifa->flags & IFA_F_HOMEADDRESS) {
				score.attrs |= IPV6_SADDR_SCORE_HOA;
				if (!(ifa_result->flags & IFA_F_HOMEADDRESS)) {
					score.rule = 4;
					goto record_it;
				}
			} else {
				if (hiscore.attrs & IPV6_SADDR_SCORE_HOA)
					continue;
			}
#else
			if (hiscore.rule < 4)
				hiscore.rule++;
#endif

			/* Rule 5: Prefer outgoing interface */
			if (hiscore.rule < 5) {
				if (daddr_dev == NULL ||
				    daddr_dev == ifa_result->idev->dev)
					hiscore.attrs |= IPV6_SADDR_SCORE_OIF;
				hiscore.rule++;
			}
			if (daddr_dev == NULL ||
			    daddr_dev == ifa->idev->dev) {
				score.attrs |= IPV6_SADDR_SCORE_OIF;
				if (!(hiscore.attrs & IPV6_SADDR_SCORE_OIF)) {
					score.rule = 5;
					goto record_it;
				}
			} else {
				if (hiscore.attrs & IPV6_SADDR_SCORE_OIF)
					continue;
			}

			/* Rule 6: Prefer matching label */
			if (hiscore.rule < 6) {
				if (ipv6_saddr_label(&ifa_result->addr, hiscore.addr_type) == daddr_label)
					hiscore.attrs |= IPV6_SADDR_SCORE_LABEL;
				hiscore.rule++;
			}
			if (ipv6_saddr_label(&ifa->addr, score.addr_type) == daddr_label) {
				score.attrs |= IPV6_SADDR_SCORE_LABEL;
				if (!(hiscore.attrs & IPV6_SADDR_SCORE_LABEL)) {
					score.rule = 6;
					goto record_it;
				}
			} else {
				if (hiscore.attrs & IPV6_SADDR_SCORE_LABEL)
					continue;
			}

#ifdef CONFIG_IPV6_PRIVACY
			/* Rule 7: Prefer public address
			 * Note: prefer temprary address if use_tempaddr >= 2
			 */
			if (hiscore.rule < 7) {
				if ((!(ifa_result->flags & IFA_F_TEMPORARY)) ^
				    (ifa_result->idev->cnf.use_tempaddr >= 2))
					hiscore.attrs |= IPV6_SADDR_SCORE_PRIVACY;
				hiscore.rule++;
			}
			if ((!(ifa->flags & IFA_F_TEMPORARY)) ^
			    (ifa->idev->cnf.use_tempaddr >= 2)) {
				score.attrs |= IPV6_SADDR_SCORE_PRIVACY;
				if (!(hiscore.attrs & IPV6_SADDR_SCORE_PRIVACY)) {
					score.rule = 7;
					goto record_it;
				}
			} else {
				if (hiscore.attrs & IPV6_SADDR_SCORE_PRIVACY)
					continue;
			}
#else
			if (hiscore.rule < 7)
				hiscore.rule++;
#endif
			/* Rule 8: Use longest matching prefix */
			if (hiscore.rule < 8) {
				hiscore.matchlen = ipv6_addr_diff(&ifa_result->addr, daddr);
				hiscore.rule++;
			}
			score.matchlen = ipv6_addr_diff(&ifa->addr, daddr);
			if (score.matchlen > hiscore.matchlen) {
				score.rule = 8;
				goto record_it;
			}
#if 0
			else if (score.matchlen < hiscore.matchlen)
				continue;
#endif

			/* Final Rule: choose first available one */
			continue;
record_it:
			if (ifa_result)
				in6_ifa_put(ifa_result);
			in6_ifa_hold(ifa);
			ifa_result = ifa;
			hiscore = score;
		}
		read_unlock_bh(&idev->lock);
	}
	rcu_read_unlock();
	read_unlock(&dev_base_lock);

	if (!ifa_result)
		return -EADDRNOTAVAIL;

	ipv6_addr_copy(saddr, &ifa_result->addr);
	in6_ifa_put(ifa_result);
	return 0;
}


int ipv6_get_saddr(struct dst_entry *dst,
		   struct in6_addr *daddr, struct in6_addr *saddr)
{
	return ipv6_dev_get_saddr(dst ? ip6_dst_idev(dst)->dev : NULL, daddr, saddr);
}

EXPORT_SYMBOL(ipv6_get_saddr);

int ipv6_get_lladdr(struct net_device *dev, struct in6_addr *addr,
		    unsigned char banned_flags)
{
	struct inet6_dev *idev;
	int err = -EADDRNOTAVAIL;

	rcu_read_lock();
	if ((idev = __in6_dev_get(dev)) != NULL) {
		struct inet6_ifaddr *ifp;

		read_lock_bh(&idev->lock);
		for (ifp=idev->addr_list; ifp; ifp=ifp->if_next) {
			if (ifp->scope == IFA_LINK && !(ifp->flags & banned_flags)) {
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
	for (ifp=idev->addr_list; ifp; ifp=ifp->if_next)
		cnt++;
	read_unlock_bh(&idev->lock);
	return cnt;
}

int ipv6_chk_addr(struct in6_addr *addr, struct net_device *dev, int strict)
{
	struct inet6_ifaddr * ifp;
	u8 hash = ipv6_addr_hash(addr);

	read_lock_bh(&addrconf_hash_lock);
	for(ifp = inet6_addr_lst[hash]; ifp; ifp=ifp->lst_next) {
		if (ipv6_addr_equal(&ifp->addr, addr) &&
		    !(ifp->flags&IFA_F_TENTATIVE)) {
			if (dev == NULL || ifp->idev->dev == dev ||
			    !(ifp->scope&(IFA_LINK|IFA_HOST) || strict))
				break;
		}
	}
	read_unlock_bh(&addrconf_hash_lock);
	return ifp != NULL;
}

EXPORT_SYMBOL(ipv6_chk_addr);

static
int ipv6_chk_same_addr(const struct in6_addr *addr, struct net_device *dev)
{
	struct inet6_ifaddr * ifp;
	u8 hash = ipv6_addr_hash(addr);

	for(ifp = inet6_addr_lst[hash]; ifp; ifp=ifp->lst_next) {
		if (ipv6_addr_equal(&ifp->addr, addr)) {
			if (dev == NULL || ifp->idev->dev == dev)
				break;
		}
	}
	return ifp != NULL;
}

struct inet6_ifaddr * ipv6_get_ifaddr(struct in6_addr *addr, struct net_device *dev, int strict)
{
	struct inet6_ifaddr * ifp;
	u8 hash = ipv6_addr_hash(addr);

	read_lock_bh(&addrconf_hash_lock);
	for(ifp = inet6_addr_lst[hash]; ifp; ifp=ifp->lst_next) {
		if (ipv6_addr_equal(&ifp->addr, addr)) {
			if (dev == NULL || ifp->idev->dev == dev ||
			    !(ifp->scope&(IFA_LINK|IFA_HOST) || strict)) {
				in6_ifa_hold(ifp);
				break;
			}
		}
	}
	read_unlock_bh(&addrconf_hash_lock);

	return ifp;
}

int ipv6_rcv_saddr_equal(const struct sock *sk, const struct sock *sk2)
{
	const struct in6_addr *sk_rcv_saddr6 = &inet6_sk(sk)->rcv_saddr;
	const struct in6_addr *sk2_rcv_saddr6 = inet6_rcv_saddr(sk2);
	__be32 sk_rcv_saddr = inet_sk(sk)->rcv_saddr;
	__be32 sk2_rcv_saddr = inet_rcv_saddr(sk2);
	int sk_ipv6only = ipv6_only_sock(sk);
	int sk2_ipv6only = inet_v6_ipv6only(sk2);
	int addr_type = ipv6_addr_type(sk_rcv_saddr6);
	int addr_type2 = sk2_rcv_saddr6 ? ipv6_addr_type(sk2_rcv_saddr6) : IPV6_ADDR_MAPPED;

	if (!sk2_rcv_saddr && !sk_ipv6only)
		return 1;

	if (addr_type2 == IPV6_ADDR_ANY &&
	    !(sk2_ipv6only && addr_type == IPV6_ADDR_MAPPED))
		return 1;

	if (addr_type == IPV6_ADDR_ANY &&
	    !(sk_ipv6only && addr_type2 == IPV6_ADDR_MAPPED))
		return 1;

	if (sk2_rcv_saddr6 &&
	    ipv6_addr_equal(sk_rcv_saddr6, sk2_rcv_saddr6))
		return 1;

	if (addr_type == IPV6_ADDR_MAPPED &&
	    !sk2_ipv6only &&
	    (!sk2_rcv_saddr || !sk_rcv_saddr || sk_rcv_saddr == sk2_rcv_saddr))
		return 1;

	return 0;
}

/* Gets referenced address, destroys ifaddr */

static void addrconf_dad_stop(struct inet6_ifaddr *ifp)
{
	if (ifp->flags&IFA_F_PERMANENT) {
		spin_lock_bh(&ifp->lock);
		addrconf_del_timer(ifp);
		ifp->flags |= IFA_F_TENTATIVE;
		spin_unlock_bh(&ifp->lock);
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

void addrconf_dad_failure(struct inet6_ifaddr *ifp)
{
	if (net_ratelimit())
		printk(KERN_INFO "%s: duplicate address detected!\n", ifp->idev->dev->name);
	addrconf_dad_stop(ifp);
}

/* Join to solicited addr multicast group. */

void addrconf_join_solict(struct net_device *dev, struct in6_addr *addr)
{
	struct in6_addr maddr;

	if (dev->flags&(IFF_LOOPBACK|IFF_NOARP))
		return;

	addrconf_addr_solict_mult(addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);
}

void addrconf_leave_solict(struct inet6_dev *idev, struct in6_addr *addr)
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
	ipv6_addr_prefix(&addr, &ifp->addr, ifp->prefix_len);
	if (ipv6_addr_any(&addr))
		return;
	ipv6_dev_ac_inc(ifp->idev->dev, &addr);
}

static void addrconf_leave_anycast(struct inet6_ifaddr *ifp)
{
	struct in6_addr addr;
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
	}
	return -1;
}

static int ipv6_inherit_eui64(u8 *eui, struct inet6_dev *idev)
{
	int err = -1;
	struct inet6_ifaddr *ifp;

	read_lock_bh(&idev->lock);
	for (ifp=idev->addr_list; ifp; ifp=ifp->if_next) {
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
	 *  - ISATAP (draft-ietf-ngtrans-isatap-13.txt) 5.1
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
		idev->cnf.regen_max_retry * idev->cnf.dad_transmits * idev->nd_parms->retrans_time - desync_factor;
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

/* Create "default" multicast route to the interface */

static void addrconf_add_mroute(struct net_device *dev)
{
	struct fib6_config cfg = {
		.fc_table = RT6_TABLE_LOCAL,
		.fc_metric = IP6_RT_PRIO_ADDRCONF,
		.fc_ifindex = dev->ifindex,
		.fc_dst_len = 8,
		.fc_flags = RTF_UP,
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

	if ((idev = ipv6_find_idev(dev)) == NULL)
		return NULL;

	/* Add default multicast route */
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
	unsigned long rt_expires;
	struct inet6_dev *in6_dev;

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

	/* Avoid arithmetic overflow. Really, we could
	   save rt_expires in seconds, likely valid_lft,
	   but it would require division in fib gc, that it
	   not good.
	 */
	if (valid_lft >= 0x7FFFFFFF/HZ)
		rt_expires = 0x7FFFFFFF - (0x7FFFFFFF % HZ);
	else
		rt_expires = valid_lft * HZ;

	/*
	 * We convert this (in jiffies) to clock_t later.
	 * Avoid arithmetic overflow there as well.
	 * Overflow can happen only if HZ < USER_HZ.
	 */
	if (HZ < USER_HZ && rt_expires > 0x7FFFFFFF / USER_HZ)
		rt_expires = 0x7FFFFFFF / USER_HZ;

	if (pinfo->onlink) {
		struct rt6_info *rt;
		rt = rt6_lookup(&pinfo->prefix, NULL, dev->ifindex, 1);

		if (rt && ((rt->rt6i_flags & (RTF_GATEWAY | RTF_DEFAULT)) == 0)) {
			if (rt->rt6i_flags&RTF_EXPIRES) {
				if (valid_lft == 0) {
					ip6_del_rt(rt);
					rt = NULL;
				} else {
					rt->rt6i_expires = jiffies + rt_expires;
				}
			}
		} else if (valid_lft) {
			addrconf_prefix_route(&pinfo->prefix, pinfo->prefix_len,
					      dev, jiffies_to_clock_t(rt_expires), RTF_ADDRCONF|RTF_EXPIRES|RTF_PREFIX_RT);
		}
		if (rt)
			dst_release(&rt->u.dst);
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

		ifp = ipv6_get_ifaddr(&addr, dev, 1);

		if (ifp == NULL && valid_lft) {
			int max_addresses = in6_dev->cnf.max_addresses;
			u32 addr_flags = 0;

#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
			if (in6_dev->cnf.optimistic_dad &&
			    !ipv6_devconf.forwarding)
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
					/* XXX: IPsec */
					update_lft = 0;
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
			for (ift=in6_dev->tempaddr_list; ift; ift=ift->tmp_next) {
				/*
				 * When adjusting the lifetimes of an existing
				 * temporary address, only lower the lifetimes.
				 * Implementations must not increase the
				 * lifetimes of an existing temporary address
				 * when processing a Prefix Information Option.
				 */
				spin_lock(&ift->lock);
				flags = ift->flags;
				if (ift->valid_lft > valid_lft &&
				    ift->valid_lft - valid_lft > (jiffies - ift->tstamp) / HZ)
					ift->valid_lft = valid_lft + (jiffies - ift->tstamp) / HZ;
				if (ift->prefered_lft > prefered_lft &&
				    ift->prefered_lft - prefered_lft > (jiffies - ift->tstamp) / HZ)
					ift->prefered_lft = prefered_lft + (jiffies - ift->tstamp) / HZ;
				spin_unlock(&ift->lock);
				if (!(flags&IFA_F_TENTATIVE))
					ipv6_ifa_notify(0, ift);
			}

			if (create && in6_dev->cnf.use_tempaddr > 0) {
				/*
				 * When a new public address is created as described in [ADDRCONF],
				 * also create a new temporary address.
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
int addrconf_set_dstaddr(void __user *arg)
{
	struct in6_ifreq ireq;
	struct net_device *dev;
	int err = -EINVAL;

	rtnl_lock();

	err = -EFAULT;
	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		goto err_exit;

	dev = __dev_get_by_index(ireq.ifr6_ifindex);

	err = -ENODEV;
	if (dev == NULL)
		goto err_exit;

#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
	if (dev->type == ARPHRD_SIT) {
		struct ifreq ifr;
		mm_segment_t	oldfs;
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
		ifr.ifr_ifru.ifru_data = (void __user *)&p;

		oldfs = get_fs(); set_fs(KERNEL_DS);
		err = dev->do_ioctl(dev, &ifr, SIOCADDTUNNEL);
		set_fs(oldfs);

		if (err == 0) {
			err = -ENOBUFS;
			if ((dev = __dev_get_by_name(p.name)) == NULL)
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
static int inet6_addr_add(int ifindex, struct in6_addr *pfx, int plen,
			  __u8 ifa_flags, __u32 prefered_lft, __u32 valid_lft)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *idev;
	struct net_device *dev;
	int scope;
	u32 flags = RTF_EXPIRES;

	ASSERT_RTNL();

	/* check the lifetime */
	if (!valid_lft || prefered_lft > valid_lft)
		return -EINVAL;

	if ((dev = __dev_get_by_index(ifindex)) == NULL)
		return -ENODEV;

	if ((idev = addrconf_add_dev(dev)) == NULL)
		return -ENOBUFS;

	scope = ipv6_addr_scope(pfx);

	if (valid_lft == INFINITY_LIFE_TIME) {
		ifa_flags |= IFA_F_PERMANENT;
		flags = 0;
	} else if (valid_lft >= 0x7FFFFFFF/HZ)
		valid_lft = 0x7FFFFFFF/HZ;

	if (prefered_lft == 0)
		ifa_flags |= IFA_F_DEPRECATED;
	else if ((prefered_lft >= 0x7FFFFFFF/HZ) &&
		 (prefered_lft != INFINITY_LIFE_TIME))
		prefered_lft = 0x7FFFFFFF/HZ;

	ifp = ipv6_add_addr(idev, pfx, plen, scope, ifa_flags);

	if (!IS_ERR(ifp)) {
		spin_lock_bh(&ifp->lock);
		ifp->valid_lft = valid_lft;
		ifp->prefered_lft = prefered_lft;
		ifp->tstamp = jiffies;
		spin_unlock_bh(&ifp->lock);

		addrconf_prefix_route(&ifp->addr, ifp->prefix_len, dev,
				      jiffies_to_clock_t(valid_lft * HZ), flags);
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

static int inet6_addr_del(int ifindex, struct in6_addr *pfx, int plen)
{
	struct inet6_ifaddr *ifp;
	struct inet6_dev *idev;
	struct net_device *dev;

	if ((dev = __dev_get_by_index(ifindex)) == NULL)
		return -ENODEV;

	if ((idev = __in6_dev_get(dev)) == NULL)
		return -ENXIO;

	read_lock_bh(&idev->lock);
	for (ifp = idev->addr_list; ifp; ifp=ifp->if_next) {
		if (ifp->prefix_len == plen &&
		    ipv6_addr_equal(pfx, &ifp->addr)) {
			in6_ifa_hold(ifp);
			read_unlock_bh(&idev->lock);

			ipv6_del_addr(ifp);

			/* If the last address is deleted administratively,
			   disable IPv6 on this interface.
			 */
			if (idev->addr_list == NULL)
				addrconf_ifdown(idev->dev, 1);
			return 0;
		}
	}
	read_unlock_bh(&idev->lock);
	return -EADDRNOTAVAIL;
}


int addrconf_add_ifaddr(void __user *arg)
{
	struct in6_ifreq ireq;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		return -EFAULT;

	rtnl_lock();
	err = inet6_addr_add(ireq.ifr6_ifindex, &ireq.ifr6_addr, ireq.ifr6_prefixlen,
			     IFA_F_PERMANENT, INFINITY_LIFE_TIME, INFINITY_LIFE_TIME);
	rtnl_unlock();
	return err;
}

int addrconf_del_ifaddr(void __user *arg)
{
	struct in6_ifreq ireq;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (copy_from_user(&ireq, arg, sizeof(struct in6_ifreq)))
		return -EFAULT;

	rtnl_lock();
	err = inet6_addr_del(ireq.ifr6_ifindex, &ireq.ifr6_addr, ireq.ifr6_prefixlen);
	rtnl_unlock();
	return err;
}

#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
static void sit_add_v4_addrs(struct inet6_dev *idev)
{
	struct inet6_ifaddr * ifp;
	struct in6_addr addr;
	struct net_device *dev;
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
		ifp = ipv6_add_addr(idev, &addr, 128, scope, IFA_F_PERMANENT);
		if (!IS_ERR(ifp)) {
			spin_lock_bh(&ifp->lock);
			ifp->flags &= ~IFA_F_TENTATIVE;
			spin_unlock_bh(&ifp->lock);
			ipv6_ifa_notify(RTM_NEWADDR, ifp);
			in6_ifa_put(ifp);
		}
		return;
	}

	for_each_netdev(dev) {
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

				ifp = ipv6_add_addr(idev, &addr, plen, flag,
						    IFA_F_PERMANENT);
				if (!IS_ERR(ifp)) {
					spin_lock_bh(&ifp->lock);
					ifp->flags &= ~IFA_F_TENTATIVE;
					spin_unlock_bh(&ifp->lock);
					ipv6_ifa_notify(RTM_NEWADDR, ifp);
					in6_ifa_put(ifp);
				}
			}
		}
	}
}
#endif

static void init_loopback(struct net_device *dev)
{
	struct inet6_dev  *idev;
	struct inet6_ifaddr * ifp;

	/* ::1 */

	ASSERT_RTNL();

	if ((idev = ipv6_find_idev(dev)) == NULL) {
		printk(KERN_DEBUG "init loopback: add_dev failed\n");
		return;
	}

	ifp = ipv6_add_addr(idev, &in6addr_loopback, 128, IFA_HOST, IFA_F_PERMANENT);
	if (!IS_ERR(ifp)) {
		spin_lock_bh(&ifp->lock);
		ifp->flags &= ~IFA_F_TENTATIVE;
		spin_unlock_bh(&ifp->lock);
		ipv6_ifa_notify(RTM_NEWADDR, ifp);
		in6_ifa_put(ifp);
	}
}

static void addrconf_add_linklocal(struct inet6_dev *idev, struct in6_addr *addr)
{
	struct inet6_ifaddr * ifp;
	u32 addr_flags = IFA_F_PERMANENT;

#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
	if (idev->cnf.optimistic_dad &&
	    !ipv6_devconf.forwarding)
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

	idev = addrconf_add_dev(dev);
	if (idev == NULL)
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

	sit_add_v4_addrs(idev);

	if (dev->flags&IFF_POINTOPOINT) {
		addrconf_add_mroute(dev);
		addrconf_add_lroute(dev);
	} else
		sit_route_add(dev);
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

	/* first try to inherit the link-local address from the link device */
	if (idev->dev->iflink &&
	    (link_dev = __dev_get_by_index(idev->dev->iflink))) {
		if (!ipv6_inherit_linklocal(idev, link_dev))
			return;
	}
	/* then try to inherit it from any device */
	for_each_netdev(link_dev) {
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

	if ((idev = addrconf_add_dev(dev)) == NULL) {
		printk(KERN_DEBUG "init ip6-ip6: add_dev failed\n");
		return;
	}
	ip6_tnl_add_linklocal(idev);
}

static int ipv6_hwtype(struct net_device *dev)
{
	if ((dev->type == ARPHRD_ETHER) ||
	    (dev->type == ARPHRD_LOOPBACK) ||
	    (dev->type == ARPHRD_SIT) ||
	    (dev->type == ARPHRD_TUNNEL6) ||
	    (dev->type == ARPHRD_FDDI) ||
	    (dev->type == ARPHRD_IEEE802_TR) ||
	    (dev->type == ARPHRD_ARCNET) ||
	    (dev->type == ARPHRD_INFINIBAND))
		return 1;

	return 0;
}

static int addrconf_notify(struct notifier_block *this, unsigned long event,
			   void * data)
{
	struct net_device *dev = (struct net_device *) data;
	struct inet6_dev *idev;
	int run_pending = 0;

	if (!ipv6_hwtype(dev))
		return NOTIFY_OK;

	idev = __in6_dev_get(dev);

	switch(event) {
	case NETDEV_REGISTER:
		if (!idev) {
			idev = ipv6_add_dev(dev);
			if (!idev)
				printk(KERN_WARNING "IPv6: add_dev failed for %s\n",
					dev->name);
		}
		break;
	case NETDEV_UP:
	case NETDEV_CHANGE:
		if (event == NETDEV_UP) {
			if (!netif_carrier_ok(dev)) {
				/* device is not ready yet. */
				printk(KERN_INFO
					"ADDRCONF(NETDEV_UP): %s: "
					"link is not ready\n",
					dev->name);
				break;
			}

			if (idev)
				idev->if_flags |= IF_READY;
		} else {
			if (!netif_carrier_ok(dev)) {
				/* device is still not ready. */
				break;
			}

			if (idev) {
				if (idev->if_flags & IF_READY) {
					/* device is already configured. */
					break;
				}
				idev->if_flags |= IF_READY;
			}

			printk(KERN_INFO
					"ADDRCONF(NETDEV_CHANGE): %s: "
					"link becomes ready\n",
					dev->name);

			run_pending = 1;
		}

		switch(dev->type) {
#if defined(CONFIG_IPV6_SIT) || defined(CONFIG_IPV6_SIT_MODULE)
		case ARPHRD_SIT:
			addrconf_sit_config(dev);
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

			/* If the MTU changed during the interface down, when the
			   interface up, the changed MTU must be reflected in the
			   idev as well as routers.
			 */
			if (idev->cnf.mtu6 != dev->mtu && dev->mtu >= IPV6_MIN_MTU) {
				rt6_mtu_change(dev, dev->mtu);
				idev->cnf.mtu6 = dev->mtu;
			}
			idev->tstamp = jiffies;
			inet6_ifinfo_notify(RTM_NEWLINK, idev);
			/* If the changed mtu during down is lower than IPV6_MIN_MTU
			   stop IPv6 on this interface.
			 */
			if (dev->mtu < IPV6_MIN_MTU)
				addrconf_ifdown(dev, event != NETDEV_DOWN);
		}
		break;

	case NETDEV_CHANGEMTU:
		if ( idev && dev->mtu >= IPV6_MIN_MTU) {
			rt6_mtu_change(dev, dev->mtu);
			idev->cnf.mtu6 = dev->mtu;
			break;
		}

		/* MTU falled under IPV6_MIN_MTU. Stop IPv6 on this interface. */

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
#ifdef CONFIG_SYSCTL
			addrconf_sysctl_unregister(&idev->cnf);
			neigh_sysctl_unregister(idev->nd_parms);
			neigh_sysctl_register(dev, idev->nd_parms,
					      NET_IPV6, NET_IPV6_NEIGH, "ipv6",
					      &ndisc_ifinfo_sysctl_change,
					      NULL);
			addrconf_sysctl_register(idev, &idev->cnf);
#endif
			snmp6_register_dev(idev);
		}
		break;
	}

	return NOTIFY_OK;
}

/*
 *	addrconf module should be notified of a device going up
 */
static struct notifier_block ipv6_dev_notf = {
	.notifier_call = addrconf_notify,
	.priority = 0
};

static int addrconf_ifdown(struct net_device *dev, int how)
{
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifa, **bifa;
	int i;

	ASSERT_RTNL();

	if (dev == &loopback_dev && how == 1)
		how = 0;

	rt6_ifdown(dev);
	neigh_ifdown(&nd_tbl, dev);

	idev = __in6_dev_get(dev);
	if (idev == NULL)
		return -ENODEV;

	/* Step 1: remove reference to ipv6 device from parent device.
		   Do not dev_put!
	 */
	if (how == 1) {
		idev->dead = 1;

		/* protected by rtnl_lock */
		rcu_assign_pointer(dev->ip6_ptr, NULL);

		/* Step 1.5: remove snmp6 entry */
		snmp6_unregister_dev(idev);

	}

	/* Step 2: clear hash table */
	for (i=0; i<IN6_ADDR_HSIZE; i++) {
		bifa = &inet6_addr_lst[i];

		write_lock_bh(&addrconf_hash_lock);
		while ((ifa = *bifa) != NULL) {
			if (ifa->idev == idev) {
				*bifa = ifa->lst_next;
				ifa->lst_next = NULL;
				addrconf_del_timer(ifa);
				in6_ifa_put(ifa);
				continue;
			}
			bifa = &ifa->lst_next;
		}
		write_unlock_bh(&addrconf_hash_lock);
	}

	write_lock_bh(&idev->lock);

	/* Step 3: clear flags for stateless addrconf */
	if (how != 1)
		idev->if_flags &= ~(IF_RS_SENT|IF_RA_RCVD|IF_READY);

	/* Step 4: clear address list */
#ifdef CONFIG_IPV6_PRIVACY
	if (how == 1 && del_timer(&idev->regen_timer))
		in6_dev_put(idev);

	/* clear tempaddr list */
	while ((ifa = idev->tempaddr_list) != NULL) {
		idev->tempaddr_list = ifa->tmp_next;
		ifa->tmp_next = NULL;
		ifa->dead = 1;
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
	while ((ifa = idev->addr_list) != NULL) {
		idev->addr_list = ifa->if_next;
		ifa->if_next = NULL;
		ifa->dead = 1;
		addrconf_del_timer(ifa);
		write_unlock_bh(&idev->lock);

		__ipv6_ifa_notify(RTM_DELADDR, ifa);
		in6_ifa_put(ifa);

		write_lock_bh(&idev->lock);
	}
	write_unlock_bh(&idev->lock);

	/* Step 5: Discard multicast list */

	if (how == 1)
		ipv6_mc_destroy_dev(idev);
	else
		ipv6_mc_down(idev);

	/* Step 5: netlink notification of this interface */
	idev->tstamp = jiffies;
	inet6_ifinfo_notify(RTM_DELLINK, idev);

	/* Shot the device (if unregistered) */

	if (how == 1) {
#ifdef CONFIG_SYSCTL
		addrconf_sysctl_unregister(&idev->cnf);
		neigh_sysctl_unregister(idev->nd_parms);
#endif
		neigh_parms_release(&nd_tbl, idev->nd_parms);
		neigh_ifdown(&nd_tbl, dev);
		in6_dev_put(idev);
	}
	return 0;
}

static void addrconf_rs_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp = (struct inet6_ifaddr *) data;

	if (ifp->idev->cnf.forwarding)
		goto out;

	if (ifp->idev->if_flags & IF_RA_RCVD) {
		/*
		 *	Announcement received after solicitation
		 *	was sent
		 */
		goto out;
	}

	spin_lock(&ifp->lock);
	if (ifp->probes++ < ifp->idev->cnf.rtr_solicits) {
		struct in6_addr all_routers;

		/* The wait after the last probe can be shorter */
		addrconf_mod_timer(ifp, AC_RS,
				   (ifp->probes == ifp->idev->cnf.rtr_solicits) ?
				   ifp->idev->cnf.rtr_solicit_delay :
				   ifp->idev->cnf.rtr_solicit_interval);
		spin_unlock(&ifp->lock);

		ipv6_addr_all_routers(&all_routers);

		ndisc_send_rs(ifp->idev->dev, &ifp->addr, &all_routers);
	} else {
		spin_unlock(&ifp->lock);
		/*
		 * Note: we do not support deprecated "all on-link"
		 * assumption any longer.
		 */
		printk(KERN_DEBUG "%s: no IPv6 routers present\n",
		       ifp->idev->dev->name);
	}

out:
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
	if (ifp->dead)
		goto out;
	spin_lock_bh(&ifp->lock);

	if (dev->flags&(IFF_NOARP|IFF_LOOPBACK) ||
	    !(ifp->flags&IFA_F_TENTATIVE) ||
	    ifp->flags & IFA_F_NODAD) {
		ifp->flags &= ~(IFA_F_TENTATIVE|IFA_F_OPTIMISTIC);
		spin_unlock_bh(&ifp->lock);
		read_unlock_bh(&idev->lock);

		addrconf_dad_completed(ifp);
		return;
	}

	if (!(idev->if_flags & IF_READY)) {
		spin_unlock_bh(&ifp->lock);
		read_unlock_bh(&idev->lock);
		/*
		 * If the defice is not ready:
		 * - keep it tentative if it is a permanent address.
		 * - otherwise, kill it.
		 */
		in6_ifa_hold(ifp);
		addrconf_dad_stop(ifp);
		return;
	}

	/*
	 * Optimistic nodes can start receiving
	 * Frames right away
	 */
	if(ifp->flags & IFA_F_OPTIMISTIC)
		ip6_ins_rt(ifp->rt);

	addrconf_dad_kick(ifp);
	spin_unlock_bh(&ifp->lock);
out:
	read_unlock_bh(&idev->lock);
}

static void addrconf_dad_timer(unsigned long data)
{
	struct inet6_ifaddr *ifp = (struct inet6_ifaddr *) data;
	struct inet6_dev *idev = ifp->idev;
	struct in6_addr unspec;
	struct in6_addr mcaddr;

	read_lock_bh(&idev->lock);
	if (idev->dead) {
		read_unlock_bh(&idev->lock);
		goto out;
	}
	spin_lock_bh(&ifp->lock);
	if (ifp->probes == 0) {
		/*
		 * DAD was successful
		 */

		ifp->flags &= ~(IFA_F_TENTATIVE|IFA_F_OPTIMISTIC);
		spin_unlock_bh(&ifp->lock);
		read_unlock_bh(&idev->lock);

		addrconf_dad_completed(ifp);

		goto out;
	}

	ifp->probes--;
	addrconf_mod_timer(ifp, AC_DAD, ifp->idev->nd_parms->retrans_time);
	spin_unlock_bh(&ifp->lock);
	read_unlock_bh(&idev->lock);

	/* send a neighbour solicitation for our addr */
	memset(&unspec, 0, sizeof(unspec));
	addrconf_addr_solict_mult(&ifp->addr, &mcaddr);
	ndisc_send_ns(ifp->idev->dev, NULL, &ifp->addr, &mcaddr, &unspec);
out:
	in6_ifa_put(ifp);
}

static void addrconf_dad_completed(struct inet6_ifaddr *ifp)
{
	struct net_device *	dev = ifp->idev->dev;

	/*
	 *	Configure the address for reception. Now it is valid.
	 */

	ipv6_ifa_notify(RTM_NEWADDR, ifp);

	/* If added prefix is link local and forwarding is off,
	   start sending router solicitations.
	 */

	if (ifp->idev->cnf.forwarding == 0 &&
	    ifp->idev->cnf.rtr_solicits > 0 &&
	    (dev->flags&IFF_LOOPBACK) == 0 &&
	    (ipv6_addr_type(&ifp->addr) & IPV6_ADDR_LINKLOCAL)) {
		struct in6_addr all_routers;

		ipv6_addr_all_routers(&all_routers);

		/*
		 *	If a host as already performed a random delay
		 *	[...] as part of DAD [...] there is no need
		 *	to delay again before sending the first RS
		 */
		ndisc_send_rs(ifp->idev->dev, &ifp->addr, &all_routers);

		spin_lock_bh(&ifp->lock);
		ifp->probes = 1;
		ifp->idev->if_flags |= IF_RS_SENT;
		addrconf_mod_timer(ifp, AC_RS, ifp->idev->cnf.rtr_solicit_interval);
		spin_unlock_bh(&ifp->lock);
	}
}

static void addrconf_dad_run(struct inet6_dev *idev) {
	struct inet6_ifaddr *ifp;

	read_lock_bh(&idev->lock);
	for (ifp = idev->addr_list; ifp; ifp = ifp->if_next) {
		spin_lock_bh(&ifp->lock);
		if (!(ifp->flags & IFA_F_TENTATIVE)) {
			spin_unlock_bh(&ifp->lock);
			continue;
		}
		spin_unlock_bh(&ifp->lock);
		addrconf_dad_kick(ifp);
	}
	read_unlock_bh(&idev->lock);
}

#ifdef CONFIG_PROC_FS
struct if6_iter_state {
	int bucket;
};

static struct inet6_ifaddr *if6_get_first(struct seq_file *seq)
{
	struct inet6_ifaddr *ifa = NULL;
	struct if6_iter_state *state = seq->private;

	for (state->bucket = 0; state->bucket < IN6_ADDR_HSIZE; ++state->bucket) {
		ifa = inet6_addr_lst[state->bucket];
		if (ifa)
			break;
	}
	return ifa;
}

static struct inet6_ifaddr *if6_get_next(struct seq_file *seq, struct inet6_ifaddr *ifa)
{
	struct if6_iter_state *state = seq->private;

	ifa = ifa->lst_next;
try_again:
	if (!ifa && ++state->bucket < IN6_ADDR_HSIZE) {
		ifa = inet6_addr_lst[state->bucket];
		goto try_again;
	}
	return ifa;
}

static struct inet6_ifaddr *if6_get_idx(struct seq_file *seq, loff_t pos)
{
	struct inet6_ifaddr *ifa = if6_get_first(seq);

	if (ifa)
		while(pos && (ifa = if6_get_next(seq, ifa)) != NULL)
			--pos;
	return pos ? NULL : ifa;
}

static void *if6_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock_bh(&addrconf_hash_lock);
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
{
	read_unlock_bh(&addrconf_hash_lock);
}

static int if6_seq_show(struct seq_file *seq, void *v)
{
	struct inet6_ifaddr *ifp = (struct inet6_ifaddr *)v;
	seq_printf(seq,
		   NIP6_SEQFMT " %02x %02x %02x %02x %8s\n",
		   NIP6(ifp->addr),
		   ifp->idev->dev->ifindex,
		   ifp->prefix_len,
		   ifp->scope,
		   ifp->flags,
		   ifp->idev->dev->name);
	return 0;
}

static struct seq_operations if6_seq_ops = {
	.start	= if6_seq_start,
	.next	= if6_seq_next,
	.show	= if6_seq_show,
	.stop	= if6_seq_stop,
};

static int if6_seq_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int rc = -ENOMEM;
	struct if6_iter_state *s = kzalloc(sizeof(*s), GFP_KERNEL);

	if (!s)
		goto out;

	rc = seq_open(file, &if6_seq_ops);
	if (rc)
		goto out_kfree;

	seq = file->private_data;
	seq->private = s;
out:
	return rc;
out_kfree:
	kfree(s);
	goto out;
}

static const struct file_operations if6_fops = {
	.owner		= THIS_MODULE,
	.open		= if6_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

int __init if6_proc_init(void)
{
	if (!proc_net_fops_create("if_inet6", S_IRUGO, &if6_fops))
		return -ENOMEM;
	return 0;
}

void if6_proc_exit(void)
{
	proc_net_remove("if_inet6");
}
#endif	/* CONFIG_PROC_FS */

#ifdef CONFIG_IPV6_MIP6
/* Check if address is a home address configured on any interface. */
int ipv6_chk_home_addr(struct in6_addr *addr)
{
	int ret = 0;
	struct inet6_ifaddr * ifp;
	u8 hash = ipv6_addr_hash(addr);
	read_lock_bh(&addrconf_hash_lock);
	for (ifp = inet6_addr_lst[hash]; ifp; ifp = ifp->lst_next) {
		if (ipv6_addr_cmp(&ifp->addr, addr) == 0 &&
		    (ifp->flags & IFA_F_HOMEADDRESS)) {
			ret = 1;
			break;
		}
	}
	read_unlock_bh(&addrconf_hash_lock);
	return ret;
}
#endif

/*
 *	Periodic address status verification
 */

static void addrconf_verify(unsigned long foo)
{
	struct inet6_ifaddr *ifp;
	unsigned long now, next;
	int i;

	spin_lock_bh(&addrconf_verify_lock);
	now = jiffies;
	next = now + ADDR_CHECK_FREQUENCY;

	del_timer(&addr_chk_timer);

	for (i=0; i < IN6_ADDR_HSIZE; i++) {

restart:
		read_lock(&addrconf_hash_lock);
		for (ifp=inet6_addr_lst[i]; ifp; ifp=ifp->lst_next) {
			unsigned long age;
#ifdef CONFIG_IPV6_PRIVACY
			unsigned long regen_advance;
#endif

			if (ifp->flags & IFA_F_PERMANENT)
				continue;

			spin_lock(&ifp->lock);
			age = (now - ifp->tstamp) / HZ;

#ifdef CONFIG_IPV6_PRIVACY
			regen_advance = ifp->idev->cnf.regen_max_retry *
					ifp->idev->cnf.dad_transmits *
					ifp->idev->nd_parms->retrans_time / HZ;
#endif

			if (ifp->valid_lft != INFINITY_LIFE_TIME &&
			    age >= ifp->valid_lft) {
				spin_unlock(&ifp->lock);
				in6_ifa_hold(ifp);
				read_unlock(&addrconf_hash_lock);
				ipv6_del_addr(ifp);
				goto restart;
			} else if (ifp->prefered_lft == INFINITY_LIFE_TIME) {
				spin_unlock(&ifp->lock);
				continue;
			} else if (age >= ifp->prefered_lft) {
				/* jiffies - ifp->tsamp > age >= ifp->prefered_lft */
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
					read_unlock(&addrconf_hash_lock);

					ipv6_ifa_notify(0, ifp);
					in6_ifa_put(ifp);
					goto restart;
				}
#ifdef CONFIG_IPV6_PRIVACY
			} else if ((ifp->flags&IFA_F_TEMPORARY) &&
				   !(ifp->flags&IFA_F_TENTATIVE)) {
				if (age >= ifp->prefered_lft - regen_advance) {
					struct inet6_ifaddr *ifpub = ifp->ifpub;
					if (time_before(ifp->tstamp + ifp->prefered_lft * HZ, next))
						next = ifp->tstamp + ifp->prefered_lft * HZ;
					if (!ifp->regen_count && ifpub) {
						ifp->regen_count++;
						in6_ifa_hold(ifp);
						in6_ifa_hold(ifpub);
						spin_unlock(&ifp->lock);
						read_unlock(&addrconf_hash_lock);
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
		read_unlock(&addrconf_hash_lock);
	}

	addr_chk_timer.expires = time_before(next, jiffies + HZ) ? jiffies + HZ : next;
	add_timer(&addr_chk_timer);
	spin_unlock_bh(&addrconf_verify_lock);
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

	return inet6_addr_del(ifm->ifa_index, pfx, ifm->ifa_prefixlen);
}

static int inet6_addr_modify(struct inet6_ifaddr *ifp, u8 ifa_flags,
			     u32 prefered_lft, u32 valid_lft)
{
	u32 flags = RTF_EXPIRES;

	if (!valid_lft || (prefered_lft > valid_lft))
		return -EINVAL;

	if (valid_lft == INFINITY_LIFE_TIME) {
		ifa_flags |= IFA_F_PERMANENT;
		flags = 0;
	} else if (valid_lft >= 0x7FFFFFFF/HZ)
		valid_lft = 0x7FFFFFFF/HZ;

	if (prefered_lft == 0)
		ifa_flags |= IFA_F_DEPRECATED;
	else if ((prefered_lft >= 0x7FFFFFFF/HZ) &&
		 (prefered_lft != INFINITY_LIFE_TIME))
		prefered_lft = 0x7FFFFFFF/HZ;

	spin_lock_bh(&ifp->lock);
	ifp->flags = (ifp->flags & ~(IFA_F_DEPRECATED | IFA_F_PERMANENT | IFA_F_NODAD | IFA_F_HOMEADDRESS)) | ifa_flags;
	ifp->tstamp = jiffies;
	ifp->valid_lft = valid_lft;
	ifp->prefered_lft = prefered_lft;

	spin_unlock_bh(&ifp->lock);
	if (!(ifp->flags&IFA_F_TENTATIVE))
		ipv6_ifa_notify(0, ifp);

	addrconf_prefix_route(&ifp->addr, ifp->prefix_len, ifp->idev->dev,
			      jiffies_to_clock_t(valid_lft * HZ), flags);
	addrconf_verify(0);

	return 0;
}

static int
inet6_rtm_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
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

	dev =  __dev_get_by_index(ifm->ifa_index);
	if (dev == NULL)
		return -ENODEV;

	/* We ignore other flags so far. */
	ifa_flags = ifm->ifa_flags & (IFA_F_NODAD | IFA_F_HOMEADDRESS);

	ifa = ipv6_get_ifaddr(pfx, dev, 1);
	if (ifa == NULL) {
		/*
		 * It would be best to check for !NLM_F_CREATE here but
		 * userspace alreay relies on not having to provide this.
		 */
		return inet6_addr_add(ifm->ifa_index, pfx, ifm->ifa_prefixlen,
				      ifa_flags, preferred_lft, valid_lft);
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

	ci.cstamp = (u32)(TIME_DELTA(cstamp, INITIAL_JIFFIES) / HZ * 100
			+ TIME_DELTA(cstamp, INITIAL_JIFFIES) % HZ * 100 / HZ);
	ci.tstamp = (u32)(TIME_DELTA(tstamp, INITIAL_JIFFIES) / HZ * 100
			+ TIME_DELTA(tstamp, INITIAL_JIFFIES) % HZ * 100 / HZ);
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
			preferred -= tval;
			if (valid != INFINITY_LIFE_TIME)
				valid -= tval;
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

enum addr_type_t
{
	UNICAST_ADDR,
	MULTICAST_ADDR,
	ANYCAST_ADDR,
};

static int inet6_dump_addr(struct sk_buff *skb, struct netlink_callback *cb,
			   enum addr_type_t type)
{
	int idx, ip_idx;
	int s_idx, s_ip_idx;
	int err = 1;
	struct net_device *dev;
	struct inet6_dev *idev = NULL;
	struct inet6_ifaddr *ifa;
	struct ifmcaddr6 *ifmca;
	struct ifacaddr6 *ifaca;

	s_idx = cb->args[0];
	s_ip_idx = ip_idx = cb->args[1];

	idx = 0;
	for_each_netdev(dev) {
		if (idx < s_idx)
			goto cont;
		if (idx > s_idx)
			s_ip_idx = 0;
		ip_idx = 0;
		if ((idev = in6_dev_get(dev)) == NULL)
			goto cont;
		read_lock_bh(&idev->lock);
		switch (type) {
		case UNICAST_ADDR:
			/* unicast address incl. temp addr */
			for (ifa = idev->addr_list; ifa;
			     ifa = ifa->if_next, ip_idx++) {
				if (ip_idx < s_ip_idx)
					continue;
				if ((err = inet6_fill_ifaddr(skb, ifa,
				    NETLINK_CB(cb->skb).pid,
				    cb->nlh->nlmsg_seq, RTM_NEWADDR,
				    NLM_F_MULTI)) <= 0)
					goto done;
			}
			break;
		case MULTICAST_ADDR:
			/* multicast address */
			for (ifmca = idev->mc_list; ifmca;
			     ifmca = ifmca->next, ip_idx++) {
				if (ip_idx < s_ip_idx)
					continue;
				if ((err = inet6_fill_ifmcaddr(skb, ifmca,
				    NETLINK_CB(cb->skb).pid,
				    cb->nlh->nlmsg_seq, RTM_GETMULTICAST,
				    NLM_F_MULTI)) <= 0)
					goto done;
			}
			break;
		case ANYCAST_ADDR:
			/* anycast address */
			for (ifaca = idev->ac_list; ifaca;
			     ifaca = ifaca->aca_next, ip_idx++) {
				if (ip_idx < s_ip_idx)
					continue;
				if ((err = inet6_fill_ifacaddr(skb, ifaca,
				    NETLINK_CB(cb->skb).pid,
				    cb->nlh->nlmsg_seq, RTM_GETANYCAST,
				    NLM_F_MULTI)) <= 0)
					goto done;
			}
			break;
		default:
			break;
		}
		read_unlock_bh(&idev->lock);
		in6_dev_put(idev);
cont:
		idx++;
	}
done:
	if (err <= 0) {
		read_unlock_bh(&idev->lock);
		in6_dev_put(idev);
	}
	cb->args[0] = idx;
	cb->args[1] = ip_idx;
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
		dev = __dev_get_by_index(ifm->ifa_index);

	if ((ifa = ipv6_get_ifaddr(addr, dev, 1)) == NULL) {
		err = -EADDRNOTAVAIL;
		goto errout;
	}

	if ((skb = nlmsg_new(inet6_ifaddr_msgsize(), GFP_KERNEL)) == NULL) {
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
	err = rtnl_unicast(skb, NETLINK_CB(in_skb).pid);
errout_ifa:
	in6_ifa_put(ifa);
errout:
	return err;
}

static void inet6_ifa_notify(int event, struct inet6_ifaddr *ifa)
{
	struct sk_buff *skb;
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
	err = rtnl_notify(skb, 0, RTNLGRP_IPV6_IFADDR, NULL, GFP_ATOMIC);
errout:
	if (err < 0)
		rtnl_set_sk_err(RTNLGRP_IPV6_IFADDR, err);
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
	array[DEVCONF_RTR_SOLICIT_INTERVAL] = cnf->rtr_solicit_interval;
	array[DEVCONF_RTR_SOLICIT_DELAY] = cnf->rtr_solicit_delay;
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
	array[DEVCONF_RTR_PROBE_INTERVAL] = cnf->rtr_probe_interval;
#ifdef CONFIG_IPV6_ROUTE_INFO
	array[DEVCONF_ACCEPT_RA_RT_INFO_MAX_PLEN] = cnf->accept_ra_rt_info_max_plen;
#endif
#endif
	array[DEVCONF_PROXY_NDP] = cnf->proxy_ndp;
	array[DEVCONF_ACCEPT_SOURCE_ROUTE] = cnf->accept_source_route;
#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
	array[DEVCONF_OPTIMISTIC_DAD] = cnf->optimistic_dad;
#endif
}

static inline size_t inet6_if_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ifinfomsg))
	       + nla_total_size(IFNAMSIZ) /* IFLA_IFNAME */
	       + nla_total_size(MAX_ADDR_LEN) /* IFLA_ADDRESS */
	       + nla_total_size(4) /* IFLA_MTU */
	       + nla_total_size(4) /* IFLA_LINK */
	       + nla_total_size( /* IFLA_PROTINFO */
			nla_total_size(4) /* IFLA_INET6_FLAGS */
			+ nla_total_size(sizeof(struct ifla_cacheinfo))
			+ nla_total_size(DEVCONF_MAX * 4) /* IFLA_INET6_CONF */
			+ nla_total_size(IPSTATS_MIB_MAX * 8) /* IFLA_INET6_STATS */
			+ nla_total_size(ICMP6_MIB_MAX * 8) /* IFLA_INET6_ICMP6STATS */
		 );
}

static inline void __snmp6_fill_stats(u64 *stats, void **mib, int items,
				      int bytes)
{
	int i;
	int pad = bytes - sizeof(u64) * items;
	BUG_ON(pad < 0);

	/* Use put_unaligned() because stats may not be aligned for u64. */
	put_unaligned(items, &stats[0]);
	for (i = 1; i < items; i++)
		put_unaligned(snmp_fold_field(mib, i), &stats[i]);

	memset(&stats[items], 0, pad);
}

static void snmp6_fill_stats(u64 *stats, struct inet6_dev *idev, int attrtype,
			     int bytes)
{
	switch(attrtype) {
	case IFLA_INET6_STATS:
		__snmp6_fill_stats(stats, (void **)idev->stats.ipv6, IPSTATS_MIB_MAX, bytes);
		break;
	case IFLA_INET6_ICMP6STATS:
		__snmp6_fill_stats(stats, (void **)idev->stats.icmpv6, ICMP6_MIB_MAX, bytes);
		break;
	}
}

static int inet6_fill_ifinfo(struct sk_buff *skb, struct inet6_dev *idev,
			     u32 pid, u32 seq, int event, unsigned int flags)
{
	struct net_device *dev = idev->dev;
	struct nlattr *nla;
	struct ifinfomsg *hdr;
	struct nlmsghdr *nlh;
	void *protoinfo;
	struct ifla_cacheinfo ci;

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

	NLA_PUT_U32(skb, IFLA_INET6_FLAGS, idev->if_flags);

	ci.max_reasm_len = IPV6_MAXPLEN;
	ci.tstamp = (__u32)(TIME_DELTA(idev->tstamp, INITIAL_JIFFIES) / HZ * 100
		    + TIME_DELTA(idev->tstamp, INITIAL_JIFFIES) % HZ * 100 / HZ);
	ci.reachable_time = idev->nd_parms->reachable_time;
	ci.retrans_time = idev->nd_parms->retrans_time;
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

	nla_nest_end(skb, protoinfo);
	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int inet6_dump_ifinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx, err;
	int s_idx = cb->args[0];
	struct net_device *dev;
	struct inet6_dev *idev;

	read_lock(&dev_base_lock);
	idx = 0;
	for_each_netdev(dev) {
		if (idx < s_idx)
			goto cont;
		if ((idev = in6_dev_get(dev)) == NULL)
			goto cont;
		err = inet6_fill_ifinfo(skb, idev, NETLINK_CB(cb->skb).pid,
				cb->nlh->nlmsg_seq, RTM_NEWLINK, NLM_F_MULTI);
		in6_dev_put(idev);
		if (err <= 0)
			break;
cont:
		idx++;
	}
	read_unlock(&dev_base_lock);
	cb->args[0] = idx;

	return skb->len;
}

void inet6_ifinfo_notify(int event, struct inet6_dev *idev)
{
	struct sk_buff *skb;
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
	err = rtnl_notify(skb, 0, RTNLGRP_IPV6_IFADDR, NULL, GFP_ATOMIC);
errout:
	if (err < 0)
		rtnl_set_sk_err(RTNLGRP_IPV6_IFADDR, err);
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
	err = rtnl_notify(skb, 0, RTNLGRP_IPV6_PREFIX, NULL, GFP_ATOMIC);
errout:
	if (err < 0)
		rtnl_set_sk_err(RTNLGRP_IPV6_PREFIX, err);
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
		dst_hold(&ifp->rt->u.dst);
		if (ip6_del_rt(ifp->rt))
			dst_free(&ifp->rt->u.dst);
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
int addrconf_sysctl_forward(ctl_table *ctl, int write, struct file * filp,
			   void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int *valp = ctl->data;
	int val = *valp;
	int ret;

	ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);

	if (write && valp != &ipv6_devconf_dflt.forwarding) {
		if (valp != &ipv6_devconf.forwarding) {
			if ((!*valp) ^ (!val)) {
				struct inet6_dev *idev = (struct inet6_dev *)ctl->extra1;
				if (idev == NULL)
					return ret;
				dev_forward_change(idev);
			}
		} else {
			ipv6_devconf_dflt.forwarding = ipv6_devconf.forwarding;
			addrconf_forward_change();
		}
		if (*valp)
			rt6_purge_dflt_routers();
	}

	return ret;
}

static int addrconf_sysctl_forward_strategy(ctl_table *table,
					    int __user *name, int nlen,
					    void __user *oldval,
					    size_t __user *oldlenp,
					    void __user *newval, size_t newlen)
{
	int *valp = table->data;
	int new;

	if (!newval || !newlen)
		return 0;
	if (newlen != sizeof(int))
		return -EINVAL;
	if (get_user(new, (int __user *)newval))
		return -EFAULT;
	if (new == *valp)
		return 0;
	if (oldval && oldlenp) {
		size_t len;
		if (get_user(len, oldlenp))
			return -EFAULT;
		if (len) {
			if (len > table->maxlen)
				len = table->maxlen;
			if (copy_to_user(oldval, valp, len))
				return -EFAULT;
			if (put_user(len, oldlenp))
				return -EFAULT;
		}
	}

	if (valp != &ipv6_devconf_dflt.forwarding) {
		if (valp != &ipv6_devconf.forwarding) {
			struct inet6_dev *idev = (struct inet6_dev *)table->extra1;
			int changed;
			if (unlikely(idev == NULL))
				return -ENODEV;
			changed = (!*valp) ^ (!new);
			*valp = new;
			if (changed)
				dev_forward_change(idev);
		} else {
			*valp = new;
			addrconf_forward_change();
		}

		if (*valp)
			rt6_purge_dflt_routers();
	} else
		*valp = new;

	return 1;
}

static struct addrconf_sysctl_table
{
	struct ctl_table_header *sysctl_header;
	ctl_table addrconf_vars[__NET_IPV6_MAX];
	ctl_table addrconf_dev[2];
	ctl_table addrconf_conf_dir[2];
	ctl_table addrconf_proto_dir[2];
	ctl_table addrconf_root_dir[2];
} addrconf_sysctl __read_mostly = {
	.sysctl_header = NULL,
	.addrconf_vars = {
		{
			.ctl_name	=	NET_IPV6_FORWARDING,
			.procname	=	"forwarding",
			.data		=	&ipv6_devconf.forwarding,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&addrconf_sysctl_forward,
			.strategy	=	&addrconf_sysctl_forward_strategy,
		},
		{
			.ctl_name	=	NET_IPV6_HOP_LIMIT,
			.procname	=	"hop_limit",
			.data		=	&ipv6_devconf.hop_limit,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_MTU,
			.procname	=	"mtu",
			.data		=	&ipv6_devconf.mtu6,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_ACCEPT_RA,
			.procname	=	"accept_ra",
			.data		=	&ipv6_devconf.accept_ra,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_ACCEPT_REDIRECTS,
			.procname	=	"accept_redirects",
			.data		=	&ipv6_devconf.accept_redirects,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_AUTOCONF,
			.procname	=	"autoconf",
			.data		=	&ipv6_devconf.autoconf,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_DAD_TRANSMITS,
			.procname	=	"dad_transmits",
			.data		=	&ipv6_devconf.dad_transmits,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_RTR_SOLICITS,
			.procname	=	"router_solicitations",
			.data		=	&ipv6_devconf.rtr_solicits,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_RTR_SOLICIT_INTERVAL,
			.procname	=	"router_solicitation_interval",
			.data		=	&ipv6_devconf.rtr_solicit_interval,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec_jiffies,
			.strategy	=	&sysctl_jiffies,
		},
		{
			.ctl_name	=	NET_IPV6_RTR_SOLICIT_DELAY,
			.procname	=	"router_solicitation_delay",
			.data		=	&ipv6_devconf.rtr_solicit_delay,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec_jiffies,
			.strategy	=	&sysctl_jiffies,
		},
		{
			.ctl_name	=	NET_IPV6_FORCE_MLD_VERSION,
			.procname	=	"force_mld_version",
			.data		=	&ipv6_devconf.force_mld_version,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
#ifdef CONFIG_IPV6_PRIVACY
		{
			.ctl_name	=	NET_IPV6_USE_TEMPADDR,
			.procname	=	"use_tempaddr",
			.data		=	&ipv6_devconf.use_tempaddr,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_TEMP_VALID_LFT,
			.procname	=	"temp_valid_lft",
			.data		=	&ipv6_devconf.temp_valid_lft,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_TEMP_PREFERED_LFT,
			.procname	=	"temp_prefered_lft",
			.data		=	&ipv6_devconf.temp_prefered_lft,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_REGEN_MAX_RETRY,
			.procname	=	"regen_max_retry",
			.data		=	&ipv6_devconf.regen_max_retry,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_MAX_DESYNC_FACTOR,
			.procname	=	"max_desync_factor",
			.data		=	&ipv6_devconf.max_desync_factor,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
#endif
		{
			.ctl_name	=	NET_IPV6_MAX_ADDRESSES,
			.procname	=	"max_addresses",
			.data		=	&ipv6_devconf.max_addresses,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_ACCEPT_RA_DEFRTR,
			.procname	=	"accept_ra_defrtr",
			.data		=	&ipv6_devconf.accept_ra_defrtr,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_ACCEPT_RA_PINFO,
			.procname	=	"accept_ra_pinfo",
			.data		=	&ipv6_devconf.accept_ra_pinfo,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
#ifdef CONFIG_IPV6_ROUTER_PREF
		{
			.ctl_name	=	NET_IPV6_ACCEPT_RA_RTR_PREF,
			.procname	=	"accept_ra_rtr_pref",
			.data		=	&ipv6_devconf.accept_ra_rtr_pref,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_RTR_PROBE_INTERVAL,
			.procname	=	"router_probe_interval",
			.data		=	&ipv6_devconf.rtr_probe_interval,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec_jiffies,
			.strategy	=	&sysctl_jiffies,
		},
#ifdef CONFIG_IPV6_ROUTE_INFO
		{
			.ctl_name	=	NET_IPV6_ACCEPT_RA_RT_INFO_MAX_PLEN,
			.procname	=	"accept_ra_rt_info_max_plen",
			.data		=	&ipv6_devconf.accept_ra_rt_info_max_plen,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
#endif
#endif
		{
			.ctl_name	=	NET_IPV6_PROXY_NDP,
			.procname	=	"proxy_ndp",
			.data		=	&ipv6_devconf.proxy_ndp,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
		{
			.ctl_name	=	NET_IPV6_ACCEPT_SOURCE_ROUTE,
			.procname	=	"accept_source_route",
			.data		=	&ipv6_devconf.accept_source_route,
			.maxlen		=	sizeof(int),
			.mode		=	0644,
			.proc_handler	=	&proc_dointvec,
		},
#ifdef CONFIG_IPV6_OPTIMISTIC_DAD
		{
			.ctl_name	=	CTL_UNNUMBERED,
			.procname       =       "optimistic_dad",
			.data           =       &ipv6_devconf.optimistic_dad,
			.maxlen         =       sizeof(int),
			.mode           =       0644,
			.proc_handler   =       &proc_dointvec,

		},
#endif
		{
			.ctl_name	=	0,	/* sentinel */
		}
	},
	.addrconf_dev = {
		{
			.ctl_name	=	NET_PROTO_CONF_ALL,
			.procname	=	"all",
			.mode		=	0555,
			.child		=	addrconf_sysctl.addrconf_vars,
		},
		{
			.ctl_name	=	0,	/* sentinel */
		}
	},
	.addrconf_conf_dir = {
		{
			.ctl_name	=	NET_IPV6_CONF,
			.procname	=	"conf",
			.mode		=	0555,
			.child		=	addrconf_sysctl.addrconf_dev,
		},
		{
			.ctl_name	=	0,	/* sentinel */
		}
	},
	.addrconf_proto_dir = {
		{
			.ctl_name	=	NET_IPV6,
			.procname	=	"ipv6",
			.mode		=	0555,
			.child		=	addrconf_sysctl.addrconf_conf_dir,
		},
		{
			.ctl_name	=	0,	/* sentinel */
		}
	},
	.addrconf_root_dir = {
		{
			.ctl_name	=	CTL_NET,
			.procname	=	"net",
			.mode		=	0555,
			.child		=	addrconf_sysctl.addrconf_proto_dir,
		},
		{
			.ctl_name	=	0,	/* sentinel */
		}
	},
};

static void addrconf_sysctl_register(struct inet6_dev *idev, struct ipv6_devconf *p)
{
	int i;
	struct net_device *dev = idev ? idev->dev : NULL;
	struct addrconf_sysctl_table *t;
	char *dev_name = NULL;

	t = kmemdup(&addrconf_sysctl, sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return;
	for (i=0; t->addrconf_vars[i].data; i++) {
		t->addrconf_vars[i].data += (char*)p - (char*)&ipv6_devconf;
		t->addrconf_vars[i].extra1 = idev; /* embedded; no ref */
	}
	if (dev) {
		dev_name = dev->name;
		t->addrconf_dev[0].ctl_name = dev->ifindex;
	} else {
		dev_name = "default";
		t->addrconf_dev[0].ctl_name = NET_PROTO_CONF_DEFAULT;
	}

	/*
	 * Make a copy of dev_name, because '.procname' is regarded as const
	 * by sysctl and we wouldn't want anyone to change it under our feet
	 * (see SIOCSIFNAME).
	 */
	dev_name = kstrdup(dev_name, GFP_KERNEL);
	if (!dev_name)
	    goto free;

	t->addrconf_dev[0].procname = dev_name;

	t->addrconf_dev[0].child = t->addrconf_vars;
	t->addrconf_conf_dir[0].child = t->addrconf_dev;
	t->addrconf_proto_dir[0].child = t->addrconf_conf_dir;
	t->addrconf_root_dir[0].child = t->addrconf_proto_dir;

	t->sysctl_header = register_sysctl_table(t->addrconf_root_dir);
	if (t->sysctl_header == NULL)
		goto free_procname;
	else
		p->sysctl = t;
	return;

	/* error path */
 free_procname:
	kfree(dev_name);
 free:
	kfree(t);

	return;
}

static void addrconf_sysctl_unregister(struct ipv6_devconf *p)
{
	if (p->sysctl) {
		struct addrconf_sysctl_table *t = p->sysctl;
		p->sysctl = NULL;
		unregister_sysctl_table(t->sysctl_header);
		kfree(t->addrconf_dev[0].procname);
		kfree(t);
	}
}


#endif

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
	return atomic_notifier_chain_unregister(&inet6addr_chain,nb);
}

EXPORT_SYMBOL(unregister_inet6addr_notifier);

/*
 *	Init / cleanup code
 */

int __init addrconf_init(void)
{
	int err = 0;

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
	if (!ipv6_add_dev(&loopback_dev))
		err = -ENOMEM;
	rtnl_unlock();
	if (err)
		return err;

	ip6_null_entry.rt6i_idev = in6_dev_get(&loopback_dev);
#ifdef CONFIG_IPV6_MULTIPLE_TABLES
	ip6_prohibit_entry.rt6i_idev = in6_dev_get(&loopback_dev);
	ip6_blk_hole_entry.rt6i_idev = in6_dev_get(&loopback_dev);
#endif

	register_netdevice_notifier(&ipv6_dev_notf);

	addrconf_verify(0);

	err = __rtnl_register(PF_INET6, RTM_GETLINK, NULL, inet6_dump_ifinfo);
	if (err < 0)
		goto errout;

	/* Only the first call to __rtnl_register can fail */
	__rtnl_register(PF_INET6, RTM_NEWADDR, inet6_rtm_newaddr, NULL);
	__rtnl_register(PF_INET6, RTM_DELADDR, inet6_rtm_deladdr, NULL);
	__rtnl_register(PF_INET6, RTM_GETADDR, inet6_rtm_getaddr, inet6_dump_ifaddr);
	__rtnl_register(PF_INET6, RTM_GETMULTICAST, NULL, inet6_dump_ifmcaddr);
	__rtnl_register(PF_INET6, RTM_GETANYCAST, NULL, inet6_dump_ifacaddr);

#ifdef CONFIG_SYSCTL
	addrconf_sysctl.sysctl_header =
		register_sysctl_table(addrconf_sysctl.addrconf_root_dir);
	addrconf_sysctl_register(NULL, &ipv6_devconf_dflt);
#endif

	return 0;
errout:
	unregister_netdevice_notifier(&ipv6_dev_notf);

	return err;
}

void __exit addrconf_cleanup(void)
{
	struct net_device *dev;
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifa;
	int i;

	unregister_netdevice_notifier(&ipv6_dev_notf);

#ifdef CONFIG_SYSCTL
	addrconf_sysctl_unregister(&ipv6_devconf_dflt);
	addrconf_sysctl_unregister(&ipv6_devconf);
#endif

	rtnl_lock();

	/*
	 *	clean dev list.
	 */

	for_each_netdev(dev) {
		if ((idev = __in6_dev_get(dev)) == NULL)
			continue;
		addrconf_ifdown(dev, 1);
	}
	addrconf_ifdown(&loopback_dev, 2);

	/*
	 *	Check hash table.
	 */

	write_lock_bh(&addrconf_hash_lock);
	for (i=0; i < IN6_ADDR_HSIZE; i++) {
		for (ifa=inet6_addr_lst[i]; ifa; ) {
			struct inet6_ifaddr *bifa;

			bifa = ifa;
			ifa = ifa->lst_next;
			printk(KERN_DEBUG "bug: IPv6 address leakage detected: ifa=%p\n", bifa);
			/* Do not free it; something is wrong.
			   Now we can investigate it with debugger.
			 */
		}
	}
	write_unlock_bh(&addrconf_hash_lock);

	del_timer(&addr_chk_timer);

	rtnl_unlock();

#ifdef CONFIG_PROC_FS
	proc_net_remove("if_inet6");
#endif
}
