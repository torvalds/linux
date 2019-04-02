// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2010-2019  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "defs.h"
#include "main.h"

#include <asm/current.h>
#include <linux/dcache.h>
#include <linux/defs.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/stddef.h>
#include <linux/stringify.h>
#include <linux/sysfs.h>
#include <net/net_namespace.h>

#include "bat_algo.h"
#include "bridge_loop_avoidance.h"
#include "distributed-arp-table.h"
#include "gateway_client.h"
#include "icmp_socket.h"
#include "log.h"
#include "multicast.h"
#include "network-coding.h"
#include "originator.h"
#include "translation-table.h"

static struct dentry *batadv_defs;

/**
 * batadv_defs_deprecated() - Log use of deprecated batadv defs access
 * @file: file which was accessed
 * @alt: explanation what can be used as alternative
 */
void batadv_defs_deprecated(struct file *file, const char *alt)
{
	struct dentry *dentry = file_dentry(file);
	const char *name = dentry->d_name.name;

	pr_warn_ratelimited(DEPRECATED "%s (pid %d) Use of defs file \"%s\".\n%s",
			    current->comm, task_pid_nr(current), name, alt);
}

static int batadv_algorithms_open(struct inode *inode, struct file *file)
{
	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_ROUTING_ALGOS instead\n");
	return single_open(file, batadv_algo_seq_print_text, NULL);
}

static int neighbors_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_NEIGHBORS instead\n");
	return single_open(file, batadv_hardif_neigh_seq_print_text, net_dev);
}

static int batadv_originators_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_ORIGINATORS instead\n");
	return single_open(file, batadv_orig_seq_print_text, net_dev);
}

/**
 * batadv_originators_hardif_open() - handles defs output for the originator
 *  table of an hard interface
 * @inode: inode pointer to defs file
 * @file: pointer to the seq_file
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_originators_hardif_open(struct inode *inode,
					  struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_HARDIFS instead\n");
	return single_open(file, batadv_orig_hardif_seq_print_text, net_dev);
}

static int batadv_gateways_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_GATEWAYS instead\n");
	return single_open(file, batadv_gw_client_seq_print_text, net_dev);
}

static int batadv_transtable_global_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_TRANSTABLE_GLOBAL instead\n");
	return single_open(file, batadv_tt_global_seq_print_text, net_dev);
}

#ifdef CONFIG_BATMAN_ADV_BLA
static int batadv_bla_claim_table_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_BLA_CLAIM instead\n");
	return single_open(file, batadv_bla_claim_table_seq_print_text,
			   net_dev);
}

static int batadv_bla_backbone_table_open(struct inode *inode,
					  struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_BLA_BACKBONE instead\n");
	return single_open(file, batadv_bla_backbone_table_seq_print_text,
			   net_dev);
}

#endif

#ifdef CONFIG_BATMAN_ADV_DAT
/**
 * batadv_dat_cache_open() - Prepare file handler for reads from dat_cache
 * @inode: inode which was opened
 * @file: file handle to be initialized
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_dat_cache_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_DAT_CACHE instead\n");
	return single_open(file, batadv_dat_cache_seq_print_text, net_dev);
}
#endif

static int batadv_transtable_local_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_TRANSTABLE_LOCAL instead\n");
	return single_open(file, batadv_tt_local_seq_print_text, net_dev);
}

struct batadv_deinfo {
	struct attribute attr;
	const struct file_operations fops;
};

#ifdef CONFIG_BATMAN_ADV_NC
static int batadv_nc_nodes_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file, "");
	return single_open(file, batadv_nc_nodes_seq_print_text, net_dev);
}
#endif

#ifdef CONFIG_BATMAN_ADV_MCAST
/**
 * batadv_mcast_flags_open() - prepare file handler for reads from mcast_flags
 * @inode: inode which was opened
 * @file: file handle to be initialized
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_mcast_flags_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_defs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_MCAST_FLAGS instead\n");
	return single_open(file, batadv_mcast_flags_seq_print_text, net_dev);
}
#endif

#define BATADV_DEINFO(_name, _mode, _open)		\
struct batadv_deinfo batadv_deinfo_##_name = {	\
	.attr = {					\
		.name = __stringify(_name),		\
		.mode = _mode,				\
	},						\
	.fops = {					\
		.owner = THIS_MODULE,			\
		.open = _open,				\
		.read	= seq_read,			\
		.llseek = seq_lseek,			\
		.release = single_release,		\
	},						\
}

/* the following attributes are general and therefore they will be directly
 * placed in the BATADV_DEFS_SUBDIR subdirectory of defs
 */
static BATADV_DEINFO(routing_algos, 0444, batadv_algorithms_open);

static struct batadv_deinfo *batadv_general_deinfos[] = {
	&batadv_deinfo_routing_algos,
	NULL,
};

/* The following attributes are per soft interface */
static BATADV_DEINFO(neighbors, 0444, neighbors_open);
static BATADV_DEINFO(originators, 0444, batadv_originators_open);
static BATADV_DEINFO(gateways, 0444, batadv_gateways_open);
static BATADV_DEINFO(transtable_global, 0444, batadv_transtable_global_open);
#ifdef CONFIG_BATMAN_ADV_BLA
static BATADV_DEINFO(bla_claim_table, 0444, batadv_bla_claim_table_open);
static BATADV_DEINFO(bla_backbone_table, 0444,
			batadv_bla_backbone_table_open);
#endif
#ifdef CONFIG_BATMAN_ADV_DAT
static BATADV_DEINFO(dat_cache, 0444, batadv_dat_cache_open);
#endif
static BATADV_DEINFO(transtable_local, 0444, batadv_transtable_local_open);
#ifdef CONFIG_BATMAN_ADV_NC
static BATADV_DEINFO(nc_nodes, 0444, batadv_nc_nodes_open);
#endif
#ifdef CONFIG_BATMAN_ADV_MCAST
static BATADV_DEINFO(mcast_flags, 0444, batadv_mcast_flags_open);
#endif

static struct batadv_deinfo *batadv_mesh_deinfos[] = {
	&batadv_deinfo_neighbors,
	&batadv_deinfo_originators,
	&batadv_deinfo_gateways,
	&batadv_deinfo_transtable_global,
#ifdef CONFIG_BATMAN_ADV_BLA
	&batadv_deinfo_bla_claim_table,
	&batadv_deinfo_bla_backbone_table,
#endif
#ifdef CONFIG_BATMAN_ADV_DAT
	&batadv_deinfo_dat_cache,
#endif
	&batadv_deinfo_transtable_local,
#ifdef CONFIG_BATMAN_ADV_NC
	&batadv_deinfo_nc_nodes,
#endif
#ifdef CONFIG_BATMAN_ADV_MCAST
	&batadv_deinfo_mcast_flags,
#endif
	NULL,
};

#define BATADV_HARDIF_DEINFO(_name, _mode, _open)		\
struct batadv_deinfo batadv_hardif_deinfo_##_name = {	\
	.attr = {						\
		.name = __stringify(_name),			\
		.mode = _mode,					\
	},							\
	.fops = {						\
		.owner = THIS_MODULE,				\
		.open = _open,					\
		.read	= seq_read,				\
		.llseek = seq_lseek,				\
		.release = single_release,			\
	},							\
}

static BATADV_HARDIF_DEINFO(originators, 0444,
			       batadv_originators_hardif_open);

static struct batadv_deinfo *batadv_hardif_deinfos[] = {
	&batadv_hardif_deinfo_originators,
	NULL,
};

/**
 * batadv_defs_init() - Initialize soft interface independent defs entries
 */
void batadv_defs_init(void)
{
	struct batadv_deinfo **bat_de;
	struct dentry *file;

	batadv_defs = defs_create_dir(BATADV_DEFS_SUBDIR, NULL);
	if (batadv_defs == ERR_PTR(-ENODEV))
		batadv_defs = NULL;

	if (!batadv_defs)
		goto err;

	for (bat_de = batadv_general_deinfos; *bat_de; ++bat_de) {
		file = defs_create_file(((*bat_de)->attr).name,
					   S_IFREG | ((*bat_de)->attr).mode,
					   batadv_defs, NULL,
					   &(*bat_de)->fops);
		if (!file) {
			pr_err("Can't add general defs file: %s\n",
			       ((*bat_de)->attr).name);
			goto err;
		}
	}

	return;
err:
	defs_remove_recursive(batadv_defs);
	batadv_defs = NULL;
}

/**
 * batadv_defs_destroy() - Remove all defs entries
 */
void batadv_defs_destroy(void)
{
	defs_remove_recursive(batadv_defs);
	batadv_defs = NULL;
}

/**
 * batadv_defs_add_hardif() - creates the base directory for a hard interface
 *  in defs.
 * @hard_iface: hard interface which should be added.
 *
 * Return: 0 on success or negative error number in case of failure
 */
int batadv_defs_add_hardif(struct batadv_hard_iface *hard_iface)
{
	struct net *net = dev_net(hard_iface->net_dev);
	struct batadv_deinfo **bat_de;
	struct dentry *file;

	if (!batadv_defs)
		goto out;

	if (net != &init_net)
		return 0;

	hard_iface->de_dir = defs_create_dir(hard_iface->net_dev->name,
						   batadv_defs);
	if (!hard_iface->de_dir)
		goto out;

	for (bat_de = batadv_hardif_deinfos; *bat_de; ++bat_de) {
		file = defs_create_file(((*bat_de)->attr).name,
					   S_IFREG | ((*bat_de)->attr).mode,
					   hard_iface->de_dir,
					   hard_iface->net_dev,
					   &(*bat_de)->fops);
		if (!file)
			goto rem_attr;
	}

	return 0;
rem_attr:
	defs_remove_recursive(hard_iface->de_dir);
	hard_iface->de_dir = NULL;
out:
	return -ENOMEM;
}

/**
 * batadv_defs_rename_hardif() - Fix defs path for renamed hardif
 * @hard_iface: hard interface which was renamed
 */
void batadv_defs_rename_hardif(struct batadv_hard_iface *hard_iface)
{
	const char *name = hard_iface->net_dev->name;
	struct dentry *dir;
	struct dentry *d;

	dir = hard_iface->de_dir;
	if (!dir)
		return;

	d = defs_rename(dir->d_parent, dir, dir->d_parent, name);
	if (!d)
		pr_err("Can't rename defs dir to %s\n", name);
}

/**
 * batadv_defs_del_hardif() - delete the base directory for a hard interface
 *  in defs.
 * @hard_iface: hard interface which is deleted.
 */
void batadv_defs_del_hardif(struct batadv_hard_iface *hard_iface)
{
	struct net *net = dev_net(hard_iface->net_dev);

	if (net != &init_net)
		return;

	if (batadv_defs) {
		defs_remove_recursive(hard_iface->de_dir);
		hard_iface->de_dir = NULL;
	}
}

/**
 * batadv_defs_add_meshif() - Initialize interface dependent defs entries
 * @dev: netdev struct of the soft interface
 *
 * Return: 0 on success or negative error number in case of failure
 */
int batadv_defs_add_meshif(struct net_device *dev)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	struct batadv_deinfo **bat_de;
	struct net *net = dev_net(dev);
	struct dentry *file;

	if (!batadv_defs)
		goto out;

	if (net != &init_net)
		return 0;

	bat_priv->de_dir = defs_create_dir(dev->name, batadv_defs);
	if (!bat_priv->de_dir)
		goto out;

	if (batadv_socket_setup(bat_priv) < 0)
		goto rem_attr;

	if (batadv_de_log_setup(bat_priv) < 0)
		goto rem_attr;

	for (bat_de = batadv_mesh_deinfos; *bat_de; ++bat_de) {
		file = defs_create_file(((*bat_de)->attr).name,
					   S_IFREG | ((*bat_de)->attr).mode,
					   bat_priv->de_dir,
					   dev, &(*bat_de)->fops);
		if (!file) {
			batadv_err(dev, "Can't add defs file: %s/%s\n",
				   dev->name, ((*bat_de)->attr).name);
			goto rem_attr;
		}
	}

	if (batadv_nc_init_defs(bat_priv) < 0)
		goto rem_attr;

	return 0;
rem_attr:
	defs_remove_recursive(bat_priv->de_dir);
	bat_priv->de_dir = NULL;
out:
	return -ENOMEM;
}

/**
 * batadv_defs_rename_meshif() - Fix defs path for renamed softif
 * @dev: net_device which was renamed
 */
void batadv_defs_rename_meshif(struct net_device *dev)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	const char *name = dev->name;
	struct dentry *dir;
	struct dentry *d;

	dir = bat_priv->de_dir;
	if (!dir)
		return;

	d = defs_rename(dir->d_parent, dir, dir->d_parent, name);
	if (!d)
		pr_err("Can't rename defs dir to %s\n", name);
}

/**
 * batadv_defs_del_meshif() - Remove interface dependent defs entries
 * @dev: netdev struct of the soft interface
 */
void batadv_defs_del_meshif(struct net_device *dev)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	struct net *net = dev_net(dev);

	if (net != &init_net)
		return;

	batadv_de_log_cleanup(bat_priv);

	if (batadv_defs) {
		defs_remove_recursive(bat_priv->de_dir);
		bat_priv->de_dir = NULL;
	}
}
