/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _NET_NETDEV_LOCK_H
#define _NET_NETDEV_LOCK_H

#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>

static inline bool netdev_trylock(struct net_device *dev)
{
	return mutex_trylock(&dev->lock);
}

static inline void netdev_assert_locked(const struct net_device *dev)
{
	lockdep_assert_held(&dev->lock);
}

static inline void
netdev_assert_locked_or_invisible(const struct net_device *dev)
{
	if (dev->reg_state == NETREG_REGISTERED ||
	    dev->reg_state == NETREG_UNREGISTERING)
		netdev_assert_locked(dev);
}

static inline bool netdev_need_ops_lock(const struct net_device *dev)
{
	bool ret = dev->request_ops_lock || !!dev->queue_mgmt_ops;

#if IS_ENABLED(CONFIG_NET_SHAPER)
	ret |= !!dev->netdev_ops->net_shaper_ops;
#endif

	return ret;
}

static inline void netdev_lock_ops(struct net_device *dev)
{
	if (netdev_need_ops_lock(dev))
		netdev_lock(dev);
}

static inline void netdev_unlock_ops(struct net_device *dev)
{
	if (netdev_need_ops_lock(dev))
		netdev_unlock(dev);
}

static inline void netdev_lock_ops_to_full(struct net_device *dev)
{
	if (netdev_need_ops_lock(dev))
		netdev_assert_locked(dev);
	else
		netdev_lock(dev);
}

static inline void netdev_unlock_full_to_ops(struct net_device *dev)
{
	if (netdev_need_ops_lock(dev))
		netdev_assert_locked(dev);
	else
		netdev_unlock(dev);
}

static inline void netdev_ops_assert_locked(const struct net_device *dev)
{
	if (netdev_need_ops_lock(dev))
		lockdep_assert_held(&dev->lock);
	else
		ASSERT_RTNL();
}

static inline void
netdev_ops_assert_locked_or_invisible(const struct net_device *dev)
{
	if (dev->reg_state == NETREG_REGISTERED ||
	    dev->reg_state == NETREG_UNREGISTERING)
		netdev_ops_assert_locked(dev);
}

static inline void netdev_lock_ops_compat(struct net_device *dev)
{
	if (netdev_need_ops_lock(dev))
		netdev_lock(dev);
	else
		rtnl_lock();
}

static inline void netdev_unlock_ops_compat(struct net_device *dev)
{
	if (netdev_need_ops_lock(dev))
		netdev_unlock(dev);
	else
		rtnl_unlock();
}

static inline int netdev_lock_cmp_fn(const struct lockdep_map *a,
				     const struct lockdep_map *b)
{
	if (a == b)
		return 0;

	/* Allow locking multiple devices only under rtnl_lock,
	 * the exact order doesn't matter.
	 * Note that upper devices don't lock their ops, so nesting
	 * mostly happens in batched device removal for now.
	 */
	return lockdep_rtnl_is_held() ? -1 : 1;
}

#define netdev_lockdep_set_classes(dev)				\
{								\
	static struct lock_class_key qdisc_tx_busylock_key;	\
	static struct lock_class_key qdisc_xmit_lock_key;	\
	static struct lock_class_key dev_addr_list_lock_key;	\
	static struct lock_class_key dev_instance_lock_key;	\
	unsigned int i;						\
								\
	(dev)->qdisc_tx_busylock = &qdisc_tx_busylock_key;	\
	lockdep_set_class(&(dev)->addr_list_lock,		\
			  &dev_addr_list_lock_key);		\
	lockdep_set_class(&(dev)->lock,				\
			  &dev_instance_lock_key);		\
	lock_set_cmp_fn(&dev->lock, netdev_lock_cmp_fn, NULL);	\
	for (i = 0; i < (dev)->num_tx_queues; i++)		\
		lockdep_set_class(&(dev)->_tx[i]._xmit_lock,	\
				  &qdisc_xmit_lock_key);	\
}

#define netdev_lock_dereference(p, dev)				\
	rcu_dereference_protected(p, lockdep_is_held(&(dev)->lock))

int netdev_debug_event(struct notifier_block *nb, unsigned long event,
		       void *ptr);

#endif
