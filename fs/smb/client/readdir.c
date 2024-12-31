// SPDX-License-Identifier: LGPL-2.1
/*
 *
 *   Directory search handling
 *
 *   Copyright (C) International Business Machines  Corp., 2004, 2008
 *   Copyright (C) Red Hat, Inc., 2011
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
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
#include "smb2proto.h"
#include "fs_context.h"
#include "cached_dir.h"
#include "reparse.h"

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
			cifs_dbg(FYI, "empty cifs private file data\n");
			return;
		}
		if (cf->invalidHandle)
			cifs_dbg(FYI, "Invalid handle\n");
		if (cf->srch_inf.endOfSearch)
			cifs_dbg(FYI, "end of search\n");
		if (cf->srch_inf.emptyDir)
			cifs_dbg(FYI, "empty dir\n");
	}
}
#else
static inline void dump_cifs_file_struct(struct file *file, char *label)
{
}
#endif /* DEBUG2 */

/*
 * Attempt to preload the dcache with the results from the FIND_FIRST/NEXT
 *
 * Find the dentry that matches "name". If there isn't one, create one. If it's
 * a negative dentry or the uniqueid or filetype(mode) changed,
 * then drop it and recreate it.
 */
static void
cifs_prime_dcache(struct dentry *parent, struct qstr *name,
		    struct cifs_fattr *fattr)
{
	struct dentry *dentry, *alias;
	struct inode *inode;
	struct super_block *sb = parent->d_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	bool posix = cifs_sb_master_tcon(cifs_sb)->posix_extensions;
	bool reparse_need_reval = false;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);
	int rc;

	cifs_dbg(FYI, "%s: for %s\n", __func__, name->name);

	dentry = d_hash_and_lookup(parent, name);
	if (!dentry) {
		/*
		 * If we know that the inode will need to be revalidated
		 * immediately, then don't create a new dentry for it.
		 * We'll end up doing an on the wire call either way and
		 * this spares us an invalidation.
		 */
retry:
		if (posix) {
			switch (fattr->cf_mode & S_IFMT) {
			case S_IFLNK:
			case S_IFBLK:
			case S_IFCHR:
				reparse_need_reval = true;
				break;
			default:
				break;
			}
		} else if (fattr->cf_cifsattrs & ATTR_REPARSE) {
			reparse_need_reval = true;
		}

		if (reparse_need_reval ||
		    (fattr->cf_flags & CIFS_FATTR_NEED_REVAL))
			return;

		dentry = d_alloc_parallel(parent, name, &wq);
	}
	if (IS_ERR(dentry))
		return;
	if (!d_in_lookup(dentry)) {
		inode = d_inode(dentry);
		if (inode) {
			if (d_mountpoint(dentry)) {
				dput(dentry);
				return;
			}
			/*
			 * If we're generating inode numbers, then we don't
			 * want to clobber the existing one with the one that
			 * the readdir code created.
			 */
			if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM))
				fattr->cf_uniqueid = CIFS_I(inode)->uniqueid;

			/*
			 * Update inode in place if both i_ino and i_mode didn't
			 * change.
			 */
			if (CIFS_I(inode)->uniqueid == fattr->cf_uniqueid) {
				/*
				 * Query dir responses don't provide enough
				 * information about reparse points other than
				 * their reparse tags.  Save an invalidation by
				 * not clobbering some existing attributes when
				 * reparse tag and ctime haven't changed.
				 */
				rc = 0;
				if (fattr->cf_cifsattrs & ATTR_REPARSE) {
					if (likely(reparse_inode_match(inode, fattr))) {
						fattr->cf_mode = inode->i_mode;
						fattr->cf_rdev = inode->i_rdev;
						fattr->cf_uid = inode->i_uid;
						fattr->cf_gid = inode->i_gid;
						fattr->cf_eof = CIFS_I(inode)->netfs.remote_i_size;
						fattr->cf_symlink_target = NULL;
					} else {
						CIFS_I(inode)->time = 0;
						rc = -ESTALE;
					}
				}
				if (!rc && !cifs_fattr_to_inode(inode, fattr, true)) {
					dput(dentry);
					return;
				}
			}
		}
		d_invalidate(dentry);
		dput(dentry);
		goto retry;
	} else {
		inode = cifs_iget(sb, fattr);
		if (!inode)
			inode = ERR_PTR(-ENOMEM);
		alias = d_splice_alias(inode, dentry);
		d_lookup_done(dentry);
		if (alias && !IS_ERR(alias))
			dput(alias);
	}
	dput(dentry);
}

static void
cifs_fill_common_info(struct cifs_fattr *fattr, struct cifs_sb_info *cifs_sb)
{
	struct cifs_open_info_data data = {
		.reparse = { .tag = fattr->cf_cifstag, },
	};

	fattr->cf_uid = cifs_sb->ctx->linux_uid;
	fattr->cf_gid = cifs_sb->ctx->linux_gid;

	/*
	 * The IO_REPARSE_TAG_LX_ tags originally were used by WSL but they
	 * are preferred by the Linux client in some cases since, unlike
	 * the NFS reparse tag (or EAs), they don't require an extra query
	 * to determine which type of special file they represent.
	 * TODO: go through all documented  reparse tags to see if we can
	 * reasonably map some of them to directories vs. files vs. symlinks
	 */
	if ((fattr->cf_cifsattrs & ATTR_REPARSE) &&
	    cifs_reparse_point_to_fattr(cifs_sb, fattr, &data))
		goto out_reparse;

	if (fattr->cf_cifsattrs & ATTR_DIRECTORY) {
		fattr->cf_mode = S_IFDIR | cifs_sb->ctx->dir_mode;
		fattr->cf_dtype = DT_DIR;
	} else {
		fattr->cf_mode = S_IFREG | cifs_sb->ctx->file_mode;
		fattr->cf_dtype = DT_REG;
	}

out_reparse:
	/* non-unix readdir doesn't provide nlink */
	fattr->cf_flags |= CIFS_FATTR_UNKNOWN_NLINK;

	if (fattr->cf_cifsattrs & ATTR_READONLY)
		fattr->cf_mode &= ~S_IWUGO;

	/*
	 * We of course don't get ACL info in FIND_FIRST/NEXT results, so
	 * mark it for revalidation so that "ls -l" will look right. It might
	 * be super-slow, but if we don't do this then the ownership of files
	 * may look wrong since the inodes may not have timed out by the time
	 * "ls" does a stat() call on them.
	 */
	if ((cifs_sb->mnt_cifs_flags & CIFS_MOUNT_CIFS_ACL) ||
	    (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_MODE_FROM_SID))
		fattr->cf_flags |= CIFS_FATTR_NEED_REVAL;

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

/* Fill a cifs_fattr struct with info from SMB_FIND_FILE_POSIX_INFO. */
static void
cifs_posix_to_fattr(struct cifs_fattr *fattr, struct smb2_posix_info *info,
		    struct cifs_sb_info *cifs_sb)
{
	struct smb2_posix_info_parsed parsed;

	posix_info_parse(info, NULL, &parsed);

	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_uniqueid = le64_to_cpu(info->Inode);
	fattr->cf_bytes = le64_to_cpu(info->AllocationSize);
	fattr->cf_eof = le64_to_cpu(info->EndOfFile);

	fattr->cf_atime = cifs_NTtimeToUnix(info->LastAccessTime);
	fattr->cf_mtime = cifs_NTtimeToUnix(info->LastWriteTime);
	fattr->cf_ctime = cifs_NTtimeToUnix(info->CreationTime);

	fattr->cf_nlink = le32_to_cpu(info->HardLinks);
	fattr->cf_cifsattrs = le32_to_cpu(info->DosAttributes);

	if (fattr->cf_cifsattrs & ATTR_REPARSE)
		fattr->cf_cifstag = le32_to_cpu(info->ReparseTag);

	/* The Mode field in the response can now include the file type as well */
	fattr->cf_mode = wire_mode_to_posix(le32_to_cpu(info->Mode),
					    fattr->cf_cifsattrs & ATTR_DIRECTORY);
	fattr->cf_dtype = S_DT(le32_to_cpu(info->Mode));

	switch (fattr->cf_mode & S_IFMT) {
	case S_IFLNK:
	case S_IFBLK:
	case S_IFCHR:
		fattr->cf_flags |= CIFS_FATTR_NEED_REVAL;
		break;
	default:
		break;
	}

	cifs_dbg(FYI, "posix fattr: dev %d, reparse %d, mode %o\n",
		 le32_to_cpu(info->DeviceId),
		 le32_to_cpu(info->ReparseTag),
		 le32_to_cpu(info->Mode));

	sid_to_id(cifs_sb, &parsed.owner, fattr, SIDOWNER);
	sid_to_id(cifs_sb, &parsed.group, fattr, SIDGROUP);
}

static void __dir_info_to_fattr(struct cifs_fattr *fattr, const void *info)
{
	const FILE_DIRECTORY_INFO *fi = info;

	memset(fattr, 0, sizeof(*fattr));
	fattr->cf_cifsattrs = le32_to_cpu(fi->ExtFileAttributes);
	fattr->cf_eof = le64_to_cpu(fi->EndOfFile);
	fattr->cf_bytes = le64_to_cpu(fi->AllocationSize);
	fattr->cf_createtime = le64_to_cpu(fi->CreationTime);
	fattr->cf_atime = cifs_NTtimeToUnix(fi->LastAccessTime);
	fattr->cf_ctime = cifs_NTtimeToUnix(fi->ChangeTime);
	fattr->cf_mtime = cifs_NTtimeToUnix(fi->LastWriteTime);
}

void
cifs_dir_info_to_fattr(struct cifs_fattr *fattr, FILE_DIRECTORY_INFO *info,
		       struct cifs_sb_info *cifs_sb)
{
	__dir_info_to_fattr(fattr, info);
	cifs_fill_common_info(fattr, cifs_sb);
}

static void cifs_fulldir_info_to_fattr(struct cifs_fattr *fattr,
				       const void *info,
				       struct cifs_sb_info *cifs_sb)
{
	const FILE_FULL_DIRECTORY_INFO *di = info;

	__dir_info_to_fattr(fattr, info);

	/* See MS-FSCC 2.4.14, 2.4.19 */
	if (fattr->cf_cifsattrs & ATTR_REPARSE)
		fattr->cf_cifstag = le32_to_cpu(di->EaSize);
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

static int
_initiate_cifs_search(const unsigned int xid, struct file *file,
		     const char *full_path)
{
	__u16 search_flags;
	int rc = 0;
	struct cifsFileInfo *cifsFile;
	struct cifs_sb_info *cifs_sb = CIFS_FILE_SB(file);
	struct tcon_link *tlink = NULL;
	struct cifs_tcon *tcon;
	struct TCP_Server_Info *server;

	if (file->private_data == NULL) {
		tlink = cifs_sb_tlink(cifs_sb);
		if (IS_ERR(tlink))
			return PTR_ERR(tlink);

		cifsFile = kzalloc(sizeof(struct cifsFileInfo), GFP_KERNEL);
		if (cifsFile == NULL) {
			rc = -ENOMEM;
			goto error_exit;
		}
		spin_lock_init(&cifsFile->file_info_lock);
		file->private_data = cifsFile;
		cifsFile->tlink = cifs_get_tlink(tlink);
		tcon = tlink_tcon(tlink);
	} else {
		cifsFile = file->private_data;
		tcon = tlink_tcon(cifsFile->tlink);
	}

	server = tcon->ses->server;

	if (!server->ops->query_dir_first) {
		rc = -ENOSYS;
		goto error_exit;
	}

	cifsFile->invalidHandle = true;
	cifsFile->srch_inf.endOfSearch = false;

	cifs_dbg(FYI, "Full path: %s start at: %lld\n", full_path, file->f_pos);

ffirst_retry:
	/* test for Unix extensions */
	/* but now check for them on the share/mount not on the SMB session */
	/* if (cap_unix(tcon->ses) { */
	if (tcon->unix_ext)
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_UNIX;
	else if (tcon->posix_extensions)
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_POSIX_INFO;
	else if ((tcon->ses->capabilities &
		  tcon->ses->server->vals->cap_nt_find) == 0) {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_INFO_STANDARD;
	} else if (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM) {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_ID_FULL_DIR_INFO;
	} else /* not srvinos - BB fixme add check for backlevel? */ {
		cifsFile->srch_inf.info_level = SMB_FIND_FILE_FULL_DIRECTORY_INFO;
	}

	search_flags = CIFS_SEARCH_CLOSE_AT_END | CIFS_SEARCH_RETURN_RESUME;
	if (backup_cred(cifs_sb))
		search_flags |= CIFS_SEARCH_BACKUP_SEARCH;

	rc = server->ops->query_dir_first(xid, tcon, full_path, cifs_sb,
					  &cifsFile->fid, search_flags,
					  &cifsFile->srch_inf);

	if (rc == 0) {
		cifsFile->invalidHandle = false;
	} else if ((rc == -EOPNOTSUPP) &&
		   (cifs_sb->mnt_cifs_flags & CIFS_MOUNT_SERVER_INUM)) {
		cifs_sb->mnt_cifs_flags &= ~CIFS_MOUNT_SERVER_INUM;
		goto ffirst_retry;
	}
error_exit:
	cifs_put_tlink(tlink);
	return rc;
}

static int
initiate_cifs_search(const unsigned int xid, struct file *file,
		     const char *full_path)
{
	int rc, retry_count = 0;

	do {
		rc = _initiate_cifs_search(xid, file, full_path);
		/*
		 * If we don't have enough credits to start reading the
		 * directory just try again after short wait.
		 */
		if (rc != -EDEADLK)
			break;

		usleep_range(512, 2048);
	} while (retry_count++ < 5);

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
	cifs_dbg(FYI, "Unicode string longer than PATH_MAX found\n");
	return len << 1;
}

static char *nxt_dir_entry(char *old_entry, char *end_of_smb, int level)
{
	char *new_entry;
	FILE_DIRECTORY_INFO *pDirInfo = (FILE_DIRECTORY_INFO *)old_entry;

	if (level == SMB_FIND_FILE_INFO_STANDARD) {
		FIND_FILE_STANDARD_INFO *pfData;
		pfData = (FIND_FILE_STANDARD_INFO *)pDirInfo;

		new_entry = old_entry + sizeof(FIND_FILE_STANDARD_INFO) + 1 +
				pfData->FileNameLength;
	} else {
		u32 next_offset = le32_to_cpu(pDirInfo->NextEntryOffset);

		if (old_entry + next_offset < old_entry) {
			cifs_dbg(VFS, "Invalid offset %u\n", next_offset);
			return NULL;
		}
		new_entry = old_entry + next_offset;
	}
	cifs_dbg(FYI, "new entry %p old entry %p\n", new_entry, old_entry);
	/* validate that new_entry is not past end of SMB */
	if (new_entry >= end_of_smb) {
		cifs_dbg(VFS, "search entry %p began after end of SMB %p old entry %p\n",
			 new_entry, end_of_smb, old_entry);
		return NULL;
	} else if (((level == SMB_FIND_FILE_INFO_STANDARD) &&
		    (new_entry + sizeof(FIND_FILE_STANDARD_INFO) + 1 > end_of_smb))
		  || ((level != SMB_FIND_FILE_INFO_STANDARD) &&
		   (new_entry + sizeof(FILE_DIRECTORY_INFO) + 1 > end_of_smb)))  {
		cifs_dbg(VFS, "search entry %p extends after end of SMB %p\n",
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

static void cifs_fill_dirent_posix(struct cifs_dirent *de,
				   const struct smb2_posix_info *info)
{
	struct smb2_posix_info_parsed parsed;

	/* payload should have already been checked at this point */
	if (posix_info_parse(info, NULL, &parsed) < 0) {
		cifs_dbg(VFS, "Invalid POSIX info payload\n");
		return;
	}

	de->name = parsed.name;
	de->namelen = parsed.name_len;
	de->resume_key = info->Ignored;
	de->ino = le64_to_cpu(info->Inode);
}

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
	/* one byte length, no endianness conversion */
	de->namelen = info->FileNameLength;
	de->resume_key = info->ResumeKey;
}

static int cifs_fill_dirent(struct cifs_dirent *de, const void *info,
		u16 level, bool is_unicode)
{
	memset(de, 0, sizeof(*de));

	switch (level) {
	case SMB_FIND_FILE_POSIX_INFO:
		cifs_fill_dirent_posix(de, info);
		break;
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
		cifs_dbg(FYI, "Unknown findfirst level %d\n", level);
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
	struct inode *inode = file_inode(file);
	struct cifsInodeInfo *cifs_inode_info = CIFS_I(inode);

	if (cifs_inode_info->time == 0)
		return 1; /* directory was changed, e.g. unlink or new file */
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

/*
 * Find the corresponding entry in the search. Note that the SMB server returns
 * search entries for . and .. which complicates logic here if we choose to
 * parse for them and we do not assume that they are located in the findfirst
 * return buffer. We start counting in the buffer with entry 2 and increment for
 * every entry (do not increment for . or .. entry).
 */
static int
find_cifs_entry(const unsigned int xid, struct cifs_tcon *tcon, loff_t pos,
		struct file *file, const char *full_path,
		char **current_entry, int *num_to_ret)
{
	__u16 search_flags;
	int rc = 0;
	int pos_in_buf = 0;
	loff_t first_entry_in_buffer;
	loff_t index_to_find = pos;
	struct cifsFileInfo *cfile = file->private_data;
	struct cifs_sb_info *cifs_sb = CIFS_FILE_SB(file);
	struct TCP_Server_Info *server = tcon->ses->server;
	/* check if index in the buffer */

	if (!server->ops->query_dir_first || !server->ops->query_dir_next)
		return -ENOSYS;

	if ((cfile == NULL) || (current_entry == NULL) || (num_to_ret == NULL))
		return -ENOENT;

	*current_entry = NULL;
	first_entry_in_buffer = cfile->srch_inf.index_of_last_entry -
					cfile->srch_inf.entries_in_buffer;

	/*
	 * If first entry in buf is zero then is first buffer
	 * in search response data which means it is likely . and ..
	 * will be in this buffer, although some servers do not return
	 * . and .. for the root of a drive and for those we need
	 * to start two entries earlier.
	 */

	dump_cifs_file_struct(file, "In fce ");
	if (((index_to_find < cfile->srch_inf.index_of_last_entry) &&
	     is_dir_changed(file)) || (index_to_find < first_entry_in_buffer)) {
		/* close and restart search */
		cifs_dbg(FYI, "search backing up - close and restart search\n");
		spin_lock(&cfile->file_info_lock);
		if (server->ops->dir_needs_close(cfile)) {
			cfile->invalidHandle = true;
			spin_unlock(&cfile->file_info_lock);
			if (server->ops->close_dir)
				server->ops->close_dir(xid, tcon, &cfile->fid);
		} else
			spin_unlock(&cfile->file_info_lock);
		if (cfile->srch_inf.ntwrk_buf_start) {
			cifs_dbg(FYI, "freeing SMB ff cache buf on search rewind\n");
			if (cfile->srch_inf.smallBuf)
				cifs_small_buf_release(cfile->srch_inf.
						ntwrk_buf_start);
			else
				cifs_buf_release(cfile->srch_inf.
						ntwrk_buf_start);
			cfile->srch_inf.ntwrk_buf_start = NULL;
		}
		rc = initiate_cifs_search(xid, file, full_path);
		if (rc) {
			cifs_dbg(FYI, "error %d reinitiating a search on rewind\n",
				 rc);
			return rc;
		}
		/* FindFirst/Next set last_entry to NULL on malformed reply */
		if (cfile->srch_inf.last_entry)
			cifs_save_resume_key(cfile->srch_inf.last_entry, cfile);
	}

	search_flags = CIFS_SEARCH_CLOSE_AT_END | CIFS_SEARCH_RETURN_RESUME;
	if (backup_cred(cifs_sb))
		search_flags |= CIFS_SEARCH_BACKUP_SEARCH;

	while ((index_to_find >= cfile->srch_inf.index_of_last_entry) &&
	       (rc == 0) && !cfile->srch_inf.endOfSearch) {
		cifs_dbg(FYI, "calling findnext2\n");
		rc = server->ops->query_dir_next(xid, tcon, &cfile->fid,
						 search_flags,
						 &cfile->srch_inf);
		/* FindFirst/Next set last_entry to NULL on malformed reply */
		if (cfile->srch_inf.last_entry)
			cifs_save_resume_key(cfile->srch_inf.last_entry, cfile);
		if (rc)
			return -ENOENT;
	}
	if (index_to_find < cfile->srch_inf.index_of_last_entry) {
		/* we found the buffer that contains the entry */
		/* scan and find it */
		int i;
		char *cur_ent;
		char *end_of_smb;

		if (cfile->srch_inf.ntwrk_buf_start == NULL) {
			cifs_dbg(VFS, "ntwrk_buf_start is NULL during readdir\n");
			return -EIO;
		}

		end_of_smb = cfile->srch_inf.ntwrk_buf_start +
			server->ops->calc_smb_size(
					cfile->srch_inf.ntwrk_buf_start);

		cur_ent = cfile->srch_inf.srch_entries_start;
		first_entry_in_buffer = cfile->srch_inf.index_of_last_entry
					- cfile->srch_inf.entries_in_buffer;
		pos_in_buf = index_to_find - first_entry_in_buffer;
		cifs_dbg(FYI, "found entry - pos_in_buf %d\n", pos_in_buf);

		for (i = 0; (i < (pos_in_buf)) && (cur_ent != NULL); i++) {
			/* go entry by entry figuring out which is first */
			cur_ent = nxt_dir_entry(cur_ent, end_of_smb,
						cfile->srch_inf.info_level);
		}
		if ((cur_ent == NULL) && (i < pos_in_buf)) {
			/* BB fixme - check if we should flag this error */
			cifs_dbg(VFS, "reached end of buf searching for pos in buf %d index to find %lld rc %d\n",
				 pos_in_buf, index_to_find, rc);
		}
		rc = 0;
		*current_entry = cur_ent;
	} else {
		cifs_dbg(FYI, "index not in buffer - could not findnext into it\n");
		return 0;
	}

	if (pos_in_buf >= cfile->srch_inf.entries_in_buffer) {
		cifs_dbg(FYI, "can not return entries pos_in_buf beyond last\n");
		*num_to_ret = 0;
	} else
		*num_to_ret = cfile->srch_inf.entries_in_buffer - pos_in_buf;

	return rc;
}

static bool emit_cached_dirents(struct cached_dirents *cde,
				struct dir_context *ctx)
{
	struct cached_dirent *dirent;
	bool rc;

	list_for_each_entry(dirent, &cde->entries, entry) {
		/*
		 * Skip all early entries prior to the current lseek()
		 * position.
		 */
		if (ctx->pos > dirent->pos)
			continue;
		/*
		 * We recorded the current ->pos value for the dirent
		 * when we stored it in the cache.
		 * However, this sequence of ->pos values may have holes
		 * in it, for example dot-dirs returned from the server
		 * are suppressed.
		 * Handle this by forcing ctx->pos to be the same as the
		 * ->pos of the current dirent we emit from the cache.
		 * This means that when we emit these entries from the cache
		 * we now emit them with the same ->pos value as in the
		 * initial scan.
		 */
		ctx->pos = dirent->pos;
		rc = dir_emit(ctx, dirent->name, dirent->namelen,
			      dirent->fattr.cf_uniqueid,
			      dirent->fattr.cf_dtype);
		if (!rc)
			return rc;
		ctx->pos++;
	}
	return true;
}

static void update_cached_dirents_count(struct cached_dirents *cde,
					struct dir_context *ctx)
{
	if (cde->ctx != ctx)
		return;
	if (cde->is_valid || cde->is_failed)
		return;

	cde->pos++;
}

static void finished_cached_dirents_count(struct cached_dirents *cde,
					struct dir_context *ctx)
{
	if (cde->ctx != ctx)
		return;
	if (cde->is_valid || cde->is_failed)
		return;
	if (ctx->pos != cde->pos)
		return;

	cde->is_valid = 1;
}

static void add_cached_dirent(struct cached_dirents *cde,
			      struct dir_context *ctx,
			      const char *name, int namelen,
			      struct cifs_fattr *fattr)
{
	struct cached_dirent *de;

	if (cde->ctx != ctx)
		return;
	if (cde->is_valid || cde->is_failed)
		return;
	if (ctx->pos != cde->pos) {
		cde->is_failed = 1;
		return;
	}
	de = kzalloc(sizeof(*de), GFP_ATOMIC);
	if (de == NULL) {
		cde->is_failed = 1;
		return;
	}
	de->namelen = namelen;
	de->name = kstrndup(name, namelen, GFP_ATOMIC);
	if (de->name == NULL) {
		kfree(de);
		cde->is_failed = 1;
		return;
	}
	de->pos = ctx->pos;

	memcpy(&de->fattr, fattr, sizeof(struct cifs_fattr));

	list_add_tail(&de->entry, &cde->entries);
}

static bool cifs_dir_emit(struct dir_context *ctx,
			  const char *name, int namelen,
			  struct cifs_fattr *fattr,
			  struct cached_fid *cfid)
{
	bool rc;
	ino_t ino = cifs_uniqueid_to_ino_t(fattr->cf_uniqueid);

	rc = dir_emit(ctx, name, namelen, ino, fattr->cf_dtype);
	if (!rc)
		return rc;

	if (cfid) {
		mutex_lock(&cfid->dirents.de_mutex);
		add_cached_dirent(&cfid->dirents, ctx, name, namelen,
				  fattr);
		mutex_unlock(&cfid->dirents.de_mutex);
	}

	return rc;
}

static int cifs_filldir(char *find_entry, struct file *file,
			struct dir_context *ctx,
			char *scratch_buf, unsigned int max_len,
			struct cached_fid *cfid)
{
	struct cifsFileInfo *file_info = file->private_data;
	struct super_block *sb = file_inode(file)->i_sb;
	struct cifs_sb_info *cifs_sb = CIFS_SB(sb);
	struct cifs_dirent de = { NULL, };
	struct cifs_fattr fattr;
	struct qstr name;
	int rc = 0;

	rc = cifs_fill_dirent(&de, find_entry, file_info->srch_inf.info_level,
			      file_info->srch_inf.unicode);
	if (rc)
		return rc;

	if (de.namelen > max_len) {
		cifs_dbg(VFS, "bad search response length %zd past smb end\n",
			 de.namelen);
		return -EINVAL;
	}

	/* skip . and .. since we added them first */
	if (cifs_entry_is_dot(&de, file_info->srch_inf.unicode))
		return 0;

	if (file_info->srch_inf.unicode) {
		struct nls_table *nlt = cifs_sb->local_nls;
		int map_type;

		map_type = cifs_remap(cifs_sb);
		name.name = scratch_buf;
		name.len =
			cifs_from_utf16((char *)name.name, (__le16 *)de.name,
					UNICODE_NAME_MAX,
					min_t(size_t, de.namelen,
					      (size_t)max_len), nlt, map_type);
		name.len -= nls_nullsize(nlt);
	} else {
		name.name = de.name;
		name.len = de.namelen;
	}

	switch (file_info->srch_inf.info_level) {
	case SMB_FIND_FILE_POSIX_INFO:
		cifs_posix_to_fattr(&fattr,
				    (struct smb2_posix_info *)find_entry,
				    cifs_sb);
		break;
	case SMB_FIND_FILE_UNIX:
		cifs_unix_basic_to_fattr(&fattr,
					 &((FILE_UNIX_INFO *)find_entry)->basic,
					 cifs_sb);
		if (S_ISLNK(fattr.cf_mode))
			fattr.cf_flags |= CIFS_FATTR_NEED_REVAL;
		break;
	case SMB_FIND_FILE_INFO_STANDARD:
		cifs_std_info_to_fattr(&fattr,
				       (FIND_FILE_STANDARD_INFO *)find_entry,
				       cifs_sb);
		break;
	case SMB_FIND_FILE_FULL_DIRECTORY_INFO:
	case SMB_FIND_FILE_ID_FULL_DIR_INFO:
		cifs_fulldir_info_to_fattr(&fattr, find_entry, cifs_sb);
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
	    couldbe_mf_symlink(&fattr))
		/*
		 * trying to get the type and mode can be slow,
		 * so just call those regular files for now, and mark
		 * for reval
		 */
		fattr.cf_flags |= CIFS_FATTR_NEED_REVAL;

	cifs_prime_dcache(file_dentry(file), &name, &fattr);

	return !cifs_dir_emit(ctx, name.name, name.len,
			      &fattr, cfid);
}


int cifs_readdir(struct file *file, struct dir_context *ctx)
{
	int rc = 0;
	unsigned int xid;
	int i;
	struct tcon_link *tlink = NULL;
	struct cifs_tcon *tcon;
	struct cifsFileInfo *cifsFile;
	char *current_entry;
	int num_to_fill = 0;
	char *tmp_buf = NULL;
	char *end_of_smb;
	unsigned int max_len;
	const char *full_path;
	void *page = alloc_dentry_path();
	struct cached_fid *cfid = NULL;
	struct cifs_sb_info *cifs_sb = CIFS_FILE_SB(file);

	xid = get_xid();

	full_path = build_path_from_dentry(file_dentry(file), page);
	if (IS_ERR(full_path)) {
		rc = PTR_ERR(full_path);
		goto rddir2_exit;
	}

	if (file->private_data == NULL) {
		tlink = cifs_sb_tlink(cifs_sb);
		if (IS_ERR(tlink))
			goto cache_not_found;
		tcon = tlink_tcon(tlink);
	} else {
		cifsFile = file->private_data;
		tcon = tlink_tcon(cifsFile->tlink);
	}

	rc = open_cached_dir(xid, tcon, full_path, cifs_sb, false, &cfid);
	cifs_put_tlink(tlink);
	if (rc)
		goto cache_not_found;

	mutex_lock(&cfid->dirents.de_mutex);
	/*
	 * If this was reading from the start of the directory
	 * we need to initialize scanning and storing the
	 * directory content.
	 */
	if (ctx->pos == 0 && cfid->dirents.ctx == NULL) {
		cfid->dirents.ctx = ctx;
		cfid->dirents.pos = 2;
	}
	/*
	 * If we already have the entire directory cached then
	 * we can just serve the cache.
	 */
	if (cfid->dirents.is_valid) {
		if (!dir_emit_dots(file, ctx)) {
			mutex_unlock(&cfid->dirents.de_mutex);
			goto rddir2_exit;
		}
		emit_cached_dirents(&cfid->dirents, ctx);
		mutex_unlock(&cfid->dirents.de_mutex);
		goto rddir2_exit;
	}
	mutex_unlock(&cfid->dirents.de_mutex);

	/* Drop the cache while calling initiate_cifs_search and
	 * find_cifs_entry in case there will be reconnects during
	 * query_directory.
	 */
	close_cached_dir(cfid);
	cfid = NULL;

 cache_not_found:
	/*
	 * Ensure FindFirst doesn't fail before doing filldir() for '.' and
	 * '..'. Otherwise we won't be able to notify VFS in case of failure.
	 */
	if (file->private_data == NULL) {
		rc = initiate_cifs_search(xid, file, full_path);
		cifs_dbg(FYI, "initiate cifs search rc %d\n", rc);
		if (rc)
			goto rddir2_exit;
	}

	if (!dir_emit_dots(file, ctx))
		goto rddir2_exit;

	/* 1) If search is active,
		is in current search buffer?
		if it before then restart search
		if after then keep searching till find it */
	cifsFile = file->private_data;
	if (cifsFile->srch_inf.endOfSearch) {
		if (cifsFile->srch_inf.emptyDir) {
			cifs_dbg(FYI, "End of search, empty dir\n");
			rc = 0;
			goto rddir2_exit;
		}
	} /* else {
		cifsFile->invalidHandle = true;
		tcon->ses->server->close(xid, tcon, &cifsFile->fid);
	} */

	tcon = tlink_tcon(cifsFile->tlink);
	rc = find_cifs_entry(xid, tcon, ctx->pos, file, full_path,
			     &current_entry, &num_to_fill);
	open_cached_dir(xid, tcon, full_path, cifs_sb, false, &cfid);
	if (rc) {
		cifs_dbg(FYI, "fce error %d\n", rc);
		goto rddir2_exit;
	} else if (current_entry != NULL) {
		cifs_dbg(FYI, "entry %lld found\n", ctx->pos);
	} else {
		if (cfid) {
			mutex_lock(&cfid->dirents.de_mutex);
			finished_cached_dirents_count(&cfid->dirents, ctx);
			mutex_unlock(&cfid->dirents.de_mutex);
		}
		cifs_dbg(FYI, "Could not find entry\n");
		goto rddir2_exit;
	}
	cifs_dbg(FYI, "loop through %d times filling dir for net buf %p\n",
		 num_to_fill, cifsFile->srch_inf.ntwrk_buf_start);
	max_len = tcon->ses->server->ops->calc_smb_size(
			cifsFile->srch_inf.ntwrk_buf_start);
	end_of_smb = cifsFile->srch_inf.ntwrk_buf_start + max_len;

	tmp_buf = kmalloc(UNICODE_NAME_MAX, GFP_KERNEL);
	if (tmp_buf == NULL) {
		rc = -ENOMEM;
		goto rddir2_exit;
	}

	for (i = 0; i < num_to_fill; i++) {
		if (current_entry == NULL) {
			/* evaluate whether this case is an error */
			cifs_dbg(VFS, "past SMB end,  num to fill %d i %d\n",
				 num_to_fill, i);
			break;
		}
		/*
		 * if buggy server returns . and .. late do we want to
		 * check for that here?
		 */
		*tmp_buf = 0;
		rc = cifs_filldir(current_entry, file, ctx,
				  tmp_buf, max_len, cfid);
		if (rc) {
			if (rc > 0)
				rc = 0;
			break;
		}

		ctx->pos++;
		if (cfid) {
			mutex_lock(&cfid->dirents.de_mutex);
			update_cached_dirents_count(&cfid->dirents, ctx);
			mutex_unlock(&cfid->dirents.de_mutex);
		}

		if (ctx->pos ==
			cifsFile->srch_inf.index_of_last_entry) {
			cifs_dbg(FYI, "last entry in buf at pos %lld %s\n",
				 ctx->pos, tmp_buf);
			cifs_save_resume_key(current_entry, cifsFile);
			break;
		}
		current_entry =
			nxt_dir_entry(current_entry, end_of_smb,
				      cifsFile->srch_inf.info_level);
	}
	kfree(tmp_buf);

rddir2_exit:
	if (cfid)
		close_cached_dir(cfid);
	free_dentry_path(page);
	free_xid(xid);
	return rc;
}
