/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmdebug.h
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#ifndef DLMDEBUG_H
#define DLMDEBUG_H

void dlm_print_one_mle(struct dlm_master_list_entry *mle);

#ifdef CONFIG_DEBUG_FS

struct debug_lockres {
	int dl_len;
	char *dl_buf;
	struct dlm_ctxt *dl_ctxt;
	struct dlm_lock_resource *dl_res;
};

void dlm_debug_init(struct dlm_ctxt *dlm);

void dlm_create_debugfs_subroot(struct dlm_ctxt *dlm);
void dlm_destroy_debugfs_subroot(struct dlm_ctxt *dlm);

void dlm_create_debugfs_root(void);
void dlm_destroy_debugfs_root(void);

#else

static inline void dlm_debug_init(struct dlm_ctxt *dlm)
{
}
static inline void dlm_create_debugfs_subroot(struct dlm_ctxt *dlm)
{
}
static inline void dlm_destroy_debugfs_subroot(struct dlm_ctxt *dlm)
{
}
static inline void dlm_create_debugfs_root(void)
{
}
static inline void dlm_destroy_debugfs_root(void)
{
}

#endif	/* CONFIG_DEBUG_FS */
#endif	/* DLMDEBUG_H */
