// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <net/genetlink.h>
#include <trace/events/devlink.h>
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

int
devlink_nl_health_reporter_fill(struct sk_buff *msg,
				struct devlink_health_reporter *reporter,
				enum devlink_command cmd, u32 portid,
				u32 seq, int flags)
{
	struct devlink *devlink = reporter->devlink;
	struct nlattr *reporter_attr;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto genlmsg_cancel;

	if (reporter->devlink_port) {
		if (nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX, reporter->devlink_port->index))
			goto genlmsg_cancel;
	}
	reporter_attr = nla_nest_start_noflag(msg,
					      DEVLINK_ATTR_HEALTH_REPORTER);
	if (!reporter_attr)
		goto genlmsg_cancel;
	if (nla_put_string(msg, DEVLINK_ATTR_HEALTH_REPORTER_NAME,
			   reporter->ops->name))
		goto reporter_nest_cancel;
	if (nla_put_u8(msg, DEVLINK_ATTR_HEALTH_REPORTER_STATE,
		       reporter->health_state))
		goto reporter_nest_cancel;
	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_ERR_COUNT,
			      reporter->error_count, DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_RECOVER_COUNT,
			      reporter->recovery_count, DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (reporter->ops->recover &&
	    nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD,
			      reporter->graceful_period,
			      DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (reporter->ops->recover &&
	    nla_put_u8(msg, DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER,
		       reporter->auto_recover))
		goto reporter_nest_cancel;
	if (reporter->dump_fmsg &&
	    nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_DUMP_TS,
			      jiffies_to_msecs(reporter->dump_ts),
			      DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (reporter->dump_fmsg &&
	    nla_put_u64_64bit(msg, DEVLINK_ATTR_HEALTH_REPORTER_DUMP_TS_NS,
			      reporter->dump_real_ts, DEVLINK_ATTR_PAD))
		goto reporter_nest_cancel;
	if (reporter->ops->dump &&
	    nla_put_u8(msg, DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP,
		       reporter->auto_dump))
		goto reporter_nest_cancel;

	nla_nest_end(msg, reporter_attr);
	genlmsg_end(msg, hdr);
	return 0;

reporter_nest_cancel:
	nla_nest_cancel(msg, reporter_attr);
genlmsg_cancel:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

struct devlink_health_reporter *
devlink_health_reporter_get_from_attrs(struct devlink *devlink,
				       struct nlattr **attrs)
{
	struct devlink_port *devlink_port;
	char *reporter_name;

	if (!attrs[DEVLINK_ATTR_HEALTH_REPORTER_NAME])
		return NULL;

	reporter_name = nla_data(attrs[DEVLINK_ATTR_HEALTH_REPORTER_NAME]);
	devlink_port = devlink_port_get_from_attrs(devlink, attrs);
	if (IS_ERR(devlink_port))
		return devlink_health_reporter_find_by_name(devlink,
							    reporter_name);
	else
		return devlink_port_health_reporter_find_by_name(devlink_port,
								 reporter_name);
}

struct devlink_health_reporter *
devlink_health_reporter_get_from_info(struct devlink *devlink,
				      struct genl_info *info)
{
	return devlink_health_reporter_get_from_attrs(devlink, info->attrs);
}

int devlink_nl_cmd_health_reporter_get_doit(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;
	struct sk_buff *msg;
	int err;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_health_reporter_fill(msg, reporter,
					      DEVLINK_CMD_HEALTH_REPORTER_GET,
					      info->snd_portid, info->snd_seq,
					      0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int
devlink_nl_cmd_health_reporter_get_dump_one(struct sk_buff *msg,
					    struct devlink *devlink,
					    struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_health_reporter *reporter;
	struct devlink_port *port;
	unsigned long port_index;
	int idx = 0;
	int err;

	list_for_each_entry(reporter, &devlink->reporter_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_health_reporter_fill(msg, reporter,
						      DEVLINK_CMD_HEALTH_REPORTER_GET,
						      NETLINK_CB(cb->skb).portid,
						      cb->nlh->nlmsg_seq,
						      NLM_F_MULTI);
		if (err) {
			state->idx = idx;
			return err;
		}
		idx++;
	}
	xa_for_each(&devlink->ports, port_index, port) {
		list_for_each_entry(reporter, &port->reporter_list, list) {
			if (idx < state->idx) {
				idx++;
				continue;
			}
			err = devlink_nl_health_reporter_fill(msg, reporter,
							      DEVLINK_CMD_HEALTH_REPORTER_GET,
							      NETLINK_CB(cb->skb).portid,
							      cb->nlh->nlmsg_seq,
							      NLM_F_MULTI);
			if (err) {
				state->idx = idx;
				return err;
			}
			idx++;
		}
	}

	return 0;
}

const struct devlink_cmd devl_cmd_health_reporter_get = {
	.dump_one		= devlink_nl_cmd_health_reporter_get_dump_one,
};

int devlink_nl_cmd_health_reporter_set_doit(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	if (!reporter->ops->recover &&
	    (info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD] ||
	     info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER]))
		return -EOPNOTSUPP;

	if (!reporter->ops->dump &&
	    info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP])
		return -EOPNOTSUPP;

	if (info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD])
		reporter->graceful_period =
			nla_get_u64(info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_GRACEFUL_PERIOD]);

	if (info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER])
		reporter->auto_recover =
			nla_get_u8(info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_RECOVER]);

	if (info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP])
		reporter->auto_dump =
		nla_get_u8(info->attrs[DEVLINK_ATTR_HEALTH_REPORTER_AUTO_DUMP]);

	return 0;
}

static void devlink_recover_notify(struct devlink_health_reporter *reporter,
				   enum devlink_command cmd)
{
	struct devlink *devlink = reporter->devlink;
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_HEALTH_REPORTER_RECOVER);
	WARN_ON(!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED));

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_health_reporter_fill(msg, reporter, cmd, 0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink), msg,
				0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

void
devlink_health_reporter_recovery_done(struct devlink_health_reporter *reporter)
{
	reporter->recovery_count++;
	reporter->last_recovery_ts = jiffies;
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_recovery_done);

static int
devlink_health_reporter_recover(struct devlink_health_reporter *reporter,
				void *priv_ctx, struct netlink_ext_ack *extack)
{
	int err;

	if (reporter->health_state == DEVLINK_HEALTH_REPORTER_STATE_HEALTHY)
		return 0;

	if (!reporter->ops->recover)
		return -EOPNOTSUPP;

	err = reporter->ops->recover(reporter, priv_ctx, extack);
	if (err)
		return err;

	devlink_health_reporter_recovery_done(reporter);
	reporter->health_state = DEVLINK_HEALTH_REPORTER_STATE_HEALTHY;
	devlink_recover_notify(reporter, DEVLINK_CMD_HEALTH_REPORTER_RECOVER);

	return 0;
}

int devlink_health_report(struct devlink_health_reporter *reporter,
			  const char *msg, void *priv_ctx)
{
	enum devlink_health_reporter_state prev_health_state;
	struct devlink *devlink = reporter->devlink;
	unsigned long recover_ts_threshold;
	int ret;

	/* write a log message of the current error */
	WARN_ON(!msg);
	trace_devlink_health_report(devlink, reporter->ops->name, msg);
	reporter->error_count++;
	prev_health_state = reporter->health_state;
	reporter->health_state = DEVLINK_HEALTH_REPORTER_STATE_ERROR;
	devlink_recover_notify(reporter, DEVLINK_CMD_HEALTH_REPORTER_RECOVER);

	/* abort if the previous error wasn't recovered */
	recover_ts_threshold = reporter->last_recovery_ts +
			       msecs_to_jiffies(reporter->graceful_period);
	if (reporter->auto_recover &&
	    (prev_health_state != DEVLINK_HEALTH_REPORTER_STATE_HEALTHY ||
	     (reporter->last_recovery_ts && reporter->recovery_count &&
	      time_is_after_jiffies(recover_ts_threshold)))) {
		trace_devlink_health_recover_aborted(devlink,
						     reporter->ops->name,
						     reporter->health_state,
						     jiffies -
						     reporter->last_recovery_ts);
		return -ECANCELED;
	}

	if (reporter->auto_dump) {
		mutex_lock(&reporter->dump_lock);
		/* store current dump of current error, for later analysis */
		devlink_health_do_dump(reporter, priv_ctx, NULL);
		mutex_unlock(&reporter->dump_lock);
	}

	if (!reporter->auto_recover)
		return 0;

	devl_lock(devlink);
	ret = devlink_health_reporter_recover(reporter, priv_ctx, NULL);
	devl_unlock(devlink);

	return ret;
}
EXPORT_SYMBOL_GPL(devlink_health_report);

void
devlink_health_reporter_state_update(struct devlink_health_reporter *reporter,
				     enum devlink_health_reporter_state state)
{
	if (WARN_ON(state != DEVLINK_HEALTH_REPORTER_STATE_HEALTHY &&
		    state != DEVLINK_HEALTH_REPORTER_STATE_ERROR))
		return;

	if (reporter->health_state == state)
		return;

	reporter->health_state = state;
	trace_devlink_health_reporter_state_update(reporter->devlink,
						   reporter->ops->name, state);
	devlink_recover_notify(reporter, DEVLINK_CMD_HEALTH_REPORTER_RECOVER);
}
EXPORT_SYMBOL_GPL(devlink_health_reporter_state_update);

int devlink_nl_cmd_health_reporter_recover_doit(struct sk_buff *skb,
						struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	return devlink_health_reporter_recover(reporter, NULL, info->extack);
}
