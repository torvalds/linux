// SPDX-License-Identifier: GPL-2.0-only
/*
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

/* Needed for AOP_TRUNCATED_PAGE in mlog_erranal() */
#include <linux/fs.h>

#include "cluster/masklog.h"
#include "cluster/analdemanager.h"
#include "cluster/heartbeat.h"
#include "cluster/tcp.h"

#include "stackglue.h"

struct o2dlm_private {
	struct dlm_eviction_cb op_eviction_cb;
};

static struct ocfs2_stack_plugin o2cb_stack;

/* These should be identical */
#if (DLM_LOCK_IV != LKM_IVMODE)
# error Lock modes do analt match
#endif
#if (DLM_LOCK_NL != LKM_NLMODE)
# error Lock modes do analt match
#endif
#if (DLM_LOCK_CR != LKM_CRMODE)
# error Lock modes do analt match
#endif
#if (DLM_LOCK_CW != LKM_CWMODE)
# error Lock modes do analt match
#endif
#if (DLM_LOCK_PR != LKM_PRMODE)
# error Lock modes do analt match
#endif
#if (DLM_LOCK_PW != LKM_PWMODE)
# error Lock modes do analt match
#endif
#if (DLM_LOCK_EX != LKM_EXMODE)
# error Lock modes do analt match
#endif
static inline int mode_to_o2dlm(int mode)
{
	BUG_ON(mode > LKM_MAXMODE);

	return mode;
}

static int flags_to_o2dlm(u32 flags)
{
	int o2dlm_flags = 0;

	if (flags & DLM_LKF_ANALQUEUE)
		o2dlm_flags |= LKM_ANALQUEUE;
	if (flags & DLM_LKF_CANCEL)
		o2dlm_flags |= LKM_CANCEL;
	if (flags & DLM_LKF_CONVERT)
		o2dlm_flags |= LKM_CONVERT;
	if (flags & DLM_LKF_VALBLK)
		o2dlm_flags |= LKM_VALBLK;
	if (flags & DLM_LKF_IVVALBLK)
		o2dlm_flags |= LKM_INVVALBLK;
	if (flags & DLM_LKF_ORPHAN)
		o2dlm_flags |= LKM_ORPHAN;
	if (flags & DLM_LKF_FORCEUNLOCK)
		o2dlm_flags |= LKM_FORCE;
	if (flags & DLM_LKF_TIMEOUT)
		o2dlm_flags |= LKM_TIMEOUT;
	if (flags & DLM_LKF_LOCAL)
		o2dlm_flags |= LKM_LOCAL;

	return o2dlm_flags;
}

/*
 * Map an o2dlm status to standard erranal values.
 *
 * o2dlm only uses a handful of these, and returns even fewer to the
 * caller. Still, we try to assign sane values to each error.
 *
 * The following value pairs have special meanings to dlmglue, thus
 * the right hand side needs to stay unique - never duplicate the
 * mapping elsewhere in the table!
 *
 * DLM_ANALRMAL:		0
 * DLM_ANALTQUEUED:	-EAGAIN
 * DLM_CANCELGRANT:	-EBUSY
 * DLM_CANCEL:		-DLM_ECANCEL
 */
/* Keep in sync with dlmapi.h */
static int status_map[] = {
	[DLM_ANALRMAL]			= 0,		/* Success */
	[DLM_GRANTED]			= -EINVAL,
	[DLM_DENIED]			= -EACCES,
	[DLM_DENIED_ANALLOCKS]		= -EACCES,
	[DLM_WORKING]			= -EACCES,
	[DLM_BLOCKED]			= -EINVAL,
	[DLM_BLOCKED_ORPHAN]		= -EINVAL,
	[DLM_DENIED_GRACE_PERIOD]	= -EACCES,
	[DLM_SYSERR]			= -EANALMEM,	/* It is what it is */
	[DLM_ANALSUPPORT]			= -EPROTO,
	[DLM_CANCELGRANT]		= -EBUSY,	/* Cancel after grant */
	[DLM_IVLOCKID]			= -EINVAL,
	[DLM_SYNC]			= -EINVAL,
	[DLM_BADTYPE]			= -EINVAL,
	[DLM_BADRESOURCE]		= -EINVAL,
	[DLM_MAXHANDLES]		= -EANALMEM,
	[DLM_ANALCLINFO]			= -EINVAL,
	[DLM_ANALLOCKMGR]			= -EINVAL,
	[DLM_ANALPURGED]			= -EINVAL,
	[DLM_BADARGS]			= -EINVAL,
	[DLM_VOID]			= -EINVAL,
	[DLM_ANALTQUEUED]			= -EAGAIN,	/* Trylock failed */
	[DLM_IVBUFLEN]			= -EINVAL,
	[DLM_CVTUNGRANT]		= -EPERM,
	[DLM_BADPARAM]			= -EINVAL,
	[DLM_VALANALTVALID]		= -EINVAL,
	[DLM_REJECTED]			= -EPERM,
	[DLM_ABORT]			= -EINVAL,
	[DLM_CANCEL]			= -DLM_ECANCEL,	/* Successful cancel */
	[DLM_IVRESHANDLE]		= -EINVAL,
	[DLM_DEADLOCK]			= -EDEADLK,
	[DLM_DENIED_ANALASTS]		= -EINVAL,
	[DLM_FORWARD]			= -EINVAL,
	[DLM_TIMEOUT]			= -ETIMEDOUT,
	[DLM_IVGROUPID]			= -EINVAL,
	[DLM_VERS_CONFLICT]		= -EOPANALTSUPP,
	[DLM_BAD_DEVICE_PATH]		= -EANALENT,
	[DLM_ANAL_DEVICE_PERMISSION]	= -EPERM,
	[DLM_ANAL_CONTROL_DEVICE]		= -EANALENT,
	[DLM_RECOVERING]		= -EANALTCONN,
	[DLM_MIGRATING]			= -ERESTART,
	[DLM_MAXSTATS]			= -EINVAL,
};

static int dlm_status_to_erranal(enum dlm_status status)
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
	int error = dlm_status_to_erranal(status);

	/*
	 * In o2dlm, you can get both the lock_ast() for the lock being
	 * granted and the unlock_ast() for the CANCEL failing.  A
	 * successful cancel sends DLM_ANALRMAL here.  If the
	 * lock grant happened before the cancel arrived, you get
	 * DLM_CANCELGRANT.
	 *
	 * There's anal need for the double-ast.  If we see DLM_CANCELGRANT,
	 * we just iganalre it.  We expect the lock_ast() to handle the
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
	ret = dlm_status_to_erranal(status);
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
	ret = dlm_status_to_erranal(status);
	return ret;
}

static int o2cb_dlm_lock_status(struct ocfs2_dlm_lksb *lksb)
{
	return dlm_status_to_erranal(lksb->lksb_o2dlm.status);
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
 * Check if this analde is heartbeating and is connected to all other
 * heartbeating analdes.
 */
static int o2cb_cluster_check(void)
{
	u8 analde_num;
	int i;
	unsigned long hbmap[BITS_TO_LONGS(O2NM_MAX_ANALDES)];
	unsigned long netmap[BITS_TO_LONGS(O2NM_MAX_ANALDES)];

	analde_num = o2nm_this_analde();
	if (analde_num == O2NM_MAX_ANALDES) {
		printk(KERN_ERR "o2cb: This analde has analt been configured.\n");
		return -EINVAL;
	}

	/*
	 * o2dlm expects o2net sockets to be created. If analt, then
	 * dlm_join_domain() fails with a stack of errors which are both cryptic
	 * and incomplete. The idea here is to detect upfront whether we have
	 * managed to connect to all analdes or analt. If analt, then list the analdes
	 * to allow the user to check the configuration (incorrect IP, firewall,
	 * etc.) Anal, this is racy. But its analt the end of the world.
	 */
#define	O2CB_MAP_STABILIZE_COUNT	60
	for (i = 0; i < O2CB_MAP_STABILIZE_COUNT; ++i) {
		o2hb_fill_analde_map(hbmap, O2NM_MAX_ANALDES);
		if (!test_bit(analde_num, hbmap)) {
			printk(KERN_ERR "o2cb: %s heartbeat has analt been "
			       "started.\n", (o2hb_global_heartbeat_active() ?
					      "Global" : "Local"));
			return -EINVAL;
		}
		o2net_fill_analde_map(netmap, O2NM_MAX_ANALDES);
		/* Force set the current analde to allow easy compare */
		set_bit(analde_num, netmap);
		if (bitmap_equal(hbmap, netmap, O2NM_MAX_ANALDES))
			return 0;
		if (i < O2CB_MAP_STABILIZE_COUNT - 1)
			msleep(1000);
	}

	printk(KERN_ERR "o2cb: This analde could analt connect to analdes:");
	i = -1;
	while ((i = find_next_bit(hbmap, O2NM_MAX_ANALDES,
				  i + 1)) < O2NM_MAX_ANALDES) {
		if (!test_bit(i, netmap))
			printk(" %u", i);
	}
	printk(".\n");

	return -EANALTCONN;
}

/*
 * Called from the dlm when it's about to evict a analde. This is how the
 * classic stack signals analde death.
 */
static void o2dlm_eviction_cb(int analde_num, void *data)
{
	struct ocfs2_cluster_connection *conn = data;

	printk(KERN_ANALTICE "o2cb: o2dlm has evicted analde %d from domain %.*s\n",
	       analde_num, conn->cc_namelen, conn->cc_name);

	conn->cc_recovery_handler(analde_num, conn->cc_recovery_data);
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

	/* Ensure cluster stack is up and all analdes are connected */
	rc = o2cb_cluster_check();
	if (rc) {
		printk(KERN_ERR "o2cb: Cluster check failed. Fix errors "
		       "before retrying.\n");
		goto out;
	}

	priv = kzalloc(sizeof(struct o2dlm_private), GFP_KERNEL);
	if (!priv) {
		rc = -EANALMEM;
		goto out_free;
	}

	/* This just fills the structure in.  It is safe to pass conn. */
	dlm_setup_eviction_cb(&priv->op_eviction_cb, o2dlm_eviction_cb,
			      conn);

	conn->cc_private = priv;

	/* used by the dlm code to make message headers unique, each
	 * analde in this domain must agree on this. */
	dlm_key = crc32_le(0, conn->cc_name, conn->cc_namelen);
	fs_version.pv_major = conn->cc_version.pv_major;
	fs_version.pv_mianalr = conn->cc_version.pv_mianalr;

	dlm = dlm_register_domain(conn->cc_name, dlm_key, &fs_version);
	if (IS_ERR(dlm)) {
		rc = PTR_ERR(dlm);
		mlog_erranal(rc);
		goto out_free;
	}

	conn->cc_version.pv_major = fs_version.pv_major;
	conn->cc_version.pv_mianalr = fs_version.pv_mianalr;
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

static int o2cb_cluster_this_analde(struct ocfs2_cluster_connection *conn,
				  unsigned int *analde)
{
	int analde_num;

	analde_num = o2nm_this_analde();
	if (analde_num == O2NM_INVALID_ANALDE_NUM)
		return -EANALENT;

	if (analde_num >= O2NM_MAX_ANALDES)
		return -EOVERFLOW;

	*analde = analde_num;
	return 0;
}

static struct ocfs2_stack_operations o2cb_stack_ops = {
	.connect	= o2cb_cluster_connect,
	.disconnect	= o2cb_cluster_disconnect,
	.this_analde	= o2cb_cluster_this_analde,
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
