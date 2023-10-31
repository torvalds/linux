// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/fs.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

// clang-format off
#define SYSTEM_DOS_ATTRIB     "system.dos_attrib"
#define SYSTEM_NTFS_ATTRIB    "system.ntfs_attrib"
#define SYSTEM_NTFS_ATTRIB_BE "system.ntfs_attrib_be"
#define SYSTEM_NTFS_SECURITY  "system.ntfs_security"
// clang-format on

static inline size_t unpacked_ea_size(const struct EA_FULL *ea)
{
	return ea->size ? le32_to_cpu(ea->size) :
			  ALIGN(struct_size(ea, name,
					    1 + ea->name_len +
						    le16_to_cpu(ea->elength)),
				4);
}

static inline size_t packed_ea_size(const struct EA_FULL *ea)
{
	return struct_size(ea, name,
			   1 + ea->name_len + le16_to_cpu(ea->elength)) -
	       offsetof(struct EA_FULL, flags);
}

/*
 * find_ea
 *
 * Assume there is at least one xattr in the list.
 */
static inline bool find_ea(const struct EA_FULL *ea_all, u32 bytes,
			   const char *name, u8 name_len, u32 *off, u32 *ea_sz)
{
	u32 ea_size;

	*off = 0;
	if (!ea_all)
		return false;

	for (; *off < bytes; *off += ea_size) {
		const struct EA_FULL *ea = Add2Ptr(ea_all, *off);
		ea_size = unpacked_ea_size(ea);
		if (ea->name_len == name_len &&
		    !memcmp(ea->name, name, name_len)) {
			if (ea_sz)
				*ea_sz = ea_size;
			return true;
		}
	}

	return false;
}

/*
 * ntfs_read_ea - Read all extended attributes.
 * @ea:		New allocated memory.
 * @info:	Pointer into resident data.
 */
static int ntfs_read_ea(struct ntfs_inode *ni, struct EA_FULL **ea,
			size_t add_bytes, const struct EA_INFO **info)
{
	int err = -EINVAL;
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	struct ATTR_LIST_ENTRY *le = NULL;
	struct ATTRIB *attr_info, *attr_ea;
	void *ea_p;
	u32 size, off, ea_size;

	static_assert(le32_to_cpu(ATTR_EA_INFO) < le32_to_cpu(ATTR_EA));

	*ea = NULL;
	*info = NULL;

	attr_info =
		ni_find_attr(ni, NULL, &le, ATTR_EA_INFO, NULL, 0, NULL, NULL);
	attr_ea =
		ni_find_attr(ni, attr_info, &le, ATTR_EA, NULL, 0, NULL, NULL);

	if (!attr_ea || !attr_info)
		return 0;

	*info = resident_data_ex(attr_info, sizeof(struct EA_INFO));
	if (!*info)
		goto out;

	/* Check Ea limit. */
	size = le32_to_cpu((*info)->size);
	if (size > sbi->ea_max_size) {
		err = -EFBIG;
		goto out;
	}

	if (attr_size(attr_ea) > sbi->ea_max_size) {
		err = -EFBIG;
		goto out;
	}

	if (!size) {
		/* EA info persists, but xattr is empty. Looks like EA problem. */
		goto out;
	}

	/* Allocate memory for packed Ea. */
	ea_p = kmalloc(size_add(size, add_bytes), GFP_NOFS);
	if (!ea_p)
		return -ENOMEM;

	if (attr_ea->non_res) {
		struct runs_tree run;

		run_init(&run);

		err = attr_load_runs_range(ni, ATTR_EA, NULL, 0, &run, 0, size);
		if (!err)
			err = ntfs_read_run_nb(sbi, &run, 0, ea_p, size, NULL);
		run_close(&run);

		if (err)
			goto out1;
	} else {
		void *p = resident_data_ex(attr_ea, size);

		if (!p)
			goto out1;
		memcpy(ea_p, p, size);
	}

	memset(Add2Ptr(ea_p, size), 0, add_bytes);

	err = -EINVAL;
	/* Check all attributes for consistency. */
	for (off = 0; off < size; off += ea_size) {
		const struct EA_FULL *ef = Add2Ptr(ea_p, off);
		u32 bytes = size - off;

		/* Check if we can use field ea->size. */
		if (bytes < sizeof(ef->size))
			goto out1;

		if (ef->size) {
			ea_size = le32_to_cpu(ef->size);
			if (ea_size > bytes)
				goto out1;
			continue;
		}

		/* Check if we can use fields ef->name_len and ef->elength. */
		if (bytes < offsetof(struct EA_FULL, name))
			goto out1;

		ea_size = ALIGN(struct_size(ef, name,
					    1 + ef->name_len +
						    le16_to_cpu(ef->elength)),
				4);
		if (ea_size > bytes)
			goto out1;
	}

	*ea = ea_p;
	return 0;

out1:
	kfree(ea_p);
out:
	ntfs_set_state(sbi, NTFS_DIRTY_DIRTY);
	return err;
}

/*
 * ntfs_list_ea
 *
 * Copy a list of xattrs names into the buffer
 * provided, or compute the buffer size required.
 *
 * Return:
 * * Number of bytes used / required on
 * * -ERRNO - on failure
 */
static ssize_t ntfs_list_ea(struct ntfs_inode *ni, char *buffer,
			    size_t bytes_per_buffer)
{
	const struct EA_INFO *info;
	struct EA_FULL *ea_all = NULL;
	const struct EA_FULL *ea;
	u32 off, size;
	int err;
	int ea_size;
	size_t ret;

	err = ntfs_read_ea(ni, &ea_all, 0, &info);
	if (err)
		return err;

	if (!info || !ea_all)
		return 0;

	size = le32_to_cpu(info->size);

	/* Enumerate all xattrs. */
	ret = 0;
	for (off = 0; off + sizeof(struct EA_FULL) < size; off += ea_size) {
		ea = Add2Ptr(ea_all, off);
		ea_size = unpacked_ea_size(ea);

		if (!ea->name_len)
			break;

		if (buffer) {
			/* Check if we can use field ea->name */
			if (off + ea_size > size)
				break;

			if (ret + ea->name_len + 1 > bytes_per_buffer) {
				err = -ERANGE;
				goto out;
			}

			memcpy(buffer + ret, ea->name, ea->name_len);
			buffer[ret + ea->name_len] = 0;
		}

		ret += ea->name_len + 1;
	}

out:
	kfree(ea_all);
	return err ? err : ret;
}

static int ntfs_get_ea(struct inode *inode, const char *name, size_t name_len,
		       void *buffer, size_t size, size_t *required)
{
	struct ntfs_inode *ni = ntfs_i(inode);
	const struct EA_INFO *info;
	struct EA_FULL *ea_all = NULL;
	const struct EA_FULL *ea;
	u32 off, len;
	int err;

	if (!(ni->ni_flags & NI_FLAG_EA))
		return -ENODATA;

	if (!required)
		ni_lock(ni);

	len = 0;

	if (name_len > 255) {
		err = -ENAMETOOLONG;
		goto out;
	}

	err = ntfs_read_ea(ni, &ea_all, 0, &info);
	if (err)
		goto out;

	if (!info)
		goto out;

	/* Enumerate all xattrs. */
	if (!find_ea(ea_all, le32_to_cpu(info->size), name, name_len, &off,
		     NULL)) {
		err = -ENODATA;
		goto out;
	}
	ea = Add2Ptr(ea_all, off);

	len = le16_to_cpu(ea->elength);
	if (!buffer) {
		err = 0;
		goto out;
	}

	if (len > size) {
		err = -ERANGE;
		if (required)
			*required = len;
		goto out;
	}

	memcpy(buffer, ea->name + ea->name_len + 1, len);
	err = 0;

out:
	kfree(ea_all);
	if (!required)
		ni_unlock(ni);

	return err ? err : len;
}

static noinline int ntfs_set_ea(struct inode *inode, const char *name,
				size_t name_len, const void *value,
				size_t val_size, int flags, bool locked,
				__le16 *ea_size)
{
	struct ntfs_inode *ni = ntfs_i(inode);
	struct ntfs_sb_info *sbi = ni->mi.sbi;
	int err;
	struct EA_INFO ea_info;
	const struct EA_INFO *info;
	struct EA_FULL *new_ea;
	struct EA_FULL *ea_all = NULL;
	size_t add, new_pack;
	u32 off, size, ea_sz;
	__le16 size_pack;
	struct ATTRIB *attr;
	struct ATTR_LIST_ENTRY *le;
	struct mft_inode *mi;
	struct runs_tree ea_run;
	u64 new_sz;
	void *p;

	if (!locked)
		ni_lock(ni);

	run_init(&ea_run);

	if (name_len > 255) {
		err = -ENAMETOOLONG;
		goto out;
	}

	add = ALIGN(struct_size(ea_all, name, 1 + name_len + val_size), 4);

	err = ntfs_read_ea(ni, &ea_all, add, &info);
	if (err)
		goto out;

	if (!info) {
		memset(&ea_info, 0, sizeof(ea_info));
		size = 0;
		size_pack = 0;
	} else {
		memcpy(&ea_info, info, sizeof(ea_info));
		size = le32_to_cpu(ea_info.size);
		size_pack = ea_info.size_pack;
	}

	if (info && find_ea(ea_all, size, name, name_len, &off, &ea_sz)) {
		struct EA_FULL *ea;

		if (flags & XATTR_CREATE) {
			err = -EEXIST;
			goto out;
		}

		ea = Add2Ptr(ea_all, off);

		/*
		 * Check simple case when we try to insert xattr with the same value
		 * e.g. ntfs_save_wsl_perm
		 */
		if (val_size && le16_to_cpu(ea->elength) == val_size &&
		    !memcmp(ea->name + ea->name_len + 1, value, val_size)) {
			/* xattr already contains the required value. */
			goto out;
		}

		/* Remove current xattr. */
		if (ea->flags & FILE_NEED_EA)
			le16_add_cpu(&ea_info.count, -1);

		le16_add_cpu(&ea_info.size_pack, 0 - packed_ea_size(ea));

		memmove(ea, Add2Ptr(ea, ea_sz), size - off - ea_sz);

		size -= ea_sz;
		memset(Add2Ptr(ea_all, size), 0, ea_sz);

		ea_info.size = cpu_to_le32(size);

		if ((flags & XATTR_REPLACE) && !val_size) {
			/* Remove xattr. */
			goto update_ea;
		}
	} else {
		if (flags & XATTR_REPLACE) {
			err = -ENODATA;
			goto out;
		}

		if (!ea_all) {
			ea_all = kzalloc(add, GFP_NOFS);
			if (!ea_all) {
				err = -ENOMEM;
				goto out;
			}
		}
	}

	/* Append new xattr. */
	new_ea = Add2Ptr(ea_all, size);
	new_ea->size = cpu_to_le32(add);
	new_ea->flags = 0;
	new_ea->name_len = name_len;
	new_ea->elength = cpu_to_le16(val_size);
	memcpy(new_ea->name, name, name_len);
	new_ea->name[name_len] = 0;
	memcpy(new_ea->name + name_len + 1, value, val_size);
	new_pack = le16_to_cpu(ea_info.size_pack) + packed_ea_size(new_ea);
	ea_info.size_pack = cpu_to_le16(new_pack);
	/* New size of ATTR_EA. */
	size += add;
	ea_info.size = cpu_to_le32(size);

	/*
	 * 1. Check ea_info.size_pack for overflow.
	 * 2. New attribute size must fit value from $AttrDef
	 */
	if (new_pack > 0xffff || size > sbi->ea_max_size) {
		ntfs_inode_warn(
			inode,
			"The size of extended attributes must not exceed 64KiB");
		err = -EFBIG; // -EINVAL?
		goto out;
	}

update_ea:

	if (!info) {
		/* Create xattr. */
		if (!size) {
			err = 0;
			goto out;
		}

		err = ni_insert_resident(ni, sizeof(struct EA_INFO),
					 ATTR_EA_INFO, NULL, 0, NULL, NULL,
					 NULL);
		if (err)
			goto out;

		err = ni_insert_resident(ni, 0, ATTR_EA, NULL, 0, NULL, NULL,
					 NULL);
		if (err)
			goto out;
	}

	new_sz = size;
	err = attr_set_size(ni, ATTR_EA, NULL, 0, &ea_run, new_sz, &new_sz,
			    false, NULL);
	if (err)
		goto out;

	le = NULL;
	attr = ni_find_attr(ni, NULL, &le, ATTR_EA_INFO, NULL, 0, NULL, &mi);
	if (!attr) {
		err = -EINVAL;
		goto out;
	}

	if (!size) {
		/* Delete xattr, ATTR_EA_INFO */
		ni_remove_attr_le(ni, attr, mi, le);
	} else {
		p = resident_data_ex(attr, sizeof(struct EA_INFO));
		if (!p) {
			err = -EINVAL;
			goto out;
		}
		memcpy(p, &ea_info, sizeof(struct EA_INFO));
		mi->dirty = true;
	}

	le = NULL;
	attr = ni_find_attr(ni, NULL, &le, ATTR_EA, NULL, 0, NULL, &mi);
	if (!attr) {
		err = -EINVAL;
		goto out;
	}

	if (!size) {
		/* Delete xattr, ATTR_EA */
		ni_remove_attr_le(ni, attr, mi, le);
	} else if (attr->non_res) {
		err = attr_load_runs_range(ni, ATTR_EA, NULL, 0, &ea_run, 0,
					   size);
		if (err)
			goto out;

		err = ntfs_sb_write_run(sbi, &ea_run, 0, ea_all, size, 0);
		if (err)
			goto out;
	} else {
		p = resident_data_ex(attr, size);
		if (!p) {
			err = -EINVAL;
			goto out;
		}
		memcpy(p, ea_all, size);
		mi->dirty = true;
	}

	/* Check if we delete the last xattr. */
	if (size)
		ni->ni_flags |= NI_FLAG_EA;
	else
		ni->ni_flags &= ~NI_FLAG_EA;

	if (ea_info.size_pack != size_pack)
		ni->ni_flags |= NI_FLAG_UPDATE_PARENT;
	if (ea_size)
		*ea_size = ea_info.size_pack;
	mark_inode_dirty(&ni->vfs_inode);

out:
	if (!locked)
		ni_unlock(ni);

	run_close(&ea_run);
	kfree(ea_all);

	return err;
}

#ifdef CONFIG_NTFS3_FS_POSIX_ACL

/*
 * ntfs_get_acl - inode_operations::get_acl
 */
struct posix_acl *ntfs_get_acl(struct mnt_idmap *idmap, struct dentry *dentry,
			       int type)
{
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni = ntfs_i(inode);
	const char *name;
	size_t name_len;
	struct posix_acl *acl;
	size_t req;
	int err;
	void *buf;

	/* Allocate PATH_MAX bytes. */
	buf = __getname();
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

	ni_lock(ni);

	err = ntfs_get_ea(inode, name, name_len, buf, PATH_MAX, &req);

	ni_unlock(ni);

	/* Translate extended attribute to acl. */
	if (err >= 0) {
		acl = posix_acl_from_xattr(&init_user_ns, buf, err);
	} else if (err == -ENODATA) {
		acl = NULL;
	} else {
		acl = ERR_PTR(err);
	}

	if (!IS_ERR(acl))
		set_cached_acl(inode, type, acl);

	__putname(buf);

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
		size = posix_acl_xattr_size(acl->a_count);
		value = kmalloc(size, GFP_NOFS);
		if (!value)
			return -ENOMEM;
		err = posix_acl_to_xattr(&init_user_ns, acl, value, size);
		if (err < 0)
			goto out;
		flags = 0;
	}

	err = ntfs_set_ea(inode, name, name_len, value, size, flags, 0, NULL);
	if (err == -ENODATA && !size)
		err = 0; /* Removing non existed xattr. */
	if (!err) {
		set_cached_acl(inode, type, acl);
		inode->i_mode = mode;
		inode_set_ctime_current(inode);
		mark_inode_dirty(inode);
	}

out:
	kfree(value);

	return err;
}

/*
 * ntfs_set_acl - inode_operations::set_acl
 */
int ntfs_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		 struct posix_acl *acl, int type)
{
	return ntfs_set_acl_ex(idmap, d_inode(dentry), acl, type, false);
}

/*
 * ntfs_init_acl - Initialize the ACLs of a new inode.
 *
 * Called from ntfs_create_inode().
 */
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

/*
 * ntfs_acl_chmod - Helper for ntfs3_setattr().
 */
int ntfs_acl_chmod(struct mnt_idmap *idmap, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct super_block *sb = inode->i_sb;

	if (!(sb->s_flags & SB_POSIXACL))
		return 0;

	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	return posix_acl_chmod(idmap, dentry, inode->i_mode);
}

/*
 * ntfs_listxattr - inode_operations::listxattr
 */
ssize_t ntfs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni = ntfs_i(inode);
	ssize_t ret;

	if (!(ni->ni_flags & NI_FLAG_EA)) {
		/* no xattr in file */
		return 0;
	}

	ni_lock(ni);

	ret = ntfs_list_ea(ni, buffer, size);

	ni_unlock(ni);

	return ret;
}

static int ntfs_getxattr(const struct xattr_handler *handler, struct dentry *de,
			 struct inode *inode, const char *name, void *buffer,
			 size_t size)
{
	int err;
	struct ntfs_inode *ni = ntfs_i(inode);

	/* Dispatch request. */
	if (!strcmp(name, SYSTEM_DOS_ATTRIB)) {
		/* system.dos_attrib */
		if (!buffer) {
			err = sizeof(u8);
		} else if (size < sizeof(u8)) {
			err = -ENODATA;
		} else {
			err = sizeof(u8);
			*(u8 *)buffer = le32_to_cpu(ni->std_fa);
		}
		goto out;
	}

	if (!strcmp(name, SYSTEM_NTFS_ATTRIB) ||
	    !strcmp(name, SYSTEM_NTFS_ATTRIB_BE)) {
		/* system.ntfs_attrib */
		if (!buffer) {
			err = sizeof(u32);
		} else if (size < sizeof(u32)) {
			err = -ENODATA;
		} else {
			err = sizeof(u32);
			*(u32 *)buffer = le32_to_cpu(ni->std_fa);
			if (!strcmp(name, SYSTEM_NTFS_ATTRIB_BE))
				*(__be32 *)buffer = cpu_to_be32(*(u32 *)buffer);
		}
		goto out;
	}

	if (!strcmp(name, SYSTEM_NTFS_SECURITY)) {
		/* system.ntfs_security*/
		struct SECURITY_DESCRIPTOR_RELATIVE *sd = NULL;
		size_t sd_size = 0;

		if (!is_ntfs3(ni->mi.sbi)) {
			/* We should get nt4 security. */
			err = -EINVAL;
			goto out;
		} else if (le32_to_cpu(ni->std_security_id) <
			   SECURITY_ID_FIRST) {
			err = -ENOENT;
			goto out;
		}

		err = ntfs_get_security_by_id(ni->mi.sbi, ni->std_security_id,
					      &sd, &sd_size);
		if (err)
			goto out;

		if (!is_sd_valid(sd, sd_size)) {
			ntfs_inode_warn(
				inode,
				"looks like you get incorrect security descriptor id=%u",
				ni->std_security_id);
		}

		if (!buffer) {
			err = sd_size;
		} else if (size < sd_size) {
			err = -ENODATA;
		} else {
			err = sd_size;
			memcpy(buffer, sd, sd_size);
		}
		kfree(sd);
		goto out;
	}

	/* Deal with NTFS extended attribute. */
	err = ntfs_get_ea(inode, name, strlen(name), buffer, size, NULL);

out:
	return err;
}

/*
 * ntfs_setxattr - inode_operations::setxattr
 */
static noinline int ntfs_setxattr(const struct xattr_handler *handler,
				  struct mnt_idmap *idmap, struct dentry *de,
				  struct inode *inode, const char *name,
				  const void *value, size_t size, int flags)
{
	int err = -EINVAL;
	struct ntfs_inode *ni = ntfs_i(inode);
	enum FILE_ATTRIBUTE new_fa;

	/* Dispatch request. */
	if (!strcmp(name, SYSTEM_DOS_ATTRIB)) {
		if (sizeof(u8) != size)
			goto out;
		new_fa = cpu_to_le32(*(u8 *)value);
		goto set_new_fa;
	}

	if (!strcmp(name, SYSTEM_NTFS_ATTRIB) ||
	    !strcmp(name, SYSTEM_NTFS_ATTRIB_BE)) {
		if (size != sizeof(u32))
			goto out;
		if (!strcmp(name, SYSTEM_NTFS_ATTRIB_BE))
			new_fa = cpu_to_le32(be32_to_cpu(*(__be32 *)value));
		else
			new_fa = cpu_to_le32(*(u32 *)value);

		if (S_ISREG(inode->i_mode)) {
			/* Process compressed/sparsed in special way. */
			ni_lock(ni);
			err = ni_new_attr_flags(ni, new_fa);
			ni_unlock(ni);
			if (err)
				goto out;
		}
set_new_fa:
		/*
		 * Thanks Mark Harmstone:
		 * Keep directory bit consistency.
		 */
		if (S_ISDIR(inode->i_mode))
			new_fa |= FILE_ATTRIBUTE_DIRECTORY;
		else
			new_fa &= ~FILE_ATTRIBUTE_DIRECTORY;

		if (ni->std_fa != new_fa) {
			ni->std_fa = new_fa;
			if (new_fa & FILE_ATTRIBUTE_READONLY)
				inode->i_mode &= ~0222;
			else
				inode->i_mode |= 0222;
			/* Std attribute always in primary record. */
			ni->mi.dirty = true;
			mark_inode_dirty(inode);
		}
		err = 0;

		goto out;
	}

	if (!strcmp(name, SYSTEM_NTFS_SECURITY)) {
		/* system.ntfs_security*/
		__le32 security_id;
		bool inserted;
		struct ATTR_STD_INFO5 *std;

		if (!is_ntfs3(ni->mi.sbi)) {
			/*
			 * We should replace ATTR_SECURE.
			 * Skip this way cause it is nt4 feature.
			 */
			err = -EINVAL;
			goto out;
		}

		if (!is_sd_valid(value, size)) {
			err = -EINVAL;
			ntfs_inode_warn(
				inode,
				"you try to set invalid security descriptor");
			goto out;
		}

		err = ntfs_insert_security(ni->mi.sbi, value, size,
					   &security_id, &inserted);
		if (err)
			goto out;

		ni_lock(ni);
		std = ni_std5(ni);
		if (!std) {
			err = -EINVAL;
		} else if (std->security_id != security_id) {
			std->security_id = ni->std_security_id = security_id;
			/* Std attribute always in primary record. */
			ni->mi.dirty = true;
			mark_inode_dirty(&ni->vfs_inode);
		}
		ni_unlock(ni);
		goto out;
	}

	/* Deal with NTFS extended attribute. */
	err = ntfs_set_ea(inode, name, strlen(name), value, size, flags, 0,
			  NULL);

out:
	inode_set_ctime_current(inode);
	mark_inode_dirty(inode);

	return err;
}

/*
 * ntfs_save_wsl_perm
 *
 * save uid/gid/mode in xattr
 */
int ntfs_save_wsl_perm(struct inode *inode, __le16 *ea_size)
{
	int err;
	__le32 value;
	struct ntfs_inode *ni = ntfs_i(inode);

	ni_lock(ni);
	value = cpu_to_le32(i_uid_read(inode));
	err = ntfs_set_ea(inode, "$LXUID", sizeof("$LXUID") - 1, &value,
			  sizeof(value), 0, true, ea_size);
	if (err)
		goto out;

	value = cpu_to_le32(i_gid_read(inode));
	err = ntfs_set_ea(inode, "$LXGID", sizeof("$LXGID") - 1, &value,
			  sizeof(value), 0, true, ea_size);
	if (err)
		goto out;

	value = cpu_to_le32(inode->i_mode);
	err = ntfs_set_ea(inode, "$LXMOD", sizeof("$LXMOD") - 1, &value,
			  sizeof(value), 0, true, ea_size);
	if (err)
		goto out;

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		value = cpu_to_le32(inode->i_rdev);
		err = ntfs_set_ea(inode, "$LXDEV", sizeof("$LXDEV") - 1, &value,
				  sizeof(value), 0, true, ea_size);
		if (err)
			goto out;
	}

out:
	ni_unlock(ni);
	/* In case of error should we delete all WSL xattr? */
	return err;
}

/*
 * ntfs_get_wsl_perm
 *
 * get uid/gid/mode from xattr
 * it is called from ntfs_iget5->ntfs_read_mft
 */
void ntfs_get_wsl_perm(struct inode *inode)
{
	size_t sz;
	__le32 value[3];

	if (ntfs_get_ea(inode, "$LXUID", sizeof("$LXUID") - 1, &value[0],
			sizeof(value[0]), &sz) == sizeof(value[0]) &&
	    ntfs_get_ea(inode, "$LXGID", sizeof("$LXGID") - 1, &value[1],
			sizeof(value[1]), &sz) == sizeof(value[1]) &&
	    ntfs_get_ea(inode, "$LXMOD", sizeof("$LXMOD") - 1, &value[2],
			sizeof(value[2]), &sz) == sizeof(value[2])) {
		i_uid_write(inode, (uid_t)le32_to_cpu(value[0]));
		i_gid_write(inode, (gid_t)le32_to_cpu(value[1]));
		inode->i_mode = le32_to_cpu(value[2]);

		if (ntfs_get_ea(inode, "$LXDEV", sizeof("$$LXDEV") - 1,
				&value[0], sizeof(value),
				&sz) == sizeof(value[0])) {
			inode->i_rdev = le32_to_cpu(value[0]);
		}
	}
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

const struct xattr_handler *ntfs_xattr_handlers[] = {
	&ntfs_other_xattr_handler,
	NULL,
};
// clang-format on
