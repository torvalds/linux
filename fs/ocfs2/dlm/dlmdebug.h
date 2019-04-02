/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmde.h
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#ifndef DLMDE_H
#define DLMDE_H

void dlm_print_one_mle(struct dlm_master_list_entry *mle);

#ifdef CONFIG_DE_FS

struct dlm_de_ctxt {
	struct dentry *de_state_dentry;
	struct dentry *de_lockres_dentry;
	struct dentry *de_mle_dentry;
	struct dentry *de_purgelist_dentry;
};

struct de_lockres {
	int dl_len;
	char *dl_buf;
	struct dlm_ctxt *dl_ctxt;
	struct dlm_lock_resource *dl_res;
};

int dlm_de_init(struct dlm_ctxt *dlm);
void dlm_de_shutdown(struct dlm_ctxt *dlm);

int dlm_create_defs_subroot(struct dlm_ctxt *dlm);
void dlm_destroy_defs_subroot(struct dlm_ctxt *dlm);

int dlm_create_defs_root(void);
void dlm_destroy_defs_root(void);

#else

static inline int dlm_de_init(struct dlm_ctxt *dlm)
{
	return 0;
}
static inline void dlm_de_shutdown(struct dlm_ctxt *dlm)
{
}
static inline int dlm_create_defs_subroot(struct dlm_ctxt *dlm)
{
	return 0;
}
static inline void dlm_destroy_defs_subroot(struct dlm_ctxt *dlm)
{
}
static inline int dlm_create_defs_root(void)
{
	return 0;
}
static inline void dlm_destroy_defs_root(void)
{
}

#endif	/* CONFIG_DE_FS */
#endif	/* DLMDE_H */
