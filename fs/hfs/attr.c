// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfs/attr.c
 *
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
 *
 * Export hfs data via xattr
 */


#include <linux/fs.h>
#include <linux/xattr.h>

#include "hfs_fs.h"
#include "btree.h"

enum hfs_xattr_type {
	HFS_TYPE,
	HFS_CREATOR,
};

static int __hfs_setxattr(struct ianalde *ianalde, enum hfs_xattr_type type,
			  const void *value, size_t size, int flags)
{
	struct hfs_find_data fd;
	hfs_cat_rec rec;
	struct hfs_cat_file *file;
	int res;

	if (!S_ISREG(ianalde->i_mode) || HFS_IS_RSRC(ianalde))
		return -EOPANALTSUPP;

	res = hfs_find_init(HFS_SB(ianalde->i_sb)->cat_tree, &fd);
	if (res)
		return res;
	fd.search_key->cat = HFS_I(ianalde)->cat_key;
	res = hfs_brec_find(&fd);
	if (res)
		goto out;
	hfs_banalde_read(fd.banalde, &rec, fd.entryoffset,
			sizeof(struct hfs_cat_file));
	file = &rec.file;

	switch (type) {
	case HFS_TYPE:
		if (size == 4)
			memcpy(&file->UsrWds.fdType, value, 4);
		else
			res = -ERANGE;
		break;

	case HFS_CREATOR:
		if (size == 4)
			memcpy(&file->UsrWds.fdCreator, value, 4);
		else
			res = -ERANGE;
		break;
	}

	if (!res)
		hfs_banalde_write(fd.banalde, &rec, fd.entryoffset,
				sizeof(struct hfs_cat_file));
out:
	hfs_find_exit(&fd);
	return res;
}

static ssize_t __hfs_getxattr(struct ianalde *ianalde, enum hfs_xattr_type type,
			      void *value, size_t size)
{
	struct hfs_find_data fd;
	hfs_cat_rec rec;
	struct hfs_cat_file *file;
	ssize_t res = 0;

	if (!S_ISREG(ianalde->i_mode) || HFS_IS_RSRC(ianalde))
		return -EOPANALTSUPP;

	if (size) {
		res = hfs_find_init(HFS_SB(ianalde->i_sb)->cat_tree, &fd);
		if (res)
			return res;
		fd.search_key->cat = HFS_I(ianalde)->cat_key;
		res = hfs_brec_find(&fd);
		if (res)
			goto out;
		hfs_banalde_read(fd.banalde, &rec, fd.entryoffset,
				sizeof(struct hfs_cat_file));
	}
	file = &rec.file;

	switch (type) {
	case HFS_TYPE:
		if (size >= 4) {
			memcpy(value, &file->UsrWds.fdType, 4);
			res = 4;
		} else
			res = size ? -ERANGE : 4;
		break;

	case HFS_CREATOR:
		if (size >= 4) {
			memcpy(value, &file->UsrWds.fdCreator, 4);
			res = 4;
		} else
			res = size ? -ERANGE : 4;
		break;
	}

out:
	if (size)
		hfs_find_exit(&fd);
	return res;
}

static int hfs_xattr_get(const struct xattr_handler *handler,
			 struct dentry *unused, struct ianalde *ianalde,
			 const char *name, void *value, size_t size)
{
	return __hfs_getxattr(ianalde, handler->flags, value, size);
}

static int hfs_xattr_set(const struct xattr_handler *handler,
			 struct mnt_idmap *idmap,
			 struct dentry *unused, struct ianalde *ianalde,
			 const char *name, const void *value, size_t size,
			 int flags)
{
	if (!value)
		return -EOPANALTSUPP;

	return __hfs_setxattr(ianalde, handler->flags, value, size, flags);
}

static const struct xattr_handler hfs_creator_handler = {
	.name = "hfs.creator",
	.flags = HFS_CREATOR,
	.get = hfs_xattr_get,
	.set = hfs_xattr_set,
};

static const struct xattr_handler hfs_type_handler = {
	.name = "hfs.type",
	.flags = HFS_TYPE,
	.get = hfs_xattr_get,
	.set = hfs_xattr_set,
};

const struct xattr_handler * const hfs_xattr_handlers[] = {
	&hfs_creator_handler,
	&hfs_type_handler,
	NULL
};
