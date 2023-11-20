// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <net/genetlink.h>
#include <net/sock.h>
#include <trace/events/devlink.h>
#include "devl_internal.h"

struct devlink_fmsg_item {
	struct list_head list;
	int attrtype;
	u8 nla_type;
	u16 len;
	int value[];
};

struct devlink_fmsg {
	struct list_head item_list;
	int err; /* first error encountered on some devlink_fmsg_XXX() call */
	bool putting_binary; /* This flag forces enclosing of binary data
			      * in an array brackets. It forces using
			      * of designated API:
			      * devlink_fmsg_binary_pair_nest_start()
			      * devlink_fmsg_binary_pair_nest_end()
			      */
};

static struct devlink_fmsg *devlink_fmsg_alloc(void)
{
	struct devlink_fmsg *fmsg;

	fmsg = kzalloc(sizeof(*fmsg), GFP_KERNEL);
	if (!fmsg)
		return NULL;

	INIT_LIST_HEAD(&fmsg->item_list);

	return fmsg;
}

static void devlink_fmsg_free(struct devlink_fmsg *fmsg)
{
	struct devlink_fmsg_item *item, *tmp;

	list_for_each_entry_safe(item, tmp, &fmsg->item_list, list) {
		list_del(&item->list);
		kfree(item);
	}
	kfree(fmsg);
}

struct devlink_health_reporter {
	struct list_head list;
	void *priv;
	const struct devlink_health_reporter_ops *ops;
	struct devlink *devlink;
	struct devlink_port *devlink_port;
	struct devlink_fmsg *dump_fmsg;
	u64 graceful_period;
	bool auto_recover;
	bool auto_dump;
	u8 health_state;
	u64 dump_ts;
	u64 dump_real_ts;
	u64 error_count;
	u64 recovery_count;
	u64 last_recovery_ts;
};

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

static struct devlink_health_reporter *
devlink_health_reporter_find_by_name(struct devlink *devlink,
				     const char *reporter_name)
{
	return __devlink_health_reporter_find_by_name(&devlink->reporter_list,
						      reporter_name);
}

static struct devlink_health_reporter *
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

static int
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

static struct devlink_health_reporter *
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

static struct devlink_health_reporter *
devlink_health_reporter_get_from_info(struct devlink *devlink,
				      struct genl_info *info)
{
	return devlink_health_reporter_get_from_attrs(devlink, info->attrs);
}

int devlink_nl_health_reporter_get_doit(struct sk_buff *skb,
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

static int devlink_nl_health_reporter_get_dump_one(struct sk_buff *msg,
						   struct devlink *devlink,
						   struct netlink_callback *cb,
						   int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	const struct genl_info *info = genl_info_dump(cb);
	struct devlink_health_reporter *reporter;
	unsigned long port_index_end = ULONG_MAX;
	struct nlattr **attrs = info->attrs;
	unsigned long port_index_start = 0;
	struct devlink_port *port;
	unsigned long port_index;
	int idx = 0;
	int err;

	if (attrs && attrs[DEVLINK_ATTR_PORT_INDEX]) {
		port_index_start = nla_get_u32(attrs[DEVLINK_ATTR_PORT_INDEX]);
		port_index_end = port_index_start;
		flags |= NLM_F_DUMP_FILTERED;
		goto per_port_dump;
	}

	list_for_each_entry(reporter, &devlink->reporter_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_health_reporter_fill(msg, reporter,
						      DEVLINK_CMD_HEALTH_REPORTER_GET,
						      NETLINK_CB(cb->skb).portid,
						      cb->nlh->nlmsg_seq,
						      flags);
		if (err) {
			state->idx = idx;
			return err;
		}
		idx++;
	}
per_port_dump:
	xa_for_each_range(&devlink->ports, port_index, port,
			  port_index_start, port_index_end) {
		list_for_each_entry(reporter, &port->reporter_list, list) {
			if (idx < state->idx) {
				idx++;
				continue;
			}
			err = devlink_nl_health_reporter_fill(msg, reporter,
							      DEVLINK_CMD_HEALTH_REPORTER_GET,
							      NETLINK_CB(cb->skb).portid,
							      cb->nlh->nlmsg_seq,
							      flags);
			if (err) {
				state->idx = idx;
				return err;
			}
			idx++;
		}
	}

	return 0;
}

int devlink_nl_health_reporter_get_dumpit(struct sk_buff *skb,
					  struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb,
				 devlink_nl_health_reporter_get_dump_one);
}

int devlink_nl_health_reporter_set_doit(struct sk_buff *skb,
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
	ASSERT_DEVLINK_REGISTERED(devlink);

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

static void
devlink_health_dump_clear(struct devlink_health_reporter *reporter)
{
	if (!reporter->dump_fmsg)
		return;
	devlink_fmsg_free(reporter->dump_fmsg);
	reporter->dump_fmsg = NULL;
}

static int devlink_health_do_dump(struct devlink_health_reporter *reporter,
				  void *priv_ctx,
				  struct netlink_ext_ack *extack)
{
	int err;

	if (!reporter->ops->dump)
		return 0;

	if (reporter->dump_fmsg)
		return 0;

	reporter->dump_fmsg = devlink_fmsg_alloc();
	if (!reporter->dump_fmsg)
		return -ENOMEM;

	devlink_fmsg_obj_nest_start(reporter->dump_fmsg);

	err = reporter->ops->dump(reporter, reporter->dump_fmsg,
				  priv_ctx, extack);
	if (err)
		goto dump_err;

	devlink_fmsg_obj_nest_end(reporter->dump_fmsg);
	err = reporter->dump_fmsg->err;
	if (err)
		goto dump_err;

	reporter->dump_ts = jiffies;
	reporter->dump_real_ts = ktime_get_real_ns();

	return 0;

dump_err:
	devlink_health_dump_clear(reporter);
	return err;
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
		devl_lock(devlink);
		/* store current dump of current error, for later analysis */
		devlink_health_do_dump(reporter, priv_ctx, NULL);
		devl_unlock(devlink);
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

int devlink_nl_health_reporter_recover_doit(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	return devlink_health_reporter_recover(reporter, NULL, info->extack);
}

static void devlink_fmsg_err_if_binary(struct devlink_fmsg *fmsg)
{
	if (!fmsg->err && fmsg->putting_binary)
		fmsg->err = -EINVAL;
}

static void devlink_fmsg_nest_common(struct devlink_fmsg *fmsg, int attrtype)
{
	struct devlink_fmsg_item *item;

	if (fmsg->err)
		return;

	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		fmsg->err = -ENOMEM;
		return;
	}

	item->attrtype = attrtype;
	list_add_tail(&item->list, &fmsg->item_list);
}

void devlink_fmsg_obj_nest_start(struct devlink_fmsg *fmsg)
{
	devlink_fmsg_err_if_binary(fmsg);
	devlink_fmsg_nest_common(fmsg, DEVLINK_ATTR_FMSG_OBJ_NEST_START);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_obj_nest_start);

static void devlink_fmsg_nest_end(struct devlink_fmsg *fmsg)
{
	devlink_fmsg_err_if_binary(fmsg);
	devlink_fmsg_nest_common(fmsg, DEVLINK_ATTR_FMSG_NEST_END);
}

void devlink_fmsg_obj_nest_end(struct devlink_fmsg *fmsg)
{
	devlink_fmsg_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_obj_nest_end);

#define DEVLINK_FMSG_MAX_SIZE (GENLMSG_DEFAULT_SIZE - GENL_HDRLEN - NLA_HDRLEN)

static void devlink_fmsg_put_name(struct devlink_fmsg *fmsg, const char *name)
{
	struct devlink_fmsg_item *item;

	devlink_fmsg_err_if_binary(fmsg);
	if (fmsg->err)
		return;

	if (strlen(name) + 1 > DEVLINK_FMSG_MAX_SIZE) {
		fmsg->err = -EMSGSIZE;
		return;
	}

	item = kzalloc(sizeof(*item) + strlen(name) + 1, GFP_KERNEL);
	if (!item) {
		fmsg->err = -ENOMEM;
		return;
	}

	item->nla_type = NLA_NUL_STRING;
	item->len = strlen(name) + 1;
	item->attrtype = DEVLINK_ATTR_FMSG_OBJ_NAME;
	memcpy(&item->value, name, item->len);
	list_add_tail(&item->list, &fmsg->item_list);
}

void devlink_fmsg_pair_nest_start(struct devlink_fmsg *fmsg, const char *name)
{
	devlink_fmsg_err_if_binary(fmsg);
	devlink_fmsg_nest_common(fmsg, DEVLINK_ATTR_FMSG_PAIR_NEST_START);
	devlink_fmsg_put_name(fmsg, name);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_pair_nest_start);

void devlink_fmsg_pair_nest_end(struct devlink_fmsg *fmsg)
{
	devlink_fmsg_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_pair_nest_end);

void devlink_fmsg_arr_pair_nest_start(struct devlink_fmsg *fmsg,
				      const char *name)
{
	devlink_fmsg_pair_nest_start(fmsg, name);
	devlink_fmsg_nest_common(fmsg, DEVLINK_ATTR_FMSG_ARR_NEST_START);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_arr_pair_nest_start);

void devlink_fmsg_arr_pair_nest_end(struct devlink_fmsg *fmsg)
{
	devlink_fmsg_nest_end(fmsg);
	devlink_fmsg_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_arr_pair_nest_end);

void devlink_fmsg_binary_pair_nest_start(struct devlink_fmsg *fmsg,
					 const char *name)
{
	devlink_fmsg_arr_pair_nest_start(fmsg, name);
	fmsg->putting_binary = true;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_binary_pair_nest_start);

void devlink_fmsg_binary_pair_nest_end(struct devlink_fmsg *fmsg)
{
	if (fmsg->err)
		return;

	if (!fmsg->putting_binary)
		fmsg->err = -EINVAL;

	fmsg->putting_binary = false;
	devlink_fmsg_arr_pair_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_binary_pair_nest_end);

static void devlink_fmsg_put_value(struct devlink_fmsg *fmsg,
				   const void *value, u16 value_len,
				   u8 value_nla_type)
{
	struct devlink_fmsg_item *item;

	if (fmsg->err)
		return;

	if (value_len > DEVLINK_FMSG_MAX_SIZE) {
		fmsg->err = -EMSGSIZE;
		return;
	}

	item = kzalloc(sizeof(*item) + value_len, GFP_KERNEL);
	if (!item) {
		fmsg->err = -ENOMEM;
		return;
	}

	item->nla_type = value_nla_type;
	item->len = value_len;
	item->attrtype = DEVLINK_ATTR_FMSG_OBJ_VALUE_DATA;
	memcpy(&item->value, value, item->len);
	list_add_tail(&item->list, &fmsg->item_list);
}

static void devlink_fmsg_bool_put(struct devlink_fmsg *fmsg, bool value)
{
	devlink_fmsg_err_if_binary(fmsg);
	devlink_fmsg_put_value(fmsg, &value, sizeof(value), NLA_FLAG);
}

static void devlink_fmsg_u8_put(struct devlink_fmsg *fmsg, u8 value)
{
	devlink_fmsg_err_if_binary(fmsg);
	devlink_fmsg_put_value(fmsg, &value, sizeof(value), NLA_U8);
}

void devlink_fmsg_u32_put(struct devlink_fmsg *fmsg, u32 value)
{
	devlink_fmsg_err_if_binary(fmsg);
	devlink_fmsg_put_value(fmsg, &value, sizeof(value), NLA_U32);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u32_put);

static void devlink_fmsg_u64_put(struct devlink_fmsg *fmsg, u64 value)
{
	devlink_fmsg_err_if_binary(fmsg);
	devlink_fmsg_put_value(fmsg, &value, sizeof(value), NLA_U64);
}

void devlink_fmsg_string_put(struct devlink_fmsg *fmsg, const char *value)
{
	devlink_fmsg_err_if_binary(fmsg);
	devlink_fmsg_put_value(fmsg, value, strlen(value) + 1, NLA_NUL_STRING);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_string_put);

void devlink_fmsg_binary_put(struct devlink_fmsg *fmsg, const void *value,
			     u16 value_len)
{
	if (!fmsg->err && !fmsg->putting_binary)
		fmsg->err = -EINVAL;

	devlink_fmsg_put_value(fmsg, value, value_len, NLA_BINARY);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_binary_put);

void devlink_fmsg_bool_pair_put(struct devlink_fmsg *fmsg, const char *name,
				bool value)
{
	devlink_fmsg_pair_nest_start(fmsg, name);
	devlink_fmsg_bool_put(fmsg, value);
	devlink_fmsg_pair_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_bool_pair_put);

void devlink_fmsg_u8_pair_put(struct devlink_fmsg *fmsg, const char *name,
			      u8 value)
{
	devlink_fmsg_pair_nest_start(fmsg, name);
	devlink_fmsg_u8_put(fmsg, value);
	devlink_fmsg_pair_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u8_pair_put);

void devlink_fmsg_u32_pair_put(struct devlink_fmsg *fmsg, const char *name,
			       u32 value)
{
	devlink_fmsg_pair_nest_start(fmsg, name);
	devlink_fmsg_u32_put(fmsg, value);
	devlink_fmsg_pair_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u32_pair_put);

void devlink_fmsg_u64_pair_put(struct devlink_fmsg *fmsg, const char *name,
			       u64 value)
{
	devlink_fmsg_pair_nest_start(fmsg, name);
	devlink_fmsg_u64_put(fmsg, value);
	devlink_fmsg_pair_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_u64_pair_put);

void devlink_fmsg_string_pair_put(struct devlink_fmsg *fmsg, const char *name,
				  const char *value)
{
	devlink_fmsg_pair_nest_start(fmsg, name);
	devlink_fmsg_string_put(fmsg, value);
	devlink_fmsg_pair_nest_end(fmsg);
}
EXPORT_SYMBOL_GPL(devlink_fmsg_string_pair_put);

void devlink_fmsg_binary_pair_put(struct devlink_fmsg *fmsg, const char *name,
				  const void *value, u32 value_len)
{
	u32 data_size;
	u32 offset;

	devlink_fmsg_binary_pair_nest_start(fmsg, name);

	for (offset = 0; offset < value_len; offset += data_size) {
		data_size = value_len - offset;
		if (data_size > DEVLINK_FMSG_MAX_SIZE)
			data_size = DEVLINK_FMSG_MAX_SIZE;

		devlink_fmsg_binary_put(fmsg, value + offset, data_size);
	}

	devlink_fmsg_binary_pair_nest_end(fmsg);
	fmsg->putting_binary = false;
}
EXPORT_SYMBOL_GPL(devlink_fmsg_binary_pair_put);

static int
devlink_fmsg_item_fill_type(struct devlink_fmsg_item *msg, struct sk_buff *skb)
{
	switch (msg->nla_type) {
	case NLA_FLAG:
	case NLA_U8:
	case NLA_U32:
	case NLA_U64:
	case NLA_NUL_STRING:
	case NLA_BINARY:
		return nla_put_u8(skb, DEVLINK_ATTR_FMSG_OBJ_VALUE_TYPE,
				  msg->nla_type);
	default:
		return -EINVAL;
	}
}

static int
devlink_fmsg_item_fill_data(struct devlink_fmsg_item *msg, struct sk_buff *skb)
{
	int attrtype = DEVLINK_ATTR_FMSG_OBJ_VALUE_DATA;
	u8 tmp;

	switch (msg->nla_type) {
	case NLA_FLAG:
		/* Always provide flag data, regardless of its value */
		tmp = *(bool *)msg->value;

		return nla_put_u8(skb, attrtype, tmp);
	case NLA_U8:
		return nla_put_u8(skb, attrtype, *(u8 *)msg->value);
	case NLA_U32:
		return nla_put_u32(skb, attrtype, *(u32 *)msg->value);
	case NLA_U64:
		return nla_put_u64_64bit(skb, attrtype, *(u64 *)msg->value,
					 DEVLINK_ATTR_PAD);
	case NLA_NUL_STRING:
		return nla_put_string(skb, attrtype, (char *)&msg->value);
	case NLA_BINARY:
		return nla_put(skb, attrtype, msg->len, (void *)&msg->value);
	default:
		return -EINVAL;
	}
}

static int
devlink_fmsg_prepare_skb(struct devlink_fmsg *fmsg, struct sk_buff *skb,
			 int *start)
{
	struct devlink_fmsg_item *item;
	struct nlattr *fmsg_nlattr;
	int err = 0;
	int i = 0;

	fmsg_nlattr = nla_nest_start_noflag(skb, DEVLINK_ATTR_FMSG);
	if (!fmsg_nlattr)
		return -EMSGSIZE;

	list_for_each_entry(item, &fmsg->item_list, list) {
		if (i < *start) {
			i++;
			continue;
		}

		switch (item->attrtype) {
		case DEVLINK_ATTR_FMSG_OBJ_NEST_START:
		case DEVLINK_ATTR_FMSG_PAIR_NEST_START:
		case DEVLINK_ATTR_FMSG_ARR_NEST_START:
		case DEVLINK_ATTR_FMSG_NEST_END:
			err = nla_put_flag(skb, item->attrtype);
			break;
		case DEVLINK_ATTR_FMSG_OBJ_VALUE_DATA:
			err = devlink_fmsg_item_fill_type(item, skb);
			if (err)
				break;
			err = devlink_fmsg_item_fill_data(item, skb);
			break;
		case DEVLINK_ATTR_FMSG_OBJ_NAME:
			err = nla_put_string(skb, item->attrtype,
					     (char *)&item->value);
			break;
		default:
			err = -EINVAL;
			break;
		}
		if (!err)
			*start = ++i;
		else
			break;
	}

	nla_nest_end(skb, fmsg_nlattr);
	return err;
}

static int devlink_fmsg_snd(struct devlink_fmsg *fmsg,
			    struct genl_info *info,
			    enum devlink_command cmd, int flags)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	bool last = false;
	int index = 0;
	void *hdr;
	int err;

	if (fmsg->err)
		return fmsg->err;

	while (!last) {
		int tmp_index = index;

		skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;

		hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
				  &devlink_nl_family, flags | NLM_F_MULTI, cmd);
		if (!hdr) {
			err = -EMSGSIZE;
			goto nla_put_failure;
		}

		err = devlink_fmsg_prepare_skb(fmsg, skb, &index);
		if (!err)
			last = true;
		else if (err != -EMSGSIZE || tmp_index == index)
			goto nla_put_failure;

		genlmsg_end(skb, hdr);
		err = genlmsg_reply(skb, info);
		if (err)
			return err;
	}

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	nlh = nlmsg_put(skb, info->snd_portid, info->snd_seq,
			NLMSG_DONE, 0, flags | NLM_F_MULTI);
	if (!nlh) {
		err = -EMSGSIZE;
		goto nla_put_failure;
	}

	return genlmsg_reply(skb, info);

nla_put_failure:
	nlmsg_free(skb);
	return err;
}

static int devlink_fmsg_dumpit(struct devlink_fmsg *fmsg, struct sk_buff *skb,
			       struct netlink_callback *cb,
			       enum devlink_command cmd)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	int index = state->idx;
	int tmp_index = index;
	void *hdr;
	int err;

	if (fmsg->err)
		return fmsg->err;

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &devlink_nl_family, NLM_F_ACK | NLM_F_MULTI, cmd);
	if (!hdr) {
		err = -EMSGSIZE;
		goto nla_put_failure;
	}

	err = devlink_fmsg_prepare_skb(fmsg, skb, &index);
	if ((err && err != -EMSGSIZE) || tmp_index == index)
		goto nla_put_failure;

	state->idx = index;
	genlmsg_end(skb, hdr);
	return skb->len;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return err;
}

int devlink_nl_health_reporter_diagnose_doit(struct sk_buff *skb,
					     struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;
	struct devlink_fmsg *fmsg;
	int err;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	if (!reporter->ops->diagnose)
		return -EOPNOTSUPP;

	fmsg = devlink_fmsg_alloc();
	if (!fmsg)
		return -ENOMEM;

	devlink_fmsg_obj_nest_start(fmsg);

	err = reporter->ops->diagnose(reporter, fmsg, info->extack);
	if (err)
		goto out;

	devlink_fmsg_obj_nest_end(fmsg);

	err = devlink_fmsg_snd(fmsg, info,
			       DEVLINK_CMD_HEALTH_REPORTER_DIAGNOSE, 0);

out:
	devlink_fmsg_free(fmsg);
	return err;
}

static struct devlink_health_reporter *
devlink_health_reporter_get_from_cb_lock(struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct devlink_health_reporter *reporter;
	struct nlattr **attrs = info->attrs;
	struct devlink *devlink;

	devlink = devlink_get_from_attrs_lock(sock_net(cb->skb->sk), attrs);
	if (IS_ERR(devlink))
		return NULL;

	reporter = devlink_health_reporter_get_from_attrs(devlink, attrs);
	if (!reporter) {
		devl_unlock(devlink);
		devlink_put(devlink);
	}
	return reporter;
}

int devlink_nl_health_reporter_dump_get_dumpit(struct sk_buff *skb,
					       struct netlink_callback *cb)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_health_reporter *reporter;
	struct devlink *devlink;
	int err;

	reporter = devlink_health_reporter_get_from_cb_lock(cb);
	if (!reporter)
		return -EINVAL;

	devlink = reporter->devlink;
	if (!reporter->ops->dump) {
		devl_unlock(devlink);
		devlink_put(devlink);
		return -EOPNOTSUPP;
	}

	if (!state->idx) {
		err = devlink_health_do_dump(reporter, NULL, cb->extack);
		if (err)
			goto unlock;
		state->dump_ts = reporter->dump_ts;
	}
	if (!reporter->dump_fmsg || state->dump_ts != reporter->dump_ts) {
		NL_SET_ERR_MSG(cb->extack, "Dump trampled, please retry");
		err = -EAGAIN;
		goto unlock;
	}

	err = devlink_fmsg_dumpit(reporter->dump_fmsg, skb, cb,
				  DEVLINK_CMD_HEALTH_REPORTER_DUMP_GET);
unlock:
	devl_unlock(devlink);
	devlink_put(devlink);
	return err;
}

int devlink_nl_health_reporter_dump_clear_doit(struct sk_buff *skb,
					       struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	if (!reporter->ops->dump)
		return -EOPNOTSUPP;

	devlink_health_dump_clear(reporter);
	return 0;
}

int devlink_nl_health_reporter_test_doit(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_health_reporter *reporter;

	reporter = devlink_health_reporter_get_from_info(devlink, info);
	if (!reporter)
		return -EINVAL;

	if (!reporter->ops->test)
		return -EOPNOTSUPP;

	return reporter->ops->test(reporter, info->extack);
}
