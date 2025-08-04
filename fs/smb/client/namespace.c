// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Contains mounting routines used for handling traversal via SMB junctions.
 *
 *   Copyright (c) 2007 Igor Mammedov
 *   Copyright (C) International Business Machines  Corp., 2008
 *   Author(s): Igor Mammedov (niallain@gmail.com)
 *		Steve French (sfrench@us.ibm.com)
 *   Copyright (c) 2023 Paulo Alcantara <palcantara@suse.de>
 */

#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/vfs.h>
#include <linux/fs.h>
#include <linux/inet.h>
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifsfs.h"
#include "cifs_debug.h"
#include "fs_context.h"

static LIST_HEAD(cifs_automount_list);

static void cifs_expire_automounts(struct work_struct *work);
static DECLARE_DELAYED_WORK(cifs_automount_task,
			    cifs_expire_automounts);
static int cifs_mountpoint_expiry_timeout = 500 * HZ;

static void cifs_expire_automounts(struct work_struct *work)
{
	struct list_head *list = &cifs_automount_list;

	mark_mounts_for_expiry(list);
	if (!list_empty(list))
		schedule_delayed_work(&cifs_automount_task,
				      cifs_mountpoint_expiry_timeout);
}

void cifs_release_automount_timer(void)
{
	if (WARN_ON(!list_empty(&cifs_automount_list)))
		return;
	cancel_delayed_work_sync(&cifs_automount_task);
}

/**
 * cifs_build_devname - build a devicename from a UNC and optional prepath
 * @nodename:	pointer to UNC string
 * @prepath:	pointer to prefixpath (or NULL if there isn't one)
 *
 * Build a new cifs devicename after chasing a DFS referral. Allocate a buffer
 * big enough to hold the final thing. Copy the UNC from the nodename, and
 * concatenate the prepath onto the end of it if there is one.
 *
 * Returns pointer to the built string, or a ERR_PTR. Caller is responsible
 * for freeing the returned string.
 */
char *
cifs_build_devname(char *nodename, const char *prepath)
{
	size_t pplen;
	size_t unclen;
	char *dev;
	char *pos;

	/* skip over any preceding delimiters */
	nodename += strspn(nodename, "\\");
	if (!*nodename)
		return ERR_PTR(-EINVAL);

	/* get length of UNC and set pos to last char */
	unclen = strlen(nodename);
	pos = nodename + unclen - 1;

	/* trim off any trailing delimiters */
	while (*pos == '\\') {
		--pos;
		--unclen;
	}

	/* allocate a buffer:
	 * +2 for preceding "//"
	 * +1 for delimiter between UNC and prepath
	 * +1 for trailing NULL
	 */
	pplen = prepath ? strlen(prepath) : 0;
	dev = kmalloc(2 + unclen + 1 + pplen + 1, GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	pos = dev;
	/* add the initial "//" */
	*pos = '/';
	++pos;
	*pos = '/';
	++pos;

	/* copy in the UNC portion from referral */
	memcpy(pos, nodename, unclen);
	pos += unclen;

	/* copy the prefixpath remainder (if there is one) */
	if (pplen) {
		*pos = '/';
		++pos;
		memcpy(pos, prepath, pplen);
		pos += pplen;
	}

	/* NULL terminator */
	*pos = '\0';

	convert_delimiter(dev, '/');
	return dev;
}

static bool is_dfs_mount(struct dentry *dentry)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(dentry->d_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	bool ret;

	spin_lock(&tcon->tc_lock);
	ret = !!tcon->origin_fullpath;
	spin_unlock(&tcon->tc_lock);
	return ret;
}

/* Return full path out of a dentry set for automount */
static char *automount_fullpath(struct dentry *dentry, void *page)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(dentry->d_sb);
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);
	size_t len;
	char *s;

	spin_lock(&tcon->tc_lock);
	if (!tcon->origin_fullpath) {
		spin_unlock(&tcon->tc_lock);
		return build_path_from_dentry_optional_prefix(dentry,
							      page,
							      true);
	}
	spin_unlock(&tcon->tc_lock);

	if (unlikely(!page))
		return ERR_PTR(-ENOMEM);

	s = dentry_path_raw(dentry, page, PATH_MAX);
	if (IS_ERR(s))
		return s;
	/* for root, we want "" */
	if (!s[1])
		s++;

	spin_lock(&tcon->tc_lock);
	len = strlen(tcon->origin_fullpath);
	if (s < (char *)page + len) {
		spin_unlock(&tcon->tc_lock);
		return ERR_PTR(-ENAMETOOLONG);
	}

	s -= len;
	memcpy(s, tcon->origin_fullpath, len);
	spin_unlock(&tcon->tc_lock);
	convert_delimiter(s, '/');

	return s;
}

static void fs_context_set_ids(struct smb3_fs_context *ctx)
{
	kuid_t uid = current_fsuid();
	kgid_t gid = current_fsgid();

	if (ctx->multiuser) {
		if (!ctx->uid_specified)
			ctx->linux_uid = uid;
		if (!ctx->gid_specified)
			ctx->linux_gid = gid;
	}
	if (!ctx->cruid_specified)
		ctx->cred_uid = uid;
}

/*
 * Create a vfsmount that we can automount
 */
static struct vfsmount *cifs_do_automount(struct path *path)
{
	int rc;
	struct dentry *mntpt = path->dentry;
	struct fs_context *fc;
	void *page = NULL;
	struct smb3_fs_context *ctx, *cur_ctx;
	struct smb3_fs_context tmp;
	char *full_path;
	struct vfsmount *mnt;
	struct cifs_sb_info *mntpt_sb;
	struct cifs_ses *ses;

	if (IS_ROOT(mntpt))
		return ERR_PTR(-ESTALE);

	mntpt_sb = CIFS_SB(mntpt->d_sb);
	ses = cifs_sb_master_tcon(mntpt_sb)->ses;
	cur_ctx = mntpt_sb->ctx;

	/*
	 * At this point, the root session should be in the mntpt sb. We should
	 * bring the sb context passwords in sync with the root session's
	 * passwords. This would help prevent unnecessary retries and password
	 * swaps for automounts.
	 */
	mutex_lock(&ses->session_mutex);
	rc = smb3_sync_session_ctx_passwords(mntpt_sb, ses);
	mutex_unlock(&ses->session_mutex);

	if (rc)
		return ERR_PTR(rc);

	fc = fs_context_for_submount(path->mnt->mnt_sb->s_type, mntpt);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	ctx = smb3_fc2context(fc);

	page = alloc_dentry_path();
	full_path = automount_fullpath(mntpt, page);
	if (IS_ERR(full_path)) {
		mnt = ERR_CAST(full_path);
		goto out;
	}

	tmp = *cur_ctx;
	tmp.source = NULL;
	tmp.leaf_fullpath = NULL;
	tmp.UNC = tmp.prepath = NULL;
	tmp.dfs_root_ses = NULL;
	fs_context_set_ids(&tmp);

	rc = smb3_fs_context_dup(ctx, &tmp);
	if (rc) {
		mnt = ERR_PTR(rc);
		goto out;
	}

	rc = smb3_parse_devname(full_path, ctx);
	if (rc) {
		mnt = ERR_PTR(rc);
		goto out;
	}

	ctx->source = smb3_fs_context_fullpath(ctx, '/');
	if (IS_ERR(ctx->source)) {
		mnt = ERR_CAST(ctx->source);
		ctx->source = NULL;
		goto out;
	}
	ctx->dfs_automount = ctx->dfs_conn = is_dfs_mount(mntpt);
	cifs_dbg(FYI, "%s: ctx: source=%s UNC=%s prepath=%s dfs_automount=%d\n",
		 __func__, ctx->source, ctx->UNC, ctx->prepath, ctx->dfs_automount);

	mnt = fc_mount(fc);
out:
	put_fs_context(fc);
	free_dentry_path(page);
	return mnt;
}

/*
 * Attempt to automount the referral
 */
struct vfsmount *cifs_d_automount(struct path *path)
{
	struct vfsmount *newmnt;

	cifs_dbg(FYI, "%s: %pd\n", __func__, path->dentry);

	newmnt = cifs_do_automount(path);
	if (IS_ERR(newmnt)) {
		cifs_dbg(FYI, "leaving %s [automount failed]\n" , __func__);
		return newmnt;
	}

	mnt_set_expiry(newmnt, &cifs_automount_list);
	schedule_delayed_work(&cifs_automount_task,
			      cifs_mountpoint_expiry_timeout);
	cifs_dbg(FYI, "leaving %s [ok]\n" , __func__);
	return newmnt;
}

const struct inode_operations cifs_namespace_inode_operations = {
};
