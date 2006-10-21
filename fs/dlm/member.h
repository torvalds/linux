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

#ifndef __MEMBER_DOT_H__
#define __MEMBER_DOT_H__

int dlm_ls_stop(struct dlm_ls *ls);
int dlm_ls_start(struct dlm_ls *ls);
void dlm_clear_members(struct dlm_ls *ls);
void dlm_clear_members_gone(struct dlm_ls *ls);
int dlm_recover_members(struct dlm_ls *ls, struct dlm_recover *rv,int *neg_out);
int dlm_is_removed(struct dlm_ls *ls, int nodeid);

#endif                          /* __MEMBER_DOT_H__ */

