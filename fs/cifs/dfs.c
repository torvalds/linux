// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Paulo Alcantara <palcantara@suse.de>
 */

#include <linux/namei.h>
#include "cifsproto.h"
#include "cifs_debug.h"
#include "dns_resolve.h"
#include "fs_context.h"
#include "dfs.h"

/**
 * dfs_parse_target_referral - set fs context for dfs target referral
 *
 * @full_path: full path in UNC format.
 * @ref: dfs referral pointer.
 * @ctx: smb3 fs context pointer.
 *
 * Return zero if dfs referral was parsed correctly, otherwise non-zero.
 */
int dfs_parse_target_referral(const char *full_path, const struct dfs_info3_param *ref,
			      struct smb3_fs_context *ctx)
{
	int rc;
	const char *prepath = NULL;
	char *path;

	if (!full_path || !*full_path || !ref || !ctx)
		return -EINVAL;

	if (WARN_ON_ONCE(!ref->node_name || ref->path_consumed < 0))
		return -EINVAL;

	if (strlen(full_path) - ref->path_consumed) {
		prepath = full_path + ref->path_consumed;
		/* skip initial delimiter */
		if (*prepath == '/' || *prepath == '\\')
			prepath++;
	}

	path = cifs_build_devname(ref->node_name, prepath);
	if (IS_ERR(path))
		return PTR_ERR(path);

	rc = smb3_parse_devname(path, ctx);
	if (rc)
		goto out;

	rc = dns_resolve_server_name_to_ip(path, (struct sockaddr *)&ctx->dstaddr, NULL);

out:
	kfree(path);
	return rc;
}

/*
 * cifs_build_path_to_root returns full path to root when we do not have an
 * existing connection (tcon)
 */
static char *build_unc_path_to_root(const struct smb3_fs_context *ctx,
				    const struct cifs_sb_info *cifs_sb, bool useppath)
{
	char *full_path, *pos;
	unsigned int pplen = useppath && ctx->prepath ? strlen(ctx->prepath) + 1 : 0;
	unsigned int unc_len = strnlen(ctx->UNC, MAX_TREE_SIZE + 1);

	if (unc_len > MAX_TREE_SIZE)
		return ERR_PTR(-EINVAL);

	full_path = kmalloc(unc_len + pplen + 1, GFP_KERNEL);
	if (full_path == NULL)
		return ERR_PTR(-ENOMEM);

	memcpy(full_path, ctx->UNC, unc_len);
	pos = full_path + unc_len;

	if (pplen) {
		*pos = CIFS_DIR_SEP(cifs_sb);
		memcpy(pos + 1, ctx->prepath, pplen);
		pos += pplen;
	}

	*pos = '\0'; /* add trailing null */
	convert_delimiter(full_path, CIFS_DIR_SEP(cifs_sb));
	cifs_dbg(FYI, "%s: full_path=%s\n", __func__, full_path);
	return full_path;
}

static int get_session(struct cifs_mount_ctx *mnt_ctx, const char *full_path)
{
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	int rc;

	ctx->leaf_fullpath = (char *)full_path;
	rc = cifs_mount_get_session(mnt_ctx);
	ctx->leaf_fullpath = NULL;
	if (!rc) {
		struct cifs_ses *ses = mnt_ctx->ses;

		mutex_lock(&ses->session_mutex);
		ses->dfs_root_ses = mnt_ctx->root_ses;
		mutex_unlock(&ses->session_mutex);
	}
	return rc;
}

static void set_root_ses(struct cifs_mount_ctx *mnt_ctx)
{
	if (mnt_ctx->ses) {
		spin_lock(&cifs_tcp_ses_lock);
		mnt_ctx->ses->ses_count++;
		spin_unlock(&cifs_tcp_ses_lock);
		dfs_cache_add_refsrv_session(&mnt_ctx->mount_id, mnt_ctx->ses);
	}
	mnt_ctx->root_ses = mnt_ctx->ses;
}

static int get_dfs_conn(struct cifs_mount_ctx *mnt_ctx, const char *ref_path, const char *full_path,
			const struct dfs_cache_tgt_iterator *tit)
{
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	struct dfs_info3_param ref = {};
	int rc;

	rc = dfs_cache_get_tgt_referral(ref_path + 1, tit, &ref);
	if (rc)
		return rc;

	rc = dfs_parse_target_referral(full_path + 1, &ref, ctx);
	if (rc)
		goto out;

	cifs_mount_put_conns(mnt_ctx);
	rc = get_session(mnt_ctx, ref_path);
	if (rc)
		goto out;

	if (ref.flags & DFSREF_REFERRAL_SERVER)
		set_root_ses(mnt_ctx);

	rc = -EREMOTE;
	if (ref.flags & DFSREF_STORAGE_SERVER) {
		rc = cifs_mount_get_tcon(mnt_ctx);
		if (rc)
			goto out;

		/* some servers may not advertise referral capability under ref.flags */
		if (!(ref.flags & DFSREF_REFERRAL_SERVER) &&
		    is_tcon_dfs(mnt_ctx->tcon))
			set_root_ses(mnt_ctx);

		rc = cifs_is_path_remote(mnt_ctx);
	}

out:
	free_dfs_info_param(&ref);
	return rc;
}

static int __dfs_mount_share(struct cifs_mount_ctx *mnt_ctx)
{
	struct cifs_sb_info *cifs_sb = mnt_ctx->cifs_sb;
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	char *ref_path = NULL, *full_path = NULL;
	struct dfs_cache_tgt_iterator *tit;
	struct TCP_Server_Info *server;
	char *origin_fullpath = NULL;
	int num_links = 0;
	int rc;

	ref_path = dfs_get_path(cifs_sb, ctx->UNC);
	if (IS_ERR(ref_path))
		return PTR_ERR(ref_path);

	full_path = build_unc_path_to_root(ctx, cifs_sb, true);
	if (IS_ERR(full_path)) {
		rc = PTR_ERR(full_path);
		full_path = NULL;
		goto out;
	}

	origin_fullpath = kstrdup(full_path, GFP_KERNEL);
	if (!origin_fullpath) {
		rc = -ENOMEM;
		goto out;
	}

	do {
		struct dfs_cache_tgt_list tl = DFS_CACHE_TGT_LIST_INIT(tl);

		rc = dfs_get_referral(mnt_ctx, ref_path + 1, NULL, &tl);
		if (rc)
			break;

		tit = dfs_cache_get_tgt_iterator(&tl);
		if (!tit) {
			cifs_dbg(VFS, "%s: dfs referral (%s) with no targets\n", __func__,
				 ref_path + 1);
			rc = -ENOENT;
			dfs_cache_free_tgts(&tl);
			break;
		}

		do {
			rc = get_dfs_conn(mnt_ctx, ref_path, full_path, tit);
			if (!rc)
				break;
			if (rc == -EREMOTE) {
				if (++num_links > MAX_NESTED_LINKS) {
					rc = -ELOOP;
					break;
				}
				kfree(ref_path);
				kfree(full_path);
				ref_path = full_path = NULL;

				full_path = build_unc_path_to_root(ctx, cifs_sb, true);
				if (IS_ERR(full_path)) {
					rc = PTR_ERR(full_path);
					full_path = NULL;
				} else {
					ref_path = dfs_get_path(cifs_sb, full_path);
					if (IS_ERR(ref_path)) {
						rc = PTR_ERR(ref_path);
						ref_path = NULL;
					}
				}
				break;
			}
		} while ((tit = dfs_cache_get_next_tgt(&tl, tit)));
		dfs_cache_free_tgts(&tl);
	} while (rc == -EREMOTE);

	if (!rc) {
		server = mnt_ctx->server;

		mutex_lock(&server->refpath_lock);
		server->origin_fullpath = origin_fullpath;
		server->current_fullpath = server->leaf_fullpath;
		mutex_unlock(&server->refpath_lock);
		origin_fullpath = NULL;
	}

out:
	kfree(origin_fullpath);
	kfree(ref_path);
	kfree(full_path);
	return rc;
}

int dfs_mount_share(struct cifs_mount_ctx *mnt_ctx, bool *isdfs)
{
	struct cifs_sb_info *cifs_sb = mnt_ctx->cifs_sb;
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	int rc;

	*isdfs = false;

	rc = get_session(mnt_ctx, NULL);
	if (rc)
		return rc;
	mnt_ctx->root_ses = mnt_ctx->ses;
	/*
	 * If called with 'nodfs' mount option, then skip DFS resolving.  Otherwise unconditionally
	 * try to get an DFS referral (even cached) to determine whether it is an DFS mount.
	 *
	 * Skip prefix path to provide support for DFS referrals from w2k8 servers which don't seem
	 * to respond with PATH_NOT_COVERED to requests that include the prefix.
	 */
	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_DFS) ||
	    dfs_get_referral(mnt_ctx, ctx->UNC + 1, NULL, NULL)) {
		rc = cifs_mount_get_tcon(mnt_ctx);
		if (rc)
			return rc;

		rc = cifs_is_path_remote(mnt_ctx);
		if (!rc || rc != -EREMOTE)
			return rc;
	}

	*isdfs = true;
	set_root_ses(mnt_ctx);

	return __dfs_mount_share(mnt_ctx);
}
