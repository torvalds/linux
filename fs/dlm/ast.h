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

#define DLM_ENQUEUE_CALLBACK_NEED_SCHED	1
#define DLM_ENQUEUE_CALLBACK_SUCCESS	0
#define DLM_ENQUEUE_CALLBACK_FAILURE	-1
int dlm_enqueue_lkb_callback(struct dlm_lkb *lkb, uint32_t flags, int mode,
			     int status, uint32_t sbflags);
#define DLM_DEQUEUE_CALLBACK_EMPTY	2
#define DLM_DEQUEUE_CALLBACK_LAST	1
#define DLM_DEQUEUE_CALLBACK_SUCCESS	0
int dlm_dequeue_lkb_callback(struct dlm_lkb *lkb, struct dlm_callback **cb);
void dlm_add_cb(struct dlm_lkb *lkb, uint32_t flags, int mode, int status,
                uint32_t sbflags);
void dlm_callback_set_last_ptr(struct dlm_callback **from,
			       struct dlm_callback *to);

void dlm_release_callback(struct kref *ref);
void dlm_purge_lkb_callbacks(struct dlm_lkb *lkb);
void dlm_callback_work(struct work_struct *work);
int dlm_callback_start(struct dlm_ls *ls);
void dlm_callback_stop(struct dlm_ls *ls);
void dlm_callback_suspend(struct dlm_ls *ls);
void dlm_callback_resume(struct dlm_ls *ls);

#endif


