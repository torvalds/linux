/*
 *   fs/cifs/file.c
 *
 *   vfs operations that deal with files
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *              Jeremy Allison (jra@samba.org)
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
#include <linux/backing-dev.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/delay.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

static inline struct cifsFileInfo *cifs_init_private(
	struct cifsFileInfo *private_data, struct inode *inode,
	struct file *file, __u16 netfid)
{
	memset(private_data, 0, sizeof(struct cifsFileInfo));
	private_data->netfid = netfid;
	private_data->pid = current->tgid;
	mutex_init(&private_data->fh_mutex);
	mutex_init(&private_data->lock_mutex);
	INIT_LIST_HEAD(&private_data->llist);
	private_data->pfile = file; /* needed for writepage */
	private_data->pInode = inode;
	private_data->invalidHandle = false;
	private_data->closePend = false;
	/* we have to track num writers to the inode, since writepages
	does not tell us which handle the write is for so there can
	be a close (overlapping with write) of the filehandle that
	cifs_writepages chose to use */
	atomic_set(&private_data->wrtPending, 0);

	return private_data;
}

static inline int cifs_convert_flags(unsigned int flags)
{
	if ((flags & O_ACCMODE) == O_RDONLY)
		return GENERIC_READ;
	else if ((flags & O_ACCMODE) == O_WRONLY)
		return GENERIC_WRITE;
	else if ((flags & O_ACCMODE) == O_RDWR) {
		/* GENERIC_ALL is too much permission to request
		   can cause unnecessary access denied on create */
		/* return GENERIC_ALL; */
		return (GENERIC_READ | GENERIC_WRITE);
	}

	return (READ_CONTROL | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES |
		FILE_WRITE_EA | FILE_APPEND_DATA | FILE_WRITE_DATA |
		FILE_READ_DATA);
}

static inline fmode_t cifs_posix_convert_flags(unsigned int flags)
{
	fmode_t posix_flags = 0;

	if ((flags & O_ACCMODE) == O_RDONLY)
		posix_flags = FMODE_READ;
	else if ((flags & O_ACCMODE) == O_WRONLY)
		posix_flags = FMODE_WRITE;
	else if ((flags & O_ACCMODE) == O_RDWR) {
		/* GENERIC_ALL is too much permission to request
		   can cause unnecessary access denied on create */
		/* return GENERIC_ALL; */
		posix_flags = FMODE_READ | FMODE_WRITE;
	}
	/* can not map O_CREAT or O_EXCL or O_TRUNC flags when
	   reopening a file.  They had their effect on the original open */
	if (flags & O_APPEND)
		posix_flags |= (fmode_t)O_APPEND;
	if (flags & O_SYNC)
		posix_flags |= (fmode_t)O_SYNC;
	if (flags & O_DIRECTORY)
		posix_flags |= (fmode_t)O_DIRECTORY;
	if (flags & O_NOFOLLOW)
		posix_flags |= (fmode_t)O_NOFOLLOW;
	if (flags & O_DIRECT)
		posix_flags |= (fmode_t)O_DIRECT;

	return posix_flags;
}

static inline int cifs_get_disposition(unsigned int flags)
{
	if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
		return FILE_CREATE;
	else if ((flags & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC))
		return FILE_OVERWRITE_IF;
	else if ((flags & O_CREAT) == O_CREAT)
		return FILE_OPEN_IF;
	else if ((flags & O_TRUNC) == O_TRUNC)
		return FILE_OVERWRITE;
	else
		return FILE_OPEN;
}

/* all arguments to this function must be checked for validity in caller */
static inline int cifs_posix_open_inode_helper(struct inode *inode,
			struct file *file, struct cifsInodeInfo *pCifsInode,
			struct cifsFileInfo *pCifsFile, int oplock, u16 netfid)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
/*	struct timespec temp; */   /* BB REMOVEME BB */

	file->private_data = kmalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
	if (file->private_data == NULL)
		return -ENOMEM;
	pCifsFile = cifs_init_private(file->private_data, inode, file, netfid);
	write_lock(&GlobalSMBSeslock);
	list_add(&pCifsFile->tlist, &cifs_sb->tcon->openFileList);

	pCifsInode = CIFS_I(file->f_path.dentry->d_inode);
	if (pCifsInode == NULL) {
		write_unlock(&GlobalSMBSeslock);
		return -EINVAL;
	}

	/* want handles we can use to read with first
	   in the list so we do not have to walk the
	   list to search for one in write_begin */
	if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
		list_add_tail(&pCifsFile->flist,
			      &pCifsInode->openFileList);
	} else {
		list_add(&pCifsFile->flist,
			 &pCifsInode->openFileList);
	}

	if (pCifsInode->clientCanCacheRead) {
		/* we have the inode open somewhere else
		   no need to discard cache data */
		goto psx_client_can_cache;
	}

	/* BB FIXME need to fix this check to move it earlier into posix_open
	   BB  fIX following section BB FIXME */

	/* if not oplocked, invalidate inode pages if mtime or file
	   size changed */
/*	temp = cifs_NTtimeToUnix(le64_to_cpu(buf->LastWriteTime));
	if (timespec_equal(&file->f_path.dentry->d_inode->i_mtime, &temp) &&
			   (file->f_path.dentry->d_inode->i_size ==
			    (loff_t)le64_to_cpu(buf->EndOfFile))) {
		cFYI(1, ("inode unchanged on server"));
	} else {
		if (file->f_path.dentry->d_inode->i_mapping) {
			rc = filemap_write_and_wait(file->f_path.dentry->d_inode->i_mapping);
			if (rc != 0)
				CIFS_I(file->f_path.dentry->d_inode)->write_behind_rc = rc;
		}
		cFYI(1, ("invalidating remote inode since open detected it "
			 "changed"));
		invalidate_remote_inode(file->f_path.dentry->d_inode);
	} */

psx_client_can_cache:
	if ((oplock & 0xF) == OPLOCK_EXCLUSIVE) {
		pCifsInode->clientCanCacheAll = true;
		pCifsInode->clientCanCacheRead = true;
		cFYI(1, ("Exclusive Oplock granted on inode %p",
			 file->f_path.dentry->d_inode));
	} else if ((oplock & 0xF) == OPLOCK_READ)
		pCifsInode->clientCanCacheRead = true;

	/* will have to change the unlock if we reenable the
	   filemap_fdatawrite (which does not seem necessary */
	write_unlock(&GlobalSMBSeslock);
	return 0;
}

/* all arguments to this function must be checked for validity in caller */
static inline int cifs_open_inode_helper(struct inode *inode, struct file *file,
	struct cifsInodeInfo *pCifsInode, struct cifsFileInfo *pCifsFile,
	struct cifsTconInfo *pTcon, int *oplock, FILE_ALL_INFO *buf,
	char *full_path, int xid)
{
	struct timespec temp;
	int rc;

	/* want handles we can use to read with first
	   in the list so we do not have to walk the
	   list to search for one in write_begin */
	if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
		list_add_tail(&pCifsFile->flist,
			      &pCifsInode->openFileList);
	} else {
		list_add(&pCifsFile->flist,
			 &pCifsInode->openFileList);
	}
	write_unlock(&GlobalSMBSeslock);
	if (pCifsInode->clientCanCacheRead) {
		/* we have the inode open somewhere else
		   no need to discard cache data */
		goto client_can_cache;
	}

	/* BB need same check in cifs_create too? */
	/* if not oplocked, invalidate inode pages if mtime or file
	   size changed */
	temp = cifs_NTtimeToUnix(le64_to_cpu(buf->LastWriteTime));
	if (timespec_equal(&file->f_path.dentry->d_inode->i_mtime, &temp) &&
			   (file->f_path.dentry->d_inode->i_size ==
			    (loff_t)le64_to_cpu(buf->EndOfFile))) {
		cFYI(1, ("inode unchanged on server"));
	} else {
		if (file->f_path.dentry->d_inode->i_mapping) {
		/* BB no need to lock inode until after invalidate
		   since namei code should already have it locked? */
			rc = filemap_write_and_wait(file->f_path.dentry->d_inode->i_mapping);
			if (rc != 0)
				CIFS_I(file->f_path.dentry->d_inode)->write_behind_rc = rc;
		}
		cFYI(1, ("invalidating remote inode since open detected it "
			 "changed"));
		invalidate_remote_inode(file->f_path.dentry->d_inode);
	}

client_can_cache:
	if (pTcon->unix_ext)
		rc = cifs_get_inode_info_unix(&file->f_path.dentry->d_inode,
			full_path, inode->i_sb, xid);
	else
		rc = cifs_get_inode_info(&file->f_path.dentry->d_inode,
			full_path, buf, inode->i_sb, xid, NULL);

	if ((*oplock & 0xF) == OPLOCK_EXCLUSIVE) {
		pCifsInode->clientCanCacheAll = true;
		pCifsInode->clientCanCacheRead = true;
		cFYI(1, ("Exclusive Oplock granted on inode %p",
			 file->f_path.dentry->d_inode));
	} else if ((*oplock & 0xF) == OPLOCK_READ)
		pCifsInode->clientCanCacheRead = true;

	return rc;
}

int cifs_open(struct inode *inode, struct file *file)
{
	int rc = -EACCES;
	int xid, oplock;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *tcon;
	struct cifsFileInfo *pCifsFile;
	struct cifsInodeInfo *pCifsInode;
	struct list_head *tmp;
	char *full_path = NULL;
	int desiredAccess;
	int disposition;
	__u16 netfid;
	FILE_ALL_INFO *buf = NULL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	tcon = cifs_sb->tcon;

	/* search inode for this file and fill in file->private_data */
	pCifsInode = CIFS_I(file->f_path.dentry->d_inode);
	read_lock(&GlobalSMBSeslock);
	list_for_each(tmp, &pCifsInode->openFileList) {
		pCifsFile = list_entry(tmp, struct cifsFileInfo,
				       flist);
		if ((pCifsFile->pfile == NULL) &&
		    (pCifsFile->pid == current->tgid)) {
			/* mode set in cifs_create */

			/* needed for writepage */
			pCifsFile->pfile = file;

			file->private_data = pCifsFile;
			break;
		}
	}
	read_unlock(&GlobalSMBSeslock);

	if (file->private_data != NULL) {
		rc = 0;
		FreeXid(xid);
		return rc;
	} else if ((file->f_flags & O_CREAT) && (file->f_flags & O_EXCL))
			cERROR(1, ("could not find file instance for "
				   "new file %p", file));

	full_path = build_path_from_dentry(file->f_path.dentry);
	if (full_path == NULL) {
		FreeXid(xid);
		return -ENOMEM;
	}

	cFYI(1, ("inode = 0x%p file flags are 0x%x for %s",
		 inode, file->f_flags, full_path));

	if (oplockEnabled)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;

	if (!tcon->broken_posix_open && tcon->unix_ext &&
	    (tcon->ses->capabilities & CAP_UNIX) &&
	    (CIFS_UNIX_POSIX_PATH_OPS_CAP &
			le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		int oflags = (int) cifs_posix_convert_flags(file->f_flags);
		/* can not refresh inode info since size could be stale */
		rc = cifs_posix_open(full_path, &inode, inode->i_sb,
				     cifs_sb->mnt_file_mode /* ignored */,
				     oflags, &oplock, &netfid, xid);
		if (rc == 0) {
			cFYI(1, ("posix open succeeded"));
			/* no need for special case handling of setting mode
			   on read only files needed here */

			cifs_posix_open_inode_helper(inode, file, pCifsInode,
						     pCifsFile, oplock, netfid);
			goto out;
		} else if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
			if (tcon->ses->serverNOS)
				cERROR(1, ("server %s of type %s returned"
					   " unexpected error on SMB posix open"
					   ", disabling posix open support."
					   " Check if server update available.",
					   tcon->ses->serverName,
					   tcon->ses->serverNOS));
			tcon->broken_posix_open = true;
		} else if ((rc != -EIO) && (rc != -EREMOTE) &&
			 (rc != -EOPNOTSUPP)) /* path not found or net err */
			goto out;
		/* else fallthrough to retry open the old way on network i/o
		   or DFS errors */
	}

	desiredAccess = cifs_convert_flags(file->f_flags);

/*********************************************************************
 *  open flag mapping table:
 *
 *	POSIX Flag            CIFS Disposition
 *	----------            ----------------
 *	O_CREAT               FILE_OPEN_IF
 *	O_CREAT | O_EXCL      FILE_CREATE
 *	O_CREAT | O_TRUNC     FILE_OVERWRITE_IF
 *	O_TRUNC               FILE_OVERWRITE
 *	none of the above     FILE_OPEN
 *
 *	Note that there is not a direct match between disposition
 *	FILE_SUPERSEDE (ie create whether or not file exists although
 *	O_CREAT | O_TRUNC is similar but truncates the existing
 *	file rather than creating a new file as FILE_SUPERSEDE does
 *	(which uses the attributes / metadata passed in on open call)
 *?
 *?  O_SYNC is a reasonable match to CIFS writethrough flag
 *?  and the read write flags match reasonably.  O_LARGEFILE
 *?  is irrelevant because largefile support is always used
 *?  by this client. Flags O_APPEND, O_DIRECT, O_DIRECTORY,
 *	 O_FASYNC, O_NOFOLLOW, O_NONBLOCK need further investigation
 *********************************************************************/

	disposition = cifs_get_disposition(file->f_flags);

	/* BB pass O_SYNC flag through on file attributes .. BB */

	/* Also refresh inode by passing in file_info buf returned by SMBOpen
	   and calling get_inode_info with returned buf (at least helps
	   non-Unix server case) */

	/* BB we can not do this if this is the second open of a file
	   and the first handle has writebehind data, we might be
	   able to simply do a filemap_fdatawrite/filemap_fdatawait first */
	buf = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto out;
	}

	if (cifs_sb->tcon->ses->capabilities & CAP_NT_SMBS)
		rc = CIFSSMBOpen(xid, tcon, full_path, disposition,
			 desiredAccess, CREATE_NOT_DIR, &netfid, &oplock, buf,
			 cifs_sb->local_nls, cifs_sb->mnt_cifs_flags
				 & CIFS_MOUNT_MAP_SPECIAL_CHR);
	else
		rc = -EIO; /* no NT SMB support fall into legacy open below */

	if (rc == -EIO) {
		/* Old server, try legacy style OpenX */
		rc = SMBLegacyOpen(xid, tcon, full_path, disposition,
			desiredAccess, CREATE_NOT_DIR, &netfid, &oplock, buf,
			cifs_sb->local_nls, cifs_sb->mnt_cifs_flags
				& CIFS_MOUNT_MAP_SPECIAL_CHR);
	}
	if (rc) {
		cFYI(1, ("cifs_open returned 0x%x", rc));
		goto out;
	}
	file->private_data =
		kmalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
	if (file->private_data == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	pCifsFile = cifs_init_private(file->private_data, inode, file, netfid);
	write_lock(&GlobalSMBSeslock);
	list_add(&pCifsFile->tlist, &tcon->openFileList);

	pCifsInode = CIFS_I(file->f_path.dentry->d_inode);
	if (pCifsInode) {
		rc = cifs_open_inode_helper(inode, file, pCifsInode,
					    pCifsFile, tcon,
					    &oplock, buf, full_path, xid);
	} else {
		write_unlock(&GlobalSMBSeslock);
	}

	if (oplock & CIFS_CREATE_ACTION) {
		/* time to set mode which we can not set earlier due to
		   problems creating new read-only files */
		if (tcon->unix_ext) {
			struct cifs_unix_set_info_args args = {
				.mode	= inode->i_mode,
				.uid	= NO_CHANGE_64,
				.gid	= NO_CHANGE_64,
				.ctime	= NO_CHANGE_64,
				.atime	= NO_CHANGE_64,
				.mtime	= NO_CHANGE_64,
				.device	= 0,
			};
			CIFSSMBUnixSetInfo(xid, tcon, full_path, &args,
					    cifs_sb->local_nls,
					    cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		}
	}

out:
	kfree(buf);
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

/* Try to reacquire byte range locks that were released when session */
/* to server was lost */
static int cifs_relock_file(struct cifsFileInfo *cifsFile)
{
	int rc = 0;

/* BB list all locks open on this file and relock */

	return rc;
}

static int cifs_reopen_file(struct file *file, bool can_flush)
{
	int rc = -EACCES;
	int xid, oplock;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *tcon;
	struct cifsFileInfo *pCifsFile;
	struct cifsInodeInfo *pCifsInode;
	struct inode *inode;
	char *full_path = NULL;
	int desiredAccess;
	int disposition = FILE_OPEN;
	__u16 netfid;

	if (file->private_data)
		pCifsFile = (struct cifsFileInfo *)file->private_data;
	else
		return -EBADF;

	xid = GetXid();
	mutex_unlock(&pCifsFile->fh_mutex);
	if (!pCifsFile->invalidHandle) {
		mutex_lock(&pCifsFile->fh_mutex);
		FreeXid(xid);
		return 0;
	}

	if (file->f_path.dentry == NULL) {
		cERROR(1, ("no valid name if dentry freed"));
		dump_stack();
		rc = -EBADF;
		goto reopen_error_exit;
	}

	inode = file->f_path.dentry->d_inode;
	if (inode == NULL) {
		cERROR(1, ("inode not valid"));
		dump_stack();
		rc = -EBADF;
		goto reopen_error_exit;
	}

	cifs_sb = CIFS_SB(inode->i_sb);
	tcon = cifs_sb->tcon;

/* can not grab rename sem here because various ops, including
   those that already have the rename sem can end up causing writepage
   to get called and if the server was down that means we end up here,
   and we can never tell if the caller already has the rename_sem */
	full_path = build_path_from_dentry(file->f_path.dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
reopen_error_exit:
		mutex_lock(&pCifsFile->fh_mutex);
		FreeXid(xid);
		return rc;
	}

	cFYI(1, ("inode = 0x%p file flags 0x%x for %s",
		 inode, file->f_flags, full_path));

	if (oplockEnabled)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;

	if (tcon->unix_ext && (tcon->ses->capabilities & CAP_UNIX) &&
	    (CIFS_UNIX_POSIX_PATH_OPS_CAP &
			le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		int oflags = (int) cifs_posix_convert_flags(file->f_flags);
		/* can not refresh inode info since size could be stale */
		rc = cifs_posix_open(full_path, NULL, inode->i_sb,
				     cifs_sb->mnt_file_mode /* ignored */,
				     oflags, &oplock, &netfid, xid);
		if (rc == 0) {
			cFYI(1, ("posix reopen succeeded"));
			goto reopen_success;
		}
		/* fallthrough to retry open the old way on errors, especially
		   in the reconnect path it is important to retry hard */
	}

	desiredAccess = cifs_convert_flags(file->f_flags);

	/* Can not refresh inode by passing in file_info buf to be returned
	   by SMBOpen and then calling get_inode_info with returned buf
	   since file might have write behind data that needs to be flushed
	   and server version of file size can be stale. If we knew for sure
	   that inode was not dirty locally we could do this */

	rc = CIFSSMBOpen(xid, tcon, full_path, disposition, desiredAccess,
			 CREATE_NOT_DIR, &netfid, &oplock, NULL,
			 cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc) {
		mutex_lock(&pCifsFile->fh_mutex);
		cFYI(1, ("cifs_open returned 0x%x", rc));
		cFYI(1, ("oplock: %d", oplock));
	} else {
reopen_success:
		pCifsFile->netfid = netfid;
		pCifsFile->invalidHandle = false;
		mutex_lock(&pCifsFile->fh_mutex);
		pCifsInode = CIFS_I(inode);
		if (pCifsInode) {
			if (can_flush) {
				rc = filemap_write_and_wait(inode->i_mapping);
				if (rc != 0)
					CIFS_I(inode)->write_behind_rc = rc;
			/* temporarily disable caching while we
			   go to server to get inode info */
				pCifsInode->clientCanCacheAll = false;
				pCifsInode->clientCanCacheRead = false;
				if (tcon->unix_ext)
					rc = cifs_get_inode_info_unix(&inode,
						full_path, inode->i_sb, xid);
				else
					rc = cifs_get_inode_info(&inode,
						full_path, NULL, inode->i_sb,
						xid, NULL);
			} /* else we are writing out data to server already
			     and could deadlock if we tried to flush data, and
			     since we do not know if we have data that would
			     invalidate the current end of file on the server
			     we can not go to the server to get the new inod
			     info */
			if ((oplock & 0xF) == OPLOCK_EXCLUSIVE) {
				pCifsInode->clientCanCacheAll = true;
				pCifsInode->clientCanCacheRead = true;
				cFYI(1, ("Exclusive Oplock granted on inode %p",
					 file->f_path.dentry->d_inode));
			} else if ((oplock & 0xF) == OPLOCK_READ) {
				pCifsInode->clientCanCacheRead = true;
				pCifsInode->clientCanCacheAll = false;
			} else {
				pCifsInode->clientCanCacheRead = false;
				pCifsInode->clientCanCacheAll = false;
			}
			cifs_relock_file(pCifsFile);
		}
	}
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

int cifs_close(struct inode *inode, struct file *file)
{
	int rc = 0;
	int xid, timeout;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsFileInfo *pSMBFile =
		(struct cifsFileInfo *)file->private_data;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;
	if (pSMBFile) {
		struct cifsLockInfo *li, *tmp;
		write_lock(&GlobalSMBSeslock);
		pSMBFile->closePend = true;
		if (pTcon) {
			/* no sense reconnecting to close a file that is
			   already closed */
			if (!pTcon->need_reconnect) {
				write_unlock(&GlobalSMBSeslock);
				timeout = 2;
				while ((atomic_read(&pSMBFile->wrtPending) != 0)
					&& (timeout <= 2048)) {
					/* Give write a better chance to get to
					server ahead of the close.  We do not
					want to add a wait_q here as it would
					increase the memory utilization as
					the struct would be in each open file,
					but this should give enough time to
					clear the socket */
					cFYI(DBG2,
						("close delay, write pending"));
					msleep(timeout);
					timeout *= 4;
				}
				if (atomic_read(&pSMBFile->wrtPending))
					cERROR(1, ("close with pending write"));
				if (!pTcon->need_reconnect &&
				    !pSMBFile->invalidHandle)
					rc = CIFSSMBClose(xid, pTcon,
						  pSMBFile->netfid);
			} else
				write_unlock(&GlobalSMBSeslock);
		} else
			write_unlock(&GlobalSMBSeslock);

		/* Delete any outstanding lock records.
		   We'll lose them when the file is closed anyway. */
		mutex_lock(&pSMBFile->lock_mutex);
		list_for_each_entry_safe(li, tmp, &pSMBFile->llist, llist) {
			list_del(&li->llist);
			kfree(li);
		}
		mutex_unlock(&pSMBFile->lock_mutex);

		write_lock(&GlobalSMBSeslock);
		list_del(&pSMBFile->flist);
		list_del(&pSMBFile->tlist);
		write_unlock(&GlobalSMBSeslock);
		timeout = 10;
		/* We waited above to give the SMBWrite a chance to issue
		   on the wire (so we do not get SMBWrite returning EBADF
		   if writepages is racing with close.  Note that writepages
		   does not specify a file handle, so it is possible for a file
		   to be opened twice, and the application close the "wrong"
		   file handle - in these cases we delay long enough to allow
		   the SMBWrite to get on the wire before the SMB Close.
		   We allow total wait here over 45 seconds, more than
		   oplock break time, and more than enough to allow any write
		   to complete on the server, or to time out on the client */
		while ((atomic_read(&pSMBFile->wrtPending) != 0)
				&& (timeout <= 50000)) {
			cERROR(1, ("writes pending, delay free of handle"));
			msleep(timeout);
			timeout *= 8;
		}
		kfree(file->private_data);
		file->private_data = NULL;
	} else
		rc = -EBADF;

	read_lock(&GlobalSMBSeslock);
	if (list_empty(&(CIFS_I(inode)->openFileList))) {
		cFYI(1, ("closing last open instance for inode %p", inode));
		/* if the file is not open we do not know if we can cache info
		   on this inode, much less write behind and read ahead */
		CIFS_I(inode)->clientCanCacheRead = false;
		CIFS_I(inode)->clientCanCacheAll  = false;
	}
	read_unlock(&GlobalSMBSeslock);
	if ((rc == 0) && CIFS_I(inode)->write_behind_rc)
		rc = CIFS_I(inode)->write_behind_rc;
	FreeXid(xid);
	return rc;
}

int cifs_closedir(struct inode *inode, struct file *file)
{
	int rc = 0;
	int xid;
	struct cifsFileInfo *pCFileStruct =
	    (struct cifsFileInfo *)file->private_data;
	char *ptmp;

	cFYI(1, ("Closedir inode = 0x%p", inode));

	xid = GetXid();

	if (pCFileStruct) {
		struct cifsTconInfo *pTcon;
		struct cifs_sb_info *cifs_sb =
			CIFS_SB(file->f_path.dentry->d_sb);

		pTcon = cifs_sb->tcon;

		cFYI(1, ("Freeing private data in close dir"));
		write_lock(&GlobalSMBSeslock);
		if (!pCFileStruct->srch_inf.endOfSearch &&
		    !pCFileStruct->invalidHandle) {
			pCFileStruct->invalidHandle = true;
			write_unlock(&GlobalSMBSeslock);
			rc = CIFSFindClose(xid, pTcon, pCFileStruct->netfid);
			cFYI(1, ("Closing uncompleted readdir with rc %d",
				 rc));
			/* not much we can do if it fails anyway, ignore rc */
			rc = 0;
		} else
			write_unlock(&GlobalSMBSeslock);
		ptmp = pCFileStruct->srch_inf.ntwrk_buf_start;
		if (ptmp) {
			cFYI(1, ("closedir free smb buf in srch struct"));
			pCFileStruct->srch_inf.ntwrk_buf_start = NULL;
			if (pCFileStruct->srch_inf.smallBuf)
				cifs_small_buf_release(ptmp);
			else
				cifs_buf_release(ptmp);
		}
		kfree(file->private_data);
		file->private_data = NULL;
	}
	/* BB can we lock the filestruct while this is going on? */
	FreeXid(xid);
	return rc;
}

static int store_file_lock(struct cifsFileInfo *fid, __u64 len,
				__u64 offset, __u8 lockType)
{
	struct cifsLockInfo *li =
		kmalloc(sizeof(struct cifsLockInfo), GFP_KERNEL);
	if (li == NULL)
		return -ENOMEM;
	li->offset = offset;
	li->length = len;
	li->type = lockType;
	mutex_lock(&fid->lock_mutex);
	list_add(&li->llist, &fid->llist);
	mutex_unlock(&fid->lock_mutex);
	return 0;
}

int cifs_lock(struct file *file, int cmd, struct file_lock *pfLock)
{
	int rc, xid;
	__u32 numLock = 0;
	__u32 numUnlock = 0;
	__u64 length;
	bool wait_flag = false;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *tcon;
	__u16 netfid;
	__u8 lockType = LOCKING_ANDX_LARGE_FILES;
	bool posix_locking = 0;

	length = 1 + pfLock->fl_end - pfLock->fl_start;
	rc = -EACCES;
	xid = GetXid();

	cFYI(1, ("Lock parm: 0x%x flockflags: "
		 "0x%x flocktype: 0x%x start: %lld end: %lld",
		cmd, pfLock->fl_flags, pfLock->fl_type, pfLock->fl_start,
		pfLock->fl_end));

	if (pfLock->fl_flags & FL_POSIX)
		cFYI(1, ("Posix"));
	if (pfLock->fl_flags & FL_FLOCK)
		cFYI(1, ("Flock"));
	if (pfLock->fl_flags & FL_SLEEP) {
		cFYI(1, ("Blocking lock"));
		wait_flag = true;
	}
	if (pfLock->fl_flags & FL_ACCESS)
		cFYI(1, ("Process suspended by mandatory locking - "
			 "not implemented yet"));
	if (pfLock->fl_flags & FL_LEASE)
		cFYI(1, ("Lease on file - not implemented yet"));
	if (pfLock->fl_flags &
	    (~(FL_POSIX | FL_FLOCK | FL_SLEEP | FL_ACCESS | FL_LEASE)))
		cFYI(1, ("Unknown lock flags 0x%x", pfLock->fl_flags));

	if (pfLock->fl_type == F_WRLCK) {
		cFYI(1, ("F_WRLCK "));
		numLock = 1;
	} else if (pfLock->fl_type == F_UNLCK) {
		cFYI(1, ("F_UNLCK"));
		numUnlock = 1;
		/* Check if unlock includes more than
		one lock range */
	} else if (pfLock->fl_type == F_RDLCK) {
		cFYI(1, ("F_RDLCK"));
		lockType |= LOCKING_ANDX_SHARED_LOCK;
		numLock = 1;
	} else if (pfLock->fl_type == F_EXLCK) {
		cFYI(1, ("F_EXLCK"));
		numLock = 1;
	} else if (pfLock->fl_type == F_SHLCK) {
		cFYI(1, ("F_SHLCK"));
		lockType |= LOCKING_ANDX_SHARED_LOCK;
		numLock = 1;
	} else
		cFYI(1, ("Unknown type of lock"));

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	tcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	netfid = ((struct cifsFileInfo *)file->private_data)->netfid;

	if ((tcon->ses->capabilities & CAP_UNIX) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		posix_locking = 1;
	/* BB add code here to normalize offset and length to
	account for negative length which we can not accept over the
	wire */
	if (IS_GETLK(cmd)) {
		if (posix_locking) {
			int posix_lock_type;
			if (lockType & LOCKING_ANDX_SHARED_LOCK)
				posix_lock_type = CIFS_RDLCK;
			else
				posix_lock_type = CIFS_WRLCK;
			rc = CIFSSMBPosixLock(xid, tcon, netfid, 1 /* get */,
					length,	pfLock,
					posix_lock_type, wait_flag);
			FreeXid(xid);
			return rc;
		}

		/* BB we could chain these into one lock request BB */
		rc = CIFSSMBLock(xid, tcon, netfid, length, pfLock->fl_start,
				 0, 1, lockType, 0 /* wait flag */ );
		if (rc == 0) {
			rc = CIFSSMBLock(xid, tcon, netfid, length,
					 pfLock->fl_start, 1 /* numUnlock */ ,
					 0 /* numLock */ , lockType,
					 0 /* wait flag */ );
			pfLock->fl_type = F_UNLCK;
			if (rc != 0)
				cERROR(1, ("Error unlocking previously locked "
					   "range %d during test of lock", rc));
			rc = 0;

		} else {
			/* if rc == ERR_SHARING_VIOLATION ? */
			rc = 0;	/* do not change lock type to unlock
				   since range in use */
		}

		FreeXid(xid);
		return rc;
	}

	if (!numLock && !numUnlock) {
		/* if no lock or unlock then nothing
		to do since we do not know what it is */
		FreeXid(xid);
		return -EOPNOTSUPP;
	}

	if (posix_locking) {
		int posix_lock_type;
		if (lockType & LOCKING_ANDX_SHARED_LOCK)
			posix_lock_type = CIFS_RDLCK;
		else
			posix_lock_type = CIFS_WRLCK;

		if (numUnlock == 1)
			posix_lock_type = CIFS_UNLCK;

		rc = CIFSSMBPosixLock(xid, tcon, netfid, 0 /* set */,
				      length, pfLock,
				      posix_lock_type, wait_flag);
	} else {
		struct cifsFileInfo *fid =
			(struct cifsFileInfo *)file->private_data;

		if (numLock) {
			rc = CIFSSMBLock(xid, tcon, netfid, length,
					pfLock->fl_start,
					0, numLock, lockType, wait_flag);

			if (rc == 0) {
				/* For Windows locks we must store them. */
				rc = store_file_lock(fid, length,
						pfLock->fl_start, lockType);
			}
		} else if (numUnlock) {
			/* For each stored lock that this unlock overlaps
			   completely, unlock it. */
			int stored_rc = 0;
			struct cifsLockInfo *li, *tmp;

			rc = 0;
			mutex_lock(&fid->lock_mutex);
			list_for_each_entry_safe(li, tmp, &fid->llist, llist) {
				if (pfLock->fl_start <= li->offset &&
						(pfLock->fl_start + length) >=
						(li->offset + li->length)) {
					stored_rc = CIFSSMBLock(xid, tcon,
							netfid,
							li->length, li->offset,
							1, 0, li->type, false);
					if (stored_rc)
						rc = stored_rc;

					list_del(&li->llist);
					kfree(li);
				}
			}
			mutex_unlock(&fid->lock_mutex);
		}
	}

	if (pfLock->fl_flags & FL_POSIX)
		posix_lock_file_wait(file, pfLock);
	FreeXid(xid);
	return rc;
}

/*
 * Set the timeout on write requests past EOF. For some servers (Windows)
 * these calls can be very long.
 *
 * If we're writing >10M past the EOF we give a 180s timeout. Anything less
 * than that gets a 45s timeout. Writes not past EOF get 15s timeouts.
 * The 10M cutoff is totally arbitrary. A better scheme for this would be
 * welcome if someone wants to suggest one.
 *
 * We may be able to do a better job with this if there were some way to
 * declare that a file should be sparse.
 */
static int
cifs_write_timeout(struct cifsInodeInfo *cifsi, loff_t offset)
{
	if (offset <= cifsi->server_eof)
		return CIFS_STD_OP;
	else if (offset > (cifsi->server_eof + (10 * 1024 * 1024)))
		return CIFS_VLONG_OP;
	else
		return CIFS_LONG_OP;
}

/* update the file size (if needed) after a write */
static void
cifs_update_eof(struct cifsInodeInfo *cifsi, loff_t offset,
		      unsigned int bytes_written)
{
	loff_t end_of_write = offset + bytes_written;

	if (end_of_write > cifsi->server_eof)
		cifsi->server_eof = end_of_write;
}

ssize_t cifs_user_write(struct file *file, const char __user *write_data,
	size_t write_size, loff_t *poffset)
{
	int rc = 0;
	unsigned int bytes_written = 0;
	unsigned int total_written;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid, long_op;
	struct cifsFileInfo *open_file;
	struct cifsInodeInfo *cifsi = CIFS_I(file->f_path.dentry->d_inode);

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);

	pTcon = cifs_sb->tcon;

	/* cFYI(1,
	   (" write %d bytes to offset %lld of %s", write_size,
	   *poffset, file->f_path.dentry->d_name.name)); */

	if (file->private_data == NULL)
		return -EBADF;
	open_file = (struct cifsFileInfo *) file->private_data;

	rc = generic_write_checks(file, poffset, &write_size, 0);
	if (rc)
		return rc;

	xid = GetXid();

	long_op = cifs_write_timeout(cifsi, *poffset);
	for (total_written = 0; write_size > total_written;
	     total_written += bytes_written) {
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			if (file->private_data == NULL) {
				/* file has been closed on us */
				FreeXid(xid);
			/* if we have gotten here we have written some data
			   and blocked, and the file has been freed on us while
			   we blocked so return what we managed to write */
				return total_written;
			}
			if (open_file->closePend) {
				FreeXid(xid);
				if (total_written)
					return total_written;
				else
					return -EBADF;
			}
			if (open_file->invalidHandle) {
				/* we could deadlock if we called
				   filemap_fdatawait from here so tell
				   reopen_file not to flush data to server
				   now */
				rc = cifs_reopen_file(file, false);
				if (rc != 0)
					break;
			}

			rc = CIFSSMBWrite(xid, pTcon,
				open_file->netfid,
				min_t(const int, cifs_sb->wsize,
				      write_size - total_written),
				*poffset, &bytes_written,
				NULL, write_data + total_written, long_op);
		}
		if (rc || (bytes_written == 0)) {
			if (total_written)
				break;
			else {
				FreeXid(xid);
				return rc;
			}
		} else {
			cifs_update_eof(cifsi, *poffset, bytes_written);
			*poffset += bytes_written;
		}
		long_op = CIFS_STD_OP; /* subsequent writes fast -
				    15 seconds is plenty */
	}

	cifs_stats_bytes_written(pTcon, total_written);

	/* since the write may have blocked check these pointers again */
	if ((file->f_path.dentry) && (file->f_path.dentry->d_inode)) {
		struct inode *inode = file->f_path.dentry->d_inode;
/* Do not update local mtime - server will set its actual value on write
 *		inode->i_ctime = inode->i_mtime =
 * 			current_fs_time(inode->i_sb);*/
		if (total_written > 0) {
			spin_lock(&inode->i_lock);
			if (*poffset > file->f_path.dentry->d_inode->i_size)
				i_size_write(file->f_path.dentry->d_inode,
					*poffset);
			spin_unlock(&inode->i_lock);
		}
		mark_inode_dirty_sync(file->f_path.dentry->d_inode);
	}
	FreeXid(xid);
	return total_written;
}

static ssize_t cifs_write(struct file *file, const char *write_data,
			  size_t write_size, loff_t *poffset)
{
	int rc = 0;
	unsigned int bytes_written = 0;
	unsigned int total_written;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid, long_op;
	struct cifsFileInfo *open_file;
	struct cifsInodeInfo *cifsi = CIFS_I(file->f_path.dentry->d_inode);

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);

	pTcon = cifs_sb->tcon;

	cFYI(1, ("write %zd bytes to offset %lld of %s", write_size,
	   *poffset, file->f_path.dentry->d_name.name));

	if (file->private_data == NULL)
		return -EBADF;
	open_file = (struct cifsFileInfo *)file->private_data;

	xid = GetXid();

	long_op = cifs_write_timeout(cifsi, *poffset);
	for (total_written = 0; write_size > total_written;
	     total_written += bytes_written) {
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			if (file->private_data == NULL) {
				/* file has been closed on us */
				FreeXid(xid);
			/* if we have gotten here we have written some data
			   and blocked, and the file has been freed on us
			   while we blocked so return what we managed to
			   write */
				return total_written;
			}
			if (open_file->closePend) {
				FreeXid(xid);
				if (total_written)
					return total_written;
				else
					return -EBADF;
			}
			if (open_file->invalidHandle) {
				/* we could deadlock if we called
				   filemap_fdatawait from here so tell
				   reopen_file not to flush data to
				   server now */
				rc = cifs_reopen_file(file, false);
				if (rc != 0)
					break;
			}
			if (experimEnabled || (pTcon->ses->server &&
				((pTcon->ses->server->secMode &
				(SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
				== 0))) {
				struct kvec iov[2];
				unsigned int len;

				len = min((size_t)cifs_sb->wsize,
					  write_size - total_written);
				/* iov[0] is reserved for smb header */
				iov[1].iov_base = (char *)write_data +
						  total_written;
				iov[1].iov_len = len;
				rc = CIFSSMBWrite2(xid, pTcon,
						open_file->netfid, len,
						*poffset, &bytes_written,
						iov, 1, long_op);
			} else
				rc = CIFSSMBWrite(xid, pTcon,
					 open_file->netfid,
					 min_t(const int, cifs_sb->wsize,
					       write_size - total_written),
					 *poffset, &bytes_written,
					 write_data + total_written,
					 NULL, long_op);
		}
		if (rc || (bytes_written == 0)) {
			if (total_written)
				break;
			else {
				FreeXid(xid);
				return rc;
			}
		} else {
			cifs_update_eof(cifsi, *poffset, bytes_written);
			*poffset += bytes_written;
		}
		long_op = CIFS_STD_OP; /* subsequent writes fast -
				    15 seconds is plenty */
	}

	cifs_stats_bytes_written(pTcon, total_written);

	/* since the write may have blocked check these pointers again */
	if ((file->f_path.dentry) && (file->f_path.dentry->d_inode)) {
/*BB We could make this contingent on superblock ATIME flag too */
/*		file->f_path.dentry->d_inode->i_ctime =
		file->f_path.dentry->d_inode->i_mtime = CURRENT_TIME;*/
		if (total_written > 0) {
			spin_lock(&file->f_path.dentry->d_inode->i_lock);
			if (*poffset > file->f_path.dentry->d_inode->i_size)
				i_size_write(file->f_path.dentry->d_inode,
					     *poffset);
			spin_unlock(&file->f_path.dentry->d_inode->i_lock);
		}
		mark_inode_dirty_sync(file->f_path.dentry->d_inode);
	}
	FreeXid(xid);
	return total_written;
}

#ifdef CONFIG_CIFS_EXPERIMENTAL
struct cifsFileInfo *find_readable_file(struct cifsInodeInfo *cifs_inode)
{
	struct cifsFileInfo *open_file = NULL;

	read_lock(&GlobalSMBSeslock);
	/* we could simply get the first_list_entry since write-only entries
	   are always at the end of the list but since the first entry might
	   have a close pending, we go through the whole list */
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (open_file->closePend)
			continue;
		if (open_file->pfile && ((open_file->pfile->f_flags & O_RDWR) ||
		    (open_file->pfile->f_flags & O_RDONLY))) {
			if (!open_file->invalidHandle) {
				/* found a good file */
				/* lock it so it will not be closed on us */
				atomic_inc(&open_file->wrtPending);
				read_unlock(&GlobalSMBSeslock);
				return open_file;
			} /* else might as well continue, and look for
			     another, or simply have the caller reopen it
			     again rather than trying to fix this handle */
		} else /* write only file */
			break; /* write only files are last so must be done */
	}
	read_unlock(&GlobalSMBSeslock);
	return NULL;
}
#endif

struct cifsFileInfo *find_writable_file(struct cifsInodeInfo *cifs_inode)
{
	struct cifsFileInfo *open_file;
	bool any_available = false;
	int rc;

	/* Having a null inode here (because mapping->host was set to zero by
	the VFS or MM) should not happen but we had reports of on oops (due to
	it being zero) during stress testcases so we need to check for it */

	if (cifs_inode == NULL) {
		cERROR(1, ("Null inode passed to cifs_writeable_file"));
		dump_stack();
		return NULL;
	}

	read_lock(&GlobalSMBSeslock);
refind_writable:
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (open_file->closePend ||
		    (!any_available && open_file->pid != current->tgid))
			continue;

		if (open_file->pfile &&
		    ((open_file->pfile->f_flags & O_RDWR) ||
		     (open_file->pfile->f_flags & O_WRONLY))) {
			atomic_inc(&open_file->wrtPending);

			if (!open_file->invalidHandle) {
				/* found a good writable file */
				read_unlock(&GlobalSMBSeslock);
				return open_file;
			}

			read_unlock(&GlobalSMBSeslock);
			/* Had to unlock since following call can block */
			rc = cifs_reopen_file(open_file->pfile, false);
			if (!rc) {
				if (!open_file->closePend)
					return open_file;
				else { /* start over in case this was deleted */
				       /* since the list could be modified */
					read_lock(&GlobalSMBSeslock);
					atomic_dec(&open_file->wrtPending);
					goto refind_writable;
				}
			}

			/* if it fails, try another handle if possible -
			(we can not do this if closePending since
			loop could be modified - in which case we
			have to start at the beginning of the list
			again. Note that it would be bad
			to hold up writepages here (rather than
			in caller) with continuous retries */
			cFYI(1, ("wp failed on reopen file"));
			read_lock(&GlobalSMBSeslock);
			/* can not use this handle, no write
			   pending on this one after all */
			atomic_dec(&open_file->wrtPending);

			if (open_file->closePend) /* list could have changed */
				goto refind_writable;
			/* else we simply continue to the next entry. Thus
			   we do not loop on reopen errors.  If we
			   can not reopen the file, for example if we
			   reconnected to a server with another client
			   racing to delete or lock the file we would not
			   make progress if we restarted before the beginning
			   of the loop here. */
		}
	}
	/* couldn't find useable FH with same pid, try any available */
	if (!any_available) {
		any_available = true;
		goto refind_writable;
	}
	read_unlock(&GlobalSMBSeslock);
	return NULL;
}

static int cifs_partialpagewrite(struct page *page, unsigned from, unsigned to)
{
	struct address_space *mapping = page->mapping;
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	char *write_data;
	int rc = -EFAULT;
	int bytes_written = 0;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct inode *inode;
	struct cifsFileInfo *open_file;

	if (!mapping || !mapping->host)
		return -EFAULT;

	inode = page->mapping->host;
	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb->tcon;

	offset += (loff_t)from;
	write_data = kmap(page);
	write_data += from;

	if ((to > PAGE_CACHE_SIZE) || (from > to)) {
		kunmap(page);
		return -EIO;
	}

	/* racing with truncate? */
	if (offset > mapping->host->i_size) {
		kunmap(page);
		return 0; /* don't care */
	}

	/* check to make sure that we are not extending the file */
	if (mapping->host->i_size - offset < (loff_t)to)
		to = (unsigned)(mapping->host->i_size - offset);

	open_file = find_writable_file(CIFS_I(mapping->host));
	if (open_file) {
		bytes_written = cifs_write(open_file->pfile, write_data,
					   to-from, &offset);
		atomic_dec(&open_file->wrtPending);
		/* Does mm or vfs already set times? */
		inode->i_atime = inode->i_mtime = current_fs_time(inode->i_sb);
		if ((bytes_written > 0) && (offset))
			rc = 0;
		else if (bytes_written < 0)
			rc = bytes_written;
	} else {
		cFYI(1, ("No writeable filehandles for inode"));
		rc = -EIO;
	}

	kunmap(page);
	return rc;
}

static int cifs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	struct backing_dev_info *bdi = mapping->backing_dev_info;
	unsigned int bytes_to_write;
	unsigned int bytes_written;
	struct cifs_sb_info *cifs_sb;
	int done = 0;
	pgoff_t end;
	pgoff_t index;
	int range_whole = 0;
	struct kvec *iov;
	int len;
	int n_iov = 0;
	pgoff_t next;
	int nr_pages;
	__u64 offset = 0;
	struct cifsFileInfo *open_file;
	struct cifsInodeInfo *cifsi = CIFS_I(mapping->host);
	struct page *page;
	struct pagevec pvec;
	int rc = 0;
	int scanned = 0;
	int xid, long_op;

	cifs_sb = CIFS_SB(mapping->host->i_sb);

	/*
	 * If wsize is smaller that the page cache size, default to writing
	 * one page at a time via cifs_writepage
	 */
	if (cifs_sb->wsize < PAGE_CACHE_SIZE)
		return generic_writepages(mapping, wbc);

	if ((cifs_sb->tcon->ses) && (cifs_sb->tcon->ses->server))
		if (cifs_sb->tcon->ses->server->secMode &
				(SECMODE_SIGN_REQUIRED | SECMODE_SIGN_ENABLED))
			if (!experimEnabled)
				return generic_writepages(mapping, wbc);

	iov = kmalloc(32 * sizeof(struct kvec), GFP_KERNEL);
	if (iov == NULL)
		return generic_writepages(mapping, wbc);


	/*
	 * BB: Is this meaningful for a non-block-device file system?
	 * If it is, we should test it again after we do I/O
	 */
	if (wbc->nonblocking && bdi_write_congested(bdi)) {
		wbc->encountered_congestion = 1;
		kfree(iov);
		return 0;
	}

	xid = GetXid();

	pagevec_init(&pvec, 0);
	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = 1;
		scanned = 1;
	}
retry:
	while (!done && (index <= end) &&
	       (nr_pages = pagevec_lookup_tag(&pvec, mapping, &index,
			PAGECACHE_TAG_DIRTY,
			min(end - index, (pgoff_t)PAGEVEC_SIZE - 1) + 1))) {
		int first;
		unsigned int i;

		first = -1;
		next = 0;
		n_iov = 0;
		bytes_to_write = 0;

		for (i = 0; i < nr_pages; i++) {
			page = pvec.pages[i];
			/*
			 * At this point we hold neither mapping->tree_lock nor
			 * lock on the page itself: the page may be truncated or
			 * invalidated (changing page->mapping to NULL), or even
			 * swizzled back from swapper_space to tmpfs file
			 * mapping
			 */

			if (first < 0)
				lock_page(page);
			else if (!trylock_page(page))
				break;

			if (unlikely(page->mapping != mapping)) {
				unlock_page(page);
				break;
			}

			if (!wbc->range_cyclic && page->index > end) {
				done = 1;
				unlock_page(page);
				break;
			}

			if (next && (page->index != next)) {
				/* Not next consecutive page */
				unlock_page(page);
				break;
			}

			if (wbc->sync_mode != WB_SYNC_NONE)
				wait_on_page_writeback(page);

			if (PageWriteback(page) ||
					!clear_page_dirty_for_io(page)) {
				unlock_page(page);
				break;
			}

			/*
			 * This actually clears the dirty bit in the radix tree.
			 * See cifs_writepage() for more commentary.
			 */
			set_page_writeback(page);

			if (page_offset(page) >= mapping->host->i_size) {
				done = 1;
				unlock_page(page);
				end_page_writeback(page);
				break;
			}

			/*
			 * BB can we get rid of this?  pages are held by pvec
			 */
			page_cache_get(page);

			len = min(mapping->host->i_size - page_offset(page),
				  (loff_t)PAGE_CACHE_SIZE);

			/* reserve iov[0] for the smb header */
			n_iov++;
			iov[n_iov].iov_base = kmap(page);
			iov[n_iov].iov_len = len;
			bytes_to_write += len;

			if (first < 0) {
				first = i;
				offset = page_offset(page);
			}
			next = page->index + 1;
			if (bytes_to_write + PAGE_CACHE_SIZE > cifs_sb->wsize)
				break;
		}
		if (n_iov) {
			/* Search for a writable handle every time we call
			 * CIFSSMBWrite2.  We can't rely on the last handle
			 * we used to still be valid
			 */
			open_file = find_writable_file(CIFS_I(mapping->host));
			if (!open_file) {
				cERROR(1, ("No writable handles for inode"));
				rc = -EBADF;
			} else {
				long_op = cifs_write_timeout(cifsi, offset);
				rc = CIFSSMBWrite2(xid, cifs_sb->tcon,
						   open_file->netfid,
						   bytes_to_write, offset,
						   &bytes_written, iov, n_iov,
						   long_op);
				atomic_dec(&open_file->wrtPending);
				cifs_update_eof(cifsi, offset, bytes_written);

				if (rc || bytes_written < bytes_to_write) {
					cERROR(1, ("Write2 ret %d, wrote %d",
						  rc, bytes_written));
					/* BB what if continued retry is
					   requested via mount flags? */
					if (rc == -ENOSPC)
						set_bit(AS_ENOSPC, &mapping->flags);
					else
						set_bit(AS_EIO, &mapping->flags);
				} else {
					cifs_stats_bytes_written(cifs_sb->tcon,
								 bytes_written);
				}
			}
			for (i = 0; i < n_iov; i++) {
				page = pvec.pages[first + i];
				/* Should we also set page error on
				success rc but too little data written? */
				/* BB investigate retry logic on temporary
				server crash cases and how recovery works
				when page marked as error */
				if (rc)
					SetPageError(page);
				kunmap(page);
				unlock_page(page);
				end_page_writeback(page);
				page_cache_release(page);
			}
			if ((wbc->nr_to_write -= n_iov) <= 0)
				done = 1;
			index = next;
		} else
			/* Need to re-find the pages we skipped */
			index = pvec.pages[0]->index + 1;

		pagevec_release(&pvec);
	}
	if (!scanned && !done) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = 1;
		index = 0;
		goto retry;
	}
	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = index;

	FreeXid(xid);
	kfree(iov);
	return rc;
}

static int cifs_writepage(struct page *page, struct writeback_control *wbc)
{
	int rc = -EFAULT;
	int xid;

	xid = GetXid();
/* BB add check for wbc flags */
	page_cache_get(page);
	if (!PageUptodate(page))
		cFYI(1, ("ppw - page not up to date"));

	/*
	 * Set the "writeback" flag, and clear "dirty" in the radix tree.
	 *
	 * A writepage() implementation always needs to do either this,
	 * or re-dirty the page with "redirty_page_for_writepage()" in
	 * the case of a failure.
	 *
	 * Just unlocking the page will cause the radix tree tag-bits
	 * to fail to update with the state of the page correctly.
	 */
	set_page_writeback(page);
	rc = cifs_partialpagewrite(page, 0, PAGE_CACHE_SIZE);
	SetPageUptodate(page); /* BB add check for error and Clearuptodate? */
	unlock_page(page);
	end_page_writeback(page);
	page_cache_release(page);
	FreeXid(xid);
	return rc;
}

static int cifs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	int rc;
	struct inode *inode = mapping->host;

	cFYI(1, ("write_end for page %p from pos %lld with %d bytes",
		 page, pos, copied));

	if (PageChecked(page)) {
		if (copied == len)
			SetPageUptodate(page);
		ClearPageChecked(page);
	} else if (!PageUptodate(page) && copied == PAGE_CACHE_SIZE)
		SetPageUptodate(page);

	if (!PageUptodate(page)) {
		char *page_data;
		unsigned offset = pos & (PAGE_CACHE_SIZE - 1);
		int xid;

		xid = GetXid();
		/* this is probably better than directly calling
		   partialpage_write since in this function the file handle is
		   known which we might as well	leverage */
		/* BB check if anything else missing out of ppw
		   such as updating last write time */
		page_data = kmap(page);
		rc = cifs_write(file, page_data + offset, copied, &pos);
		/* if (rc < 0) should we set writebehind rc? */
		kunmap(page);

		FreeXid(xid);
	} else {
		rc = copied;
		pos += copied;
		set_page_dirty(page);
	}

	if (rc > 0) {
		spin_lock(&inode->i_lock);
		if (pos > inode->i_size)
			i_size_write(inode, pos);
		spin_unlock(&inode->i_lock);
	}

	unlock_page(page);
	page_cache_release(page);

	return rc;
}

int cifs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	int xid;
	int rc = 0;
	struct cifsTconInfo *tcon;
	struct cifsFileInfo *smbfile =
		(struct cifsFileInfo *)file->private_data;
	struct inode *inode = file->f_path.dentry->d_inode;

	xid = GetXid();

	cFYI(1, ("Sync file - name: %s datasync: 0x%x",
		dentry->d_name.name, datasync));

	rc = filemap_write_and_wait(inode->i_mapping);
	if (rc == 0) {
		rc = CIFS_I(inode)->write_behind_rc;
		CIFS_I(inode)->write_behind_rc = 0;
		tcon = CIFS_SB(inode->i_sb)->tcon;
		if (!rc && tcon && smbfile &&
		   !(CIFS_SB(inode->i_sb)->mnt_cifs_flags & CIFS_MOUNT_NOSSYNC))
			rc = CIFSSMBFlush(xid, tcon, smbfile->netfid);
	}

	FreeXid(xid);
	return rc;
}

/* static void cifs_sync_page(struct page *page)
{
	struct address_space *mapping;
	struct inode *inode;
	unsigned long index = page->index;
	unsigned int rpages = 0;
	int rc = 0;

	cFYI(1, ("sync page %p",page));
	mapping = page->mapping;
	if (!mapping)
		return 0;
	inode = mapping->host;
	if (!inode)
		return; */

/*	fill in rpages then
	result = cifs_pagein_inode(inode, index, rpages); */ /* BB finish */

/*	cFYI(1, ("rpages is %d for sync page of Index %ld", rpages, index));

#if 0
	if (rc < 0)
		return rc;
	return 0;
#endif
} */

/*
 * As file closes, flush all cached write data for this inode checking
 * for write behind errors.
 */
int cifs_flush(struct file *file, fl_owner_t id)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	int rc = 0;

	/* Rather than do the steps manually:
	   lock the inode for writing
	   loop through pages looking for write behind data (dirty pages)
	   coalesce into contiguous 16K (or smaller) chunks to write to server
	   send to server (prefer in parallel)
	   deal with writebehind errors
	   unlock inode for writing
	   filemapfdatawrite appears easier for the time being */

	rc = filemap_fdatawrite(inode->i_mapping);
	/* reset wb rc if we were able to write out dirty pages */
	if (!rc) {
		rc = CIFS_I(inode)->write_behind_rc;
		CIFS_I(inode)->write_behind_rc = 0;
	}

	cFYI(1, ("Flush inode %p file %p rc %d", inode, file, rc));

	return rc;
}

ssize_t cifs_user_read(struct file *file, char __user *read_data,
	size_t read_size, loff_t *poffset)
{
	int rc = -EACCES;
	unsigned int bytes_read = 0;
	unsigned int total_read = 0;
	unsigned int current_read_size;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid;
	struct cifsFileInfo *open_file;
	char *smb_read_data;
	char __user *current_offset;
	struct smb_com_read_rsp *pSMBr;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;

	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		cFYI(1, ("attempting read on write only file instance"));

	for (total_read = 0, current_offset = read_data;
	     read_size > total_read;
	     total_read += bytes_read, current_offset += bytes_read) {
		current_read_size = min_t(const int, read_size - total_read,
					  cifs_sb->rsize);
		rc = -EAGAIN;
		smb_read_data = NULL;
		while (rc == -EAGAIN) {
			int buf_type = CIFS_NO_BUFFER;
			if ((open_file->invalidHandle) &&
			    (!open_file->closePend)) {
				rc = cifs_reopen_file(file, true);
				if (rc != 0)
					break;
			}
			rc = CIFSSMBRead(xid, pTcon,
					 open_file->netfid,
					 current_read_size, *poffset,
					 &bytes_read, &smb_read_data,
					 &buf_type);
			pSMBr = (struct smb_com_read_rsp *)smb_read_data;
			if (smb_read_data) {
				if (copy_to_user(current_offset,
						smb_read_data +
						4 /* RFC1001 length field */ +
						le16_to_cpu(pSMBr->DataOffset),
						bytes_read))
					rc = -EFAULT;

				if (buf_type == CIFS_SMALL_BUFFER)
					cifs_small_buf_release(smb_read_data);
				else if (buf_type == CIFS_LARGE_BUFFER)
					cifs_buf_release(smb_read_data);
				smb_read_data = NULL;
			}
		}
		if (rc || (bytes_read == 0)) {
			if (total_read) {
				break;
			} else {
				FreeXid(xid);
				return rc;
			}
		} else {
			cifs_stats_bytes_read(pTcon, bytes_read);
			*poffset += bytes_read;
		}
	}
	FreeXid(xid);
	return total_read;
}


static ssize_t cifs_read(struct file *file, char *read_data, size_t read_size,
	loff_t *poffset)
{
	int rc = -EACCES;
	unsigned int bytes_read = 0;
	unsigned int total_read;
	unsigned int current_read_size;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int xid;
	char *current_offset;
	struct cifsFileInfo *open_file;
	int buf_type = CIFS_NO_BUFFER;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	pTcon = cifs_sb->tcon;

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;

	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		cFYI(1, ("attempting read on write only file instance"));

	for (total_read = 0, current_offset = read_data;
	     read_size > total_read;
	     total_read += bytes_read, current_offset += bytes_read) {
		current_read_size = min_t(const int, read_size - total_read,
					  cifs_sb->rsize);
		/* For windows me and 9x we do not want to request more
		than it negotiated since it will refuse the read then */
		if ((pTcon->ses) &&
			!(pTcon->ses->capabilities & CAP_LARGE_FILES)) {
			current_read_size = min_t(const int, current_read_size,
					pTcon->ses->server->maxBuf - 128);
		}
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			if ((open_file->invalidHandle) &&
			    (!open_file->closePend)) {
				rc = cifs_reopen_file(file, true);
				if (rc != 0)
					break;
			}
			rc = CIFSSMBRead(xid, pTcon,
					 open_file->netfid,
					 current_read_size, *poffset,
					 &bytes_read, &current_offset,
					 &buf_type);
		}
		if (rc || (bytes_read == 0)) {
			if (total_read) {
				break;
			} else {
				FreeXid(xid);
				return rc;
			}
		} else {
			cifs_stats_bytes_read(pTcon, total_read);
			*poffset += bytes_read;
		}
	}
	FreeXid(xid);
	return total_read;
}

int cifs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct dentry *dentry = file->f_path.dentry;
	int rc, xid;

	xid = GetXid();
	rc = cifs_revalidate(dentry);
	if (rc) {
		cFYI(1, ("Validation prior to mmap failed, error=%d", rc));
		FreeXid(xid);
		return rc;
	}
	rc = generic_file_mmap(file, vma);
	FreeXid(xid);
	return rc;
}


static void cifs_copy_cache_pages(struct address_space *mapping,
	struct list_head *pages, int bytes_read, char *data,
	struct pagevec *plru_pvec)
{
	struct page *page;
	char *target;

	while (bytes_read > 0) {
		if (list_empty(pages))
			break;

		page = list_entry(pages->prev, struct page, lru);
		list_del(&page->lru);

		if (add_to_page_cache(page, mapping, page->index,
				      GFP_KERNEL)) {
			page_cache_release(page);
			cFYI(1, ("Add page cache failed"));
			data += PAGE_CACHE_SIZE;
			bytes_read -= PAGE_CACHE_SIZE;
			continue;
		}

		target = kmap_atomic(page, KM_USER0);

		if (PAGE_CACHE_SIZE > bytes_read) {
			memcpy(target, data, bytes_read);
			/* zero the tail end of this partial page */
			memset(target + bytes_read, 0,
			       PAGE_CACHE_SIZE - bytes_read);
			bytes_read = 0;
		} else {
			memcpy(target, data, PAGE_CACHE_SIZE);
			bytes_read -= PAGE_CACHE_SIZE;
		}
		kunmap_atomic(target, KM_USER0);

		flush_dcache_page(page);
		SetPageUptodate(page);
		unlock_page(page);
		if (!pagevec_add(plru_pvec, page))
			__pagevec_lru_add_file(plru_pvec);
		data += PAGE_CACHE_SIZE;
	}
	return;
}

static int cifs_readpages(struct file *file, struct address_space *mapping,
	struct list_head *page_list, unsigned num_pages)
{
	int rc = -EACCES;
	int xid;
	loff_t offset;
	struct page *page;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	unsigned int bytes_read = 0;
	unsigned int read_size, i;
	char *smb_read_data = NULL;
	struct smb_com_read_rsp *pSMBr;
	struct pagevec lru_pvec;
	struct cifsFileInfo *open_file;
	int buf_type = CIFS_NO_BUFFER;

	xid = GetXid();
	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}
	open_file = (struct cifsFileInfo *)file->private_data;
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	pTcon = cifs_sb->tcon;

	pagevec_init(&lru_pvec, 0);
	cFYI(DBG2, ("rpages: num pages %d", num_pages));
	for (i = 0; i < num_pages; ) {
		unsigned contig_pages;
		struct page *tmp_page;
		unsigned long expected_index;

		if (list_empty(page_list))
			break;

		page = list_entry(page_list->prev, struct page, lru);
		offset = (loff_t)page->index << PAGE_CACHE_SHIFT;

		/* count adjacent pages that we will read into */
		contig_pages = 0;
		expected_index =
			list_entry(page_list->prev, struct page, lru)->index;
		list_for_each_entry_reverse(tmp_page, page_list, lru) {
			if (tmp_page->index == expected_index) {
				contig_pages++;
				expected_index++;
			} else
				break;
		}
		if (contig_pages + i >  num_pages)
			contig_pages = num_pages - i;

		/* for reads over a certain size could initiate async
		   read ahead */

		read_size = contig_pages * PAGE_CACHE_SIZE;
		/* Read size needs to be in multiples of one page */
		read_size = min_t(const unsigned int, read_size,
				  cifs_sb->rsize & PAGE_CACHE_MASK);
		cFYI(DBG2, ("rpages: read size 0x%x  contiguous pages %d",
				read_size, contig_pages));
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			if ((open_file->invalidHandle) &&
			    (!open_file->closePend)) {
				rc = cifs_reopen_file(file, true);
				if (rc != 0)
					break;
			}

			rc = CIFSSMBRead(xid, pTcon,
					 open_file->netfid,
					 read_size, offset,
					 &bytes_read, &smb_read_data,
					 &buf_type);
			/* BB more RC checks ? */
			if (rc == -EAGAIN) {
				if (smb_read_data) {
					if (buf_type == CIFS_SMALL_BUFFER)
						cifs_small_buf_release(smb_read_data);
					else if (buf_type == CIFS_LARGE_BUFFER)
						cifs_buf_release(smb_read_data);
					smb_read_data = NULL;
				}
			}
		}
		if ((rc < 0) || (smb_read_data == NULL)) {
			cFYI(1, ("Read error in readpages: %d", rc));
			break;
		} else if (bytes_read > 0) {
			task_io_account_read(bytes_read);
			pSMBr = (struct smb_com_read_rsp *)smb_read_data;
			cifs_copy_cache_pages(mapping, page_list, bytes_read,
				smb_read_data + 4 /* RFC1001 hdr */ +
				le16_to_cpu(pSMBr->DataOffset), &lru_pvec);

			i +=  bytes_read >> PAGE_CACHE_SHIFT;
			cifs_stats_bytes_read(pTcon, bytes_read);
			if ((bytes_read & PAGE_CACHE_MASK) != bytes_read) {
				i++; /* account for partial page */

				/* server copy of file can have smaller size
				   than client */
				/* BB do we need to verify this common case ?
				   this case is ok - if we are at server EOF
				   we will hit it on next read */

				/* break; */
			}
		} else {
			cFYI(1, ("No bytes read (%d) at offset %lld . "
				 "Cleaning remaining pages from readahead list",
				 bytes_read, offset));
			/* BB turn off caching and do new lookup on
			   file size at server? */
			break;
		}
		if (smb_read_data) {
			if (buf_type == CIFS_SMALL_BUFFER)
				cifs_small_buf_release(smb_read_data);
			else if (buf_type == CIFS_LARGE_BUFFER)
				cifs_buf_release(smb_read_data);
			smb_read_data = NULL;
		}
		bytes_read = 0;
	}

	pagevec_lru_add_file(&lru_pvec);

/* need to free smb_read_data buf before exit */
	if (smb_read_data) {
		if (buf_type == CIFS_SMALL_BUFFER)
			cifs_small_buf_release(smb_read_data);
		else if (buf_type == CIFS_LARGE_BUFFER)
			cifs_buf_release(smb_read_data);
		smb_read_data = NULL;
	}

	FreeXid(xid);
	return rc;
}

static int cifs_readpage_worker(struct file *file, struct page *page,
	loff_t *poffset)
{
	char *read_data;
	int rc;

	page_cache_get(page);
	read_data = kmap(page);
	/* for reads over a certain size could initiate async read ahead */

	rc = cifs_read(file, read_data, PAGE_CACHE_SIZE, poffset);

	if (rc < 0)
		goto io_error;
	else
		cFYI(1, ("Bytes read %d", rc));

	file->f_path.dentry->d_inode->i_atime =
		current_fs_time(file->f_path.dentry->d_inode->i_sb);

	if (PAGE_CACHE_SIZE > rc)
		memset(read_data + rc, 0, PAGE_CACHE_SIZE - rc);

	flush_dcache_page(page);
	SetPageUptodate(page);
	rc = 0;

io_error:
	kunmap(page);
	page_cache_release(page);
	return rc;
}

static int cifs_readpage(struct file *file, struct page *page)
{
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	int rc = -EACCES;
	int xid;

	xid = GetXid();

	if (file->private_data == NULL) {
		FreeXid(xid);
		return -EBADF;
	}

	cFYI(1, ("readpage %p at offset %d 0x%x\n",
		 page, (int)offset, (int)offset));

	rc = cifs_readpage_worker(file, page, &offset);

	unlock_page(page);

	FreeXid(xid);
	return rc;
}

static int is_inode_writable(struct cifsInodeInfo *cifs_inode)
{
	struct cifsFileInfo *open_file;

	read_lock(&GlobalSMBSeslock);
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (open_file->closePend)
			continue;
		if (open_file->pfile &&
		    ((open_file->pfile->f_flags & O_RDWR) ||
		     (open_file->pfile->f_flags & O_WRONLY))) {
			read_unlock(&GlobalSMBSeslock);
			return 1;
		}
	}
	read_unlock(&GlobalSMBSeslock);
	return 0;
}

/* We do not want to update the file size from server for inodes
   open for write - to avoid races with writepage extending
   the file - in the future we could consider allowing
   refreshing the inode only on increases in the file size
   but this is tricky to do without racing with writebehind
   page caching in the current Linux kernel design */
bool is_size_safe_to_change(struct cifsInodeInfo *cifsInode, __u64 end_of_file)
{
	if (!cifsInode)
		return true;

	if (is_inode_writable(cifsInode)) {
		/* This inode is open for write at least once */
		struct cifs_sb_info *cifs_sb;

		cifs_sb = CIFS_SB(cifsInode->vfs_inode.i_sb);
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO) {
			/* since no page cache to corrupt on directio
			we can change size safely */
			return true;
		}

		if (i_size_read(&cifsInode->vfs_inode) < end_of_file)
			return true;

		return false;
	} else
		return true;
}

static int cifs_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
{
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	loff_t offset = pos & (PAGE_CACHE_SIZE - 1);
	loff_t page_start = pos & PAGE_MASK;
	loff_t i_size;
	struct page *page;
	int rc = 0;

	cFYI(1, ("write_begin from %lld len %d", (long long)pos, len));

	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page) {
		rc = -ENOMEM;
		goto out;
	}

	if (PageUptodate(page))
		goto out;

	/*
	 * If we write a full page it will be up to date, no need to read from
	 * the server. If the write is short, we'll end up doing a sync write
	 * instead.
	 */
	if (len == PAGE_CACHE_SIZE)
		goto out;

	/*
	 * optimize away the read when we have an oplock, and we're not
	 * expecting to use any of the data we'd be reading in. That
	 * is, when the page lies beyond the EOF, or straddles the EOF
	 * and the write will cover all of the existing data.
	 */
	if (CIFS_I(mapping->host)->clientCanCacheRead) {
		i_size = i_size_read(mapping->host);
		if (page_start >= i_size ||
		    (offset == 0 && (pos + len) >= i_size)) {
			zero_user_segments(page, 0, offset,
					   offset + len,
					   PAGE_CACHE_SIZE);
			/*
			 * PageChecked means that the parts of the page
			 * to which we're not writing are considered up
			 * to date. Once the data is copied to the
			 * page, it can be set uptodate.
			 */
			SetPageChecked(page);
			goto out;
		}
	}

	if ((file->f_flags & O_ACCMODE) != O_WRONLY) {
		/*
		 * might as well read a page, it is fast enough. If we get
		 * an error, we don't need to return it. cifs_write_end will
		 * do a sync write instead since PG_uptodate isn't set.
		 */
		cifs_readpage_worker(file, page, &page_start);
	} else {
		/* we could try using another file handle if there is one -
		   but how would we lock it to prevent close of that handle
		   racing with this read? In any case
		   this will be written out by write_end so is fine */
	}
out:
	*pagep = page;
	return rc;
}

const struct address_space_operations cifs_addr_ops = {
	.readpage = cifs_readpage,
	.readpages = cifs_readpages,
	.writepage = cifs_writepage,
	.writepages = cifs_writepages,
	.write_begin = cifs_write_begin,
	.write_end = cifs_write_end,
	.set_page_dirty = __set_page_dirty_nobuffers,
	/* .sync_page = cifs_sync_page, */
	/* .direct_IO = */
};

/*
 * cifs_readpages requires the server to support a buffer large enough to
 * contain the header plus one complete page of data.  Otherwise, we need
 * to leave cifs_readpages out of the address space operations.
 */
const struct address_space_operations cifs_addr_ops_smallbuf = {
	.readpage = cifs_readpage,
	.writepage = cifs_writepage,
	.writepages = cifs_writepages,
	.write_begin = cifs_write_begin,
	.write_end = cifs_write_end,
	.set_page_dirty = __set_page_dirty_nobuffers,
	/* .sync_page = cifs_sync_page, */
	/* .direct_IO = */
};
