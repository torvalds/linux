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

#include "dlm/dlmapi.h"

struct ocfs2_locking_protocol {
	void (*lp_lock_ast)(void *astarg);
	void (*lp_blocking_ast)(void *astarg, int level);
	void (*lp_unlock_ast)(void *astarg, int error);
};

int ocfs2_dlm_lock(struct dlm_ctxt *dlm,
		   int mode,
		   struct dlm_lockstatus *lksb,
		   u32 flags,
		   void *name,
		   unsigned int namelen,
		   void *astarg);
int ocfs2_dlm_unlock(struct dlm_ctxt *dlm,
		     struct dlm_lockstatus *lksb,
		     u32 flags,
		     void *astarg);

void o2cb_get_stack(struct ocfs2_locking_protocol *proto);
void o2cb_put_stack(void);

#endif  /* STACKGLUE_H */
