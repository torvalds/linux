/*
 * hsr_debugfs code
 * Copyright (C) 2019 Texas Instruments Incorporated
 *
 * Author(s):
 *	Murali Karicheri <m-karicheri2@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/debugfs.h>
#include "hsr_main.h"
#include "hsr_framereg.h"

static struct dentry *hsr_debugfs_root_dir;

static void print_mac_address(struct seq_file *sfp, unsigned char *mac)
{
	seq_printf(sfp, "%02x:%02x:%02x:%02x:%02x:%02x:",
		   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* hsr_node_table_show - Formats and prints node_table entries */
static int
hsr_node_table_show(struct seq_file *sfp, void *data)
{
	struct hsr_priv *priv = (struct hsr_priv *)sfp->private;
	struct hsr_node *node;

	seq_puts(sfp, "Node Table entries\n");
	seq_puts(sfp, "MAC-Address-A,   MAC-Address-B, time_in[A], ");
	seq_puts(sfp, "time_in[B], Address-B port\n");
	rcu_read_lock();
	list_for_each_entry_rcu(node, &priv->node_db, mac_list) {
		/* skip self node */
		if (hsr_addr_is_self(priv, node->macaddress_A))
			continue;
		print_mac_address(sfp, &node->macaddress_A[0]);
		seq_puts(sfp, " ");
		print_mac_address(sfp, &node->macaddress_B[0]);
		seq_printf(sfp, "0x%lx, ", node->time_in[HSR_PT_SLAVE_A]);
		seq_printf(sfp, "0x%lx ", node->time_in[HSR_PT_SLAVE_B]);
		seq_printf(sfp, "0x%x\n", node->addr_B_port);
	}
	rcu_read_unlock();
	return 0;
}

/* hsr_node_table_open - Open the node_table file
 *
 * Description:
 * This routine opens a debugfs file node_table of specific hsr device
 */
static int
hsr_node_table_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hsr_node_table_show, inode->i_private);
}

void hsr_debugfs_rename(struct net_device *dev)
{
	struct hsr_priv *priv = netdev_priv(dev);
	struct dentry *d;

	d = debugfs_rename(hsr_debugfs_root_dir, priv->node_tbl_root,
			   hsr_debugfs_root_dir, dev->name);
	if (IS_ERR(d))
		netdev_warn(dev, "failed to rename\n");
	else
		priv->node_tbl_root = d;
}

static const struct file_operations hsr_fops = {
	.open	= hsr_node_table_open,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* hsr_debugfs_init - create hsr node_table file for dumping
 * the node table
 *
 * Description:
 * When debugfs is configured this routine sets up the node_table file per
 * hsr device for dumping the node_table entries
 */
void hsr_debugfs_init(struct hsr_priv *priv, struct net_device *hsr_dev)
{
	struct dentry *de = NULL;

	de = debugfs_create_dir(hsr_dev->name, hsr_debugfs_root_dir);
	if (IS_ERR(de)) {
		pr_err("Cannot create hsr debugfs directory\n");
		return;
	}

	priv->node_tbl_root = de;

	de = debugfs_create_file("node_table", S_IFREG | 0444,
				 priv->node_tbl_root, priv,
				 &hsr_fops);
	if (IS_ERR(de)) {
		pr_err("Cannot create hsr node_table file\n");
		debugfs_remove(priv->node_tbl_root);
		priv->node_tbl_root = NULL;
		return;
	}
	priv->node_tbl_file = de;
}

/* hsr_debugfs_term - Tear down debugfs intrastructure
 *
 * Description:
 * When Debufs is configured this routine removes debugfs file system
 * elements that are specific to hsr
 */
void
hsr_debugfs_term(struct hsr_priv *priv)
{
	debugfs_remove(priv->node_tbl_file);
	priv->node_tbl_file = NULL;
	debugfs_remove(priv->node_tbl_root);
	priv->node_tbl_root = NULL;
}

void hsr_debugfs_create_root(void)
{
	hsr_debugfs_root_dir = debugfs_create_dir("hsr", NULL);
	if (IS_ERR(hsr_debugfs_root_dir)) {
		pr_err("Cannot create hsr debugfs root directory\n");
		hsr_debugfs_root_dir = NULL;
	}
}

void hsr_debugfs_remove_root(void)
{
	/* debugfs_remove() internally checks NULL and ERROR */
	debugfs_remove(hsr_debugfs_root_dir);
}
