// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2010-2018  B.A.T.M.A.N. contributors:
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

#include "log.h"
#include "main.h"

#include <linux/compiler.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/eventpoll.h>
#include <linux/export.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/sched.h> /* for linux/wait.h */
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <stdarg.h>

#include "debugfs.h"
#include "trace.h"

#ifdef CONFIG_BATMAN_ADV_DEBUGFS

#define BATADV_LOG_BUFF_MASK (batadv_log_buff_len - 1)

static const int batadv_log_buff_len = BATADV_LOG_BUF_LEN;

static char *batadv_log_char_addr(struct batadv_priv_debug_log *debug_log,
				  size_t idx)
{
	return &debug_log->log_buff[idx & BATADV_LOG_BUFF_MASK];
}

static void batadv_emit_log_char(struct batadv_priv_debug_log *debug_log,
				 char c)
{
	char *char_addr;

	char_addr = batadv_log_char_addr(debug_log, debug_log->log_end);
	*char_addr = c;
	debug_log->log_end++;

	if (debug_log->log_end - debug_log->log_start > batadv_log_buff_len)
		debug_log->log_start = debug_log->log_end - batadv_log_buff_len;
}

__printf(2, 3)
static int batadv_fdebug_log(struct batadv_priv_debug_log *debug_log,
			     const char *fmt, ...)
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

static int batadv_log_open(struct inode *inode, struct file *file)
{
	if (!try_module_get(THIS_MODULE))
		return -EBUSY;

	batadv_debugfs_deprecated(file,
				  "Use tracepoint batadv:batadv_dbg instead\n");

	nonseekable_open(inode, file);
	file->private_data = inode->i_private;
	return 0;
}

static int batadv_log_release(struct inode *inode, struct file *file)
{
	module_put(THIS_MODULE);
	return 0;
}

static bool batadv_log_empty(struct batadv_priv_debug_log *debug_log)
{
	return !(debug_log->log_start - debug_log->log_end);
}

static ssize_t batadv_log_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct batadv_priv *bat_priv = file->private_data;
	struct batadv_priv_debug_log *debug_log = bat_priv->debug_log;
	int error, i = 0;
	char *char_addr;
	char c;

	if ((file->f_flags & O_NONBLOCK) && batadv_log_empty(debug_log))
		return -EAGAIN;

	if (!buf)
		return -EINVAL;

	if (count == 0)
		return 0;

	if (!access_ok(buf, count))
		return -EFAULT;

	error = wait_event_interruptible(debug_log->queue_wait,
					 (!batadv_log_empty(debug_log)));

	if (error)
		return error;

	spin_lock_bh(&debug_log->lock);

	while ((!error) && (i < count) &&
	       (debug_log->log_start != debug_log->log_end)) {
		char_addr = batadv_log_char_addr(debug_log,
						 debug_log->log_start);
		c = *char_addr;

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

static __poll_t batadv_log_poll(struct file *file, poll_table *wait)
{
	struct batadv_priv *bat_priv = file->private_data;
	struct batadv_priv_debug_log *debug_log = bat_priv->debug_log;

	poll_wait(file, &debug_log->queue_wait, wait);

	if (!batadv_log_empty(debug_log))
		return EPOLLIN | EPOLLRDNORM;

	return 0;
}

static const struct file_operations batadv_log_fops = {
	.open           = batadv_log_open,
	.release        = batadv_log_release,
	.read           = batadv_log_read,
	.poll           = batadv_log_poll,
	.llseek         = no_llseek,
};

/**
 * batadv_debug_log_setup() - Initialize debug log
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: 0 on success or negative error number in case of failure
 */
int batadv_debug_log_setup(struct batadv_priv *bat_priv)
{
	struct dentry *d;

	if (!bat_priv->debug_dir)
		goto err;

	bat_priv->debug_log = kzalloc(sizeof(*bat_priv->debug_log), GFP_ATOMIC);
	if (!bat_priv->debug_log)
		goto err;

	spin_lock_init(&bat_priv->debug_log->lock);
	init_waitqueue_head(&bat_priv->debug_log->queue_wait);

	d = debugfs_create_file("log", 0400, bat_priv->debug_dir, bat_priv,
				&batadv_log_fops);
	if (!d)
		goto err;

	return 0;

err:
	return -ENOMEM;
}

/**
 * batadv_debug_log_cleanup() - Destroy debug log
 * @bat_priv: the bat priv with all the soft interface information
 */
void batadv_debug_log_cleanup(struct batadv_priv *bat_priv)
{
	kfree(bat_priv->debug_log);
	bat_priv->debug_log = NULL;
}

#endif /* CONFIG_BATMAN_ADV_DEBUGFS */

/**
 * batadv_debug_log() - Add debug log entry
 * @bat_priv: the bat priv with all the soft interface information
 * @fmt: format string
 *
 * Return: 0 on success or negative error number in case of failure
 */
int batadv_debug_log(struct batadv_priv *bat_priv, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

#ifdef CONFIG_BATMAN_ADV_DEBUGFS
	batadv_fdebug_log(bat_priv->debug_log, "[%10u] %pV",
			  jiffies_to_msecs(jiffies), &vaf);
#endif

	trace_batadv_dbg(bat_priv, &vaf);

	va_end(args);

	return 0;
}
