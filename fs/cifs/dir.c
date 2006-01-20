/*
 *   fs/cifs/dir.c
 *
 *   vfs operations that deal with dentries
 * 
 *   Copyright (C) International Business Machines  Corp., 2002,2005
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
	char dirsep = CIFS_DIR_SEP(CIFS_SB(direntry->d_sb));

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
			full_path[namelen] = dirsep;
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

/* char * build_wildcard_path_from_dentry(struct dentry *direntry)
{
	if(full_path == NULL)
		return full_path;

	full_path[namelen] = '\\';
	full_path[namelen+1] = '*';
	full_path[namelen+2] = 0;
BB remove above eight lines BB */

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

	if(nd && (nd->flags & LOOKUP_OPEN)) {
		int oflags = nd->intent.open.flags;

		desiredAccess = 0;
		if (oflags & FMODE_READ)
			desiredAccess |= GENERIC_READ;
		if (oflags & FMODE_WRITE) {
			desiredAccess |= GENERIC_WRITE;
			if (!(oflags & FMODE_READ))
				write_only = TRUE;
		}

		if((oflags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
			disposition = FILE_CREATE;
		else if((oflags & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC))
			disposition = FILE_OVERWRITE_IF;
		else if((oflags & O_CREAT) == O_CREAT)
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
			 &fileHandle, &oplock, buf, cifs_sb->local_nls,
			 cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	if(rc == -EIO) {
		/* old server, retry the open legacy style */
		rc = SMBLegacyOpen(xid, pTcon, full_path, disposition,
			desiredAccess, CREATE_NOT_DIR,
			&fileHandle, &oplock, buf, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	} 
	if (rc) {
		cFYI(1, ("cifs_create returned 0x%x ", rc));
	} else {
		/* If Open reported that we actually created a file
		then we now have to set the mode if possible */
		if ((cifs_sb->tcon->ses->capabilities & CAP_UNIX) &&
			(oplock & CIFS_CREATE_ACTION))
			if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
				CIFSSMBUnixSetPerms(xid, pTcon, full_path, mode,
					(__u64)current->fsuid,
					(__u64)current->fsgid,
					0 /* dev */,
					cifs_sb->local_nls, 
					cifs_sb->mnt_cifs_flags & 
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			} else {
				CIFSSMBUnixSetPerms(xid, pTcon, full_path, mode,
					(__u64)-1,
					(__u64)-1,
					0 /* dev */,
					cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags & 
						CIFS_MOUNT_MAP_SPECIAL_CHR);
			}
		else {
			/* BB implement mode setting via Windows security descriptors */
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
			if(newinode) {
				newinode->i_mode = mode;
				if((oplock & CIFS_CREATE_ACTION) &&
				  (cifs_sb->mnt_cifs_flags & 
				     CIFS_MOUNT_SET_UID)) {
					newinode->i_uid = current->fsuid;
					newinode->i_gid = current->fsgid;
				}
			}
		}

		if (rc != 0) {
			cFYI(1,
			     ("Create worked but get_inode_info failed rc = %d",
			      rc));
		} else {
			if (pTcon->nocase)
				direntry->d_op = &cifs_ci_dentry_ops;
			else
				direntry->d_op = &cifs_dentry_ops;
			d_instantiate(direntry, newinode);
		}
		if((nd->flags & LOOKUP_OPEN) == FALSE) {
			/* mknod case - do not leave file open */
			CIFSSMBClose(xid, pTcon, fileHandle);
		} else if(newinode) {
			pCifsFile =
			   kmalloc(sizeof (struct cifsFileInfo), GFP_KERNEL);
			
			if(pCifsFile == NULL)
				goto cifs_create_out;
			memset((char *)pCifsFile, 0,
			       sizeof (struct cifsFileInfo));
			pCifsFile->netfid = fileHandle;
			pCifsFile->pid = current->tgid;
			pCifsFile->pInode = newinode;
			pCifsFile->invalidHandle = FALSE;
			pCifsFile->closePend     = FALSE;
			init_MUTEX(&pCifsFile->fh_sem);
			/* set the following in open now 
				pCifsFile->pfile = file; */
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
					cFYI(1,("Exclusive Oplock for inode %p",
						newinode));
				} else if((oplock & 0xF) == OPLOCK_READ)
					pCifsInode->clientCanCacheRead = TRUE;
			}
			write_unlock(&GlobalSMBSeslock);
		}
	} 
cifs_create_out:
	kfree(buf);
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

int cifs_mknod(struct inode *inode, struct dentry *direntry, int mode, 
		dev_t device_number) 
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
	else if (pTcon->ses->capabilities & CAP_UNIX) {
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
			rc = CIFSSMBUnixSetPerms(xid, pTcon, full_path,
				mode,(__u64)current->fsuid,(__u64)current->fsgid,
				device_number, cifs_sb->local_nls,
				cifs_sb->mnt_cifs_flags & 
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		} else {
			rc = CIFSSMBUnixSetPerms(xid, pTcon,
				full_path, mode, (__u64)-1, (__u64)-1,
				device_number, cifs_sb->local_nls,
				cifs_sb->mnt_cifs_flags & 
					CIFS_MOUNT_MAP_SPECIAL_CHR);
		}

		if(!rc) {
			rc = cifs_get_inode_info_unix(&newinode, full_path,
						inode->i_sb,xid);
			if (pTcon->nocase)
				direntry->d_op = &cifs_ci_dentry_ops;
			else
				direntry->d_op = &cifs_dentry_ops;
			if(rc == 0)
				d_instantiate(direntry, newinode);
		}
	} else {
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) {
			int oplock = 0;
			u16 fileHandle;
			FILE_ALL_INFO * buf;

			cFYI(1,("sfu compat create special file"));

			buf = kmalloc(sizeof(FILE_ALL_INFO),GFP_KERNEL);
			if(buf == NULL) {
				kfree(full_path);
				FreeXid(xid);
				return -ENOMEM;
			}

			rc = CIFSSMBOpen(xid, pTcon, full_path,
					 FILE_CREATE, /* fail if exists */
					 GENERIC_WRITE /* BB would 
					  WRITE_OWNER | WRITE_DAC be better? */,
					 /* Create a file and set the
					    file attribute to SYSTEM */
					 CREATE_NOT_DIR | CREATE_OPTION_SPECIAL,
					 &fileHandle, &oplock, buf,
					 cifs_sb->local_nls,
					 cifs_sb->mnt_cifs_flags & 
					    CIFS_MOUNT_MAP_SPECIAL_CHR);

			if(!rc) {
				/* BB Do not bother to decode buf since no
				   local inode yet to put timestamps in,
				   but we can reuse it safely */
				int bytes_written;
				struct win_dev *pdev;
				pdev = (struct win_dev *)buf;
				if(S_ISCHR(mode)) {
					memcpy(pdev->type, "IntxCHR", 8);
					pdev->major =
					      cpu_to_le64(MAJOR(device_number));
					pdev->minor = 
					      cpu_to_le64(MINOR(device_number));
					rc = CIFSSMBWrite(xid, pTcon,
						fileHandle,
						sizeof(struct win_dev),
						0, &bytes_written, (char *)pdev,
						NULL, 0);
				} else if(S_ISBLK(mode)) {
					memcpy(pdev->type, "IntxBLK", 8);
					pdev->major =
					      cpu_to_le64(MAJOR(device_number));
					pdev->minor =
					      cpu_to_le64(MINOR(device_number));
					rc = CIFSSMBWrite(xid, pTcon,
						fileHandle,
						sizeof(struct win_dev),
						0, &bytes_written, (char *)pdev,
						NULL, 0);
				} /* else if(S_ISFIFO */
				CIFSSMBClose(xid, pTcon, fileHandle);
				d_drop(direntry);
			}
			kfree(buf);
			/* add code here to set EAs */
		}
	}

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
		if (pTcon->nocase)
			direntry->d_op = &cifs_ci_dentry_ops;
		else
			direntry->d_op = &cifs_dentry_ops;
		d_add(direntry, newInode);

		/* since paths are not looked up by component - the parent 
		   directories are presumed to be good here */
		renew_parental_timestamps(direntry);

	} else if (rc == -ENOENT) {
		rc = 0;
		direntry->d_time = jiffies;
		if (pTcon->nocase)
			direntry->d_op = &cifs_ci_dentry_ops;
		else
			direntry->d_op = &cifs_dentry_ops;
		d_add(direntry, NULL);
	/*	if it was once a directory (but how can we tell?) we could do  
			shrink_dcache_parent(direntry); */
	} else {
		cERROR(1,("Error 0x%x on cifs_get_inode_info in lookup of %s",
			   rc,full_path));
		/* BB special case check for Access Denied - watch security 
		exposure of returning dir info implicitly via different rc 
		if file exists or not but no access BB */
	}

	kfree(full_path);
	FreeXid(xid);
	return ERR_PTR(rc);
}

static int
cifs_d_revalidate(struct dentry *direntry, struct nameidata *nd)
{
	int isValid = 1;

	if (direntry->d_inode) {
		if (cifs_revalidate(direntry)) {
			return 0;
		}
	} else {
		cFYI(1, ("neg dentry 0x%p name = %s",
			 direntry, direntry->d_name.name));
		if(time_after(jiffies, direntry->d_time + HZ) || 
			!lookupCacheEnabled) {
			d_drop(direntry);
			isValid = 0;
		} 
	}

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

static int cifs_ci_hash(struct dentry *dentry, struct qstr *q)
{
	struct nls_table *codepage = CIFS_SB(dentry->d_inode->i_sb)->local_nls;
	unsigned long hash;
	int i;

	hash = init_name_hash();
	for (i = 0; i < q->len; i++)
		hash = partial_name_hash(nls_tolower(codepage, q->name[i]),
					 hash);
	q->hash = end_name_hash(hash);

	return 0;
}

static int cifs_ci_compare(struct dentry *dentry, struct qstr *a,
			   struct qstr *b)
{
	struct nls_table *codepage = CIFS_SB(dentry->d_inode->i_sb)->local_nls;

	if ((a->len == b->len) &&
	    (nls_strnicmp(codepage, a->name, b->name, a->len) == 0)) {
		/*
		 * To preserve case, don't let an existing negative dentry's
		 * case take precedence.  If a is not a negative dentry, this
		 * should have no side effects
		 */
		memcpy((unsigned char *)a->name, b->name, a->len);
		return 0;
	}
	return 1;
}

struct dentry_operations cifs_ci_dentry_ops = {
	.d_revalidate = cifs_d_revalidate,
	.d_hash = cifs_ci_hash,
	.d_compare = cifs_ci_compare,
};
