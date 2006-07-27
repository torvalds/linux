/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base: policy rules.
 *
 * Version:	$Id: fib_rules.c,v 1.17 2001/10/31 21:55:54 davem Exp $
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Fixes:
 * 		Rani Assaf	:	local_rule cannot be deleted
 *		Marc Boucher	:	routing by fwmark
 */

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
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/rcupdate.h>

#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/ip_fib.h>

#define FRprintk(a...)

struct fib_rule
{
	struct hlist_node hlist;
	atomic_t	r_clntref;
	u32		r_preference;
	unsigned char	r_table;
	unsigned char	r_action;
	unsigned char	r_dst_len;
	unsigned char	r_src_len;
	u32		r_src;
	u32		r_srcmask;
	u32		r_dst;
	u32		r_dstmask;
	u32		r_srcmap;
	u8		r_flags;
	u8		r_tos;
#ifdef CONFIG_IP_ROUTE_FWMARK
	u32		r_fwmark;
#endif
	int		r_ifindex;
#ifdef CONFIG_NET_CLS_ROUTE
	__u32		r_tclassid;
#endif
	char		r_ifname[IFNAMSIZ];
	int		r_dead;
	struct		rcu_head rcu;
};

static struct fib_rule default_rule = {
	.r_clntref =	ATOMIC_INIT(2),
	.r_preference =	0x7FFF,
	.r_table =	RT_TABLE_DEFAULT,
	.r_action =	RTN_UNICAST,
};

static struct fib_rule main_rule = {
	.r_clntref =	ATOMIC_INIT(2),
	.r_preference =	0x7FFE,
	.r_table =	RT_TABLE_MAIN,
	.r_action =	RTN_UNICAST,
};

static struct fib_rule local_rule = {
	.r_clntref =	ATOMIC_INIT(2),
	.r_table =	RT_TABLE_LOCAL,
	.r_action =	RTN_UNICAST,
};

static struct hlist_head fib_rules;

/* writer func called from netlink -- rtnl_sem hold*/

static void rtmsg_rule(int, struct fib_rule *);

int inet_rtm_delrule(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct rtmsg *rtm = NLMSG_DATA(nlh);
	struct fib_rule *r;
	struct hlist_node *node;
	int err = -ESRCH;

	hlist_for_each_entry(r, node, &fib_rules, hlist) {
		if ((!rta[RTA_SRC-1] || memcmp(RTA_DATA(rta[RTA_SRC-1]), &r->r_src, 4) == 0) &&
		    rtm->rtm_src_len == r->r_src_len &&
		    rtm->rtm_dst_len == r->r_dst_len &&
		    (!rta[RTA_DST-1] || memcmp(RTA_DATA(rta[RTA_DST-1]), &r->r_dst, 4) == 0) &&
		    rtm->rtm_tos == r->r_tos &&
#ifdef CONFIG_IP_ROUTE_FWMARK
		    (!rta[RTA_PROTOINFO-1] || memcmp(RTA_DATA(rta[RTA_PROTOINFO-1]), &r->r_fwmark, 4) == 0) &&
#endif
		    (!rtm->rtm_type || rtm->rtm_type == r->r_action) &&
		    (!rta[RTA_PRIORITY-1] || memcmp(RTA_DATA(rta[RTA_PRIORITY-1]), &r->r_preference, 4) == 0) &&
		    (!rta[RTA_IIF-1] || rtattr_strcmp(rta[RTA_IIF-1], r->r_ifname) == 0) &&
		    (!rtm->rtm_table || (r && rtm->rtm_table == r->r_table))) {
			err = -EPERM;
			if (r == &local_rule)
				break;

			hlist_del_rcu(&r->hlist);
			r->r_dead = 1;
			rtmsg_rule(RTM_DELRULE, r);
			fib_rule_put(r);
			err = 0;
			break;
		}
	}
	return err;
}

/* Allocate new unique table id */

static struct fib_table *fib_empty_table(void)
{
	int id;

	for (id = 1; id <= RT_TABLE_MAX; id++)
		if (fib_tables[id] == NULL)
			return __fib_new_table(id);
	return NULL;
}

static inline void fib_rule_put_rcu(struct rcu_head *head)
{
	struct fib_rule *r = container_of(head, struct fib_rule, rcu);
	kfree(r);
}

void fib_rule_put(struct fib_rule *r)
{
	if (atomic_dec_and_test(&r->r_clntref)) {
		if (r->r_dead)
			call_rcu(&r->rcu, fib_rule_put_rcu);
		else
			printk("Freeing alive rule %p\n", r);
	}
}

/* writer func called from netlink -- rtnl_sem hold*/

int inet_rtm_newrule(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct rtmsg *rtm = NLMSG_DATA(nlh);
	struct fib_rule *r, *new_r, *last = NULL;
	struct hlist_node *node = NULL;
	unsigned char table_id;

	if (rtm->rtm_src_len > 32 || rtm->rtm_dst_len > 32 ||
	    (rtm->rtm_tos & ~IPTOS_TOS_MASK))
		return -EINVAL;

	if (rta[RTA_IIF-1] && RTA_PAYLOAD(rta[RTA_IIF-1]) > IFNAMSIZ)
		return -EINVAL;

	table_id = rtm->rtm_table;
	if (table_id == RT_TABLE_UNSPEC) {
		struct fib_table *table;
		if (rtm->rtm_type == RTN_UNICAST) {
			if ((table = fib_empty_table()) == NULL)
				return -ENOBUFS;
			table_id = table->tb_id;
		}
	}

	new_r = kzalloc(sizeof(*new_r), GFP_KERNEL);
	if (!new_r)
		return -ENOMEM;

	if (rta[RTA_SRC-1])
		memcpy(&new_r->r_src, RTA_DATA(rta[RTA_SRC-1]), 4);
	if (rta[RTA_DST-1])
		memcpy(&new_r->r_dst, RTA_DATA(rta[RTA_DST-1]), 4);
	if (rta[RTA_GATEWAY-1])
		memcpy(&new_r->r_srcmap, RTA_DATA(rta[RTA_GATEWAY-1]), 4);
	new_r->r_src_len = rtm->rtm_src_len;
	new_r->r_dst_len = rtm->rtm_dst_len;
	new_r->r_srcmask = inet_make_mask(rtm->rtm_src_len);
	new_r->r_dstmask = inet_make_mask(rtm->rtm_dst_len);
	new_r->r_tos = rtm->rtm_tos;
#ifdef CONFIG_IP_ROUTE_FWMARK
	if (rta[RTA_PROTOINFO-1])
		memcpy(&new_r->r_fwmark, RTA_DATA(rta[RTA_PROTOINFO-1]), 4);
#endif
	new_r->r_action = rtm->rtm_type;
	new_r->r_flags = rtm->rtm_flags;
	if (rta[RTA_PRIORITY-1])
		memcpy(&new_r->r_preference, RTA_DATA(rta[RTA_PRIORITY-1]), 4);
	new_r->r_table = table_id;
	if (rta[RTA_IIF-1]) {
		struct net_device *dev;
		rtattr_strlcpy(new_r->r_ifname, rta[RTA_IIF-1], IFNAMSIZ);
		new_r->r_ifindex = -1;
		dev = __dev_get_by_name(new_r->r_ifname);
		if (dev)
			new_r->r_ifindex = dev->ifindex;
	}
#ifdef CONFIG_NET_CLS_ROUTE
	if (rta[RTA_FLOW-1])
		memcpy(&new_r->r_tclassid, RTA_DATA(rta[RTA_FLOW-1]), 4);
#endif
	r = container_of(fib_rules.first, struct fib_rule, hlist);

	if (!new_r->r_preference) {
		if (r && r->hlist.next != NULL) {
			r = container_of(r->hlist.next, struct fib_rule, hlist);
			if (r->r_preference)
				new_r->r_preference = r->r_preference - 1;
		}
	}

	hlist_for_each_entry(r, node, &fib_rules, hlist) {
		if (r->r_preference > new_r->r_preference)
			break;
		last = r;
	}
	atomic_inc(&new_r->r_clntref);

	if (last)
		hlist_add_after_rcu(&last->hlist, &new_r->hlist);
	else
		hlist_add_before_rcu(&new_r->hlist, &r->hlist);

	rtmsg_rule(RTM_NEWRULE, new_r);
	return 0;
}

#ifdef CONFIG_NET_CLS_ROUTE
u32 fib_rules_tclass(struct fib_result *res)
{
	if (res->r)
		return res->r->r_tclassid;
	return 0;
}
#endif

/* callers should hold rtnl semaphore */

static void fib_rules_detach(struct net_device *dev)
{
	struct hlist_node *node;
	struct fib_rule *r;

	hlist_for_each_entry(r, node, &fib_rules, hlist) {
		if (r->r_ifindex == dev->ifindex)
			r->r_ifindex = -1;

	}
}

/* callers should hold rtnl semaphore */

static void fib_rules_attach(struct net_device *dev)
{
	struct hlist_node *node;
	struct fib_rule *r;

	hlist_for_each_entry(r, node, &fib_rules, hlist) {
		if (r->r_ifindex == -1 && strcmp(dev->name, r->r_ifname) == 0)
			r->r_ifindex = dev->ifindex;
	}
}

int fib_lookup(const struct flowi *flp, struct fib_result *res)
{
	int err;
	struct fib_rule *r, *policy;
	struct fib_table *tb;
	struct hlist_node *node;

	u32 daddr = flp->fl4_dst;
	u32 saddr = flp->fl4_src;

FRprintk("Lookup: %u.%u.%u.%u <- %u.%u.%u.%u ",
	NIPQUAD(flp->fl4_dst), NIPQUAD(flp->fl4_src));

	rcu_read_lock();

	hlist_for_each_entry_rcu(r, node, &fib_rules, hlist) {
		if (((saddr^r->r_src) & r->r_srcmask) ||
		    ((daddr^r->r_dst) & r->r_dstmask) ||
		    (r->r_tos && r->r_tos != flp->fl4_tos) ||
#ifdef CONFIG_IP_ROUTE_FWMARK
		    (r->r_fwmark && r->r_fwmark != flp->fl4_fwmark) ||
#endif
		    (r->r_ifindex && r->r_ifindex != flp->iif))
			continue;

FRprintk("tb %d r %d ", r->r_table, r->r_action);
		switch (r->r_action) {
		case RTN_UNICAST:
			policy = r;
			break;
		case RTN_UNREACHABLE:
			rcu_read_unlock();
			return -ENETUNREACH;
		default:
		case RTN_BLACKHOLE:
			rcu_read_unlock();
			return -EINVAL;
		case RTN_PROHIBIT:
			rcu_read_unlock();
			return -EACCES;
		}

		if ((tb = fib_get_table(r->r_table)) == NULL)
			continue;
		err = tb->tb_lookup(tb, flp, res);
		if (err == 0) {
			res->r = policy;
			if (policy)
				atomic_inc(&policy->r_clntref);
			rcu_read_unlock();
			return 0;
		}
		if (err < 0 && err != -EAGAIN) {
			rcu_read_unlock();
			return err;
		}
	}
FRprintk("FAILURE\n");
	rcu_read_unlock();
	return -ENETUNREACH;
}

void fib_select_default(const struct flowi *flp, struct fib_result *res)
{
	if (res->r && res->r->r_action == RTN_UNICAST &&
	    FIB_RES_GW(*res) && FIB_RES_NH(*res).nh_scope == RT_SCOPE_LINK) {
		struct fib_table *tb;
		if ((tb = fib_get_table(res->r->r_table)) != NULL)
			tb->tb_select_default(tb, flp, res);
	}
}

static int fib_rules_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;

	if (event == NETDEV_UNREGISTER)
		fib_rules_detach(dev);
	else if (event == NETDEV_REGISTER)
		fib_rules_attach(dev);
	return NOTIFY_DONE;
}


static struct notifier_block fib_rules_notifier = {
	.notifier_call =fib_rules_event,
};

static __inline__ int inet_fill_rule(struct sk_buff *skb,
				     struct fib_rule *r,
				     u32 pid, u32 seq, int event,
				     unsigned int flags)
{
	struct rtmsg *rtm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_NEW(skb, pid, seq, event, sizeof(*rtm), flags);
	rtm = NLMSG_DATA(nlh);
	rtm->rtm_family = AF_INET;
	rtm->rtm_dst_len = r->r_dst_len;
	rtm->rtm_src_len = r->r_src_len;
	rtm->rtm_tos = r->r_tos;
#ifdef CONFIG_IP_ROUTE_FWMARK
	if (r->r_fwmark)
		RTA_PUT(skb, RTA_PROTOINFO, 4, &r->r_fwmark);
#endif
	rtm->rtm_table = r->r_table;
	rtm->rtm_protocol = 0;
	rtm->rtm_scope = 0;
	rtm->rtm_type = r->r_action;
	rtm->rtm_flags = r->r_flags;

	if (r->r_dst_len)
		RTA_PUT(skb, RTA_DST, 4, &r->r_dst);
	if (r->r_src_len)
		RTA_PUT(skb, RTA_SRC, 4, &r->r_src);
	if (r->r_ifname[0])
		RTA_PUT(skb, RTA_IIF, IFNAMSIZ, &r->r_ifname);
	if (r->r_preference)
		RTA_PUT(skb, RTA_PRIORITY, 4, &r->r_preference);
	if (r->r_srcmap)
		RTA_PUT(skb, RTA_GATEWAY, 4, &r->r_srcmap);
#ifdef CONFIG_NET_CLS_ROUTE
	if (r->r_tclassid)
		RTA_PUT(skb, RTA_FLOW, 4, &r->r_tclassid);
#endif
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

/* callers should hold rtnl semaphore */

static void rtmsg_rule(int event, struct fib_rule *r)
{
	int size = NLMSG_SPACE(sizeof(struct rtmsg) + 128);
	struct sk_buff *skb = alloc_skb(size, GFP_KERNEL);

	if (!skb)
		netlink_set_err(rtnl, 0, RTNLGRP_IPV4_RULE, ENOBUFS);
	else if (inet_fill_rule(skb, r, 0, 0, event, 0) < 0) {
		kfree_skb(skb);
		netlink_set_err(rtnl, 0, RTNLGRP_IPV4_RULE, EINVAL);
	} else {
		netlink_broadcast(rtnl, skb, 0, RTNLGRP_IPV4_RULE, GFP_KERNEL);
	}
}

int inet_dump_rules(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx = 0;
	int s_idx = cb->args[0];
	struct fib_rule *r;
	struct hlist_node *node;

	rcu_read_lock();
	hlist_for_each_entry(r, node, &fib_rules, hlist) {
		if (idx < s_idx)
			goto next;
		if (inet_fill_rule(skb, r, NETLINK_CB(cb->skb).pid,
				   cb->nlh->nlmsg_seq,
				   RTM_NEWRULE, NLM_F_MULTI) < 0)
			break;
next:
		idx++;
	}
	rcu_read_unlock();
	cb->args[0] = idx;

	return skb->len;
}

void __init fib_rules_init(void)
{
	INIT_HLIST_HEAD(&fib_rules);
	hlist_add_head(&local_rule.hlist, &fib_rules);
	hlist_add_after(&local_rule.hlist, &main_rule.hlist);
	hlist_add_after(&main_rule.hlist, &default_rule.hlist);
	register_netdevice_notifier(&fib_rules_notifier);
}
