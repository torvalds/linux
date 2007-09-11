/*
 *	NET3	IP device support routines.
 *
 *	Version: $Id: devinet.c,v 1.44 2001/10/31 21:55:54 davem Exp $
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Derived from the IP parts of dev.c 1.0.19
 * 		Authors:	Ross Biro
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	Changes:
 *		Alexey Kuznetsov:	pa_* fields are replaced with ifaddr
 *					lists.
 *		Cyrus Durgin:		updated for kmod
 *		Matthias Andree:	in devinet_ioctl, compare label and
 *					address (4.4BSD alias style support),
 *					fall back to comparing just the label
 *					if no match found.
 */


#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/capability.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_addr.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#include <linux/kmod.h>

#include <net/arp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/ip_fib.h>
#include <net/rtnetlink.h>

struct ipv4_devconf ipv4_devconf = {
	.data = {
		[NET_IPV4_CONF_ACCEPT_REDIRECTS - 1] = 1,
		[NET_IPV4_CONF_SEND_REDIRECTS - 1] = 1,
		[NET_IPV4_CONF_SECURE_REDIRECTS - 1] = 1,
		[NET_IPV4_CONF_SHARED_MEDIA - 1] = 1,
	},
};

static struct ipv4_devconf ipv4_devconf_dflt = {
	.data = {
		[NET_IPV4_CONF_ACCEPT_REDIRECTS - 1] = 1,
		[NET_IPV4_CONF_SEND_REDIRECTS - 1] = 1,
		[NET_IPV4_CONF_SECURE_REDIRECTS - 1] = 1,
		[NET_IPV4_CONF_SHARED_MEDIA - 1] = 1,
		[NET_IPV4_CONF_ACCEPT_SOURCE_ROUTE - 1] = 1,
	},
};

#define IPV4_DEVCONF_DFLT(attr) IPV4_DEVCONF(ipv4_devconf_dflt, attr)

static const struct nla_policy ifa_ipv4_policy[IFA_MAX+1] = {
	[IFA_LOCAL]     	= { .type = NLA_U32 },
	[IFA_ADDRESS]   	= { .type = NLA_U32 },
	[IFA_BROADCAST] 	= { .type = NLA_U32 },
	[IFA_ANYCAST]   	= { .type = NLA_U32 },
	[IFA_LABEL]     	= { .type = NLA_STRING, .len = IFNAMSIZ - 1 },
};

static void rtmsg_ifa(int event, struct in_ifaddr *, struct nlmsghdr *, u32);

static BLOCKING_NOTIFIER_HEAD(inetaddr_chain);
static void inet_del_ifa(struct in_device *in_dev, struct in_ifaddr **ifap,
			 int destroy);
#ifdef CONFIG_SYSCTL
static void devinet_sysctl_register(struct in_device *in_dev,
				    struct ipv4_devconf *p);
static void devinet_sysctl_unregister(struct ipv4_devconf *p);
#endif

/* Locks all the inet devices. */

static struct in_ifaddr *inet_alloc_ifa(void)
{
	struct in_ifaddr *ifa = kzalloc(sizeof(*ifa), GFP_KERNEL);

	if (ifa) {
		INIT_RCU_HEAD(&ifa->rcu_head);
	}

	return ifa;
}

static void inet_rcu_free_ifa(struct rcu_head *head)
{
	struct in_ifaddr *ifa = container_of(head, struct in_ifaddr, rcu_head);
	if (ifa->ifa_dev)
		in_dev_put(ifa->ifa_dev);
	kfree(ifa);
}

static inline void inet_free_ifa(struct in_ifaddr *ifa)
{
	call_rcu(&ifa->rcu_head, inet_rcu_free_ifa);
}

void in_dev_finish_destroy(struct in_device *idev)
{
	struct net_device *dev = idev->dev;

	BUG_TRAP(!idev->ifa_list);
	BUG_TRAP(!idev->mc_list);
#ifdef NET_REFCNT_DEBUG
	printk(KERN_DEBUG "in_dev_finish_destroy: %p=%s\n",
	       idev, dev ? dev->name : "NIL");
#endif
	dev_put(dev);
	if (!idev->dead)
		printk("Freeing alive in_device %p\n", idev);
	else {
		kfree(idev);
	}
}

static struct in_device *inetdev_init(struct net_device *dev)
{
	struct in_device *in_dev;

	ASSERT_RTNL();

	in_dev = kzalloc(sizeof(*in_dev), GFP_KERNEL);
	if (!in_dev)
		goto out;
	INIT_RCU_HEAD(&in_dev->rcu_head);
	memcpy(&in_dev->cnf, &ipv4_devconf_dflt, sizeof(in_dev->cnf));
	in_dev->cnf.sysctl = NULL;
	in_dev->dev = dev;
	if ((in_dev->arp_parms = neigh_parms_alloc(dev, &arp_tbl)) == NULL)
		goto out_kfree;
	/* Reference in_dev->dev */
	dev_hold(dev);
#ifdef CONFIG_SYSCTL
	neigh_sysctl_register(dev, in_dev->arp_parms, NET_IPV4,
			      NET_IPV4_NEIGH, "ipv4", NULL, NULL);
#endif

	/* Account for reference dev->ip_ptr (below) */
	in_dev_hold(in_dev);

#ifdef CONFIG_SYSCTL
	devinet_sysctl_register(in_dev, &in_dev->cnf);
#endif
	ip_mc_init_dev(in_dev);
	if (dev->flags & IFF_UP)
		ip_mc_up(in_dev);

	/* we can receive as soon as ip_ptr is set -- do this last */
	rcu_assign_pointer(dev->ip_ptr, in_dev);
out:
	return in_dev;
out_kfree:
	kfree(in_dev);
	in_dev = NULL;
	goto out;
}

static void in_dev_rcu_put(struct rcu_head *head)
{
	struct in_device *idev = container_of(head, struct in_device, rcu_head);
	in_dev_put(idev);
}

static void inetdev_destroy(struct in_device *in_dev)
{
	struct in_ifaddr *ifa;
	struct net_device *dev;

	ASSERT_RTNL();

	dev = in_dev->dev;
	if (dev == &loopback_dev)
		return;

	in_dev->dead = 1;

	ip_mc_destroy_dev(in_dev);

	while ((ifa = in_dev->ifa_list) != NULL) {
		inet_del_ifa(in_dev, &in_dev->ifa_list, 0);
		inet_free_ifa(ifa);
	}

#ifdef CONFIG_SYSCTL
	devinet_sysctl_unregister(&in_dev->cnf);
#endif

	dev->ip_ptr = NULL;

#ifdef CONFIG_SYSCTL
	neigh_sysctl_unregister(in_dev->arp_parms);
#endif
	neigh_parms_release(&arp_tbl, in_dev->arp_parms);
	arp_ifdown(dev);

	call_rcu(&in_dev->rcu_head, in_dev_rcu_put);
}

int inet_addr_onlink(struct in_device *in_dev, __be32 a, __be32 b)
{
	rcu_read_lock();
	for_primary_ifa(in_dev) {
		if (inet_ifa_match(a, ifa)) {
			if (!b || inet_ifa_match(b, ifa)) {
				rcu_read_unlock();
				return 1;
			}
		}
	} endfor_ifa(in_dev);
	rcu_read_unlock();
	return 0;
}

static void __inet_del_ifa(struct in_device *in_dev, struct in_ifaddr **ifap,
			 int destroy, struct nlmsghdr *nlh, u32 pid)
{
	struct in_ifaddr *promote = NULL;
	struct in_ifaddr *ifa, *ifa1 = *ifap;
	struct in_ifaddr *last_prim = in_dev->ifa_list;
	struct in_ifaddr *prev_prom = NULL;
	int do_promote = IN_DEV_PROMOTE_SECONDARIES(in_dev);

	ASSERT_RTNL();

	/* 1. Deleting primary ifaddr forces deletion all secondaries
	 * unless alias promotion is set
	 **/

	if (!(ifa1->ifa_flags & IFA_F_SECONDARY)) {
		struct in_ifaddr **ifap1 = &ifa1->ifa_next;

		while ((ifa = *ifap1) != NULL) {
			if (!(ifa->ifa_flags & IFA_F_SECONDARY) &&
			    ifa1->ifa_scope <= ifa->ifa_scope)
				last_prim = ifa;

			if (!(ifa->ifa_flags & IFA_F_SECONDARY) ||
			    ifa1->ifa_mask != ifa->ifa_mask ||
			    !inet_ifa_match(ifa1->ifa_address, ifa)) {
				ifap1 = &ifa->ifa_next;
				prev_prom = ifa;
				continue;
			}

			if (!do_promote) {
				*ifap1 = ifa->ifa_next;

				rtmsg_ifa(RTM_DELADDR, ifa, nlh, pid);
				blocking_notifier_call_chain(&inetaddr_chain,
						NETDEV_DOWN, ifa);
				inet_free_ifa(ifa);
			} else {
				promote = ifa;
				break;
			}
		}
	}

	/* 2. Unlink it */

	*ifap = ifa1->ifa_next;

	/* 3. Announce address deletion */

	/* Send message first, then call notifier.
	   At first sight, FIB update triggered by notifier
	   will refer to already deleted ifaddr, that could confuse
	   netlink listeners. It is not true: look, gated sees
	   that route deleted and if it still thinks that ifaddr
	   is valid, it will try to restore deleted routes... Grr.
	   So that, this order is correct.
	 */
	rtmsg_ifa(RTM_DELADDR, ifa1, nlh, pid);
	blocking_notifier_call_chain(&inetaddr_chain, NETDEV_DOWN, ifa1);

	if (promote) {

		if (prev_prom) {
			prev_prom->ifa_next = promote->ifa_next;
			promote->ifa_next = last_prim->ifa_next;
			last_prim->ifa_next = promote;
		}

		promote->ifa_flags &= ~IFA_F_SECONDARY;
		rtmsg_ifa(RTM_NEWADDR, promote, nlh, pid);
		blocking_notifier_call_chain(&inetaddr_chain,
				NETDEV_UP, promote);
		for (ifa = promote->ifa_next; ifa; ifa = ifa->ifa_next) {
			if (ifa1->ifa_mask != ifa->ifa_mask ||
			    !inet_ifa_match(ifa1->ifa_address, ifa))
					continue;
			fib_add_ifaddr(ifa);
		}

	}
	if (destroy)
		inet_free_ifa(ifa1);
}

static void inet_del_ifa(struct in_device *in_dev, struct in_ifaddr **ifap,
			 int destroy)
{
	__inet_del_ifa(in_dev, ifap, destroy, NULL, 0);
}

static int __inet_insert_ifa(struct in_ifaddr *ifa, struct nlmsghdr *nlh,
			     u32 pid)
{
	struct in_device *in_dev = ifa->ifa_dev;
	struct in_ifaddr *ifa1, **ifap, **last_primary;

	ASSERT_RTNL();

	if (!ifa->ifa_local) {
		inet_free_ifa(ifa);
		return 0;
	}

	ifa->ifa_flags &= ~IFA_F_SECONDARY;
	last_primary = &in_dev->ifa_list;

	for (ifap = &in_dev->ifa_list; (ifa1 = *ifap) != NULL;
	     ifap = &ifa1->ifa_next) {
		if (!(ifa1->ifa_flags & IFA_F_SECONDARY) &&
		    ifa->ifa_scope <= ifa1->ifa_scope)
			last_primary = &ifa1->ifa_next;
		if (ifa1->ifa_mask == ifa->ifa_mask &&
		    inet_ifa_match(ifa1->ifa_address, ifa)) {
			if (ifa1->ifa_local == ifa->ifa_local) {
				inet_free_ifa(ifa);
				return -EEXIST;
			}
			if (ifa1->ifa_scope != ifa->ifa_scope) {
				inet_free_ifa(ifa);
				return -EINVAL;
			}
			ifa->ifa_flags |= IFA_F_SECONDARY;
		}
	}

	if (!(ifa->ifa_flags & IFA_F_SECONDARY)) {
		net_srandom(ifa->ifa_local);
		ifap = last_primary;
	}

	ifa->ifa_next = *ifap;
	*ifap = ifa;

	/* Send message first, then call notifier.
	   Notifier will trigger FIB update, so that
	   listeners of netlink will know about new ifaddr */
	rtmsg_ifa(RTM_NEWADDR, ifa, nlh, pid);
	blocking_notifier_call_chain(&inetaddr_chain, NETDEV_UP, ifa);

	return 0;
}

static int inet_insert_ifa(struct in_ifaddr *ifa)
{
	return __inet_insert_ifa(ifa, NULL, 0);
}

static int inet_set_ifa(struct net_device *dev, struct in_ifaddr *ifa)
{
	struct in_device *in_dev = __in_dev_get_rtnl(dev);

	ASSERT_RTNL();

	if (!in_dev) {
		inet_free_ifa(ifa);
		return -ENOBUFS;
	}
	ipv4_devconf_setall(in_dev);
	if (ifa->ifa_dev != in_dev) {
		BUG_TRAP(!ifa->ifa_dev);
		in_dev_hold(in_dev);
		ifa->ifa_dev = in_dev;
	}
	if (LOOPBACK(ifa->ifa_local))
		ifa->ifa_scope = RT_SCOPE_HOST;
	return inet_insert_ifa(ifa);
}

struct in_device *inetdev_by_index(int ifindex)
{
	struct net_device *dev;
	struct in_device *in_dev = NULL;
	read_lock(&dev_base_lock);
	dev = __dev_get_by_index(ifindex);
	if (dev)
		in_dev = in_dev_get(dev);
	read_unlock(&dev_base_lock);
	return in_dev;
}

/* Called only from RTNL semaphored context. No locks. */

struct in_ifaddr *inet_ifa_byprefix(struct in_device *in_dev, __be32 prefix,
				    __be32 mask)
{
	ASSERT_RTNL();

	for_primary_ifa(in_dev) {
		if (ifa->ifa_mask == mask && inet_ifa_match(prefix, ifa))
			return ifa;
	} endfor_ifa(in_dev);
	return NULL;
}

static int inet_rtm_deladdr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct nlattr *tb[IFA_MAX+1];
	struct in_device *in_dev;
	struct ifaddrmsg *ifm;
	struct in_ifaddr *ifa, **ifap;
	int err = -EINVAL;

	ASSERT_RTNL();

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, ifa_ipv4_policy);
	if (err < 0)
		goto errout;

	ifm = nlmsg_data(nlh);
	in_dev = inetdev_by_index(ifm->ifa_index);
	if (in_dev == NULL) {
		err = -ENODEV;
		goto errout;
	}

	__in_dev_put(in_dev);

	for (ifap = &in_dev->ifa_list; (ifa = *ifap) != NULL;
	     ifap = &ifa->ifa_next) {
		if (tb[IFA_LOCAL] &&
		    ifa->ifa_local != nla_get_be32(tb[IFA_LOCAL]))
			continue;

		if (tb[IFA_LABEL] && nla_strcmp(tb[IFA_LABEL], ifa->ifa_label))
			continue;

		if (tb[IFA_ADDRESS] &&
		    (ifm->ifa_prefixlen != ifa->ifa_prefixlen ||
		    !inet_ifa_match(nla_get_be32(tb[IFA_ADDRESS]), ifa)))
			continue;

		__inet_del_ifa(in_dev, ifap, 1, nlh, NETLINK_CB(skb).pid);
		return 0;
	}

	err = -EADDRNOTAVAIL;
errout:
	return err;
}

static struct in_ifaddr *rtm_to_ifaddr(struct nlmsghdr *nlh)
{
	struct nlattr *tb[IFA_MAX+1];
	struct in_ifaddr *ifa;
	struct ifaddrmsg *ifm;
	struct net_device *dev;
	struct in_device *in_dev;
	int err = -EINVAL;

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, ifa_ipv4_policy);
	if (err < 0)
		goto errout;

	ifm = nlmsg_data(nlh);
	if (ifm->ifa_prefixlen > 32 || tb[IFA_LOCAL] == NULL) {
		err = -EINVAL;
		goto errout;
	}

	dev = __dev_get_by_index(ifm->ifa_index);
	if (dev == NULL) {
		err = -ENODEV;
		goto errout;
	}

	in_dev = __in_dev_get_rtnl(dev);
	if (in_dev == NULL) {
		err = -ENOBUFS;
		goto errout;
	}

	ipv4_devconf_setall(in_dev);

	ifa = inet_alloc_ifa();
	if (ifa == NULL) {
		/*
		 * A potential indev allocation can be left alive, it stays
		 * assigned to its device and is destroy with it.
		 */
		err = -ENOBUFS;
		goto errout;
	}

	in_dev_hold(in_dev);

	if (tb[IFA_ADDRESS] == NULL)
		tb[IFA_ADDRESS] = tb[IFA_LOCAL];

	ifa->ifa_prefixlen = ifm->ifa_prefixlen;
	ifa->ifa_mask = inet_make_mask(ifm->ifa_prefixlen);
	ifa->ifa_flags = ifm->ifa_flags;
	ifa->ifa_scope = ifm->ifa_scope;
	ifa->ifa_dev = in_dev;

	ifa->ifa_local = nla_get_be32(tb[IFA_LOCAL]);
	ifa->ifa_address = nla_get_be32(tb[IFA_ADDRESS]);

	if (tb[IFA_BROADCAST])
		ifa->ifa_broadcast = nla_get_be32(tb[IFA_BROADCAST]);

	if (tb[IFA_ANYCAST])
		ifa->ifa_anycast = nla_get_be32(tb[IFA_ANYCAST]);

	if (tb[IFA_LABEL])
		nla_strlcpy(ifa->ifa_label, tb[IFA_LABEL], IFNAMSIZ);
	else
		memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);

	return ifa;

errout:
	return ERR_PTR(err);
}

static int inet_rtm_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct in_ifaddr *ifa;

	ASSERT_RTNL();

	ifa = rtm_to_ifaddr(nlh);
	if (IS_ERR(ifa))
		return PTR_ERR(ifa);

	return __inet_insert_ifa(ifa, nlh, NETLINK_CB(skb).pid);
}

/*
 *	Determine a default network mask, based on the IP address.
 */

static __inline__ int inet_abc_len(__be32 addr)
{
	int rc = -1;	/* Something else, probably a multicast. */

	if (ZERONET(addr))
		rc = 0;
	else {
		__u32 haddr = ntohl(addr);

		if (IN_CLASSA(haddr))
			rc = 8;
		else if (IN_CLASSB(haddr))
			rc = 16;
		else if (IN_CLASSC(haddr))
			rc = 24;
	}

	return rc;
}


int devinet_ioctl(unsigned int cmd, void __user *arg)
{
	struct ifreq ifr;
	struct sockaddr_in sin_orig;
	struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
	struct in_device *in_dev;
	struct in_ifaddr **ifap = NULL;
	struct in_ifaddr *ifa = NULL;
	struct net_device *dev;
	char *colon;
	int ret = -EFAULT;
	int tryaddrmatch = 0;

	/*
	 *	Fetch the caller's info block into kernel space
	 */

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		goto out;
	ifr.ifr_name[IFNAMSIZ - 1] = 0;

	/* save original address for comparison */
	memcpy(&sin_orig, sin, sizeof(*sin));

	colon = strchr(ifr.ifr_name, ':');
	if (colon)
		*colon = 0;

#ifdef CONFIG_KMOD
	dev_load(ifr.ifr_name);
#endif

	switch (cmd) {
	case SIOCGIFADDR:	/* Get interface address */
	case SIOCGIFBRDADDR:	/* Get the broadcast address */
	case SIOCGIFDSTADDR:	/* Get the destination address */
	case SIOCGIFNETMASK:	/* Get the netmask for the interface */
		/* Note that these ioctls will not sleep,
		   so that we do not impose a lock.
		   One day we will be forced to put shlock here (I mean SMP)
		 */
		tryaddrmatch = (sin_orig.sin_family == AF_INET);
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		break;

	case SIOCSIFFLAGS:
		ret = -EACCES;
		if (!capable(CAP_NET_ADMIN))
			goto out;
		break;
	case SIOCSIFADDR:	/* Set interface address (and family) */
	case SIOCSIFBRDADDR:	/* Set the broadcast address */
	case SIOCSIFDSTADDR:	/* Set the destination address */
	case SIOCSIFNETMASK: 	/* Set the netmask for the interface */
		ret = -EACCES;
		if (!capable(CAP_NET_ADMIN))
			goto out;
		ret = -EINVAL;
		if (sin->sin_family != AF_INET)
			goto out;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	rtnl_lock();

	ret = -ENODEV;
	if ((dev = __dev_get_by_name(ifr.ifr_name)) == NULL)
		goto done;

	if (colon)
		*colon = ':';

	if ((in_dev = __in_dev_get_rtnl(dev)) != NULL) {
		if (tryaddrmatch) {
			/* Matthias Andree */
			/* compare label and address (4.4BSD style) */
			/* note: we only do this for a limited set of ioctls
			   and only if the original address family was AF_INET.
			   This is checked above. */
			for (ifap = &in_dev->ifa_list; (ifa = *ifap) != NULL;
			     ifap = &ifa->ifa_next) {
				if (!strcmp(ifr.ifr_name, ifa->ifa_label) &&
				    sin_orig.sin_addr.s_addr ==
							ifa->ifa_address) {
					break; /* found */
				}
			}
		}
		/* we didn't get a match, maybe the application is
		   4.3BSD-style and passed in junk so we fall back to
		   comparing just the label */
		if (!ifa) {
			for (ifap = &in_dev->ifa_list; (ifa = *ifap) != NULL;
			     ifap = &ifa->ifa_next)
				if (!strcmp(ifr.ifr_name, ifa->ifa_label))
					break;
		}
	}

	ret = -EADDRNOTAVAIL;
	if (!ifa && cmd != SIOCSIFADDR && cmd != SIOCSIFFLAGS)
		goto done;

	switch (cmd) {
	case SIOCGIFADDR:	/* Get interface address */
		sin->sin_addr.s_addr = ifa->ifa_local;
		goto rarok;

	case SIOCGIFBRDADDR:	/* Get the broadcast address */
		sin->sin_addr.s_addr = ifa->ifa_broadcast;
		goto rarok;

	case SIOCGIFDSTADDR:	/* Get the destination address */
		sin->sin_addr.s_addr = ifa->ifa_address;
		goto rarok;

	case SIOCGIFNETMASK:	/* Get the netmask for the interface */
		sin->sin_addr.s_addr = ifa->ifa_mask;
		goto rarok;

	case SIOCSIFFLAGS:
		if (colon) {
			ret = -EADDRNOTAVAIL;
			if (!ifa)
				break;
			ret = 0;
			if (!(ifr.ifr_flags & IFF_UP))
				inet_del_ifa(in_dev, ifap, 1);
			break;
		}
		ret = dev_change_flags(dev, ifr.ifr_flags);
		break;

	case SIOCSIFADDR:	/* Set interface address (and family) */
		ret = -EINVAL;
		if (inet_abc_len(sin->sin_addr.s_addr) < 0)
			break;

		if (!ifa) {
			ret = -ENOBUFS;
			if ((ifa = inet_alloc_ifa()) == NULL)
				break;
			if (colon)
				memcpy(ifa->ifa_label, ifr.ifr_name, IFNAMSIZ);
			else
				memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);
		} else {
			ret = 0;
			if (ifa->ifa_local == sin->sin_addr.s_addr)
				break;
			inet_del_ifa(in_dev, ifap, 0);
			ifa->ifa_broadcast = 0;
			ifa->ifa_anycast = 0;
		}

		ifa->ifa_address = ifa->ifa_local = sin->sin_addr.s_addr;

		if (!(dev->flags & IFF_POINTOPOINT)) {
			ifa->ifa_prefixlen = inet_abc_len(ifa->ifa_address);
			ifa->ifa_mask = inet_make_mask(ifa->ifa_prefixlen);
			if ((dev->flags & IFF_BROADCAST) &&
			    ifa->ifa_prefixlen < 31)
				ifa->ifa_broadcast = ifa->ifa_address |
						     ~ifa->ifa_mask;
		} else {
			ifa->ifa_prefixlen = 32;
			ifa->ifa_mask = inet_make_mask(32);
		}
		ret = inet_set_ifa(dev, ifa);
		break;

	case SIOCSIFBRDADDR:	/* Set the broadcast address */
		ret = 0;
		if (ifa->ifa_broadcast != sin->sin_addr.s_addr) {
			inet_del_ifa(in_dev, ifap, 0);
			ifa->ifa_broadcast = sin->sin_addr.s_addr;
			inet_insert_ifa(ifa);
		}
		break;

	case SIOCSIFDSTADDR:	/* Set the destination address */
		ret = 0;
		if (ifa->ifa_address == sin->sin_addr.s_addr)
			break;
		ret = -EINVAL;
		if (inet_abc_len(sin->sin_addr.s_addr) < 0)
			break;
		ret = 0;
		inet_del_ifa(in_dev, ifap, 0);
		ifa->ifa_address = sin->sin_addr.s_addr;
		inet_insert_ifa(ifa);
		break;

	case SIOCSIFNETMASK: 	/* Set the netmask for the interface */

		/*
		 *	The mask we set must be legal.
		 */
		ret = -EINVAL;
		if (bad_mask(sin->sin_addr.s_addr, 0))
			break;
		ret = 0;
		if (ifa->ifa_mask != sin->sin_addr.s_addr) {
			__be32 old_mask = ifa->ifa_mask;
			inet_del_ifa(in_dev, ifap, 0);
			ifa->ifa_mask = sin->sin_addr.s_addr;
			ifa->ifa_prefixlen = inet_mask_len(ifa->ifa_mask);

			/* See if current broadcast address matches
			 * with current netmask, then recalculate
			 * the broadcast address. Otherwise it's a
			 * funny address, so don't touch it since
			 * the user seems to know what (s)he's doing...
			 */
			if ((dev->flags & IFF_BROADCAST) &&
			    (ifa->ifa_prefixlen < 31) &&
			    (ifa->ifa_broadcast ==
			     (ifa->ifa_local|~old_mask))) {
				ifa->ifa_broadcast = (ifa->ifa_local |
						      ~sin->sin_addr.s_addr);
			}
			inet_insert_ifa(ifa);
		}
		break;
	}
done:
	rtnl_unlock();
out:
	return ret;
rarok:
	rtnl_unlock();
	ret = copy_to_user(arg, &ifr, sizeof(struct ifreq)) ? -EFAULT : 0;
	goto out;
}

static int inet_gifconf(struct net_device *dev, char __user *buf, int len)
{
	struct in_device *in_dev = __in_dev_get_rtnl(dev);
	struct in_ifaddr *ifa;
	struct ifreq ifr;
	int done = 0;

	if (!in_dev || (ifa = in_dev->ifa_list) == NULL)
		goto out;

	for (; ifa; ifa = ifa->ifa_next) {
		if (!buf) {
			done += sizeof(ifr);
			continue;
		}
		if (len < (int) sizeof(ifr))
			break;
		memset(&ifr, 0, sizeof(struct ifreq));
		if (ifa->ifa_label)
			strcpy(ifr.ifr_name, ifa->ifa_label);
		else
			strcpy(ifr.ifr_name, dev->name);

		(*(struct sockaddr_in *)&ifr.ifr_addr).sin_family = AF_INET;
		(*(struct sockaddr_in *)&ifr.ifr_addr).sin_addr.s_addr =
								ifa->ifa_local;

		if (copy_to_user(buf, &ifr, sizeof(struct ifreq))) {
			done = -EFAULT;
			break;
		}
		buf  += sizeof(struct ifreq);
		len  -= sizeof(struct ifreq);
		done += sizeof(struct ifreq);
	}
out:
	return done;
}

__be32 inet_select_addr(const struct net_device *dev, __be32 dst, int scope)
{
	__be32 addr = 0;
	struct in_device *in_dev;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);
	if (!in_dev)
		goto no_in_dev;

	for_primary_ifa(in_dev) {
		if (ifa->ifa_scope > scope)
			continue;
		if (!dst || inet_ifa_match(dst, ifa)) {
			addr = ifa->ifa_local;
			break;
		}
		if (!addr)
			addr = ifa->ifa_local;
	} endfor_ifa(in_dev);
no_in_dev:
	rcu_read_unlock();

	if (addr)
		goto out;

	/* Not loopback addresses on loopback should be preferred
	   in this case. It is importnat that lo is the first interface
	   in dev_base list.
	 */
	read_lock(&dev_base_lock);
	rcu_read_lock();
	for_each_netdev(dev) {
		if ((in_dev = __in_dev_get_rcu(dev)) == NULL)
			continue;

		for_primary_ifa(in_dev) {
			if (ifa->ifa_scope != RT_SCOPE_LINK &&
			    ifa->ifa_scope <= scope) {
				addr = ifa->ifa_local;
				goto out_unlock_both;
			}
		} endfor_ifa(in_dev);
	}
out_unlock_both:
	read_unlock(&dev_base_lock);
	rcu_read_unlock();
out:
	return addr;
}

static __be32 confirm_addr_indev(struct in_device *in_dev, __be32 dst,
			      __be32 local, int scope)
{
	int same = 0;
	__be32 addr = 0;

	for_ifa(in_dev) {
		if (!addr &&
		    (local == ifa->ifa_local || !local) &&
		    ifa->ifa_scope <= scope) {
			addr = ifa->ifa_local;
			if (same)
				break;
		}
		if (!same) {
			same = (!local || inet_ifa_match(local, ifa)) &&
				(!dst || inet_ifa_match(dst, ifa));
			if (same && addr) {
				if (local || !dst)
					break;
				/* Is the selected addr into dst subnet? */
				if (inet_ifa_match(addr, ifa))
					break;
				/* No, then can we use new local src? */
				if (ifa->ifa_scope <= scope) {
					addr = ifa->ifa_local;
					break;
				}
				/* search for large dst subnet for addr */
				same = 0;
			}
		}
	} endfor_ifa(in_dev);

	return same? addr : 0;
}

/*
 * Confirm that local IP address exists using wildcards:
 * - dev: only on this interface, 0=any interface
 * - dst: only in the same subnet as dst, 0=any dst
 * - local: address, 0=autoselect the local address
 * - scope: maximum allowed scope value for the local address
 */
__be32 inet_confirm_addr(const struct net_device *dev, __be32 dst, __be32 local, int scope)
{
	__be32 addr = 0;
	struct in_device *in_dev;

	if (dev) {
		rcu_read_lock();
		if ((in_dev = __in_dev_get_rcu(dev)))
			addr = confirm_addr_indev(in_dev, dst, local, scope);
		rcu_read_unlock();

		return addr;
	}

	read_lock(&dev_base_lock);
	rcu_read_lock();
	for_each_netdev(dev) {
		if ((in_dev = __in_dev_get_rcu(dev))) {
			addr = confirm_addr_indev(in_dev, dst, local, scope);
			if (addr)
				break;
		}
	}
	rcu_read_unlock();
	read_unlock(&dev_base_lock);

	return addr;
}

/*
 *	Device notifier
 */

int register_inetaddr_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&inetaddr_chain, nb);
}

int unregister_inetaddr_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&inetaddr_chain, nb);
}

/* Rename ifa_labels for a device name change. Make some effort to preserve existing
 * alias numbering and to create unique labels if possible.
*/
static void inetdev_changename(struct net_device *dev, struct in_device *in_dev)
{
	struct in_ifaddr *ifa;
	int named = 0;

	for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
		char old[IFNAMSIZ], *dot;

		memcpy(old, ifa->ifa_label, IFNAMSIZ);
		memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);
		if (named++ == 0)
			continue;
		dot = strchr(ifa->ifa_label, ':');
		if (dot == NULL) {
			sprintf(old, ":%d", named);
			dot = old;
		}
		if (strlen(dot) + strlen(dev->name) < IFNAMSIZ) {
			strcat(ifa->ifa_label, dot);
		} else {
			strcpy(ifa->ifa_label + (IFNAMSIZ - strlen(dot) - 1), dot);
		}
	}
}

/* Called only under RTNL semaphore */

static int inetdev_event(struct notifier_block *this, unsigned long event,
			 void *ptr)
{
	struct net_device *dev = ptr;
	struct in_device *in_dev = __in_dev_get_rtnl(dev);

	ASSERT_RTNL();

	if (!in_dev) {
		if (event == NETDEV_REGISTER) {
			in_dev = inetdev_init(dev);
			if (!in_dev)
				return notifier_from_errno(-ENOMEM);
			if (dev == &loopback_dev) {
				IN_DEV_CONF_SET(in_dev, NOXFRM, 1);
				IN_DEV_CONF_SET(in_dev, NOPOLICY, 1);
			}
		}
		goto out;
	}

	switch (event) {
	case NETDEV_REGISTER:
		printk(KERN_DEBUG "inetdev_event: bug\n");
		dev->ip_ptr = NULL;
		break;
	case NETDEV_UP:
		if (dev->mtu < 68)
			break;
		if (dev == &loopback_dev) {
			struct in_ifaddr *ifa;
			if ((ifa = inet_alloc_ifa()) != NULL) {
				ifa->ifa_local =
				  ifa->ifa_address = htonl(INADDR_LOOPBACK);
				ifa->ifa_prefixlen = 8;
				ifa->ifa_mask = inet_make_mask(8);
				in_dev_hold(in_dev);
				ifa->ifa_dev = in_dev;
				ifa->ifa_scope = RT_SCOPE_HOST;
				memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);
				inet_insert_ifa(ifa);
			}
		}
		ip_mc_up(in_dev);
		break;
	case NETDEV_DOWN:
		ip_mc_down(in_dev);
		break;
	case NETDEV_CHANGEMTU:
		if (dev->mtu >= 68)
			break;
		/* MTU falled under 68, disable IP */
	case NETDEV_UNREGISTER:
		inetdev_destroy(in_dev);
		break;
	case NETDEV_CHANGENAME:
		/* Do not notify about label change, this event is
		 * not interesting to applications using netlink.
		 */
		inetdev_changename(dev, in_dev);

#ifdef CONFIG_SYSCTL
		devinet_sysctl_unregister(&in_dev->cnf);
		neigh_sysctl_unregister(in_dev->arp_parms);
		neigh_sysctl_register(dev, in_dev->arp_parms, NET_IPV4,
				      NET_IPV4_NEIGH, "ipv4", NULL, NULL);
		devinet_sysctl_register(in_dev, &in_dev->cnf);
#endif
		break;
	}
out:
	return NOTIFY_DONE;
}

static struct notifier_block ip_netdev_notifier = {
	.notifier_call =inetdev_event,
};

static inline size_t inet_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ifaddrmsg))
	       + nla_total_size(4) /* IFA_ADDRESS */
	       + nla_total_size(4) /* IFA_LOCAL */
	       + nla_total_size(4) /* IFA_BROADCAST */
	       + nla_total_size(4) /* IFA_ANYCAST */
	       + nla_total_size(IFNAMSIZ); /* IFA_LABEL */
}

static int inet_fill_ifaddr(struct sk_buff *skb, struct in_ifaddr *ifa,
			    u32 pid, u32 seq, int event, unsigned int flags)
{
	struct ifaddrmsg *ifm;
	struct nlmsghdr  *nlh;

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(*ifm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	ifm = nlmsg_data(nlh);
	ifm->ifa_family = AF_INET;
	ifm->ifa_prefixlen = ifa->ifa_prefixlen;
	ifm->ifa_flags = ifa->ifa_flags|IFA_F_PERMANENT;
	ifm->ifa_scope = ifa->ifa_scope;
	ifm->ifa_index = ifa->ifa_dev->dev->ifindex;

	if (ifa->ifa_address)
		NLA_PUT_BE32(skb, IFA_ADDRESS, ifa->ifa_address);

	if (ifa->ifa_local)
		NLA_PUT_BE32(skb, IFA_LOCAL, ifa->ifa_local);

	if (ifa->ifa_broadcast)
		NLA_PUT_BE32(skb, IFA_BROADCAST, ifa->ifa_broadcast);

	if (ifa->ifa_anycast)
		NLA_PUT_BE32(skb, IFA_ANYCAST, ifa->ifa_anycast);

	if (ifa->ifa_label[0])
		NLA_PUT_STRING(skb, IFA_LABEL, ifa->ifa_label);

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int inet_dump_ifaddr(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx, ip_idx;
	struct net_device *dev;
	struct in_device *in_dev;
	struct in_ifaddr *ifa;
	int s_ip_idx, s_idx = cb->args[0];

	s_ip_idx = ip_idx = cb->args[1];
	idx = 0;
	for_each_netdev(dev) {
		if (idx < s_idx)
			goto cont;
		if (idx > s_idx)
			s_ip_idx = 0;
		if ((in_dev = __in_dev_get_rtnl(dev)) == NULL)
			goto cont;

		for (ifa = in_dev->ifa_list, ip_idx = 0; ifa;
		     ifa = ifa->ifa_next, ip_idx++) {
			if (ip_idx < s_ip_idx)
				continue;
			if (inet_fill_ifaddr(skb, ifa, NETLINK_CB(cb->skb).pid,
					     cb->nlh->nlmsg_seq,
					     RTM_NEWADDR, NLM_F_MULTI) <= 0)
				goto done;
		}
cont:
		idx++;
	}

done:
	cb->args[0] = idx;
	cb->args[1] = ip_idx;

	return skb->len;
}

static void rtmsg_ifa(int event, struct in_ifaddr* ifa, struct nlmsghdr *nlh,
		      u32 pid)
{
	struct sk_buff *skb;
	u32 seq = nlh ? nlh->nlmsg_seq : 0;
	int err = -ENOBUFS;

	skb = nlmsg_new(inet_nlmsg_size(), GFP_KERNEL);
	if (skb == NULL)
		goto errout;

	err = inet_fill_ifaddr(skb, ifa, pid, seq, event, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in inet_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	err = rtnl_notify(skb, pid, RTNLGRP_IPV4_IFADDR, nlh, GFP_KERNEL);
errout:
	if (err < 0)
		rtnl_set_sk_err(RTNLGRP_IPV4_IFADDR, err);
}

#ifdef CONFIG_SYSCTL

static void devinet_copy_dflt_conf(int i)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	for_each_netdev(dev) {
		struct in_device *in_dev;
		rcu_read_lock();
		in_dev = __in_dev_get_rcu(dev);
		if (in_dev && !test_bit(i, in_dev->cnf.state))
			in_dev->cnf.data[i] = ipv4_devconf_dflt.data[i];
		rcu_read_unlock();
	}
	read_unlock(&dev_base_lock);
}

static int devinet_conf_proc(ctl_table *ctl, int write,
			     struct file* filp, void __user *buffer,
			     size_t *lenp, loff_t *ppos)
{
	int ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);

	if (write) {
		struct ipv4_devconf *cnf = ctl->extra1;
		int i = (int *)ctl->data - cnf->data;

		set_bit(i, cnf->state);

		if (cnf == &ipv4_devconf_dflt)
			devinet_copy_dflt_conf(i);
	}

	return ret;
}

static int devinet_conf_sysctl(ctl_table *table, int __user *name, int nlen,
			       void __user *oldval, size_t __user *oldlenp,
			       void __user *newval, size_t newlen)
{
	struct ipv4_devconf *cnf;
	int *valp = table->data;
	int new;
	int i;

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

	*valp = new;

	cnf = table->extra1;
	i = (int *)table->data - cnf->data;

	set_bit(i, cnf->state);

	if (cnf == &ipv4_devconf_dflt)
		devinet_copy_dflt_conf(i);

	return 1;
}

void inet_forward_change(void)
{
	struct net_device *dev;
	int on = IPV4_DEVCONF_ALL(FORWARDING);

	IPV4_DEVCONF_ALL(ACCEPT_REDIRECTS) = !on;
	IPV4_DEVCONF_DFLT(FORWARDING) = on;

	read_lock(&dev_base_lock);
	for_each_netdev(dev) {
		struct in_device *in_dev;
		rcu_read_lock();
		in_dev = __in_dev_get_rcu(dev);
		if (in_dev)
			IN_DEV_CONF_SET(in_dev, FORWARDING, on);
		rcu_read_unlock();
	}
	read_unlock(&dev_base_lock);

	rt_cache_flush(0);
}

static int devinet_sysctl_forward(ctl_table *ctl, int write,
				  struct file* filp, void __user *buffer,
				  size_t *lenp, loff_t *ppos)
{
	int *valp = ctl->data;
	int val = *valp;
	int ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);

	if (write && *valp != val) {
		if (valp == &IPV4_DEVCONF_ALL(FORWARDING))
			inet_forward_change();
		else if (valp != &IPV4_DEVCONF_DFLT(FORWARDING))
			rt_cache_flush(0);
	}

	return ret;
}

int ipv4_doint_and_flush(ctl_table *ctl, int write,
			 struct file* filp, void __user *buffer,
			 size_t *lenp, loff_t *ppos)
{
	int *valp = ctl->data;
	int val = *valp;
	int ret = proc_dointvec(ctl, write, filp, buffer, lenp, ppos);

	if (write && *valp != val)
		rt_cache_flush(0);

	return ret;
}

int ipv4_doint_and_flush_strategy(ctl_table *table, int __user *name, int nlen,
				  void __user *oldval, size_t __user *oldlenp,
				  void __user *newval, size_t newlen)
{
	int ret = devinet_conf_sysctl(table, name, nlen, oldval, oldlenp,
				      newval, newlen);

	if (ret == 1)
		rt_cache_flush(0);

	return ret;
}


#define DEVINET_SYSCTL_ENTRY(attr, name, mval, proc, sysctl) \
	{ \
		.ctl_name	= NET_IPV4_CONF_ ## attr, \
		.procname	= name, \
		.data		= ipv4_devconf.data + \
				  NET_IPV4_CONF_ ## attr - 1, \
		.maxlen		= sizeof(int), \
		.mode		= mval, \
		.proc_handler	= proc, \
		.strategy	= sysctl, \
		.extra1		= &ipv4_devconf, \
	}

#define DEVINET_SYSCTL_RW_ENTRY(attr, name) \
	DEVINET_SYSCTL_ENTRY(attr, name, 0644, devinet_conf_proc, \
			     devinet_conf_sysctl)

#define DEVINET_SYSCTL_RO_ENTRY(attr, name) \
	DEVINET_SYSCTL_ENTRY(attr, name, 0444, devinet_conf_proc, \
			     devinet_conf_sysctl)

#define DEVINET_SYSCTL_COMPLEX_ENTRY(attr, name, proc, sysctl) \
	DEVINET_SYSCTL_ENTRY(attr, name, 0644, proc, sysctl)

#define DEVINET_SYSCTL_FLUSHING_ENTRY(attr, name) \
	DEVINET_SYSCTL_COMPLEX_ENTRY(attr, name, ipv4_doint_and_flush, \
				     ipv4_doint_and_flush_strategy)

static struct devinet_sysctl_table {
	struct ctl_table_header *sysctl_header;
	ctl_table		devinet_vars[__NET_IPV4_CONF_MAX];
	ctl_table		devinet_dev[2];
	ctl_table		devinet_conf_dir[2];
	ctl_table		devinet_proto_dir[2];
	ctl_table		devinet_root_dir[2];
} devinet_sysctl = {
	.devinet_vars = {
		DEVINET_SYSCTL_COMPLEX_ENTRY(FORWARDING, "forwarding",
					     devinet_sysctl_forward,
					     devinet_conf_sysctl),
		DEVINET_SYSCTL_RO_ENTRY(MC_FORWARDING, "mc_forwarding"),

		DEVINET_SYSCTL_RW_ENTRY(ACCEPT_REDIRECTS, "accept_redirects"),
		DEVINET_SYSCTL_RW_ENTRY(SECURE_REDIRECTS, "secure_redirects"),
		DEVINET_SYSCTL_RW_ENTRY(SHARED_MEDIA, "shared_media"),
		DEVINET_SYSCTL_RW_ENTRY(RP_FILTER, "rp_filter"),
		DEVINET_SYSCTL_RW_ENTRY(SEND_REDIRECTS, "send_redirects"),
		DEVINET_SYSCTL_RW_ENTRY(ACCEPT_SOURCE_ROUTE,
					"accept_source_route"),
		DEVINET_SYSCTL_RW_ENTRY(PROXY_ARP, "proxy_arp"),
		DEVINET_SYSCTL_RW_ENTRY(MEDIUM_ID, "medium_id"),
		DEVINET_SYSCTL_RW_ENTRY(BOOTP_RELAY, "bootp_relay"),
		DEVINET_SYSCTL_RW_ENTRY(LOG_MARTIANS, "log_martians"),
		DEVINET_SYSCTL_RW_ENTRY(TAG, "tag"),
		DEVINET_SYSCTL_RW_ENTRY(ARPFILTER, "arp_filter"),
		DEVINET_SYSCTL_RW_ENTRY(ARP_ANNOUNCE, "arp_announce"),
		DEVINET_SYSCTL_RW_ENTRY(ARP_IGNORE, "arp_ignore"),
		DEVINET_SYSCTL_RW_ENTRY(ARP_ACCEPT, "arp_accept"),

		DEVINET_SYSCTL_FLUSHING_ENTRY(NOXFRM, "disable_xfrm"),
		DEVINET_SYSCTL_FLUSHING_ENTRY(NOPOLICY, "disable_policy"),
		DEVINET_SYSCTL_FLUSHING_ENTRY(FORCE_IGMP_VERSION,
					      "force_igmp_version"),
		DEVINET_SYSCTL_FLUSHING_ENTRY(PROMOTE_SECONDARIES,
					      "promote_secondaries"),
	},
	.devinet_dev = {
		{
			.ctl_name	= NET_PROTO_CONF_ALL,
			.procname	= "all",
			.mode		= 0555,
			.child		= devinet_sysctl.devinet_vars,
		},
	},
	.devinet_conf_dir = {
		{
			.ctl_name	= NET_IPV4_CONF,
			.procname	= "conf",
			.mode		= 0555,
			.child		= devinet_sysctl.devinet_dev,
		},
	},
	.devinet_proto_dir = {
		{
			.ctl_name	= NET_IPV4,
			.procname	= "ipv4",
			.mode		= 0555,
			.child 		= devinet_sysctl.devinet_conf_dir,
		},
	},
	.devinet_root_dir = {
		{
			.ctl_name	= CTL_NET,
			.procname 	= "net",
			.mode		= 0555,
			.child		= devinet_sysctl.devinet_proto_dir,
		},
	},
};

static void devinet_sysctl_register(struct in_device *in_dev,
				    struct ipv4_devconf *p)
{
	int i;
	struct net_device *dev = in_dev ? in_dev->dev : NULL;
	struct devinet_sysctl_table *t = kmemdup(&devinet_sysctl, sizeof(*t),
						 GFP_KERNEL);
	char *dev_name = NULL;

	if (!t)
		return;
	for (i = 0; i < ARRAY_SIZE(t->devinet_vars) - 1; i++) {
		t->devinet_vars[i].data += (char *)p - (char *)&ipv4_devconf;
		t->devinet_vars[i].extra1 = p;
	}

	if (dev) {
		dev_name = dev->name;
		t->devinet_dev[0].ctl_name = dev->ifindex;
	} else {
		dev_name = "default";
		t->devinet_dev[0].ctl_name = NET_PROTO_CONF_DEFAULT;
	}

	/*
	 * Make a copy of dev_name, because '.procname' is regarded as const
	 * by sysctl and we wouldn't want anyone to change it under our feet
	 * (see SIOCSIFNAME).
	 */
	dev_name = kstrdup(dev_name, GFP_KERNEL);
	if (!dev_name)
	    goto free;

	t->devinet_dev[0].procname    = dev_name;
	t->devinet_dev[0].child	      = t->devinet_vars;
	t->devinet_conf_dir[0].child  = t->devinet_dev;
	t->devinet_proto_dir[0].child = t->devinet_conf_dir;
	t->devinet_root_dir[0].child  = t->devinet_proto_dir;

	t->sysctl_header = register_sysctl_table(t->devinet_root_dir);
	if (!t->sysctl_header)
	    goto free_procname;

	p->sysctl = t;
	return;

	/* error path */
 free_procname:
	kfree(dev_name);
 free:
	kfree(t);
	return;
}

static void devinet_sysctl_unregister(struct ipv4_devconf *p)
{
	if (p->sysctl) {
		struct devinet_sysctl_table *t = p->sysctl;
		p->sysctl = NULL;
		unregister_sysctl_table(t->sysctl_header);
		kfree(t->devinet_dev[0].procname);
		kfree(t);
	}
}
#endif

void __init devinet_init(void)
{
	register_gifconf(PF_INET, inet_gifconf);
	register_netdevice_notifier(&ip_netdev_notifier);

	rtnl_register(PF_INET, RTM_NEWADDR, inet_rtm_newaddr, NULL);
	rtnl_register(PF_INET, RTM_DELADDR, inet_rtm_deladdr, NULL);
	rtnl_register(PF_INET, RTM_GETADDR, NULL, inet_dump_ifaddr);
#ifdef CONFIG_SYSCTL
	devinet_sysctl.sysctl_header =
		register_sysctl_table(devinet_sysctl.devinet_root_dir);
	devinet_sysctl_register(NULL, &ipv4_devconf_dflt);
#endif
}

EXPORT_SYMBOL(in_dev_finish_destroy);
EXPORT_SYMBOL(inet_select_addr);
EXPORT_SYMBOL(inetdev_by_index);
EXPORT_SYMBOL(register_inetaddr_notifier);
EXPORT_SYMBOL(unregister_inetaddr_notifier);
