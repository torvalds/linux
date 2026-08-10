// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/core/dev_addr_lists.c - Functions for handling net device lists
 * Copyright (c) 2010 Jiri Pirko <jpirko@redhat.com>
 *
 * This file contains functions for working with unicast, multicast and device
 * addresses lists.
 */

#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/export.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <kunit/visibility.h>

#include "dev.h"

/*
 * General list handling functions
 */

static int __hw_addr_insert(struct netdev_hw_addr_list *list,
			    struct netdev_hw_addr *new, int addr_len)
{
	struct rb_node **ins_point = &list->tree.rb_node, *parent = NULL;
	struct netdev_hw_addr *ha;

	while (*ins_point) {
		int diff;

		ha = rb_entry(*ins_point, struct netdev_hw_addr, node);
		diff = memcmp(new->addr, ha->addr, addr_len);
		if (diff == 0)
			diff = memcmp(&new->type, &ha->type, sizeof(new->type));

		parent = *ins_point;
		if (diff < 0)
			ins_point = &parent->rb_left;
		else if (diff > 0)
			ins_point = &parent->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node_rcu(&new->node, parent, ins_point);
	rb_insert_color(&new->node, &list->tree);

	return 0;
}

static struct netdev_hw_addr*
__hw_addr_create(const unsigned char *addr, int addr_len,
		 unsigned char addr_type, bool global, bool sync)
{
	struct netdev_hw_addr *ha;
	int alloc_size;

	alloc_size = sizeof(*ha);
	if (alloc_size < L1_CACHE_BYTES)
		alloc_size = L1_CACHE_BYTES;
	ha = kmalloc(alloc_size, GFP_ATOMIC);
	if (!ha)
		return NULL;
	memcpy(ha->addr, addr, addr_len);
	ha->type = addr_type;
	ha->refcount = 1;
	ha->global_use = global;
	ha->synced = sync ? 1 : 0;
	ha->sync_cnt = 0;

	return ha;
}

static int __hw_addr_add_ex(struct netdev_hw_addr_list *list,
			    const unsigned char *addr, int addr_len,
			    unsigned char addr_type, bool global, bool sync,
			    int sync_count, bool exclusive)
{
	struct rb_node **ins_point = &list->tree.rb_node, *parent = NULL;
	struct netdev_hw_addr *ha;

	if (addr_len > MAX_ADDR_LEN)
		return -EINVAL;

	while (*ins_point) {
		int diff;

		ha = rb_entry(*ins_point, struct netdev_hw_addr, node);
		diff = memcmp(addr, ha->addr, addr_len);
		if (diff == 0)
			diff = memcmp(&addr_type, &ha->type, sizeof(addr_type));

		parent = *ins_point;
		if (diff < 0) {
			ins_point = &parent->rb_left;
		} else if (diff > 0) {
			ins_point = &parent->rb_right;
		} else {
			if (exclusive)
				return -EEXIST;
			if (global) {
				/* check if addr is already used as global */
				if (ha->global_use)
					return 0;
				else
					ha->global_use = true;
			}
			if (sync) {
				if (ha->synced && sync_count)
					return -EEXIST;
				else
					ha->synced++;
			}
			ha->refcount++;
			return 0;
		}
	}

	ha = __hw_addr_create(addr, addr_len, addr_type, global, sync);
	if (!ha)
		return -ENOMEM;

	rb_link_node(&ha->node, parent, ins_point);
	rb_insert_color(&ha->node, &list->tree);

	list_add_tail_rcu(&ha->list, &list->list);
	list->count++;

	return 0;
}

static int __hw_addr_add(struct netdev_hw_addr_list *list,
			 const unsigned char *addr, int addr_len,
			 unsigned char addr_type)
{
	return __hw_addr_add_ex(list, addr, addr_len, addr_type, false, false,
				0, false);
}

static int __hw_addr_del_entry(struct netdev_hw_addr_list *list,
			       struct netdev_hw_addr *ha, bool global,
			       bool sync)
{
	if (global && !ha->global_use)
		return -ENOENT;

	if (sync && !ha->synced)
		return -ENOENT;

	if (global)
		ha->global_use = false;

	if (sync)
		ha->synced--;

	if (--ha->refcount)
		return 0;

	rb_erase(&ha->node, &list->tree);

	list_del_rcu(&ha->list);
	kfree_rcu(ha, rcu_head);
	list->count--;
	return 0;
}

static struct netdev_hw_addr *__hw_addr_lookup(struct netdev_hw_addr_list *list,
					       const unsigned char *addr, int addr_len,
					       unsigned char addr_type)
{
	struct rb_node *node;

	node = list->tree.rb_node;

	while (node) {
		struct netdev_hw_addr *ha = rb_entry(node, struct netdev_hw_addr, node);
		int diff = memcmp(addr, ha->addr, addr_len);

		if (diff == 0 && addr_type)
			diff = memcmp(&addr_type, &ha->type, sizeof(addr_type));

		if (diff < 0)
			node = node->rb_left;
		else if (diff > 0)
			node = node->rb_right;
		else
			return ha;
	}

	return NULL;
}

static int __hw_addr_del_ex(struct netdev_hw_addr_list *list,
			    const unsigned char *addr, int addr_len,
			    unsigned char addr_type, bool global, bool sync)
{
	struct netdev_hw_addr *ha = __hw_addr_lookup(list, addr, addr_len, addr_type);

	if (!ha)
		return -ENOENT;
	return __hw_addr_del_entry(list, ha, global, sync);
}

static int __hw_addr_del(struct netdev_hw_addr_list *list,
			 const unsigned char *addr, int addr_len,
			 unsigned char addr_type)
{
	return __hw_addr_del_ex(list, addr, addr_len, addr_type, false, false);
}

static int __hw_addr_sync_one(struct netdev_hw_addr_list *to_list,
			       struct netdev_hw_addr *ha,
			       int addr_len)
{
	int err;

	err = __hw_addr_add_ex(to_list, ha->addr, addr_len, ha->type,
			       false, true, ha->sync_cnt, false);
	if (err && err != -EEXIST)
		return err;

	if (!err) {
		ha->sync_cnt++;
		ha->refcount++;
	}

	return 0;
}

static void __hw_addr_unsync_one(struct netdev_hw_addr_list *to_list,
				 struct netdev_hw_addr_list *from_list,
				 struct netdev_hw_addr *ha,
				 int addr_len)
{
	int err;

	err = __hw_addr_del_ex(to_list, ha->addr, addr_len, ha->type,
			       false, true);
	if (err)
		return;
	ha->sync_cnt--;
	/* address on from list is not marked synced */
	__hw_addr_del_entry(from_list, ha, false, false);
}

int __hw_addr_sync_multiple(struct netdev_hw_addr_list *to_list,
			    struct netdev_hw_addr_list *from_list,
			    int addr_len)
{
	int err = 0;
	struct netdev_hw_addr *ha, *tmp;

	list_for_each_entry_safe(ha, tmp, &from_list->list, list) {
		if (ha->sync_cnt == ha->refcount) {
			__hw_addr_unsync_one(to_list, from_list, ha, addr_len);
		} else {
			err = __hw_addr_sync_one(to_list, ha, addr_len);
			if (err)
				break;
		}
	}
	return err;
}
EXPORT_SYMBOL(__hw_addr_sync_multiple);

/* This function only works where there is a strict 1-1 relationship
 * between source and destination of they synch. If you ever need to
 * sync addresses to more then 1 destination, you need to use
 * __hw_addr_sync_multiple().
 */
int __hw_addr_sync(struct netdev_hw_addr_list *to_list,
		   struct netdev_hw_addr_list *from_list,
		   int addr_len)
{
	int err = 0;
	struct netdev_hw_addr *ha, *tmp;

	list_for_each_entry_safe(ha, tmp, &from_list->list, list) {
		if (!ha->sync_cnt) {
			err = __hw_addr_sync_one(to_list, ha, addr_len);
			if (err)
				break;
		} else if (ha->refcount == 1)
			__hw_addr_unsync_one(to_list, from_list, ha, addr_len);
	}
	return err;
}
EXPORT_SYMBOL(__hw_addr_sync);

void __hw_addr_unsync(struct netdev_hw_addr_list *to_list,
		      struct netdev_hw_addr_list *from_list,
		      int addr_len)
{
	struct netdev_hw_addr *ha, *tmp;

	list_for_each_entry_safe(ha, tmp, &from_list->list, list) {
		if (ha->sync_cnt)
			__hw_addr_unsync_one(to_list, from_list, ha, addr_len);
	}
}
EXPORT_SYMBOL(__hw_addr_unsync);

/**
 *  __hw_addr_sync_dev - Synchronize device's multicast list
 *  @list: address list to synchronize
 *  @dev:  device to sync
 *  @sync: function to call if address should be added
 *  @unsync: function to call if address should be removed
 *
 *  This function is intended to be called from the ndo_set_rx_mode
 *  function of devices that require explicit address add/remove
 *  notifications.  The unsync function may be NULL in which case
 *  the addresses requiring removal will simply be removed without
 *  any notification to the device.
 **/
int __hw_addr_sync_dev(struct netdev_hw_addr_list *list,
		       struct net_device *dev,
		       int (*sync)(struct net_device *, const unsigned char *),
		       int (*unsync)(struct net_device *,
				     const unsigned char *))
{
	struct netdev_hw_addr *ha, *tmp;
	int err;

	/* first go through and flush out any stale entries */
	list_for_each_entry_safe(ha, tmp, &list->list, list) {
		if (!ha->sync_cnt || ha->refcount != 1)
			continue;

		/* if unsync is defined and fails defer unsyncing address */
		if (unsync && unsync(dev, ha->addr))
			continue;

		ha->sync_cnt--;
		__hw_addr_del_entry(list, ha, false, false);
	}

	/* go through and sync new entries to the list */
	list_for_each_entry_safe(ha, tmp, &list->list, list) {
		if (ha->sync_cnt)
			continue;

		err = sync(dev, ha->addr);
		if (err)
			return err;

		ha->sync_cnt++;
		ha->refcount++;
	}

	return 0;
}
EXPORT_SYMBOL(__hw_addr_sync_dev);

/**
 *  __hw_addr_ref_sync_dev - Synchronize device's multicast address list taking
 *  into account references
 *  @list: address list to synchronize
 *  @dev:  device to sync
 *  @sync: function to call if address or reference on it should be added
 *  @unsync: function to call if address or some reference on it should removed
 *
 *  This function is intended to be called from the ndo_set_rx_mode
 *  function of devices that require explicit address or references on it
 *  add/remove notifications. The unsync function may be NULL in which case
 *  the addresses or references on it requiring removal will simply be
 *  removed without any notification to the device. That is responsibility of
 *  the driver to identify and distribute address or references on it between
 *  internal address tables.
 **/
int __hw_addr_ref_sync_dev(struct netdev_hw_addr_list *list,
			   struct net_device *dev,
			   int (*sync)(struct net_device *,
				       const unsigned char *, int),
			   int (*unsync)(struct net_device *,
					 const unsigned char *, int))
{
	struct netdev_hw_addr *ha, *tmp;
	int err, ref_cnt;

	/* first go through and flush out any unsynced/stale entries */
	list_for_each_entry_safe(ha, tmp, &list->list, list) {
		/* sync if address is not used */
		if ((ha->sync_cnt << 1) <= ha->refcount)
			continue;

		/* if fails defer unsyncing address */
		ref_cnt = ha->refcount - ha->sync_cnt;
		if (unsync && unsync(dev, ha->addr, ref_cnt))
			continue;

		ha->refcount = (ref_cnt << 1) + 1;
		ha->sync_cnt = ref_cnt;
		__hw_addr_del_entry(list, ha, false, false);
	}

	/* go through and sync updated/new entries to the list */
	list_for_each_entry_safe(ha, tmp, &list->list, list) {
		/* sync if address added or reused */
		if ((ha->sync_cnt << 1) >= ha->refcount)
			continue;

		ref_cnt = ha->refcount - ha->sync_cnt;
		err = sync(dev, ha->addr, ref_cnt);
		if (err)
			return err;

		ha->refcount = ref_cnt << 1;
		ha->sync_cnt = ref_cnt;
	}

	return 0;
}
EXPORT_SYMBOL(__hw_addr_ref_sync_dev);

/**
 *  __hw_addr_ref_unsync_dev - Remove synchronized addresses and references on
 *  it from device
 *  @list: address list to remove synchronized addresses (references on it) from
 *  @dev:  device to sync
 *  @unsync: function to call if address and references on it should be removed
 *
 *  Remove all addresses that were added to the device by
 *  __hw_addr_ref_sync_dev(). This function is intended to be called from the
 *  ndo_stop or ndo_open functions on devices that require explicit address (or
 *  references on it) add/remove notifications. If the unsync function pointer
 *  is NULL then this function can be used to just reset the sync_cnt for the
 *  addresses in the list.
 **/
void __hw_addr_ref_unsync_dev(struct netdev_hw_addr_list *list,
			      struct net_device *dev,
			      int (*unsync)(struct net_device *,
					    const unsigned char *, int))
{
	struct netdev_hw_addr *ha, *tmp;

	list_for_each_entry_safe(ha, tmp, &list->list, list) {
		if (!ha->sync_cnt)
			continue;

		/* if fails defer unsyncing address */
		if (unsync && unsync(dev, ha->addr, ha->sync_cnt))
			continue;

		ha->refcount -= ha->sync_cnt - 1;
		ha->sync_cnt = 0;
		__hw_addr_del_entry(list, ha, false, false);
	}
}
EXPORT_SYMBOL(__hw_addr_ref_unsync_dev);

/**
 *  __hw_addr_unsync_dev - Remove synchronized addresses from device
 *  @list: address list to remove synchronized addresses from
 *  @dev:  device to sync
 *  @unsync: function to call if address should be removed
 *
 *  Remove all addresses that were added to the device by __hw_addr_sync_dev().
 *  This function is intended to be called from the ndo_stop or ndo_open
 *  functions on devices that require explicit address add/remove
 *  notifications.  If the unsync function pointer is NULL then this function
 *  can be used to just reset the sync_cnt for the addresses in the list.
 **/
void __hw_addr_unsync_dev(struct netdev_hw_addr_list *list,
			  struct net_device *dev,
			  int (*unsync)(struct net_device *,
					const unsigned char *))
{
	struct netdev_hw_addr *ha, *tmp;

	list_for_each_entry_safe(ha, tmp, &list->list, list) {
		if (!ha->sync_cnt)
			continue;

		/* if unsync is defined and fails defer unsyncing address */
		if (unsync && unsync(dev, ha->addr))
			continue;

		ha->sync_cnt--;
		__hw_addr_del_entry(list, ha, false, false);
	}
}
EXPORT_SYMBOL(__hw_addr_unsync_dev);

void __hw_addr_flush(struct netdev_hw_addr_list *list)
{
	struct netdev_hw_addr *ha, *tmp;

	list->tree = RB_ROOT;
	list_for_each_entry_safe(ha, tmp, &list->list, list) {
		list_del_rcu(&ha->list);
		kfree_rcu(ha, rcu_head);
	}
	list->count = 0;
}
EXPORT_SYMBOL_IF_KUNIT(__hw_addr_flush);

void __hw_addr_init(struct netdev_hw_addr_list *list)
{
	INIT_LIST_HEAD(&list->list);
	list->count = 0;
	list->tree = RB_ROOT;
}
EXPORT_SYMBOL(__hw_addr_init);

static void __hw_addr_splice(struct netdev_hw_addr_list *dst,
			     struct netdev_hw_addr_list *src)
{
	src->tree = RB_ROOT;
	list_splice_init(&src->list, &dst->list);
	dst->count += src->count;
	src->count = 0;
}

/**
 *  __hw_addr_list_snapshot - create a snapshot copy of an address list
 *  @snap: destination snapshot list (needs to be __hw_addr_init-initialized)
 *  @list: source address list to snapshot
 *  @addr_len: length of addresses
 *  @cache: entry cache to reuse entries from; falls back to GFP_ATOMIC
 *
 *  Creates a copy of @list reusing entries from @cache when available.
 *  Must be called under a spinlock.
 *
 *  Return: 0 on success, -errno on failure.
 */
int __hw_addr_list_snapshot(struct netdev_hw_addr_list *snap,
			    const struct netdev_hw_addr_list *list,
			    int addr_len, struct netdev_hw_addr_list *cache)
{
	struct netdev_hw_addr *ha, *entry;

	list_for_each_entry(ha, &list->list, list) {
		if (cache->count) {
			entry = list_first_entry(&cache->list,
						 struct netdev_hw_addr, list);
			list_del(&entry->list);
			cache->count--;
			memcpy(entry->addr, ha->addr, addr_len);
			entry->type = ha->type;
			entry->global_use = false;
			entry->synced = 0;
		} else {
			entry = __hw_addr_create(ha->addr, addr_len, ha->type,
						 false, false);
			if (!entry) {
				__hw_addr_flush(snap);
				return -ENOMEM;
			}
		}
		entry->sync_cnt = ha->sync_cnt;
		entry->refcount = ha->refcount;

		list_add_tail(&entry->list, &snap->list);
		__hw_addr_insert(snap, entry, addr_len);
		snap->count++;
	}

	return 0;
}
EXPORT_SYMBOL_IF_KUNIT(__hw_addr_list_snapshot);

/**
 *  __hw_addr_list_reconcile - sync snapshot changes back and free snapshots
 *  @real_list: the real address list to update
 *  @work: the working snapshot (modified by driver via __hw_addr_sync_dev)
 *  @ref: the reference snapshot (untouched copy of original state)
 *  @addr_len: length of addresses
 *  @cache: entry cache to return snapshot entries to for reuse
 *
 *  Walks the reference snapshot and compares each entry against the work
 *  snapshot to compute sync_cnt deltas. Applies those deltas to @real_list.
 *  Returns snapshot entries to @cache for reuse; frees both snapshots.
 *  Caller must hold netif_addr_lock_bh.
 */
void __hw_addr_list_reconcile(struct netdev_hw_addr_list *real_list,
			      struct netdev_hw_addr_list *work,
			      struct netdev_hw_addr_list *ref, int addr_len,
			      struct netdev_hw_addr_list *cache)
{
	struct netdev_hw_addr *ref_ha, *tmp, *work_ha, *real_ha;
	int delta;

	list_for_each_entry_safe(ref_ha, tmp, &ref->list, list) {
		work_ha = __hw_addr_lookup(work, ref_ha->addr, addr_len,
					   ref_ha->type);
		if (work_ha)
			delta = work_ha->sync_cnt - ref_ha->sync_cnt;
		else
			delta = -1;

		if (delta == 0)
			continue;

		real_ha = __hw_addr_lookup(real_list, ref_ha->addr, addr_len,
					   ref_ha->type);
		if (!real_ha) {
			/* The real entry was concurrently removed. If the
			 * driver synced this addr to hardware (delta > 0),
			 * re-insert it as a stale entry so the next work
			 * run unsyncs it from hardware.
			 */
			if (delta > 0) {
				rb_erase(&ref_ha->node, &ref->tree);
				list_del(&ref_ha->list);
				ref->count--;
				ref_ha->sync_cnt = delta;
				ref_ha->refcount = delta;
				list_add_tail_rcu(&ref_ha->list,
						  &real_list->list);
				__hw_addr_insert(real_list, ref_ha,
						 addr_len);
				real_list->count++;
			}
			continue;
		}

		real_ha->sync_cnt += delta;
		real_ha->refcount += delta;
		if (!real_ha->refcount) {
			rb_erase(&real_ha->node, &real_list->tree);
			list_del_rcu(&real_ha->list);
			kfree_rcu(real_ha, rcu_head);
			real_list->count--;
		}
	}

	__hw_addr_splice(cache, work);
	__hw_addr_splice(cache, ref);
}
EXPORT_SYMBOL_IF_KUNIT(__hw_addr_list_reconcile);

/*
 * Device addresses handling functions
 */

/* Check that netdev->dev_addr is not written to directly as this would
 * break the rbtree layout. All changes should go thru dev_addr_set() and co.
 * Remove this check in mid-2024.
 */
void dev_addr_check(struct net_device *dev)
{
	if (!memcmp(dev->dev_addr, dev->dev_addr_shadow, MAX_ADDR_LEN))
		return;

	netdev_warn(dev, "Current addr:  %*ph\n", MAX_ADDR_LEN, dev->dev_addr);
	netdev_warn(dev, "Expected addr: %*ph\n",
		    MAX_ADDR_LEN, dev->dev_addr_shadow);
	netdev_WARN(dev, "Incorrect netdev->dev_addr\n");
}

/**
 *	dev_addr_flush - Flush device address list
 *	@dev: device
 *
 *	Flush device address list and reset ->dev_addr.
 *
 *	The caller must hold the rtnl_mutex.
 */
void dev_addr_flush(struct net_device *dev)
{
	/* rtnl_mutex must be held here */
	dev_addr_check(dev);

	__hw_addr_flush(&dev->dev_addrs);
	dev->dev_addr = NULL;
}

/**
 *	dev_addr_init - Init device address list
 *	@dev: device
 *
 *	Init device address list and create the first element,
 *	used by ->dev_addr.
 *
 *	The caller must hold the rtnl_mutex.
 */
int dev_addr_init(struct net_device *dev)
{
	unsigned char addr[MAX_ADDR_LEN];
	struct netdev_hw_addr *ha;
	int err;

	/* rtnl_mutex must be held here */

	__hw_addr_init(&dev->dev_addrs);
	memset(addr, 0, sizeof(addr));
	err = __hw_addr_add(&dev->dev_addrs, addr, sizeof(addr),
			    NETDEV_HW_ADDR_T_LAN);
	if (!err) {
		/*
		 * Get the first (previously created) address from the list
		 * and set dev_addr pointer to this location.
		 */
		ha = list_first_entry(&dev->dev_addrs.list,
				      struct netdev_hw_addr, list);
		dev->dev_addr = ha->addr;
	}
	return err;
}

void dev_addr_mod(struct net_device *dev, unsigned int offset,
		  const void *addr, size_t len)
{
	struct netdev_hw_addr *ha;

	dev_addr_check(dev);

	ha = container_of(dev->dev_addr, struct netdev_hw_addr, addr[0]);
	rb_erase(&ha->node, &dev->dev_addrs.tree);
	memcpy(&ha->addr[offset], addr, len);
	memcpy(&dev->dev_addr_shadow[offset], addr, len);
	WARN_ON(__hw_addr_insert(&dev->dev_addrs, ha, dev->addr_len));
}
EXPORT_SYMBOL(dev_addr_mod);

/**
 *	dev_addr_add - Add a device address
 *	@dev: device
 *	@addr: address to add
 *	@addr_type: address type
 *
 *	Add a device address to the device or increase the reference count if
 *	it already exists.
 *
 *	The caller must hold the rtnl_mutex.
 */
int dev_addr_add(struct net_device *dev, const unsigned char *addr,
		 unsigned char addr_type)
{
	int err;

	ASSERT_RTNL();

	err = netif_pre_changeaddr_notify(dev, addr, NULL);
	if (err)
		return err;
	err = __hw_addr_add(&dev->dev_addrs, addr, dev->addr_len, addr_type);
	if (!err)
		call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
	return err;
}
EXPORT_SYMBOL(dev_addr_add);

/**
 *	dev_addr_del - Release a device address.
 *	@dev: device
 *	@addr: address to delete
 *	@addr_type: address type
 *
 *	Release reference to a device address and remove it from the device
 *	if the reference count drops to zero.
 *
 *	The caller must hold the rtnl_mutex.
 */
int dev_addr_del(struct net_device *dev, const unsigned char *addr,
		 unsigned char addr_type)
{
	int err;
	struct netdev_hw_addr *ha;

	ASSERT_RTNL();

	/*
	 * We can not remove the first address from the list because
	 * dev->dev_addr points to that.
	 */
	ha = list_first_entry(&dev->dev_addrs.list,
			      struct netdev_hw_addr, list);
	if (!memcmp(ha->addr, addr, dev->addr_len) &&
	    ha->type == addr_type && ha->refcount == 1)
		return -ENOENT;

	err = __hw_addr_del(&dev->dev_addrs, addr, dev->addr_len,
			    addr_type);
	if (!err)
		call_netdevice_notifiers(NETDEV_CHANGEADDR, dev);
	return err;
}
EXPORT_SYMBOL(dev_addr_del);

/*
 * Unicast list handling functions
 */

/**
 *	dev_uc_add_excl - Add a global secondary unicast address
 *	@dev: device
 *	@addr: address to add
 */
int dev_uc_add_excl(struct net_device *dev, const unsigned char *addr)
{
	int err;

	netif_addr_lock_bh(dev);
	err = __hw_addr_add_ex(&dev->uc, addr, dev->addr_len,
			       NETDEV_HW_ADDR_T_UNICAST, true, false,
			       0, true);
	if (!err)
		__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
	return err;
}
EXPORT_SYMBOL(dev_uc_add_excl);

/**
 *	dev_uc_add - Add a secondary unicast address
 *	@dev: device
 *	@addr: address to add
 *
 *	Add a secondary unicast address to the device or increase
 *	the reference count if it already exists.
 */
int dev_uc_add(struct net_device *dev, const unsigned char *addr)
{
	int err;

	netif_addr_lock_bh(dev);
	err = __hw_addr_add(&dev->uc, addr, dev->addr_len,
			    NETDEV_HW_ADDR_T_UNICAST);
	if (!err)
		__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
	return err;
}
EXPORT_SYMBOL(dev_uc_add);

/**
 *	dev_uc_del - Release secondary unicast address.
 *	@dev: device
 *	@addr: address to delete
 *
 *	Release reference to a secondary unicast address and remove it
 *	from the device if the reference count drops to zero.
 */
int dev_uc_del(struct net_device *dev, const unsigned char *addr)
{
	int err;

	netif_addr_lock_bh(dev);
	err = __hw_addr_del(&dev->uc, addr, dev->addr_len,
			    NETDEV_HW_ADDR_T_UNICAST);
	if (!err)
		__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
	return err;
}
EXPORT_SYMBOL(dev_uc_del);

/**
 *	dev_uc_sync - Synchronize device's unicast list to another device
 *	@to: destination device
 *	@from: source device
 *
 *	Add newly added addresses to the destination device and release
 *	addresses that have no users left. The source device must be
 *	locked by netif_addr_lock_bh.
 *
 *	This function is intended to be called from the dev->set_rx_mode
 *	function of layered software devices.  This function assumes that
 *	addresses will only ever be synced to the @to devices and no other.
 */
int dev_uc_sync(struct net_device *to, struct net_device *from)
{
	int err = 0;

	if (to->addr_len != from->addr_len)
		return -EINVAL;

	netif_addr_lock(to);
	err = __hw_addr_sync(&to->uc, &from->uc, to->addr_len);
	if (!err)
		__dev_set_rx_mode(to);
	netif_addr_unlock(to);
	return err;
}
EXPORT_SYMBOL(dev_uc_sync);

/**
 *	dev_uc_sync_multiple - Synchronize device's unicast list to another
 *	device, but allow for multiple calls to sync to multiple devices.
 *	@to: destination device
 *	@from: source device
 *
 *	Add newly added addresses to the destination device and release
 *	addresses that have been deleted from the source. The source device
 *	must be locked by netif_addr_lock_bh.
 *
 *	This function is intended to be called from the dev->set_rx_mode
 *	function of layered software devices.  It allows for a single source
 *	device to be synced to multiple destination devices.
 */
int dev_uc_sync_multiple(struct net_device *to, struct net_device *from)
{
	int err = 0;

	if (to->addr_len != from->addr_len)
		return -EINVAL;

	netif_addr_lock(to);
	err = __hw_addr_sync_multiple(&to->uc, &from->uc, to->addr_len);
	if (!err)
		__dev_set_rx_mode(to);
	netif_addr_unlock(to);
	return err;
}
EXPORT_SYMBOL(dev_uc_sync_multiple);

/**
 *	dev_uc_unsync - Remove synchronized addresses from the destination device
 *	@to: destination device
 *	@from: source device
 *
 *	Remove all addresses that were added to the destination device by
 *	dev_uc_sync(). This function is intended to be called from the
 *	dev->stop function of layered software devices.
 */
void dev_uc_unsync(struct net_device *to, struct net_device *from)
{
	if (to->addr_len != from->addr_len)
		return;

	/* netif_addr_lock_bh() uses lockdep subclass 0, this is okay for two
	 * reasons:
	 * 1) This is always called without any addr_list_lock, so as the
	 *    outermost one here, it must be 0.
	 * 2) This is called by some callers after unlinking the upper device,
	 *    so the dev->lower_level becomes 1 again.
	 * Therefore, the subclass for 'from' is 0, for 'to' is either 1 or
	 * larger.
	 */
	netif_addr_lock_bh(from);
	netif_addr_lock(to);
	__hw_addr_unsync(&to->uc, &from->uc, to->addr_len);
	__dev_set_rx_mode(to);
	netif_addr_unlock(to);
	netif_addr_unlock_bh(from);
}
EXPORT_SYMBOL(dev_uc_unsync);

/**
 *	dev_uc_flush - Flush unicast addresses
 *	@dev: device
 *
 *	Flush unicast addresses.
 */
void dev_uc_flush(struct net_device *dev)
{
	netif_addr_lock_bh(dev);
	__hw_addr_flush(&dev->uc);
	netif_addr_unlock_bh(dev);
}
EXPORT_SYMBOL(dev_uc_flush);

/**
 *	dev_uc_init - Init unicast address list
 *	@dev: device
 *
 *	Init unicast address list.
 */
void dev_uc_init(struct net_device *dev)
{
	__hw_addr_init(&dev->uc);
}
EXPORT_SYMBOL(dev_uc_init);

/*
 * Multicast list handling functions
 */

/**
 *	dev_mc_add_excl - Add a global secondary multicast address
 *	@dev: device
 *	@addr: address to add
 */
int dev_mc_add_excl(struct net_device *dev, const unsigned char *addr)
{
	int err;

	netif_addr_lock_bh(dev);
	err = __hw_addr_add_ex(&dev->mc, addr, dev->addr_len,
			       NETDEV_HW_ADDR_T_MULTICAST, true, false,
			       0, true);
	if (!err)
		__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
	return err;
}
EXPORT_SYMBOL(dev_mc_add_excl);

static int __dev_mc_add(struct net_device *dev, const unsigned char *addr,
			bool global)
{
	int err;

	netif_addr_lock_bh(dev);
	err = __hw_addr_add_ex(&dev->mc, addr, dev->addr_len,
			       NETDEV_HW_ADDR_T_MULTICAST, global, false,
			       0, false);
	if (!err)
		__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
	return err;
}
/**
 *	dev_mc_add - Add a multicast address
 *	@dev: device
 *	@addr: address to add
 *
 *	Add a multicast address to the device or increase
 *	the reference count if it already exists.
 */
int dev_mc_add(struct net_device *dev, const unsigned char *addr)
{
	return __dev_mc_add(dev, addr, false);
}
EXPORT_SYMBOL(dev_mc_add);

/**
 *	dev_mc_add_global - Add a global multicast address
 *	@dev: device
 *	@addr: address to add
 *
 *	Add a global multicast address to the device.
 */
int dev_mc_add_global(struct net_device *dev, const unsigned char *addr)
{
	return __dev_mc_add(dev, addr, true);
}
EXPORT_SYMBOL(dev_mc_add_global);

static int __dev_mc_del(struct net_device *dev, const unsigned char *addr,
			bool global)
{
	int err;

	netif_addr_lock_bh(dev);
	err = __hw_addr_del_ex(&dev->mc, addr, dev->addr_len,
			       NETDEV_HW_ADDR_T_MULTICAST, global, false);
	if (!err)
		__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
	return err;
}

/**
 *	dev_mc_del - Delete a multicast address.
 *	@dev: device
 *	@addr: address to delete
 *
 *	Release reference to a multicast address and remove it
 *	from the device if the reference count drops to zero.
 */
int dev_mc_del(struct net_device *dev, const unsigned char *addr)
{
	return __dev_mc_del(dev, addr, false);
}
EXPORT_SYMBOL(dev_mc_del);

/**
 *	dev_mc_del_global - Delete a global multicast address.
 *	@dev: device
 *	@addr: address to delete
 *
 *	Release reference to a multicast address and remove it
 *	from the device if the reference count drops to zero.
 */
int dev_mc_del_global(struct net_device *dev, const unsigned char *addr)
{
	return __dev_mc_del(dev, addr, true);
}
EXPORT_SYMBOL(dev_mc_del_global);

/**
 *	dev_mc_sync - Synchronize device's multicast list to another device
 *	@to: destination device
 *	@from: source device
 *
 *	Add newly added addresses to the destination device and release
 *	addresses that have no users left. The source device must be
 *	locked by netif_addr_lock_bh.
 *
 *	This function is intended to be called from the ndo_set_rx_mode
 *	function of layered software devices.
 */
int dev_mc_sync(struct net_device *to, struct net_device *from)
{
	int err = 0;

	if (to->addr_len != from->addr_len)
		return -EINVAL;

	netif_addr_lock(to);
	err = __hw_addr_sync(&to->mc, &from->mc, to->addr_len);
	if (!err)
		__dev_set_rx_mode(to);
	netif_addr_unlock(to);
	return err;
}
EXPORT_SYMBOL(dev_mc_sync);

/**
 *	dev_mc_sync_multiple - Synchronize device's multicast list to another
 *	device, but allow for multiple calls to sync to multiple devices.
 *	@to: destination device
 *	@from: source device
 *
 *	Add newly added addresses to the destination device and release
 *	addresses that have no users left. The source device must be
 *	locked by netif_addr_lock_bh.
 *
 *	This function is intended to be called from the ndo_set_rx_mode
 *	function of layered software devices.  It allows for a single
 *	source device to be synced to multiple destination devices.
 */
int dev_mc_sync_multiple(struct net_device *to, struct net_device *from)
{
	int err = 0;

	if (to->addr_len != from->addr_len)
		return -EINVAL;

	netif_addr_lock(to);
	err = __hw_addr_sync_multiple(&to->mc, &from->mc, to->addr_len);
	if (!err)
		__dev_set_rx_mode(to);
	netif_addr_unlock(to);
	return err;
}
EXPORT_SYMBOL(dev_mc_sync_multiple);

/**
 *	dev_mc_unsync - Remove synchronized addresses from the destination device
 *	@to: destination device
 *	@from: source device
 *
 *	Remove all addresses that were added to the destination device by
 *	dev_mc_sync(). This function is intended to be called from the
 *	dev->stop function of layered software devices.
 */
void dev_mc_unsync(struct net_device *to, struct net_device *from)
{
	if (to->addr_len != from->addr_len)
		return;

	/* See the above comments inside dev_uc_unsync(). */
	netif_addr_lock_bh(from);
	netif_addr_lock(to);
	__hw_addr_unsync(&to->mc, &from->mc, to->addr_len);
	__dev_set_rx_mode(to);
	netif_addr_unlock(to);
	netif_addr_unlock_bh(from);
}
EXPORT_SYMBOL(dev_mc_unsync);

/**
 *	dev_mc_flush - Flush multicast addresses
 *	@dev: device
 *
 *	Flush multicast addresses.
 */
void dev_mc_flush(struct net_device *dev)
{
	netif_addr_lock_bh(dev);
	__hw_addr_flush(&dev->mc);
	netif_addr_unlock_bh(dev);
}
EXPORT_SYMBOL(dev_mc_flush);

/**
 *	dev_mc_init - Init multicast address list
 *	@dev: device
 *
 *	Init multicast address list.
 */
void dev_mc_init(struct net_device *dev)
{
	__hw_addr_init(&dev->mc);
}
EXPORT_SYMBOL(dev_mc_init);

static int netif_addr_lists_snapshot(struct net_device *dev,
				     struct netdev_hw_addr_list *uc_snap,
				     struct netdev_hw_addr_list *mc_snap,
				     struct netdev_hw_addr_list *uc_ref,
				     struct netdev_hw_addr_list *mc_ref)
{
	int err;

	err = __hw_addr_list_snapshot(uc_snap, &dev->uc, dev->addr_len,
				      &dev->rx_mode_addr_cache);
	if (!err)
		err = __hw_addr_list_snapshot(uc_ref, &dev->uc, dev->addr_len,
					      &dev->rx_mode_addr_cache);
	if (!err)
		err = __hw_addr_list_snapshot(mc_snap, &dev->mc,
					      dev->addr_len,
					      &dev->rx_mode_addr_cache);
	if (!err)
		err = __hw_addr_list_snapshot(mc_ref, &dev->mc, dev->addr_len,
					      &dev->rx_mode_addr_cache);

	if (err) {
		__hw_addr_flush(uc_snap);
		__hw_addr_flush(uc_ref);
		__hw_addr_flush(mc_snap);
	}

	return err;
}

static void netif_addr_lists_reconcile(struct net_device *dev,
				       struct netdev_hw_addr_list *uc_snap,
				       struct netdev_hw_addr_list *mc_snap,
				       struct netdev_hw_addr_list *uc_ref,
				       struct netdev_hw_addr_list *mc_ref)
{
	__hw_addr_list_reconcile(&dev->uc, uc_snap, uc_ref, dev->addr_len,
				 &dev->rx_mode_addr_cache);
	__hw_addr_list_reconcile(&dev->mc, mc_snap, mc_ref, dev->addr_len,
				 &dev->rx_mode_addr_cache);
}

/**
 * netif_uc_promisc_update() - evaluate whether uc_promisc should be toggled.
 * @dev: device
 *
 * Must be called under netif_addr_lock_bh.
 * Return: +1 to enter promisc, -1 to leave, 0 for no change.
 */
static int netif_uc_promisc_update(struct net_device *dev)
{
	if (dev->priv_flags & IFF_UNICAST_FLT)
		return 0;

	if (!netdev_uc_empty(dev) && !dev->uc_promisc) {
		dev->uc_promisc = true;
		return 1;
	}
	if (netdev_uc_empty(dev) && dev->uc_promisc) {
		dev->uc_promisc = false;
		return -1;
	}
	return 0;
}

/* Total retry budget (4): 1+2+4+8 = 15 seconds */
#define NETIF_RX_MODE_RETRY_MAX	4

void netif_rx_mode_schedule_retry(struct net_device *dev)
{
	unsigned long delay;

	netdev_assert_locked_ops_compat(dev);

	if (dev->rx_mode_retry_count >= NETIF_RX_MODE_RETRY_MAX) {
		netdev_err(dev, "rx_mode retry limit reached, giving up\n");
		return;
	}

	delay = HZ << dev->rx_mode_retry_count;
	if (mod_timer(&dev->rx_mode_retry_timer, jiffies + delay))
		return;
	if (!dev->rx_mode_retry_count)
		netdev_info(dev, "rx_mode install failed, retrying with backoff\n");
	dev->rx_mode_retry_count++;
}
EXPORT_SYMBOL_GPL(netif_rx_mode_schedule_retry);

void netif_rx_mode_cancel_retry(struct net_device *dev)
{
	timer_delete_sync(&dev->rx_mode_retry_timer);
	dev->rx_mode_retry_count = 0;
}

void netif_rx_mode_run(struct net_device *dev)
{
	struct netdev_hw_addr_list uc_snap, mc_snap, uc_ref, mc_ref;
	const struct net_device_ops *ops = dev->netdev_ops;
	int promisc_inc;
	int err;

	might_sleep();
	netdev_assert_locked_ops_compat(dev);

	__hw_addr_init(&uc_snap);
	__hw_addr_init(&mc_snap);
	__hw_addr_init(&uc_ref);
	__hw_addr_init(&mc_ref);

	if (!(dev->flags & IFF_UP) || !netif_device_present(dev))
		return;

	if (ops->ndo_set_rx_mode_async) {
		netif_addr_lock_bh(dev);
		err = netif_addr_lists_snapshot(dev, &uc_snap, &mc_snap,
						&uc_ref, &mc_ref);
		if (err) {
			netif_addr_unlock_bh(dev);
			netif_rx_mode_schedule_retry(dev);
			return;
		}

		promisc_inc = netif_uc_promisc_update(dev);
		netif_addr_unlock_bh(dev);
	} else {
		netif_addr_lock_bh(dev);
		promisc_inc = netif_uc_promisc_update(dev);
		netif_addr_unlock_bh(dev);
	}

	if (promisc_inc)
		__dev_set_promiscuity(dev, promisc_inc, false);

	if (ops->ndo_set_rx_mode_async) {
		err = ops->ndo_set_rx_mode_async(dev, &uc_snap, &mc_snap);

		netif_addr_lock_bh(dev);
		netif_addr_lists_reconcile(dev, &uc_snap, &mc_snap,
					   &uc_ref, &mc_ref);
		netif_addr_unlock_bh(dev);

		if (err)
			netif_rx_mode_schedule_retry(dev);
		else
			dev->rx_mode_retry_count = 0;
	} else if (ops->ndo_set_rx_mode) {
		netif_addr_lock_bh(dev);
		ops->ndo_set_rx_mode(dev);
		netif_addr_unlock_bh(dev);
	}
}

static void netif_rx_mode_queue(struct net_device *dev)
{
	__netdev_work_core_sched(dev, NETDEV_WORK_RX_MODE);
}

static void netif_rx_mode_retry(struct timer_list *t)
{
	struct net_device *dev =
		timer_container_of(dev, t, rx_mode_retry_timer);

	netif_rx_mode_queue(dev);
}

void netif_rx_mode_init(struct net_device *dev)
{
	__hw_addr_init(&dev->rx_mode_addr_cache);
	timer_setup(&dev->rx_mode_retry_timer, netif_rx_mode_retry, 0);
}

/**
 * __dev_set_rx_mode() - upload unicast and multicast address lists to device
 * and configure RX filtering.
 * @dev: device
 *
 * When the device doesn't support unicast filtering it is put in promiscuous
 * mode while unicast addresses are present.
 */
void __dev_set_rx_mode(struct net_device *dev)
{
	const struct net_device_ops *ops = dev->netdev_ops;
	int promisc_inc;

	/* dev_open will call this function so the list will stay sane. */
	if (!(dev->flags & IFF_UP))
		return;

	if (!netif_device_present(dev))
		return;

	if (ops->ndo_set_rx_mode_async || ops->ndo_change_rx_flags ||
	    netdev_need_ops_lock(dev)) {
		netif_rx_mode_queue(dev);
		return;
	}

	/* Legacy path for non-ops-locked HW devices. */

	promisc_inc = netif_uc_promisc_update(dev);
	if (promisc_inc)
		__dev_set_promiscuity(dev, promisc_inc, false);

	if (ops->ndo_set_rx_mode)
		ops->ndo_set_rx_mode(dev);
}

void dev_set_rx_mode(struct net_device *dev)
{
	netif_addr_lock_bh(dev);
	__dev_set_rx_mode(dev);
	netif_addr_unlock_bh(dev);
}

/**
 * netif_rx_mode_sync() - sync rx mode inline
 * @dev: network device
 *
 * Drivers implementing ndo_set_rx_mode_async() have their rx mode callback
 * executed from a workqueue. This allows the callback to sleep, but means
 * the hardware update is deferred and may not be visible to userspace
 * by the time the initiating syscall returns. netif_rx_mode_sync() steals
 * workqueue update and executes it inline. This preserves the atomicity of
 * operations to the userspace.
 */
void netif_rx_mode_sync(struct net_device *dev)
{
	if (__netdev_work_core_cancel(dev, NETDEV_WORK_RX_MODE))
		netif_rx_mode_run(dev);
}
