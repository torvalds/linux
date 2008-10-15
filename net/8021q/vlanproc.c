/******************************************************************************
 * vlanproc.c	VLAN Module. /proc filesystem interface.
 *
 *		This module is completely hardware-independent and provides
 *		access to the router using Linux /proc filesystem.
 *
 * Author:	Ben Greear, <greearb@candelatech.com> coppied from wanproc.c
 *               by: Gene Kozin	<genek@compuserve.com>
 *
 * Copyright:	(c) 1998 Ben Greear
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 * ============================================================================
 * Jan 20, 1998        Ben Greear     Initial Version
 *****************************************************************************/

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include "vlanproc.h"
#include "vlan.h"

/****** Function Prototypes *************************************************/

/* Methods for preparing data for reading proc entries */
static int vlan_seq_show(struct seq_file *seq, void *v);
static void *vlan_seq_start(struct seq_file *seq, loff_t *pos);
static void *vlan_seq_next(struct seq_file *seq, void *v, loff_t *pos);
static void vlan_seq_stop(struct seq_file *seq, void *);
static int vlandev_seq_show(struct seq_file *seq, void *v);

/*
 *	Global Data
 */


/*
 *	Names of the proc directory entries
 */

static const char name_root[]	 = "vlan";
static const char name_conf[]	 = "config";

/*
 *	Structures for interfacing with the /proc filesystem.
 *	VLAN creates its own directory /proc/net/vlan with the folowing
 *	entries:
 *	config		device status/configuration
 *	<device>	entry for each  device
 */

/*
 *	Generic /proc/net/vlan/<file> file and inode operations
 */

static const struct seq_operations vlan_seq_ops = {
	.start = vlan_seq_start,
	.next = vlan_seq_next,
	.stop = vlan_seq_stop,
	.show = vlan_seq_show,
};

static int vlan_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &vlan_seq_ops,
			sizeof(struct seq_net_private));
}

static const struct file_operations vlan_fops = {
	.owner	 = THIS_MODULE,
	.open    = vlan_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_net,
};

/*
 *	/proc/net/vlan/<device> file and inode operations
 */

static int vlandev_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, vlandev_seq_show, PDE(inode)->data);
}

static const struct file_operations vlandev_fops = {
	.owner = THIS_MODULE,
	.open    = vlandev_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/*
 * Proc filesystem derectory entries.
 */

/* Strings */
static const char *vlan_name_type_str[VLAN_NAME_TYPE_HIGHEST] = {
    [VLAN_NAME_TYPE_RAW_PLUS_VID]        = "VLAN_NAME_TYPE_RAW_PLUS_VID",
    [VLAN_NAME_TYPE_PLUS_VID_NO_PAD]	 = "VLAN_NAME_TYPE_PLUS_VID_NO_PAD",
    [VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD] = "VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD",
    [VLAN_NAME_TYPE_PLUS_VID]		 = "VLAN_NAME_TYPE_PLUS_VID",
};
/*
 *	Interface functions
 */

/*
 *	Clean up /proc/net/vlan entries
 */

void vlan_proc_cleanup(struct net *net)
{
	struct vlan_net *vn = net_generic(net, vlan_net_id);

	if (vn->proc_vlan_conf)
		remove_proc_entry(name_conf, vn->proc_vlan_dir);

	if (vn->proc_vlan_dir)
		proc_net_remove(net, name_root);

	/* Dynamically added entries should be cleaned up as their vlan_device
	 * is removed, so we should not have to take care of it here...
	 */
}

/*
 *	Create /proc/net/vlan entries
 */

int vlan_proc_init(struct net *net)
{
	struct vlan_net *vn = net_generic(net, vlan_net_id);

	vn->proc_vlan_dir = proc_net_mkdir(net, name_root, net->proc_net);
	if (!vn->proc_vlan_dir)
		goto err;

	vn->proc_vlan_conf = proc_create(name_conf, S_IFREG|S_IRUSR|S_IWUSR,
				     vn->proc_vlan_dir, &vlan_fops);
	if (!vn->proc_vlan_conf)
		goto err;
	return 0;

err:
	pr_err("%s: can't create entry in proc filesystem!\n", __func__);
	vlan_proc_cleanup(net);
	return -ENOBUFS;
}

/*
 *	Add directory entry for VLAN device.
 */

int vlan_proc_add_dev(struct net_device *vlandev)
{
	struct vlan_dev_info *dev_info = vlan_dev_info(vlandev);
	struct vlan_net *vn = net_generic(dev_net(vlandev), vlan_net_id);

	dev_info->dent =
		proc_create_data(vlandev->name, S_IFREG|S_IRUSR|S_IWUSR,
				 vn->proc_vlan_dir, &vlandev_fops, vlandev);
	if (!dev_info->dent)
		return -ENOBUFS;
	return 0;
}

/*
 *	Delete directory entry for VLAN device.
 */
int vlan_proc_rem_dev(struct net_device *vlandev)
{
	struct vlan_net *vn = net_generic(dev_net(vlandev), vlan_net_id);

	/** NOTE:  This will consume the memory pointed to by dent, it seems. */
	if (vlan_dev_info(vlandev)->dent) {
		remove_proc_entry(vlan_dev_info(vlandev)->dent->name,
				  vn->proc_vlan_dir);
		vlan_dev_info(vlandev)->dent = NULL;
	}
	return 0;
}

/****** Proc filesystem entry points ****************************************/

/*
 * The following few functions build the content of /proc/net/vlan/config
 */

/* start read of /proc/net/vlan/config */
static void *vlan_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(dev_base_lock)
{
	struct net_device *dev;
	struct net *net = seq_file_net(seq);
	loff_t i = 1;

	read_lock(&dev_base_lock);

	if (*pos == 0)
		return SEQ_START_TOKEN;

	for_each_netdev(net, dev) {
		if (!is_vlan_dev(dev))
			continue;

		if (i++ == *pos)
			return dev;
	}

	return  NULL;
}

static void *vlan_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct net_device *dev;
	struct net *net = seq_file_net(seq);

	++*pos;

	dev = (struct net_device *)v;
	if (v == SEQ_START_TOKEN)
		dev = net_device_entry(&net->dev_base_head);

	for_each_netdev_continue(net, dev) {
		if (!is_vlan_dev(dev))
			continue;

		return dev;
	}

	return NULL;
}

static void vlan_seq_stop(struct seq_file *seq, void *v)
	__releases(dev_base_lock)
{
	read_unlock(&dev_base_lock);
}

static int vlan_seq_show(struct seq_file *seq, void *v)
{
	struct net *net = seq_file_net(seq);
	struct vlan_net *vn = net_generic(net, vlan_net_id);

	if (v == SEQ_START_TOKEN) {
		const char *nmtype = NULL;

		seq_puts(seq, "VLAN Dev name	 | VLAN ID\n");

		if (vn->name_type < ARRAY_SIZE(vlan_name_type_str))
		    nmtype =  vlan_name_type_str[vn->name_type];

		seq_printf(seq, "Name-Type: %s\n",
			   nmtype ? nmtype :  "UNKNOWN");
	} else {
		const struct net_device *vlandev = v;
		const struct vlan_dev_info *dev_info = vlan_dev_info(vlandev);

		seq_printf(seq, "%-15s| %d  | %s\n",  vlandev->name,
			   dev_info->vlan_id,    dev_info->real_dev->name);
	}
	return 0;
}

static int vlandev_seq_show(struct seq_file *seq, void *offset)
{
	struct net_device *vlandev = (struct net_device *) seq->private;
	const struct vlan_dev_info *dev_info = vlan_dev_info(vlandev);
	struct net_device_stats *stats = &vlandev->stats;
	static const char fmt[] = "%30s %12lu\n";
	int i;

	if (!is_vlan_dev(vlandev))
		return 0;

	seq_printf(seq,
		   "%s  VID: %d	 REORDER_HDR: %i  dev->priv_flags: %hx\n",
		   vlandev->name, dev_info->vlan_id,
		   (int)(dev_info->flags & 1), vlandev->priv_flags);

	seq_printf(seq, fmt, "total frames received", stats->rx_packets);
	seq_printf(seq, fmt, "total bytes received", stats->rx_bytes);
	seq_printf(seq, fmt, "Broadcast/Multicast Rcvd", stats->multicast);
	seq_puts(seq, "\n");
	seq_printf(seq, fmt, "total frames transmitted", stats->tx_packets);
	seq_printf(seq, fmt, "total bytes transmitted", stats->tx_bytes);
	seq_printf(seq, fmt, "total headroom inc",
		   dev_info->cnt_inc_headroom_on_tx);
	seq_printf(seq, fmt, "total encap on xmit",
		   dev_info->cnt_encap_on_xmit);
	seq_printf(seq, "Device: %s", dev_info->real_dev->name);
	/* now show all PRIORITY mappings relating to this VLAN */
	seq_printf(seq, "\nINGRESS priority mappings: "
			"0:%u  1:%u  2:%u  3:%u  4:%u  5:%u  6:%u 7:%u\n",
		   dev_info->ingress_priority_map[0],
		   dev_info->ingress_priority_map[1],
		   dev_info->ingress_priority_map[2],
		   dev_info->ingress_priority_map[3],
		   dev_info->ingress_priority_map[4],
		   dev_info->ingress_priority_map[5],
		   dev_info->ingress_priority_map[6],
		   dev_info->ingress_priority_map[7]);

	seq_printf(seq, "EGRESSS priority Mappings: ");
	for (i = 0; i < 16; i++) {
		const struct vlan_priority_tci_mapping *mp
			= dev_info->egress_priority_map[i];
		while (mp) {
			seq_printf(seq, "%u:%hu ",
				   mp->priority, ((mp->vlan_qos >> 13) & 0x7));
			mp = mp->next;
		}
	}
	seq_puts(seq, "\n");

	return 0;
}
