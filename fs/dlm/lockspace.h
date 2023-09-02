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

#ifndef __LOCKSPACE_DOT_H__
#define __LOCKSPACE_DOT_H__

/* DLM_LSFL_FS
 *   The lockspace user is in the kernel (i.e. filesystem).  Enables
 *   direct bast/cast callbacks.
 *
 * internal lockspace flag - will be removed in future
 */
#define DLM_LSFL_FS	0x00000004

int dlm_lockspace_init(void);
void dlm_lockspace_exit(void);
struct dlm_ls *dlm_find_lockspace_global(uint32_t id);
struct dlm_ls *dlm_find_lockspace_local(void *id);
struct dlm_ls *dlm_find_lockspace_device(int minor);
void dlm_put_lockspace(struct dlm_ls *ls);
void dlm_stop_lockspaces(void);
int dlm_new_user_lockspace(const char *name, const char *cluster,
			   uint32_t flags, int lvblen,
			   const struct dlm_lockspace_ops *ops,
			   void *ops_arg, int *ops_result,
			   dlm_lockspace_t **lockspace);

#endif				/* __LOCKSPACE_DOT_H__ */

