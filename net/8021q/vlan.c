/*
 * INET		802.1Q VLAN
 *		Ethernet-type device handling.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *              Please send support related email to: netdev@vger.kernel.org
 *              VLAN Home Page: http://www.candelatech.com/~greear/vlan.html
 *
 * Fixes:
 *              Fix for packet capture - Nick Eggleston <nick@dccinc.com>;
 *		Add HW acceleration hooks - David S. Miller <davem@redhat.com>;
 *		Correct all the locking - David S. Miller <davem@redhat.com>;
 *		Use hash table for VLAN groups - David S. Miller <davem@redhat.com>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rculist.h>
#include <net/p8022.h>
#include <net/arp.h>
#include <linux/rtnetlink.h>
#include <linux/notifier.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <asm/uaccess.h>

#include <linux/if_vlan.h>
#include "vlan.h"
#include "vlanproc.h"

#define DRV_VERSION "1.8"

/* Global VLAN variables */

int vlan_net_id __read_mostly;

const char vlan_fullname[] = "802.1Q VLAN Support";
const char vlan_version[] = DRV_VERSION;

/* End of global variables definitions. */

static void vlan_group_free(struct vlan_group *grp)
{
	int i;

	for (i = 0; i < VLAN_GROUP_ARRAY_SPLIT_PARTS; i++)
		kfree(grp->vlan_devices_arrays[i]);
	kfree(grp);
}

static struct vlan_group *vlan_group_alloc(struct net_device *real_dev)
{
	struct vlan_group *grp;

	grp = kzalloc(sizeof(struct vlan_group), GFP_KERNEL);
	if (!grp)
		return NULL;

	grp->real_dev = real_dev;
	return grp;
}

static int vlan_group_prealloc_vid(struct vlan_group *vg, u16 vlan_id)
{
	struct net_device **array;
	unsigned int size;

	ASSERT_RTNL();

	array = vg->vlan_devices_arrays[vlan_id / VLAN_GROUP_ARRAY_PART_LEN];
	if (array != NULL)
		return 0;

	size = sizeof(struct net_device *) * VLAN_GROUP_ARRAY_PART_LEN;
	array = kzalloc(size, GFP_KERNEL);
	if (array == NULL)
		return -ENOBUFS;

	vg->vlan_devices_arrays[vlan_id / VLAN_GROUP_ARRAY_PART_LEN] = array;
	return 0;
}

static void vlan_rcu_free(struct rcu_head *rcu)
{
	vlan_group_free(container_of(rcu, struct vlan_group, rcu));
}

void unregister_vlan_dev(struct net_device *dev, struct list_head *head)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
	struct net_device *real_dev = vlan->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	struct vlan_group *grp;
	u16 vlan_id = vlan->vlan_id;

	ASSERT_RTNL();

	grp = rtnl_dereference(real_dev->vlgrp);
	BUG_ON(!grp);

	/* Take it out of our own structures, but be sure to interlock with
	 * HW accelerating devices or SW vlan input packet processing if
	 * VLAN is not 0 (leave it there for 802.1p).
	 */
	if (vlan_id && (real_dev->features & NETIF_F_HW_VLAN_FILTER))
		ops->ndo_vlan_rx_kill_vid(real_dev, vlan_id);

	grp->nr_vlans--;

	if (vlan->flags & VLAN_FLAG_GVRP)
		vlan_gvrp_request_leave(dev);

	vlan_group_set_device(grp, vlan_id, NULL);
	/* Because unregister_netdevice_queue() makes sure at least one rcu
	 * grace period is respected before device freeing,
	 * we dont need to call synchronize_net() here.
	 */
	unregister_netdevice_queue(dev, head);

	/* If the group is now empty, kill off the group. */
	if (grp->nr_vlans == 0) {
		vlan_gvrp_uninit_applicant(real_dev);

		RCU_INIT_POINTER(real_dev->vlgrp, NULL);

		/* Free the group, after all cpu's are done. */
		call_rcu(&grp->rcu, vlan_rcu_free);
	}

	/* Get rid of the vlan's reference to real_dev */
	dev_put(real_dev);
}

int vlan_check_real_dev(struct net_device *real_dev, u16 vlan_id)
{
	const char *name = real_dev->name;
	const struct net_device_ops *ops = real_dev->netdev_ops;

	if (real_dev->features & NETIF_F_VLAN_CHALLENGED) {
		pr_info("VLANs not supported on %s\n", name);
		return -EOPNOTSUPP;
	}

	if ((real_dev->features & NETIF_F_HW_VLAN_FILTER) &&
	    (!ops->ndo_vlan_rx_add_vid || !ops->ndo_vlan_rx_kill_vid)) {
		pr_info("Device %s has buggy VLAN hw accel\n", name);
		return -EOPNOTSUPP;
	}

	if (vlan_find_dev(real_dev, vlan_id) != NULL)
		return -EEXIST;

	return 0;
}

int register_vlan_dev(struct net_device *dev)
{
	struct vlan_dev_info *vlan = vlan_dev_info(dev);
	struct net_device *real_dev = vlan->real_dev;
	const struct net_device_ops *ops = real_dev->netdev_ops;
	u16 vlan_id = vlan->vlan_id;
	struct vlan_group *grp, *ngrp = NULL;
	int err;

	grp = rtnl_dereference(real_dev->vlgrp);
	if (!grp) {
		ngrp = grp = vlan_group_alloc(real_dev);
		if (!grp)
			return -ENOBUFS;
		err = vlan_gvrp_init_applicant(real_dev);
		if (err < 0)
			goto out_free_group;
	}

	err = vlan_group_prealloc_vid(grp, vlan_id);
	if (err < 0)
		goto out_uninit_applicant;

	err = register_netdevice(dev);
	if (err < 0)
		goto out_uninit_applicant;

	/* Account for reference in struct vlan_dev_info */
	dev_hold(real_dev);

	netif_stacked_transfer_operstate(real_dev, dev);
	linkwatch_fire_event(dev); /* _MUST_ call rfc2863_policy() */

	/* So, got the sucker initialized, now lets place
	 * it into our local structure.
	 */
	vlan_group_set_device(grp, vlan_id, dev);
	grp->nr_vlans++;

	if (ngrp) {
		rcu_assign_pointer(real_dev->vlgrp, ngrp);
	}
	if (real_dev->features & NETIF_F_HW_VLAN_FILTER)
		ops->ndo_vlan_rx_add_vid(real_dev, vlan_id);

	return 0;

out_uninit_applicant:
	if (ngrp)
		vlan_gvrp_uninit_applicant(real_dev);
out_free_group:
	if (ngrp) {
		/* Free the group, after all cpu's are done. */
		call_rcu(&ngrp->rcu, vlan_rcu_free);
	}
	return err;
}

/*  Attach a VLAN device to a mac address (ie Ethernet Card).
 *  Returns 0 if the device was created or a negative error code otherwise.
 */
static int register_vlan_device(struct net_device *real_dev, u16 vlan_id)
{
	struct net_device *new_dev;
	struct net *net = dev_net(real_dev);
	struct vlan_net *vn = net_generic(net, vlan_net_id);
	char name[IFNAMSIZ];
	int err;

	if (vlan_id >= VLAN_VID_MASK)
		return -ERANGE;

	err = vlan_check_real_dev(real_dev, vlan_id);
	if (err < 0)
		return err;

	/* Gotta set up the fields for the device. */
	switch (vn->name_type) {
	case VLAN_NAME_TYPE_RAW_PLUS_VID:
		/* name will look like:	 eth1.0005 */
		snprintf(name, IFNAMSIZ, "%s.%.4i", real_dev->name, vlan_id);
		break;
	case VLAN_NAME_TYPE_PLUS_VID_NO_PAD:
		/* Put our vlan.VID in the name.
		 * Name will look like:	 vlan5
		 */
		snprintf(name, IFNAMSIZ, "vlan%i", vlan_id);
		break;
	case VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD:
		/* Put our vlan.VID in the name.
		 * Name will look like:	 eth0.5
		 */
		snprintf(name, IFNAMSIZ, "%s.%i", real_dev->name, vlan_id);
		break;
	case VLAN_NAME_TYPE_PLUS_VID:
		/* Put our vlan.VID in the name.
		 * Name will look like:	 vlan0005
		 */
	default:
		snprintf(name, IFNAMSIZ, "vlan%.4i", vlan_id);
	}

	new_dev = alloc_netdev(sizeof(struct vlan_dev_info), name, vlan_setup);

	if (new_dev == NULL)
		return -ENOBUFS;

	dev_net_set(new_dev, net);
	/* need 4 bytes for extra VLAN header info,
	 * hope the underlying device can handle it.
	 */
	new_dev->mtu = real_dev->mtu;

	vlan_dev_info(new_dev)->vlan_id = vlan_id;
	vlan_dev_info(new_dev)->real_dev = real_dev;
	vlan_dev_info(new_dev)->dent = NULL;
	vlan_dev_info(new_dev)->flags = VLAN_FLAG_REORDER_HDR;

	new_dev->rtnl_link_ops = &vlan_link_ops;
	err = register_vlan_dev(new_dev);
	if (err < 0)
		goto out_free_newdev;

	return 0;

out_free_newdev:
	free_netdev(new_dev);
	return err;
}

static void vlan_sync_address(struct net_device *dev,
			      struct net_device *vlandev)
{
	struct vlan_dev_info *vlan = vlan_dev_info(vlandev);

	/* May be called without an actual change */
	if (!compare_ether_addr(vlan->real_dev_addr, dev->dev_addr))
		return;

	/* vlan address was different from the old address and is equal to
	 * the new address */
	if (compare_ether_addr(vlandev->dev_addr, vlan->real_dev_addr) &&
	    !compare_ether_addr(vlandev->dev_addr, dev->dev_addr))
		dev_uc_del(dev, vlandev->dev_addr);

	/* vlan address was equal to the old address and is different from
	 * the new address */
	if (!compare_ether_addr(vlandev->dev_addr, vlan->real_dev_addr) &&
	    compare_ether_addr(vlandev->dev_addr, dev->dev_addr))
		dev_uc_add(dev, vlandev->dev_addr);

	memcpy(vlan->real_dev_addr, dev->dev_addr, ETH_ALEN);
}

static void vlan_transfer_features(struct net_device *dev,
				   struct net_device *vlandev)
{
	vlandev->gso_max_size = dev->gso_max_size;

	if (dev->features & NETIF_F_HW_VLAN_TX)
		vlandev->hard_header_len = dev->hard_header_len;
	else
		vlandev->hard_header_len = dev->hard_header_len + VLAN_HLEN;

#if defined(CONFIG_FCOE) || defined(CONFIG_FCOE_MODULE)
	vlandev->fcoe_ddp_xid = dev->fcoe_ddp_xid;
#endif

	netdev_update_features(vlandev);
}

static void __vlan_device_event(struct net_device *dev, unsigned long event)
{
	switch (event) {
	case NETDEV_CHANGENAME:
		vlan_proc_rem_dev(dev);
		if (vlan_proc_add_dev(dev) < 0)
			pr_warn("failed to change proc name for %s\n",
				dev->name);
		break;
	case NETDEV_REGISTER:
		if (vlan_proc_add_dev(dev) < 0)
			pr_warn("failed to add proc entry for %s\n", dev->name);
		break;
	case NETDEV_UNREGISTER:
		vlan_proc_rem_dev(dev);
		break;
	}
}

static int vlan_device_event(struct notifier_block *unused, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = ptr;
	struct vlan_group *grp;
	int i, flgs;
	struct net_device *vlandev;
	struct vlan_dev_info *vlan;
	LIST_HEAD(list);

	if (is_vlan_dev(dev))
		__vlan_device_event(dev, event);

	if ((event == NETDEV_UP) &&
	    (dev->features & NETIF_F_HW_VLAN_FILTER) &&
	    dev->netdev_ops->ndo_vlan_rx_add_vid) {
		pr_info("adding VLAN 0 to HW filter on device %s\n",
			dev->name);
		dev->netdev_ops->ndo_vlan_rx_add_vid(dev, 0);
	}

	grp = rtnl_dereference(dev->vlgrp);
	if (!grp)
		goto out;

	/* It is OK that we do not hold the group lock right now,
	 * as we run under the RTNL lock.
	 */

	switch (event) {
	case NETDEV_CHANGE:
		/* Propagate real device state to vlan devices */
		for (i = 0; i < VLAN_N_VID; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			netif_stacked_transfer_operstate(dev, vlandev);
		}
		break;

	case NETDEV_CHANGEADDR:
		/* Adjust unicast filters on underlying device */
		for (i = 0; i < VLAN_N_VID; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			flgs = vlandev->flags;
			if (!(flgs & IFF_UP))
				continue;

			vlan_sync_address(dev, vlandev);
		}
		break;

	case NETDEV_CHANGEMTU:
		for (i = 0; i < VLAN_N_VID; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			if (vlandev->mtu <= dev->mtu)
				continue;

			dev_set_mtu(vlandev, dev->mtu);
		}
		break;

	case NETDEV_FEAT_CHANGE:
		/* Propagate device features to underlying device */
		for (i = 0; i < VLAN_N_VID; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			vlan_transfer_features(dev, vlandev);
		}

		break;

	case NETDEV_DOWN:
		/* Put all VLANs for this dev in the down state too.  */
		for (i = 0; i < VLAN_N_VID; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			flgs = vlandev->flags;
			if (!(flgs & IFF_UP))
				continue;

			vlan = vlan_dev_info(vlandev);
			if (!(vlan->flags & VLAN_FLAG_LOOSE_BINDING))
				dev_change_flags(vlandev, flgs & ~IFF_UP);
			netif_stacked_transfer_operstate(dev, vlandev);
		}
		break;

	case NETDEV_UP:
		/* Put all VLANs for this dev in the up state too.  */
		for (i = 0; i < VLAN_N_VID; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			flgs = vlandev->flags;
			if (flgs & IFF_UP)
				continue;

			vlan = vlan_dev_info(vlandev);
			if (!(vlan->flags & VLAN_FLAG_LOOSE_BINDING))
				dev_change_flags(vlandev, flgs | IFF_UP);
			netif_stacked_transfer_operstate(dev, vlandev);
		}
		break;

	case NETDEV_UNREGISTER:
		/* twiddle thumbs on netns device moves */
		if (dev->reg_state != NETREG_UNREGISTERING)
			break;

		for (i = 0; i < VLAN_N_VID; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			/* unregistration of last vlan destroys group, abort
			 * afterwards */
			if (grp->nr_vlans == 1)
				i = VLAN_N_VID;

			unregister_vlan_dev(vlandev, &list);
		}
		unregister_netdevice_many(&list);
		break;

	case NETDEV_PRE_TYPE_CHANGE:
		/* Forbid underlaying device to change its type. */
		return NOTIFY_BAD;

	case NETDEV_NOTIFY_PEERS:
	case NETDEV_BONDING_FAILOVER:
		/* Propagate to vlan devices */
		for (i = 0; i < VLAN_N_VID; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			call_netdevice_notifiers(event, vlandev);
		}
		break;
	}

out:
	return NOTIFY_DONE;
}

static struct notifier_block vlan_notifier_block __read_mostly = {
	.notifier_call = vlan_device_event,
};

/*
 *	VLAN IOCTL handler.
 *	o execute requested action or pass command to the device driver
 *   arg is really a struct vlan_ioctl_args __user *.
 */
static int vlan_ioctl_handler(struct net *net, void __user *arg)
{
	int err;
	struct vlan_ioctl_args args;
	struct net_device *dev = NULL;

	if (copy_from_user(&args, arg, sizeof(struct vlan_ioctl_args)))
		return -EFAULT;

	/* Null terminate this sucker, just in case. */
	args.device1[23] = 0;
	args.u.device2[23] = 0;

	rtnl_lock();

	switch (args.cmd) {
	case SET_VLAN_INGRESS_PRIORITY_CMD:
	case SET_VLAN_EGRESS_PRIORITY_CMD:
	case SET_VLAN_FLAG_CMD:
	case ADD_VLAN_CMD:
	case DEL_VLAN_CMD:
	case GET_VLAN_REALDEV_NAME_CMD:
	case GET_VLAN_VID_CMD:
		err = -ENODEV;
		dev = __dev_get_by_name(net, args.device1);
		if (!dev)
			goto out;

		err = -EINVAL;
		if (args.cmd != ADD_VLAN_CMD && !is_vlan_dev(dev))
			goto out;
	}

	switch (args.cmd) {
	case SET_VLAN_INGRESS_PRIORITY_CMD:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		vlan_dev_set_ingress_priority(dev,
					      args.u.skb_priority,
					      args.vlan_qos);
		err = 0;
		break;

	case SET_VLAN_EGRESS_PRIORITY_CMD:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		err = vlan_dev_set_egress_priority(dev,
						   args.u.skb_priority,
						   args.vlan_qos);
		break;

	case SET_VLAN_FLAG_CMD:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		err = vlan_dev_change_flags(dev,
					    args.vlan_qos ? args.u.flag : 0,
					    args.u.flag);
		break;

	case SET_VLAN_NAME_TYPE_CMD:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		if ((args.u.name_type >= 0) &&
		    (args.u.name_type < VLAN_NAME_TYPE_HIGHEST)) {
			struct vlan_net *vn;

			vn = net_generic(net, vlan_net_id);
			vn->name_type = args.u.name_type;
			err = 0;
		} else {
			err = -EINVAL;
		}
		break;

	case ADD_VLAN_CMD:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		err = register_vlan_device(dev, args.u.VID);
		break;

	case DEL_VLAN_CMD:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			break;
		unregister_vlan_dev(dev, NULL);
		err = 0;
		break;

	case GET_VLAN_REALDEV_NAME_CMD:
		err = 0;
		vlan_dev_get_realdev_name(dev, args.u.device2);
		if (copy_to_user(arg, &args,
				 sizeof(struct vlan_ioctl_args)))
			err = -EFAULT;
		break;

	case GET_VLAN_VID_CMD:
		err = 0;
		args.u.VID = vlan_dev_vlan_id(dev);
		if (copy_to_user(arg, &args,
				 sizeof(struct vlan_ioctl_args)))
		      err = -EFAULT;
		break;

	default:
		err = -EOPNOTSUPP;
		break;
	}
out:
	rtnl_unlock();
	return err;
}

static int __net_init vlan_init_net(struct net *net)
{
	struct vlan_net *vn = net_generic(net, vlan_net_id);
	int err;

	vn->name_type = VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD;

	err = vlan_proc_init(net);

	return err;
}

static void __net_exit vlan_exit_net(struct net *net)
{
	vlan_proc_cleanup(net);
}

static struct pernet_operations vlan_net_ops = {
	.init = vlan_init_net,
	.exit = vlan_exit_net,
	.id   = &vlan_net_id,
	.size = sizeof(struct vlan_net),
};

static int __init vlan_proto_init(void)
{
	int err;

	pr_info("%s v%s\n", vlan_fullname, vlan_version);

	err = register_pernet_subsys(&vlan_net_ops);
	if (err < 0)
		goto err0;

	err = register_netdevice_notifier(&vlan_notifier_block);
	if (err < 0)
		goto err2;

	err = vlan_gvrp_init();
	if (err < 0)
		goto err3;

	err = vlan_netlink_init();
	if (err < 0)
		goto err4;

	vlan_ioctl_set(vlan_ioctl_handler);
	return 0;

err4:
	vlan_gvrp_uninit();
err3:
	unregister_netdevice_notifier(&vlan_notifier_block);
err2:
	unregister_pernet_subsys(&vlan_net_ops);
err0:
	return err;
}

static void __exit vlan_cleanup_module(void)
{
	vlan_ioctl_set(NULL);
	vlan_netlink_fini();

	unregister_netdevice_notifier(&vlan_notifier_block);

	unregister_pernet_subsys(&vlan_net_ops);
	rcu_barrier(); /* Wait for completion of call_rcu()'s */

	vlan_gvrp_uninit();
}

module_init(vlan_proto_init);
module_exit(vlan_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
