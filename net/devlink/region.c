// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Jiri Pirko <jiri@mellanox.com>
 */

#include "devl_internal.h"

struct devlink_region {
	struct devlink *devlink;
	struct devlink_port *port;
	struct list_head list;
	union {
		const struct devlink_region_ops *ops;
		const struct devlink_port_region_ops *port_ops;
	};
	struct mutex snapshot_lock; /* protects snapshot_list,
				     * max_snapshots and cur_snapshots
				     * consistency.
				     */
	struct list_head snapshot_list;
	u32 max_snapshots;
	u32 cur_snapshots;
	u64 size;
};

struct devlink_snapshot {
	struct list_head list;
	struct devlink_region *region;
	u8 *data;
	u32 id;
};

static struct devlink_region *
devlink_region_get_by_name(struct devlink *devlink, const char *region_name)
{
	struct devlink_region *region;

	list_for_each_entry(region, &devlink->region_list, list)
		if (!strcmp(region->ops->name, region_name))
			return region;

	return NULL;
}

static struct devlink_region *
devlink_port_region_get_by_name(struct devlink_port *port,
				const char *region_name)
{
	struct devlink_region *region;

	list_for_each_entry(region, &port->region_list, list)
		if (!strcmp(region->ops->name, region_name))
			return region;

	return NULL;
}

static struct devlink_snapshot *
devlink_region_snapshot_get_by_id(struct devlink_region *region, u32 id)
{
	struct devlink_snapshot *snapshot;

	list_for_each_entry(snapshot, &region->snapshot_list, list)
		if (snapshot->id == id)
			return snapshot;

	return NULL;
}

static int devlink_nl_region_snapshot_id_put(struct sk_buff *msg,
					     struct devlink *devlink,
					     struct devlink_snapshot *snapshot)
{
	struct nlattr *snap_attr;
	int err;

	snap_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_REGION_SNAPSHOT);
	if (!snap_attr)
		return -EINVAL;

	err = nla_put_u32(msg, DEVLINK_ATTR_REGION_SNAPSHOT_ID, snapshot->id);
	if (err)
		goto nla_put_failure;

	nla_nest_end(msg, snap_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, snap_attr);
	return err;
}

static int devlink_nl_region_snapshots_id_put(struct sk_buff *msg,
					      struct devlink *devlink,
					      struct devlink_region *region)
{
	struct devlink_snapshot *snapshot;
	struct nlattr *snapshots_attr;
	int err;

	snapshots_attr = nla_nest_start_noflag(msg,
					       DEVLINK_ATTR_REGION_SNAPSHOTS);
	if (!snapshots_attr)
		return -EINVAL;

	list_for_each_entry(snapshot, &region->snapshot_list, list) {
		err = devlink_nl_region_snapshot_id_put(msg, devlink, snapshot);
		if (err)
			goto nla_put_failure;
	}

	nla_nest_end(msg, snapshots_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, snapshots_attr);
	return err;
}

static int devlink_nl_region_fill(struct sk_buff *msg, struct devlink *devlink,
				  enum devlink_command cmd, u32 portid,
				  u32 seq, int flags,
				  struct devlink_region *region)
{
	void *hdr;
	int err;

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	err = devlink_nl_put_handle(msg, devlink);
	if (err)
		goto nla_put_failure;

	if (region->port) {
		err = nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX,
				  region->port->index);
		if (err)
			goto nla_put_failure;
	}

	err = nla_put_string(msg, DEVLINK_ATTR_REGION_NAME, region->ops->name);
	if (err)
		goto nla_put_failure;

	err = nla_put_u64_64bit(msg, DEVLINK_ATTR_REGION_SIZE,
				region->size,
				DEVLINK_ATTR_PAD);
	if (err)
		goto nla_put_failure;

	err = nla_put_u32(msg, DEVLINK_ATTR_REGION_MAX_SNAPSHOTS,
			  region->max_snapshots);
	if (err)
		goto nla_put_failure;

	err = devlink_nl_region_snapshots_id_put(msg, devlink, region);
	if (err)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return err;
}

static struct sk_buff *
devlink_nl_region_notify_build(struct devlink_region *region,
			       struct devlink_snapshot *snapshot,
			       enum devlink_command cmd, u32 portid, u32 seq)
{
	struct devlink *devlink = region->devlink;
	struct sk_buff *msg;
	void *hdr;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return ERR_PTR(-ENOMEM);

	hdr = genlmsg_put(msg, portid, seq, &devlink_nl_family, 0, cmd);
	if (!hdr) {
		err = -EMSGSIZE;
		goto out_free_msg;
	}

	err = devlink_nl_put_handle(msg, devlink);
	if (err)
		goto out_cancel_msg;

	if (region->port) {
		err = nla_put_u32(msg, DEVLINK_ATTR_PORT_INDEX,
				  region->port->index);
		if (err)
			goto out_cancel_msg;
	}

	err = nla_put_string(msg, DEVLINK_ATTR_REGION_NAME,
			     region->ops->name);
	if (err)
		goto out_cancel_msg;

	if (snapshot) {
		err = nla_put_u32(msg, DEVLINK_ATTR_REGION_SNAPSHOT_ID,
				  snapshot->id);
		if (err)
			goto out_cancel_msg;
	} else {
		err = nla_put_u64_64bit(msg, DEVLINK_ATTR_REGION_SIZE,
					region->size, DEVLINK_ATTR_PAD);
		if (err)
			goto out_cancel_msg;
	}
	genlmsg_end(msg, hdr);

	return msg;

out_cancel_msg:
	genlmsg_cancel(msg, hdr);
out_free_msg:
	nlmsg_free(msg);
	return ERR_PTR(err);
}

static void devlink_nl_region_notify(struct devlink_region *region,
				     struct devlink_snapshot *snapshot,
				     enum devlink_command cmd)
{
	struct devlink *devlink = region->devlink;
	struct sk_buff *msg;

	WARN_ON(cmd != DEVLINK_CMD_REGION_NEW && cmd != DEVLINK_CMD_REGION_DEL);
	if (!xa_get_mark(&devlinks, devlink->index, DEVLINK_REGISTERED))
		return;

	msg = devlink_nl_region_notify_build(region, snapshot, cmd, 0, 0);
	if (IS_ERR(msg))
		return;

	genlmsg_multicast_netns(&devlink_nl_family, devlink_net(devlink), msg,
				0, DEVLINK_MCGRP_CONFIG, GFP_KERNEL);
}

void devlink_regions_notify_register(struct devlink *devlink)
{
	struct devlink_region *region;

	list_for_each_entry(region, &devlink->region_list, list)
		devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);
}

void devlink_regions_notify_unregister(struct devlink *devlink)
{
	struct devlink_region *region;

	list_for_each_entry_reverse(region, &devlink->region_list, list)
		devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_DEL);
}

/**
 * __devlink_snapshot_id_increment - Increment number of snapshots using an id
 *	@devlink: devlink instance
 *	@id: the snapshot id
 *
 *	Track when a new snapshot begins using an id. Load the count for the
 *	given id from the snapshot xarray, increment it, and store it back.
 *
 *	Called when a new snapshot is created with the given id.
 *
 *	The id *must* have been previously allocated by
 *	devlink_region_snapshot_id_get().
 *
 *	Returns 0 on success, or an error on failure.
 */
static int __devlink_snapshot_id_increment(struct devlink *devlink, u32 id)
{
	unsigned long count;
	void *p;
	int err;

	xa_lock(&devlink->snapshot_ids);
	p = xa_load(&devlink->snapshot_ids, id);
	if (WARN_ON(!p)) {
		err = -EINVAL;
		goto unlock;
	}

	if (WARN_ON(!xa_is_value(p))) {
		err = -EINVAL;
		goto unlock;
	}

	count = xa_to_value(p);
	count++;

	err = xa_err(__xa_store(&devlink->snapshot_ids, id, xa_mk_value(count),
				GFP_ATOMIC));
unlock:
	xa_unlock(&devlink->snapshot_ids);
	return err;
}

/**
 * __devlink_snapshot_id_decrement - Decrease number of snapshots using an id
 *	@devlink: devlink instance
 *	@id: the snapshot id
 *
 *	Track when a snapshot is deleted and stops using an id. Load the count
 *	for the given id from the snapshot xarray, decrement it, and store it
 *	back.
 *
 *	If the count reaches zero, erase this id from the xarray, freeing it
 *	up for future re-use by devlink_region_snapshot_id_get().
 *
 *	Called when a snapshot using the given id is deleted, and when the
 *	initial allocator of the id is finished using it.
 */
static void __devlink_snapshot_id_decrement(struct devlink *devlink, u32 id)
{
	unsigned long count;
	void *p;

	xa_lock(&devlink->snapshot_ids);
	p = xa_load(&devlink->snapshot_ids, id);
	if (WARN_ON(!p))
		goto unlock;

	if (WARN_ON(!xa_is_value(p)))
		goto unlock;

	count = xa_to_value(p);

	if (count > 1) {
		count--;
		__xa_store(&devlink->snapshot_ids, id, xa_mk_value(count),
			   GFP_ATOMIC);
	} else {
		/* If this was the last user, we can erase this id */
		__xa_erase(&devlink->snapshot_ids, id);
	}
unlock:
	xa_unlock(&devlink->snapshot_ids);
}

/**
 *	__devlink_snapshot_id_insert - Insert a specific snapshot ID
 *	@devlink: devlink instance
 *	@id: the snapshot id
 *
 *	Mark the given snapshot id as used by inserting a zero value into the
 *	snapshot xarray.
 *
 *	This must be called while holding the devlink instance lock. Unlike
 *	devlink_snapshot_id_get, the initial reference count is zero, not one.
 *	It is expected that the id will immediately be used before
 *	releasing the devlink instance lock.
 *
 *	Returns zero on success, or an error code if the snapshot id could not
 *	be inserted.
 */
static int __devlink_snapshot_id_insert(struct devlink *devlink, u32 id)
{
	int err;

	xa_lock(&devlink->snapshot_ids);
	if (xa_load(&devlink->snapshot_ids, id)) {
		xa_unlock(&devlink->snapshot_ids);
		return -EEXIST;
	}
	err = xa_err(__xa_store(&devlink->snapshot_ids, id, xa_mk_value(0),
				GFP_ATOMIC));
	xa_unlock(&devlink->snapshot_ids);
	return err;
}

/**
 *	__devlink_region_snapshot_id_get - get snapshot ID
 *	@devlink: devlink instance
 *	@id: storage to return snapshot id
 *
 *	Allocates a new snapshot id. Returns zero on success, or a negative
 *	error on failure. Must be called while holding the devlink instance
 *	lock.
 *
 *	Snapshot IDs are tracked using an xarray which stores the number of
 *	users of the snapshot id.
 *
 *	Note that the caller of this function counts as a 'user', in order to
 *	avoid race conditions. The caller must release its hold on the
 *	snapshot by using devlink_region_snapshot_id_put.
 */
static int __devlink_region_snapshot_id_get(struct devlink *devlink, u32 *id)
{
	return xa_alloc(&devlink->snapshot_ids, id, xa_mk_value(1),
			xa_limit_32b, GFP_KERNEL);
}

/**
 *	__devlink_region_snapshot_create - create a new snapshot
 *	This will add a new snapshot of a region. The snapshot
 *	will be stored on the region struct and can be accessed
 *	from devlink. This is useful for future analyses of snapshots.
 *	Multiple snapshots can be created on a region.
 *	The @snapshot_id should be obtained using the getter function.
 *
 *	Must be called only while holding the region snapshot lock.
 *
 *	@region: devlink region of the snapshot
 *	@data: snapshot data
 *	@snapshot_id: snapshot id to be created
 */
static int
__devlink_region_snapshot_create(struct devlink_region *region,
				 u8 *data, u32 snapshot_id)
{
	struct devlink *devlink = region->devlink;
	struct devlink_snapshot *snapshot;
	int err;

	lockdep_assert_held(&region->snapshot_lock);

	/* check if region can hold one more snapshot */
	if (region->cur_snapshots == region->max_snapshots)
		return -ENOSPC;

	if (devlink_region_snapshot_get_by_id(region, snapshot_id))
		return -EEXIST;

	snapshot = kzalloc(sizeof(*snapshot), GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;

	err = __devlink_snapshot_id_increment(devlink, snapshot_id);
	if (err)
		goto err_snapshot_id_increment;

	snapshot->id = snapshot_id;
	snapshot->region = region;
	snapshot->data = data;

	list_add_tail(&snapshot->list, &region->snapshot_list);

	region->cur_snapshots++;

	devlink_nl_region_notify(region, snapshot, DEVLINK_CMD_REGION_NEW);
	return 0;

err_snapshot_id_increment:
	kfree(snapshot);
	return err;
}

static void devlink_region_snapshot_del(struct devlink_region *region,
					struct devlink_snapshot *snapshot)
{
	struct devlink *devlink = region->devlink;

	lockdep_assert_held(&region->snapshot_lock);

	devlink_nl_region_notify(region, snapshot, DEVLINK_CMD_REGION_DEL);
	region->cur_snapshots--;
	list_del(&snapshot->list);
	region->ops->destructor(snapshot->data);
	__devlink_snapshot_id_decrement(devlink, snapshot->id);
	kfree(snapshot);
}

int devlink_nl_region_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_port *port = NULL;
	struct devlink_region *region;
	const char *region_name;
	struct sk_buff *msg;
	unsigned int index;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_NAME))
		return -EINVAL;

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port)
			return -ENODEV;
	}

	region_name = nla_data(info->attrs[DEVLINK_ATTR_REGION_NAME]);
	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	err = devlink_nl_region_fill(msg, devlink, DEVLINK_CMD_REGION_GET,
				     info->snd_portid, info->snd_seq, 0,
				     region);
	if (err) {
		nlmsg_free(msg);
		return err;
	}

	return genlmsg_reply(msg, info);
}

static int devlink_nl_cmd_region_get_port_dumpit(struct sk_buff *msg,
						 struct netlink_callback *cb,
						 struct devlink_port *port,
						 int *idx, int start, int flags)
{
	struct devlink_region *region;
	int err = 0;

	list_for_each_entry(region, &port->region_list, list) {
		if (*idx < start) {
			(*idx)++;
			continue;
		}
		err = devlink_nl_region_fill(msg, port->devlink,
					     DEVLINK_CMD_REGION_GET,
					     NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq,
					     flags, region);
		if (err)
			goto out;
		(*idx)++;
	}

out:
	return err;
}

static int devlink_nl_region_get_dump_one(struct sk_buff *msg,
					  struct devlink *devlink,
					  struct netlink_callback *cb,
					  int flags)
{
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct devlink_region *region;
	struct devlink_port *port;
	unsigned long port_index;
	int idx = 0;
	int err;

	list_for_each_entry(region, &devlink->region_list, list) {
		if (idx < state->idx) {
			idx++;
			continue;
		}
		err = devlink_nl_region_fill(msg, devlink,
					     DEVLINK_CMD_REGION_GET,
					     NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq, flags,
					     region);
		if (err) {
			state->idx = idx;
			return err;
		}
		idx++;
	}

	xa_for_each(&devlink->ports, port_index, port) {
		err = devlink_nl_cmd_region_get_port_dumpit(msg, cb, port, &idx,
							    state->idx, flags);
		if (err) {
			state->idx = idx;
			return err;
		}
	}

	return 0;
}

int devlink_nl_region_get_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	return devlink_nl_dumpit(skb, cb, devlink_nl_region_get_dump_one);
}

int devlink_nl_cmd_region_del(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_snapshot *snapshot;
	struct devlink_port *port = NULL;
	struct devlink_region *region;
	const char *region_name;
	unsigned int index;
	u32 snapshot_id;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_NAME) ||
	    GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_SNAPSHOT_ID))
		return -EINVAL;

	region_name = nla_data(info->attrs[DEVLINK_ATTR_REGION_NAME]);
	snapshot_id = nla_get_u32(info->attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID]);

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port)
			return -ENODEV;
	}

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region)
		return -EINVAL;

	mutex_lock(&region->snapshot_lock);
	snapshot = devlink_region_snapshot_get_by_id(region, snapshot_id);
	if (!snapshot) {
		mutex_unlock(&region->snapshot_lock);
		return -EINVAL;
	}

	devlink_region_snapshot_del(region, snapshot);
	mutex_unlock(&region->snapshot_lock);
	return 0;
}

int devlink_nl_cmd_region_new(struct sk_buff *skb, struct genl_info *info)
{
	struct devlink *devlink = info->user_ptr[0];
	struct devlink_snapshot *snapshot;
	struct devlink_port *port = NULL;
	struct nlattr *snapshot_id_attr;
	struct devlink_region *region;
	const char *region_name;
	unsigned int index;
	u32 snapshot_id;
	u8 *data;
	int err;

	if (GENL_REQ_ATTR_CHECK(info, DEVLINK_ATTR_REGION_NAME)) {
		NL_SET_ERR_MSG(info->extack, "No region name provided");
		return -EINVAL;
	}

	region_name = nla_data(info->attrs[DEVLINK_ATTR_REGION_NAME]);

	if (info->attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(info->attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port)
			return -ENODEV;
	}

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region) {
		NL_SET_ERR_MSG(info->extack, "The requested region does not exist");
		return -EINVAL;
	}

	if (!region->ops->snapshot) {
		NL_SET_ERR_MSG(info->extack, "The requested region does not support taking an immediate snapshot");
		return -EOPNOTSUPP;
	}

	mutex_lock(&region->snapshot_lock);

	if (region->cur_snapshots == region->max_snapshots) {
		NL_SET_ERR_MSG(info->extack, "The region has reached the maximum number of stored snapshots");
		err = -ENOSPC;
		goto unlock;
	}

	snapshot_id_attr = info->attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID];
	if (snapshot_id_attr) {
		snapshot_id = nla_get_u32(snapshot_id_attr);

		if (devlink_region_snapshot_get_by_id(region, snapshot_id)) {
			NL_SET_ERR_MSG(info->extack, "The requested snapshot id is already in use");
			err = -EEXIST;
			goto unlock;
		}

		err = __devlink_snapshot_id_insert(devlink, snapshot_id);
		if (err)
			goto unlock;
	} else {
		err = __devlink_region_snapshot_id_get(devlink, &snapshot_id);
		if (err) {
			NL_SET_ERR_MSG(info->extack, "Failed to allocate a new snapshot id");
			goto unlock;
		}
	}

	if (port)
		err = region->port_ops->snapshot(port, region->port_ops,
						 info->extack, &data);
	else
		err = region->ops->snapshot(devlink, region->ops,
					    info->extack, &data);
	if (err)
		goto err_snapshot_capture;

	err = __devlink_region_snapshot_create(region, data, snapshot_id);
	if (err)
		goto err_snapshot_create;

	if (!snapshot_id_attr) {
		struct sk_buff *msg;

		snapshot = devlink_region_snapshot_get_by_id(region,
							     snapshot_id);
		if (WARN_ON(!snapshot)) {
			err = -EINVAL;
			goto unlock;
		}

		msg = devlink_nl_region_notify_build(region, snapshot,
						     DEVLINK_CMD_REGION_NEW,
						     info->snd_portid,
						     info->snd_seq);
		err = PTR_ERR_OR_ZERO(msg);
		if (err)
			goto err_notify;

		err = genlmsg_reply(msg, info);
		if (err)
			goto err_notify;
	}

	mutex_unlock(&region->snapshot_lock);
	return 0;

err_snapshot_create:
	region->ops->destructor(data);
err_snapshot_capture:
	__devlink_snapshot_id_decrement(devlink, snapshot_id);
	mutex_unlock(&region->snapshot_lock);
	return err;

err_notify:
	devlink_region_snapshot_del(region, snapshot);
unlock:
	mutex_unlock(&region->snapshot_lock);
	return err;
}

static int devlink_nl_cmd_region_read_chunk_fill(struct sk_buff *msg,
						 u8 *chunk, u32 chunk_size,
						 u64 addr)
{
	struct nlattr *chunk_attr;
	int err;

	chunk_attr = nla_nest_start_noflag(msg, DEVLINK_ATTR_REGION_CHUNK);
	if (!chunk_attr)
		return -EINVAL;

	err = nla_put(msg, DEVLINK_ATTR_REGION_CHUNK_DATA, chunk_size, chunk);
	if (err)
		goto nla_put_failure;

	err = nla_put_u64_64bit(msg, DEVLINK_ATTR_REGION_CHUNK_ADDR, addr,
				DEVLINK_ATTR_PAD);
	if (err)
		goto nla_put_failure;

	nla_nest_end(msg, chunk_attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(msg, chunk_attr);
	return err;
}

#define DEVLINK_REGION_READ_CHUNK_SIZE 256

typedef int devlink_chunk_fill_t(void *cb_priv, u8 *chunk, u32 chunk_size,
				 u64 curr_offset,
				 struct netlink_ext_ack *extack);

static int
devlink_nl_region_read_fill(struct sk_buff *skb, devlink_chunk_fill_t *cb,
			    void *cb_priv, u64 start_offset, u64 end_offset,
			    u64 *new_offset, struct netlink_ext_ack *extack)
{
	u64 curr_offset = start_offset;
	int err = 0;
	u8 *data;

	/* Allocate and re-use a single buffer */
	data = kmalloc(DEVLINK_REGION_READ_CHUNK_SIZE, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	*new_offset = start_offset;

	while (curr_offset < end_offset) {
		u32 data_size;

		data_size = min_t(u32, end_offset - curr_offset,
				  DEVLINK_REGION_READ_CHUNK_SIZE);

		err = cb(cb_priv, data, data_size, curr_offset, extack);
		if (err)
			break;

		err = devlink_nl_cmd_region_read_chunk_fill(skb, data, data_size, curr_offset);
		if (err)
			break;

		curr_offset += data_size;
	}
	*new_offset = curr_offset;

	kfree(data);

	return err;
}

static int
devlink_region_snapshot_fill(void *cb_priv, u8 *chunk, u32 chunk_size,
			     u64 curr_offset,
			     struct netlink_ext_ack __always_unused *extack)
{
	struct devlink_snapshot *snapshot = cb_priv;

	memcpy(chunk, &snapshot->data[curr_offset], chunk_size);

	return 0;
}

static int
devlink_region_port_direct_fill(void *cb_priv, u8 *chunk, u32 chunk_size,
				u64 curr_offset, struct netlink_ext_ack *extack)
{
	struct devlink_region *region = cb_priv;

	return region->port_ops->read(region->port, region->port_ops, extack,
				      curr_offset, chunk_size, chunk);
}

static int
devlink_region_direct_fill(void *cb_priv, u8 *chunk, u32 chunk_size,
			   u64 curr_offset, struct netlink_ext_ack *extack)
{
	struct devlink_region *region = cb_priv;

	return region->ops->read(region->devlink, region->ops, extack,
				 curr_offset, chunk_size, chunk);
}

int devlink_nl_cmd_region_read_dumpit(struct sk_buff *skb,
				      struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct devlink_nl_dump_state *state = devlink_dump_state(cb);
	struct nlattr *chunks_attr, *region_attr, *snapshot_attr;
	u64 ret_offset, start_offset, end_offset = U64_MAX;
	struct nlattr **attrs = info->info.attrs;
	struct devlink_port *port = NULL;
	devlink_chunk_fill_t *region_cb;
	struct devlink_region *region;
	const char *region_name;
	struct devlink *devlink;
	unsigned int index;
	void *region_cb_priv;
	void *hdr;
	int err;

	start_offset = state->start_offset;

	devlink = devlink_get_from_attrs_lock(sock_net(cb->skb->sk), attrs);
	if (IS_ERR(devlink))
		return PTR_ERR(devlink);

	if (!attrs[DEVLINK_ATTR_REGION_NAME]) {
		NL_SET_ERR_MSG(cb->extack, "No region name provided");
		err = -EINVAL;
		goto out_unlock;
	}

	if (attrs[DEVLINK_ATTR_PORT_INDEX]) {
		index = nla_get_u32(attrs[DEVLINK_ATTR_PORT_INDEX]);

		port = devlink_port_get_by_index(devlink, index);
		if (!port) {
			err = -ENODEV;
			goto out_unlock;
		}
	}

	region_attr = attrs[DEVLINK_ATTR_REGION_NAME];
	region_name = nla_data(region_attr);

	if (port)
		region = devlink_port_region_get_by_name(port, region_name);
	else
		region = devlink_region_get_by_name(devlink, region_name);

	if (!region) {
		NL_SET_ERR_MSG_ATTR(cb->extack, region_attr, "Requested region does not exist");
		err = -EINVAL;
		goto out_unlock;
	}

	snapshot_attr = attrs[DEVLINK_ATTR_REGION_SNAPSHOT_ID];
	if (!snapshot_attr) {
		if (!nla_get_flag(attrs[DEVLINK_ATTR_REGION_DIRECT])) {
			NL_SET_ERR_MSG(cb->extack, "No snapshot id provided");
			err = -EINVAL;
			goto out_unlock;
		}

		if (!region->ops->read) {
			NL_SET_ERR_MSG(cb->extack, "Requested region does not support direct read");
			err = -EOPNOTSUPP;
			goto out_unlock;
		}

		if (port)
			region_cb = &devlink_region_port_direct_fill;
		else
			region_cb = &devlink_region_direct_fill;
		region_cb_priv = region;
	} else {
		struct devlink_snapshot *snapshot;
		u32 snapshot_id;

		if (nla_get_flag(attrs[DEVLINK_ATTR_REGION_DIRECT])) {
			NL_SET_ERR_MSG_ATTR(cb->extack, snapshot_attr, "Direct region read does not use snapshot");
			err = -EINVAL;
			goto out_unlock;
		}

		snapshot_id = nla_get_u32(snapshot_attr);
		snapshot = devlink_region_snapshot_get_by_id(region, snapshot_id);
		if (!snapshot) {
			NL_SET_ERR_MSG_ATTR(cb->extack, snapshot_attr, "Requested snapshot does not exist");
			err = -EINVAL;
			goto out_unlock;
		}
		region_cb = &devlink_region_snapshot_fill;
		region_cb_priv = snapshot;
	}

	if (attrs[DEVLINK_ATTR_REGION_CHUNK_ADDR] &&
	    attrs[DEVLINK_ATTR_REGION_CHUNK_LEN]) {
		if (!start_offset)
			start_offset =
				nla_get_u64(attrs[DEVLINK_ATTR_REGION_CHUNK_ADDR]);

		end_offset = nla_get_u64(attrs[DEVLINK_ATTR_REGION_CHUNK_ADDR]);
		end_offset += nla_get_u64(attrs[DEVLINK_ATTR_REGION_CHUNK_LEN]);
	}

	if (end_offset > region->size)
		end_offset = region->size;

	/* return 0 if there is no further data to read */
	if (start_offset == end_offset) {
		err = 0;
		goto out_unlock;
	}

	hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			  &devlink_nl_family, NLM_F_ACK | NLM_F_MULTI,
			  DEVLINK_CMD_REGION_READ);
	if (!hdr) {
		err = -EMSGSIZE;
		goto out_unlock;
	}

	err = devlink_nl_put_handle(skb, devlink);
	if (err)
		goto nla_put_failure;

	if (region->port) {
		err = nla_put_u32(skb, DEVLINK_ATTR_PORT_INDEX,
				  region->port->index);
		if (err)
			goto nla_put_failure;
	}

	err = nla_put_string(skb, DEVLINK_ATTR_REGION_NAME, region_name);
	if (err)
		goto nla_put_failure;

	chunks_attr = nla_nest_start_noflag(skb, DEVLINK_ATTR_REGION_CHUNKS);
	if (!chunks_attr) {
		err = -EMSGSIZE;
		goto nla_put_failure;
	}

	err = devlink_nl_region_read_fill(skb, region_cb, region_cb_priv,
					  start_offset, end_offset, &ret_offset,
					  cb->extack);

	if (err && err != -EMSGSIZE)
		goto nla_put_failure;

	/* Check if there was any progress done to prevent infinite loop */
	if (ret_offset == start_offset) {
		err = -EINVAL;
		goto nla_put_failure;
	}

	state->start_offset = ret_offset;

	nla_nest_end(skb, chunks_attr);
	genlmsg_end(skb, hdr);
	devl_unlock(devlink);
	devlink_put(devlink);
	return skb->len;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
out_unlock:
	devl_unlock(devlink);
	devlink_put(devlink);
	return err;
}

/**
 * devl_region_create - create a new address region
 *
 * @devlink: devlink
 * @ops: region operations and name
 * @region_max_snapshots: Maximum supported number of snapshots for region
 * @region_size: size of region
 */
struct devlink_region *devl_region_create(struct devlink *devlink,
					  const struct devlink_region_ops *ops,
					  u32 region_max_snapshots,
					  u64 region_size)
{
	struct devlink_region *region;

	devl_assert_locked(devlink);

	if (WARN_ON(!ops) || WARN_ON(!ops->destructor))
		return ERR_PTR(-EINVAL);

	if (devlink_region_get_by_name(devlink, ops->name))
		return ERR_PTR(-EEXIST);

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region)
		return ERR_PTR(-ENOMEM);

	region->devlink = devlink;
	region->max_snapshots = region_max_snapshots;
	region->ops = ops;
	region->size = region_size;
	INIT_LIST_HEAD(&region->snapshot_list);
	mutex_init(&region->snapshot_lock);
	list_add_tail(&region->list, &devlink->region_list);
	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);

	return region;
}
EXPORT_SYMBOL_GPL(devl_region_create);

/**
 *	devlink_region_create - create a new address region
 *
 *	@devlink: devlink
 *	@ops: region operations and name
 *	@region_max_snapshots: Maximum supported number of snapshots for region
 *	@region_size: size of region
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
struct devlink_region *
devlink_region_create(struct devlink *devlink,
		      const struct devlink_region_ops *ops,
		      u32 region_max_snapshots, u64 region_size)
{
	struct devlink_region *region;

	devl_lock(devlink);
	region = devl_region_create(devlink, ops, region_max_snapshots,
				    region_size);
	devl_unlock(devlink);
	return region;
}
EXPORT_SYMBOL_GPL(devlink_region_create);

/**
 *	devlink_port_region_create - create a new address region for a port
 *
 *	@port: devlink port
 *	@ops: region operations and name
 *	@region_max_snapshots: Maximum supported number of snapshots for region
 *	@region_size: size of region
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
struct devlink_region *
devlink_port_region_create(struct devlink_port *port,
			   const struct devlink_port_region_ops *ops,
			   u32 region_max_snapshots, u64 region_size)
{
	struct devlink *devlink = port->devlink;
	struct devlink_region *region;
	int err = 0;

	ASSERT_DEVLINK_PORT_INITIALIZED(port);

	if (WARN_ON(!ops) || WARN_ON(!ops->destructor))
		return ERR_PTR(-EINVAL);

	devl_lock(devlink);

	if (devlink_port_region_get_by_name(port, ops->name)) {
		err = -EEXIST;
		goto unlock;
	}

	region = kzalloc(sizeof(*region), GFP_KERNEL);
	if (!region) {
		err = -ENOMEM;
		goto unlock;
	}

	region->devlink = devlink;
	region->port = port;
	region->max_snapshots = region_max_snapshots;
	region->port_ops = ops;
	region->size = region_size;
	INIT_LIST_HEAD(&region->snapshot_list);
	mutex_init(&region->snapshot_lock);
	list_add_tail(&region->list, &port->region_list);
	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_NEW);

	devl_unlock(devlink);
	return region;

unlock:
	devl_unlock(devlink);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(devlink_port_region_create);

/**
 * devl_region_destroy - destroy address region
 *
 * @region: devlink region to destroy
 */
void devl_region_destroy(struct devlink_region *region)
{
	struct devlink *devlink = region->devlink;
	struct devlink_snapshot *snapshot, *ts;

	devl_assert_locked(devlink);

	/* Free all snapshots of region */
	mutex_lock(&region->snapshot_lock);
	list_for_each_entry_safe(snapshot, ts, &region->snapshot_list, list)
		devlink_region_snapshot_del(region, snapshot);
	mutex_unlock(&region->snapshot_lock);

	list_del(&region->list);
	mutex_destroy(&region->snapshot_lock);

	devlink_nl_region_notify(region, NULL, DEVLINK_CMD_REGION_DEL);
	kfree(region);
}
EXPORT_SYMBOL_GPL(devl_region_destroy);

/**
 *	devlink_region_destroy - destroy address region
 *
 *	@region: devlink region to destroy
 *
 *	Context: Takes and release devlink->lock <mutex>.
 */
void devlink_region_destroy(struct devlink_region *region)
{
	struct devlink *devlink = region->devlink;

	devl_lock(devlink);
	devl_region_destroy(region);
	devl_unlock(devlink);
}
EXPORT_SYMBOL_GPL(devlink_region_destroy);

/**
 *	devlink_region_snapshot_id_get - get snapshot ID
 *
 *	This callback should be called when adding a new snapshot,
 *	Driver should use the same id for multiple snapshots taken
 *	on multiple regions at the same time/by the same trigger.
 *
 *	The caller of this function must use devlink_region_snapshot_id_put
 *	when finished creating regions using this id.
 *
 *	Returns zero on success, or a negative error code on failure.
 *
 *	@devlink: devlink
 *	@id: storage to return id
 */
int devlink_region_snapshot_id_get(struct devlink *devlink, u32 *id)
{
	return __devlink_region_snapshot_id_get(devlink, id);
}
EXPORT_SYMBOL_GPL(devlink_region_snapshot_id_get);

/**
 *	devlink_region_snapshot_id_put - put snapshot ID reference
 *
 *	This should be called by a driver after finishing creating snapshots
 *	with an id. Doing so ensures that the ID can later be released in the
 *	event that all snapshots using it have been destroyed.
 *
 *	@devlink: devlink
 *	@id: id to release reference on
 */
void devlink_region_snapshot_id_put(struct devlink *devlink, u32 id)
{
	__devlink_snapshot_id_decrement(devlink, id);
}
EXPORT_SYMBOL_GPL(devlink_region_snapshot_id_put);

/**
 *	devlink_region_snapshot_create - create a new snapshot
 *	This will add a new snapshot of a region. The snapshot
 *	will be stored on the region struct and can be accessed
 *	from devlink. This is useful for future analyses of snapshots.
 *	Multiple snapshots can be created on a region.
 *	The @snapshot_id should be obtained using the getter function.
 *
 *	@region: devlink region of the snapshot
 *	@data: snapshot data
 *	@snapshot_id: snapshot id to be created
 */
int devlink_region_snapshot_create(struct devlink_region *region,
				   u8 *data, u32 snapshot_id)
{
	int err;

	mutex_lock(&region->snapshot_lock);
	err = __devlink_region_snapshot_create(region, data, snapshot_id);
	mutex_unlock(&region->snapshot_lock);
	return err;
}
EXPORT_SYMBOL_GPL(devlink_region_snapshot_create);
