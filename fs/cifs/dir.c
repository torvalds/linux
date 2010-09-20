/*
 *   fs/cifs/dir.c
 *
 *   vfs operations that deal with dentries
 *
 *   Copyright (C) International Business Machines  Corp., 2002,2009
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
#include <linux/mount.h>
#include <linux/file.h>
#include "cifsfs.h"
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"

static void
renew_parental_timestamps(struct dentry *direntry)
{
	/* BB check if there is a way to get the kernel to do this or if we
	   really need this */
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
	int namelen;
	int pplen;
	int dfsplen;
	char *full_path;
	char dirsep;
	struct cifs_sb_info *cifs_sb = CIFS_SB(direntry->d_sb);
	struct cifsTconInfo *tcon = cifs_sb_master_tcon(cifs_sb);

	if (direntry == NULL)
		return NULL;  /* not much we can do if dentry is freed and
		we need to reopen the file after it was closed implicitly
		when the server crashed */

	dirsep = CIFS_DIR_SEP(cifs_sb);
	pplen = cifs_sb->prepathlen;
	if (tcon->Flags & SMB_SHARE_IS_IN_DFS)
		dfsplen = strnlen(tcon->treeName, MAX_TREE_SIZE + 1);
	else
		dfsplen = 0;
cifs_bp_rename_retry:
	namelen = pplen + dfsplen;
	for (temp = direntry; !IS_ROOT(temp);) {
		namelen += (1 + temp->d_name.len);
		temp = temp->d_parent;
		if (temp == NULL) {
			cERROR(1, "corrupt dentry");
			return NULL;
		}
	}

	full_path = kmalloc(namelen+1, GFP_KERNEL);
	if (full_path == NULL)
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
			cFYI(0, "name: %s", full_path + namelen);
		}
		temp = temp->d_parent;
		if (temp == NULL) {
			cERROR(1, "corrupt dentry");
			kfree(full_path);
			return NULL;
		}
	}
	if (namelen != pplen + dfsplen) {
		cERROR(1, "did not end path lookup where expected namelen is %d",
			namelen);
		/* presumably this is only possible if racing with a rename
		of one of the parent directories  (we can not lock the dentries
		above us to prevent this, but retrying should be harmless) */
		kfree(full_path);
		goto cifs_bp_rename_retry;
	}
	/* DIR_SEP already set for byte  0 / vs \ but not for
	   subsequent slashes in prepath which currently must
	   be entered the right way - not sure if there is an alternative
	   since the '\' is a valid posix character so we can not switch
	   those safely to '/' if any are found in the middle of the prepath */
	/* BB test paths to Windows with '/' in the midst of prepath */

	if (dfsplen) {
		strncpy(full_path, tcon->treeName, dfsplen);
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) {
			int i;
			for (i = 0; i < dfsplen; i++) {
				if (full_path[i] == '\\')
					full_path[i] = '/';
			}
		}
	}
	strncpy(full_path + dfsplen, CIFS_SB(direntry->d_sb)->prepath, pplen);
	return full_path;
}

struct cifsFileInfo *
cifs_new_fileinfo(struct inode *newinode, __u16 fileHandle, struct file *file,
		  struct vfsmount *mnt, struct cifsTconInfo *tcon,
		  unsigned int oflags, __u32 oplock)
{
	struct cifsFileInfo *pCifsFile;
	struct cifsInodeInfo *pCifsInode;
	struct cifs_sb_info *cifs_sb = CIFS_SB(mnt->mnt_sb);

	pCifsFile = kzalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
	if (pCifsFile == NULL)
		return pCifsFile;

	pCifsFile->netfid = fileHandle;
	pCifsFile->pid = current->tgid;
	pCifsFile->pInode = igrab(newinode);
	pCifsFile->mnt = mnt;
	pCifsFile->pfile = file;
	pCifsFile->invalidHandle = false;
	pCifsFile->closePend = false;
	pCifsFile->tcon = tcon;
	mutex_init(&pCifsFile->fh_mutex);
	mutex_init(&pCifsFile->lock_mutex);
	INIT_LIST_HEAD(&pCifsFile->llist);
	atomic_set(&pCifsFile->count, 1);
	INIT_WORK(&pCifsFile->oplock_break, cifs_oplock_break);

	write_lock(&GlobalSMBSeslock);
	list_add(&pCifsFile->tlist, &tcon->openFileList);
	pCifsInode = CIFS_I(newinode);
	if (pCifsInode) {
		/* if readable file instance put first in list*/
		if (oflags & FMODE_READ)
			list_add(&pCifsFile->flist, &pCifsInode->openFileList);
		else
			list_add_tail(&pCifsFile->flist,
				      &pCifsInode->openFileList);

		if ((oplock & 0xF) == OPLOCK_EXCLUSIVE) {
			pCifsInode->clientCanCacheAll = true;
			pCifsInode->clientCanCacheRead = true;
			cFYI(1, "Exclusive Oplock inode %p", newinode);
		} else if ((oplock & 0xF) == OPLOCK_READ)
				pCifsInode->clientCanCacheRead = true;
	}
	write_unlock(&GlobalSMBSeslock);

	file->private_data = pCifsFile;

	return pCifsFile;
}

int cifs_posix_open(char *full_path, struct inode **pinode,
			struct super_block *sb, int mode, int oflags,
			__u32 *poplock, __u16 *pnetfid, int xid)
{
	int rc;
	FILE_UNIX_BASIC_INFO *presp_data;
	__u32 posix_flags = 0;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_fattr fattr;
	struct cifsTconInfo *tcon = cifs_sb_tcon(cifs_sb);

	cFYI(1, "posix open %s", full_path);

	presp_data = kzalloc(sizeof(FILE_UNIX_BASIC_INFO), GFP_KERNEL);
	if (presp_data == NULL)
		return -ENOMEM;

/* So far cifs posix extensions can only map the following flags.
   There are other valid fmode oflags such as FMODE_LSEEK, FMODE_PREAD, but
   so far we do not seem to need them, and we can treat them as local only */
	if ((oflags & (FMODE_READ | FMODE_WRITE)) ==
		(FMODE_READ | FMODE_WRITE))
		posix_flags = SMB_O_RDWR;
	else if (oflags & FMODE_READ)
		posix_flags = SMB_O_RDONLY;
	else if (oflags & FMODE_WRITE)
		posix_flags = SMB_O_WRONLY;
	if (oflags & O_CREAT)
		posix_flags |= SMB_O_CREAT;
	if (oflags & O_EXCL)
		posix_flags |= SMB_O_EXCL;
	if (oflags & O_TRUNC)
		posix_flags |= SMB_O_TRUNC;
	/* be safe and imply O_SYNC for O_DSYNC */
	if (oflags & O_DSYNC)
		posix_flags |= SMB_O_SYNC;
	if (oflags & O_DIRECTORY)
		posix_flags |= SMB_O_DIRECTORY;
	if (oflags & O_NOFOLLOW)
		posix_flags |= SMB_O_NOFOLLOW;
	if (oflags & O_DIRECT)
		posix_flags |= SMB_O_DIRECT;

	mode &= ~current_umask();
	rc = CIFSPOSIXCreate(xid, tcon, posix_flags, mode, pnetfid, presp_data,
			     poplock, full_path, cifs_sb->local_nls,
			     cifs_sb->mnt_cifs_flags &
					CIFS_MOUNT_MAP_SPECIAL_CHR);
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

static void setup_cifs_dentry(struct cifsTconInfo *tcon,
			      struct dentry *direntry,
			      struct inode *newinode)
{
	if (tcon->nocase)
		direntry->d_op = &cifs_ci_dentry_ops;
	else
		direntry->d_op = &cifs_dentry_ops;
	d_instantiate(direntry, newinode);
}

/* Inode operations in similar order to how they appear in Linux file fs.h */

int
cifs_create(struct inode *inode, struct dentry *direntry, int mode,
		struct nameidata *nd)
{
	int rc = -ENOENT;
	int xid;
	int create_options = CREATE_NOT_DIR;
	__u32 oplock = 0;
	int oflags;
	/*
	 * BB below access is probably too much for mknod to request
	 *    but we have to do query and setpathinfo so requesting
	 *    less could fail (unless we want to request getatr and setatr
	 *    permissions (only).  At least for POSIX we do not have to
	 *    request so much.
	 */
	int desiredAccess = GENERIC_READ | GENERIC_WRITE;
	__u16 fileHandle;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *tcon;
	char *full_path = NULL;
	FILE_ALL_INFO *buf = NULL;
	struct inode *newinode = NULL;
	int disposition = FILE_OVERWRITE_IF;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	tcon = cifs_sb_tcon(cifs_sb);

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto cifs_create_out;
	}

	if (oplockEnabled)
		oplock = REQ_OPLOCK;

	if (nd && (nd->flags & LOOKUP_OPEN))
		oflags = nd->intent.open.flags;
	else
		oflags = FMODE_READ | SMB_O_CREAT;

	if (tcon->unix_ext && (tcon->ses->capabilities & CAP_UNIX) &&
	    (CIFS_UNIX_POSIX_PATH_OPS_CAP &
			le64_to_cpu(tcon->fsUnixInfo.Capability))) {
		rc = cifs_posix_open(full_path, &newinode,
			inode->i_sb, mode, oflags, &oplock, &fileHandle, xid);
		/* EIO could indicate that (posix open) operation is not
		   supported, despite what server claimed in capability
		   negotation.  EREMOTE indicates DFS junction, which is not
		   handled in posix open */

		if (rc == 0) {
			if (newinode == NULL) /* query inode info */
				goto cifs_create_get_file_info;
			else /* success, no need to query */
				goto cifs_create_set_dentry;
		} else if ((rc != -EIO) && (rc != -EREMOTE) &&
			 (rc != -EOPNOTSUPP) && (rc != -EINVAL))
			goto cifs_create_out;
		/* else fallthrough to retry, using older open call, this is
		   case where server does not support this SMB level, and
		   falsely claims capability (also get here for DFS case
		   which should be rare for path not covered on files) */
	}

	if (nd && (nd->flags & LOOKUP_OPEN)) {
		/* if the file is going to stay open, then we
		   need to set the desired access properly */
		desiredAccess = 0;
		if (oflags & FMODE_READ)
			desiredAccess |= GENERIC_READ; /* is this too little? */
		if (oflags & FMODE_WRITE)
			desiredAccess |= GENERIC_WRITE;

		if ((oflags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
			disposition = FILE_CREATE;
		else if ((oflags & (O_CREAT | O_TRUNC)) == (O_CREAT | O_TRUNC))
			disposition = FILE_OVERWRITE_IF;
		else if ((oflags & O_CREAT) == O_CREAT)
			disposition = FILE_OPEN_IF;
		else
			cFYI(1, "Create flag not set in create function");
	}

	/* BB add processing to set equivalent of mode - e.g. via CreateX with
	   ACLs */

	buf = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto cifs_create_out;
	}

	/*
	 * if we're not using unix extensions, see if we need to set
	 * ATTR_READONLY on the create call
	 */
	if (!tcon->unix_ext && (mode & S_IWUGO) == 0)
		create_options |= CREATE_OPTION_READONLY;

	if (tcon->ses->capabilities & CAP_NT_SMBS)
		rc = CIFSSMBOpen(xid, tcon, full_path, disposition,
			 desiredAccess, create_options,
			 &fileHandle, &oplock, buf, cifs_sb->local_nls,
			 cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	else
		rc = -EIO; /* no NT SMB support fall into legacy open below */

	if (rc == -EIO) {
		/* old server, retry the open legacy style */
		rc = SMBLegacyOpen(xid, tcon, full_path, disposition,
			desiredAccess, create_options,
			&fileHandle, &oplock, buf, cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	}
	if (rc) {
		cFYI(1, "cifs_create returned 0x%x", rc);
		goto cifs_create_out;
	}

	/* If Open reported that we actually created a file
	   then we now have to set the mode if possible */
	if ((tcon->unix_ext) && (oplock & CIFS_CREATE_ACTION)) {
		struct cifs_unix_set_info_args args = {
				.mode	= mode,
				.ctime	= NO_CHANGE_64,
				.atime	= NO_CHANGE_64,
				.mtime	= NO_CHANGE_64,
				.device	= 0,
		};

		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
			args.uid = (__u64) current_fsuid();
			if (inode->i_mode & S_ISGID)
				args.gid = (__u64) inode->i_gid;
			else
				args.gid = (__u64) current_fsgid();
		} else {
			args.uid = NO_CHANGE_64;
			args.gid = NO_CHANGE_64;
		}
		CIFSSMBUnixSetPathInfo(xid, tcon, full_path, &args,
					cifs_sb->local_nls,
					cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
	} else {
		/* BB implement mode setting via Windows security
		   descriptors e.g. */
		/* CIFSSMBWinSetPerms(xid,tcon,path,mode,-1,-1,nls);*/

		/* Could set r/o dos attribute if mode & 0222 == 0 */
	}

cifs_create_get_file_info:
	/* server might mask mode so we have to query for it */
	if (tcon->unix_ext)
		rc = cifs_get_inode_info_unix(&newinode, full_path,
					      inode->i_sb, xid);
	else {
		rc = cifs_get_inode_info(&newinode, full_path, buf,
					 inode->i_sb, xid, &fileHandle);
		if (newinode) {
			if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DYNPERM)
				newinode->i_mode = mode;
			if ((oplock & CIFS_CREATE_ACTION) &&
			    (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID)) {
				newinode->i_uid = current_fsuid();
				if (inode->i_mode & S_ISGID)
					newinode->i_gid = inode->i_gid;
				else
					newinode->i_gid = current_fsgid();
			}
		}
	}

cifs_create_set_dentry:
	if (rc == 0)
		setup_cifs_dentry(tcon, direntry, newinode);
	else
		cFYI(1, "Create worked, get_inode_info failed rc = %d", rc);

	if (newinode && nd && (nd->flags & LOOKUP_OPEN)) {
		struct cifsFileInfo *pfile_info;
		struct file *filp;

		filp = lookup_instantiate_filp(nd, direntry, generic_file_open);
		if (IS_ERR(filp)) {
			rc = PTR_ERR(filp);
			CIFSSMBClose(xid, tcon, fileHandle);
			goto cifs_create_out;
		}

		pfile_info = cifs_new_fileinfo(newinode, fileHandle, filp,
					       nd->path.mnt, tcon, oflags,
						oplock);
		if (pfile_info == NULL) {
			fput(filp);
			CIFSSMBClose(xid, tcon, fileHandle);
			rc = -ENOMEM;
		}
	} else {
		CIFSSMBClose(xid, tcon, fileHandle);
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
	struct inode *newinode = NULL;
	int oplock = 0;
	u16 fileHandle;
	FILE_ALL_INFO *buf = NULL;
	unsigned int bytes_written;
	struct win_dev *pdev;

	if (!old_valid_dev(device_number))
		return -EINVAL;

	xid = GetXid();

	cifs_sb = CIFS_SB(inode->i_sb);
	pTcon = cifs_sb_tcon(cifs_sb);

	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto mknod_out;
	}

	if (pTcon->unix_ext) {
		struct cifs_unix_set_info_args args = {
			.mode	= mode & ~current_umask(),
			.ctime	= NO_CHANGE_64,
			.atime	= NO_CHANGE_64,
			.mtime	= NO_CHANGE_64,
			.device	= device_number,
		};
		if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SET_UID) {
			args.uid = (__u64) current_fsuid();
			args.gid = (__u64) current_fsgid();
		} else {
			args.uid = NO_CHANGE_64;
			args.gid = NO_CHANGE_64;
		}
		rc = CIFSSMBUnixSetPathInfo(xid, pTcon, full_path, &args,
					    cifs_sb->local_nls,
					    cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		if (rc)
			goto mknod_out;

		rc = cifs_get_inode_info_unix(&newinode, full_path,
						inode->i_sb, xid);
		if (pTcon->nocase)
			direntry->d_op = &cifs_ci_dentry_ops;
		else
			direntry->d_op = &cifs_dentry_ops;

		if (rc == 0)
			d_instantiate(direntry, newinode);
		goto mknod_out;
	}

	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL))
		goto mknod_out;


	cFYI(1, "sfu compat create special file");

	buf = kmalloc(sizeof(FILE_ALL_INFO), GFP_KERNEL);
	if (buf == NULL) {
		kfree(full_path);
		rc = -ENOMEM;
		FreeXid(xid);
		return rc;
	}

	/* FIXME: would WRITE_OWNER | WRITE_DAC be better? */
	rc = CIFSSMBOpen(xid, pTcon, full_path, FILE_CREATE,
			 GENERIC_WRITE, CREATE_NOT_DIR | CREATE_OPTION_SPECIAL,
			 &fileHandle, &oplock, buf, cifs_sb->local_nls,
			 cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (rc)
		goto mknod_out;

	/* BB Do not bother to decode buf since no local inode yet to put
	 * timestamps in, but we can reuse it safely */

	pdev = (struct win_dev *)buf;
	if (S_ISCHR(mode)) {
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
	} else if (S_ISBLK(mode)) {
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
	} /* else if (S_ISFIFO) */
	CIFSSMBClose(xid, pTcon, fileHandle);
	d_drop(direntry);

	/* FIXME: add code here to set EAs */

mknod_out:
	kfree(full_path);
	kfree(buf);
	FreeXid(xid);
	return rc;
}

struct dentry *
cifs_lookup(struct inode *parent_dir_inode, struct dentry *direntry,
	    struct nameidata *nd)
{
	int xid;
	int rc = 0; /* to get around spurious gcc warning, set to zero here */
	__u32 oplock = 0;
	__u16 fileHandle = 0;
	bool posix_open = false;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsFileInfo *cfile;
	struct inode *newInode = NULL;
	char *full_path = NULL;
	struct file *filp;

	xid = GetXid();

	cFYI(1, "parent inode = 0x%p name is: %s and dentry = 0x%p",
	      parent_dir_inode, direntry->d_name.name, direntry);

	/* check whether path exists */

	cifs_sb = CIFS_SB(parent_dir_inode->i_sb);
	pTcon = cifs_sb_tcon(cifs_sb);

	/*
	 * Don't allow the separator character in a path component.
	 * The VFS will not allow "/", but "\" is allowed by posix.
	 */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS)) {
		int i;
		for (i = 0; i < direntry->d_name.len; i++)
			if (direntry->d_name.name[i] == '\\') {
				cFYI(1, "Invalid file name");
				FreeXid(xid);
				return ERR_PTR(-EINVAL);
			}
	}

	/*
	 * O_EXCL: optimize away the lookup, but don't hash the dentry. Let
	 * the VFS handle the create.
	 */
	if (nd && (nd->flags & LOOKUP_EXCL)) {
		d_instantiate(direntry, NULL);
		return NULL;
	}

	/* can not grab the rename sem here since it would
	deadlock in the cases (beginning of sys_rename itself)
	in which we already have the sb rename sem */
	full_path = build_path_from_dentry(direntry);
	if (full_path == NULL) {
		FreeXid(xid);
		return ERR_PTR(-ENOMEM);
	}

	if (direntry->d_inode != NULL) {
		cFYI(1, "non-NULL inode in lookup");
	} else {
		cFYI(1, "NULL inode in lookup");
	}
	cFYI(1, "Full path: %s inode = 0x%p", full_path, direntry->d_inode);

	/* Posix open is only called (at lookup time) for file create now.
	 * For opens (rather than creates), because we do not know if it
	 * is a file or directory yet, and current Samba no longer allows
	 * us to do posix open on dirs, we could end up wasting an open call
	 * on what turns out to be a dir. For file opens, we wait to call posix
	 * open till cifs_open.  It could be added here (lookup) in the future
	 * but the performance tradeoff of the extra network request when EISDIR
	 * or EACCES is returned would have to be weighed against the 50%
	 * reduction in network traffic in the other paths.
	 */
	if (pTcon->unix_ext) {
		if (nd && !(nd->flags & (LOOKUP_PARENT | LOOKUP_DIRECTORY)) &&
		     (nd->flags & LOOKUP_OPEN) && !pTcon->broken_posix_open &&
		     (nd->intent.open.flags & O_CREAT)) {
			rc = cifs_posix_open(full_path, &newInode,
					parent_dir_inode->i_sb,
					nd->intent.open.create_mode,
					nd->intent.open.flags, &oplock,
					&fileHandle, xid);
			/*
			 * The check below works around a bug in POSIX
			 * open in samba versions 3.3.1 and earlier where
			 * open could incorrectly fail with invalid parameter.
			 * If either that or op not supported returned, follow
			 * the normal lookup.
			 */
			if ((rc == 0) || (rc == -ENOENT))
				posix_open = true;
			else if ((rc == -EINVAL) || (rc != -EOPNOTSUPP))
				pTcon->broken_posix_open = true;
		}
		if (!posix_open)
			rc = cifs_get_inode_info_unix(&newInode, full_path,
						parent_dir_inode->i_sb, xid);
	} else
		rc = cifs_get_inode_info(&newInode, full_path, NULL,
				parent_dir_inode->i_sb, xid, NULL);

	if ((rc == 0) && (newInode != NULL)) {
		if (pTcon->nocase)
			direntry->d_op = &cifs_ci_dentry_ops;
		else
			direntry->d_op = &cifs_dentry_ops;
		d_add(direntry, newInode);
		if (posix_open) {
			filp = lookup_instantiate_filp(nd, direntry,
						       generic_file_open);
			if (IS_ERR(filp)) {
				rc = PTR_ERR(filp);
				CIFSSMBClose(xid, pTcon, fileHandle);
				goto lookup_out;
			}

			cfile = cifs_new_fileinfo(newInode, fileHandle, filp,
						  nd->path.mnt, pTcon,
						  nd->intent.open.flags,
						  oplock);
			if (cfile == NULL) {
				fput(filp);
				CIFSSMBClose(xid, pTcon, fileHandle);
				rc = -ENOMEM;
				goto lookup_out;
			}
		}
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
	} else if (rc != -EACCES) {
		cERROR(1, "Unexpected lookup error %d", rc);
		/* We special case check for Access Denied - since that
		is a common return code */
	}

lookup_out:
	kfree(full_path);
	FreeXid(xid);
	return ERR_PTR(rc);
}

static int
cifs_d_revalidate(struct dentry *direntry, struct nameidata *nd)
{
	int isValid = 1;

	if (direntry->d_inode) {
		if (cifs_revalidate_dentry(direntry))
			return 0;
	} else {
		cFYI(1, "neg dentry 0x%p name = %s",
			 direntry, direntry->d_name.name);
		if (time_after(jiffies, direntry->d_time + HZ) ||
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

	cFYI(1, "In cifs d_delete, name = %s", direntry->d_name.name);

	return rc;
}     */

const struct dentry_operations cifs_dentry_ops = {
	.d_revalidate = cifs_d_revalidate,
/* d_delete:       cifs_d_delete,      */ /* not needed except for debugging */
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
		memcpy((void *)a->name, b->name, a->len);
		return 0;
	}
	return 1;
}

const struct dentry_operations cifs_ci_dentry_ops = {
	.d_revalidate = cifs_d_revalidate,
	.d_hash = cifs_ci_hash,
	.d_compare = cifs_ci_compare,
};
