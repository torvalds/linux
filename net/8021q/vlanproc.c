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
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/kernel.h>
#include <linux/slab.h>		/* kmalloc(), kfree() */
#include <linux/mm.h>
#include <linux/string.h>	/* inline mem*, str* functions */
#include <linux/init.h>		/* __initfunc et al. */
#include <asm/byteorder.h>	/* htons(), etc. */
#include <asm/uaccess.h>	/* copy_to_user */
#include <asm/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
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

static struct seq_operations vlan_seq_ops = {
	.start = vlan_seq_start,
	.next = vlan_seq_next,
	.stop = vlan_seq_stop,
	.show = vlan_seq_show,
};

static int vlan_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &vlan_seq_ops);
}

static const struct file_operations vlan_fops = {
	.owner	 = THIS_MODULE,
	.open    = vlan_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
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

/*
 *	/proc/net/vlan
 */

static struct proc_dir_entry *proc_vlan_dir;

/*
 *	/proc/net/vlan/config
 */

static struct proc_dir_entry *proc_vlan_conf;

/* Strings */
static const char *vlan_name_type_str[VLAN_NAME_TYPE_HIGHEST] = {
    [VLAN_NAME_TYPE_RAW_PLUS_VID]       = "VLAN_NAME_TYPE_RAW_PLUS_VID",
    [VLAN_NAME_TYPE_PLUS_VID_NO_PAD]	= "VLAN_NAME_TYPE_PLUS_VID_NO_PAD",
    [VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD]= "VLAN_NAME_TYPE_RAW_PLUS_VID_NO_PAD",
    [VLAN_NAME_TYPE_PLUS_VID]		= "VLAN_NAME_TYPE_PLUS_VID",
};
/*
 *	Interface functions
 */

/*
 *	Clean up /proc/net/vlan entries
 */

void vlan_proc_cleanup(void)
{
	if (proc_vlan_conf)
		remove_proc_entry(name_conf, proc_vlan_dir);

	if (proc_vlan_dir)
		proc_net_remove(name_root);

	/* Dynamically added entries should be cleaned up as their vlan_device
	 * is removed, so we should not have to take care of it here...
	 */
}

/*
 *	Create /proc/net/vlan entries
 */

int __init vlan_proc_init(void)
{
	proc_vlan_dir = proc_mkdir(name_root, proc_net);
	if (proc_vlan_dir) {
		proc_vlan_conf = create_proc_entry(name_conf,
						   S_IFREG|S_IRUSR|S_IWUSR,
						   proc_vlan_dir);
		if (proc_vlan_conf) {
			proc_vlan_conf->proc_fops = &vlan_fops;
			return 0;
		}
	}
	vlan_proc_cleanup();
	return -ENOBUFS;
}

/*
 *	Add directory entry for VLAN device.
 */

int vlan_proc_add_dev (struct net_device *vlandev)
{
	struct vlan_dev_info *dev_info = VLAN_DEV_INFO(vlandev);

	if (!(vlandev->priv_flags & IFF_802_1Q_VLAN)) {
		printk(KERN_ERR
		       "ERROR:	vlan_proc_add, device -:%s:- is NOT a VLAN\n",
		       vlandev->name);
		return -EINVAL;
	}

	dev_info->dent = create_proc_entry(vlandev->name,
					   S_IFREG|S_IRUSR|S_IWUSR,
					   proc_vlan_dir);
	if (!dev_info->dent)
		return -ENOBUFS;

	dev_info->dent->proc_fops = &vlandev_fops;
	dev_info->dent->data = vlandev;

#ifdef VLAN_DEBUG
	printk(KERN_ERR "vlan_proc_add, device -:%s:- being added.\n",
	       vlandev->name);
#endif
	return 0;
}

/*
 *	Delete directory entry for VLAN device.
 */
int vlan_proc_rem_dev(struct net_device *vlandev)
{
	if (!vlandev) {
		printk(VLAN_ERR "%s: invalid argument: %p\n",
			__FUNCTION__, vlandev);
		return -EINVAL;
	}

	if (!(vlandev->priv_flags & IFF_802_1Q_VLAN)) {
		printk(VLAN_DBG "%s: invalid argument, device: %s is not a VLAN device, priv_flags: 0x%4hX.\n",
			__FUNCTION__, vlandev->name, vlandev->priv_flags);
		return -EINVAL;
	}

#ifdef VLAN_DEBUG
	printk(VLAN_DBG "%s: dev: %p\n", __FUNCTION__, vlandev);
#endif

	/** NOTE:  This will consume the memory pointed to by dent, it seems. */
	if (VLAN_DEV_INFO(vlandev)->dent) {
		remove_proc_entry(VLAN_DEV_INFO(vlandev)->dent->name, proc_vlan_dir);
		VLAN_DEV_INFO(vlandev)->dent = NULL;
	}

	return 0;
}

/****** Proc filesystem entry points ****************************************/

/*
 * The following few functions build the content of /proc/net/vlan/config
 */

/* starting at dev, find a VLAN device */
static struct net_device *vlan_skip(struct net_device *dev)
{
	while (dev && !(dev->priv_flags & IFF_802_1Q_VLAN))
		dev = dev->next;

	return dev;
}

/* start read of /proc/net/vlan/config */
static void *vlan_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct net_device *dev;
	loff_t i = 1;

	read_lock(&dev_base_lock);

	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (dev = vlan_skip(dev_base); dev && i < *pos;
	     dev = vlan_skip(dev->next), ++i);

	return  (i == *pos) ? dev : NULL;
}

static void *vlan_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;

	return vlan_skip((v == SEQ_START_TOKEN)
			    ? dev_base
			    : ((struct net_device *)v)->next);
}

static void vlan_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&dev_base_lock);
}

static int vlan_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		const char *nmtype = NULL;

		seq_puts(seq, "VLAN Dev name	 | VLAN ID\n");

		if (vlan_name_type < ARRAY_SIZE(vlan_name_type_str))
		    nmtype =  vlan_name_type_str[vlan_name_type];

		seq_printf(seq, "Name-Type: %s\n",
			   nmtype ? nmtype :  "UNKNOWN" );
	} else {
		const struct net_device *vlandev = v;
		const struct vlan_dev_info *dev_info = VLAN_DEV_INFO(vlandev);

		seq_printf(seq, "%-15s| %d  | %s\n",  vlandev->name,
			   dev_info->vlan_id,    dev_info->real_dev->name);
	}
	return 0;
}

static int vlandev_seq_show(struct seq_file *seq, void *offset)
{
	struct net_device *vlandev = (struct net_device *) seq->private;
	const struct vlan_dev_info *dev_info = VLAN_DEV_INFO(vlandev);
	struct net_device_stats *stats;
	static const char fmt[] = "%30s %12lu\n";
	int i;

	if ((vlandev == NULL) || (!(vlandev->priv_flags & IFF_802_1Q_VLAN)))
		return 0;

	seq_printf(seq, "%s  VID: %d	 REORDER_HDR: %i  dev->priv_flags: %hx\n",
		       vlandev->name, dev_info->vlan_id,
		       (int)(dev_info->flags & 1), vlandev->priv_flags);


	stats = vlan_dev_get_stats(vlandev);

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
	seq_printf(seq,
		       "\nINGRESS priority mappings: 0:%lu  1:%lu  2:%lu  3:%lu  4:%lu  5:%lu  6:%lu 7:%lu\n",
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
			seq_printf(seq, "%lu:%hu ",
				   mp->priority, ((mp->vlan_qos >> 13) & 0x7));
			mp = mp->next;
		}
	}
	seq_puts(seq, "\n");

	return 0;
}
