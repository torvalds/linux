// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pocessing of EA's
 *
 * Part of this file is based on code from the NTFS-3G.
 *
 * Copyright (c) 2014-2021 Jean-Pierre Andre
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include <linux/fs.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>

#include "layout.h"
#include "attrib.h"
#include "index.h"
#include "dir.h"
#include "ea.h"

static int ntfs_write_ea(struct ntfs_inode *ni, __le32 type, char *value, s64 ea_off,
		s64 ea_size, bool need_truncate)
{
	struct inode *ea_vi;
	int err = 0;
	s64 written;

	ea_vi = ntfs_attr_iget(VFS_I(ni), type, AT_UNNAMED, 0);
	if (IS_ERR(ea_vi))
		return PTR_ERR(ea_vi);

	written = ntfs_inode_attr_pwrite(ea_vi, ea_off, ea_size, value, false);
	if (written != ea_size)
		err = -EIO;
	else {
		struct ntfs_inode *ea_ni = NTFS_I(ea_vi);

		if (need_truncate && ea_ni->data_size > ea_off + ea_size)
			ntfs_attr_truncate(ea_ni, ea_off + ea_size);
		mark_mft_record_dirty(ni);
	}

	iput(ea_vi);
	return err;
}

static int ntfs_ea_lookup(char *ea_buf, s64 ea_buf_size, const char *name,
			  int name_len, s64 *ea_offset, s64 *ea_size)
{
	const struct ea_attr *p_ea;
	size_t actual_size;
	loff_t offset, p_ea_size;
	unsigned int next;

	if (ea_buf_size < sizeof(struct ea_attr))
		goto out;

	offset = 0;
	do {
		p_ea = (const struct ea_attr *)&ea_buf[offset];
		next = le32_to_cpu(p_ea->next_entry_offset);
		p_ea_size = next ? next : (ea_buf_size - offset);

		if (p_ea_size < sizeof(struct ea_attr) ||
		    offset + p_ea_size > ea_buf_size)
			break;

		if ((s64)p_ea->ea_name_length + 1 >
		    p_ea_size - offsetof(struct ea_attr, ea_name))
			break;

		actual_size = ALIGN(struct_size(p_ea, ea_name, 1 + p_ea->ea_name_length +
					le16_to_cpu(p_ea->ea_value_length)), 4);
		if (actual_size > p_ea_size)
			break;

		if (p_ea->ea_name_length == name_len &&
		    !memcmp(p_ea->ea_name, name, name_len)) {
			*ea_offset = offset;
			*ea_size = next ? next : actual_size;

			if (ea_buf_size < *ea_offset + *ea_size)
				goto out;

			return 0;
		}
		offset += next;
	} while (next > 0 && offset < ea_buf_size);

out:
	return -ENOENT;
}

/*
 * Return the existing EA
 *
 * The EA_INFORMATION is not examined and the consistency of the
 * existing EA is not checked.
 *
 * If successful, the full attribute is returned unchanged
 * and its size is returned.
 * If the designated buffer is too small, the needed size is
 * returned, and the buffer is left unchanged.
 * If there is an error, a negative value is returned and errno
 * is set according to the error.
 */
static int ntfs_get_ea(struct inode *inode, const char *name, size_t name_len,
		void *buffer, size_t size)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	const struct ea_attr *p_ea;
	char *ea_buf;
	s64 ea_off, ea_size, all_ea_size, ea_info_size;
	int err;
	u32 ea_info_qlen;
	u16 ea_value_len;
	struct ea_information *p_ea_info;

	if (!NInoHasEA(ni))
		return -ENODATA;

	p_ea_info = ntfs_attr_readall(ni, AT_EA_INFORMATION, NULL, 0,
			&ea_info_size);
	if (!p_ea_info || ea_info_size != sizeof(struct ea_information)) {
		kvfree(p_ea_info);
		return -ENODATA;
	}

	ea_info_qlen = le32_to_cpu(p_ea_info->ea_query_length);
	kvfree(p_ea_info);

	ea_buf = ntfs_attr_readall(ni, AT_EA, NULL, 0, &all_ea_size);
	if (!ea_buf)
		return -ENODATA;

	if (ea_info_qlen > all_ea_size) {
		err = -EIO;
		goto free_ea_buf;
	}

	err = ntfs_ea_lookup(ea_buf, ea_info_qlen, name, name_len, &ea_off,
			&ea_size);
	if (!err) {
		p_ea = (struct ea_attr *)&ea_buf[ea_off];
		ea_value_len = le16_to_cpu(p_ea->ea_value_length);
		if (!buffer) {
			kvfree(ea_buf);
			return ea_value_len;
		}

		if (ea_value_len > size) {
			err = -ERANGE;
			goto free_ea_buf;
		}

		memcpy(buffer, &p_ea->ea_name[p_ea->ea_name_length + 1],
				ea_value_len);
		kvfree(ea_buf);
		return ea_value_len;
	}

	err = -ENODATA;
free_ea_buf:
	kvfree(ea_buf);
	return err;
}

static inline int ea_packed_size(const struct ea_attr *p_ea)
{
	/*
	 * 4 bytes for header (flags and lengths) + name length + 1 +
	 * value length.
	 */
	return 5 + p_ea->ea_name_length + le16_to_cpu(p_ea->ea_value_length);
}

/*
 * Set a new EA, and set EA_INFORMATION accordingly
 *
 * This is roughly the same as ZwSetEaFile() on Windows, however
 * the "offset to next" of the last EA should not be cleared.
 *
 * Consistency of the new EA is first checked.
 *
 * EA_INFORMATION is set first, and it is restored to its former
 * state if setting EA fails.
 */
static int ntfs_set_ea(struct inode *inode, const char *name, size_t name_len,
		const void *value, size_t val_size, int flags,
		__le16 *packed_ea_size)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct ea_information *p_ea_info = NULL;
	int ea_packed, err = 0;
	struct ea_attr *p_ea;
	u32 ea_info_qsize = 0;
	char *ea_buf = NULL;
	size_t new_ea_size = ALIGN(struct_size(p_ea, ea_name, 1 + name_len + val_size), 4);
	s64 ea_off, ea_info_size, all_ea_size, ea_size;

	if (name_len > 255)
		return -ENAMETOOLONG;

	if (ntfs_attr_exist(ni, AT_EA_INFORMATION, AT_UNNAMED, 0)) {
		p_ea_info = ntfs_attr_readall(ni, AT_EA_INFORMATION, NULL, 0,
						&ea_info_size);
		if (!p_ea_info || ea_info_size != sizeof(struct ea_information))
			goto out;

		ea_buf = ntfs_attr_readall(ni, AT_EA, NULL, 0, &all_ea_size);
		if (!ea_buf) {
			ea_info_qsize = 0;
			kvfree(p_ea_info);
			goto create_ea_info;
		}

		ea_info_qsize = le32_to_cpu(p_ea_info->ea_query_length);
	} else {
create_ea_info:
		p_ea_info = kzalloc(sizeof(struct ea_information), GFP_NOFS);
		if (!p_ea_info)
			return -ENOMEM;

		ea_info_qsize = 0;
		err = ntfs_attr_add(ni, AT_EA_INFORMATION, AT_UNNAMED, 0,
				(char *)p_ea_info, sizeof(struct ea_information));
		if (err)
			goto out;

		if (ntfs_attr_exist(ni, AT_EA, AT_UNNAMED, 0)) {
			err = ntfs_attr_remove(ni, AT_EA, AT_UNNAMED, 0);
			if (err)
				goto out;
		}

		goto alloc_new_ea;
	}

	if (ea_info_qsize > all_ea_size) {
		err = -EIO;
		goto out;
	}

	err = ntfs_ea_lookup(ea_buf, ea_info_qsize, name, name_len, &ea_off,
			&ea_size);
	if (ea_info_qsize && !err) {
		if (flags & XATTR_CREATE) {
			err = -EEXIST;
			goto out;
		}

		p_ea = (struct ea_attr *)(ea_buf + ea_off);

		if (val_size &&
		    le16_to_cpu(p_ea->ea_value_length) == val_size &&
		    !memcmp(p_ea->ea_name + p_ea->ea_name_length + 1, value,
			    val_size))
			goto out;

		le16_add_cpu(&p_ea_info->ea_length, 0 - ea_packed_size(p_ea));

		if (p_ea->flags & NEED_EA)
			le16_add_cpu(&p_ea_info->need_ea_count, -1);

		memmove((char *)p_ea, (char *)p_ea + ea_size, ea_info_qsize - (ea_off + ea_size));
		ea_info_qsize -= ea_size;
		p_ea_info->ea_query_length = cpu_to_le32(ea_info_qsize);

		err = ntfs_write_ea(ni, AT_EA_INFORMATION, (char *)p_ea_info, 0,
				sizeof(struct ea_information), false);
		if (err)
			goto out;

		err = ntfs_write_ea(ni, AT_EA, ea_buf, 0, ea_info_qsize, true);
		if (err)
			goto out;

		if ((flags & XATTR_REPLACE) && !val_size) {
			/* Remove xattr. */
			goto out;
		}
	} else {
		if (flags & XATTR_REPLACE) {
			err = -ENODATA;
			goto out;
		}
	}
	kvfree(ea_buf);

alloc_new_ea:
	ea_buf = kzalloc(new_ea_size, GFP_NOFS);
	if (!ea_buf) {
		err = -ENOMEM;
		goto out;
	}

	/*
	 * EA and REPARSE_POINT compatibility not checked any more,
	 * required by Windows 10, but having both may lead to
	 * problems with earlier versions.
	 */
	p_ea = (struct ea_attr *)ea_buf;
	memcpy(p_ea->ea_name, name, name_len);
	p_ea->ea_name_length = name_len;
	p_ea->ea_name[name_len] = 0;
	memcpy(p_ea->ea_name + name_len + 1, value, val_size);
	p_ea->ea_value_length = cpu_to_le16(val_size);
	p_ea->next_entry_offset = cpu_to_le32(new_ea_size);

	ea_packed = le16_to_cpu(p_ea_info->ea_length) + ea_packed_size(p_ea);
	p_ea_info->ea_length = cpu_to_le16(ea_packed);
	p_ea_info->ea_query_length = cpu_to_le32(ea_info_qsize + new_ea_size);

	if (ea_packed > 0xffff ||
	    ntfs_attr_size_bounds_check(ni->vol, AT_EA, new_ea_size)) {
		err = -EFBIG;
		goto out;
	}

	/*
	 * no EA or EA_INFORMATION : add them
	 */
	if (!ntfs_attr_exist(ni, AT_EA, AT_UNNAMED, 0)) {
		err = ntfs_attr_add(ni, AT_EA, AT_UNNAMED, 0, (char *)p_ea,
				new_ea_size);
		if (err)
			goto out;
	} else {
		err = ntfs_write_ea(ni, AT_EA, (char *)p_ea, ea_info_qsize,
				new_ea_size, false);
		if (err)
			goto out;
	}

	err = ntfs_write_ea(ni, AT_EA_INFORMATION, (char *)p_ea_info, 0,
			sizeof(struct ea_information), false);
	if (err)
		goto out;

	if (packed_ea_size)
		*packed_ea_size = p_ea_info->ea_length;
	mark_mft_record_dirty(ni);
out:
	if (ea_info_qsize > 0)
		NInoSetHasEA(ni);
	else
		NInoClearHasEA(ni);

	kvfree(ea_buf);
	kvfree(p_ea_info);

	return err;
}

/*
 * Check for the presence of an EA "$LXDEV" (used by WSL)
 * and return its value as a device address
 */
int ntfs_ea_get_wsl_inode(struct inode *inode, dev_t *rdevp, unsigned int flags)
{
	int err;
	__le32 v;

	if (!(flags & NTFS_VOL_UID)) {
		/* Load uid to lxuid EA */
		err = ntfs_get_ea(inode, "$LXUID", sizeof("$LXUID") - 1, &v,
				sizeof(v));
		if (err < 0)
			return err;
		if (err != sizeof(v))
			return -EIO;
		i_uid_write(inode, le32_to_cpu(v));
	}

	if (!(flags & NTFS_VOL_GID)) {
		/* Load gid to lxgid EA */
		err = ntfs_get_ea(inode, "$LXGID", sizeof("$LXGID") - 1, &v,
				sizeof(v));
		if (err < 0)
			return err;
		if (err != sizeof(v))
			return -EIO;
		i_gid_write(inode, le32_to_cpu(v));
	}

	/* Load mode to lxmod EA */
	err = ntfs_get_ea(inode, "$LXMOD", sizeof("$LXMOD") - 1, &v, sizeof(v));
	if (err == sizeof(v)) {
		inode->i_mode = le32_to_cpu(v);
	} else {
		/* Everyone gets all permissions. */
		inode->i_mode |= 0777;
	}

	/* Load mode to lxdev EA */
	err = ntfs_get_ea(inode, "$LXDEV", sizeof("$LXDEV") - 1, &v, sizeof(v));
	if (err == sizeof(v))
		*rdevp = le32_to_cpu(v);
	err = 0;

	return err;
}

int ntfs_ea_set_wsl_inode(struct inode *inode, dev_t rdev, __le16 *ea_size,
		unsigned int flags)
{
	__le32 v;
	int err;

	if (flags & NTFS_EA_UID) {
		/* Store uid to lxuid EA */
		v = cpu_to_le32(i_uid_read(inode));
		err = ntfs_set_ea(inode, "$LXUID", sizeof("$LXUID") - 1, &v,
				sizeof(v), 0, ea_size);
		if (err)
			return err;
	}

	if (flags & NTFS_EA_GID) {
		/* Store gid to lxgid EA */
		v = cpu_to_le32(i_gid_read(inode));
		err = ntfs_set_ea(inode, "$LXGID", sizeof("$LXGID") - 1, &v,
				sizeof(v), 0, ea_size);
		if (err)
			return err;
	}

	if (flags & NTFS_EA_MODE) {
		/* Store mode to lxmod EA */
		v = cpu_to_le32(inode->i_mode);
		err = ntfs_set_ea(inode, "$LXMOD", sizeof("$LXMOD") - 1, &v,
				sizeof(v), 0, ea_size);
		if (err)
			return err;
	}

	if (rdev) {
		v = cpu_to_le32(rdev);
		err = ntfs_set_ea(inode, "$LXDEV", sizeof("$LXDEV") - 1, &v, sizeof(v),
				0, ea_size);
	}

	return err;
}

ssize_t ntfs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni = NTFS_I(inode);
	const struct ea_attr *p_ea;
	s64 offset, ea_buf_size, ea_info_size;
	s64 ea_size;
	u32 next;
	int err = 0;
	u32 ea_info_qsize;
	char *ea_buf = NULL;
	ssize_t ret = 0;
	struct ea_information *ea_info;

	if (!NInoHasEA(ni))
		return 0;

	mutex_lock(&NTFS_I(inode)->mrec_lock);
	ea_info = ntfs_attr_readall(ni, AT_EA_INFORMATION, NULL, 0,
			&ea_info_size);
	if (!ea_info || ea_info_size != sizeof(struct ea_information))
		goto out;

	ea_info_qsize = le32_to_cpu(ea_info->ea_query_length);

	ea_buf = ntfs_attr_readall(ni, AT_EA, NULL, 0, &ea_buf_size);
	if (!ea_buf)
		goto out;

	if (ea_info_qsize > ea_buf_size || ea_info_qsize == 0)
		goto out;

	if (ea_info_qsize < sizeof(struct ea_attr)) {
		err = -EIO;
		goto out;
	}

	offset = 0;
	do {
		p_ea = (const struct ea_attr *)&ea_buf[offset];
		next = le32_to_cpu(p_ea->next_entry_offset);
		ea_size = next ? next : (ea_info_qsize - offset);

		if (ea_size < sizeof(struct ea_attr) ||
		    offset + ea_size > ea_info_qsize) {
			err = -EIO;
			goto out;
		}

		if ((int)p_ea->ea_name_length + 1 >
			ea_size - offsetof(struct ea_attr, ea_name)) {
			err = -EIO;
			goto out;
		}

		if (buffer) {
			if (ret + p_ea->ea_name_length + 1 > size) {
				err = -ERANGE;
				goto out;
			}

			memcpy(buffer + ret, p_ea->ea_name, p_ea->ea_name_length);
			buffer[ret + p_ea->ea_name_length] = 0;
		}

		ret += p_ea->ea_name_length + 1;
		offset += ea_size;
	} while (next > 0 && offset < ea_info_qsize);

out:
	mutex_unlock(&NTFS_I(inode)->mrec_lock);
	kvfree(ea_info);
	kvfree(ea_buf);

	return err ? err : ret;
}

// clang-format off
#define SYSTEM_DOS_ATTRIB     "system.dos_attrib"
#define SYSTEM_NTFS_ATTRIB    "system.ntfs_attrib"
#define SYSTEM_NTFS_ATTRIB_BE "system.ntfs_attrib_be"
// clang-format on

static int ntfs_getxattr(const struct xattr_handler *handler,
		struct dentry *unused, struct inode *inode, const char *name,
		void *buffer, size_t size)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	int err;

	if (NVolShutdown(ni->vol))
		return -EIO;

	if (!strcmp(name, SYSTEM_DOS_ATTRIB)) {
		if (!buffer) {
			err = sizeof(u8);
		} else if (size < sizeof(u8)) {
			err = -ENODATA;
		} else {
			err = sizeof(u8);
			*(u8 *)buffer = (u8)(le32_to_cpu(ni->flags) & 0x3F);
		}
		goto out;
	}

	if (!strcmp(name, SYSTEM_NTFS_ATTRIB) ||
	    !strcmp(name, SYSTEM_NTFS_ATTRIB_BE)) {
		if (!buffer) {
			err = sizeof(u32);
		} else if (size < sizeof(u32)) {
			err = -ENODATA;
		} else {
			err = sizeof(u32);
			*(u32 *)buffer = le32_to_cpu(ni->flags);
			if (!strcmp(name, SYSTEM_NTFS_ATTRIB_BE))
				*(__be32 *)buffer = cpu_to_be32(*(u32 *)buffer);
		}
		goto out;
	}

	mutex_lock(&ni->mrec_lock);
	err = ntfs_get_ea(inode, name, strlen(name), buffer, size);
	mutex_unlock(&ni->mrec_lock);

out:
	return err;
}

static int ntfs_new_attr_flags(struct ntfs_inode *ni, __le32 fattr)
{
	struct ntfs_attr_search_ctx *ctx;
	struct mft_record *m;
	struct attr_record *a;
	__le16 new_aflags;
	int mp_size, mp_ofs, name_ofs, arec_size, err;

	m = map_mft_record(ni);
	if (IS_ERR(m))
		return PTR_ERR(m);

	ctx = ntfs_attr_get_search_ctx(ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto err_out;
	}

	err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (err) {
		err = -EINVAL;
		goto err_out;
	}

	a = ctx->attr;
	new_aflags = ctx->attr->flags;

	if (fattr & FILE_ATTR_SPARSE_FILE)
		new_aflags |= ATTR_IS_SPARSE;
	else
		new_aflags &= ~ATTR_IS_SPARSE;

	if (fattr & FILE_ATTR_COMPRESSED)
		new_aflags |= ATTR_IS_COMPRESSED;
	else
		new_aflags &= ~ATTR_IS_COMPRESSED;

	if (new_aflags == a->flags)
		return 0;

	if ((new_aflags & (ATTR_IS_SPARSE | ATTR_IS_COMPRESSED)) ==
			  (ATTR_IS_SPARSE | ATTR_IS_COMPRESSED)) {
		pr_err("file can't be sparsed and compressed\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}

	if (!a->non_resident)
		goto out;

	if (a->data.non_resident.data_size) {
		pr_err("Can't change sparsed/compressed for non-empty file\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}

	if (new_aflags & (ATTR_IS_SPARSE | ATTR_IS_COMPRESSED))
		name_ofs = (offsetof(struct attr_record,
				     data.non_resident.compressed_size) +
					sizeof(a->data.non_resident.compressed_size) + 7) & ~7;
	else
		name_ofs = (offsetof(struct attr_record,
				     data.non_resident.compressed_size) + 7) & ~7;

	mp_size = ntfs_get_size_for_mapping_pairs(ni->vol, ni->runlist.rl, 0, -1, -1);
	if (unlikely(mp_size < 0)) {
		err = mp_size;
		ntfs_debug("Failed to get size for mapping pairs array, error code %i.\n", err);
		goto err_out;
	}

	mp_ofs = (name_ofs + a->name_length * sizeof(__le16) + 7) & ~7;
	arec_size = (mp_ofs + mp_size + 7) & ~7;

	err = ntfs_attr_record_resize(m, a, arec_size);
	if (unlikely(err))
		goto err_out;

	if (new_aflags & (ATTR_IS_SPARSE | ATTR_IS_COMPRESSED)) {
		a->data.non_resident.compression_unit = 0;
		if (new_aflags & ATTR_IS_COMPRESSED || ni->vol->major_ver < 3)
			a->data.non_resident.compression_unit = 4;
		a->data.non_resident.compressed_size = 0;
		ni->itype.compressed.size = 0;
		if (a->data.non_resident.compression_unit) {
			ni->itype.compressed.block_size = 1U <<
				(a->data.non_resident.compression_unit +
				 ni->vol->cluster_size_bits);
			ni->itype.compressed.block_size_bits =
					ffs(ni->itype.compressed.block_size) -
					1;
			ni->itype.compressed.block_clusters = 1U <<
					a->data.non_resident.compression_unit;
		} else {
			ni->itype.compressed.block_size = 0;
			ni->itype.compressed.block_size_bits = 0;
			ni->itype.compressed.block_clusters = 0;
		}

		if (new_aflags & ATTR_IS_SPARSE) {
			NInoSetSparse(ni);
			ni->flags |= FILE_ATTR_SPARSE_FILE;
		}

		if (new_aflags & ATTR_IS_COMPRESSED) {
			NInoSetCompressed(ni);
			ni->flags |= FILE_ATTR_COMPRESSED;
		}
	} else {
		ni->flags &= ~(FILE_ATTR_SPARSE_FILE | FILE_ATTR_COMPRESSED);
		a->data.non_resident.compression_unit = 0;
		NInoClearSparse(ni);
		NInoClearCompressed(ni);
	}

	a->name_offset = cpu_to_le16(name_ofs);
	a->data.non_resident.mapping_pairs_offset = cpu_to_le16(mp_ofs);

out:
	a->flags = new_aflags;
	mark_mft_record_dirty(ctx->ntfs_ino);
err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(ni);
	return err;
}

static int ntfs_setxattr(const struct xattr_handler *handler,
		struct mnt_idmap *idmap, struct dentry *unused,
		struct inode *inode, const char *name, const void *value,
		size_t size, int flags)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	int err;
	__le32 fattr;

	if (NVolShutdown(ni->vol))
		return -EIO;

	if (!strcmp(name, SYSTEM_DOS_ATTRIB)) {
		if (sizeof(u8) != size) {
			err = -EINVAL;
			goto out;
		}
		fattr = cpu_to_le32(*(u8 *)value);
		goto set_fattr;
	}

	if (!strcmp(name, SYSTEM_NTFS_ATTRIB) ||
	    !strcmp(name, SYSTEM_NTFS_ATTRIB_BE)) {
		if (size != sizeof(u32)) {
			err = -EINVAL;
			goto out;
		}
		if (!strcmp(name, SYSTEM_NTFS_ATTRIB_BE))
			fattr = cpu_to_le32(be32_to_cpu(*(__be32 *)value));
		else
			fattr = cpu_to_le32(*(u32 *)value);

		if (S_ISREG(inode->i_mode)) {
			mutex_lock(&ni->mrec_lock);
			err = ntfs_new_attr_flags(ni, fattr);
			mutex_unlock(&ni->mrec_lock);
			if (err)
				goto out;
		}

set_fattr:
		if (S_ISDIR(inode->i_mode))
			fattr |= FILE_ATTR_DIRECTORY;
		else
			fattr &= ~FILE_ATTR_DIRECTORY;

		if (ni->flags != fattr) {
			ni->flags = fattr;
			if (fattr & FILE_ATTR_READONLY)
				inode->i_mode &= ~0222;
			else
				inode->i_mode |= 0222;
			NInoSetFileNameDirty(ni);
			mark_inode_dirty(inode);
		}
		err = 0;
		goto out;
	}

	mutex_lock(&ni->mrec_lock);
	err = ntfs_set_ea(inode, name, strlen(name), value, size, flags, NULL);
	mutex_unlock(&ni->mrec_lock);

out:
	inode_set_ctime_current(inode);
	mark_inode_dirty(inode);
	return err;
}

static bool ntfs_xattr_user_list(struct dentry *dentry)
{
	return true;
}

// clang-format off
static const struct xattr_handler ntfs_other_xattr_handler = {
	.prefix	= "",
	.get	= ntfs_getxattr,
	.set	= ntfs_setxattr,
	.list	= ntfs_xattr_user_list,
};

const struct xattr_handler * const ntfs_xattr_handlers[] = {
	&ntfs_other_xattr_handler,
	NULL,
};
// clang-format on

#ifdef CONFIG_NTFS_FS_POSIX_ACL
struct posix_acl *ntfs_get_acl(struct mnt_idmap *idmap, struct dentry *dentry,
			       int type)
{
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni = NTFS_I(inode);
	const char *name;
	size_t name_len;
	struct posix_acl *acl;
	int err;
	void *buf;

	buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	/* Possible values of 'type' was already checked above. */
	if (type == ACL_TYPE_ACCESS) {
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		name_len = sizeof(XATTR_NAME_POSIX_ACL_ACCESS) - 1;
	} else {
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		name_len = sizeof(XATTR_NAME_POSIX_ACL_DEFAULT) - 1;
	}

	mutex_lock(&ni->mrec_lock);
	err = ntfs_get_ea(inode, name, name_len, buf, PATH_MAX);
	mutex_unlock(&ni->mrec_lock);

	/* Translate extended attribute to acl. */
	if (err >= 0)
		acl = posix_acl_from_xattr(&init_user_ns, buf, err);
	else if (err == -ENODATA)
		acl = NULL;
	else
		acl = ERR_PTR(err);

	if (!IS_ERR(acl))
		set_cached_acl(inode, type, acl);

	kfree(buf);

	return acl;
}

static noinline int ntfs_set_acl_ex(struct mnt_idmap *idmap,
				    struct inode *inode, struct posix_acl *acl,
				    int type, bool init_acl)
{
	const char *name;
	size_t size, name_len;
	void *value;
	int err;
	int flags;
	umode_t mode;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	mode = inode->i_mode;
	switch (type) {
	case ACL_TYPE_ACCESS:
		/* Do not change i_mode if we are in init_acl */
		if (acl && !init_acl) {
			err = posix_acl_update_mode(idmap, inode, &mode, &acl);
			if (err)
				return err;
		}
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		name_len = sizeof(XATTR_NAME_POSIX_ACL_ACCESS) - 1;
		break;

	case ACL_TYPE_DEFAULT:
		if (!S_ISDIR(inode->i_mode))
			return acl ? -EACCES : 0;
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		name_len = sizeof(XATTR_NAME_POSIX_ACL_DEFAULT) - 1;
		break;

	default:
		return -EINVAL;
	}

	if (!acl) {
		/* Remove xattr if it can be presented via mode. */
		size = 0;
		value = NULL;
		flags = XATTR_REPLACE;
	} else {
		value = posix_acl_to_xattr(&init_user_ns, acl, &size, GFP_NOFS);
		if (!value)
			return -ENOMEM;
		flags = 0;
	}

	mutex_lock(&NTFS_I(inode)->mrec_lock);
	err = ntfs_set_ea(inode, name, name_len, value, size, flags, NULL);
	mutex_unlock(&NTFS_I(inode)->mrec_lock);
	if (err == -ENODATA && !size)
		err = 0; /* Removing non existed xattr. */
	if (!err) {
		__le16 ea_size = 0;
		umode_t old_mode = inode->i_mode;

		inode->i_mode = mode;
		mutex_lock(&NTFS_I(inode)->mrec_lock);
		err = ntfs_ea_set_wsl_inode(inode, 0, &ea_size, NTFS_EA_MODE);
		if (err) {
			ntfs_set_ea(inode, name, name_len, NULL, 0,
				    XATTR_REPLACE, NULL);
			mutex_unlock(&NTFS_I(inode)->mrec_lock);
			inode->i_mode = old_mode;
			goto out;
		}
		mutex_unlock(&NTFS_I(inode)->mrec_lock);

		set_cached_acl(inode, type, acl);
		inode_set_ctime_current(inode);
		mark_inode_dirty(inode);
	}

out:
	kfree(value);

	return err;
}

int ntfs_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct posix_acl *acl, int type)
{
	return ntfs_set_acl_ex(idmap, d_inode(dentry), acl, type, false);
}

int ntfs_init_acl(struct mnt_idmap *idmap, struct inode *inode,
		  struct inode *dir)
{
	struct posix_acl *default_acl, *acl;
	int err;

	err = posix_acl_create(dir, &inode->i_mode, &default_acl, &acl);
	if (err)
		return err;

	if (default_acl) {
		err = ntfs_set_acl_ex(idmap, inode, default_acl,
				      ACL_TYPE_DEFAULT, true);
		posix_acl_release(default_acl);
	} else {
		inode->i_default_acl = NULL;
	}

	if (acl) {
		if (!err)
			err = ntfs_set_acl_ex(idmap, inode, acl,
					      ACL_TYPE_ACCESS, true);
		posix_acl_release(acl);
	} else {
		inode->i_acl = NULL;
	}

	return err;
}
#endif
