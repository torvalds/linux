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

#include <linux/types.h>
#include <linux/list.h>

#include "dlm/dlmapi.h"

#include "stackglue.h"

static struct ocfs2_locking_protocol *lproto;

enum dlm_status ocfs2_dlm_lock(struct dlm_ctxt *dlm,
		   int mode,
		   struct dlm_lockstatus *lksb,
		   u32 flags,
		   void *name,
		   unsigned int namelen,
		   void *astarg)
{
	BUG_ON(lproto == NULL);
	return dlmlock(dlm, mode, lksb, flags, name, namelen,
		       lproto->lp_lock_ast, astarg,
		       lproto->lp_blocking_ast);
}

enum dlm_status ocfs2_dlm_unlock(struct dlm_ctxt *dlm,
		     struct dlm_lockstatus *lksb,
		     u32 flags,
		     void *astarg)
{
	BUG_ON(lproto == NULL);

	return dlmunlock(dlm, lksb, flags, lproto->lp_unlock_ast, astarg);
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
