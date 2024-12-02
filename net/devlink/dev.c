// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <linux/device.h>
#include <net/genetlink.h>
#include <net/sock.h>
#include "devl_internal.h"

struct devlink_info_req {
	struct sk_buff *msg;
	void (*version_cb)(const char *version_name,
			   enum devlink_info_version_type version_type,
			   void *version_cb_priv);
	void *version_cb_priv;
};

struct devlink_reload_combination {
	enum devlink_reload_action action;
	enum devlink_reload_limit limit;
};

static const struct devlink_reload_combination devlink_reload_invalid_combinations[] = {
	{
		/* can't reinitialize driver with no down time */
		.action = DEVLINK_RELOAD_ACTION_DRIVER_REINIT,
		.limit = DEVLINK_RELOAD_LIMIT_NO_RESET,
	},
};

static bool
devlink_reload_combination_is_invalid(enum devlink_reload_action action,
				      enum devlink_reload_limit limit)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devlink_reload_invalid_combinations); i++)
		if (devlink_reload_invalid_combinations[i].action == action &&
		    devlink_reload_invalid_combinations[i].limit == limit)
			return true;
	return false;
}

static bool
devlink_reload_action_is_supported(struct devlink *devlink, enum devlink_reload_action action)
{
	return test_bit(action, &devlink->ops->reload_actions);
}

static bool
devlink_reload_limit_is_supported(struct devlink *devlink, enum devlink_reload_limit limit)
{
	return test_bit(limit, &devlink->ops->reload_limits);
}

static int devlink_reload_stat_put(struct sk_buff *msg,
				   enum devlink_reload_limit limit, u32 value)
{
	struct nlattr *reload_stats_entry;

	reload_stats_entry = nla_nest_start(msg, DEVLINK_ATTR_RELOAD_STATS_ENTRY);
	if (!reload_stats_entry)
		return -EMSGSIZE;

	if (nla_put_u8(msg, DEVLINK_ATTR_RELOAD_STATS_LIMIT, limit) ||
	    nla_put_u32(msg, DEVLINK_ATTR_RELOAD_STATS_VALUE, value))
		goto nla_put_failure;
	nla_nest_end(msg, reload_stats_entry);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, reload_stats_entry);
	return -EMSGSIZE;
}

static int
devlink_reload_stats_put(struct sk_buff *msg, struct devlink *devlink, bool is_remote)
{
	struct nlattr *reload_stats_attr, *act_info, *act_stats;
	int i, j, stat_idx;
	u32 value;

	if (!is_remote)
		reload_stats_attr = nla_nest_start(msg, DEVLINK_ATTR_RELOAD_STATS);
	else
		reload_stats_attr = nla_nest_start(msg, DEVLINK_ATTR_REMOTE_RELOAD_STATS);

	if (!reload_stats_attr)
		return -EMSGSIZE;

	for (i = 0; i <= DEVLINK_RELOAD_ACTION_MAX; i++) {
		if ((!is_remote &&
		     !devlink_reload_action_is_supported(devlink, i)) ||
		    i == DEVLINK_RELOAD_ACTION_UNSPEC)
			continue;
		act_info = nla_nest_start(msg, DEVLINK_ATTR_RELOAD_ACTION_INFO);
		if (!act_info)
			goto nla_put_failure;

		if (nla_put_u8(msg, DEVLINK_ATTR_RELOAD_ACTION, i))
			goto action_info_nest_cancel;
		act_stats = nla_nest_start(msg, DEVLINK_ATTR_RELOAD_ACTION_STATS);
		if (!act_stats)
			goto action_info_nest_cancel;

		for (j = 0; j <= DEVLINK_RELOAD_LIMIT_MAX; j++) {
			/* Remote stats are shown even if not locally supported.
			 * Stats of actions with unspecified limit are shown
			 * though drivers don't need to register unspecified
			 * limit.
			 */
			if ((!is_remote && j != DEVLINK_RELOAD_LIMIT_UNSPEC &&
			     !devlink_reload_limit_is_supported(devlink, j)) ||
			    devlink_reload_combination_is_invalid(i, j))
				continue;

			stat_idx = j * __DEVLINK_RELOAD_ACTION_MAX + i;
			if (!is_remote)
				value = devlink->stats.reload_stats[stat_idx];
			else
				value = devlink->stats.remote_reload_stats[stat_idx];
			if (devlink_reload_stat_put(msg, j, value))
				goto action_stats_nest_cancel;
		}
		nla_nest_end(msg, act_stats);
		nla_nest_end(msg, act_info);
	}
	nla_nest_end(msg, reload_stats_attr);
	return 0;

action_stats_nest_cancel:
	nla_nest_cancel(msg, act_stats);
action_info_nest_cancel:
	nla_nest_cancel(msg, act_info);
nla_put_failure:
	nla_nest_cancel(msg, reload_stats_attr);
	return -EMSGSIZE;
}

static int devlink_nl_nested_fill(struct sk_buff *msg, struct devlink *devlink)
{
	unsigned long rel_index;
	void *unused;
	int err;

	xa_for_each(&devlink->nested_rels, rel_index, unused) {
		err = devlink_rel_devlink_handle_put(msg, devlink,
						     rel_index,
						     DEVLINK_ATTR_NESTED_DEVLINK,
						     NULL);
		if (err)
			return err;
	}
	return 0;
}

static int devlink_nl_fill(struct sk_buff *msg, struct devlink *devlink,
			   enum devlink_command cmd, u32 portid,
			   u32 seq, int flags)
{
	struct nlattr *dev_stats;
	void *hdr;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;
	if (nla_put_u8(msg, DEVLINK_ATTR_RELOAD_FAILED, devlink->reload_failed))
		goto nla_put_failure;

	dev_stats = nla_nest_start(msg, DEVLINK_ATTR_DEV_STATS);
	if (!dev_stats)
		goto nla_put_failure;

	if (devlink_reload_stats_put(msg, devlink, false))
		goto dev_stats_nest_cancel;
	if (devlink_reload_stats_put(msg, devlink, true))
		goto dev_stats_nest_cancel;

	nla_nest_end(msg, dev_stats);

	if (devlink_nl_nested_fill(msg, devlink))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

dev_stats_nest_cancel:
	nla_nest_cancel(msg, dev_stats);
nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void devlink_notify(struct devlink *devlink, enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_NEW && cmd != DEVLINK_CMD_DEL);
	WARN_ON(!devl_is_registered(devlink));

	if (!devlink_nl_notify_need(devlink))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_fill(msg, devlink, cmd, 0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	devlink_nl_notify_send(devlink, msg);
}

int devlink_nl_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_fill(msg, devlink, DEVLINK_CMD_NEW,
			      info->snd_portid, info->snd_seq, 0);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int
devlink_nl_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
			struct netlink_callback *cb, int flags)
{
	return devlink_nl_fill(msg, devlink, DEVLINK_CMD_NEW,
			       NETLINK_CB(cb->skb).portid,
			       cb->nlh->nlmsg_seq, flags);
}

int devlink_nl_get_dumpit(struct sk_buff *msg, struct netlink_callback *cb)
{
	return devlink_nl_dumpit(msg, cb, devlink_nl_get_dump_one);
}

static void devlink_rel_notify_cb(struct devlink *devlink, u32 obj_index)
{
	devlink_notify(devlink, DEVLINK_CMD_NEW);
}

static void devlink_rel_cleanup_cb(struct devlink *devlink, u32 obj_index,
				   u32 rel_index)
{
	xa_erase(&devlink->nested_rels, rel_index);
}

int devl_nested_devlink_set(struct devlink *devlink,
			    struct devlink *nested_devlink)
{
	u32 rel_index;
	int err;

	err = devlink_rel_nested_in_add(&rel_index, devlink->index, 0,
					devlink_rel_notify_cb,
					devlink_rel_cleanup_cb,
					nested_devlink);
	if (err)
		return err;
	return xa_insert(&devlink->nested_rels, rel_index,
			 xa_mk_value(0), GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(devl_nested_devlink_set);

void devlink_notify_register(struct devlink *devlink)
{
	devlink_notify(devlink, DEVLINK_CMD_NEW);
	devlink_linecards_notify_register(devlink);
	devlink_ports_notify_register(devlink);
	devlink_trap_policers_notify_register(devlink);
	devlink_trap_groups_notify_register(devlink);
	devlink_traps_notify_register(devlink);
	devlink_rates_notify_register(devlink);
	devlink_regions_notify_register(devlink);
	devlink_params_notify_register(devlink);
}

void devlink_notify_unregister(struct devlink *devlink)
{
	devlink_params_notify_unregister(devlink);
	devlink_regions_notify_unregister(devlink);
	devlink_rates_notify_unregister(devlink);
	devlink_traps_notify_unregister(devlink);
	devlink_trap_groups_notify_unregister(devlink);
	devlink_trap_policers_notify_unregister(devlink);
	devlink_ports_notify_unregister(devlink);
	devlink_linecards_notify_unregister(devlink);
	devlink_notify(devlink, DEVLINK_CMD_DEL);
}

static void devlink_reload_failed_set(struct devlink *devlink,
				      bool reload_failed)
{
	if (devlink->reload_failed == reload_failed)
		return;
	devlink->reload_failed = reload_failed;
	devlink_notify(devlink, DEVLINK_CMD_NEW);
}

bool devlink_is_reload_failed(const struct devlink *devlink)
{
	return devlink->reload_failed;
}
EXPORT_SYMBOL_GPL(devlink_is_reload_failed);

static void
__devlink_reload_stats_update(struct devlink *devlink, u32 *reload_stats,
			      enum devlink_reload_limit limit, u32 actions_performed)
{
	unsigned long actions = actions_performed;
	int stat_idx;
	int action;

	for_each_set_bit(action, &actions, __DEVLINK_RELOAD_ACTION_MAX) {
		stat_idx = limit * __DEVLINK_RELOAD_ACTION_MAX + action;
		reload_stats[stat_idx]++;
	}
	devlink_notify(devlink, DEVLINK_CMD_NEW);
}

static void
devlink_reload_stats_update(struct devlink *devlink, enum devlink_reload_limit limit,
			    u32 actions_performed)
{
	__devlink_reload_stats_update(devlink, devlink->stats.reload_stats, limit,
				      actions_performed);
}

/**
 *	devlink_remote_reload_actions_performed - Update devlink on reload actions
 *	  performed which are not a direct result of devlink reload call.
 *
 *	This should be called by a driver after performing reload actions in case it was not
 *	a result of devlink reload call. For example fw_activate was performed as a result
 *	of devlink reload triggered fw_activate on another host.
 *	The motivation for this function is to keep data on reload actions performed on this
 *	function whether it was done due to direct devlink reload call or not.
 *
 *	@devlink: devlink
 *	@limit: reload limit
 *	@actions_performed: bitmask of actions performed
 */
void devlink_remote_reload_actions_performed(struct devlink *devlink,
					     enum devlink_reload_limit limit,
					     u32 actions_performed)
{
	if (WARN_ON(!actions_performed ||
		    actions_performed & BIT(DEVLINK_RELOAD_ACTION_UNSPEC) ||
		    actions_performed >= BIT(__DEVLINK_RELOAD_ACTION_MAX) ||
		    limit > DEVLINK_RELOAD_LIMIT_MAX))
		return;

	__devlink_reload_stats_update(devlink, devlink->stats.remote_reload_stats, limit,
				      actions_performed);
}
EXPORT_SYMBOL_GPL(devlink_remote_reload_actions_performed);

static struct net *devlink_netns_get(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct nlattr *netns_pid_attr = info->attrs[DEVLINK_ATTR_NETNS_PID];
	struct nlattr *netns_fd_attr = info->attrs[DEVLINK_ATTR_NETNS_FD];
	struct nlattr *netns_id_attr = info->attrs[DEVLINK_ATTR_NETNS_ID];
	struct net *net;

	if (!!netns_pid_attr + !!netns_fd_attr + !!netns_id_attr > 1) {
		NL_SET_ERR_MSG(info->extack, "multiple netns identifying attributes specified");
		return ERR_PTR(-EINVAL);
	}

	if (netns_pid_attr) {
		net = get_net_ns_by_pid(nla_get_u32(netns_pid_attr));
	} else if (netns_fd_attr) {
		net = get_net_ns_by_fd(nla_get_u32(netns_fd_attr));
	} else if (netns_id_attr) {
		net = get_net_ns_by_id(sock_net(skb->sk),
				       nla_get_u32(netns_id_attr));
		if (!net)
			net = ERR_PTR(-EINVAL);
	} else {
		WARN_ON(1);
		net = ERR_PTR(-EINVAL);
	}
	if (IS_ERR(net)) {
		NL_SET_ERR_MSG(info->extack, "Unknown network namespace");
		return ERR_PTR(-EINVAL);
	}
	if (!netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN)) {
		put_net(net);
		return ERR_PTR(-EPERM);
	}
	return net;
}

static void devlink_reload_netns_change(struct devlink *devlink,
					struct net *curr_net,
					struct net *dest_net)
{
	/* Userspace needs to be notified about devlink objects
	 * removed from original and entering new network namespace.
	 * The rest of the devlink objects are re-created during
	 * reload process so the notifications are generated separatelly.
	 */
	devlink_notify_unregister(devlink);
	write_pnet(&devlink->_net, dest_net);
	devlink_notify_register(devlink);
	devlink_rel_nested_in_notify(devlink);
}

static void devlink_reload_reinit_sanity_check(struct devlink *devlink)
{
	WARN_ON(!list_empty(&devlink->trap_policer_list));
	WARN_ON(!list_empty(&devlink->trap_group_list));
	WARN_ON(!list_empty(&devlink->trap_list));
	WARN_ON(!list_empty(&devlink->dpipe_table_list));
	WARN_ON(!list_empty(&devlink->sb_list));
	WARN_ON(!list_empty(&devlink->rate_list));
	WARN_ON(!list_empty(&devlink->linecard_list));
	WARN_ON(!xa_empty(&devlink->ports));
}

int devlink_reload(struct devlink *devlink, struct net *dest_net,
		   enum devlink_reload_action action,
		   enum devlink_reload_limit limit,
		   u32 *actions_performed, struct netlink_ext_ack *extack)
{
	u32 remote_reload_stats[DEVLINK_RELOAD_STATS_ARRAY_SIZE];
	struct net *curr_net;
	int err;

	/* Make sure the reload operations are invoked with the device lock
	 * held to allow drivers to trigger functionality that expects it
	 * (e.g., PCI reset) and to close possible races between these
	 * operations and probe/remove.
	 */
	device_lock_assert(devlink->dev);

	memcpy(remote_reload_stats, devlink->stats.remote_reload_stats,
	       sizeof(remote_reload_stats));

	err = devlink->ops->reload_down(devlink, !!dest_net, action, limit, extack);
	if (err)
		return err;

	curr_net = devlink_net(devlink);
	if (dest_net && !net_eq(dest_net, curr_net))
		devlink_reload_netns_change(devlink, curr_net, dest_net);

	if (action == DEVLINK_RELOAD_ACTION_DRIVER_REINIT) {
		devlink_params_driverinit_load_new(devlink);
		devlink_reload_reinit_sanity_check(devlink);
	}

	err = devlink->ops->reload_up(devlink, action, limit, actions_performed, extack);
	devlink_reload_failed_set(devlink, !!err);
	if (err)
		return err;

	WARN_ON(!(*actions_performed & BIT(action)));
	/* Catch driver on updating the remote action within devlink reload */
	WARN_ON(memcmp(remote_reload_stats, devlink->stats.remote_reload_stats,
		       sizeof(remote_reload_stats)));
	devlink_reload_stats_update(devlink, limit, *actions_performed);
	return 0;
}

static int
devlink_nl_reload_actions_performed_snd(struct devlink *devlink, u32 actions_performed,
					enum devlink_command cmd, struct genl_info *info)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq, &devlink_nl_family, 0, cmd);
	if (!hdr)
		goto free_msg;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_bitfield32(msg, DEVLINK_ATTR_RELOAD_ACTIONS_PERFORMED, actions_performed,
			       actions_performed))
		goto nla_put_failure;
	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);

nla_put_failure:
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

int devlink_nl_reload_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	enum devlink_reload_action action;
	enum devlink_reload_limit limit;
	struct net *dest_net = NULL;
	u32 actions_performed;
	int err;

	err = devlink_resources_validate(devlink, NULL, info);
	if (err) {
		NL_SET_ERR_MSG(info->extack, "resources size validation failed");
		return err;
	}

	action = nla_get_u8_default(info->attrs[DEVLINK_ATTR_RELOAD_ACTION],
				    DEVLINK_RELOAD_ACTION_DRIVER_REINIT);

	if (!devlink_reload_action_is_supported(devlink, action)) {
		NL_SET_ERR_MSG(info->extack, "Requested reload action is not supported by the driver");
		return -EOPNOTSUPP;
	}

	limit = DEVLINK_RELOAD_LIMIT_UNSPEC;
	if (info->attrs[DEVLINK_ATTR_RELOAD_LIMITS]) {
		struct nla_bitfield32 limits;
		u32 limits_selected;

		limits = nla_get_bitfield32(info->attrs[DEVLINK_ATTR_RELOAD_LIMITS]);
		limits_selected = limits.value & limits.selector;
		if (!limits_selected) {
			NL_SET_ERR_MSG(info->extack, "Invalid limit selected");
			return -EINVAL;
		}
		for (limit = 0 ; limit <= DEVLINK_RELOAD_LIMIT_MAX ; limit++)
			if (limits_selected & BIT(limit))
				break;
		/* UAPI enables multiselection, but currently it is not used */
		if (limits_selected != BIT(limit)) {
			NL_SET_ERR_MSG(info->extack, "Multiselection of limit is not supported");
			return -EOPNOTSUPP;
		}
		if (!devlink_reload_limit_is_supported(devlink, limit)) {
			NL_SET_ERR_MSG(info->extack, "Requested limit is not supported by the driver");
			return -EOPNOTSUPP;
		}
		if (devlink_reload_combination_is_invalid(action, limit)) {
			NL_SET_ERR_MSG(info->extack, "Requested limit is invalid for this action");
			return -EINVAL;
		}
	}
	if (info->attrs[DEVLINK_ATTR_NETNS_PID] ||
	    info->attrs[DEVLINK_ATTR_NETNS_FD] ||
	    info->attrs[DEVLINK_ATTR_NETNS_ID]) {
		dest_net = devlink_netns_get(skb, info);
		if (IS_ERR(dest_net))
			return PTR_ERR(dest_net);
		if (!net_eq(dest_net, devlink_net(devlink)) &&
		    action != DEVLINK_RELOAD_ACTION_DRIVER_REINIT) {
			NL_SET_ERR_MSG_MOD(info->extack,
					   "Changing namespace is only supported for reinit action");
			return -EOPNOTSUPP;
		}
	}

	err = devlink_reload(devlink, dest_net, action, limit, &actions_performed, info->extack);

	if (dest_net)
		put_net(dest_net);

	if (err)
		return err;
	/* For backward compatibility generate reply only if attributes used by user */
	if (!info->attrs[DEVLINK_ATTR_RELOAD_ACTION] && !info->attrs[DEVLINK_ATTR_RELOAD_LIMITS])
		return 0;

	return devlink_nl_reload_actions_performed_snd(devlink, actions_performed,
						       DEVLINK_CMD_RELOAD, info);
}

bool devlink_reload_actions_valid(const struct devlink_ops *ops)
{
	const struct devlink_reload_combination *comb;
	int i;

	if (!devlink_reload_supported(ops)) {
		if (WARN_ON(ops->reload_actions))
			return false;
		return true;
	}

	if (WARN_ON(!ops->reload_actions ||
		    ops->reload_actions & BIT(DEVLINK_RELOAD_ACTION_UNSPEC) ||
		    ops->reload_actions >= BIT(__DEVLINK_RELOAD_ACTION_MAX)))
		return false;

	if (WARN_ON(ops->reload_limits & BIT(DEVLINK_RELOAD_LIMIT_UNSPEC) ||
		    ops->reload_limits >= BIT(__DEVLINK_RELOAD_LIMIT_MAX)))
		return false;

	for (i = 0; i < ARRAY_SIZE(devlink_reload_invalid_combinations); i++)  {
		comb = &devlink_reload_invalid_combinations[i];
		if (ops->reload_actions == BIT(comb->action) &&
		    ops->reload_limits == BIT(comb->limit))
			return false;
	}
	return true;
}

static int devlink_nl_eswitch_fill(struct sk_buff *msg, struct devlink *devlink,
				   enum devlink_command cmd, u32 portid,
				   u32 seq, int flags)
{
	const struct devlink_ops *ops = devlink->ops;
	enum devlink_eswitch_encap_mode encap_mode;
	u8 inline_mode;
	void *hdr;
	int err = 0;
	u16 mode;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	err = devlink_nl_put_handle(msg, devlink);
	if (err)
		goto nla_put_failure;

	if (ops->eswitch_mode_get) {
		err = ops->eswitch_mode_get(devlink, &mode);
		if (err)
			goto nla_put_failure;
		err = nla_put_u16(msg, DEVLINK_ATTR_ESWITCH_MODE, mode);
		if (err)
			goto nla_put_failure;
	}

	if (ops->eswitch_inline_mode_get) {
		err = ops->eswitch_inline_mode_get(devlink, &inline_mode);
		if (err)
			goto nla_put_failure;
		err = nla_put_u8(msg, DEVLINK_ATTR_ESWITCH_INLINE_MODE,
				 inline_mode);
		if (err)
			goto nla_put_failure;
	}

	if (ops->eswitch_encap_mode_get) {
		err = ops->eswitch_encap_mode_get(devlink, &encap_mode);
		if (err)
			goto nla_put_failure;
		err = nla_put_u8(msg, DEVLINK_ATTR_ESWITCH_ENCAP_MODE, encap_mode);
		if (err)
			goto nla_put_failure;
	}

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return err;
}

int devlink_nl_eswitch_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_eswitch_fill(msg, devlink, DEVLINK_CMD_ESWITCH_GET,
				      info->snd_portid, info->snd_seq, 0);

	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

int devlink_nl_eswitch_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	const struct devlink_ops *ops = devlink->ops;
	enum devlink_eswitch_encap_mode encap_mode;
	u8 inline_mode;
	int err = 0;
	u16 mode;

	if (info->attrs[DEVLINK_ATTR_ESWITCH_MODE]) {
		if (!ops->eswitch_mode_set)
			return -EOPNOTSUPP;
		mode = nla_get_u16(info->attrs[DEVLINK_ATTR_ESWITCH_MODE]);
		err = devlink_rate_nodes_check(devlink, mode, info->extack);
		if (err)
			return err;
		err = ops->eswitch_mode_set(devlink, mode, info->extack);
		if (err)
			return err;
	}

	if (info->attrs[DEVLINK_ATTR_ESWITCH_INLINE_MODE]) {
		if (!ops->eswitch_inline_mode_set)
			return -EOPNOTSUPP;
		inline_mode = nla_get_u8(info->attrs[DEVLINK_ATTR_ESWITCH_INLINE_MODE]);
		err = ops->eswitch_inline_mode_set(devlink, inline_mode,
						   info->extack);
		if (err)
			return err;
	}

	if (info->attrs[DEVLINK_ATTR_ESWITCH_ENCAP_MODE]) {
		if (!ops->eswitch_encap_mode_set)
			return -EOPNOTSUPP;
		encap_mode = nla_get_u8(info->attrs[DEVLINK_ATTR_ESWITCH_ENCAP_MODE]);
		err = ops->eswitch_encap_mode_set(devlink, encap_mode,
						  info->extack);
		if (err)
			return err;
	}

	return 0;
}

int devlink_info_serial_number_put(struct devlink_info_req *req, const char *sn)
{
	if (!req->msg)
		return 0;
	return nla_put_string(req->msg, DEVLINK_ATTR_INFO_SERIAL_NUMBER, sn);
}
EXPORT_SYMBOL_GPL(devlink_info_serial_number_put);

int devlink_info_board_serial_number_put(struct devlink_info_req *req,
					 const char *bsn)
{
	if (!req->msg)
		return 0;
	return nla_put_string(req->msg, DEVLINK_ATTR_INFO_BOARD_SERIAL_NUMBER,
			      bsn);
}
EXPORT_SYMBOL_GPL(devlink_info_board_serial_number_put);

static int devlink_info_version_put(struct devlink_info_req *req, int attr,
				    const char *version_name,
				    const char *version_value,
				    enum devlink_info_version_type version_type)
{
	struct nlattr *nest;
	int err;

	if (req->version_cb)
		req->version_cb(version_name, version_type,
				req->version_cb_priv);

	if (!req->msg)
		return 0;

	nest = nla_nest_start_noflag(req->msg, attr);
	if (!nest)
		return -EMSGSIZE;

	err = nla_put_string(req->msg, DEVLINK_ATTR_INFO_VERSION_NAME,
			     version_name);
	if (err)
		goto nla_put_failure;

	err = nla_put_string(req->msg, DEVLINK_ATTR_INFO_VERSION_VALUE,
			     version_value);
	if (err)
		goto nla_put_failure;

	nla_nest_end(req->msg, nest);

	return 0;

nla_put_failure:
	nla_nest_cancel(req->msg, nest);
	return err;
}

int devlink_info_version_fixed_put(struct devlink_info_req *req,
				   const char *version_name,
				   const char *version_value)
{
	return devlink_info_version_put(req, DEVLINK_ATTR_INFO_VERSION_FIXED,
					version_name, version_value,
					DEVLINK_INFO_VERSION_TYPE_NONE);
}
EXPORT_SYMBOL_GPL(devlink_info_version_fixed_put);

int devlink_info_version_stored_put(struct devlink_info_req *req,
				    const char *version_name,
				    const char *version_value)
{
	return devlink_info_version_put(req, DEVLINK_ATTR_INFO_VERSION_STORED,
					version_name, version_value,
					DEVLINK_INFO_VERSION_TYPE_NONE);
}
EXPORT_SYMBOL_GPL(devlink_info_version_stored_put);

int devlink_info_version_stored_put_ext(struct devlink_info_req *req,
					const char *version_name,
					const char *version_value,
					enum devlink_info_version_type version_type)
{
	return devlink_info_version_put(req, DEVLINK_ATTR_INFO_VERSION_STORED,
					version_name, version_value,
					version_type);
}
EXPORT_SYMBOL_GPL(devlink_info_version_stored_put_ext);

int devlink_info_version_running_put(struct devlink_info_req *req,
				     const char *version_name,
				     const char *version_value)
{
	return devlink_info_version_put(req, DEVLINK_ATTR_INFO_VERSION_RUNNING,
					version_name, version_value,
					DEVLINK_INFO_VERSION_TYPE_NONE);
}
EXPORT_SYMBOL_GPL(devlink_info_version_running_put);

int devlink_info_version_running_put_ext(struct devlink_info_req *req,
					 const char *version_name,
					 const char *version_value,
					 enum devlink_info_version_type version_type)
{
	return devlink_info_version_put(req, DEVLINK_ATTR_INFO_VERSION_RUNNING,
					version_name, version_value,
					version_type);
}
EXPORT_SYMBOL_GPL(devlink_info_version_running_put_ext);

static int devlink_nl_driver_info_get(struct device_driver *drv,
				      struct devlink_info_req *req)
{
	if (!drv)
		return 0;

	if (drv->name[0])
		return nla_put_string(req->msg, DEVLINK_ATTR_INFO_DRIVER_NAME,
				      drv->name);

	return 0;
}

static int
devlink_nl_info_fill(struct sk_buff *msg, struct devlink *devlink,
		     enum devlink_command cmd, u32 portid,
		     u32 seq, int flags, struct netlink_ext_ack *extack)
{
	struct device *dev = devlink_to_dev(devlink);
	struct devlink_info_req req = {};
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	err = -EMSGSIZE;
	if (devlink_nl_put_handle(msg, devlink))
		goto err_cancel_msg;

	req.msg = msg;
	if (devlink->ops->info_get) {
		err = devlink->ops->info_get(devlink, &req, extack);
		if (err)
			goto err_cancel_msg;
	}

	err = devlink_nl_driver_info_get(dev->driver, &req);
	if (err)
		goto err_cancel_msg;

	genlmsg_end(msg, hdr);
	return 0;

err_cancel_msg:
	genlmsg_cancel(msg, hdr);
	return err;
}

int devlink_nl_info_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_info_fill(msg, devlink, DEVLINK_CMD_INFO_GET,
				   info->snd_portid, info->snd_seq, 0,
				   info->extack);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int
devlink_nl_info_get_dump_one(struct sk_buff *msg, struct devlink *devlink,
			     struct netlink_callback *cb, int flags)
{
	int err;

	err = devlink_nl_info_fill(msg, devlink, DEVLINK_CMD_INFO_GET,
				   NETLINK_CB(cb->skb).portid,
				   cb->nlh->nlmsg_seq, flags,
				   cb->extack);
	if (err == -EOPNOTSUPP)
		err = 0;
	return err;
}

int devlink_nl_info_get_dumpit(struct sk_buff *msg, struct netlink_callback *cb)
{
	return devlink_nl_dumpit(msg, cb, devlink_nl_info_get_dump_one);
}

static int devlink_nl_flash_update_fill(struct sk_buff *msg,
					struct devlink *devlink,
					enum devlink_command cmd,
					struct devlink_flash_notify *params)
{
	void *hdr;

	hdr = genlmsg_put(msg, 0, 0, &devlink_nl_family, 0, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (cmd != DEVLINK_CMD_FLASH_UPDATE_STATUS)
		goto out;

	if (params->status_msg &&
	    nla_put_string(msg, DEVLINK_ATTR_FLASH_UPDATE_STATUS_MSG,
			   params->status_msg))
		goto nla_put_failure;
	if (params->component &&
	    nla_put_string(msg, DEVLINK_ATTR_FLASH_UPDATE_COMPONENT,
			   params->component))
		goto nla_put_failure;
	if (devlink_nl_put_u64(msg, DEVLINK_ATTR_FLASH_UPDATE_STATUS_DONE,
			       params->done))
		goto nla_put_failure;
	if (devlink_nl_put_u64(msg, DEVLINK_ATTR_FLASH_UPDATE_STATUS_TOTAL,
			       params->total))
		goto nla_put_failure;
	if (devlink_nl_put_u64(msg, DEVLINK_ATTR_FLASH_UPDATE_STATUS_TIMEOUT,
			       params->timeout))
		goto nla_put_failure;

out:
	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static void __devlink_flash_update_notify(struct devlink *devlink,
					  enum devlink_command cmd,
					  struct devlink_flash_notify *params)
{
	struct sk_buff *msg;
	int err;

	WARN_ON(cmd != DEVLINK_CMD_FLASH_UPDATE &&
		cmd != DEVLINK_CMD_FLASH_UPDATE_END &&
		cmd != DEVLINK_CMD_FLASH_UPDATE_STATUS);

	if (!devl_is_registered(devlink) || !devlink_nl_notify_need(devlink))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_flash_update_fill(msg, devlink, cmd, params);
	if (err)
		goto out_free_msg;

	devlink_nl_notify_send(devlink, msg);
	return;

out_free_msg:
	nlmsg_free(msg);
}

static void devlink_flash_update_begin_notify(struct devlink *devlink)
{
	struct devlink_flash_notify params = {};

	__devlink_flash_update_notify(devlink,
				      DEVLINK_CMD_FLASH_UPDATE,
				      &params);
}

static void devlink_flash_update_end_notify(struct devlink *devlink)
{
	struct devlink_flash_notify params = {};

	__devlink_flash_update_notify(devlink,
				      DEVLINK_CMD_FLASH_UPDATE_END,
				      &params);
}

void devlink_flash_update_status_notify(struct devlink *devlink,
					const char *status_msg,
					const char *component,
					unsigned long done,
					unsigned long total)
{
	struct devlink_flash_notify params = {
		.status_msg = status_msg,
		.component = component,
		.done = done,
		.total = total,
	};

	__devlink_flash_update_notify(devlink,
				      DEVLINK_CMD_FLASH_UPDATE_STATUS,
				      &params);
}
EXPORT_SYMBOL_GPL(devlink_flash_update_status_notify);

void devlink_flash_update_timeout_notify(struct devlink *devlink,
					 const char *status_msg,
					 const char *component,
					 unsigned long timeout)
{
	struct devlink_flash_notify params = {
		.status_msg = status_msg,
		.component = component,
		.timeout = timeout,
	};

	__devlink_flash_update_notify(devlink,
				      DEVLINK_CMD_FLASH_UPDATE_STATUS,
				      &params);
}
EXPORT_SYMBOL_GPL(devlink_flash_update_timeout_notify);

struct devlink_flash_component_lookup_ctx {
	const char *lookup_name;
	bool lookup_name_found;
};

static void
devlink_flash_component_lookup_cb(const char *version_name,
				  enum devlink_info_version_type version_type,
				  void *version_cb_priv)
{
	struct devlink_flash_component_lookup_ctx *lookup_ctx = version_cb_priv;

	if (version_type != DEVLINK_INFO_VERSION_TYPE_COMPONENT ||
	    lookup_ctx->lookup_name_found)
		return;

	lookup_ctx->lookup_name_found =
		!strcmp(lookup_ctx->lookup_name, version_name);
}

static int devlink_flash_component_get(struct devlink *devlink,
				       struct nlattr *nla_component,
				       const char **p_component,
				       struct netlink_ext_ack *extack)
{
	struct devlink_flash_component_lookup_ctx lookup_ctx = {};
	struct devlink_info_req req = {};
	const char *component;
	int ret;

	if (!nla_component)
		return 0;

	component = nla_data(nla_component);

	if (!devlink->ops->info_get) {
		NL_SET_ERR_MSG_ATTR(extack, nla_component,
				    "component update is not supported by this device");
		return -EOPNOTSUPP;
	}

	lookup_ctx.lookup_name = component;
	req.version_cb = devlink_flash_component_lookup_cb;
	req.version_cb_priv = &lookup_ctx;

	ret = devlink->ops->info_get(devlink, &req, NULL);
	if (ret)
		return ret;

	if (!lookup_ctx.lookup_name_found) {
		NL_SET_ERR_MSG_ATTR(extack, nla_component,
				    "selected component is not supported by this device");
		return -EINVAL;
	}
	*p_component = component;
	return 0;
}

int devlink_nl_flash_update_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *nla_overwrite_mask, *nla_file_name;
	struct devlink_flash_update_params params = {};
	struct devlink *devlink = info->user_ptr[0];
	const char *file_name;
	u32 supported_params;
	int ret;

	if (!devlink->ops->flash_update)
		return -EOPNOTSUPP;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME))
		return -EINVAL;

	ret = devlink_flash_component_get(devlink,
					  info->attrs[DEVLINK_ATTR_FLASH_UPDATE_COMPONENT],
					  &params.component, info->extack);
	if (ret)
		return ret;

	supported_params = devlink->ops->supported_flash_update_params;

	nla_overwrite_mask = info->attrs[DEVLINK_ATTR_FLASH_UPDATE_OVERWRITE_MASK];
	if (nla_overwrite_mask) {
		struct nla_bitfield32 sections;

		if (!(supported_params & DEVLINK_SUPPORT_FLASH_UPDATE_OVERWRITE_MASK)) {
			NL_SET_ERR_MSG_ATTR(info->extack, nla_overwrite_mask,
					    "overwrite settings are not supported by this device");
			return -EOPNOTSUPP;
		}
		sections = nla_get_bitfield32(nla_overwrite_mask);
		params.overwrite_mask = sections.value & sections.selector;
	}

	nla_file_name = info->attrs[DEVLINK_ATTR_FLASH_UPDATE_FILE_NAME];
	file_name = nla_data(nla_file_name);
	ret = request_firmware(&params.fw, file_name, devlink->dev);
	if (ret) {
		NL_SET_ERR_MSG_ATTR(info->extack, nla_file_name,
				    "failed to locate the requested firmware file");
		return ret;
	}

	devlink_flash_update_begin_notify(devlink);
	ret = devlink->ops->flash_update(devlink, &params, info->extack);
	devlink_flash_update_end_notify(devlink);

	release_firmware(params.fw);

	return ret;
}

static void __devlink_compat_running_version(struct devlink *devlink,
					     char *buf, size_t len)
{
	struct devlink_info_req req = {};
	const struct nlattr *nlattr;
	struct sk_buff *msg;
	int rem, err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	req.msg = msg;
	err = devlink->ops->info_get(devlink, &req, NULL);
	if (err)
		goto free_msg;

	nla_for_each_attr_type(nlattr, DEVLINK_ATTR_INFO_VERSION_RUNNING,
			       (void *)msg->data, msg->len, rem) {
		const struct nlattr *kv;
		int rem_kv;

		nla_for_each_nested_type(kv, DEVLINK_ATTR_INFO_VERSION_VALUE,
					 nlattr, rem_kv) {
			strlcat(buf, nla_data(kv), len);
			strlcat(buf, " ", len);
		}
	}
free_msg:
	nlmsg_consume(msg);
}

void devlink_compat_running_version(struct devlink *devlink,
				    char *buf, size_t len)
{
	if (!devlink->ops->info_get)
		return;

	devl_lock(devlink);
	if (devl_is_registered(devlink))
		__devlink_compat_running_version(devlink, buf, len);
	devl_unlock(devlink);
}

int devlink_compat_flash_update(struct devlink *devlink, const char *file_name)
{
	struct devlink_flash_update_params params = {};
	int ret;

	devl_lock(devlink);
	if (!devl_is_registered(devlink)) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (!devlink->ops->flash_update) {
		ret = -EOPNOTSUPP;
		goto out_unlock;
	}

	ret = request_firmware(&params.fw, file_name, devlink->dev);
	if (ret)
		goto out_unlock;

	devlink_flash_update_begin_notify(devlink);
	ret = devlink->ops->flash_update(devlink, &params, NULL);
	devlink_flash_update_end_notify(devlink);

	release_firmware(params.fw);
out_unlock:
	devl_unlock(devlink);

	return ret;
}

static int
devlink_nl_selftests_fill(struct sk_buff *msg, struct devlink *devlink,
			  u32 portid, u32 seq, int flags,
			  struct netlink_ext_ack *extack)
{
	struct nlattr *selftests;
	void *hdr;
	int err;
	int i;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags,
			  DEVLINK_CMD_SELFTESTS_GET);
	if (!hdr)
		return -EMSGSIZE;

	err = -EMSGSIZE;
	if (devlink_nl_put_handle(msg, devlink))
		goto err_cancel_msg;

	selftests = nla_nest_start(msg, DEVLINK_ATTR_SELFTESTS);
	if (!selftests)
		goto err_cancel_msg;

	for (i = DEVLINK_ATTR_SELFTEST_ID_UNSPEC + 1;
	     i <= DEVLINK_ATTR_SELFTEST_ID_MAX; i++) {
		if (devlink->ops->selftest_check(devlink, i, extack)) {
			err = nla_put_flag(msg, i);
			if (err)
				goto err_cancel_msg;
		}
	}

	nla_nest_end(msg, selftests);
	genlmsg_end(msg, hdr);
	return 0;

err_cancel_msg:
	genlmsg_cancel(msg, hdr);
	return err;
}

int devlink_nl_selftests_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	if (!devlink->ops->selftest_check)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_selftests_fill(msg, devlink, info->snd_portid,
					info->snd_seq, 0, info->extack);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_selftests_get_dump_one(struct sk_buff *msg,
					     struct devlink *devlink,
					     struct netlink_callback *cb,
					     int flags)
{
	if (!devlink->ops->selftest_check)
		return 0;

	return devlink_nl_selftests_fill(msg, devlink,
					 NETLINK_CB(cb->skb).portid,
					 cb->nlh->nlmsg_seq, flags,
					 cb->extack);
}

int devlink_nl_selftests_get_dumpit(struct sk_buff *skb,
				    struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_selftests_get_dump_one);
}

static int devlink_selftest_result_put(struct sk_buff *skb, unsigned int id,
				       enum devlink_selftest_status test_status)
{
	struct nlattr *result_attr;

	result_attr = nla_nest_start(skb, DEVLINK_ATTR_SELFTEST_RESULT);
	if (!result_attr)
		return -EMSGSIZE;

	if (nla_put_u32(skb, DEVLINK_ATTR_SELFTEST_RESULT_ID, id) ||
	    nla_put_u8(skb, DEVLINK_ATTR_SELFTEST_RESULT_STATUS,
		       test_status))
		goto nla_put_failure;

	nla_nest_end(skb, result_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, result_attr);
	return -EMSGSIZE;
}

static const struct nla_policy devlink_selftest_nl_policy[DEVLINK_ATTR_SELFTEST_ID_MAX + 1] = {
	[DEVLINK_ATTR_SELFTEST_ID_FLASH] = { .type = NLA_FLAG },
};

int devlink_nl_selftests_run_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[DEVLINK_ATTR_SELFTEST_ID_MAX + 1];
	struct devlink *devlink = info->user_ptr[0];
	struct nlattr *attrs, *selftests;
	struct sk_buff *msg;
	void *hdr;
	int err;
	int i;

	if (!devlink->ops->selftest_run || !devlink->ops->selftest_check)
		return -EOPNOTSUPP;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_SELFTESTS))
		return -EINVAL;

	attrs = info->attrs[DEVLINK_ATTR_SELFTESTS];

	err = nla_parse_nested(tb, DEVLINK_ATTR_SELFTEST_ID_MAX, attrs,
			       devlink_selftest_nl_policy, info->extack);
	if (err < 0)
		return err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = -EMSGSIZE;
	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq,
			  &devlink_nl_family, 0, DEVLINK_CMD_SELFTESTS_RUN);
	if (!hdr)
		goto free_msg;

	if (devlink_nl_put_handle(msg, devlink))
		goto genlmsg_cancel;

	selftests = nla_nest_start(msg, DEVLINK_ATTR_SELFTESTS);
	if (!selftests)
		goto genlmsg_cancel;

	for (i = DEVLINK_ATTR_SELFTEST_ID_UNSPEC + 1;
	     i <= DEVLINK_ATTR_SELFTEST_ID_MAX; i++) {
		enum devlink_selftest_status test_status;

		if (nla_get_flag(tb[i])) {
			if (!devlink->ops->selftest_check(devlink, i,
							  info->extack)) {
				if (devlink_selftest_result_put(msg, i,
								DEVLINK_SELFTEST_STATUS_SKIP))
					goto selftests_nest_cancel;
				continue;
			}

			test_status = devlink->ops->selftest_run(devlink, i,
								 info->extack);
			if (devlink_selftest_result_put(msg, i, test_status))
				goto selftests_nest_cancel;
		}
	}

	nla_nest_end(msg, selftests);
	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

selftests_nest_cancel:
	nla_nest_cancel(msg, selftests);
genlmsg_cancel:
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return err;
}
