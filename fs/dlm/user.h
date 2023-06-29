/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2006-2010 Red Hat, Inc.  All rights reserved.
 */

#ifndef __USER_DOT_H__
#define __USER_DOT_H__

void dlm_purge_lkb_callbacks(struct dlm_lkb *lkb);
void dlm_user_add_ast(struct dlm_lkb *lkb, uint32_t flags, int mode,
		      int status, uint32_t sbflags);
int dlm_user_init(void);
void dlm_user_exit(void);
int dlm_device_deregister(struct dlm_ls *ls);
int dlm_user_daemon_available(void);

#endif
