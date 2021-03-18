/*
 * debugfs code for HSR & PRP
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

/* hsr_node_table_show - Formats and prints node_table entries */
static int
hsr_node_table_show(struct seq_file *sfp, void *data)
{
	struct hsr_priv *priv = (struct hsr_priv *)sfp->private;
	struct hsr_node *node;

	seq_printf(sfp, "Node Table entries for (%s) device\n",
		   (priv->prot_version == PRP_V1 ? "PRP" : "HSR"));
	seq_puts(sfp, "MAC-Address-A,    MAC-Address-B,    time_in[A], ");
	seq_puts(sfp, "time_in[B], Address-B port, ");
	if (priv->prot_version == PRP_V1)
		seq_puts(sfp, "SAN-A, SAN-B, DAN-P\n");
	else
		seq_puts(sfp, "DAN-H\n");

	rcu_read_lock();
	list_for_each_entry_rcu(node, &priv->node_db, mac_list) {
		/* skip self node */
		if (hsr_addr_is_self(priv, node->macaddress_A))
			continue;
		seq_printf(sfp, "%pM ", &node->macaddress_A[0]);
		seq_printf(sfp, "%pM ", &node->macaddress_B[0]);
		seq_printf(sfp, "%10lx, ", node->time_in[HSR_PT_SLAVE_A]);
		seq_printf(sfp, "%10lx, ", node->time_in[HSR_PT_SLAVE_B]);
		seq_printf(sfp, "%14x, ", node->addr_B_port);

		if (priv->prot_version == PRP_V1)
			seq_printf(sfp, "%5x, %5x, %5x\n",
				   node->san_a, node->san_b,
				   (node->san_a == 0 && node->san_b == 0));
		else
			seq_printf(sfp, "%5x\n", 1);
	}
	rcu_read_unlock();
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(hsr_node_table);

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
				 &hsr_node_table_fops);
	if (IS_ERR(de)) {
		pr_err("Cannot create hsr node_table file\n");
		debugfs_remove(priv->node_tbl_root);
		priv->node_tbl_root = NULL;
		return;
	}
}

/* hsr_debugfs_term - Tear down debugfs intrastructure
 *
 * Description:
 * When Debugfs is configured this routine removes debugfs file system
 * elements that are specific to hsr
 */
void
hsr_debugfs_term(struct hsr_priv *priv)
{
	debugfs_remove_recursive(priv->node_tbl_root);
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
