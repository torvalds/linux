/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Paulo Alcantara <palcantara@suse.de>
 */

#ifndef _CIFS_DFS_H
#define _CIFS_DFS_H

#include "cifsglob.h"
#include "cifsproto.h"
#include "fs_context.h"
#include "dfs_cache.h"
#include "cifs_unicode.h"
#include <linux/namei.h>

#define DFS_INTERLINK(v) \
	(((v) & DFSREF_REFERRAL_SERVER) && !((v) & DFSREF_STORAGE_SERVER))

struct dfs_ref {
	char *path;
	char *full_path;
	struct dfs_cache_tgt_list tl;
	struct dfs_cache_tgt_iterator *tit;
};

struct dfs_ref_walk {
	struct dfs_ref *ref;
	struct dfs_ref refs[MAX_NESTED_LINKS];
};

#define ref_walk_start(w)	((w)->refs)
#define ref_walk_end(w)	(&(w)->refs[ARRAY_SIZE((w)->refs) - 1])
#define ref_walk_cur(w)	((w)->ref)
#define ref_walk_descend(w)	(--ref_walk_cur(w) >= ref_walk_start(w))

#define ref_walk_tit(w)	(ref_walk_cur(w)->tit)
#define ref_walk_empty(w)	(!ref_walk_tit(w))
#define ref_walk_path(w)	(ref_walk_cur(w)->path)
#define ref_walk_fpath(w)	(ref_walk_cur(w)->full_path)
#define ref_walk_tl(w)		(&ref_walk_cur(w)->tl)

static inline struct dfs_ref_walk *ref_walk_alloc(void)
{
	struct dfs_ref_walk *rw;

	rw = kmalloc(sizeof(*rw), GFP_KERNEL);
	if (!rw)
		return ERR_PTR(-ENOMEM);
	return rw;
}

static inline void ref_walk_init(struct dfs_ref_walk *rw)
{
	memset(rw, 0, sizeof(*rw));
	ref_walk_cur(rw) = ref_walk_start(rw);
}

static inline void __ref_walk_free(struct dfs_ref *ref)
{
	kfree(ref->path);
	kfree(ref->full_path);
	dfs_cache_free_tgts(&ref->tl);
	memset(ref, 0, sizeof(*ref));
}

static inline void ref_walk_free(struct dfs_ref_walk *rw)
{
	struct dfs_ref *ref = ref_walk_start(rw);

	for (; ref <= ref_walk_end(rw); ref++)
		__ref_walk_free(ref);
	kfree(rw);
}

static inline int ref_walk_advance(struct dfs_ref_walk *rw)
{
	struct dfs_ref *ref = ref_walk_cur(rw) + 1;

	if (ref > ref_walk_end(rw))
		return -ELOOP;
	__ref_walk_free(ref);
	ref_walk_cur(rw) = ref;
	return 0;
}

static inline struct dfs_cache_tgt_iterator *
ref_walk_next_tgt(struct dfs_ref_walk *rw)
{
	struct dfs_cache_tgt_iterator *tit;
	struct dfs_ref *ref = ref_walk_cur(rw);

	if (!ref->tit)
		tit = dfs_cache_get_tgt_iterator(&ref->tl);
	else
		tit = dfs_cache_get_next_tgt(&ref->tl, ref->tit);
	ref->tit = tit;
	return tit;
}

static inline int ref_walk_get_tgt(struct dfs_ref_walk *rw,
				   struct dfs_info3_param *tgt)
{
	zfree_dfs_info_param(tgt);
	return dfs_cache_get_tgt_referral(ref_walk_path(rw) + 1,
					  ref_walk_tit(rw), tgt);
}

static inline int ref_walk_num_tgts(struct dfs_ref_walk *rw)
{
	return dfs_cache_get_nr_tgts(ref_walk_tl(rw));
}

static inline void ref_walk_set_tgt_hint(struct dfs_ref_walk *rw)
{
	dfs_cache_noreq_update_tgthint(ref_walk_path(rw) + 1,
				       ref_walk_tit(rw));
}

int dfs_parse_target_referral(const char *full_path, const struct dfs_info3_param *ref,
			      struct smb3_fs_context *ctx);
int dfs_mount_share(struct cifs_mount_ctx *mnt_ctx, bool *isdfs);

static inline char *dfs_get_path(struct cifs_sb_info *cifs_sb, const char *path)
{
	return dfs_cache_canonical_path(path, cifs_sb->local_nls, cifs_remap(cifs_sb));
}

static inline int dfs_get_referral(struct cifs_mount_ctx *mnt_ctx, const char *path,
				   struct dfs_info3_param *ref, struct dfs_cache_tgt_list *tl)
{
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	struct cifs_sb_info *cifs_sb = mnt_ctx->cifs_sb;
	struct cifs_ses *rses = ctx->dfs_root_ses ?: mnt_ctx->ses;

	return dfs_cache_find(mnt_ctx->xid, rses, cifs_sb->local_nls,
			      cifs_remap(cifs_sb), path, ref, tl);
}

/*
 * cifs_get_smb_ses() already guarantees an active reference of
 * @ses->dfs_root_ses when a new session is created, so we need to put extra
 * references of all DFS root sessions that were used across the mount process
 * in dfs_mount_share().
 */
static inline void dfs_put_root_smb_sessions(struct cifs_mount_ctx *mnt_ctx)
{
	const struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	struct cifs_ses *ses = ctx->dfs_root_ses;
	struct cifs_ses *cur;

	if (!ses)
		return;

	for (cur = ses; cur; cur = cur->dfs_root_ses) {
		if (cur->dfs_root_ses)
			cifs_put_smb_ses(cur->dfs_root_ses);
	}
	cifs_put_smb_ses(ses);
}

#endif /* _CIFS_DFS_H */
