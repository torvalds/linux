// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <net/genetlink.h>

#include "devl_internal.h"

DEFINE_XARRAY_FLAGS(devlinks, XA_FLAGS_ALLOC);

void *devlink_priv(struct devlink *devlink)
{
	return &devlink->priv;
}
EXPORT_SYMBOL_GPL(devlink_priv);

struct devlink *priv_to_devlink(void *priv)
{
	return container_of(priv, struct devlink, priv);
}
EXPORT_SYMBOL_GPL(priv_to_devlink);

struct device *devlink_to_dev(const struct devlink *devlink)
{
	return devlink->dev;
}
EXPORT_SYMBOL_GPL(devlink_to_dev);

struct net *devlink_net(const struct devlink *devlink)
{
	return read_pnet(&devlink->_net);
}
EXPORT_SYMBOL_GPL(devlink_net);

void devl_assert_locked(struct devlink *devlink)
{
	lockdep_assert_held(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devl_assert_locked);

#ifdef CONFIG_LOCKDEP
/* For use in conjunction with LOCKDEP only e.g. rcu_dereference_protected() */
bool devl_lock_is_held(struct devlink *devlink)
{
	return lockdep_is_held(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devl_lock_is_held);
#endif

void devl_lock(struct devlink *devlink)
{
	mutex_lock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devl_lock);

int devl_trylock(struct devlink *devlink)
{
	return mutex_trylock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devl_trylock);

void devl_unlock(struct devlink *devlink)
{
	mutex_unlock(&devlink->lock);
}
EXPORT_SYMBOL_GPL(devl_unlock);

struct devlink *__must_check devlink_try_get(struct devlink *devlink)
{
	if (refcount_inc_not_zero(&devlink->refcount))
		return devlink;
	return NULL;
}

static void __devlink_put_rcu(struct rcu_head *head)
{
	struct devlink *devlink = container_of(head, struct devlink, rcu);

	complete(&devlink->comp);
}

void devlink_put(struct devlink *devlink)
{
	if (refcount_dec_and_test(&devlink->refcount))
		/* Make sure unregister operation that may await the completion
		 * is unblocked only after all users are after the end of
		 * RCU grace period.
		 */
		call_rcu(&devlink->rcu, __devlink_put_rcu);
}

struct devlink *
devlinks_xa_find_get(struct net *net, unsigned long *indexp,
		     void * (*xa_find_fn)(struct xarray *, unsigned long *,
					  unsigned long, xa_mark_t))
{
	struct devlink *devlink;

	rcu_read_lock();
retry:
	devlink = xa_find_fn(&devlinks, indexp, ULONG_MAX, DEVLINK_REGISTERED);
	if (!devlink)
		goto unlock;

	/* In case devlink_unregister() was already called and "unregistering"
	 * mark was set, do not allow to get a devlink reference here.
	 * This prevents live-lock of devlink_unregister() wait for completion.
	 */
	if (xa_get_mark(&devlinks, *indexp, DEVLINK_UNREGISTERING))
		goto retry;

	/* For a possible retry, the xa_find_after() should be always used */
	xa_find_fn = xa_find_after;
	if (!devlink_try_get(devlink))
		goto retry;
	if (!net_eq(devlink_net(devlink), net)) {
		devlink_put(devlink);
		goto retry;
	}
unlock:
	rcu_read_unlock();
	return devlink;
}

struct devlink *
devlinks_xa_find_get_first(struct net *net, unsigned long *indexp)
{
	return devlinks_xa_find_get(net, indexp, xa_find);
}

struct devlink *
devlinks_xa_find_get_next(struct net *net, unsigned long *indexp)
{
	return devlinks_xa_find_get(net, indexp, xa_find_after);
}

/**
 *	devlink_set_features - Set devlink supported features
 *
 *	@devlink: devlink
 *	@features: devlink support features
 *
 *	This interface allows us to set reload ops separatelly from
 *	the devlink_alloc.
 */
void devlink_set_features(struct devlink *devlink, u64 features)
{
	ASSERT_DEVLINK_NOT_REGISTERED(devlink);

	WARN_ON(features & DEVLINK_F_RELOAD &&
		!devlink_reload_supported(devlink->ops));
	devlink->features = features;
}
EXPORT_SYMBOL_GPL(devlink_set_features);

/**
 *	devlink_register - Register devlink instance
 *
 *	@devlink: devlink
 */
void devlink_register(struct devlink *devlink)
{
	ASSERT_DEVLINK_NOT_REGISTERED(devlink);
	/* Make sure that we are in .probe() routine */

	xa_set_mark(&devlinks, devlink->index, DEVLINK_REGISTERED);
	devlink_notify_register(devlink);
}
EXPORT_SYMBOL_GPL(devlink_register);

/**
 *	devlink_unregister - Unregister devlink instance
 *
 *	@devlink: devlink
 */
void devlink_unregister(struct devlink *devlink)
{
	ASSERT_DEVLINK_REGISTERED(devlink);
	/* Make sure that we are in .remove() routine */

	xa_set_mark(&devlinks, devlink->index, DEVLINK_UNREGISTERING);
	devlink_put(devlink);
	wait_for_completion(&devlink->comp);

	devlink_notify_unregister(devlink);
	xa_clear_mark(&devlinks, devlink->index, DEVLINK_REGISTERED);
	xa_clear_mark(&devlinks, devlink->index, DEVLINK_UNREGISTERING);
}
EXPORT_SYMBOL_GPL(devlink_unregister);

/**
 *	devlink_alloc_ns - Allocate new devlink instance resources
 *	in specific namespace
 *
 *	@ops: ops
 *	@priv_size: size of user private data
 *	@net: net namespace
 *	@dev: parent device
 *
 *	Allocate new devlink instance resources, including devlink index
 *	and name.
 */
struct devlink *devlink_alloc_ns(const struct devlink_ops *ops,
				 size_t priv_size, struct net *net,
				 struct device *dev)
{
	struct devlink *devlink;
	static u32 last_id;
	int ret;

	WARN_ON(!ops || !dev);
	if (!devlink_reload_actions_valid(ops))
		return NULL;

	devlink = kzalloc(sizeof(*devlink) + priv_size, GFP_KERNEL);
	if (!devlink)
		return NULL;

	ret = xa_alloc_cyclic(&devlinks, &devlink->index, devlink, xa_limit_31b,
			      &last_id, GFP_KERNEL);
	if (ret < 0)
		goto err_xa_alloc;

	devlink->netdevice_nb.notifier_call = devlink_port_netdevice_event;
	ret = register_netdevice_notifier_net(net, &devlink->netdevice_nb);
	if (ret)
		goto err_register_netdevice_notifier;

	devlink->dev = dev;
	devlink->ops = ops;
	xa_init_flags(&devlink->ports, XA_FLAGS_ALLOC);
	xa_init_flags(&devlink->snapshot_ids, XA_FLAGS_ALLOC);
	write_pnet(&devlink->_net, net);
	INIT_LIST_HEAD(&devlink->rate_list);
	INIT_LIST_HEAD(&devlink->linecard_list);
	INIT_LIST_HEAD(&devlink->sb_list);
	INIT_LIST_HEAD_RCU(&devlink->dpipe_table_list);
	INIT_LIST_HEAD(&devlink->resource_list);
	INIT_LIST_HEAD(&devlink->param_list);
	INIT_LIST_HEAD(&devlink->region_list);
	INIT_LIST_HEAD(&devlink->reporter_list);
	INIT_LIST_HEAD(&devlink->trap_list);
	INIT_LIST_HEAD(&devlink->trap_group_list);
	INIT_LIST_HEAD(&devlink->trap_policer_list);
	lockdep_register_key(&devlink->lock_key);
	mutex_init(&devlink->lock);
	lockdep_set_class(&devlink->lock, &devlink->lock_key);
	mutex_init(&devlink->reporters_lock);
	mutex_init(&devlink->linecards_lock);
	refcount_set(&devlink->refcount, 1);
	init_completion(&devlink->comp);

	return devlink;

err_register_netdevice_notifier:
	xa_erase(&devlinks, devlink->index);
err_xa_alloc:
	kfree(devlink);
	return NULL;
}
EXPORT_SYMBOL_GPL(devlink_alloc_ns);

/**
 *	devlink_free - Free devlink instance resources
 *
 *	@devlink: devlink
 */
void devlink_free(struct devlink *devlink)
{
	ASSERT_DEVLINK_NOT_REGISTERED(devlink);

	mutex_destroy(&devlink->linecards_lock);
	mutex_destroy(&devlink->reporters_lock);
	mutex_destroy(&devlink->lock);
	lockdep_unregister_key(&devlink->lock_key);
	WARN_ON(!list_empty(&devlink->trap_policer_list));
	WARN_ON(!list_empty(&devlink->trap_group_list));
	WARN_ON(!list_empty(&devlink->trap_list));
	WARN_ON(!list_empty(&devlink->reporter_list));
	WARN_ON(!list_empty(&devlink->region_list));
	WARN_ON(!list_empty(&devlink->param_list));
	WARN_ON(!list_empty(&devlink->resource_list));
	WARN_ON(!list_empty(&devlink->dpipe_table_list));
	WARN_ON(!list_empty(&devlink->sb_list));
	WARN_ON(!list_empty(&devlink->rate_list));
	WARN_ON(!list_empty(&devlink->linecard_list));
	WARN_ON(!xa_empty(&devlink->ports));

	xa_destroy(&devlink->snapshot_ids);
	xa_destroy(&devlink->ports);

	WARN_ON_ONCE(unregister_netdevice_notifier_net(devlink_net(devlink),
						       &devlink->netdevice_nb));

	xa_erase(&devlinks, devlink->index);

	kfree(devlink);
}
EXPORT_SYMBOL_GPL(devlink_free);

static void __net_exit devlink_pernet_pre_exit(struct net *net)
{
	struct devlink *devlink;
	u32 actions_performed;
	unsigned long index;
	int err;

	/* In case network namespace is getting destroyed, reload
	 * all devlink instances from this namespace into init_net.
	 */
	devlinks_xa_for_each_registered_get(net, index, devlink) {
		WARN_ON(!(devlink->features & DEVLINK_F_RELOAD));
		mutex_lock(&devlink->lock);
		err = devlink_reload(devlink, &init_net,
				     DEVLINK_RELOAD_ACTION_DRIVER_REINIT,
				     DEVLINK_RELOAD_LIMIT_UNSPEC,
				     &actions_performed, NULL);
		mutex_unlock(&devlink->lock);
		if (err && err != -EOPNOTSUPP)
			pr_warn("Failed to reload devlink instance into init_net\n");
		devlink_put(devlink);
	}
}

static struct pernet_operations devlink_pernet_ops __net_initdata = {
	.pre_exit = devlink_pernet_pre_exit,
};

static int __init devlink_init(void)
{
	int err;

	err = genl_register_family(&devlink_nl_family);
	if (err)
		goto out;
	err = register_pernet_subsys(&devlink_pernet_ops);

out:
	WARN_ON(err);
	return err;
}

subsys_initcall(devlink_init);
