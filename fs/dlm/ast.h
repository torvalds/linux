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

void dlm_del_ast(struct dlm_lkb *lkb);
int dlm_add_lkb_callback(struct dlm_lkb *lkb, uint32_t flags, int mode,
                         int status, uint32_t sbflags, uint64_t seq);
int dlm_rem_lkb_callback(struct dlm_ls *ls, struct dlm_lkb *lkb,
                         struct dlm_callback *cb, int *resid);
void dlm_add_cb(struct dlm_lkb *lkb, uint32_t flags, int mode, int status,
                uint32_t sbflags);

void dlm_callback_work(struct work_struct *work);
int dlm_callback_start(struct dlm_ls *ls);
void dlm_callback_stop(struct dlm_ls *ls);
void dlm_callback_suspend(struct dlm_ls *ls);
void dlm_callback_resume(struct dlm_ls *ls);

#endif


