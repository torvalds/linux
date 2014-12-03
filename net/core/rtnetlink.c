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
#include <linux/if_bridge.h>
#include <linux/if_vlan.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>

#include <asm/uaccess.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/switchdev.h>
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
	rtnl_calcit_func 	calcit;
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

#ifdef CONFIG_PROVE_LOCKING
int lockdep_rtnl_is_held(void)
{
	return lockdep_is_held(&rtnl_mutex);
}
EXPORT_SYMBOL(lockdep_rtnl_is_held);
#endif /* #ifdef CONFIG_PROVE_LOCKING */

static struct rtnl_link *rtnl_msg_handlers[RTNL_FAMILY_MAX + 1];

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

	if (protocol <= RTNL_FAMILY_MAX)
		tab = rtnl_msg_handlers[protocol];
	else
		tab = NULL;

	if (tab == NULL || tab[msgindex].doit == NULL)
		tab = rtnl_msg_handlers[PF_UNSPEC];

	return tab[msgindex].doit;
}

static rtnl_dumpit_func rtnl_get_dumpit(int protocol, int msgindex)
{
	struct rtnl_link *tab;

	if (protocol <= RTNL_FAMILY_MAX)
		tab = rtnl_msg_handlers[protocol];
	else
		tab = NULL;

	if (tab == NULL || tab[msgindex].dumpit == NULL)
		tab = rtnl_msg_handlers[PF_UNSPEC];

	return tab[msgindex].dumpit;
}

static rtnl_calcit_func rtnl_get_calcit(int protocol, int msgindex)
{
	struct rtnl_link *tab;

	if (protocol <= RTNL_FAMILY_MAX)
		tab = rtnl_msg_handlers[protocol];
	else
		tab = NULL;

	if (tab == NULL || tab[msgindex].calcit == NULL)
		tab = rtnl_msg_handlers[PF_UNSPEC];

	return tab[msgindex].calcit;
}

/**
 * __rtnl_register - Register a rtnetlink message type
 * @protocol: Protocol family or PF_UNSPEC
 * @msgtype: rtnetlink message type
 * @doit: Function pointer called for each request message
 * @dumpit: Function pointer called for each dump request (NLM_F_DUMP) message
 * @calcit: Function pointer to calc size of dump message
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
		    rtnl_doit_func doit, rtnl_dumpit_func dumpit,
		    rtnl_calcit_func calcit)
{
	struct rtnl_link *tab;
	int msgindex;

	BUG_ON(protocol < 0 || protocol > RTNL_FAMILY_MAX);
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

	if (calcit)
		tab[msgindex].calcit = calcit;

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
 * of memory implies no sense in continuing.
 */
void rtnl_register(int protocol, int msgtype,
		   rtnl_doit_func doit, rtnl_dumpit_func dumpit,
		   rtnl_calcit_func calcit)
{
	if (__rtnl_register(protocol, msgtype, doit, dumpit, calcit) < 0)
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

	BUG_ON(protocol < 0 || protocol > RTNL_FAMILY_MAX);
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
	BUG_ON(protocol < 0 || protocol > RTNL_FAMILY_MAX);

	kfree(rtnl_msg_handlers[protocol]);
	rtnl_msg_handlers[protocol] = NULL;
}
EXPORT_SYMBOL_GPL(rtnl_unregister_all);

static LIST_HEAD(link_ops);

static const struct rtnl_link_ops *rtnl_link_ops_get(const char *kind)
{
	const struct rtnl_link_ops *ops;

	list_for_each_entry(ops, &link_ops, list) {
		if (!strcmp(ops->kind, kind))
			return ops;
	}
	return NULL;
}

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
	if (rtnl_link_ops_get(ops->kind))
		return -EEXIST;

	/* The check for setup is here because if ops
	 * does not have that filled up, it is not possible
	 * to use the ops for creating device. So do not
	 * fill up dellink as well. That disables rtnl_dellink.
	 */
	if (ops->setup && !ops->dellink)
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

/* Return with the rtnl_lock held when there are no network
 * devices unregistering in any network namespace.
 */
static void rtnl_lock_unregistering_all(void)
{
	struct net *net;
	bool unregistering;
	DEFINE_WAIT(wait);

	for (;;) {
		prepare_to_wait(&netdev_unregistering_wq, &wait,
				TASK_UNINTERRUPTIBLE);
		unregistering = false;
		rtnl_lock();
		for_each_net(net) {
			if (net->dev_unreg_count > 0) {
				unregistering = true;
				break;
			}
		}
		if (!unregistering)
			break;
		__rtnl_unlock();
		schedule();
	}
	finish_wait(&netdev_unregistering_wq, &wait);
}

/**
 * rtnl_link_unregister - Unregister rtnl_link_ops from rtnetlink.
 * @ops: struct rtnl_link_ops * to unregister
 */
void rtnl_link_unregister(struct rtnl_link_ops *ops)
{
	/* Close the race with cleanup_net() */
	mutex_lock(&net_mutex);
	rtnl_lock_unregistering_all();
	__rtnl_link_unregister(ops);
	rtnl_unlock();
	mutex_unlock(&net_mutex);
}
EXPORT_SYMBOL_GPL(rtnl_link_unregister);

static size_t rtnl_link_get_slave_info_data_size(const struct net_device *dev)
{
	struct net_device *master_dev;
	const struct rtnl_link_ops *ops;

	master_dev = netdev_master_upper_dev_get((struct net_device *) dev);
	if (!master_dev)
		return 0;
	ops = master_dev->rtnl_link_ops;
	if (!ops || !ops->get_slave_size)
		return 0;
	/* IFLA_INFO_SLAVE_DATA + nested data */
	return nla_total_size(sizeof(struct nlattr)) +
	       ops->get_slave_size(master_dev, dev);
}

static size_t rtnl_link_get_size(const struct net_device *dev)
{
	const struct rtnl_link_ops *ops = dev->rtnl_link_ops;
	size_t size;

	if (!ops)
		return 0;

	size = nla_total_size(sizeof(struct nlattr)) + /* IFLA_LINKINFO */
	       nla_total_size(strlen(ops->kind) + 1);  /* IFLA_INFO_KIND */

	if (ops->get_size)
		/* IFLA_INFO_DATA + nested data */
		size += nla_total_size(sizeof(struct nlattr)) +
			ops->get_size(dev);

	if (ops->get_xstats_size)
		/* IFLA_INFO_XSTATS */
		size += nla_total_size(ops->get_xstats_size(dev));

	size += rtnl_link_get_slave_info_data_size(dev);

	return size;
}

static LIST_HEAD(rtnl_af_ops);

static const struct rtnl_af_ops *rtnl_af_lookup(const int family)
{
	const struct rtnl_af_ops *ops;

	list_for_each_entry(ops, &rtnl_af_ops, list) {
		if (ops->family == family)
			return ops;
	}

	return NULL;
}

/**
 * rtnl_af_register - Register rtnl_af_ops with rtnetlink.
 * @ops: struct rtnl_af_ops * to register
 *
 * Returns 0 on success or a negative error code.
 */
void rtnl_af_register(struct rtnl_af_ops *ops)
{
	rtnl_lock();
	list_add_tail(&ops->list, &rtnl_af_ops);
	rtnl_unlock();
}
EXPORT_SYMBOL_GPL(rtnl_af_register);

/**
 * __rtnl_af_unregister - Unregister rtnl_af_ops from rtnetlink.
 * @ops: struct rtnl_af_ops * to unregister
 *
 * The caller must hold the rtnl_mutex.
 */
void __rtnl_af_unregister(struct rtnl_af_ops *ops)
{
	list_del(&ops->list);
}
EXPORT_SYMBOL_GPL(__rtnl_af_unregister);

/**
 * rtnl_af_unregister - Unregister rtnl_af_ops from rtnetlink.
 * @ops: struct rtnl_af_ops * to unregister
 */
void rtnl_af_unregister(struct rtnl_af_ops *ops)
{
	rtnl_lock();
	__rtnl_af_unregister(ops);
	rtnl_unlock();
}
EXPORT_SYMBOL_GPL(rtnl_af_unregister);

static size_t rtnl_link_get_af_size(const struct net_device *dev)
{
	struct rtnl_af_ops *af_ops;
	size_t size;

	/* IFLA_AF_SPEC */
	size = nla_total_size(sizeof(struct nlattr));

	list_for_each_entry(af_ops, &rtnl_af_ops, list) {
		if (af_ops->get_link_af_size) {
			/* AF_* + nested data */
			size += nla_total_size(sizeof(struct nlattr)) +
				af_ops->get_link_af_size(dev);
		}
	}

	return size;
}

static bool rtnl_have_link_slave_info(const struct net_device *dev)
{
	struct net_device *master_dev;

	master_dev = netdev_master_upper_dev_get((struct net_device *) dev);
	if (master_dev && master_dev->rtnl_link_ops)
		return true;
	return false;
}

static int rtnl_link_slave_info_fill(struct sk_buff *skb,
				     const struct net_device *dev)
{
	struct net_device *master_dev;
	const struct rtnl_link_ops *ops;
	struct nlattr *slave_data;
	int err;

	master_dev = netdev_master_upper_dev_get((struct net_device *) dev);
	if (!master_dev)
		return 0;
	ops = master_dev->rtnl_link_ops;
	if (!ops)
		return 0;
	if (nla_put_string(skb, IFLA_INFO_SLAVE_KIND, ops->kind) < 0)
		return -EMSGSIZE;
	if (ops->fill_slave_info) {
		slave_data = nla_nest_start(skb, IFLA_INFO_SLAVE_DATA);
		if (!slave_data)
			return -EMSGSIZE;
		err = ops->fill_slave_info(skb, master_dev, dev);
		if (err < 0)
			goto err_cancel_slave_data;
		nla_nest_end(skb, slave_data);
	}
	return 0;

err_cancel_slave_data:
	nla_nest_cancel(skb, slave_data);
	return err;
}

static int rtnl_link_info_fill(struct sk_buff *skb,
			       const struct net_device *dev)
{
	const struct rtnl_link_ops *ops = dev->rtnl_link_ops;
	struct nlattr *data;
	int err;

	if (!ops)
		return 0;
	if (nla_put_string(skb, IFLA_INFO_KIND, ops->kind) < 0)
		return -EMSGSIZE;
	if (ops->fill_xstats) {
		err = ops->fill_xstats(skb, dev);
		if (err < 0)
			return err;
	}
	if (ops->fill_info) {
		data = nla_nest_start(skb, IFLA_INFO_DATA);
		if (data == NULL)
			return -EMSGSIZE;
		err = ops->fill_info(skb, dev);
		if (err < 0)
			goto err_cancel_data;
		nla_nest_end(skb, data);
	}
	return 0;

err_cancel_data:
	nla_nest_cancel(skb, data);
	return err;
}

static int rtnl_link_fill(struct sk_buff *skb, const struct net_device *dev)
{
	struct nlattr *linkinfo;
	int err = -EMSGSIZE;

	linkinfo = nla_nest_start(skb, IFLA_LINKINFO);
	if (linkinfo == NULL)
		goto out;

	err = rtnl_link_info_fill(skb, dev);
	if (err < 0)
		goto err_cancel_link;

	err = rtnl_link_slave_info_fill(skb, dev);
	if (err < 0)
		goto err_cancel_link;

	nla_nest_end(skb, linkinfo);
	return 0;

err_cancel_link:
	nla_nest_cancel(skb, linkinfo);
out:
	return err;
}

int rtnetlink_send(struct sk_buff *skb, struct net *net, u32 pid, unsigned int group, int echo)
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
			if (nla_put_u32(skb, i+1, metrics[i]))
				goto nla_put_failure;
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
		       long expires, u32 error)
{
	struct rta_cacheinfo ci = {
		.rta_lastuse = jiffies_delta_to_clock_t(jiffies - dst->lastuse),
		.rta_used = dst->__use,
		.rta_clntref = atomic_read(&(dst->__refcnt)),
		.rta_error = error,
		.rta_id =  id,
	};

	if (expires) {
		unsigned long clock;

		clock = jiffies_to_clock_t(abs(expires));
		clock = min_t(unsigned long, clock, INT_MAX);
		ci.rta_expires = (expires > 0) ? clock : -clock;
	}
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

static unsigned int rtnl_dev_get_flags(const struct net_device *dev)
{
	return (dev->flags & ~(IFF_PROMISC | IFF_ALLMULTI)) |
	       (dev->gflags & (IFF_PROMISC | IFF_ALLMULTI));
}

static unsigned int rtnl_dev_combine_flags(const struct net_device *dev,
					   const struct ifinfomsg *ifm)
{
	unsigned int flags = ifm->ifi_flags;

	/* bugwards compatibility: ifi_change == 0 is treated as ~0 */
	if (ifm->ifi_change)
		flags = (flags & ifm->ifi_change) |
			(rtnl_dev_get_flags(dev) & ~ifm->ifi_change);

	return flags;
}

static void copy_rtnl_link_stats(struct rtnl_link_stats *a,
				 const struct rtnl_link_stats64 *b)
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
}

static void copy_rtnl_link_stats64(void *v, const struct rtnl_link_stats64 *b)
{
	memcpy(v, b, sizeof(*b));
}

/* All VF info */
static inline int rtnl_vfinfo_size(const struct net_device *dev,
				   u32 ext_filter_mask)
{
	if (dev->dev.parent && dev_is_pci(dev->dev.parent) &&
	    (ext_filter_mask & RTEXT_FILTER_VF)) {
		int num_vfs = dev_num_vf(dev->dev.parent);
		size_t size = nla_total_size(sizeof(struct nlattr));
		size += nla_total_size(num_vfs * sizeof(struct nlattr));
		size += num_vfs *
			(nla_total_size(sizeof(struct ifla_vf_mac)) +
			 nla_total_size(sizeof(struct ifla_vf_vlan)) +
			 nla_total_size(sizeof(struct ifla_vf_spoofchk)) +
			 nla_total_size(sizeof(struct ifla_vf_rate)) +
			 nla_total_size(sizeof(struct ifla_vf_link_state)));
		return size;
	} else
		return 0;
}

static size_t rtnl_port_size(const struct net_device *dev,
			     u32 ext_filter_mask)
{
	size_t port_size = nla_total_size(4)		/* PORT_VF */
		+ nla_total_size(PORT_PROFILE_MAX)	/* PORT_PROFILE */
		+ nla_total_size(sizeof(struct ifla_port_vsi))
							/* PORT_VSI_TYPE */
		+ nla_total_size(PORT_UUID_MAX)		/* PORT_INSTANCE_UUID */
		+ nla_total_size(PORT_UUID_MAX)		/* PORT_HOST_UUID */
		+ nla_total_size(1)			/* PROT_VDP_REQUEST */
		+ nla_total_size(2);			/* PORT_VDP_RESPONSE */
	size_t vf_ports_size = nla_total_size(sizeof(struct nlattr));
	size_t vf_port_size = nla_total_size(sizeof(struct nlattr))
		+ port_size;
	size_t port_self_size = nla_total_size(sizeof(struct nlattr))
		+ port_size;

	if (!dev->netdev_ops->ndo_get_vf_port || !dev->dev.parent ||
	    !(ext_filter_mask & RTEXT_FILTER_VF))
		return 0;
	if (dev_num_vf(dev->dev.parent))
		return port_self_size + vf_ports_size +
			vf_port_size * dev_num_vf(dev->dev.parent);
	else
		return port_self_size;
}

static noinline size_t if_nlmsg_size(const struct net_device *dev,
				     u32 ext_filter_mask)
{
	return NLMSG_ALIGN(sizeof(struct ifinfomsg))
	       + nla_total_size(IFNAMSIZ) /* IFLA_IFNAME */
	       + nla_total_size(IFALIASZ) /* IFLA_IFALIAS */
	       + nla_total_size(IFNAMSIZ) /* IFLA_QDISC */
	       + nla_total_size(sizeof(struct rtnl_link_ifmap))
	       + nla_total_size(sizeof(struct rtnl_link_stats))
	       + nla_total_size(sizeof(struct rtnl_link_stats64))
	       + nla_total_size(MAX_ADDR_LEN) /* IFLA_ADDRESS */
	       + nla_total_size(MAX_ADDR_LEN) /* IFLA_BROADCAST */
	       + nla_total_size(4) /* IFLA_TXQLEN */
	       + nla_total_size(4) /* IFLA_WEIGHT */
	       + nla_total_size(4) /* IFLA_MTU */
	       + nla_total_size(4) /* IFLA_LINK */
	       + nla_total_size(4) /* IFLA_MASTER */
	       + nla_total_size(1) /* IFLA_CARRIER */
	       + nla_total_size(4) /* IFLA_PROMISCUITY */
	       + nla_total_size(4) /* IFLA_NUM_TX_QUEUES */
	       + nla_total_size(4) /* IFLA_NUM_RX_QUEUES */
	       + nla_total_size(1) /* IFLA_OPERSTATE */
	       + nla_total_size(1) /* IFLA_LINKMODE */
	       + nla_total_size(4) /* IFLA_CARRIER_CHANGES */
	       + nla_total_size(ext_filter_mask
			        & RTEXT_FILTER_VF ? 4 : 0) /* IFLA_NUM_VF */
	       + rtnl_vfinfo_size(dev, ext_filter_mask) /* IFLA_VFINFO_LIST */
	       + rtnl_port_size(dev, ext_filter_mask) /* IFLA_VF_PORTS + IFLA_PORT_SELF */
	       + rtnl_link_get_size(dev) /* IFLA_LINKINFO */
	       + rtnl_link_get_af_size(dev) /* IFLA_AF_SPEC */
	       + nla_total_size(MAX_PHYS_ITEM_ID_LEN) /* IFLA_PHYS_PORT_ID */
	       + nla_total_size(MAX_PHYS_ITEM_ID_LEN); /* IFLA_PHYS_SWITCH_ID */
}

static int rtnl_vf_ports_fill(struct sk_buff *skb, struct net_device *dev)
{
	struct nlattr *vf_ports;
	struct nlattr *vf_port;
	int vf;
	int err;

	vf_ports = nla_nest_start(skb, IFLA_VF_PORTS);
	if (!vf_ports)
		return -EMSGSIZE;

	for (vf = 0; vf < dev_num_vf(dev->dev.parent); vf++) {
		vf_port = nla_nest_start(skb, IFLA_VF_PORT);
		if (!vf_port)
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_PORT_VF, vf))
			goto nla_put_failure;
		err = dev->netdev_ops->ndo_get_vf_port(dev, vf, skb);
		if (err == -EMSGSIZE)
			goto nla_put_failure;
		if (err) {
			nla_nest_cancel(skb, vf_port);
			continue;
		}
		nla_nest_end(skb, vf_port);
	}

	nla_nest_end(skb, vf_ports);

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, vf_ports);
	return -EMSGSIZE;
}

static int rtnl_port_self_fill(struct sk_buff *skb, struct net_device *dev)
{
	struct nlattr *port_self;
	int err;

	port_self = nla_nest_start(skb, IFLA_PORT_SELF);
	if (!port_self)
		return -EMSGSIZE;

	err = dev->netdev_ops->ndo_get_vf_port(dev, PORT_SELF_VF, skb);
	if (err) {
		nla_nest_cancel(skb, port_self);
		return (err == -EMSGSIZE) ? err : 0;
	}

	nla_nest_end(skb, port_self);

	return 0;
}

static int rtnl_port_fill(struct sk_buff *skb, struct net_device *dev,
			  u32 ext_filter_mask)
{
	int err;

	if (!dev->netdev_ops->ndo_get_vf_port || !dev->dev.parent ||
	    !(ext_filter_mask & RTEXT_FILTER_VF))
		return 0;

	err = rtnl_port_self_fill(skb, dev);
	if (err)
		return err;

	if (dev_num_vf(dev->dev.parent)) {
		err = rtnl_vf_ports_fill(skb, dev);
		if (err)
			return err;
	}

	return 0;
}

static int rtnl_phys_port_id_fill(struct sk_buff *skb, struct net_device *dev)
{
	int err;
	struct netdev_phys_item_id ppid;

	err = dev_get_phys_port_id(dev, &ppid);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	if (nla_put(skb, IFLA_PHYS_PORT_ID, ppid.id_len, ppid.id))
		return -EMSGSIZE;

	return 0;
}

static int rtnl_phys_switch_id_fill(struct sk_buff *skb, struct net_device *dev)
{
	int err;
	struct netdev_phys_item_id psid;

	err = netdev_switch_parent_id_get(dev, &psid);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	if (nla_put(skb, IFLA_PHYS_SWITCH_ID, psid.id_len, psid.id))
		return -EMSGSIZE;

	return 0;
}

static int rtnl_fill_ifinfo(struct sk_buff *skb, struct net_device *dev,
			    int type, u32 pid, u32 seq, u32 change,
			    unsigned int flags, u32 ext_filter_mask)
{
	struct ifinfomsg *ifm;
	struct nlmsghdr *nlh;
	struct rtnl_link_stats64 temp;
	const struct rtnl_link_stats64 *stats;
	struct nlattr *attr, *af_spec;
	struct rtnl_af_ops *af_ops;
	struct net_device *upper_dev = netdev_master_upper_dev_get(dev);

	ASSERT_RTNL();
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

	if (nla_put_string(skb, IFLA_IFNAME, dev->name) ||
	    nla_put_u32(skb, IFLA_TXQLEN, dev->tx_queue_len) ||
	    nla_put_u8(skb, IFLA_OPERSTATE,
		       netif_running(dev) ? dev->operstate : IF_OPER_DOWN) ||
	    nla_put_u8(skb, IFLA_LINKMODE, dev->link_mode) ||
	    nla_put_u32(skb, IFLA_MTU, dev->mtu) ||
	    nla_put_u32(skb, IFLA_GROUP, dev->group) ||
	    nla_put_u32(skb, IFLA_PROMISCUITY, dev->promiscuity) ||
	    nla_put_u32(skb, IFLA_NUM_TX_QUEUES, dev->num_tx_queues) ||
#ifdef CONFIG_RPS
	    nla_put_u32(skb, IFLA_NUM_RX_QUEUES, dev->num_rx_queues) ||
#endif
	    (dev->ifindex != dev->iflink &&
	     nla_put_u32(skb, IFLA_LINK, dev->iflink)) ||
	    (upper_dev &&
	     nla_put_u32(skb, IFLA_MASTER, upper_dev->ifindex)) ||
	    nla_put_u8(skb, IFLA_CARRIER, netif_carrier_ok(dev)) ||
	    (dev->qdisc &&
	     nla_put_string(skb, IFLA_QDISC, dev->qdisc->ops->id)) ||
	    (dev->ifalias &&
	     nla_put_string(skb, IFLA_IFALIAS, dev->ifalias)) ||
	    nla_put_u32(skb, IFLA_CARRIER_CHANGES,
			atomic_read(&dev->carrier_changes)))
		goto nla_put_failure;

	if (1) {
		struct rtnl_link_ifmap map = {
			.mem_start   = dev->mem_start,
			.mem_end     = dev->mem_end,
			.base_addr   = dev->base_addr,
			.irq         = dev->irq,
			.dma         = dev->dma,
			.port        = dev->if_port,
		};
		if (nla_put(skb, IFLA_MAP, sizeof(map), &map))
			goto nla_put_failure;
	}

	if (dev->addr_len) {
		if (nla_put(skb, IFLA_ADDRESS, dev->addr_len, dev->dev_addr) ||
		    nla_put(skb, IFLA_BROADCAST, dev->addr_len, dev->broadcast))
			goto nla_put_failure;
	}

	if (rtnl_phys_port_id_fill(skb, dev))
		goto nla_put_failure;

	if (rtnl_phys_switch_id_fill(skb, dev))
		goto nla_put_failure;

	attr = nla_reserve(skb, IFLA_STATS,
			sizeof(struct rtnl_link_stats));
	if (attr == NULL)
		goto nla_put_failure;

	stats = dev_get_stats(dev, &temp);
	copy_rtnl_link_stats(nla_data(attr), stats);

	attr = nla_reserve(skb, IFLA_STATS64,
			sizeof(struct rtnl_link_stats64));
	if (attr == NULL)
		goto nla_put_failure;
	copy_rtnl_link_stats64(nla_data(attr), stats);

	if (dev->dev.parent && (ext_filter_mask & RTEXT_FILTER_VF) &&
	    nla_put_u32(skb, IFLA_NUM_VF, dev_num_vf(dev->dev.parent)))
		goto nla_put_failure;

	if (dev->netdev_ops->ndo_get_vf_config && dev->dev.parent
	    && (ext_filter_mask & RTEXT_FILTER_VF)) {
		int i;

		struct nlattr *vfinfo, *vf;
		int num_vfs = dev_num_vf(dev->dev.parent);

		vfinfo = nla_nest_start(skb, IFLA_VFINFO_LIST);
		if (!vfinfo)
			goto nla_put_failure;
		for (i = 0; i < num_vfs; i++) {
			struct ifla_vf_info ivi;
			struct ifla_vf_mac vf_mac;
			struct ifla_vf_vlan vf_vlan;
			struct ifla_vf_rate vf_rate;
			struct ifla_vf_tx_rate vf_tx_rate;
			struct ifla_vf_spoofchk vf_spoofchk;
			struct ifla_vf_link_state vf_linkstate;

			/*
			 * Not all SR-IOV capable drivers support the
			 * spoofcheck query.  Preset to -1 so the user
			 * space tool can detect that the driver didn't
			 * report anything.
			 */
			ivi.spoofchk = -1;
			memset(ivi.mac, 0, sizeof(ivi.mac));
			/* The default value for VF link state is "auto"
			 * IFLA_VF_LINK_STATE_AUTO which equals zero
			 */
			ivi.linkstate = 0;
			if (dev->netdev_ops->ndo_get_vf_config(dev, i, &ivi))
				break;
			vf_mac.vf =
				vf_vlan.vf =
				vf_rate.vf =
				vf_tx_rate.vf =
				vf_spoofchk.vf =
				vf_linkstate.vf = ivi.vf;

			memcpy(vf_mac.mac, ivi.mac, sizeof(ivi.mac));
			vf_vlan.vlan = ivi.vlan;
			vf_vlan.qos = ivi.qos;
			vf_tx_rate.rate = ivi.max_tx_rate;
			vf_rate.min_tx_rate = ivi.min_tx_rate;
			vf_rate.max_tx_rate = ivi.max_tx_rate;
			vf_spoofchk.setting = ivi.spoofchk;
			vf_linkstate.link_state = ivi.linkstate;
			vf = nla_nest_start(skb, IFLA_VF_INFO);
			if (!vf) {
				nla_nest_cancel(skb, vfinfo);
				goto nla_put_failure;
			}
			if (nla_put(skb, IFLA_VF_MAC, sizeof(vf_mac), &vf_mac) ||
			    nla_put(skb, IFLA_VF_VLAN, sizeof(vf_vlan), &vf_vlan) ||
			    nla_put(skb, IFLA_VF_RATE, sizeof(vf_rate),
				    &vf_rate) ||
			    nla_put(skb, IFLA_VF_TX_RATE, sizeof(vf_tx_rate),
				    &vf_tx_rate) ||
			    nla_put(skb, IFLA_VF_SPOOFCHK, sizeof(vf_spoofchk),
				    &vf_spoofchk) ||
			    nla_put(skb, IFLA_VF_LINK_STATE, sizeof(vf_linkstate),
				    &vf_linkstate))
				goto nla_put_failure;
			nla_nest_end(skb, vf);
		}
		nla_nest_end(skb, vfinfo);
	}

	if (rtnl_port_fill(skb, dev, ext_filter_mask))
		goto nla_put_failure;

	if (dev->rtnl_link_ops || rtnl_have_link_slave_info(dev)) {
		if (rtnl_link_fill(skb, dev) < 0)
			goto nla_put_failure;
	}

	if (!(af_spec = nla_nest_start(skb, IFLA_AF_SPEC)))
		goto nla_put_failure;

	list_for_each_entry(af_ops, &rtnl_af_ops, list) {
		if (af_ops->fill_link_af) {
			struct nlattr *af;
			int err;

			if (!(af = nla_nest_start(skb, af_ops->family)))
				goto nla_put_failure;

			err = af_ops->fill_link_af(skb, dev);

			/*
			 * Caller may return ENODATA to indicate that there
			 * was no data to be dumped. This is not an error, it
			 * means we should trim the attribute header and
			 * continue.
			 */
			if (err == -ENODATA)
				nla_nest_cancel(skb, af);
			else if (err < 0)
				goto nla_put_failure;

			nla_nest_end(skb, af);
		}
	}

	nla_nest_end(skb, af_spec);

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static const struct nla_policy ifla_policy[IFLA_MAX+1] = {
	[IFLA_IFNAME]		= { .type = NLA_STRING, .len = IFNAMSIZ-1 },
	[IFLA_ADDRESS]		= { .type = NLA_BINARY, .len = MAX_ADDR_LEN },
	[IFLA_BROADCAST]	= { .type = NLA_BINARY, .len = MAX_ADDR_LEN },
	[IFLA_MAP]		= { .len = sizeof(struct rtnl_link_ifmap) },
	[IFLA_MTU]		= { .type = NLA_U32 },
	[IFLA_LINK]		= { .type = NLA_U32 },
	[IFLA_MASTER]		= { .type = NLA_U32 },
	[IFLA_CARRIER]		= { .type = NLA_U8 },
	[IFLA_TXQLEN]		= { .type = NLA_U32 },
	[IFLA_WEIGHT]		= { .type = NLA_U32 },
	[IFLA_OPERSTATE]	= { .type = NLA_U8 },
	[IFLA_LINKMODE]		= { .type = NLA_U8 },
	[IFLA_LINKINFO]		= { .type = NLA_NESTED },
	[IFLA_NET_NS_PID]	= { .type = NLA_U32 },
	[IFLA_NET_NS_FD]	= { .type = NLA_U32 },
	[IFLA_IFALIAS]	        = { .type = NLA_STRING, .len = IFALIASZ-1 },
	[IFLA_VFINFO_LIST]	= {. type = NLA_NESTED },
	[IFLA_VF_PORTS]		= { .type = NLA_NESTED },
	[IFLA_PORT_SELF]	= { .type = NLA_NESTED },
	[IFLA_AF_SPEC]		= { .type = NLA_NESTED },
	[IFLA_EXT_MASK]		= { .type = NLA_U32 },
	[IFLA_PROMISCUITY]	= { .type = NLA_U32 },
	[IFLA_NUM_TX_QUEUES]	= { .type = NLA_U32 },
	[IFLA_NUM_RX_QUEUES]	= { .type = NLA_U32 },
	[IFLA_PHYS_PORT_ID]	= { .type = NLA_BINARY, .len = MAX_PHYS_ITEM_ID_LEN },
	[IFLA_CARRIER_CHANGES]	= { .type = NLA_U32 },  /* ignored */
	[IFLA_PHYS_SWITCH_ID]	= { .type = NLA_BINARY, .len = MAX_PHYS_ITEM_ID_LEN },
};

static const struct nla_policy ifla_info_policy[IFLA_INFO_MAX+1] = {
	[IFLA_INFO_KIND]	= { .type = NLA_STRING },
	[IFLA_INFO_DATA]	= { .type = NLA_NESTED },
	[IFLA_INFO_SLAVE_KIND]	= { .type = NLA_STRING },
	[IFLA_INFO_SLAVE_DATA]	= { .type = NLA_NESTED },
};

static const struct nla_policy ifla_vfinfo_policy[IFLA_VF_INFO_MAX+1] = {
	[IFLA_VF_INFO]		= { .type = NLA_NESTED },
};

static const struct nla_policy ifla_vf_policy[IFLA_VF_MAX+1] = {
	[IFLA_VF_MAC]		= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_vf_mac) },
	[IFLA_VF_VLAN]		= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_vf_vlan) },
	[IFLA_VF_TX_RATE]	= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_vf_tx_rate) },
	[IFLA_VF_SPOOFCHK]	= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_vf_spoofchk) },
	[IFLA_VF_RATE]		= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_vf_rate) },
	[IFLA_VF_LINK_STATE]	= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_vf_link_state) },
};

static const struct nla_policy ifla_port_policy[IFLA_PORT_MAX+1] = {
	[IFLA_PORT_VF]		= { .type = NLA_U32 },
	[IFLA_PORT_PROFILE]	= { .type = NLA_STRING,
				    .len = PORT_PROFILE_MAX },
	[IFLA_PORT_VSI_TYPE]	= { .type = NLA_BINARY,
				    .len = sizeof(struct ifla_port_vsi)},
	[IFLA_PORT_INSTANCE_UUID] = { .type = NLA_BINARY,
				      .len = PORT_UUID_MAX },
	[IFLA_PORT_HOST_UUID]	= { .type = NLA_STRING,
				    .len = PORT_UUID_MAX },
	[IFLA_PORT_REQUEST]	= { .type = NLA_U8, },
	[IFLA_PORT_RESPONSE]	= { .type = NLA_U16, },
};

static int rtnl_dump_ifinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	int h, s_h;
	int idx = 0, s_idx;
	struct net_device *dev;
	struct hlist_head *head;
	struct nlattr *tb[IFLA_MAX+1];
	u32 ext_filter_mask = 0;
	int err;
	int hdrlen;

	s_h = cb->args[0];
	s_idx = cb->args[1];

	rcu_read_lock();
	cb->seq = net->dev_base_seq;

	/* A hack to preserve kernel<->userspace interface.
	 * The correct header is ifinfomsg. It is consistent with rtnl_getlink.
	 * However, before Linux v3.9 the code here assumed rtgenmsg and that's
	 * what iproute2 < v3.9.0 used.
	 * We can detect the old iproute2. Even including the IFLA_EXT_MASK
	 * attribute, its netlink message is shorter than struct ifinfomsg.
	 */
	hdrlen = nlmsg_len(cb->nlh) < sizeof(struct ifinfomsg) ?
		 sizeof(struct rtgenmsg) : sizeof(struct ifinfomsg);

	if (nlmsg_parse(cb->nlh, hdrlen, tb, IFLA_MAX, ifla_policy) >= 0) {

		if (tb[IFLA_EXT_MASK])
			ext_filter_mask = nla_get_u32(tb[IFLA_EXT_MASK]);
	}

	for (h = s_h; h < NETDEV_HASHENTRIES; h++, s_idx = 0) {
		idx = 0;
		head = &net->dev_index_head[h];
		hlist_for_each_entry_rcu(dev, head, index_hlist) {
			if (idx < s_idx)
				goto cont;
			err = rtnl_fill_ifinfo(skb, dev, RTM_NEWLINK,
					       NETLINK_CB(cb->skb).portid,
					       cb->nlh->nlmsg_seq, 0,
					       NLM_F_MULTI,
					       ext_filter_mask);
			/* If we ran out of room on the first message,
			 * we're in trouble
			 */
			WARN_ON((err == -EMSGSIZE) && (skb->len == 0));

			if (err <= 0)
				goto out;

			nl_dump_check_consistent(cb, nlmsg_hdr(skb));
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

int rtnl_nla_parse_ifla(struct nlattr **tb, const struct nlattr *head, int len)
{
	return nla_parse(tb, IFLA_MAX, head, len, ifla_policy);
}
EXPORT_SYMBOL(rtnl_nla_parse_ifla);

struct net *rtnl_link_get_net(struct net *src_net, struct nlattr *tb[])
{
	struct net *net;
	/* Examine the link attributes and figure out which
	 * network namespace we are talking about.
	 */
	if (tb[IFLA_NET_NS_PID])
		net = get_net_ns_by_pid(nla_get_u32(tb[IFLA_NET_NS_PID]));
	else if (tb[IFLA_NET_NS_FD])
		net = get_net_ns_by_fd(nla_get_u32(tb[IFLA_NET_NS_FD]));
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

	if (tb[IFLA_AF_SPEC]) {
		struct nlattr *af;
		int rem, err;

		nla_for_each_nested(af, tb[IFLA_AF_SPEC], rem) {
			const struct rtnl_af_ops *af_ops;

			if (!(af_ops = rtnl_af_lookup(nla_type(af))))
				return -EAFNOSUPPORT;

			if (!af_ops->set_link_af)
				return -EOPNOTSUPP;

			if (af_ops->validate_link_af) {
				err = af_ops->validate_link_af(dev, af);
				if (err < 0)
					return err;
			}
		}
	}

	return 0;
}

static int do_setvfinfo(struct net_device *dev, struct nlattr *attr)
{
	int rem, err = -EINVAL;
	struct nlattr *vf;
	const struct net_device_ops *ops = dev->netdev_ops;

	nla_for_each_nested(vf, attr, rem) {
		switch (nla_type(vf)) {
		case IFLA_VF_MAC: {
			struct ifla_vf_mac *ivm;
			ivm = nla_data(vf);
			err = -EOPNOTSUPP;
			if (ops->ndo_set_vf_mac)
				err = ops->ndo_set_vf_mac(dev, ivm->vf,
							  ivm->mac);
			break;
		}
		case IFLA_VF_VLAN: {
			struct ifla_vf_vlan *ivv;
			ivv = nla_data(vf);
			err = -EOPNOTSUPP;
			if (ops->ndo_set_vf_vlan)
				err = ops->ndo_set_vf_vlan(dev, ivv->vf,
							   ivv->vlan,
							   ivv->qos);
			break;
		}
		case IFLA_VF_TX_RATE: {
			struct ifla_vf_tx_rate *ivt;
			struct ifla_vf_info ivf;
			ivt = nla_data(vf);
			err = -EOPNOTSUPP;
			if (ops->ndo_get_vf_config)
				err = ops->ndo_get_vf_config(dev, ivt->vf,
							     &ivf);
			if (err)
				break;
			err = -EOPNOTSUPP;
			if (ops->ndo_set_vf_rate)
				err = ops->ndo_set_vf_rate(dev, ivt->vf,
							   ivf.min_tx_rate,
							   ivt->rate);
			break;
		}
		case IFLA_VF_RATE: {
			struct ifla_vf_rate *ivt;
			ivt = nla_data(vf);
			err = -EOPNOTSUPP;
			if (ops->ndo_set_vf_rate)
				err = ops->ndo_set_vf_rate(dev, ivt->vf,
							   ivt->min_tx_rate,
							   ivt->max_tx_rate);
			break;
		}
		case IFLA_VF_SPOOFCHK: {
			struct ifla_vf_spoofchk *ivs;
			ivs = nla_data(vf);
			err = -EOPNOTSUPP;
			if (ops->ndo_set_vf_spoofchk)
				err = ops->ndo_set_vf_spoofchk(dev, ivs->vf,
							       ivs->setting);
			break;
		}
		case IFLA_VF_LINK_STATE: {
			struct ifla_vf_link_state *ivl;
			ivl = nla_data(vf);
			err = -EOPNOTSUPP;
			if (ops->ndo_set_vf_link_state)
				err = ops->ndo_set_vf_link_state(dev, ivl->vf,
								 ivl->link_state);
			break;
		}
		default:
			err = -EINVAL;
			break;
		}
		if (err)
			break;
	}
	return err;
}

static int do_set_master(struct net_device *dev, int ifindex)
{
	struct net_device *upper_dev = netdev_master_upper_dev_get(dev);
	const struct net_device_ops *ops;
	int err;

	if (upper_dev) {
		if (upper_dev->ifindex == ifindex)
			return 0;
		ops = upper_dev->netdev_ops;
		if (ops->ndo_del_slave) {
			err = ops->ndo_del_slave(upper_dev, dev);
			if (err)
				return err;
		} else {
			return -EOPNOTSUPP;
		}
	}

	if (ifindex) {
		upper_dev = __dev_get_by_index(dev_net(dev), ifindex);
		if (!upper_dev)
			return -EINVAL;
		ops = upper_dev->netdev_ops;
		if (ops->ndo_add_slave) {
			err = ops->ndo_add_slave(upper_dev, dev);
			if (err)
				return err;
		} else {
			return -EOPNOTSUPP;
		}
	}
	return 0;
}

#define DO_SETLINK_MODIFIED	0x01
/* notify flag means notify + modified. */
#define DO_SETLINK_NOTIFY	0x03
static int do_setlink(const struct sk_buff *skb,
		      struct net_device *dev, struct ifinfomsg *ifm,
		      struct nlattr **tb, char *ifname, int status)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int err;

	if (tb[IFLA_NET_NS_PID] || tb[IFLA_NET_NS_FD]) {
		struct net *net = rtnl_link_get_net(dev_net(dev), tb);
		if (IS_ERR(net)) {
			err = PTR_ERR(net);
			goto errout;
		}
		if (!netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN)) {
			err = -EPERM;
			goto errout;
		}
		err = dev_change_net_namespace(dev, net, ifname);
		put_net(net);
		if (err)
			goto errout;
		status |= DO_SETLINK_MODIFIED;
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

		status |= DO_SETLINK_NOTIFY;
	}

	if (tb[IFLA_ADDRESS]) {
		struct sockaddr *sa;
		int len;

		len = sizeof(sa_family_t) + dev->addr_len;
		sa = kmalloc(len, GFP_KERNEL);
		if (!sa) {
			err = -ENOMEM;
			goto errout;
		}
		sa->sa_family = dev->type;
		memcpy(sa->sa_data, nla_data(tb[IFLA_ADDRESS]),
		       dev->addr_len);
		err = dev_set_mac_address(dev, sa);
		kfree(sa);
		if (err)
			goto errout;
		status |= DO_SETLINK_MODIFIED;
	}

	if (tb[IFLA_MTU]) {
		err = dev_set_mtu(dev, nla_get_u32(tb[IFLA_MTU]));
		if (err < 0)
			goto errout;
		status |= DO_SETLINK_MODIFIED;
	}

	if (tb[IFLA_GROUP]) {
		dev_set_group(dev, nla_get_u32(tb[IFLA_GROUP]));
		status |= DO_SETLINK_NOTIFY;
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
		status |= DO_SETLINK_MODIFIED;
	}

	if (tb[IFLA_IFALIAS]) {
		err = dev_set_alias(dev, nla_data(tb[IFLA_IFALIAS]),
				    nla_len(tb[IFLA_IFALIAS]));
		if (err < 0)
			goto errout;
		status |= DO_SETLINK_NOTIFY;
	}

	if (tb[IFLA_BROADCAST]) {
		nla_memcpy(dev->broadcast, tb[IFLA_BROADCAST], dev->addr_len);
		call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
	}

	if (ifm->ifi_flags || ifm->ifi_change) {
		err = dev_change_flags(dev, rtnl_dev_combine_flags(dev, ifm));
		if (err < 0)
			goto errout;
	}

	if (tb[IFLA_MASTER]) {
		err = do_set_master(dev, nla_get_u32(tb[IFLA_MASTER]));
		if (err)
			goto errout;
		status |= DO_SETLINK_MODIFIED;
	}

	if (tb[IFLA_CARRIER]) {
		err = dev_change_carrier(dev, nla_get_u8(tb[IFLA_CARRIER]));
		if (err)
			goto errout;
		status |= DO_SETLINK_MODIFIED;
	}

	if (tb[IFLA_TXQLEN]) {
		unsigned long value = nla_get_u32(tb[IFLA_TXQLEN]);

		if (dev->tx_queue_len ^ value)
			status |= DO_SETLINK_NOTIFY;

		dev->tx_queue_len = value;
	}

	if (tb[IFLA_OPERSTATE])
		set_operstate(dev, nla_get_u8(tb[IFLA_OPERSTATE]));

	if (tb[IFLA_LINKMODE]) {
		unsigned char value = nla_get_u8(tb[IFLA_LINKMODE]);

		write_lock_bh(&dev_base_lock);
		if (dev->link_mode ^ value)
			status |= DO_SETLINK_NOTIFY;
		dev->link_mode = value;
		write_unlock_bh(&dev_base_lock);
	}

	if (tb[IFLA_VFINFO_LIST]) {
		struct nlattr *attr;
		int rem;
		nla_for_each_nested(attr, tb[IFLA_VFINFO_LIST], rem) {
			if (nla_type(attr) != IFLA_VF_INFO) {
				err = -EINVAL;
				goto errout;
			}
			err = do_setvfinfo(dev, attr);
			if (err < 0)
				goto errout;
			status |= DO_SETLINK_NOTIFY;
		}
	}
	err = 0;

	if (tb[IFLA_VF_PORTS]) {
		struct nlattr *port[IFLA_PORT_MAX+1];
		struct nlattr *attr;
		int vf;
		int rem;

		err = -EOPNOTSUPP;
		if (!ops->ndo_set_vf_port)
			goto errout;

		nla_for_each_nested(attr, tb[IFLA_VF_PORTS], rem) {
			if (nla_type(attr) != IFLA_VF_PORT)
				continue;
			err = nla_parse_nested(port, IFLA_PORT_MAX,
				attr, ifla_port_policy);
			if (err < 0)
				goto errout;
			if (!port[IFLA_PORT_VF]) {
				err = -EOPNOTSUPP;
				goto errout;
			}
			vf = nla_get_u32(port[IFLA_PORT_VF]);
			err = ops->ndo_set_vf_port(dev, vf, port);
			if (err < 0)
				goto errout;
			status |= DO_SETLINK_NOTIFY;
		}
	}
	err = 0;

	if (tb[IFLA_PORT_SELF]) {
		struct nlattr *port[IFLA_PORT_MAX+1];

		err = nla_parse_nested(port, IFLA_PORT_MAX,
			tb[IFLA_PORT_SELF], ifla_port_policy);
		if (err < 0)
			goto errout;

		err = -EOPNOTSUPP;
		if (ops->ndo_set_vf_port)
			err = ops->ndo_set_vf_port(dev, PORT_SELF_VF, port);
		if (err < 0)
			goto errout;
		status |= DO_SETLINK_NOTIFY;
	}

	if (tb[IFLA_AF_SPEC]) {
		struct nlattr *af;
		int rem;

		nla_for_each_nested(af, tb[IFLA_AF_SPEC], rem) {
			const struct rtnl_af_ops *af_ops;

			if (!(af_ops = rtnl_af_lookup(nla_type(af))))
				BUG();

			err = af_ops->set_link_af(dev, af);
			if (err < 0)
				goto errout;

			status |= DO_SETLINK_NOTIFY;
		}
	}
	err = 0;

errout:
	if (status & DO_SETLINK_MODIFIED) {
		if (status & DO_SETLINK_NOTIFY)
			netdev_state_change(dev);

		if (err < 0)
			net_warn_ratelimited("A link change request failed with some changes committed already. Interface %s may have been left with an inconsistent configuration, please check.\n",
					     dev->name);
	}

	return err;
}

static int rtnl_setlink(struct sk_buff *skb, struct nlmsghdr *nlh)
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

	err = do_setlink(skb, dev, ifm, tb, ifname, 0);
errout:
	return err;
}

static int rtnl_dellink(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	const struct rtnl_link_ops *ops;
	struct net_device *dev;
	struct ifinfomsg *ifm;
	char ifname[IFNAMSIZ];
	struct nlattr *tb[IFLA_MAX+1];
	int err;
	LIST_HEAD(list_kill);

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
	if (!ops || !ops->dellink)
		return -EOPNOTSUPP;

	ops->dellink(dev, &list_kill);
	unregister_netdevice_many(&list_kill);
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

	__dev_notify_flags(dev, old_flags, ~0U);
	return 0;
}
EXPORT_SYMBOL(rtnl_configure_link);

struct net_device *rtnl_create_link(struct net *net,
	char *ifname, unsigned char name_assign_type,
	const struct rtnl_link_ops *ops, struct nlattr *tb[])
{
	int err;
	struct net_device *dev;
	unsigned int num_tx_queues = 1;
	unsigned int num_rx_queues = 1;

	if (tb[IFLA_NUM_TX_QUEUES])
		num_tx_queues = nla_get_u32(tb[IFLA_NUM_TX_QUEUES]);
	else if (ops->get_num_tx_queues)
		num_tx_queues = ops->get_num_tx_queues();

	if (tb[IFLA_NUM_RX_QUEUES])
		num_rx_queues = nla_get_u32(tb[IFLA_NUM_RX_QUEUES]);
	else if (ops->get_num_rx_queues)
		num_rx_queues = ops->get_num_rx_queues();

	err = -ENOMEM;
	dev = alloc_netdev_mqs(ops->priv_size, ifname, name_assign_type,
			       ops->setup, num_tx_queues, num_rx_queues);
	if (!dev)
		goto err;

	dev_net_set(dev, net);
	dev->rtnl_link_ops = ops;
	dev->rtnl_link_state = RTNL_LINK_INITIALIZING;

	if (tb[IFLA_MTU])
		dev->mtu = nla_get_u32(tb[IFLA_MTU]);
	if (tb[IFLA_ADDRESS]) {
		memcpy(dev->dev_addr, nla_data(tb[IFLA_ADDRESS]),
				nla_len(tb[IFLA_ADDRESS]));
		dev->addr_assign_type = NET_ADDR_SET;
	}
	if (tb[IFLA_BROADCAST])
		memcpy(dev->broadcast, nla_data(tb[IFLA_BROADCAST]),
				nla_len(tb[IFLA_BROADCAST]));
	if (tb[IFLA_TXQLEN])
		dev->tx_queue_len = nla_get_u32(tb[IFLA_TXQLEN]);
	if (tb[IFLA_OPERSTATE])
		set_operstate(dev, nla_get_u8(tb[IFLA_OPERSTATE]));
	if (tb[IFLA_LINKMODE])
		dev->link_mode = nla_get_u8(tb[IFLA_LINKMODE]);
	if (tb[IFLA_GROUP])
		dev_set_group(dev, nla_get_u32(tb[IFLA_GROUP]));

	return dev;

err:
	return ERR_PTR(err);
}
EXPORT_SYMBOL(rtnl_create_link);

static int rtnl_group_changelink(const struct sk_buff *skb,
		struct net *net, int group,
		struct ifinfomsg *ifm,
		struct nlattr **tb)
{
	struct net_device *dev;
	int err;

	for_each_netdev(net, dev) {
		if (dev->group == group) {
			err = do_setlink(skb, dev, ifm, tb, NULL, 0);
			if (err < 0)
				return err;
		}
	}

	return 0;
}

static int rtnl_newlink(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	const struct rtnl_link_ops *ops;
	const struct rtnl_link_ops *m_ops = NULL;
	struct net_device *dev;
	struct net_device *master_dev = NULL;
	struct ifinfomsg *ifm;
	char kind[MODULE_NAME_LEN];
	char ifname[IFNAMSIZ];
	struct nlattr *tb[IFLA_MAX+1];
	struct nlattr *linkinfo[IFLA_INFO_MAX+1];
	unsigned char name_assign_type = NET_NAME_USER;
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
	else {
		if (ifname[0])
			dev = __dev_get_by_name(net, ifname);
		else
			dev = NULL;
	}

	if (dev) {
		master_dev = netdev_master_upper_dev_get(dev);
		if (master_dev)
			m_ops = master_dev->rtnl_link_ops;
	}

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
		struct nlattr *attr[ops ? ops->maxtype + 1 : 0];
		struct nlattr *slave_attr[m_ops ? m_ops->slave_maxtype + 1 : 0];
		struct nlattr **data = NULL;
		struct nlattr **slave_data = NULL;
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

		if (m_ops) {
			if (m_ops->slave_maxtype &&
			    linkinfo[IFLA_INFO_SLAVE_DATA]) {
				err = nla_parse_nested(slave_attr,
						       m_ops->slave_maxtype,
						       linkinfo[IFLA_INFO_SLAVE_DATA],
						       m_ops->slave_policy);
				if (err < 0)
					return err;
				slave_data = slave_attr;
			}
			if (m_ops->slave_validate) {
				err = m_ops->slave_validate(tb, slave_data);
				if (err < 0)
					return err;
			}
		}

		if (dev) {
			int status = 0;

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
				status |= DO_SETLINK_NOTIFY;
			}

			if (linkinfo[IFLA_INFO_SLAVE_DATA]) {
				if (!m_ops || !m_ops->slave_changelink)
					return -EOPNOTSUPP;

				err = m_ops->slave_changelink(master_dev, dev,
							      tb, slave_data);
				if (err < 0)
					return err;
				status |= DO_SETLINK_NOTIFY;
			}

			return do_setlink(skb, dev, ifm, tb, ifname, status);
		}

		if (!(nlh->nlmsg_flags & NLM_F_CREATE)) {
			if (ifm->ifi_index == 0 && tb[IFLA_GROUP])
				return rtnl_group_changelink(skb, net,
						nla_get_u32(tb[IFLA_GROUP]),
						ifm, tb);
			return -ENODEV;
		}

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

		if (!ops->setup)
			return -EOPNOTSUPP;

		if (!ifname[0]) {
			snprintf(ifname, IFNAMSIZ, "%s%%d", ops->kind);
			name_assign_type = NET_NAME_ENUM;
		}

		dest_net = rtnl_link_get_net(net, tb);
		if (IS_ERR(dest_net))
			return PTR_ERR(dest_net);

		dev = rtnl_create_link(dest_net, ifname, name_assign_type, ops, tb);
		if (IS_ERR(dev)) {
			err = PTR_ERR(dev);
			goto out;
		}

		dev->ifindex = ifm->ifi_index;

		if (ops->newlink) {
			err = ops->newlink(net, dev, tb, data);
			/* Drivers should call free_netdev() in ->destructor
			 * and unregister it on failure after registration
			 * so that device could be finally freed in rtnl_unlock.
			 */
			if (err < 0) {
				/* If device is not registered at all, free it now */
				if (dev->reg_state == NETREG_UNINITIALIZED)
					free_netdev(dev);
				goto out;
			}
		} else {
			err = register_netdevice(dev);
			if (err < 0) {
				free_netdev(dev);
				goto out;
			}
		}
		err = rtnl_configure_link(dev, ifm);
		if (err < 0)
			unregister_netdevice(dev);
out:
		put_net(dest_net);
		return err;
	}
}

static int rtnl_getlink(struct sk_buff *skb, struct nlmsghdr* nlh)
{
	struct net *net = sock_net(skb->sk);
	struct ifinfomsg *ifm;
	char ifname[IFNAMSIZ];
	struct nlattr *tb[IFLA_MAX+1];
	struct net_device *dev = NULL;
	struct sk_buff *nskb;
	int err;
	u32 ext_filter_mask = 0;

	err = nlmsg_parse(nlh, sizeof(*ifm), tb, IFLA_MAX, ifla_policy);
	if (err < 0)
		return err;

	if (tb[IFLA_IFNAME])
		nla_strlcpy(ifname, tb[IFLA_IFNAME], IFNAMSIZ);

	if (tb[IFLA_EXT_MASK])
		ext_filter_mask = nla_get_u32(tb[IFLA_EXT_MASK]);

	ifm = nlmsg_data(nlh);
	if (ifm->ifi_index > 0)
		dev = __dev_get_by_index(net, ifm->ifi_index);
	else if (tb[IFLA_IFNAME])
		dev = __dev_get_by_name(net, ifname);
	else
		return -EINVAL;

	if (dev == NULL)
		return -ENODEV;

	nskb = nlmsg_new(if_nlmsg_size(dev, ext_filter_mask), GFP_KERNEL);
	if (nskb == NULL)
		return -ENOBUFS;

	err = rtnl_fill_ifinfo(nskb, dev, RTM_NEWLINK, NETLINK_CB(skb).portid,
			       nlh->nlmsg_seq, 0, 0, ext_filter_mask);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in if_nlmsg_size */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(nskb);
	} else
		err = rtnl_unicast(nskb, net, NETLINK_CB(skb).portid);

	return err;
}

static u16 rtnl_calcit(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	struct nlattr *tb[IFLA_MAX+1];
	u32 ext_filter_mask = 0;
	u16 min_ifinfo_dump_size = 0;
	int hdrlen;

	/* Same kernel<->userspace interface hack as in rtnl_dump_ifinfo. */
	hdrlen = nlmsg_len(nlh) < sizeof(struct ifinfomsg) ?
		 sizeof(struct rtgenmsg) : sizeof(struct ifinfomsg);

	if (nlmsg_parse(nlh, hdrlen, tb, IFLA_MAX, ifla_policy) >= 0) {
		if (tb[IFLA_EXT_MASK])
			ext_filter_mask = nla_get_u32(tb[IFLA_EXT_MASK]);
	}

	if (!ext_filter_mask)
		return NLMSG_GOODSIZE;
	/*
	 * traverse the list of net devices and compute the minimum
	 * buffer size based upon the filter mask.
	 */
	list_for_each_entry(dev, &net->dev_base_head, dev_list) {
		min_ifinfo_dump_size = max_t(u16, min_ifinfo_dump_size,
					     if_nlmsg_size(dev,
						           ext_filter_mask));
	}

	return min_ifinfo_dump_size;
}

static int rtnl_dump_all(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx;
	int s_idx = cb->family;

	if (s_idx == 0)
		s_idx = 1;
	for (idx = 1; idx <= RTNL_FAMILY_MAX; idx++) {
		int type = cb->nlh->nlmsg_type-RTM_BASE;
		if (idx < s_idx || idx == PF_PACKET)
			continue;
		if (rtnl_msg_handlers[idx] == NULL ||
		    rtnl_msg_handlers[idx][type].dumpit == NULL)
			continue;
		if (idx > s_idx) {
			memset(&cb->args[0], 0, sizeof(cb->args));
			cb->prev_seq = 0;
			cb->seq = 0;
		}
		if (rtnl_msg_handlers[idx][type].dumpit(skb, cb))
			break;
	}
	cb->family = idx;

	return skb->len;
}

struct sk_buff *rtmsg_ifinfo_build_skb(int type, struct net_device *dev,
				       unsigned int change, gfp_t flags)
{
	struct net *net = dev_net(dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;
	size_t if_info_size;

	skb = nlmsg_new((if_info_size = if_nlmsg_size(dev, 0)), flags);
	if (skb == NULL)
		goto errout;

	err = rtnl_fill_ifinfo(skb, dev, type, 0, 0, change, 0, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in if_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	return skb;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_LINK, err);
	return NULL;
}

void rtmsg_ifinfo_send(struct sk_buff *skb, struct net_device *dev, gfp_t flags)
{
	struct net *net = dev_net(dev);

	rtnl_notify(skb, net, 0, RTNLGRP_LINK, NULL, flags);
}

void rtmsg_ifinfo(int type, struct net_device *dev, unsigned int change,
		  gfp_t flags)
{
	struct sk_buff *skb;

	skb = rtmsg_ifinfo_build_skb(type, dev, change, flags);
	if (skb)
		rtmsg_ifinfo_send(skb, dev, flags);
}
EXPORT_SYMBOL(rtmsg_ifinfo);

static int nlmsg_populate_fdb_fill(struct sk_buff *skb,
				   struct net_device *dev,
				   u8 *addr, u32 pid, u32 seq,
				   int type, unsigned int flags,
				   int nlflags)
{
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*ndm), nlflags);
	if (!nlh)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family  = AF_BRIDGE;
	ndm->ndm_pad1	 = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags	 = flags;
	ndm->ndm_type	 = 0;
	ndm->ndm_ifindex = dev->ifindex;
	ndm->ndm_state   = NUD_PERMANENT;

	if (nla_put(skb, NDA_LLADDR, ETH_ALEN, addr))
		goto nla_put_failure;

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static inline size_t rtnl_fdb_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ndmsg)) + nla_total_size(ETH_ALEN);
}

static void rtnl_fdb_notify(struct net_device *dev, u8 *addr, int type)
{
	struct net *net = dev_net(dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(rtnl_fdb_nlmsg_size(), GFP_ATOMIC);
	if (!skb)
		goto errout;

	err = nlmsg_populate_fdb_fill(skb, dev, addr, 0, 0, type, NTF_SELF, 0);
	if (err < 0) {
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_NEIGH, NULL, GFP_ATOMIC);
	return;
errout:
	rtnl_set_sk_err(net, RTNLGRP_NEIGH, err);
}

/**
 * ndo_dflt_fdb_add - default netdevice operation to add an FDB entry
 */
int ndo_dflt_fdb_add(struct ndmsg *ndm,
		     struct nlattr *tb[],
		     struct net_device *dev,
		     const unsigned char *addr, u16 vid,
		     u16 flags)
{
	int err = -EINVAL;

	/* If aging addresses are supported device will need to
	 * implement its own handler for this.
	 */
	if (ndm->ndm_state && !(ndm->ndm_state & NUD_PERMANENT)) {
		pr_info("%s: FDB only supports static addresses\n", dev->name);
		return err;
	}

	if (is_unicast_ether_addr(addr) || is_link_local_ether_addr(addr))
		err = dev_uc_add_excl(dev, addr);
	else if (is_multicast_ether_addr(addr))
		err = dev_mc_add_excl(dev, addr);

	/* Only return duplicate errors if NLM_F_EXCL is set */
	if (err == -EEXIST && !(flags & NLM_F_EXCL))
		err = 0;

	return err;
}
EXPORT_SYMBOL(ndo_dflt_fdb_add);

static int fdb_vid_parse(struct nlattr *vlan_attr, u16 *p_vid)
{
	u16 vid = 0;

	if (vlan_attr) {
		if (nla_len(vlan_attr) != sizeof(u16)) {
			pr_info("PF_BRIDGE: RTM_NEWNEIGH with invalid vlan\n");
			return -EINVAL;
		}

		vid = nla_get_u16(vlan_attr);

		if (!vid || vid >= VLAN_VID_MASK) {
			pr_info("PF_BRIDGE: RTM_NEWNEIGH with invalid vlan id %d\n",
				vid);
			return -EINVAL;
		}
	}
	*p_vid = vid;
	return 0;
}

static int rtnl_fdb_add(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	struct ndmsg *ndm;
	struct nlattr *tb[NDA_MAX+1];
	struct net_device *dev;
	u8 *addr;
	u16 vid;
	int err;

	err = nlmsg_parse(nlh, sizeof(*ndm), tb, NDA_MAX, NULL);
	if (err < 0)
		return err;

	ndm = nlmsg_data(nlh);
	if (ndm->ndm_ifindex == 0) {
		pr_info("PF_BRIDGE: RTM_NEWNEIGH with invalid ifindex\n");
		return -EINVAL;
	}

	dev = __dev_get_by_index(net, ndm->ndm_ifindex);
	if (dev == NULL) {
		pr_info("PF_BRIDGE: RTM_NEWNEIGH with unknown ifindex\n");
		return -ENODEV;
	}

	if (!tb[NDA_LLADDR] || nla_len(tb[NDA_LLADDR]) != ETH_ALEN) {
		pr_info("PF_BRIDGE: RTM_NEWNEIGH with invalid address\n");
		return -EINVAL;
	}

	addr = nla_data(tb[NDA_LLADDR]);

	err = fdb_vid_parse(tb[NDA_VLAN], &vid);
	if (err)
		return err;

	err = -EOPNOTSUPP;

	/* Support fdb on master device the net/bridge default case */
	if ((!ndm->ndm_flags || ndm->ndm_flags & NTF_MASTER) &&
	    (dev->priv_flags & IFF_BRIDGE_PORT)) {
		struct net_device *br_dev = netdev_master_upper_dev_get(dev);
		const struct net_device_ops *ops = br_dev->netdev_ops;

		err = ops->ndo_fdb_add(ndm, tb, dev, addr, vid,
				       nlh->nlmsg_flags);
		if (err)
			goto out;
		else
			ndm->ndm_flags &= ~NTF_MASTER;
	}

	/* Embedded bridge, macvlan, and any other device support */
	if ((ndm->ndm_flags & NTF_SELF)) {
		if (dev->netdev_ops->ndo_fdb_add)
			err = dev->netdev_ops->ndo_fdb_add(ndm, tb, dev, addr,
							   vid,
							   nlh->nlmsg_flags);
		else
			err = ndo_dflt_fdb_add(ndm, tb, dev, addr, vid,
					       nlh->nlmsg_flags);

		if (!err) {
			rtnl_fdb_notify(dev, addr, RTM_NEWNEIGH);
			ndm->ndm_flags &= ~NTF_SELF;
		}
	}
out:
	return err;
}

/**
 * ndo_dflt_fdb_del - default netdevice operation to delete an FDB entry
 */
int ndo_dflt_fdb_del(struct ndmsg *ndm,
		     struct nlattr *tb[],
		     struct net_device *dev,
		     const unsigned char *addr, u16 vid)
{
	int err = -EINVAL;

	/* If aging addresses are supported device will need to
	 * implement its own handler for this.
	 */
	if (!(ndm->ndm_state & NUD_PERMANENT)) {
		pr_info("%s: FDB only supports static addresses\n", dev->name);
		return err;
	}

	if (is_unicast_ether_addr(addr) || is_link_local_ether_addr(addr))
		err = dev_uc_del(dev, addr);
	else if (is_multicast_ether_addr(addr))
		err = dev_mc_del(dev, addr);

	return err;
}
EXPORT_SYMBOL(ndo_dflt_fdb_del);

static int rtnl_fdb_del(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	struct ndmsg *ndm;
	struct nlattr *tb[NDA_MAX+1];
	struct net_device *dev;
	int err = -EINVAL;
	__u8 *addr;
	u16 vid;

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	err = nlmsg_parse(nlh, sizeof(*ndm), tb, NDA_MAX, NULL);
	if (err < 0)
		return err;

	ndm = nlmsg_data(nlh);
	if (ndm->ndm_ifindex == 0) {
		pr_info("PF_BRIDGE: RTM_DELNEIGH with invalid ifindex\n");
		return -EINVAL;
	}

	dev = __dev_get_by_index(net, ndm->ndm_ifindex);
	if (dev == NULL) {
		pr_info("PF_BRIDGE: RTM_DELNEIGH with unknown ifindex\n");
		return -ENODEV;
	}

	if (!tb[NDA_LLADDR] || nla_len(tb[NDA_LLADDR]) != ETH_ALEN) {
		pr_info("PF_BRIDGE: RTM_DELNEIGH with invalid address\n");
		return -EINVAL;
	}

	addr = nla_data(tb[NDA_LLADDR]);

	err = fdb_vid_parse(tb[NDA_VLAN], &vid);
	if (err)
		return err;

	err = -EOPNOTSUPP;

	/* Support fdb on master device the net/bridge default case */
	if ((!ndm->ndm_flags || ndm->ndm_flags & NTF_MASTER) &&
	    (dev->priv_flags & IFF_BRIDGE_PORT)) {
		struct net_device *br_dev = netdev_master_upper_dev_get(dev);
		const struct net_device_ops *ops = br_dev->netdev_ops;

		if (ops->ndo_fdb_del)
			err = ops->ndo_fdb_del(ndm, tb, dev, addr, vid);

		if (err)
			goto out;
		else
			ndm->ndm_flags &= ~NTF_MASTER;
	}

	/* Embedded bridge, macvlan, and any other device support */
	if (ndm->ndm_flags & NTF_SELF) {
		if (dev->netdev_ops->ndo_fdb_del)
			err = dev->netdev_ops->ndo_fdb_del(ndm, tb, dev, addr,
							   vid);
		else
			err = ndo_dflt_fdb_del(ndm, tb, dev, addr, vid);

		if (!err) {
			rtnl_fdb_notify(dev, addr, RTM_DELNEIGH);
			ndm->ndm_flags &= ~NTF_SELF;
		}
	}
out:
	return err;
}

static int nlmsg_populate_fdb(struct sk_buff *skb,
			      struct netlink_callback *cb,
			      struct net_device *dev,
			      int *idx,
			      struct netdev_hw_addr_list *list)
{
	struct netdev_hw_addr *ha;
	int err;
	u32 portid, seq;

	portid = NETLINK_CB(cb->skb).portid;
	seq = cb->nlh->nlmsg_seq;

	list_for_each_entry(ha, &list->list, list) {
		if (*idx < cb->args[0])
			goto skip;

		err = nlmsg_populate_fdb_fill(skb, dev, ha->addr,
					      portid, seq,
					      RTM_NEWNEIGH, NTF_SELF,
					      NLM_F_MULTI);
		if (err < 0)
			return err;
skip:
		*idx += 1;
	}
	return 0;
}

/**
 * ndo_dflt_fdb_dump - default netdevice operation to dump an FDB table.
 * @nlh: netlink message header
 * @dev: netdevice
 *
 * Default netdevice operation to dump the existing unicast address list.
 * Returns number of addresses from list put in skb.
 */
int ndo_dflt_fdb_dump(struct sk_buff *skb,
		      struct netlink_callback *cb,
		      struct net_device *dev,
		      struct net_device *filter_dev,
		      int idx)
{
	int err;

	netif_addr_lock_bh(dev);
	err = nlmsg_populate_fdb(skb, cb, dev, &idx, &dev->uc);
	if (err)
		goto out;
	nlmsg_populate_fdb(skb, cb, dev, &idx, &dev->mc);
out:
	netif_addr_unlock_bh(dev);
	return idx;
}
EXPORT_SYMBOL(ndo_dflt_fdb_dump);

static int rtnl_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net_device *dev;
	struct nlattr *tb[IFLA_MAX+1];
	struct net_device *bdev = NULL;
	struct net_device *br_dev = NULL;
	const struct net_device_ops *ops = NULL;
	const struct net_device_ops *cops = NULL;
	struct ifinfomsg *ifm = nlmsg_data(cb->nlh);
	struct net *net = sock_net(skb->sk);
	int brport_idx = 0;
	int br_idx = 0;
	int idx = 0;

	if (nlmsg_parse(cb->nlh, sizeof(struct ifinfomsg), tb, IFLA_MAX,
			ifla_policy) == 0) {
		if (tb[IFLA_MASTER])
			br_idx = nla_get_u32(tb[IFLA_MASTER]);
	}

	brport_idx = ifm->ifi_index;

	if (br_idx) {
		br_dev = __dev_get_by_index(net, br_idx);
		if (!br_dev)
			return -ENODEV;

		ops = br_dev->netdev_ops;
		bdev = br_dev;
	}

	for_each_netdev(net, dev) {
		if (brport_idx && (dev->ifindex != brport_idx))
			continue;

		if (!br_idx) { /* user did not specify a specific bridge */
			if (dev->priv_flags & IFF_BRIDGE_PORT) {
				br_dev = netdev_master_upper_dev_get(dev);
				cops = br_dev->netdev_ops;
			}

			bdev = dev;
		} else {
			if (dev != br_dev &&
			    !(dev->priv_flags & IFF_BRIDGE_PORT))
				continue;

			if (br_dev != netdev_master_upper_dev_get(dev) &&
			    !(dev->priv_flags & IFF_EBRIDGE))
				continue;

			bdev = br_dev;
			cops = ops;
		}

		if (dev->priv_flags & IFF_BRIDGE_PORT) {
			if (cops && cops->ndo_fdb_dump)
				idx = cops->ndo_fdb_dump(skb, cb, br_dev, dev,
							 idx);
		}

		idx = ndo_dflt_fdb_dump(skb, cb, dev, NULL, idx);
		if (dev->netdev_ops->ndo_fdb_dump)
			idx = dev->netdev_ops->ndo_fdb_dump(skb, cb, bdev, dev,
							    idx);

		cops = NULL;
	}

	cb->args[0] = idx;
	return skb->len;
}

static int brport_nla_put_flag(struct sk_buff *skb, u32 flags, u32 mask,
			       unsigned int attrnum, unsigned int flag)
{
	if (mask & flag)
		return nla_put_u8(skb, attrnum, !!(flags & flag));
	return 0;
}

int ndo_dflt_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
			    struct net_device *dev, u16 mode,
			    u32 flags, u32 mask)
{
	struct nlmsghdr *nlh;
	struct ifinfomsg *ifm;
	struct nlattr *br_afspec;
	struct nlattr *protinfo;
	u8 operstate = netif_running(dev) ? dev->operstate : IF_OPER_DOWN;
	struct net_device *br_dev = netdev_master_upper_dev_get(dev);

	nlh = nlmsg_put(skb, pid, seq, RTM_NEWLINK, sizeof(*ifm), NLM_F_MULTI);
	if (nlh == NULL)
		return -EMSGSIZE;

	ifm = nlmsg_data(nlh);
	ifm->ifi_family = AF_BRIDGE;
	ifm->__ifi_pad = 0;
	ifm->ifi_type = dev->type;
	ifm->ifi_index = dev->ifindex;
	ifm->ifi_flags = dev_get_flags(dev);
	ifm->ifi_change = 0;


	if (nla_put_string(skb, IFLA_IFNAME, dev->name) ||
	    nla_put_u32(skb, IFLA_MTU, dev->mtu) ||
	    nla_put_u8(skb, IFLA_OPERSTATE, operstate) ||
	    (br_dev &&
	     nla_put_u32(skb, IFLA_MASTER, br_dev->ifindex)) ||
	    (dev->addr_len &&
	     nla_put(skb, IFLA_ADDRESS, dev->addr_len, dev->dev_addr)) ||
	    (dev->ifindex != dev->iflink &&
	     nla_put_u32(skb, IFLA_LINK, dev->iflink)))
		goto nla_put_failure;

	br_afspec = nla_nest_start(skb, IFLA_AF_SPEC);
	if (!br_afspec)
		goto nla_put_failure;

	if (nla_put_u16(skb, IFLA_BRIDGE_FLAGS, BRIDGE_FLAGS_SELF) ||
	    nla_put_u16(skb, IFLA_BRIDGE_MODE, mode)) {
		nla_nest_cancel(skb, br_afspec);
		goto nla_put_failure;
	}
	nla_nest_end(skb, br_afspec);

	protinfo = nla_nest_start(skb, IFLA_PROTINFO | NLA_F_NESTED);
	if (!protinfo)
		goto nla_put_failure;

	if (brport_nla_put_flag(skb, flags, mask,
				IFLA_BRPORT_MODE, BR_HAIRPIN_MODE) ||
	    brport_nla_put_flag(skb, flags, mask,
				IFLA_BRPORT_GUARD, BR_BPDU_GUARD) ||
	    brport_nla_put_flag(skb, flags, mask,
				IFLA_BRPORT_FAST_LEAVE,
				BR_MULTICAST_FAST_LEAVE) ||
	    brport_nla_put_flag(skb, flags, mask,
				IFLA_BRPORT_PROTECT, BR_ROOT_BLOCK) ||
	    brport_nla_put_flag(skb, flags, mask,
				IFLA_BRPORT_LEARNING, BR_LEARNING) ||
	    brport_nla_put_flag(skb, flags, mask,
				IFLA_BRPORT_LEARNING_SYNC, BR_LEARNING_SYNC) ||
	    brport_nla_put_flag(skb, flags, mask,
				IFLA_BRPORT_UNICAST_FLOOD, BR_FLOOD) ||
	    brport_nla_put_flag(skb, flags, mask,
				IFLA_BRPORT_PROXYARP, BR_PROXYARP)) {
		nla_nest_cancel(skb, protinfo);
		goto nla_put_failure;
	}

	nla_nest_end(skb, protinfo);

	return nlmsg_end(skb, nlh);
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}
EXPORT_SYMBOL(ndo_dflt_bridge_getlink);

static int rtnl_bridge_getlink(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	int idx = 0;
	u32 portid = NETLINK_CB(cb->skb).portid;
	u32 seq = cb->nlh->nlmsg_seq;
	u32 filter_mask = 0;

	if (nlmsg_len(cb->nlh) > sizeof(struct ifinfomsg)) {
		struct nlattr *extfilt;

		extfilt = nlmsg_find_attr(cb->nlh, sizeof(struct ifinfomsg),
					  IFLA_EXT_MASK);
		if (extfilt) {
			if (nla_len(extfilt) < sizeof(filter_mask))
				return -EINVAL;

			filter_mask = nla_get_u32(extfilt);
		}
	}

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		const struct net_device_ops *ops = dev->netdev_ops;
		struct net_device *br_dev = netdev_master_upper_dev_get(dev);

		if (br_dev && br_dev->netdev_ops->ndo_bridge_getlink) {
			if (idx >= cb->args[0] &&
			    br_dev->netdev_ops->ndo_bridge_getlink(
				    skb, portid, seq, dev, filter_mask) < 0)
				break;
			idx++;
		}

		if (ops->ndo_bridge_getlink) {
			if (idx >= cb->args[0] &&
			    ops->ndo_bridge_getlink(skb, portid, seq, dev,
						    filter_mask) < 0)
				break;
			idx++;
		}
	}
	rcu_read_unlock();
	cb->args[0] = idx;

	return skb->len;
}

static inline size_t bridge_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ifinfomsg))
		+ nla_total_size(IFNAMSIZ)	/* IFLA_IFNAME */
		+ nla_total_size(MAX_ADDR_LEN)	/* IFLA_ADDRESS */
		+ nla_total_size(sizeof(u32))	/* IFLA_MASTER */
		+ nla_total_size(sizeof(u32))	/* IFLA_MTU */
		+ nla_total_size(sizeof(u32))	/* IFLA_LINK */
		+ nla_total_size(sizeof(u32))	/* IFLA_OPERSTATE */
		+ nla_total_size(sizeof(u8))	/* IFLA_PROTINFO */
		+ nla_total_size(sizeof(struct nlattr))	/* IFLA_AF_SPEC */
		+ nla_total_size(sizeof(u16))	/* IFLA_BRIDGE_FLAGS */
		+ nla_total_size(sizeof(u16));	/* IFLA_BRIDGE_MODE */
}

static int rtnl_bridge_notify(struct net_device *dev, u16 flags)
{
	struct net *net = dev_net(dev);
	struct net_device *br_dev = netdev_master_upper_dev_get(dev);
	struct sk_buff *skb;
	int err = -EOPNOTSUPP;

	skb = nlmsg_new(bridge_nlmsg_size(), GFP_ATOMIC);
	if (!skb) {
		err = -ENOMEM;
		goto errout;
	}

	if ((!flags || (flags & BRIDGE_FLAGS_MASTER)) &&
	    br_dev && br_dev->netdev_ops->ndo_bridge_getlink) {
		err = br_dev->netdev_ops->ndo_bridge_getlink(skb, 0, 0, dev, 0);
		if (err < 0)
			goto errout;
	}

	if ((flags & BRIDGE_FLAGS_SELF) &&
	    dev->netdev_ops->ndo_bridge_getlink) {
		err = dev->netdev_ops->ndo_bridge_getlink(skb, 0, 0, dev, 0);
		if (err < 0)
			goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_LINK, NULL, GFP_ATOMIC);
	return 0;
errout:
	WARN_ON(err == -EMSGSIZE);
	kfree_skb(skb);
	rtnl_set_sk_err(net, RTNLGRP_LINK, err);
	return err;
}

static int rtnl_bridge_setlink(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	struct ifinfomsg *ifm;
	struct net_device *dev;
	struct nlattr *br_spec, *attr = NULL;
	int rem, err = -EOPNOTSUPP;
	u16 oflags, flags = 0;
	bool have_flags = false;

	if (nlmsg_len(nlh) < sizeof(*ifm))
		return -EINVAL;

	ifm = nlmsg_data(nlh);
	if (ifm->ifi_family != AF_BRIDGE)
		return -EPFNOSUPPORT;

	dev = __dev_get_by_index(net, ifm->ifi_index);
	if (!dev) {
		pr_info("PF_BRIDGE: RTM_SETLINK with unknown ifindex\n");
		return -ENODEV;
	}

	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (br_spec) {
		nla_for_each_nested(attr, br_spec, rem) {
			if (nla_type(attr) == IFLA_BRIDGE_FLAGS) {
				if (nla_len(attr) < sizeof(flags))
					return -EINVAL;

				have_flags = true;
				flags = nla_get_u16(attr);
				break;
			}
		}
	}

	oflags = flags;

	if (!flags || (flags & BRIDGE_FLAGS_MASTER)) {
		struct net_device *br_dev = netdev_master_upper_dev_get(dev);

		if (!br_dev || !br_dev->netdev_ops->ndo_bridge_setlink) {
			err = -EOPNOTSUPP;
			goto out;
		}

		err = br_dev->netdev_ops->ndo_bridge_setlink(dev, nlh);
		if (err)
			goto out;

		flags &= ~BRIDGE_FLAGS_MASTER;
	}

	if ((flags & BRIDGE_FLAGS_SELF)) {
		if (!dev->netdev_ops->ndo_bridge_setlink)
			err = -EOPNOTSUPP;
		else
			err = dev->netdev_ops->ndo_bridge_setlink(dev, nlh);

		if (!err)
			flags &= ~BRIDGE_FLAGS_SELF;
	}

	if (have_flags)
		memcpy(nla_data(attr), &flags, sizeof(flags));
	/* Generate event to notify upper layer of bridge change */
	if (!err)
		err = rtnl_bridge_notify(dev, oflags);
out:
	return err;
}

static int rtnl_bridge_dellink(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	struct ifinfomsg *ifm;
	struct net_device *dev;
	struct nlattr *br_spec, *attr = NULL;
	int rem, err = -EOPNOTSUPP;
	u16 oflags, flags = 0;
	bool have_flags = false;

	if (nlmsg_len(nlh) < sizeof(*ifm))
		return -EINVAL;

	ifm = nlmsg_data(nlh);
	if (ifm->ifi_family != AF_BRIDGE)
		return -EPFNOSUPPORT;

	dev = __dev_get_by_index(net, ifm->ifi_index);
	if (!dev) {
		pr_info("PF_BRIDGE: RTM_SETLINK with unknown ifindex\n");
		return -ENODEV;
	}

	br_spec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);
	if (br_spec) {
		nla_for_each_nested(attr, br_spec, rem) {
			if (nla_type(attr) == IFLA_BRIDGE_FLAGS) {
				if (nla_len(attr) < sizeof(flags))
					return -EINVAL;

				have_flags = true;
				flags = nla_get_u16(attr);
				break;
			}
		}
	}

	oflags = flags;

	if (!flags || (flags & BRIDGE_FLAGS_MASTER)) {
		struct net_device *br_dev = netdev_master_upper_dev_get(dev);

		if (!br_dev || !br_dev->netdev_ops->ndo_bridge_dellink) {
			err = -EOPNOTSUPP;
			goto out;
		}

		err = br_dev->netdev_ops->ndo_bridge_dellink(dev, nlh);
		if (err)
			goto out;

		flags &= ~BRIDGE_FLAGS_MASTER;
	}

	if ((flags & BRIDGE_FLAGS_SELF)) {
		if (!dev->netdev_ops->ndo_bridge_dellink)
			err = -EOPNOTSUPP;
		else
			err = dev->netdev_ops->ndo_bridge_dellink(dev, nlh);

		if (!err)
			flags &= ~BRIDGE_FLAGS_SELF;
	}

	if (have_flags)
		memcpy(nla_data(attr), &flags, sizeof(flags));
	/* Generate event to notify upper layer of bridge change */
	if (!err)
		err = rtnl_bridge_notify(dev, oflags);
out:
	return err;
}

/* Process one rtnetlink message. */

static int rtnetlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	rtnl_doit_func doit;
	int sz_idx, kind;
	int family;
	int type;
	int err;

	type = nlh->nlmsg_type;
	if (type > RTM_MAX)
		return -EOPNOTSUPP;

	type -= RTM_BASE;

	/* All the messages must have at least 1 byte length */
	if (nlmsg_len(nlh) < sizeof(struct rtgenmsg))
		return 0;

	family = ((struct rtgenmsg *)nlmsg_data(nlh))->rtgen_family;
	sz_idx = type>>2;
	kind = type&3;

	if (kind != 2 && !netlink_net_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (kind == 2 && nlh->nlmsg_flags&NLM_F_DUMP) {
		struct sock *rtnl;
		rtnl_dumpit_func dumpit;
		rtnl_calcit_func calcit;
		u16 min_dump_alloc = 0;

		dumpit = rtnl_get_dumpit(family, type);
		if (dumpit == NULL)
			return -EOPNOTSUPP;
		calcit = rtnl_get_calcit(family, type);
		if (calcit)
			min_dump_alloc = calcit(skb, nlh);

		__rtnl_unlock();
		rtnl = net->rtnl;
		{
			struct netlink_dump_control c = {
				.dump		= dumpit,
				.min_dump_alloc	= min_dump_alloc,
			};
			err = netlink_dump_start(rtnl, skb, nlh, &c);
		}
		rtnl_lock();
		return err;
	}

	doit = rtnl_get_doit(family, type);
	if (doit == NULL)
		return -EOPNOTSUPP;

	return doit(skb, nlh);
}

static void rtnetlink_rcv(struct sk_buff *skb)
{
	rtnl_lock();
	netlink_rcv_skb(skb, &rtnetlink_rcv_msg);
	rtnl_unlock();
}

static int rtnetlink_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_UP:
	case NETDEV_DOWN:
	case NETDEV_PRE_UP:
	case NETDEV_POST_INIT:
	case NETDEV_REGISTER:
	case NETDEV_CHANGE:
	case NETDEV_PRE_TYPE_CHANGE:
	case NETDEV_GOING_DOWN:
	case NETDEV_UNREGISTER:
	case NETDEV_UNREGISTER_FINAL:
	case NETDEV_RELEASE:
	case NETDEV_JOIN:
		break;
	default:
		rtmsg_ifinfo(RTM_NEWLINK, dev, 0, GFP_KERNEL);
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
	struct netlink_kernel_cfg cfg = {
		.groups		= RTNLGRP_MAX,
		.input		= rtnetlink_rcv,
		.cb_mutex	= &rtnl_mutex,
		.flags		= NL_CFG_F_NONROOT_RECV,
	};

	sk = netlink_kernel_create(net, NETLINK_ROUTE, &cfg);
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
	if (register_pernet_subsys(&rtnetlink_net_ops))
		panic("rtnetlink_init: cannot initialize rtnetlink\n");

	register_netdevice_notifier(&rtnetlink_dev_notifier);

	rtnl_register(PF_UNSPEC, RTM_GETLINK, rtnl_getlink,
		      rtnl_dump_ifinfo, rtnl_calcit);
	rtnl_register(PF_UNSPEC, RTM_SETLINK, rtnl_setlink, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_NEWLINK, rtnl_newlink, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_DELLINK, rtnl_dellink, NULL, NULL);

	rtnl_register(PF_UNSPEC, RTM_GETADDR, NULL, rtnl_dump_all, NULL);
	rtnl_register(PF_UNSPEC, RTM_GETROUTE, NULL, rtnl_dump_all, NULL);

	rtnl_register(PF_BRIDGE, RTM_NEWNEIGH, rtnl_fdb_add, NULL, NULL);
	rtnl_register(PF_BRIDGE, RTM_DELNEIGH, rtnl_fdb_del, NULL, NULL);
	rtnl_register(PF_BRIDGE, RTM_GETNEIGH, NULL, rtnl_fdb_dump, NULL);

	rtnl_register(PF_BRIDGE, RTM_GETLINK, NULL, rtnl_bridge_getlink, NULL);
	rtnl_register(PF_BRIDGE, RTM_DELLINK, rtnl_bridge_dellink, NULL, NULL);
	rtnl_register(PF_BRIDGE, RTM_SETLINK, rtnl_bridge_setlink, NULL, NULL);
}

