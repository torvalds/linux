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
		err = written < 0 ? written : -EIO;
	else {
		struct ntfs_inode *ea_ni = NTFS_I(ea_vi);

		if (need_truncate && ea_ni->data_size > ea_off + ea_size)
			err = ntfs_attr_truncate(ea_ni, ea_off + ea_size);
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

	if (!ea_buf_size)
		return -ENOENT;

	offset = 0;
	do {
		if (ea_buf_size - offset < sizeof(struct ea_attr))
			return -EUCLEAN;

		p_ea = (const struct ea_attr *)&ea_buf[offset];
		next = le32_to_cpu(p_ea->next_entry_offset);
		p_ea_size = next ? next : (ea_buf_size - offset);

		if (p_ea_size < sizeof(struct ea_attr) ||
		    offset + p_ea_size > ea_buf_size)
			return -EUCLEAN;

		if ((s64)p_ea->ea_name_length + 1 >
		    p_ea_size - offsetof(struct ea_attr, ea_name))
			return -EUCLEAN;

		actual_size = ALIGN(struct_size(p_ea, ea_name, 1 + p_ea->ea_name_length +
					le16_to_cpu(p_ea->ea_value_length)), 4);
		if (actual_size > p_ea_size ||
		    p_ea->ea_name[p_ea->ea_name_length])
			return -EUCLEAN;

		if (name && p_ea->ea_name_length == name_len &&
		    !memcmp(p_ea->ea_name, name, name_len)) {
			*ea_offset = offset;
			*ea_size = p_ea_size;

			if (ea_buf_size < *ea_offset + *ea_size)
				return -EUCLEAN;

			return 0;
		}
		offset += next;
	} while (next > 0 && offset < ea_buf_size);

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
	if (IS_ERR(p_ea_info))
		return PTR_ERR(p_ea_info);
	if (ea_info_size != sizeof(struct ea_information)) {
		kvfree(p_ea_info);
		return -EUCLEAN;
	}

	ea_info_qlen = le32_to_cpu(p_ea_info->ea_query_length);
	kvfree(p_ea_info);

	ea_buf = ntfs_attr_readall(ni, AT_EA, NULL, 0, &all_ea_size);
	if (IS_ERR(ea_buf))
		return PTR_ERR(ea_buf);

	if (ea_info_qlen > all_ea_size) {
		err = -EUCLEAN;
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

	if (err == -ENOENT)
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

/* Unlike ntfs_attr_exist(), preserve lookup errors. */
static int ntfs_ea_attr_lookup(struct ntfs_inode *ni, __le32 type)
{
	struct ntfs_attr_search_ctx *ctx;
	struct mft_record *mrec;
	int err;

	mrec = map_mft_record(ni);
	if (IS_ERR(mrec))
		return PTR_ERR(mrec);
	ctx = ntfs_attr_get_search_ctx(ni, mrec);
	if (!ctx) {
		err = -ENOMEM;
		goto out;
	}
	err = ntfs_attr_lookup(type, AT_UNNAMED, 0, CASE_SENSITIVE,
			       0, NULL, 0, ctx);
	ntfs_attr_put_search_ctx(ctx);
out:
	unmap_mft_record(ni);
	return err;
}

/* A NULL value means that the attribute should not exist. */
static int ntfs_ea_update_attr(struct ntfs_inode *ni, __le32 type,
			       char *value, s64 size)
{
	int err = ntfs_ea_attr_lookup(ni, type);

	if (err && err != -ENOENT)
		return err;
	if (!value)
		return err == -ENOENT ? 0 :
			ntfs_attr_remove(ni, type, AT_UNNAMED, 0, NULL);
	if (err == -ENOENT)
		return ntfs_attr_add(ni, type, AT_UNNAMED, 0, value, size);
	return ntfs_write_ea(ni, type, value, 0, size, true);
}

/*
 * Prepare the new EA list before changing either NTFS attribute. Keep the
 * original values until both updates succeed, so a failed update can be
 * rolled back. This does not provide atomicity across a crash or power loss.
 */
static int ntfs_set_ea(struct inode *inode, const char *name, size_t name_len,
		const void *value, size_t val_size, int flags,
		__le16 *packed_ea_size)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	struct ea_information *old_info = NULL, info = {};
	char *old_ea = NULL, *ea = NULL;
	struct ea_attr *entry;
	s64 old_size = 0, info_size, offset = 0, entry_size = 0;
	u32 size = 0, new_size, next;
	size_t add_size;
	int err, info_err, ea_err, packed = 0, needed = 0;
	bool exists, remove = !value;

	if (name_len > U8_MAX)
		return -ENAMETOOLONG;
	if (val_size > U16_MAX)
		return -E2BIG;

	err = ntfs_ea_attr_lookup(ni, AT_EA_INFORMATION);
	if (err && err != -ENOENT)
		return err;
	exists = !err;
	err = ntfs_ea_attr_lookup(ni, AT_EA);
	if (err && err != -ENOENT)
		return err;
	/* Do not silently discard an orphaned attribute. */
	if (exists != !err)
		return -EUCLEAN;

	if (exists) {
		old_info = ntfs_attr_readall(ni, AT_EA_INFORMATION, NULL, 0,
					     &info_size);
		if (IS_ERR(old_info))
			return PTR_ERR(old_info);
		if (info_size != sizeof(*old_info)) {
			err = -EUCLEAN;
			goto out;
		}
		old_ea = ntfs_attr_readall(ni, AT_EA, NULL, 0, &old_size);
		if (IS_ERR(old_ea)) {
			err = PTR_ERR(old_ea);
			old_ea = NULL;
			goto out;
		}
		size = le32_to_cpu(old_info->ea_query_length);
		if (size > old_size) {
			err = -EUCLEAN;
			goto out;
		}
	}

	/* Validate even the entries following the one being replaced. */
	err = ntfs_ea_lookup(old_ea, size, NULL, 0, &offset, &entry_size);
	if (err != -ENOENT)
		goto out;
	err = ntfs_ea_lookup(old_ea, size, name, name_len, &offset, &entry_size);
	if (err && err != -ENOENT)
		goto out;
	if (!err) {
		if (flags & XATTR_CREATE) {
			err = -EEXIST;
			goto out;
		}
		entry = (struct ea_attr *)(old_ea + offset);
		if (!remove && le16_to_cpu(entry->ea_value_length) == val_size &&
		    !memcmp(entry->ea_name + name_len + 1, value, val_size)) {
			info = *old_info;
			goto done;
		}
	} else {
		if (remove || (flags & XATTR_REPLACE)) {
			err = -ENODATA;
			goto out;
		}
		offset = size;
		entry_size = 0;
	}

	add_size = remove ? 0 : ALIGN(sizeof(*entry) + name_len + 1 + val_size, 4);
	new_size = size - entry_size + add_size;
	if (new_size && ntfs_attr_size_bounds_check(ni->vol, AT_EA, new_size)) {
		err = -EFBIG;
		goto out;
	}
	if (new_size) {
		ea = kvzalloc(new_size, GFP_NOFS);
		if (!ea) {
			err = -ENOMEM;
			goto out;
		}
		if (offset)
			memcpy(ea, old_ea, offset);
		if (size > offset + entry_size)
			memcpy(ea + offset, old_ea + offset + entry_size,
			       size - offset - entry_size);
	}
	size -= entry_size;

	/* Link a zero-terminated old tail before appending the new entry. */
	for (offset = 0; offset < size; offset += next) {
		entry = (struct ea_attr *)(ea + offset);
		next = le32_to_cpu(entry->next_entry_offset);
		if (!next) {
			next = size - offset;
			entry->next_entry_offset = cpu_to_le32(next);
		}
		packed += ea_packed_size(entry);
		if (entry->flags & NEED_EA)
			needed++;
	}
	if (!remove) {
		entry = (struct ea_attr *)(ea + size);
		entry->next_entry_offset = cpu_to_le32(add_size);
		entry->ea_name_length = name_len;
		entry->ea_value_length = cpu_to_le16(val_size);
		memcpy(entry->ea_name, name, name_len);
		memcpy(entry->ea_name + name_len + 1, value, val_size);
		packed += ea_packed_size(entry);
	}
	if (packed > U16_MAX) {
		err = -EFBIG;
		goto out;
	}
	info.ea_length = cpu_to_le16(packed);
	info.need_ea_count = cpu_to_le16(needed);
	info.ea_query_length = cpu_to_le32(new_size);

	/* All preparation is complete; errors from here require rollback. */
	err = ntfs_ea_update_attr(ni, AT_EA, ea, new_size);
	if (!err)
		err = ntfs_ea_update_attr(ni, AT_EA_INFORMATION,
					  new_size ? (char *)&info : NULL,
					  sizeof(info));
	if (err) {
		/* Try both restorations even if the first one fails. */
		ea_err = ntfs_ea_update_attr(ni, AT_EA, old_ea, old_size);
		info_err = ntfs_ea_update_attr(ni, AT_EA_INFORMATION,
					       (char *)old_info, sizeof(info));
		if (ea_err || info_err) {
			NVolSetErrors(ni->vol);
			NVolSetShutdown(ni->vol);
			ntfs_error(inode->i_sb,
				   "Failed to restore EAs of inode 0x%llx (%d, %d); shutting down.",
				   ni->mft_no, ea_err, info_err);
			err = -EIO;
		}
		goto out;
	}
	size = new_size;
	mark_mft_record_dirty(ni);
done:
	if (packed_ea_size)
		*packed_ea_size = info.ea_length;
	if (size)
		NInoSetHasEA(ni);
	else
		NInoClearHasEA(ni);
out:
	kvfree(ea);
	kvfree(old_ea);
	kvfree(old_info);
	return err;
}
/*
 * Check for the presence of an EA "$LXDEV" (used by WSL)
 * and return its value as a device address
 */
int ntfs_ea_get_wsl_inode(struct inode *inode, dev_t *rdevp, unsigned int flags,
			  bool *has_lxmod)
{
	int err;
	__le32 v;

	*has_lxmod = false;

	if (!(flags & NTFS_VOL_UID)) {
		/* Load uid to lxuid EA */
		err = ntfs_get_ea(inode, "$LXUID", sizeof("$LXUID") - 1, &v,
				sizeof(v));
		if (err == sizeof(v))
			i_uid_write(inode, le32_to_cpu(v));
	}

	if (!(flags & NTFS_VOL_GID)) {
		/* Load gid to lxgid EA */
		err = ntfs_get_ea(inode, "$LXGID", sizeof("$LXGID") - 1, &v,
				sizeof(v));
		if (err == sizeof(v))
			i_gid_write(inode, le32_to_cpu(v));
	}

	/* Load mode to lxmod EA */
	err = ntfs_get_ea(inode, "$LXMOD", sizeof("$LXMOD") - 1, &v, sizeof(v));
	if (err == sizeof(v)) {
		inode->i_mode = le32_to_cpu(v);
		*has_lxmod = true;
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
	int err = 0;

	if (ea_size)
		*ea_size = 0;

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

int ntfs_ea_get_linuxflags(struct inode *inode)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	__le32 value;
	int err;

	err = ntfs_get_ea(inode, "$LINUXFLAGS", sizeof("$LINUXFLAGS") - 1,
			  &value, sizeof(value));
	if (err == -ENODATA)
		return 0;
	if (err < 0)
		return err == -ERANGE ? -EUCLEAN : err;
	if (err != sizeof(value))
		return -EUCLEAN;

	ni->linuxflags = le32_to_cpu(value);
	if (ni->linuxflags & NTFS_LINUXFLAGS_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	if (ni->linuxflags & NTFS_LINUXFLAGS_APPEND)
		inode->i_flags |= S_APPEND;
	return 0;
}

int ntfs_ea_set_linuxflags(struct inode *inode, u32 linuxflags)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	__le32 value;
	int err;

	if (linuxflags) {
		value = cpu_to_le32(linuxflags);
		err = ntfs_set_ea(inode, "$LINUXFLAGS", sizeof("$LINUXFLAGS") - 1,
				  &value, sizeof(value), 0, NULL);
	} else if (NInoHasEA(ni)) {
		err = ntfs_set_ea(inode, "$LINUXFLAGS", sizeof("$LINUXFLAGS") - 1,
				  NULL, 0, XATTR_REPLACE, NULL);
		if (err == -ENODATA)
			err = 0;
	} else {
		err = 0;
	}

	if (!err)
		ni->linuxflags = linuxflags;
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
	if (IS_ERR(ea_info)) {
		err = PTR_ERR(ea_info);
		ea_info = NULL;
		goto out;
	}
	if (ea_info_size != sizeof(struct ea_information)) {
		err = -EUCLEAN;
		goto out;
	}

	ea_info_qsize = le32_to_cpu(ea_info->ea_query_length);

	ea_buf = ntfs_attr_readall(ni, AT_EA, NULL, 0, &ea_buf_size);
	if (IS_ERR(ea_buf)) {
		err = PTR_ERR(ea_buf);
		ea_buf = NULL;
		goto out;
	}

	if (ea_info_qsize > ea_buf_size) {
		err = -EUCLEAN;
		goto out;
	}

	/* Validate the entire chain, including size-only queries. */
	err = ntfs_ea_lookup(ea_buf, ea_info_qsize, NULL, 0, &offset,
			     &ea_size);
	if (err != -ENOENT)
		goto out;
	err = 0;
	if (!ea_info_qsize)
		goto out;

	offset = 0;
	do {
		p_ea = (const struct ea_attr *)&ea_buf[offset];
		next = le32_to_cpu(p_ea->next_entry_offset);
		ea_size = next ? next : (ea_info_qsize - offset);

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

	/*
	 * Stream permissions and privileges belong to the base inode. This
	 * also lets VFS killpriv helpers find the base security attributes.
	 */
	if (ntfs_inode_is_named_stream(ni)) {
		ni = ni->ext.base_ntfs_ino;
		inode = VFS_I(ni);
	}
	if (NVolShutdown(ni->vol))
		return -EIO;

	if (!strcmp(name, SYSTEM_DOS_ATTRIB)) {
		if (!buffer) {
			err = sizeof(u8);
		} else if (size < sizeof(u8)) {
			err = -ERANGE;
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
			err = -ERANGE;
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
	u16 old_name_ofs, old_mp_ofs;
	int mp_size, mp_ofs, name_ofs, old_arec_size, arec_size, err;

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

	if (new_aflags == a->flags) {
		err = 0;
		goto err_out;
	}

	if ((new_aflags & (ATTR_IS_SPARSE | ATTR_IS_COMPRESSED)) ==
			  (ATTR_IS_SPARSE | ATTR_IS_COMPRESSED)) {
		pr_err("file can't be sparsed and compressed\n");
		err = -EOPNOTSUPP;
		goto err_out;
	}

	if (!a->non_resident) {
		if (!(new_aflags & (ATTR_IS_SPARSE | ATTR_IS_COMPRESSED))) {
			err = 0;
			goto err_out;
		}

		if (le32_to_cpu(a->data.resident.value_length)) {
			pr_err("Can't change sparse/compressed for non-empty file");
			err = -EOPNOTSUPP;
			goto err_out;
		}

		err = ntfs_attr_make_non_resident(ni, 0);
		if (err)
			goto err_out;

		ntfs_attr_reinit_search_ctx(ctx);
		err = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
				CASE_SENSITIVE,
				0, NULL, 0, ctx);
		if (err) {
			err = -EINVAL;
			goto err_out;
		}
		a = ctx->attr;
	} else {
		if (a->data.non_resident.data_size) {
			pr_err("Can't change sparsed/compressed for non-empty file");
			err = -EOPNOTSUPP;
			goto err_out;
		}
	}

	old_name_ofs = le16_to_cpu(a->name_offset);
	old_mp_ofs = le16_to_cpu(a->data.non_resident.mapping_pairs_offset);

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
	old_arec_size = le32_to_cpu(a->length);

	/*
	 * Move payloads before shrinking the record. Otherwise resizing moves
	 * the following attribute over the old payload before it can be copied.
	 *
	 * When offsets increase, move mapping_pairs first to avoid name
	 * overwriting the start of mapping_pairs.
	 */
	if (arec_size < old_arec_size) {
		if (name_ofs > old_name_ofs) {
			/* Payload offsets increased: move mapping pairs first. */
			if (mp_ofs != old_mp_ofs)
				memmove((u8 *)a + mp_ofs,
						(u8 *)a + old_mp_ofs,
						mp_size);
			if (a->name_length && name_ofs != old_name_ofs)
				memmove((u8 *)a + name_ofs,
						(u8 *)a + old_name_ofs,
						a->name_length *
							sizeof(__le16));
		} else {
			/* Payload offsets decreased or unchanged: move name first. */
			if (a->name_length && name_ofs != old_name_ofs)
				memmove((u8 *)a + name_ofs,
						(u8 *)a + old_name_ofs,
						a->name_length *
							sizeof(__le16));
			if (mp_ofs != old_mp_ofs)
				memmove((u8 *)a + mp_ofs,
						(u8 *)a + old_mp_ofs,
						mp_size);
		}
	}

	err = ntfs_attr_record_resize(ctx->mrec, a, arec_size);
	if (unlikely(err))
		goto err_out;

	/*
	 * When compressed/sparse state changes, the non-resident header grows or
	 * shrinks by the compressed_size field. Update the in-record payload layout
	 * to match the new offsets before exposing the new mapping_pairs_offset.
	 */
	if (arec_size > old_arec_size) {
		if (mp_ofs != old_mp_ofs)
			memmove((u8 *)a + mp_ofs, (u8 *)a + old_mp_ofs, mp_size);
		if (a->name_length)
			memmove((u8 *)a + name_ofs, (u8 *)a + old_name_ofs,
				a->name_length * sizeof(__le16));
	}

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
	} else {
		a->data.non_resident.compression_unit = 0;
	}

	a->name_offset = cpu_to_le16(name_ofs);
	a->data.non_resident.mapping_pairs_offset = cpu_to_le16(mp_ofs);

	a->flags = new_aflags;

	if (new_aflags & ATTR_IS_SPARSE) {
		NInoSetSparse(ni);
		ni->flags |= FILE_ATTR_SPARSE_FILE;
	} else {
		NInoClearSparse(ni);
		ni->flags &= ~FILE_ATTR_SPARSE_FILE;
	}

	if (new_aflags & ATTR_IS_COMPRESSED) {
		NInoSetCompressed(ni);
		ni->flags |= FILE_ATTR_COMPRESSED;
	} else {
		NInoClearCompressed(ni);
		ni->flags &= ~FILE_ATTR_COMPRESSED;
	}

	mark_mft_record_dirty(ctx->ntfs_ino);
err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(ni);
	return err;
}

static bool ntfs_is_reserved_lxattr(const char *name)
{
	return !strcmp(name, "$LXUID") || !strcmp(name, "$LXGID") ||
	       !strcmp(name, "$LXMOD") || !strcmp(name, "$LXDEV");
}

static int ntfs_validate_fattr(struct ntfs_inode *ni, __le32 fattr)
{
	const __le32 wof_flags = FILE_ATTR_SPARSE_FILE |
				 FILE_ATTR_REPARSE_POINT;

	if (NInoWofCompressed(ni) && ((ni->flags ^ fattr) & wof_flags))
		return -EPERM;

	return 0;
}

static int ntfs_setxattr(const struct xattr_handler *handler,
		const struct mnt_idmap *idmap, struct dentry *unused,
		struct inode *inode, const char *name, const void *value,
		size_t size, int flags)
{
	struct ntfs_inode *ni = NTFS_I(inode);
	int err;
	__le32 fattr;

	if (ntfs_inode_is_named_stream(ni))
		return -EOPNOTSUPP;
	if (NVolShutdown(ni->vol))
		return -EIO;

	/* Only fileattr_set may change the flags and their cached state. */
	if (!strcmp(name, "$LINUXFLAGS"))
		return -EPERM;

	if (ntfs_is_reserved_lxattr(name) && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!strcmp(name, SYSTEM_DOS_ATTRIB)) {
		if (sizeof(u8) != size) {
			err = -EINVAL;
			goto out;
		}
		fattr = cpu_to_le32((le32_to_cpu(ni->flags) & ~0xffU) |
				    *(u8 *)value);
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

		err = ntfs_validate_fattr(ni, fattr);
		if (err)
			goto out;

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

		err = ntfs_validate_fattr(ni, fattr);
		if (err)
			goto out;

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
	if (!err) {
		inode_set_ctime_current(inode);
		mark_inode_dirty(inode);
	}
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
struct posix_acl *ntfs_get_acl(const struct mnt_idmap *idmap, struct dentry *dentry,
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

static noinline int ntfs_set_acl_ex(const struct mnt_idmap *idmap,
				    struct inode *inode, struct posix_acl *acl,
				    int type, bool init_acl)
{
	const char *name;
	size_t size, name_len;
	void *value, *old_value = NULL;
	int err, old_len = -ENODATA;
	bool acl_committed = false;
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
	if (!init_acl) {
		/* Save the old xattr for rollback. */
		old_len = ntfs_get_ea(inode, name, name_len, NULL, 0);
		if (old_len >= 0) {
			/* kmalloc(0) returns the non-NULL ZERO_SIZE_PTR. */
			old_value = kmalloc(old_len, GFP_NOFS);
			if (!old_value) {
				err = -ENOMEM;
				goto out_unlock;
			}
			err = ntfs_get_ea(inode, name, name_len,
					  old_value, old_len);
			if (err != old_len) {
				kfree(old_value);
				old_value = NULL;
				if (err >= 0)
					err = -EIO;
				goto out_unlock;
			}
		} else if (old_len != -ENODATA) {
			err = old_len;
			goto out_unlock;
		}
	}
	err = ntfs_set_ea(inode, name, name_len, value, size, flags, NULL);
	if (!err)
		acl_committed = true;
out_unlock:
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
			if (acl_committed) {
				int undo_err;

				if (old_len >= 0) {
					/*
					 * A removed or zero-length xattr must
					 * be recreated without XATTR_REPLACE.
					 */
					int undo_flags = acl && old_len ?
						XATTR_REPLACE : 0;

					undo_err = ntfs_set_ea(inode, name,
							name_len, old_value,
							old_len, undo_flags,
							NULL);
				} else {
					undo_err = ntfs_set_ea(inode, name,
							name_len, NULL, 0,
							XATTR_REPLACE, NULL);
				}
				if (undo_err) {
					/*
					 * Record a failed rollback so the next
					 * persistence point marks the volume
					 * dirty.  Run chkdsk.
					 */
					NVolSetErrors(NTFS_I(inode)->vol);
					ntfs_error(inode->i_sb,
						   "Failed to undo the ACL xattr: %d",
						   undo_err);
				}
			}
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
	kfree(old_value);
	kfree(value);

	return err;
}

int ntfs_set_acl(const struct mnt_idmap *idmap, struct dentry *dentry,
		 struct posix_acl *acl, int type)
{
	return ntfs_set_acl_ex(idmap, d_inode(dentry), acl, type, false);
}

int ntfs_init_acl(const struct mnt_idmap *idmap, struct inode *inode,
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
