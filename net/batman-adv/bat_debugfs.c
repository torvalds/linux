/* Copyright (C) 2010-2012 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include "main.h"

#include <linux/debugfs.h>

#include "bat_debugfs.h"
#include "translation-table.h"
#include "originator.h"
#include "hard-interface.h"
#include "gateway_common.h"
#include "gateway_client.h"
#include "soft-interface.h"
#include "vis.h"
#include "icmp_socket.h"
#include "bridge_loop_avoidance.h"

static struct dentry *batadv_debugfs;

#ifdef CONFIG_BATMAN_ADV_DEBUG
#define LOG_BUFF_MASK (batadv_log_buff_len - 1)
#define LOG_BUFF(idx) (debug_log->log_buff[(idx) & LOG_BUFF_MASK])

static int batadv_log_buff_len = LOG_BUF_LEN;

static void batadv_emit_log_char(struct debug_log *debug_log, char c)
{
	LOG_BUFF(debug_log->log_end) = c;
	debug_log->log_end++;

	if (debug_log->log_end - debug_log->log_start > batadv_log_buff_len)
		debug_log->log_start = debug_log->log_end - batadv_log_buff_len;
}

__printf(2, 3)
static int batadv_fdebug_log(struct debug_log *debug_log, const char *fmt, ...)
{
	va_list args;
	static char debug_log_buf[256];
	char *p;

	if (!debug_log)
		return 0;

	spin_lock_bh(&debug_log->lock);
	va_start(args, fmt);
	vscnprintf(debug_log_buf, sizeof(debug_log_buf), fmt, args);
	va_end(args);

	for (p = debug_log_buf; *p != 0; p++)
		batadv_emit_log_char(debug_log, *p);

	spin_unlock_bh(&debug_log->lock);

	wake_up(&debug_log->queue_wait);

	return 0;
}

int batadv_debug_log(struct bat_priv *bat_priv, const char *fmt, ...)
{
	va_list args;
	char tmp_log_buf[256];

	va_start(args, fmt);
	vscnprintf(tmp_log_buf, sizeof(tmp_log_buf), fmt, args);
	batadv_fdebug_log(bat_priv->debug_log, "[%10u] %s",
			  jiffies_to_msecs(jiffies), tmp_log_buf);
	va_end(args);

	return 0;
}

static int batadv_log_open(struct inode *inode, struct file *file)
{
	nonseekable_open(inode, file);
	file->private_data = inode->i_private;
	batadv_inc_module_count();
	return 0;
}

static int batadv_log_release(struct inode *inode, struct file *file)
{
	batadv_dec_module_count();
	return 0;
}

static ssize_t batadv_log_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct bat_priv *bat_priv = file->private_data;
	struct debug_log *debug_log = bat_priv->debug_log;
	int error, i = 0;
	char c;

	if ((file->f_flags & O_NONBLOCK) &&
	    !(debug_log->log_end - debug_log->log_start))
		return -EAGAIN;

	if (!buf)
		return -EINVAL;

	if (count == 0)
		return 0;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	error = wait_event_interruptible(debug_log->queue_wait,
				(debug_log->log_start - debug_log->log_end));

	if (error)
		return error;

	spin_lock_bh(&debug_log->lock);

	while ((!error) && (i < count) &&
	       (debug_log->log_start != debug_log->log_end)) {
		c = LOG_BUFF(debug_log->log_start);

		debug_log->log_start++;

		spin_unlock_bh(&debug_log->lock);

		error = __put_user(c, buf);

		spin_lock_bh(&debug_log->lock);

		buf++;
		i++;

	}

	spin_unlock_bh(&debug_log->lock);

	if (!error)
		return i;

	return error;
}

static unsigned int batadv_log_poll(struct file *file, poll_table *wait)
{
	struct bat_priv *bat_priv = file->private_data;
	struct debug_log *debug_log = bat_priv->debug_log;

	poll_wait(file, &debug_log->queue_wait, wait);

	if (debug_log->log_end - debug_log->log_start)
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations batadv_log_fops = {
	.open           = batadv_log_open,
	.release        = batadv_log_release,
	.read           = batadv_log_read,
	.poll           = batadv_log_poll,
	.llseek         = no_llseek,
};

static int batadv_debug_log_setup(struct bat_priv *bat_priv)
{
	struct dentry *d;

	if (!bat_priv->debug_dir)
		goto err;

	bat_priv->debug_log = kzalloc(sizeof(*bat_priv->debug_log), GFP_ATOMIC);
	if (!bat_priv->debug_log)
		goto err;

	spin_lock_init(&bat_priv->debug_log->lock);
	init_waitqueue_head(&bat_priv->debug_log->queue_wait);

	d = debugfs_create_file("log", S_IFREG | S_IRUSR,
				bat_priv->debug_dir, bat_priv,
				&batadv_log_fops);
	if (!d)
		goto err;

	return 0;

err:
	return -ENOMEM;
}

static void batadv_debug_log_cleanup(struct bat_priv *bat_priv)
{
	kfree(bat_priv->debug_log);
	bat_priv->debug_log = NULL;
}
#else /* CONFIG_BATMAN_ADV_DEBUG */
static int batadv_debug_log_setup(struct bat_priv *bat_priv)
{
	bat_priv->debug_log = NULL;
	return 0;
}

static void batadv_debug_log_cleanup(struct bat_priv *bat_priv)
{
	return;
}
#endif

static int batadv_algorithms_open(struct inode *inode, struct file *file)
{
	return single_open(file, batadv_algo_seq_print_text, NULL);
}

static int batadv_originators_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, batadv_orig_seq_print_text, net_dev);
}

static int batadv_gateways_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, batadv_gw_client_seq_print_text, net_dev);
}

static int batadv_transtable_global_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, batadv_tt_global_seq_print_text, net_dev);
}

#ifdef CONFIG_BATMAN_ADV_BLA
static int batadv_bla_claim_table_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, batadv_bla_claim_table_seq_print_text,
			   net_dev);
}
#endif

static int batadv_transtable_local_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, batadv_tt_local_seq_print_text, net_dev);
}

static int batadv_vis_data_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, batadv_vis_seq_print_text, net_dev);
}

struct bat_debuginfo {
	struct attribute attr;
	const struct file_operations fops;
};

#define BAT_DEBUGINFO(_name, _mode, _open)		\
struct bat_debuginfo batadv_debuginfo_##_name = {	\
	.attr = { .name = __stringify(_name),		\
		  .mode = _mode, },			\
	.fops = { .owner = THIS_MODULE,			\
		  .open = _open,			\
		  .read	= seq_read,			\
		  .llseek = seq_lseek,			\
		  .release = single_release,		\
		}					\
};

static BAT_DEBUGINFO(routing_algos, S_IRUGO, batadv_algorithms_open);
static BAT_DEBUGINFO(originators, S_IRUGO, batadv_originators_open);
static BAT_DEBUGINFO(gateways, S_IRUGO, batadv_gateways_open);
static BAT_DEBUGINFO(transtable_global, S_IRUGO, batadv_transtable_global_open);
#ifdef CONFIG_BATMAN_ADV_BLA
static BAT_DEBUGINFO(bla_claim_table, S_IRUGO, batadv_bla_claim_table_open);
#endif
static BAT_DEBUGINFO(transtable_local, S_IRUGO, batadv_transtable_local_open);
static BAT_DEBUGINFO(vis_data, S_IRUGO, batadv_vis_data_open);

static struct bat_debuginfo *batadv_mesh_debuginfos[] = {
	&batadv_debuginfo_originators,
	&batadv_debuginfo_gateways,
	&batadv_debuginfo_transtable_global,
#ifdef CONFIG_BATMAN_ADV_BLA
	&batadv_debuginfo_bla_claim_table,
#endif
	&batadv_debuginfo_transtable_local,
	&batadv_debuginfo_vis_data,
	NULL,
};

void batadv_debugfs_init(void)
{
	struct bat_debuginfo *bat_debug;
	struct dentry *file;

	batadv_debugfs = debugfs_create_dir(DEBUGFS_BAT_SUBDIR, NULL);
	if (batadv_debugfs == ERR_PTR(-ENODEV))
		batadv_debugfs = NULL;

	if (!batadv_debugfs)
		goto out;

	bat_debug = &batadv_debuginfo_routing_algos;
	file = debugfs_create_file(bat_debug->attr.name,
				   S_IFREG | bat_debug->attr.mode,
				   batadv_debugfs, NULL, &bat_debug->fops);
	if (!file)
		pr_err("Can't add debugfs file: %s\n", bat_debug->attr.name);

out:
	return;
}

void batadv_debugfs_destroy(void)
{
	if (batadv_debugfs) {
		debugfs_remove_recursive(batadv_debugfs);
		batadv_debugfs = NULL;
	}
}

int batadv_debugfs_add_meshif(struct net_device *dev)
{
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct bat_debuginfo **bat_debug;
	struct dentry *file;

	if (!batadv_debugfs)
		goto out;

	bat_priv->debug_dir = debugfs_create_dir(dev->name, batadv_debugfs);
	if (!bat_priv->debug_dir)
		goto out;

	if (batadv_socket_setup(bat_priv) < 0)
		goto rem_attr;

	if (batadv_debug_log_setup(bat_priv) < 0)
		goto rem_attr;

	for (bat_debug = batadv_mesh_debuginfos; *bat_debug; ++bat_debug) {
		file = debugfs_create_file(((*bat_debug)->attr).name,
					  S_IFREG | ((*bat_debug)->attr).mode,
					  bat_priv->debug_dir,
					  dev, &(*bat_debug)->fops);
		if (!file) {
			batadv_err(dev, "Can't add debugfs file: %s/%s\n",
				   dev->name, ((*bat_debug)->attr).name);
			goto rem_attr;
		}
	}

	return 0;
rem_attr:
	debugfs_remove_recursive(bat_priv->debug_dir);
	bat_priv->debug_dir = NULL;
out:
#ifdef CONFIG_DEBUG_FS
	return -ENOMEM;
#else
	return 0;
#endif /* CONFIG_DEBUG_FS */
}

void batadv_debugfs_del_meshif(struct net_device *dev)
{
	struct bat_priv *bat_priv = netdev_priv(dev);

	batadv_debug_log_cleanup(bat_priv);

	if (batadv_debugfs) {
		debugfs_remove_recursive(bat_priv->debug_dir);
		bat_priv->debug_dir = NULL;
	}
}
