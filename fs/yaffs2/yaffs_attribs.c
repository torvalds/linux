/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "yaffs_guts.h"
#include "yaffs_attribs.h"

void yaffs_load_attribs(struct yaffs_obj *obj, struct yaffs_obj_hdr *oh)
{
	obj->yst_uid = oh->yst_uid;
	obj->yst_gid = oh->yst_gid;
	obj->yst_atime = oh->yst_atime;
	obj->yst_mtime = oh->yst_mtime;
	obj->yst_ctime = oh->yst_ctime;
	obj->yst_rdev = oh->yst_rdev;
}

void yaffs_load_attribs_oh(struct yaffs_obj_hdr *oh, struct yaffs_obj *obj)
{
	oh->yst_uid = obj->yst_uid;
	oh->yst_gid = obj->yst_gid;
	oh->yst_atime = obj->yst_atime;
	oh->yst_mtime = obj->yst_mtime;
	oh->yst_ctime = obj->yst_ctime;
	oh->yst_rdev = obj->yst_rdev;

}

void yaffs_load_current_time(struct yaffs_obj *obj, int do_a, int do_c)
{
	obj->yst_mtime = Y_CURRENT_TIME;
	if (do_a)
		obj->yst_atime = obj->yst_mtime;
	if (do_c)
		obj->yst_ctime = obj->yst_mtime;
}

void yaffs_attribs_init(struct yaffs_obj *obj, u32 gid, u32 uid, u32 rdev)
{
	yaffs_load_current_time(obj, 1, 1);
	obj->yst_rdev = rdev;
	obj->yst_uid = uid;
	obj->yst_gid = gid;
}

loff_t yaffs_get_file_size(struct yaffs_obj *obj)
{
	YCHAR *alias = NULL;
	obj = yaffs_get_equivalent_obj(obj);

	switch (obj->variant_type) {
	case YAFFS_OBJECT_TYPE_FILE:
		return obj->variant.file_variant.file_size;
	case YAFFS_OBJECT_TYPE_SYMLINK:
		alias = obj->variant.symlink_variant.alias;
		if (!alias)
			return 0;
		return strnlen(alias, YAFFS_MAX_ALIAS_LENGTH);
	default:
		return 0;
	}
}

int yaffs_set_attribs(struct yaffs_obj *obj, struct iattr *attr)
{
	unsigned int valid = attr->ia_valid;

	if (valid & ATTR_MODE)
		obj->yst_mode = attr->ia_mode;
	if (valid & ATTR_UID)
		obj->yst_uid = attr->ia_uid;
	if (valid & ATTR_GID)
		obj->yst_gid = attr->ia_gid;

	if (valid & ATTR_ATIME)
		obj->yst_atime = Y_TIME_CONVERT(attr->ia_atime);
	if (valid & ATTR_CTIME)
		obj->yst_ctime = Y_TIME_CONVERT(attr->ia_ctime);
	if (valid & ATTR_MTIME)
		obj->yst_mtime = Y_TIME_CONVERT(attr->ia_mtime);

	if (valid & ATTR_SIZE)
		yaffs_resize_file(obj, attr->ia_size);

	yaffs_update_oh(obj, NULL, 1, 0, 0, NULL);

	return YAFFS_OK;

}

int yaffs_get_attribs(struct yaffs_obj *obj, struct iattr *attr)
{
	unsigned int valid = 0;

	attr->ia_mode = obj->yst_mode;
	valid |= ATTR_MODE;
	attr->ia_uid = obj->yst_uid;
	valid |= ATTR_UID;
	attr->ia_gid = obj->yst_gid;
	valid |= ATTR_GID;

	Y_TIME_CONVERT(attr->ia_atime) = obj->yst_atime;
	valid |= ATTR_ATIME;
	Y_TIME_CONVERT(attr->ia_ctime) = obj->yst_ctime;
	valid |= ATTR_CTIME;
	Y_TIME_CONVERT(attr->ia_mtime) = obj->yst_mtime;
	valid |= ATTR_MTIME;

	attr->ia_size = yaffs_get_file_size(obj);
	valid |= ATTR_SIZE;

	attr->ia_valid = valid;

	return YAFFS_OK;
}
