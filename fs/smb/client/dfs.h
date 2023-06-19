/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Paulo Alcantara <palcantara@suse.de>
 */

#ifndef _CIFS_DFS_H
#define _CIFS_DFS_H

#include "cifsglob.h"
#include "fs_context.h"
#include "cifs_unicode.h"

struct dfs_root_ses {
	struct list_head list;
	struct cifs_ses *ses;
};

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

	return dfs_cache_find(mnt_ctx->xid, ctx->dfs_root_ses, cifs_sb->local_nls,
			      cifs_remap(cifs_sb), path, ref, tl);
}

/* Return DFS full path out of a dentry set for automount */
static inline char *dfs_get_automount_devname(struct dentry *dentry, void *page)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(dentry->d_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	struct TCP_Server_Info *server = tcon->ses->server;
	size_t len;
	char *s;

	spin_lock(&server->srv_lock);
	if (unlikely(!server->origin_fullpath)) {
		spin_unlock(&server->srv_lock);
		return ERR_PTR(-EREMOTE);
	}
	spin_unlock(&server->srv_lock);

	s = dentry_path_raw(dentry, page, PATH_MAX);
	if (IS_ERR(s))
		return s;
	/* for root, we want "" */
	if (!s[1])
		s++;

	spin_lock(&server->srv_lock);
	len = strlen(server->origin_fullpath);
	if (s < (char *)page + len) {
		spin_unlock(&server->srv_lock);
		return ERR_PTR(-ENAMETOOLONG);
	}

	s -= len;
	memcpy(s, server->origin_fullpath, len);
	spin_unlock(&server->srv_lock);
	convert_delimiter(s, '/');

	return s;
}

static inline void dfs_put_root_smb_sessions(struct list_head *head)
{
	struct dfs_root_ses *root, *tmp;

	list_for_each_entry_safe(root, tmp, head, list) {
		list_del_init(&root->list);
		cifs_put_smb_ses(root->ses);
		kfree(root);
	}
}

#endif /* _CIFS_DFS_H */
