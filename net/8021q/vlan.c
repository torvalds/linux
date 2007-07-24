/*
 * INET		802.1Q VLAN
 *		Ethernet-type device handling.
 *
 * Authors:	Ben Greear <greearb@candelatech.com>
 *              Please send support related email to: vlan@scry.wanfear.com
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

#include <asm/uaccess.h> /* for copy_from_user */
#include <linux/capability.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/datalink.h>
#include <linux/mm.h>
#include <linux/in.h>
#include <linux/init.h>
#include <net/p8022.h>
#include <net/arp.h>
#include <linux/rtnetlink.h>
#include <linux/notifier.h>

#include <linux/if_vlan.h>
#include "vlan.h"
#include "vlanproc.h"

#define DRV_VERSION "1.8"

/* Global VLAN variables */

/* Our listing of VLAN group(s) */
static struct hlist_head vlan_group_hash[VLAN_GRP_HASH_SIZE];
#define vlan_grp_hashfn(IDX)	((((IDX) >> VLAN_GRP_HASH_SHIFT) ^ (IDX)) & VLAN_GRP_HASH_MASK)

static char vlan_fullname[] = "802.1Q VLAN Support";
static char vlan_version[] = DRV_VERSION;
static char vlan_copyright[] = "Ben Greear <greearb@candelatech.com>";
static char vlan_buggyright[] = "David S. Miller <davem@redhat.com>";

static int vlan_device_event(struct notifier_block *, unsigned long, void *);
static int vlan_ioctl_handler(void __user *);
static int unregister_vlan_dev(struct net_device *, unsigned short );

static struct notifier_block vlan_notifier_block = {
	.notifier_call = vlan_device_event,
};

/* These may be changed at run-time through IOCTLs */

/* Determines interface naming scheme. */
unsigned short vlan_name_type = VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD;

static struct packet_type vlan_packet_type = {
	.type = __constant_htons(ETH_P_8021Q),
	.func = vlan_skb_recv, /* VLAN receive method */
};

/* End of global variables definitions. */

/*
 * Function vlan_proto_init (pro)
 *
 *    Initialize VLAN protocol layer,
 *
 */
static int __init vlan_proto_init(void)
{
	int err;

	printk(VLAN_INF "%s v%s %s\n",
	       vlan_fullname, vlan_version, vlan_copyright);
	printk(VLAN_INF "All bugs added by %s\n",
	       vlan_buggyright);

	/* proc file system initialization */
	err = vlan_proc_init();
	if (err < 0) {
		printk(KERN_ERR
		       "%s %s: can't create entry in proc filesystem!\n",
		       __FUNCTION__, VLAN_NAME);
		return err;
	}

	dev_add_pack(&vlan_packet_type);

	/* Register us to receive netdevice events */
	err = register_netdevice_notifier(&vlan_notifier_block);
	if (err < 0)
		goto err1;

	err = vlan_netlink_init();
	if (err < 0)
		goto err2;

	vlan_ioctl_set(vlan_ioctl_handler);
	return 0;

err2:
	unregister_netdevice_notifier(&vlan_notifier_block);
err1:
	vlan_proc_cleanup();
	dev_remove_pack(&vlan_packet_type);
	return err;
}

/*
 *     Module 'remove' entry point.
 *     o delete /proc/net/router directory and static entries.
 */
static void __exit vlan_cleanup_module(void)
{
	int i;

	vlan_netlink_fini();
	vlan_ioctl_set(NULL);

	/* Un-register us from receiving netdevice events */
	unregister_netdevice_notifier(&vlan_notifier_block);

	dev_remove_pack(&vlan_packet_type);

	/* This table must be empty if there are no module
	 * references left.
	 */
	for (i = 0; i < VLAN_GRP_HASH_SIZE; i++) {
		BUG_ON(!hlist_empty(&vlan_group_hash[i]));
	}
	vlan_proc_cleanup();

	synchronize_net();
}

module_init(vlan_proto_init);
module_exit(vlan_cleanup_module);

/* Must be invoked with RCU read lock (no preempt) */
static struct vlan_group *__vlan_find_group(int real_dev_ifindex)
{
	struct vlan_group *grp;
	struct hlist_node *n;
	int hash = vlan_grp_hashfn(real_dev_ifindex);

	hlist_for_each_entry_rcu(grp, n, &vlan_group_hash[hash], hlist) {
		if (grp->real_dev_ifindex == real_dev_ifindex)
			return grp;
	}

	return NULL;
}

/*  Find the protocol handler.  Assumes VID < VLAN_VID_MASK.
 *
 * Must be invoked with RCU read lock (no preempt)
 */
struct net_device *__find_vlan_dev(struct net_device *real_dev,
				   unsigned short VID)
{
	struct vlan_group *grp = __vlan_find_group(real_dev->ifindex);

	if (grp)
		return vlan_group_get_device(grp, VID);

	return NULL;
}

static void vlan_group_free(struct vlan_group *grp)
{
	int i;

	for (i=0; i < VLAN_GROUP_ARRAY_SPLIT_PARTS; i++)
		kfree(grp->vlan_devices_arrays[i]);
	kfree(grp);
}

static struct vlan_group *vlan_group_alloc(int ifindex)
{
	struct vlan_group *grp;
	unsigned int size;
	unsigned int i;

	grp = kzalloc(sizeof(struct vlan_group), GFP_KERNEL);
	if (!grp)
		return NULL;

	size = sizeof(struct net_device *) * VLAN_GROUP_ARRAY_PART_LEN;

	for (i = 0; i < VLAN_GROUP_ARRAY_SPLIT_PARTS; i++) {
		grp->vlan_devices_arrays[i] = kzalloc(size, GFP_KERNEL);
		if (!grp->vlan_devices_arrays[i])
			goto err;
	}

	grp->real_dev_ifindex = ifindex;
	hlist_add_head_rcu(&grp->hlist,
			   &vlan_group_hash[vlan_grp_hashfn(ifindex)]);
	return grp;

err:
	vlan_group_free(grp);
	return NULL;
}

static void vlan_rcu_free(struct rcu_head *rcu)
{
	vlan_group_free(container_of(rcu, struct vlan_group, rcu));
}


/* This returns 0 if everything went fine.
 * It will return 1 if the group was killed as a result.
 * A negative return indicates failure.
 *
 * The RTNL lock must be held.
 */
static int unregister_vlan_dev(struct net_device *real_dev,
			       unsigned short vlan_id)
{
	struct net_device *dev = NULL;
	int real_dev_ifindex = real_dev->ifindex;
	struct vlan_group *grp;
	int i, ret;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "%s: VID: %i\n", __FUNCTION__, vlan_id);
#endif

	/* sanity check */
	if (vlan_id >= VLAN_VID_MASK)
		return -EINVAL;

	ASSERT_RTNL();
	grp = __vlan_find_group(real_dev_ifindex);

	ret = 0;

	if (grp) {
		dev = vlan_group_get_device(grp, vlan_id);
		if (dev) {
			/* Remove proc entry */
			vlan_proc_rem_dev(dev);

			/* Take it out of our own structures, but be sure to
			 * interlock with HW accelerating devices or SW vlan
			 * input packet processing.
			 */
			if (real_dev->features & NETIF_F_HW_VLAN_FILTER)
				real_dev->vlan_rx_kill_vid(real_dev, vlan_id);

			vlan_group_set_device(grp, vlan_id, NULL);
			synchronize_net();


			/* Caller unregisters (and if necessary, puts)
			 * VLAN device, but we get rid of the reference to
			 * real_dev here.
			 */
			dev_put(real_dev);

			/* If the group is now empty, kill off the
			 * group.
			 */
			for (i = 0; i < VLAN_VID_MASK; i++)
				if (vlan_group_get_device(grp, i))
					break;

			if (i == VLAN_VID_MASK) {
				if (real_dev->features & NETIF_F_HW_VLAN_RX)
					real_dev->vlan_rx_register(real_dev, NULL);

				hlist_del_rcu(&grp->hlist);

				/* Free the group, after all cpu's are done. */
				call_rcu(&grp->rcu, vlan_rcu_free);

				grp = NULL;
				ret = 1;
			}
		}
	}

	return ret;
}

int unregister_vlan_device(struct net_device *dev)
{
	int ret;

	ret = unregister_vlan_dev(VLAN_DEV_INFO(dev)->real_dev,
				  VLAN_DEV_INFO(dev)->vlan_id);
	unregister_netdevice(dev);

	if (ret == 1)
		ret = 0;
	return ret;
}

/*
 * vlan network devices have devices nesting below it, and are a special
 * "super class" of normal network devices; split their locks off into a
 * separate class since they always nest.
 */
static struct lock_class_key vlan_netdev_xmit_lock_key;

static int vlan_dev_init(struct net_device *dev)
{
	struct net_device *real_dev = VLAN_DEV_INFO(dev)->real_dev;

	/* IFF_BROADCAST|IFF_MULTICAST; ??? */
	dev->flags  = real_dev->flags & ~IFF_UP;
	dev->iflink = real_dev->ifindex;
	dev->state  = (real_dev->state & ((1<<__LINK_STATE_NOCARRIER) |
					  (1<<__LINK_STATE_DORMANT))) |
		      (1<<__LINK_STATE_PRESENT);

	if (is_zero_ether_addr(dev->dev_addr))
		memcpy(dev->dev_addr, real_dev->dev_addr, dev->addr_len);
	if (is_zero_ether_addr(dev->broadcast))
		memcpy(dev->broadcast, real_dev->broadcast, dev->addr_len);

	if (real_dev->features & NETIF_F_HW_VLAN_TX) {
		dev->hard_header     = real_dev->hard_header;
		dev->hard_header_len = real_dev->hard_header_len;
		dev->hard_start_xmit = vlan_dev_hwaccel_hard_start_xmit;
		dev->rebuild_header  = real_dev->rebuild_header;
	} else {
		dev->hard_header     = vlan_dev_hard_header;
		dev->hard_header_len = real_dev->hard_header_len + VLAN_HLEN;
		dev->hard_start_xmit = vlan_dev_hard_start_xmit;
		dev->rebuild_header  = vlan_dev_rebuild_header;
	}
	dev->hard_header_parse = real_dev->hard_header_parse;
	dev->hard_header_cache = NULL;

	lockdep_set_class(&dev->_xmit_lock, &vlan_netdev_xmit_lock_key);
	return 0;
}

void vlan_setup(struct net_device *new_dev)
{
	SET_MODULE_OWNER(new_dev);

	ether_setup(new_dev);

	/* new_dev->ifindex = 0;  it will be set when added to
	 * the global list.
	 * iflink is set as well.
	 */
	new_dev->get_stats = vlan_dev_get_stats;

	/* Make this thing known as a VLAN device */
	new_dev->priv_flags |= IFF_802_1Q_VLAN;

	/* Set us up to have no queue, as the underlying Hardware device
	 * can do all the queueing we could want.
	 */
	new_dev->tx_queue_len = 0;

	/* set up method calls */
	new_dev->change_mtu = vlan_dev_change_mtu;
	new_dev->init = vlan_dev_init;
	new_dev->open = vlan_dev_open;
	new_dev->stop = vlan_dev_stop;
	new_dev->set_multicast_list = vlan_dev_set_multicast_list;
	new_dev->change_rx_flags = vlan_change_rx_flags;
	new_dev->destructor = free_netdev;
	new_dev->do_ioctl = vlan_dev_ioctl;

	memset(new_dev->broadcast, 0, ETH_ALEN);
}

static void vlan_transfer_operstate(const struct net_device *dev, struct net_device *vlandev)
{
	/* Have to respect userspace enforced dormant state
	 * of real device, also must allow supplicant running
	 * on VLAN device
	 */
	if (dev->operstate == IF_OPER_DORMANT)
		netif_dormant_on(vlandev);
	else
		netif_dormant_off(vlandev);

	if (netif_carrier_ok(dev)) {
		if (!netif_carrier_ok(vlandev))
			netif_carrier_on(vlandev);
	} else {
		if (netif_carrier_ok(vlandev))
			netif_carrier_off(vlandev);
	}
}

int vlan_check_real_dev(struct net_device *real_dev, unsigned short vlan_id)
{
	if (real_dev->features & NETIF_F_VLAN_CHALLENGED) {
		printk(VLAN_DBG "%s: VLANs not supported on %s.\n",
			__FUNCTION__, real_dev->name);
		return -EOPNOTSUPP;
	}

	if ((real_dev->features & NETIF_F_HW_VLAN_RX) &&
	    !real_dev->vlan_rx_register) {
		printk(VLAN_DBG "%s: Device %s has buggy VLAN hw accel.\n",
			__FUNCTION__, real_dev->name);
		return -EOPNOTSUPP;
	}

	if ((real_dev->features & NETIF_F_HW_VLAN_FILTER) &&
	    (!real_dev->vlan_rx_add_vid || !real_dev->vlan_rx_kill_vid)) {
		printk(VLAN_DBG "%s: Device %s has buggy VLAN hw accel.\n",
			__FUNCTION__, real_dev->name);
		return -EOPNOTSUPP;
	}

	/* The real device must be up and operating in order to
	 * assosciate a VLAN device with it.
	 */
	if (!(real_dev->flags & IFF_UP))
		return -ENETDOWN;

	if (__find_vlan_dev(real_dev, vlan_id) != NULL) {
		/* was already registered. */
		printk(VLAN_DBG "%s: ALREADY had VLAN registered\n", __FUNCTION__);
		return -EEXIST;
	}

	return 0;
}

int register_vlan_dev(struct net_device *dev)
{
	struct vlan_dev_info *vlan = VLAN_DEV_INFO(dev);
	struct net_device *real_dev = vlan->real_dev;
	unsigned short vlan_id = vlan->vlan_id;
	struct vlan_group *grp, *ngrp = NULL;
	int err;

	grp = __vlan_find_group(real_dev->ifindex);
	if (!grp) {
		ngrp = grp = vlan_group_alloc(real_dev->ifindex);
		if (!grp)
			return -ENOBUFS;
	}

	err = register_netdevice(dev);
	if (err < 0)
		goto out_free_group;

	/* Account for reference in struct vlan_dev_info */
	dev_hold(real_dev);

	vlan_transfer_operstate(real_dev, dev);
	linkwatch_fire_event(dev); /* _MUST_ call rfc2863_policy() */

	/* So, got the sucker initialized, now lets place
	 * it into our local structure.
	 */
	vlan_group_set_device(grp, vlan_id, dev);
	if (ngrp && real_dev->features & NETIF_F_HW_VLAN_RX)
		real_dev->vlan_rx_register(real_dev, ngrp);
	if (real_dev->features & NETIF_F_HW_VLAN_FILTER)
		real_dev->vlan_rx_add_vid(real_dev, vlan_id);

	if (vlan_proc_add_dev(dev) < 0)
		printk(KERN_WARNING "VLAN: failed to add proc entry for %s\n",
		       dev->name);
	return 0;

out_free_group:
	if (ngrp)
		vlan_group_free(ngrp);
	return err;
}

/*  Attach a VLAN device to a mac address (ie Ethernet Card).
 *  Returns 0 if the device was created or a negative error code otherwise.
 */
static int register_vlan_device(struct net_device *real_dev,
				unsigned short VLAN_ID)
{
	struct net_device *new_dev;
	char name[IFNAMSIZ];
	int err;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "%s: if_name -:%s:-	vid: %i\n",
		__FUNCTION__, eth_IF_name, VLAN_ID);
#endif

	if (VLAN_ID >= VLAN_VID_MASK)
		return -ERANGE;

	err = vlan_check_real_dev(real_dev, VLAN_ID);
	if (err < 0)
		return err;

	/* Gotta set up the fields for the device. */
#ifdef VLAN_DEBUG
	printk(VLAN_DBG "About to allocate name, vlan_name_type: %i\n",
	       vlan_name_type);
#endif
	switch (vlan_name_type) {
	case VLAN_NAME_TYPE_RAW_PLUS_VID:
		/* name will look like:	 eth1.0005 */
		snprintf(name, IFNAMSIZ, "%s.%.4i", real_dev->name, VLAN_ID);
		break;
	case VLAN_NAME_TYPE_PLUS_VID_NO_PAD:
		/* Put our vlan.VID in the name.
		 * Name will look like:	 vlan5
		 */
		snprintf(name, IFNAMSIZ, "vlan%i", VLAN_ID);
		break;
	case VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD:
		/* Put our vlan.VID in the name.
		 * Name will look like:	 eth0.5
		 */
		snprintf(name, IFNAMSIZ, "%s.%i", real_dev->name, VLAN_ID);
		break;
	case VLAN_NAME_TYPE_PLUS_VID:
		/* Put our vlan.VID in the name.
		 * Name will look like:	 vlan0005
		 */
	default:
		snprintf(name, IFNAMSIZ, "vlan%.4i", VLAN_ID);
	}

	new_dev = alloc_netdev(sizeof(struct vlan_dev_info), name,
			       vlan_setup);

	if (new_dev == NULL)
		return -ENOBUFS;

	/* need 4 bytes for extra VLAN header info,
	 * hope the underlying device can handle it.
	 */
	new_dev->mtu = real_dev->mtu;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "Allocated new name -:%s:-\n", new_dev->name);
	VLAN_MEM_DBG("new_dev->priv malloc, addr: %p  size: %i\n",
		     new_dev->priv,
		     sizeof(struct vlan_dev_info));
#endif

	VLAN_DEV_INFO(new_dev)->vlan_id = VLAN_ID; /* 1 through VLAN_VID_MASK */
	VLAN_DEV_INFO(new_dev)->real_dev = real_dev;
	VLAN_DEV_INFO(new_dev)->dent = NULL;
	VLAN_DEV_INFO(new_dev)->flags = VLAN_FLAG_REORDER_HDR;

	new_dev->rtnl_link_ops = &vlan_link_ops;
	err = register_vlan_dev(new_dev);
	if (err < 0)
		goto out_free_newdev;

	/* Account for reference in struct vlan_dev_info */
	dev_hold(real_dev);
#ifdef VLAN_DEBUG
	printk(VLAN_DBG "Allocated new device successfully, returning.\n");
#endif
	return 0;

out_free_newdev:
	free_netdev(new_dev);
	return err;
}

static void vlan_sync_address(struct net_device *dev,
			      struct net_device *vlandev)
{
	struct vlan_dev_info *vlan = VLAN_DEV_INFO(vlandev);

	/* May be called without an actual change */
	if (!compare_ether_addr(vlan->real_dev_addr, dev->dev_addr))
		return;

	/* vlan address was different from the old address and is equal to
	 * the new address */
	if (compare_ether_addr(vlandev->dev_addr, vlan->real_dev_addr) &&
	    !compare_ether_addr(vlandev->dev_addr, dev->dev_addr))
		dev_unicast_delete(dev, vlandev->dev_addr, ETH_ALEN);

	/* vlan address was equal to the old address and is different from
	 * the new address */
	if (!compare_ether_addr(vlandev->dev_addr, vlan->real_dev_addr) &&
	    compare_ether_addr(vlandev->dev_addr, dev->dev_addr))
		dev_unicast_add(dev, vlandev->dev_addr, ETH_ALEN);

	memcpy(vlan->real_dev_addr, dev->dev_addr, ETH_ALEN);
}

static int vlan_device_event(struct notifier_block *unused, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct vlan_group *grp = __vlan_find_group(dev->ifindex);
	int i, flgs;
	struct net_device *vlandev;

	if (!grp)
		goto out;

	/* It is OK that we do not hold the group lock right now,
	 * as we run under the RTNL lock.
	 */

	switch (event) {
	case NETDEV_CHANGE:
		/* Propagate real device state to vlan devices */
		for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			vlan_transfer_operstate(dev, vlandev);
		}
		break;

	case NETDEV_CHANGEADDR:
		/* Adjust unicast filters on underlying device */
		for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			vlan_sync_address(dev, vlandev);
		}
		break;

	case NETDEV_DOWN:
		/* Put all VLANs for this dev in the down state too.  */
		for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			flgs = vlandev->flags;
			if (!(flgs & IFF_UP))
				continue;

			dev_change_flags(vlandev, flgs & ~IFF_UP);
		}
		break;

	case NETDEV_UP:
		/* Put all VLANs for this dev in the up state too.  */
		for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			flgs = vlandev->flags;
			if (flgs & IFF_UP)
				continue;

			dev_change_flags(vlandev, flgs | IFF_UP);
		}
		break;

	case NETDEV_UNREGISTER:
		/* Delete all VLANs for this dev. */
		for (i = 0; i < VLAN_GROUP_ARRAY_LEN; i++) {
			int ret;

			vlandev = vlan_group_get_device(grp, i);
			if (!vlandev)
				continue;

			ret = unregister_vlan_dev(dev,
						  VLAN_DEV_INFO(vlandev)->vlan_id);

			unregister_netdevice(vlandev);

			/* Group was destroyed? */
			if (ret == 1)
				break;
		}
		break;
	}

out:
	return NOTIFY_DONE;
}

/*
 *	VLAN IOCTL handler.
 *	o execute requested action or pass command to the device driver
 *   arg is really a struct vlan_ioctl_args __user *.
 */
static int vlan_ioctl_handler(void __user *arg)
{
	int err;
	unsigned short vid = 0;
	struct vlan_ioctl_args args;
	struct net_device *dev = NULL;

	if (copy_from_user(&args, arg, sizeof(struct vlan_ioctl_args)))
		return -EFAULT;

	/* Null terminate this sucker, just in case. */
	args.device1[23] = 0;
	args.u.device2[23] = 0;

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "%s: args.cmd: %x\n", __FUNCTION__, args.cmd);
#endif

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
		dev = __dev_get_by_name(args.device1);
		if (!dev)
			goto out;

		err = -EINVAL;
		if (args.cmd != ADD_VLAN_CMD &&
		    !(dev->priv_flags & IFF_802_1Q_VLAN))
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
		err = vlan_dev_set_vlan_flag(dev,
					     args.u.flag,
					     args.vlan_qos);
		break;

	case SET_VLAN_NAME_TYPE_CMD:
		err = -EPERM;
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if ((args.u.name_type >= 0) &&
		    (args.u.name_type < VLAN_NAME_TYPE_HIGHEST)) {
			vlan_name_type = args.u.name_type;
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
		err = unregister_vlan_device(dev);
		break;

	case GET_VLAN_INGRESS_PRIORITY_CMD:
		/* TODO:  Implement
		   err = vlan_dev_get_ingress_priority(args);
		   if (copy_to_user((void*)arg, &args,
			sizeof(struct vlan_ioctl_args))) {
			err = -EFAULT;
		   }
		*/
		err = -EINVAL;
		break;
	case GET_VLAN_EGRESS_PRIORITY_CMD:
		/* TODO:  Implement
		   err = vlan_dev_get_egress_priority(args.device1, &(args.args);
		   if (copy_to_user((void*)arg, &args,
			sizeof(struct vlan_ioctl_args))) {
			err = -EFAULT;
		   }
		*/
		err = -EINVAL;
		break;
	case GET_VLAN_REALDEV_NAME_CMD:
		err = 0;
		vlan_dev_get_realdev_name(dev, args.u.device2);
		if (copy_to_user(arg, &args,
				 sizeof(struct vlan_ioctl_args))) {
			err = -EFAULT;
		}
		break;

	case GET_VLAN_VID_CMD:
		err = 0;
		vlan_dev_get_vid(dev, &vid);
		args.u.VID = vid;
		if (copy_to_user(arg, &args,
				 sizeof(struct vlan_ioctl_args))) {
		      err = -EFAULT;
		}
		break;

	default:
		/* pass on to underlying device instead?? */
		printk(VLAN_DBG "%s: Unknown VLAN CMD: %x \n",
			__FUNCTION__, args.cmd);
		err = -EINVAL;
		break;
	}
out:
	rtnl_unlock();
	return err;
}

MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
