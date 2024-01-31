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

int smb2_create_reparse_symlink(const unsigned int xid, struct inode *inode,
				struct dentry *dentry, struct cifs_tcon *tcon,
				const char *full_path, const char *symname)
{
	struct reparse_symlink_data_buffer *buf = NULL;
	struct cifs_open_info_data data;
	struct cifs_sb_info *cifs_sb = CIFS_SB(inode->i_sb);
	struct inode *new;
	struct kvec iov;
	__le16 *path;
	char *sym, sep = CIFS_DIR_SEP(cifs_sb);
	u16 len, plen;
	int rc = 0;

	sym = kstrdup(symname, GFP_KERNEL);
	if (!sym)
		return -ENOMEM;

	data = (struct cifs_open_info_data) {
		.reparse_point = true,
		.reparse = { .tag = IO_REPARSE_TAG_SYMLINK, },
		.symlink_target = sym,
	};

	convert_delimiter(sym, sep);
	path = cifs_convert_path_to_utf16(sym, cifs_sb);
	if (!path) {
		rc = -ENOMEM;
		goto out;
	}

	plen = 2 * UniStrnlen((wchar_t *)path, PATH_MAX);
	len = sizeof(*buf) + plen * 2;
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto out;
	}

	buf->ReparseTag = cpu_to_le32(IO_REPARSE_TAG_SYMLINK);
	buf->ReparseDataLength = cpu_to_le16(len - sizeof(struct reparse_data_buffer));
	buf->SubstituteNameOffset = cpu_to_le16(plen);
	buf->SubstituteNameLength = cpu_to_le16(plen);
	memcpy(&buf->PathBuffer[plen], path, plen);
	buf->PrintNameOffset = 0;
	buf->PrintNameLength = cpu_to_le16(plen);
	memcpy(buf->PathBuffer, path, plen);
	buf->Flags = cpu_to_le32(*symname != '/' ? SYMLINK_FLAG_RELATIVE : 0);
	if (*sym != sep)
		buf->Flags = cpu_to_le32(SYMLINK_FLAG_RELATIVE);

	convert_delimiter(sym, '/');
	iov.iov_base = buf;
	iov.iov_len = len;
	new = smb2_get_reparse_inode(&data, inode->i_sb, xid,
				     tcon, full_path, &iov, NULL);
	if (!IS_ERR(new))
		d_instantiate(dentry, new);
	else
		rc = PTR_ERR(new);
out:
	kfree(path);
	cifs_free_open_info(&data);
	kfree(buf);
	return rc;
}

static int nfs_set_reparse_buf(struct reparse_posix_data *buf,
			       mode_t mode, dev_t dev,
			       struct kvec *iov)
{
	u64 type;
	u16 len, dlen;

	len = sizeof(*buf);

	switch ((type = reparse_mode_nfs_type(mode))) {
	case NFS_SPECFILE_BLK:
	case NFS_SPECFILE_CHR:
		dlen = sizeof(__le64);
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
	*(__le64 *)buf->DataBuffer = cpu_to_le64(((u64)MAJOR(dev) << 32) |
						 MINOR(dev));
	iov->iov_base = buf;
	iov->iov_len = len + dlen;
	return 0;
}

static int mknod_nfs(unsigned int xid, struct inode *inode,
		     struct dentry *dentry, struct cifs_tcon *tcon,
		     const char *full_path, umode_t mode, dev_t dev)
{
	struct cifs_open_info_data data;
	struct reparse_posix_data *p;
	struct inode *new;
	struct kvec iov;
	__u8 buf[sizeof(*p) + sizeof(__le64)];
	int rc;

	p = (struct reparse_posix_data *)buf;
	rc = nfs_set_reparse_buf(p, mode, dev, &iov);
	if (rc)
		return rc;

	data = (struct cifs_open_info_data) {
		.reparse_point = true,
		.reparse = { .tag = IO_REPARSE_TAG_NFS, .posix = p, },
	};

	new = smb2_get_reparse_inode(&data, inode->i_sb, xid,
				     tcon, full_path, &iov, NULL);
	if (!IS_ERR(new))
		d_instantiate(dentry, new);
	else
		rc = PTR_ERR(new);
	cifs_free_open_info(&data);
	return rc;
}

static int wsl_set_reparse_buf(struct reparse_data_buffer *buf,
			       mode_t mode, struct kvec *iov)
{
	u32 tag;

	switch ((tag = reparse_mode_wsl_tag(mode))) {
	case IO_REPARSE_TAG_LX_BLK:
	case IO_REPARSE_TAG_LX_CHR:
	case IO_REPARSE_TAG_LX_FIFO:
	case IO_REPARSE_TAG_AF_UNIX:
		break;
	default:
		return -EOPNOTSUPP;
	}

	buf->ReparseTag = cpu_to_le32(tag);
	buf->Reserved = 0;
	buf->ReparseDataLength = 0;
	iov->iov_base = buf;
	iov->iov_len = sizeof(*buf);
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
		{ .name = "$LXUID", .value = uid, .size = 4, },
		{ .name = "$LXGID", .value = gid, .size = 4, },
		{ .name = "$LXMOD", .value = mode, .size = 4, },
		{ .name = "$LXDEV", .value = dev, .size = 8, },
	};
	size_t cc_len;
	u32 dlen = 0, next = 0;
	int i, num_xattrs;
	u8 name_size = strlen(xattrs[0].name) + 1;

	memset(iov, 0, sizeof(*iov));

	/* Exclude $LXDEV xattr for sockets and fifos */
	if (S_ISSOCK(_mode) || S_ISFIFO(_mode))
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
		     const char *full_path, umode_t mode, dev_t dev)
{
	struct cifs_open_info_data data;
	struct reparse_data_buffer buf;
	struct inode *new;
	struct kvec reparse_iov, xattr_iov;
	int rc;

	rc = wsl_set_reparse_buf(&buf, mode, &reparse_iov);
	if (rc)
		return rc;

	rc = wsl_set_xattrs(inode, mode, dev, &xattr_iov);
	if (rc)
		return rc;

	data = (struct cifs_open_info_data) {
		.reparse_point = true,
		.reparse = { .tag = le32_to_cpu(buf.ReparseTag), .buf = &buf, },
	};

	new = smb2_get_reparse_inode(&data, inode->i_sb,
				     xid, tcon, full_path,
				     &reparse_iov, &xattr_iov);
	if (!IS_ERR(new))
		d_instantiate(dentry, new);
	else
		rc = PTR_ERR(new);
	cifs_free_open_info(&data);
	kfree(xattr_iov.iov_base);
	return rc;
}

int smb2_mknod_reparse(unsigned int xid, struct inode *inode,
		       struct dentry *dentry, struct cifs_tcon *tcon,
		       const char *full_path, umode_t mode, dev_t dev)
{
	struct smb3_fs_context *ctx = CIFS_SB(inode->i_sb)->ctx;
	int rc = -EOPNOTSUPP;

	switch (ctx->reparse_type) {
	case CIFS_REPARSE_TYPE_NFS:
		rc = mknod_nfs(xid, inode, dentry, tcon, full_path, mode, dev);
		break;
	case CIFS_REPARSE_TYPE_WSL:
		rc = mknod_wsl(xid, inode, dentry, tcon, full_path, mode, dev);
		break;
	}
	return rc;
}

/* See MS-FSCC 2.1.2.6 for the 'NFS' style reparse tags */
static int parse_reparse_posix(struct reparse_posix_data *buf,
			       struct cifs_sb_info *cifs_sb,
			       struct cifs_open_info_data *data)
{
	unsigned int len;
	u64 type;

	switch ((type = le64_to_cpu(buf->InodeType))) {
	case NFS_SPECFILE_LNK:
		len = le16_to_cpu(buf->ReparseDataLength);
		data->symlink_target = cifs_strndup_from_utf16(buf->DataBuffer,
							       len, true,
							       cifs_sb->local_nls);
		if (!data->symlink_target)
			return -ENOMEM;
		convert_delimiter(data->symlink_target, '/');
		cifs_dbg(FYI, "%s: target path: %s\n",
			 __func__, data->symlink_target);
		break;
	case NFS_SPECFILE_CHR:
	case NFS_SPECFILE_BLK:
	case NFS_SPECFILE_FIFO:
	case NFS_SPECFILE_SOCK:
		break;
	default:
		cifs_dbg(VFS, "%s: unhandled inode type: 0x%llx\n",
			 __func__, type);
		return -EOPNOTSUPP;
	}
	return 0;
}

static int parse_reparse_symlink(struct reparse_symlink_data_buffer *sym,
				 u32 plen, bool unicode,
				 struct cifs_sb_info *cifs_sb,
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

	data->symlink_target = cifs_strndup_from_utf16(sym->PathBuffer + offs,
						       len, unicode,
						       cifs_sb->local_nls);
	if (!data->symlink_target)
		return -ENOMEM;

	convert_delimiter(data->symlink_target, '/');
	cifs_dbg(FYI, "%s: target path: %s\n", __func__, data->symlink_target);

	return 0;
}

int parse_reparse_point(struct reparse_data_buffer *buf,
			u32 plen, struct cifs_sb_info *cifs_sb,
			bool unicode, struct cifs_open_info_data *data)
{
	data->reparse.buf = buf;

	/* See MS-FSCC 2.1.2 */
	switch (le32_to_cpu(buf->ReparseTag)) {
	case IO_REPARSE_TAG_NFS:
		return parse_reparse_posix((struct reparse_posix_data *)buf,
					   cifs_sb, data);
	case IO_REPARSE_TAG_SYMLINK:
		return parse_reparse_symlink(
			(struct reparse_symlink_data_buffer *)buf,
			plen, unicode, cifs_sb, data);
	case IO_REPARSE_TAG_LX_SYMLINK:
	case IO_REPARSE_TAG_AF_UNIX:
	case IO_REPARSE_TAG_LX_FIFO:
	case IO_REPARSE_TAG_LX_CHR:
	case IO_REPARSE_TAG_LX_BLK:
		return 0;
	default:
		cifs_dbg(VFS, "%s: unhandled reparse tag: 0x%08x\n",
			 __func__, le32_to_cpu(buf->ReparseTag));
		return -EOPNOTSUPP;
	}
}

int smb2_parse_reparse_point(struct cifs_sb_info *cifs_sb,
			     struct kvec *rsp_iov,
			     struct cifs_open_info_data *data)
{
	struct reparse_data_buffer *buf;
	struct smb2_ioctl_rsp *io = rsp_iov->iov_base;
	u32 plen = le32_to_cpu(io->OutputCount);

	buf = (struct reparse_data_buffer *)((u8 *)io +
					     le32_to_cpu(io->OutputOffset));
	return parse_reparse_point(buf, plen, cifs_sb, true, data);
}

bool cifs_reparse_point_to_fattr(struct cifs_sb_info *cifs_sb,
				 struct cifs_fattr *fattr,
				 struct cifs_open_info_data *data)
{
	struct reparse_posix_data *buf = data->reparse.posix;
	u32 tag = data->reparse.tag;

	if (tag == IO_REPARSE_TAG_NFS && buf) {
		switch (le64_to_cpu(buf->InodeType)) {
		case NFS_SPECFILE_CHR:
			fattr->cf_mode |= S_IFCHR;
			fattr->cf_dtype = DT_CHR;
			fattr->cf_rdev = reparse_nfs_mkdev(buf);
			break;
		case NFS_SPECFILE_BLK:
			fattr->cf_mode |= S_IFBLK;
			fattr->cf_dtype = DT_BLK;
			fattr->cf_rdev = reparse_nfs_mkdev(buf);
			break;
		case NFS_SPECFILE_FIFO:
			fattr->cf_mode |= S_IFIFO;
			fattr->cf_dtype = DT_FIFO;
			break;
		case NFS_SPECFILE_SOCK:
			fattr->cf_mode |= S_IFSOCK;
			fattr->cf_dtype = DT_SOCK;
			break;
		case NFS_SPECFILE_LNK:
			fattr->cf_mode |= S_IFLNK;
			fattr->cf_dtype = DT_LNK;
			break;
		default:
			WARN_ON_ONCE(1);
			return false;
		}
		return true;
	}

	switch (tag) {
	case IO_REPARSE_TAG_LX_SYMLINK:
		fattr->cf_mode |= S_IFLNK;
		fattr->cf_dtype = DT_LNK;
		break;
	case IO_REPARSE_TAG_LX_FIFO:
		fattr->cf_mode |= S_IFIFO;
		fattr->cf_dtype = DT_FIFO;
		break;
	case IO_REPARSE_TAG_AF_UNIX:
		fattr->cf_mode |= S_IFSOCK;
		fattr->cf_dtype = DT_SOCK;
		break;
	case IO_REPARSE_TAG_LX_CHR:
		fattr->cf_mode |= S_IFCHR;
		fattr->cf_dtype = DT_CHR;
		break;
	case IO_REPARSE_TAG_LX_BLK:
		fattr->cf_mode |= S_IFBLK;
		fattr->cf_dtype = DT_BLK;
		break;
	case 0: /* SMB1 symlink */
	case IO_REPARSE_TAG_SYMLINK:
	case IO_REPARSE_TAG_NFS:
		fattr->cf_mode |= S_IFLNK;
		fattr->cf_dtype = DT_LNK;
		break;
	default:
		return false;
	}
	return true;
}
