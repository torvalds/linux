/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Device Layer
 *
 * Authors:     Steve Whitehouse <SteveW@ACM.org>
 *              Eduardo Marcelo Serrat <emserrat@geocities.com>
 *
 * Changes:
 *          Steve Whitehouse : Devices now see incoming frames so they
 *                             can mark on who it came from.
 *          Steve Whitehouse : Fixed bug in creating neighbours. Each neighbour
 *                             can now have a device specific setup func.
 *          Steve Whitehouse : Added /proc/sys/net/decnet/conf/<dev>/
 *          Steve Whitehouse : Fixed bug which sometimes killed timer
 *          Steve Whitehouse : Multiple ifaddr support
 *          Steve Whitehouse : SIOCGIFCONF is now a compile time option
 *          Steve Whitehouse : /proc/sys/net/decnet/conf/<sys>/forwarding
 *          Steve Whitehouse : Removed timer1 - it's a user space issue now
 *         Patrick Caulfield : Fixed router hello message format
 *          Steve Whitehouse : Got rid of constant sizes for blksize for
 *                             devices. All mtu based now.
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/if_addr.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/sysctl.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <asm/uaccess.h>
#include <net/net_namespace.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/fib_rules.h>
#include <net/netlink.h>
#include <net/dn.h>
#include <net/dn_dev.h>
#include <net/dn_route.h>
#include <net/dn_neigh.h>
#include <net/dn_fib.h>

#define DN_IFREQ_SIZE (sizeof(struct ifreq) - sizeof(struct sockaddr) + sizeof(struct sockaddr_dn))

static char dn_rt_all_end_mcast[ETH_ALEN] = {0xAB,0x00,0x00,0x04,0x00,0x00};
static char dn_rt_all_rt_mcast[ETH_ALEN]  = {0xAB,0x00,0x00,0x03,0x00,0x00};
static char dn_hiord[ETH_ALEN]            = {0xAA,0x00,0x04,0x00,0x00,0x00};
static unsigned char dn_eco_version[3]    = {0x02,0x00,0x00};

extern struct neigh_table dn_neigh_table;

/*
 * decnet_address is kept in network order.
 */
__le16 decnet_address = 0;

static DEFINE_SPINLOCK(dndev_lock);
static struct net_device *decnet_default_device;
static BLOCKING_NOTIFIER_HEAD(dnaddr_chain);

static struct dn_dev *dn_dev_create(struct net_device *dev, int *err);
static void dn_dev_delete(struct net_device *dev);
static void dn_ifaddr_notify(int event, struct dn_ifaddr *ifa);

static int dn_eth_up(struct net_device *);
static void dn_eth_down(struct net_device *);
static void dn_send_brd_hello(struct net_device *dev, struct dn_ifaddr *ifa);
static void dn_send_ptp_hello(struct net_device *dev, struct dn_ifaddr *ifa);

static struct dn_dev_parms dn_dev_list[] =  {
{
	.type =		ARPHRD_ETHER, /* Ethernet */
	.mode =		DN_DEV_BCAST,
	.state =	DN_DEV_S_RU,
	.t2 =		1,
	.t3 =		10,
	.name =		"ethernet",
	.up =		dn_eth_up,
	.down = 	dn_eth_down,
	.timer3 =	dn_send_brd_hello,
},
{
	.type =		ARPHRD_IPGRE, /* DECnet tunneled over GRE in IP */
	.mode =		DN_DEV_BCAST,
	.state =	DN_DEV_S_RU,
	.t2 =		1,
	.t3 =		10,
	.name =		"ipgre",
	.timer3 =	dn_send_brd_hello,
},
#if 0
{
	.type =		ARPHRD_X25, /* Bog standard X.25 */
	.mode =		DN_DEV_UCAST,
	.state =	DN_DEV_S_DS,
	.t2 =		1,
	.t3 =		120,
	.name =		"x25",
	.timer3 =	dn_send_ptp_hello,
},
#endif
#if 0
{
	.type =		ARPHRD_PPP, /* DECnet over PPP */
	.mode =		DN_DEV_BCAST,
	.state =	DN_DEV_S_RU,
	.t2 =		1,
	.t3 =		10,
	.name =		"ppp",
	.timer3 =	dn_send_brd_hello,
},
#endif
{
	.type =		ARPHRD_DDCMP, /* DECnet over DDCMP */
	.mode =		DN_DEV_UCAST,
	.state =	DN_DEV_S_DS,
	.t2 =		1,
	.t3 =		120,
	.name =		"ddcmp",
	.timer3 =	dn_send_ptp_hello,
},
{
	.type =		ARPHRD_LOOPBACK, /* Loopback interface - always last */
	.mode =		DN_DEV_BCAST,
	.state =	DN_DEV_S_RU,
	.t2 =		1,
	.t3 =		10,
	.name =		"loopback",
	.timer3 =	dn_send_brd_hello,
}
};

#define DN_DEV_LIST_SIZE ARRAY_SIZE(dn_dev_list)

#define DN_DEV_PARMS_OFFSET(x) offsetof(struct dn_dev_parms, x)

#ifdef CONFIG_SYSCTL

static int min_t2[] = { 1 };
static int max_t2[] = { 60 }; /* No max specified, but this seems sensible */
static int min_t3[] = { 1 };
static int max_t3[] = { 8191 }; /* Must fit in 16 bits when multiplied by BCT3MULT or T3MULT */

static int min_priority[1];
static int max_priority[] = { 127 }; /* From DECnet spec */

static int dn_forwarding_proc(struct ctl_table *, int,
			void __user *, size_t *, loff_t *);
static struct dn_dev_sysctl_table {
	struct ctl_table_header *sysctl_header;
	struct ctl_table dn_dev_vars[5];
} dn_dev_sysctl = {
	NULL,
	{
	{
		.procname = "forwarding",
		.data = (void *)DN_DEV_PARMS_OFFSET(forwarding),
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = dn_forwarding_proc,
	},
	{
		.procname = "priority",
		.data = (void *)DN_DEV_PARMS_OFFSET(priority),
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = &min_priority,
		.extra2 = &max_priority
	},
	{
		.procname = "t2",
		.data = (void *)DN_DEV_PARMS_OFFSET(t2),
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = &min_t2,
		.extra2 = &max_t2
	},
	{
		.procname = "t3",
		.data = (void *)DN_DEV_PARMS_OFFSET(t3),
		.maxlen = sizeof(int),
		.mode = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1 = &min_t3,
		.extra2 = &max_t3
	},
	{0}
	},
};

static void dn_dev_sysctl_register(struct net_device *dev, struct dn_dev_parms *parms)
{
	struct dn_dev_sysctl_table *t;
	int i;

	char path[sizeof("net/decnet/conf/") + IFNAMSIZ];

	t = kmemdup(&dn_dev_sysctl, sizeof(*t), GFP_KERNEL);
	if (t == NULL)
		return;

	for(i = 0; i < ARRAY_SIZE(t->dn_dev_vars) - 1; i++) {
		long offset = (long)t->dn_dev_vars[i].data;
		t->dn_dev_vars[i].data = ((char *)parms) + offset;
	}

	snprintf(path, sizeof(path), "net/decnet/conf/%s",
		dev? dev->name : parms->name);

	t->dn_dev_vars[0].extra1 = (void *)dev;

	t->sysctl_header = register_net_sysctl(&init_net, path, t->dn_dev_vars);
	if (t->sysctl_header == NULL)
		kfree(t);
	else
		parms->sysctl = t;
}

static void dn_dev_sysctl_unregister(struct dn_dev_parms *parms)
{
	if (parms->sysctl) {
		struct dn_dev_sysctl_table *t = parms->sysctl;
		parms->sysctl = NULL;
		unregister_net_sysctl_table(t->sysctl_header);
		kfree(t);
	}
}

static int dn_forwarding_proc(struct ctl_table *table, int write,
				void __user *buffer,
				size_t *lenp, loff_t *ppos)
{
#ifdef CONFIG_DECNET_ROUTER
	struct net_device *dev = table->extra1;
	struct dn_dev *dn_db;
	int err;
	int tmp, old;

	if (table->extra1 == NULL)
		return -EINVAL;

	dn_db = rcu_dereference_raw(dev->dn_ptr);
	old = dn_db->parms.forwarding;

	err = proc_dointvec(table, write, buffer, lenp, ppos);

	if ((err >= 0) && write) {
		if (dn_db->parms.forwarding < 0)
			dn_db->parms.forwarding = 0;
		if (dn_db->parms.forwarding > 2)
			dn_db->parms.forwarding = 2;
		/*
		 * What an ugly hack this is... its works, just. It
		 * would be nice if sysctl/proc were just that little
		 * bit more flexible so I don't have to write a special
		 * routine, or suffer hacks like this - SJW
		 */
		tmp = dn_db->parms.forwarding;
		dn_db->parms.forwarding = old;
		if (dn_db->parms.down)
			dn_db->parms.down(dev);
		dn_db->parms.forwarding = tmp;
		if (dn_db->parms.up)
			dn_db->parms.up(dev);
	}

	return err;
#else
	return -EINVAL;
#endif
}

#else /* CONFIG_SYSCTL */
static void dn_dev_sysctl_unregister(struct dn_dev_parms *parms)
{
}
static void dn_dev_sysctl_register(struct net_device *dev, struct dn_dev_parms *parms)
{
}

#endif /* CONFIG_SYSCTL */

static inline __u16 mtu2blksize(struct net_device *dev)
{
	u32 blksize = dev->mtu;
	if (blksize > 0xffff)
		blksize = 0xffff;

	if (dev->type == ARPHRD_ETHER ||
	    dev->type == ARPHRD_PPP ||
	    dev->type == ARPHRD_IPGRE ||
	    dev->type == ARPHRD_LOOPBACK)
		blksize -= 2;

	return (__u16)blksize;
}

static struct dn_ifaddr *dn_dev_alloc_ifa(void)
{
	struct dn_ifaddr *ifa;

	ifa = kzalloc(sizeof(*ifa), GFP_KERNEL);

	return ifa;
}

static void dn_dev_free_ifa(struct dn_ifaddr *ifa)
{
	kfree_rcu(ifa, rcu);
}

static void dn_dev_del_ifa(struct dn_dev *dn_db, struct dn_ifaddr __rcu **ifap, int destroy)
{
	struct dn_ifaddr *ifa1 = rtnl_dereference(*ifap);
	unsigned char mac_addr[6];
	struct net_device *dev = dn_db->dev;

	ASSERT_RTNL();

	*ifap = ifa1->ifa_next;

	if (dn_db->dev->type == ARPHRD_ETHER) {
		if (ifa1->ifa_local != dn_eth2dn(dev->dev_addr)) {
			dn_dn2eth(mac_addr, ifa1->ifa_local);
			dev_mc_del(dev, mac_addr);
		}
	}

	dn_ifaddr_notify(RTM_DELADDR, ifa1);
	blocking_notifier_call_chain(&dnaddr_chain, NETDEV_DOWN, ifa1);
	if (destroy) {
		dn_dev_free_ifa(ifa1);

		if (dn_db->ifa_list == NULL)
			dn_dev_delete(dn_db->dev);
	}
}

static int dn_dev_insert_ifa(struct dn_dev *dn_db, struct dn_ifaddr *ifa)
{
	struct net_device *dev = dn_db->dev;
	struct dn_ifaddr *ifa1;
	unsigned char mac_addr[6];

	ASSERT_RTNL();

	/* Check for duplicates */
	for (ifa1 = rtnl_dereference(dn_db->ifa_list);
	     ifa1 != NULL;
	     ifa1 = rtnl_dereference(ifa1->ifa_next)) {
		if (ifa1->ifa_local == ifa->ifa_local)
			return -EEXIST;
	}

	if (dev->type == ARPHRD_ETHER) {
		if (ifa->ifa_local != dn_eth2dn(dev->dev_addr)) {
			dn_dn2eth(mac_addr, ifa->ifa_local);
			dev_mc_add(dev, mac_addr);
		}
	}

	ifa->ifa_next = dn_db->ifa_list;
	rcu_assign_pointer(dn_db->ifa_list, ifa);

	dn_ifaddr_notify(RTM_NEWADDR, ifa);
	blocking_notifier_call_chain(&dnaddr_chain, NETDEV_UP, ifa);

	return 0;
}

static int dn_dev_set_ifa(struct net_device *dev, struct dn_ifaddr *ifa)
{
	struct dn_dev *dn_db = rtnl_dereference(dev->dn_ptr);
	int rv;

	if (dn_db == NULL) {
		int err;
		dn_db = dn_dev_create(dev, &err);
		if (dn_db == NULL)
			return err;
	}

	ifa->ifa_dev = dn_db;

	if (dev->flags & IFF_LOOPBACK)
		ifa->ifa_scope = RT_SCOPE_HOST;

	rv = dn_dev_insert_ifa(dn_db, ifa);
	if (rv)
		dn_dev_free_ifa(ifa);
	return rv;
}


int dn_dev_ioctl(unsigned int cmd, void __user *arg)
{
	char buffer[DN_IFREQ_SIZE];
	struct ifreq *ifr = (struct ifreq *)buffer;
	struct sockaddr_dn *sdn = (struct sockaddr_dn *)&ifr->ifr_addr;
	struct dn_dev *dn_db;
	struct net_device *dev;
	struct dn_ifaddr *ifa = NULL;
	struct dn_ifaddr __rcu **ifap = NULL;
	int ret = 0;

	if (copy_from_user(ifr, arg, DN_IFREQ_SIZE))
		return -EFAULT;
	ifr->ifr_name[IFNAMSIZ-1] = 0;

	dev_load(&init_net, ifr->ifr_name);

	switch (cmd) {
	case SIOCGIFADDR:
		break;
	case SIOCSIFADDR:
		if (!capable(CAP_NET_ADMIN))
			return -EACCES;
		if (sdn->sdn_family != AF_DECnet)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	rtnl_lock();

	if ((dev = __dev_get_by_name(&init_net, ifr->ifr_name)) == NULL) {
		ret = -ENODEV;
		goto done;
	}

	if ((dn_db = rtnl_dereference(dev->dn_ptr)) != NULL) {
		for (ifap = &dn_db->ifa_list;
		     (ifa = rtnl_dereference(*ifap)) != NULL;
		     ifap = &ifa->ifa_next)
			if (strcmp(ifr->ifr_name, ifa->ifa_label) == 0)
				break;
	}

	if (ifa == NULL && cmd != SIOCSIFADDR) {
		ret = -EADDRNOTAVAIL;
		goto done;
	}

	switch (cmd) {
	case SIOCGIFADDR:
		*((__le16 *)sdn->sdn_nodeaddr) = ifa->ifa_local;
		goto rarok;

	case SIOCSIFADDR:
		if (!ifa) {
			if ((ifa = dn_dev_alloc_ifa()) == NULL) {
				ret = -ENOBUFS;
				break;
			}
			memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);
		} else {
			if (ifa->ifa_local == dn_saddr2dn(sdn))
				break;
			dn_dev_del_ifa(dn_db, ifap, 0);
		}

		ifa->ifa_local = ifa->ifa_address = dn_saddr2dn(sdn);

		ret = dn_dev_set_ifa(dev, ifa);
	}
done:
	rtnl_unlock();

	return ret;
rarok:
	if (copy_to_user(arg, ifr, DN_IFREQ_SIZE))
		ret = -EFAULT;
	goto done;
}

struct net_device *dn_dev_get_default(void)
{
	struct net_device *dev;

	spin_lock(&dndev_lock);
	dev = decnet_default_device;
	if (dev) {
		if (dev->dn_ptr)
			dev_hold(dev);
		else
			dev = NULL;
	}
	spin_unlock(&dndev_lock);

	return dev;
}

int dn_dev_set_default(struct net_device *dev, int force)
{
	struct net_device *old = NULL;
	int rv = -EBUSY;
	if (!dev->dn_ptr)
		return -ENODEV;

	spin_lock(&dndev_lock);
	if (force || decnet_default_device == NULL) {
		old = decnet_default_device;
		decnet_default_device = dev;
		rv = 0;
	}
	spin_unlock(&dndev_lock);

	if (old)
		dev_put(old);
	return rv;
}

static void dn_dev_check_default(struct net_device *dev)
{
	spin_lock(&dndev_lock);
	if (dev == decnet_default_device) {
		decnet_default_device = NULL;
	} else {
		dev = NULL;
	}
	spin_unlock(&dndev_lock);

	if (dev)
		dev_put(dev);
}

/*
 * Called with RTNL
 */
static struct dn_dev *dn_dev_by_index(int ifindex)
{
	struct net_device *dev;
	struct dn_dev *dn_dev = NULL;

	dev = __dev_get_by_index(&init_net, ifindex);
	if (dev)
		dn_dev = rtnl_dereference(dev->dn_ptr);

	return dn_dev;
}

static const struct nla_policy dn_ifa_policy[IFA_MAX+1] = {
	[IFA_ADDRESS]		= { .type = NLA_U16 },
	[IFA_LOCAL]		= { .type = NLA_U16 },
	[IFA_LABEL]		= { .type = NLA_STRING,
				    .len = IFNAMSIZ - 1 },
	[IFA_FLAGS]		= { .type = NLA_U32 },
};

static int dn_nl_deladdr(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tb[IFA_MAX+1];
	struct dn_dev *dn_db;
	struct ifaddrmsg *ifm;
	struct dn_ifaddr *ifa;
	struct dn_ifaddr __rcu **ifap;
	int err = -EINVAL;

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (!net_eq(net, &init_net))
		goto errout;

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, dn_ifa_policy);
	if (err < 0)
		goto errout;

	err = -ENODEV;
	ifm = nlmsg_data(nlh);
	if ((dn_db = dn_dev_by_index(ifm->ifa_index)) == NULL)
		goto errout;

	err = -EADDRNOTAVAIL;
	for (ifap = &dn_db->ifa_list;
	     (ifa = rtnl_dereference(*ifap)) != NULL;
	     ifap = &ifa->ifa_next) {
		if (tb[IFA_LOCAL] &&
		    nla_memcmp(tb[IFA_LOCAL], &ifa->ifa_local, 2))
			continue;

		if (tb[IFA_LABEL] && nla_strcmp(tb[IFA_LABEL], ifa->ifa_label))
			continue;

		dn_dev_del_ifa(dn_db, ifap, 1);
		return 0;
	}

errout:
	return err;
}

static int dn_nl_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tb[IFA_MAX+1];
	struct net_device *dev;
	struct dn_dev *dn_db;
	struct ifaddrmsg *ifm;
	struct dn_ifaddr *ifa;
	int err;

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (!net_eq(net, &init_net))
		return -EINVAL;

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, dn_ifa_policy);
	if (err < 0)
		return err;

	if (tb[IFA_LOCAL] == NULL)
		return -EINVAL;

	ifm = nlmsg_data(nlh);
	if ((dev = __dev_get_by_index(&init_net, ifm->ifa_index)) == NULL)
		return -ENODEV;

	if ((dn_db = rtnl_dereference(dev->dn_ptr)) == NULL) {
		dn_db = dn_dev_create(dev, &err);
		if (!dn_db)
			return err;
	}

	if ((ifa = dn_dev_alloc_ifa()) == NULL)
		return -ENOBUFS;

	if (tb[IFA_ADDRESS] == NULL)
		tb[IFA_ADDRESS] = tb[IFA_LOCAL];

	ifa->ifa_local = nla_get_le16(tb[IFA_LOCAL]);
	ifa->ifa_address = nla_get_le16(tb[IFA_ADDRESS]);
	ifa->ifa_flags = tb[IFA_FLAGS] ? nla_get_u32(tb[IFA_FLAGS]) :
					 ifm->ifa_flags;
	ifa->ifa_scope = ifm->ifa_scope;
	ifa->ifa_dev = dn_db;

	if (tb[IFA_LABEL])
		nla_strlcpy(ifa->ifa_label, tb[IFA_LABEL], IFNAMSIZ);
	else
		memcpy(ifa->ifa_label, dev->name, IFNAMSIZ);

	err = dn_dev_insert_ifa(dn_db, ifa);
	if (err)
		dn_dev_free_ifa(ifa);

	return err;
}

static inline size_t dn_ifaddr_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ifaddrmsg))
	       + nla_total_size(IFNAMSIZ) /* IFA_LABEL */
	       + nla_total_size(2) /* IFA_ADDRESS */
	       + nla_total_size(2) /* IFA_LOCAL */
	       + nla_total_size(4); /* IFA_FLAGS */
}

static int dn_nl_fill_ifaddr(struct sk_buff *skb, struct dn_ifaddr *ifa,
			     u32 portid, u32 seq, int event, unsigned int flags)
{
	struct ifaddrmsg *ifm;
	struct nlmsghdr *nlh;
	u32 ifa_flags = ifa->ifa_flags | IFA_F_PERMANENT;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*ifm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	ifm = nlmsg_data(nlh);
	ifm->ifa_family = AF_DECnet;
	ifm->ifa_prefixlen = 16;
	ifm->ifa_flags = ifa_flags;
	ifm->ifa_scope = ifa->ifa_scope;
	ifm->ifa_index = ifa->ifa_dev->dev->ifindex;

	if ((ifa->ifa_address &&
	     nla_put_le16(skb, IFA_ADDRESS, ifa->ifa_address)) ||
	    (ifa->ifa_local &&
	     nla_put_le16(skb, IFA_LOCAL, ifa->ifa_local)) ||
	    (ifa->ifa_label[0] &&
	     nla_put_string(skb, IFA_LABEL, ifa->ifa_label)) ||
	     nla_put_u32(skb, IFA_FLAGS, ifa_flags))
		goto nla_put_failure;
	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static void dn_ifaddr_notify(int event, struct dn_ifaddr *ifa)
{
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = alloc_skb(dn_ifaddr_nlmsg_size(), GFP_KERNEL);
	if (skb == NULL)
		goto errout;

	err = dn_nl_fill_ifaddr(skb, ifa, 0, 0, event, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in dn_ifaddr_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, &init_net, 0, RTNLGRP_DECnet_IFADDR, NULL, GFP_KERNEL);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(&init_net, RTNLGRP_DECnet_IFADDR, err);
}

static int dn_nl_dump_ifaddr(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	int idx, dn_idx = 0, skip_ndevs, skip_naddr;
	struct net_device *dev;
	struct dn_dev *dn_db;
	struct dn_ifaddr *ifa;

	if (!net_eq(net, &init_net))
		return 0;

	skip_ndevs = cb->args[0];
	skip_naddr = cb->args[1];

	idx = 0;
	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
		if (idx < skip_ndevs)
			goto cont;
		else if (idx > skip_ndevs) {
			/* Only skip over addresses for first dev dumped
			 * in this iteration (idx == skip_ndevs) */
			skip_naddr = 0;
		}

		if ((dn_db = rcu_dereference(dev->dn_ptr)) == NULL)
			goto cont;

		for (ifa = rcu_dereference(dn_db->ifa_list), dn_idx = 0; ifa;
		     ifa = rcu_dereference(ifa->ifa_next), dn_idx++) {
			if (dn_idx < skip_naddr)
				continue;

			if (dn_nl_fill_ifaddr(skb, ifa, NETLINK_CB(cb->skb).portid,
					      cb->nlh->nlmsg_seq, RTM_NEWADDR,
					      NLM_F_MULTI) < 0)
				goto done;
		}
cont:
		idx++;
	}
done:
	rcu_read_unlock();
	cb->args[0] = idx;
	cb->args[1] = dn_idx;

	return skb->len;
}

static int dn_dev_get_first(struct net_device *dev, __le16 *addr)
{
	struct dn_dev *dn_db;
	struct dn_ifaddr *ifa;
	int rv = -ENODEV;

	rcu_read_lock();
	dn_db = rcu_dereference(dev->dn_ptr);
	if (dn_db == NULL)
		goto out;

	ifa = rcu_dereference(dn_db->ifa_list);
	if (ifa != NULL) {
		*addr = ifa->ifa_local;
		rv = 0;
	}
out:
	rcu_read_unlock();
	return rv;
}

/*
 * Find a default address to bind to.
 *
 * This is one of those areas where the initial VMS concepts don't really
 * map onto the Linux concepts, and since we introduced multiple addresses
 * per interface we have to cope with slightly odd ways of finding out what
 * "our address" really is. Mostly it's not a problem; for this we just guess
 * a sensible default. Eventually the routing code will take care of all the
 * nasties for us I hope.
 */
int dn_dev_bind_default(__le16 *addr)
{
	struct net_device *dev;
	int rv;
	dev = dn_dev_get_default();
last_chance:
	if (dev) {
		rv = dn_dev_get_first(dev, addr);
		dev_put(dev);
		if (rv == 0 || dev == init_net.loopback_dev)
			return rv;
	}
	dev = init_net.loopback_dev;
	dev_hold(dev);
	goto last_chance;
}

static void dn_send_endnode_hello(struct net_device *dev, struct dn_ifaddr *ifa)
{
	struct endnode_hello_message *msg;
	struct sk_buff *skb = NULL;
	__le16 *pktlen;
	struct dn_dev *dn_db = rcu_dereference_raw(dev->dn_ptr);

	if ((skb = dn_alloc_skb(NULL, sizeof(*msg), GFP_ATOMIC)) == NULL)
		return;

	skb->dev = dev;

	msg = (struct endnode_hello_message *)skb_put(skb,sizeof(*msg));

	msg->msgflg  = 0x0D;
	memcpy(msg->tiver, dn_eco_version, 3);
	dn_dn2eth(msg->id, ifa->ifa_local);
	msg->iinfo   = DN_RT_INFO_ENDN;
	msg->blksize = cpu_to_le16(mtu2blksize(dev));
	msg->area    = 0x00;
	memset(msg->seed, 0, 8);
	memcpy(msg->neighbor, dn_hiord, ETH_ALEN);

	if (dn_db->router) {
		struct dn_neigh *dn = (struct dn_neigh *)dn_db->router;
		dn_dn2eth(msg->neighbor, dn->addr);
	}

	msg->timer   = cpu_to_le16((unsigned short)dn_db->parms.t3);
	msg->mpd     = 0x00;
	msg->datalen = 0x02;
	memset(msg->data, 0xAA, 2);

	pktlen = (__le16 *)skb_push(skb,2);
	*pktlen = cpu_to_le16(skb->len - 2);

	skb_reset_network_header(skb);

	dn_rt_finish_output(skb, dn_rt_all_rt_mcast, msg->id);
}


#define DRDELAY (5 * HZ)

static int dn_am_i_a_router(struct dn_neigh *dn, struct dn_dev *dn_db, struct dn_ifaddr *ifa)
{
	/* First check time since device went up */
	if (time_before(jiffies, dn_db->uptime + DRDELAY))
		return 0;

	/* If there is no router, then yes... */
	if (!dn_db->router)
		return 1;

	/* otherwise only if we have a higher priority or.. */
	if (dn->priority < dn_db->parms.priority)
		return 1;

	/* if we have equal priority and a higher node number */
	if (dn->priority != dn_db->parms.priority)
		return 0;

	if (le16_to_cpu(dn->addr) < le16_to_cpu(ifa->ifa_local))
		return 1;

	return 0;
}

static void dn_send_router_hello(struct net_device *dev, struct dn_ifaddr *ifa)
{
	int n;
	struct dn_dev *dn_db = rcu_dereference_raw(dev->dn_ptr);
	struct dn_neigh *dn = (struct dn_neigh *)dn_db->router;
	struct sk_buff *skb;
	size_t size;
	unsigned char *ptr;
	unsigned char *i1, *i2;
	__le16 *pktlen;
	char *src;

	if (mtu2blksize(dev) < (26 + 7))
		return;

	n = mtu2blksize(dev) - 26;
	n /= 7;

	if (n > 32)
		n = 32;

	size = 2 + 26 + 7 * n;

	if ((skb = dn_alloc_skb(NULL, size, GFP_ATOMIC)) == NULL)
		return;

	skb->dev = dev;
	ptr = skb_put(skb, size);

	*ptr++ = DN_RT_PKT_CNTL | DN_RT_PKT_ERTH;
	*ptr++ = 2; /* ECO */
	*ptr++ = 0;
	*ptr++ = 0;
	dn_dn2eth(ptr, ifa->ifa_local);
	src = ptr;
	ptr += ETH_ALEN;
	*ptr++ = dn_db->parms.forwarding == 1 ?
			DN_RT_INFO_L1RT : DN_RT_INFO_L2RT;
	*((__le16 *)ptr) = cpu_to_le16(mtu2blksize(dev));
	ptr += 2;
	*ptr++ = dn_db->parms.priority; /* Priority */
	*ptr++ = 0; /* Area: Reserved */
	*((__le16 *)ptr) = cpu_to_le16((unsigned short)dn_db->parms.t3);
	ptr += 2;
	*ptr++ = 0; /* MPD: Reserved */
	i1 = ptr++;
	memset(ptr, 0, 7); /* Name: Reserved */
	ptr += 7;
	i2 = ptr++;

	n = dn_neigh_elist(dev, ptr, n);

	*i2 = 7 * n;
	*i1 = 8 + *i2;

	skb_trim(skb, (27 + *i2));

	pktlen = (__le16 *)skb_push(skb, 2);
	*pktlen = cpu_to_le16(skb->len - 2);

	skb_reset_network_header(skb);

	if (dn_am_i_a_router(dn, dn_db, ifa)) {
		struct sk_buff *skb2 = skb_copy(skb, GFP_ATOMIC);
		if (skb2) {
			dn_rt_finish_output(skb2, dn_rt_all_end_mcast, src);
		}
	}

	dn_rt_finish_output(skb, dn_rt_all_rt_mcast, src);
}

static void dn_send_brd_hello(struct net_device *dev, struct dn_ifaddr *ifa)
{
	struct dn_dev *dn_db = rcu_dereference_raw(dev->dn_ptr);

	if (dn_db->parms.forwarding == 0)
		dn_send_endnode_hello(dev, ifa);
	else
		dn_send_router_hello(dev, ifa);
}

static void dn_send_ptp_hello(struct net_device *dev, struct dn_ifaddr *ifa)
{
	int tdlen = 16;
	int size = dev->hard_header_len + 2 + 4 + tdlen;
	struct sk_buff *skb = dn_alloc_skb(NULL, size, GFP_ATOMIC);
	int i;
	unsigned char *ptr;
	char src[ETH_ALEN];

	if (skb == NULL)
		return ;

	skb->dev = dev;
	skb_push(skb, dev->hard_header_len);
	ptr = skb_put(skb, 2 + 4 + tdlen);

	*ptr++ = DN_RT_PKT_HELO;
	*((__le16 *)ptr) = ifa->ifa_local;
	ptr += 2;
	*ptr++ = tdlen;

	for(i = 0; i < tdlen; i++)
		*ptr++ = 0252;

	dn_dn2eth(src, ifa->ifa_local);
	dn_rt_finish_output(skb, dn_rt_all_rt_mcast, src);
}

static int dn_eth_up(struct net_device *dev)
{
	struct dn_dev *dn_db = rcu_dereference_raw(dev->dn_ptr);

	if (dn_db->parms.forwarding == 0)
		dev_mc_add(dev, dn_rt_all_end_mcast);
	else
		dev_mc_add(dev, dn_rt_all_rt_mcast);

	dn_db->use_long = 1;

	return 0;
}

static void dn_eth_down(struct net_device *dev)
{
	struct dn_dev *dn_db = rcu_dereference_raw(dev->dn_ptr);

	if (dn_db->parms.forwarding == 0)
		dev_mc_del(dev, dn_rt_all_end_mcast);
	else
		dev_mc_del(dev, dn_rt_all_rt_mcast);
}

static void dn_dev_set_timer(struct net_device *dev);

static void dn_dev_timer_func(unsigned long arg)
{
	struct net_device *dev = (struct net_device *)arg;
	struct dn_dev *dn_db;
	struct dn_ifaddr *ifa;

	rcu_read_lock();
	dn_db = rcu_dereference(dev->dn_ptr);
	if (dn_db->t3 <= dn_db->parms.t2) {
		if (dn_db->parms.timer3) {
			for (ifa = rcu_dereference(dn_db->ifa_list);
			     ifa;
			     ifa = rcu_dereference(ifa->ifa_next)) {
				if (!(ifa->ifa_flags & IFA_F_SECONDARY))
					dn_db->parms.timer3(dev, ifa);
			}
		}
		dn_db->t3 = dn_db->parms.t3;
	} else {
		dn_db->t3 -= dn_db->parms.t2;
	}
	rcu_read_unlock();
	dn_dev_set_timer(dev);
}

static void dn_dev_set_timer(struct net_device *dev)
{
	struct dn_dev *dn_db = rcu_dereference_raw(dev->dn_ptr);

	if (dn_db->parms.t2 > dn_db->parms.t3)
		dn_db->parms.t2 = dn_db->parms.t3;

	dn_db->timer.data = (unsigned long)dev;
	dn_db->timer.function = dn_dev_timer_func;
	dn_db->timer.expires = jiffies + (dn_db->parms.t2 * HZ);

	add_timer(&dn_db->timer);
}

static struct dn_dev *dn_dev_create(struct net_device *dev, int *err)
{
	int i;
	struct dn_dev_parms *p = dn_dev_list;
	struct dn_dev *dn_db;

	for(i = 0; i < DN_DEV_LIST_SIZE; i++, p++) {
		if (p->type == dev->type)
			break;
	}

	*err = -ENODEV;
	if (i == DN_DEV_LIST_SIZE)
		return NULL;

	*err = -ENOBUFS;
	if ((dn_db = kzalloc(sizeof(struct dn_dev), GFP_ATOMIC)) == NULL)
		return NULL;

	memcpy(&dn_db->parms, p, sizeof(struct dn_dev_parms));

	rcu_assign_pointer(dev->dn_ptr, dn_db);
	dn_db->dev = dev;
	init_timer(&dn_db->timer);

	dn_db->uptime = jiffies;

	dn_db->neigh_parms = neigh_parms_alloc(dev, &dn_neigh_table);
	if (!dn_db->neigh_parms) {
		RCU_INIT_POINTER(dev->dn_ptr, NULL);
		kfree(dn_db);
		return NULL;
	}

	if (dn_db->parms.up) {
		if (dn_db->parms.up(dev) < 0) {
			neigh_parms_release(&dn_neigh_table, dn_db->neigh_parms);
			dev->dn_ptr = NULL;
			kfree(dn_db);
			return NULL;
		}
	}

	dn_dev_sysctl_register(dev, &dn_db->parms);

	dn_dev_set_timer(dev);

	*err = 0;
	return dn_db;
}


/*
 * This processes a device up event. We only start up
 * the loopback device & ethernet devices with correct
 * MAC addresses automatically. Others must be started
 * specifically.
 *
 * FIXME: How should we configure the loopback address ? If we could dispense
 * with using decnet_address here and for autobind, it will be one less thing
 * for users to worry about setting up.
 */

void dn_dev_up(struct net_device *dev)
{
	struct dn_ifaddr *ifa;
	__le16 addr = decnet_address;
	int maybe_default = 0;
	struct dn_dev *dn_db = rtnl_dereference(dev->dn_ptr);

	if ((dev->type != ARPHRD_ETHER) && (dev->type != ARPHRD_LOOPBACK))
		return;

	/*
	 * Need to ensure that loopback device has a dn_db attached to it
	 * to allow creation of neighbours against it, even though it might
	 * not have a local address of its own. Might as well do the same for
	 * all autoconfigured interfaces.
	 */
	if (dn_db == NULL) {
		int err;
		dn_db = dn_dev_create(dev, &err);
		if (dn_db == NULL)
			return;
	}

	if (dev->type == ARPHRD_ETHER) {
		if (memcmp(dev->dev_addr, dn_hiord, 4) != 0)
			return;
		addr = dn_eth2dn(dev->dev_addr);
		maybe_default = 1;
	}

	if (addr == 0)
		return;

	if ((ifa = dn_dev_alloc_ifa()) == NULL)
		return;

	ifa->ifa_local = ifa->ifa_address = addr;
	ifa->ifa_flags = 0;
	ifa->ifa_scope = RT_SCOPE_UNIVERSE;
	strcpy(ifa->ifa_label, dev->name);

	dn_dev_set_ifa(dev, ifa);

	/*
	 * Automagically set the default device to the first automatically
	 * configured ethernet card in the system.
	 */
	if (maybe_default) {
		dev_hold(dev);
		if (dn_dev_set_default(dev, 0))
			dev_put(dev);
	}
}

static void dn_dev_delete(struct net_device *dev)
{
	struct dn_dev *dn_db = rtnl_dereference(dev->dn_ptr);

	if (dn_db == NULL)
		return;

	del_timer_sync(&dn_db->timer);
	dn_dev_sysctl_unregister(&dn_db->parms);
	dn_dev_check_default(dev);
	neigh_ifdown(&dn_neigh_table, dev);

	if (dn_db->parms.down)
		dn_db->parms.down(dev);

	dev->dn_ptr = NULL;

	neigh_parms_release(&dn_neigh_table, dn_db->neigh_parms);
	neigh_ifdown(&dn_neigh_table, dev);

	if (dn_db->router)
		neigh_release(dn_db->router);
	if (dn_db->peer)
		neigh_release(dn_db->peer);

	kfree(dn_db);
}

void dn_dev_down(struct net_device *dev)
{
	struct dn_dev *dn_db = rtnl_dereference(dev->dn_ptr);
	struct dn_ifaddr *ifa;

	if (dn_db == NULL)
		return;

	while ((ifa = rtnl_dereference(dn_db->ifa_list)) != NULL) {
		dn_dev_del_ifa(dn_db, &dn_db->ifa_list, 0);
		dn_dev_free_ifa(ifa);
	}

	dn_dev_delete(dev);
}

void dn_dev_init_pkt(struct sk_buff *skb)
{
}

void dn_dev_veri_pkt(struct sk_buff *skb)
{
}

void dn_dev_hello(struct sk_buff *skb)
{
}

void dn_dev_devices_off(void)
{
	struct net_device *dev;

	rtnl_lock();
	for_each_netdev(&init_net, dev)
		dn_dev_down(dev);
	rtnl_unlock();

}

void dn_dev_devices_on(void)
{
	struct net_device *dev;

	rtnl_lock();
	for_each_netdev(&init_net, dev) {
		if (dev->flags & IFF_UP)
			dn_dev_up(dev);
	}
	rtnl_unlock();
}

int register_dnaddr_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&dnaddr_chain, nb);
}

int unregister_dnaddr_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&dnaddr_chain, nb);
}

#ifdef CONFIG_PROC_FS
static inline int is_dn_dev(struct net_device *dev)
{
	return dev->dn_ptr != NULL;
}

static void *dn_dev_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	int i;
	struct net_device *dev;

	rcu_read_lock();

	if (*pos == 0)
		return SEQ_START_TOKEN;

	i = 1;
	for_each_netdev_rcu(&init_net, dev) {
		if (!is_dn_dev(dev))
			continue;

		if (i++ == *pos)
			return dev;
	}

	return NULL;
}

static void *dn_dev_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct net_device *dev;

	++*pos;

	dev = v;
	if (v == SEQ_START_TOKEN)
		dev = net_device_entry(&init_net.dev_base_head);

	for_each_netdev_continue_rcu(&init_net, dev) {
		if (!is_dn_dev(dev))
			continue;

		return dev;
	}

	return NULL;
}

static void dn_dev_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static char *dn_type2asc(char type)
{
	switch (type) {
	case DN_DEV_BCAST:
		return "B";
	case DN_DEV_UCAST:
		return "U";
	case DN_DEV_MPOINT:
		return "M";
	}

	return "?";
}

static int dn_dev_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "Name     Flags T1   Timer1 T3   Timer3 BlkSize Pri State DevType    Router Peer\n");
	else {
		struct net_device *dev = v;
		char peer_buf[DN_ASCBUF_LEN];
		char router_buf[DN_ASCBUF_LEN];
		struct dn_dev *dn_db = rcu_dereference(dev->dn_ptr);

		seq_printf(seq, "%-8s %1s     %04u %04u   %04lu %04lu"
				"   %04hu    %03d %02x    %-10s %-7s %-7s\n",
				dev->name ? dev->name : "???",
				dn_type2asc(dn_db->parms.mode),
				0, 0,
				dn_db->t3, dn_db->parms.t3,
				mtu2blksize(dev),
				dn_db->parms.priority,
				dn_db->parms.state, dn_db->parms.name,
				dn_db->router ? dn_addr2asc(le16_to_cpu(*(__le16 *)dn_db->router->primary_key), router_buf) : "",
				dn_db->peer ? dn_addr2asc(le16_to_cpu(*(__le16 *)dn_db->peer->primary_key), peer_buf) : "");
	}
	return 0;
}

static const struct seq_operations dn_dev_seq_ops = {
	.start	= dn_dev_seq_start,
	.next	= dn_dev_seq_next,
	.stop	= dn_dev_seq_stop,
	.show	= dn_dev_seq_show,
};

static int dn_dev_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &dn_dev_seq_ops);
}

static const struct file_operations dn_dev_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = dn_dev_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release,
};

#endif /* CONFIG_PROC_FS */

static int addr[2];
module_param_array(addr, int, NULL, 0444);
MODULE_PARM_DESC(addr, "The DECnet address of this machine: area,node");

void __init dn_dev_init(void)
{
	if (addr[0] > 63 || addr[0] < 0) {
		printk(KERN_ERR "DECnet: Area must be between 0 and 63");
		return;
	}

	if (addr[1] > 1023 || addr[1] < 0) {
		printk(KERN_ERR "DECnet: Node must be between 0 and 1023");
		return;
	}

	decnet_address = cpu_to_le16((addr[0] << 10) | addr[1]);

	dn_dev_devices_on();

	rtnl_register(PF_DECnet, RTM_NEWADDR, dn_nl_newaddr, NULL, NULL);
	rtnl_register(PF_DECnet, RTM_DELADDR, dn_nl_deladdr, NULL, NULL);
	rtnl_register(PF_DECnet, RTM_GETADDR, NULL, dn_nl_dump_ifaddr, NULL);

	proc_create("decnet_dev", S_IRUGO, init_net.proc_net, &dn_dev_seq_fops);

#ifdef CONFIG_SYSCTL
	{
		int i;
		for(i = 0; i < DN_DEV_LIST_SIZE; i++)
			dn_dev_sysctl_register(NULL, &dn_dev_list[i]);
	}
#endif /* CONFIG_SYSCTL */
}

void __exit dn_dev_cleanup(void)
{
#ifdef CONFIG_SYSCTL
	{
		int i;
		for(i = 0; i < DN_DEV_LIST_SIZE; i++)
			dn_dev_sysctl_unregister(&dn_dev_list[i]);
	}
#endif /* CONFIG_SYSCTL */

	remove_proc_entry("decnet_dev", init_net.proc_net);

	dn_dev_devices_off();
}
