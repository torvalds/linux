/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#ifndef __RECOVERD_DOT_H__
#define __RECOVERD_DOT_H__

void dlm_recoverd_stop(struct dlm_ls *ls);
int dlm_recoverd_start(struct dlm_ls *ls);
void dlm_recoverd_suspend(struct dlm_ls *ls);
void dlm_recoverd_resume(struct dlm_ls *ls);

#endif				/* __RECOVERD_DOT_H__ */

