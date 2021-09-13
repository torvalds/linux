// SPDX-License-Identifier: GPL-2.0-or-later
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
#include <linux/uaccess.h>

#include <linux/if_vlan.h>
#include "vlan.h"
#include "vlanproc.h"

#define DRV_VERSION "1.8"

/* Global VLAN variables */

unsigned int vlan_net_id __read_mostly;

const char vlan_fullname[] = "802.1Q VLAN Support";
const char vlan_version[] = DRV_VERSION;

/* End of global variables definitions. */

static int vlan_group_prealloc_vid(struct vlan_group *vg,
				   __be16 vlan_proto, u16 vlan_id)
{
	struct net_device **array;
	unsigned int vidx;
	unsigned int size;
	int pidx;

	ASSERT_RTNL();

	pidx  = vlan_proto_idx(vlan_proto);
	if (pidx < 0)
		return -EINVAL;

	vidx  = vlan_id / VLAN_GROUP_ARRAY_PART_LEN;
	array = vg->vlan_devices_arrays[pidx][vidx];
	if (array != NULL)
		return 0;

	size = sizeof(struct net_device *) * VLAN_GROUP_ARRAY_PART_LEN;
	array = kzalloc(size, GFP_KERNEL_ACCOUNT);
	if (array == NULL)
		return -ENOBUFS;

	/* paired with smp_rmb() in __vlan_group_get_device() */
	smp_wmb();

	vg->vlan_devices_arrays[pidx][vidx] = array;
	return 0;
}

static void vlan_stacked_transfer_operstate(const struct net_device *rootdev,
					    struct net_device *dev,
					    struct vlan_dev_priv *vlan)
{
	if (!(vlan->flags & VLAN_FLAG_BRIDGE_BINDING))
		netif_stacked_transfer_operstate(rootdev, dev);
}

void unregister_vlan_dev(struct net_device *dev, struct list_head *head)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;
	struct vlan_info *vlan_info;
	struct vlan_group *grp;
	u16 vlan_id = vlan->vlan_id;

	ASSERT_RTNL();

	vlan_info = rtnl_dereference(real_dev->vlan_info);
	BUG_ON(!vlan_info);

	grp = &vlan_info->grp;

	grp->nr_vlan_devs--;

	if (vlan->flags & VLAN_FLAG_MVRP)
		vlan_mvrp_request_leave(dev);
	if (vlan->flags & VLAN_FLAG_GVRP)
		vlan_gvrp_request_leave(dev);

	vlan_group_set_device(grp, vlan->vlan_proto, vlan_id, NULL);

	netdev_upper_dev_unlink(real_dev, dev);
	/* Because unregister_netdevice_queue() makes sure at least one rcu
	 * grace period is respected before device freeing,
	 * we dont need to call synchronize_net() here.
	 */
	unregister_netdevice_queue(dev, head);

	if (grp->nr_vlan_devs == 0) {
		vlan_mvrp_uninit_applicant(real_dev);
		vlan_gvrp_uninit_applicant(real_dev);
	}

	vlan_vid_del(real_dev, vlan->vlan_proto, vlan_id);

	/* Get rid of the vlan's reference to real_dev */
	dev_put(real_dev);
}

int vlan_check_real_dev(struct net_device *real_dev,
			__be16 protocol, u16 vlan_id,
			struct netlink_ext_ack *extack)
{
	const char *name = real_dev->name;

	if (real_dev->features & NETIF_F_VLAN_CHALLENGED) {
		pr_info("VLANs not supported on %s\n", name);
		NL_SET_ERR_MSG_MOD(extack, "VLANs not supported on device");
		return -EOPNOTSUPP;
	}

	if (vlan_find_dev(real_dev, protocol, vlan_id) != NULL) {
		NL_SET_ERR_MSG_MOD(extack, "VLAN device already exists");
		return -EEXIST;
	}

	return 0;
}

int register_vlan_dev(struct net_device *dev, struct netlink_ext_ack *extack)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(dev);
	struct net_device *real_dev = vlan->real_dev;
	u16 vlan_id = vlan->vlan_id;
	struct vlan_info *vlan_info;
	struct vlan_group *grp;
	int err;

	err = vlan_vid_add(real_dev, vlan->vlan_proto, vlan_id);
	if (err)
		return err;

	vlan_info = rtnl_dereference(real_dev->vlan_info);
	/* vlan_info should be there now. vlan_vid_add took care of it */
	BUG_ON(!vlan_info);

	grp = &vlan_info->grp;
	if (grp->nr_vlan_devs == 0) {
		err = vlan_gvrp_init_applicant(real_dev);
		if (err < 0)
			goto out_vid_del;
		err = vlan_mvrp_init_applicant(real_dev);
		if (err < 0)
			goto out_uninit_gvrp;
	}

	err = vlan_group_prealloc_vid(grp, vlan->vlan_proto, vlan_id);
	if (err < 0)
		goto out_uninit_mvrp;

	err = register_netdevice(dev);
	if (err < 0)
		goto out_uninit_mvrp;

	err = netdev_upper_dev_link(real_dev, dev, extack);
	if (err)
		goto out_unregister_netdev;

	/* Account for reference in struct vlan_dev_priv */
	dev_hold(real_dev);

	vlan_stacked_transfer_operstate(real_dev, dev, vlan);
	linkwatch_fire_event(dev); /* _MUST_ call rfc2863_policy() */

	/* So, got the sucker initialized, now lets place
	 * it into our local structure.
	 */
	vlan_group_set_device(grp, vlan->vlan_proto, vlan_id, dev);
	grp->nr_vlan_devs++;

	return 0;

out_unregister_netdev:
	unregister_netdevice(dev);
out_uninit_mvrp:
	if (grp->nr_vlan_devs == 0)
		vlan_mvrp_uninit_applicant(real_dev);
out_uninit_gvrp:
	if (grp->nr_vlan_devs == 0)
		vlan_gvrp_uninit_applicant(real_dev);
out_vid_del:
	vlan_vid_del(real_dev, vlan->vlan_proto, vlan_id);
	return err;
}

/*  Attach a VLAN device to a mac address (ie Ethernet Card).
 *  Returns 0 if the device was created or a negative error code otherwise.
 */
static int register_vlan_device(struct net_device *real_dev, u16 vlan_id)
{
	struct net_device *new_dev;
	struct vlan_dev_priv *vlan;
	struct net *net = dev_net(real_dev);
	struct vlan_net *vn = net_generic(net, vlan_net_id);
	char name[IFNAMSIZ];
	int err;

	if (vlan_id >= VLAN_VID_MASK)
		return -ERANGE;

	err = vlan_check_real_dev(real_dev, htons(ETH_P_8021Q), vlan_id,
				  NULL);
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

	new_dev = alloc_netdev(sizeof(struct vlan_dev_priv), name,
			       NET_NAME_UNKNOWN, vlan_setup);

	if (new_dev == NULL)
		return -ENOBUFS;

	dev_net_set(new_dev, net);
	/* need 4 bytes for extra VLAN header info,
	 * hope the underlying device can handle it.
	 */
	new_dev->mtu = real_dev->mtu;

	vlan = vlan_dev_priv(new_dev);
	vlan->vlan_proto = htons(ETH_P_8021Q);
	vlan->vlan_id = vlan_id;
	vlan->real_dev = real_dev;
	vlan->dent = NULL;
	vlan->flags = VLAN_FLAG_REORDER_HDR;

	new_dev->rtnl_link_ops = &vlan_link_ops;
	err = register_vlan_dev(new_dev, NULL);
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
	struct vlan_dev_priv *vlan = vlan_dev_priv(vlandev);

	/* May be called without an actual change */
	if (ether_addr_equal(vlan->real_dev_addr, dev->dev_addr))
		return;

	/* vlan continues to inherit address of lower device */
	if (vlan_dev_inherit_address(vlandev, dev))
		goto out;

	/* vlan address was different from the old address and is equal to
	 * the new address */
	if (!ether_addr_equal(vlandev->dev_addr, vlan->real_dev_addr) &&
	    ether_addr_equal(vlandev->dev_addr, dev->dev_addr))
		dev_uc_del(dev, vlandev->dev_addr);

	/* vlan address was equal to the old address and is different from
	 * the new address */
	if (ether_addr_equal(vlandev->dev_addr, vlan->real_dev_addr) &&
	    !ether_addr_equal(vlandev->dev_addr, dev->dev_addr))
		dev_uc_add(dev, vlandev->dev_addr);

out:
	ether_addr_copy(vlan->real_dev_addr, dev->dev_addr);
}

static void vlan_transfer_features(struct net_device *dev,
				   struct net_device *vlandev)
{
	struct vlan_dev_priv *vlan = vlan_dev_priv(vlandev);

	vlandev->gso_max_size = dev->gso_max_size;
	vlandev->gso_max_segs = dev->gso_max_segs;

	if (vlan_hw_offload_capable(dev->features, vlan->vlan_proto))
		vlandev->hard_header_len = dev->hard_header_len;
	else
		vlandev->hard_header_len = dev->hard_header_len + VLAN_HLEN;

#if IS_ENABLED(CONFIG_FCOE)
	vlandev->fcoe_ddp_xid = dev->fcoe_ddp_xid;
#endif

	vlandev->priv_flags &= ~IFF_XMIT_DST_RELEASE;
	vlandev->priv_flags |= (vlan->real_dev->priv_flags & IFF_XMIT_DST_RELEASE);
	vlandev->hw_enc_features = vlan_tnl_features(vlan->real_dev);

	netdev_update_features(vlandev);
}

static int __vlan_device_event(struct net_device *dev, unsigned long event)
{
	int err = 0;

	switch (event) {
	case NETDEV_CHANGENAME:
		vlan_proc_rem_dev(dev);
		err = vlan_proc_add_dev(dev);
		break;
	case NETDEV_REGISTER:
		err = vlan_proc_add_dev(dev);
		break;
	case NETDEV_UNREGISTER:
		vlan_proc_rem_dev(dev);
		break;
	}

	return err;
}

static int vlan_device_event(struct notifier_block *unused, unsigned long event,
			     void *ptr)
{
	struct netlink_ext_ack *extack = netdev_notifier_info_to_extack(ptr);
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct vlan_group *grp;
	struct vlan_info *vlan_info;
	int i, flgs;
	struct net_device *vlandev;
	struct vlan_dev_priv *vlan;
	bool last = false;
	LIST_HEAD(list);
	int err;

	if (is_vlan_dev(dev)) {
		int err = __vlan_device_event(dev, event);

		if (err)
			return notifier_from_errno(err);
	}

	if ((event == NETDEV_UP) &&
	    (dev->features & NETIF_F_HW_VLAN_CTAG_FILTER)) {
		pr_info("adding VLAN 0 to HW filter on device %s\n",
			dev->name);
		vlan_vid_add(dev, htons(ETH_P_8021Q), 0);
	}
	if (event == NETDEV_DOWN &&
	    (dev->features & NETIF_F_HW_VLAN_CTAG_FILTER))
		vlan_vid_del(dev, htons(ETH_P_8021Q), 0);

	vlan_info = rtnl_dereference(dev->vlan_info);
	if (!vlan_info)
		goto out;
	grp = &vlan_info->grp;

	/* It is OK that we do not hold the group lock right now,
	 * as we run under the RTNL lock.
	 */

	switch (event) {
	case NETDEV_CHANGE:
		/* Propagate real device state to vlan devices */
		vlan_group_for_each_dev(grp, i, vlandev)
			vlan_stacked_transfer_operstate(dev, vlandev,
							vlan_dev_priv(vlandev));
		break;

	case NETDEV_CHANGEADDR:
		/* Adjust unicast filters on underlying device */
		vlan_group_for_each_dev(grp, i, vlandev) {
			flgs = vlandev->flags;
			if (!(flgs & IFF_UP))
				continue;

			vlan_sync_address(dev, vlandev);
		}
		break;

	case NETDEV_CHANGEMTU:
		vlan_group_for_each_dev(grp, i, vlandev) {
			if (vlandev->mtu <= dev->mtu)
				continue;

			dev_set_mtu(vlandev, dev->mtu);
		}
		break;

	case NETDEV_FEAT_CHANGE:
		/* Propagate device features to underlying device */
		vlan_group_for_each_dev(grp, i, vlandev)
			vlan_transfer_features(dev, vlandev);
		break;

	case NETDEV_DOWN: {
		struct net_device *tmp;
		LIST_HEAD(close_list);

		/* Put all VLANs for this dev in the down state too.  */
		vlan_group_for_each_dev(grp, i, vlandev) {
			flgs = vlandev->flags;
			if (!(flgs & IFF_UP))
				continue;

			vlan = vlan_dev_priv(vlandev);
			if (!(vlan->flags & VLAN_FLAG_LOOSE_BINDING))
				list_add(&vlandev->close_list, &close_list);
		}

		dev_close_many(&close_list, false);

		list_for_each_entry_safe(vlandev, tmp, &close_list, close_list) {
			vlan_stacked_transfer_operstate(dev, vlandev,
							vlan_dev_priv(vlandev));
			list_del_init(&vlandev->close_list);
		}
		list_del(&close_list);
		break;
	}
	case NETDEV_UP:
		/* Put all VLANs for this dev in the up state too.  */
		vlan_group_for_each_dev(grp, i, vlandev) {
			flgs = dev_get_flags(vlandev);
			if (flgs & IFF_UP)
				continue;

			vlan = vlan_dev_priv(vlandev);
			if (!(vlan->flags & VLAN_FLAG_LOOSE_BINDING))
				dev_change_flags(vlandev, flgs | IFF_UP,
						 extack);
			vlan_stacked_transfer_operstate(dev, vlandev, vlan);
		}
		break;

	case NETDEV_UNREGISTER:
		/* twiddle thumbs on netns device moves */
		if (dev->reg_state != NETREG_UNREGISTERING)
			break;

		vlan_group_for_each_dev(grp, i, vlandev) {
			/* removal of last vid destroys vlan_info, abort
			 * afterwards */
			if (vlan_info->nr_vids == 1)
				last = true;

			unregister_vlan_dev(vlandev, &list);
			if (last)
				break;
		}
		unregister_netdevice_many(&list);
		break;

	case NETDEV_PRE_TYPE_CHANGE:
		/* Forbid underlaying device to change its type. */
		if (vlan_uses_dev(dev))
			return NOTIFY_BAD;
		break;

	case NETDEV_NOTIFY_PEERS:
	case NETDEV_BONDING_FAILOVER:
	case NETDEV_RESEND_IGMP:
		/* Propagate to vlan devices */
		vlan_group_for_each_dev(grp, i, vlandev)
			call_netdevice_notifiers(event, vlandev);
		break;

	case NETDEV_CVLAN_FILTER_PUSH_INFO:
		err = vlan_filter_push_vids(vlan_info, htons(ETH_P_8021Q));
		if (err)
			return notifier_from_errno(err);
		break;

	case NETDEV_CVLAN_FILTER_DROP_INFO:
		vlan_filter_drop_vids(vlan_info, htons(ETH_P_8021Q));
		break;

	case NETDEV_SVLAN_FILTER_PUSH_INFO:
		err = vlan_filter_push_vids(vlan_info, htons(ETH_P_8021AD));
		if (err)
			return notifier_from_errno(err);
		break;

	case NETDEV_SVLAN_FILTER_DROP_INFO:
		vlan_filter_drop_vids(vlan_info, htons(ETH_P_8021AD));
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
	args.device1[sizeof(args.device1) - 1] = 0;
	args.u.device2[sizeof(args.u.device2) - 1] = 0;

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
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			break;
		vlan_dev_set_ingress_priority(dev,
					      args.u.skb_priority,
					      args.vlan_qos);
		err = 0;
		break;

	case SET_VLAN_EGRESS_PRIORITY_CMD:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			break;
		err = vlan_dev_set_egress_priority(dev,
						   args.u.skb_priority,
						   args.vlan_qos);
		break;

	case SET_VLAN_FLAG_CMD:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			break;
		err = vlan_dev_change_flags(dev,
					    args.vlan_qos ? args.u.flag : 0,
					    args.u.flag);
		break;

	case SET_VLAN_NAME_TYPE_CMD:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			break;
		if (args.u.name_type < VLAN_NAME_TYPE_HIGHEST) {
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
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			break;
		err = register_vlan_device(dev, args.u.VID);
		break;

	case DEL_VLAN_CMD:
		err = -EPERM;
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			break;
		unregister_vlan_dev(dev, NULL);
		err = 0;
		break;

	case GET_VLAN_REALDEV_NAME_CMD:
		err = 0;
		vlan_dev_get_realdev_name(dev, args.u.device2,
					  sizeof(args.u.device2));
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

	err = vlan_mvrp_init();
	if (err < 0)
		goto err4;

	err = vlan_netlink_init();
	if (err < 0)
		goto err5;

	vlan_ioctl_set(vlan_ioctl_handler);
	return 0;

err5:
	vlan_mvrp_uninit();
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

	vlan_mvrp_uninit();
	vlan_gvrp_uninit();
}

module_init(vlan_proto_init);
module_exit(vlan_cleanup_module);

MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);
