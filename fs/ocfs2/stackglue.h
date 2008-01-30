/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * stackglue.h
 *
 * Glue to the underlying cluster stack.
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


#ifndef STACKGLUE_H
#define STACKGLUE_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/dlmconstants.h>

/*
 * dlmconstants.h does not have a LOCAL flag.  We hope to remove it
 * some day, but right now we need it.  Let's fake it.  This value is larger
 * than any flag in dlmconstants.h.
 */
#define DLM_LKF_LOCAL		0x00100000

/*
 * This shadows DLM_LOCKSPACE_LEN in fs/dlm/dlm_internal.h.  That probably
 * wants to be in a public header.
 */
#define GROUP_NAME_MAX		64


#include "dlm/dlmapi.h"

struct ocfs2_protocol_version {
	u8 pv_major;
	u8 pv_minor;
};

struct ocfs2_locking_protocol {
	struct ocfs2_protocol_version lp_max_version;
	void (*lp_lock_ast)(void *astarg);
	void (*lp_blocking_ast)(void *astarg, int level);
	void (*lp_unlock_ast)(void *astarg, int error);
};

union ocfs2_dlm_lksb {
	struct dlm_lockstatus lksb_o2dlm;
};

struct ocfs2_cluster_connection {
	char cc_name[GROUP_NAME_MAX];
	int cc_namelen;
	struct ocfs2_protocol_version cc_version;
	void (*cc_recovery_handler)(int node_num, void *recovery_data);
	void *cc_recovery_data;
	void *cc_lockspace;
	void *cc_private;
};

int ocfs2_cluster_connect(const char *group,
			  int grouplen,
			  void (*recovery_handler)(int node_num,
						   void *recovery_data),
			  void *recovery_data,
			  struct ocfs2_cluster_connection **conn);
int ocfs2_cluster_disconnect(struct ocfs2_cluster_connection *conn);
void ocfs2_cluster_hangup(const char *group, int grouplen);
int ocfs2_cluster_this_node(unsigned int *node);

int ocfs2_dlm_lock(struct ocfs2_cluster_connection *conn,
		   int mode,
		   union ocfs2_dlm_lksb *lksb,
		   u32 flags,
		   void *name,
		   unsigned int namelen,
		   void *astarg);
int ocfs2_dlm_unlock(struct ocfs2_cluster_connection *conn,
		     union ocfs2_dlm_lksb *lksb,
		     u32 flags,
		     void *astarg);

int ocfs2_dlm_lock_status(union ocfs2_dlm_lksb *lksb);
void *ocfs2_dlm_lvb(union ocfs2_dlm_lksb *lksb);

void o2cb_get_stack(struct ocfs2_locking_protocol *proto);
void o2cb_put_stack(void);

#endif  /* STACKGLUE_H */
