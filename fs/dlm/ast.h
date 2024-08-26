/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2010 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#ifndef __ASTD_DOT_H__
#define __ASTD_DOT_H__

bool dlm_may_skip_callback(struct dlm_lkb *lkb, uint32_t flags, int mode,
			   int status, uint32_t sbflags, int *copy_lvb);
int dlm_get_cb(struct dlm_lkb *lkb, uint32_t flags, int mode,
	       int status, uint32_t sbflags,
	       struct dlm_callback **cb);
void dlm_add_cb(struct dlm_lkb *lkb, uint32_t flags, int mode, int status,
                uint32_t sbflags);

int dlm_callback_start(struct dlm_ls *ls);
void dlm_callback_stop(struct dlm_ls *ls);
void dlm_callback_suspend(struct dlm_ls *ls);
void dlm_callback_resume(struct dlm_ls *ls);

#endif


