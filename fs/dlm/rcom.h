/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __RCOM_DOT_H__
#define __RCOM_DOT_H__

int dlm_rcom_status(struct dlm_ls *ls, int nodeid);
int dlm_rcom_names(struct dlm_ls *ls, int nodeid, char *last_name,int last_len);
int dlm_send_rcom_lookup(struct dlm_rsb *r, int dir_nodeid);
int dlm_send_rcom_lock(struct dlm_rsb *r, struct dlm_lkb *lkb);
void dlm_receive_rcom(struct dlm_ls *ls, struct dlm_rcom *rc, int nodeid);
int dlm_send_ls_not_ready(int nodeid, struct dlm_rcom *rc_in);

#endif

