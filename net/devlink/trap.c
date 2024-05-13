// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include <trace/events/devlink.h>

#include "devl_internal.h"

struct devlink_stats {
	u64_stats_t rx_bytes;
	u64_stats_t rx_packets;
	struct u64_stats_sync syncp;
};

/**
 * struct devlink_trap_policer_item - Packet trap policer attributes.
 * @policer: Immutable packet trap policer attributes.
 * @rate: Rate in packets / sec.
 * @burst: Burst size in packets.
 * @list: trap_policer_list member.
 *
 * Describes packet trap policer attributes. Created by devlink during trap
 * policer registration.
 */
struct devlink_trap_policer_item {
	const struct devlink_trap_policer *policer;
	u64 rate;
	u64 burst;
	struct list_head list;
};

/**
 * struct devlink_trap_group_item - Packet trap group attributes.
 * @group: Immutable packet trap group attributes.
 * @policer_item: Associated policer item. Can be NULL.
 * @list: trap_group_list member.
 * @stats: Trap group statistics.
 *
 * Describes packet trap group attributes. Created by devlink during trap
 * group registration.
 */
struct devlink_trap_group_item {
	const struct devlink_trap_group *group;
	struct devlink_trap_policer_item *policer_item;
	struct list_head list;
	struct devlink_stats __percpu *stats;
};

/**
 * struct devlink_trap_item - Packet trap attributes.
 * @trap: Immutable packet trap attributes.
 * @group_item: Associated group item.
 * @list: trap_list member.
 * @action: Trap action.
 * @stats: Trap statistics.
 * @priv: Driver private information.
 *
 * Describes both mutable and immutable packet trap attributes. Created by
 * devlink during trap registration and used for all trap related operations.
 */
struct devlink_trap_item {
	const struct devlink_trap *trap;
	struct devlink_trap_group_item *group_item;
	struct list_head list;
	enum devlink_trap_action action;
	struct devlink_stats __percpu *stats;
	void *priv;
};

static struct devlink_trap_policer_item *
devlink_trap_policer_item_lookup(struct devlink *devlink, u32 id)
{
	struct devlink_trap_policer_item *policer_item;

	list_for_each_entry(policer_item, &devlink->trap_policer_list, list) {
		if (policer_item->policer->id == id)
			return policer_item;
	}

	return NULL;
}

static struct devlink_trap_item *
devlink_trap_item_lookup(struct devlink *devlink, const char *name)
{
	struct devlink_trap_item *trap_item;

	list_for_each_entry(trap_item, &devlink->trap_list, list) {
		if (!strcmp(trap_item->trap->name, name))
			return trap_item;
	}

	return NULL;
}

static struct devlink_trap_item *
devlink_trap_item_get_from_info(struct devlink *devlink,
				struct genl_info *info)
{
	struct nlattr *attr;

	if (!info->attrs[DEVLINK_ATTR_TRAP_NAME])
		return NULL;
	attr = info->attrs[DEVLINK_ATTR_TRAP_NAME];

	return devlink_trap_item_lookup(devlink, nla_data(attr));
}

static int
devlink_trap_action_get_from_info(struct genl_info *info,
				  enum devlink_trap_action *p_trap_action)
{
	u8 val;

	val = nla_get_u8(info->attrs[DEVLINK_ATTR_TRAP_ACTION]);
	switch (val) {
	case DEVLINK_TRAP_ACTION_DROP:
	case DEVLINK_TRAP_ACTION_TRAP:
	case DEVLINK_TRAP_ACTION_MIRROR:
		*p_trap_action = val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int devlink_trap_metadata_put(struct sk_buff *msg,
				     const struct devlink_trap *trap)
{
	struct nlattr *attr;

	attr = nla_nest_start(msg, DEVLINK_ATTR_TRAP_METADATA);
	if (!attr)
		return -EMSGSIZE;

	if ((trap->metadata_cap & DEVLINK_TRAP_METADATA_TYPE_F_IN_PORT) &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_METADATA_TYPE_IN_PORT))
		goto nla_put_failure;
	if ((trap->metadata_cap & DEVLINK_TRAP_METADATA_TYPE_F_FA_COOKIE) &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_METADATA_TYPE_FA_COOKIE))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static void devlink_trap_stats_read(struct devlink_stats __percpu *trap_stats,
				    struct devlink_stats *stats)
{
	int i;

	memset(stats, 0, sizeof(*stats));
	for_each_possible_cpu(i) {
		struct devlink_stats *cpu_stats;
		u64 rx_packets, rx_bytes;
		unsigned int start;

		cpu_stats = per_cpu_ptr(trap_stats, i);
		do {
			start = u64_stats_fetch_begin(&cpu_stats->syncp);
			rx_packets = u64_stats_read(&cpu_stats->rx_packets);
			rx_bytes = u64_stats_read(&cpu_stats->rx_bytes);
		} while (u64_stats_fetch_retry(&cpu_stats->syncp, start));

		u64_stats_add(&stats->rx_packets, rx_packets);
		u64_stats_add(&stats->rx_bytes, rx_bytes);
	}
}

static int
devlink_trap_group_stats_put(struct sk_buff *msg,
			     struct devlink_stats __percpu *trap_stats)
{
	struct devlink_stats stats;
	struct nlattr *attr;

	devlink_trap_stats_read(trap_stats, &stats);

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_PACKETS,
			      u64_stats_read(&stats.rx_packets),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_BYTES,
			      u64_stats_read(&stats.rx_bytes),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int devlink_trap_stats_put(struct sk_buff *msg, struct devlink *devlink,
				  const struct devlink_trap_item *trap_item)
{
	struct devlink_stats stats;
	struct nlattr *attr;
	u64 drops = 0;
	int err;

	if (devlink->ops->trap_drop_counter_get) {
		err = devlink->ops->trap_drop_counter_get(devlink,
							  trap_item->trap,
							  &drops);
		if (err)
			return err;
	}

	devlink_trap_stats_read(trap_item->stats, &stats);

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (devlink->ops->trap_drop_counter_get &&
	    nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_DROPPED, drops,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_PACKETS,
			      u64_stats_read(&stats.rx_packets),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_BYTES,
			      u64_stats_read(&stats.rx_bytes),
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int devlink_nl_trap_fill(struct sk_buff *msg, struct devlink *devlink,
				const struct devlink_trap_item *trap_item,
				enum devlink_command cmd, u32 portid, u32 seq,
				int flags)
{
	struct devlink_trap_group_item *group_item = trap_item->group_item;
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_string(msg, DEVLINK_ATTR_TRAP_GROUP_NAME,
			   group_item->group->name))
		goto nla_put_failure;

	if (nla_put_string(msg, DEVLINK_ATTR_TRAP_NAME, trap_item->trap->name))
		goto nla_put_failure;

	if (nla_put_u8(msg, DEVLINK_ATTR_TRAP_TYPE, trap_item->trap->type))
		goto nla_put_failure;

	if (trap_item->trap->generic &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_GENERIC))
		goto nla_put_failure;

	if (nla_put_u8(msg, DEVLINK_ATTR_TRAP_ACTION, trap_item->action))
		goto nla_put_failure;

	err = devlink_trap_metadata_put(msg, trap_item->trap);
	if (err)
		goto nla_put_failure;

	err = devlink_trap_stats_put(msg, devlink, trap_item);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

int devlink_nl_trap_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_item *trap_item;
	struct sk_buff *msg;
	int err;

	if (list_empty(&devlink->trap_list))
		return -EOPNOTSUPP;

	trap_item = devlink_trap_item_get_from_info(devlink, info);
	if (!trap_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap");
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_trap_fill(msg, devlink, trap_item,
				   DEVLINK_CMD_TRAP_NEW, info->snd_portid,
				   info->snd_seq, 0);
	if (err)
		goto err_trap_fill;

	return genlmsg_reply(msg, info);

err_trap_fill:
	nlmsg_free(msg);
	return err;
}

static int devlink_nl_trap_get_dump_one(struct sk_buff *msg,
					struct devlink *devlink,
					struct netlink_callback *cb, int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_trap_item *trap_item;
	int idx = 0;
	int err = 0;

	list_for_each_entry(trap_item, &devlink->trap_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_trap_fill(msg, devlink, trap_item,
					   DEVLINK_CMD_TRAP_NEW,
					   NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq, flags);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

int devlink_nl_trap_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_trap_get_dump_one);
}

static int __devlink_trap_action_set(struct devlink *devlink,
				     struct devlink_trap_item *trap_item,
				     enum devlink_trap_action trap_action,
				     struct netlink_ext_ack *extack)
{
	int err;

	if (trap_item->action != trap_action &&
	    trap_item->trap->type != DEVLINK_TRAP_TYPE_DROP) {
		NL_SET_ERR_MSG(extack, "Cannot change action of non-drop traps. Skipping");
		return 0;
	}

	err = devlink->ops->trap_action_set(devlink, trap_item->trap,
					    trap_action, extack);
	if (err)
		return err;

	trap_item->action = trap_action;

	return 0;
}

static int devlink_trap_action_set(struct devlink *devlink,
				   struct devlink_trap_item *trap_item,
				   struct genl_info *info)
{
	enum devlink_trap_action trap_action;
	int err;

	if (!info->attrs[DEVLINK_ATTR_TRAP_ACTION])
		return 0;

	err = devlink_trap_action_get_from_info(info, &trap_action);
	if (err) {
		NL_SET_ERR_MSG(info->extack, "Invalid trap action");
		return -EINVAL;
	}

	return __devlink_trap_action_set(devlink, trap_item, trap_action,
					 info->extack);
}

int devlink_nl_cmd_trap_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_item *trap_item;

	if (list_empty(&devlink->trap_list))
		return -EOPNOTSUPP;

	trap_item = devlink_trap_item_get_from_info(devlink, info);
	if (!trap_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap");
		return -ENOENT;
	}

	return devlink_trap_action_set(devlink, trap_item, info);
}

static struct devlink_trap_group_item *
devlink_trap_group_item_lookup(struct devlink *devlink, const char *name)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry(group_item, &devlink->trap_group_list, list) {
		if (!strcmp(group_item->group->name, name))
			return group_item;
	}

	return NULL;
}

static struct devlink_trap_group_item *
devlink_trap_group_item_lookup_by_id(struct devlink *devlink, u16 id)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry(group_item, &devlink->trap_group_list, list) {
		if (group_item->group->id == id)
			return group_item;
	}

	return NULL;
}

static struct devlink_trap_group_item *
devlink_trap_group_item_get_from_info(struct devlink *devlink,
				      struct genl_info *info)
{
	char *name;

	if (!info->attrs[DEVLINK_ATTR_TRAP_GROUP_NAME])
		return NULL;
	name = nla_data(info->attrs[DEVLINK_ATTR_TRAP_GROUP_NAME]);

	return devlink_trap_group_item_lookup(devlink, name);
}

static int
devlink_nl_trap_group_fill(struct sk_buff *msg, struct devlink *devlink,
			   const struct devlink_trap_group_item *group_item,
			   enum devlink_command cmd, u32 portid, u32 seq,
			   int flags)
{
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_string(msg, DEVLINK_ATTR_TRAP_GROUP_NAME,
			   group_item->group->name))
		goto nla_put_failure;

	if (group_item->group->generic &&
	    nla_put_flag(msg, DEVLINK_ATTR_TRAP_GENERIC))
		goto nla_put_failure;

	if (group_item->policer_item &&
	    nla_put_u32(msg, DEVLINK_ATTR_TRAP_POLICER_ID,
			group_item->policer_item->policer->id))
		goto nla_put_failure;

	err = devlink_trap_group_stats_put(msg, group_item->stats);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

int devlink_nl_trap_group_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_group_item *group_item;
	struct sk_buff *msg;
	int err;

	if (list_empty(&devlink->trap_group_list))
		return -EOPNOTSUPP;

	group_item = devlink_trap_group_item_get_from_info(devlink, info);
	if (!group_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap group");
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_trap_group_fill(msg, devlink, group_item,
					 DEVLINK_CMD_TRAP_GROUP_NEW,
					 info->snd_portid, info->snd_seq, 0);
	if (err)
		goto err_trap_group_fill;

	return genlmsg_reply(msg, info);

err_trap_group_fill:
	nlmsg_free(msg);
	return err;
}

static int devlink_nl_trap_group_get_dump_one(struct sk_buff *msg,
					      struct devlink *devlink,
					      struct netlink_callback *cb,
					      int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_trap_group_item *group_item;
	int idx = 0;
	int err = 0;

	list_for_each_entry(group_item, &devlink->trap_group_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_trap_group_fill(msg, devlink, group_item,
						 DEVLINK_CMD_TRAP_GROUP_NEW,
						 NETLINK_CB(cb->skb).portid,
						 cb->nlh->nlmsg_seq, flags);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

int devlink_nl_trap_group_get_dumpit(struct sk_buff *skb,
				     struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_trap_group_get_dump_one);
}

static int
__devlink_trap_group_action_set(struct devlink *devlink,
				struct devlink_trap_group_item *group_item,
				enum devlink_trap_action trap_action,
				struct netlink_ext_ack *extack)
{
	const char *group_name = group_item->group->name;
	struct devlink_trap_item *trap_item;
	int err;

	if (devlink->ops->trap_group_action_set) {
		err = devlink->ops->trap_group_action_set(devlink, group_item->group,
							  trap_action, extack);
		if (err)
			return err;

		list_for_each_entry(trap_item, &devlink->trap_list, list) {
			if (strcmp(trap_item->group_item->group->name, group_name))
				continue;
			if (trap_item->action != trap_action &&
			    trap_item->trap->type != DEVLINK_TRAP_TYPE_DROP)
				continue;
			trap_item->action = trap_action;
		}

		return 0;
	}

	list_for_each_entry(trap_item, &devlink->trap_list, list) {
		if (strcmp(trap_item->group_item->group->name, group_name))
			continue;
		err = __devlink_trap_action_set(devlink, trap_item,
						trap_action, extack);
		if (err)
			return err;
	}

	return 0;
}

static int
devlink_trap_group_action_set(struct devlink *devlink,
			      struct devlink_trap_group_item *group_item,
			      struct genl_info *info, bool *p_modified)
{
	enum devlink_trap_action trap_action;
	int err;

	if (!info->attrs[DEVLINK_ATTR_TRAP_ACTION])
		return 0;

	err = devlink_trap_action_get_from_info(info, &trap_action);
	if (err) {
		NL_SET_ERR_MSG(info->extack, "Invalid trap action");
		return -EINVAL;
	}

	err = __devlink_trap_group_action_set(devlink, group_item, trap_action,
					      info->extack);
	if (err)
		return err;

	*p_modified = true;

	return 0;
}

static int devlink_trap_group_set(struct devlink *devlink,
				  struct devlink_trap_group_item *group_item,
				  struct genl_info *info)
{
	struct devlink_trap_policer_item *policer_item;
	struct netlink_ext_ack *extack = info->extack;
	const struct devlink_trap_policer *policer;
	struct nlattr **attrs = info->attrs;
	u32 policer_id;
	int err;

	if (!attrs[DEVLINK_ATTR_TRAP_POLICER_ID])
		return 0;

	if (!devlink->ops->trap_group_set)
		return -EOPNOTSUPP;

	policer_id = nla_get_u32(attrs[DEVLINK_ATTR_TRAP_POLICER_ID]);
	policer_item = devlink_trap_policer_item_lookup(devlink, policer_id);
	if (policer_id && !policer_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap policer");
		return -ENOENT;
	}
	policer = policer_item ? policer_item->policer : NULL;

	err = devlink->ops->trap_group_set(devlink, group_item->group, policer,
					   extack);
	if (err)
		return err;

	group_item->policer_item = policer_item;

	return 0;
}

int devlink_nl_cmd_trap_group_set_doit(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_trap_group_item *group_item;
	bool modified = false;
	int err;

	if (list_empty(&devlink->trap_group_list))
		return -EOPNOTSUPP;

	group_item = devlink_trap_group_item_get_from_info(devlink, info);
	if (!group_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap group");
		return -ENOENT;
	}

	err = devlink_trap_group_action_set(devlink, group_item, info,
					    &modified);
	if (err)
		return err;

	err = devlink_trap_group_set(devlink, group_item, info);
	if (err)
		goto err_trap_group_set;

	return 0;

err_trap_group_set:
	if (modified)
		NL_SET_ERR_MSG(extack, "Trap group set failed, but some changes were committed already");
	return err;
}

static struct devlink_trap_policer_item *
devlink_trap_policer_item_get_from_info(struct devlink *devlink,
					struct genl_info *info)
{
	u32 id;

	if (!info->attrs[DEVLINK_ATTR_TRAP_POLICER_ID])
		return NULL;
	id = nla_get_u32(info->attrs[DEVLINK_ATTR_TRAP_POLICER_ID]);

	return devlink_trap_policer_item_lookup(devlink, id);
}

static int
devlink_trap_policer_stats_put(struct sk_buff *msg, struct devlink *devlink,
			       const struct devlink_trap_policer *policer)
{
	struct nlattr *attr;
	u64 drops;
	int err;

	if (!devlink->ops->trap_policer_counter_get)
		return 0;

	err = devlink->ops->trap_policer_counter_get(devlink, policer, &drops);
	if (err)
		return err;

	attr = nla_nest_start(msg, DEVLINK_ATTR_STATS);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_STATS_RX_DROPPED, drops,
			      DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	return 0;

nla_put_failure:
	nla_nest_cancel(msg, attr);
	return -EMSGSIZE;
}

static int
devlink_nl_trap_policer_fill(struct sk_buff *msg, struct devlink *devlink,
			     const struct devlink_trap_policer_item *policer_item,
			     enum devlink_command cmd, u32 portid, u32 seq,
			     int flags)
{
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	if (devlink_nl_put_handle(msg, devlink))
		goto nla_put_failure;

	if (nla_put_u32(msg, DEVLINK_ATTR_TRAP_POLICER_ID,
			policer_item->policer->id))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_TRAP_POLICER_RATE,
			      policer_item->rate, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, DEVLINK_ATTR_TRAP_POLICER_BURST,
			      policer_item->burst, DEVLINK_ATTR_PAD))
		goto nla_put_failure;

	err = devlink_trap_policer_stats_put(msg, devlink,
					     policer_item->policer);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

int devlink_nl_trap_policer_get_doit(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct devlink_trap_policer_item *policer_item;
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];
	struct sk_buff *msg;
	int err;

	if (list_empty(&devlink->trap_policer_list))
		return -EOPNOTSUPP;

	policer_item = devlink_trap_policer_item_get_from_info(devlink, info);
	if (!policer_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap policer");
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_trap_policer_fill(msg, devlink, policer_item,
					   DEVLINK_CMD_TRAP_POLICER_NEW,
					   info->snd_portid, info->snd_seq, 0);
	if (err)
		goto err_trap_policer_fill;

	return genlmsg_reply(msg, info);

err_trap_policer_fill:
	nlmsg_free(msg);
	return err;
}

static int devlink_nl_trap_policer_get_dump_one(struct sk_buff *msg,
						struct devlink *devlink,
						struct netlink_callback *cb,
						int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_trap_policer_item *policer_item;
	int idx = 0;
	int err = 0;

	list_for_each_entry(policer_item, &devlink->trap_policer_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_trap_policer_fill(msg, devlink, policer_item,
						   DEVLINK_CMD_TRAP_POLICER_NEW,
						   NETLINK_CB(cb->skb).portid,
						   cb->nlh->nlmsg_seq, flags);
		if (err) {
			state->idx = idx;
			break;
		}
		idx++;
	}

	return err;
}

int devlink_nl_trap_policer_get_dumpit(struct sk_buff *skb,
				       struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_trap_policer_get_dump_one);
}

static int
devlink_trap_policer_set(struct devlink *devlink,
			 struct devlink_trap_policer_item *policer_item,
			 struct genl_info *info)
{
	struct netlink_ext_ack *extack = info->extack;
	struct nlattr **attrs = info->attrs;
	u64 rate, burst;
	int err;

	rate = policer_item->rate;
	burst = policer_item->burst;

	if (attrs[DEVLINK_ATTR_TRAP_POLICER_RATE])
		rate = nla_get_u64(attrs[DEVLINK_ATTR_TRAP_POLICER_RATE]);

	if (attrs[DEVLINK_ATTR_TRAP_POLICER_BURST])
		burst = nla_get_u64(attrs[DEVLINK_ATTR_TRAP_POLICER_BURST]);

	if (rate < policer_item->policer->min_rate) {
		NL_SET_ERR_MSG(extack, "Policer rate lower than limit");
		return -EINVAL;
	}

	if (rate > policer_item->policer->max_rate) {
		NL_SET_ERR_MSG(extack, "Policer rate higher than limit");
		return -EINVAL;
	}

	if (burst < policer_item->policer->min_burst) {
		NL_SET_ERR_MSG(extack, "Policer burst size lower than limit");
		return -EINVAL;
	}

	if (burst > policer_item->policer->max_burst) {
		NL_SET_ERR_MSG(extack, "Policer burst size higher than limit");
		return -EINVAL;
	}

	err = devlink->ops->trap_policer_set(devlink, policer_item->policer,
					     rate, burst, info->extack);
	if (err)
		return err;

	policer_item->rate = rate;
	policer_item->burst = burst;

	return 0;
}

int devlink_nl_cmd_trap_policer_set_doit(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct devlink_trap_policer_item *policer_item;
	struct netlink_ext_ack *extack = info->extack;
	struct devlink *devlink = info->user_ptr[0];

	if (list_empty(&devlink->trap_policer_list))
		return -EOPNOTSUPP;

	if (!devlink->ops->trap_policer_set)
		return -EOPNOTSUPP;

	policer_item = devlink_trap_policer_item_get_from_info(devlink, info);
	if (!policer_item) {
		NL_SET_ERR_MSG(extack, "Device did not register this trap policer");
		return -ENOENT;
	}

	return devlink_trap_policer_set(devlink, policer_item, info);
}

#define DEVLINK_TRAP(_id, _type)					      \
	{								      \
		.type = DEVLINK_TRAP_TYPE_##_type,			      \
		.id = DEVLINK_TRAP_GENERIC_ID_##_id,			      \
		.name = DEVLINK_TRAP_GENERIC_NAME_##_id,		      \
	}

static const struct devlink_trap devlink_trap_generic[] = {
	DEVLINK_TRAP(SMAC_MC, DROP),
	DEVLINK_TRAP(VLAN_TAG_MISMATCH, DROP),
	DEVLINK_TRAP(INGRESS_VLAN_FILTER, DROP),
	DEVLINK_TRAP(INGRESS_STP_FILTER, DROP),
	DEVLINK_TRAP(EMPTY_TX_LIST, DROP),
	DEVLINK_TRAP(PORT_LOOPBACK_FILTER, DROP),
	DEVLINK_TRAP(BLACKHOLE_ROUTE, DROP),
	DEVLINK_TRAP(TTL_ERROR, EXCEPTION),
	DEVLINK_TRAP(TAIL_DROP, DROP),
	DEVLINK_TRAP(NON_IP_PACKET, DROP),
	DEVLINK_TRAP(UC_DIP_MC_DMAC, DROP),
	DEVLINK_TRAP(DIP_LB, DROP),
	DEVLINK_TRAP(SIP_MC, DROP),
	DEVLINK_TRAP(SIP_LB, DROP),
	DEVLINK_TRAP(CORRUPTED_IP_HDR, DROP),
	DEVLINK_TRAP(IPV4_SIP_BC, DROP),
	DEVLINK_TRAP(IPV6_MC_DIP_RESERVED_SCOPE, DROP),
	DEVLINK_TRAP(IPV6_MC_DIP_INTERFACE_LOCAL_SCOPE, DROP),
	DEVLINK_TRAP(MTU_ERROR, EXCEPTION),
	DEVLINK_TRAP(UNRESOLVED_NEIGH, EXCEPTION),
	DEVLINK_TRAP(RPF, EXCEPTION),
	DEVLINK_TRAP(REJECT_ROUTE, EXCEPTION),
	DEVLINK_TRAP(IPV4_LPM_UNICAST_MISS, EXCEPTION),
	DEVLINK_TRAP(IPV6_LPM_UNICAST_MISS, EXCEPTION),
	DEVLINK_TRAP(NON_ROUTABLE, DROP),
	DEVLINK_TRAP(DECAP_ERROR, EXCEPTION),
	DEVLINK_TRAP(OVERLAY_SMAC_MC, DROP),
	DEVLINK_TRAP(INGRESS_FLOW_ACTION_DROP, DROP),
	DEVLINK_TRAP(EGRESS_FLOW_ACTION_DROP, DROP),
	DEVLINK_TRAP(STP, CONTROL),
	DEVLINK_TRAP(LACP, CONTROL),
	DEVLINK_TRAP(LLDP, CONTROL),
	DEVLINK_TRAP(IGMP_QUERY, CONTROL),
	DEVLINK_TRAP(IGMP_V1_REPORT, CONTROL),
	DEVLINK_TRAP(IGMP_V2_REPORT, CONTROL),
	DEVLINK_TRAP(IGMP_V3_REPORT, CONTROL),
	DEVLINK_TRAP(IGMP_V2_LEAVE, CONTROL),
	DEVLINK_TRAP(MLD_QUERY, CONTROL),
	DEVLINK_TRAP(MLD_V1_REPORT, CONTROL),
	DEVLINK_TRAP(MLD_V2_REPORT, CONTROL),
	DEVLINK_TRAP(MLD_V1_DONE, CONTROL),
	DEVLINK_TRAP(IPV4_DHCP, CONTROL),
	DEVLINK_TRAP(IPV6_DHCP, CONTROL),
	DEVLINK_TRAP(ARP_REQUEST, CONTROL),
	DEVLINK_TRAP(ARP_RESPONSE, CONTROL),
	DEVLINK_TRAP(ARP_OVERLAY, CONTROL),
	DEVLINK_TRAP(IPV6_NEIGH_SOLICIT, CONTROL),
	DEVLINK_TRAP(IPV6_NEIGH_ADVERT, CONTROL),
	DEVLINK_TRAP(IPV4_BFD, CONTROL),
	DEVLINK_TRAP(IPV6_BFD, CONTROL),
	DEVLINK_TRAP(IPV4_OSPF, CONTROL),
	DEVLINK_TRAP(IPV6_OSPF, CONTROL),
	DEVLINK_TRAP(IPV4_BGP, CONTROL),
	DEVLINK_TRAP(IPV6_BGP, CONTROL),
	DEVLINK_TRAP(IPV4_VRRP, CONTROL),
	DEVLINK_TRAP(IPV6_VRRP, CONTROL),
	DEVLINK_TRAP(IPV4_PIM, CONTROL),
	DEVLINK_TRAP(IPV6_PIM, CONTROL),
	DEVLINK_TRAP(UC_LB, CONTROL),
	DEVLINK_TRAP(LOCAL_ROUTE, CONTROL),
	DEVLINK_TRAP(EXTERNAL_ROUTE, CONTROL),
	DEVLINK_TRAP(IPV6_UC_DIP_LINK_LOCAL_SCOPE, CONTROL),
	DEVLINK_TRAP(IPV6_DIP_ALL_NODES, CONTROL),
	DEVLINK_TRAP(IPV6_DIP_ALL_ROUTERS, CONTROL),
	DEVLINK_TRAP(IPV6_ROUTER_SOLICIT, CONTROL),
	DEVLINK_TRAP(IPV6_ROUTER_ADVERT, CONTROL),
	DEVLINK_TRAP(IPV6_REDIRECT, CONTROL),
	DEVLINK_TRAP(IPV4_ROUTER_ALERT, CONTROL),
	DEVLINK_TRAP(IPV6_ROUTER_ALERT, CONTROL),
	DEVLINK_TRAP(PTP_EVENT, CONTROL),
	DEVLINK_TRAP(PTP_GENERAL, CONTROL),
	DEVLINK_TRAP(FLOW_ACTION_SAMPLE, CONTROL),
	DEVLINK_TRAP(FLOW_ACTION_TRAP, CONTROL),
	DEVLINK_TRAP(EARLY_DROP, DROP),
	DEVLINK_TRAP(VXLAN_PARSING, DROP),
	DEVLINK_TRAP(LLC_SNAP_PARSING, DROP),
	DEVLINK_TRAP(VLAN_PARSING, DROP),
	DEVLINK_TRAP(PPPOE_PPP_PARSING, DROP),
	DEVLINK_TRAP(MPLS_PARSING, DROP),
	DEVLINK_TRAP(ARP_PARSING, DROP),
	DEVLINK_TRAP(IP_1_PARSING, DROP),
	DEVLINK_TRAP(IP_N_PARSING, DROP),
	DEVLINK_TRAP(GRE_PARSING, DROP),
	DEVLINK_TRAP(UDP_PARSING, DROP),
	DEVLINK_TRAP(TCP_PARSING, DROP),
	DEVLINK_TRAP(IPSEC_PARSING, DROP),
	DEVLINK_TRAP(SCTP_PARSING, DROP),
	DEVLINK_TRAP(DCCP_PARSING, DROP),
	DEVLINK_TRAP(GTP_PARSING, DROP),
	DEVLINK_TRAP(ESP_PARSING, DROP),
	DEVLINK_TRAP(BLACKHOLE_NEXTHOP, DROP),
	DEVLINK_TRAP(DMAC_FILTER, DROP),
	DEVLINK_TRAP(EAPOL, CONTROL),
	DEVLINK_TRAP(LOCKED_PORT, DROP),
};

#define DEVLINK_TRAP_GROUP(_id)						      \
	{								      \
		.id = DEVLINK_TRAP_GROUP_GENERIC_ID_##_id,		      \
		.name = DEVLINK_TRAP_GROUP_GENERIC_NAME_##_id,		      \
	}

static const struct devlink_trap_group devlink_trap_group_generic[] = {
	DEVLINK_TRAP_GROUP(L2_DROPS),
	DEVLINK_TRAP_GROUP(L3_DROPS),
	DEVLINK_TRAP_GROUP(L3_EXCEPTIONS),
	DEVLINK_TRAP_GROUP(BUFFER_DROPS),
	DEVLINK_TRAP_GROUP(TUNNEL_DROPS),
	DEVLINK_TRAP_GROUP(ACL_DROPS),
	DEVLINK_TRAP_GROUP(STP),
	DEVLINK_TRAP_GROUP(LACP),
	DEVLINK_TRAP_GROUP(LLDP),
	DEVLINK_TRAP_GROUP(MC_SNOOPING),
	DEVLINK_TRAP_GROUP(DHCP),
	DEVLINK_TRAP_GROUP(NEIGH_DISCOVERY),
	DEVLINK_TRAP_GROUP(BFD),
	DEVLINK_TRAP_GROUP(OSPF),
	DEVLINK_TRAP_GROUP(BGP),
	DEVLINK_TRAP_GROUP(VRRP),
	DEVLINK_TRAP_GROUP(PIM),
	DEVLINK_TRAP_GROUP(UC_LB),
	DEVLINK_TRAP_GROUP(LOCAL_DELIVERY),
	DEVLINK_TRAP_GROUP(EXTERNAL_DELIVERY),
	DEVLINK_TRAP_GROUP(IPV6),
	DEVLINK_TRAP_GROUP(PTP_EVENT),
	DEVLINK_TRAP_GROUP(PTP_GENERAL),
	DEVLINK_TRAP_GROUP(ACL_SAMPLE),
	DEVLINK_TRAP_GROUP(ACL_TRAP),
	DEVLINK_TRAP_GROUP(PARSER_ERROR_DROPS),
	DEVLINK_TRAP_GROUP(EAPOL),
};

static int devlink_trap_generic_verify(const struct devlink_trap *trap)
{
	if (trap->id > DEVLINK_TRAP_GENERIC_ID_MAX)
		return -EINVAL;

	if (strcmp(trap->name, devlink_trap_generic[trap->id].name))
		return -EINVAL;

	if (trap->type != devlink_trap_generic[trap->id].type)
		return -EINVAL;

	return 0;
}

static int devlink_trap_driver_verify(const struct devlink_trap *trap)
{
	int i;

	if (trap->id <= DEVLINK_TRAP_GENERIC_ID_MAX)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(devlink_trap_generic); i++) {
		if (!strcmp(trap->name, devlink_trap_generic[i].name))
			return -EEXIST;
	}

	return 0;
}

static int devlink_trap_verify(const struct devlink_trap *trap)
{
	if (!trap || !trap->name)
		return -EINVAL;

	if (trap->generic)
		return devlink_trap_generic_verify(trap);
	else
		return devlink_trap_driver_verify(trap);
}

static int
devlink_trap_group_generic_verify(const struct devlink_trap_group *group)
{
	if (group->id > DEVLINK_TRAP_GROUP_GENERIC_ID_MAX)
		return -EINVAL;

	if (strcmp(group->name, devlink_trap_group_generic[group->id].name))
		return -EINVAL;

	return 0;
}

static int
devlink_trap_group_driver_verify(const struct devlink_trap_group *group)
{
	int i;

	if (group->id <= DEVLINK_TRAP_GROUP_GENERIC_ID_MAX)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(devlink_trap_group_generic); i++) {
		if (!strcmp(group->name, devlink_trap_group_generic[i].name))
			return -EEXIST;
	}

	return 0;
}

static int devlink_trap_group_verify(const struct devlink_trap_group *group)
{
	if (group->generic)
		return devlink_trap_group_generic_verify(group);
	else
		return devlink_trap_group_driver_verify(group);
}

static void
devlink_trap_group_notify(struct devlink *devlink,
			  const struct devlink_trap_group_item *group_item,
			  enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON_ONCE(cmd != DEVLINK_CMD_TRAP_GROUP_NEW &&
		     cmd != DEVLINK_CMD_TRAP_GROUP_DEL);
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_trap_group_fill(msg, devlink, group_item, cmd, 0, 0,
					 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

void devlink_trap_groups_notify_register(struct devlink *devlink)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry(group_item, &devlink->trap_group_list, list)
		devlink_trap_group_notify(devlink, group_item,
					  DEVLINK_CMD_TRAP_GROUP_NEW);
}

void devlink_trap_groups_notify_unregister(struct devlink *devlink)
{
	struct devlink_trap_group_item *group_item;

	list_for_each_entry_reverse(group_item, &devlink->trap_group_list, list)
		devlink_trap_group_notify(devlink, group_item,
					  DEVLINK_CMD_TRAP_GROUP_DEL);
}

static int
devlink_trap_item_group_link(struct devlink *devlink,
			     struct devlink_trap_item *trap_item)
{
	u16 group_id = trap_item->trap->init_group_id;
	struct devlink_trap_group_item *group_item;

	group_item = devlink_trap_group_item_lookup_by_id(devlink, group_id);
	if (WARN_ON_ONCE(!group_item))
		return -EINVAL;

	trap_item->group_item = group_item;

	return 0;
}

static void devlink_trap_notify(struct devlink *devlink,
				const struct devlink_trap_item *trap_item,
				enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON_ONCE(cmd != DEVLINK_CMD_TRAP_NEW &&
		     cmd != DEVLINK_CMD_TRAP_DEL);
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_trap_fill(msg, devlink, trap_item, cmd, 0, 0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

void devlink_traps_notify_register(struct devlink *devlink)
{
	struct devlink_trap_item *trap_item;

	list_for_each_entry(trap_item, &devlink->trap_list, list)
		devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_NEW);
}

void devlink_traps_notify_unregister(struct devlink *devlink)
{
	struct devlink_trap_item *trap_item;

	list_for_each_entry_reverse(trap_item, &devlink->trap_list, list)
		devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_DEL);
}

static int
devlink_trap_register(struct devlink *devlink,
		      const struct devlink_trap *trap, void *priv)
{
	struct devlink_trap_item *trap_item;
	int err;

	if (devlink_trap_item_lookup(devlink, trap->name))
		return -EEXIST;

	trap_item = kzalloc(sizeof(*trap_item), GFP_KERNEL);
	if (!trap_item)
		return -ENOMEM;

	trap_item->stats = netdev_alloc_pcpu_stats(struct devlink_stats);
	if (!trap_item->stats) {
		err = -ENOMEM;
		goto err_stats_alloc;
	}

	trap_item->trap = trap;
	trap_item->action = trap->init_action;
	trap_item->priv = priv;

	err = devlink_trap_item_group_link(devlink, trap_item);
	if (err)
		goto err_group_link;

	err = devlink->ops->trap_init(devlink, trap, trap_item);
	if (err)
		goto err_trap_init;

	list_add_tail(&trap_item->list, &devlink->trap_list);
	devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_NEW);

	return 0;

err_trap_init:
err_group_link:
	free_percpu(trap_item->stats);
err_stats_alloc:
	kfree(trap_item);
	return err;
}

static void devlink_trap_unregister(struct devlink *devlink,
				    const struct devlink_trap *trap)
{
	struct devlink_trap_item *trap_item;

	trap_item = devlink_trap_item_lookup(devlink, trap->name);
	if (WARN_ON_ONCE(!trap_item))
		return;

	devlink_trap_notify(devlink, trap_item, DEVLINK_CMD_TRAP_DEL);
	list_del(&trap_item->list);
	if (devlink->ops->trap_fini)
		devlink->ops->trap_fini(devlink, trap, trap_item);
	free_percpu(trap_item->stats);
	kfree(trap_item);
}

static void devlink_trap_disable(struct devlink *devlink,
				 const struct devlink_trap *trap)
{
	struct devlink_trap_item *trap_item;

	trap_item = devlink_trap_item_lookup(devlink, trap->name);
	if (WARN_ON_ONCE(!trap_item))
		return;

	devlink->ops->trap_action_set(devlink, trap, DEVLINK_TRAP_ACTION_DROP,
				      NULL);
	trap_item->action = DEVLINK_TRAP_ACTION_DROP;
}

/**
 * devl_traps_register - Register packet traps with devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 * @priv: Driver private information.
 *
 * Return: Non-zero value on failure.
 */
int devl_traps_register(struct devlink *devlink,
			const struct devlink_trap *traps,
			size_t traps_count, void *priv)
{
	int i, err;

	if (!devlink->ops->trap_init || !devlink->ops->trap_action_set)
		return -EINVAL;

	devl_assert_locked(devlink);
	for (i = 0; i < traps_count; i++) {
		const struct devlink_trap *trap = &traps[i];

		err = devlink_trap_verify(trap);
		if (err)
			goto err_trap_verify;

		err = devlink_trap_register(devlink, trap, priv);
		if (err)
			goto err_trap_register;
	}

	return 0;

err_trap_register:
err_trap_verify:
	for (i--; i >= 0; i--)
		devlink_trap_unregister(devlink, &traps[i]);
	return err;
}
EXPORT_SYMBOL_GPL(devl_traps_register);

/**
 * devlink_traps_register - Register packet traps with devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 * @priv: Driver private information.
 *
 * Context: Takes and release devlink->lock <mutex>.
 *
 * Return: Non-zero value on failure.
 */
int devlink_traps_register(struct devlink *devlink,
			   const struct devlink_trap *traps,
			   size_t traps_count, void *priv)
{
	int err;

	devl_lock(devlink);
	err = devl_traps_register(devlink, traps, traps_count, priv);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_traps_register);

/**
 * devl_traps_unregister - Unregister packet traps from devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 */
void devl_traps_unregister(struct devlink *devlink,
			   const struct devlink_trap *traps,
			   size_t traps_count)
{
	int i;

	devl_assert_locked(devlink);
	/* Make sure we do not have any packets in-flight while unregistering
	 * traps by disabling all of them and waiting for a grace period.
	 */
	for (i = traps_count - 1; i >= 0; i--)
		devlink_trap_disable(devlink, &traps[i]);
	synchronize_rcu();
	for (i = traps_count - 1; i >= 0; i--)
		devlink_trap_unregister(devlink, &traps[i]);
}
EXPORT_SYMBOL_GPL(devl_traps_unregister);

/**
 * devlink_traps_unregister - Unregister packet traps from devlink.
 * @devlink: devlink.
 * @traps: Packet traps.
 * @traps_count: Count of provided packet traps.
 *
 * Context: Takes and release devlink->lock <mutex>.
 */
void devlink_traps_unregister(struct devlink *devlink,
			      const struct devlink_trap *traps,
			      size_t traps_count)
{
	devl_lock(devlink);
	devl_traps_unregister(devlink, traps, traps_count);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_traps_unregister);

static void
devlink_trap_stats_update(struct devlink_stats __percpu *trap_stats,
			  size_t skb_len)
{
	struct devlink_stats *stats;

	stats = this_cpu_ptr(trap_stats);
	u64_stats_update_begin(&stats->syncp);
	u64_stats_add(&stats->rx_bytes, skb_len);
	u64_stats_inc(&stats->rx_packets);
	u64_stats_update_end(&stats->syncp);
}

static void
devlink_trap_report_metadata_set(struct devlink_trap_metadata *metadata,
				 const struct devlink_trap_item *trap_item,
				 struct devlink_port *in_devlink_port,
				 const struct flow_action_cookie *fa_cookie)
{
	metadata->trap_name = trap_item->trap->name;
	metadata->trap_group_name = trap_item->group_item->group->name;
	metadata->fa_cookie = fa_cookie;
	metadata->trap_type = trap_item->trap->type;

	spin_lock(&in_devlink_port->type_lock);
	if (in_devlink_port->type == DEVLINK_PORT_TYPE_ETH)
		metadata->input_dev = in_devlink_port->type_eth.netdev;
	spin_unlock(&in_devlink_port->type_lock);
}

/**
 * devlink_trap_report - Report trapped packet to drop monitor.
 * @devlink: devlink.
 * @skb: Trapped packet.
 * @trap_ctx: Trap context.
 * @in_devlink_port: Input devlink port.
 * @fa_cookie: Flow action cookie. Could be NULL.
 */
void devlink_trap_report(struct devlink *devlink, struct sk_buff *skb,
			 void *trap_ctx, struct devlink_port *in_devlink_port,
			 const struct flow_action_cookie *fa_cookie)

{
	struct devlink_trap_item *trap_item = trap_ctx;

	devlink_trap_stats_update(trap_item->stats, skb->len);
	devlink_trap_stats_update(trap_item->group_item->stats, skb->len);

	if (tracepoint_enabled(devlink_trap_report)) {
		struct devlink_trap_metadata metadata = {};

		devlink_trap_report_metadata_set(&metadata, trap_item,
						 in_devlink_port, fa_cookie);
		trace_devlink_trap_report(devlink, skb, &metadata);
	}
}
EXPORT_SYMBOL_GPL(devlink_trap_report);

/**
 * devlink_trap_ctx_priv - Trap context to driver private information.
 * @trap_ctx: Trap context.
 *
 * Return: Driver private information passed during registration.
 */
void *devlink_trap_ctx_priv(void *trap_ctx)
{
	struct devlink_trap_item *trap_item = trap_ctx;

	return trap_item->priv;
}
EXPORT_SYMBOL_GPL(devlink_trap_ctx_priv);

static int
devlink_trap_group_item_policer_link(struct devlink *devlink,
				     struct devlink_trap_group_item *group_item)
{
	u32 policer_id = group_item->group->init_policer_id;
	struct devlink_trap_policer_item *policer_item;

	if (policer_id == 0)
		return 0;

	policer_item = devlink_trap_policer_item_lookup(devlink, policer_id);
	if (WARN_ON_ONCE(!policer_item))
		return -EINVAL;

	group_item->policer_item = policer_item;

	return 0;
}

static int
devlink_trap_group_register(struct devlink *devlink,
			    const struct devlink_trap_group *group)
{
	struct devlink_trap_group_item *group_item;
	int err;

	if (devlink_trap_group_item_lookup(devlink, group->name))
		return -EEXIST;

	group_item = kzalloc(sizeof(*group_item), GFP_KERNEL);
	if (!group_item)
		return -ENOMEM;

	group_item->stats = netdev_alloc_pcpu_stats(struct devlink_stats);
	if (!group_item->stats) {
		err = -ENOMEM;
		goto err_stats_alloc;
	}

	group_item->group = group;

	err = devlink_trap_group_item_policer_link(devlink, group_item);
	if (err)
		goto err_policer_link;

	if (devlink->ops->trap_group_init) {
		err = devlink->ops->trap_group_init(devlink, group);
		if (err)
			goto err_group_init;
	}

	list_add_tail(&group_item->list, &devlink->trap_group_list);
	devlink_trap_group_notify(devlink, group_item,
				  DEVLINK_CMD_TRAP_GROUP_NEW);

	return 0;

err_group_init:
err_policer_link:
	free_percpu(group_item->stats);
err_stats_alloc:
	kfree(group_item);
	return err;
}

static void
devlink_trap_group_unregister(struct devlink *devlink,
			      const struct devlink_trap_group *group)
{
	struct devlink_trap_group_item *group_item;

	group_item = devlink_trap_group_item_lookup(devlink, group->name);
	if (WARN_ON_ONCE(!group_item))
		return;

	devlink_trap_group_notify(devlink, group_item,
				  DEVLINK_CMD_TRAP_GROUP_DEL);
	list_del(&group_item->list);
	free_percpu(group_item->stats);
	kfree(group_item);
}

/**
 * devl_trap_groups_register - Register packet trap groups with devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Return: Non-zero value on failure.
 */
int devl_trap_groups_register(struct devlink *devlink,
			      const struct devlink_trap_group *groups,
			      size_t groups_count)
{
	int i, err;

	devl_assert_locked(devlink);
	for (i = 0; i < groups_count; i++) {
		const struct devlink_trap_group *group = &groups[i];

		err = devlink_trap_group_verify(group);
		if (err)
			goto err_trap_group_verify;

		err = devlink_trap_group_register(devlink, group);
		if (err)
			goto err_trap_group_register;
	}

	return 0;

err_trap_group_register:
err_trap_group_verify:
	for (i--; i >= 0; i--)
		devlink_trap_group_unregister(devlink, &groups[i]);
	return err;
}
EXPORT_SYMBOL_GPL(devl_trap_groups_register);

/**
 * devlink_trap_groups_register - Register packet trap groups with devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Context: Takes and release devlink->lock <mutex>.
 *
 * Return: Non-zero value on failure.
 */
int devlink_trap_groups_register(struct devlink *devlink,
				 const struct devlink_trap_group *groups,
				 size_t groups_count)
{
	int err;

	devl_lock(devlink);
	err = devl_trap_groups_register(devlink, groups, groups_count);
	devl_unlock(devlink);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_trap_groups_register);

/**
 * devl_trap_groups_unregister - Unregister packet trap groups from devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 */
void devl_trap_groups_unregister(struct devlink *devlink,
				 const struct devlink_trap_group *groups,
				 size_t groups_count)
{
	int i;

	devl_assert_locked(devlink);
	for (i = groups_count - 1; i >= 0; i--)
		devlink_trap_group_unregister(devlink, &groups[i]);
}
EXPORT_SYMBOL_GPL(devl_trap_groups_unregister);

/**
 * devlink_trap_groups_unregister - Unregister packet trap groups from devlink.
 * @devlink: devlink.
 * @groups: Packet trap groups.
 * @groups_count: Count of provided packet trap groups.
 *
 * Context: Takes and release devlink->lock <mutex>.
 */
void devlink_trap_groups_unregister(struct devlink *devlink,
				    const struct devlink_trap_group *groups,
				    size_t groups_count)
{
	devl_lock(devlink);
	devl_trap_groups_unregister(devlink, groups, groups_count);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_trap_groups_unregister);

static void
devlink_trap_policer_notify(struct devlink *devlink,
			    const struct devlink_trap_policer_item *policer_item,
			    enum devlink_command cmd)
{
	struct sk_buff *msg;
	int err;

	WARN_ON_ONCE(cmd != DEVLINK_CMD_TRAP_POLICER_NEW &&
		     cmd != DEVLINK_CMD_TRAP_POLICER_DEL);
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	err = devlink_nl_trap_policer_fill(msg, devlink, policer_item, cmd, 0,
					   0, 0);
	if (err) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink),
				msg, 0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

void devlink_trap_policers_notify_register(struct devlink *devlink)
{
	struct devlink_trap_policer_item *policer_item;

	list_for_each_entry(policer_item, &devlink->trap_policer_list, list)
		devlink_trap_policer_notify(devlink, policer_item,
					    DEVLINK_CMD_TRAP_POLICER_NEW);
}

void devlink_trap_policers_notify_unregister(struct devlink *devlink)
{
	struct devlink_trap_policer_item *policer_item;

	list_for_each_entry_reverse(policer_item, &devlink->trap_policer_list,
				    list)
		devlink_trap_policer_notify(devlink, policer_item,
					    DEVLINK_CMD_TRAP_POLICER_DEL);
}

static int
devlink_trap_policer_register(struct devlink *devlink,
			      const struct devlink_trap_policer *policer)
{
	struct devlink_trap_policer_item *policer_item;
	int err;

	if (devlink_trap_policer_item_lookup(devlink, policer->id))
		return -EEXIST;

	policer_item = kzalloc(sizeof(*policer_item), GFP_KERNEL);
	if (!policer_item)
		return -ENOMEM;

	policer_item->policer = policer;
	policer_item->rate = policer->init_rate;
	policer_item->burst = policer->init_burst;

	if (devlink->ops->trap_policer_init) {
		err = devlink->ops->trap_policer_init(devlink, policer);
		if (err)
			goto err_policer_init;
	}

	list_add_tail(&policer_item->list, &devlink->trap_policer_list);
	devlink_trap_policer_notify(devlink, policer_item,
				    DEVLINK_CMD_TRAP_POLICER_NEW);

	return 0;

err_policer_init:
	kfree(policer_item);
	return err;
}

static void
devlink_trap_policer_unregister(struct devlink *devlink,
				const struct devlink_trap_policer *policer)
{
	struct devlink_trap_policer_item *policer_item;

	policer_item = devlink_trap_policer_item_lookup(devlink, policer->id);
	if (WARN_ON_ONCE(!policer_item))
		return;

	devlink_trap_policer_notify(devlink, policer_item,
				    DEVLINK_CMD_TRAP_POLICER_DEL);
	list_del(&policer_item->list);
	if (devlink->ops->trap_policer_fini)
		devlink->ops->trap_policer_fini(devlink, policer);
	kfree(policer_item);
}

/**
 * devl_trap_policers_register - Register packet trap policers with devlink.
 * @devlink: devlink.
 * @policers: Packet trap policers.
 * @policers_count: Count of provided packet trap policers.
 *
 * Return: Non-zero value on failure.
 */
int
devl_trap_policers_register(struct devlink *devlink,
			    const struct devlink_trap_policer *policers,
			    size_t policers_count)
{
	int i, err;

	devl_assert_locked(devlink);
	for (i = 0; i < policers_count; i++) {
		const struct devlink_trap_policer *policer = &policers[i];

		if (WARN_ON(policer->id == 0 ||
			    policer->max_rate < policer->min_rate ||
			    policer->max_burst < policer->min_burst)) {
			err = -EINVAL;
			goto err_trap_policer_verify;
		}

		err = devlink_trap_policer_register(devlink, policer);
		if (err)
			goto err_trap_policer_register;
	}
	return 0;

err_trap_policer_register:
err_trap_policer_verify:
	for (i--; i >= 0; i--)
		devlink_trap_policer_unregister(devlink, &policers[i]);
	return err;
}
EXPORT_SYMBOL_GPL(devl_trap_policers_register);

/**
 * devl_trap_policers_unregister - Unregister packet trap policers from devlink.
 * @devlink: devlink.
 * @policers: Packet trap policers.
 * @policers_count: Count of provided packet trap policers.
 */
void
devl_trap_policers_unregister(struct devlink *devlink,
			      const struct devlink_trap_policer *policers,
			      size_t policers_count)
{
	int i;

	devl_assert_locked(devlink);
	for (i = policers_count - 1; i >= 0; i--)
		devlink_trap_policer_unregister(devlink, &policers[i]);
}
EXPORT_SYMBOL_GPL(devl_trap_policers_unregister);
