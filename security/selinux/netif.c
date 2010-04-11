/*
 * Network interface table.
 *
 * Network interfaces (devices) do not have a security field, so we
 * maintain a table associating each interface with a SID.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2003 Red Hat, Inc., James Morris <jmorris@redhat.com>
 * Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *		      Paul Moore <paul.moore@hp.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
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

struct sel_netif {
	struct list_head list;
	struct netif_security_struct nsec;
	struct rcu_head rcu_head;
};

static u32 sel_netif_total;
static LIST_HEAD(sel_netif_list);
static DEFINE_SPINLOCK(sel_netif_lock);
static struct list_head sel_netif_hash[SEL_NETIF_HASH_SIZE];

/**
 * sel_netif_hashfn - Hashing function for the interface table
 * @ifindex: the network interface
 *
 * Description:
 * This is the hashing function for the network interface table, it returns the
 * bucket number for the given interface.
 *
 */
static inline u32 sel_netif_hashfn(int ifindex)
{
	return (ifindex & (SEL_NETIF_HASH_SIZE - 1));
}

/**
 * sel_netif_find - Search for an interface record
 * @ifindex: the network interface
 *
 * Description:
 * Search the network interface table and return the record matching @ifindex.
 * If an entry can not be found in the table return NULL.
 *
 */
static inline struct sel_netif *sel_netif_find(int ifindex)
{
	int idx = sel_netif_hashfn(ifindex);
	struct sel_netif *netif;

	list_for_each_entry_rcu(netif, &sel_netif_hash[idx], list)
		/* all of the devices should normally fit in the hash, so we
		 * optimize for that case */
		if (likely(netif->nsec.ifindex == ifindex))
			return netif;

	return NULL;
}

/**
 * sel_netif_insert - Insert a new interface into the table
 * @netif: the new interface record
 *
 * Description:
 * Add a new interface record to the network interface hash table.  Returns
 * zero on success, negative values on failure.
 *
 */
static int sel_netif_insert(struct sel_netif *netif)
{
	int idx;

	if (sel_netif_total >= SEL_NETIF_HASH_MAX)
		return -ENOSPC;

	idx = sel_netif_hashfn(netif->nsec.ifindex);
	list_add_rcu(&netif->list, &sel_netif_hash[idx]);
	sel_netif_total++;

	return 0;
}

/**
 * sel_netif_free - Frees an interface entry
 * @p: the entry's RCU field
 *
 * Description:
 * This function is designed to be used as a callback to the call_rcu()
 * function so that memory allocated to a hash table interface entry can be
 * released safely.
 *
 */
static void sel_netif_free(struct rcu_head *p)
{
	struct sel_netif *netif = container_of(p, struct sel_netif, rcu_head);
	kfree(netif);
}

/**
 * sel_netif_destroy - Remove an interface record from the table
 * @netif: the existing interface record
 *
 * Description:
 * Remove an existing interface record from the network interface table.
 *
 */
static void sel_netif_destroy(struct sel_netif *netif)
{
	list_del_rcu(&netif->list);
	sel_netif_total--;
	call_rcu(&netif->rcu_head, sel_netif_free);
}

/**
 * sel_netif_sid_slow - Lookup the SID of a network interface using the policy
 * @ifindex: the network interface
 * @sid: interface SID
 *
 * Description:
 * This function determines the SID of a network interface by quering the
 * security policy.  The result is added to the network interface table to
 * speedup future queries.  Returns zero on success, negative values on
 * failure.
 *
 */
static int sel_netif_sid_slow(int ifindex, u32 *sid)
{
	int ret;
	struct sel_netif *netif;
	struct sel_netif *new = NULL;
	struct net_device *dev;

	/* NOTE: we always use init's network namespace since we don't
	 * currently support containers */

	dev = dev_get_by_index(&init_net, ifindex);
	if (unlikely(dev == NULL)) {
		printk(KERN_WARNING
		       "SELinux: failure in sel_netif_sid_slow(),"
		       " invalid network interface (%d)\n", ifindex);
		return -ENOENT;
	}

	spin_lock_bh(&sel_netif_lock);
	netif = sel_netif_find(ifindex);
	if (netif != NULL) {
		*sid = netif->nsec.sid;
		ret = 0;
		goto out;
	}
	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	if (new == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	ret = security_netif_sid(dev->name, &new->nsec.sid);
	if (ret != 0)
		goto out;
	new->nsec.ifindex = ifindex;
	ret = sel_netif_insert(new);
	if (ret != 0)
		goto out;
	*sid = new->nsec.sid;

out:
	spin_unlock_bh(&sel_netif_lock);
	dev_put(dev);
	if (unlikely(ret)) {
		printk(KERN_WARNING
		       "SELinux: failure in sel_netif_sid_slow(),"
		       " unable to determine network interface label (%d)\n",
		       ifindex);
		kfree(new);
	}
	return ret;
}

/**
 * sel_netif_sid - Lookup the SID of a network interface
 * @ifindex: the network interface
 * @sid: interface SID
 *
 * Description:
 * This function determines the SID of a network interface using the fastest
 * method possible.  First the interface table is queried, but if an entry
 * can't be found then the policy is queried and the result is added to the
 * table to speedup future queries.  Returns zero on success, negative values
 * on failure.
 *
 */
int sel_netif_sid(int ifindex, u32 *sid)
{
	struct sel_netif *netif;

	rcu_read_lock();
	netif = sel_netif_find(ifindex);
	if (likely(netif != NULL)) {
		*sid = netif->nsec.sid;
		rcu_read_unlock();
		return 0;
	}
	rcu_read_unlock();

	return sel_netif_sid_slow(ifindex, sid);
}

/**
 * sel_netif_kill - Remove an entry from the network interface table
 * @ifindex: the network interface
 *
 * Description:
 * This function removes the entry matching @ifindex from the network interface
 * table if it exists.
 *
 */
static void sel_netif_kill(int ifindex)
{
	struct sel_netif *netif;

	rcu_read_lock();
	spin_lock_bh(&sel_netif_lock);
	netif = sel_netif_find(ifindex);
	if (netif)
		sel_netif_destroy(netif);
	spin_unlock_bh(&sel_netif_lock);
	rcu_read_unlock();
}

/**
 * sel_netif_flush - Flush the entire network interface table
 *
 * Description:
 * Remove all entries from the network interface table.
 *
 */
static void sel_netif_flush(void)
{
	int idx;
	struct sel_netif *netif;

	spin_lock_bh(&sel_netif_lock);
	for (idx = 0; idx < SEL_NETIF_HASH_SIZE; idx++)
		list_for_each_entry(netif, &sel_netif_hash[idx], list)
			sel_netif_destroy(netif);
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

	if (dev_net(dev) != &init_net)
		return NOTIFY_DONE;

	if (event == NETDEV_DOWN)
		sel_netif_kill(dev->ifindex);

	return NOTIFY_DONE;
}

static struct notifier_block sel_netif_netdev_notifier = {
	.notifier_call = sel_netif_netdev_notifier_handler,
};

static __init int sel_netif_init(void)
{
	int i, err;

	if (!selinux_enabled)
		return 0;

	for (i = 0; i < SEL_NETIF_HASH_SIZE; i++)
		INIT_LIST_HEAD(&sel_netif_hash[i]);

	register_netdevice_notifier(&sel_netif_netdev_notifier);

	err = avc_add_callback(sel_netif_avc_callback, AVC_CALLBACK_RESET,
			       SECSID_NULL, SECSID_NULL, SECCLASS_NULL, 0);
	if (err)
		panic("avc_add_callback() failed, error %d\n", err);

	return err;
}

__initcall(sel_netif_init);

