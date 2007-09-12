/*
 * Network interface table.
 *
 * Network interfaces (devices) do not have a security field, so we
 * maintain a table associating each interface with a SID.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <net/net_namespace.h>

#include "security.h"
#include "objsec.h"
#include "netif.h"

#define SEL_NETIF_HASH_SIZE	64
#define SEL_NETIF_HASH_MAX	1024

#undef DEBUG

#ifdef DEBUG
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

struct sel_netif
{
	struct list_head list;
	struct netif_security_struct nsec;
	struct rcu_head rcu_head;
};

static u32 sel_netif_total;
static LIST_HEAD(sel_netif_list);
static DEFINE_SPINLOCK(sel_netif_lock);
static struct list_head sel_netif_hash[SEL_NETIF_HASH_SIZE];

static inline u32 sel_netif_hasfn(struct net_device *dev)
{
	return (dev->ifindex & (SEL_NETIF_HASH_SIZE - 1));
}

/*
 * All of the devices should normally fit in the hash, so we optimize
 * for that case.
 */
static inline struct sel_netif *sel_netif_find(struct net_device *dev)
{
	struct list_head *pos;
	int idx = sel_netif_hasfn(dev);

	__list_for_each_rcu(pos, &sel_netif_hash[idx]) {
		struct sel_netif *netif = list_entry(pos,
		                                     struct sel_netif, list);
		if (likely(netif->nsec.dev == dev))
			return netif;
	}
	return NULL;
}

static int sel_netif_insert(struct sel_netif *netif)
{
	int idx, ret = 0;
	
	if (sel_netif_total >= SEL_NETIF_HASH_MAX) {
		ret = -ENOSPC;
		goto out;
	}
	
	idx = sel_netif_hasfn(netif->nsec.dev);
	list_add_rcu(&netif->list, &sel_netif_hash[idx]);
	sel_netif_total++;
out:
	return ret;
}

static void sel_netif_free(struct rcu_head *p)
{
	struct sel_netif *netif = container_of(p, struct sel_netif, rcu_head);

	DEBUGP("%s: %s\n", __FUNCTION__, netif->nsec.dev->name);
	kfree(netif);
}

static void sel_netif_destroy(struct sel_netif *netif)
{
	DEBUGP("%s: %s\n", __FUNCTION__, netif->nsec.dev->name);

	list_del_rcu(&netif->list);
	sel_netif_total--;
	call_rcu(&netif->rcu_head, sel_netif_free);
}

static struct sel_netif *sel_netif_lookup(struct net_device *dev)
{
	int ret;
	struct sel_netif *netif, *new;
	struct netif_security_struct *nsec;

	netif = sel_netif_find(dev);
	if (likely(netif != NULL))
		goto out;
	
	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	if (!new) {
		netif = ERR_PTR(-ENOMEM);
		goto out;
	}
	
	nsec = &new->nsec;

	ret = security_netif_sid(dev->name, &nsec->if_sid, &nsec->msg_sid);
	if (ret < 0) {
		kfree(new);
		netif = ERR_PTR(ret);
		goto out;
	}

	nsec->dev = dev;
	
	spin_lock_bh(&sel_netif_lock);
	
	netif = sel_netif_find(dev);
	if (netif) {
		spin_unlock_bh(&sel_netif_lock);
		kfree(new);
		goto out;
	}
	
	ret = sel_netif_insert(new);
	spin_unlock_bh(&sel_netif_lock);
	
	if (ret) {
		kfree(new);
		netif = ERR_PTR(ret);
		goto out;
	}

	netif = new;
	
	DEBUGP("new: ifindex=%u name=%s if_sid=%u msg_sid=%u\n", dev->ifindex, dev->name,
	        nsec->if_sid, nsec->msg_sid);
out:
	return netif;
}

static void sel_netif_assign_sids(u32 if_sid_in, u32 msg_sid_in, u32 *if_sid_out, u32 *msg_sid_out)
{
	if (if_sid_out)
		*if_sid_out = if_sid_in;
	if (msg_sid_out)
		*msg_sid_out = msg_sid_in;
}

static int sel_netif_sids_slow(struct net_device *dev, u32 *if_sid, u32 *msg_sid)
{
	int ret = 0;
	u32 tmp_if_sid, tmp_msg_sid;
	
	ret = security_netif_sid(dev->name, &tmp_if_sid, &tmp_msg_sid);
	if (!ret)
		sel_netif_assign_sids(tmp_if_sid, tmp_msg_sid, if_sid, msg_sid);
	return ret;
}

int sel_netif_sids(struct net_device *dev, u32 *if_sid, u32 *msg_sid)
{
	int ret = 0;
	struct sel_netif *netif;

	rcu_read_lock();
	netif = sel_netif_lookup(dev);
	if (IS_ERR(netif)) {
		rcu_read_unlock();
		ret = sel_netif_sids_slow(dev, if_sid, msg_sid);
		goto out;
	}
	sel_netif_assign_sids(netif->nsec.if_sid, netif->nsec.msg_sid, if_sid, msg_sid);
	rcu_read_unlock();
out:
	return ret;
}

static void sel_netif_kill(struct net_device *dev)
{
	struct sel_netif *netif;

	spin_lock_bh(&sel_netif_lock);
	netif = sel_netif_find(dev);
	if (netif)
		sel_netif_destroy(netif);
	spin_unlock_bh(&sel_netif_lock);
}

static void sel_netif_flush(void)
{
	int idx;

	spin_lock_bh(&sel_netif_lock);
	for (idx = 0; idx < SEL_NETIF_HASH_SIZE; idx++) {
		struct sel_netif *netif;
		
		list_for_each_entry(netif, &sel_netif_hash[idx], list)
			sel_netif_destroy(netif);
	}
	spin_unlock_bh(&sel_netif_lock);
}

static int sel_netif_avc_callback(u32 event, u32 ssid, u32 tsid,
                                  u16 class, u32 perms, u32 *retained)
{
	if (event == AVC_CALLBACK_RESET) {
		sel_netif_flush();
		synchronize_net();
	}
	return 0;
}

static int sel_netif_netdev_notifier_handler(struct notifier_block *this,
                                             unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;

	if (dev->nd_net != &init_net)
		return NOTIFY_DONE;

	if (event == NETDEV_DOWN)
		sel_netif_kill(dev);

	return NOTIFY_DONE;
}

static struct notifier_block sel_netif_netdev_notifier = {
	.notifier_call = sel_netif_netdev_notifier_handler,
};

static __init int sel_netif_init(void)
{
	int i, err = 0;
	
	if (!selinux_enabled)
		goto out;

	for (i = 0; i < SEL_NETIF_HASH_SIZE; i++)
		INIT_LIST_HEAD(&sel_netif_hash[i]);

	register_netdevice_notifier(&sel_netif_netdev_notifier);
	
	err = avc_add_callback(sel_netif_avc_callback, AVC_CALLBACK_RESET,
	                       SECSID_NULL, SECSID_NULL, SECCLASS_NULL, 0);
	if (err)
		panic("avc_add_callback() failed, error %d\n", err);

out:
	return err;
}

__initcall(sel_netif_init);

