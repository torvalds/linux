// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Paulo Alcantara <pc@manguebit.com>
 */

#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include "cifsglob.h"
#include "smb2proto.h"
#include "cifsproto.h"
#include "cifs_unicode.h"
#include "cifs_debug.h"
#include "fs_context.h"
#include "reparse.h"

static int mknod_nfs(unsigned int xid, struct inode *inode,
		     struct dentry *dentry, struct cifs_tcon *tcon,
		     const char *full_path, umode_t mode, dev_t dev,
		     const char *symname);

static int mknod_wsl(unsigned int xid, struct inode *inode,
		     struct dentry *dentry, struct cifs_tcon *tcon,
		     const char *full_path, umode_t mode, dev_t dev,
		     const char *symname);

static int create_native_symlink(const unsigned int xid, struct inode *inode,
				 struct dentry *dentry, struct cifs_tcon *tcon,
				 const char *full_path, const char *symname);

static int detect_directory_symlink_target(struct cifs_sb_info *cifs_sb,
					   const unsigned int xid,
					   const char *full_path,
					   const char *symname,
					   bool *directory);

int smb2_create_reparse_symlink(const unsigned int xid, struct inode *inode,
				struct dentry *dentry, struct cifs_tcon *tcon,
				const char *full_path, const char *symname)
{
	switch (get_cifs_symlink_type(CIFS_SB(inode->i_sb))) {
	case CIFS_SYMLINK_TYPE_NATIVE:
		return create_native_symlink(xid, inode, dentry, tcon, full_path, symname);
	case CIFS_SYMLINK_TYPE_NFS:
		return mknod_nfs(xid, inode, dentry, tcon, full_path, S_IFLNK, 0, symname);
	case CIFS_SYMLINK_TYPE_WSL:
		return mknod_wsl(xid, inode, dentry, tcon, full_path, S_IFLNK, 0, symname);
	default:
		return -EOPNOTSUPP;
	}
}

static int create_native_symlink(const unsigned int xid, struct inode *inode,
				 struct dentry *dentry, struct cifs_tcon *tcon,
				 const char *full_path, const char *symname)
{
	struct reparse_symlink_data_buffer *buf = NULL;
	struct cifs_open_info_data data = {};
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct inode *new;
	struct kvec iov;
	__le16 *path = NULL;
	bool directory;
	char *symlink_target = NULL;
	char *sym = NULL;
	char sep = CIFS_DIR_SEP(cifs_sb);
	u16 len, plen, poff, slen;
	int rc = 0;

	if (strlen(symname) > REPARSE_SYM_PATH_MAX)
		return -ENAMETOOLONG;

	symlink_target = kstrdup(symname, GFP_KERNEL);
	if (!symlink_target) {
		rc = -ENOMEM;
		goto out;
	}

	data = (struct cifs_open_info_data) {
		.reparse_point = true,
		.reparse = { .tag = IO_REPARSE_TAG_SYMLINK, },
		.symlink_target = symlink_target,
	};

	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) && symname[0] == '/') {
		/*
		 * This is a request to create an absolute symlink on the server
		 * which does not support POSIX paths, and expects symlink in
		 * NT-style path. So convert absolute Linux symlink target path
		 * to the absolute NT-style path. Root of the NT-style path for
		 * symlinks is specified in "symlinkroot" mount option. This will
		 * ensure compatibility of this symlink stored in absolute form
		 * on the SMB server.
		 */
		if (!strstarts(symname, cifs_sb->ctx->symlinkroot)) {
			/*
			 * If the absolute Linux symlink target path is not
			 * inside "symlinkroot" location then there is no way
			 * to convert such Linux symlink to NT-style path.
			 */
			cifs_dbg(VFS,
				 "absolute symlink '%s' cannot be converted to NT format "
				 "because it is outside of symlinkroot='%s'\n",
				 symname, cifs_sb->ctx->symlinkroot);
			rc = -EINVAL;
			goto out;
		}
		len = strlen(cifs_sb->ctx->symlinkroot);
		if (cifs_sb->ctx->symlinkroot[len-1] != '/')
			len++;
		if (symname[len] >= 'a' && symname[len] <= 'z' &&
		    (symname[len+1] == '/' || symname[len+1] == '\0')) {
			/*
			 * Symlink points to Linux target /symlinkroot/x/path/...
			 * where 'x' is the lowercase local Windows drive.
			 * NT-style path for 'x' has common form \??\X:\path\...
			 * with uppercase local Windows drive.
			 */
			int common_path_len = strlen(symname+len+1)+1;
			sym = kzalloc(6+common_path_len, GFP_KERNEL);
			if (!sym) {
				rc = -ENOMEM;
				goto out;
			}
			memcpy(sym, "\\??\\", 4);
			sym[4] = symname[len] - ('a'-'A');
			sym[5] = ':';
			memcpy(sym+6, symname+len+1, common_path_len);
		} else {
			/* Unhandled absolute symlink. Report an error. */
			cifs_dbg(
				 VFS,
				 "absolute symlink '%s' cannot be converted to NT format "
				 "because it points to unknown target\n",
				 symname);
			rc = -EINVAL;
			goto out;
		}
	} else {
		/*
		 * This is request to either create an absolute symlink on
		 * server which expects POSIX paths or it is an request to
		 * create a relative symlink from the current directory.
		 * These paths have same format as relative SMB symlinks,
		 * so no conversion is needed. So just take symname as-is.
		 */
		sym = kstrdup(symname, GFP_KERNEL);
		if (!sym) {
			rc = -ENOMEM;
			goto out;
		}
	}

	if (sep == '\\')
		convert_delimiter(sym, sep);

	/*
	 * For absolute NT symlinks it is required to pass also leading
	 * backslash and to not mangle NT object prefix "\\??\\" and not to
	 * mangle colon in drive letter. But cifs_convert_path_to_utf16()
	 * removes leading backslash and replaces '?' and ':'. So temporary
	 * mask these characters in NT object prefix by '_' and then change
	 * them back.
	 */
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) && symname[0] == '/')
		sym[0] = sym[1] = sym[2] = sym[5] = '_';

	path = cifs_convert_path_to_utf16(sym, cifs_sb);
	if (!path) {
		rc = -ENOMEM;
		goto out;
	}

	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) && symname[0] == '/') {
		sym[0] = '\\';
		sym[1] = sym[2] = '?';
		sym[5] = ':';
		path[0] = cpu_to_le16('\\');
		path[1] = path[2] = cpu_to_le16('?');
		path[5] = cpu_to_le16(':');
	}

	/*
	 * SMB distinguish between symlink to directory and symlink to file.
	 * They cannot be exchanged (symlink of file type which points to
	 * directory cannot be resolved and vice-versa). Try to detect if
	 * the symlink target could be a directory or not. When detection
	 * fails then treat symlink as a file (non-directory) symlink.
	 */
	directory = false;
	rc = detect_directory_symlink_target(cifs_sb, xid, full_path, symname, &directory);
	if (rc < 0)
		goto out;

	slen = 2 * UniStrnlen((wchar_t *)path, REPARSE_SYM_PATH_MAX);
	poff = 0;
	plen = slen;
	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) && symname[0] == '/') {
		/*
		 * For absolute NT symlinks skip leading "\\??\\" in PrintName as
		 * PrintName is user visible location in DOS/Win32 format (not in NT format).
		 */
		poff = 4;
		plen -= 2 * poff;
	}
	len = sizeof(*buf) + plen + slen;
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto out;
	}

	buf->ReparseTag = cpu_to_le32(IO_REPARSE_TAG_SYMLINK);
	buf->ReparseDataLength = cpu_to_le16(len - sizeof(struct reparse_data_buffer));

	buf->SubstituteNameOffset = cpu_to_le16(plen);
	buf->SubstituteNameLength = cpu_to_le16(slen);
	memcpy(&buf->PathBuffer[plen], path, slen);

	buf->PrintNameOffset = 0;
	buf->PrintNameLength = cpu_to_le16(plen);
	memcpy(buf->PathBuffer, path+poff, plen);

	buf->Flags = cpu_to_le32(*symname != '/' ? SYMLINK_FLAG_RELATIVE : 0);

	iov.iov_base = buf;
	iov.iov_len = len;
	new = smb2_get_reparse_inode(&data, inode->i_sb, xid,
				     tcon, full_path, directory,
				     &iov, NULL);
	if (!IS_ERR(new))
		d_instantiate(dentry, new);
	else
		rc = PTR_ERR(new);
out:
	kfree(sym);
	kfree(path);
	cifs_free_open_info(&data);
	kfree(buf);
	return rc;
}

static int detect_directory_symlink_target(struct cifs_sb_info *cifs_sb,
					   const unsigned int xid,
					   const char *full_path,
					   const char *symname,
					   bool *directory)
{
	char sep = CIFS_DIR_SEP(cifs_sb);
	struct cifs_open_parms oparms;
	struct tcon_link *tlink;
	struct cifs_tcon *tcon;
	const char *basename;
	struct cifs_fid fid;
	char *resolved_path;
	int full_path_len;
	int basename_len;
	int symname_len;
	char *path_sep;
	__u32 oplock;
	int open_rc;

	/*
	 * First do some simple check. If the original Linux symlink target ends
	 * with slash, or last path component is dot or dot-dot then it is for
	 * sure symlink to the directory.
	 */
	basename = kbasename(symname);
	basename_len = strlen(basename);
	if (basename_len == 0 || /* symname ends with slash */
	    (basename_len == 1 && basename[0] == '.') || /* last component is "." */
	    (basename_len == 2 && basename[0] == '.' && basename[1] == '.')) { /* or ".." */
		*directory = true;
		return 0;
	}

	/*
	 * For absolute symlinks it is not possible to determinate
	 * if it should point to directory or file.
	 */
	if (symname[0] == '/') {
		cifs_dbg(FYI,
			 "%s: cannot determinate if the symlink target path '%s' "
			 "is directory or not, creating '%s' as file symlink\n",
			 __func__, symname, full_path);
		return 0;
	}

	/*
	 * If it was not detected as directory yet and the symlink is relative
	 * then try to resolve the path on the SMB server, check if the path
	 * exists and determinate if it is a directory or not.
	 */

	full_path_len = strlen(full_path);
	symname_len = strlen(symname);

	tlink = cifs_sb_tlink(cifs_sb);
	if (IS_ERR(tlink))
		return PTR_ERR(tlink);

	resolved_path = kzalloc(full_path_len + symname_len + 1, GFP_KERNEL);
	if (!resolved_path) {
		cifs_put_tlink(tlink);
		return -ENOMEM;
	}

	/*
	 * Compose the resolved SMB symlink path from the SMB full path
	 * and Linux target symlink path.
	 */
	memcpy(resolved_path, full_path, full_path_len+1);
	path_sep = strrchr(resolved_path, sep);
	if (path_sep)
		path_sep++;
	else
		path_sep = resolved_path;
	memcpy(path_sep, symname, symname_len+1);
	if (sep == '\\')
		convert_delimiter(path_sep, sep);

	tcon = tlink_tcon(tlink);
	oparms = CIFS_OPARMS(cifs_sb, tcon, resolved_path,
			     FILE_READ_ATTRIBUTES, FILE_OPEN, 0, ACL_NO_MODE);
	oparms.fid = &fid;

	/* Try to open as a directory (NOT_FILE) */
	oplock = 0;
	oparms.create_options = cifs_create_options(cifs_sb,
						    CREATE_NOT_FILE | OPEN_REPARSE_POINT);
	open_rc = tcon->ses->server->ops->open(xid, &oparms, &oplock, NULL);
	if (open_rc == 0) {
		/* Successful open means that the target path is definitely a directory. */
		*directory = true;
		tcon->ses->server->ops->close(xid, tcon, &fid);
	} else if (open_rc == -ENOTDIR) {
		/* -ENOTDIR means that the target path is definitely a file. */
		*directory = false;
	} else if (open_rc == -ENOENT) {
		/* -ENOENT means that the target path does not exist. */
		cifs_dbg(FYI,
			 "%s: symlink target path '%s' does not exist, "
			 "creating '%s' as file symlink\n",
			 __func__, symname, full_path);
	} else {
		/* Try to open as a file (NOT_DIR) */
		oplock = 0;
		oparms.create_options = cifs_create_options(cifs_sb,
							    CREATE_NOT_DIR | OPEN_REPARSE_POINT);
		open_rc = tcon->ses->server->ops->open(xid, &oparms, &oplock, NULL);
		if (open_rc == 0) {
			/* Successful open means that the target path is definitely a file. */
			*directory = false;
			tcon->ses->server->ops->close(xid, tcon, &fid);
		} else if (open_rc == -EISDIR) {
			/* -EISDIR means that the target path is definitely a directory. */
			*directory = true;
		} else {
			/*
			 * This code branch is called when we do not have a permission to
			 * open the resolved_path or some other client/process denied
			 * opening the resolved_path.
			 *
			 * TODO: Try to use ops->query_dir_first on the parent directory
			 * of resolved_path, search for basename of resolved_path and
			 * check if the ATTR_DIRECTORY is set in fi.Attributes. In some
			 * case this could work also when opening of the path is denied.
			 */
			cifs_dbg(FYI,
				 "%s: cannot determinate if the symlink target path '%s' "
				 "is directory or not, creating '%s' as file symlink\n",
				 __func__, symname, full_path);
		}
	}

	kfree(resolved_path);
	cifs_put_tlink(tlink);
	return 0;
}

static int create_native_socket(const unsigned int xid, struct inode *inode,
				struct dentry *dentry, struct cifs_tcon *tcon,
				const char *full_path)
{
	struct reparse_data_buffer buf = {
		.ReparseTag = cpu_to_le32(IO_REPARSE_TAG_AF_UNIX),
		.ReparseDataLength = cpu_to_le16(0),
	};
	struct cifs_open_info_data data = {
		.reparse_point = true,
		.reparse = { .tag = IO_REPARSE_TAG_AF_UNIX, .buf = &buf, },
	};
	struct kvec iov = {
		.iov_base = &buf,
		.iov_len = sizeof(buf),
	};
	struct inode *new;
	int rc = 0;

	new = smb2_get_reparse_inode(&data, inode->i_sb, xid,
				     tcon, full_path, false, &iov, NULL);
	if (!IS_ERR(new))
		d_instantiate(dentry, new);
	else
		rc = PTR_ERR(new);
	cifs_free_open_info(&data);
	return rc;
}

static int nfs_set_reparse_buf(struct reparse_nfs_data_buffer *buf,
			       mode_t mode, dev_t dev,
			       __le16 *symname_utf16,
			       int symname_utf16_len,
			       struct kvec *iov)
{
	u64 type;
	u16 len, dlen;

	len = sizeof(*buf);

	switch ((type = reparse_mode_nfs_type(mode))) {
	case NFS_SPECFILE_BLK:
	case NFS_SPECFILE_CHR:
		dlen = 2 * sizeof(__le32);
		((__le32 *)buf->DataBuffer)[0] = cpu_to_le32(MAJOR(dev));
		((__le32 *)buf->DataBuffer)[1] = cpu_to_le32(MINOR(dev));
		break;
	case NFS_SPECFILE_LNK:
		dlen = symname_utf16_len;
		memcpy(buf->DataBuffer, symname_utf16, symname_utf16_len);
		break;
	case NFS_SPECFILE_FIFO:
	case NFS_SPECFILE_SOCK:
		dlen = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	buf->ReparseTag = cpu_to_le32(IO_REPARSE_TAG_NFS);
	buf->Reserved = 0;
	buf->InodeType = cpu_to_le64(type);
	buf->ReparseDataLength = cpu_to_le16(len + dlen -
					     sizeof(struct reparse_data_buffer));
	iov->iov_base = buf;
	iov->iov_len = len + dlen;
	return 0;
}

static int mknod_nfs(unsigned int xid, struct inode *inode,
		     struct dentry *dentry, struct cifs_tcon *tcon,
		     const char *full_path, umode_t mode, dev_t dev,
		     const char *symname)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifs_open_info_data data;
	struct reparse_nfs_data_buffer *p = NULL;
	__le16 *symname_utf16 = NULL;
	int symname_utf16_len = 0;
	struct inode *new;
	struct kvec iov;
	__u8 buf[sizeof(*p) + sizeof(__le64)];
	int rc;

	if (S_ISLNK(mode)) {
		symname_utf16 = cifs_strndup_to_utf16(symname, strlen(symname),
						      &symname_utf16_len,
						      cifs_sb->local_nls,
						      NO_MAP_UNI_RSVD);
		if (!symname_utf16) {
			rc = -ENOMEM;
			goto out;
		}
		symname_utf16_len -= 2; /* symlink is without trailing wide-nul */
		p = kzalloc(sizeof(*p) + symname_utf16_len, GFP_KERNEL);
		if (!p) {
			rc = -ENOMEM;
			goto out;
		}
	} else {
		p = (struct reparse_nfs_data_buffer *)buf;
	}
	rc = nfs_set_reparse_buf(p, mode, dev, symname_utf16, symname_utf16_len, &iov);
	if (rc)
		goto out;

	data = (struct cifs_open_info_data) {
		.reparse_point = true,
		.reparse = { .tag = IO_REPARSE_TAG_NFS, .buf = (struct reparse_data_buffer *)p, },
		.symlink_target = kstrdup(symname, GFP_KERNEL),
	};

	new = smb2_get_reparse_inode(&data, inode->i_sb, xid,
				     tcon, full_path, false, &iov, NULL);
	if (!IS_ERR(new))
		d_instantiate(dentry, new);
	else
		rc = PTR_ERR(new);
	cifs_free_open_info(&data);
out:
	if (S_ISLNK(mode)) {
		kfree(symname_utf16);
		kfree(p);
	}
	return rc;
}

static int wsl_set_reparse_buf(struct reparse_data_buffer **buf,
			       mode_t mode, const char *symname,
			       struct cifs_sb_info *cifs_sb,
			       struct kvec *iov)
{
	struct reparse_wsl_symlink_data_buffer *symlink_buf;
	__le16 *symname_utf16;
	int symname_utf16_len;
	int symname_utf8_maxlen;
	int symname_utf8_len;
	size_t buf_len;
	u32 tag;

	switch ((tag = reparse_mode_wsl_tag(mode))) {
	case IO_REPARSE_TAG_LX_BLK:
	case IO_REPARSE_TAG_LX_CHR:
	case IO_REPARSE_TAG_LX_FIFO:
	case IO_REPARSE_TAG_AF_UNIX:
		buf_len = sizeof(struct reparse_data_buffer);
		*buf = kzalloc(buf_len, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;
		break;
	case IO_REPARSE_TAG_LX_SYMLINK:
		symname_utf16 = cifs_strndup_to_utf16(symname, strlen(symname),
						      &symname_utf16_len,
						      cifs_sb->local_nls,
						      NO_MAP_UNI_RSVD);
		if (!symname_utf16)
			return -ENOMEM;
		symname_utf8_maxlen = symname_utf16_len/2*3;
		symlink_buf = kzalloc(sizeof(struct reparse_wsl_symlink_data_buffer) +
				      symname_utf8_maxlen, GFP_KERNEL);
		if (!symlink_buf) {
			kfree(symname_utf16);
			return -ENOMEM;
		}
		/* Flag 0x02000000 is unknown, but all wsl symlinks have this value */
		symlink_buf->Flags = cpu_to_le32(0x02000000);
		/* PathBuffer is in UTF-8 but without trailing null-term byte */
		symname_utf8_len = utf16s_to_utf8s((wchar_t *)symname_utf16, symname_utf16_len/2,
						   UTF16_LITTLE_ENDIAN,
						   symlink_buf->PathBuffer,
						   symname_utf8_maxlen);
		*buf = (struct reparse_data_buffer *)symlink_buf;
		buf_len = sizeof(struct reparse_wsl_symlink_data_buffer) + symname_utf8_len;
		kfree(symname_utf16);
		break;
	default:
		return -EOPNOTSUPP;
	}

	(*buf)->ReparseTag = cpu_to_le32(tag);
	(*buf)->Reserved = 0;
	(*buf)->ReparseDataLength = cpu_to_le16(buf_len - sizeof(struct reparse_data_buffer));
	iov->iov_base = *buf;
	iov->iov_len = buf_len;
	return 0;
}

static struct smb2_create_ea_ctx *ea_create_context(u32 dlen, size_t *cc_len)
{
	struct smb2_create_ea_ctx *cc;

	*cc_len = round_up(sizeof(*cc) + dlen, 8);
	cc = kzalloc(*cc_len, GFP_KERNEL);
	if (!cc)
		return ERR_PTR(-ENOMEM);

	cc->ctx.NameOffset = cpu_to_le16(offsetof(struct smb2_create_ea_ctx,
						  name));
	cc->ctx.NameLength = cpu_to_le16(4);
	memcpy(cc->name, SMB2_CREATE_EA_BUFFER, strlen(SMB2_CREATE_EA_BUFFER));
	cc->ctx.DataOffset = cpu_to_le16(offsetof(struct smb2_create_ea_ctx, ea));
	cc->ctx.DataLength = cpu_to_le32(dlen);
	return cc;
}

struct wsl_xattr {
	const char	*name;
	__le64		value;
	u16		size;
	u32		next;
};

static int wsl_set_xattrs(struct inode *inode, umode_t _mode,
			  dev_t _dev, struct kvec *iov)
{
	struct smb2_file_full_ea_info *ea;
	struct smb2_create_ea_ctx *cc;
	struct smb3_fs_context *ctx = CIFS_SB(inode->i_sb)->ctx;
	__le64 uid = cpu_to_le64(from_kuid(current_user_ns(), ctx->linux_uid));
	__le64 gid = cpu_to_le64(from_kgid(current_user_ns(), ctx->linux_gid));
	__le64 dev = cpu_to_le64(((u64)MINOR(_dev) << 32) | MAJOR(_dev));
	__le64 mode = cpu_to_le64(_mode);
	struct wsl_xattr xattrs[] = {
		{ .name = SMB2_WSL_XATTR_UID,  .value = uid,  .size = SMB2_WSL_XATTR_UID_SIZE, },
		{ .name = SMB2_WSL_XATTR_GID,  .value = gid,  .size = SMB2_WSL_XATTR_GID_SIZE, },
		{ .name = SMB2_WSL_XATTR_MODE, .value = mode, .size = SMB2_WSL_XATTR_MODE_SIZE, },
		{ .name = SMB2_WSL_XATTR_DEV,  .value = dev, .size = SMB2_WSL_XATTR_DEV_SIZE, },
	};
	size_t cc_len;
	u32 dlen = 0, next = 0;
	int i, num_xattrs;
	u8 name_size = SMB2_WSL_XATTR_NAME_LEN + 1;

	memset(iov, 0, sizeof(*iov));

	/* Exclude $LXDEV xattr for non-device files */
	if (!S_ISBLK(_mode) && !S_ISCHR(_mode))
		num_xattrs = ARRAY_SIZE(xattrs) - 1;
	else
		num_xattrs = ARRAY_SIZE(xattrs);

	for (i = 0; i < num_xattrs; i++) {
		xattrs[i].next = ALIGN(sizeof(*ea) + name_size +
				       xattrs[i].size, 4);
		dlen += xattrs[i].next;
	}

	cc = ea_create_context(dlen, &cc_len);
	if (IS_ERR(cc))
		return PTR_ERR(cc);

	ea = &cc->ea;
	for (i = 0; i < num_xattrs; i++) {
		ea = (void *)((u8 *)ea + next);
		next = xattrs[i].next;
		ea->next_entry_offset = cpu_to_le32(next);

		ea->ea_name_length = name_size - 1;
		ea->ea_value_length = cpu_to_le16(xattrs[i].size);
		memcpy(ea->ea_data, xattrs[i].name, name_size);
		memcpy(&ea->ea_data[name_size],
		       &xattrs[i].value, xattrs[i].size);
	}
	ea->next_entry_offset = 0;

	iov->iov_base = cc;
	iov->iov_len = cc_len;
	return 0;
}

static int mknod_wsl(unsigned int xid, struct inode *inode,
		     struct dentry *dentry, struct cifs_tcon *tcon,
		     const char *full_path, umode_t mode, dev_t dev,
		     const char *symname)
{
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct cifs_open_info_data data;
	struct reparse_data_buffer *buf;
	struct smb2_create_ea_ctx *cc;
	struct inode *new;
	unsigned int len;
	struct kvec reparse_iov, xattr_iov;
	int rc;

	rc = wsl_set_reparse_buf(&buf, mode, symname, cifs_sb, &reparse_iov);
	if (rc)
		return rc;

	rc = wsl_set_xattrs(inode, mode, dev, &xattr_iov);
	if (rc) {
		kfree(buf);
		return rc;
	}

	data = (struct cifs_open_info_data) {
		.reparse_point = true,
		.reparse = { .tag = le32_to_cpu(buf->ReparseTag), .buf = buf, },
		.symlink_target = kstrdup(symname, GFP_KERNEL),
	};

	cc = xattr_iov.iov_base;
	len = le32_to_cpu(cc->ctx.DataLength);
	memcpy(data.wsl.eas, &cc->ea, len);
	data.wsl.eas_len = len;

	new = smb2_get_reparse_inode(&data, inode->i_sb,
				     xid, tcon, full_path, false,
				     &reparse_iov, &xattr_iov);
	if (!IS_ERR(new))
		d_instantiate(dentry, new);
	else
		rc = PTR_ERR(new);
	cifs_free_open_info(&data);
	kfree(xattr_iov.iov_base);
	kfree(buf);
	return rc;
}

int smb2_mknod_reparse(unsigned int xid, struct inode *inode,
		       struct dentry *dentry, struct cifs_tcon *tcon,
		       const char *full_path, umode_t mode, dev_t dev)
{
	struct smb3_fs_context *ctx = CIFS_SB(inode->i_sb)->ctx;

	if (S_ISSOCK(mode) && !ctx->nonativesocket && ctx->reparse_type != CIFS_REPARSE_TYPE_NONE)
		return create_native_socket(xid, inode, dentry, tcon, full_path);

	switch (ctx->reparse_type) {
	case CIFS_REPARSE_TYPE_NFS:
		return mknod_nfs(xid, inode, dentry, tcon, full_path, mode, dev, NULL);
	case CIFS_REPARSE_TYPE_WSL:
		return mknod_wsl(xid, inode, dentry, tcon, full_path, mode, dev, NULL);
	default:
		return -EOPNOTSUPP;
	}
}

/* See MS-FSCC 2.1.2.6 for the 'NFS' style reparse tags */
static int parse_reparse_nfs(struct reparse_nfs_data_buffer *buf,
			       struct cifs_sb_info *cifs_sb,
			       struct cifs_open_info_data *data)
{
	unsigned int len;
	u64 type;

	len = le16_to_cpu(buf->ReparseDataLength);
	if (len < sizeof(buf->InodeType)) {
		cifs_dbg(VFS, "srv returned malformed nfs buffer\n");
		return -EIO;
	}

	len -= sizeof(buf->InodeType);

	switch ((type = le64_to_cpu(buf->InodeType))) {
	case NFS_SPECFILE_LNK:
		if (len == 0 || (len % 2)) {
			cifs_dbg(VFS, "srv returned malformed nfs symlink buffer\n");
			return -EIO;
		}
		/*
		 * Check that buffer does not contain UTF-16 null codepoint
		 * because Linux cannot process symlink with null byte.
		 */
		if (UniStrnlen((wchar_t *)buf->DataBuffer, len/2) != len/2) {
			cifs_dbg(VFS, "srv returned null byte in nfs symlink target location\n");
			return -EIO;
		}
		data->symlink_target = cifs_strndup_from_utf16(buf->DataBuffer,
							       len, true,
							       cifs_sb->local_nls);
		if (!data->symlink_target)
			return -ENOMEM;
		cifs_dbg(FYI, "%s: target path: %s\n",
			 __func__, data->symlink_target);
		break;
	case NFS_SPECFILE_CHR:
	case NFS_SPECFILE_BLK:
		/* DataBuffer for block and char devices contains two 32-bit numbers */
		if (len != 8) {
			cifs_dbg(VFS, "srv returned malformed nfs buffer for type: 0x%llx\n", type);
			return -EIO;
		}
		break;
	case NFS_SPECFILE_FIFO:
	case NFS_SPECFILE_SOCK:
		/* DataBuffer for fifos and sockets is empty */
		if (len != 0) {
			cifs_dbg(VFS, "srv returned malformed nfs buffer for type: 0x%llx\n", type);
			return -EIO;
		}
		break;
	default:
		cifs_dbg(VFS, "%s: unhandled inode type: 0x%llx\n",
			 __func__, type);
		return -EOPNOTSUPP;
	}
	return 0;
}

int smb2_parse_native_symlink(char **target, const char *buf, unsigned int len,
			      bool relative,
			      const char *full_path,
			      struct cifs_sb_info *cifs_sb)
{
	char sep = CIFS_DIR_SEP(cifs_sb);
	char *linux_target = NULL;
	char *smb_target = NULL;
	int symlinkroot_len;
	int abs_path_len;
	char *abs_path;
	int levels;
	int rc;
	int i;

	/* Check that length it valid */
	if (!len || (len % 2)) {
		cifs_dbg(VFS, "srv returned malformed symlink buffer\n");
		rc = -EIO;
		goto out;
	}

	/*
	 * Check that buffer does not contain UTF-16 null codepoint
	 * because Linux cannot process symlink with null byte.
	 */
	if (UniStrnlen((wchar_t *)buf, len/2) != len/2) {
		cifs_dbg(VFS, "srv returned null byte in native symlink target location\n");
		rc = -EIO;
		goto out;
	}

	smb_target = cifs_strndup_from_utf16(buf, len, true, cifs_sb->local_nls);
	if (!smb_target) {
		rc = -ENOMEM;
		goto out;
	}

	if (!(cifs_sb->mnt_cifs_flags & CIFS_MOUNT_POSIX_PATHS) && !relative) {
		/*
		 * This is an absolute symlink from the server which does not
		 * support POSIX paths, so the symlink is in NT-style path.
		 * So convert it to absolute Linux symlink target path. Root of
		 * the NT-style path for symlinks is specified in "symlinkroot"
		 * mount option.
		 *
		 * Root of the DOS and Win32 paths is at NT path \??\
		 * It means that DOS/Win32 path C:\folder\file.txt is
		 * NT path \??\C:\folder\file.txt
		 *
		 * NT systems have some well-known object symlinks in their NT
		 * hierarchy, which is needed to take into account when resolving
		 * other symlinks. Most commonly used symlink paths are:
		 * \?? -> \GLOBAL??
		 * \DosDevices -> \??
		 * \GLOBAL??\GLOBALROOT -> \
		 * \GLOBAL??\Global -> \GLOBAL??
		 * \GLOBAL??\NUL -> \Device\Null
		 * \GLOBAL??\UNC -> \Device\Mup
		 * \GLOBAL??\PhysicalDrive0 -> \Device\Harddisk0\DR0 (for each harddisk)
		 * \GLOBAL??\A: -> \Device\Floppy0 (if A: is the first floppy)
		 * \GLOBAL??\C: -> \Device\HarddiskVolume1 (if C: is the first harddisk)
		 * \GLOBAL??\D: -> \Device\CdRom0 (if D: is first cdrom)
		 * \SystemRoot -> \Device\Harddisk0\Partition1\WINDOWS (or where is NT system installed)
		 * \Volume{...} -> \Device\HarddiskVolume1 (where ... is system generated guid)
		 *
		 * In most common cases, absolute NT symlinks points to path on
		 * DOS/Win32 drive letter, system-specific Volume or on UNC share.
		 * Here are few examples of commonly used absolute NT symlinks
		 * created by mklink.exe tool:
		 * \??\C:\folder\file.txt
		 * \??\\C:\folder\file.txt
		 * \??\UNC\server\share\file.txt
		 * \??\\UNC\server\share\file.txt
		 * \??\Volume{b75e2c83-0000-0000-0000-602f00000000}\folder\file.txt
		 *
		 * It means that the most common path prefix \??\ is also NT path
		 * symlink (to \GLOBAL??). It is less common that second path
		 * separator is double backslash, but it is valid.
		 *
		 * Volume guid is randomly generated by the target system and so
		 * only the target system knows the mapping between guid and the
		 * hardisk number. Over SMB it is not possible to resolve this
		 * mapping, therefore symlinks pointing to target location of
		 * volume guids are totally unusable over SMB.
		 *
		 * For now parse only symlink paths available for DOS and Win32.
		 * Those are paths with \??\ prefix or paths which points to \??\
		 * via other NT symlink (\DosDevices\, \GLOBAL??\, ...).
		 */
		abs_path = smb_target;
globalroot:
		if (strstarts(abs_path, "\\??\\"))
			abs_path += sizeof("\\??\\")-1;
		else if (strstarts(abs_path, "\\DosDevices\\"))
			abs_path += sizeof("\\DosDevices\\")-1;
		else if (strstarts(abs_path, "\\GLOBAL??\\"))
			abs_path += sizeof("\\GLOBAL??\\")-1;
		else {
			/* Unhandled absolute symlink, points outside of DOS/Win32 */
			cifs_dbg(VFS,
				 "absolute symlink '%s' cannot be converted from NT format "
				 "because points to unknown target\n",
				 smb_target);
			rc = -EIO;
			goto out;
		}

		/* Sometimes path separator after \?? is double backslash */
		if (abs_path[0] == '\\')
			abs_path++;

		while (strstarts(abs_path, "Global\\"))
			abs_path += sizeof("Global\\")-1;

		if (strstarts(abs_path, "GLOBALROOT\\")) {
			/* Label globalroot requires path with leading '\\', so do not trim '\\' */
			abs_path += sizeof("GLOBALROOT")-1;
			goto globalroot;
		}

		/* For now parse only paths to drive letters */
		if (((abs_path[0] >= 'A' && abs_path[0] <= 'Z') ||
		     (abs_path[0] >= 'a' && abs_path[0] <= 'z')) &&
		    abs_path[1] == ':' &&
		    (abs_path[2] == '\\' || abs_path[2] == '\0')) {
			/* Convert drive letter to lowercase and drop colon */
			char drive_letter = abs_path[0];
			if (drive_letter >= 'A' && drive_letter <= 'Z')
				drive_letter += 'a'-'A';
			abs_path++;
			abs_path[0] = drive_letter;
		} else {
			/* Unhandled absolute symlink. Report an error. */
			cifs_dbg(VFS,
				 "absolute symlink '%s' cannot be converted from NT format "
				 "because points to unknown target\n",
				 smb_target);
			rc = -EIO;
			goto out;
		}

		abs_path_len = strlen(abs_path)+1;
		symlinkroot_len = strlen(cifs_sb->ctx->symlinkroot);
		if (cifs_sb->ctx->symlinkroot[symlinkroot_len-1] == '/')
			symlinkroot_len--;
		linux_target = kmalloc(symlinkroot_len + 1 + abs_path_len, GFP_KERNEL);
		if (!linux_target) {
			rc = -ENOMEM;
			goto out;
		}
		memcpy(linux_target, cifs_sb->ctx->symlinkroot, symlinkroot_len);
		linux_target[symlinkroot_len] = '/';
		memcpy(linux_target + symlinkroot_len + 1, abs_path, abs_path_len);
	} else if (smb_target[0] == sep && relative) {
		/*
		 * This is a relative SMB symlink from the top of the share,
		 * which is the top level directory of the Linux mount point.
		 * Linux does not support such relative symlinks, so convert
		 * it to the relative symlink from the current directory.
		 * full_path is the SMB path to the symlink (from which is
		 * extracted current directory) and smb_target is the SMB path
		 * where symlink points, therefore full_path must always be on
		 * the SMB share.
		 */
		int smb_target_len = strlen(smb_target)+1;
		levels = 0;
		for (i = 1; full_path[i]; i++) { /* i=1 to skip leading sep */
			if (full_path[i] == sep)
				levels++;
		}
		linux_target = kmalloc(levels*3 + smb_target_len, GFP_KERNEL);
		if (!linux_target) {
			rc = -ENOMEM;
			goto out;
		}
		for (i = 0; i < levels; i++) {
			linux_target[i*3 + 0] = '.';
			linux_target[i*3 + 1] = '.';
			linux_target[i*3 + 2] = sep;
		}
		memcpy(linux_target + levels*3, smb_target+1, smb_target_len); /* +1 to skip leading sep */
	} else {
		/*
		 * This is either an absolute symlink in POSIX-style format
		 * or relative SMB symlink from the current directory.
		 * These paths have same format as Linux symlinks, so no
		 * conversion is needed.
		 */
		linux_target = smb_target;
		smb_target = NULL;
	}

	if (sep == '\\')
		convert_delimiter(linux_target, '/');

	rc = 0;
	*target = linux_target;

	cifs_dbg(FYI, "%s: symlink target: %s\n", __func__, *target);

out:
	if (rc != 0)
		kfree(linux_target);
	kfree(smb_target);
	return rc;
}

static int parse_reparse_native_symlink(struct reparse_symlink_data_buffer *sym,
				 u32 plen,
				 struct cifs_sb_info *cifs_sb,
				 const char *full_path,
				 struct cifs_open_info_data *data)
{
	unsigned int len;
	unsigned int offs;

	/* We handle Symbolic Link reparse tag here. See: MS-FSCC 2.1.2.4 */

	offs = le16_to_cpu(sym->SubstituteNameOffset);
	len = le16_to_cpu(sym->SubstituteNameLength);
	if (offs + 20 > plen || offs + len + 20 > plen) {
		cifs_dbg(VFS, "srv returned malformed symlink buffer\n");
		return -EIO;
	}

	return smb2_parse_native_symlink(&data->symlink_target,
					 sym->PathBuffer + offs,
					 len,
					 le32_to_cpu(sym->Flags) & SYMLINK_FLAG_RELATIVE,
					 full_path,
					 cifs_sb);
}

static int parse_reparse_wsl_symlink(struct reparse_wsl_symlink_data_buffer *buf,
				     struct cifs_sb_info *cifs_sb,
				     struct cifs_open_info_data *data)
{
	int len = le16_to_cpu(buf->ReparseDataLength);
	int symname_utf8_len;
	__le16 *symname_utf16;
	int symname_utf16_len;

	if (len <= sizeof(buf->Flags)) {
		cifs_dbg(VFS, "srv returned malformed wsl symlink buffer\n");
		return -EIO;
	}

	/* PathBuffer is in UTF-8 but without trailing null-term byte */
	symname_utf8_len = len - sizeof(buf->Flags);
	/*
	 * Check that buffer does not contain null byte
	 * because Linux cannot process symlink with null byte.
	 */
	if (strnlen(buf->PathBuffer, symname_utf8_len) != symname_utf8_len) {
		cifs_dbg(VFS, "srv returned null byte in wsl symlink target location\n");
		return -EIO;
	}
	symname_utf16 = kzalloc(symname_utf8_len * 2, GFP_KERNEL);
	if (!symname_utf16)
		return -ENOMEM;
	symname_utf16_len = utf8s_to_utf16s(buf->PathBuffer, symname_utf8_len,
					    UTF16_LITTLE_ENDIAN,
					    (wchar_t *) symname_utf16, symname_utf8_len * 2);
	if (symname_utf16_len < 0) {
		kfree(symname_utf16);
		return symname_utf16_len;
	}
	symname_utf16_len *= 2; /* utf8s_to_utf16s() returns number of u16 items, not byte length */

	data->symlink_target = cifs_strndup_from_utf16((u8 *)symname_utf16,
						       symname_utf16_len, true,
						       cifs_sb->local_nls);
	kfree(symname_utf16);
	if (!data->symlink_target)
		return -ENOMEM;

	return 0;
}

int parse_reparse_point(struct reparse_data_buffer *buf,
			u32 plen, struct cifs_sb_info *cifs_sb,
			const char *full_path,
			struct cifs_open_info_data *data)
{
	struct cifs_tcon *tcon = cifs_sb_master_tcon(cifs_sb);

	data->reparse.buf = buf;

	/* See MS-FSCC 2.1.2 */
	switch (le32_to_cpu(buf->ReparseTag)) {
	case IO_REPARSE_TAG_NFS:
		return parse_reparse_nfs((struct reparse_nfs_data_buffer *)buf,
					   cifs_sb, data);
	case IO_REPARSE_TAG_SYMLINK:
		return parse_reparse_native_symlink(
			(struct reparse_symlink_data_buffer *)buf,
			plen, cifs_sb, full_path, data);
	case IO_REPARSE_TAG_LX_SYMLINK:
		return parse_reparse_wsl_symlink(
			(struct reparse_wsl_symlink_data_buffer *)buf,
			cifs_sb, data);
	case IO_REPARSE_TAG_AF_UNIX:
	case IO_REPARSE_TAG_LX_FIFO:
	case IO_REPARSE_TAG_LX_CHR:
	case IO_REPARSE_TAG_LX_BLK:
		if (le16_to_cpu(buf->ReparseDataLength) != 0) {
			cifs_dbg(VFS, "srv returned malformed buffer for reparse point: 0x%08x\n",
				 le32_to_cpu(buf->ReparseTag));
			return -EIO;
		}
		return 0;
	default:
		cifs_tcon_dbg(VFS | ONCE, "unhandled reparse tag: 0x%08x\n",
			      le32_to_cpu(buf->ReparseTag));
		return -EOPNOTSUPP;
	}
}

int smb2_parse_reparse_point(struct cifs_sb_info *cifs_sb,
			     const char *full_path,
			     struct kvec *rsp_iov,
			     struct cifs_open_info_data *data)
{
	struct reparse_data_buffer *buf;
	struct smb2_ioctl_rsp *io = rsp_iov->iov_base;
	u32 plen = le32_to_cpu(io->OutputCount);

	buf = (struct reparse_data_buffer *)((u8 *)io +
					     le32_to_cpu(io->OutputOffset));
	return parse_reparse_point(buf, plen, cifs_sb, full_path, data);
}

static bool wsl_to_fattr(struct cifs_open_info_data *data,
			 struct cifs_sb_info *cifs_sb,
			 u32 tag, struct cifs_fattr *fattr)
{
	struct smb2_file_full_ea_info *ea;
	bool have_xattr_dev = false;
	u32 next = 0;

	switch (tag) {
	case IO_REPARSE_TAG_LX_SYMLINK:
		fattr->cf_mode |= S_IFLNK;
		break;
	case IO_REPARSE_TAG_LX_FIFO:
		fattr->cf_mode |= S_IFIFO;
		break;
	case IO_REPARSE_TAG_AF_UNIX:
		fattr->cf_mode |= S_IFSOCK;
		break;
	case IO_REPARSE_TAG_LX_CHR:
		fattr->cf_mode |= S_IFCHR;
		break;
	case IO_REPARSE_TAG_LX_BLK:
		fattr->cf_mode |= S_IFBLK;
		break;
	}

	if (!data->wsl.eas_len)
		goto out;

	ea = (struct smb2_file_full_ea_info *)data->wsl.eas;
	do {
		const char *name;
		void *v;
		u8 nlen;

		ea = (void *)((u8 *)ea + next);
		next = le32_to_cpu(ea->next_entry_offset);
		if (!le16_to_cpu(ea->ea_value_length))
			continue;

		name = ea->ea_data;
		nlen = ea->ea_name_length;
		v = (void *)((u8 *)ea->ea_data + ea->ea_name_length + 1);

		if (!strncmp(name, SMB2_WSL_XATTR_UID, nlen))
			fattr->cf_uid = wsl_make_kuid(cifs_sb, v);
		else if (!strncmp(name, SMB2_WSL_XATTR_GID, nlen))
			fattr->cf_gid = wsl_make_kgid(cifs_sb, v);
		else if (!strncmp(name, SMB2_WSL_XATTR_MODE, nlen)) {
			/* File type in reparse point tag and in xattr mode must match. */
			if (S_DT(fattr->cf_mode) != S_DT(le32_to_cpu(*(__le32 *)v)))
				return false;
			fattr->cf_mode = (umode_t)le32_to_cpu(*(__le32 *)v);
		} else if (!strncmp(name, SMB2_WSL_XATTR_DEV, nlen)) {
			fattr->cf_rdev = reparse_mkdev(v);
			have_xattr_dev = true;
		}
	} while (next);
out:

	/* Major and minor numbers for char and block devices are mandatory. */
	if (!have_xattr_dev && (tag == IO_REPARSE_TAG_LX_CHR || tag == IO_REPARSE_TAG_LX_BLK))
		return false;

	fattr->cf_dtype = S_DT(fattr->cf_mode);
	return true;
}

static bool posix_reparse_to_fattr(struct cifs_sb_info *cifs_sb,
				   struct cifs_fattr *fattr,
				   struct cifs_open_info_data *data)
{
	struct reparse_nfs_data_buffer *buf = (struct reparse_nfs_data_buffer *)data->reparse.buf;

	if (buf == NULL)
		return true;

	if (le16_to_cpu(buf->ReparseDataLength) < sizeof(buf->InodeType)) {
		WARN_ON_ONCE(1);
		return false;
	}

	switch (le64_to_cpu(buf->InodeType)) {
	case NFS_SPECFILE_CHR:
		if (le16_to_cpu(buf->ReparseDataLength) != sizeof(buf->InodeType) + 8) {
			WARN_ON_ONCE(1);
			return false;
		}
		fattr->cf_mode |= S_IFCHR;
		fattr->cf_rdev = reparse_mkdev(buf->DataBuffer);
		break;
	case NFS_SPECFILE_BLK:
		if (le16_to_cpu(buf->ReparseDataLength) != sizeof(buf->InodeType) + 8) {
			WARN_ON_ONCE(1);
			return false;
		}
		fattr->cf_mode |= S_IFBLK;
		fattr->cf_rdev = reparse_mkdev(buf->DataBuffer);
		break;
	case NFS_SPECFILE_FIFO:
		fattr->cf_mode |= S_IFIFO;
		break;
	case NFS_SPECFILE_SOCK:
		fattr->cf_mode |= S_IFSOCK;
		break;
	case NFS_SPECFILE_LNK:
		fattr->cf_mode |= S_IFLNK;
		break;
	default:
		WARN_ON_ONCE(1);
		return false;
	}
	return true;
}

bool cifs_reparse_point_to_fattr(struct cifs_sb_info *cifs_sb,
				 struct cifs_fattr *fattr,
				 struct cifs_open_info_data *data)
{
	u32 tag = data->reparse.tag;
	bool ok;

	switch (tag) {
	case IO_REPARSE_TAG_INTERNAL:
		if (!(fattr->cf_cifsattrs & ATTR_DIRECTORY))
			return false;
		fallthrough;
	case IO_REPARSE_TAG_DFS:
	case IO_REPARSE_TAG_DFSR:
	case IO_REPARSE_TAG_MOUNT_POINT:
		/* See cifs_create_junction_fattr() */
		fattr->cf_mode = S_IFDIR | 0711;
		break;
	case IO_REPARSE_TAG_LX_SYMLINK:
	case IO_REPARSE_TAG_LX_FIFO:
	case IO_REPARSE_TAG_AF_UNIX:
	case IO_REPARSE_TAG_LX_CHR:
	case IO_REPARSE_TAG_LX_BLK:
		ok = wsl_to_fattr(data, cifs_sb, tag, fattr);
		if (!ok)
			return false;
		break;
	case IO_REPARSE_TAG_NFS:
		ok = posix_reparse_to_fattr(cifs_sb, fattr, data);
		if (!ok)
			return false;
		break;
	case 0: /* SMB1 symlink */
	case IO_REPARSE_TAG_SYMLINK:
		fattr->cf_mode |= S_IFLNK;
		break;
	default:
		return false;
	}

	fattr->cf_dtype = S_DT(fattr->cf_mode);
	return true;
}
