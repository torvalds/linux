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
 * @devname:		optional pointer for saving device name
 *
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

	rc = dns_resolve_server_name_to_ip(name, &srvIP);
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

/**
 * cifs_dfs_do_mount - mounts specified path using DFS full path
 *
 * Always pass down @fullpath to smb3_do_mount() so we can use the root server
 * to perform failover in case we failed to connect to the first target in the
 * referral.
 *
 * @cifs_sb:		parent/root superblock
 * @fullpath:		full path in UNC format
 */
static struct vfsmount *cifs_dfs_do_mount(struct dentry *mntpt,
					  struct cifs_sb_info *cifs_sb,
					  const char *fullpath)
{
	struct vfsmount *mnt;
	char *mountdata;
	char *devname;

	devname = kstrndup(fullpath, strlen(fullpath), GFP_KERNEL);
	if (!devname)
		return ERR_PTR(-ENOMEM);

	convert_delimiter(devname, '/');

	/* strip first '\' from fullpath */
	mountdata = cifs_compose_mount_options(cifs_sb->mountdata,
					       fullpath + 1, NULL, NULL);
	if (IS_ERR(mountdata)) {
		kfree(devname);
		return (struct vfsmount *)mountdata;
	}

	mnt = vfs_submount(mntpt, &cifs_fs_type, devname, mountdata);
	kfree(mountdata);
	kfree(devname);
	return mnt;
}

/*
 * Create a vfsmount that we can automount
 */
static struct vfsmount *cifs_dfs_do_automount(struct dentry *mntpt)
{
	struct cifs_sb_info *cifs_sb;
	struct cifs_ses *ses;
	struct cifs_tcon *tcon;
	char *full_path, *root_path;
	unsigned int xid;
	int rc;
	struct vfsmount *mnt;

	cifs_dbg(FYI, "in %s\n", __func__);
	BUG_ON(IS_ROOT(mntpt));

	/*
	 * The MSDFS spec states that paths in DFS referral requests and
	 * responses must be prefixed by a single '\' character instead of
	 * the double backslashes usually used in the UNC. This function
	 * gives us the latter, so we must adjust the result.
	 */
	mnt = ERR_PTR(-ENOMEM);

	cifs_sb = CIFS_SB(mntpt->d_sb);
	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_DFS) {
		mnt = ERR_PTR(-EREMOTE);
		goto cdda_exit;
	}

	/* always use tree name prefix */
	full_path = build_path_from_dentry_optional_prefix(mntpt, true);
	if (full_path == NULL)
		goto cdda_exit;

	cifs_dbg(FYI, "%s: full_path: %s\n", __func__, full_path);

	if (!cifs_sb_master_tlink(cifs_sb)) {
		cifs_dbg(FYI, "%s: master tlink is NULL\n", __func__);
		goto free_full_path;
	}

	tcon = cifs_sb_master_tcon(cifs_sb);
	if (!tcon) {
		cifs_dbg(FYI, "%s: master tcon is NULL\n", __func__);
		goto free_full_path;
	}

	root_path = kstrdup(tcon->treeName, GFP_KERNEL);
	if (!root_path) {
		mnt = ERR_PTR(-ENOMEM);
		goto free_full_path;
	}
	cifs_dbg(FYI, "%s: root path: %s\n", __func__, root_path);

	ses = tcon->ses;
	xid = get_xid();

	/*
	 * If DFS root has been expired, then unconditionally fetch it again to
	 * refresh DFS referral cache.
	 */
	rc = dfs_cache_find(xid, ses, cifs_sb->local_nls, cifs_remap(cifs_sb),
			    root_path + 1, NULL, NULL);
	if (!rc) {
		rc = dfs_cache_find(xid, ses, cifs_sb->local_nls,
				    cifs_remap(cifs_sb), full_path + 1,
				    NULL, NULL);
	}

	free_xid(xid);

	if (rc) {
		mnt = ERR_PTR(rc);
		goto free_root_path;
	}
	/*
	 * OK - we were able to get and cache a referral for @full_path.
	 *
	 * Now, pass it down to cifs_mount() and it will retry every available
	 * node server in case of failures - no need to do it here.
	 */
	mnt = cifs_dfs_do_mount(mntpt, cifs_sb, full_path);
	cifs_dbg(FYI, "%s: cifs_dfs_do_mount:%s , mnt:%p\n", __func__,
		 full_path + 1, mnt);

free_root_path:
	kfree(root_path);
free_full_path:
	kfree(full_path);
cdda_exit:
	cifs_dbg(FYI, "leaving %s\n" , __func__);
	return mnt;
}

/*
 * Attempt to automount the referral
 */
struct vfsmount *cifs_dfs_d_automount(struct path *path)
{
	struct vfsmount *newmnt;

	cifs_dbg(FYI, "in %s\n", __func__);

	newmnt = cifs_dfs_do_automount(path->dentry);
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
