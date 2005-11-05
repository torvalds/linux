/*
 *   fs/cifs/readdir.c
 *
 *   Directory search handling
 * 
 *   Copyright (C) International Business Machines  Corp., 2004, 2005
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
#include <linux/smp_lock.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "cifsfs.h"

/* BB fixme - add debug wrappers around this function to disable it fixme BB */
/* static void dump_cifs_file_struct(struct file *file, char *label)
{
	struct cifsFileInfo * cf;

	if(file) {
		cf = file->private_data;
		if(cf == NULL) {
			cFYI(1,("empty cifs private file data"));
			return;
		}
		if(cf->invalidHandle) {
			cFYI(1,("invalid handle"));
		}
		if(cf->srch_inf.endOfSearch) {
			cFYI(1,("end of search"));
		}
		if(cf->srch_inf.emptyDir) {
			cFYI(1,("empty dir"));
		}
		
	}
} */

/* Returns one if new inode created (which therefore needs to be hashed) */
/* Might check in the future if inode number changed so we can rehash inode */
static int construct_dentry(struct qstr *qstring, struct file *file,
	struct inode **ptmp_inode, struct dentry **pnew_dentry)
{
	struct dentry *tmp_dentry;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	int rc = 0;

	cFYI(1, ("For %s", qstring->name));
	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;

	qstring->hash = full_name_hash(qstring->name, qstring->len);
	tmp_dentry = d_lookup(file->f_dentry, qstring);
	if (tmp_dentry) {
		cFYI(0, ("existing dentry with inode 0x%p", tmp_dentry->d_inode));
		*ptmp_inode = tmp_dentry->d_inode;
/* BB overwrite old name? i.e. tmp_dentry->d_name and tmp_dentry->d_name.len??*/
		if(*ptmp_inode == NULL) {
			*ptmp_inode = new_inode(file->f_dentry->d_sb);
			if(*ptmp_inode == NULL)
				return rc;
			rc = 1;
			d_instantiate(tmp_dentry, *ptmp_inode);
		}
	} else {
		tmp_dentry = d_alloc(file->f_dentry, qstring);
		if(tmp_dentry == NULL) {
			cERROR(1,("Failed allocating dentry"));
			*ptmp_inode = NULL;
			return rc;
		}

		*ptmp_inode = new_inode(file->f_dentry->d_sb);
		if (pTcon->nocase)
			tmp_dentry->d_op = &cifs_ci_dentry_ops;
		else
			tmp_dentry->d_op = &cifs_dentry_ops;
		if(*ptmp_inode == NULL)
			return rc;
		rc = 1;
		d_instantiate(tmp_dentry, *ptmp_inode);
		d_rehash(tmp_dentry);
	}

	tmp_dentry->d_time = jiffies;
	*pnew_dentry = tmp_dentry;
	return rc;
}

static void fill_in_inode(struct inode *tmp_inode,
	FILE_DIRECTORY_INFO *pfindData, int *pobject_type, int isNewInode)
{
	loff_t local_size;
	struct timespec local_mtime;

	struct cifsInodeInfo *cifsInfo = CIFS_I(tmp_inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(tmp_inode->i_sb);
	__u32 attr = le32_to_cpu(pfindData->ExtFileAttributes);
	__u64 allocation_size = le64_to_cpu(pfindData->AllocationSize);
	__u64 end_of_file = le64_to_cpu(pfindData->EndOfFile);

	cifsInfo->cifsAttrs = attr;
	cifsInfo->time = jiffies;

	/* save mtime and size */
	local_mtime = tmp_inode->i_mtime;
	local_size  = tmp_inode->i_size;

	/* Linux can not store file creation time unfortunately so ignore it */
	tmp_inode->i_atime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastAccessTime));
	tmp_inode->i_mtime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastWriteTime));
	tmp_inode->i_ctime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->ChangeTime));
	/* treat dos attribute of read-only as read-only mode bit e.g. 555? */
	/* 2767 perms - indicate mandatory locking */
		/* BB fill in uid and gid here? with help from winbind? 
		   or retrieve from NTFS stream extended attribute */
	if (atomic_read(&cifsInfo->inUse) == 0) {
		tmp_inode->i_uid = cifs_sb->mnt_uid;
		tmp_inode->i_gid = cifs_sb->mnt_gid;
		/* set default mode. will override for dirs below */
		tmp_inode->i_mode = cifs_sb->mnt_file_mode;
	}

	if (attr & ATTR_DIRECTORY) {
		*pobject_type = DT_DIR;
		/* override default perms since we do not lock dirs */
		if(atomic_read(&cifsInfo->inUse) == 0) {
			tmp_inode->i_mode = cifs_sb->mnt_dir_mode;
		}
		tmp_inode->i_mode |= S_IFDIR;
	} else if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) && 
		   (attr & ATTR_SYSTEM) && (end_of_file == 0)) {
		*pobject_type = DT_FIFO;
		tmp_inode->i_mode |= S_IFIFO;
/* BB Finish for SFU style symlinks and devies */
/*	} else if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL) &&
		(attr & ATTR_SYSTEM) && ) { */
/* we no longer mark these because we could not follow them */
/*        } else if (attr & ATTR_REPARSE) {
                *pobject_type = DT_LNK;
                tmp_inode->i_mode |= S_IFLNK; */
	} else {
		*pobject_type = DT_REG;
		tmp_inode->i_mode |= S_IFREG;
		if (attr & ATTR_READONLY)
			tmp_inode->i_mode &= ~(S_IWUGO);
	} /* could add code here - to validate if device or weird share type? */

	/* can not fill in nlink here as in qpathinfo version and Unx search */
	if (atomic_read(&cifsInfo->inUse) == 0) {
		atomic_set(&cifsInfo->inUse, 1);
	}

	if (is_size_safe_to_change(cifsInfo)) {
		/* can not safely change the file size here if the 
		client is writing to it due to potential races */
		i_size_write(tmp_inode, end_of_file);

	/* 512 bytes (2**9) is the fake blocksize that must be used */
	/* for this calculation, even though the reported blocksize is larger */
		tmp_inode->i_blocks = (512 - 1 + allocation_size) >> 9;
	}

	if (allocation_size < end_of_file)
		cFYI(1, ("May be sparse file, allocation less than file size"));
	cFYI(1,
	     ("File Size %ld and blocks %ld and blocksize %ld",
	      (unsigned long)tmp_inode->i_size, tmp_inode->i_blocks,
	      tmp_inode->i_blksize));
	if (S_ISREG(tmp_inode->i_mode)) {
		cFYI(1, ("File inode"));
		tmp_inode->i_op = &cifs_file_inode_ops;
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO)
			tmp_inode->i_fop = &cifs_file_direct_ops;
		else
			tmp_inode->i_fop = &cifs_file_ops;
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			tmp_inode->i_fop->lock = NULL;
		tmp_inode->i_data.a_ops = &cifs_addr_ops;
		if((cifs_sb->tcon) && (cifs_sb->tcon->ses) &&
		   (cifs_sb->tcon->ses->server->maxBuf <
			4096 + MAX_CIFS_HDR_SIZE))
			tmp_inode->i_data.a_ops->readpages = NULL;
		if(isNewInode)
			return; /* No sense invalidating pages for new inode
				   since have not started caching readahead file
				   data yet */

		if (timespec_equal(&tmp_inode->i_mtime, &local_mtime) &&
			(local_size == tmp_inode->i_size)) {
			cFYI(1, ("inode exists but unchanged"));
		} else {
			/* file may have changed on server */
			cFYI(1, ("invalidate inode, readdir detected change"));
			invalidate_remote_inode(tmp_inode);
		}
	} else if (S_ISDIR(tmp_inode->i_mode)) {
		cFYI(1, ("Directory inode"));
		tmp_inode->i_op = &cifs_dir_inode_ops;
		tmp_inode->i_fop = &cifs_dir_ops;
	} else if (S_ISLNK(tmp_inode->i_mode)) {
		cFYI(1, ("Symbolic Link inode"));
		tmp_inode->i_op = &cifs_symlink_inode_ops;
	} else {
		cFYI(1, ("Init special inode"));
		init_special_inode(tmp_inode, tmp_inode->i_mode,
				   tmp_inode->i_rdev);
	}
}

static void unix_fill_in_inode(struct inode *tmp_inode,
	FILE_UNIX_INFO *pfindData, int *pobject_type, int isNewInode)
{
	loff_t local_size;
	struct timespec local_mtime;

	struct cifsInodeInfo *cifsInfo = CIFS_I(tmp_inode);
	struct cifs_sb_info *cifs_sb = CIFS_SB(tmp_inode->i_sb);

	__u32 type = le32_to_cpu(pfindData->Type);
	__u64 num_of_bytes = le64_to_cpu(pfindData->NumOfBytes);
	__u64 end_of_file = le64_to_cpu(pfindData->EndOfFile);
	cifsInfo->time = jiffies;
	atomic_inc(&cifsInfo->inUse);

	/* save mtime and size */
	local_mtime = tmp_inode->i_mtime;
	local_size  = tmp_inode->i_size;

	tmp_inode->i_atime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastAccessTime));
	tmp_inode->i_mtime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastModificationTime));
	tmp_inode->i_ctime =
	    cifs_NTtimeToUnix(le64_to_cpu(pfindData->LastStatusChange));

	tmp_inode->i_mode = le64_to_cpu(pfindData->Permissions);
	if (type == UNIX_FILE) {
		*pobject_type = DT_REG;
		tmp_inode->i_mode |= S_IFREG;
	} else if (type == UNIX_SYMLINK) {
		*pobject_type = DT_LNK;
		tmp_inode->i_mode |= S_IFLNK;
	} else if (type == UNIX_DIR) {
		*pobject_type = DT_DIR;
		tmp_inode->i_mode |= S_IFDIR;
	} else if (type == UNIX_CHARDEV) {
		*pobject_type = DT_CHR;
		tmp_inode->i_mode |= S_IFCHR;
		tmp_inode->i_rdev = MKDEV(le64_to_cpu(pfindData->DevMajor),
				le64_to_cpu(pfindData->DevMinor) & MINORMASK);
	} else if (type == UNIX_BLOCKDEV) {
		*pobject_type = DT_BLK;
		tmp_inode->i_mode |= S_IFBLK;
		tmp_inode->i_rdev = MKDEV(le64_to_cpu(pfindData->DevMajor),
				le64_to_cpu(pfindData->DevMinor) & MINORMASK);
	} else if (type == UNIX_FIFO) {
		*pobject_type = DT_FIFO;
		tmp_inode->i_mode |= S_IFIFO;
	} else if (type == UNIX_SOCKET) {
		*pobject_type = DT_SOCK;
		tmp_inode->i_mode |= S_IFSOCK;
	}

	tmp_inode->i_uid = le64_to_cpu(pfindData->Uid);
	tmp_inode->i_gid = le64_to_cpu(pfindData->Gid);
	tmp_inode->i_nlink = le64_to_cpu(pfindData->Nlinks);

	if (is_size_safe_to_change(cifsInfo)) {
		/* can not safely change the file size here if the 
		client is writing to it due to potential races */
		i_size_write(tmp_inode,end_of_file);

	/* 512 bytes (2**9) is the fake blocksize that must be used */
	/* for this calculation, not the real blocksize */
		tmp_inode->i_blocks = (512 - 1 + num_of_bytes) >> 9;
	}

	if (S_ISREG(tmp_inode->i_mode)) {
		cFYI(1, ("File inode"));
		tmp_inode->i_op = &cifs_file_inode_ops;
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_DIRECT_IO)
			tmp_inode->i_fop = &cifs_file_direct_ops;
		else
			tmp_inode->i_fop = &cifs_file_ops;
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_NO_BRL)
			tmp_inode->i_fop->lock = NULL;
		tmp_inode->i_data.a_ops = &cifs_addr_ops;
		if((cifs_sb->tcon) && (cifs_sb->tcon->ses) &&
		   (cifs_sb->tcon->ses->server->maxBuf < 
			4096 + MAX_CIFS_HDR_SIZE))
			tmp_inode->i_data.a_ops->readpages = NULL;

		if(isNewInode)
			return; /* No sense invalidating pages for new inode since we
					   have not started caching readahead file data yet */

		if (timespec_equal(&tmp_inode->i_mtime, &local_mtime) &&
			(local_size == tmp_inode->i_size)) {
			cFYI(1, ("inode exists but unchanged"));
		} else {
			/* file may have changed on server */
			cFYI(1, ("invalidate inode, readdir detected change"));
			invalidate_remote_inode(tmp_inode);
		}
	} else if (S_ISDIR(tmp_inode->i_mode)) {
		cFYI(1, ("Directory inode"));
		tmp_inode->i_op = &cifs_dir_inode_ops;
		tmp_inode->i_fop = &cifs_dir_ops;
	} else if (S_ISLNK(tmp_inode->i_mode)) {
		cFYI(1, ("Symbolic Link inode"));
		tmp_inode->i_op = &cifs_symlink_inode_ops;
/* tmp_inode->i_fop = *//* do not need to set to anything */
	} else {
		cFYI(1, ("Special inode")); 
		init_special_inode(tmp_inode, tmp_inode->i_mode,
				   tmp_inode->i_rdev);
	}
}

static int initiate_cifs_search(const int xid, struct file *file)
{
	int rc = 0;
	char * full_path;
	struct cifsFileInfo * cifsFile;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;

	if(file->private_data == NULL) {
		file->private_data = 
			kmalloc(sizeof(struct cifsFileInfo),GFP_KERNEL);
	}

	if(file->private_data == NULL) {
		return -ENOMEM;
	} else {
		memset(file->private_data,0,sizeof(struct cifsFileInfo));
	}
	cifsFile = file->private_data;
	cifsFile->invalidHandle = TRUE;
	cifsFile->srch_inf.endOfSearch = FALSE;

	if(file->f_dentry == NULL)
		return -ENOENT;

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	if(cifs_sb == NULL)
		return -EINVAL;

	pTcon = cifs_sb->tcon;
	if(pTcon == NULL)
		return -EINVAL;

	down(&file->f_dentry->d_sb->s_vfs_rename_sem);
	full_path = build_path_from_dentry(file->f_dentry);
	up(&file->f_dentry->d_sb->s_vfs_rename_sem);

	if(full_path == NULL) {
		return -ENOMEM;
	}

	cFYI(1, ("Full path: %s start at: %lld", full_path, file->f_pos));

ffirst_retry:
	/* test for Unix extensions */
	if (pTcon->ses->capabilities & CAP_UNIX) {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_UNIX; 
	} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_ID_FULL_DIR_INFO;
	} else /* not srvinos - BB fixme add check for backlevel? */ {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_DIRECTORY_INFO;
	}

	rc = CIFSFindFirst(xid, pTcon,full_path,cifs_sb->local_nls,
		&cifsFile->netfid, &cifsFile->srch_inf,
		cifs_sb->mnt_cifs_flags & 
			CIFS_MOUNT_MAP_SPECIAL_CHR, CIFS_DIR_SEP(cifs_sb));
	if(rc == 0)
		cifsFile->invalidHandle = FALSE;
	if((rc == -EOPNOTSUPP) && 
		(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)) {
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_SERVER_INUM;
		goto ffirst_retry;
	}
	kfree(full_path);
	return rc;
}

/* return length of unicode string in bytes */
static int cifs_unicode_bytelen(char *str)
{
	int len;
	__le16 * ustr = (__le16 *)str;

	for(len=0;len <= PATH_MAX;len++) {
		if(ustr[len] == 0)
			return len << 1;
	}
	cFYI(1,("Unicode string longer than PATH_MAX found"));
	return len << 1;
}

static char *nxt_dir_entry(char *old_entry, char *end_of_smb)
{
	char * new_entry;
	FILE_DIRECTORY_INFO * pDirInfo = (FILE_DIRECTORY_INFO *)old_entry;

	new_entry = old_entry + le32_to_cpu(pDirInfo->NextEntryOffset);
	cFYI(1,("new entry %p old entry %p",new_entry,old_entry));
	/* validate that new_entry is not past end of SMB */
	if(new_entry >= end_of_smb) {
		cERROR(1,
		      ("search entry %p began after end of SMB %p old entry %p",
			new_entry, end_of_smb, old_entry)); 
		return NULL;
	} else if (new_entry + sizeof(FILE_DIRECTORY_INFO) > end_of_smb) {
		cERROR(1,("search entry %p extends after end of SMB %p",
			new_entry, end_of_smb));
		return NULL;
	} else 
		return new_entry;

}

#define UNICODE_DOT cpu_to_le16(0x2e)

/* return 0 if no match and 1 for . (current directory) and 2 for .. (parent) */
static int cifs_entry_is_dot(char *current_entry, struct cifsFileInfo *cfile)
{
	int rc = 0;
	char * filename = NULL;
	int len = 0; 

	if(cfile->srch_inf.info_level == 0x202) {
		FILE_UNIX_INFO * pFindData = (FILE_UNIX_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		if(cfile->srch_inf.unicode) {
			len = cifs_unicode_bytelen(filename);
		} else {
			/* BB should we make this strnlen of PATH_MAX? */
			len = strnlen(filename, 5);
		}
	} else if(cfile->srch_inf.info_level == 0x101) {
		FILE_DIRECTORY_INFO * pFindData = 
			(FILE_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if(cfile->srch_inf.info_level == 0x102) {
		FILE_FULL_DIRECTORY_INFO * pFindData = 
			(FILE_FULL_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if(cfile->srch_inf.info_level == 0x105) {
		SEARCH_ID_FULL_DIR_INFO * pFindData = 
			(SEARCH_ID_FULL_DIR_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if(cfile->srch_inf.info_level == 0x104) {
		FILE_BOTH_DIRECTORY_INFO * pFindData = 
			(FILE_BOTH_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else {
		cFYI(1,("Unknown findfirst level %d",cfile->srch_inf.info_level));
	}

	if(filename) {
		if(cfile->srch_inf.unicode) {
			__le16 *ufilename = (__le16 *)filename;
			if(len == 2) {
				/* check for . */
				if(ufilename[0] == UNICODE_DOT)
					rc = 1;
			} else if(len == 4) {
				/* check for .. */
				if((ufilename[0] == UNICODE_DOT)
				   &&(ufilename[1] == UNICODE_DOT))
					rc = 2;
			}
		} else /* ASCII */ {
			if(len == 1) {
				if(filename[0] == '.') 
					rc = 1;
			} else if(len == 2) {
				if((filename[0] == '.') && (filename[1] == '.')) 
					rc = 2;
			}
		}
	}

	return rc;
}

/* Check if directory that we are searching has changed so we can decide
   whether we can use the cached search results from the previous search */
static int is_dir_changed(struct file * file)
{
	struct inode * inode;
	struct cifsInodeInfo *cifsInfo;

	if(file->f_dentry == NULL)
		return 0;

	inode = file->f_dentry->d_inode;

	if(inode == NULL)
		return 0;

	cifsInfo = CIFS_I(inode);

	if(cifsInfo->time == 0)
		return 1; /* directory was changed, perhaps due to unlink */
	else
		return 0;

}

/* find the corresponding entry in the search */
/* Note that the SMB server returns search entries for . and .. which
   complicates logic here if we choose to parse for them and we do not
   assume that they are located in the findfirst return buffer.*/
/* We start counting in the buffer with entry 2 and increment for every
   entry (do not increment for . or .. entry) */
static int find_cifs_entry(const int xid, struct cifsTconInfo *pTcon,
	struct file *file, char **ppCurrentEntry, int *num_to_ret) 
{
	int rc = 0;
	int pos_in_buf = 0;
	loff_t first_entry_in_buffer;
	loff_t index_to_find = file->f_pos;
	struct cifsFileInfo * cifsFile = file->private_data;
	/* check if index in the buffer */
	
	if((cifsFile == NULL) || (ppCurrentEntry == NULL) || 
	   (num_to_ret == NULL))
		return -ENOENT;
	
	*ppCurrentEntry = NULL;
	first_entry_in_buffer = 
		cifsFile->srch_inf.index_of_last_entry - 
			cifsFile->srch_inf.entries_in_buffer;
/*	dump_cifs_file_struct(file, "In fce ");*/
	if(((index_to_find < cifsFile->srch_inf.index_of_last_entry) && 
	     is_dir_changed(file)) || 
	   (index_to_find < first_entry_in_buffer)) {
		/* close and restart search */
		cFYI(1,("search backing up - close and restart search"));
		cifsFile->invalidHandle = TRUE;
		CIFSFindClose(xid, pTcon, cifsFile->netfid);
		kfree(cifsFile->search_resume_name);
		cifsFile->search_resume_name = NULL;
		if(cifsFile->srch_inf.ntwrk_buf_start) {
			cFYI(1,("freeing SMB ff cache buf on search rewind"));
			cifs_buf_release(cifsFile->srch_inf.ntwrk_buf_start);
		}
		rc = initiate_cifs_search(xid,file);
		if(rc) {
			cFYI(1,("error %d reinitiating a search on rewind",rc));
			return rc;
		}
	}

	while((index_to_find >= cifsFile->srch_inf.index_of_last_entry) && 
	      (rc == 0) && (cifsFile->srch_inf.endOfSearch == FALSE)){
	 	cFYI(1,("calling findnext2"));
		rc = CIFSFindNext(xid,pTcon,cifsFile->netfid, 
				  &cifsFile->srch_inf);
		if(rc)
			return -ENOENT;
	}
	if(index_to_find < cifsFile->srch_inf.index_of_last_entry) {
		/* we found the buffer that contains the entry */
		/* scan and find it */
		int i;
		char * current_entry;
		char * end_of_smb = cifsFile->srch_inf.ntwrk_buf_start + 
			smbCalcSize((struct smb_hdr *)
				cifsFile->srch_inf.ntwrk_buf_start);
		first_entry_in_buffer = cifsFile->srch_inf.index_of_last_entry
					- cifsFile->srch_inf.entries_in_buffer;
		pos_in_buf = index_to_find - first_entry_in_buffer;
		cFYI(1,("found entry - pos_in_buf %d",pos_in_buf)); 
		current_entry = cifsFile->srch_inf.srch_entries_start;
		for(i=0;(i<(pos_in_buf)) && (current_entry != NULL);i++) {
			/* go entry by entry figuring out which is first */
			/* if( . or ..)
				skip */
			rc = cifs_entry_is_dot(current_entry,cifsFile);
			if(rc == 1) /* is . or .. so skip */ {
				cFYI(1,("Entry is .")); /* BB removeme BB */
				/* continue; */
			} else if (rc == 2 ) {
				cFYI(1,("Entry is ..")); /* BB removeme BB */
				/* continue; */
			}
			current_entry = nxt_dir_entry(current_entry,end_of_smb);
		}
		if((current_entry == NULL) && (i < pos_in_buf)) {
			/* BB fixme - check if we should flag this error */
			cERROR(1,("reached end of buf searching for pos in buf"
			  " %d index to find %lld rc %d",
			  pos_in_buf,index_to_find,rc));
		}
		rc = 0;
		*ppCurrentEntry = current_entry;
	} else {
		cFYI(1,("index not in buffer - could not findnext into it"));
		return 0;
	}

	if(pos_in_buf >= cifsFile->srch_inf.entries_in_buffer) {
		cFYI(1,("can not return entries pos_in_buf beyond last entry"));
		*num_to_ret = 0;
	} else
		*num_to_ret = cifsFile->srch_inf.entries_in_buffer - pos_in_buf;

	return rc;
}

/* inode num, inode type and filename returned */
static int cifs_get_name_from_search_buf(struct qstr *pqst,
	char *current_entry, __u16 level, unsigned int unicode,
	struct cifs_sb_info * cifs_sb, ino_t *pinum)
{
	int rc = 0;
	unsigned int len = 0;
	char * filename;
	struct nls_table * nlt = cifs_sb->local_nls;

	*pinum = 0;

	if(level == SMB_FIND_FILE_UNIX) {
		FILE_UNIX_INFO * pFindData = (FILE_UNIX_INFO *)current_entry;

		filename = &pFindData->FileName[0];
		if(unicode) {
			len = cifs_unicode_bytelen(filename);
		} else {
			/* BB should we make this strnlen of PATH_MAX? */
			len = strnlen(filename, PATH_MAX);
		}

		/* BB fixme - hash low and high 32 bits if not 64 bit arch BB fixme */
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)
			*pinum = pFindData->UniqueId;
	} else if(level == SMB_FIND_FILE_DIRECTORY_INFO) {
		FILE_DIRECTORY_INFO * pFindData = 
			(FILE_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if(level == SMB_FIND_FILE_FULL_DIRECTORY_INFO) {
		FILE_FULL_DIRECTORY_INFO * pFindData = 
			(FILE_FULL_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else if(level == SMB_FIND_FILE_ID_FULL_DIR_INFO) {
		SEARCH_ID_FULL_DIR_INFO * pFindData = 
			(SEARCH_ID_FULL_DIR_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		*pinum = pFindData->UniqueId;
	} else if(level == SMB_FIND_FILE_BOTH_DIRECTORY_INFO) {
		FILE_BOTH_DIRECTORY_INFO * pFindData = 
			(FILE_BOTH_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
	} else {
		cFYI(1,("Unknown findfirst level %d",level));
		return -EINVAL;
	}
	if(unicode) {
		/* BB fixme - test with long names */
		/* Note converted filename can be longer than in unicode */
		if(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR)
			pqst->len = cifs_convertUCSpath((char *)pqst->name,
					(__le16 *)filename, len/2, nlt);
		else
			pqst->len = cifs_strfromUCS_le((char *)pqst->name,
					(wchar_t *)filename,len/2,nlt);
	} else {
		pqst->name = filename;
		pqst->len = len;
	}
	pqst->hash = full_name_hash(pqst->name,pqst->len);
/*	cFYI(1,("filldir on %s",pqst->name));  */
	return rc;
}

static int cifs_filldir(char *pfindEntry, struct file *file,
	filldir_t filldir, void *direntry, char *scratch_buf)
{
	int rc = 0;
	struct qstr qstring;
	struct cifsFileInfo * pCifsF;
	unsigned obj_type;
	ino_t  inum;
	struct cifs_sb_info * cifs_sb;
	struct inode *tmp_inode;
	struct dentry *tmp_dentry;

	/* get filename and len into qstring */
	/* get dentry */
	/* decide whether to create and populate ionde */
	if((direntry == NULL) || (file == NULL))
		return -EINVAL;

	pCifsF = file->private_data;
	
	if((scratch_buf == NULL) || (pfindEntry == NULL) || (pCifsF == NULL))
		return -ENOENT;

	if(file->f_dentry == NULL)
		return -ENOENT;

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);

	qstring.name = scratch_buf;
	rc = cifs_get_name_from_search_buf(&qstring,pfindEntry,
			pCifsF->srch_inf.info_level,
			pCifsF->srch_inf.unicode,cifs_sb,
			&inum /* returned */);

	if(rc)
		return rc;

	rc = construct_dentry(&qstring,file,&tmp_inode, &tmp_dentry);
	if((tmp_inode == NULL) || (tmp_dentry == NULL))
		return -ENOMEM;

	if(rc) {
		/* inode created, we need to hash it with right inode number */
		if(inum != 0) {
			/* BB fixme - hash the 2 32 quantities bits together if necessary BB */
			tmp_inode->i_ino = inum;
		}
		insert_inode_hash(tmp_inode);
	}

	/* we pass in rc below, indicating whether it is a new inode,
	   so we can figure out whether to invalidate the inode cached
	   data if the file has changed */
	if(pCifsF->srch_inf.info_level == SMB_FIND_FILE_UNIX) {
		unix_fill_in_inode(tmp_inode,
				   (FILE_UNIX_INFO *)pfindEntry,&obj_type, rc);
	} else {
		fill_in_inode(tmp_inode,
			      (FILE_DIRECTORY_INFO *)pfindEntry,&obj_type, rc);
	}
	
	rc = filldir(direntry,qstring.name,qstring.len,file->f_pos,
		     tmp_inode->i_ino,obj_type);
	if(rc) {
		cFYI(1,("filldir rc = %d",rc));
	}

	dput(tmp_dentry);
	return rc;
}

static int cifs_save_resume_key(const char *current_entry,
	struct cifsFileInfo *cifsFile)
{
	int rc = 0;
	unsigned int len = 0;
	__u16 level;
	char * filename;

	if((cifsFile == NULL) || (current_entry == NULL))
		return -EINVAL;

	level = cifsFile->srch_inf.info_level;

	if(level == SMB_FIND_FILE_UNIX) {
		FILE_UNIX_INFO * pFindData = (FILE_UNIX_INFO *)current_entry;

		filename = &pFindData->FileName[0];
		if(cifsFile->srch_inf.unicode) {
			len = cifs_unicode_bytelen(filename);
		} else {
			/* BB should we make this strnlen of PATH_MAX? */
			len = strnlen(filename, PATH_MAX);
		}
		cifsFile->srch_inf.resume_key = pFindData->ResumeKey;
	} else if(level == SMB_FIND_FILE_DIRECTORY_INFO) {
		FILE_DIRECTORY_INFO * pFindData = 
			(FILE_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		cifsFile->srch_inf.resume_key = pFindData->FileIndex;
	} else if(level == SMB_FIND_FILE_FULL_DIRECTORY_INFO) {
		FILE_FULL_DIRECTORY_INFO * pFindData = 
			(FILE_FULL_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		cifsFile->srch_inf.resume_key = pFindData->FileIndex;
	} else if(level == SMB_FIND_FILE_ID_FULL_DIR_INFO) {
		SEARCH_ID_FULL_DIR_INFO * pFindData = 
			(SEARCH_ID_FULL_DIR_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		cifsFile->srch_inf.resume_key = pFindData->FileIndex;
	} else if(level == SMB_FIND_FILE_BOTH_DIRECTORY_INFO) {
		FILE_BOTH_DIRECTORY_INFO * pFindData = 
			(FILE_BOTH_DIRECTORY_INFO *)current_entry;
		filename = &pFindData->FileName[0];
		len = le32_to_cpu(pFindData->FileNameLength);
		cifsFile->srch_inf.resume_key = pFindData->FileIndex;
	} else {
		cFYI(1,("Unknown findfirst level %d",level));
		return -EINVAL;
	}
	cifsFile->srch_inf.resume_name_len = len;
	cifsFile->srch_inf.presume_name = filename;
	return rc;
}

int cifs_readdir(struct file *file, void *direntry, filldir_t filldir)
{
	int rc = 0;
	int xid,i;
	struct cifs_sb_info *cifs_sb;
	struct cifsTconInfo *pTcon;
	struct cifsFileInfo *cifsFile = NULL;
	char * current_entry;
	int num_to_fill = 0;
	char * tmp_buf = NULL;
	char * end_of_smb;

	xid = GetXid();

	if(file->f_dentry == NULL) {
		FreeXid(xid);
		return -EIO;
	}

	cifs_sb = CIFS_SB(file->f_dentry->d_sb);
	pTcon = cifs_sb->tcon;
	if(pTcon == NULL)
		return -EINVAL;

	switch ((int) file->f_pos) {
	case 0:
		/*if (filldir(direntry, ".", 1, file->f_pos,
		     file->f_dentry->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("Filldir for current dir failed "));
			rc = -ENOMEM;
			break;
		}
		file->f_pos++; */
	case 1:
		/* if (filldir(direntry, "..", 2, file->f_pos,
		     file->f_dentry->d_parent->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, ("Filldir for parent dir failed "));
			rc = -ENOMEM;
			break;
		}
		file->f_pos++; */
	case 2:
		/* 1) If search is active, 
			is in current search buffer? 
			if it before then restart search
			if after then keep searching till find it */

		if(file->private_data == NULL) {
			rc = initiate_cifs_search(xid,file);
			cFYI(1,("initiate cifs search rc %d",rc));
			if(rc) {
				FreeXid(xid);
				return rc;
			}
		}
	default:
		if(file->private_data == NULL) {
			rc = -EINVAL;
			FreeXid(xid);
			return rc;
		}
		cifsFile = file->private_data;
		if (cifsFile->srch_inf.endOfSearch) {
			if(cifsFile->srch_inf.emptyDir) {
				cFYI(1, ("End of search, empty dir"));
				rc = 0;
				break;
			}
		} /* else {
			cifsFile->invalidHandle = TRUE;
			CIFSFindClose(xid, pTcon, cifsFile->netfid);
		} 
		kfree(cifsFile->search_resume_name);
		cifsFile->search_resume_name = NULL; */

		/* BB account for . and .. in f_pos as special case */

		rc = find_cifs_entry(xid,pTcon, file,
				&current_entry,&num_to_fill);
		if(rc) {
			cFYI(1,("fce error %d",rc)); 
			goto rddir2_exit;
		} else if (current_entry != NULL) {
			cFYI(1,("entry %lld found",file->f_pos));
		} else {
			cFYI(1,("could not find entry"));
			goto rddir2_exit;
		}
		cFYI(1,("loop through %d times filling dir for net buf %p",
			num_to_fill,cifsFile->srch_inf.ntwrk_buf_start)); 
		end_of_smb = cifsFile->srch_inf.ntwrk_buf_start +
			smbCalcSize((struct smb_hdr *)
				    cifsFile->srch_inf.ntwrk_buf_start);
		/* To be safe - for UCS to UTF-8 with strings loaded
		with the rare long characters alloc more to account for
		such multibyte target UTF-8 characters. cifs_unicode.c,
		which actually does the conversion, has the same limit */
		tmp_buf = kmalloc((2 * NAME_MAX) + 4, GFP_KERNEL);
		for(i=0;(i<num_to_fill) && (rc == 0);i++) {
			if(current_entry == NULL) {
				/* evaluate whether this case is an error */
				cERROR(1,("past end of SMB num to fill %d i %d",
					  num_to_fill, i));
				break;
			}

			rc = cifs_filldir(current_entry, file, 
					filldir, direntry,tmp_buf);
			file->f_pos++;
			if(file->f_pos == cifsFile->srch_inf.index_of_last_entry) {
				cFYI(1,("last entry in buf at pos %lld %s",
					file->f_pos,tmp_buf)); /* BB removeme BB */
				cifs_save_resume_key(current_entry,cifsFile);
				break;
			} else 
				current_entry = nxt_dir_entry(current_entry,
							      end_of_smb);
		}
		kfree(tmp_buf);
		break;
	} /* end switch */

rddir2_exit:
	FreeXid(xid);
	return rc;
}
