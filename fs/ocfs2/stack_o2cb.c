// SPDX-License-Identifier: GPL-2.0-only
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * stack_o2cb.c
 *
 * Code which interfaces ocfs2 with the o2cb stack.
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/module.h>

/* Needed for AOP_TRUNCATED_PAGE in mlog_erryes() */
#include <linux/fs.h>

#include "cluster/masklog.h"
#include "cluster/yesdemanager.h"
#include "cluster/heartbeat.h"
#include "cluster/tcp.h"

#include "stackglue.h"

struct o2dlm_private {
	struct dlm_eviction_cb op_eviction_cb;
};

static struct ocfs2_stack_plugin o2cb_stack;

/* These should be identical */
#if (DLM_LOCK_IV != LKM_IVMODE)
# error Lock modes do yest match
#endif
#if (DLM_LOCK_NL != LKM_NLMODE)
# error Lock modes do yest match
#endif
#if (DLM_LOCK_CR != LKM_CRMODE)
# error Lock modes do yest match
#endif
#if (DLM_LOCK_CW != LKM_CWMODE)
# error Lock modes do yest match
#endif
#if (DLM_LOCK_PR != LKM_PRMODE)
# error Lock modes do yest match
#endif
#if (DLM_LOCK_PW != LKM_PWMODE)
# error Lock modes do yest match
#endif
#if (DLM_LOCK_EX != LKM_EXMODE)
# error Lock modes do yest match
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
 * Map an o2dlm status to standard erryes values.
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

static int dlm_status_to_erryes(enum dlm_status status)
{
	BUG_ON(status < 0 || status >= ARRAY_SIZE(status_map));

	return status_map[status];
}

static void o2dlm_lock_ast_wrapper(void *astarg)
{
	struct ocfs2_dlm_lksb *lksb = astarg;

	lksb->lksb_conn->cc_proto->lp_lock_ast(lksb);
}

static void o2dlm_blocking_ast_wrapper(void *astarg, int level)
{
	struct ocfs2_dlm_lksb *lksb = astarg;

	lksb->lksb_conn->cc_proto->lp_blocking_ast(lksb, level);
}

static void o2dlm_unlock_ast_wrapper(void *astarg, enum dlm_status status)
{
	struct ocfs2_dlm_lksb *lksb = astarg;
	int error = dlm_status_to_erryes(status);

	/*
	 * In o2dlm, you can get both the lock_ast() for the lock being
	 * granted and the unlock_ast() for the CANCEL failing.  A
	 * successful cancel sends DLM_NORMAL here.  If the
	 * lock grant happened before the cancel arrived, you get
	 * DLM_CANCELGRANT.
	 *
	 * There's yes need for the double-ast.  If we see DLM_CANCELGRANT,
	 * we just igyesre it.  We expect the lock_ast() to handle the
	 * granted lock.
	 */
	if (status == DLM_CANCELGRANT)
		return;

	lksb->lksb_conn->cc_proto->lp_unlock_ast(lksb, error);
}

static int o2cb_dlm_lock(struct ocfs2_cluster_connection *conn,
			 int mode,
			 struct ocfs2_dlm_lksb *lksb,
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
	ret = dlm_status_to_erryes(status);
	return ret;
}

static int o2cb_dlm_unlock(struct ocfs2_cluster_connection *conn,
			   struct ocfs2_dlm_lksb *lksb,
			   u32 flags)
{
	enum dlm_status status;
	int o2dlm_flags = flags_to_o2dlm(flags);
	int ret;

	status = dlmunlock(conn->cc_lockspace, &lksb->lksb_o2dlm,
			   o2dlm_flags, o2dlm_unlock_ast_wrapper, lksb);
	ret = dlm_status_to_erryes(status);
	return ret;
}

static int o2cb_dlm_lock_status(struct ocfs2_dlm_lksb *lksb)
{
	return dlm_status_to_erryes(lksb->lksb_o2dlm.status);
}

/*
 * o2dlm aways has a "valid" LVB. If the dlm loses track of the LVB
 * contents, it will zero out the LVB.  Thus the caller can always trust
 * the contents.
 */
static int o2cb_dlm_lvb_valid(struct ocfs2_dlm_lksb *lksb)
{
	return 1;
}

static void *o2cb_dlm_lvb(struct ocfs2_dlm_lksb *lksb)
{
	return (void *)(lksb->lksb_o2dlm.lvb);
}

static void o2cb_dump_lksb(struct ocfs2_dlm_lksb *lksb)
{
	dlm_print_one_lock(lksb->lksb_o2dlm.lockid);
}

/*
 * Check if this yesde is heartbeating and is connected to all other
 * heartbeating yesdes.
 */
static int o2cb_cluster_check(void)
{
	u8 yesde_num;
	int i;
	unsigned long hbmap[BITS_TO_LONGS(O2NM_MAX_NODES)];
	unsigned long netmap[BITS_TO_LONGS(O2NM_MAX_NODES)];

	yesde_num = o2nm_this_yesde();
	if (yesde_num == O2NM_MAX_NODES) {
		printk(KERN_ERR "o2cb: This yesde has yest been configured.\n");
		return -EINVAL;
	}

	/*
	 * o2dlm expects o2net sockets to be created. If yest, then
	 * dlm_join_domain() fails with a stack of errors which are both cryptic
	 * and incomplete. The idea here is to detect upfront whether we have
	 * managed to connect to all yesdes or yest. If yest, then list the yesdes
	 * to allow the user to check the configuration (incorrect IP, firewall,
	 * etc.) Yes, this is racy. But its yest the end of the world.
	 */
#define	O2CB_MAP_STABILIZE_COUNT	60
	for (i = 0; i < O2CB_MAP_STABILIZE_COUNT; ++i) {
		o2hb_fill_yesde_map(hbmap, sizeof(hbmap));
		if (!test_bit(yesde_num, hbmap)) {
			printk(KERN_ERR "o2cb: %s heartbeat has yest been "
			       "started.\n", (o2hb_global_heartbeat_active() ?
					      "Global" : "Local"));
			return -EINVAL;
		}
		o2net_fill_yesde_map(netmap, sizeof(netmap));
		/* Force set the current yesde to allow easy compare */
		set_bit(yesde_num, netmap);
		if (!memcmp(hbmap, netmap, sizeof(hbmap)))
			return 0;
		if (i < O2CB_MAP_STABILIZE_COUNT - 1)
			msleep(1000);
	}

	printk(KERN_ERR "o2cb: This yesde could yest connect to yesdes:");
	i = -1;
	while ((i = find_next_bit(hbmap, O2NM_MAX_NODES,
				  i + 1)) < O2NM_MAX_NODES) {
		if (!test_bit(i, netmap))
			printk(" %u", i);
	}
	printk(".\n");

	return -ENOTCONN;
}

/*
 * Called from the dlm when it's about to evict a yesde. This is how the
 * classic stack signals yesde death.
 */
static void o2dlm_eviction_cb(int yesde_num, void *data)
{
	struct ocfs2_cluster_connection *conn = data;

	printk(KERN_NOTICE "o2cb: o2dlm has evicted yesde %d from domain %.*s\n",
	       yesde_num, conn->cc_namelen, conn->cc_name);

	conn->cc_recovery_handler(yesde_num, conn->cc_recovery_data);
}

static int o2cb_cluster_connect(struct ocfs2_cluster_connection *conn)
{
	int rc = 0;
	u32 dlm_key;
	struct dlm_ctxt *dlm;
	struct o2dlm_private *priv;
	struct dlm_protocol_version fs_version;

	BUG_ON(conn == NULL);
	BUG_ON(conn->cc_proto == NULL);

	/* Ensure cluster stack is up and all yesdes are connected */
	rc = o2cb_cluster_check();
	if (rc) {
		printk(KERN_ERR "o2cb: Cluster check failed. Fix errors "
		       "before retrying.\n");
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
	 * yesde in this domain must agree on this. */
	dlm_key = crc32_le(0, conn->cc_name, conn->cc_namelen);
	fs_version.pv_major = conn->cc_version.pv_major;
	fs_version.pv_miyesr = conn->cc_version.pv_miyesr;

	dlm = dlm_register_domain(conn->cc_name, dlm_key, &fs_version);
	if (IS_ERR(dlm)) {
		rc = PTR_ERR(dlm);
		mlog_erryes(rc);
		goto out_free;
	}

	conn->cc_version.pv_major = fs_version.pv_major;
	conn->cc_version.pv_miyesr = fs_version.pv_miyesr;
	conn->cc_lockspace = dlm;

	dlm_register_eviction_cb(dlm, &priv->op_eviction_cb);

out_free:
	if (rc)
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

static int o2cb_cluster_this_yesde(struct ocfs2_cluster_connection *conn,
				  unsigned int *yesde)
{
	int yesde_num;

	yesde_num = o2nm_this_yesde();
	if (yesde_num == O2NM_INVALID_NODE_NUM)
		return -ENOENT;

	if (yesde_num >= O2NM_MAX_NODES)
		return -EOVERFLOW;

	*yesde = yesde_num;
	return 0;
}

static struct ocfs2_stack_operations o2cb_stack_ops = {
	.connect	= o2cb_cluster_connect,
	.disconnect	= o2cb_cluster_disconnect,
	.this_yesde	= o2cb_cluster_this_yesde,
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
