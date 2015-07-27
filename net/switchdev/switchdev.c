/*
 * net/switchdev/switchdev.c - Switch device API
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
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
#include <linux/if_bridge.h>
#include <net/ip_fib.h>
#include <net/switchdev.h>

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
		.id = SWITCHDEV_ATTR_UNDEFINED
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
		if (first.id == SWITCHDEV_ATTR_UNDEFINED)
			first = *attr;
		else if (memcmp(&first, attr, sizeof(*attr)))
			return -ENODATA;
	}

	return err;
}
EXPORT_SYMBOL_GPL(switchdev_port_attr_get);

static int __switchdev_port_attr_set(struct net_device *dev,
				     struct switchdev_attr *attr)
{
	const struct switchdev_ops *ops = dev->switchdev_ops;
	struct net_device *lower_dev;
	struct list_head *iter;
	int err = -EOPNOTSUPP;

	if (ops && ops->switchdev_port_attr_set)
		return ops->switchdev_port_attr_set(dev, attr);

	if (attr->flags & SWITCHDEV_F_NO_RECURSE)
		return err;

	/* Switch device port(s) may be stacked under
	 * bond/team/vlan dev, so recurse down to set attr on
	 * each port.
	 */

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = __switchdev_port_attr_set(lower_dev, attr);
		if (err)
			break;
	}

	return err;
}

struct switchdev_attr_set_work {
	struct work_struct work;
	struct net_device *dev;
	struct switchdev_attr attr;
};

static void switchdev_port_attr_set_work(struct work_struct *work)
{
	struct switchdev_attr_set_work *asw =
		container_of(work, struct switchdev_attr_set_work, work);
	int err;

	rtnl_lock();
	err = switchdev_port_attr_set(asw->dev, &asw->attr);
	if (err && err != -EOPNOTSUPP)
		netdev_err(asw->dev, "failed (err=%d) to set attribute (id=%d)\n",
			   err, asw->attr.id);
	rtnl_unlock();

	dev_put(asw->dev);
	kfree(work);
}

static int switchdev_port_attr_set_defer(struct net_device *dev,
					 struct switchdev_attr *attr)
{
	struct switchdev_attr_set_work *asw;

	asw = kmalloc(sizeof(*asw), GFP_ATOMIC);
	if (!asw)
		return -ENOMEM;

	INIT_WORK(&asw->work, switchdev_port_attr_set_work);

	dev_hold(dev);
	asw->dev = dev;
	memcpy(&asw->attr, attr, sizeof(asw->attr));

	schedule_work(&asw->work);

	return 0;
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
 */
int switchdev_port_attr_set(struct net_device *dev, struct switchdev_attr *attr)
{
	int err;

	if (!rtnl_is_locked()) {
		/* Running prepare-commit transaction across stacked
		 * devices requires nothing moves, so if rtnl_lock is
		 * not held, schedule a worker thread to hold rtnl_lock
		 * while setting attr.
		 */

		return switchdev_port_attr_set_defer(dev, attr);
	}

	/* Phase I: prepare for attr set. Driver/device should fail
	 * here if there are going to be issues in the commit phase,
	 * such as lack of resources or support.  The driver/device
	 * should reserve resources needed for the commit phase here,
	 * but should not commit the attr.
	 */

	attr->trans = SWITCHDEV_TRANS_PREPARE;
	err = __switchdev_port_attr_set(dev, attr);
	if (err) {
		/* Prepare phase failed: abort the transaction.  Any
		 * resources reserved in the prepare phase are
		 * released.
		 */

		if (err != -EOPNOTSUPP) {
			attr->trans = SWITCHDEV_TRANS_ABORT;
			__switchdev_port_attr_set(dev, attr);
		}

		return err;
	}

	/* Phase II: commit attr set.  This cannot fail as a fault
	 * of driver/device.  If it does, it's a bug in the driver/device
	 * because the driver said everythings was OK in phase I.
	 */

	attr->trans = SWITCHDEV_TRANS_COMMIT;
	err = __switchdev_port_attr_set(dev, attr);
	WARN(err, "%s: Commit of attribute (id=%d) failed.\n",
	     dev->name, attr->id);

	return err;
}
EXPORT_SYMBOL_GPL(switchdev_port_attr_set);

static int __switchdev_port_obj_add(struct net_device *dev,
				    struct switchdev_obj *obj)
{
	const struct switchdev_ops *ops = dev->switchdev_ops;
	struct net_device *lower_dev;
	struct list_head *iter;
	int err = -EOPNOTSUPP;

	if (ops && ops->switchdev_port_obj_add)
		return ops->switchdev_port_obj_add(dev, obj);

	/* Switch device port(s) may be stacked under
	 * bond/team/vlan dev, so recurse down to add object on
	 * each port.
	 */

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = __switchdev_port_obj_add(lower_dev, obj);
		if (err)
			break;
	}

	return err;
}

/**
 *	switchdev_port_obj_add - Add port object
 *
 *	@dev: port device
 *	@obj: object to add
 *
 *	Use a 2-phase prepare-commit transaction model to ensure
 *	system is not left in a partially updated state due to
 *	failure from driver/device.
 *
 *	rtnl_lock must be held.
 */
int switchdev_port_obj_add(struct net_device *dev, struct switchdev_obj *obj)
{
	int err;

	ASSERT_RTNL();

	/* Phase I: prepare for obj add. Driver/device should fail
	 * here if there are going to be issues in the commit phase,
	 * such as lack of resources or support.  The driver/device
	 * should reserve resources needed for the commit phase here,
	 * but should not commit the obj.
	 */

	obj->trans = SWITCHDEV_TRANS_PREPARE;
	err = __switchdev_port_obj_add(dev, obj);
	if (err) {
		/* Prepare phase failed: abort the transaction.  Any
		 * resources reserved in the prepare phase are
		 * released.
		 */

		if (err != -EOPNOTSUPP) {
			obj->trans = SWITCHDEV_TRANS_ABORT;
			__switchdev_port_obj_add(dev, obj);
		}

		return err;
	}

	/* Phase II: commit obj add.  This cannot fail as a fault
	 * of driver/device.  If it does, it's a bug in the driver/device
	 * because the driver said everythings was OK in phase I.
	 */

	obj->trans = SWITCHDEV_TRANS_COMMIT;
	err = __switchdev_port_obj_add(dev, obj);
	WARN(err, "%s: Commit of object (id=%d) failed.\n", dev->name, obj->id);

	return err;
}
EXPORT_SYMBOL_GPL(switchdev_port_obj_add);

/**
 *	switchdev_port_obj_del - Delete port object
 *
 *	@dev: port device
 *	@obj: object to delete
 */
int switchdev_port_obj_del(struct net_device *dev, struct switchdev_obj *obj)
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
		err = switchdev_port_obj_del(lower_dev, obj);
		if (err)
			break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(switchdev_port_obj_del);

/**
 *	switchdev_port_obj_dump - Dump port objects
 *
 *	@dev: port device
 *	@obj: object to dump
 */
int switchdev_port_obj_dump(struct net_device *dev, struct switchdev_obj *obj)
{
	const struct switchdev_ops *ops = dev->switchdev_ops;
	struct net_device *lower_dev;
	struct list_head *iter;
	int err = -EOPNOTSUPP;

	if (ops && ops->switchdev_port_obj_dump)
		return ops->switchdev_port_obj_dump(dev, obj);

	/* Switch device port(s) may be stacked under
	 * bond/team/vlan dev, so recurse down to dump objects on
	 * first port at bottom of stack.
	 */

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		err = switchdev_port_obj_dump(lower_dev, obj);
		break;
	}

	return err;
}
EXPORT_SYMBOL_GPL(switchdev_port_obj_dump);

static DEFINE_MUTEX(switchdev_mutex);
static RAW_NOTIFIER_HEAD(switchdev_notif_chain);

/**
 *	register_switchdev_notifier - Register notifier
 *	@nb: notifier_block
 *
 *	Register switch device notifier. This should be used by code
 *	which needs to monitor events happening in particular device.
 *	Return values are same as for atomic_notifier_chain_register().
 */
int register_switchdev_notifier(struct notifier_block *nb)
{
	int err;

	mutex_lock(&switchdev_mutex);
	err = raw_notifier_chain_register(&switchdev_notif_chain, nb);
	mutex_unlock(&switchdev_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(register_switchdev_notifier);

/**
 *	unregister_switchdev_notifier - Unregister notifier
 *	@nb: notifier_block
 *
 *	Unregister switch device notifier.
 *	Return values are same as for atomic_notifier_chain_unregister().
 */
int unregister_switchdev_notifier(struct notifier_block *nb)
{
	int err;

	mutex_lock(&switchdev_mutex);
	err = raw_notifier_chain_unregister(&switchdev_notif_chain, nb);
	mutex_unlock(&switchdev_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(unregister_switchdev_notifier);

/**
 *	call_switchdev_notifiers - Call notifiers
 *	@val: value passed unmodified to notifier function
 *	@dev: port device
 *	@info: notifier information data
 *
 *	Call all network notifier blocks. This should be called by driver
 *	when it needs to propagate hardware event.
 *	Return values are same as for atomic_notifier_call_chain().
 */
int call_switchdev_notifiers(unsigned long val, struct net_device *dev,
			     struct switchdev_notifier_info *info)
{
	int err;

	info->dev = dev;
	mutex_lock(&switchdev_mutex);
	err = raw_notifier_call_chain(&switchdev_notif_chain, val, info);
	mutex_unlock(&switchdev_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(call_switchdev_notifiers);

struct switchdev_vlan_dump {
	struct switchdev_obj obj;
	struct sk_buff *skb;
	u32 filter_mask;
	u16 flags;
	u16 begin;
	u16 end;
};

static int switchdev_port_vlan_dump_put(struct net_device *dev,
					struct switchdev_vlan_dump *dump)
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

static int switchdev_port_vlan_dump_cb(struct net_device *dev,
				       struct switchdev_obj *obj)
{
	struct switchdev_vlan_dump *dump =
		container_of(obj, struct switchdev_vlan_dump, obj);
	struct switchdev_obj_vlan *vlan = &dump->obj.u.vlan;
	int err = 0;

	if (vlan->vid_begin > vlan->vid_end)
		return -EINVAL;

	if (dump->filter_mask & RTEXT_FILTER_BRVLAN) {
		dump->flags = vlan->flags;
		for (dump->begin = dump->end = vlan->vid_begin;
		     dump->begin <= vlan->vid_end;
		     dump->begin++, dump->end++) {
			err = switchdev_port_vlan_dump_put(dev, dump);
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
				err = switchdev_port_vlan_dump_put(dev, dump);
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
				err = switchdev_port_vlan_dump_put(dev, dump);
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
		.obj = {
			.id = SWITCHDEV_OBJ_PORT_VLAN,
			.cb = switchdev_port_vlan_dump_cb,
		},
		.skb = skb,
		.filter_mask = filter_mask,
	};
	int err = 0;

	if ((filter_mask & RTEXT_FILTER_BRVLAN) ||
	    (filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED)) {
		err = switchdev_port_obj_dump(dev, &dump.obj);
		if (err)
			goto err_out;
		if (filter_mask & RTEXT_FILTER_BRVLAN_COMPRESSED)
			/* last one */
			err = switchdev_port_vlan_dump_put(dev, &dump);
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
		.id = SWITCHDEV_ATTR_PORT_BRIDGE_FLAGS,
	};
	u16 mode = BRIDGE_MODE_UNDEF;
	u32 mask = BR_LEARNING | BR_LEARNING_SYNC;
	int err;

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
		.id = SWITCHDEV_ATTR_PORT_BRIDGE_FLAGS,
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
				  switchdev_port_bridge_policy);
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
					     struct switchdev_obj *obj))
{
	struct nlattr *attr;
	struct bridge_vlan_info *vinfo;
	struct switchdev_obj obj = {
		.id = SWITCHDEV_OBJ_PORT_VLAN,
	};
	struct switchdev_obj_vlan *vlan = &obj.u.vlan;
	int rem;
	int err;

	nla_for_each_nested(attr, afspec, rem) {
		if (nla_type(attr) != IFLA_BRIDGE_VLAN_INFO)
			continue;
		if (nla_len(attr) != sizeof(struct bridge_vlan_info))
			return -EINVAL;
		vinfo = nla_data(attr);
		vlan->flags = vinfo->flags;
		if (vinfo->flags & BRIDGE_VLAN_INFO_RANGE_BEGIN) {
			if (vlan->vid_begin)
				return -EINVAL;
			vlan->vid_begin = vinfo->vid;
		} else if (vinfo->flags & BRIDGE_VLAN_INFO_RANGE_END) {
			if (!vlan->vid_begin)
				return -EINVAL;
			vlan->vid_end = vinfo->vid;
			if (vlan->vid_end <= vlan->vid_begin)
				return -EINVAL;
			err = f(dev, &obj);
			if (err)
				return err;
			memset(vlan, 0, sizeof(*vlan));
		} else {
			if (vlan->vid_begin)
				return -EINVAL;
			vlan->vid_begin = vinfo->vid;
			vlan->vid_end = vinfo->vid;
			err = f(dev, &obj);
			if (err)
				return err;
			memset(vlan, 0, sizeof(*vlan));
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
	struct switchdev_obj obj = {
		.id = SWITCHDEV_OBJ_PORT_FDB,
		.u.fdb = {
			.addr = addr,
			.vid = vid,
		},
	};

	return switchdev_port_obj_add(dev, &obj);
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
	struct switchdev_obj obj = {
		.id = SWITCHDEV_OBJ_PORT_FDB,
		.u.fdb = {
			.addr = addr,
			.vid = vid,
		},
	};

	return switchdev_port_obj_del(dev, &obj);
}
EXPORT_SYMBOL_GPL(switchdev_port_fdb_del);

struct switchdev_fdb_dump {
	struct switchdev_obj obj;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	int idx;
};

static int switchdev_port_fdb_dump_cb(struct net_device *dev,
				      struct switchdev_obj *obj)
{
	struct switchdev_fdb_dump *dump =
		container_of(obj, struct switchdev_fdb_dump, obj);
	u32 portid = NETLINK_CB(dump->cb->skb).portid;
	u32 seq = dump->cb->nlh->nlmsg_seq;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	if (dump->idx < dump->cb->args[0])
		goto skip;

	nlh = nlmsg_put(dump->skb, portid, seq, RTM_NEWNEIGH,
			sizeof(*ndm), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family  = AF_BRIDGE;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags   = NTF_SELF;
	ndm->ndm_type    = 0;
	ndm->ndm_ifindex = dev->ifindex;
	ndm->ndm_state   = NUD_REACHABLE;

	if (nla_put(dump->skb, NDA_LLADDR, ETH_ALEN, obj->u.fdb.addr))
		goto nla_put_failure;

	if (obj->u.fdb.vid && nla_put_u16(dump->skb, NDA_VLAN, obj->u.fdb.vid))
		goto nla_put_failure;

	nlmsg_end(dump->skb, nlh);

skip:
	dump->idx++;
	return 0;

nla_put_failure:
	nlmsg_cancel(dump->skb, nlh);
	return -EMSGSIZE;
}

/**
 *	switchdev_port_fdb_dump - Dump port FDB (MAC/VLAN) entries
 *
 *	@skb: netlink skb
 *	@cb: netlink callback
 *	@dev: port device
 *	@filter_dev: filter device
 *	@idx:
 *
 *	Delete FDB entry from switch device.
 */
int switchdev_port_fdb_dump(struct sk_buff *skb, struct netlink_callback *cb,
			    struct net_device *dev,
			    struct net_device *filter_dev, int idx)
{
	struct switchdev_fdb_dump dump = {
		.obj = {
			.id = SWITCHDEV_OBJ_PORT_FDB,
			.cb = switchdev_port_fdb_dump_cb,
		},
		.skb = skb,
		.cb = cb,
		.idx = idx,
	};
	int err;

	err = switchdev_port_obj_dump(dev, &dump.obj);
	if (err)
		return err;

	return dump.idx;
}
EXPORT_SYMBOL_GPL(switchdev_port_fdb_dump);

static struct net_device *switchdev_get_lowest_dev(struct net_device *dev)
{
	const struct switchdev_ops *ops = dev->switchdev_ops;
	struct net_device *lower_dev;
	struct net_device *port_dev;
	struct list_head *iter;

	/* Recusively search down until we find a sw port dev.
	 * (A sw port dev supports switchdev_port_attr_get).
	 */

	if (ops && ops->switchdev_port_attr_get)
		return dev;

	netdev_for_each_lower_dev(dev, lower_dev, iter) {
		port_dev = switchdev_get_lowest_dev(lower_dev);
		if (port_dev)
			return port_dev;
	}

	return NULL;
}

static struct net_device *switchdev_get_dev_by_nhs(struct fib_info *fi)
{
	struct switchdev_attr attr = {
		.id = SWITCHDEV_ATTR_PORT_PARENT_ID,
	};
	struct switchdev_attr prev_attr;
	struct net_device *dev = NULL;
	int nhsel;

	/* For this route, all nexthop devs must be on the same switch. */

	for (nhsel = 0; nhsel < fi->fib_nhs; nhsel++) {
		const struct fib_nh *nh = &fi->fib_nh[nhsel];

		if (!nh->nh_dev)
			return NULL;

		dev = switchdev_get_lowest_dev(nh->nh_dev);
		if (!dev)
			return NULL;

		if (switchdev_port_attr_get(dev, &attr))
			return NULL;

		if (nhsel > 0) {
			if (prev_attr.u.ppid.id_len != attr.u.ppid.id_len)
				return NULL;
			if (memcmp(prev_attr.u.ppid.id, attr.u.ppid.id,
				   attr.u.ppid.id_len))
				return NULL;
		}

		prev_attr = attr;
	}

	return dev;
}

/**
 *	switchdev_fib_ipv4_add - Add/modify switch IPv4 route entry
 *
 *	@dst: route's IPv4 destination address
 *	@dst_len: destination address length (prefix length)
 *	@fi: route FIB info structure
 *	@tos: route TOS
 *	@type: route type
 *	@nlflags: netlink flags passed in (NLM_F_*)
 *	@tb_id: route table ID
 *
 *	Add/modify switch IPv4 route entry.
 */
int switchdev_fib_ipv4_add(u32 dst, int dst_len, struct fib_info *fi,
			   u8 tos, u8 type, u32 nlflags, u32 tb_id)
{
	struct switchdev_obj fib_obj = {
		.id = SWITCHDEV_OBJ_IPV4_FIB,
		.u.ipv4_fib = {
			.dst = dst,
			.dst_len = dst_len,
			.fi = fi,
			.tos = tos,
			.type = type,
			.nlflags = nlflags,
			.tb_id = tb_id,
		},
	};
	struct net_device *dev;
	int err = 0;

	/* Don't offload route if using custom ip rules or if
	 * IPv4 FIB offloading has been disabled completely.
	 */

#ifdef CONFIG_IP_MULTIPLE_TABLES
	if (fi->fib_net->ipv4.fib_has_custom_rules)
		return 0;
#endif

	if (fi->fib_net->ipv4.fib_offload_disabled)
		return 0;

	dev = switchdev_get_dev_by_nhs(fi);
	if (!dev)
		return 0;

	err = switchdev_port_obj_add(dev, &fib_obj);
	if (!err)
		fi->fib_flags |= RTNH_F_OFFLOAD;

	return err == -EOPNOTSUPP ? 0 : err;
}
EXPORT_SYMBOL_GPL(switchdev_fib_ipv4_add);

/**
 *	switchdev_fib_ipv4_del - Delete IPv4 route entry from switch
 *
 *	@dst: route's IPv4 destination address
 *	@dst_len: destination address length (prefix length)
 *	@fi: route FIB info structure
 *	@tos: route TOS
 *	@type: route type
 *	@tb_id: route table ID
 *
 *	Delete IPv4 route entry from switch device.
 */
int switchdev_fib_ipv4_del(u32 dst, int dst_len, struct fib_info *fi,
			   u8 tos, u8 type, u32 tb_id)
{
	struct switchdev_obj fib_obj = {
		.id = SWITCHDEV_OBJ_IPV4_FIB,
		.u.ipv4_fib = {
			.dst = dst,
			.dst_len = dst_len,
			.fi = fi,
			.tos = tos,
			.type = type,
			.nlflags = 0,
			.tb_id = tb_id,
		},
	};
	struct net_device *dev;
	int err = 0;

	if (!(fi->fib_flags & RTNH_F_OFFLOAD))
		return 0;

	dev = switchdev_get_dev_by_nhs(fi);
	if (!dev)
		return 0;

	err = switchdev_port_obj_del(dev, &fib_obj);
	if (!err)
		fi->fib_flags &= ~RTNH_F_OFFLOAD;

	return err == -EOPNOTSUPP ? 0 : err;
}
EXPORT_SYMBOL_GPL(switchdev_fib_ipv4_del);

/**
 *	switchdev_fib_ipv4_abort - Abort an IPv4 FIB operation
 *
 *	@fi: route FIB info structure
 */
void switchdev_fib_ipv4_abort(struct fib_info *fi)
{
	/* There was a problem installing this route to the offload
	 * device.  For now, until we come up with more refined
	 * policy handling, abruptly end IPv4 fib offloading for
	 * for entire net by flushing offload device(s) of all
	 * IPv4 routes, and mark IPv4 fib offloading broken from
	 * this point forward.
	 */

	fib_flush_external(fi->fib_net);
	fi->fib_net->ipv4.fib_offload_disabled = true;
}
EXPORT_SYMBOL_GPL(switchdev_fib_ipv4_abort);
