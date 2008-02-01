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

#include "stackglue.h"

static struct ocfs2_locking_protocol *lproto;

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

enum dlm_status ocfs2_dlm_lock(struct dlm_ctxt *dlm,
		   int mode,
		   struct dlm_lockstatus *lksb,
		   u32 flags,
		   void *name,
		   unsigned int namelen,
		   void *astarg)
{
	int o2dlm_mode = mode_to_o2dlm(mode);
	int o2dlm_flags = flags_to_o2dlm(flags);

	BUG_ON(lproto == NULL);

	return dlmlock(dlm, o2dlm_mode, lksb, o2dlm_flags, name, namelen,
		       lproto->lp_lock_ast, astarg,
		       lproto->lp_blocking_ast);
}

enum dlm_status ocfs2_dlm_unlock(struct dlm_ctxt *dlm,
		     struct dlm_lockstatus *lksb,
		     u32 flags,
		     void *astarg)
{
	int o2dlm_flags = flags_to_o2dlm(flags);

	BUG_ON(lproto == NULL);

	return dlmunlock(dlm, lksb, o2dlm_flags,
			 lproto->lp_unlock_ast, astarg);
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
