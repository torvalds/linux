/*
 *   fs/cifs/file.c
 *
 *   vfs operations that deal with files
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2010
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
#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <asm/div64.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "fscache.h"

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

static u32 cifs_posix_convert_flags(unsigned int flags)
{
	u32 posix_flags = 0;

	if ((flags & O_ACCMODE) == O_RDONLY)
		posix_flags = SMB_O_RDONLY;
	else if ((flags & O_ACCMODE) == O_WRONLY)
		posix_flags = SMB_O_WRONLY;
	else if ((flags & O_ACCMODE) == O_RDWR)
		posix_flags = SMB_O_RDWR;

	if (flags & O_CREAT)
		posix_flags |= SMB_O_CREAT;
	if (flags & O_EXCL)
		posix_flags |= SMB_O_EXCL;
	if (flags & O_TRUNC)
		posix_flags |= SMB_O_TRUNC;
	/* be safe and imply O_SYNC for O_DSYNC */
	if (flags & O_DSYNC)
		posix_flags |= SMB_O_SYNC;
	if (flags & O_DIRECTORY)
		posix_flags |= SMB_O_DIRECTORY;
	if (flags & O_NOFOLLOW)
		posix_flags |= SMB_O_NOFOLLOW;
	if (flags & O_DIRECT)
		posix_flags |= SMB_O_DIRECT;

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

int cifs_posix_open(char *full_path, struct inode **pinode,
			struct super_block *sb, int mode, unsigned int f_flags,
			__u32 *poplock, __u16 *pnetfid, int xid)
{
	int rc;
	FILE_UNIX_BASIC_INFO *presp_data;
	__u32 posix_flags = 0;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_fattr fattr;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;

	cFYI(1, "posix open %s", full_path);

	presp_data = kzalloc(sizeof(FILE_UNIX_BASIC_INFO), GFP_KERNEL);
	if (presp_data == NULL)
		return -ENOMEM;

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		rc = PTR_ERR(tlink);
		goto posix_open_ret;
	}

	tcon = tlink_tcon(tlink);
	mode &= ~current_umask();

	posix_flags = cifs_posix_convert_flags(f_flags);
	rc = CIFSPOSIXCreate(xid, tcon, posix_flags, mode, pnetfid, presp_data,
			     poplock, full_path, cifs_sb->local_nls,
			     cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
	cifs_put_tlink(tlink);

	if (rc)
		goto posix_open_ret;

	if (presp_data->Type == cpu_to_le32(-1))
		goto posix_open_ret; /* open ok, caller does qpathinfo */

	if (!pinode)
		goto posix_open_ret; /* caller does not need info */

	cifs_unix_basic_to_fattr(&fattr, presp_data, cifs_sb);

	/* get new inode and set it up */
	if (*pinode == NULL) {
		cifs_fill_uniqueid(sb, &fattr);
		*pinode = cifs_iget(sb, &fattr);
		if (!*pinode) {
			rc = -ENOMEM;
			goto posix_open_ret;
		}
	} else {
		cifs_fattr_to_inode(*pinode, &fattr);
	}

posix_open_ret:
	kfree(presp_data);
	return rc;
}

static int
cifs_nt_open(char *full_path, struct inode *inode, struct cifs_sb_info *cifs_sb,
	     struct cifs_tcon *tcon, unsigned int f_flags, __u32 *poplock,
	     __u16 *pnetfid, int xid)
{
	int rc;
	int desiredAccess;
	int disposition;
	int create_options = CREATE_NOT_DIR;
	FILE_ALL_INFO *buf;

	desiredAccess = cifs_convert_flags(f_flags);

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

	disposition = cifs_get_disposition(f_flags);

	/* BB pass O_SYNC flag through on file attributes .. BB */

	buf = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (backup_cred(cifs_sb))
		create_options |= CREATE_OPEN_BACKUP_INTENT;

	if (tcon->ses->capabilities & CAP_NT_SMBS)
		rc = CIFSSMBOpen(xid, tcon, full_path, disposition,
			 desiredAccess, create_options, pnetfid, poplock, buf,
			 cifs_sb->local_nls, cifs_sb->mnt_cifs_flags
				 & CIFS_MOUNT_MAP_SPECIAL_CHR);
	else
		rc = SMBLegacyOpen(xid, tcon, full_path, disposition,
			desiredAccess, CREATE_NOT_DIR, pnetfid, poplock, buf,
			cifs_sb->local_nls, cifs_sb->mnt_cifs_flags
				& CIFS_MOUNT_MAP_SPECIAL_CHR);

	if (rc)
		goto out;

	if (tcon->unix_ext)
		rc = cifs_get_inode_info_unix(&inode, full_path, inode->i_sb,
					      xid);
	else
		rc = cifs_get_inode_info(&inode, full_path, buf, inode->i_sb,
					 xid, pnetfid);

out:
	kfree(buf);
	return rc;
}

struct cifsFileInfo *
cifs_new_fileinfo(__u16 fileHandle, struct file *file,
		  struct tcon_link *tlink, __u32 oplock)
{
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	struct cifsInodeInfo *pCifsInode = CIFS_I(inode);
	struct cifsFileInfo *pCifsFile;

	pCifsFile = kzalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
	if (pCifsFile == NULL)
		return pCifsFile;

	pCifsFile->count = 1;
	pCifsFile->netfid = fileHandle;
	pCifsFile->pid = current->tgid;
	pCifsFile->uid = current_fsuid();
	pCifsFile->dentry = dget(dentry);
	pCifsFile->f_flags = file->f_flags;
	pCifsFile->invalidHandle = false;
	pCifsFile->tlink = cifs_get_tlink(tlink);
	mutex_init(&pCifsFile->fh_mutex);
	INIT_WORK(&pCifsFile->oplock_break, cifs_oplock_break);

	spin_lock(&cifs_file_list_lock);
	list_add(&pCifsFile->tlist, &(tlink_tcon(tlink)->openFileList));
	/* if readable file instance put first in list*/
	if (file->f_mode & FMODE_READ)
		list_add(&pCifsFile->flist, &pCifsInode->openFileList);
	else
		list_add_tail(&pCifsFile->flist, &pCifsInode->openFileList);
	spin_unlock(&cifs_file_list_lock);

	cifs_set_oplock_level(pCifsInode, oplock);
	pCifsInode->can_cache_brlcks = pCifsInode->clientCanCacheAll;

	file->private_data = pCifsFile;
	return pCifsFile;
}

static void cifs_del_lock_waiters(struct cifsLockInfo *lock);

/*
 * Release a reference on the file private data. This may involve closing
 * the filehandle out on the server. Must be called without holding
 * cifs_file_list_lock.
 */
void cifsFileInfo_put(struct cifsFileInfo *cifs_file)
{
	struct inode *inode = cifs_file->dentry->d_inode;
	struct cifs_tcon *tcon = tlink_tcon(cifs_file->tlink);
	struct cifsInodeInfo *cifsi = CIFS_I(inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifsLockInfo *li, *tmp;

	spin_lock(&cifs_file_list_lock);
	if (--cifs_file->count > 0) {
		spin_unlock(&cifs_file_list_lock);
		return;
	}

	/* remove it from the lists */
	list_del(&cifs_file->flist);
	list_del(&cifs_file->tlist);

	if (list_empty(&cifsi->openFileList)) {
		cFYI(1, "closing last open instance for inode %p",
			cifs_file->dentry->d_inode);

		/* in strict cache mode we need invalidate mapping on the last
		   close  because it may cause a error when we open this file
		   again and get at least level II oplock */
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_STRICT_IO)
			CIFS_I(inode)->invalid_mapping = true;

		cifs_set_oplock_level(cifsi, 0);
	}
	spin_unlock(&cifs_file_list_lock);

	cancel_work_sync(&cifs_file->oplock_break);

	if (!tcon->need_reconnect && !cifs_file->invalidHandle) {
		int xid, rc;

		xid = GetXid();
		rc = CIFSSMBClose(xid, tcon, cifs_file->netfid);
		FreeXid(xid);
	}

	/* Delete any outstanding lock records. We'll lose them when the file
	 * is closed anyway.
	 */
	mutex_lock(&cifsi->lock_mutex);
	list_for_each_entry_safe(li, tmp, &cifsi->llist, llist) {
		if (li->netfid != cifs_file->netfid)
			continue;
		list_del(&li->llist);
		cifs_del_lock_waiters(li);
		kfree(li);
	}
	mutex_unlock(&cifsi->lock_mutex);

	cifs_put_tlink(cifs_file->tlink);
	dput(cifs_file->dentry);
	kfree(cifs_file);
}

int cifs_open(struct inode *inode, struct file *file)
{
	int rc = -EACCES;
	int xid;
	__u32 oplock;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct tcon_link *tlink;
	struct cifsFileInfo *pCifsFile = NULL;
	char *full_path = NULL;
	bool posix_open_ok = false;
	__u16 netfid;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink)) {
		FreeXid(xid);
		return PTR_ERR(tlink);
	}
	tcon = tlink_tcon(tlink);

	full_path = build_path_from_dentry(file->f_path.dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	cFYI(1, "inode = 0x%p file flags are 0x%x for %s",
		 inode, file->f_flags, full_path);

	if (tcon->ses->server->oplocks)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;

	if (!tcon->broken_posix_open && tcon->unix_ext &&
	    (tcon->ses->capabilities & CAP_UNIX) &&
	    (CIFS_UNIX_POSIX_PATH_OPS_CAP &
			le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		/* can not refresh inode info since size could be stale */
		rc = cifs_posix_open(full_path, &inode, inode->i_sb,
				cifs_sb->mnt_file_mode /* ignored */,
				file->f_flags, &oplock, &netfid, xid);
		if (rc == 0) {
			cFYI(1, "posix open succeeded");
			posix_open_ok = true;
		} else if ((rc == -EINVAL) || (rc == -EOPNOTSUPP)) {
			if (tcon->ses->serverNOS)
				cERROR(1, "server %s of type %s returned"
					   " unexpected error on SMB posix open"
					   ", disabling posix open support."
					   " Check if server update available.",
					   tcon->ses->serverName,
					   tcon->ses->serverNOS);
			tcon->broken_posix_open = true;
		} else if ((rc != -EIO) && (rc != -EREMOTE) &&
			 (rc != -EOPNOTSUPP)) /* path not found or net err */
			goto out;
		/* else fallthrough to retry open the old way on network i/o
		   or DFS errors */
	}

	if (!posix_open_ok) {
		rc = cifs_nt_open(full_path, inode, cifs_sb, tcon,
				  file->f_flags, &oplock, &netfid, xid);
		if (rc)
			goto out;
	}

	pCifsFile = cifs_new_fileinfo(netfid, file, tlink, oplock);
	if (pCifsFile == NULL) {
		CIFSSMBClose(xid, tcon, netfid);
		rc = -ENOMEM;
		goto out;
	}

	cifs_fscache_set_inode_cookie(inode, file);

	if ((oplock & CIFS_CREATE_ACTION) && !posix_open_ok && tcon->unix_ext) {
		/* time to set mode which we can not set earlier due to
		   problems creating new read-only files */
		struct cifs_unix_set_info_args args = {
			.mode	= inode->i_mode,
			.uid	= NO_CHANGE_64,
			.gid	= NO_CHANGE_64,
			.ctime	= NO_CHANGE_64,
			.atime	= NO_CHANGE_64,
			.mtime	= NO_CHANGE_64,
			.device	= 0,
		};
		CIFSSMBUnixSetFileInfo(xid, tcon, &args, netfid,
					pCifsFile->pid);
	}

out:
	kfree(full_path);
	FreeXid(xid);
	cifs_put_tlink(tlink);
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

static int cifs_reopen_file(struct cifsFileInfo *pCifsFile, bool can_flush)
{
	int rc = -EACCES;
	int xid;
	__u32 oplock;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct cifsInodeInfo *pCifsInode;
	struct inode *inode;
	char *full_path = NULL;
	int desiredAccess;
	int disposition = FILE_OPEN;
	int create_options = CREATE_NOT_DIR;
	__u16 netfid;

	xid = GetXid();
	mutex_lock(&pCifsFile->fh_mutex);
	if (!pCifsFile->invalidHandle) {
		mutex_unlock(&pCifsFile->fh_mutex);
		rc = 0;
		FreeXid(xid);
		return rc;
	}

	inode = pCifsFile->dentry->d_inode;
	cifs_sb = CIFS_SB(inode->i_sb);
	tcon = tlink_tcon(pCifsFile->tlink);

/* can not grab rename sem here because various ops, including
   those that already have the rename sem can end up causing writepage
   to get called and if the server was down that means we end up here,
   and we can never tell if the caller already has the rename_sem */
	full_path = build_path_from_dentry(pCifsFile->dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		mutex_unlock(&pCifsFile->fh_mutex);
		FreeXid(xid);
		return rc;
	}

	cFYI(1, "inode = 0x%p file flags 0x%x for %s",
		 inode, pCifsFile->f_flags, full_path);

	if (tcon->ses->server->oplocks)
		oplock = REQ_OPLOCK;
	else
		oplock = 0;

	if (tcon->unix_ext && (tcon->ses->capabilities & CAP_UNIX) &&
	    (CIFS_UNIX_POSIX_PATH_OPS_CAP &
			le64_to_cpu(tcon->fsUnixInfo.Capability))) {

		/*
		 * O_CREAT, O_EXCL and O_TRUNC already had their effect on the
		 * original open. Must mask them off for a reopen.
		 */
		unsigned int oflags = pCifsFile->f_flags &
						~(O_CREAT | O_EXCL | O_TRUNC);

		rc = cifs_posix_open(full_path, NULL, inode->i_sb,
				cifs_sb->mnt_file_mode /* ignored */,
				oflags, &oplock, &netfid, xid);
		if (rc == 0) {
			cFYI(1, "posix reopen succeeded");
			goto reopen_success;
		}
		/* fallthrough to retry open the old way on errors, especially
		   in the reconnect path it is important to retry hard */
	}

	desiredAccess = cifs_convert_flags(pCifsFile->f_flags);

	if (backup_cred(cifs_sb))
		create_options |= CREATE_OPEN_BACKUP_INTENT;

	/* Can not refresh inode by passing in file_info buf to be returned
	   by SMBOpen and then calling get_inode_info with returned buf
	   since file might have write behind data that needs to be flushed
	   and server version of file size can be stale. If we knew for sure
	   that inode was not dirty locally we could do this */

	rc = CIFSSMBOpen(xid, tcon, full_path, disposition, desiredAccess,
			 create_options, &netfid, &oplock, NULL,
			 cifs_sb->local_nls, cifs_sb->mnt_cifs_flags &
				CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc) {
		mutex_unlock(&pCifsFile->fh_mutex);
		cFYI(1, "cifs_open returned 0x%x", rc);
		cFYI(1, "oplock: %d", oplock);
		goto reopen_error_exit;
	}

reopen_success:
	pCifsFile->netfid = netfid;
	pCifsFile->invalidHandle = false;
	mutex_unlock(&pCifsFile->fh_mutex);
	pCifsInode = CIFS_I(inode);

	if (can_flush) {
		rc = filemap_write_and_wait(inode->i_mapping);
		mapping_set_error(inode->i_mapping, rc);

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

	cifs_set_oplock_level(pCifsInode, oplock);

	cifs_relock_file(pCifsFile);

reopen_error_exit:
	kfree(full_path);
	FreeXid(xid);
	return rc;
}

int cifs_close(struct inode *inode, struct file *file)
{
	if (file->private_data != NULL) {
		cifsFileInfo_put(file->private_data);
		file->private_data = NULL;
	}

	/* return code from the ->release op is always ignored */
	return 0;
}

int cifs_closedir(struct inode *inode, struct file *file)
{
	int rc = 0;
	int xid;
	struct cifsFileInfo *pCFileStruct = file->private_data;
	char *ptmp;

	cFYI(1, "Closedir inode = 0x%p", inode);

	xid = GetXid();

	if (pCFileStruct) {
		struct cifs_tcon *pTcon = tlink_tcon(pCFileStruct->tlink);

		cFYI(1, "Freeing private data in close dir");
		spin_lock(&cifs_file_list_lock);
		if (!pCFileStruct->srch_inf.endOfSearch &&
		    !pCFileStruct->invalidHandle) {
			pCFileStruct->invalidHandle = true;
			spin_unlock(&cifs_file_list_lock);
			rc = CIFSFindClose(xid, pTcon, pCFileStruct->netfid);
			cFYI(1, "Closing uncompleted readdir with rc %d",
				 rc);
			/* not much we can do if it fails anyway, ignore rc */
			rc = 0;
		} else
			spin_unlock(&cifs_file_list_lock);
		ptmp = pCFileStruct->srch_inf.ntwrk_buf_start;
		if (ptmp) {
			cFYI(1, "closedir free smb buf in srch struct");
			pCFileStruct->srch_inf.ntwrk_buf_start = NULL;
			if (pCFileStruct->srch_inf.smallBuf)
				cifs_small_buf_release(ptmp);
			else
				cifs_buf_release(ptmp);
		}
		cifs_put_tlink(pCFileStruct->tlink);
		kfree(file->private_data);
		file->private_data = NULL;
	}
	/* BB can we lock the filestruct while this is going on? */
	FreeXid(xid);
	return rc;
}

static struct cifsLockInfo *
cifs_lock_init(__u64 offset, __u64 length, __u8 type, __u16 netfid)
{
	struct cifsLockInfo *lock =
		kmalloc(sizeof(struct cifsLockInfo), GFP_KERNEL);
	if (!lock)
		return lock;
	lock->offset = offset;
	lock->length = length;
	lock->type = type;
	lock->netfid = netfid;
	lock->pid = current->tgid;
	INIT_LIST_HEAD(&lock->blist);
	init_waitqueue_head(&lock->block_q);
	return lock;
}

static void
cifs_del_lock_waiters(struct cifsLockInfo *lock)
{
	struct cifsLockInfo *li, *tmp;
	list_for_each_entry_safe(li, tmp, &lock->blist, blist) {
		list_del_init(&li->blist);
		wake_up(&li->block_q);
	}
}

static bool
__cifs_find_lock_conflict(struct cifsInodeInfo *cinode, __u64 offset,
			__u64 length, __u8 type, __u16 netfid,
			struct cifsLockInfo **conf_lock)
{
	struct cifsLockInfo *li, *tmp;

	list_for_each_entry_safe(li, tmp, &cinode->llist, llist) {
		if (offset + length <= li->offset ||
		    offset >= li->offset + li->length)
			continue;
		else if ((type & LOCKING_ANDX_SHARED_LOCK) &&
			 ((netfid == li->netfid && current->tgid == li->pid) ||
			  type == li->type))
			continue;
		else {
			*conf_lock = li;
			return true;
		}
	}
	return false;
}

static bool
cifs_find_lock_conflict(struct cifsInodeInfo *cinode, struct cifsLockInfo *lock,
			struct cifsLockInfo **conf_lock)
{
	return __cifs_find_lock_conflict(cinode, lock->offset, lock->length,
					 lock->type, lock->netfid, conf_lock);
}

/*
 * Check if there is another lock that prevents us to set the lock (mandatory
 * style). If such a lock exists, update the flock structure with its
 * properties. Otherwise, set the flock type to F_UNLCK if we can cache brlocks
 * or leave it the same if we can't. Returns 0 if we don't need to request to
 * the server or 1 otherwise.
 */
static int
cifs_lock_test(struct cifsInodeInfo *cinode, __u64 offset, __u64 length,
	       __u8 type, __u16 netfid, struct file_lock *flock)
{
	int rc = 0;
	struct cifsLockInfo *conf_lock;
	bool exist;

	mutex_lock(&cinode->lock_mutex);

	exist = __cifs_find_lock_conflict(cinode, offset, length, type, netfid,
					  &conf_lock);
	if (exist) {
		flock->fl_start = conf_lock->offset;
		flock->fl_end = conf_lock->offset + conf_lock->length - 1;
		flock->fl_pid = conf_lock->pid;
		if (conf_lock->type & LOCKING_ANDX_SHARED_LOCK)
			flock->fl_type = F_RDLCK;
		else
			flock->fl_type = F_WRLCK;
	} else if (!cinode->can_cache_brlcks)
		rc = 1;
	else
		flock->fl_type = F_UNLCK;

	mutex_unlock(&cinode->lock_mutex);
	return rc;
}

static void
cifs_lock_add(struct cifsInodeInfo *cinode, struct cifsLockInfo *lock)
{
	mutex_lock(&cinode->lock_mutex);
	list_add_tail(&lock->llist, &cinode->llist);
	mutex_unlock(&cinode->lock_mutex);
}

/*
 * Set the byte-range lock (mandatory style). Returns:
 * 1) 0, if we set the lock and don't need to request to the server;
 * 2) 1, if no locks prevent us but we need to request to the server;
 * 3) -EACCESS, if there is a lock that prevents us and wait is false.
 */
static int
cifs_lock_add_if(struct cifsInodeInfo *cinode, struct cifsLockInfo *lock,
		 bool wait)
{
	struct cifsLockInfo *conf_lock;
	bool exist;
	int rc = 0;

try_again:
	exist = false;
	mutex_lock(&cinode->lock_mutex);

	exist = cifs_find_lock_conflict(cinode, lock, &conf_lock);
	if (!exist && cinode->can_cache_brlcks) {
		list_add_tail(&lock->llist, &cinode->llist);
		mutex_unlock(&cinode->lock_mutex);
		return rc;
	}

	if (!exist)
		rc = 1;
	else if (!wait)
		rc = -EACCES;
	else {
		list_add_tail(&lock->blist, &conf_lock->blist);
		mutex_unlock(&cinode->lock_mutex);
		rc = wait_event_interruptible(lock->block_q,
					(lock->blist.prev == &lock->blist) &&
					(lock->blist.next == &lock->blist));
		if (!rc)
			goto try_again;
		mutex_lock(&cinode->lock_mutex);
		list_del_init(&lock->blist);
	}

	mutex_unlock(&cinode->lock_mutex);
	return rc;
}

/*
 * Check if there is another lock that prevents us to set the lock (posix
 * style). If such a lock exists, update the flock structure with its
 * properties. Otherwise, set the flock type to F_UNLCK if we can cache brlocks
 * or leave it the same if we can't. Returns 0 if we don't need to request to
 * the server or 1 otherwise.
 */
static int
cifs_posix_lock_test(struct file *file, struct file_lock *flock)
{
	int rc = 0;
	struct cifsInodeInfo *cinode = CIFS_I(file->f_path.dentry->d_inode);
	unsigned char saved_type = flock->fl_type;

	if ((flock->fl_flags & FL_POSIX) == 0)
		return 1;

	mutex_lock(&cinode->lock_mutex);
	posix_test_lock(file, flock);

	if (flock->fl_type == F_UNLCK && !cinode->can_cache_brlcks) {
		flock->fl_type = saved_type;
		rc = 1;
	}

	mutex_unlock(&cinode->lock_mutex);
	return rc;
}

/*
 * Set the byte-range lock (posix style). Returns:
 * 1) 0, if we set the lock and don't need to request to the server;
 * 2) 1, if we need to request to the server;
 * 3) <0, if the error occurs while setting the lock.
 */
static int
cifs_posix_lock_set(struct file *file, struct file_lock *flock)
{
	struct cifsInodeInfo *cinode = CIFS_I(file->f_path.dentry->d_inode);
	int rc = 1;

	if ((flock->fl_flags & FL_POSIX) == 0)
		return rc;

try_again:
	mutex_lock(&cinode->lock_mutex);
	if (!cinode->can_cache_brlcks) {
		mutex_unlock(&cinode->lock_mutex);
		return rc;
	}

	rc = posix_lock_file(file, flock, NULL);
	mutex_unlock(&cinode->lock_mutex);
	if (rc == FILE_LOCK_DEFERRED) {
		rc = wait_event_interruptible(flock->fl_wait, !flock->fl_next);
		if (!rc)
			goto try_again;
		locks_delete_block(flock);
	}
	return rc;
}

static int
cifs_push_mandatory_locks(struct cifsFileInfo *cfile)
{
	int xid, rc = 0, stored_rc;
	struct cifsLockInfo *li, *tmp;
	struct cifs_tcon *tcon;
	struct cifsInodeInfo *cinode = CIFS_I(cfile->dentry->d_inode);
	unsigned int num, max_num;
	LOCKING_ANDX_RANGE *buf, *cur;
	int types[] = {LOCKING_ANDX_LARGE_FILES,
		       LOCKING_ANDX_SHARED_LOCK | LOCKING_ANDX_LARGE_FILES};
	int i;

	xid = GetXid();
	tcon = tlink_tcon(cfile->tlink);

	mutex_lock(&cinode->lock_mutex);
	if (!cinode->can_cache_brlcks) {
		mutex_unlock(&cinode->lock_mutex);
		FreeXid(xid);
		return rc;
	}

	max_num = (tcon->ses->server->maxBuf - sizeof(struct smb_hdr)) /
		  sizeof(LOCKING_ANDX_RANGE);
	buf = kzalloc(max_num * sizeof(LOCKING_ANDX_RANGE), GFP_KERNEL);
	if (!buf) {
		mutex_unlock(&cinode->lock_mutex);
		FreeXid(xid);
		return rc;
	}

	for (i = 0; i < 2; i++) {
		cur = buf;
		num = 0;
		list_for_each_entry_safe(li, tmp, &cinode->llist, llist) {
			if (li->type != types[i])
				continue;
			cur->Pid = cpu_to_le16(li->pid);
			cur->LengthLow = cpu_to_le32((u32)li->length);
			cur->LengthHigh = cpu_to_le32((u32)(li->length>>32));
			cur->OffsetLow = cpu_to_le32((u32)li->offset);
			cur->OffsetHigh = cpu_to_le32((u32)(li->offset>>32));
			if (++num == max_num) {
				stored_rc = cifs_lockv(xid, tcon, cfile->netfid,
						       li->type, 0, num, buf);
				if (stored_rc)
					rc = stored_rc;
				cur = buf;
				num = 0;
			} else
				cur++;
		}

		if (num) {
			stored_rc = cifs_lockv(xid, tcon, cfile->netfid,
					       types[i], 0, num, buf);
			if (stored_rc)
				rc = stored_rc;
		}
	}

	cinode->can_cache_brlcks = false;
	mutex_unlock(&cinode->lock_mutex);

	kfree(buf);
	FreeXid(xid);
	return rc;
}

/* copied from fs/locks.c with a name change */
#define cifs_for_each_lock(inode, lockp) \
	for (lockp = &inode->i_flock; *lockp != NULL; \
	     lockp = &(*lockp)->fl_next)

struct lock_to_push {
	struct list_head llist;
	__u64 offset;
	__u64 length;
	__u32 pid;
	__u16 netfid;
	__u8 type;
};

static int
cifs_push_posix_locks(struct cifsFileInfo *cfile)
{
	struct cifsInodeInfo *cinode = CIFS_I(cfile->dentry->d_inode);
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct file_lock *flock, **before;
	unsigned int count = 0, i = 0;
	int rc = 0, xid, type;
	struct list_head locks_to_send, *el;
	struct lock_to_push *lck, *tmp;
	__u64 length;

	xid = GetXid();

	mutex_lock(&cinode->lock_mutex);
	if (!cinode->can_cache_brlcks) {
		mutex_unlock(&cinode->lock_mutex);
		FreeXid(xid);
		return rc;
	}

	lock_flocks();
	cifs_for_each_lock(cfile->dentry->d_inode, before) {
		if ((*before)->fl_flags & FL_POSIX)
			count++;
	}
	unlock_flocks();

	INIT_LIST_HEAD(&locks_to_send);

	/*
	 * Allocating count locks is enough because no FL_POSIX locks can be
	 * added to the list while we are holding cinode->lock_mutex that
	 * protects locking operations of this inode.
	 */
	for (; i < count; i++) {
		lck = kmalloc(sizeof(struct lock_to_push), GFP_KERNEL);
		if (!lck) {
			rc = -ENOMEM;
			goto err_out;
		}
		list_add_tail(&lck->llist, &locks_to_send);
	}

	el = locks_to_send.next;
	lock_flocks();
	cifs_for_each_lock(cfile->dentry->d_inode, before) {
		flock = *before;
		if ((flock->fl_flags & FL_POSIX) == 0)
			continue;
		if (el == &locks_to_send) {
			/*
			 * The list ended. We don't have enough allocated
			 * structures - something is really wrong.
			 */
			cERROR(1, "Can't push all brlocks!");
			break;
		}
		length = 1 + flock->fl_end - flock->fl_start;
		if (flock->fl_type == F_RDLCK || flock->fl_type == F_SHLCK)
			type = CIFS_RDLCK;
		else
			type = CIFS_WRLCK;
		lck = list_entry(el, struct lock_to_push, llist);
		lck->pid = flock->fl_pid;
		lck->netfid = cfile->netfid;
		lck->length = length;
		lck->type = type;
		lck->offset = flock->fl_start;
		el = el->next;
	}
	unlock_flocks();

	list_for_each_entry_safe(lck, tmp, &locks_to_send, llist) {
		struct file_lock tmp_lock;
		int stored_rc;

		tmp_lock.fl_start = lck->offset;
		stored_rc = CIFSSMBPosixLock(xid, tcon, lck->netfid, lck->pid,
					     0, lck->length, &tmp_lock,
					     lck->type, 0);
		if (stored_rc)
			rc = stored_rc;
		list_del(&lck->llist);
		kfree(lck);
	}

out:
	cinode->can_cache_brlcks = false;
	mutex_unlock(&cinode->lock_mutex);

	FreeXid(xid);
	return rc;
err_out:
	list_for_each_entry_safe(lck, tmp, &locks_to_send, llist) {
		list_del(&lck->llist);
		kfree(lck);
	}
	goto out;
}

static int
cifs_push_locks(struct cifsFileInfo *cfile)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(cfile->dentry->d_sb);
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);

	if ((tcon->ses->capabilities & CAP_UNIX) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		return cifs_push_posix_locks(cfile);

	return cifs_push_mandatory_locks(cfile);
}

static void
cifs_read_flock(struct file_lock *flock, __u8 *type, int *lock, int *unlock,
		bool *wait_flag)
{
	if (flock->fl_flags & FL_POSIX)
		cFYI(1, "Posix");
	if (flock->fl_flags & FL_FLOCK)
		cFYI(1, "Flock");
	if (flock->fl_flags & FL_SLEEP) {
		cFYI(1, "Blocking lock");
		*wait_flag = true;
	}
	if (flock->fl_flags & FL_ACCESS)
		cFYI(1, "Process suspended by mandatory locking - "
			"not implemented yet");
	if (flock->fl_flags & FL_LEASE)
		cFYI(1, "Lease on file - not implemented yet");
	if (flock->fl_flags &
	    (~(FL_POSIX | FL_FLOCK | FL_SLEEP | FL_ACCESS | FL_LEASE)))
		cFYI(1, "Unknown lock flags 0x%x", flock->fl_flags);

	*type = LOCKING_ANDX_LARGE_FILES;
	if (flock->fl_type == F_WRLCK) {
		cFYI(1, "F_WRLCK ");
		*lock = 1;
	} else if (flock->fl_type == F_UNLCK) {
		cFYI(1, "F_UNLCK");
		*unlock = 1;
		/* Check if unlock includes more than one lock range */
	} else if (flock->fl_type == F_RDLCK) {
		cFYI(1, "F_RDLCK");
		*type |= LOCKING_ANDX_SHARED_LOCK;
		*lock = 1;
	} else if (flock->fl_type == F_EXLCK) {
		cFYI(1, "F_EXLCK");
		*lock = 1;
	} else if (flock->fl_type == F_SHLCK) {
		cFYI(1, "F_SHLCK");
		*type |= LOCKING_ANDX_SHARED_LOCK;
		*lock = 1;
	} else
		cFYI(1, "Unknown type of lock");
}

static int
cifs_getlk(struct file *file, struct file_lock *flock, __u8 type,
	   bool wait_flag, bool posix_lck, int xid)
{
	int rc = 0;
	__u64 length = 1 + flock->fl_end - flock->fl_start;
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)file->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct cifsInodeInfo *cinode = CIFS_I(cfile->dentry->d_inode);
	__u16 netfid = cfile->netfid;

	if (posix_lck) {
		int posix_lock_type;

		rc = cifs_posix_lock_test(file, flock);
		if (!rc)
			return rc;

		if (type & LOCKING_ANDX_SHARED_LOCK)
			posix_lock_type = CIFS_RDLCK;
		else
			posix_lock_type = CIFS_WRLCK;
		rc = CIFSSMBPosixLock(xid, tcon, netfid, current->tgid,
				      1 /* get */, length, flock,
				      posix_lock_type, wait_flag);
		return rc;
	}

	rc = cifs_lock_test(cinode, flock->fl_start, length, type, netfid,
			    flock);
	if (!rc)
		return rc;

	/* BB we could chain these into one lock request BB */
	rc = CIFSSMBLock(xid, tcon, netfid, current->tgid, length,
			 flock->fl_start, 0, 1, type, 0, 0);
	if (rc == 0) {
		rc = CIFSSMBLock(xid, tcon, netfid, current->tgid,
				 length, flock->fl_start, 1, 0,
				 type, 0, 0);
		flock->fl_type = F_UNLCK;
		if (rc != 0)
			cERROR(1, "Error unlocking previously locked "
				   "range %d during test of lock", rc);
		return 0;
	}

	if (type & LOCKING_ANDX_SHARED_LOCK) {
		flock->fl_type = F_WRLCK;
		return 0;
	}

	rc = CIFSSMBLock(xid, tcon, netfid, current->tgid, length,
			 flock->fl_start, 0, 1,
			 type | LOCKING_ANDX_SHARED_LOCK, 0, 0);
	if (rc == 0) {
		rc = CIFSSMBLock(xid, tcon, netfid, current->tgid,
				 length, flock->fl_start, 1, 0,
				 type | LOCKING_ANDX_SHARED_LOCK,
				 0, 0);
		flock->fl_type = F_RDLCK;
		if (rc != 0)
			cERROR(1, "Error unlocking previously locked "
				  "range %d during test of lock", rc);
	} else
		flock->fl_type = F_WRLCK;

	return 0;
}

static void
cifs_move_llist(struct list_head *source, struct list_head *dest)
{
	struct list_head *li, *tmp;
	list_for_each_safe(li, tmp, source)
		list_move(li, dest);
}

static void
cifs_free_llist(struct list_head *llist)
{
	struct cifsLockInfo *li, *tmp;
	list_for_each_entry_safe(li, tmp, llist, llist) {
		cifs_del_lock_waiters(li);
		list_del(&li->llist);
		kfree(li);
	}
}

static int
cifs_unlock_range(struct cifsFileInfo *cfile, struct file_lock *flock, int xid)
{
	int rc = 0, stored_rc;
	int types[] = {LOCKING_ANDX_LARGE_FILES,
		       LOCKING_ANDX_SHARED_LOCK | LOCKING_ANDX_LARGE_FILES};
	unsigned int i;
	unsigned int max_num, num;
	LOCKING_ANDX_RANGE *buf, *cur;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct cifsInodeInfo *cinode = CIFS_I(cfile->dentry->d_inode);
	struct cifsLockInfo *li, *tmp;
	__u64 length = 1 + flock->fl_end - flock->fl_start;
	struct list_head tmp_llist;

	INIT_LIST_HEAD(&tmp_llist);

	max_num = (tcon->ses->server->maxBuf - sizeof(struct smb_hdr)) /
		  sizeof(LOCKING_ANDX_RANGE);
	buf = kzalloc(max_num * sizeof(LOCKING_ANDX_RANGE), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&cinode->lock_mutex);
	for (i = 0; i < 2; i++) {
		cur = buf;
		num = 0;
		list_for_each_entry_safe(li, tmp, &cinode->llist, llist) {
			if (flock->fl_start > li->offset ||
			    (flock->fl_start + length) <
			    (li->offset + li->length))
				continue;
			if (current->tgid != li->pid)
				continue;
			if (cfile->netfid != li->netfid)
				continue;
			if (types[i] != li->type)
				continue;
			if (!cinode->can_cache_brlcks) {
				cur->Pid = cpu_to_le16(li->pid);
				cur->LengthLow = cpu_to_le32((u32)li->length);
				cur->LengthHigh =
					cpu_to_le32((u32)(li->length>>32));
				cur->OffsetLow = cpu_to_le32((u32)li->offset);
				cur->OffsetHigh =
					cpu_to_le32((u32)(li->offset>>32));
				/*
				 * We need to save a lock here to let us add
				 * it again to the inode list if the unlock
				 * range request fails on the server.
				 */
				list_move(&li->llist, &tmp_llist);
				if (++num == max_num) {
					stored_rc = cifs_lockv(xid, tcon,
							       cfile->netfid,
							       li->type, num,
							       0, buf);
					if (stored_rc) {
						/*
						 * We failed on the unlock range
						 * request - add all locks from
						 * the tmp list to the head of
						 * the inode list.
						 */
						cifs_move_llist(&tmp_llist,
								&cinode->llist);
						rc = stored_rc;
					} else
						/*
						 * The unlock range request
						 * succeed - free the tmp list.
						 */
						cifs_free_llist(&tmp_llist);
					cur = buf;
					num = 0;
				} else
					cur++;
			} else {
				/*
				 * We can cache brlock requests - simply remove
				 * a lock from the inode list.
				 */
				list_del(&li->llist);
				cifs_del_lock_waiters(li);
				kfree(li);
			}
		}
		if (num) {
			stored_rc = cifs_lockv(xid, tcon, cfile->netfid,
					       types[i], num, 0, buf);
			if (stored_rc) {
				cifs_move_llist(&tmp_llist, &cinode->llist);
				rc = stored_rc;
			} else
				cifs_free_llist(&tmp_llist);
		}
	}

	mutex_unlock(&cinode->lock_mutex);
	kfree(buf);
	return rc;
}

static int
cifs_setlk(struct file *file,  struct file_lock *flock, __u8 type,
	   bool wait_flag, bool posix_lck, int lock, int unlock, int xid)
{
	int rc = 0;
	__u64 length = 1 + flock->fl_end - flock->fl_start;
	struct cifsFileInfo *cfile = (struct cifsFileInfo *)file->private_data;
	struct cifs_tcon *tcon = tlink_tcon(cfile->tlink);
	struct cifsInodeInfo *cinode = CIFS_I(file->f_path.dentry->d_inode);
	__u16 netfid = cfile->netfid;

	if (posix_lck) {
		int posix_lock_type;

		rc = cifs_posix_lock_set(file, flock);
		if (!rc || rc < 0)
			return rc;

		if (type & LOCKING_ANDX_SHARED_LOCK)
			posix_lock_type = CIFS_RDLCK;
		else
			posix_lock_type = CIFS_WRLCK;

		if (unlock == 1)
			posix_lock_type = CIFS_UNLCK;

		rc = CIFSSMBPosixLock(xid, tcon, netfid, current->tgid,
				      0 /* set */, length, flock,
				      posix_lock_type, wait_flag);
		goto out;
	}

	if (lock) {
		struct cifsLockInfo *lock;

		lock = cifs_lock_init(flock->fl_start, length, type, netfid);
		if (!lock)
			return -ENOMEM;

		rc = cifs_lock_add_if(cinode, lock, wait_flag);
		if (rc < 0)
			kfree(lock);
		if (rc <= 0)
			goto out;

		rc = CIFSSMBLock(xid, tcon, netfid, current->tgid, length,
				 flock->fl_start, 0, 1, type, wait_flag, 0);
		if (rc) {
			kfree(lock);
			goto out;
		}

		cifs_lock_add(cinode, lock);
	} else if (unlock)
		rc = cifs_unlock_range(cfile, flock, xid);

out:
	if (flock->fl_flags & FL_POSIX)
		posix_lock_file_wait(file, flock);
	return rc;
}

int cifs_lock(struct file *file, int cmd, struct file_lock *flock)
{
	int rc, xid;
	int lock = 0, unlock = 0;
	bool wait_flag = false;
	bool posix_lck = false;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *tcon;
	struct cifsInodeInfo *cinode;
	struct cifsFileInfo *cfile;
	__u16 netfid;
	__u8 type;

	rc = -EACCES;
	xid = GetXid();

	cFYI(1, "Lock parm: 0x%x flockflags: 0x%x flocktype: 0x%x start: %lld "
		"end: %lld", cmd, flock->fl_flags, flock->fl_type,
		flock->fl_start, flock->fl_end);

	cifs_read_flock(flock, &type, &lock, &unlock, &wait_flag);

	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	cfile = (struct cifsFileInfo *)file->private_data;
	tcon = tlink_tcon(cfile->tlink);
	netfid = cfile->netfid;
	cinode = CIFS_I(file->f_path.dentry->d_inode);

	if ((tcon->ses->capabilities & CAP_UNIX) &&
	    (CIFS_UNIX_FCNTL_CAP & le64_to_cpu(tcon->fsUnixInfo.Capability)) &&
	    ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOPOSIXBRL) == 0))
		posix_lck = true;
	/*
	 * BB add code here to normalize offset and length to account for
	 * negative length which we can not accept over the wire.
	 */
	if (IS_GETLK(cmd)) {
		rc = cifs_getlk(file, flock, type, wait_flag, posix_lck, xid);
		FreeXid(xid);
		return rc;
	}

	if (!lock && !unlock) {
		/*
		 * if no lock or unlock then nothing to do since we do not
		 * know what it is
		 */
		FreeXid(xid);
		return -EOPNOTSUPP;
	}

	rc = cifs_setlk(file, flock, type, wait_flag, posix_lck, lock, unlock,
			xid);
	FreeXid(xid);
	return rc;
}

/*
 * update the file size (if needed) after a write. Should be called with
 * the inode->i_lock held
 */
void
cifs_update_eof(struct cifsInodeInfo *cifsi, loff_t offset,
		      unsigned int bytes_written)
{
	loff_t end_of_write = offset + bytes_written;

	if (end_of_write > cifsi->server_eof)
		cifsi->server_eof = end_of_write;
}

static ssize_t cifs_write(struct cifsFileInfo *open_file, __u32 pid,
			  const char *write_data, size_t write_size,
			  loff_t *poffset)
{
	int rc = 0;
	unsigned int bytes_written = 0;
	unsigned int total_written;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *pTcon;
	int xid;
	struct dentry *dentry = open_file->dentry;
	struct cifsInodeInfo *cifsi = CIFS_I(dentry->d_inode);
	struct cifs_io_parms io_parms;

	cifs_sb = CIFS_SB(dentry->d_sb);

	cFYI(1, "write %zd bytes to offset %lld of %s", write_size,
	   *poffset, dentry->d_name.name);

	pTcon = tlink_tcon(open_file->tlink);

	xid = GetXid();

	for (total_written = 0; write_size > total_written;
	     total_written += bytes_written) {
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			struct kvec iov[2];
			unsigned int len;

			if (open_file->invalidHandle) {
				/* we could deadlock if we called
				   filemap_fdatawait from here so tell
				   reopen_file not to flush data to
				   server now */
				rc = cifs_reopen_file(open_file, false);
				if (rc != 0)
					break;
			}

			len = min((size_t)cifs_sb->wsize,
				  write_size - total_written);
			/* iov[0] is reserved for smb header */
			iov[1].iov_base = (char *)write_data + total_written;
			iov[1].iov_len = len;
			io_parms.netfid = open_file->netfid;
			io_parms.pid = pid;
			io_parms.tcon = pTcon;
			io_parms.offset = *poffset;
			io_parms.length = len;
			rc = CIFSSMBWrite2(xid, &io_parms, &bytes_written, iov,
					   1, 0);
		}
		if (rc || (bytes_written == 0)) {
			if (total_written)
				break;
			else {
				FreeXid(xid);
				return rc;
			}
		} else {
			spin_lock(&dentry->d_inode->i_lock);
			cifs_update_eof(cifsi, *poffset, bytes_written);
			spin_unlock(&dentry->d_inode->i_lock);
			*poffset += bytes_written;
		}
	}

	cifs_stats_bytes_written(pTcon, total_written);

	if (total_written > 0) {
		spin_lock(&dentry->d_inode->i_lock);
		if (*poffset > dentry->d_inode->i_size)
			i_size_write(dentry->d_inode, *poffset);
		spin_unlock(&dentry->d_inode->i_lock);
	}
	mark_inode_dirty_sync(dentry->d_inode);
	FreeXid(xid);
	return total_written;
}

struct cifsFileInfo *find_readable_file(struct cifsInodeInfo *cifs_inode,
					bool fsuid_only)
{
	struct cifsFileInfo *open_file = NULL;
	struct cifs_sb_info *cifs_sb = CIFS_SB(cifs_inode->vfs_inode.i_sb);

	/* only filter by fsuid on multiuser mounts */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		fsuid_only = false;

	spin_lock(&cifs_file_list_lock);
	/* we could simply get the first_list_entry since write-only entries
	   are always at the end of the list but since the first entry might
	   have a close pending, we go through the whole list */
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (fsuid_only && open_file->uid != current_fsuid())
			continue;
		if (OPEN_FMODE(open_file->f_flags) & FMODE_READ) {
			if (!open_file->invalidHandle) {
				/* found a good file */
				/* lock it so it will not be closed on us */
				cifsFileInfo_get(open_file);
				spin_unlock(&cifs_file_list_lock);
				return open_file;
			} /* else might as well continue, and look for
			     another, or simply have the caller reopen it
			     again rather than trying to fix this handle */
		} else /* write only file */
			break; /* write only files are last so must be done */
	}
	spin_unlock(&cifs_file_list_lock);
	return NULL;
}

struct cifsFileInfo *find_writable_file(struct cifsInodeInfo *cifs_inode,
					bool fsuid_only)
{
	struct cifsFileInfo *open_file, *inv_file = NULL;
	struct cifs_sb_info *cifs_sb;
	bool any_available = false;
	int rc;
	unsigned int refind = 0;

	/* Having a null inode here (because mapping->host was set to zero by
	the VFS or MM) should not happen but we had reports of on oops (due to
	it being zero) during stress testcases so we need to check for it */

	if (cifs_inode == NULL) {
		cERROR(1, "Null inode passed to cifs_writeable_file");
		dump_stack();
		return NULL;
	}

	cifs_sb = CIFS_SB(cifs_inode->vfs_inode.i_sb);

	/* only filter by fsuid on multiuser mounts */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MULTIUSER))
		fsuid_only = false;

	spin_lock(&cifs_file_list_lock);
refind_writable:
	if (refind > MAX_REOPEN_ATT) {
		spin_unlock(&cifs_file_list_lock);
		return NULL;
	}
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (!any_available && open_file->pid != current->tgid)
			continue;
		if (fsuid_only && open_file->uid != current_fsuid())
			continue;
		if (OPEN_FMODE(open_file->f_flags) & FMODE_WRITE) {
			if (!open_file->invalidHandle) {
				/* found a good writable file */
				cifsFileInfo_get(open_file);
				spin_unlock(&cifs_file_list_lock);
				return open_file;
			} else {
				if (!inv_file)
					inv_file = open_file;
			}
		}
	}
	/* couldn't find useable FH with same pid, try any available */
	if (!any_available) {
		any_available = true;
		goto refind_writable;
	}

	if (inv_file) {
		any_available = false;
		cifsFileInfo_get(inv_file);
	}

	spin_unlock(&cifs_file_list_lock);

	if (inv_file) {
		rc = cifs_reopen_file(inv_file, false);
		if (!rc)
			return inv_file;
		else {
			spin_lock(&cifs_file_list_lock);
			list_move_tail(&inv_file->flist,
					&cifs_inode->openFileList);
			spin_unlock(&cifs_file_list_lock);
			cifsFileInfo_put(inv_file);
			spin_lock(&cifs_file_list_lock);
			++refind;
			goto refind_writable;
		}
	}

	return NULL;
}

static int cifs_partialpagewrite(struct page *page, unsigned from, unsigned to)
{
	struct address_space *mapping = page->mapping;
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	char *write_data;
	int rc = -EFAULT;
	int bytes_written = 0;
	struct inode *inode;
	struct cifsFileInfo *open_file;

	if (!mapping || !mapping->host)
		return -EFAULT;

	inode = page->mapping->host;

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

	open_file = find_writable_file(CIFS_I(mapping->host), false);
	if (open_file) {
		bytes_written = cifs_write(open_file, open_file->pid,
					   write_data, to - from, &offset);
		cifsFileInfo_put(open_file);
		/* Does mm or vfs already set times? */
		inode->i_atime = inode->i_mtime = current_fs_time(inode->i_sb);
		if ((bytes_written > 0) && (offset))
			rc = 0;
		else if (bytes_written < 0)
			rc = bytes_written;
	} else {
		cFYI(1, "No writeable filehandles for inode");
		rc = -EIO;
	}

	kunmap(page);
	return rc;
}

/*
 * Marshal up the iov array, reserving the first one for the header. Also,
 * set wdata->bytes.
 */
static void
cifs_writepages_marshal_iov(struct kvec *iov, struct cifs_writedata *wdata)
{
	int i;
	struct inode *inode = wdata->cfile->dentry->d_inode;
	loff_t size = i_size_read(inode);

	/* marshal up the pages into iov array */
	wdata->bytes = 0;
	for (i = 0; i < wdata->nr_pages; i++) {
		iov[i + 1].iov_len = min(size - page_offset(wdata->pages[i]),
					(loff_t)PAGE_CACHE_SIZE);
		iov[i + 1].iov_base = kmap(wdata->pages[i]);
		wdata->bytes += iov[i + 1].iov_len;
	}
}

static int cifs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(mapping->host->i_sb);
	bool done = false, scanned = false, range_whole = false;
	pgoff_t end, index;
	struct cifs_writedata *wdata;
	struct page *page;
	int rc = 0;

	/*
	 * If wsize is smaller than the page cache size, default to writing
	 * one page at a time via cifs_writepage
	 */
	if (cifs_sb->wsize < PAGE_CACHE_SIZE)
		return generic_writepages(mapping, wbc);

	if (wbc->range_cyclic) {
		index = mapping->writeback_index; /* Start from prev offset */
		end = -1;
	} else {
		index = wbc->range_start >> PAGE_CACHE_SHIFT;
		end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			range_whole = true;
		scanned = true;
	}
retry:
	while (!done && index <= end) {
		unsigned int i, nr_pages, found_pages;
		pgoff_t next = 0, tofind;
		struct page **pages;

		tofind = min((cifs_sb->wsize / PAGE_CACHE_SIZE) - 1,
				end - index) + 1;

		wdata = cifs_writedata_alloc((unsigned int)tofind,
					     cifs_writev_complete);
		if (!wdata) {
			rc = -ENOMEM;
			break;
		}

		/*
		 * find_get_pages_tag seems to return a max of 256 on each
		 * iteration, so we must call it several times in order to
		 * fill the array or the wsize is effectively limited to
		 * 256 * PAGE_CACHE_SIZE.
		 */
		found_pages = 0;
		pages = wdata->pages;
		do {
			nr_pages = find_get_pages_tag(mapping, &index,
							PAGECACHE_TAG_DIRTY,
							tofind, pages);
			found_pages += nr_pages;
			tofind -= nr_pages;
			pages += nr_pages;
		} while (nr_pages && tofind && index <= end);

		if (found_pages == 0) {
			kref_put(&wdata->refcount, cifs_writedata_release);
			break;
		}

		nr_pages = 0;
		for (i = 0; i < found_pages; i++) {
			page = wdata->pages[i];
			/*
			 * At this point we hold neither mapping->tree_lock nor
			 * lock on the page itself: the page may be truncated or
			 * invalidated (changing page->mapping to NULL), or even
			 * swizzled back from swapper_space to tmpfs file
			 * mapping
			 */

			if (nr_pages == 0)
				lock_page(page);
			else if (!trylock_page(page))
				break;

			if (unlikely(page->mapping != mapping)) {
				unlock_page(page);
				break;
			}

			if (!wbc->range_cyclic && page->index > end) {
				done = true;
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
				done = true;
				unlock_page(page);
				end_page_writeback(page);
				break;
			}

			wdata->pages[i] = page;
			next = page->index + 1;
			++nr_pages;
		}

		/* reset index to refind any pages skipped */
		if (nr_pages == 0)
			index = wdata->pages[0]->index + 1;

		/* put any pages we aren't going to use */
		for (i = nr_pages; i < found_pages; i++) {
			page_cache_release(wdata->pages[i]);
			wdata->pages[i] = NULL;
		}

		/* nothing to write? */
		if (nr_pages == 0) {
			kref_put(&wdata->refcount, cifs_writedata_release);
			continue;
		}

		wdata->sync_mode = wbc->sync_mode;
		wdata->nr_pages = nr_pages;
		wdata->offset = page_offset(wdata->pages[0]);
		wdata->marshal_iov = cifs_writepages_marshal_iov;

		do {
			if (wdata->cfile != NULL)
				cifsFileInfo_put(wdata->cfile);
			wdata->cfile = find_writable_file(CIFS_I(mapping->host),
							  false);
			if (!wdata->cfile) {
				cERROR(1, "No writable handles for inode");
				rc = -EBADF;
				break;
			}
			wdata->pid = wdata->cfile->pid;
			rc = cifs_async_writev(wdata);
		} while (wbc->sync_mode == WB_SYNC_ALL && rc == -EAGAIN);

		for (i = 0; i < nr_pages; ++i)
			unlock_page(wdata->pages[i]);

		/* send failure -- clean up the mess */
		if (rc != 0) {
			for (i = 0; i < nr_pages; ++i) {
				if (rc == -EAGAIN)
					redirty_page_for_writepage(wbc,
							   wdata->pages[i]);
				else
					SetPageError(wdata->pages[i]);
				end_page_writeback(wdata->pages[i]);
				page_cache_release(wdata->pages[i]);
			}
			if (rc != -EAGAIN)
				mapping_set_error(mapping, rc);
		}
		kref_put(&wdata->refcount, cifs_writedata_release);

		wbc->nr_to_write -= nr_pages;
		if (wbc->nr_to_write <= 0)
			done = true;

		index = next;
	}

	if (!scanned && !done) {
		/*
		 * We hit the last page and there is more work to be done: wrap
		 * back to the start of the file
		 */
		scanned = true;
		index = 0;
		goto retry;
	}

	if (wbc->range_cyclic || (range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = index;

	return rc;
}

static int
cifs_writepage_locked(struct page *page, struct writeback_control *wbc)
{
	int rc;
	int xid;

	xid = GetXid();
/* BB add check for wbc flags */
	page_cache_get(page);
	if (!PageUptodate(page))
		cFYI(1, "ppw - page not up to date");

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
retry_write:
	rc = cifs_partialpagewrite(page, 0, PAGE_CACHE_SIZE);
	if (rc == -EAGAIN && wbc->sync_mode == WB_SYNC_ALL)
		goto retry_write;
	else if (rc == -EAGAIN)
		redirty_page_for_writepage(wbc, page);
	else if (rc != 0)
		SetPageError(page);
	else
		SetPageUptodate(page);
	end_page_writeback(page);
	page_cache_release(page);
	FreeXid(xid);
	return rc;
}

static int cifs_writepage(struct page *page, struct writeback_control *wbc)
{
	int rc = cifs_writepage_locked(page, wbc);
	unlock_page(page);
	return rc;
}

static int cifs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	int rc;
	struct inode *inode = mapping->host;
	struct cifsFileInfo *cfile = file->private_data;
	struct cifs_sb_info *cifs_sb = CIFS_SB(cfile->dentry->d_sb);
	__u32 pid;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = cfile->pid;
	else
		pid = current->tgid;

	cFYI(1, "write_end for page %p from pos %lld with %d bytes",
		 page, pos, copied);

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
		rc = cifs_write(cfile, pid, page_data + offset, copied, &pos);
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

int cifs_strict_fsync(struct file *file, loff_t start, loff_t end,
		      int datasync)
{
	int xid;
	int rc = 0;
	struct cifs_tcon *tcon;
	struct cifsFileInfo *smbfile = file->private_data;
	struct inode *inode = file->f_path.dentry->d_inode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);

	rc = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (rc)
		return rc;
	mutex_lock(&inode->i_mutex);

	xid = GetXid();

	cFYI(1, "Sync file - name: %s datasync: 0x%x",
		file->f_path.dentry->d_name.name, datasync);

	if (!CIFS_I(inode)->clientCanCacheRead) {
		rc = cifs_invalidate_mapping(inode);
		if (rc) {
			cFYI(1, "rc: %d during invalidate phase", rc);
			rc = 0; /* don't care about it in fsync */
		}
	}

	tcon = tlink_tcon(smbfile->tlink);
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOSSYNC))
		rc = CIFSSMBFlush(xid, tcon, smbfile->netfid);

	FreeXid(xid);
	mutex_unlock(&inode->i_mutex);
	return rc;
}

int cifs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int xid;
	int rc = 0;
	struct cifs_tcon *tcon;
	struct cifsFileInfo *smbfile = file->private_data;
	struct cifs_sb_info *cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	struct inode *inode = file->f_mapping->host;

	rc = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (rc)
		return rc;
	mutex_lock(&inode->i_mutex);

	xid = GetXid();

	cFYI(1, "Sync file - name: %s datasync: 0x%x",
		file->f_path.dentry->d_name.name, datasync);

	tcon = tlink_tcon(smbfile->tlink);
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NOSSYNC))
		rc = CIFSSMBFlush(xid, tcon, smbfile->netfid);

	FreeXid(xid);
	mutex_unlock(&inode->i_mutex);
	return rc;
}

/*
 * As file closes, flush all cached write data for this inode checking
 * for write behind errors.
 */
int cifs_flush(struct file *file, fl_owner_t id)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	int rc = 0;

	if (file->f_mode & FMODE_WRITE)
		rc = filemap_write_and_wait(inode->i_mapping);

	cFYI(1, "Flush inode %p file %p rc %d", inode, file, rc);

	return rc;
}

static int
cifs_write_allocate_pages(struct page **pages, unsigned long num_pages)
{
	int rc = 0;
	unsigned long i;

	for (i = 0; i < num_pages; i++) {
		pages[i] = alloc_page(GFP_KERNEL|__GFP_HIGHMEM);
		if (!pages[i]) {
			/*
			 * save number of pages we have already allocated and
			 * return with ENOMEM error
			 */
			num_pages = i;
			rc = -ENOMEM;
			break;
		}
	}

	if (rc) {
		for (i = 0; i < num_pages; i++)
			put_page(pages[i]);
	}
	return rc;
}

static inline
size_t get_numpages(const size_t wsize, const size_t len, size_t *cur_len)
{
	size_t num_pages;
	size_t clen;

	clen = min_t(const size_t, len, wsize);
	num_pages = DIV_ROUND_UP(clen, PAGE_SIZE);

	if (cur_len)
		*cur_len = clen;

	return num_pages;
}

static void
cifs_uncached_marshal_iov(struct kvec *iov, struct cifs_writedata *wdata)
{
	int i;
	size_t bytes = wdata->bytes;

	/* marshal up the pages into iov array */
	for (i = 0; i < wdata->nr_pages; i++) {
		iov[i + 1].iov_len = min_t(size_t, bytes, PAGE_SIZE);
		iov[i + 1].iov_base = kmap(wdata->pages[i]);
		bytes -= iov[i + 1].iov_len;
	}
}

static void
cifs_uncached_writev_complete(struct work_struct *work)
{
	int i;
	struct cifs_writedata *wdata = container_of(work,
					struct cifs_writedata, work);
	struct inode *inode = wdata->cfile->dentry->d_inode;
	struct cifsInodeInfo *cifsi = CIFS_I(inode);

	spin_lock(&inode->i_lock);
	cifs_update_eof(cifsi, wdata->offset, wdata->bytes);
	if (cifsi->server_eof > inode->i_size)
		i_size_write(inode, cifsi->server_eof);
	spin_unlock(&inode->i_lock);

	complete(&wdata->done);

	if (wdata->result != -EAGAIN) {
		for (i = 0; i < wdata->nr_pages; i++)
			put_page(wdata->pages[i]);
	}

	kref_put(&wdata->refcount, cifs_writedata_release);
}

/* attempt to send write to server, retry on any -EAGAIN errors */
static int
cifs_uncached_retry_writev(struct cifs_writedata *wdata)
{
	int rc;

	do {
		if (wdata->cfile->invalidHandle) {
			rc = cifs_reopen_file(wdata->cfile, false);
			if (rc != 0)
				continue;
		}
		rc = cifs_async_writev(wdata);
	} while (rc == -EAGAIN);

	return rc;
}

static ssize_t
cifs_iovec_write(struct file *file, const struct iovec *iov,
		 unsigned long nr_segs, loff_t *poffset)
{
	unsigned long nr_pages, i;
	size_t copied, len, cur_len;
	ssize_t total_written = 0;
	loff_t offset;
	struct iov_iter it;
	struct cifsFileInfo *open_file;
	struct cifs_tcon *tcon;
	struct cifs_sb_info *cifs_sb;
	struct cifs_writedata *wdata, *tmp;
	struct list_head wdata_list;
	int rc;
	pid_t pid;

	len = iov_length(iov, nr_segs);
	if (!len)
		return 0;

	rc = generic_write_checks(file, poffset, &len, 0);
	if (rc)
		return rc;

	INIT_LIST_HEAD(&wdata_list);
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	open_file = file->private_data;
	tcon = tlink_tcon(open_file->tlink);
	offset = *poffset;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = open_file->pid;
	else
		pid = current->tgid;

	iov_iter_init(&it, iov, nr_segs, len, 0);
	do {
		size_t save_len;

		nr_pages = get_numpages(cifs_sb->wsize, len, &cur_len);
		wdata = cifs_writedata_alloc(nr_pages,
					     cifs_uncached_writev_complete);
		if (!wdata) {
			rc = -ENOMEM;
			break;
		}

		rc = cifs_write_allocate_pages(wdata->pages, nr_pages);
		if (rc) {
			kfree(wdata);
			break;
		}

		save_len = cur_len;
		for (i = 0; i < nr_pages; i++) {
			copied = min_t(const size_t, cur_len, PAGE_SIZE);
			copied = iov_iter_copy_from_user(wdata->pages[i], &it,
							 0, copied);
			cur_len -= copied;
			iov_iter_advance(&it, copied);
		}
		cur_len = save_len - cur_len;

		wdata->sync_mode = WB_SYNC_ALL;
		wdata->nr_pages = nr_pages;
		wdata->offset = (__u64)offset;
		wdata->cfile = cifsFileInfo_get(open_file);
		wdata->pid = pid;
		wdata->bytes = cur_len;
		wdata->marshal_iov = cifs_uncached_marshal_iov;
		rc = cifs_uncached_retry_writev(wdata);
		if (rc) {
			kref_put(&wdata->refcount, cifs_writedata_release);
			break;
		}

		list_add_tail(&wdata->list, &wdata_list);
		offset += cur_len;
		len -= cur_len;
	} while (len > 0);

	/*
	 * If at least one write was successfully sent, then discard any rc
	 * value from the later writes. If the other write succeeds, then
	 * we'll end up returning whatever was written. If it fails, then
	 * we'll get a new rc value from that.
	 */
	if (!list_empty(&wdata_list))
		rc = 0;

	/*
	 * Wait for and collect replies for any successful sends in order of
	 * increasing offset. Once an error is hit or we get a fatal signal
	 * while waiting, then return without waiting for any more replies.
	 */
restart_loop:
	list_for_each_entry_safe(wdata, tmp, &wdata_list, list) {
		if (!rc) {
			/* FIXME: freezable too? */
			rc = wait_for_completion_killable(&wdata->done);
			if (rc)
				rc = -EINTR;
			else if (wdata->result)
				rc = wdata->result;
			else
				total_written += wdata->bytes;

			/* resend call if it's a retryable error */
			if (rc == -EAGAIN) {
				rc = cifs_uncached_retry_writev(wdata);
				goto restart_loop;
			}
		}
		list_del_init(&wdata->list);
		kref_put(&wdata->refcount, cifs_writedata_release);
	}

	if (total_written > 0)
		*poffset += total_written;

	cifs_stats_bytes_written(tcon, total_written);
	return total_written ? total_written : (ssize_t)rc;
}

ssize_t cifs_user_writev(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t pos)
{
	ssize_t written;
	struct inode *inode;

	inode = iocb->ki_filp->f_path.dentry->d_inode;

	/*
	 * BB - optimize the way when signing is disabled. We can drop this
	 * extra memory-to-memory copying and use iovec buffers for constructing
	 * write request.
	 */

	written = cifs_iovec_write(iocb->ki_filp, iov, nr_segs, &pos);
	if (written > 0) {
		CIFS_I(inode)->invalid_mapping = true;
		iocb->ki_pos = pos;
	}

	return written;
}

ssize_t cifs_strict_writev(struct kiocb *iocb, const struct iovec *iov,
			   unsigned long nr_segs, loff_t pos)
{
	struct inode *inode;

	inode = iocb->ki_filp->f_path.dentry->d_inode;

	if (CIFS_I(inode)->clientCanCacheAll)
		return generic_file_aio_write(iocb, iov, nr_segs, pos);

	/*
	 * In strict cache mode we need to write the data to the server exactly
	 * from the pos to pos+len-1 rather than flush all affected pages
	 * because it may cause a error with mandatory locks on these pages but
	 * not on the region from pos to ppos+len-1.
	 */

	return cifs_user_writev(iocb, iov, nr_segs, pos);
}

static ssize_t
cifs_iovec_read(struct file *file, const struct iovec *iov,
		 unsigned long nr_segs, loff_t *poffset)
{
	int rc;
	int xid;
	ssize_t total_read;
	unsigned int bytes_read = 0;
	size_t len, cur_len;
	int iov_offset = 0;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *pTcon;
	struct cifsFileInfo *open_file;
	struct smb_com_read_rsp *pSMBr;
	struct cifs_io_parms io_parms;
	char *read_data;
	unsigned int rsize;
	__u32 pid;

	if (!nr_segs)
		return 0;

	len = iov_length(iov, nr_segs);
	if (!len)
		return 0;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);

	/* FIXME: set up handlers for larger reads and/or convert to async */
	rsize = min_t(unsigned int, cifs_sb->rsize, CIFSMaxBufSize);

	open_file = file->private_data;
	pTcon = tlink_tcon(open_file->tlink);

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = open_file->pid;
	else
		pid = current->tgid;

	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		cFYI(1, "attempting read on write only file instance");

	for (total_read = 0; total_read < len; total_read += bytes_read) {
		cur_len = min_t(const size_t, len - total_read, rsize);
		rc = -EAGAIN;
		read_data = NULL;

		while (rc == -EAGAIN) {
			int buf_type = CIFS_NO_BUFFER;
			if (open_file->invalidHandle) {
				rc = cifs_reopen_file(open_file, true);
				if (rc != 0)
					break;
			}
			io_parms.netfid = open_file->netfid;
			io_parms.pid = pid;
			io_parms.tcon = pTcon;
			io_parms.offset = *poffset;
			io_parms.length = cur_len;
			rc = CIFSSMBRead(xid, &io_parms, &bytes_read,
					 &read_data, &buf_type);
			pSMBr = (struct smb_com_read_rsp *)read_data;
			if (read_data) {
				char *data_offset = read_data + 4 +
						le16_to_cpu(pSMBr->DataOffset);
				if (memcpy_toiovecend(iov, data_offset,
						      iov_offset, bytes_read))
					rc = -EFAULT;
				if (buf_type == CIFS_SMALL_BUFFER)
					cifs_small_buf_release(read_data);
				else if (buf_type == CIFS_LARGE_BUFFER)
					cifs_buf_release(read_data);
				read_data = NULL;
				iov_offset += bytes_read;
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

ssize_t cifs_user_readv(struct kiocb *iocb, const struct iovec *iov,
			       unsigned long nr_segs, loff_t pos)
{
	ssize_t read;

	read = cifs_iovec_read(iocb->ki_filp, iov, nr_segs, &pos);
	if (read > 0)
		iocb->ki_pos = pos;

	return read;
}

ssize_t cifs_strict_readv(struct kiocb *iocb, const struct iovec *iov,
			  unsigned long nr_segs, loff_t pos)
{
	struct inode *inode;

	inode = iocb->ki_filp->f_path.dentry->d_inode;

	if (CIFS_I(inode)->clientCanCacheRead)
		return generic_file_aio_read(iocb, iov, nr_segs, pos);

	/*
	 * In strict cache mode we need to read from the server all the time
	 * if we don't have level II oplock because the server can delay mtime
	 * change - so we can't make a decision about inode invalidating.
	 * And we can also fail with pagereading if there are mandatory locks
	 * on pages affected by this read but not on the region from pos to
	 * pos+len-1.
	 */

	return cifs_user_readv(iocb, iov, nr_segs, pos);
}

static ssize_t cifs_read(struct file *file, char *read_data, size_t read_size,
			 loff_t *poffset)
{
	int rc = -EACCES;
	unsigned int bytes_read = 0;
	unsigned int total_read;
	unsigned int current_read_size;
	unsigned int rsize;
	struct cifs_sb_info *cifs_sb;
	struct cifs_tcon *pTcon;
	int xid;
	char *current_offset;
	struct cifsFileInfo *open_file;
	struct cifs_io_parms io_parms;
	int buf_type = CIFS_NO_BUFFER;
	__u32 pid;

	xid = GetXid();
	cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);

	/* FIXME: set up handlers for larger reads and/or convert to async */
	rsize = min_t(unsigned int, cifs_sb->rsize, CIFSMaxBufSize);

	if (file->private_data == NULL) {
		rc = -EBADF;
		FreeXid(xid);
		return rc;
	}
	open_file = file->private_data;
	pTcon = tlink_tcon(open_file->tlink);

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = open_file->pid;
	else
		pid = current->tgid;

	if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		cFYI(1, "attempting read on write only file instance");

	for (total_read = 0, current_offset = read_data;
	     read_size > total_read;
	     total_read += bytes_read, current_offset += bytes_read) {
		current_read_size = min_t(uint, read_size - total_read, rsize);

		/* For windows me and 9x we do not want to request more
		than it negotiated since it will refuse the read then */
		if ((pTcon->ses) &&
			!(pTcon->ses->capabilities & CAP_LARGE_FILES)) {
			current_read_size = min_t(uint, current_read_size,
					CIFSMaxBufSize);
		}
		rc = -EAGAIN;
		while (rc == -EAGAIN) {
			if (open_file->invalidHandle) {
				rc = cifs_reopen_file(open_file, true);
				if (rc != 0)
					break;
			}
			io_parms.netfid = open_file->netfid;
			io_parms.pid = pid;
			io_parms.tcon = pTcon;
			io_parms.offset = *poffset;
			io_parms.length = current_read_size;
			rc = CIFSSMBRead(xid, &io_parms, &bytes_read,
					 &current_offset, &buf_type);
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

/*
 * If the page is mmap'ed into a process' page tables, then we need to make
 * sure that it doesn't change while being written back.
 */
static int
cifs_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *page = vmf->page;

	lock_page(page);
	return VM_FAULT_LOCKED;
}

static struct vm_operations_struct cifs_file_vm_ops = {
	.fault = filemap_fault,
	.page_mkwrite = cifs_page_mkwrite,
};

int cifs_file_strict_mmap(struct file *file, struct vm_area_struct *vma)
{
	int rc, xid;
	struct inode *inode = file->f_path.dentry->d_inode;

	xid = GetXid();

	if (!CIFS_I(inode)->clientCanCacheRead) {
		rc = cifs_invalidate_mapping(inode);
		if (rc)
			return rc;
	}

	rc = generic_file_mmap(file, vma);
	if (rc == 0)
		vma->vm_ops = &cifs_file_vm_ops;
	FreeXid(xid);
	return rc;
}

int cifs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	int rc, xid;

	xid = GetXid();
	rc = cifs_revalidate_file(file);
	if (rc) {
		cFYI(1, "Validation prior to mmap failed, error=%d", rc);
		FreeXid(xid);
		return rc;
	}
	rc = generic_file_mmap(file, vma);
	if (rc == 0)
		vma->vm_ops = &cifs_file_vm_ops;
	FreeXid(xid);
	return rc;
}

static int cifs_readpages(struct file *file, struct address_space *mapping,
	struct list_head *page_list, unsigned num_pages)
{
	int rc;
	struct list_head tmplist;
	struct cifsFileInfo *open_file = file->private_data;
	struct cifs_sb_info *cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	unsigned int rsize = cifs_sb->rsize;
	pid_t pid;

	/*
	 * Give up immediately if rsize is too small to read an entire page.
	 * The VFS will fall back to readpage. We should never reach this
	 * point however since we set ra_pages to 0 when the rsize is smaller
	 * than a cache page.
	 */
	if (unlikely(rsize < PAGE_CACHE_SIZE))
		return 0;

	/*
	 * Reads as many pages as possible from fscache. Returns -ENOBUFS
	 * immediately if the cookie is negative
	 */
	rc = cifs_readpages_from_fscache(mapping->host, mapping, page_list,
					 &num_pages);
	if (rc == 0)
		return rc;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_RWPIDFORWARD)
		pid = open_file->pid;
	else
		pid = current->tgid;

	rc = 0;
	INIT_LIST_HEAD(&tmplist);

	cFYI(1, "%s: file=%p mapping=%p num_pages=%u", __func__, file,
		mapping, num_pages);

	/*
	 * Start with the page at end of list and move it to private
	 * list. Do the same with any following pages until we hit
	 * the rsize limit, hit an index discontinuity, or run out of
	 * pages. Issue the async read and then start the loop again
	 * until the list is empty.
	 *
	 * Note that list order is important. The page_list is in
	 * the order of declining indexes. When we put the pages in
	 * the rdata->pages, then we want them in increasing order.
	 */
	while (!list_empty(page_list)) {
		unsigned int bytes = PAGE_CACHE_SIZE;
		unsigned int expected_index;
		unsigned int nr_pages = 1;
		loff_t offset;
		struct page *page, *tpage;
		struct cifs_readdata *rdata;

		page = list_entry(page_list->prev, struct page, lru);

		/*
		 * Lock the page and put it in the cache. Since no one else
		 * should have access to this page, we're safe to simply set
		 * PG_locked without checking it first.
		 */
		__set_page_locked(page);
		rc = add_to_page_cache_locked(page, mapping,
					      page->index, GFP_KERNEL);

		/* give up if we can't stick it in the cache */
		if (rc) {
			__clear_page_locked(page);
			break;
		}

		/* move first page to the tmplist */
		offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
		list_move_tail(&page->lru, &tmplist);

		/* now try and add more pages onto the request */
		expected_index = page->index + 1;
		list_for_each_entry_safe_reverse(page, tpage, page_list, lru) {
			/* discontinuity ? */
			if (page->index != expected_index)
				break;

			/* would this page push the read over the rsize? */
			if (bytes + PAGE_CACHE_SIZE > rsize)
				break;

			__set_page_locked(page);
			if (add_to_page_cache_locked(page, mapping,
						page->index, GFP_KERNEL)) {
				__clear_page_locked(page);
				break;
			}
			list_move_tail(&page->lru, &tmplist);
			bytes += PAGE_CACHE_SIZE;
			expected_index++;
			nr_pages++;
		}

		rdata = cifs_readdata_alloc(nr_pages);
		if (!rdata) {
			/* best to give up if we're out of mem */
			list_for_each_entry_safe(page, tpage, &tmplist, lru) {
				list_del(&page->lru);
				lru_cache_add_file(page);
				unlock_page(page);
				page_cache_release(page);
			}
			rc = -ENOMEM;
			break;
		}

		spin_lock(&cifs_file_list_lock);
		cifsFileInfo_get(open_file);
		spin_unlock(&cifs_file_list_lock);
		rdata->cfile = open_file;
		rdata->mapping = mapping;
		rdata->offset = offset;
		rdata->bytes = bytes;
		rdata->pid = pid;
		list_splice_init(&tmplist, &rdata->pages);

		do {
			if (open_file->invalidHandle) {
				rc = cifs_reopen_file(open_file, true);
				if (rc != 0)
					continue;
			}
			rc = cifs_async_readv(rdata);
		} while (rc == -EAGAIN);

		if (rc != 0) {
			list_for_each_entry_safe(page, tpage, &rdata->pages,
						 lru) {
				list_del(&page->lru);
				lru_cache_add_file(page);
				unlock_page(page);
				page_cache_release(page);
			}
			cifs_readdata_free(rdata);
			break;
		}
	}

	return rc;
}

static int cifs_readpage_worker(struct file *file, struct page *page,
	loff_t *poffset)
{
	char *read_data;
	int rc;

	/* Is the page cached? */
	rc = cifs_readpage_from_fscache(file->f_path.dentry->d_inode, page);
	if (rc == 0)
		goto read_complete;

	page_cache_get(page);
	read_data = kmap(page);
	/* for reads over a certain size could initiate async read ahead */

	rc = cifs_read(file, read_data, PAGE_CACHE_SIZE, poffset);

	if (rc < 0)
		goto io_error;
	else
		cFYI(1, "Bytes read %d", rc);

	file->f_path.dentry->d_inode->i_atime =
		current_fs_time(file->f_path.dentry->d_inode->i_sb);

	if (PAGE_CACHE_SIZE > rc)
		memset(read_data + rc, 0, PAGE_CACHE_SIZE - rc);

	flush_dcache_page(page);
	SetPageUptodate(page);

	/* send this page to the cache */
	cifs_readpage_to_fscache(file->f_path.dentry->d_inode, page);

	rc = 0;

io_error:
	kunmap(page);
	page_cache_release(page);

read_complete:
	return rc;
}

static int cifs_readpage(struct file *file, struct page *page)
{
	loff_t offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	int rc = -EACCES;
	int xid;

	xid = GetXid();

	if (file->private_data == NULL) {
		rc = -EBADF;
		FreeXid(xid);
		return rc;
	}

	cFYI(1, "readpage %p at offset %d 0x%x\n",
		 page, (int)offset, (int)offset);

	rc = cifs_readpage_worker(file, page, &offset);

	unlock_page(page);

	FreeXid(xid);
	return rc;
}

static int is_inode_writable(struct cifsInodeInfo *cifs_inode)
{
	struct cifsFileInfo *open_file;

	spin_lock(&cifs_file_list_lock);
	list_for_each_entry(open_file, &cifs_inode->openFileList, flist) {
		if (OPEN_FMODE(open_file->f_flags) & FMODE_WRITE) {
			spin_unlock(&cifs_file_list_lock);
			return 1;
		}
	}
	spin_unlock(&cifs_file_list_lock);
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

	cFYI(1, "write_begin from %lld len %d", (long long)pos, len);

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

static int cifs_release_page(struct page *page, gfp_t gfp)
{
	if (PagePrivate(page))
		return 0;

	return cifs_fscache_release_page(page, gfp);
}

static void cifs_invalidate_page(struct page *page, unsigned long offset)
{
	struct cifsInodeInfo *cifsi = CIFS_I(page->mapping->host);

	if (offset == 0)
		cifs_fscache_invalidate_page(page, &cifsi->vfs_inode);
}

static int cifs_launder_page(struct page *page)
{
	int rc = 0;
	loff_t range_start = page_offset(page);
	loff_t range_end = range_start + (loff_t)(PAGE_CACHE_SIZE - 1);
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0,
		.range_start = range_start,
		.range_end = range_end,
	};

	cFYI(1, "Launder page: %p", page);

	if (clear_page_dirty_for_io(page))
		rc = cifs_writepage_locked(page, &wbc);

	cifs_fscache_invalidate_page(page, page->mapping->host);
	return rc;
}

void cifs_oplock_break(struct work_struct *work)
{
	struct cifsFileInfo *cfile = container_of(work, struct cifsFileInfo,
						  oplock_break);
	struct inode *inode = cfile->dentry->d_inode;
	struct cifsInodeInfo *cinode = CIFS_I(inode);
	int rc = 0;

	if (inode && S_ISREG(inode->i_mode)) {
		if (cinode->clientCanCacheRead)
			break_lease(inode, O_RDONLY);
		else
			break_lease(inode, O_WRONLY);
		rc = filemap_fdatawrite(inode->i_mapping);
		if (cinode->clientCanCacheRead == 0) {
			rc = filemap_fdatawait(inode->i_mapping);
			mapping_set_error(inode->i_mapping, rc);
			invalidate_remote_inode(inode);
		}
		cFYI(1, "Oplock flush inode %p rc %d", inode, rc);
	}

	rc = cifs_push_locks(cfile);
	if (rc)
		cERROR(1, "Push locks rc = %d", rc);

	/*
	 * releasing stale oplock after recent reconnect of smb session using
	 * a now incorrect file handle is not a data integrity issue but do
	 * not bother sending an oplock release if session to server still is
	 * disconnected since oplock already released by the server
	 */
	if (!cfile->oplock_break_cancelled) {
		rc = CIFSSMBLock(0, tlink_tcon(cfile->tlink), cfile->netfid,
				 current->tgid, 0, 0, 0, 0,
				 LOCKING_ANDX_OPLOCK_RELEASE, false,
				 cinode->clientCanCacheRead ? 1 : 0);
		cFYI(1, "Oplock release rc = %d", rc);
	}
}

const struct address_space_operations cifs_addr_ops = {
	.readpage = cifs_readpage,
	.readpages = cifs_readpages,
	.writepage = cifs_writepage,
	.writepages = cifs_writepages,
	.write_begin = cifs_write_begin,
	.write_end = cifs_write_end,
	.set_page_dirty = __set_page_dirty_nobuffers,
	.releasepage = cifs_release_page,
	.invalidatepage = cifs_invalidate_page,
	.launder_page = cifs_launder_page,
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
	.releasepage = cifs_release_page,
	.invalidatepage = cifs_invalidate_page,
	.launder_page = cifs_launder_page,
};
