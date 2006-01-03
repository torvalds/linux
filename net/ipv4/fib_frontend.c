/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base: FIB frontend.
 *
 * Version:	$Id: fib_frontend.c,v 1.26 2001/10/31 21:55:54 davem Exp $
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/init.h>

#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/arp.h>
#include <net/ip_fib.h>

#define FFprint(a...) printk(KERN_DEBUG a)

#ifndef CONFIG_IP_MULTIPLE_TABLES

#define RT_TABLE_MIN RT_TABLE_MAIN

struct fib_table *ip_fib_local_table;
struct fib_table *ip_fib_main_table;

#else

#define RT_TABLE_MIN 1

struct fib_table *fib_tables[RT_TABLE_MAX+1];

struct fib_table *__fib_new_table(int id)
{
	struct fib_table *tb;

	tb = fib_hash_init(id);
	if (!tb)
		return NULL;
	fib_tables[id] = tb;
	return tb;
}


#endif /* CONFIG_IP_MULTIPLE_TABLES */


static void fib_flush(void)
{
	int flushed = 0;
#ifdef CONFIG_IP_MULTIPLE_TABLES
	struct fib_table *tb;
	int id;

	for (id = RT_TABLE_MAX; id>0; id--) {
		if ((tb = fib_get_table(id))==NULL)
			continue;
		flushed += tb->tb_flush(tb);
	}
#else /* CONFIG_IP_MULTIPLE_TABLES */
	flushed += ip_fib_main_table->tb_flush(ip_fib_main_table);
	flushed += ip_fib_local_table->tb_flush(ip_fib_local_table);
#endif /* CONFIG_IP_MULTIPLE_TABLES */

	if (flushed)
		rt_cache_flush(-1);
}

/*
 *	Find the first device with a given source address.
 */

struct net_device * ip_dev_find(u32 addr)
{
	struct flowi fl = { .nl_u = { .ip4_u = { .daddr = addr } } };
	struct fib_result res;
	struct net_device *dev = NULL;

#ifdef CONFIG_IP_MULTIPLE_TABLES
	res.r = NULL;
#endif

	if (!ip_fib_local_table ||
	    ip_fib_local_table->tb_lookup(ip_fib_local_table, &fl, &res))
		return NULL;
	if (res.type != RTN_LOCAL)
		goto out;
	dev = FIB_RES_DEV(res);

	if (dev)
		dev_hold(dev);
out:
	fib_res_put(&res);
	return dev;
}

unsigned inet_addr_type(u32 addr)
{
	struct flowi		fl = { .nl_u = { .ip4_u = { .daddr = addr } } };
	struct fib_result	res;
	unsigned ret = RTN_BROADCAST;

	if (ZERONET(addr) || BADCLASS(addr))
		return RTN_BROADCAST;
	if (MULTICAST(addr))
		return RTN_MULTICAST;

#ifdef CONFIG_IP_MULTIPLE_TABLES
	res.r = NULL;
#endif
	
	if (ip_fib_local_table) {
		ret = RTN_UNICAST;
		if (!ip_fib_local_table->tb_lookup(ip_fib_local_table,
						   &fl, &res)) {
			ret = res.type;
			fib_res_put(&res);
		}
	}
	return ret;
}

/* Given (packet source, input interface) and optional (dst, oif, tos):
   - (main) check, that source is valid i.e. not broadcast or our local
     address.
   - figure out what "logical" interface this packet arrived
     and calculate "specific destination" address.
   - check, that packet arrived from expected physical interface.
 */

int fib_validate_source(u32 src, u32 dst, u8 tos, int oif,
			struct net_device *dev, u32 *spec_dst, u32 *itag)
{
	struct in_device *in_dev;
	struct flowi fl = { .nl_u = { .ip4_u =
				      { .daddr = src,
					.saddr = dst,
					.tos = tos } },
			    .iif = oif };
	struct fib_result res;
	int no_addr, rpf;
	int ret;

	no_addr = rpf = 0;
	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);
	if (in_dev) {
		no_addr = in_dev->ifa_list == NULL;
		rpf = IN_DEV_RPFILTER(in_dev);
	}
	rcu_read_unlock();

	if (in_dev == NULL)
		goto e_inval;

	if (fib_lookup(&fl, &res))
		goto last_resort;
	if (res.type != RTN_UNICAST)
		goto e_inval_res;
	*spec_dst = FIB_RES_PREFSRC(res);
	fib_combine_itag(itag, &res);
#ifdef CONFIG_IP_ROUTE_MULTIPATH
	if (FIB_RES_DEV(res) == dev || res.fi->fib_nhs > 1)
#else
	if (FIB_RES_DEV(res) == dev)
#endif
	{
		ret = FIB_RES_NH(res).nh_scope >= RT_SCOPE_HOST;
		fib_res_put(&res);
		return ret;
	}
	fib_res_put(&res);
	if (no_addr)
		goto last_resort;
	if (rpf)
		goto e_inval;
	fl.oif = dev->ifindex;

	ret = 0;
	if (fib_lookup(&fl, &res) == 0) {
		if (res.type == RTN_UNICAST) {
			*spec_dst = FIB_RES_PREFSRC(res);
			ret = FIB_RES_NH(res).nh_scope >= RT_SCOPE_HOST;
		}
		fib_res_put(&res);
	}
	return ret;

last_resort:
	if (rpf)
		goto e_inval;
	*spec_dst = inet_select_addr(dev, 0, RT_SCOPE_UNIVERSE);
	*itag = 0;
	return 0;

e_inval_res:
	fib_res_put(&res);
e_inval:
	return -EINVAL;
}

#ifndef CONFIG_IP_NOSIOCRT

/*
 *	Handle IP routing ioctl calls. These are used to manipulate the routing tables
 */
 
int ip_rt_ioctl(unsigned int cmd, void __user *arg)
{
	int err;
	struct kern_rta rta;
	struct rtentry  r;
	struct {
		struct nlmsghdr nlh;
		struct rtmsg	rtm;
	} req;

	switch (cmd) {
	case SIOCADDRT:		/* Add a route */
	case SIOCDELRT:		/* Delete a route */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(&r, arg, sizeof(struct rtentry)))
			return -EFAULT;
		rtnl_lock();
		err = fib_convert_rtentry(cmd, &req.nlh, &req.rtm, &rta, &r);
		if (err == 0) {
			if (cmd == SIOCDELRT) {
				struct fib_table *tb = fib_get_table(req.rtm.rtm_table);
				err = -ESRCH;
				if (tb)
					err = tb->tb_delete(tb, &req.rtm, &rta, &req.nlh, NULL);
			} else {
				struct fib_table *tb = fib_new_table(req.rtm.rtm_table);
				err = -ENOBUFS;
				if (tb)
					err = tb->tb_insert(tb, &req.rtm, &rta, &req.nlh, NULL);
			}
			kfree(rta.rta_mx);
		}
		rtnl_unlock();
		return err;
	}
	return -EINVAL;
}

#else

int ip_rt_ioctl(unsigned int cmd, void *arg)
{
	return -EINVAL;
}

#endif

static int inet_check_attr(struct rtmsg *r, struct rtattr **rta)
{
	int i;

	for (i=1; i<=RTA_MAX; i++) {
		struct rtattr *attr = rta[i-1];
		if (attr) {
			if (RTA_PAYLOAD(attr) < 4)
				return -EINVAL;
			if (i != RTA_MULTIPATH && i != RTA_METRICS)
				rta[i-1] = (struct rtattr*)RTA_DATA(attr);
		}
	}
	return 0;
}

int inet_rtm_delroute(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct fib_table * tb;
	struct rtattr **rta = arg;
	struct rtmsg *r = NLMSG_DATA(nlh);

	if (inet_check_attr(r, rta))
		return -EINVAL;

	tb = fib_get_table(r->rtm_table);
	if (tb)
		return tb->tb_delete(tb, r, (struct kern_rta*)rta, nlh, &NETLINK_CB(skb));
	return -ESRCH;
}

int inet_rtm_newroute(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct fib_table * tb;
	struct rtattr **rta = arg;
	struct rtmsg *r = NLMSG_DATA(nlh);

	if (inet_check_attr(r, rta))
		return -EINVAL;

	tb = fib_new_table(r->rtm_table);
	if (tb)
		return tb->tb_insert(tb, r, (struct kern_rta*)rta, nlh, &NETLINK_CB(skb));
	return -ENOBUFS;
}

int inet_dump_fib(struct sk_buff *skb, struct netlink_callback *cb)
{
	int t;
	int s_t;
	struct fib_table *tb;

	if (NLMSG_PAYLOAD(cb->nlh, 0) >= sizeof(struct rtmsg) &&
	    ((struct rtmsg*)NLMSG_DATA(cb->nlh))->rtm_flags&RTM_F_CLONED)
		return ip_rt_dump(skb, cb);

	s_t = cb->args[0];
	if (s_t == 0)
		s_t = cb->args[0] = RT_TABLE_MIN;

	for (t=s_t; t<=RT_TABLE_MAX; t++) {
		if (t < s_t) continue;
		if (t > s_t)
			memset(&cb->args[1], 0, sizeof(cb->args)-sizeof(cb->args[0]));
		if ((tb = fib_get_table(t))==NULL)
			continue;
		if (tb->tb_dump(tb, skb, cb) < 0) 
			break;
	}

	cb->args[0] = t;

	return skb->len;
}

/* Prepare and feed intra-kernel routing request.
   Really, it should be netlink message, but :-( netlink
   can be not configured, so that we feed it directly
   to fib engine. It is legal, because all events occur
   only when netlink is already locked.
 */

static void fib_magic(int cmd, int type, u32 dst, int dst_len, struct in_ifaddr *ifa)
{
	struct fib_table * tb;
	struct {
		struct nlmsghdr	nlh;
		struct rtmsg	rtm;
	} req;
	struct kern_rta rta;

	memset(&req.rtm, 0, sizeof(req.rtm));
	memset(&rta, 0, sizeof(rta));

	if (type == RTN_UNICAST)
		tb = fib_new_table(RT_TABLE_MAIN);
	else
		tb = fib_new_table(RT_TABLE_LOCAL);

	if (tb == NULL)
		return;

	req.nlh.nlmsg_len = sizeof(req);
	req.nlh.nlmsg_type = cmd;
	req.nlh.nlmsg_flags = NLM_F_REQUEST|NLM_F_CREATE|NLM_F_APPEND;
	req.nlh.nlmsg_pid = 0;
	req.nlh.nlmsg_seq = 0;

	req.rtm.rtm_dst_len = dst_len;
	req.rtm.rtm_table = tb->tb_id;
	req.rtm.rtm_protocol = RTPROT_KERNEL;
	req.rtm.rtm_scope = (type != RTN_LOCAL ? RT_SCOPE_LINK : RT_SCOPE_HOST);
	req.rtm.rtm_type = type;

	rta.rta_dst = &dst;
	rta.rta_prefsrc = &ifa->ifa_local;
	rta.rta_oif = &ifa->ifa_dev->dev->ifindex;

	if (cmd == RTM_NEWROUTE)
		tb->tb_insert(tb, &req.rtm, &rta, &req.nlh, NULL);
	else
		tb->tb_delete(tb, &req.rtm, &rta, &req.nlh, NULL);
}

void fib_add_ifaddr(struct in_ifaddr *ifa)
{
	struct in_device *in_dev = ifa->ifa_dev;
	struct net_device *dev = in_dev->dev;
	struct in_ifaddr *prim = ifa;
	u32 mask = ifa->ifa_mask;
	u32 addr = ifa->ifa_local;
	u32 prefix = ifa->ifa_address&mask;

	if (ifa->ifa_flags&IFA_F_SECONDARY) {
		prim = inet_ifa_byprefix(in_dev, prefix, mask);
		if (prim == NULL) {
			printk(KERN_DEBUG "fib_add_ifaddr: bug: prim == NULL\n");
			return;
		}
	}

	fib_magic(RTM_NEWROUTE, RTN_LOCAL, addr, 32, prim);

	if (!(dev->flags&IFF_UP))
		return;

	/* Add broadcast address, if it is explicitly assigned. */
	if (ifa->ifa_broadcast && ifa->ifa_broadcast != 0xFFFFFFFF)
		fib_magic(RTM_NEWROUTE, RTN_BROADCAST, ifa->ifa_broadcast, 32, prim);

	if (!ZERONET(prefix) && !(ifa->ifa_flags&IFA_F_SECONDARY) &&
	    (prefix != addr || ifa->ifa_prefixlen < 32)) {
		fib_magic(RTM_NEWROUTE, dev->flags&IFF_LOOPBACK ? RTN_LOCAL :
			  RTN_UNICAST, prefix, ifa->ifa_prefixlen, prim);

		/* Add network specific broadcasts, when it takes a sense */
		if (ifa->ifa_prefixlen < 31) {
			fib_magic(RTM_NEWROUTE, RTN_BROADCAST, prefix, 32, prim);
			fib_magic(RTM_NEWROUTE, RTN_BROADCAST, prefix|~mask, 32, prim);
		}
	}
}

static void fib_del_ifaddr(struct in_ifaddr *ifa)
{
	struct in_device *in_dev = ifa->ifa_dev;
	struct net_device *dev = in_dev->dev;
	struct in_ifaddr *ifa1;
	struct in_ifaddr *prim = ifa;
	u32 brd = ifa->ifa_address|~ifa->ifa_mask;
	u32 any = ifa->ifa_address&ifa->ifa_mask;
#define LOCAL_OK	1
#define BRD_OK		2
#define BRD0_OK		4
#define BRD1_OK		8
	unsigned ok = 0;

	if (!(ifa->ifa_flags&IFA_F_SECONDARY))
		fib_magic(RTM_DELROUTE, dev->flags&IFF_LOOPBACK ? RTN_LOCAL :
			  RTN_UNICAST, any, ifa->ifa_prefixlen, prim);
	else {
		prim = inet_ifa_byprefix(in_dev, any, ifa->ifa_mask);
		if (prim == NULL) {
			printk(KERN_DEBUG "fib_del_ifaddr: bug: prim == NULL\n");
			return;
		}
	}

	/* Deletion is more complicated than add.
	   We should take care of not to delete too much :-)

	   Scan address list to be sure that addresses are really gone.
	 */

	for (ifa1 = in_dev->ifa_list; ifa1; ifa1 = ifa1->ifa_next) {
		if (ifa->ifa_local == ifa1->ifa_local)
			ok |= LOCAL_OK;
		if (ifa->ifa_broadcast == ifa1->ifa_broadcast)
			ok |= BRD_OK;
		if (brd == ifa1->ifa_broadcast)
			ok |= BRD1_OK;
		if (any == ifa1->ifa_broadcast)
			ok |= BRD0_OK;
	}

	if (!(ok&BRD_OK))
		fib_magic(RTM_DELROUTE, RTN_BROADCAST, ifa->ifa_broadcast, 32, prim);
	if (!(ok&BRD1_OK))
		fib_magic(RTM_DELROUTE, RTN_BROADCAST, brd, 32, prim);
	if (!(ok&BRD0_OK))
		fib_magic(RTM_DELROUTE, RTN_BROADCAST, any, 32, prim);
	if (!(ok&LOCAL_OK)) {
		fib_magic(RTM_DELROUTE, RTN_LOCAL, ifa->ifa_local, 32, prim);

		/* Check, that this local address finally disappeared. */
		if (inet_addr_type(ifa->ifa_local) != RTN_LOCAL) {
			/* And the last, but not the least thing.
			   We must flush stray FIB entries.

			   First of all, we scan fib_info list searching
			   for stray nexthop entries, then ignite fib_flush.
			*/
			if (fib_sync_down(ifa->ifa_local, NULL, 0))
				fib_flush();
		}
	}
#undef LOCAL_OK
#undef BRD_OK
#undef BRD0_OK
#undef BRD1_OK
}

static void nl_fib_lookup(struct fib_result_nl *frn, struct fib_table *tb )
{
	
	struct fib_result       res;
	struct flowi            fl = { .nl_u = { .ip4_u = { .daddr = frn->fl_addr, 
							    .fwmark = frn->fl_fwmark,
							    .tos = frn->fl_tos,
							    .scope = frn->fl_scope } } };
	if (tb) {
		local_bh_disable();

		frn->tb_id = tb->tb_id;
		frn->err = tb->tb_lookup(tb, &fl, &res);

		if (!frn->err) {
			frn->prefixlen = res.prefixlen;
			frn->nh_sel = res.nh_sel;
			frn->type = res.type;
			frn->scope = res.scope;
		}
		local_bh_enable();
	}
}

static void nl_fib_input(struct sock *sk, int len)
{
	struct sk_buff *skb = NULL;
        struct nlmsghdr *nlh = NULL;
	struct fib_result_nl *frn;
	u32 pid;     
	struct fib_table *tb;
	
	skb = skb_dequeue(&sk->sk_receive_queue);
	nlh = (struct nlmsghdr *)skb->data;
	if (skb->len < NLMSG_SPACE(0) || skb->len < nlh->nlmsg_len ||
	    nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*frn))) {
		kfree_skb(skb);
		return;
	}
	
	frn = (struct fib_result_nl *) NLMSG_DATA(nlh);
	tb = fib_get_table(frn->tb_id_in);

	nl_fib_lookup(frn, tb);
	
	pid = nlh->nlmsg_pid;           /*pid of sending process */
	NETLINK_CB(skb).pid = 0;         /* from kernel */
	NETLINK_CB(skb).dst_pid = pid;
	NETLINK_CB(skb).dst_group = 0;  /* unicast */
	netlink_unicast(sk, skb, pid, MSG_DONTWAIT);
}    

static void nl_fib_lookup_init(void)
{
      netlink_kernel_create(NETLINK_FIB_LOOKUP, 0, nl_fib_input, THIS_MODULE);
}

static void fib_disable_ip(struct net_device *dev, int force)
{
	if (fib_sync_down(0, dev, force))
		fib_flush();
	rt_cache_flush(0);
	arp_ifdown(dev);
}

static int fib_inetaddr_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr*)ptr;

	switch (event) {
	case NETDEV_UP:
		fib_add_ifaddr(ifa);
#ifdef CONFIG_IP_ROUTE_MULTIPATH
		fib_sync_up(ifa->ifa_dev->dev);
#endif
		rt_cache_flush(-1);
		break;
	case NETDEV_DOWN:
		fib_del_ifaddr(ifa);
		if (ifa->ifa_dev->ifa_list == NULL) {
			/* Last address was deleted from this interface.
			   Disable IP.
			 */
			fib_disable_ip(ifa->ifa_dev->dev, 1);
		} else {
			rt_cache_flush(-1);
		}
		break;
	}
	return NOTIFY_DONE;
}

static int fib_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct in_device *in_dev = __in_dev_get_rtnl(dev);

	if (event == NETDEV_UNREGISTER) {
		fib_disable_ip(dev, 2);
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
		rt_cache_flush(-1);
		break;
	case NETDEV_DOWN:
		fib_disable_ip(dev, 0);
		break;
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGE:
		rt_cache_flush(0);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block fib_inetaddr_notifier = {
	.notifier_call =fib_inetaddr_event,
};

static struct notifier_block fib_netdev_notifier = {
	.notifier_call =fib_netdev_event,
};

void __init ip_fib_init(void)
{
#ifndef CONFIG_IP_MULTIPLE_TABLES
	ip_fib_local_table = fib_hash_init(RT_TABLE_LOCAL);
	ip_fib_main_table  = fib_hash_init(RT_TABLE_MAIN);
#else
	fib_rules_init();
#endif

	register_netdevice_notifier(&fib_netdev_notifier);
	register_inetaddr_notifier(&fib_inetaddr_notifier);
	nl_fib_lookup_init();
}

EXPORT_SYMBOL(inet_addr_type);
EXPORT_SYMBOL(ip_rt_ioctl);
