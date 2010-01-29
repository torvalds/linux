/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * stack_o2cb.c
 *
 * Code which interfaces ocfs2 with the o2cb stack.
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

#include <linux/kernel.h>
#include <linux/crc32.h>
#include <linux/module.h>

/* Needed for AOP_TRUNCATED_PAGE in mlog_errno() */
#include <linux/fs.h>

#include "cluster/masklog.h"
#include "cluster/nodemanager.h"
#include "cluster/heartbeat.h"

#include "stackglue.h"

struct o2dlm_private {
	struct dlm_eviction_cb op_eviction_cb;
};

static struct ocfs2_stack_plugin o2cb_stack;

/* These should be identical */
#if (DLM_LOCK_IV != LKM_IVMODE)
# error Lock modes do not match
#endif
#if (DLM_LOCK_NL != LKM_NLMODE)
# error Lock modes do not match
#endif
#if (DLM_LOCK_CR != LKM_CRMODE)
# error Lock modes do not match
#endif
#if (DLM_LOCK_CW != LKM_CWMODE)
# error Lock modes do not match
#endif
#if (DLM_LOCK_PR != LKM_PRMODE)
# error Lock modes do not match
#endif
#if (DLM_LOCK_PW != LKM_PWMODE)
# error Lock modes do not match
#endif
#if (DLM_LOCK_EX != LKM_EXMODE)
# error Lock modes do not match
#endif
static inline int mode_to_o2dlm(int mode)
{
	BUG_ON(mode > LKM_MAXMODE);

	return mode;
}

#define map_flag(_generic, _o2dlm)		\
	if (flags & (_generic)) {		\
		flags &= ~(_generic);		\
		o2dlm_flags |= (_o2dlm);	\
	}
static int flags_to_o2dlm(u32 flags)
{
	int o2dlm_flags = 0;

	map_flag(DLM_LKF_NOQUEUE, LKM_NOQUEUE);
	map_flag(DLM_LKF_CANCEL, LKM_CANCEL);
	map_flag(DLM_LKF_CONVERT, LKM_CONVERT);
	map_flag(DLM_LKF_VALBLK, LKM_VALBLK);
	map_flag(DLM_LKF_IVVALBLK, LKM_INVVALBLK);
	map_flag(DLM_LKF_ORPHAN, LKM_ORPHAN);
	map_flag(DLM_LKF_FORCEUNLOCK, LKM_FORCE);
	map_flag(DLM_LKF_TIMEOUT, LKM_TIMEOUT);
	map_flag(DLM_LKF_LOCAL, LKM_LOCAL);

	/* map_flag() should have cleared every flag passed in */
	BUG_ON(flags != 0);

	return o2dlm_flags;
}
#undef map_flag

/*
 * Map an o2dlm status to standard errno values.
 *
 * o2dlm only uses a handful of these, and returns even fewer to the
 * caller. Still, we try to assign sane values to each error.
 *
 * The following value pairs have special meanings to dlmglue, thus
 * the right hand side needs to stay unique - never duplicate the
 * mapping elsewhere in the table!
 *
 * DLM_NORMAL:		0
 * DLM_NOTQUEUED:	-EAGAIN
 * DLM_CANCELGRANT:	-EBUSY
 * DLM_CANCEL:		-DLM_ECANCEL
 */
/* Keep in sync with dlmapi.h */
static int status_map[] = {
	[DLM_NORMAL]			= 0,		/* Success */
	[DLM_GRANTED]			= -EINVAL,
	[DLM_DENIED]			= -EACCES,
	[DLM_DENIED_NOLOCKS]		= -EACCES,
	[DLM_WORKING]			= -EACCES,
	[DLM_BLOCKED]			= -EINVAL,
	[DLM_BLOCKED_ORPHAN]		= -EINVAL,
	[DLM_DENIED_GRACE_PERIOD]	= -EACCES,
	[DLM_SYSERR]			= -ENOMEM,	/* It is what it is */
	[DLM_NOSUPPORT]			= -EPROTO,
	[DLM_CANCELGRANT]		= -EBUSY,	/* Cancel after grant */
	[DLM_IVLOCKID]			= -EINVAL,
	[DLM_SYNC]			= -EINVAL,
	[DLM_BADTYPE]			= -EINVAL,
	[DLM_BADRESOURCE]		= -EINVAL,
	[DLM_MAXHANDLES]		= -ENOMEM,
	[DLM_NOCLINFO]			= -EINVAL,
	[DLM_NOLOCKMGR]			= -EINVAL,
	[DLM_NOPURGED]			= -EINVAL,
	[DLM_BADARGS]			= -EINVAL,
	[DLM_VOID]			= -EINVAL,
	[DLM_NOTQUEUED]			= -EAGAIN,	/* Trylock failed */
	[DLM_IVBUFLEN]			= -EINVAL,
	[DLM_CVTUNGRANT]		= -EPERM,
	[DLM_BADPARAM]			= -EINVAL,
	[DLM_VALNOTVALID]		= -EINVAL,
	[DLM_REJECTED]			= -EPERM,
	[DLM_ABORT]			= -EINVAL,
	[DLM_CANCEL]			= -DLM_ECANCEL,	/* Successful cancel */
	[DLM_IVRESHANDLE]		= -EINVAL,
	[DLM_DEADLOCK]			= -EDEADLK,
	[DLM_DENIED_NOASTS]		= -EINVAL,
	[DLM_FORWARD]			= -EINVAL,
	[DLM_TIMEOUT]			= -ETIMEDOUT,
	[DLM_IVGROUPID]			= -EINVAL,
	[DLM_VERS_CONFLICT]		= -EOPNOTSUPP,
	[DLM_BAD_DEVICE_PATH]		= -ENOENT,
	[DLM_NO_DEVICE_PERMISSION]	= -EPERM,
	[DLM_NO_CONTROL_DEVICE]		= -ENOENT,
	[DLM_RECOVERING]		= -ENOTCONN,
	[DLM_MIGRATING]			= -ERESTART,
	[DLM_MAXSTATS]			= -EINVAL,
};

static int dlm_status_to_errno(enum dlm_status status)
{
	BUG_ON(status < 0 || status >= ARRAY_SIZE(status_map));

	return status_map[status];
}

static void o2dlm_lock_ast_wrapper(void *astarg)
{
	union ocfs2_dlm_lksb *lksb = astarg;

	BUG_ON(o2cb_stack.sp_proto == NULL);

	o2cb_stack.sp_proto->lp_lock_ast(lksb);
}

static void o2dlm_blocking_ast_wrapper(void *astarg, int level)
{
	union ocfs2_dlm_lksb *lksb = astarg;

	BUG_ON(o2cb_stack.sp_proto == NULL);

	o2cb_stack.sp_proto->lp_blocking_ast(lksb, level);
}

static void o2dlm_unlock_ast_wrapper(void *astarg, enum dlm_status status)
{
	union ocfs2_dlm_lksb *lksb = astarg;

	int error = dlm_status_to_errno(status);

	BUG_ON(o2cb_stack.sp_proto == NULL);

	/*
	 * In o2dlm, you can get both the lock_ast() for the lock being
	 * granted and the unlock_ast() for the CANCEL failing.  A
	 * successful cancel sends DLM_NORMAL here.  If the
	 * lock grant happened before the cancel arrived, you get
	 * DLM_CANCELGRANT.
	 *
	 * There's no need for the double-ast.  If we see DLM_CANCELGRANT,
	 * we just ignore it.  We expect the lock_ast() to handle the
	 * granted lock.
	 */
	if (status == DLM_CANCELGRANT)
		return;

	o2cb_stack.sp_proto->lp_unlock_ast(lksb, error);
}

static int o2cb_dlm_lock(struct ocfs2_cluster_connection *conn,
			 int mode,
			 union ocfs2_dlm_lksb *lksb,
			 u32 flags,
			 void *name,
			 unsigned int namelen)
{
	enum dlm_status status;
	int o2dlm_mode = mode_to_o2dlm(mode);
	int o2dlm_flags = flags_to_o2dlm(flags);
	int ret;

	status = dlmlock(conn->cc_lockspace, o2dlm_mode, &lksb->lksb_o2dlm,
			 o2dlm_flags, name, namelen,
			 o2dlm_lock_ast_wrapper, lksb,
			 o2dlm_blocking_ast_wrapper);
	ret = dlm_status_to_errno(status);
	return ret;
}

static int o2cb_dlm_unlock(struct ocfs2_cluster_connection *conn,
			   union ocfs2_dlm_lksb *lksb,
			   u32 flags)
{
	enum dlm_status status;
	int o2dlm_flags = flags_to_o2dlm(flags);
	int ret;

	status = dlmunlock(conn->cc_lockspace, &lksb->lksb_o2dlm,
			   o2dlm_flags, o2dlm_unlock_ast_wrapper, lksb);
	ret = dlm_status_to_errno(status);
	return ret;
}

static int o2cb_dlm_lock_status(union ocfs2_dlm_lksb *lksb)
{
	return dlm_status_to_errno(lksb->lksb_o2dlm.status);
}

/*
 * o2dlm aways has a "valid" LVB. If the dlm loses track of the LVB
 * contents, it will zero out the LVB.  Thus the caller can always trust
 * the contents.
 */
static int o2cb_dlm_lvb_valid(union ocfs2_dlm_lksb *lksb)
{
	return 1;
}

static void *o2cb_dlm_lvb(union ocfs2_dlm_lksb *lksb)
{
	return (void *)(lksb->lksb_o2dlm.lvb);
}

static void o2cb_dump_lksb(union ocfs2_dlm_lksb *lksb)
{
	dlm_print_one_lock(lksb->lksb_o2dlm.lockid);
}

/*
 * Called from the dlm when it's about to evict a node. This is how the
 * classic stack signals node death.
 */
static void o2dlm_eviction_cb(int node_num, void *data)
{
	struct ocfs2_cluster_connection *conn = data;

	mlog(ML_NOTICE, "o2dlm has evicted node %d from group %.*s\n",
	     node_num, conn->cc_namelen, conn->cc_name);

	conn->cc_recovery_handler(node_num, conn->cc_recovery_data);
}

static int o2cb_cluster_connect(struct ocfs2_cluster_connection *conn)
{
	int rc = 0;
	u32 dlm_key;
	struct dlm_ctxt *dlm;
	struct o2dlm_private *priv;
	struct dlm_protocol_version fs_version;

	BUG_ON(conn == NULL);
	BUG_ON(o2cb_stack.sp_proto == NULL);

	/* for now we only have one cluster/node, make sure we see it
	 * in the heartbeat universe */
	if (!o2hb_check_local_node_heartbeating()) {
		rc = -EINVAL;
		goto out;
	}

	priv = kzalloc(sizeof(struct o2dlm_private), GFP_KERNEL);
	if (!priv) {
		rc = -ENOMEM;
		goto out_free;
	}

	/* This just fills the structure in.  It is safe to pass conn. */
	dlm_setup_eviction_cb(&priv->op_eviction_cb, o2dlm_eviction_cb,
			      conn);

	conn->cc_private = priv;

	/* used by the dlm code to make message headers unique, each
	 * node in this domain must agree on this. */
	dlm_key = crc32_le(0, conn->cc_name, conn->cc_namelen);
	fs_version.pv_major = conn->cc_version.pv_major;
	fs_version.pv_minor = conn->cc_version.pv_minor;

	dlm = dlm_register_domain(conn->cc_name, dlm_key, &fs_version);
	if (IS_ERR(dlm)) {
		rc = PTR_ERR(dlm);
		mlog_errno(rc);
		goto out_free;
	}

	conn->cc_version.pv_major = fs_version.pv_major;
	conn->cc_version.pv_minor = fs_version.pv_minor;
	conn->cc_lockspace = dlm;

	dlm_register_eviction_cb(dlm, &priv->op_eviction_cb);

out_free:
	if (rc && conn->cc_private)
		kfree(conn->cc_private);

out:
	return rc;
}

static int o2cb_cluster_disconnect(struct ocfs2_cluster_connection *conn)
{
	struct dlm_ctxt *dlm = conn->cc_lockspace;
	struct o2dlm_private *priv = conn->cc_private;

	dlm_unregister_eviction_cb(&priv->op_eviction_cb);
	conn->cc_private = NULL;
	kfree(priv);

	dlm_unregister_domain(dlm);
	conn->cc_lockspace = NULL;

	return 0;
}

static int o2cb_cluster_this_node(unsigned int *node)
{
	int node_num;

	node_num = o2nm_this_node();
	if (node_num == O2NM_INVALID_NODE_NUM)
		return -ENOENT;

	if (node_num >= O2NM_MAX_NODES)
		return -EOVERFLOW;

	*node = node_num;
	return 0;
}

static struct ocfs2_stack_operations o2cb_stack_ops = {
	.connect	= o2cb_cluster_connect,
	.disconnect	= o2cb_cluster_disconnect,
	.this_node	= o2cb_cluster_this_node,
	.dlm_lock	= o2cb_dlm_lock,
	.dlm_unlock	= o2cb_dlm_unlock,
	.lock_status	= o2cb_dlm_lock_status,
	.lvb_valid	= o2cb_dlm_lvb_valid,
	.lock_lvb	= o2cb_dlm_lvb,
	.dump_lksb	= o2cb_dump_lksb,
};

static struct ocfs2_stack_plugin o2cb_stack = {
	.sp_name	= "o2cb",
	.sp_ops		= &o2cb_stack_ops,
	.sp_owner	= THIS_MODULE,
};

static int __init o2cb_stack_init(void)
{
	return ocfs2_stack_glue_register(&o2cb_stack);
}

static void __exit o2cb_stack_exit(void)
{
	ocfs2_stack_glue_unregister(&o2cb_stack);
}

MODULE_AUTHOR("Oracle");
MODULE_DESCRIPTION("ocfs2 driver for the classic o2cb stack");
MODULE_LICENSE("GPL");
module_init(o2cb_stack_init);
module_exit(o2cb_stack_exit);
