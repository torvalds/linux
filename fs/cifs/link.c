/*
 *   fs/cifs/link.c
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2003
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/namei.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

int
cifs_hardlink(struct dentry *old_file, struct inode *inode,
	      struct dentry *direntry)
{
	int rc = -EACCES;
	int xid;
	char *fromName = NULL;
	char *toName = NULL;
	struct cifs_sb_info *cifs_sb_target;
	struct cifsTconInfo *pTcon;
	struct cifsInodeInfo *cifsInode;

	xid = GetXid();

	cifs_sb_target = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb_target->tcon;

/* No need to check for cross device links since server will do that
   BB note DFS case in future though (when we may have to check) */

	fromName = build_path_from_dentry(old_file);
	toName = build_path_from_dentry(direntry);
	if ((fromName == NULL) || (toName == NULL)) {
		rc = -ENOMEM;
		goto cifs_hl_exit;
	}

/*	if (cifs_sb_target->tcon->ses->capabilities & CAP_UNIX)*/
	if (pTcon->unix_ext)
		rc = CIFSUnixCreateHardLink(xid, pTcon, fromName, toName,
					    cifs_sb_target->local_nls,
					    cifs_sb_target->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
	else {
		rc = CIFSCreateHardLink(xid, pTcon, fromName, toName,
					cifs_sb_target->local_nls,
					cifs_sb_target->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		if ((rc == -EIO) || (rc == -EINVAL))
			rc = -EOPNOTSUPP;
	}

	d_drop(direntry);	/* force new lookup from server of target */

	/* if source file is cached (oplocked) revalidate will not go to server
	   until the file is closed or oplock broken so update nlinks locally */
	if (old_file->d_inode) {
		cifsInode = CIFS_I(old_file->d_inode);
		if (rc == 0) {
			old_file->d_inode->i_nlink++;
/* BB should we make this contingent on superblock flag NOATIME? */
/*			old_file->d_inode->i_ctime = CURRENT_TIME;*/
			/* parent dir timestamps will update from srv
			within a second, would it really be worth it
			to set the parent dir cifs inode time to zero
			to force revalidate (faster) for it too? */
		}
		/* if not oplocked will force revalidate to get info
		   on source file from srv */
		cifsInode->time = 0;

		/* Will update parent dir timestamps from srv within a second.
		   Would it really be worth it to set the parent dir (cifs
		   inode) time field to zero to force revalidate on parent
		   directory faster ie
			CIFS_I(inode)->time = 0;  */
	}

cifs_hl_exit:
	kfree(fromName);
	kfree(toName);
	FreeXid(xid);
	return rc;
}

void *
cifs_follow_link(struct dentry *direntry, struct nameidata *nd)
{
	struct inode *inode = direntry->d_inode;
	int rc = -EACCES;
	int xid;
	char *full_path = NULL;
	char *target_path = ERR_PTR(-ENOMEM);
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;

	xid = GetXid();

	full_path = build_path_from_dentry(direntry);

	if (!full_path)
		goto out_no_free;

	cFYI(1, ("Full path: %s inode = 0x%p", full_path, inode));
	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;
	target_path = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!target_path) {
		target_path = ERR_PTR(-ENOMEM);
		goto out;
	}

	/* We could change this to:
		if (pTcon->unix_ext)
	   but there does not seem any point in refusing to
	   get symlink info if we can, even if unix extensions
	   turned off for this mount */

	if (pTcon->ses->capabilities & CAP_UNIX)
		rc = CIFSSMBUnixQuerySymLink(xid, pTcon, full_path,
					     target_path,
					     PATH_MAX-1,
					     cifs_sb->local_nls);
	else {
		/* BB add read reparse point symlink code here */
		/* rc = CIFSSMBQueryReparseLinkInfo */
		/* BB Add code to Query ReparsePoint info */
		/* BB Add MAC style xsymlink check here if enabled */
	}

	if (rc == 0) {

/* BB Add special case check for Samba DFS symlinks */

		target_path[PATH_MAX-1] = 0;
	} else {
		kfree(target_path);
		target_path = ERR_PTR(rc);
	}

out:
	kfree(full_path);
out_no_free:
	FreeXid(xid);
	nd_set_link(nd, target_path);
	return NULL;	/* No cookie */
}

int
cifs_symlink(struct inode *inode, struct dentry *direntry, const char *symname)
{
	int rc = -EOPNOTSUPP;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct inode *newinode = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	full_path = build_path_from_dentry(direntry);

	if (full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}

	cFYI(1, ("Full path: %s", full_path));
	cFYI(1, ("symname is %s", symname));

	/* BB what if DFS and this volume is on different share? BB */
	if (pTcon->unix_ext)
		rc = CIFSUnixCreateSymLink(xid, pTcon, full_path, symname,
					   cifs_sb->local_nls);
	/* else
	   rc = CIFSCreateReparseSymLink(xid, pTcon, fromName, toName,
					cifs_sb_target->local_nls); */

	if (rc == 0) {
		if (pTcon->unix_ext)
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						      inode->i_sb, xid);
		else
			rc = cifs_get_inode_info(&newinode, full_path, NULL,
						 inode->i_sb, xid);

		if (rc != 0) {
			cFYI(1, ("Create symlink ok, getinodeinfo fail rc = %d",
			      rc));
		} else {
			if (pTcon->nocase)
				direntry->d_op = &cifs_ci_dentry_ops;
			else
				direntry->d_op = &cifs_dentry_ops;
			d_instantiate(direntry, newinode);
		}
	}

	kfree(full_path);
	FreeXid(xid);
	return rc;
}

int
cifs_readlink(struct dentry *direntry, char __user *pBuffer, int buflen)
{
	struct inode *inode = direntry->d_inode;
	int rc = -EACCES;
	int xid;
	int oplock = FALSE;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	char *tmp_path = NULL;
	char *tmpbuffer;
	unsigned char *referrals = NULL;
	unsigned int num_referrals = 0;
	int len;
	__u16 fid;

	xid = GetXid();
	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

/* BB would it be safe against deadlock to grab this sem
      even though rename itself grabs the sem and calls lookup? */
/*       mutex_lock(&inode->i_sb->s_vfs_rename_mutex);*/
	full_path = build_path_from_dentry(direntry);
/*       mutex_unlock(&inode->i_sb->s_vfs_rename_mutex);*/

	if (full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}

	cFYI(1,
	     ("Full path: %s inode = 0x%p pBuffer = 0x%p buflen = %d",
	      full_path, inode, pBuffer, buflen));
	if (buflen > PATH_MAX)
		len = PATH_MAX;
	else
		len = buflen;
	tmpbuffer = kmalloc(len, GFP_KERNEL);
	if (tmpbuffer == NULL) {
		kfree(full_path);
		FreeXid(xid);
		return -ENOMEM;
	}

/* BB add read reparse point symlink code and
	Unix extensions symlink code here BB */
/* We could disable this based on pTcon->unix_ext flag instead ... but why? */
	if (cifs_sb->tcon->ses->capabilities & CAP_UNIX)
		rc = CIFSSMBUnixQuerySymLink(xid, pTcon, full_path,
				tmpbuffer,
				len - 1,
				cifs_sb->local_nls);
	else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) {
		cERROR(1, ("SFU style symlinks not implemented yet"));
		/* add open and read as in fs/cifs/inode.c */
	} else {
		rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_OPEN, GENERIC_READ,
				OPEN_REPARSE_POINT, &fid, &oplock, NULL,
				cifs_sb->local_nls,
				cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (!rc) {
			rc = CIFSSMBQueryReparseLinkInfo(xid, pTcon, full_path,
				tmpbuffer,
				len - 1,
				fid,
				cifs_sb->local_nls);
			if (CIFSSMBClose(xid, pTcon, fid)) {
				cFYI(1, ("Error closing junction point "
					 "(open for ioctl)"));
			}
			if (rc == -EIO) {
				/* Query if DFS Junction */
				tmp_path =
					kmalloc(MAX_TREE_SIZE + MAX_PATHCONF + 1,
						GFP_KERNEL);
				if (tmp_path) {
					strncpy(tmp_path, pTcon->treeName,
						MAX_TREE_SIZE);
					strncat(tmp_path, full_path,
						MAX_PATHCONF);
					rc = get_dfs_path(xid, pTcon->ses,
						tmp_path,
						cifs_sb->local_nls,
						&num_referrals, &referrals,
						cifs_sb->mnt_cifs_flags &
						    CIFS_MOUNT_MAP_SPECIAL_CHR);
					cFYI(1, ("Get DFS for %s rc = %d ",
						tmp_path, rc));
					if ((num_referrals == 0) && (rc == 0))
						rc = -EACCES;
					else {
						cFYI(1, ("num referral: %d",
							num_referrals));
						if (referrals) {
							cFYI(1,("referral string: %s", referrals));
							strncpy(tmpbuffer,
								referrals,
								len-1);
						}
					}
					kfree(referrals);
					kfree(tmp_path);
}
				/* BB add code like else decode referrals
				then memcpy to tmpbuffer and free referrals
				string array BB */
			}
		}
	}
	/* BB Anything else to do to handle recursive links? */
	/* BB Should we be using page ops here? */

	/* BB null terminate returned string in pBuffer? BB */
	if (rc == 0) {
		rc = vfs_readlink(direntry, pBuffer, len, tmpbuffer);
		cFYI(1,
		     ("vfs_readlink called from cifs_readlink returned %d",
		      rc));
	}

	kfree(tmpbuffer);
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

void cifs_put_link(struct dentry *direntry, struct nameidata *nd, void *cookie)
{
	char *p = nd_get_link(nd);
	if (!IS_ERR(p))
		kfree(p);
}
