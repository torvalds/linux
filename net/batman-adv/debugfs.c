// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2010-2020  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 */

#include "debugfs.h"
#include "main.h"

#include <asm/current.h>
#include <linux/dcache.h>
#include <linux/debugfs.h>
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

static struct dentry *batadv_debugfs;

/**
 * batadv_debugfs_deprecated() - Log use of deprecated batadv debugfs access
 * @file: file which was accessed
 * @alt: explanation what can be used as alternative
 */
void batadv_debugfs_deprecated(struct file *file, const char *alt)
{
	struct dentry *dentry = file_dentry(file);
	const char *name = dentry->d_name.name;

	pr_warn_ratelimited(DEPRECATED "%s (pid %d) Use of debugfs file \"%s\".\n%s",
			    current->comm, task_pid_nr(current), name, alt);
}

static int batadv_algorithms_open(struct inode *inode, struct file *file)
{
	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_ROUTING_ALGOS instead\n");
	return single_open(file, batadv_algo_seq_print_text, NULL);
}

static int neighbors_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_NEIGHBORS instead\n");
	return single_open(file, batadv_hardif_neigh_seq_print_text, net_dev);
}

static int batadv_originators_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_ORIGINATORS instead\n");
	return single_open(file, batadv_orig_seq_print_text, net_dev);
}

/**
 * batadv_originators_hardif_open() - handles debugfs output for the originator
 *  table of an hard interface
 * @inode: inode pointer to debugfs file
 * @file: pointer to the seq_file
 *
 * Return: 0 on success or negative error number in case of failure
 */
static int batadv_originators_hardif_open(struct inode *inode,
					  struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_HARDIFS instead\n");
	return single_open(file, batadv_orig_hardif_seq_print_text, net_dev);
}

static int batadv_gateways_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_GATEWAYS instead\n");
	return single_open(file, batadv_gw_client_seq_print_text, net_dev);
}

static int batadv_transtable_global_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_TRANSTABLE_GLOBAL instead\n");
	return single_open(file, batadv_tt_global_seq_print_text, net_dev);
}

#ifdef CONFIG_BATMAN_ADV_BLA
static int batadv_bla_claim_table_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_BLA_CLAIM instead\n");
	return single_open(file, batadv_bla_claim_table_seq_print_text,
			   net_dev);
}

static int batadv_bla_backbone_table_open(struct inode *inode,
					  struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_debugfs_deprecated(file,
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

	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_DAT_CACHE instead\n");
	return single_open(file, batadv_dat_cache_seq_print_text, net_dev);
}
#endif

static int batadv_transtable_local_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_TRANSTABLE_LOCAL instead\n");
	return single_open(file, batadv_tt_local_seq_print_text, net_dev);
}

struct batadv_debuginfo {
	struct attribute attr;
	const struct file_operations fops;
};

#ifdef CONFIG_BATMAN_ADV_NC
static int batadv_nc_nodes_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;

	batadv_debugfs_deprecated(file, "");
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

	batadv_debugfs_deprecated(file,
				  "Use genl command BATADV_CMD_GET_MCAST_FLAGS instead\n");
	return single_open(file, batadv_mcast_flags_seq_print_text, net_dev);
}
#endif

#define BATADV_DEBUGINFO(_name, _mode, _open)		\
struct batadv_debuginfo batadv_debuginfo_##_name = {	\
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
 * placed in the BATADV_DEBUGFS_SUBDIR subdirectory of debugfs
 */
static BATADV_DEBUGINFO(routing_algos, 0444, batadv_algorithms_open);

static struct batadv_debuginfo *batadv_general_debuginfos[] = {
	&batadv_debuginfo_routing_algos,
	NULL,
};

/* The following attributes are per soft interface */
static BATADV_DEBUGINFO(neighbors, 0444, neighbors_open);
static BATADV_DEBUGINFO(originators, 0444, batadv_originators_open);
static BATADV_DEBUGINFO(gateways, 0444, batadv_gateways_open);
static BATADV_DEBUGINFO(transtable_global, 0444, batadv_transtable_global_open);
#ifdef CONFIG_BATMAN_ADV_BLA
static BATADV_DEBUGINFO(bla_claim_table, 0444, batadv_bla_claim_table_open);
static BATADV_DEBUGINFO(bla_backbone_table, 0444,
			batadv_bla_backbone_table_open);
#endif
#ifdef CONFIG_BATMAN_ADV_DAT
static BATADV_DEBUGINFO(dat_cache, 0444, batadv_dat_cache_open);
#endif
static BATADV_DEBUGINFO(transtable_local, 0444, batadv_transtable_local_open);
#ifdef CONFIG_BATMAN_ADV_NC
static BATADV_DEBUGINFO(nc_nodes, 0444, batadv_nc_nodes_open);
#endif
#ifdef CONFIG_BATMAN_ADV_MCAST
static BATADV_DEBUGINFO(mcast_flags, 0444, batadv_mcast_flags_open);
#endif

static struct batadv_debuginfo *batadv_mesh_debuginfos[] = {
	&batadv_debuginfo_neighbors,
	&batadv_debuginfo_originators,
	&batadv_debuginfo_gateways,
	&batadv_debuginfo_transtable_global,
#ifdef CONFIG_BATMAN_ADV_BLA
	&batadv_debuginfo_bla_claim_table,
	&batadv_debuginfo_bla_backbone_table,
#endif
#ifdef CONFIG_BATMAN_ADV_DAT
	&batadv_debuginfo_dat_cache,
#endif
	&batadv_debuginfo_transtable_local,
#ifdef CONFIG_BATMAN_ADV_NC
	&batadv_debuginfo_nc_nodes,
#endif
#ifdef CONFIG_BATMAN_ADV_MCAST
	&batadv_debuginfo_mcast_flags,
#endif
	NULL,
};

#define BATADV_HARDIF_DEBUGINFO(_name, _mode, _open)		\
struct batadv_debuginfo batadv_hardif_debuginfo_##_name = {	\
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

static BATADV_HARDIF_DEBUGINFO(originators, 0444,
			       batadv_originators_hardif_open);

static struct batadv_debuginfo *batadv_hardif_debuginfos[] = {
	&batadv_hardif_debuginfo_originators,
	NULL,
};

/**
 * batadv_debugfs_init() - Initialize soft interface independent debugfs entries
 */
void batadv_debugfs_init(void)
{
	struct batadv_debuginfo **bat_debug;

	batadv_debugfs = debugfs_create_dir(BATADV_DEBUGFS_SUBDIR, NULL);

	for (bat_debug = batadv_general_debuginfos; *bat_debug; ++bat_debug)
		debugfs_create_file(((*bat_debug)->attr).name,
				    S_IFREG | ((*bat_debug)->attr).mode,
				    batadv_debugfs, NULL, &(*bat_debug)->fops);
}

/**
 * batadv_debugfs_destroy() - Remove all debugfs entries
 */
void batadv_debugfs_destroy(void)
{
	debugfs_remove_recursive(batadv_debugfs);
	batadv_debugfs = NULL;
}

/**
 * batadv_debugfs_add_hardif() - creates the base directory for a hard interface
 *  in debugfs.
 * @hard_iface: hard interface which should be added.
 */
void batadv_debugfs_add_hardif(struct batadv_hard_iface *hard_iface)
{
	struct net *net = dev_net(hard_iface->net_dev);
	struct batadv_debuginfo **bat_debug;

	if (net != &init_net)
		return;

	hard_iface->debug_dir = debugfs_create_dir(hard_iface->net_dev->name,
						   batadv_debugfs);

	for (bat_debug = batadv_hardif_debuginfos; *bat_debug; ++bat_debug)
		debugfs_create_file(((*bat_debug)->attr).name,
				    S_IFREG | ((*bat_debug)->attr).mode,
				    hard_iface->debug_dir, hard_iface->net_dev,
				    &(*bat_debug)->fops);
}

/**
 * batadv_debugfs_rename_hardif() - Fix debugfs path for renamed hardif
 * @hard_iface: hard interface which was renamed
 */
void batadv_debugfs_rename_hardif(struct batadv_hard_iface *hard_iface)
{
	const char *name = hard_iface->net_dev->name;
	struct dentry *dir;

	dir = hard_iface->debug_dir;
	if (!dir)
		return;

	debugfs_rename(dir->d_parent, dir, dir->d_parent, name);
}

/**
 * batadv_debugfs_del_hardif() - delete the base directory for a hard interface
 *  in debugfs.
 * @hard_iface: hard interface which is deleted.
 */
void batadv_debugfs_del_hardif(struct batadv_hard_iface *hard_iface)
{
	struct net *net = dev_net(hard_iface->net_dev);

	if (net != &init_net)
		return;

	if (batadv_debugfs) {
		debugfs_remove_recursive(hard_iface->debug_dir);
		hard_iface->debug_dir = NULL;
	}
}

/**
 * batadv_debugfs_add_meshif() - Initialize interface dependent debugfs entries
 * @dev: netdev struct of the soft interface
 *
 * Return: 0 on success or negative error number in case of failure
 */
int batadv_debugfs_add_meshif(struct net_device *dev)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	struct batadv_debuginfo **bat_debug;
	struct net *net = dev_net(dev);

	if (net != &init_net)
		return 0;

	bat_priv->debug_dir = debugfs_create_dir(dev->name, batadv_debugfs);

	batadv_socket_setup(bat_priv);

	if (batadv_debug_log_setup(bat_priv) < 0)
		goto rem_attr;

	for (bat_debug = batadv_mesh_debuginfos; *bat_debug; ++bat_debug)
		debugfs_create_file(((*bat_debug)->attr).name,
				    S_IFREG | ((*bat_debug)->attr).mode,
				    bat_priv->debug_dir, dev,
				    &(*bat_debug)->fops);

	batadv_nc_init_debugfs(bat_priv);

	return 0;
rem_attr:
	debugfs_remove_recursive(bat_priv->debug_dir);
	bat_priv->debug_dir = NULL;
	return -ENOMEM;
}

/**
 * batadv_debugfs_rename_meshif() - Fix debugfs path for renamed softif
 * @dev: net_device which was renamed
 */
void batadv_debugfs_rename_meshif(struct net_device *dev)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	const char *name = dev->name;
	struct dentry *dir;

	dir = bat_priv->debug_dir;
	if (!dir)
		return;

	debugfs_rename(dir->d_parent, dir, dir->d_parent, name);
}

/**
 * batadv_debugfs_del_meshif() - Remove interface dependent debugfs entries
 * @dev: netdev struct of the soft interface
 */
void batadv_debugfs_del_meshif(struct net_device *dev)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	struct net *net = dev_net(dev);

	if (net != &init_net)
		return;

	batadv_debug_log_cleanup(bat_priv);

	if (batadv_debugfs) {
		debugfs_remove_recursive(bat_priv->debug_dir);
		bat_priv->debug_dir = NULL;
	}
}
