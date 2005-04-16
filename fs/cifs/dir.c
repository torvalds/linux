/*
 *   fs/cifs/dir.c
 *
 *   vfs operations that deal with dentries
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
#include <linux/slab.h>
#include <linux/namei.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

void
renew_parental_timestamps(struct dentry *direntry)
{
	/* BB check if there is a way to get the kernel to do this or if we really need this */
	do {
		direntry->d_time = jiffies;
		direntry = direntry->d_parent;
	} while (!IS_ROOT(direntry));	
}

/* Note: caller must free return buffer */
char *
build_path_from_dentry(struct dentry *direntry)
{
	struct dentry *temp;
	int namelen = 0;
	char *full_path;

	if(direntry == NULL)
		return NULL;  /* not much we can do if dentry is freed and
		we need to reopen the file after it was closed implicitly
		when the server crashed */

cifs_bp_rename_retry:
	for (temp = direntry; !IS_ROOT(temp);) {
		namelen += (1 + temp->d_name.len);
		temp = temp->d_parent;
		if(temp == NULL) {
			cERROR(1,("corrupt dentry"));
			return NULL;
		}
	}

	full_path = kmalloc(namelen+1, GFP_KERNEL);
	if(full_path == NULL)
		return full_path;
	full_path[namelen] = 0;	/* trailing null */

	for (temp = direntry; !IS_ROOT(temp);) {
		namelen -= 1 + temp->d_name.len;
		if (namelen < 0) {
			break;
		} else {
			full_path[namelen] = '\\';
			strncpy(full_path + namelen + 1, temp->d_name.name,
				temp->d_name.len);
			cFYI(0, (" name: %s ", full_path + namelen));
		}
		temp = temp->d_parent;
		if(temp == NULL) {
			cERROR(1,("corrupt dentry"));
			kfree(full_path);
			return NULL;
		}
	}
	if (namelen != 0) {
		cERROR(1,
		       ("We did not end path lookup where we expected namelen is %d",
			namelen));
		/* presumably this is only possible if we were racing with a rename 
		of one of the parent directories  (we can not lock the dentries
		above us to prevent this, but retrying should be harmless) */
		kfree(full_path);
		namelen = 0;
		goto cifs_bp_rename_retry;
	}

	return full_path;
}

/* Note: caller must free return buffer */
char *
build_wildcard_path_from_dentry(struct dentry *direntry)
{
	struct dentry *temp;
	int namelen = 0;
	char *full_path;

	if(direntry == NULL)
		return NULL;  /* not much we can do if dentry is freed and
		we need to reopen the file after it was closed implicitly
		when the server crashed */

cifs_bwp_rename_retry:
	for (temp = direntry; !IS_ROOT(temp);) {
		namelen += (1 + temp->d_name.len);
		temp = temp->d_parent;
		if(temp == NULL) {
			cERROR(1,("corrupt dentry"));
			return NULL;
		}
	}

	full_path = kmalloc(namelen+3, GFP_KERNEL);
	if(full_path == NULL)
		return full_path;

	full_path[namelen] = '\\';
	full_path[namelen+1] = '*';
	full_path[namelen+2] = 0;  /* trailing null */

	for (temp = direntry; !IS_ROOT(temp);) {
		namelen -= 1 + temp->d_name.len;
		if (namelen < 0) {
			break;
		} else {
			full_path[namelen] = '\\';
			strncpy(full_path + namelen + 1, temp->d_name.name,
				temp->d_name.len);
			cFYI(0, (" name: %s ", full_path + namelen));
		}
		temp = temp->d_parent;
		if(temp == NULL) {
			cERROR(1,("corrupt dentry"));
			kfree(full_path);
			return NULL;
		}
	}
	if (namelen != 0) {
		cERROR(1,
		       ("We did not end path lookup where we expected namelen is %d",
			namelen));
		/* presumably this is only possible if we were racing with a rename 
		of one of the parent directories  (we can not lock the dentries
		above us to prevent this, but retrying should be harmless) */
		kfree(full_path);
		namelen = 0;
		goto cifs_bwp_rename_retry;
	}

	return full_path;
}

/* Inode operations in similar order to how they appear in the Linux file fs.h */

int
cifs_create(struct inode *inode, struct dentry *direntry, int mode,
		struct nameidata *nd)
{
	int rc = -ENOENT;
	int xid;
	int oplock = 0;
	int desiredAccess = GENERIC_READ | GENERIC_WRITE;
	__u16 fileHandle;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	FILE_ALL_INFO * buf = NULL;
	struct inode *newinode = NULL;
	struct cifsFileInfo * pCifsFile = NULL;
	struct cifsInodeInfo * pCifsInode;
	int disposition = FILE_OVERWRITE_IF;
	int write_only = FALSE;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	down(&direntry->d_sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(direntry);
	up(&direntry->d_sb->s_vfs_rename_sem);
	if(full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}

	if(nd) {
		if ((nd->intent.open.flags & O_ACCMODE) == O_RDONLY)
			desiredAccess = GENERIC_READ;
		else if ((nd->intent.open.flags & O_ACCMODE) == O_WRONLY) {
			desiredAccess = GENERIC_WRITE;
			write_only = TRUE;
		} else if ((nd->intent.open.flags & O_ACCMODE) == O_RDWR) {
			/* GENERIC_ALL is too much permission to request */
			/* can cause unnecessary access denied on create */
			/* desiredAccess = GENERIC_ALL; */
			desiredAccess = GENERIC_READ | GENERIC_WRITE;
		}

		if((nd->intent.open.flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
			disposition = FILE_CREATE;
		else if((nd->intent.open.flags & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC))
			disposition = FILE_OVERWRITE_IF;
		else if((nd->intent.open.flags & O_CREAT) == O_CREAT)
			disposition = FILE_OPEN_IF;
		else {
			cFYI(1,("Create flag not set in create function"));
		}
	}

	/* BB add processing to set equivalent of mode - e.g. via CreateX with ACLs */
	if (oplockEnabled)
		oplock = REQ_OPLOCK;

	buf = kmalloc(sizeof(FILE_ALL_INFO),GFP_KERNEL);
	if(buf == NULL) {
		kfree(full_path);
		FreeXid(xid);
		return -ENOMEM;
	}

	rc = CIFSSMBOpen(xid, pTcon, full_path, disposition,
			 desiredAccess, CREATE_NOT_DIR,
			 &fileHandle, &oplock, buf, cifs_sb->local_nls);
	if (rc) {
		cFYI(1, ("cifs_create returned 0x%x ", rc));
	} else {
		/* If Open reported that we actually created a file
		then we now have to set the mode if possible */
		if ((cifs_sb->tcon->ses->capabilities & CAP_UNIX) &&
			(oplock & CIFS_CREATE_ACTION))
			if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
				CIFSSMBUnixSetPerms(xid, pTcon, full_path, mode,
					(__u64)current->euid,
					(__u64)current->egid,
					0 /* dev */,
					cifs_sb->local_nls);
			} else {
				CIFSSMBUnixSetPerms(xid, pTcon, full_path, mode,
					(__u64)-1,
					(__u64)-1,
					0 /* dev */,
					cifs_sb->local_nls);
			}
		else {
			/* BB implement via Windows security descriptors */
			/* eg CIFSSMBWinSetPerms(xid,pTcon,full_path,mode,-1,-1,local_nls);*/
			/* could set r/o dos attribute if mode & 0222 == 0 */
		}

	/* BB server might mask mode so we have to query for Unix case*/
		if (pTcon->ses->capabilities & CAP_UNIX)
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						 inode->i_sb,xid);
		else {
			rc = cifs_get_inode_info(&newinode, full_path,
						 buf, inode->i_sb,xid);
			if(newinode)
				newinode->i_mode = mode;
		}

		if (rc != 0) {
			cFYI(1,("Create worked but get_inode_info failed with rc = %d",
			      rc));
		} else {
			direntry->d_op = &cifs_dentry_ops;
			d_instantiate(direntry, newinode);
		}
		if((nd->flags & LOOKUP_OPEN) == FALSE) {
			/* mknod case - do not leave file open */
			CIFSSMBClose(xid, pTcon, fileHandle);
		} else if(newinode) {
			pCifsFile = (struct cifsFileInfo *)
			   kmalloc(sizeof (struct cifsFileInfo), GFP_KERNEL);
		
			if (pCifsFile) {
				memset((char *)pCifsFile, 0,
				       sizeof (struct cifsFileInfo));
				pCifsFile->netfid = fileHandle;
				pCifsFile->pid = current->tgid;
				pCifsFile->pInode = newinode;
				pCifsFile->invalidHandle = FALSE;
				pCifsFile->closePend     = FALSE;
				init_MUTEX(&pCifsFile->fh_sem);
				/* put the following in at open now */
				/* pCifsFile->pfile = file; */ 
				write_lock(&GlobalSMBSeslock);
				list_add(&pCifsFile->tlist,&pTcon->openFileList);
				pCifsInode = CIFS_I(newinode);
				if(pCifsInode) {
				/* if readable file instance put first in list*/
					if (write_only == TRUE) {
                                        	list_add_tail(&pCifsFile->flist,
							&pCifsInode->openFileList);
					} else {
						list_add(&pCifsFile->flist,
							&pCifsInode->openFileList);
					}
					if((oplock & 0xF) == OPLOCK_EXCLUSIVE) {
						pCifsInode->clientCanCacheAll = TRUE;
						pCifsInode->clientCanCacheRead = TRUE;
						cFYI(1,("Exclusive Oplock granted on inode %p",
							newinode));
					} else if((oplock & 0xF) == OPLOCK_READ)
						pCifsInode->clientCanCacheRead = TRUE;
				}
				write_unlock(&GlobalSMBSeslock);
			}
		}
	} 

	if (buf)
	    kfree(buf);
	if (full_path)
	    kfree(full_path);
	FreeXid(xid);

	return rc;
}

int cifs_mknod(struct inode *inode, struct dentry *direntry, int mode, dev_t device_number) 
{
	int rc = -EPERM;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;
	struct inode * newinode = NULL;

	if (!old_valid_dev(device_number))
		return -EINVAL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	down(&direntry->d_sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(direntry);
	up(&direntry->d_sb->s_vfs_rename_sem);
	if(full_path == NULL)
		rc = -ENOMEM;
	
	if (full_path && (pTcon->ses->capabilities & CAP_UNIX)) {
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
			rc = CIFSSMBUnixSetPerms(xid, pTcon, full_path,
				mode,(__u64)current->euid,(__u64)current->egid,
				device_number, cifs_sb->local_nls);
		} else {
			rc = CIFSSMBUnixSetPerms(xid, pTcon,
				full_path, mode, (__u64)-1, (__u64)-1,
				device_number, cifs_sb->local_nls);
		}

		if(!rc) {
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						inode->i_sb,xid);
			direntry->d_op = &cifs_dentry_ops;
			if(rc == 0)
				d_instantiate(direntry, newinode);
		}
	}

	if (full_path)
		kfree(full_path);
	FreeXid(xid);

	return rc;
}


struct dentry *
cifs_lookup(struct inode *parent_dir_inode, struct dentry *direntry, struct nameidata *nd)
{
	int xid;
	int rc = 0; /* to get around spurious gcc warning, set to zero here */
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct inode *newInode = NULL;
	char *full_path = NULL;

	xid = GetXid();

	cFYI(1,
	     (" parent inode = 0x%p name is: %s and dentry = 0x%p",
	      parent_dir_inode, direntry->d_name.name, direntry));

	/* BB Add check of incoming data - e.g. frame not longer than maximum SMB - let server check the namelen BB */

	/* check whether path exists */

	cifs_sb = CIFS_SB(parent_dir_inode->i_sb);
	pTcon = cifs_sb->tcon;

	/* can not grab the rename sem here since it would
	deadlock in the cases (beginning of sys_rename itself)
	in which we already have the sb rename sem */
	full_path = build_path_from_dentry(direntry);
	if(full_path == NULL) {
		FreeXid(xid);
		return ERR_PTR(-ENOMEM);
	}

	if (direntry->d_inode != NULL) {
		cFYI(1, (" non-NULL inode in lookup"));
	} else {
		cFYI(1, (" NULL inode in lookup"));
	}
	cFYI(1,
	     (" Full path: %s inode = 0x%p", full_path, direntry->d_inode));

	if (pTcon->ses->capabilities & CAP_UNIX)
		rc = cifs_get_inode_info_unix(&newInode, full_path,
					      parent_dir_inode->i_sb,xid);
	else
		rc = cifs_get_inode_info(&newInode, full_path, NULL,
					 parent_dir_inode->i_sb,xid);

	if ((rc == 0) && (newInode != NULL)) {
		direntry->d_op = &cifs_dentry_ops;
		d_add(direntry, newInode);

		/* since paths are not looked up by component - the parent directories are presumed to be good here */
		renew_parental_timestamps(direntry);

	} else if (rc == -ENOENT) {
		rc = 0;
		d_add(direntry, NULL);
	} else {
		cERROR(1,("Error 0x%x or on cifs_get_inode_info in lookup",rc));
		/* BB special case check for Access Denied - watch security 
		exposure of returning dir info implicitly via different rc 
		if file exists or not but no access BB */
	}

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return ERR_PTR(rc);
}

int
cifs_dir_open(struct inode *inode, struct file *file)
{				/* NB: currently unused since searches are opened in readdir */
	int rc = 0;
	int xid;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	char *full_path = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	if(file->f_dentry) {
		down(&file->f_dentry->d_sb->s_vfs_rename_sem);
		full_path = build_wildcard_path_from_dentry(file->f_dentry);
		up(&file->f_dentry->d_sb->s_vfs_rename_sem);
	} else {
		FreeXid(xid);
		return -EIO;
	}

	cFYI(1, ("inode = 0x%p and full path is %s", inode, full_path));

	if (full_path)
		kfree(full_path);
	FreeXid(xid);
	return rc;
}

static int
cifs_d_revalidate(struct dentry *direntry, struct nameidata *nd)
{
	int isValid = 1;

/*	lock_kernel(); *//* surely we do not want to lock the kernel for a whole network round trip which could take seconds */

	if (direntry->d_inode) {
		if (cifs_revalidate(direntry)) {
			/* unlock_kernel(); */
			return 0;
		}
	} else {
		cFYI(1,
		     ("In cifs_d_revalidate with no inode but name = %s and dentry 0x%p",
		      direntry->d_name.name, direntry));
	}

/*    unlock_kernel(); */

	return isValid;
}

/* static int cifs_d_delete(struct dentry *direntry)
{
	int rc = 0;

	cFYI(1, ("In cifs d_delete, name = %s", direntry->d_name.name));

	return rc;
}     */

struct dentry_operations cifs_dentry_ops = {
	.d_revalidate = cifs_d_revalidate,
/* d_delete:       cifs_d_delete,       *//* not needed except for debugging */
	/* no need for d_hash, d_compare, d_release, d_iput ... yet. BB confirm this BB */
};
