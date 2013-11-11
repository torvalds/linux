/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __RECOVER_DOT_H__
#define __RECOVER_DOT_H__

int dlm_wait_function(struct dlm_ls *ls, int (*testfn) (struct dlm_ls *ls));
uint32_t dlm_recover_status(struct dlm_ls *ls);
void dlm_set_recover_status(struct dlm_ls *ls, uint32_t status);
int dlm_recover_members_wait(struct dlm_ls *ls);
int dlm_recover_directory_wait(struct dlm_ls *ls);
int dlm_recover_locks_wait(struct dlm_ls *ls);
int dlm_recover_done_wait(struct dlm_ls *ls);
int dlm_recover_masters(struct dlm_ls *ls);
int dlm_recover_master_reply(struct dlm_ls *ls, struct dlm_rcom *rc);
int dlm_recover_locks(struct dlm_ls *ls);
void dlm_recovered_lock(struct dlm_rsb *r);
int dlm_create_root_list(struct dlm_ls *ls);
void dlm_release_root_list(struct dlm_ls *ls);
void dlm_clear_toss(struct dlm_ls *ls);
void dlm_recover_rsbs(struct dlm_ls *ls);

#endif				/* __RECOVER_DOT_H__ */

