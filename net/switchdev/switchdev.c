/*
 * net/switchdev/switchdev.c - Switch device API
 * Copyright (c) 2014-2015 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2014-2015 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/if_vlan.h>
#include <linux/rtnetlink.h>
#include <net/switchdev.h>

/**
 *	switchdev_trans_item_enqueue - Enqueue data item to transaction queue
 *
 *	@trans: transaction
 *	@data: pointer to data being queued
 *	@destructor: data destructor
 *	@tritem: transaction item being queued
 *
 *	Enqeueue data item to transaction queue. tritem is typically placed in
 *	cointainter pointed at by data pointer. Destructor is called on
 *	transaction abort and after successful commit phase in case
 *	the caller did not dequeue the item before.
 */
void switchdev_trans_item_enqueue(struct switchdev_trans *trans,
				  void *data, void (*destructor)(void const *),
				  struct switchdev_trans_item *tritem)
{
	tritem->data = data;
	tritem->destructor = destructor;
	list_add_tail(&tritem->list, &trans->item_list);
}
EXPORT_SYMBOL_GPL(switchdev_trans_item_enqueue);

static struct switchdev_trans_item *
__switchdev_trans_item_dequeue(struct switchdev_trans *trans)
{
	struct switchdev_trans_item *tritem;

	if (list_empty(&trans->item_list))
		return NULL;
	tritem = list_first_entry(&trans->item_list,
				  struct switchdev_trans_item, list);
	list_del(&tritem->list);
	return tritem;
}

/**
 *	switchdev_trans_item_dequeue - Dequeue data item from transaction queue
 *
 *	@trans: transaction
 */
void *switchdev_trans_item_dequeue(struct switchdev_trans *trans)
{
	struct switchdev_trans_item *tritem;

	tritem = __switchdev_trans_item_dequeue(trans);
	BUG_ON(!tritem);
	return tritem->data;
}
EXPORT_SYMBOL_GPL(switchdev_trans_item_dequeue);

static void switchdev_trans_init(struct switchdev_trans *trans)
{
	INIT_LIST_HEAD(&trans->item_list);
}

static void switchdev_trans_items_destroy(struct switchdev_trans *trans)
{
	struct switchdev_trans_item *tritem;

	while ((tritem = __switchdev_trans_item_dequeue(trans)))
		tritem->destructor(tritem->data);
}

static void switchdev_trans_items_warn_destroy(struct net_device *dev,
					       struct switchdev_trans *trans)
{
	WARN(!list_empty(&trans->item_list), "%s: transaction item queue is not empty.\n",
	     dev->name);
	switchdev_trans_items_destroy(trans);
}

static LIST_HEAD(deferred);
static DEFINE_SPINLOCK(deferred_lock);

typedef void switchdev_deferred_func_t(struct net_device *dev,
				       const void *data);

struct switchdev_deferred_item {
	struct list_head list;
	struct net_device *dev;
	switchdev_deferred_func_t *func;
	unsigned long data[0];
};

static struct switchdev_deferred_item *switchdev_deferred_dequeue(void)
{
	struct switchdev_deferred_item *dfitem;

	spin_lock_bh(&deferred_lock);
	if (list_empty(&deferred)) {
		dfitem = NULL;
		goto unlock;
	}
	dfitem = list_first_entry(&deferred,
				  struct switchdev_deferred_item, list);
	list_del(&dfitem->list);
unlock:
	spin_unlock_bh(&deferred_lock);
	return dfitem;
}

/**
 *	switchdev_deferred_process - Process ops in deferred queue
 *
 *	Called to flush the ops currently queued in deferred ops queue.
 *	rtnl_lock must be held.
 */
void switchdev_deferred_process(void)
{
	struct switchdev_deferred_item *dfitem;

	ASSERT_RTNL();

	while ((dfitem = switchdev_deferred_dequeue())) {
		dfitem->func(dfitem->dev, dfitem->data);
		dev_put(dfitem->dev);
		kfree(dfitem);
	}
}
EXPORT_SYMBOL_GPL(switchdev_deferred_process);

static void switchdev_deferred_process_work(struct work_struct *work)
{
	rtnl_lock();
	switchdev_deferred_process();
	rtnl_unlock();
}

static DECLARE_WORK(deferred_process_work, switchdev_deferred_process_work);

static int switchdev_deferred_enqueue(struct net_device *dev,
				      const void *data, size_t data_len,
				      switchdev_deferred_func_t *func)
{
	struct switchdev_deferred_item *dfitem;

	dfitem = kmalloc(sizeof(*dfitem) + data_len, GFP_ATOMIC);
	if (!dfitem)
		return -ENOMEM;
	dfitem->dev = dev;
	dfitem->func = func;
	memcpy(dfitem->data, data, data_len);
	dev_hold(dev);
	spin_lock_bh(&deferred_lock);
	list_add_tail(&dfitem->list, &deferred);
	spin_unlock_bh(&deferred_lock);
	schedule_work(&deferred_process_work);
	return 0;
}

/**
 *	switchdev_port_attr_get - Get port attribute
 *
 *	@dev: port device
 *	@attr: attribute to get
 */
int switchdev_port_attr_get(struct net_device *dev, struct switchdev_attr *attr)
{
	const struct switchdev_ops *ops = dev->switchdev_ops;
	struct net_device *lower_dev;
	struct list_head *iter;
	struct switchdev_attr first = {
		.id = SWITCHDEV_ATTR_ID_UNDEFINED
	};
	int err = -EOPNOTSUPP;

	if (ops && ops->switchdev_port_attr_get)
		return ops->switchdev_port_attr_get(dev, attr);

	if (attr->flags & SWITCHDEV_F_NO_RECURSE)
		return err;

	/* Switch device port(s) may be stacked under
	 * bond/team/vlan dev, so recurse down to get attr on
	 * each port.  Return -ENODATA if attr values don't
	 * compare across ports.
	 */

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = switchdev_port_attr_get(lower_dev, attr);
		if (err)
			break;
		if (first.id == SWITCHDEV_ATTR_ID_UNDEFINED)
			first = *attr;
		else if (memcmp(&first, attr, sizeof(*attr)))
			return -ENODATA;
	}

	return err;
}
EXPORT_SYMBOL_GPL(switchdev_port_attr_get);

static int __switchdev_port_attr_set(struct net_device *dev,
				     const struct switchdev_attr *attr,
				     struct switchdev_trans *trans)
{
	const struct switchdev_ops *ops = dev->switchdev_ops;
	struct net_device *lower_dev;
	struct list_head *iter;
	int err = -EOPNOTSUPP;

	if (ops && ops->switchdev_port_attr_set) {
		err = ops->switchdev_port_attr_set(dev, attr, trans);
		goto done;
	}

	if (attr->flags & SWITCHDEV_F_NO_RECURSE)
		goto done;

	/* Switch device port(s) may be stacked under
	 * bond/team/vlan dev, so recurse down to set attr on
	 * each port.
	 */

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = __switchdev_port_attr_set(lower_dev, attr, trans);
		if (err)
			break;
	}

done:
	if (err == -EOPNOTSUPP && attr->flags & SWITCHDEV_F_SKIP_EOPNOTSUPP)
		err = 0;

	return err;
}

static int switchdev_port_attr_set_now(struct net_device *dev,
				       const struct switchdev_attr *attr)
{
	struct switchdev_trans trans;
	int err;

	switchdev_trans_init(&trans);

	/* Phase I: prepare for attr set. Driver/device should fail
	 * here if there are going to be issues in the commit phase,
	 * such as lack of resources or support.  The driver/device
	 * should reserve resources needed for the commit phase here,
	 * but should not commit the attr.
	 */

	trans.ph_prepare = true;
	err = __switchdev_port_attr_set(dev, attr, &trans);
	if (err) {
		/* Prepare phase failed: abort the transaction.  Any
		 * resources reserved in the prepare phase are
		 * released.
		 */

		if (err != -EOPNOTSUPP)
			switchdev_trans_items_destroy(&trans);

		return err;
	}

	/* Phase II: commit attr set.  This cannot fail as a fault
	 * of driver/device.  If it does, it's a bug in the driver/device
	 * because the driver said everythings was OK in phase I.
	 */

	trans.ph_prepare = false;
	err = __switchdev_port_attr_set(dev, attr, &trans);
	WARN(err, "%s: Commit of attribute (id=%d) failed.\n",
	     dev->name, attr->id);
	switchdev_trans_items_warn_destroy(dev, &trans);

	return err;
}

static void switchdev_port_attr_set_deferred(struct net_device *dev,
					     const void *data)
{
	const struct switchdev_attr *attr = data;
	int err;

	err = switchdev_port_attr_set_now(dev, attr);
	if (err && err != -EOPNOTSUPP)
		netdev_err(dev, "failed (err=%d) to set attribute (id=%d)\n",
			   err, attr->id);
	if (attr->complete)
		attr->complete(dev, err, attr->complete_priv);
}

static int switchdev_port_attr_set_defer(struct net_device *dev,
					 const struct switchdev_attr *attr)
{
	return switchdev_deferred_enqueue(dev, attr, sizeof(*attr),
					  switchdev_port_attr_set_deferred);
}

/**
 *	switchdev_port_attr_set - Set port attribute
 *
 *	@dev: port device
 *	@attr: attribute to set
 *
 *	Use a 2-phase prepare-commit transaction model to ensure
 *	system is not left in a partially updated state due to
 *	failure from driver/device.
 *
 *	rtnl_lock must be held and must not be in atomic section,
 *	in case SWITCHDEV_F_DEFER flag is not set.
 */
int switchdev_port_attr_set(struct net_device *dev,
			    const struct switchdev_attr *attr)
{
	if (attr->flags & SWITCHDEV_F_DEFER)
		return switchdev_port_attr_set_defer(dev, attr);
	ASSERT_RTNL();
	return switchdev_port_attr_set_now(dev, attr);
}
EXPORT_SYMBOL_GPL(switchdev_port_attr_set);

static size_t switchdev_obj_size(const struct switchdev_obj *obj)
{
	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		return sizeof(struct switchdev_obj_port_vlan);
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		return sizeof(struct switchdev_obj_port_fdb);
	case SWITCHDEV_OBJ_ID_PORT_MDB:
		return sizeof(struct switchdev_obj_port_mdb);
	default:
		BUG();
	}
	return 0;
}

static int __switchdev_port_obj_add(struct net_device *dev,
				    const struct switchdev_obj *obj,
				    struct switchdev_trans *trans)
{
	const struct switchdev_ops *ops = dev->switchdev_ops;
	struct net_device *lower_dev;
	struct list_head *iter;
	int err = -EOPNOTSUPP;

	if (ops && ops->switchdev_port_obj_add)
		return ops->switchdev_port_obj_add(dev, obj, trans);

	/* Switch device port(s) may be stacked under
	 * bond/team/vlan dev, so recurse down to add object on
	 * each port.
	 */

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = __switchdev_port_obj_add(lower_dev, obj, trans);
		if (err)
			break;
	}

	return err;
}

static int switchdev_port_obj_add_now(struct net_device *dev,
				      const struct switchdev_obj *obj)
{
	struct switchdev_trans trans;
	int err;

	ASSERT_RTNL();

	switchdev_trans_init(&trans);

	/* Phase I: prepare for obj add. Driver/device should fail
	 * here if there are going to be issues in the commit phase,
	 * such as lack of resources or support.  The driver/device
	 * should reserve resources needed for the commit phase here,
	 * but should not commit the obj.
	 */

	trans.ph_prepare = true;
	err = __switchdev_port_obj_add(dev, obj, &trans);
	if (err) {
		/* Prepare phase failed: abort the transaction.  Any
		 * resources reserved in the prepare phase are
		 * released.
		 */

		if (err != -EOPNOTSUPP)
			switchdev_trans_items_destroy(&trans);

		return err;
	}

	/* Phase II: commit obj add.  This cannot fail as a fault
	 * of driver/device.  If it does, it's a bug in the driver/device
	 * because the driver said everythings was OK in phase I.
	 */

	trans.ph_prepare = false;
	err = __switchdev_port_obj_add(dev, obj, &trans);
	WARN(err, "%s: Commit of object (id=%d) failed.\n", dev->name, obj->id);
	switchdev_trans_items_warn_destroy(dev, &trans);

	return err;
}

static void switchdev_port_obj_add_deferred(struct net_device *dev,
					    const void *data)
{
	const struct switchdev_obj *obj = data;
	int err;

	err = switchdev_port_obj_add_now(dev, obj);
	if (err && err != -EOPNOTSUPP)
		netdev_err(dev, "failed (err=%d) to add object (id=%d)\n",
			   err, obj->id);
	if (obj->complete)
		obj->complete(dev, err, obj->complete_priv);
}

static int switchdev_port_obj_add_defer(struct net_device *dev,
					const struct switchdev_obj *obj)
{
	return switchdev_deferred_enqueue(dev, obj, switchdev_obj_size(obj),
					  switchdev_port_obj_add_deferred);
}

/**
 *	switchdev_port_obj_add - Add port object
 *
 *	@dev: port device
 *	@id: object ID
 *	@obj: object to add
 *
 *	Use a 2-phase prepare-commit transaction model to ensure
 *	system is not left in a partially updated state due to
 *	failure from driver/device.
 *
 *	rtnl_lock must be held and must not be in atomic section,
 *	in case SWITCHDEV_F_DEFER flag is not set.
 */
int switchdev_port_obj_add(struct net_device *dev,
			   const struct switchdev_obj *obj)
{
	if (obj->flags & SWITCHDEV_F_DEFER)
		return switchdev_port_obj_add_defer(dev, obj);
	ASSERT_RTNL();
	return switchdev_port_obj_add_now(dev, obj);
}
EXPORT_SYMBOL_GPL(switchdev_port_obj_add);

static int switchdev_port_obj_del_now(struct net_device *dev,
				      const struct switchdev_obj *obj)
{
	const struct switchdev_ops *ops = dev->switchdev_ops;
	struct net_device *lower_dev;
	struct list_head *iter;
	int err = -EOPNOTSUPP;

	if (ops && ops->switchdev_port_obj_del)
		return ops->switchdev_port_obj_del(dev, obj);

	/* Switch device port(s) may be stacked under
	 * bond/team/vlan dev, so recurse down to delete object on
	 * each port.
	 */

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = switchdev_port_obj_del_now(lower_dev, obj);
		if (err)
			break;
	}

	return err;
}

static void switchdev_port_obj_del_deferred(struct net_device *dev,
					    const void *data)
{
	const struct switchdev_obj *obj = data;
	int err;

	err = switchdev_port_obj_del_now(dev, obj);
	if (err && err != -EOPNOTSUPP)
		netdev_err(dev, "failed (err=%d) to del object (id=%d)\n",
			   err, obj->id);
	if (obj->complete)
		obj->complete(dev, err, obj->complete_priv);
}

static int switchdev_port_obj_del_defer(struct net_device *dev,
					const struct switchdev_obj *obj)
{
	return switchdev_deferred_enqueue(dev, obj, switchdev_obj_size(obj),
					  switchdev_port_obj_del_deferred);
}

/**
 *	switchdev_port_obj_del - Delete port object
 *
 *	@dev: port device
 *	@id: object ID
 *	@obj: object to delete
 *
 *	rtnl_lock must be held and must not be in atomic section,
 *	in case SWITCHDEV_F_DEFER flag is not set.
 */
int switchdev_port_obj_del(struct net_device *dev,
			   const struct switchdev_obj *obj)
{
	if (obj->flags & SWITCHDEV_F_DEFER)
		return switchdev_port_obj_del_defer(dev, obj);
	ASSERT_RTNL();
	return switchdev_port_obj_del_now(dev, obj);
}
EXPORT_SYMBOL_GPL(switchdev_port_obj_del);

/**
 *	switchdev_port_obj_dump - Dump port objects
 *
 *	@dev: port device
 *	@id: object ID
 *	@obj: object to dump
 *	@cb: function to call with a filled object
 *
 *	rtnl_lock must be held.
 */
int switchdev_port_obj_dump(struct net_device *dev, struct switchdev_obj *obj,
			    switchdev_obj_dump_cb_t *cb)
{
	const struct switchdev_ops *ops = dev->switchdev_ops;
	struct net_device *lower_dev;
	struct list_head *iter;
	int err = -EOPNOTSUPP;

	ASSERT_RTNL();

	if (ops && ops->switchdev_port_obj_dump)
		return ops->switchdev_port_obj_dump(dev, obj, cb);

	/* Switch device port(s) may be stacked under
	 * bond/team/vlan dev, so recurse down to dump objects on
	 * first port at bottom of stack.
	 */

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = switchdev_port_obj_dump(lower_dev, obj, cb);
		break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(switchdev_port_obj_dump);

static ATOMIC_NOTIFIER_HEAD(switchdev_notif_chain);

/**
 *	register_switchdev_notifier - Register notifier
 *	@nb: notifier_block
 *
 *	Register switch device notifier.
 */
int register_switchdev_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&switchdev_notif_chain, nb);
}
EXPORT_SYMBOL_GPL(register_switchdev_notifier);

/**
 *	unregister_switchdev_notifier - Unregister notifier
 *	@nb: notifier_block
 *
 *	Unregister switch device notifier.
 */
int unregister_switchdev_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&switchdev_notif_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_switchdev_notifier);

/**
 *	call_switchdev_notifiers - Call notifiers
 *	@val: value passed unmodified to notifier function
 *	@dev: port device
 *	@info: notifier information data
 *
 *	Call all network notifier blocks.
 */
int call_switchdev_notifiers(unsigned long val, struct net_device *dev,
			     struct switchdev_notifier_info *info)
{
	info->dev = dev;
	return atomic_notifier_call_chain(&switchdev_notif_chain, val, info);
}
EXPORT_SYMBOL_GPL(call_switchdev_notifiers);

struct switchdev_vlan_dump {
	struct switchdev_obj_port_vlan vlan;
	struct sk_buff *skb;
	u32 filter_mask;
	u16 flags;
	u16 begin;
	u16 end;
};

static int switchdev_port_vlan_dump_put(struct switchdev_vlan_dump *dump)
{
	struct bridge_vlan_info vinfo;

	vinfo.flags = dump->flags;

	if (dump->begin == 0 && dump->end == 0) {
		return 0;
	} else if (dump->begin == dump->end) {
		vinfo.vid = dump->begin;
		if (nla_put(dump->skb, IFLA_BRIDGE_VLAN_INFO,
			    sizeof(vinfo), &vinfo))
			return -EMSGSIZE;
	} else {
		vinfo.vid = dump->begin;
		vinfo.flags |= BRIDGE_VLAN_INFO_RANGE_BEGIN;
		if (nla_put(dump->skb, IFLA_BRIDGE_VLAN_INFO,
			    sizeof(vinfo), &vinfo))
			return -EMSGSIZE;
		vinfo.vid = dump->end;
		vinfo.flags &= ~BRIDGE_VLAN_INFO_RANGE_BEGIN;
		vinfo.flags |= BRIDGE_VLAN_INFO_RANGE_END;
		if (nla_put(dump->skb, IFLA_BRIDGE_VLAN_INFO,
			    sizeof(vinfo), &vinfo))
			return -EMSGSIZE;
	}

	return 0;
}

static int switchdev_port_vlan_dump_cb(struct switchdev_obj *obj)
{
	struct switchdev_obj_port_vlan *vlan = SWITCHDEV_OBJ_PORT_VLAN(obj);
	struct switchdev_vlan_dump *dump =
		container_of(vlan, struct switchdev_vlan_dump, vlan);
	int err = 0;

	if (vlan->vid_begin > vlan->vid_end)
		return -EINVAL;

	if (dump->filter_mask & RTEXT_FILTER_BRVLAN) {
		dump->flags = vlan->flags;
		for (dump->begin = dump->end = vlan->vid_begin;
		     dump->begin <= vlan->vid_end;
		     dump->begin++, dump->end++) {
			err = switchdev_port_vlan_dump_put(dump);
			if (err)
				return err;
		}
	} else if (dump->filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED) {
		if (dump->begin > vlan->vid_begin &&
		    dump->begin >= vlan->vid_end) {
			if ((dump->begin - 1) == vlan->vid_end &&
			    dump->flags == vlan->flags) {
				/* prepend */
				dump->begin = vlan->vid_begin;
			} else {
				err = switchdev_port_vlan_dump_put(dump);
				dump->flags = vlan->flags;
				dump->begin = vlan->vid_begin;
				dump->end = vlan->vid_end;
			}
		} else if (dump->end <= vlan->vid_begin &&
		           dump->end < vlan->vid_end) {
			if ((dump->end  + 1) == vlan->vid_begin &&
			    dump->flags == vlan->flags) {
				/* append */
				dump->end = vlan->vid_end;
			} else {
				err = switchdev_port_vlan_dump_put(dump);
				dump->flags = vlan->flags;
				dump->begin = vlan->vid_begin;
				dump->end = vlan->vid_end;
			}
		} else {
			err = -EINVAL;
		}
	}

	return err;
}

static int switchdev_port_vlan_fill(struct sk_buff *skb, struct net_device *dev,
				    u32 filter_mask)
{
	struct switchdev_vlan_dump dump = {
		.vlan.obj.orig_dev = dev,
		.vlan.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
		.skb = skb,
		.filter_mask = filter_mask,
	};
	int err = 0;

	if ((filter_mask & RTEXT_FILTER_BRVLAN) ||
	    (filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED)) {
		err = switchdev_port_obj_dump(dev, &dump.vlan.obj,
					      switchdev_port_vlan_dump_cb);
		if (err)
			goto err_out;
		if (filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED)
			/* last one */
			err = switchdev_port_vlan_dump_put(&dump);
	}

err_out:
	return err == -EOPNOTSUPP ? 0 : err;
}

/**
 *	switchdev_port_bridge_getlink - Get bridge port attributes
 *
 *	@dev: port device
 *
 *	Called for SELF on rtnl_bridge_getlink to get bridge port
 *	attributes.
 */
int switchdev_port_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
				  struct net_device *dev, u32 filter_mask,
				  int nlflags)
{
	struct switchdev_attr attr = {
		.orig_dev = dev,
		.id = SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS,
	};
	u16 mode = BRIDGE_MODE_UNDEF;
	u32 mask = BR_LEARNING | BR_LEARNING_SYNC | BR_FLOOD;
	int err;

	if (!netif_is_bridge_port(dev))
		return -EOPNOTSUPP;

	err = switchdev_port_attr_get(dev, &attr);
	if (err && err != -EOPNOTSUPP)
		return err;

	return ndo_dflt_bridge_getlink(skb, pid, seq, dev, mode,
				       attr.u.brport_flags, mask, nlflags,
				       filter_mask, switchdev_port_vlan_fill);
}
EXPORT_SYMBOL_GPL(switchdev_port_bridge_getlink);

static int switchdev_port_br_setflag(struct net_device *dev,
				     struct nlattr *nlattr,
				     unsigned long brport_flag)
{
	struct switchdev_attr attr = {
		.orig_dev = dev,
		.id = SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS,
	};
	u8 flag = nla_get_u8(nlattr);
	int err;

	err = switchdev_port_attr_get(dev, &attr);
	if (err)
		return err;

	if (flag)
		attr.u.brport_flags |= brport_flag;
	else
		attr.u.brport_flags &= ~brport_flag;

	return switchdev_port_attr_set(dev, &attr);
}

static const struct nla_policy
switchdev_port_bridge_policy[IFLA_BRPORT_MAX + 1] = {
	[IFLA_BRPORT_STATE]		= { .type = NLA_U8 },
	[IFLA_BRPORT_COST]		= { .type = NLA_U32 },
	[IFLA_BRPORT_PRIORITY]		= { .type = NLA_U16 },
	[IFLA_BRPORT_MODE]		= { .type = NLA_U8 },
	[IFLA_BRPORT_GUARD]		= { .type = NLA_U8 },
	[IFLA_BRPORT_PROTECT]		= { .type = NLA_U8 },
	[IFLA_BRPORT_FAST_LEAVE]	= { .type = NLA_U8 },
	[IFLA_BRPORT_LEARNING]		= { .type = NLA_U8 },
	[IFLA_BRPORT_LEARNING_SYNC]	= { .type = NLA_U8 },
	[IFLA_BRPORT_UNICAST_FLOOD]	= { .type = NLA_U8 },
};

static int switchdev_port_br_setlink_protinfo(struct net_device *dev,
					      struct nlattr *protinfo)
{
	struct nlattr *attr;
	int rem;
	int err;

	err = nla_validate_nested(protinfo, IFLA_BRPORT_MAX,
				  switchdev_port_bridge_policy, NULL);
	if (err)
		return err;

	nla_for_each_nested(attr, protinfo, rem) {
		switch (nla_type(attr)) {
		case IFLA_BRPORT_LEARNING:
			err = switchdev_port_br_setflag(dev, attr,
							BR_LEARNING);
			break;
		case IFLA_BRPORT_LEARNING_SYNC:
			err = switchdev_port_br_setflag(dev, attr,
							BR_LEARNING_SYNC);
			break;
		case IFLA_BRPORT_UNICAST_FLOOD:
			err = switchdev_port_br_setflag(dev, attr, BR_FLOOD);
			break;
		default:
			err = -EOPNOTSUPP;
			break;
		}
		if (err)
			return err;
	}

	return 0;
}

static int switchdev_port_br_afspec(struct net_device *dev,
				    struct nlattr *afspec,
				    int (*f)(struct net_device *dev,
					     const struct switchdev_obj *obj))
{
	struct nlattr *attr;
	struct bridge_vlan_info *vinfo;
	struct switchdev_obj_port_vlan vlan = {
		.obj.orig_dev = dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_VLAN,
	};
	int rem;
	int err;

	nla_for_each_nested(attr, afspec, rem) {
		if (nla_type(attr) != IFLA_BRIDGE_VLAN_INFO)
			continue;
		if (nla_len(attr) != sizeof(struct bridge_vlan_info))
			return -EINVAL;
		vinfo = nla_data(attr);
		if (!vinfo->vid || vinfo->vid >= VLAN_VID_MASK)
			return -EINVAL;
		vlan.flags = vinfo->flags;
		if (vinfo->flags & BRIDGE_VLAN_INFO_RANGE_BEGIN) {
			if (vlan.vid_begin)
				return -EINVAL;
			vlan.vid_begin = vinfo->vid;
			/* don't allow range of pvids */
			if (vlan.flags & BRIDGE_VLAN_INFO_PVID)
				return -EINVAL;
		} else if (vinfo->flags & BRIDGE_VLAN_INFO_RANGE_END) {
			if (!vlan.vid_begin)
				return -EINVAL;
			vlan.vid_end = vinfo->vid;
			if (vlan.vid_end <= vlan.vid_begin)
				return -EINVAL;
			err = f(dev, &vlan.obj);
			if (err)
				return err;
			vlan.vid_begin = 0;
		} else {
			if (vlan.vid_begin)
				return -EINVAL;
			vlan.vid_begin = vinfo->vid;
			vlan.vid_end = vinfo->vid;
			err = f(dev, &vlan.obj);
			if (err)
				return err;
			vlan.vid_begin = 0;
		}
	}

	return 0;
}

/**
 *	switchdev_port_bridge_setlink - Set bridge port attributes
 *
 *	@dev: port device
 *	@nlh: netlink header
 *	@flags: netlink flags
 *
 *	Called for SELF on rtnl_bridge_setlink to set bridge port
 *	attributes.
 */
int switchdev_port_bridge_setlink(struct net_device *dev,
				  struct nlmsghdr *nlh, u16 flags)
{
	struct nlattr *protinfo;
	struct nlattr *afspec;
	int err = 0;

	if (!netif_is_bridge_port(dev))
		return -EOPNOTSUPP;

	protinfo = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg),
				   IFLA_PROTINFO);
	if (protinfo) {
		err = switchdev_port_br_setlink_protinfo(dev, protinfo);
		if (err)
			return err;
	}

	afspec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg),
				 IFLA_AF_SPEC);
	if (afspec)
		err = switchdev_port_br_afspec(dev, afspec,
					       switchdev_port_obj_add);

	return err;
}
EXPORT_SYMBOL_GPL(switchdev_port_bridge_setlink);

/**
 *	switchdev_port_bridge_dellink - Set bridge port attributes
 *
 *	@dev: port device
 *	@nlh: netlink header
 *	@flags: netlink flags
 *
 *	Called for SELF on rtnl_bridge_dellink to set bridge port
 *	attributes.
 */
int switchdev_port_bridge_dellink(struct net_device *dev,
				  struct nlmsghdr *nlh, u16 flags)
{
	struct nlattr *afspec;

	if (!netif_is_bridge_port(dev))
		return -EOPNOTSUPP;

	afspec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg),
				 IFLA_AF_SPEC);
	if (afspec)
		return switchdev_port_br_afspec(dev, afspec,
						switchdev_port_obj_del);

	return 0;
}
EXPORT_SYMBOL_GPL(switchdev_port_bridge_dellink);

/**
 *	switchdev_port_fdb_add - Add FDB (MAC/VLAN) entry to port
 *
 *	@ndmsg: netlink hdr
 *	@nlattr: netlink attributes
 *	@dev: port device
 *	@addr: MAC address to add
 *	@vid: VLAN to add
 *
 *	Add FDB entry to switch device.
 */
int switchdev_port_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			   struct net_device *dev, const unsigned char *addr,
			   u16 vid, u16 nlm_flags)
{
	struct switchdev_obj_port_fdb fdb = {
		.obj.orig_dev = dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_FDB,
		.vid = vid,
	};

	ether_addr_copy(fdb.addr, addr);
	return switchdev_port_obj_add(dev, &fdb.obj);
}
EXPORT_SYMBOL_GPL(switchdev_port_fdb_add);

/**
 *	switchdev_port_fdb_del - Delete FDB (MAC/VLAN) entry from port
 *
 *	@ndmsg: netlink hdr
 *	@nlattr: netlink attributes
 *	@dev: port device
 *	@addr: MAC address to delete
 *	@vid: VLAN to delete
 *
 *	Delete FDB entry from switch device.
 */
int switchdev_port_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
			   struct net_device *dev, const unsigned char *addr,
			   u16 vid)
{
	struct switchdev_obj_port_fdb fdb = {
		.obj.orig_dev = dev,
		.obj.id = SWITCHDEV_OBJ_ID_PORT_FDB,
		.vid = vid,
	};

	ether_addr_copy(fdb.addr, addr);
	return switchdev_port_obj_del(dev, &fdb.obj);
}
EXPORT_SYMBOL_GPL(switchdev_port_fdb_del);

bool switchdev_port_same_parent_id(struct net_device *a,
				   struct net_device *b)
{
	struct switchdev_attr a_attr = {
		.orig_dev = a,
		.id = SWITCHDEV_ATTR_ID_PORT_PARENT_ID,
	};
	struct switchdev_attr b_attr = {
		.orig_dev = b,
		.id = SWITCHDEV_ATTR_ID_PORT_PARENT_ID,
	};

	if (switchdev_port_attr_get(a, &a_attr) ||
	    switchdev_port_attr_get(b, &b_attr))
		return false;

	return netdev_phys_item_id_same(&a_attr.u.ppid, &b_attr.u.ppid);
}
EXPORT_SYMBOL_GPL(switchdev_port_same_parent_id);
