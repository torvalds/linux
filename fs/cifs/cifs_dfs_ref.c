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
#include <linux/vfs.h>
#include <linux/fs.h>
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
	cancel_delayed_work(&cifs_dfs_automount_task);
	flush_scheduled_work();
}

/**
 * cifs_get_share_name	-	extracts share name from UNC
 * @node_name:	pointer to UNC string
 *
 * Extracts sharename form full UNC.
 * i.e. strips from UNC trailing path that is not part of share
 * name and fixup missing '\' in the begining of DFS node refferal
 * if neccessary.
 * Returns pointer to share name on success or NULL on error.
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
		return NULL;

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
		cERROR(1, ("%s: no server name end in node name: %s",
			__func__, node_name));
		kfree(UNC);
		return NULL;
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
 * compose_mount_options	-	creates mount options for refferral
 * @sb_mountdata:	parent/root DFS mount options (template)
 * @ref_unc:		refferral server UNC
 * @devname:		pointer for saving device name
 *
 * creates mount options for submount based on template options sb_mountdata
 * and replacing unc,ip,prefixpath options with ones we've got form ref_unc.
 *
 * Returns: pointer to new mount options or ERR_PTR.
 * Caller is responcible for freeing retunrned value if it is not error.
 */
static char *compose_mount_options(const char *sb_mountdata,
				   const char *ref_unc,
				   char **devname)
{
	int rc;
	char *mountdata;
	int md_len;
	char *tkn_e;
	char *srvIP = NULL;
	char sep = ',';
	int off, noff;

	if (sb_mountdata == NULL)
		return ERR_PTR(-EINVAL);

	*devname = cifs_get_share_name(ref_unc);
	rc = dns_resolve_server_name_to_ip(*devname, &srvIP);
	if (rc != 0) {
		cERROR(1, ("%s: Failed to resolve server part of %s to IP",
			  __func__, *devname));
		mountdata = ERR_PTR(rc);
		goto compose_mount_options_out;
	}
	md_len = strlen(sb_mountdata) + strlen(srvIP) + strlen(ref_unc) + 3;
	mountdata = kzalloc(md_len+1, GFP_KERNEL);
	if (mountdata == NULL) {
		mountdata = ERR_PTR(-ENOMEM);
		goto compose_mount_options_out;
	}

	/* copy all options except of unc,ip,prefixpath */
	off = 0;
	if (strncmp(sb_mountdata, "sep=", 4) == 0) {
			sep = sb_mountdata[4];
			strncpy(mountdata, sb_mountdata, 5);
			off += 5;
	}
	while ((tkn_e = strchr(sb_mountdata+off, sep))) {
		noff = (tkn_e - (sb_mountdata+off)) + 1;
		if (strnicmp(sb_mountdata+off, "unc=", 4) == 0) {
			off += noff;
			continue;
		}
		if (strnicmp(sb_mountdata+off, "ip=", 3) == 0) {
			off += noff;
			continue;
		}
		if (strnicmp(sb_mountdata+off, "prefixpath=", 3) == 0) {
			off += noff;
			continue;
		}
		strncat(mountdata, sb_mountdata+off, noff);
		off += noff;
	}
	strcat(mountdata, sb_mountdata+off);
	mountdata[md_len] = '\0';

	/* copy new IP and ref share name */
	strcat(mountdata, ",ip=");
	strcat(mountdata, srvIP);
	strcat(mountdata, ",unc=");
	strcat(mountdata, *devname);

	/* find & copy prefixpath */
	tkn_e = strchr(ref_unc+2, '\\');
	if (tkn_e) {
		tkn_e = strchr(tkn_e+1, '\\');
		if (tkn_e) {
			strcat(mountdata, ",prefixpath=");
			strcat(mountdata, tkn_e+1);
		}
	}

	/*cFYI(1,("%s: parent mountdata: %s", __func__,sb_mountdata));*/
	/*cFYI(1, ("%s: submount mountdata: %s", __func__, mountdata ));*/

compose_mount_options_out:
	kfree(srvIP);
	return mountdata;
}


static struct vfsmount *cifs_dfs_do_refmount(const struct vfsmount *mnt_parent,
		struct dentry *dentry, char *ref_unc)
{
	struct cifs_sb_info *cifs_sb;
	struct vfsmount *mnt;
	char *mountdata;
	char *devname = NULL;

	cifs_sb = CIFS_SB(dentry->d_inode->i_sb);
	mountdata = compose_mount_options(cifs_sb->mountdata,
						ref_unc, &devname);

	if (IS_ERR(mountdata))
		return (struct vfsmount *)mountdata;

	mnt = vfs_kern_mount(&cifs_fs_type, 0, devname, mountdata);
	kfree(mountdata);
	kfree(devname);
	return mnt;

}

static char *build_full_dfs_path_from_dentry(struct dentry *dentry)
{
	char *full_path = NULL;
	char *search_path;
	char *tmp_path;
	size_t l_max_len;
	struct cifs_sb_info *cifs_sb;

	if (dentry->d_inode == NULL)
		return NULL;

	cifs_sb = CIFS_SB(dentry->d_inode->i_sb);

	if (cifs_sb->tcon == NULL)
		return NULL;

	search_path = build_path_from_dentry(dentry);
	if (search_path == NULL)
		return NULL;

	if (cifs_sb->tcon->Flags & SMB_SHARE_IS_IN_DFS) {
		int i;
		/* we should use full path name for correct working with DFS */
		l_max_len = strnlen(cifs_sb->tcon->treeName, MAX_TREE_SIZE+1) +
					strnlen(search_path, MAX_PATHCONF) + 1;
		tmp_path = kmalloc(l_max_len, GFP_KERNEL);
		if (tmp_path == NULL) {
			kfree(search_path);
			return NULL;
		}
		strncpy(tmp_path, cifs_sb->tcon->treeName, l_max_len);
		tmp_path[l_max_len-1] = 0;
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS)
			for (i = 0; i < l_max_len; i++) {
				if (tmp_path[i] == '\\')
					tmp_path[i] = '/';
			}
		strncat(tmp_path, search_path, l_max_len - strlen(tmp_path));

		full_path = tmp_path;
		kfree(search_path);
	} else {
		full_path = search_path;
	}
	return full_path;
}

static int add_mount_helper(struct vfsmount *newmnt, struct nameidata *nd,
				struct list_head *mntlist)
{
	/* stolen from afs code */
	int err;

	mntget(newmnt);
	err = do_add_mount(newmnt, nd, nd->path.mnt->mnt_flags, mntlist);
	switch (err) {
	case 0:
		path_put(&nd->path);
		nd->path.mnt = newmnt;
		nd->path.dentry = dget(newmnt->mnt_root);
		schedule_delayed_work(&cifs_dfs_automount_task,
				      cifs_dfs_mountpoint_expiry_timeout);
		break;
	case -EBUSY:
		/* someone else made a mount here whilst we were busy */
		while (d_mountpoint(nd->path.dentry) &&
		       follow_down(&nd->path.mnt, &nd->path.dentry))
			;
		err = 0;
	default:
		mntput(newmnt);
		break;
	}
	return err;
}

static void dump_referral(const struct dfs_info3_param *ref)
{
	cFYI(1, ("DFS: ref path: %s", ref->path_name));
	cFYI(1, ("DFS: node path: %s", ref->node_name));
	cFYI(1, ("DFS: fl: %hd, srv_type: %hd", ref->flags, ref->server_type));
	cFYI(1, ("DFS: ref_flags: %hd, path_consumed: %hd", ref->ref_flag,
				ref->path_consumed));
}


static void*
cifs_dfs_follow_mountpoint(struct dentry *dentry, struct nameidata *nd)
{
	struct dfs_info3_param *referrals = NULL;
	unsigned int num_referrals = 0;
	struct cifs_sb_info *cifs_sb;
	struct cifsSesInfo *ses;
	char *full_path = NULL;
	int xid, i;
	int rc = 0;
	struct vfsmount *mnt = ERR_PTR(-ENOENT);

	cFYI(1, ("in %s", __func__));
	BUG_ON(IS_ROOT(dentry));

	xid = GetXid();

	dput(nd->path.dentry);
	nd->path.dentry = dget(dentry);

	cifs_sb = CIFS_SB(dentry->d_inode->i_sb);
	ses = cifs_sb->tcon->ses;

	if (!ses) {
		rc = -EINVAL;
		goto out_err;
	}

	full_path = build_full_dfs_path_from_dentry(dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto out_err;
	}

	rc = get_dfs_path(xid, ses , full_path, cifs_sb->local_nls,
		&num_referrals, &referrals,
		cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);

	for (i = 0; i < num_referrals; i++) {
		dump_referral(referrals+i);
		/* connect to a storage node */
		if (referrals[i].flags & DFSREF_STORAGE_SERVER) {
			int len;
			len = strlen(referrals[i].node_name);
			if (len < 2) {
				cERROR(1, ("%s: Net Address path too short: %s",
					__func__, referrals[i].node_name));
				rc = -EINVAL;
				goto out_err;
			}
			mnt = cifs_dfs_do_refmount(nd->path.mnt,
						nd->path.dentry,
						referrals[i].node_name);
			cFYI(1, ("%s: cifs_dfs_do_refmount:%s , mnt:%p",
					 __func__,
					referrals[i].node_name, mnt));

			/* complete mount procedure if we accured submount */
			if (!IS_ERR(mnt))
				break;
		}
	}

	/* we need it cause for() above could exit without valid submount */
	rc = PTR_ERR(mnt);
	if (IS_ERR(mnt))
		goto out_err;

	nd->path.mnt->mnt_flags |= MNT_SHRINKABLE;
	rc = add_mount_helper(mnt, nd, &cifs_dfs_automount_list);

out:
	FreeXid(xid);
	free_dfs_info_array(referrals, num_referrals);
	kfree(full_path);
	cFYI(1, ("leaving %s" , __func__));
	return ERR_PTR(rc);
out_err:
	path_put(&nd->path);
	goto out;
}

struct inode_operations cifs_dfs_referral_inode_operations = {
	.follow_link = cifs_dfs_follow_mountpoint,
};

