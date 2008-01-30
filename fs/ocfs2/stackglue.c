/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * stackglue.c
 *
 * Code which implements an OCFS2 specific interface to underlying
 * cluster stacks.
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

#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/kmod.h>

/* Needed for AOP_TRUNCATED_PAGE in mlog_errno() */
#include <linux/fs.h>

#include "cluster/masklog.h"
#include "cluster/nodemanager.h"
#include "cluster/heartbeat.h"

#include "stackglue.h"

static struct ocfs2_locking_protocol *lproto;

struct o2dlm_private {
	struct dlm_eviction_cb op_eviction_cb;
};

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
 * DLM_CANCELGRANT:	-DLM_ECANCEL
 * DLM_CANCEL:		-DLM_EUNLOCK
 */
/* Keep in sync with dlmapi.h */
static int status_map[] = {
	[DLM_NORMAL]			= 0,		/* Success */
	[DLM_GRANTED]			= -EINVAL,
	[DLM_DENIED]			= -EACCES,
	[DLM_DENIED_NOLOCKS]		= -EACCES,
	[DLM_WORKING]			= -EBUSY,
	[DLM_BLOCKED]			= -EINVAL,
	[DLM_BLOCKED_ORPHAN]		= -EINVAL,
	[DLM_DENIED_GRACE_PERIOD]	= -EACCES,
	[DLM_SYSERR]			= -ENOMEM,	/* It is what it is */
	[DLM_NOSUPPORT]			= -EPROTO,
	[DLM_CANCELGRANT]		= -DLM_ECANCEL, /* Cancel after grant */
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
	[DLM_CANCEL]			= -DLM_EUNLOCK,	/* Successful cancel */
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
	BUG_ON(status > (sizeof(status_map) / sizeof(status_map[0])));

	return status_map[status];
}

static void o2dlm_lock_ast_wrapper(void *astarg)
{
	BUG_ON(lproto == NULL);

	lproto->lp_lock_ast(astarg);
}

static void o2dlm_blocking_ast_wrapper(void *astarg, int level)
{
	BUG_ON(lproto == NULL);

	lproto->lp_blocking_ast(astarg, level);
}

static void o2dlm_unlock_ast_wrapper(void *astarg, enum dlm_status status)
{
	int error;

	BUG_ON(lproto == NULL);

	/*
	 * XXX: CANCEL values are sketchy.
	 *
	 * Currently we have preserved the o2dlm paradigm.  You can get
	 * unlock_ast() whether the cancel succeded or not.
	 *
	 * First, we're going to pass DLM_EUNLOCK just like fs/dlm does for
	 * successful unlocks.  That is a clean behavior.
	 *
	 * In o2dlm, you can get both the lock_ast() for the lock being
	 * granted and the unlock_ast() for the CANCEL failing.  A
	 * successful cancel sends DLM_NORMAL here.  If the
	 * lock grant happened before the cancel arrived, you get
	 * DLM_CANCELGRANT.  For now, we'll use DLM_ECANCEL to signify
	 * CANCELGRANT - the CANCEL was supposed to happen but didn't.  We
	 * can then use DLM_EUNLOCK to signify a successful CANCEL -
	 * effectively, the CANCEL caused the lock to roll back.
	 *
	 * In the future, we will likely move the o2dlm to send only one
	 * ast - either unlock_ast() for a successful CANCEL or lock_ast()
	 * when the grant succeeds.  At that point, we'll send DLM_ECANCEL
	 * for all cancel results (CANCELGRANT will no longer exist).
	 */
	error = dlm_status_to_errno(status);

	/* Successful unlock is DLM_EUNLOCK */
	if (!error)
		error = -DLM_EUNLOCK;

	lproto->lp_unlock_ast(astarg, error);
}

int ocfs2_dlm_lock(struct ocfs2_cluster_connection *conn,
		   int mode,
		   union ocfs2_dlm_lksb *lksb,
		   u32 flags,
		   void *name,
		   unsigned int namelen,
		   void *astarg)
{
	enum dlm_status status;
	int o2dlm_mode = mode_to_o2dlm(mode);
	int o2dlm_flags = flags_to_o2dlm(flags);
	int ret;

	BUG_ON(lproto == NULL);

	status = dlmlock(conn->cc_lockspace, o2dlm_mode, &lksb->lksb_o2dlm,
			 o2dlm_flags, name, namelen,
			 o2dlm_lock_ast_wrapper, astarg,
			 o2dlm_blocking_ast_wrapper);
	ret = dlm_status_to_errno(status);
	return ret;
}

int ocfs2_dlm_unlock(struct ocfs2_cluster_connection *conn,
		     union ocfs2_dlm_lksb *lksb,
		     u32 flags,
		     void *astarg)
{
	enum dlm_status status;
	int o2dlm_flags = flags_to_o2dlm(flags);
	int ret;

	BUG_ON(lproto == NULL);

	status = dlmunlock(conn->cc_lockspace, &lksb->lksb_o2dlm,
			   o2dlm_flags, o2dlm_unlock_ast_wrapper, astarg);
	ret = dlm_status_to_errno(status);
	return ret;
}

int ocfs2_dlm_lock_status(union ocfs2_dlm_lksb *lksb)
{
	return dlm_status_to_errno(lksb->lksb_o2dlm.status);
}

/*
 * Why don't we cast to ocfs2_meta_lvb?  The "clean" answer is that we
 * don't cast at the glue level.  The real answer is that the header
 * ordering is nigh impossible.
 */
void *ocfs2_dlm_lvb(union ocfs2_dlm_lksb *lksb)
{
	return (void *)(lksb->lksb_o2dlm.lvb);
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

int ocfs2_cluster_connect(const char *group,
			  int grouplen,
			  void (*recovery_handler)(int node_num,
						   void *recovery_data),
			  void *recovery_data,
			  struct ocfs2_cluster_connection **conn)
{
	int rc = 0;
	struct ocfs2_cluster_connection *new_conn;
	u32 dlm_key;
	struct dlm_ctxt *dlm;
	struct o2dlm_private *priv;
	struct dlm_protocol_version dlm_version;

	BUG_ON(group == NULL);
	BUG_ON(conn == NULL);
	BUG_ON(recovery_handler == NULL);

	if (grouplen > GROUP_NAME_MAX) {
		rc = -EINVAL;
		goto out;
	}

	/* for now we only have one cluster/node, make sure we see it
	 * in the heartbeat universe */
	if (!o2hb_check_local_node_heartbeating()) {
		rc = -EINVAL;
		goto out;
	}

	new_conn = kzalloc(sizeof(struct ocfs2_cluster_connection),
			   GFP_KERNEL);
	if (!new_conn) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(new_conn->cc_name, group, grouplen);
	new_conn->cc_namelen = grouplen;
	new_conn->cc_recovery_handler = recovery_handler;
	new_conn->cc_recovery_data = recovery_data;

	/* Start the new connection at our maximum compatibility level */
	new_conn->cc_version = lproto->lp_max_version;

	priv = kzalloc(sizeof(struct o2dlm_private), GFP_KERNEL);
	if (!priv) {
		rc = -ENOMEM;
		goto out_free;
	}

	/* This just fills the structure in.  It is safe to use new_conn. */
	dlm_setup_eviction_cb(&priv->op_eviction_cb, o2dlm_eviction_cb,
			      new_conn);

	new_conn->cc_private = priv;

	/* used by the dlm code to make message headers unique, each
	 * node in this domain must agree on this. */
	dlm_key = crc32_le(0, group, grouplen);
	dlm_version.pv_major = new_conn->cc_version.pv_major;
	dlm_version.pv_minor = new_conn->cc_version.pv_minor;

	dlm = dlm_register_domain(group, dlm_key, &dlm_version);
	if (IS_ERR(dlm)) {
		rc = PTR_ERR(dlm);
		mlog_errno(rc);
		goto out_free;
	}

	new_conn->cc_version.pv_major = dlm_version.pv_major;
	new_conn->cc_version.pv_minor = dlm_version.pv_minor;
	new_conn->cc_lockspace = dlm;

	dlm_register_eviction_cb(dlm, &priv->op_eviction_cb);

	*conn = new_conn;

out_free:
	if (rc) {
		kfree(new_conn->cc_private);
		kfree(new_conn);
	}

out:
	return rc;
}


int ocfs2_cluster_disconnect(struct ocfs2_cluster_connection *conn)
{
	struct dlm_ctxt *dlm = conn->cc_lockspace;
	struct o2dlm_private *priv = conn->cc_private;

	dlm_unregister_eviction_cb(&priv->op_eviction_cb);
	dlm_unregister_domain(dlm);

	kfree(priv);
	kfree(conn);

	return 0;
}

static void o2hb_stop(const char *group)
{
	int ret;
	char *argv[5], *envp[3];

	argv[0] = (char *)o2nm_get_hb_ctl_path();
	argv[1] = "-K";
	argv[2] = "-u";
	argv[3] = (char *)group;
	argv[4] = NULL;

	mlog(0, "Run: %s %s %s %s\n", argv[0], argv[1], argv[2], argv[3]);

	/* minimal command environment taken from cpu_run_sbin_hotplug */
	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[2] = NULL;

	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	if (ret < 0)
		mlog_errno(ret);
}

/*
 * Hangup is a hack for tools compatibility.  Older ocfs2-tools software
 * expects the filesystem to call "ocfs2_hb_ctl" during unmount.  This
 * happens regardless of whether the DLM got started, so we can't do it
 * in ocfs2_cluster_disconnect().  We bring the o2hb_stop() function into
 * the glue and provide a "hangup" API for super.c to call.
 *
 * Other stacks will eventually provide a NULL ->hangup() pointer.
 */
void ocfs2_cluster_hangup(const char *group, int grouplen)
{
	BUG_ON(group == NULL);
	BUG_ON(group[grouplen] != '\0');

	o2hb_stop(group);
}

int ocfs2_cluster_this_node(unsigned int *node)
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

void o2cb_get_stack(struct ocfs2_locking_protocol *proto)
{
	BUG_ON(proto == NULL);

	lproto = proto;
}

void o2cb_put_stack(void)
{
	lproto = NULL;
}
