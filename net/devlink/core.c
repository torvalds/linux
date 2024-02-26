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

static struct devlink *devlinks_xa_get(unsigned long index)
{
	struct devlink *devlink;

	rcu_read_lock();
	devlink = xa_find(&devlinks, &index, index, DEVLINK_REGISTERED);
	if (!devlink || !devlink_try_get(devlink))
		devlink = NULL;
	rcu_read_unlock();
	return devlink;
}

/* devlink_rels xarray contains 1:1 relationships between
 * devlink object and related nested devlink instance.
 * The xarray index is used to get the nested object from
 * the nested-in object code.
 */
static DEFINE_XARRAY_FLAGS(devlink_rels, XA_FLAGS_ALLOC1);

#define DEVLINK_REL_IN_USE XA_MARK_0

struct devlink_rel {
	u32 index;
	refcount_t refcount;
	u32 devlink_index;
	struct {
		u32 devlink_index;
		u32 obj_index;
		devlink_rel_notify_cb_t *notify_cb;
		devlink_rel_cleanup_cb_t *cleanup_cb;
		struct delayed_work notify_work;
	} nested_in;
};

static void devlink_rel_free(struct devlink_rel *rel)
{
	xa_erase(&devlink_rels, rel->index);
	kfree(rel);
}

static void __devlink_rel_get(struct devlink_rel *rel)
{
	refcount_inc(&rel->refcount);
}

static void __devlink_rel_put(struct devlink_rel *rel)
{
	if (refcount_dec_and_test(&rel->refcount))
		devlink_rel_free(rel);
}

static void devlink_rel_nested_in_notify_work(struct work_struct *work)
{
	struct devlink_rel *rel = container_of(work, struct devlink_rel,
					       nested_in.notify_work.work);
	struct devlink *devlink;

	devlink = devlinks_xa_get(rel->nested_in.devlink_index);
	if (!devlink)
		goto rel_put;
	if (!devl_trylock(devlink)) {
		devlink_put(devlink);
		goto reschedule_work;
	}
	if (!devl_is_registered(devlink)) {
		devl_unlock(devlink);
		devlink_put(devlink);
		goto rel_put;
	}
	if (!xa_get_mark(&devlink_rels, rel->index, DEVLINK_REL_IN_USE))
		rel->nested_in.cleanup_cb(devlink, rel->nested_in.obj_index, rel->index);
	rel->nested_in.notify_cb(devlink, rel->nested_in.obj_index);
	devl_unlock(devlink);
	devlink_put(devlink);

rel_put:
	__devlink_rel_put(rel);
	return;

reschedule_work:
	schedule_delayed_work(&rel->nested_in.notify_work, 1);
}

static void devlink_rel_nested_in_notify_work_schedule(struct devlink_rel *rel)
{
	__devlink_rel_get(rel);
	schedule_delayed_work(&rel->nested_in.notify_work, 0);
}

static struct devlink_rel *devlink_rel_alloc(void)
{
	struct devlink_rel *rel;
	static u32 next;
	int err;

	rel = kzalloc(sizeof(*rel), GFP_KERNEL);
	if (!rel)
		return ERR_PTR(-ENOMEM);

	err = xa_alloc_cyclic(&devlink_rels, &rel->index, rel,
			      xa_limit_32b, &next, GFP_KERNEL);
	if (err) {
		kfree(rel);
		return ERR_PTR(err);
	}

	refcount_set(&rel->refcount, 1);
	INIT_DELAYED_WORK(&rel->nested_in.notify_work,
			  &devlink_rel_nested_in_notify_work);
	return rel;
}

static void devlink_rel_put(struct devlink *devlink)
{
	struct devlink_rel *rel = devlink->rel;

	if (!rel)
		return;
	xa_clear_mark(&devlink_rels, rel->index, DEVLINK_REL_IN_USE);
	devlink_rel_nested_in_notify_work_schedule(rel);
	__devlink_rel_put(rel);
	devlink->rel = NULL;
}

void devlink_rel_nested_in_clear(u32 rel_index)
{
	xa_clear_mark(&devlink_rels, rel_index, DEVLINK_REL_IN_USE);
}

int devlink_rel_nested_in_add(u32 *rel_index, u32 devlink_index,
			      u32 obj_index, devlink_rel_notify_cb_t *notify_cb,
			      devlink_rel_cleanup_cb_t *cleanup_cb,
			      struct devlink *devlink)
{
	struct devlink_rel *rel = devlink_rel_alloc();

	ASSERT_DEVLINK_NOT_REGISTERED(devlink);

	if (IS_ERR(rel))
		return PTR_ERR(rel);

	rel->devlink_index = devlink->index;
	rel->nested_in.devlink_index = devlink_index;
	rel->nested_in.obj_index = obj_index;
	rel->nested_in.notify_cb = notify_cb;
	rel->nested_in.cleanup_cb = cleanup_cb;
	*rel_index = rel->index;
	xa_set_mark(&devlink_rels, rel->index, DEVLINK_REL_IN_USE);
	devlink->rel = rel;
	return 0;
}

/**
 * devlink_rel_nested_in_notify - Notify the object this devlink
 *				  instance is nested in.
 * @devlink: devlink
 *
 * This is called upon network namespace change of devlink instance.
 * In case this devlink instance is nested in another devlink object,
 * a notification of a change of this object should be sent
 * over netlink. The parent devlink instance lock needs to be
 * taken during the notification preparation.
 * However, since the devlink lock of nested instance is held here,
 * we would end with wrong devlink instance lock ordering and
 * deadlock. Therefore the work is utilized to avoid that.
 */
void devlink_rel_nested_in_notify(struct devlink *devlink)
{
	struct devlink_rel *rel = devlink->rel;

	if (!rel)
		return;
	devlink_rel_nested_in_notify_work_schedule(rel);
}

static struct devlink_rel *devlink_rel_find(unsigned long rel_index)
{
	return xa_find(&devlink_rels, &rel_index, rel_index,
		       DEVLINK_REL_IN_USE);
}

static struct devlink *devlink_rel_devlink_get(u32 rel_index)
{
	struct devlink_rel *rel;
	u32 devlink_index;

	if (!rel_index)
		return NULL;
	xa_lock(&devlink_rels);
	rel = devlink_rel_find(rel_index);
	if (rel)
		devlink_index = rel->devlink_index;
	xa_unlock(&devlink_rels);
	if (!rel)
		return NULL;
	return devlinks_xa_get(devlink_index);
}

int devlink_rel_devlink_handle_put(struct sk_buff *msg, struct devlink *devlink,
				   u32 rel_index, int attrtype,
				   bool *msg_updated)
{
	struct net *net = devlink_net(devlink);
	struct devlink *rel_devlink;
	int err;

	rel_devlink = devlink_rel_devlink_get(rel_index);
	if (!rel_devlink)
		return 0;
	err = devlink_nl_put_nested_handle(msg, net, rel_devlink, attrtype);
	devlink_put(rel_devlink);
	if (!err && msg_updated)
		*msg_updated = true;
	return err;
}

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
	put_device(devlink->dev);
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
	devlink_rel_nested_in_notify(devlink);

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
	devlink_rel_put(devlink);
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

	devlink->dev = get_device(dev);
	devlink->ops = ops;
	xa_init_flags(&devlink->ports, XA_FLAGS_ALLOC);
	xa_init_flags(&devlink->params, XA_FLAGS_ALLOC);
	xa_init_flags(&devlink->snapshot_ids, XA_FLAGS_ALLOC);
	xa_init_flags(&devlink->nested_rels, XA_FLAGS_ALLOC);
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

	xa_destroy(&devlink->nested_rels);
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
		devl_dev_lock(devlink, true);
		err = 0;
		if (devl_is_registered(devlink))
			err = devlink_reload(devlink, &init_net,
					     DEVLINK_RELOAD_ACTION_DRIVER_REINIT,
					     DEVLINK_RELOAD_LIMIT_UNSPEC,
					     &actions_performed, NULL);
		devl_dev_unlock(devlink, true);
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
