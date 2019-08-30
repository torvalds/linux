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

struct dlm_debug_ctxt {
	struct dentry *debug_state_dentry;
	struct dentry *debug_lockres_dentry;
	struct dentry *debug_mle_dentry;
	struct dentry *debug_purgelist_dentry;
};

struct debug_lockres {
	int dl_len;
	char *dl_buf;
	struct dlm_ctxt *dl_ctxt;
	struct dlm_lock_resource *dl_res;
};

int dlm_debug_init(struct dlm_ctxt *dlm);
void dlm_debug_shutdown(struct dlm_ctxt *dlm);

int dlm_create_debugfs_subroot(struct dlm_ctxt *dlm);
void dlm_destroy_debugfs_subroot(struct dlm_ctxt *dlm);

int dlm_create_debugfs_root(void);
void dlm_destroy_debugfs_root(void);

#else

static inline int dlm_debug_init(struct dlm_ctxt *dlm)
{
	return 0;
}
static inline void dlm_debug_shutdown(struct dlm_ctxt *dlm)
{
}
static inline int dlm_create_debugfs_subroot(struct dlm_ctxt *dlm)
{
	return 0;
}
static inline void dlm_destroy_debugfs_subroot(struct dlm_ctxt *dlm)
{
}
static inline int dlm_create_debugfs_root(void)
{
	return 0;
}
static inline void dlm_destroy_debugfs_root(void)
{
}

#endif	/* CONFIG_DEBUG_FS */
#endif	/* DLMDEBUG_H */
