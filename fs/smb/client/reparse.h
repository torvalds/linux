/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Paulo Alcantara <pc@manguebit.com>
 */

#ifndef _CIFS_REPARSE_H
#define _CIFS_REPARSE_H

#include <linux/fs.h>
#include <linux/stat.h>
#include "cifsglob.h"

static inline dev_t reparse_nfs_mkdev(struct reparse_posix_data *buf)
{
	u64 v = le64_to_cpu(*(__le64 *)buf->DataBuffer);

	return MKDEV(v >> 32, v & 0xffffffff);
}

static inline u64 reparse_mode_nfs_type(mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFBLK: return NFS_SPECFILE_BLK;
	case S_IFCHR: return NFS_SPECFILE_CHR;
	case S_IFIFO: return NFS_SPECFILE_FIFO;
	case S_IFSOCK: return NFS_SPECFILE_SOCK;
	}
	return 0;
}

static inline u32 reparse_mode_wsl_tag(mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFBLK: return IO_REPARSE_TAG_LX_BLK;
	case S_IFCHR: return IO_REPARSE_TAG_LX_CHR;
	case S_IFIFO: return IO_REPARSE_TAG_LX_FIFO;
	case S_IFSOCK: return IO_REPARSE_TAG_AF_UNIX;
	}
	return 0;
}

/*
 * Match a reparse point inode if reparse tag and ctime haven't changed.
 *
 * Windows Server updates ctime of reparse points when their data have changed.
 * The server doesn't allow changing reparse tags from existing reparse points,
 * though it's worth checking.
 */
static inline bool reparse_inode_match(struct inode *inode,
				       struct cifs_fattr *fattr)
{
	struct timespec64 ctime = inode_get_ctime(inode);

	return (CIFS_I(inode)->cifsAttrs & ATTR_REPARSE) &&
		CIFS_I(inode)->reparse_tag == fattr->cf_cifstag &&
		timespec64_equal(&ctime, &fattr->cf_ctime);
}

static inline bool cifs_open_data_reparse(struct cifs_open_info_data *data)
{
	struct smb2_file_all_info *fi = &data->fi;
	u32 attrs = le32_to_cpu(fi->Attributes);
	bool ret;

	ret = data->reparse_point || (attrs & ATTR_REPARSE);
	if (ret)
		attrs |= ATTR_REPARSE;
	fi->Attributes = cpu_to_le32(attrs);
	return ret;
}

bool cifs_reparse_point_to_fattr(struct cifs_sb_info *cifs_sb,
				 struct cifs_fattr *fattr,
				 struct cifs_open_info_data *data);
int smb2_create_reparse_symlink(const unsigned int xid, struct inode *inode,
				struct dentry *dentry, struct cifs_tcon *tcon,
				const char *full_path, const char *symname);
int smb2_mknod_reparse(unsigned int xid, struct inode *inode,
		       struct dentry *dentry, struct cifs_tcon *tcon,
		       const char *full_path, umode_t mode, dev_t dev);
int smb2_parse_reparse_point(struct cifs_sb_info *cifs_sb, struct kvec *rsp_iov,
			     struct cifs_open_info_data *data);

#endif /* _CIFS_REPARSE_H */
