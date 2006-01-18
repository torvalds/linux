/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __LOCK_DOT_H__
#define __LOCK_DOT_H__

void dlm_print_rsb(struct dlm_rsb *r);
int dlm_receive_message(struct dlm_header *hd, int nodeid, int recovery);
int dlm_modes_compat(int mode1, int mode2);
int dlm_find_rsb(struct dlm_ls *ls, char *name, int namelen,
	unsigned int flags, struct dlm_rsb **r_ret);
void dlm_put_rsb(struct dlm_rsb *r);
void dlm_hold_rsb(struct dlm_rsb *r);
int dlm_put_lkb(struct dlm_lkb *lkb);
void dlm_scan_rsbs(struct dlm_ls *ls);

int dlm_purge_locks(struct dlm_ls *ls);
void dlm_purge_mstcpy_locks(struct dlm_rsb *r);
int dlm_grant_after_purge(struct dlm_ls *ls);
int dlm_recover_waiters_post(struct dlm_ls *ls);
void dlm_recover_waiters_pre(struct dlm_ls *ls);
int dlm_recover_master_copy(struct dlm_ls *ls, struct dlm_rcom *rc);
int dlm_recover_process_copy(struct dlm_ls *ls, struct dlm_rcom *rc);

static inline int is_master(struct dlm_rsb *r)
{
	return !r->res_nodeid;
}

static inline void lock_rsb(struct dlm_rsb *r)
{
	down(&r->res_sem);
}

static inline void unlock_rsb(struct dlm_rsb *r)
{
	up(&r->res_sem);
}

#endif

