/*
 *   Contains the CIFS DFS referral mounting routines used for handling
 *   traversal via DFS junction point
 *
 *   Copyright (c) 2007 Igor Mammedov
 *   Copyright (C) International Business Machines  Corp., 2008
 *   Author(s): Igor Mammedov (niallain@gmail.com)
 *		Steve French (sfrench@us.ibm.com)
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version
 *   2 of the License, or (at your option) any later version.
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
 * cifs_get_share_name	-	extracts share name from UNC
 * @node_name:	pointer to UNC string
 *
 * Extracts sharename form full UNC.
 * i.e. strips from UNC trailing path that is not part of share
 * name and fixup missing '\' in the beginning of DFS node refferal
 * if necessary.
 * Returns pointer to share name on success or ERR_PTR on error.
 * Caller is responsible for freeing returned string.
 */
static char *cifs_get_share_name(const char *node_name)
{
	int len;
	char *UNC;
	char *pSep;

	len = strlen(node_name);
	UNC = kmalloc(len+2 /*for term null and additional \ if it's missed */,
			 GFP_KERNEL);
	if (!UNC)
		return ERR_PTR(-ENOMEM);

	/* get share name and server name */
	if (node_name[1] != '\\') {
		UNC[0] = '\\';
		strncpy(UNC+1, node_name, len);
		len++;
		UNC[len] = 0;
	} else {
		strncpy(UNC, node_name, len);
		UNC[len] = 0;
	}

	/* find server name end */
	pSep = memchr(UNC+2, '\\', len-2);
	if (!pSep) {
		cERROR(1, "%s: no server name end in node name: %s",
			__func__, node_name);
		kfree(UNC);
		return ERR_PTR(-EINVAL);
	}

	/* find sharename end */
	pSep++;
	pSep = memchr(UNC+(pSep-UNC), '\\', len-(pSep-UNC));
	if (pSep) {
		/* trim path up to sharename end
		 * now we have share name in UNC */
		*pSep = 0;
	}

	return UNC;
}


/**
 * cifs_compose_mount_options	-	creates mount options for refferral
 * @sb_mountdata:	parent/root DFS mount options (template)
 * @fullpath:		full path in UNC format
 * @ref:		server's referral
 * @devname:		pointer for saving device name
 *
 * creates mount options for submount based on template options sb_mountdata
 * and replacing unc,ip,prefixpath options with ones we've got form ref_unc.
 *
 * Returns: pointer to new mount options or ERR_PTR.
 * Caller is responcible for freeing retunrned value if it is not error.
 */
char *cifs_compose_mount_options(const char *sb_mountdata,
				   const char *fullpath,
				   const struct dfs_info3_param *ref,
				   char **devname)
{
	int rc;
	char *mountdata = NULL;
	int md_len;
	char *tkn_e;
	char *srvIP = NULL;
	char sep = ',';
	int off, noff;

	if (sb_mountdata == NULL)
		return ERR_PTR(-EINVAL);

	*devname = cifs_get_share_name(ref->node_name);
	if (IS_ERR(*devname)) {
		rc = PTR_ERR(*devname);
		*devname = NULL;
		goto compose_mount_options_err;
	}

	rc = dns_resolve_server_name_to_ip(*devname, &srvIP);
	if (rc < 0) {
		cERROR(1, "%s: Failed to resolve server part of %s to IP: %d",
			  __func__, *devname, rc);
		goto compose_mount_options_err;
	}
	/* md_len = strlen(...) + 12 for 'sep+prefixpath='
	 * assuming that we have 'unc=' and 'ip=' in
	 * the original sb_mountdata
	 */
	md_len = strlen(sb_mountdata) + rc + strlen(ref->node_name) + 12 +
			INET6_ADDRSTRLEN;
	mountdata = kzalloc(md_len+1, GFP_KERNEL);
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

		if (strnicmp(sb_mountdata + off, "unc=", 4) == 0) {
			off += noff;
			continue;
		}
		if (strnicmp(sb_mountdata + off, "ip=", 3) == 0) {
			off += noff;
			continue;
		}
		if (strnicmp(sb_mountdata + off, "prefixpath=", 11) == 0) {
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
	strncat(mountdata, &sep, 1);
	strcat(mountdata, "unc=");
	strcat(mountdata, *devname);

	/* find & copy prefixpath */
	tkn_e = strchr(ref->node_name + 2, '\\');
	if (tkn_e == NULL) {
		/* invalid unc, missing share name*/
		rc = -EINVAL;
		goto compose_mount_options_err;
	}

	tkn_e = strchr(tkn_e + 1, '\\');
	if (tkn_e || (strlen(fullpath) - ref->path_consumed)) {
		strncat(mountdata, &sep, 1);
		strcat(mountdata, "prefixpath=");
		if (tkn_e)
			strcat(mountdata, tkn_e + 1);
		strcat(mountdata, fullpath + ref->path_consumed);
	}

	/*cFYI(1, "%s: parent mountdata: %s", __func__,sb_mountdata);*/
	/*cFYI(1, "%s: submount mountdata: %s", __func__, mountdata );*/

compose_mount_options_out:
	kfree(srvIP);
	return mountdata;

compose_mount_options_err:
	kfree(mountdata);
	mountdata = ERR_PTR(rc);
	kfree(*devname);
	*devname = NULL;
	goto compose_mount_options_out;
}

/**
 * cifs_dfs_do_refmount - mounts specified path using provided refferal
 * @cifs_sb:		parent/root superblock
 * @fullpath:		full path in UNC format
 * @ref:		server's referral
 */
static struct vfsmount *cifs_dfs_do_refmount(struct cifs_sb_info *cifs_sb,
		const char *fullpath, const struct dfs_info3_param *ref)
{
	struct vfsmount *mnt;
	char *mountdata;
	char *devname = NULL;

	/* strip first '\' from fullpath */
	mountdata = cifs_compose_mount_options(cifs_sb->mountdata,
			fullpath + 1, ref, &devname);

	if (IS_ERR(mountdata))
		return (struct vfsmount *)mountdata;

	mnt = vfs_kern_mount(&cifs_fs_type, 0, devname, mountdata);
	kfree(mountdata);
	kfree(devname);
	return mnt;

}

static void dump_referral(const struct dfs_info3_param *ref)
{
	cFYI(1, "DFS: ref path: %s", ref->path_name);
	cFYI(1, "DFS: node path: %s", ref->node_name);
	cFYI(1, "DFS: fl: %hd, srv_type: %hd", ref->flags, ref->server_type);
	cFYI(1, "DFS: ref_flags: %hd, path_consumed: %hd", ref->ref_flag,
				ref->path_consumed);
}

/*
 * Create a vfsmount that we can automount
 */
static struct vfsmount *cifs_dfs_do_automount(struct dentry *mntpt)
{
	struct dfs_info3_param *referrals = NULL;
	unsigned int num_referrals = 0;
	struct cifs_sb_info *cifs_sb;
	struct cifs_ses *ses;
	char *full_path;
	int xid, i;
	int rc;
	struct vfsmount *mnt;
	struct tcon_link *tlink;

	cFYI(1, "in %s", __func__);
	BUG_ON(IS_ROOT(mntpt));

	/*
	 * The MSDFS spec states that paths in DFS referral requests and
	 * responses must be prefixed by a single '\' character instead of
	 * the double backslashes usually used in the UNC. This function
	 * gives us the latter, so we must adjust the result.
	 */
	mnt = ERR_PTR(-ENOMEM);
	full_path = build_path_from_dentry(mntpt);
	if (full_path == NULL)
		goto cdda_exit;

	cifs_sb = CIFS_SB(mntpt->d_inode->i_sb);
	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		mnt = ERR_CAST(tlink);
		goto free_full_path;
	}
	ses = tlink_tcon(tlink)->ses;

	xid = GetXid();
	rc = get_dfs_path(xid, ses, full_path + 1, cifs_sb->local_nls,
		&num_referrals, &referrals,
		cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	FreeXid(xid);

	cifs_put_tlink(tlink);

	mnt = ERR_PTR(-ENOENT);
	for (i = 0; i < num_referrals; i++) {
		int len;
		dump_referral(referrals + i);
		/* connect to a node */
		len = strlen(referrals[i].node_name);
		if (len < 2) {
			cERROR(1, "%s: Net Address path too short: %s",
					__func__, referrals[i].node_name);
			mnt = ERR_PTR(-EINVAL);
			break;
		}
		mnt = cifs_dfs_do_refmount(cifs_sb,
				full_path, referrals + i);
		cFYI(1, "%s: cifs_dfs_do_refmount:%s , mnt:%p", __func__,
					referrals[i].node_name, mnt);
		if (!IS_ERR(mnt))
			goto success;
	}

	/* no valid submounts were found; return error from get_dfs_path() by
	 * preference */
	if (rc != 0)
		mnt = ERR_PTR(rc);

success:
	free_dfs_info_array(referrals, num_referrals);
free_full_path:
	kfree(full_path);
cdda_exit:
	cFYI(1, "leaving %s" , __func__);
	return mnt;
}

/*
 * Attempt to automount the referral
 */
struct vfsmount *cifs_dfs_d_automount(struct path *path)
{
	struct vfsmount *newmnt;

	cFYI(1, "in %s", __func__);

	newmnt = cifs_dfs_do_automount(path->dentry);
	if (IS_ERR(newmnt)) {
		cFYI(1, "leaving %s [automount failed]" , __func__);
		return newmnt;
	}

	mntget(newmnt); /* prevent immediate expiration */
	mnt_set_expiry(newmnt, &cifs_dfs_automount_list);
	schedule_delayed_work(&cifs_dfs_automount_task,
			      cifs_dfs_mountpoint_expiry_timeout);
	cFYI(1, "leaving %s [ok]" , __func__);
	return newmnt;
}

const struct inode_operations cifs_dfs_referral_inode_operations = {
};
