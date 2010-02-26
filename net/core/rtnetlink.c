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

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
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
#include <linux/if_addr.h>
#include <linux/pci.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/udp.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <net/fib_rules.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>

struct rtnl_link {
	rtnl_doit_func		doit;
	rtnl_dumpit_func	dumpit;
};

static DEFINE_MUTEX(rtnl_mutex);

void rtnl_lock(void)
{
	mutex_lock(&rtnl_mutex);
}
EXPORT_SYMBOL(rtnl_lock);

void __rtnl_unlock(void)
{
	mutex_unlock(&rtnl_mutex);
}

void rtnl_unlock(void)
{
	/* This fellow will unlock it for us. */
	netdev_run_todo();
}
EXPORT_SYMBOL(rtnl_unlock);

int rtnl_trylock(void)
{
	return mutex_trylock(&rtnl_mutex);
}
EXPORT_SYMBOL(rtnl_trylock);

int rtnl_is_locked(void)
{
	return mutex_is_locked(&rtnl_mutex);
}
EXPORT_SYMBOL(rtnl_is_locked);

static struct rtnl_link *rtnl_msg_handlers[NPROTO];

static inline int rtm_msgindex(int msgtype)
{
	int msgindex = msgtype - RTM_BASE;

	/*
	 * msgindex < 0 implies someone tried to register a netlink
	 * control code. msgindex >= RTM_NR_MSGTYPES may indicate that
	 * the message type has not been added to linux/rtnetlink.h
	 */
	BUG_ON(msgindex < 0 || msgindex >= RTM_NR_MSGTYPES);

	return msgindex;
}

static rtnl_doit_func rtnl_get_doit(int protocol, int msgindex)
{
	struct rtnl_link *tab;

	tab = rtnl_msg_handlers[protocol];
	if (tab == NULL || tab[msgindex].doit == NULL)
		tab = rtnl_msg_handlers[PF_UNSPEC];

	return tab ? tab[msgindex].doit : NULL;
}

static rtnl_dumpit_func rtnl_get_dumpit(int protocol, int msgindex)
{
	struct rtnl_link *tab;

	tab = rtnl_msg_handlers[protocol];
	if (tab == NULL || tab[msgindex].dumpit == NULL)
		tab = rtnl_msg_handlers[PF_UNSPEC];

	return tab ? tab[msgindex].dumpit : NULL;
}

/**
 * __rtnl_register - Register a rtnetlink message type
 * @protocol: Protocol family or PF_UNSPEC
 * @msgtype: rtnetlink message type
 * @doit: Function pointer called for each request message
 * @dumpit: Function pointer called for each dump request (NLM_F_DUMP) message
 *
 * Registers the specified function pointers (at least one of them has
 * to be non-NULL) to be called whenever a request message for the
 * specified protocol family and message type is received.
 *
 * The special protocol family PF_UNSPEC may be used to define fallback
 * function pointers for the case when no entry for the specific protocol
 * family exists.
 *
 * Returns 0 on success or a negative error code.
 */
int __rtnl_register(int protocol, int msgtype,
		    rtnl_doit_func doit, rtnl_dumpit_func dumpit)
{
	struct rtnl_link *tab;
	int msgindex;

	BUG_ON(protocol < 0 || protocol >= NPROTO);
	msgindex = rtm_msgindex(msgtype);

	tab = rtnl_msg_handlers[protocol];
	if (tab == NULL) {
		tab = kcalloc(RTM_NR_MSGTYPES, sizeof(*tab), GFP_KERNEL);
		if (tab == NULL)
			return -ENOBUFS;

		rtnl_msg_handlers[protocol] = tab;
	}

	if (doit)
		tab[msgindex].doit = doit;

	if (dumpit)
		tab[msgindex].dumpit = dumpit;

	return 0;
}
EXPORT_SYMBOL_GPL(__rtnl_register);

/**
 * rtnl_register - Register a rtnetlink message type
 *
 * Identical to __rtnl_register() but panics on failure. This is useful
 * as failure of this function is very unlikely, it can only happen due
 * to lack of memory when allocating the chain to store all message
 * handlers for a protocol. Meant for use in init functions where lack
 * of memory implies no sense in continueing.
 */
void rtnl_register(int protocol, int msgtype,
		   rtnl_doit_func doit, rtnl_dumpit_func dumpit)
{
	if (__rtnl_register(protocol, msgtype, doit, dumpit) < 0)
		panic("Unable to register rtnetlink message handler, "
		      "protocol = %d, message type = %d\n",
		      protocol, msgtype);
}
EXPORT_SYMBOL_GPL(rtnl_register);

/**
 * rtnl_unregister - Unregister a rtnetlink message type
 * @protocol: Protocol family or PF_UNSPEC
 * @msgtype: rtnetlink message type
 *
 * Returns 0 on success or a negative error code.
 */
int rtnl_unregister(int protocol, int msgtype)
{
	int msgindex;

	BUG_ON(protocol < 0 || protocol >= NPROTO);
	msgindex = rtm_msgindex(msgtype);

	if (rtnl_msg_handlers[protocol] == NULL)
		return -ENOENT;

	rtnl_msg_handlers[protocol][msgindex].doit = NULL;
	rtnl_msg_handlers[protocol][msgindex].dumpit = NULL;

	return 0;
}
EXPORT_SYMBOL_GPL(rtnl_unregister);

/**
 * rtnl_unregister_all - Unregister all rtnetlink message type of a protocol
 * @protocol : Protocol family or PF_UNSPEC
 *
 * Identical to calling rtnl_unregster() for all registered message types
 * of a certain protocol family.
 */
void rtnl_unregister_all(int protocol)
{
	BUG_ON(protocol < 0 || protocol >= NPROTO);

	kfree(rtnl_msg_handlers[protocol]);
	rtnl_msg_handlers[protocol] = NULL;
}
EXPORT_SYMBOL_GPL(rtnl_unregister_all);

static LIST_HEAD(link_ops);

/**
 * __rtnl_link_register - Register rtnl_link_ops with rtnetlink.
 * @ops: struct rtnl_link_ops * to register
 *
 * The caller must hold the rtnl_mutex. This function should be used
 * by drivers that create devices during module initialization. It
 * must be called before registering the devices.
 *
 * Returns 0 on success or a negative error code.
 */
int __rtnl_link_register(struct rtnl_link_ops *ops)
{
	if (!ops->dellink)
		ops->dellink = unregister_netdevice_queue;

	list_add_tail(&ops->list, &link_ops);
	return 0;
}
EXPORT_SYMBOL_GPL(__rtnl_link_register);

/**
 * rtnl_link_register - Register rtnl_link_ops with rtnetlink.
 * @ops: struct rtnl_link_ops * to register
 *
 * Returns 0 on success or a negative error code.
 */
int rtnl_link_register(struct rtnl_link_ops *ops)
{
	int err;

	rtnl_lock();
	err = __rtnl_link_register(ops);
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL_GPL(rtnl_link_register);

static void __rtnl_kill_links(struct net *net, struct rtnl_link_ops *ops)
{
	struct net_device *dev;
	LIST_HEAD(list_kill);

	for_each_netdev(net, dev) {
		if (dev->rtnl_link_ops == ops)
			ops->dellink(dev, &list_kill);
	}
	unregister_netdevice_many(&list_kill);
}

void rtnl_kill_links(struct net *net, struct rtnl_link_ops *ops)
{
	rtnl_lock();
	__rtnl_kill_links(net, ops);
	rtnl_unlock();
}
EXPORT_SYMBOL_GPL(rtnl_kill_links);

/**
 * __rtnl_link_unregister - Unregister rtnl_link_ops from rtnetlink.
 * @ops: struct rtnl_link_ops * to unregister
 *
 * The caller must hold the rtnl_mutex.
 */
void __rtnl_link_unregister(struct rtnl_link_ops *ops)
{
	struct net *net;

	for_each_net(net) {
		__rtnl_kill_links(net, ops);
	}
	list_del(&ops->list);
}
EXPORT_SYMBOL_GPL(__rtnl_link_unregister);

/**
 * rtnl_link_unregister - Unregister rtnl_link_ops from rtnetlink.
 * @ops: struct rtnl_link_ops * to unregister
 */
void rtnl_link_unregister(struct rtnl_link_ops *ops)
{
	rtnl_lock();
	__rtnl_link_unregister(ops);
	rtnl_unlock();
}
EXPORT_SYMBOL_GPL(rtnl_link_unregister);

static const struct rtnl_link_ops *rtnl_link_ops_get(const char *kind)
{
	const struct rtnl_link_ops *ops;

	list_for_each_entry(ops, &link_ops, list) {
		if (!strcmp(ops->kind, kind))
			return ops;
	}
	return NULL;
}

static size_t rtnl_link_get_size(const struct net_device *dev)
{
	const struct rtnl_link_ops *ops = dev->rtnl_link_ops;
	size_t size;

	if (!ops)
		return 0;

	size = nlmsg_total_size(sizeof(struct nlattr)) + /* IFLA_LINKINFO */
	       nlmsg_total_size(strlen(ops->kind) + 1);	 /* IFLA_INFO_KIND */

	if (ops->get_size)
		/* IFLA_INFO_DATA + nested data */
		size += nlmsg_total_size(sizeof(struct nlattr)) +
			ops->get_size(dev);

	if (ops->get_xstats_size)
		size += ops->get_xstats_size(dev);	/* IFLA_INFO_XSTATS */

	return size;
}

static int rtnl_link_fill(struct sk_buff *skb, const struct net_device *dev)
{
	const struct rtnl_link_ops *ops = dev->rtnl_link_ops;
	struct nlattr *linkinfo, *data;
	int err = -EMSGSIZE;

	linkinfo = nla_nest_start(skb, IFLA_LINKINFO);
	if (linkinfo == NULL)
		goto out;

	if (nla_put_string(skb, IFLA_INFO_KIND, ops->kind) < 0)
		goto err_cancel_link;
	if (ops->fill_xstats) {
		err = ops->fill_xstats(skb, dev);
		if (err < 0)
			goto err_cancel_link;
	}
	if (ops->fill_info) {
		data = nla_nest_start(skb, IFLA_INFO_DATA);
		if (data == NULL)
			goto err_cancel_link;
		err = ops->fill_info(skb, dev);
		if (err < 0)
			goto err_cancel_data;
		nla_nest_end(skb, data);
	}

	nla_nest_end(skb, linkinfo);
	return 0;

err_cancel_data:
	nla_nest_cancel(skb, data);
err_cancel_link:
	nla_nest_cancel(skb, linkinfo);
out:
	return err;
}

static const int rtm_min[RTM_NR_FAMILIES] =
{
	[RTM_FAM(RTM_NEWLINK)]      = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
	[RTM_FAM(RTM_NEWADDR)]      = NLMSG_LENGTH(sizeof(struct ifaddrmsg)),
	[RTM_FAM(RTM_NEWROUTE)]     = NLMSG_LENGTH(sizeof(struct rtmsg)),
	[RTM_FAM(RTM_NEWRULE)]      = NLMSG_LENGTH(sizeof(struct fib_rule_hdr)),
	[RTM_FAM(RTM_NEWQDISC)]     = NLMSG_LENGTH(sizeof(struct tcmsg)),
	[RTM_FAM(RTM_NEWTCLASS)]    = NLMSG_LENGTH(sizeof(struct tcmsg)),
	[RTM_FAM(RTM_NEWTFILTER)]   = NLMSG_LENGTH(sizeof(struct tcmsg)),
	[RTM_FAM(RTM_NEWACTION)]    = NLMSG_LENGTH(sizeof(struct tcamsg)),
	[RTM_FAM(RTM_GETMULTICAST)] = NLMSG_LENGTH(sizeof(struct rtgenmsg)),
	[RTM_FAM(RTM_GETANYCAST)]   = NLMSG_LENGTH(sizeof(struct rtgenmsg)),
};

static const int rta_max[RTM_NR_FAMILIES] =
{
	[RTM_FAM(RTM_NEWLINK)]      = IFLA_MAX,
	[RTM_FAM(RTM_NEWADDR)]      = IFA_MAX,
	[RTM_FAM(RTM_NEWROUTE)]     = RTA_MAX,
	[RTM_FAM(RTM_NEWRULE)]      = FRA_MAX,
	[RTM_FAM(RTM_NEWQDISC)]     = TCA_MAX,
	[RTM_FAM(RTM_NEWTCLASS)]    = TCA_MAX,
	[RTM_FAM(RTM_NEWTFILTER)]   = TCA_MAX,
	[RTM_FAM(RTM_NEWACTION)]    = TCAA_MAX,
};

void __rta_fill(struct sk_buff *skb, int attrtype, int attrlen, const void *data)
{
	struct rtattr *rta;
	int size = RTA_LENGTH(attrlen);

	rta = (struct rtattr *)skb_put(skb, RTA_ALIGN(size));
	rta->rta_type = attrtype;
	rta->rta_len = size;
	memcpy(RTA_DATA(rta), data, attrlen);
	memset(RTA_DATA(rta) + attrlen, 0, RTA_ALIGN(size) - size);
}
EXPORT_SYMBOL(__rta_fill);

int rtnetlink_send(struct sk_buff *skb, struct net *net, u32 pid, unsigned group, int echo)
{
	struct sock *rtnl = net->rtnl;
	int err = 0;

	NETLINK_CB(skb).dst_group = group;
	if (echo)
		atomic_inc(&skb->users);
	netlink_broadcast(rtnl, skb, pid, group, GFP_KERNEL);
	if (echo)
		err = netlink_unicast(rtnl, skb, pid, MSG_DONTWAIT);
	return err;
}

int rtnl_unicast(struct sk_buff *skb, struct net *net, u32 pid)
{
	struct sock *rtnl = net->rtnl;

	return nlmsg_unicast(rtnl, skb, pid);
}
EXPORT_SYMBOL(rtnl_unicast);

void rtnl_notify(struct sk_buff *skb, struct net *net, u32 pid, u32 group,
		 struct nlmsghdr *nlh, gfp_t flags)
{
	struct sock *rtnl = net->rtnl;
	int report = 0;

	if (nlh)
		report = nlmsg_report(nlh);

	nlmsg_notify(rtnl, skb, pid, group, report, flags);
}
EXPORT_SYMBOL(rtnl_notify);

void rtnl_set_sk_err(struct net *net, u32 group, int error)
{
	struct sock *rtnl = net->rtnl;

	netlink_set_err(rtnl, 0, group, error);
}
EXPORT_SYMBOL(rtnl_set_sk_err);

int rtnetlink_put_metrics(struct sk_buff *skb, u32 *metrics)
{
	struct nlattr *mx;
	int i, valid = 0;

	mx = nla_nest_start(skb, RTA_METRICS);
	if (mx == NULL)
		return -ENOBUFS;

	for (i = 0; i < RTAX_MAX; i++) {
		if (metrics[i]) {
			valid++;
			NLA_PUT_U32(skb, i+1, metrics[i]);
		}
	}

	if (!valid) {
		nla_nest_cancel(skb, mx);
		return 0;
	}

	return nla_nest_end(skb, mx);

nla_put_failure:
	nla_nest_cancel(skb, mx);
	return -EMSGSIZE;
}
EXPORT_SYMBOL(rtnetlink_put_metrics);

int rtnl_put_cacheinfo(struct sk_buff *skb, struct dst_entry *dst, u32 id,
		       u32 ts, u32 tsage, long expires, u32 error)
{
	struct rta_cacheinfo ci = {
		.rta_lastuse = jiffies_to_clock_t(jiffies - dst->lastuse),
		.rta_used = dst->__use,
		.rta_clntref = atomic_read(&(dst->__refcnt)),
		.rta_error = error,
		.rta_id =  id,
		.rta_ts = ts,
		.rta_tsage = tsage,
	};

	if (expires)
		ci.rta_expires = jiffies_to_clock_t(expires);

	return nla_put(skb, RTA_CACHEINFO, sizeof(ci), &ci);
}
EXPORT_SYMBOL_GPL(rtnl_put_cacheinfo);

static void set_operstate(struct net_device *dev, unsigned char transition)
{
	unsigned char operstate = dev->operstate;

	switch (transition) {
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
	}

	if (dev->operstate != operstate) {
		write_lock_bh(&dev_base_lock);
		dev->operstate = operstate;
		write_unlock_bh(&dev_base_lock);
		netdev_state_change(dev);
	}
}

static unsigned int rtnl_dev_combine_flags(const struct net_device *dev,
					   const struct ifinfomsg *ifm)
{
	unsigned int flags = ifm->ifi_flags;

	/* bugwards compatibility: ifi_change == 0 is treated as ~0 */
	if (ifm->ifi_change)
		flags = (flags & ifm->ifi_change) |
			(dev->flags & ~ifm->ifi_change);

	return flags;
}

static void copy_rtnl_link_stats(struct rtnl_link_stats *a,
				 const struct net_device_stats *b)
{
	a->rx_packets = b->rx_packets;
	a->tx_packets = b->tx_packets;
	a->rx_bytes = b->rx_bytes;
	a->tx_bytes = b->tx_bytes;
	a->rx_errors = b->rx_errors;
	a->tx_errors = b->tx_errors;
	a->rx_dropped = b->rx_dropped;
	a->tx_dropped = b->tx_dropped;

	a->multicast = b->multicast;
	a->collisions = b->collisions;

	a->rx_length_errors = b->rx_length_errors;
	a->rx_over_errors = b->rx_over_errors;
	a->rx_crc_errors = b->rx_crc_errors;
	a->rx_frame_errors = b->rx_frame_errors;
	a->rx_fifo_errors = b->rx_fifo_errors;
	a->rx_missed_errors = b->rx_missed_errors;

	a->tx_aborted_errors = b->tx_aborted_errors;
	a->tx_carrier_errors = b->tx_carrier_errors;
	a->tx_fifo_errors = b->tx_fifo_errors;
	a->tx_heartbeat_errors = b->tx_heartbeat_errors;
	a->tx_window_errors = b->tx_window_errors;

	a->rx_compressed = b->rx_compressed;
	a->tx_compressed = b->tx_compressed;
};

static inline int rtnl_vfinfo_size(const struct net_device *dev)
{
	if (dev->dev.parent && dev_is_pci(dev->dev.parent))
		return dev_num_vf(dev->dev.parent) *
			sizeof(struct ifla_vf_info);
	else
		return 0;
}

static inline size_t if_nlmsg_size(const struct net_device *dev)
{
	return NLMSG_ALIGN(sizeof(struct ifinfomsg))
	       + nla_total_size(IFNAMSIZ) /* IFLA_IFNAME */
	       + nla_total_size(IFALIASZ) /* IFLA_IFALIAS */
	       + nla_total_size(IFNAMSIZ) /* IFLA_QDISC */
	       + nla_total_size(sizeof(struct rtnl_link_ifmap))
	       + nla_total_size(sizeof(struct rtnl_link_stats))
	       + nla_total_size(MAX_ADDR_LEN) /* IFLA_ADDRESS */
	       + nla_total_size(MAX_ADDR_LEN) /* IFLA_BROADCAST */
	       + nla_total_size(4) /* IFLA_TXQLEN */
	       + nla_total_size(4) /* IFLA_WEIGHT */
	       + nla_total_size(4) /* IFLA_MTU */
	       + nla_total_size(4) /* IFLA_LINK */
	       + nla_total_size(4) /* IFLA_MASTER */
	       + nla_total_size(1) /* IFLA_OPERSTATE */
	       + nla_total_size(1) /* IFLA_LINKMODE */
	       + nla_total_size(4) /* IFLA_NUM_VF */
	       + nla_total_size(rtnl_vfinfo_size(dev)) /* IFLA_VFINFO */
	       + rtnl_link_get_size(dev); /* IFLA_LINKINFO */
}

static int rtnl_fill_ifinfo(struct sk_buff *skb, struct net_device *dev,
			    int type, u32 pid, u32 seq, u32 change,
			    unsigned int flags)
{
	struct ifinfomsg *ifm;
	struct nlmsghdr *nlh;
	const struct net_device_stats *stats;
	struct nlattr *attr;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*ifm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	ifm = nlmsg_data(nlh);
	ifm->ifi_family = AF_UNSPEC;
	ifm->__ifi_pad = 0;
	ifm->ifi_type = dev->type;
	ifm->ifi_index = dev->ifindex;
	ifm->ifi_flags = dev_get_flags(dev);
	ifm->ifi_change = change;

	NLA_PUT_STRING(skb, IFLA_IFNAME, dev->name);
	NLA_PUT_U32(skb, IFLA_TXQLEN, dev->tx_queue_len);
	NLA_PUT_U8(skb, IFLA_OPERSTATE,
		   netif_running(dev) ? dev->operstate : IF_OPER_DOWN);
	NLA_PUT_U8(skb, IFLA_LINKMODE, dev->link_mode);
	NLA_PUT_U32(skb, IFLA_MTU, dev->mtu);

	if (dev->ifindex != dev->iflink)
		NLA_PUT_U32(skb, IFLA_LINK, dev->iflink);

	if (dev->master)
		NLA_PUT_U32(skb, IFLA_MASTER, dev->master->ifindex);

	if (dev->qdisc)
		NLA_PUT_STRING(skb, IFLA_QDISC, dev->qdisc->ops->id);

	if (dev->ifalias)
		NLA_PUT_STRING(skb, IFLA_IFALIAS, dev->ifalias);

	if (1) {
		struct rtnl_link_ifmap map = {
			.mem_start   = dev->mem_start,
			.mem_end     = dev->mem_end,
			.base_addr   = dev->base_addr,
			.irq         = dev->irq,
			.dma         = dev->dma,
			.port        = dev->if_port,
		};
		NLA_PUT(skb, IFLA_MAP, sizeof(map), &map);
	}

	if (dev->addr_len) {
		NLA_PUT(skb, IFLA_ADDRESS, dev->addr_len, dev->dev_addr);
		NLA_PUT(skb, IFLA_BROADCAST, dev->addr_len, dev->broadcast);
	}

	attr = nla_reserve(skb, IFLA_STATS,
			sizeof(struct rtnl_link_stats));
	if (attr == NULL)
		goto nla_put_failure;

	stats = dev_get_stats(dev);
	copy_rtnl_link_stats(nla_data(attr), stats);

	if (dev->netdev_ops->ndo_get_vf_config && dev->dev.parent) {
		int i;
		struct ifla_vf_info ivi;

		NLA_PUT_U32(skb, IFLA_NUM_VF, dev_num_vf(dev->dev.parent));
		for (i = 0; i < dev_num_vf(dev->dev.parent); i++) {
			if (dev->netdev_ops->ndo_get_vf_config(dev, i, &ivi))
				break;
			NLA_PUT(skb, IFLA_VFINFO, sizeof(ivi), &ivi);
		}
	}
	if (dev->rtnl_link_ops) {
		if (rtnl_link_fill(skb, dev) < 0)
			goto nla_put_failure;
	}

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int rtnl_dump_ifinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	int h, s_h;
	int idx = 0, s_idx;
	struct net_device *dev;
	struct hlist_head *head;
	struct hlist_node *node;

	s_h = cb->args[0];
	s_idx = cb->args[1];

	for (h = s_h; h < NETDEV_HASHENTRIES; h++, s_idx = 0) {
		idx = 0;
		head = &net->dev_index_head[h];
		hlist_for_each_entry(dev, node, head, index_hlist) {
			if (idx < s_idx)
				goto cont;
			if (rtnl_fill_ifinfo(skb, dev, RTM_NEWLINK,
					     NETLINK_CB(cb->skb).pid,
					     cb->nlh->nlmsg_seq, 0,
					     NLM_F_MULTI) <= 0)
				goto out;
cont:
			idx++;
		}
	}
out:
	cb->args[1] = idx;
	cb->args[0] = h;

	return skb->len;
}

const struct nla_policy ifla_policy[IFLA_MAX+1] = {
	[IFLA_IFNAME]		= { .type = NLA_STRING, .len = IFNAMSIZ-1 },
	[IFLA_ADDRESS]		= { .type = NLA_BINARY, .len = MAX_ADDR_LEN },
	[IFLA_BROADCAST]	= { .type = NLA_BINARY, .len = MAX_ADDR_LEN },
	[IFLA_MAP]		= { .len = sizeof(struct rtnl_link_ifmap) },
	[IFLA_MTU]		= { .type = NLA_U32 },
	[IFLA_LINK]		= { .type = NLA_U32 },
	[IFLA_TXQLEN]		= { .type = NLA_U32 },
	[IFLA_WEIGHT]		= { .type = NLA_U32 },
	[IFLA_OPERSTATE]	= { .type = NLA_U8 },
	[IFLA_LINKMODE]		= { .type = NLA_U8 },
	[IFLA_LINKINFO]		= { .type = NLA_NESTED },
	[IFLA_NET_NS_PID]	= { .type = NLA_U32 },
	[IFLA_IFALIAS]	        = { .type = NLA_STRING, .len = IFALIASZ-1 },
	[IFLA_VF_MAC]		= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_vf_mac) },
	[IFLA_VF_VLAN]		= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_vf_vlan) },
	[IFLA_VF_TX_RATE]	= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_vf_tx_rate) },
};
EXPORT_SYMBOL(ifla_policy);

static const struct nla_policy ifla_info_policy[IFLA_INFO_MAX+1] = {
	[IFLA_INFO_KIND]	= { .type = NLA_STRING },
	[IFLA_INFO_DATA]	= { .type = NLA_NESTED },
};

struct net *rtnl_link_get_net(struct net *src_net, struct nlattr *tb[])
{
	struct net *net;
	/* Examine the link attributes and figure out which
	 * network namespace we are talking about.
	 */
	if (tb[IFLA_NET_NS_PID])
		net = get_net_ns_by_pid(nla_get_u32(tb[IFLA_NET_NS_PID]));
	else
		net = get_net(src_net);
	return net;
}
EXPORT_SYMBOL(rtnl_link_get_net);

static int validate_linkmsg(struct net_device *dev, struct nlattr *tb[])
{
	if (dev) {
		if (tb[IFLA_ADDRESS] &&
		    nla_len(tb[IFLA_ADDRESS]) < dev->addr_len)
			return -EINVAL;

		if (tb[IFLA_BROADCAST] &&
		    nla_len(tb[IFLA_BROADCAST]) < dev->addr_len)
			return -EINVAL;
	}

	return 0;
}

static int do_setlink(struct net_device *dev, struct ifinfomsg *ifm,
		      struct nlattr **tb, char *ifname, int modified)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int send_addr_notify = 0;
	int err;

	if (tb[IFLA_NET_NS_PID]) {
		struct net *net = rtnl_link_get_net(dev_net(dev), tb);
		if (IS_ERR(net)) {
			err = PTR_ERR(net);
			goto errout;
		}
		err = dev_change_net_namespace(dev, net, ifname);
		put_net(net);
		if (err)
			goto errout;
		modified = 1;
	}

	if (tb[IFLA_MAP]) {
		struct rtnl_link_ifmap *u_map;
		struct ifmap k_map;

		if (!ops->ndo_set_config) {
			err = -EOPNOTSUPP;
			goto errout;
		}

		if (!netif_device_present(dev)) {
			err = -ENODEV;
			goto errout;
		}

		u_map = nla_data(tb[IFLA_MAP]);
		k_map.mem_start = (unsigned long) u_map->mem_start;
		k_map.mem_end = (unsigned long) u_map->mem_end;
		k_map.base_addr = (unsigned short) u_map->base_addr;
		k_map.irq = (unsigned char) u_map->irq;
		k_map.dma = (unsigned char) u_map->dma;
		k_map.port = (unsigned char) u_map->port;

		err = ops->ndo_set_config(dev, &k_map);
		if (err < 0)
			goto errout;

		modified = 1;
	}

	if (tb[IFLA_ADDRESS]) {
		struct sockaddr *sa;
		int len;

		if (!ops->ndo_set_mac_address) {
			err = -EOPNOTSUPP;
			goto errout;
		}

		if (!netif_device_present(dev)) {
			err = -ENODEV;
			goto errout;
		}

		len = sizeof(sa_family_t) + dev->addr_len;
		sa = kmalloc(len, GFP_KERNEL);
		if (!sa) {
			err = -ENOMEM;
			goto errout;
		}
		sa->sa_family = dev->type;
		memcpy(sa->sa_data, nla_data(tb[IFLA_ADDRESS]),
		       dev->addr_len);
		err = ops->ndo_set_mac_address(dev, sa);
		kfree(sa);
		if (err)
			goto errout;
		send_addr_notify = 1;
		modified = 1;
	}

	if (tb[IFLA_MTU]) {
		err = dev_set_mtu(dev, nla_get_u32(tb[IFLA_MTU]));
		if (err < 0)
			goto errout;
		modified = 1;
	}

	/*
	 * Interface selected by interface index but interface
	 * name provided implies that a name change has been
	 * requested.
	 */
	if (ifm->ifi_index > 0 && ifname[0]) {
		err = dev_change_name(dev, ifname);
		if (err < 0)
			goto errout;
		modified = 1;
	}

	if (tb[IFLA_IFALIAS]) {
		err = dev_set_alias(dev, nla_data(tb[IFLA_IFALIAS]),
				    nla_len(tb[IFLA_IFALIAS]));
		if (err < 0)
			goto errout;
		modified = 1;
	}

	if (tb[IFLA_BROADCAST]) {
		nla_memcpy(dev->broadcast, tb[IFLA_BROADCAST], dev->addr_len);
		send_addr_notify = 1;
	}

	if (ifm->ifi_flags || ifm->ifi_change) {
		err = dev_change_flags(dev, rtnl_dev_combine_flags(dev, ifm));
		if (err < 0)
			goto errout;
	}

	if (tb[IFLA_TXQLEN])
		dev->tx_queue_len = nla_get_u32(tb[IFLA_TXQLEN]);

	if (tb[IFLA_OPERSTATE])
		set_operstate(dev, nla_get_u8(tb[IFLA_OPERSTATE]));

	if (tb[IFLA_LINKMODE]) {
		write_lock_bh(&dev_base_lock);
		dev->link_mode = nla_get_u8(tb[IFLA_LINKMODE]);
		write_unlock_bh(&dev_base_lock);
	}

	if (tb[IFLA_VF_MAC]) {
		struct ifla_vf_mac *ivm;
		ivm = nla_data(tb[IFLA_VF_MAC]);
		err = -EOPNOTSUPP;
		if (ops->ndo_set_vf_mac)
			err = ops->ndo_set_vf_mac(dev, ivm->vf, ivm->mac);
		if (err < 0)
			goto errout;
		modified = 1;
	}

	if (tb[IFLA_VF_VLAN]) {
		struct ifla_vf_vlan *ivv;
		ivv = nla_data(tb[IFLA_VF_VLAN]);
		err = -EOPNOTSUPP;
		if (ops->ndo_set_vf_vlan)
			err = ops->ndo_set_vf_vlan(dev, ivv->vf,
						   ivv->vlan,
						   ivv->qos);
		if (err < 0)
			goto errout;
		modified = 1;
	}
	err = 0;

	if (tb[IFLA_VF_TX_RATE]) {
		struct ifla_vf_tx_rate *ivt;
		ivt = nla_data(tb[IFLA_VF_TX_RATE]);
		err = -EOPNOTSUPP;
		if (ops->ndo_set_vf_tx_rate)
			err = ops->ndo_set_vf_tx_rate(dev, ivt->vf, ivt->rate);
		if (err < 0)
			goto errout;
		modified = 1;
	}
	err = 0;

errout:
	if (err < 0 && modified && net_ratelimit())
		printk(KERN_WARNING "A link change request failed with "
		       "some changes comitted already. Interface %s may "
		       "have been left with an inconsistent configuration, "
		       "please check.\n", dev->name);

	if (send_addr_notify)
		call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
	return err;
}

static int rtnl_setlink(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct ifinfomsg *ifm;
	struct net_device *dev;
	int err;
	struct nlattr *tb[IFLA_MAX+1];
	char ifname[IFNAMSIZ];

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFLA_MAX, ifla_policy);
	if (err < 0)
		goto errout;

	if (tb[IFLA_IFNAME])
		nla_strlcpy(ifname, tb[IFLA_IFNAME], IFNAMSIZ);
	else
		ifname[0] = '\0';

	err = -EINVAL;
	ifm = nlmsg_data(nlh);
	if (ifm->ifi_index > 0)
		dev = __dev_get_by_index(net, ifm->ifi_index);
	else if (tb[IFLA_IFNAME])
		dev = __dev_get_by_name(net, ifname);
	else
		goto errout;

	if (dev == NULL) {
		err = -ENODEV;
		goto errout;
	}

	err = validate_linkmsg(dev, tb);
	if (err < 0)
		goto errout;

	err = do_setlink(dev, ifm, tb, ifname, 0);
errout:
	return err;
}

static int rtnl_dellink(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	const struct rtnl_link_ops *ops;
	struct net_device *dev;
	struct ifinfomsg *ifm;
	char ifname[IFNAMSIZ];
	struct nlattr *tb[IFLA_MAX+1];
	int err;

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFLA_MAX, ifla_policy);
	if (err < 0)
		return err;

	if (tb[IFLA_IFNAME])
		nla_strlcpy(ifname, tb[IFLA_IFNAME], IFNAMSIZ);

	ifm = nlmsg_data(nlh);
	if (ifm->ifi_index > 0)
		dev = __dev_get_by_index(net, ifm->ifi_index);
	else if (tb[IFLA_IFNAME])
		dev = __dev_get_by_name(net, ifname);
	else
		return -EINVAL;

	if (!dev)
		return -ENODEV;

	ops = dev->rtnl_link_ops;
	if (!ops)
		return -EOPNOTSUPP;

	ops->dellink(dev, NULL);
	return 0;
}

int rtnl_configure_link(struct net_device *dev, const struct ifinfomsg *ifm)
{
	unsigned int old_flags;
	int err;

	old_flags = dev->flags;
	if (ifm && (ifm->ifi_flags || ifm->ifi_change)) {
		err = __dev_change_flags(dev, rtnl_dev_combine_flags(dev, ifm));
		if (err < 0)
			return err;
	}

	dev->rtnl_link_state = RTNL_LINK_INITIALIZED;
	rtmsg_ifinfo(RTM_NEWLINK, dev, ~0U);

	__dev_notify_flags(dev, old_flags);
	return 0;
}
EXPORT_SYMBOL(rtnl_configure_link);

struct net_device *rtnl_create_link(struct net *src_net, struct net *net,
	char *ifname, const struct rtnl_link_ops *ops, struct nlattr *tb[])
{
	int err;
	struct net_device *dev;
	unsigned int num_queues = 1;
	unsigned int real_num_queues = 1;

	if (ops->get_tx_queues) {
		err = ops->get_tx_queues(src_net, tb, &num_queues,
					 &real_num_queues);
		if (err)
			goto err;
	}
	err = -ENOMEM;
	dev = alloc_netdev_mq(ops->priv_size, ifname, ops->setup, num_queues);
	if (!dev)
		goto err;

	dev_net_set(dev, net);
	dev->rtnl_link_ops = ops;
	dev->rtnl_link_state = RTNL_LINK_INITIALIZING;
	dev->real_num_tx_queues = real_num_queues;

	if (strchr(dev->name, '%')) {
		err = dev_alloc_name(dev, dev->name);
		if (err < 0)
			goto err_free;
	}

	if (tb[IFLA_MTU])
		dev->mtu = nla_get_u32(tb[IFLA_MTU]);
	if (tb[IFLA_ADDRESS])
		memcpy(dev->dev_addr, nla_data(tb[IFLA_ADDRESS]),
				nla_len(tb[IFLA_ADDRESS]));
	if (tb[IFLA_BROADCAST])
		memcpy(dev->broadcast, nla_data(tb[IFLA_BROADCAST]),
				nla_len(tb[IFLA_BROADCAST]));
	if (tb[IFLA_TXQLEN])
		dev->tx_queue_len = nla_get_u32(tb[IFLA_TXQLEN]);
	if (tb[IFLA_OPERSTATE])
		set_operstate(dev, nla_get_u8(tb[IFLA_OPERSTATE]));
	if (tb[IFLA_LINKMODE])
		dev->link_mode = nla_get_u8(tb[IFLA_LINKMODE]);

	return dev;

err_free:
	free_netdev(dev);
err:
	return ERR_PTR(err);
}
EXPORT_SYMBOL(rtnl_create_link);

static int rtnl_newlink(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	const struct rtnl_link_ops *ops;
	struct net_device *dev;
	struct ifinfomsg *ifm;
	char kind[MODULE_NAME_LEN];
	char ifname[IFNAMSIZ];
	struct nlattr *tb[IFLA_MAX+1];
	struct nlattr *linkinfo[IFLA_INFO_MAX+1];
	int err;

#ifdef CONFIG_MODULES
replay:
#endif
	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFLA_MAX, ifla_policy);
	if (err < 0)
		return err;

	if (tb[IFLA_IFNAME])
		nla_strlcpy(ifname, tb[IFLA_IFNAME], IFNAMSIZ);
	else
		ifname[0] = '\0';

	ifm = nlmsg_data(nlh);
	if (ifm->ifi_index > 0)
		dev = __dev_get_by_index(net, ifm->ifi_index);
	else if (ifname[0])
		dev = __dev_get_by_name(net, ifname);
	else
		dev = NULL;

	err = validate_linkmsg(dev, tb);
	if (err < 0)
		return err;

	if (tb[IFLA_LINKINFO]) {
		err = nla_parse_nested(linkinfo, IFLA_INFO_MAX,
				       tb[IFLA_LINKINFO], ifla_info_policy);
		if (err < 0)
			return err;
	} else
		memset(linkinfo, 0, sizeof(linkinfo));

	if (linkinfo[IFLA_INFO_KIND]) {
		nla_strlcpy(kind, linkinfo[IFLA_INFO_KIND], sizeof(kind));
		ops = rtnl_link_ops_get(kind);
	} else {
		kind[0] = '\0';
		ops = NULL;
	}

	if (1) {
		struct nlattr *attr[ops ? ops->maxtype + 1 : 0], **data = NULL;
		struct net *dest_net;

		if (ops) {
			if (ops->maxtype && linkinfo[IFLA_INFO_DATA]) {
				err = nla_parse_nested(attr, ops->maxtype,
						       linkinfo[IFLA_INFO_DATA],
						       ops->policy);
				if (err < 0)
					return err;
				data = attr;
			}
			if (ops->validate) {
				err = ops->validate(tb, data);
				if (err < 0)
					return err;
			}
		}

		if (dev) {
			int modified = 0;

			if (nlh->nlmsg_flags & NLM_F_EXCL)
				return -EEXIST;
			if (nlh->nlmsg_flags & NLM_F_REPLACE)
				return -EOPNOTSUPP;

			if (linkinfo[IFLA_INFO_DATA]) {
				if (!ops || ops != dev->rtnl_link_ops ||
				    !ops->changelink)
					return -EOPNOTSUPP;

				err = ops->changelink(dev, tb, data);
				if (err < 0)
					return err;
				modified = 1;
			}

			return do_setlink(dev, ifm, tb, ifname, modified);
		}

		if (!(nlh->nlmsg_flags & NLM_F_CREATE))
			return -ENODEV;

		if (ifm->ifi_index)
			return -EOPNOTSUPP;
		if (tb[IFLA_MAP] || tb[IFLA_MASTER] || tb[IFLA_PROTINFO])
			return -EOPNOTSUPP;

		if (!ops) {
#ifdef CONFIG_MODULES
			if (kind[0]) {
				__rtnl_unlock();
				request_module("rtnl-link-%s", kind);
				rtnl_lock();
				ops = rtnl_link_ops_get(kind);
				if (ops)
					goto replay;
			}
#endif
			return -EOPNOTSUPP;
		}

		if (!ifname[0])
			snprintf(ifname, IFNAMSIZ, "%s%%d", ops->kind);

		dest_net = rtnl_link_get_net(net, tb);
		dev = rtnl_create_link(net, dest_net, ifname, ops, tb);

		if (IS_ERR(dev))
			err = PTR_ERR(dev);
		else if (ops->newlink)
			err = ops->newlink(net, dev, tb, data);
		else
			err = register_netdevice(dev);
		if (err < 0 && !IS_ERR(dev)) {
			free_netdev(dev);
			goto out;
		}

		err = rtnl_configure_link(dev, ifm);
		if (err < 0)
			unregister_netdevice(dev);
out:
		put_net(dest_net);
		return err;
	}
}

static int rtnl_getlink(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct ifinfomsg *ifm;
	char ifname[IFNAMSIZ];
	struct nlattr *tb[IFLA_MAX+1];
	struct net_device *dev = NULL;
	struct sk_buff *nskb;
	int err;

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFLA_MAX, ifla_policy);
	if (err < 0)
		return err;

	if (tb[IFLA_IFNAME])
		nla_strlcpy(ifname, tb[IFLA_IFNAME], IFNAMSIZ);

	ifm = nlmsg_data(nlh);
	if (ifm->ifi_index > 0)
		dev = __dev_get_by_index(net, ifm->ifi_index);
	else if (tb[IFLA_IFNAME])
		dev = __dev_get_by_name(net, ifname);
	else
		return -EINVAL;

	if (dev == NULL)
		return -ENODEV;

	nskb = nlmsg_new(if_nlmsg_size(dev), GFP_KERNEL);
	if (nskb == NULL)
		return -ENOBUFS;

	err = rtnl_fill_ifinfo(nskb, dev, RTM_NEWLINK, NETLINK_CB(skb).pid,
			       nlh->nlmsg_seq, 0, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in if_nlmsg_size */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(nskb);
	} else
		err = rtnl_unicast(nskb, net, NETLINK_CB(skb).pid);

	return err;
}

static int rtnl_dump_all(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx;
	int s_idx = cb->family;

	if (s_idx == 0)
		s_idx = 1;
	for (idx = 1; idx < NPROTO; idx++) {
		int type = cb->nlh->nlmsg_type-RTM_BASE;
		if (idx < s_idx || idx == PF_PACKET)
			continue;
		if (rtnl_msg_handlers[idx] == NULL ||
		    rtnl_msg_handlers[idx][type].dumpit == NULL)
			continue;
		if (idx > s_idx)
			memset(&cb->args[0], 0, sizeof(cb->args));
		if (rtnl_msg_handlers[idx][type].dumpit(skb, cb))
			break;
	}
	cb->family = idx;

	return skb->len;
}

void rtmsg_ifinfo(int type, struct net_device *dev, unsigned change)
{
	struct net *net = dev_net(dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(if_nlmsg_size(dev), GFP_KERNEL);
	if (skb == NULL)
		goto errout;

	err = rtnl_fill_ifinfo(skb, dev, type, 0, 0, change, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in if_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, 0, RTNLGRP_LINK, NULL, GFP_KERNEL);
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_LINK, err);
}

/* Protected by RTNL sempahore.  */
static struct rtattr **rta_buf;
static int rtattr_max;

/* Process one rtnetlink message. */

static int rtnetlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	rtnl_doit_func doit;
	int sz_idx, kind;
	int min_len;
	int family;
	int type;
	int err;

	type = nlh->nlmsg_type;
	if (type > RTM_MAX)
		return -EOPNOTSUPP;

	type -= RTM_BASE;

	/* All the messages must have at least 1 byte length */
	if (nlh->nlmsg_len < NLMSG_LENGTH(sizeof(struct rtgenmsg)))
		return 0;

	family = ((struct rtgenmsg *)NLMSG_DATA(nlh))->rtgen_family;
	if (family >= NPROTO)
		return -EAFNOSUPPORT;

	sz_idx = type>>2;
	kind = type&3;

	if (kind != 2 && security_netlink_recv(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (kind == 2 && nlh->nlmsg_flags&NLM_F_DUMP) {
		struct sock *rtnl;
		rtnl_dumpit_func dumpit;

		dumpit = rtnl_get_dumpit(family, type);
		if (dumpit == NULL)
			return -EOPNOTSUPP;

		__rtnl_unlock();
		rtnl = net->rtnl;
		err = netlink_dump_start(rtnl, skb, nlh, dumpit, NULL);
		rtnl_lock();
		return err;
	}

	memset(rta_buf, 0, (rtattr_max * sizeof(struct rtattr *)));

	min_len = rtm_min[sz_idx];
	if (nlh->nlmsg_len < min_len)
		return -EINVAL;

	if (nlh->nlmsg_len > min_len) {
		int attrlen = nlh->nlmsg_len - NLMSG_ALIGN(min_len);
		struct rtattr *attr = (void *)nlh + NLMSG_ALIGN(min_len);

		while (RTA_OK(attr, attrlen)) {
			unsigned flavor = attr->rta_type;
			if (flavor) {
				if (flavor > rta_max[sz_idx])
					return -EINVAL;
				rta_buf[flavor-1] = attr;
			}
			attr = RTA_NEXT(attr, attrlen);
		}
	}

	doit = rtnl_get_doit(family, type);
	if (doit == NULL)
		return -EOPNOTSUPP;

	return doit(skb, nlh, (void *)&rta_buf[0]);
}

static void rtnetlink_rcv(struct sk_buff *skb)
{
	rtnl_lock();
	netlink_rcv_skb(skb, &rtnetlink_rcv_msg);
	rtnl_unlock();
}

static int rtnetlink_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;

	switch (event) {
	case NETDEV_UP:
	case NETDEV_DOWN:
	case NETDEV_PRE_UP:
	case NETDEV_POST_INIT:
	case NETDEV_REGISTER:
	case NETDEV_CHANGE:
	case NETDEV_GOING_DOWN:
	case NETDEV_UNREGISTER:
	case NETDEV_UNREGISTER_BATCH:
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


static int __net_init rtnetlink_net_init(struct net *net)
{
	struct sock *sk;
	sk = netlink_kernel_create(net, NETLINK_ROUTE, RTNLGRP_MAX,
				   rtnetlink_rcv, &rtnl_mutex, THIS_MODULE);
	if (!sk)
		return -ENOMEM;
	net->rtnl = sk;
	return 0;
}

static void __net_exit rtnetlink_net_exit(struct net *net)
{
	netlink_kernel_release(net->rtnl);
	net->rtnl = NULL;
}

static struct pernet_operations rtnetlink_net_ops = {
	.init = rtnetlink_net_init,
	.exit = rtnetlink_net_exit,
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

	if (register_pernet_subsys(&rtnetlink_net_ops))
		panic("rtnetlink_init: cannot initialize rtnetlink\n");

	netlink_set_nonroot(NETLINK_ROUTE, NL_NONROOT_RECV);
	register_netdevice_notifier(&rtnetlink_dev_notifier);

	rtnl_register(PF_UNSPEC, RTM_GETLINK, rtnl_getlink, rtnl_dump_ifinfo);
	rtnl_register(PF_UNSPEC, RTM_SETLINK, rtnl_setlink, NULL);
	rtnl_register(PF_UNSPEC, RTM_NEWLINK, rtnl_newlink, NULL);
	rtnl_register(PF_UNSPEC, RTM_DELLINK, rtnl_dellink, NULL);

	rtnl_register(PF_UNSPEC, RTM_GETADDR, NULL, rtnl_dump_all);
	rtnl_register(PF_UNSPEC, RTM_GETROUTE, NULL, rtnl_dump_all);
}

