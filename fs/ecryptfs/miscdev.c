/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 2008 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mhalcrow@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/fs.h>
#include <linux/hash.h>
#include <linux/random.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/module.h>
#include "ecryptfs_kernel.h"

static atomic_t ecryptfs_num_miscdev_opens;

/**
 * ecryptfs_miscdev_poll
 * @file: dev file (ignored)
 * @pt: dev poll table (ignored)
 *
 * Returns the poll mask
 */
static unsigned int
ecryptfs_miscdev_poll(struct file *file, poll_table *pt)
{
	struct ecryptfs_daemon *daemon;
	unsigned int mask = 0;
	uid_t euid = current_euid();
	int rc;

	mutex_lock(&ecryptfs_daemon_hash_mux);
	/* TODO: Just use file->private_data? */
	rc = ecryptfs_find_daemon_by_euid(&daemon, euid, current_user_ns());
	BUG_ON(rc || !daemon);
	mutex_lock(&daemon->mux);
	mutex_unlock(&ecryptfs_daemon_hash_mux);
	if (daemon->flags & ECRYPTFS_DAEMON_ZOMBIE) {
		printk(KERN_WARNING "%s: Attempt to poll on zombified "
		       "daemon\n", __func__);
		goto out_unlock_daemon;
	}
	if (daemon->flags & ECRYPTFS_DAEMON_IN_READ)
		goto out_unlock_daemon;
	if (daemon->flags & ECRYPTFS_DAEMON_IN_POLL)
		goto out_unlock_daemon;
	daemon->flags |= ECRYPTFS_DAEMON_IN_POLL;
	mutex_unlock(&daemon->mux);
	poll_wait(file, &daemon->wait, pt);
	mutex_lock(&daemon->mux);
	if (!list_empty(&daemon->msg_ctx_out_queue))
		mask |= POLLIN | POLLRDNORM;
out_unlock_daemon:
	daemon->flags &= ~ECRYPTFS_DAEMON_IN_POLL;
	mutex_unlock(&daemon->mux);
	return mask;
}

/**
 * ecryptfs_miscdev_open
 * @inode: inode of miscdev handle (ignored)
 * @file: file for miscdev handle (ignored)
 *
 * Returns zero on success; non-zero otherwise
 */
static int
ecryptfs_miscdev_open(struct inode *inode, struct file *file)
{
	struct ecryptfs_daemon *daemon = NULL;
	uid_t euid = current_euid();
	int rc;

	mutex_lock(&ecryptfs_daemon_hash_mux);
	rc = try_module_get(THIS_MODULE);
	if (rc == 0) {
		rc = -EIO;
		printk(KERN_ERR "%s: Error attempting to increment module use "
		       "count; rc = [%d]\n", __func__, rc);
		goto out_unlock_daemon_list;
	}
	rc = ecryptfs_find_daemon_by_euid(&daemon, euid, current_user_ns());
	if (rc || !daemon) {
		rc = ecryptfs_spawn_daemon(&daemon, euid, current_user_ns(),
					   task_pid(current));
		if (rc) {
			printk(KERN_ERR "%s: Error attempting to spawn daemon; "
			       "rc = [%d]\n", __func__, rc);
			goto out_module_put_unlock_daemon_list;
		}
	}
	mutex_lock(&daemon->mux);
	if (daemon->pid != task_pid(current)) {
		rc = -EINVAL;
		printk(KERN_ERR "%s: pid [0x%p] has registered with euid [%d], "
		       "but pid [0x%p] has attempted to open the handle "
		       "instead\n", __func__, daemon->pid, daemon->euid,
		       task_pid(current));
		goto out_unlock_daemon;
	}
	if (daemon->flags & ECRYPTFS_DAEMON_MISCDEV_OPEN) {
		rc = -EBUSY;
		printk(KERN_ERR "%s: Miscellaneous device handle may only be "
		       "opened once per daemon; pid [0x%p] already has this "
		       "handle open\n", __func__, daemon->pid);
		goto out_unlock_daemon;
	}
	daemon->flags |= ECRYPTFS_DAEMON_MISCDEV_OPEN;
	atomic_inc(&ecryptfs_num_miscdev_opens);
out_unlock_daemon:
	mutex_unlock(&daemon->mux);
out_module_put_unlock_daemon_list:
	if (rc)
		module_put(THIS_MODULE);
out_unlock_daemon_list:
	mutex_unlock(&ecryptfs_daemon_hash_mux);
	return rc;
}

/**
 * ecryptfs_miscdev_release
 * @inode: inode of fs/ecryptfs/euid handle (ignored)
 * @file: file for fs/ecryptfs/euid handle (ignored)
 *
 * This keeps the daemon registered until the daemon sends another
 * ioctl to fs/ecryptfs/ctl or until the kernel module unregisters.
 *
 * Returns zero on success; non-zero otherwise
 */
static int
ecryptfs_miscdev_release(struct inode *inode, struct file *file)
{
	struct ecryptfs_daemon *daemon = NULL;
	uid_t euid = current_euid();
	int rc;

	mutex_lock(&ecryptfs_daemon_hash_mux);
	rc = ecryptfs_find_daemon_by_euid(&daemon, euid, current_user_ns());
	BUG_ON(rc || !daemon);
	mutex_lock(&daemon->mux);
	BUG_ON(daemon->pid != task_pid(current));
	BUG_ON(!(daemon->flags & ECRYPTFS_DAEMON_MISCDEV_OPEN));
	daemon->flags &= ~ECRYPTFS_DAEMON_MISCDEV_OPEN;
	atomic_dec(&ecryptfs_num_miscdev_opens);
	mutex_unlock(&daemon->mux);
	rc = ecryptfs_exorcise_daemon(daemon);
	if (rc) {
		printk(KERN_CRIT "%s: Fatal error whilst attempting to "
		       "shut down daemon; rc = [%d]. Please report this "
		       "bug.\n", __func__, rc);
		BUG();
	}
	module_put(THIS_MODULE);
	mutex_unlock(&ecryptfs_daemon_hash_mux);
	return rc;
}

/**
 * ecryptfs_send_miscdev
 * @data: Data to send to daemon; may be NULL
 * @data_size: Amount of data to send to daemon
 * @msg_ctx: Message context, which is used to handle the reply. If
 *           this is NULL, then we do not expect a reply.
 * @msg_type: Type of message
 * @msg_flags: Flags for message
 * @daemon: eCryptfs daemon object
 *
 * Add msg_ctx to queue and then, if it exists, notify the blocked
 * miscdevess about the data being available. Must be called with
 * ecryptfs_daemon_hash_mux held.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_send_miscdev(char *data, size_t data_size,
			  struct ecryptfs_msg_ctx *msg_ctx, u8 msg_type,
			  u16 msg_flags, struct ecryptfs_daemon *daemon)
{
	int rc = 0;

	mutex_lock(&msg_ctx->mux);
	msg_ctx->msg = kmalloc((sizeof(*msg_ctx->msg) + data_size),
			       GFP_KERNEL);
	if (!msg_ctx->msg) {
		rc = -ENOMEM;
		printk(KERN_ERR "%s: Out of memory whilst attempting "
		       "to kmalloc(%zd, GFP_KERNEL)\n", __func__,
		       (sizeof(*msg_ctx->msg) + data_size));
		goto out_unlock;
	}
	msg_ctx->msg->index = msg_ctx->index;
	msg_ctx->msg->data_len = data_size;
	msg_ctx->type = msg_type;
	memcpy(msg_ctx->msg->data, data, data_size);
	msg_ctx->msg_size = (sizeof(*msg_ctx->msg) + data_size);
	mutex_lock(&daemon->mux);
	list_add_tail(&msg_ctx->daemon_out_list, &daemon->msg_ctx_out_queue);
	daemon->num_queued_msg_ctx++;
	wake_up_interruptible(&daemon->wait);
	mutex_unlock(&daemon->mux);
out_unlock:
	mutex_unlock(&msg_ctx->mux);
	return rc;
}

/**
 * ecryptfs_miscdev_read - format and send message from queue
 * @file: fs/ecryptfs/euid miscdevfs handle (ignored)
 * @buf: User buffer into which to copy the next message on the daemon queue
 * @count: Amount of space available in @buf
 * @ppos: Offset in file (ignored)
 *
 * Pulls the most recent message from the daemon queue, formats it for
 * being sent via a miscdevfs handle, and copies it into @buf
 *
 * Returns the number of bytes copied into the user buffer
 */
static ssize_t
ecryptfs_miscdev_read(struct file *file, char __user *buf, size_t count,
		      loff_t *ppos)
{
	struct ecryptfs_daemon *daemon;
	struct ecryptfs_msg_ctx *msg_ctx;
	size_t packet_length_size;
	char packet_length[3];
	size_t i;
	size_t total_length;
	uid_t euid = current_euid();
	int rc;

	mutex_lock(&ecryptfs_daemon_hash_mux);
	/* TODO: Just use file->private_data? */
	rc = ecryptfs_find_daemon_by_euid(&daemon, euid, current_user_ns());
	BUG_ON(rc || !daemon);
	mutex_lock(&daemon->mux);
	if (daemon->flags & ECRYPTFS_DAEMON_ZOMBIE) {
		rc = 0;
		mutex_unlock(&ecryptfs_daemon_hash_mux);
		printk(KERN_WARNING "%s: Attempt to read from zombified "
		       "daemon\n", __func__);
		goto out_unlock_daemon;
	}
	if (daemon->flags & ECRYPTFS_DAEMON_IN_READ) {
		rc = 0;
		mutex_unlock(&ecryptfs_daemon_hash_mux);
		goto out_unlock_daemon;
	}
	/* This daemon will not go away so long as this flag is set */
	daemon->flags |= ECRYPTFS_DAEMON_IN_READ;
	mutex_unlock(&ecryptfs_daemon_hash_mux);
check_list:
	if (list_empty(&daemon->msg_ctx_out_queue)) {
		mutex_unlock(&daemon->mux);
		rc = wait_event_interruptible(
			daemon->wait, !list_empty(&daemon->msg_ctx_out_queue));
		mutex_lock(&daemon->mux);
		if (rc < 0) {
			rc = 0;
			goto out_unlock_daemon;
		}
	}
	if (daemon->flags & ECRYPTFS_DAEMON_ZOMBIE) {
		rc = 0;
		goto out_unlock_daemon;
	}
	if (list_empty(&daemon->msg_ctx_out_queue)) {
		/* Something else jumped in since the
		 * wait_event_interruptable() and removed the
		 * message from the queue; try again */
		goto check_list;
	}
	BUG_ON(euid != daemon->euid);
	BUG_ON(current_user_ns() != daemon->user_ns);
	BUG_ON(task_pid(current) != daemon->pid);
	msg_ctx = list_first_entry(&daemon->msg_ctx_out_queue,
				   struct ecryptfs_msg_ctx, daemon_out_list);
	BUG_ON(!msg_ctx);
	mutex_lock(&msg_ctx->mux);
	if (msg_ctx->msg) {
		rc = ecryptfs_write_packet_length(packet_length,
						  msg_ctx->msg_size,
						  &packet_length_size);
		if (rc) {
			rc = 0;
			printk(KERN_WARNING "%s: Error writing packet length; "
			       "rc = [%d]\n", __func__, rc);
			goto out_unlock_msg_ctx;
		}
	} else {
		packet_length_size = 0;
		msg_ctx->msg_size = 0;
	}
	/* miscdevfs packet format:
	 *  Octet 0: Type
	 *  Octets 1-4: network byte order msg_ctx->counter
	 *  Octets 5-N0: Size of struct ecryptfs_message to follow
	 *  Octets N0-N1: struct ecryptfs_message (including data)
	 *
	 *  Octets 5-N1 not written if the packet type does not
	 *  include a message */
	total_length = (1 + 4 + packet_length_size + msg_ctx->msg_size);
	if (count < total_length) {
		rc = 0;
		printk(KERN_WARNING "%s: Only given user buffer of "
		       "size [%zd], but we need [%zd] to read the "
		       "pending message\n", __func__, count, total_length);
		goto out_unlock_msg_ctx;
	}
	rc = -EFAULT;
	if (put_user(msg_ctx->type, buf))
		goto out_unlock_msg_ctx;
	if (put_user(cpu_to_be32(msg_ctx->counter), (__be32 __user *)(buf + 1)))
		goto out_unlock_msg_ctx;
	i = 5;
	if (msg_ctx->msg) {
		if (copy_to_user(&buf[i], packet_length, packet_length_size))
			goto out_unlock_msg_ctx;
		i += packet_length_size;
		if (copy_to_user(&buf[i], msg_ctx->msg, msg_ctx->msg_size))
			goto out_unlock_msg_ctx;
		i += msg_ctx->msg_size;
	}
	rc = i;
	list_del(&msg_ctx->daemon_out_list);
	kfree(msg_ctx->msg);
	msg_ctx->msg = NULL;
	/* We do not expect a reply from the userspace daemon for any
	 * message type other than ECRYPTFS_MSG_REQUEST */
	if (msg_ctx->type != ECRYPTFS_MSG_REQUEST)
		ecryptfs_msg_ctx_alloc_to_free(msg_ctx);
out_unlock_msg_ctx:
	mutex_unlock(&msg_ctx->mux);
out_unlock_daemon:
	daemon->flags &= ~ECRYPTFS_DAEMON_IN_READ;
	mutex_unlock(&daemon->mux);
	return rc;
}

/**
 * ecryptfs_miscdev_response - miscdevess response to message previously sent to daemon
 * @data: Bytes comprising struct ecryptfs_message
 * @data_size: sizeof(struct ecryptfs_message) + data len
 * @euid: Effective user id of miscdevess sending the miscdev response
 * @user_ns: The namespace in which @euid applies
 * @pid: Miscdevess id of miscdevess sending the miscdev response
 * @seq: Sequence number for miscdev response packet
 *
 * Returns zero on success; non-zero otherwise
 */
static int ecryptfs_miscdev_response(char *data, size_t data_size,
				     uid_t euid, struct user_namespace *user_ns,
				     struct pid *pid, u32 seq)
{
	struct ecryptfs_message *msg = (struct ecryptfs_message *)data;
	int rc;

	if ((sizeof(*msg) + msg->data_len) != data_size) {
		printk(KERN_WARNING "%s: (sizeof(*msg) + msg->data_len) = "
		       "[%zd]; data_size = [%zd]. Invalid packet.\n", __func__,
		       (sizeof(*msg) + msg->data_len), data_size);
		rc = -EINVAL;
		goto out;
	}
	rc = ecryptfs_process_response(msg, euid, user_ns, pid, seq);
	if (rc)
		printk(KERN_ERR
		       "Error processing response message; rc = [%d]\n", rc);
out:
	return rc;
}

/**
 * ecryptfs_miscdev_write - handle write to daemon miscdev handle
 * @file: File for misc dev handle (ignored)
 * @buf: Buffer containing user data
 * @count: Amount of data in @buf
 * @ppos: Pointer to offset in file (ignored)
 *
 * miscdevfs packet format:
 *  Octet 0: Type
 *  Octets 1-4: network byte order msg_ctx->counter (0's for non-response)
 *  Octets 5-N0: Size of struct ecryptfs_message to follow
 *  Octets N0-N1: struct ecryptfs_message (including data)
 *
 * Returns the number of bytes read from @buf
 */
static ssize_t
ecryptfs_miscdev_write(struct file *file, const char __user *buf,
		       size_t count, loff_t *ppos)
{
	__be32 counter_nbo;
	u32 seq;
	size_t packet_size, packet_size_length, i;
	ssize_t sz = 0;
	char *data;
	uid_t euid = current_euid();
	int rc;

	if (count == 0)
		goto out;

	data = memdup_user(buf, count);
	if (IS_ERR(data)) {
		printk(KERN_ERR "%s: memdup_user returned error [%ld]\n",
		       __func__, PTR_ERR(data));
		goto out;
	}
	sz = count;
	i = 0;
	switch (data[i++]) {
	case ECRYPTFS_MSG_RESPONSE:
		if (count < (1 + 4 + 1 + sizeof(struct ecryptfs_message))) {
			printk(KERN_WARNING "%s: Minimum acceptable packet "
			       "size is [%zd], but amount of data written is "
			       "only [%zd]. Discarding response packet.\n",
			       __func__,
			       (1 + 4 + 1 + sizeof(struct ecryptfs_message)),
			       count);
			goto out_free;
		}
		memcpy(&counter_nbo, &data[i], 4);
		seq = be32_to_cpu(counter_nbo);
		i += 4;
		rc = ecryptfs_parse_packet_length(&data[i], &packet_size,
						  &packet_size_length);
		if (rc) {
			printk(KERN_WARNING "%s: Error parsing packet length; "
			       "rc = [%d]\n", __func__, rc);
			goto out_free;
		}
		i += packet_size_length;
		if ((1 + 4 + packet_size_length + packet_size) != count) {
			printk(KERN_WARNING "%s: (1 + packet_size_length([%zd])"
			       " + packet_size([%zd]))([%zd]) != "
			       "count([%zd]). Invalid packet format.\n",
			       __func__, packet_size_length, packet_size,
			       (1 + packet_size_length + packet_size), count);
			goto out_free;
		}
		rc = ecryptfs_miscdev_response(&data[i], packet_size,
					       euid, current_user_ns(),
					       task_pid(current), seq);
		if (rc)
			printk(KERN_WARNING "%s: Failed to deliver miscdev "
			       "response to requesting operation; rc = [%d]\n",
			       __func__, rc);
		break;
	case ECRYPTFS_MSG_HELO:
	case ECRYPTFS_MSG_QUIT:
		break;
	default:
		ecryptfs_printk(KERN_WARNING, "Dropping miscdev "
				"message of unrecognized type [%d]\n",
				data[0]);
		break;
	}
out_free:
	kfree(data);
out:
	return sz;
}


static const struct file_operations ecryptfs_miscdev_fops = {
	.open    = ecryptfs_miscdev_open,
	.poll    = ecryptfs_miscdev_poll,
	.read    = ecryptfs_miscdev_read,
	.write   = ecryptfs_miscdev_write,
	.release = ecryptfs_miscdev_release,
};

static struct miscdevice ecryptfs_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "ecryptfs",
	.fops  = &ecryptfs_miscdev_fops
};

/**
 * ecryptfs_init_ecryptfs_miscdev
 *
 * Messages sent to the userspace daemon from the kernel are placed on
 * a queue associated with the daemon. The next read against the
 * miscdev handle by that daemon will return the oldest message placed
 * on the message queue for the daemon.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_init_ecryptfs_miscdev(void)
{
	int rc;

	atomic_set(&ecryptfs_num_miscdev_opens, 0);
	rc = misc_register(&ecryptfs_miscdev);
	if (rc)
		printk(KERN_ERR "%s: Failed to register miscellaneous device "
		       "for communications with userspace daemons; rc = [%d]\n",
		       __func__, rc);
	return rc;
}

/**
 * ecryptfs_destroy_ecryptfs_miscdev
 *
 * All of the daemons must be exorcised prior to calling this
 * function.
 */
void ecryptfs_destroy_ecryptfs_miscdev(void)
{
	BUG_ON(atomic_read(&ecryptfs_num_miscdev_opens) != 0);
	misc_deregister(&ecryptfs_miscdev);
}
