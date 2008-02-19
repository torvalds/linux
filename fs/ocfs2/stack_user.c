/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * stack_user.c
 *
 * Code which interfaces ocfs2 with fs/dlm and a userspace stack.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/reboot.h>
#include <asm/uaccess.h>

#include "stackglue.h"


/*
 * The control protocol starts with a handshake.  Until the handshake
 * is complete, the control device will fail all write(2)s.
 *
 * The handshake is simple.  First, the client reads until EOF.  Each line
 * of output is a supported protocol tag.  All protocol tags are a single
 * character followed by a two hex digit version number.  Currently the
 * only things supported is T01, for "Text-base version 0x01".  Next, the
 * client writes the version they would like to use.  If the version tag
 * written is unknown, -EINVAL is returned.  Once the negotiation is
 * complete, the client can start sending messages.
 */

/*
 * Whether or not the client has done the handshake.
 * For now, we have just one protocol version.
 */
#define OCFS2_CONTROL_PROTO			"T01\n"
#define OCFS2_CONTROL_PROTO_LEN			4
#define OCFS2_CONTROL_HANDSHAKE_INVALID		(0)
#define OCFS2_CONTROL_HANDSHAKE_READ		(1)
#define OCFS2_CONTROL_HANDSHAKE_VALID		(2)

/*
 * ocfs2_live_connection is refcounted because the filesystem and
 * miscdevice sides can detach in different order.  Let's just be safe.
 */
struct ocfs2_live_connection {
	struct list_head		oc_list;
	struct ocfs2_cluster_connection	*oc_conn;
};

struct ocfs2_control_private {
	struct list_head op_list;
	int op_state;
};

static atomic_t ocfs2_control_opened;

static LIST_HEAD(ocfs2_live_connection_list);
static LIST_HEAD(ocfs2_control_private_list);
static DEFINE_MUTEX(ocfs2_control_lock);

static inline void ocfs2_control_set_handshake_state(struct file *file,
						     int state)
{
	struct ocfs2_control_private *p = file->private_data;
	p->op_state = state;
}

static inline int ocfs2_control_get_handshake_state(struct file *file)
{
	struct ocfs2_control_private *p = file->private_data;
	return p->op_state;
}

static struct ocfs2_live_connection *ocfs2_connection_find(const char *name)
{
	size_t len = strlen(name);
	struct ocfs2_live_connection *c;

	BUG_ON(!mutex_is_locked(&ocfs2_control_lock));

	list_for_each_entry(c, &ocfs2_live_connection_list, oc_list) {
		if ((c->oc_conn->cc_namelen == len) &&
		    !strncmp(c->oc_conn->cc_name, name, len))
			return c;
	}

	return c;
}

/*
 * ocfs2_live_connection structures are created underneath the ocfs2
 * mount path.  Since the VFS prevents multiple calls to
 * fill_super(), we can't get dupes here.
 */
static int ocfs2_live_connection_new(struct ocfs2_cluster_connection *conn,
				     struct ocfs2_live_connection **c_ret)
{
	int rc = 0;
	struct ocfs2_live_connection *c;

	c = kzalloc(sizeof(struct ocfs2_live_connection), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	mutex_lock(&ocfs2_control_lock);
	c->oc_conn = conn;

	if (atomic_read(&ocfs2_control_opened))
		list_add(&c->oc_list, &ocfs2_live_connection_list);
	else {
		printk(KERN_ERR
		       "ocfs2: Userspace control daemon is not present\n");
		rc = -ESRCH;
	}

	mutex_unlock(&ocfs2_control_lock);

	if (!rc)
		*c_ret = c;
	else
		kfree(c);

	return rc;
}

/*
 * This function disconnects the cluster connection from ocfs2_control.
 * Afterwards, userspace can't affect the cluster connection.
 */
static void ocfs2_live_connection_drop(struct ocfs2_live_connection *c)
{
	mutex_lock(&ocfs2_control_lock);
	list_del_init(&c->oc_list);
	c->oc_conn = NULL;
	mutex_unlock(&ocfs2_control_lock);

	kfree(c);
}

static ssize_t ocfs2_control_cfu(char *target, size_t target_len,
				 const char __user *buf, size_t count)
{
	/* The T01 expects write(2) calls to have exactly one command */
	if (count != target_len)
		return -EINVAL;

	if (copy_from_user(target, buf, target_len))
		return -EFAULT;

	return count;
}

static ssize_t ocfs2_control_validate_handshake(struct file *file,
						const char __user *buf,
						size_t count)
{
	ssize_t ret;
	char kbuf[OCFS2_CONTROL_PROTO_LEN];

	ret = ocfs2_control_cfu(kbuf, OCFS2_CONTROL_PROTO_LEN,
				buf, count);
	if (ret != count)
		return ret;

	if (strncmp(kbuf, OCFS2_CONTROL_PROTO, OCFS2_CONTROL_PROTO_LEN))
		return -EINVAL;

	atomic_inc(&ocfs2_control_opened);
	ocfs2_control_set_handshake_state(file,
					  OCFS2_CONTROL_HANDSHAKE_VALID);


	return count;
}


static ssize_t ocfs2_control_write(struct file *file,
				   const char __user *buf,
				   size_t count,
				   loff_t *ppos)
{
	ssize_t ret;

	switch (ocfs2_control_get_handshake_state(file)) {
		case OCFS2_CONTROL_HANDSHAKE_INVALID:
			ret = -EINVAL;
			break;

		case OCFS2_CONTROL_HANDSHAKE_READ:
			ret = ocfs2_control_validate_handshake(file, buf,
							       count);
			break;

		case OCFS2_CONTROL_HANDSHAKE_VALID:
			ret = count;  /* XXX */
			break;

		default:
			BUG();
			ret = -EIO;
			break;
	}

	return ret;
}

/*
 * This is a naive version.  If we ever have a new protocol, we'll expand
 * it.  Probably using seq_file.
 */
static ssize_t ocfs2_control_read(struct file *file,
				  char __user *buf,
				  size_t count,
				  loff_t *ppos)
{
	char *proto_string = OCFS2_CONTROL_PROTO;
	size_t to_write = 0;

	if (*ppos >= OCFS2_CONTROL_PROTO_LEN)
		return 0;

	to_write = OCFS2_CONTROL_PROTO_LEN - *ppos;
	if (to_write > count)
		to_write = count;
	if (copy_to_user(buf, proto_string + *ppos, to_write))
		return -EFAULT;

	*ppos += to_write;

	/* Have we read the whole protocol list? */
	if (*ppos >= OCFS2_CONTROL_PROTO_LEN)
		ocfs2_control_set_handshake_state(file,
						  OCFS2_CONTROL_HANDSHAKE_READ);

	return to_write;
}

static int ocfs2_control_release(struct inode *inode, struct file *file)
{
	struct ocfs2_control_private *p = file->private_data;

	mutex_lock(&ocfs2_control_lock);

	if (ocfs2_control_get_handshake_state(file) !=
	    OCFS2_CONTROL_HANDSHAKE_VALID)
		goto out;

	if (atomic_dec_and_test(&ocfs2_control_opened)) {
		if (!list_empty(&ocfs2_live_connection_list)) {
			/* XXX: Do bad things! */
			printk(KERN_ERR
			       "ocfs2: Unexpected release of ocfs2_control!\n"
			       "       Loss of cluster connection requires "
			       "an emergency restart!\n");
			emergency_restart();
		}
	}

out:
	list_del_init(&p->op_list);
	file->private_data = NULL;

	mutex_unlock(&ocfs2_control_lock);

	kfree(p);

	return 0;
}

static int ocfs2_control_open(struct inode *inode, struct file *file)
{
	struct ocfs2_control_private *p;

	p = kzalloc(sizeof(struct ocfs2_control_private), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	mutex_lock(&ocfs2_control_lock);
	file->private_data = p;
	list_add(&p->op_list, &ocfs2_control_private_list);
	mutex_unlock(&ocfs2_control_lock);

	return 0;
}

static const struct file_operations ocfs2_control_fops = {
	.open    = ocfs2_control_open,
	.release = ocfs2_control_release,
	.read    = ocfs2_control_read,
	.write   = ocfs2_control_write,
	.owner   = THIS_MODULE,
};

struct miscdevice ocfs2_control_device = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "ocfs2_control",
	.fops		= &ocfs2_control_fops,
};

static int ocfs2_control_init(void)
{
	int rc;

	atomic_set(&ocfs2_control_opened, 0);

	rc = misc_register(&ocfs2_control_device);
	if (rc)
		printk(KERN_ERR
		       "ocfs2: Unable to register ocfs2_control device "
		       "(errno %d)\n",
		       -rc);

	return rc;
}

static void ocfs2_control_exit(void)
{
	int rc;

	rc = misc_deregister(&ocfs2_control_device);
	if (rc)
		printk(KERN_ERR
		       "ocfs2: Unable to deregister ocfs2_control device "
		       "(errno %d)\n",
		       -rc);
}

static int __init user_stack_init(void)
{
	return ocfs2_control_init();
}

static void __exit user_stack_exit(void)
{
	ocfs2_control_exit();
}

MODULE_AUTHOR("Oracle");
MODULE_DESCRIPTION("ocfs2 driver for userspace cluster stacks");
MODULE_LICENSE("GPL");
module_init(user_stack_init);
module_exit(user_stack_exit);
