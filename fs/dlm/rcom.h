/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#ifndef __RCOM_DOT_H__
#define __RCOM_DOT_H__

int dlm_rcom_status(struct dlm_ls *ls, int nodeid, uint32_t status_flags,
		    uint64_t seq);
int dlm_rcom_names(struct dlm_ls *ls, int nodeid, char *last_name,
		   int last_len, uint64_t seq);
int dlm_send_rcom_lookup(struct dlm_rsb *r, int dir_nodeid, uint64_t seq);
int dlm_send_rcom_lock(struct dlm_rsb *r, struct dlm_lkb *lkb, uint64_t seq);
void dlm_receive_rcom(struct dlm_ls *ls, const struct dlm_rcom *rc,
		      int nodeid);
int dlm_send_ls_not_ready(int nodeid, const struct dlm_rcom *rc_in);

#endif

