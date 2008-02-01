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
#include <linux/kmod.h>

/* Needed for AOP_TRUNCATED_PAGE in mlog_errno() */
#include <linux/fs.h>

#include "cluster/masklog.h"

#include "stackglue.h"

struct ocfs2_locking_protocol *stack_glue_lproto;


int ocfs2_dlm_lock(struct ocfs2_cluster_connection *conn,
		   int mode,
		   union ocfs2_dlm_lksb *lksb,
		   u32 flags,
		   void *name,
		   unsigned int namelen,
		   void *astarg)
{
	BUG_ON(stack_glue_lproto == NULL);

	return o2cb_stack_ops.dlm_lock(conn, mode, lksb, flags,
				       name, namelen, astarg);
}

int ocfs2_dlm_unlock(struct ocfs2_cluster_connection *conn,
		     union ocfs2_dlm_lksb *lksb,
		     u32 flags,
		     void *astarg)
{
	BUG_ON(stack_glue_lproto == NULL);

	return o2cb_stack_ops.dlm_unlock(conn, lksb, flags, astarg);
}

int ocfs2_dlm_lock_status(union ocfs2_dlm_lksb *lksb)
{
	return o2cb_stack_ops.lock_status(lksb);
}

/*
 * Why don't we cast to ocfs2_meta_lvb?  The "clean" answer is that we
 * don't cast at the glue level.  The real answer is that the header
 * ordering is nigh impossible.
 */
void *ocfs2_dlm_lvb(union ocfs2_dlm_lksb *lksb)
{
	return o2cb_stack_ops.lock_lvb(lksb);
}

void ocfs2_dlm_dump_lksb(union ocfs2_dlm_lksb *lksb)
{
	o2cb_stack_ops.dump_lksb(lksb);
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

	BUG_ON(group == NULL);
	BUG_ON(conn == NULL);
	BUG_ON(recovery_handler == NULL);

	if (grouplen > GROUP_NAME_MAX) {
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
	new_conn->cc_version = stack_glue_lproto->lp_max_version;

	rc = o2cb_stack_ops.connect(new_conn);
	if (rc) {
		mlog_errno(rc);
		goto out_free;
	}

	*conn = new_conn;

out_free:
	if (rc)
		kfree(new_conn);

out:
	return rc;
}

int ocfs2_cluster_disconnect(struct ocfs2_cluster_connection *conn)
{
	int ret;

	BUG_ON(conn == NULL);

	ret = o2cb_stack_ops.disconnect(conn);

	/* XXX Should we free it anyway? */
	if (!ret)
		kfree(conn);

	return ret;
}

void ocfs2_cluster_hangup(const char *group, int grouplen)
{
	BUG_ON(group == NULL);
	BUG_ON(group[grouplen] != '\0');

	o2cb_stack_ops.hangup(group, grouplen);
}

int ocfs2_cluster_this_node(unsigned int *node)
{
	return o2cb_stack_ops.this_node(node);
}

void ocfs2_stack_glue_set_locking_protocol(struct ocfs2_locking_protocol *proto)
{
	BUG_ON(proto != NULL);

	stack_glue_lproto = proto;
}

