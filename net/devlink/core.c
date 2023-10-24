// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <net/genetlink.h>
#define CREATE_TRACE_POINTS
#include <trace/events/devlink.h>

#include "devl_internal.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_hwmsg);
EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_hwerr);
EXPORT_TRACEPOINT_SYMBOL_GPL(devlink_trap_report);

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

/**
 * devlink_try_get() - try to obtain a reference on a devlink instance
 * @devlink: instance to reference
 *
 * Obtain a reference on a devlink instance. A reference on a devlink instance
 * only implies that it's safe to take the instance lock. It does not imply
 * that the instance is registered, use devl_is_registered() after taking
 * the instance lock to check registration status.
 */
struct devlink *__must_check devlink_try_get(struct devlink *devlink)
{
	if (refcount_inc_not_zero(&devlink->refcount))
		return devlink;
	return NULL;
}

static void devlink_release(struct work_struct *work)
{
	struct devlink *devlink;

	devlink = container_of(to_rcu_work(work), struct devlink, rwork);

	mutex_destroy(&devlink->lock);
	lockdep_unregister_key(&devlink->lock_key);
	kfree(devlink);
}

void devlink_put(struct devlink *devlink)
{
	if (refcount_dec_and_test(&devlink->refcount))
		queue_rcu_work(system_wq, &devlink->rwork);
}

struct devlink *devlinks_xa_find_get(struct net *net, unsigned long *indexp)
{
	struct devlink *devlink = NULL;

	rcu_read_lock();
retry:
	devlink = xa_find(&devlinks, indexp, ULONG_MAX, DEVLINK_REGISTERED);
	if (!devlink)
		goto unlock;

	if (!devlink_try_get(devlink))
		goto next;
	if (!net_eq(devlink_net(devlink), net)) {
		devlink_put(devlink);
		goto next;
	}
unlock:
	rcu_read_unlock();
	return devlink;

next:
	(*indexp)++;
	goto retry;
}

/**
 * devl_register - Register devlink instance
 * @devlink: devlink
 */
int devl_register(struct devlink *devlink)
{
	ASSERT_DEVLINK_NOT_REGISTERED(devlink);
	devl_assert_locked(devlink);

	xa_set_mark(&devlinks, devlink->index, DEVLINK_REGISTERED);
	devlink_notify_register(devlink);

	return 0;
}
EXPORT_SYMBOL_GPL(devl_register);

void devlink_register(struct devlink *devlink)
{
	devl_lock(devlink);
	devl_register(devlink);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_register);

/**
 * devl_unregister - Unregister devlink instance
 * @devlink: devlink
 */
void devl_unregister(struct devlink *devlink)
{
	ASSERT_DEVLINK_REGISTERED(devlink);
	devl_assert_locked(devlink);

	devlink_notify_unregister(devlink);
	xa_clear_mark(&devlinks, devlink->index, DEVLINK_REGISTERED);
}
EXPORT_SYMBOL_GPL(devl_unregister);

void devlink_unregister(struct devlink *devlink)
{
	devl_lock(devlink);
	devl_unregister(devlink);
	devl_unlock(devlink);
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

	devlink->dev = dev;
	devlink->ops = ops;
	xa_init_flags(&devlink->ports, XA_FLAGS_ALLOC);
	xa_init_flags(&devlink->params, XA_FLAGS_ALLOC);
	xa_init_flags(&devlink->snapshot_ids, XA_FLAGS_ALLOC);
	write_pnet(&devlink->_net, net);
	INIT_LIST_HEAD(&devlink->rate_list);
	INIT_LIST_HEAD(&devlink->linecard_list);
	INIT_LIST_HEAD(&devlink->sb_list);
	INIT_LIST_HEAD_RCU(&devlink->dpipe_table_list);
	INIT_LIST_HEAD(&devlink->resource_list);
	INIT_LIST_HEAD(&devlink->region_list);
	INIT_LIST_HEAD(&devlink->reporter_list);
	INIT_LIST_HEAD(&devlink->trap_list);
	INIT_LIST_HEAD(&devlink->trap_group_list);
	INIT_LIST_HEAD(&devlink->trap_policer_list);
	INIT_RCU_WORK(&devlink->rwork, devlink_release);
	lockdep_register_key(&devlink->lock_key);
	mutex_init(&devlink->lock);
	lockdep_set_class(&devlink->lock, &devlink->lock_key);
	refcount_set(&devlink->refcount, 1);

	return devlink;

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

	WARN_ON(!list_empty(&devlink->trap_policer_list));
	WARN_ON(!list_empty(&devlink->trap_group_list));
	WARN_ON(!list_empty(&devlink->trap_list));
	WARN_ON(!list_empty(&devlink->reporter_list));
	WARN_ON(!list_empty(&devlink->region_list));
	WARN_ON(!list_empty(&devlink->resource_list));
	WARN_ON(!list_empty(&devlink->dpipe_table_list));
	WARN_ON(!list_empty(&devlink->sb_list));
	WARN_ON(!list_empty(&devlink->rate_list));
	WARN_ON(!list_empty(&devlink->linecard_list));
	WARN_ON(!xa_empty(&devlink->ports));

	xa_destroy(&devlink->snapshot_ids);
	xa_destroy(&devlink->params);
	xa_destroy(&devlink->ports);

	xa_erase(&devlinks, devlink->index);

	devlink_put(devlink);
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
		devl_lock(devlink);
		err = 0;
		if (devl_is_registered(devlink))
			err = devlink_reload(devlink, &init_net,
					     DEVLINK_RELOAD_ACTION_DRIVER_REINIT,
					     DEVLINK_RELOAD_LIMIT_UNSPEC,
					     &actions_performed, NULL);
		devl_unlock(devlink);
		devlink_put(devlink);
		if (err && err != -EOPNOTSUPP)
			pr_warn("Failed to reload devlink instance into init_net\n");
	}
}

static struct pernet_operations devlink_pernet_ops __net_initdata = {
	.pre_exit = devlink_pernet_pre_exit,
};

static struct notifier_block devlink_port_netdevice_nb = {
	.notifier_call = devlink_port_netdevice_event,
};

static int __init devlink_init(void)
{
	int err;

	err = genl_register_family(&devlink_nl_family);
	if (err)
		goto out;
	err = register_pernet_subsys(&devlink_pernet_ops);
	if (err)
		goto out;
	err = register_netdevice_notifier(&devlink_port_netdevice_nb);

out:
	WARN_ON(err);
	return err;
}

subsys_initcall(devlink_init);
