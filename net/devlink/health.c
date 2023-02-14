// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <net/genetlink.h>
#include "devl_internal.h"

void *
devlink_health_reporter_priv(struct devlink_health_reporter *reporter)
{
	return reporter->priv;
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_priv);

static struct devlink_health_reporter *
__devlink_health_reporter_find_by_name(struct list_head *reporter_list,
				       const char *reporter_name)
{
	struct devlink_health_reporter *reporter;

	list_for_each_entry(reporter, reporter_list, list)
		if (!strcmp(reporter->ops->name, reporter_name))
			return reporter;
	return NULL;
}

struct devlink_health_reporter *
devlink_health_reporter_find_by_name(struct devlink *devlink,
				     const char *reporter_name)
{
	return __devlink_health_reporter_find_by_name(&devlink->reporter_list,
						      reporter_name);
}

struct devlink_health_reporter *
devlink_port_health_reporter_find_by_name(struct devlink_port *devlink_port,
					  const char *reporter_name)
{
	return __devlink_health_reporter_find_by_name(&devlink_port->reporter_list,
						      reporter_name);
}

static struct devlink_health_reporter *
__devlink_health_reporter_create(struct devlink *devlink,
				 const struct devlink_health_reporter_ops *ops,
				 u64 graceful_period, void *priv)
{
	struct devlink_health_reporter *reporter;

	if (WARN_ON(graceful_period && !ops->recover))
		return ERR_PTR(-EINVAL);

	reporter = kzalloc(sizeof(*reporter), GFP_KERNEL);
	if (!reporter)
		return ERR_PTR(-ENOMEM);

	reporter->priv = priv;
	reporter->ops = ops;
	reporter->devlink = devlink;
	reporter->graceful_period = graceful_period;
	reporter->auto_recover = !!ops->recover;
	reporter->auto_dump = !!ops->dump;
	mutex_init(&reporter->dump_lock);
	return reporter;
}

/**
 * devl_port_health_reporter_create() - create devlink health reporter for
 *                                      specified port instance
 *
 * @port: devlink_port to which health reports will relate
 * @ops: devlink health reporter ops
 * @graceful_period: min time (in msec) between recovery attempts
 * @priv: driver priv pointer
 */
struct devlink_health_reporter *
devl_port_health_reporter_create(struct devlink_port *port,
				 const struct devlink_health_reporter_ops *ops,
				 u64 graceful_period, void *priv)
{
	struct devlink_health_reporter *reporter;

	devl_assert_locked(port->devlink);

	if (__devlink_health_reporter_find_by_name(&port->reporter_list,
						   ops->name))
		return ERR_PTR(-EEXIST);

	reporter = __devlink_health_reporter_create(port->devlink, ops,
						    graceful_period, priv);
	if (IS_ERR(reporter))
		return reporter;

	reporter->devlink_port = port;
	list_add_tail(&reporter->list, &port->reporter_list);
	return reporter;
}
EXPORT_SYMBOL_GPL(devl_port_health_reporter_create);

struct devlink_health_reporter *
devlink_port_health_reporter_create(struct devlink_port *port,
				    const struct devlink_health_reporter_ops *ops,
				    u64 graceful_period, void *priv)
{
	struct devlink_health_reporter *reporter;
	struct devlink *devlink = port->devlink;

	devl_lock(devlink);
	reporter = devl_port_health_reporter_create(port, ops,
						    graceful_period, priv);
	devl_unlock(devlink);
	return reporter;
}
EXPORT_SYMBOL_GPL(devlink_port_health_reporter_create);

/**
 * devl_health_reporter_create - create devlink health reporter
 *
 * @devlink: devlink instance which the health reports will relate
 * @ops: devlink health reporter ops
 * @graceful_period: min time (in msec) between recovery attempts
 * @priv: driver priv pointer
 */
struct devlink_health_reporter *
devl_health_reporter_create(struct devlink *devlink,
			    const struct devlink_health_reporter_ops *ops,
			    u64 graceful_period, void *priv)
{
	struct devlink_health_reporter *reporter;

	devl_assert_locked(devlink);

	if (devlink_health_reporter_find_by_name(devlink, ops->name))
		return ERR_PTR(-EEXIST);

	reporter = __devlink_health_reporter_create(devlink, ops,
						    graceful_period, priv);
	if (IS_ERR(reporter))
		return reporter;

	list_add_tail(&reporter->list, &devlink->reporter_list);
	return reporter;
}
EXPORT_SYMBOL_GPL(devl_health_reporter_create);

struct devlink_health_reporter *
devlink_health_reporter_create(struct devlink *devlink,
			       const struct devlink_health_reporter_ops *ops,
			       u64 graceful_period, void *priv)
{
	struct devlink_health_reporter *reporter;

	devl_lock(devlink);
	reporter = devl_health_reporter_create(devlink, ops,
					       graceful_period, priv);
	devl_unlock(devlink);
	return reporter;
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_create);

static void
devlink_health_reporter_free(struct devlink_health_reporter *reporter)
{
	mutex_destroy(&reporter->dump_lock);
	if (reporter->dump_fmsg)
		devlink_fmsg_free(reporter->dump_fmsg);
	kfree(reporter);
}

/**
 * devl_health_reporter_destroy() - destroy devlink health reporter
 *
 * @reporter: devlink health reporter to destroy
 */
void
devl_health_reporter_destroy(struct devlink_health_reporter *reporter)
{
	devl_assert_locked(reporter->devlink);

	list_del(&reporter->list);
	devlink_health_reporter_free(reporter);
}
EXPORT_SYMBOL_GPL(devl_health_reporter_destroy);

void
devlink_health_reporter_destroy(struct devlink_health_reporter *reporter)
{
	struct devlink *devlink = reporter->devlink;

	devl_lock(devlink);
	devl_health_reporter_destroy(reporter);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_destroy);
