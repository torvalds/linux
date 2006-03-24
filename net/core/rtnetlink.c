/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Routing netlink socket interface: protocol independent part.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Fixes:
 *	Vitaly E. Lavrov		RTA_OK arithmetics was wrong.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/capability.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/mutex.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/string.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/udp.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <net/netlink.h>
#ifdef CONFIG_NET_WIRELESS_RTNETLINK
#include <linux/wireless.h>
#include <net/iw_handler.h>
#endif	/* CONFIG_NET_WIRELESS_RTNETLINK */

static DEFINE_MUTEX(rtnl_mutex);

void rtnl_lock(void)
{
	mutex_lock(&rtnl_mutex);
}

void __rtnl_unlock(void)
{
	mutex_unlock(&rtnl_mutex);
}

void rtnl_unlock(void)
{
	mutex_unlock(&rtnl_mutex);
	if (rtnl && rtnl->sk_receive_queue.qlen)
		rtnl->sk_data_ready(rtnl, 0);
	netdev_run_todo();
}

int rtnl_trylock(void)
{
	return mutex_trylock(&rtnl_mutex);
}

int rtattr_parse(struct rtattr *tb[], int maxattr, struct rtattr *rta, int len)
{
	memset(tb, 0, sizeof(struct rtattr*)*maxattr);

	while (RTA_OK(rta, len)) {
		unsigned flavor = rta->rta_type;
		if (flavor && flavor <= maxattr)
			tb[flavor-1] = rta;
		rta = RTA_NEXT(rta, len);
	}
	return 0;
}

struct sock *rtnl;

struct rtnetlink_link * rtnetlink_links[NPROTO];

static const int rtm_min[RTM_NR_FAMILIES] =
{
	[RTM_FAM(RTM_NEWLINK)]      = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
	[RTM_FAM(RTM_NEWADDR)]      = NLMSG_LENGTH(sizeof(struct ifaddrmsg)),
	[RTM_FAM(RTM_NEWROUTE)]     = NLMSG_LENGTH(sizeof(struct rtmsg)),
	[RTM_FAM(RTM_NEWNEIGH)]     = NLMSG_LENGTH(sizeof(struct ndmsg)),
	[RTM_FAM(RTM_NEWRULE)]      = NLMSG_LENGTH(sizeof(struct rtmsg)),
	[RTM_FAM(RTM_NEWQDISC)]     = NLMSG_LENGTH(sizeof(struct tcmsg)),
	[RTM_FAM(RTM_NEWTCLASS)]    = NLMSG_LENGTH(sizeof(struct tcmsg)),
	[RTM_FAM(RTM_NEWTFILTER)]   = NLMSG_LENGTH(sizeof(struct tcmsg)),
	[RTM_FAM(RTM_NEWACTION)]    = NLMSG_LENGTH(sizeof(struct tcamsg)),
	[RTM_FAM(RTM_NEWPREFIX)]    = NLMSG_LENGTH(sizeof(struct rtgenmsg)),
	[RTM_FAM(RTM_GETMULTICAST)] = NLMSG_LENGTH(sizeof(struct rtgenmsg)),
	[RTM_FAM(RTM_GETANYCAST)]   = NLMSG_LENGTH(sizeof(struct rtgenmsg)),
	[RTM_FAM(RTM_NEWNEIGHTBL)]  = NLMSG_LENGTH(sizeof(struct ndtmsg)),
};

static const int rta_max[RTM_NR_FAMILIES] =
{
	[RTM_FAM(RTM_NEWLINK)]      = IFLA_MAX,
	[RTM_FAM(RTM_NEWADDR)]      = IFA_MAX,
	[RTM_FAM(RTM_NEWROUTE)]     = RTA_MAX,
	[RTM_FAM(RTM_NEWNEIGH)]     = NDA_MAX,
	[RTM_FAM(RTM_NEWRULE)]      = RTA_MAX,
	[RTM_FAM(RTM_NEWQDISC)]     = TCA_MAX,
	[RTM_FAM(RTM_NEWTCLASS)]    = TCA_MAX,
	[RTM_FAM(RTM_NEWTFILTER)]   = TCA_MAX,
	[RTM_FAM(RTM_NEWACTION)]    = TCAA_MAX,
	[RTM_FAM(RTM_NEWNEIGHTBL)]  = NDTA_MAX,
};

void __rta_fill(struct sk_buff *skb, int attrtype, int attrlen, const void *data)
{
	struct rtattr *rta;
	int size = RTA_LENGTH(attrlen);

	rta = (struct rtattr*)skb_put(skb, RTA_ALIGN(size));
	rta->rta_type = attrtype;
	rta->rta_len = size;
	memcpy(RTA_DATA(rta), data, attrlen);
	memset(RTA_DATA(rta) + attrlen, 0, RTA_ALIGN(size) - size);
}

size_t rtattr_strlcpy(char *dest, const struct rtattr *rta, size_t size)
{
	size_t ret = RTA_PAYLOAD(rta);
	char *src = RTA_DATA(rta);

	if (ret > 0 && src[ret - 1] == '\0')
		ret--;
	if (size > 0) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memset(dest, 0, size);
		memcpy(dest, src, len);
	}
	return ret;
}

int rtnetlink_send(struct sk_buff *skb, u32 pid, unsigned group, int echo)
{
	int err = 0;

	NETLINK_CB(skb).dst_group = group;
	if (echo)
		atomic_inc(&skb->users);
	netlink_broadcast(rtnl, skb, pid, group, GFP_KERNEL);
	if (echo)
		err = netlink_unicast(rtnl, skb, pid, MSG_DONTWAIT);
	return err;
}

int rtnetlink_put_metrics(struct sk_buff *skb, u32 *metrics)
{
	struct rtattr *mx = (struct rtattr*)skb->tail;
	int i;

	RTA_PUT(skb, RTA_METRICS, 0, NULL);
	for (i=0; i<RTAX_MAX; i++) {
		if (metrics[i])
			RTA_PUT(skb, i+1, sizeof(u32), metrics+i);
	}
	mx->rta_len = skb->tail - (u8*)mx;
	if (mx->rta_len == RTA_LENGTH(0))
		skb_trim(skb, (u8*)mx - skb->data);
	return 0;

rtattr_failure:
	skb_trim(skb, (u8*)mx - skb->data);
	return -1;
}


static void set_operstate(struct net_device *dev, unsigned char transition)
{
	unsigned char operstate = dev->operstate;

	switch(transition) {
	case IF_OPER_UP:
		if ((operstate == IF_OPER_DORMANT ||
		     operstate == IF_OPER_UNKNOWN) &&
		    !netif_dormant(dev))
			operstate = IF_OPER_UP;
		break;

	case IF_OPER_DORMANT:
		if (operstate == IF_OPER_UP ||
		    operstate == IF_OPER_UNKNOWN)
			operstate = IF_OPER_DORMANT;
		break;
	};

	if (dev->operstate != operstate) {
		write_lock_bh(&dev_base_lock);
		dev->operstate = operstate;
		write_unlock_bh(&dev_base_lock);
		netdev_state_change(dev);
	}
}

static int rtnetlink_fill_ifinfo(struct sk_buff *skb, struct net_device *dev,
				 int type, u32 pid, u32 seq, u32 change, 
				 unsigned int flags)
{
	struct ifinfomsg *r;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_NEW(skb, pid, seq, type, sizeof(*r), flags);
	r = NLMSG_DATA(nlh);
	r->ifi_family = AF_UNSPEC;
	r->__ifi_pad = 0;
	r->ifi_type = dev->type;
	r->ifi_index = dev->ifindex;
	r->ifi_flags = dev_get_flags(dev);
	r->ifi_change = change;

	RTA_PUT(skb, IFLA_IFNAME, strlen(dev->name)+1, dev->name);

	if (1) {
		u32 txqlen = dev->tx_queue_len;
		RTA_PUT(skb, IFLA_TXQLEN, sizeof(txqlen), &txqlen);
	}

	if (1) {
		u32 weight = dev->weight;
		RTA_PUT(skb, IFLA_WEIGHT, sizeof(weight), &weight);
	}

	if (1) {
		u8 operstate = netif_running(dev)?dev->operstate:IF_OPER_DOWN;
		u8 link_mode = dev->link_mode;
		RTA_PUT(skb, IFLA_OPERSTATE, sizeof(operstate), &operstate);
		RTA_PUT(skb, IFLA_LINKMODE, sizeof(link_mode), &link_mode);
	}

	if (1) {
		struct rtnl_link_ifmap map = {
			.mem_start   = dev->mem_start,
			.mem_end     = dev->mem_end,
			.base_addr   = dev->base_addr,
			.irq         = dev->irq,
			.dma         = dev->dma,
			.port        = dev->if_port,
		};
		RTA_PUT(skb, IFLA_MAP, sizeof(map), &map);
	}

	if (dev->addr_len) {
		RTA_PUT(skb, IFLA_ADDRESS, dev->addr_len, dev->dev_addr);
		RTA_PUT(skb, IFLA_BROADCAST, dev->addr_len, dev->broadcast);
	}

	if (1) {
		u32 mtu = dev->mtu;
		RTA_PUT(skb, IFLA_MTU, sizeof(mtu), &mtu);
	}

	if (dev->ifindex != dev->iflink) {
		u32 iflink = dev->iflink;
		RTA_PUT(skb, IFLA_LINK, sizeof(iflink), &iflink);
	}

	if (dev->qdisc_sleeping)
		RTA_PUT(skb, IFLA_QDISC,
			strlen(dev->qdisc_sleeping->ops->id) + 1,
			dev->qdisc_sleeping->ops->id);
	
	if (dev->master) {
		u32 master = dev->master->ifindex;
		RTA_PUT(skb, IFLA_MASTER, sizeof(master), &master);
	}

	if (dev->get_stats) {
		unsigned long *stats = (unsigned long*)dev->get_stats(dev);
		if (stats) {
			struct rtattr  *a;
			__u32	       *s;
			int		i;
			int		n = sizeof(struct rtnl_link_stats)/4;

			a = __RTA_PUT(skb, IFLA_STATS, n*4);
			s = RTA_DATA(a);
			for (i=0; i<n; i++)
				s[i] = stats[i];
		}
	}
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int rtnetlink_dump_ifinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx;
	int s_idx = cb->args[0];
	struct net_device *dev;

	read_lock(&dev_base_lock);
	for (dev=dev_base, idx=0; dev; dev = dev->next, idx++) {
		if (idx < s_idx)
			continue;
		if (rtnetlink_fill_ifinfo(skb, dev, RTM_NEWLINK,
					  NETLINK_CB(cb->skb).pid,
					  cb->nlh->nlmsg_seq, 0,
					  NLM_F_MULTI) <= 0)
			break;
	}
	read_unlock(&dev_base_lock);
	cb->args[0] = idx;

	return skb->len;
}

static int do_setlink(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct ifinfomsg  *ifm = NLMSG_DATA(nlh);
	struct rtattr    **ida = arg;
	struct net_device *dev;
	int err, send_addr_notify = 0;

	if (ifm->ifi_index >= 0)
		dev = dev_get_by_index(ifm->ifi_index);
	else if (ida[IFLA_IFNAME - 1]) {
		char ifname[IFNAMSIZ];

		if (rtattr_strlcpy(ifname, ida[IFLA_IFNAME - 1],
		                   IFNAMSIZ) >= IFNAMSIZ)
			return -EINVAL;
		dev = dev_get_by_name(ifname);
	} else
		return -EINVAL;

	if (!dev)
		return -ENODEV;

	err = -EINVAL;

	if (ifm->ifi_flags)
		dev_change_flags(dev, ifm->ifi_flags);

	if (ida[IFLA_MAP - 1]) {
		struct rtnl_link_ifmap *u_map;
		struct ifmap k_map;

		if (!dev->set_config) {
			err = -EOPNOTSUPP;
			goto out;
		}

		if (!netif_device_present(dev)) {
			err = -ENODEV;
			goto out;
		}
		
		if (ida[IFLA_MAP - 1]->rta_len != RTA_LENGTH(sizeof(*u_map)))
			goto out;

		u_map = RTA_DATA(ida[IFLA_MAP - 1]);

		k_map.mem_start = (unsigned long) u_map->mem_start;
		k_map.mem_end = (unsigned long) u_map->mem_end;
		k_map.base_addr = (unsigned short) u_map->base_addr;
		k_map.irq = (unsigned char) u_map->irq;
		k_map.dma = (unsigned char) u_map->dma;
		k_map.port = (unsigned char) u_map->port;

		err = dev->set_config(dev, &k_map);

		if (err)
			goto out;
	}

	if (ida[IFLA_ADDRESS - 1]) {
		if (!dev->set_mac_address) {
			err = -EOPNOTSUPP;
			goto out;
		}
		if (!netif_device_present(dev)) {
			err = -ENODEV;
			goto out;
		}
		if (ida[IFLA_ADDRESS - 1]->rta_len != RTA_LENGTH(dev->addr_len))
			goto out;

		err = dev->set_mac_address(dev, RTA_DATA(ida[IFLA_ADDRESS - 1]));
		if (err)
			goto out;
		send_addr_notify = 1;
	}

	if (ida[IFLA_BROADCAST - 1]) {
		if (ida[IFLA_BROADCAST - 1]->rta_len != RTA_LENGTH(dev->addr_len))
			goto out;
		memcpy(dev->broadcast, RTA_DATA(ida[IFLA_BROADCAST - 1]),
		       dev->addr_len);
		send_addr_notify = 1;
	}

	if (ida[IFLA_MTU - 1]) {
		if (ida[IFLA_MTU - 1]->rta_len != RTA_LENGTH(sizeof(u32)))
			goto out;
		err = dev_set_mtu(dev, *((u32 *) RTA_DATA(ida[IFLA_MTU - 1])));

		if (err)
			goto out;

	}

	if (ida[IFLA_TXQLEN - 1]) {
		if (ida[IFLA_TXQLEN - 1]->rta_len != RTA_LENGTH(sizeof(u32)))
			goto out;

		dev->tx_queue_len = *((u32 *) RTA_DATA(ida[IFLA_TXQLEN - 1]));
	}

	if (ida[IFLA_WEIGHT - 1]) {
		if (ida[IFLA_WEIGHT - 1]->rta_len != RTA_LENGTH(sizeof(u32)))
			goto out;

		dev->weight = *((u32 *) RTA_DATA(ida[IFLA_WEIGHT - 1]));
	}

	if (ida[IFLA_OPERSTATE - 1]) {
		if (ida[IFLA_OPERSTATE - 1]->rta_len != RTA_LENGTH(sizeof(u8)))
			goto out;

		set_operstate(dev, *((u8 *) RTA_DATA(ida[IFLA_OPERSTATE - 1])));
	}

	if (ida[IFLA_LINKMODE - 1]) {
		if (ida[IFLA_LINKMODE - 1]->rta_len != RTA_LENGTH(sizeof(u8)))
			goto out;

		write_lock_bh(&dev_base_lock);
		dev->link_mode = *((u8 *) RTA_DATA(ida[IFLA_LINKMODE - 1]));
		write_unlock_bh(&dev_base_lock);
	}

	if (ifm->ifi_index >= 0 && ida[IFLA_IFNAME - 1]) {
		char ifname[IFNAMSIZ];

		if (rtattr_strlcpy(ifname, ida[IFLA_IFNAME - 1],
		                   IFNAMSIZ) >= IFNAMSIZ)
			goto out;
		err = dev_change_name(dev, ifname);
		if (err)
			goto out;
	}

#ifdef CONFIG_NET_WIRELESS_RTNETLINK
	if (ida[IFLA_WIRELESS - 1]) {

		/* Call Wireless Extensions.
		 * Various stuff checked in there... */
		err = wireless_rtnetlink_set(dev, RTA_DATA(ida[IFLA_WIRELESS - 1]), ida[IFLA_WIRELESS - 1]->rta_len);
		if (err)
			goto out;
	}
#endif	/* CONFIG_NET_WIRELESS_RTNETLINK */

	err = 0;

out:
	if (send_addr_notify)
		call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);

	dev_put(dev);
	return err;
}

#ifdef CONFIG_NET_WIRELESS_RTNETLINK
static int do_getlink(struct sk_buff *in_skb, struct nlmsghdr* in_nlh, void *arg)
{
	struct ifinfomsg  *ifm = NLMSG_DATA(in_nlh);
	struct rtattr    **ida = arg;
	struct net_device *dev;
	struct ifinfomsg *r;
	struct nlmsghdr  *nlh;
	int err = -ENOBUFS;
	struct sk_buff *skb;
	unsigned char	 *b;
	char *iw_buf = NULL;
	int iw_buf_len = 0;

	if (ifm->ifi_index >= 0)
		dev = dev_get_by_index(ifm->ifi_index);
	else
		return -EINVAL;
	if (!dev)
		return -ENODEV;

#ifdef CONFIG_NET_WIRELESS_RTNETLINK
	if (ida[IFLA_WIRELESS - 1]) {

		/* Call Wireless Extensions. We need to know the size before
		 * we can alloc. Various stuff checked in there... */
		err = wireless_rtnetlink_get(dev, RTA_DATA(ida[IFLA_WIRELESS - 1]), ida[IFLA_WIRELESS - 1]->rta_len, &iw_buf, &iw_buf_len);
		if (err)
			goto out;
	}
#endif	/* CONFIG_NET_WIRELESS_RTNETLINK */

	/* Create a skb big enough to include all the data.
	 * Some requests are way bigger than 4k... Jean II */
	skb = alloc_skb((NLMSG_LENGTH(sizeof(*r))) + (RTA_SPACE(iw_buf_len)),
			GFP_KERNEL);
	if (!skb)
		goto out;
	b = skb->tail;

	/* Put in the message the usual good stuff */
	nlh = NLMSG_PUT(skb, NETLINK_CB(in_skb).pid, in_nlh->nlmsg_seq,
			RTM_NEWLINK, sizeof(*r));
	r = NLMSG_DATA(nlh);
	r->ifi_family = AF_UNSPEC;
	r->__ifi_pad = 0;
	r->ifi_type = dev->type;
	r->ifi_index = dev->ifindex;
	r->ifi_flags = dev->flags;
	r->ifi_change = 0;

	/* Put the wireless payload if it exist */
	if(iw_buf != NULL)
		RTA_PUT(skb, IFLA_WIRELESS, iw_buf_len,
			iw_buf + IW_EV_POINT_OFF);

	nlh->nlmsg_len = skb->tail - b;

	/* Needed ? */
	NETLINK_CB(skb).dst_pid = NETLINK_CB(in_skb).pid;

	err = netlink_unicast(rtnl, skb, NETLINK_CB(in_skb).pid, MSG_DONTWAIT);
	if (err > 0)
		err = 0;
out:
	if(iw_buf != NULL)
		kfree(iw_buf);
	dev_put(dev);
	return err;

rtattr_failure:
nlmsg_failure:
	kfree_skb(skb);
	goto out;
}
#endif	/* CONFIG_NET_WIRELESS_RTNETLINK */

static int rtnetlink_dump_all(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx;
	int s_idx = cb->family;

	if (s_idx == 0)
		s_idx = 1;
	for (idx=1; idx<NPROTO; idx++) {
		int type = cb->nlh->nlmsg_type-RTM_BASE;
		if (idx < s_idx || idx == PF_PACKET)
			continue;
		if (rtnetlink_links[idx] == NULL ||
		    rtnetlink_links[idx][type].dumpit == NULL)
			continue;
		if (idx > s_idx)
			memset(&cb->args[0], 0, sizeof(cb->args));
		if (rtnetlink_links[idx][type].dumpit(skb, cb))
			break;
	}
	cb->family = idx;

	return skb->len;
}

void rtmsg_ifinfo(int type, struct net_device *dev, unsigned change)
{
	struct sk_buff *skb;
	int size = NLMSG_SPACE(sizeof(struct ifinfomsg) +
			       sizeof(struct rtnl_link_ifmap) +
			       sizeof(struct rtnl_link_stats) + 128);

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return;

	if (rtnetlink_fill_ifinfo(skb, dev, type, 0, 0, change, 0) < 0) {
		kfree_skb(skb);
		return;
	}
	NETLINK_CB(skb).dst_group = RTNLGRP_LINK;
	netlink_broadcast(rtnl, skb, 0, RTNLGRP_LINK, GFP_KERNEL);
}

/* Protected by RTNL sempahore.  */
static struct rtattr **rta_buf;
static int rtattr_max;

/* Process one rtnetlink message. */

static __inline__ int
rtnetlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh, int *errp)
{
	struct rtnetlink_link *link;
	struct rtnetlink_link *link_tab;
	int sz_idx, kind;
	int min_len;
	int family;
	int type;
	int err;

	/* Only requests are handled by kernel now */
	if (!(nlh->nlmsg_flags&NLM_F_REQUEST))
		return 0;

	type = nlh->nlmsg_type;

	/* A control message: ignore them */
	if (type < RTM_BASE)
		return 0;

	/* Unknown message: reply with EINVAL */
	if (type > RTM_MAX)
		goto err_inval;

	type -= RTM_BASE;

	/* All the messages must have at least 1 byte length */
	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct rtgenmsg)))
		return 0;

	family = ((struct rtgenmsg*)NLMSG_DATA(nlh))->rtgen_family;
	if (family >= NPROTO) {
		*errp = -EAFNOSUPPORT;
		return -1;
	}

	link_tab = rtnetlink_links[family];
	if (link_tab == NULL)
		link_tab = rtnetlink_links[PF_UNSPEC];
	link = &link_tab[type];

	sz_idx = type>>2;
	kind = type&3;

	if (kind != 2 && security_netlink_recv(skb)) {
		*errp = -EPERM;
		return -1;
	}

	if (kind == 2 && nlh->nlmsg_flags&NLM_F_DUMP) {
		if (link->dumpit == NULL)
			link = &(rtnetlink_links[PF_UNSPEC][type]);

		if (link->dumpit == NULL)
			goto err_inval;

		if ((*errp = netlink_dump_start(rtnl, skb, nlh,
						link->dumpit, NULL)) != 0) {
			return -1;
		}

		netlink_queue_skip(nlh, skb);
		return -1;
	}

	memset(rta_buf, 0, (rtattr_max * sizeof(struct rtattr *)));

	min_len = rtm_min[sz_idx];
	if (nlh->nlmsg_len < min_len)
		goto err_inval;

	if (nlh->nlmsg_len > min_len) {
		int attrlen = nlh->nlmsg_len - NLMSG_ALIGN(min_len);
		struct rtattr *attr = (void*)nlh + NLMSG_ALIGN(min_len);

		while (RTA_OK(attr, attrlen)) {
			unsigned flavor = attr->rta_type;
			if (flavor) {
				if (flavor > rta_max[sz_idx])
					goto err_inval;
				rta_buf[flavor-1] = attr;
			}
			attr = RTA_NEXT(attr, attrlen);
		}
	}

	if (link->doit == NULL)
		link = &(rtnetlink_links[PF_UNSPEC][type]);
	if (link->doit == NULL)
		goto err_inval;
	err = link->doit(skb, nlh, (void *)&rta_buf[0]);

	*errp = err;
	return err;

err_inval:
	*errp = -EINVAL;
	return -1;
}

static void rtnetlink_rcv(struct sock *sk, int len)
{
	unsigned int qlen = 0;

	do {
		mutex_lock(&rtnl_mutex);
		netlink_run_queue(sk, &qlen, &rtnetlink_rcv_msg);
		mutex_unlock(&rtnl_mutex);

		netdev_run_todo();
	} while (qlen);
}

static struct rtnetlink_link link_rtnetlink_table[RTM_NR_MSGTYPES] =
{
	[RTM_GETLINK     - RTM_BASE] = {
#ifdef CONFIG_NET_WIRELESS_RTNETLINK
					 .doit   = do_getlink,
#endif	/* CONFIG_NET_WIRELESS_RTNETLINK */
					 .dumpit = rtnetlink_dump_ifinfo },
	[RTM_SETLINK     - RTM_BASE] = { .doit   = do_setlink		 },
	[RTM_GETADDR     - RTM_BASE] = { .dumpit = rtnetlink_dump_all	 },
	[RTM_GETROUTE    - RTM_BASE] = { .dumpit = rtnetlink_dump_all	 },
	[RTM_NEWNEIGH    - RTM_BASE] = { .doit   = neigh_add		 },
	[RTM_DELNEIGH    - RTM_BASE] = { .doit   = neigh_delete		 },
	[RTM_GETNEIGH    - RTM_BASE] = { .dumpit = neigh_dump_info	 },
	[RTM_GETRULE     - RTM_BASE] = { .dumpit = rtnetlink_dump_all	 },
	[RTM_GETNEIGHTBL - RTM_BASE] = { .dumpit = neightbl_dump_info	 },
	[RTM_SETNEIGHTBL - RTM_BASE] = { .doit   = neightbl_set		 },
};

static int rtnetlink_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	switch (event) {
	case NETDEV_UNREGISTER:
		rtmsg_ifinfo(RTM_DELLINK, dev, ~0U);
		break;
	case NETDEV_REGISTER:
		rtmsg_ifinfo(RTM_NEWLINK, dev, ~0U);
		break;
	case NETDEV_UP:
	case NETDEV_DOWN:
		rtmsg_ifinfo(RTM_NEWLINK, dev, IFF_UP|IFF_RUNNING);
		break;
	case NETDEV_CHANGE:
	case NETDEV_GOING_DOWN:
		break;
	default:
		rtmsg_ifinfo(RTM_NEWLINK, dev, 0);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block rtnetlink_dev_notifier = {
	.notifier_call	= rtnetlink_event,
};

void __init rtnetlink_init(void)
{
	int i;

	rtattr_max = 0;
	for (i = 0; i < ARRAY_SIZE(rta_max); i++)
		if (rta_max[i] > rtattr_max)
			rtattr_max = rta_max[i];
	rta_buf = kmalloc(rtattr_max * sizeof(struct rtattr *), GFP_KERNEL);
	if (!rta_buf)
		panic("rtnetlink_init: cannot allocate rta_buf\n");

	rtnl = netlink_kernel_create(NETLINK_ROUTE, RTNLGRP_MAX, rtnetlink_rcv,
	                             THIS_MODULE);
	if (rtnl == NULL)
		panic("rtnetlink_init: cannot initialize rtnetlink\n");
	netlink_set_nonroot(NETLINK_ROUTE, NL_NONROOT_RECV);
	register_netdevice_notifier(&rtnetlink_dev_notifier);
	rtnetlink_links[PF_UNSPEC] = link_rtnetlink_table;
	rtnetlink_links[PF_PACKET] = link_rtnetlink_table;
}

EXPORT_SYMBOL(__rta_fill);
EXPORT_SYMBOL(rtattr_strlcpy);
EXPORT_SYMBOL(rtattr_parse);
EXPORT_SYMBOL(rtnetlink_links);
EXPORT_SYMBOL(rtnetlink_put_metrics);
EXPORT_SYMBOL(rtnl);
EXPORT_SYMBOL(rtnl_lock);
EXPORT_SYMBOL(rtnl_trylock);
EXPORT_SYMBOL(rtnl_unlock);
