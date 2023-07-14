/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#ifndef __MEMORY_DOT_H__
#define __MEMORY_DOT_H__

int dlm_memory_init(void);
void dlm_memory_exit(void);
struct dlm_rsb *dlm_allocate_rsb(struct dlm_ls *ls);
void dlm_free_rsb(struct dlm_rsb *r);
struct dlm_lkb *dlm_allocate_lkb(struct dlm_ls *ls);
void dlm_free_lkb(struct dlm_lkb *l);
char *dlm_allocate_lvb(struct dlm_ls *ls);
void dlm_free_lvb(char *l);
struct dlm_mhandle *dlm_allocate_mhandle(gfp_t allocation);
void dlm_free_mhandle(struct dlm_mhandle *mhandle);
struct writequeue_entry *dlm_allocate_writequeue(void);
void dlm_free_writequeue(struct writequeue_entry *writequeue);
struct dlm_msg *dlm_allocate_msg(gfp_t allocation);
void dlm_free_msg(struct dlm_msg *msg);
struct dlm_callback *dlm_allocate_cb(void);
void dlm_free_cb(struct dlm_callback *cb);

#endif		/* __MEMORY_DOT_H__ */

