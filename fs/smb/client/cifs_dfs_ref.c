// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Contains the CIFS DFS referral mounting routines used for handling
 *   traversal via DFS junction point
 *
 *   Copyright (c) 2007 Igor Mammedov
 *   Copyright (C) International Business Machines  Corp., 2008
 *   Author(s): Igor Mammedov (niallain@gmail.com)
 *		Steve French (sfrench@us.ibm.com)
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
#include "dns_resolve.h"
#include "cifs_debug.h"
#include "cifs_unicode.h"
#include "dfs_cache.h"
#include "fs_context.h"

static LIST_HEAD(cifs_dfs_automount_list);

static void cifs_dfs_expire_automounts(struct work_struct *work);
static DECLARE_DELAYED_WORK(cifs_dfs_automount_task,
			    cifs_dfs_expire_automounts);
static int cifs_dfs_mountpoint_expiry_timeout = 500 * HZ;

static void cifs_dfs_expire_automounts(struct work_struct *work)
{
	struct list_head *list = &cifs_dfs_automount_list;

	mark_mounts_for_expiry(list);
	if (!list_empty(list))
		schedule_delayed_work(&cifs_dfs_automount_task,
				      cifs_dfs_mountpoint_expiry_timeout);
}

void cifs_dfs_release_automount_timer(void)
{
	BUG_ON(!list_empty(&cifs_dfs_automount_list));
	cancel_delayed_work_sync(&cifs_dfs_automount_task);
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
static char *
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


/**
 * cifs_compose_mount_options	-	creates mount options for referral
 * @sb_mountdata:	parent/root DFS mount options (template)
 * @fullpath:		full path in UNC format
 * @ref:		optional server's referral
 * @devname:		return the built cifs device name if passed pointer not NULL
 * creates mount options for submount based on template options sb_mountdata
 * and replacing unc,ip,prefixpath options with ones we've got form ref_unc.
 *
 * Returns: pointer to new mount options or ERR_PTR.
 * Caller is responsible for freeing returned value if it is not error.
 */
char *cifs_compose_mount_options(const char *sb_mountdata,
				 const char *fullpath,
				 const struct dfs_info3_param *ref,
				 char **devname)
{
	int rc;
	char *name;
	char *mountdata = NULL;
	const char *prepath = NULL;
	int md_len;
	char *tkn_e;
	char *srvIP = NULL;
	char sep = ',';
	int off, noff;

	if (sb_mountdata == NULL)
		return ERR_PTR(-EINVAL);

	if (ref) {
		if (WARN_ON_ONCE(!ref->node_name || ref->path_consumed < 0))
			return ERR_PTR(-EINVAL);

		if (strlen(fullpath) - ref->path_consumed) {
			prepath = fullpath + ref->path_consumed;
			/* skip initial delimiter */
			if (*prepath == '/' || *prepath == '\\')
				prepath++;
		}

		name = cifs_build_devname(ref->node_name, prepath);
		if (IS_ERR(name)) {
			rc = PTR_ERR(name);
			name = NULL;
			goto compose_mount_options_err;
		}
	} else {
		name = cifs_build_devname((char *)fullpath, NULL);
		if (IS_ERR(name)) {
			rc = PTR_ERR(name);
			name = NULL;
			goto compose_mount_options_err;
		}
	}

	rc = dns_resolve_server_name_to_ip(name, &srvIP, NULL);
	if (rc < 0) {
		cifs_dbg(FYI, "%s: Failed to resolve server part of %s to IP: %d\n",
			 __func__, name, rc);
		goto compose_mount_options_err;
	}

	/*
	 * In most cases, we'll be building a shorter string than the original,
	 * but we do have to assume that the address in the ip= option may be
	 * much longer than the original. Add the max length of an address
	 * string to the length of the original string to allow for worst case.
	 */
	md_len = strlen(sb_mountdata) + INET6_ADDRSTRLEN;
	mountdata = kzalloc(md_len + sizeof("ip=") + 1, GFP_KERNEL);
	if (mountdata == NULL) {
		rc = -ENOMEM;
		goto compose_mount_options_err;
	}

	/* copy all options except of unc,ip,prefixpath */
	off = 0;
	if (strncmp(sb_mountdata, "sep=", 4) == 0) {
			sep = sb_mountdata[4];
			strncpy(mountdata, sb_mountdata, 5);
			off += 5;
	}

	do {
		tkn_e = strchr(sb_mountdata + off, sep);
		if (tkn_e == NULL)
			noff = strlen(sb_mountdata + off);
		else
			noff = tkn_e - (sb_mountdata + off) + 1;

		if (strncasecmp(sb_mountdata + off, "cruid=", 6) == 0) {
			off += noff;
			continue;
		}
		if (strncasecmp(sb_mountdata + off, "unc=", 4) == 0) {
			off += noff;
			continue;
		}
		if (strncasecmp(sb_mountdata + off, "ip=", 3) == 0) {
			off += noff;
			continue;
		}
		if (strncasecmp(sb_mountdata + off, "prefixpath=", 11) == 0) {
			off += noff;
			continue;
		}
		strncat(mountdata, sb_mountdata + off, noff);
		off += noff;
	} while (tkn_e);
	strcat(mountdata, sb_mountdata + off);
	mountdata[md_len] = '\0';

	/* copy new IP and ref share name */
	if (mountdata[strlen(mountdata) - 1] != sep)
		strncat(mountdata, &sep, 1);
	strcat(mountdata, "ip=");
	strcat(mountdata, srvIP);

	if (devname)
		*devname = name;
	else
		kfree(name);

	/*cifs_dbg(FYI, "%s: parent mountdata: %s\n", __func__, sb_mountdata);*/
	/*cifs_dbg(FYI, "%s: submount mountdata: %s\n", __func__, mountdata );*/

compose_mount_options_out:
	kfree(srvIP);
	return mountdata;

compose_mount_options_err:
	kfree(mountdata);
	mountdata = ERR_PTR(rc);
	kfree(name);
	goto compose_mount_options_out;
}

/*
 * Create a vfsmount that we can automount
 */
static struct vfsmount *cifs_dfs_do_automount(struct path *path)
{
	int rc;
	struct dentry *mntpt = path->dentry;
	struct fs_context *fc;
	struct cifs_sb_info *cifs_sb;
	void *page = NULL;
	struct smb3_fs_context *ctx, *cur_ctx;
	struct smb3_fs_context tmp;
	char *full_path;
	struct vfsmount *mnt;

	if (IS_ROOT(mntpt))
		return ERR_PTR(-ESTALE);

	/*
	 * The MSDFS spec states that paths in DFS referral requests and
	 * responses must be prefixed by a single '\' character instead of
	 * the double backslashes usually used in the UNC. This function
	 * gives us the latter, so we must adjust the result.
	 */
	cifs_sb = CIFS_SB(mntpt->d_sb);
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_DFS)
		return ERR_PTR(-EREMOTE);

	cur_ctx = cifs_sb->ctx;

	fc = fs_context_for_submount(path->mnt->mnt_sb->s_type, mntpt);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	ctx = smb3_fc2context(fc);

	page = alloc_dentry_path();
	/* always use tree name prefix */
	full_path = build_path_from_dentry_optional_prefix(mntpt, page, true);
	if (IS_ERR(full_path)) {
		mnt = ERR_CAST(full_path);
		goto out;
	}

	convert_delimiter(full_path, '/');
	cifs_dbg(FYI, "%s: full_path: %s\n", __func__, full_path);

	tmp = *cur_ctx;
	tmp.source = full_path;
	tmp.UNC = tmp.prepath = NULL;

	rc = smb3_fs_context_dup(ctx, &tmp);
	if (rc) {
		mnt = ERR_PTR(rc);
		goto out;
	}

	rc = smb3_parse_devname(full_path, ctx);
	if (!rc)
		mnt = fc_mount(fc);
	else
		mnt = ERR_PTR(rc);

out:
	put_fs_context(fc);
	free_dentry_path(page);
	return mnt;
}

/*
 * Attempt to automount the referral
 */
struct vfsmount *cifs_dfs_d_automount(struct path *path)
{
	struct vfsmount *newmnt;

	cifs_dbg(FYI, "%s: %pd\n", __func__, path->dentry);

	newmnt = cifs_dfs_do_automount(path);
	if (IS_ERR(newmnt)) {
		cifs_dbg(FYI, "leaving %s [automount failed]\n" , __func__);
		return newmnt;
	}

	mntget(newmnt); /* prevent immediate expiration */
	mnt_set_expiry(newmnt, &cifs_dfs_automount_list);
	schedule_delayed_work(&cifs_dfs_automount_task,
			      cifs_dfs_mountpoint_expiry_timeout);
	cifs_dbg(FYI, "leaving %s [ok]\n" , __func__);
	return newmnt;
}

const struct inode_operations cifs_dfs_referral_inode_operations = {
};
