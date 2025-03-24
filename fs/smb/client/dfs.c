// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Paulo Alcantara <palcantara@suse.de>
 */

#include "cifsproto.h"
#include "cifs_debug.h"
#include "dns_resolve.h"
#include "fs_context.h"
#include "dfs.h"

#define DFS_DOM(ctx) (ctx->dfs_root_ses ? ctx->dfs_root_ses->dns_dom : NULL)

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

	rc = dns_resolve_unc(DFS_DOM(ctx), path,
			     (struct sockaddr *)&ctx->dstaddr);
out:
	kfree(path);
	return rc;
}

static int get_session(struct cifs_mount_ctx *mnt_ctx, const char *full_path)
{
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	int rc;

	ctx->leaf_fullpath = (char *)full_path;
	ctx->dns_dom = DFS_DOM(ctx);
	rc = cifs_mount_get_session(mnt_ctx);
	ctx->leaf_fullpath = ctx->dns_dom = NULL;

	return rc;
}

/*
 * Get an active reference of @ses so that next call to cifs_put_tcon() won't
 * release it as any new DFS referrals must go through its IPC tcon.
 */
static void set_root_smb_session(struct cifs_mount_ctx *mnt_ctx)
{
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	struct cifs_ses *ses = mnt_ctx->ses;

	if (ses) {
		spin_lock(&cifs_tcp_ses_lock);
		cifs_smb_ses_inc_refcount(ses);
		spin_unlock(&cifs_tcp_ses_lock);
	}
	ctx->dfs_root_ses = ses;
}

static inline int parse_dfs_target(struct smb3_fs_context *ctx,
				   struct dfs_ref_walk *rw,
				   struct dfs_info3_param *tgt)
{
	int rc;
	const char *fpath = ref_walk_fpath(rw) + 1;

	rc = ref_walk_get_tgt(rw, tgt);
	if (!rc)
		rc = dfs_parse_target_referral(fpath, tgt, ctx);
	return rc;
}

static int setup_dfs_ref(struct dfs_info3_param *tgt, struct dfs_ref_walk *rw)
{
	struct cifs_sb_info *cifs_sb = rw->mnt_ctx->cifs_sb;
	struct smb3_fs_context *ctx = rw->mnt_ctx->fs_ctx;
	char *ref_path, *full_path;
	int rc;

	set_root_smb_session(rw->mnt_ctx);
	ref_walk_ses(rw) = ctx->dfs_root_ses;

	full_path = smb3_fs_context_fullpath(ctx, CIFS_DIR_SEP(cifs_sb));
	if (IS_ERR(full_path))
		return PTR_ERR(full_path);

	if (!tgt || (tgt->server_type == DFS_TYPE_LINK &&
		     DFS_INTERLINK(tgt->flags)))
		ref_path = dfs_get_path(cifs_sb, ctx->UNC);
	else
		ref_path = dfs_get_path(cifs_sb, full_path);
	if (IS_ERR(ref_path)) {
		rc = PTR_ERR(ref_path);
		kfree(full_path);
		return rc;
	}
	ref_walk_path(rw) = ref_path;
	ref_walk_fpath(rw) = full_path;

	return dfs_get_referral(rw->mnt_ctx,
				ref_walk_path(rw) + 1,
				ref_walk_tl(rw));
}

static int __dfs_referral_walk(struct dfs_ref_walk *rw)
{
	struct smb3_fs_context *ctx = rw->mnt_ctx->fs_ctx;
	struct cifs_mount_ctx *mnt_ctx = rw->mnt_ctx;
	struct dfs_info3_param tgt = {};
	int rc = -ENOENT;

again:
	do {
		ctx->dfs_root_ses = ref_walk_ses(rw);
		while (ref_walk_next_tgt(rw)) {
			rc = parse_dfs_target(ctx, rw, &tgt);
			if (rc)
				continue;

			cifs_mount_put_conns(mnt_ctx);
			rc = get_session(mnt_ctx, ref_walk_path(rw));
			if (rc)
				continue;

			rc = cifs_mount_get_tcon(mnt_ctx);
			if (rc) {
				if (tgt.server_type == DFS_TYPE_LINK &&
				    DFS_INTERLINK(tgt.flags))
					rc = -EREMOTE;
			} else {
				rc = cifs_is_path_remote(mnt_ctx);
				if (!rc) {
					ref_walk_set_tgt_hint(rw);
					break;
				}
			}
			if (rc == -EREMOTE) {
				rc = ref_walk_advance(rw);
				if (!rc) {
					rc = setup_dfs_ref(&tgt, rw);
					if (rc)
						break;
					ref_walk_mark_end(rw);
					goto again;
				}
			}
		}
	} while (rc && ref_walk_descend(rw));

	free_dfs_info_param(&tgt);
	return rc;
}

static int dfs_referral_walk(struct cifs_mount_ctx *mnt_ctx,
			     struct dfs_ref_walk **rw)
{
	int rc;

	*rw = ref_walk_alloc();
	if (IS_ERR(*rw)) {
		rc = PTR_ERR(*rw);
		*rw = NULL;
		return rc;
	}

	ref_walk_init(*rw, mnt_ctx);
	rc = setup_dfs_ref(NULL, *rw);
	if (!rc)
		rc = __dfs_referral_walk(*rw);
	return rc;
}

static int __dfs_mount_share(struct cifs_mount_ctx *mnt_ctx)
{
	struct cifs_sb_info *cifs_sb = mnt_ctx->cifs_sb;
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	struct dfs_ref_walk *rw = NULL;
	struct cifs_tcon *tcon;
	char *origin_fullpath;
	int rc;

	origin_fullpath = dfs_get_path(cifs_sb, ctx->source);
	if (IS_ERR(origin_fullpath))
		return PTR_ERR(origin_fullpath);

	rc = dfs_referral_walk(mnt_ctx, &rw);
	if (!rc) {
		/*
		 * Prevent superblock from being created with any missing
		 * connections.
		 */
		if (WARN_ON(!mnt_ctx->server))
			rc = -EHOSTDOWN;
		else if (WARN_ON(!mnt_ctx->ses))
			rc = -EACCES;
		else if (WARN_ON(!mnt_ctx->tcon))
			rc = -ENOENT;
	}
	if (rc)
		goto out;

	tcon = mnt_ctx->tcon;
	spin_lock(&tcon->tc_lock);
	tcon->origin_fullpath = origin_fullpath;
	origin_fullpath = NULL;
	ref_walk_set_tcon(rw, tcon);
	spin_unlock(&tcon->tc_lock);
	queue_delayed_work(dfscache_wq, &tcon->dfs_cache_work,
			   dfs_cache_get_ttl() * HZ);

out:
	kfree(origin_fullpath);
	ref_walk_free(rw);
	return rc;
}

/*
 * If @ctx->dfs_automount, then update @ctx->dstaddr earlier with the DFS root
 * server from where we'll start following any referrals.  Otherwise rely on the
 * value provided by mount(2) as the user might not have dns_resolver key set up
 * and therefore failing to upcall to resolve UNC hostname under @ctx->source.
 */
static int update_fs_context_dstaddr(struct smb3_fs_context *ctx)
{
	struct sockaddr *addr = (struct sockaddr *)&ctx->dstaddr;
	int rc = 0;

	if (!ctx->nodfs && ctx->dfs_automount) {
		rc = dns_resolve_unc(NULL, ctx->source, addr);
		if (!rc)
			cifs_set_port(addr, ctx->port);
		ctx->dfs_automount = false;
	}
	return rc;
}

int dfs_mount_share(struct cifs_mount_ctx *mnt_ctx)
{
	struct smb3_fs_context *ctx = mnt_ctx->fs_ctx;
	bool nodfs = ctx->nodfs;
	int rc;

	rc = update_fs_context_dstaddr(ctx);
	if (rc)
		return rc;

	rc = get_session(mnt_ctx, NULL);
	if (rc)
		return rc;

	/*
	 * If called with 'nodfs' mount option, then skip DFS resolving.  Otherwise unconditionally
	 * try to get an DFS referral (even cached) to determine whether it is an DFS mount.
	 *
	 * Skip prefix path to provide support for DFS referrals from w2k8 servers which don't seem
	 * to respond with PATH_NOT_COVERED to requests that include the prefix.
	 */
	if (!nodfs) {
		rc = dfs_get_referral(mnt_ctx, ctx->UNC + 1, NULL);
		if (rc) {
			cifs_dbg(FYI, "%s: no dfs referral for %s: %d\n",
				 __func__, ctx->UNC + 1, rc);
			cifs_dbg(FYI, "%s: assuming non-dfs mount...\n", __func__);
			nodfs = true;
		}
	}
	if (nodfs) {
		rc = cifs_mount_get_tcon(mnt_ctx);
		if (!rc)
			rc = cifs_is_path_remote(mnt_ctx);
		return rc;
	}

	if (!ctx->dfs_conn) {
		ctx->dfs_conn = true;
		cifs_mount_put_conns(mnt_ctx);
		rc = get_session(mnt_ctx, NULL);
	}
	if (!rc)
		rc = __dfs_mount_share(mnt_ctx);
	return rc;
}

static int target_share_matches_server(struct TCP_Server_Info *server, char *share,
				       bool *target_match)
{
	int rc = 0;
	const char *dfs_host;
	size_t dfs_host_len;

	*target_match = true;
	extract_unc_hostname(share, &dfs_host, &dfs_host_len);

	/* Check if hostnames or addresses match */
	cifs_server_lock(server);
	if (dfs_host_len != strlen(server->hostname) ||
	    strncasecmp(dfs_host, server->hostname, dfs_host_len)) {
		cifs_dbg(FYI, "%s: %.*s doesn't match %s\n", __func__,
			 (int)dfs_host_len, dfs_host, server->hostname);
		rc = match_target_ip(server, dfs_host, dfs_host_len, target_match);
		if (rc)
			cifs_dbg(VFS, "%s: failed to match target ip: %d\n", __func__, rc);
	}
	cifs_server_unlock(server);
	return rc;
}

static int tree_connect_dfs_target(const unsigned int xid,
				   struct cifs_tcon *tcon,
				   struct cifs_sb_info *cifs_sb,
				   char *tree, bool islink,
				   struct dfs_cache_tgt_list *tl)
{
	const struct smb_version_operations *ops = tcon->ses->server->ops;
	struct TCP_Server_Info *server = tcon->ses->server;
	struct dfs_cache_tgt_iterator *tit;
	char *share = NULL, *prefix = NULL;
	bool target_match;
	int rc = -ENOENT;

	/* Try to tree connect to all dfs targets */
	for (tit = dfs_cache_get_tgt_iterator(tl);
	     tit; tit = dfs_cache_get_next_tgt(tl, tit)) {
		kfree(share);
		kfree(prefix);
		share = prefix = NULL;

		/* Check if share matches with tcp ses */
		rc = dfs_cache_get_tgt_share(server->leaf_fullpath + 1, tit, &share, &prefix);
		if (rc) {
			cifs_dbg(VFS, "%s: failed to parse target share: %d\n", __func__, rc);
			break;
		}

		rc = target_share_matches_server(server, share, &target_match);
		if (rc)
			break;
		if (!target_match) {
			rc = -EHOSTUNREACH;
			continue;
		}

		dfs_cache_noreq_update_tgthint(server->leaf_fullpath + 1, tit);
		scnprintf(tree, MAX_TREE_SIZE, "\\%s", share);
		rc = ops->tree_connect(xid, tcon->ses, tree,
				       tcon, tcon->ses->local_nls);
		if (islink && !rc && cifs_sb)
			rc = cifs_update_super_prepath(cifs_sb, prefix);
		break;
	}

	kfree(share);
	kfree(prefix);
	dfs_cache_free_tgts(tl);
	return rc;
}

int cifs_tree_connect(const unsigned int xid, struct cifs_tcon *tcon)
{
	int rc;
	struct TCP_Server_Info *server = tcon->ses->server;
	const struct smb_version_operations *ops = server->ops;
	DFS_CACHE_TGT_LIST(tl);
	struct cifs_sb_info *cifs_sb = NULL;
	struct super_block *sb = NULL;
	struct dfs_info3_param ref = {0};
	char *tree;

	/* only send once per connect */
	spin_lock(&tcon->tc_lock);

	/* if tcon is marked for needing reconnect, update state */
	if (tcon->need_reconnect)
		tcon->status = TID_NEED_TCON;

	if (tcon->status == TID_GOOD) {
		spin_unlock(&tcon->tc_lock);
		return 0;
	}

	if (tcon->status != TID_NEW &&
	    tcon->status != TID_NEED_TCON) {
		spin_unlock(&tcon->tc_lock);
		return -EHOSTDOWN;
	}

	tcon->status = TID_IN_TCON;
	spin_unlock(&tcon->tc_lock);

	tree = kzalloc(MAX_TREE_SIZE, GFP_KERNEL);
	if (!tree) {
		rc = -ENOMEM;
		goto out;
	}

	if (tcon->ipc) {
		cifs_server_lock(server);
		scnprintf(tree, MAX_TREE_SIZE, "\\\\%s\\IPC$", server->hostname);
		cifs_server_unlock(server);
		rc = ops->tree_connect(xid, tcon->ses, tree,
				       tcon, tcon->ses->local_nls);
		goto out;
	}

	sb = cifs_get_dfs_tcon_super(tcon);
	if (!IS_ERR(sb))
		cifs_sb = CIFS_SB(sb);

	/* Tree connect to last share in @tcon->tree_name if no DFS referral */
	if (!server->leaf_fullpath ||
	    dfs_cache_noreq_find(server->leaf_fullpath + 1, &ref, &tl)) {
		rc = ops->tree_connect(xid, tcon->ses, tcon->tree_name,
				       tcon, tcon->ses->local_nls);
		goto out;
	}

	rc = tree_connect_dfs_target(xid, tcon, cifs_sb, tree, ref.server_type == DFS_TYPE_LINK,
				     &tl);
	free_dfs_info_param(&ref);

out:
	kfree(tree);
	cifs_put_tcp_super(sb);

	if (rc) {
		spin_lock(&tcon->tc_lock);
		if (tcon->status == TID_IN_TCON)
			tcon->status = TID_NEED_TCON;
		spin_unlock(&tcon->tc_lock);
	} else {
		spin_lock(&tcon->tc_lock);
		if (tcon->status == TID_IN_TCON)
			tcon->status = TID_GOOD;
		tcon->need_reconnect = false;
		spin_unlock(&tcon->tc_lock);
	}

	return rc;
}
