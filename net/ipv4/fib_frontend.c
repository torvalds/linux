/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base: FIB frontend.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/bitops.h>
#include <linux/capability.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/if_addr.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/ip_fib.h>
#include <net/rtnetlink.h>
#include <net/xfrm.h>

#ifndef CONFIG_IP_MULTIPLE_TABLES

static int __net_init fib4_rules_init(struct net *net)
{
	struct fib_table *local_table, *main_table;

	local_table = fib_trie_table(RT_TABLE_LOCAL);
	if (local_table == NULL)
		return -ENOMEM;

	main_table  = fib_trie_table(RT_TABLE_MAIN);
	if (main_table == NULL)
		goto fail;

	hlist_add_head_rcu(&local_table->tb_hlist,
				&net->ipv4.fib_table_hash[TABLE_LOCAL_INDEX]);
	hlist_add_head_rcu(&main_table->tb_hlist,
				&net->ipv4.fib_table_hash[TABLE_MAIN_INDEX]);
	return 0;

fail:
	kfree(local_table);
	return -ENOMEM;
}
#else

struct fib_table *fib_new_table(struct net *net, u32 id)
{
	struct fib_table *tb;
	unsigned int h;

	if (id == 0)
		id = RT_TABLE_MAIN;
	tb = fib_get_table(net, id);
	if (tb)
		return tb;

	tb = fib_trie_table(id);
	if (!tb)
		return NULL;

	switch (id) {
	case RT_TABLE_LOCAL:
		net->ipv4.fib_local = tb;
		break;

	case RT_TABLE_MAIN:
		net->ipv4.fib_main = tb;
		break;

	case RT_TABLE_DEFAULT:
		net->ipv4.fib_default = tb;
		break;

	default:
		break;
	}

	h = id & (FIB_TABLE_HASHSZ - 1);
	hlist_add_head_rcu(&tb->tb_hlist, &net->ipv4.fib_table_hash[h]);
	return tb;
}

struct fib_table *fib_get_table(struct net *net, u32 id)
{
	struct fib_table *tb;
	struct hlist_node *node;
	struct hlist_head *head;
	unsigned int h;

	if (id == 0)
		id = RT_TABLE_MAIN;
	h = id & (FIB_TABLE_HASHSZ - 1);

	rcu_read_lock();
	head = &net->ipv4.fib_table_hash[h];
	hlist_for_each_entry_rcu(tb, node, head, tb_hlist) {
		if (tb->tb_id == id) {
			rcu_read_unlock();
			return tb;
		}
	}
	rcu_read_unlock();
	return NULL;
}
#endif /* CONFIG_IP_MULTIPLE_TABLES */

static void fib_flush(struct net *net)
{
	int flushed = 0;
	struct fib_table *tb;
	struct hlist_node *node;
	struct hlist_head *head;
	unsigned int h;

	for (h = 0; h < FIB_TABLE_HASHSZ; h++) {
		head = &net->ipv4.fib_table_hash[h];
		hlist_for_each_entry(tb, node, head, tb_hlist)
			flushed += fib_table_flush(tb);
	}

	if (flushed)
		rt_cache_flush(net, -1);
}

/*
 * Find address type as if only "dev" was present in the system. If
 * on_dev is NULL then all interfaces are taken into consideration.
 */
static inline unsigned int __inet_dev_addr_type(struct net *net,
						const struct net_device *dev,
						__be32 addr)
{
	struct flowi4		fl4 = { .daddr = addr };
	struct fib_result	res;
	unsigned int ret = RTN_BROADCAST;
	struct fib_table *local_table;

	if (ipv4_is_zeronet(addr) || ipv4_is_lbcast(addr))
		return RTN_BROADCAST;
	if (ipv4_is_multicast(addr))
		return RTN_MULTICAST;

	local_table = fib_get_table(net, RT_TABLE_LOCAL);
	if (local_table) {
		ret = RTN_UNICAST;
		rcu_read_lock();
		if (!fib_table_lookup(local_table, &fl4, &res, FIB_LOOKUP_NOREF)) {
			if (!dev || dev == res.fi->fib_dev)
				ret = res.type;
		}
		rcu_read_unlock();
	}
	return ret;
}

unsigned int inet_addr_type(struct net *net, __be32 addr)
{
	return __inet_dev_addr_type(net, NULL, addr);
}
EXPORT_SYMBOL(inet_addr_type);

unsigned int inet_dev_addr_type(struct net *net, const struct net_device *dev,
				__be32 addr)
{
	return __inet_dev_addr_type(net, dev, addr);
}
EXPORT_SYMBOL(inet_dev_addr_type);

__be32 fib_compute_spec_dst(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct in_device *in_dev;
	struct fib_result res;
	struct rtable *rt;
	struct flowi4 fl4;
	struct net *net;
	int scope;

	rt = skb_rtable(skb);
	if ((rt->rt_flags & (RTCF_BROADCAST | RTCF_MULTICAST | RTCF_LOCAL)) ==
	    RTCF_LOCAL)
		return ip_hdr(skb)->daddr;

	in_dev = __in_dev_get_rcu(dev);
	BUG_ON(!in_dev);

	net = dev_net(dev);

	scope = RT_SCOPE_UNIVERSE;
	if (!ipv4_is_zeronet(ip_hdr(skb)->saddr)) {
		fl4.flowi4_oif = 0;
		fl4.flowi4_iif = net->loopback_dev->ifindex;
		fl4.daddr = ip_hdr(skb)->saddr;
		fl4.saddr = 0;
		fl4.flowi4_tos = RT_TOS(ip_hdr(skb)->tos);
		fl4.flowi4_scope = scope;
		fl4.flowi4_mark = IN_DEV_SRC_VMARK(in_dev) ? skb->mark : 0;
		if (!fib_lookup(net, &fl4, &res))
			return FIB_RES_PREFSRC(net, res);
	} else {
		scope = RT_SCOPE_LINK;
	}

	return inet_select_addr(dev, ip_hdr(skb)->saddr, scope);
}

/* Given (packet source, input interface) and optional (dst, oif, tos):
 * - (main) check, that source is valid i.e. not broadcast or our local
 *   address.
 * - figure out what "logical" interface this packet arrived
 *   and calculate "specific destination" address.
 * - check, that packet arrived from expected physical interface.
 * called with rcu_read_lock()
 */
static int __fib_validate_source(struct sk_buff *skb, __be32 src, __be32 dst,
				 u8 tos, int oif, struct net_device *dev,
				 int rpf, struct in_device *idev, u32 *itag)
{
	int ret, no_addr, accept_local;
	struct fib_result res;
	struct flowi4 fl4;
	struct net *net;
	bool dev_match;

	fl4.flowi4_oif = 0;
	fl4.flowi4_iif = oif;
	fl4.daddr = src;
	fl4.saddr = dst;
	fl4.flowi4_tos = tos;
	fl4.flowi4_scope = RT_SCOPE_UNIVERSE;

	no_addr = idev->ifa_list == NULL;

	accept_local = IN_DEV_ACCEPT_LOCAL(idev);
	fl4.flowi4_mark = IN_DEV_SRC_VMARK(idev) ? skb->mark : 0;

	net = dev_net(dev);
	if (fib_lookup(net, &fl4, &res))
		goto last_resort;
	if (res.type != RTN_UNICAST) {
		if (res.type != RTN_LOCAL || !accept_local)
			goto e_inval;
	}
	fib_combine_itag(itag, &res);
	dev_match = false;

#ifdef CONFIG_IP_ROUTE_MULTIPATH
	for (ret = 0; ret < res.fi->fib_nhs; ret++) {
		struct fib_nh *nh = &res.fi->fib_nh[ret];

		if (nh->nh_dev == dev) {
			dev_match = true;
			break;
		}
	}
#else
	if (FIB_RES_DEV(res) == dev)
		dev_match = true;
#endif
	if (dev_match) {
		ret = FIB_RES_NH(res).nh_scope >= RT_SCOPE_HOST;
		return ret;
	}
	if (no_addr)
		goto last_resort;
	if (rpf == 1)
		goto e_rpf;
	fl4.flowi4_oif = dev->ifindex;

	ret = 0;
	if (fib_lookup(net, &fl4, &res) == 0) {
		if (res.type == RTN_UNICAST)
			ret = FIB_RES_NH(res).nh_scope >= RT_SCOPE_HOST;
	}
	return ret;

last_resort:
	if (rpf)
		goto e_rpf;
	*itag = 0;
	return 0;

e_inval:
	return -EINVAL;
e_rpf:
	return -EXDEV;
}

/* Ignore rp_filter for packets protected by IPsec. */
int fib_validate_source(struct sk_buff *skb, __be32 src, __be32 dst,
			u8 tos, int oif, struct net_device *dev,
			struct in_device *idev, u32 *itag)
{
	int r = secpath_exists(skb) ? 0 : IN_DEV_RPFILTER(idev);

	if (!r && !fib_num_tclassid_users(dev_net(dev))) {
		*itag = 0;
		return 0;
	}
	return __fib_validate_source(skb, src, dst, tos, oif, dev, r, idev, itag);
}

static inline __be32 sk_extract_addr(struct sockaddr *addr)
{
	return ((struct sockaddr_in *) addr)->sin_addr.s_addr;
}

static int put_rtax(struct nlattr *mx, int len, int type, u32 value)
{
	struct nlattr *nla;

	nla = (struct nlattr *) ((char *) mx + len);
	nla->nla_type = type;
	nla->nla_len = nla_attr_size(4);
	*(u32 *) nla_data(nla) = value;

	return len + nla_total_size(4);
}

static int rtentry_to_fib_config(struct net *net, int cmd, struct rtentry *rt,
				 struct fib_config *cfg)
{
	__be32 addr;
	int plen;

	memset(cfg, 0, sizeof(*cfg));
	cfg->fc_nlinfo.nl_net = net;

	if (rt->rt_dst.sa_family != AF_INET)
		return -EAFNOSUPPORT;

	/*
	 * Check mask for validity:
	 * a) it must be contiguous.
	 * b) destination must have all host bits clear.
	 * c) if application forgot to set correct family (AF_INET),
	 *    reject request unless it is absolutely clear i.e.
	 *    both family and mask are zero.
	 */
	plen = 32;
	addr = sk_extract_addr(&rt->rt_dst);
	if (!(rt->rt_flags & RTF_HOST)) {
		__be32 mask = sk_extract_addr(&rt->rt_genmask);

		if (rt->rt_genmask.sa_family != AF_INET) {
			if (mask || rt->rt_genmask.sa_family)
				return -EAFNOSUPPORT;
		}

		if (bad_mask(mask, addr))
			return -EINVAL;

		plen = inet_mask_len(mask);
	}

	cfg->fc_dst_len = plen;
	cfg->fc_dst = addr;

	if (cmd != SIOCDELRT) {
		cfg->fc_nlflags = NLM_F_CREATE;
		cfg->fc_protocol = RTPROT_BOOT;
	}

	if (rt->rt_metric)
		cfg->fc_priority = rt->rt_metric - 1;

	if (rt->rt_flags & RTF_REJECT) {
		cfg->fc_scope = RT_SCOPE_HOST;
		cfg->fc_type = RTN_UNREACHABLE;
		return 0;
	}

	cfg->fc_scope = RT_SCOPE_NOWHERE;
	cfg->fc_type = RTN_UNICAST;

	if (rt->rt_dev) {
		char *colon;
		struct net_device *dev;
		char devname[IFNAMSIZ];

		if (copy_from_user(devname, rt->rt_dev, IFNAMSIZ-1))
			return -EFAULT;

		devname[IFNAMSIZ-1] = 0;
		colon = strchr(devname, ':');
		if (colon)
			*colon = 0;
		dev = __dev_get_by_name(net, devname);
		if (!dev)
			return -ENODEV;
		cfg->fc_oif = dev->ifindex;
		if (colon) {
			struct in_ifaddr *ifa;
			struct in_device *in_dev = __in_dev_get_rtnl(dev);
			if (!in_dev)
				return -ENODEV;
			*colon = ':';
			for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next)
				if (strcmp(ifa->ifa_label, devname) == 0)
					break;
			if (ifa == NULL)
				return -ENODEV;
			cfg->fc_prefsrc = ifa->ifa_local;
		}
	}

	addr = sk_extract_addr(&rt->rt_gateway);
	if (rt->rt_gateway.sa_family == AF_INET && addr) {
		cfg->fc_gw = addr;
		if (rt->rt_flags & RTF_GATEWAY &&
		    inet_addr_type(net, addr) == RTN_UNICAST)
			cfg->fc_scope = RT_SCOPE_UNIVERSE;
	}

	if (cmd == SIOCDELRT)
		return 0;

	if (rt->rt_flags & RTF_GATEWAY && !cfg->fc_gw)
		return -EINVAL;

	if (cfg->fc_scope == RT_SCOPE_NOWHERE)
		cfg->fc_scope = RT_SCOPE_LINK;

	if (rt->rt_flags & (RTF_MTU | RTF_WINDOW | RTF_IRTT)) {
		struct nlattr *mx;
		int len = 0;

		mx = kzalloc(3 * nla_total_size(4), GFP_KERNEL);
		if (mx == NULL)
			return -ENOMEM;

		if (rt->rt_flags & RTF_MTU)
			len = put_rtax(mx, len, RTAX_ADVMSS, rt->rt_mtu - 40);

		if (rt->rt_flags & RTF_WINDOW)
			len = put_rtax(mx, len, RTAX_WINDOW, rt->rt_window);

		if (rt->rt_flags & RTF_IRTT)
			len = put_rtax(mx, len, RTAX_RTT, rt->rt_irtt << 3);

		cfg->fc_mx = mx;
		cfg->fc_mx_len = len;
	}

	return 0;
}

/*
 * Handle IP routing ioctl calls.
 * These are used to manipulate the routing tables
 */
int ip_rt_ioctl(struct net *net, unsigned int cmd, void __user *arg)
{
	struct fib_config cfg;
	struct rtentry rt;
	int err;

	switch (cmd) {
	case SIOCADDRT:		/* Add a route */
	case SIOCDELRT:		/* Delete a route */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(&rt, arg, sizeof(rt)))
			return -EFAULT;

		rtnl_lock();
		err = rtentry_to_fib_config(net, cmd, &rt, &cfg);
		if (err == 0) {
			struct fib_table *tb;

			if (cmd == SIOCDELRT) {
				tb = fib_get_table(net, cfg.fc_table);
				if (tb)
					err = fib_table_delete(tb, &cfg);
				else
					err = -ESRCH;
			} else {
				tb = fib_new_table(net, cfg.fc_table);
				if (tb)
					err = fib_table_insert(tb, &cfg);
				else
					err = -ENOBUFS;
			}

			/* allocated by rtentry_to_fib_config() */
			kfree(cfg.fc_mx);
		}
		rtnl_unlock();
		return err;
	}
	return -EINVAL;
}

const struct nla_policy rtm_ipv4_policy[RTA_MAX + 1] = {
	[RTA_DST]		= { .type = NLA_U32 },
	[RTA_SRC]		= { .type = NLA_U32 },
	[RTA_IIF]		= { .type = NLA_U32 },
	[RTA_OIF]		= { .type = NLA_U32 },
	[RTA_GATEWAY]		= { .type = NLA_U32 },
	[RTA_PRIORITY]		= { .type = NLA_U32 },
	[RTA_PREFSRC]		= { .type = NLA_U32 },
	[RTA_METRICS]		= { .type = NLA_NESTED },
	[RTA_MULTIPATH]		= { .len = sizeof(struct rtnexthop) },
	[RTA_FLOW]		= { .type = NLA_U32 },
};

static int rtm_to_fib_config(struct net *net, struct sk_buff *skb,
			     struct nlmsghdr *nlh, struct fib_config *cfg)
{
	struct nlattr *attr;
	int err, remaining;
	struct rtmsg *rtm;

	err = nlmsg_validate(nlh, sizeof(*rtm), RTA_MAX, rtm_ipv4_policy);
	if (err < 0)
		goto errout;

	memset(cfg, 0, sizeof(*cfg));

	rtm = nlmsg_data(nlh);
	cfg->fc_dst_len = rtm->rtm_dst_len;
	cfg->fc_tos = rtm->rtm_tos;
	cfg->fc_table = rtm->rtm_table;
	cfg->fc_protocol = rtm->rtm_protocol;
	cfg->fc_scope = rtm->rtm_scope;
	cfg->fc_type = rtm->rtm_type;
	cfg->fc_flags = rtm->rtm_flags;
	cfg->fc_nlflags = nlh->nlmsg_flags;

	cfg->fc_nlinfo.pid = NETLINK_CB(skb).pid;
	cfg->fc_nlinfo.nlh = nlh;
	cfg->fc_nlinfo.nl_net = net;

	if (cfg->fc_type > RTN_MAX) {
		err = -EINVAL;
		goto errout;
	}

	nlmsg_for_each_attr(attr, nlh, sizeof(struct rtmsg), remaining) {
		switch (nla_type(attr)) {
		case RTA_DST:
			cfg->fc_dst = nla_get_be32(attr);
			break;
		case RTA_OIF:
			cfg->fc_oif = nla_get_u32(attr);
			break;
		case RTA_GATEWAY:
			cfg->fc_gw = nla_get_be32(attr);
			break;
		case RTA_PRIORITY:
			cfg->fc_priority = nla_get_u32(attr);
			break;
		case RTA_PREFSRC:
			cfg->fc_prefsrc = nla_get_be32(attr);
			break;
		case RTA_METRICS:
			cfg->fc_mx = nla_data(attr);
			cfg->fc_mx_len = nla_len(attr);
			break;
		case RTA_MULTIPATH:
			cfg->fc_mp = nla_data(attr);
			cfg->fc_mp_len = nla_len(attr);
			break;
		case RTA_FLOW:
			cfg->fc_flow = nla_get_u32(attr);
			break;
		case RTA_TABLE:
			cfg->fc_table = nla_get_u32(attr);
			break;
		}
	}

	return 0;
errout:
	return err;
}

static int inet_rtm_delroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct fib_config cfg;
	struct fib_table *tb;
	int err;

	err = rtm_to_fib_config(net, skb, nlh, &cfg);
	if (err < 0)
		goto errout;

	tb = fib_get_table(net, cfg.fc_table);
	if (tb == NULL) {
		err = -ESRCH;
		goto errout;
	}

	err = fib_table_delete(tb, &cfg);
errout:
	return err;
}

static int inet_rtm_newroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct fib_config cfg;
	struct fib_table *tb;
	int err;

	err = rtm_to_fib_config(net, skb, nlh, &cfg);
	if (err < 0)
		goto errout;

	tb = fib_new_table(net, cfg.fc_table);
	if (tb == NULL) {
		err = -ENOBUFS;
		goto errout;
	}

	err = fib_table_insert(tb, &cfg);
errout:
	return err;
}

static int inet_dump_fib(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	unsigned int h, s_h;
	unsigned int e = 0, s_e;
	struct fib_table *tb;
	struct hlist_node *node;
	struct hlist_head *head;
	int dumped = 0;

	if (nlmsg_len(cb->nlh) >= sizeof(struct rtmsg) &&
	    ((struct rtmsg *) nlmsg_data(cb->nlh))->rtm_flags & RTM_F_CLONED)
		return ip_rt_dump(skb, cb);

	s_h = cb->args[0];
	s_e = cb->args[1];

	for (h = s_h; h < FIB_TABLE_HASHSZ; h++, s_e = 0) {
		e = 0;
		head = &net->ipv4.fib_table_hash[h];
		hlist_for_each_entry(tb, node, head, tb_hlist) {
			if (e < s_e)
				goto next;
			if (dumped)
				memset(&cb->args[2], 0, sizeof(cb->args) -
						 2 * sizeof(cb->args[0]));
			if (fib_table_dump(tb, skb, cb) < 0)
				goto out;
			dumped = 1;
next:
			e++;
		}
	}
out:
	cb->args[1] = e;
	cb->args[0] = h;

	return skb->len;
}

/* Prepare and feed intra-kernel routing request.
 * Really, it should be netlink message, but :-( netlink
 * can be not configured, so that we feed it directly
 * to fib engine. It is legal, because all events occur
 * only when netlink is already locked.
 */
static void fib_magic(int cmd, int type, __be32 dst, int dst_len, struct in_ifaddr *ifa)
{
	struct net *net = dev_net(ifa->ifa_dev->dev);
	struct fib_table *tb;
	struct fib_config cfg = {
		.fc_protocol = RTPROT_KERNEL,
		.fc_type = type,
		.fc_dst = dst,
		.fc_dst_len = dst_len,
		.fc_prefsrc = ifa->ifa_local,
		.fc_oif = ifa->ifa_dev->dev->ifindex,
		.fc_nlflags = NLM_F_CREATE | NLM_F_APPEND,
		.fc_nlinfo = {
			.nl_net = net,
		},
	};

	if (type == RTN_UNICAST)
		tb = fib_new_table(net, RT_TABLE_MAIN);
	else
		tb = fib_new_table(net, RT_TABLE_LOCAL);

	if (tb == NULL)
		return;

	cfg.fc_table = tb->tb_id;

	if (type != RTN_LOCAL)
		cfg.fc_scope = RT_SCOPE_LINK;
	else
		cfg.fc_scope = RT_SCOPE_HOST;

	if (cmd == RTM_NEWROUTE)
		fib_table_insert(tb, &cfg);
	else
		fib_table_delete(tb, &cfg);
}

void fib_add_ifaddr(struct in_ifaddr *ifa)
{
	struct in_device *in_dev = ifa->ifa_dev;
	struct net_device *dev = in_dev->dev;
	struct in_ifaddr *prim = ifa;
	__be32 mask = ifa->ifa_mask;
	__be32 addr = ifa->ifa_local;
	__be32 prefix = ifa->ifa_address & mask;

	if (ifa->ifa_flags & IFA_F_SECONDARY) {
		prim = inet_ifa_byprefix(in_dev, prefix, mask);
		if (prim == NULL) {
			pr_warn("%s: bug: prim == NULL\n", __func__);
			return;
		}
	}

	fib_magic(RTM_NEWROUTE, RTN_LOCAL, addr, 32, prim);

	if (!(dev->flags & IFF_UP))
		return;

	/* Add broadcast address, if it is explicitly assigned. */
	if (ifa->ifa_broadcast && ifa->ifa_broadcast != htonl(0xFFFFFFFF))
		fib_magic(RTM_NEWROUTE, RTN_BROADCAST, ifa->ifa_broadcast, 32, prim);

	if (!ipv4_is_zeronet(prefix) && !(ifa->ifa_flags & IFA_F_SECONDARY) &&
	    (prefix != addr || ifa->ifa_prefixlen < 32)) {
		fib_magic(RTM_NEWROUTE,
			  dev->flags & IFF_LOOPBACK ? RTN_LOCAL : RTN_UNICAST,
			  prefix, ifa->ifa_prefixlen, prim);

		/* Add network specific broadcasts, when it takes a sense */
		if (ifa->ifa_prefixlen < 31) {
			fib_magic(RTM_NEWROUTE, RTN_BROADCAST, prefix, 32, prim);
			fib_magic(RTM_NEWROUTE, RTN_BROADCAST, prefix | ~mask,
				  32, prim);
		}
	}
}

/* Delete primary or secondary address.
 * Optionally, on secondary address promotion consider the addresses
 * from subnet iprim as deleted, even if they are in device list.
 * In this case the secondary ifa can be in device list.
 */
void fib_del_ifaddr(struct in_ifaddr *ifa, struct in_ifaddr *iprim)
{
	struct in_device *in_dev = ifa->ifa_dev;
	struct net_device *dev = in_dev->dev;
	struct in_ifaddr *ifa1;
	struct in_ifaddr *prim = ifa, *prim1 = NULL;
	__be32 brd = ifa->ifa_address | ~ifa->ifa_mask;
	__be32 any = ifa->ifa_address & ifa->ifa_mask;
#define LOCAL_OK	1
#define BRD_OK		2
#define BRD0_OK		4
#define BRD1_OK		8
	unsigned int ok = 0;
	int subnet = 0;		/* Primary network */
	int gone = 1;		/* Address is missing */
	int same_prefsrc = 0;	/* Another primary with same IP */

	if (ifa->ifa_flags & IFA_F_SECONDARY) {
		prim = inet_ifa_byprefix(in_dev, any, ifa->ifa_mask);
		if (prim == NULL) {
			pr_warn("%s: bug: prim == NULL\n", __func__);
			return;
		}
		if (iprim && iprim != prim) {
			pr_warn("%s: bug: iprim != prim\n", __func__);
			return;
		}
	} else if (!ipv4_is_zeronet(any) &&
		   (any != ifa->ifa_local || ifa->ifa_prefixlen < 32)) {
		fib_magic(RTM_DELROUTE,
			  dev->flags & IFF_LOOPBACK ? RTN_LOCAL : RTN_UNICAST,
			  any, ifa->ifa_prefixlen, prim);
		subnet = 1;
	}

	/* Deletion is more complicated than add.
	 * We should take care of not to delete too much :-)
	 *
	 * Scan address list to be sure that addresses are really gone.
	 */

	for (ifa1 = in_dev->ifa_list; ifa1; ifa1 = ifa1->ifa_next) {
		if (ifa1 == ifa) {
			/* promotion, keep the IP */
			gone = 0;
			continue;
		}
		/* Ignore IFAs from our subnet */
		if (iprim && ifa1->ifa_mask == iprim->ifa_mask &&
		    inet_ifa_match(ifa1->ifa_address, iprim))
			continue;

		/* Ignore ifa1 if it uses different primary IP (prefsrc) */
		if (ifa1->ifa_flags & IFA_F_SECONDARY) {
			/* Another address from our subnet? */
			if (ifa1->ifa_mask == prim->ifa_mask &&
			    inet_ifa_match(ifa1->ifa_address, prim))
				prim1 = prim;
			else {
				/* We reached the secondaries, so
				 * same_prefsrc should be determined.
				 */
				if (!same_prefsrc)
					continue;
				/* Search new prim1 if ifa1 is not
				 * using the current prim1
				 */
				if (!prim1 ||
				    ifa1->ifa_mask != prim1->ifa_mask ||
				    !inet_ifa_match(ifa1->ifa_address, prim1))
					prim1 = inet_ifa_byprefix(in_dev,
							ifa1->ifa_address,
							ifa1->ifa_mask);
				if (!prim1)
					continue;
				if (prim1->ifa_local != prim->ifa_local)
					continue;
			}
		} else {
			if (prim->ifa_local != ifa1->ifa_local)
				continue;
			prim1 = ifa1;
			if (prim != prim1)
				same_prefsrc = 1;
		}
		if (ifa->ifa_local == ifa1->ifa_local)
			ok |= LOCAL_OK;
		if (ifa->ifa_broadcast == ifa1->ifa_broadcast)
			ok |= BRD_OK;
		if (brd == ifa1->ifa_broadcast)
			ok |= BRD1_OK;
		if (any == ifa1->ifa_broadcast)
			ok |= BRD0_OK;
		/* primary has network specific broadcasts */
		if (prim1 == ifa1 && ifa1->ifa_prefixlen < 31) {
			__be32 brd1 = ifa1->ifa_address | ~ifa1->ifa_mask;
			__be32 any1 = ifa1->ifa_address & ifa1->ifa_mask;

			if (!ipv4_is_zeronet(any1)) {
				if (ifa->ifa_broadcast == brd1 ||
				    ifa->ifa_broadcast == any1)
					ok |= BRD_OK;
				if (brd == brd1 || brd == any1)
					ok |= BRD1_OK;
				if (any == brd1 || any == any1)
					ok |= BRD0_OK;
			}
		}
	}

	if (!(ok & BRD_OK))
		fib_magic(RTM_DELROUTE, RTN_BROADCAST, ifa->ifa_broadcast, 32, prim);
	if (subnet && ifa->ifa_prefixlen < 31) {
		if (!(ok & BRD1_OK))
			fib_magic(RTM_DELROUTE, RTN_BROADCAST, brd, 32, prim);
		if (!(ok & BRD0_OK))
			fib_magic(RTM_DELROUTE, RTN_BROADCAST, any, 32, prim);
	}
	if (!(ok & LOCAL_OK)) {
		fib_magic(RTM_DELROUTE, RTN_LOCAL, ifa->ifa_local, 32, prim);

		/* Check, that this local address finally disappeared. */
		if (gone &&
		    inet_addr_type(dev_net(dev), ifa->ifa_local) != RTN_LOCAL) {
			/* And the last, but not the least thing.
			 * We must flush stray FIB entries.
			 *
			 * First of all, we scan fib_info list searching
			 * for stray nexthop entries, then ignite fib_flush.
			 */
			if (fib_sync_down_addr(dev_net(dev), ifa->ifa_local))
				fib_flush(dev_net(dev));
		}
	}
#undef LOCAL_OK
#undef BRD_OK
#undef BRD0_OK
#undef BRD1_OK
}

static void nl_fib_lookup(struct fib_result_nl *frn, struct fib_table *tb)
{

	struct fib_result       res;
	struct flowi4           fl4 = {
		.flowi4_mark = frn->fl_mark,
		.daddr = frn->fl_addr,
		.flowi4_tos = frn->fl_tos,
		.flowi4_scope = frn->fl_scope,
	};

	frn->err = -ENOENT;
	if (tb) {
		local_bh_disable();

		frn->tb_id = tb->tb_id;
		rcu_read_lock();
		frn->err = fib_table_lookup(tb, &fl4, &res, FIB_LOOKUP_NOREF);

		if (!frn->err) {
			frn->prefixlen = res.prefixlen;
			frn->nh_sel = res.nh_sel;
			frn->type = res.type;
			frn->scope = res.scope;
		}
		rcu_read_unlock();
		local_bh_enable();
	}
}

static void nl_fib_input(struct sk_buff *skb)
{
	struct net *net;
	struct fib_result_nl *frn;
	struct nlmsghdr *nlh;
	struct fib_table *tb;
	u32 pid;

	net = sock_net(skb->sk);
	nlh = nlmsg_hdr(skb);
	if (skb->len < NLMSG_SPACE(0) || skb->len < nlh->nlmsg_len ||
	    nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*frn)))
		return;

	skb = skb_clone(skb, GFP_KERNEL);
	if (skb == NULL)
		return;
	nlh = nlmsg_hdr(skb);

	frn = (struct fib_result_nl *) NLMSG_DATA(nlh);
	tb = fib_get_table(net, frn->tb_id_in);

	nl_fib_lookup(frn, tb);

	pid = NETLINK_CB(skb).pid;      /* pid of sending process */
	NETLINK_CB(skb).pid = 0;        /* from kernel */
	NETLINK_CB(skb).dst_group = 0;  /* unicast */
	netlink_unicast(net->ipv4.fibnl, skb, pid, MSG_DONTWAIT);
}

static int __net_init nl_fib_lookup_init(struct net *net)
{
	struct sock *sk;
	struct netlink_kernel_cfg cfg = {
		.input	= nl_fib_input,
	};

	sk = netlink_kernel_create(net, NETLINK_FIB_LOOKUP, THIS_MODULE, &cfg);
	if (sk == NULL)
		return -EAFNOSUPPORT;
	net->ipv4.fibnl = sk;
	return 0;
}

static void nl_fib_lookup_exit(struct net *net)
{
	netlink_kernel_release(net->ipv4.fibnl);
	net->ipv4.fibnl = NULL;
}

static void fib_disable_ip(struct net_device *dev, int force, int delay)
{
	if (fib_sync_down_dev(dev, force))
		fib_flush(dev_net(dev));
	rt_cache_flush(dev_net(dev), delay);
	arp_ifdown(dev);
}

static int fib_inetaddr_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct net *net = dev_net(dev);

	switch (event) {
	case NETDEV_UP:
		fib_add_ifaddr(ifa);
#ifdef CONFIG_IP_ROUTE_MULTIPATH
		fib_sync_up(dev);
#endif
		atomic_inc(&net->ipv4.dev_addr_genid);
		rt_cache_flush(dev_net(dev), -1);
		break;
	case NETDEV_DOWN:
		fib_del_ifaddr(ifa, NULL);
		atomic_inc(&net->ipv4.dev_addr_genid);
		if (ifa->ifa_dev->ifa_list == NULL) {
			/* Last address was deleted from this interface.
			 * Disable IP.
			 */
			fib_disable_ip(dev, 1, 0);
		} else {
			rt_cache_flush(dev_net(dev), -1);
		}
		break;
	}
	return NOTIFY_DONE;
}

static int fib_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct in_device *in_dev = __in_dev_get_rtnl(dev);
	struct net *net = dev_net(dev);

	if (event == NETDEV_UNREGISTER) {
		fib_disable_ip(dev, 2, -1);
		rt_flush_dev(dev);
		return NOTIFY_DONE;
	}

	if (!in_dev)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		for_ifa(in_dev) {
			fib_add_ifaddr(ifa);
		} endfor_ifa(in_dev);
#ifdef CONFIG_IP_ROUTE_MULTIPATH
		fib_sync_up(dev);
#endif
		atomic_inc(&net->ipv4.dev_addr_genid);
		rt_cache_flush(dev_net(dev), -1);
		break;
	case NETDEV_DOWN:
		fib_disable_ip(dev, 0, 0);
		break;
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGE:
		rt_cache_flush(dev_net(dev), 0);
		break;
	case NETDEV_UNREGISTER_BATCH:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block fib_inetaddr_notifier = {
	.notifier_call = fib_inetaddr_event,
};

static struct notifier_block fib_netdev_notifier = {
	.notifier_call = fib_netdev_event,
};

static int __net_init ip_fib_net_init(struct net *net)
{
	int err;
	size_t size = sizeof(struct hlist_head) * FIB_TABLE_HASHSZ;

	/* Avoid false sharing : Use at least a full cache line */
	size = max_t(size_t, size, L1_CACHE_BYTES);

	net->ipv4.fib_table_hash = kzalloc(size, GFP_KERNEL);
	if (net->ipv4.fib_table_hash == NULL)
		return -ENOMEM;

	err = fib4_rules_init(net);
	if (err < 0)
		goto fail;
	return 0;

fail:
	kfree(net->ipv4.fib_table_hash);
	return err;
}

static void ip_fib_net_exit(struct net *net)
{
	unsigned int i;

#ifdef CONFIG_IP_MULTIPLE_TABLES
	fib4_rules_exit(net);
#endif

	rtnl_lock();
	for (i = 0; i < FIB_TABLE_HASHSZ; i++) {
		struct fib_table *tb;
		struct hlist_head *head;
		struct hlist_node *node, *tmp;

		head = &net->ipv4.fib_table_hash[i];
		hlist_for_each_entry_safe(tb, node, tmp, head, tb_hlist) {
			hlist_del(node);
			fib_table_flush(tb);
			fib_free_table(tb);
		}
	}
	rtnl_unlock();
	kfree(net->ipv4.fib_table_hash);
}

static int __net_init fib_net_init(struct net *net)
{
	int error;

#ifdef CONFIG_IP_ROUTE_CLASSID
	net->ipv4.fib_num_tclassid_users = 0;
#endif
	error = ip_fib_net_init(net);
	if (error < 0)
		goto out;
	error = nl_fib_lookup_init(net);
	if (error < 0)
		goto out_nlfl;
	error = fib_proc_init(net);
	if (error < 0)
		goto out_proc;
out:
	return error;

out_proc:
	nl_fib_lookup_exit(net);
out_nlfl:
	ip_fib_net_exit(net);
	goto out;
}

static void __net_exit fib_net_exit(struct net *net)
{
	fib_proc_exit(net);
	nl_fib_lookup_exit(net);
	ip_fib_net_exit(net);
}

static struct pernet_operations fib_net_ops = {
	.init = fib_net_init,
	.exit = fib_net_exit,
};

void __init ip_fib_init(void)
{
	rtnl_register(PF_INET, RTM_NEWROUTE, inet_rtm_newroute, NULL, NULL);
	rtnl_register(PF_INET, RTM_DELROUTE, inet_rtm_delroute, NULL, NULL);
	rtnl_register(PF_INET, RTM_GETROUTE, NULL, inet_dump_fib, NULL);

	register_pernet_subsys(&fib_net_ops);
	register_netdevice_notifier(&fib_netdev_notifier);
	register_inetaddr_notifier(&fib_inetaddr_notifier);

	fib_trie_init();
}
