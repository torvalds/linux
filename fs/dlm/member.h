/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2011 Red Hat, Inc.  All rights reserved.
**
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
int dlm_is_member(struct dlm_ls *ls, int nodeid);
int dlm_slots_version(const struct dlm_header *h);
void dlm_slot_save(struct dlm_ls *ls, struct dlm_rcom *rc,
		   struct dlm_member *memb);
void dlm_slots_copy_out(struct dlm_ls *ls, struct dlm_rcom *rc);
int dlm_slots_copy_in(struct dlm_ls *ls);
int dlm_slots_assign(struct dlm_ls *ls, int *num_slots, int *slots_size,
		     struct dlm_slot **slots_out, uint32_t *gen_out);
void dlm_lsop_recover_done(struct dlm_ls *ls);

#endif                          /* __MEMBER_DOT_H__ */

