/*
 *   fs/cifs/readdir.c
 *
 *   Directory search handling
 *
 *   Copyright (C) International Business Machines  Corp., 2004, 2008
 *   Copyright (C) Red Hat, Inc., 2011
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
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include "cifspdu.h"
#include "cifsglob.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "cifs_fs_sb.h"
#include "cifsfs.h"

/*
 * To be safe - for UCS to UTF-8 with strings loaded with the rare long
 * characters alloc more to account for such multibyte target UTF-8
 * characters.
 */
#define UNICODE_NAME_MAX ((4 * NAME_MAX) + 2)

#ifdef CONFIG_CIFS_DEBUG2
static void dump_cifs_file_struct(struct file *file, char *label)
{
	struct cifsFileInfo *cf;

	if (file) {
		cf = file->private_data;
		if (cf == NULL) {
			cFYI(1, "empty cifs private file data");
			return;
		}
		if (cf->invalidHandle)
			cFYI(1, "invalid handle");
		if (cf->srch_inf.endOfSearch)
			cFYI(1, "end of search");
		if (cf->srch_inf.emptyDir)
			cFYI(1, "empty dir");
	}
}
#else
static inline void dump_cifs_file_struct(struct file *file, char *label)
{
}
#endif /* DEBUG2 */

/*
 * Find the dentry that matches "name". If there isn't one, create one. If it's
 * a negative dentry or the uniqueid changed, then drop it and recreate it.
 */
static struct dentry *
cifs_readdir_lookup(struct dentry *parent, struct qstr *name,
		    struct cifs_fattr *fattr)
{
	struct dentry *dentry, *alias;
	struct inode *inode;
	struct super_block *sb = parent->d_inode->i_sb;

	cFYI(1, "For %s", name->name);

	if (parent->d_op && parent->d_op->d_hash)
		parent->d_op->d_hash(parent, parent->d_inode, name);
	else
		name->hash = full_name_hash(name->name, name->len);

	dentry = d_lookup(parent, name);
	if (dentry) {
		/* FIXME: check for inode number changes? */
		if (dentry->d_inode != NULL)
			return dentry;
		d_drop(dentry);
		dput(dentry);
	}

	dentry = d_alloc(parent, name);
	if (dentry == NULL)
		return NULL;

	inode = cifs_iget(sb, fattr);
	if (!inode) {
		dput(dentry);
		return NULL;
	}

	alias = d_materialise_unique(dentry, inode);
	if (alias != NULL) {
		dput(dentry);
		if (IS_ERR(alias))
			return NULL;
		dentry = alias;
	}

	return dentry;
}

static void
cifs_fill_common_info(struct cifs_fattr *fattr, struct cifs_sb_info *cifs_sb)
{
	fattr->cf_uid = cifs_sb->mnt_uid;
	fattr->cf_gid = cifs_sb->mnt_gid;

	if (fattr->cf_cifsattrs & ATTR_DIRECTORY) {
		fattr->cf_mode = S_IFDIR | cifs_sb->mnt_dir_mode;
		fattr->cf_dtype = DT_DIR;
	} else {
		fattr->cf_mode = S_IFREG | cifs_sb->mnt_file_mode;
		fattr->cf_dtype = DT_REG;
	}

	if (fattr->cf_cifsattrs & ATTR_READONLY)
		fattr->cf_mode &= ~S_IWUGO;

	if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_UNX_EMUL &&
	    fattr->cf_cifsattrs & ATTR_SYSTEM) {
		if (fattr->cf_eof == 0)  {
			fattr->cf_mode &= ~S_IFMT;
			fattr->cf_mode |= S_IFIFO;
			fattr->cf_dtype = DT_FIFO;
		} else {
			/*
			 * trying to get the type and mode via SFU can be slow,
			 * so just call those regular files for now, and mark
			 * for reval
			 */
			fattr->cf_flags |= CIFS_FATTR_NEED_REVAL;
		}
	}
}

static void
cifs_dir_info_to_fattr(struct cifs_fattr *fattr, FILE_DIRECTORY_INFO *info,
		       struct cifs_sb_info *cifs_sb)
{
	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_cifsattrs = le32_to_cpu(info->ExtFileAttributes);
	fattr->cf_eof = le64_to_cpu(info->EndOfFile);
	fattr->cf_bytes = le64_to_cpu(info->AllocationSize);
	fattr->cf_createtime = le64_to_cpu(info->CreationTime);
	fattr->cf_atime = cifs_NTtimeToUnix(info->LastAccessTime);
	fattr->cf_ctime = cifs_NTtimeToUnix(info->ChangeTime);
	fattr->cf_mtime = cifs_NTtimeToUnix(info->LastWriteTime);

	cifs_fill_common_info(fattr, cifs_sb);
}

static void
cifs_std_info_to_fattr(struct cifs_fattr *fattr, FIND_FILE_STANDARD_INFO *info,
		       struct cifs_sb_info *cifs_sb)
{
	int offset = cifs_sb_master_tcon(cifs_sb)->ses->server->timeAdj;

	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_atime = cnvrtDosUnixTm(info->LastAccessDate,
					    info->LastAccessTime, offset);
	fattr->cf_ctime = cnvrtDosUnixTm(info->LastWriteDate,
					    info->LastWriteTime, offset);
	fattr->cf_mtime = cnvrtDosUnixTm(info->LastWriteDate,
					    info->LastWriteTime, offset);

	fattr->cf_cifsattrs = le16_to_cpu(info->Attributes);
	fattr->cf_bytes = le32_to_cpu(info->AllocationSize);
	fattr->cf_eof = le32_to_cpu(info->DataSize);

	cifs_fill_common_info(fattr, cifs_sb);
}

/* BB eventually need to add the following helper function to
      resolve NT_STATUS_STOPPED_ON_SYMLINK return code when
      we try to do FindFirst on (NTFS) directory symlinks */
/*
int get_symlink_reparse_path(char *full_path, struct cifs_sb_info *cifs_sb,
			     int xid)
{
	__u16 fid;
	int len;
	int oplock = 0;
	int rc;
	struct cifs_tcon *ptcon = cifs_sb_tcon(cifs_sb);
	char *tmpbuffer;

	rc = CIFSSMBOpen(xid, ptcon, full_path, FILE_OPEN, GENERIC_READ,
			OPEN_REPARSE_POINT, &fid, &oplock, NULL,
			cifs_sb->local_nls,
			cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MAP_SPECIAL_CHR);
	if (!rc) {
		tmpbuffer = kmalloc(maxpath);
		rc = CIFSSMBQueryReparseLinkInfo(xid, ptcon, full_path,
				tmpbuffer,
				maxpath -1,
				fid,
				cifs_sb->local_nls);
		if (CIFSSMBClose(xid, ptcon, fid)) {
			cFYI(1, "Error closing temporary reparsepoint open");
		}
	}
}
 */

static int initiate_cifs_search(const int xid, struct file *file)
{
	__u16 search_flags;
	int rc = 0;
	char *full_path = NULL;
	struct cifsFileInfo *cifsFile;
	struct cifs_sb_info *cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	struct tcon_link *tlink = NULL;
	struct cifs_tcon *pTcon;

	if (file->private_data == NULL) {
		tlink = cifs_sb_tlink(cifs_sb);
		if (IS_ERR(tlink))
			return PTR_ERR(tlink);

		cifsFile = kzalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
		if (cifsFile == NULL) {
			rc = -ENOMEM;
			goto error_exit;
		}
		file->private_data = cifsFile;
		cifsFile->tlink = cifs_get_tlink(tlink);
		pTcon = tlink_tcon(tlink);
	} else {
		cifsFile = file->private_data;
		pTcon = tlink_tcon(cifsFile->tlink);
	}

	cifsFile->invalidHandle = true;
	cifsFile->srch_inf.endOfSearch = false;

	full_path = build_path_from_dentry(file->f_path.dentry);
	if (full_path == NULL) {
		rc = -ENOMEM;
		goto error_exit;
	}

	cFYI(1, "Full path: %s start at: %lld", full_path, file->f_pos);

ffirst_retry:
	/* test for Unix extensions */
	/* but now check for them on the share/mount not on the SMB session */
/*	if (pTcon->ses->capabilities & CAP_UNIX) { */
	if (pTcon->unix_ext)
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_UNIX;
	else if ((pTcon->ses->capabilities &
			(CAP_NT_SMBS | CAP_NT_FIND)) == 0) {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_INFO_STANDARD;
	} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_ID_FULL_DIR_INFO;
	} else /* not srvinos - BB fixme add check for backlevel? */ {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_DIRECTORY_INFO;
	}

	search_flags = CIFS_SEARCH_CLOSE_AT_END | CIFS_SEARCH_RETURN_RESUME;
	if (backup_cred(cifs_sb))
		search_flags |= CIFS_SEARCH_BACKUP_SEARCH;

	rc = CIFSFindFirst(xid, pTcon, full_path, cifs_sb->local_nls,
		&cifsFile->netfid, search_flags, &cifsFile->srch_inf,
		cifs_sb->mnt_cifs_flags &
			CIFS_MOUNT_MAP_SPECIAL_CHR, CIFS_DIR_SEP(cifs_sb));
	if (rc == 0)
		cifsFile->invalidHandle = false;
	/* BB add following call to handle readdir on new NTFS symlink errors
	else if STATUS_STOPPED_ON_SYMLINK
		call get_symlink_reparse_path and retry with new path */
	else if ((rc == -EOPNOTSUPP) &&
		(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)) {
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_SERVER_INUM;
		goto ffirst_retry;
	}
error_exit:
	kfree(full_path);
	cifs_put_tlink(tlink);
	return rc;
}

/* return length of unicode string in bytes */
static int cifs_unicode_bytelen(const char *str)
{
	int len;
	const __le16 *ustr = (const __le16 *)str;

	for (len = 0; len <= PATH_MAX; len++) {
		if (ustr[len] == 0)
			return len << 1;
	}
	cFYI(1, "Unicode string longer than PATH_MAX found");
	return len << 1;
}

static char *nxt_dir_entry(char *old_entry, char *end_of_smb, int level)
{
	char *new_entry;
	FILE_DIRECTORY_INFO *pDirInfo = (FILE_DIRECTORY_INFO *)old_entry;

	if (level == SMB_FIND_FILE_INFO_STANDARD) {
		FIND_FILE_STANDARD_INFO *pfData;
		pfData = (FIND_FILE_STANDARD_INFO *)pDirInfo;

		new_entry = old_entry + sizeof(FIND_FILE_STANDARD_INFO) +
				pfData->FileNameLength;
	} else
		new_entry = old_entry + le32_to_cpu(pDirInfo->NextEntryOffset);
	cFYI(1, "new entry %p old entry %p", new_entry, old_entry);
	/* validate that new_entry is not past end of SMB */
	if (new_entry >= end_of_smb) {
		cERROR(1, "search entry %p began after end of SMB %p old entry %p",
			new_entry, end_of_smb, old_entry);
		return NULL;
	} else if (((level == SMB_FIND_FILE_INFO_STANDARD) &&
		    (new_entry + sizeof(FIND_FILE_STANDARD_INFO) > end_of_smb))
		  || ((level != SMB_FIND_FILE_INFO_STANDARD) &&
		   (new_entry + sizeof(FILE_DIRECTORY_INFO) > end_of_smb)))  {
		cERROR(1, "search entry %p extends after end of SMB %p",
			new_entry, end_of_smb);
		return NULL;
	} else
		return new_entry;

}

struct cifs_dirent {
	const char	*name;
	size_t		namelen;
	u32		resume_key;
	u64		ino;
};

static void cifs_fill_dirent_unix(struct cifs_dirent *de,
		const FILE_UNIX_INFO *info, bool is_unicode)
{
	de->name = &info->FileName[0];
	if (is_unicode)
		de->namelen = cifs_unicode_bytelen(de->name);
	else
		de->namelen = strnlen(de->name, PATH_MAX);
	de->resume_key = info->ResumeKey;
	de->ino = le64_to_cpu(info->basic.UniqueId);
}

static void cifs_fill_dirent_dir(struct cifs_dirent *de,
		const FILE_DIRECTORY_INFO *info)
{
	de->name = &info->FileName[0];
	de->namelen = le32_to_cpu(info->FileNameLength);
	de->resume_key = info->FileIndex;
}

static void cifs_fill_dirent_full(struct cifs_dirent *de,
		const FILE_FULL_DIRECTORY_INFO *info)
{
	de->name = &info->FileName[0];
	de->namelen = le32_to_cpu(info->FileNameLength);
	de->resume_key = info->FileIndex;
}

static void cifs_fill_dirent_search(struct cifs_dirent *de,
		const SEARCH_ID_FULL_DIR_INFO *info)
{
	de->name = &info->FileName[0];
	de->namelen = le32_to_cpu(info->FileNameLength);
	de->resume_key = info->FileIndex;
	de->ino = le64_to_cpu(info->UniqueId);
}

static void cifs_fill_dirent_both(struct cifs_dirent *de,
		const FILE_BOTH_DIRECTORY_INFO *info)
{
	de->name = &info->FileName[0];
	de->namelen = le32_to_cpu(info->FileNameLength);
	de->resume_key = info->FileIndex;
}

static void cifs_fill_dirent_std(struct cifs_dirent *de,
		const FIND_FILE_STANDARD_INFO *info)
{
	de->name = &info->FileName[0];
	/* one byte length, no endianess conversion */
	de->namelen = info->FileNameLength;
	de->resume_key = info->ResumeKey;
}

static int cifs_fill_dirent(struct cifs_dirent *de, const void *info,
		u16 level, bool is_unicode)
{
	memset(de, 0, sizeof(*de));

	switch (level) {
	case SMB_FIND_FILE_UNIX:
		cifs_fill_dirent_unix(de, info, is_unicode);
		break;
	case SMB_FIND_FILE_DIRECTORY_INFO:
		cifs_fill_dirent_dir(de, info);
		break;
	case SMB_FIND_FILE_FULL_DIRECTORY_INFO:
		cifs_fill_dirent_full(de, info);
		break;
	case SMB_FIND_FILE_ID_FULL_DIR_INFO:
		cifs_fill_dirent_search(de, info);
		break;
	case SMB_FIND_FILE_BOTH_DIRECTORY_INFO:
		cifs_fill_dirent_both(de, info);
		break;
	case SMB_FIND_FILE_INFO_STANDARD:
		cifs_fill_dirent_std(de, info);
		break;
	default:
		cFYI(1, "Unknown findfirst level %d", level);
		return -EINVAL;
	}

	return 0;
}

#define UNICODE_DOT cpu_to_le16(0x2e)

/* return 0 if no match and 1 for . (current directory) and 2 for .. (parent) */
static int cifs_entry_is_dot(struct cifs_dirent *de, bool is_unicode)
{
	int rc = 0;

	if (!de->name)
		return 0;

	if (is_unicode) {
		__le16 *ufilename = (__le16 *)de->name;
		if (de->namelen == 2) {
			/* check for . */
			if (ufilename[0] == UNICODE_DOT)
				rc = 1;
		} else if (de->namelen == 4) {
			/* check for .. */
			if (ufilename[0] == UNICODE_DOT &&
			    ufilename[1] == UNICODE_DOT)
				rc = 2;
		}
	} else /* ASCII */ {
		if (de->namelen == 1) {
			if (de->name[0] == '.')
				rc = 1;
		} else if (de->namelen == 2) {
			if (de->name[0] == '.' && de->name[1] == '.')
				rc = 2;
		}
	}

	return rc;
}

/* Check if directory that we are searching has changed so we can decide
   whether we can use the cached search results from the previous search */
static int is_dir_changed(struct file *file)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	struct cifsInodeInfo *cifsInfo = CIFS_I(inode);

	if (cifsInfo->time == 0)
		return 1; /* directory was changed, perhaps due to unlink */
	else
		return 0;

}

static int cifs_save_resume_key(const char *current_entry,
	struct cifsFileInfo *file_info)
{
	struct cifs_dirent de;
	int rc;

	rc = cifs_fill_dirent(&de, current_entry, file_info->srch_inf.info_level,
			      file_info->srch_inf.unicode);
	if (!rc) {
		file_info->srch_inf.presume_name = de.name;
		file_info->srch_inf.resume_name_len = de.namelen;
		file_info->srch_inf.resume_key = de.resume_key;
	}
	return rc;
}

/* find the corresponding entry in the search */
/* Note that the SMB server returns search entries for . and .. which
   complicates logic here if we choose to parse for them and we do not
   assume that they are located in the findfirst return buffer.*/
/* We start counting in the buffer with entry 2 and increment for every
   entry (do not increment for . or .. entry) */
static int find_cifs_entry(const int xid, struct cifs_tcon *pTcon,
	struct file *file, char **ppCurrentEntry, int *num_to_ret)
{
	__u16 search_flags;
	int rc = 0;
	int pos_in_buf = 0;
	loff_t first_entry_in_buffer;
	loff_t index_to_find = file->f_pos;
	struct cifsFileInfo *cifsFile = file->private_data;
	struct cifs_sb_info *cifs_sb = CIFS_SB(file->f_path.dentry->d_sb);
	/* check if index in the buffer */

	if ((cifsFile == NULL) || (ppCurrentEntry == NULL) ||
	   (num_to_ret == NULL))
		return -ENOENT;

	*ppCurrentEntry = NULL;
	first_entry_in_buffer =
		cifsFile->srch_inf.index_of_last_entry -
			cifsFile->srch_inf.entries_in_buffer;

	/* if first entry in buf is zero then is first buffer
	in search response data which means it is likely . and ..
	will be in this buffer, although some servers do not return
	. and .. for the root of a drive and for those we need
	to start two entries earlier */

	dump_cifs_file_struct(file, "In fce ");
	if (((index_to_find < cifsFile->srch_inf.index_of_last_entry) &&
	     is_dir_changed(file)) ||
	   (index_to_find < first_entry_in_buffer)) {
		/* close and restart search */
		cFYI(1, "search backing up - close and restart search");
		spin_lock(&cifs_file_list_lock);
		if (!cifsFile->srch_inf.endOfSearch &&
		    !cifsFile->invalidHandle) {
			cifsFile->invalidHandle = true;
			spin_unlock(&cifs_file_list_lock);
			CIFSFindClose(xid, pTcon, cifsFile->netfid);
		} else
			spin_unlock(&cifs_file_list_lock);
		if (cifsFile->srch_inf.ntwrk_buf_start) {
			cFYI(1, "freeing SMB ff cache buf on search rewind");
			if (cifsFile->srch_inf.smallBuf)
				cifs_small_buf_release(cifsFile->srch_inf.
						ntwrk_buf_start);
			else
				cifs_buf_release(cifsFile->srch_inf.
						ntwrk_buf_start);
			cifsFile->srch_inf.ntwrk_buf_start = NULL;
		}
		rc = initiate_cifs_search(xid, file);
		if (rc) {
			cFYI(1, "error %d reinitiating a search on rewind",
				 rc);
			return rc;
		}
		/* FindFirst/Next set last_entry to NULL on malformed reply */
		if (cifsFile->srch_inf.last_entry)
			cifs_save_resume_key(cifsFile->srch_inf.last_entry,
						cifsFile);
	}

	search_flags = CIFS_SEARCH_CLOSE_AT_END | CIFS_SEARCH_RETURN_RESUME;
	if (backup_cred(cifs_sb))
		search_flags |= CIFS_SEARCH_BACKUP_SEARCH;

	while ((index_to_find >= cifsFile->srch_inf.index_of_last_entry) &&
	      (rc == 0) && !cifsFile->srch_inf.endOfSearch) {
		cFYI(1, "calling findnext2");
		rc = CIFSFindNext(xid, pTcon, cifsFile->netfid, search_flags,
				  &cifsFile->srch_inf);
		/* FindFirst/Next set last_entry to NULL on malformed reply */
		if (cifsFile->srch_inf.last_entry)
			cifs_save_resume_key(cifsFile->srch_inf.last_entry,
						cifsFile);
		if (rc)
			return -ENOENT;
	}
	if (index_to_find < cifsFile->srch_inf.index_of_last_entry) {
		/* we found the buffer that contains the entry */
		/* scan and find it */
		int i;
		char *current_entry;
		char *end_of_smb = cifsFile->srch_inf.ntwrk_buf_start +
			smbCalcSize((struct smb_hdr *)
				cifsFile->srch_inf.ntwrk_buf_start);

		current_entry = cifsFile->srch_inf.srch_entries_start;
		first_entry_in_buffer = cifsFile->srch_inf.index_of_last_entry
					- cifsFile->srch_inf.entries_in_buffer;
		pos_in_buf = index_to_find - first_entry_in_buffer;
		cFYI(1, "found entry - pos_in_buf %d", pos_in_buf);

		for (i = 0; (i < (pos_in_buf)) && (current_entry != NULL); i++) {
			/* go entry by entry figuring out which is first */
			current_entry = nxt_dir_entry(current_entry, end_of_smb,
						cifsFile->srch_inf.info_level);
		}
		if ((current_entry == NULL) && (i < pos_in_buf)) {
			/* BB fixme - check if we should flag this error */
			cERROR(1, "reached end of buf searching for pos in buf"
			  " %d index to find %lld rc %d",
			  pos_in_buf, index_to_find, rc);
		}
		rc = 0;
		*ppCurrentEntry = current_entry;
	} else {
		cFYI(1, "index not in buffer - could not findnext into it");
		return 0;
	}

	if (pos_in_buf >= cifsFile->srch_inf.entries_in_buffer) {
		cFYI(1, "can not return entries pos_in_buf beyond last");
		*num_to_ret = 0;
	} else
		*num_to_ret = cifsFile->srch_inf.entries_in_buffer - pos_in_buf;

	return rc;
}

static int cifs_filldir(char *find_entry, struct file *file, filldir_t filldir,
		void *dirent, char *scratch_buf, unsigned int max_len)
{
	struct cifsFileInfo *file_info = file->private_data;
	struct super_block *sb = file->f_path.dentry->d_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_dirent de = { NULL, };
	struct cifs_fattr fattr;
	struct dentry *dentry;
	struct qstr name;
	int rc = 0;
	ino_t ino;

	rc = cifs_fill_dirent(&de, find_entry, file_info->srch_inf.info_level,
			      file_info->srch_inf.unicode);
	if (rc)
		return rc;

	if (de.namelen > max_len) {
		cERROR(1, "bad search response length %zd past smb end",
			  de.namelen);
		return -EINVAL;
	}

	/* skip . and .. since we added them first */
	if (cifs_entry_is_dot(&de, file_info->srch_inf.unicode))
		return 0;

	if (file_info->srch_inf.unicode) {
		struct nls_table *nlt = cifs_sb->local_nls;

		name.name = scratch_buf;
		name.len =
			cifs_from_utf16((char *)name.name, (__le16 *)de.name,
					UNICODE_NAME_MAX,
					min_t(size_t, de.namelen,
					      (size_t)max_len), nlt,
					cifs_sb->mnt_cifs_flags &
						CIFS_MOUNT_MAP_SPECIAL_CHR);
		name.len -= nls_nullsize(nlt);
	} else {
		name.name = de.name;
		name.len = de.namelen;
	}

	switch (file_info->srch_inf.info_level) {
	case SMB_FIND_FILE_UNIX:
		cifs_unix_basic_to_fattr(&fattr,
					 &((FILE_UNIX_INFO *)find_entry)->basic,
					 cifs_sb);
		break;
	case SMB_FIND_FILE_INFO_STANDARD:
		cifs_std_info_to_fattr(&fattr,
				       (FIND_FILE_STANDARD_INFO *)find_entry,
				       cifs_sb);
		break;
	default:
		cifs_dir_info_to_fattr(&fattr,
				       (FILE_DIRECTORY_INFO *)find_entry,
				       cifs_sb);
		break;
	}

	if (de.ino && (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)) {
		fattr.cf_uniqueid = de.ino;
	} else {
		fattr.cf_uniqueid = iunique(sb, ROOT_I);
		cifs_autodisable_serverino(cifs_sb);
	}

	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MF_SYMLINKS) &&
	    CIFSCouldBeMFSymlink(&fattr))
		/*
		 * trying to get the type and mode can be slow,
		 * so just call those regular files for now, and mark
		 * for reval
		 */
		fattr.cf_flags |= CIFS_FATTR_NEED_REVAL;

	ino = cifs_uniqueid_to_ino_t(fattr.cf_uniqueid);
	dentry = cifs_readdir_lookup(file->f_dentry, &name, &fattr);

	rc = filldir(dirent, name.name, name.len, file->f_pos, ino,
		     fattr.cf_dtype);

	dput(dentry);
	return rc;
}


int cifs_readdir(struct file *file, void *direntry, filldir_t filldir)
{
	int rc = 0;
	int xid, i;
	struct cifs_tcon *pTcon;
	struct cifsFileInfo *cifsFile = NULL;
	char *current_entry;
	int num_to_fill = 0;
	char *tmp_buf = NULL;
	char *end_of_smb;
	unsigned int max_len;

	xid = GetXid();

	/*
	 * Ensure FindFirst doesn't fail before doing filldir() for '.' and
	 * '..'. Otherwise we won't be able to notify VFS in case of failure.
	 */
	if (file->private_data == NULL) {
		rc = initiate_cifs_search(xid, file);
		cFYI(1, "initiate cifs search rc %d", rc);
		if (rc)
			goto rddir2_exit;
	}

	switch ((int) file->f_pos) {
	case 0:
		if (filldir(direntry, ".", 1, file->f_pos,
		     file->f_path.dentry->d_inode->i_ino, DT_DIR) < 0) {
			cERROR(1, "Filldir for current dir failed");
			rc = -ENOMEM;
			break;
		}
		file->f_pos++;
	case 1:
		if (filldir(direntry, "..", 2, file->f_pos,
		     parent_ino(file->f_path.dentry), DT_DIR) < 0) {
			cERROR(1, "Filldir for parent dir failed");
			rc = -ENOMEM;
			break;
		}
		file->f_pos++;
	default:
		/* 1) If search is active,
			is in current search buffer?
			if it before then restart search
			if after then keep searching till find it */

		if (file->private_data == NULL) {
			rc = -EINVAL;
			FreeXid(xid);
			return rc;
		}
		cifsFile = file->private_data;
		if (cifsFile->srch_inf.endOfSearch) {
			if (cifsFile->srch_inf.emptyDir) {
				cFYI(1, "End of search, empty dir");
				rc = 0;
				break;
			}
		} /* else {
			cifsFile->invalidHandle = true;
			CIFSFindClose(xid, pTcon, cifsFile->netfid);
		} */

		pTcon = tlink_tcon(cifsFile->tlink);
		rc = find_cifs_entry(xid, pTcon, file,
				&current_entry, &num_to_fill);
		if (rc) {
			cFYI(1, "fce error %d", rc);
			goto rddir2_exit;
		} else if (current_entry != NULL) {
			cFYI(1, "entry %lld found", file->f_pos);
		} else {
			cFYI(1, "could not find entry");
			goto rddir2_exit;
		}
		cFYI(1, "loop through %d times filling dir for net buf %p",
			num_to_fill, cifsFile->srch_inf.ntwrk_buf_start);
		max_len = smbCalcSize((struct smb_hdr *)
				cifsFile->srch_inf.ntwrk_buf_start);
		end_of_smb = cifsFile->srch_inf.ntwrk_buf_start + max_len;

		tmp_buf = kmalloc(UNICODE_NAME_MAX, GFP_KERNEL);
		if (tmp_buf == NULL) {
			rc = -ENOMEM;
			break;
		}

		for (i = 0; (i < num_to_fill) && (rc == 0); i++) {
			if (current_entry == NULL) {
				/* evaluate whether this case is an error */
				cERROR(1, "past SMB end,  num to fill %d i %d",
					  num_to_fill, i);
				break;
			}
			/* if buggy server returns . and .. late do
			we want to check for that here? */
			rc = cifs_filldir(current_entry, file,
					filldir, direntry, tmp_buf, max_len);
			if (rc == -EOVERFLOW) {
				rc = 0;
				break;
			}

			file->f_pos++;
			if (file->f_pos ==
				cifsFile->srch_inf.index_of_last_entry) {
				cFYI(1, "last entry in buf at pos %lld %s",
					file->f_pos, tmp_buf);
				cifs_save_resume_key(current_entry, cifsFile);
				break;
			} else
				current_entry =
					nxt_dir_entry(current_entry, end_of_smb,
						cifsFile->srch_inf.info_level);
		}
		kfree(tmp_buf);
		break;
	} /* end switch */

rddir2_exit:
	FreeXid(xid);
	return rc;
}
