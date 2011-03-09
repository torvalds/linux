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
#include <linux/slab.h>
#include <linux/reboot.h>
#include <asm/uaccess.h>

#include "stackglue.h"

#include <linux/dlm_plock.h>

/*
 * The control protocol starts with a handshake.  Until the handshake
 * is complete, the control device will fail all write(2)s.
 *
 * The handshake is simple.  First, the client reads until EOF.  Each line
 * of output is a supported protocol tag.  All protocol tags are a single
 * character followed by a two hex digit version number.  Currently the
 * only things supported is T01, for "Text-base version 0x01".  Next, the
 * client writes the version they would like to use, including the newline.
 * Thus, the protocol tag is 'T01\n'.  If the version tag written is
 * unknown, -EINVAL is returned.  Once the negotiation is complete, the
 * client can start sending messages.
 *
 * The T01 protocol has three messages.  First is the "SETN" message.
 * It has the following syntax:
 *
 *  SETN<space><8-char-hex-nodenum><newline>
 *
 * This is 14 characters.
 *
 * The "SETN" message must be the first message following the protocol.
 * It tells ocfs2_control the local node number.
 *
 * Next comes the "SETV" message.  It has the following syntax:
 *
 *  SETV<space><2-char-hex-major><space><2-char-hex-minor><newline>
 *
 * This is 11 characters.
 *
 * The "SETV" message sets the filesystem locking protocol version as
 * negotiated by the client.  The client negotiates based on the maximum
 * version advertised in /sys/fs/ocfs2/max_locking_protocol.  The major
 * number from the "SETV" message must match
 * ocfs2_user_plugin.sp_max_proto.pv_major, and the minor number
 * must be less than or equal to ...sp_max_version.pv_minor.
 *
 * Once this information has been set, mounts will be allowed.  From this
 * point on, the "DOWN" message can be sent for node down notification.
 * It has the following syntax:
 *
 *  DOWN<space><32-char-cap-hex-uuid><space><8-char-hex-nodenum><newline>
 *
 * eg:
 *
 *  DOWN 632A924FDD844190BDA93C0DF6B94899 00000001\n
 *
 * This is 47 characters.
 */

/*
 * Whether or not the client has done the handshake.
 * For now, we have just one protocol version.
 */
#define OCFS2_CONTROL_PROTO			"T01\n"
#define OCFS2_CONTROL_PROTO_LEN			4

/* Handshake states */
#define OCFS2_CONTROL_HANDSHAKE_INVALID		(0)
#define OCFS2_CONTROL_HANDSHAKE_READ		(1)
#define OCFS2_CONTROL_HANDSHAKE_PROTOCOL	(2)
#define OCFS2_CONTROL_HANDSHAKE_VALID		(3)

/* Messages */
#define OCFS2_CONTROL_MESSAGE_OP_LEN		4
#define OCFS2_CONTROL_MESSAGE_SETNODE_OP	"SETN"
#define OCFS2_CONTROL_MESSAGE_SETNODE_TOTAL_LEN	14
#define OCFS2_CONTROL_MESSAGE_SETVERSION_OP	"SETV"
#define OCFS2_CONTROL_MESSAGE_SETVERSION_TOTAL_LEN	11
#define OCFS2_CONTROL_MESSAGE_DOWN_OP		"DOWN"
#define OCFS2_CONTROL_MESSAGE_DOWN_TOTAL_LEN	47
#define OCFS2_TEXT_UUID_LEN			32
#define OCFS2_CONTROL_MESSAGE_VERNUM_LEN	2
#define OCFS2_CONTROL_MESSAGE_NODENUM_LEN	8

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
	int op_this_node;
	struct ocfs2_protocol_version op_proto;
};

/* SETN<space><8-char-hex-nodenum><newline> */
struct ocfs2_control_message_setn {
	char	tag[OCFS2_CONTROL_MESSAGE_OP_LEN];
	char	space;
	char	nodestr[OCFS2_CONTROL_MESSAGE_NODENUM_LEN];
	char	newline;
};

/* SETV<space><2-char-hex-major><space><2-char-hex-minor><newline> */
struct ocfs2_control_message_setv {
	char	tag[OCFS2_CONTROL_MESSAGE_OP_LEN];
	char	space1;
	char	major[OCFS2_CONTROL_MESSAGE_VERNUM_LEN];
	char	space2;
	char	minor[OCFS2_CONTROL_MESSAGE_VERNUM_LEN];
	char	newline;
};

/* DOWN<space><32-char-cap-hex-uuid><space><8-char-hex-nodenum><newline> */
struct ocfs2_control_message_down {
	char	tag[OCFS2_CONTROL_MESSAGE_OP_LEN];
	char	space1;
	char	uuid[OCFS2_TEXT_UUID_LEN];
	char	space2;
	char	nodestr[OCFS2_CONTROL_MESSAGE_NODENUM_LEN];
	char	newline;
};

union ocfs2_control_message {
	char					tag[OCFS2_CONTROL_MESSAGE_OP_LEN];
	struct ocfs2_control_message_setn	u_setn;
	struct ocfs2_control_message_setv	u_setv;
	struct ocfs2_control_message_down	u_down;
};

static struct ocfs2_stack_plugin ocfs2_user_plugin;

static atomic_t ocfs2_control_opened;
static int ocfs2_control_this_node = -1;
static struct ocfs2_protocol_version running_proto;

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

	return NULL;
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

static int ocfs2_control_cfu(void *target, size_t target_len,
			     const char __user *buf, size_t count)
{
	/* The T01 expects write(2) calls to have exactly one command */
	if ((count != target_len) ||
	    (count > sizeof(union ocfs2_control_message)))
		return -EINVAL;

	if (copy_from_user(target, buf, target_len))
		return -EFAULT;

	return 0;
}

static ssize_t ocfs2_control_validate_protocol(struct file *file,
					       const char __user *buf,
					       size_t count)
{
	ssize_t ret;
	char kbuf[OCFS2_CONTROL_PROTO_LEN];

	ret = ocfs2_control_cfu(kbuf, OCFS2_CONTROL_PROTO_LEN,
				buf, count);
	if (ret)
		return ret;

	if (strncmp(kbuf, OCFS2_CONTROL_PROTO, OCFS2_CONTROL_PROTO_LEN))
		return -EINVAL;

	ocfs2_control_set_handshake_state(file,
					  OCFS2_CONTROL_HANDSHAKE_PROTOCOL);

	return count;
}

static void ocfs2_control_send_down(const char *uuid,
				    int nodenum)
{
	struct ocfs2_live_connection *c;

	mutex_lock(&ocfs2_control_lock);

	c = ocfs2_connection_find(uuid);
	if (c) {
		BUG_ON(c->oc_conn == NULL);
		c->oc_conn->cc_recovery_handler(nodenum,
						c->oc_conn->cc_recovery_data);
	}

	mutex_unlock(&ocfs2_control_lock);
}

/*
 * Called whenever configuration elements are sent to /dev/ocfs2_control.
 * If all configuration elements are present, try to set the global
 * values.  If there is a problem, return an error.  Skip any missing
 * elements, and only bump ocfs2_control_opened when we have all elements
 * and are successful.
 */
static int ocfs2_control_install_private(struct file *file)
{
	int rc = 0;
	int set_p = 1;
	struct ocfs2_control_private *p = file->private_data;

	BUG_ON(p->op_state != OCFS2_CONTROL_HANDSHAKE_PROTOCOL);

	mutex_lock(&ocfs2_control_lock);

	if (p->op_this_node < 0) {
		set_p = 0;
	} else if ((ocfs2_control_this_node >= 0) &&
		   (ocfs2_control_this_node != p->op_this_node)) {
		rc = -EINVAL;
		goto out_unlock;
	}

	if (!p->op_proto.pv_major) {
		set_p = 0;
	} else if (!list_empty(&ocfs2_live_connection_list) &&
		   ((running_proto.pv_major != p->op_proto.pv_major) ||
		    (running_proto.pv_minor != p->op_proto.pv_minor))) {
		rc = -EINVAL;
		goto out_unlock;
	}

	if (set_p) {
		ocfs2_control_this_node = p->op_this_node;
		running_proto.pv_major = p->op_proto.pv_major;
		running_proto.pv_minor = p->op_proto.pv_minor;
	}

out_unlock:
	mutex_unlock(&ocfs2_control_lock);

	if (!rc && set_p) {
		/* We set the global values successfully */
		atomic_inc(&ocfs2_control_opened);
		ocfs2_control_set_handshake_state(file,
					OCFS2_CONTROL_HANDSHAKE_VALID);
	}

	return rc;
}

static int ocfs2_control_get_this_node(void)
{
	int rc;

	mutex_lock(&ocfs2_control_lock);
	if (ocfs2_control_this_node < 0)
		rc = -EINVAL;
	else
		rc = ocfs2_control_this_node;
	mutex_unlock(&ocfs2_control_lock);

	return rc;
}

static int ocfs2_control_do_setnode_msg(struct file *file,
					struct ocfs2_control_message_setn *msg)
{
	long nodenum;
	char *ptr = NULL;
	struct ocfs2_control_private *p = file->private_data;

	if (ocfs2_control_get_handshake_state(file) !=
	    OCFS2_CONTROL_HANDSHAKE_PROTOCOL)
		return -EINVAL;

	if (strncmp(msg->tag, OCFS2_CONTROL_MESSAGE_SETNODE_OP,
		    OCFS2_CONTROL_MESSAGE_OP_LEN))
		return -EINVAL;

	if ((msg->space != ' ') || (msg->newline != '\n'))
		return -EINVAL;
	msg->space = msg->newline = '\0';

	nodenum = simple_strtol(msg->nodestr, &ptr, 16);
	if (!ptr || *ptr)
		return -EINVAL;

	if ((nodenum == LONG_MIN) || (nodenum == LONG_MAX) ||
	    (nodenum > INT_MAX) || (nodenum < 0))
		return -ERANGE;
	p->op_this_node = nodenum;

	return ocfs2_control_install_private(file);
}

static int ocfs2_control_do_setversion_msg(struct file *file,
					   struct ocfs2_control_message_setv *msg)
 {
	long major, minor;
	char *ptr = NULL;
	struct ocfs2_control_private *p = file->private_data;
	struct ocfs2_protocol_version *max =
		&ocfs2_user_plugin.sp_max_proto;

	if (ocfs2_control_get_handshake_state(file) !=
	    OCFS2_CONTROL_HANDSHAKE_PROTOCOL)
		return -EINVAL;

	if (strncmp(msg->tag, OCFS2_CONTROL_MESSAGE_SETVERSION_OP,
		    OCFS2_CONTROL_MESSAGE_OP_LEN))
		return -EINVAL;

	if ((msg->space1 != ' ') || (msg->space2 != ' ') ||
	    (msg->newline != '\n'))
		return -EINVAL;
	msg->space1 = msg->space2 = msg->newline = '\0';

	major = simple_strtol(msg->major, &ptr, 16);
	if (!ptr || *ptr)
		return -EINVAL;
	minor = simple_strtol(msg->minor, &ptr, 16);
	if (!ptr || *ptr)
		return -EINVAL;

	/*
	 * The major must be between 1 and 255, inclusive.  The minor
	 * must be between 0 and 255, inclusive.  The version passed in
	 * must be within the maximum version supported by the filesystem.
	 */
	if ((major == LONG_MIN) || (major == LONG_MAX) ||
	    (major > (u8)-1) || (major < 1))
		return -ERANGE;
	if ((minor == LONG_MIN) || (minor == LONG_MAX) ||
	    (minor > (u8)-1) || (minor < 0))
		return -ERANGE;
	if ((major != max->pv_major) ||
	    (minor > max->pv_minor))
		return -EINVAL;

	p->op_proto.pv_major = major;
	p->op_proto.pv_minor = minor;

	return ocfs2_control_install_private(file);
}

static int ocfs2_control_do_down_msg(struct file *file,
				     struct ocfs2_control_message_down *msg)
{
	long nodenum;
	char *p = NULL;

	if (ocfs2_control_get_handshake_state(file) !=
	    OCFS2_CONTROL_HANDSHAKE_VALID)
		return -EINVAL;

	if (strncmp(msg->tag, OCFS2_CONTROL_MESSAGE_DOWN_OP,
		    OCFS2_CONTROL_MESSAGE_OP_LEN))
		return -EINVAL;

	if ((msg->space1 != ' ') || (msg->space2 != ' ') ||
	    (msg->newline != '\n'))
		return -EINVAL;
	msg->space1 = msg->space2 = msg->newline = '\0';

	nodenum = simple_strtol(msg->nodestr, &p, 16);
	if (!p || *p)
		return -EINVAL;

	if ((nodenum == LONG_MIN) || (nodenum == LONG_MAX) ||
	    (nodenum > INT_MAX) || (nodenum < 0))
		return -ERANGE;

	ocfs2_control_send_down(msg->uuid, nodenum);

	return 0;
}

static ssize_t ocfs2_control_message(struct file *file,
				     const char __user *buf,
				     size_t count)
{
	ssize_t ret;
	union ocfs2_control_message msg;

	/* Try to catch padding issues */
	WARN_ON(offsetof(struct ocfs2_control_message_down, uuid) !=
		(sizeof(msg.u_down.tag) + sizeof(msg.u_down.space1)));

	memset(&msg, 0, sizeof(union ocfs2_control_message));
	ret = ocfs2_control_cfu(&msg, count, buf, count);
	if (ret)
		goto out;

	if ((count == OCFS2_CONTROL_MESSAGE_SETNODE_TOTAL_LEN) &&
	    !strncmp(msg.tag, OCFS2_CONTROL_MESSAGE_SETNODE_OP,
		     OCFS2_CONTROL_MESSAGE_OP_LEN))
		ret = ocfs2_control_do_setnode_msg(file, &msg.u_setn);
	else if ((count == OCFS2_CONTROL_MESSAGE_SETVERSION_TOTAL_LEN) &&
		 !strncmp(msg.tag, OCFS2_CONTROL_MESSAGE_SETVERSION_OP,
			  OCFS2_CONTROL_MESSAGE_OP_LEN))
		ret = ocfs2_control_do_setversion_msg(file, &msg.u_setv);
	else if ((count == OCFS2_CONTROL_MESSAGE_DOWN_TOTAL_LEN) &&
		 !strncmp(msg.tag, OCFS2_CONTROL_MESSAGE_DOWN_OP,
			  OCFS2_CONTROL_MESSAGE_OP_LEN))
		ret = ocfs2_control_do_down_msg(file, &msg.u_down);
	else
		ret = -EINVAL;

out:
	return ret ? ret : count;
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
			ret = ocfs2_control_validate_protocol(file, buf,
							      count);
			break;

		case OCFS2_CONTROL_HANDSHAKE_PROTOCOL:
		case OCFS2_CONTROL_HANDSHAKE_VALID:
			ret = ocfs2_control_message(file, buf, count);
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
	ssize_t ret;

	ret = simple_read_from_buffer(buf, count, ppos,
			OCFS2_CONTROL_PROTO, OCFS2_CONTROL_PROTO_LEN);

	/* Have we read the whole protocol list? */
	if (ret > 0 && *ppos >= OCFS2_CONTROL_PROTO_LEN)
		ocfs2_control_set_handshake_state(file,
						  OCFS2_CONTROL_HANDSHAKE_READ);

	return ret;
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
		/*
		 * Last valid close clears the node number and resets
		 * the locking protocol version
		 */
		ocfs2_control_this_node = -1;
		running_proto.pv_major = 0;
		running_proto.pv_major = 0;
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
	p->op_this_node = -1;

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
	.llseek  = default_llseek,
};

static struct miscdevice ocfs2_control_device = {
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

static void fsdlm_lock_ast_wrapper(void *astarg)
{
	struct ocfs2_dlm_lksb *lksb = astarg;
	int status = lksb->lksb_fsdlm.sb_status;

	/*
	 * For now we're punting on the issue of other non-standard errors
	 * where we can't tell if the unlock_ast or lock_ast should be called.
	 * The main "other error" that's possible is EINVAL which means the
	 * function was called with invalid args, which shouldn't be possible
	 * since the caller here is under our control.  Other non-standard
	 * errors probably fall into the same category, or otherwise are fatal
	 * which means we can't carry on anyway.
	 */

	if (status == -DLM_EUNLOCK || status == -DLM_ECANCEL)
		lksb->lksb_conn->cc_proto->lp_unlock_ast(lksb, 0);
	else
		lksb->lksb_conn->cc_proto->lp_lock_ast(lksb);
}

static void fsdlm_blocking_ast_wrapper(void *astarg, int level)
{
	struct ocfs2_dlm_lksb *lksb = astarg;

	lksb->lksb_conn->cc_proto->lp_blocking_ast(lksb, level);
}

static int user_dlm_lock(struct ocfs2_cluster_connection *conn,
			 int mode,
			 struct ocfs2_dlm_lksb *lksb,
			 u32 flags,
			 void *name,
			 unsigned int namelen)
{
	int ret;

	if (!lksb->lksb_fsdlm.sb_lvbptr)
		lksb->lksb_fsdlm.sb_lvbptr = (char *)lksb +
					     sizeof(struct dlm_lksb);

	ret = dlm_lock(conn->cc_lockspace, mode, &lksb->lksb_fsdlm,
		       flags|DLM_LKF_NODLCKWT, name, namelen, 0,
		       fsdlm_lock_ast_wrapper, lksb,
		       fsdlm_blocking_ast_wrapper);
	return ret;
}

static int user_dlm_unlock(struct ocfs2_cluster_connection *conn,
			   struct ocfs2_dlm_lksb *lksb,
			   u32 flags)
{
	int ret;

	ret = dlm_unlock(conn->cc_lockspace, lksb->lksb_fsdlm.sb_lkid,
			 flags, &lksb->lksb_fsdlm, lksb);
	return ret;
}

static int user_dlm_lock_status(struct ocfs2_dlm_lksb *lksb)
{
	return lksb->lksb_fsdlm.sb_status;
}

static int user_dlm_lvb_valid(struct ocfs2_dlm_lksb *lksb)
{
	int invalid = lksb->lksb_fsdlm.sb_flags & DLM_SBF_VALNOTVALID;

	return !invalid;
}

static void *user_dlm_lvb(struct ocfs2_dlm_lksb *lksb)
{
	if (!lksb->lksb_fsdlm.sb_lvbptr)
		lksb->lksb_fsdlm.sb_lvbptr = (char *)lksb +
					     sizeof(struct dlm_lksb);
	return (void *)(lksb->lksb_fsdlm.sb_lvbptr);
}

static void user_dlm_dump_lksb(struct ocfs2_dlm_lksb *lksb)
{
}

static int user_plock(struct ocfs2_cluster_connection *conn,
		      u64 ino,
		      struct file *file,
		      int cmd,
		      struct file_lock *fl)
{
	/*
	 * This more or less just demuxes the plock request into any
	 * one of three dlm calls.
	 *
	 * Internally, fs/dlm will pass these to a misc device, which
	 * a userspace daemon will read and write to.
	 *
	 * For now, cancel requests (which happen internally only),
	 * are turned into unlocks. Most of this function taken from
	 * gfs2_lock.
	 */

	if (cmd == F_CANCELLK) {
		cmd = F_SETLK;
		fl->fl_type = F_UNLCK;
	}

	if (IS_GETLK(cmd))
		return dlm_posix_get(conn->cc_lockspace, ino, file, fl);
	else if (fl->fl_type == F_UNLCK)
		return dlm_posix_unlock(conn->cc_lockspace, ino, file, fl);
	else
		return dlm_posix_lock(conn->cc_lockspace, ino, file, cmd, fl);
}

/*
 * Compare a requested locking protocol version against the current one.
 *
 * If the major numbers are different, they are incompatible.
 * If the current minor is greater than the request, they are incompatible.
 * If the current minor is less than or equal to the request, they are
 * compatible, and the requester should run at the current minor version.
 */
static int fs_protocol_compare(struct ocfs2_protocol_version *existing,
			       struct ocfs2_protocol_version *request)
{
	if (existing->pv_major != request->pv_major)
		return 1;

	if (existing->pv_minor > request->pv_minor)
		return 1;

	if (existing->pv_minor < request->pv_minor)
		request->pv_minor = existing->pv_minor;

	return 0;
}

static int user_cluster_connect(struct ocfs2_cluster_connection *conn)
{
	dlm_lockspace_t *fsdlm;
	struct ocfs2_live_connection *uninitialized_var(control);
	int rc = 0;

	BUG_ON(conn == NULL);

	rc = ocfs2_live_connection_new(conn, &control);
	if (rc)
		goto out;

	/*
	 * running_proto must have been set before we allowed any mounts
	 * to proceed.
	 */
	if (fs_protocol_compare(&running_proto, &conn->cc_version)) {
		printk(KERN_ERR
		       "Unable to mount with fs locking protocol version "
		       "%u.%u because the userspace control daemon has "
		       "negotiated %u.%u\n",
		       conn->cc_version.pv_major, conn->cc_version.pv_minor,
		       running_proto.pv_major, running_proto.pv_minor);
		rc = -EPROTO;
		ocfs2_live_connection_drop(control);
		goto out;
	}

	rc = dlm_new_lockspace(conn->cc_name, strlen(conn->cc_name),
			       &fsdlm, DLM_LSFL_FS, DLM_LVB_LEN);
	if (rc) {
		ocfs2_live_connection_drop(control);
		goto out;
	}

	conn->cc_private = control;
	conn->cc_lockspace = fsdlm;
out:
	return rc;
}

static int user_cluster_disconnect(struct ocfs2_cluster_connection *conn)
{
	dlm_release_lockspace(conn->cc_lockspace, 2);
	conn->cc_lockspace = NULL;
	ocfs2_live_connection_drop(conn->cc_private);
	conn->cc_private = NULL;
	return 0;
}

static int user_cluster_this_node(unsigned int *this_node)
{
	int rc;

	rc = ocfs2_control_get_this_node();
	if (rc < 0)
		return rc;

	*this_node = rc;
	return 0;
}

static struct ocfs2_stack_operations ocfs2_user_plugin_ops = {
	.connect	= user_cluster_connect,
	.disconnect	= user_cluster_disconnect,
	.this_node	= user_cluster_this_node,
	.dlm_lock	= user_dlm_lock,
	.dlm_unlock	= user_dlm_unlock,
	.lock_status	= user_dlm_lock_status,
	.lvb_valid	= user_dlm_lvb_valid,
	.lock_lvb	= user_dlm_lvb,
	.plock		= user_plock,
	.dump_lksb	= user_dlm_dump_lksb,
};

static struct ocfs2_stack_plugin ocfs2_user_plugin = {
	.sp_name	= "user",
	.sp_ops		= &ocfs2_user_plugin_ops,
	.sp_owner	= THIS_MODULE,
};


static int __init ocfs2_user_plugin_init(void)
{
	int rc;

	rc = ocfs2_control_init();
	if (!rc) {
		rc = ocfs2_stack_glue_register(&ocfs2_user_plugin);
		if (rc)
			ocfs2_control_exit();
	}

	return rc;
}

static void __exit ocfs2_user_plugin_exit(void)
{
	ocfs2_stack_glue_unregister(&ocfs2_user_plugin);
	ocfs2_control_exit();
}

MODULE_AUTHOR("Oracle");
MODULE_DESCRIPTION("ocfs2 driver for userspace cluster stacks");
MODULE_LICENSE("GPL");
module_init(ocfs2_user_plugin_init);
module_exit(ocfs2_user_plugin_exit);
