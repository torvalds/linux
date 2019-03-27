/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Kyle J. Kneitinger <kyle@kneit.in>
 * Copyright (c) 2018 Kyle Evans <kevans@FreeBSD.org>
 * Copyright (c) 2019 Wes Maag <wes@jwmaag.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "be.h"
#include "be_impl.h"

struct be_mountcheck_info {
	const char *path;
	char *name;
};

struct be_mount_info {
	libbe_handle_t *lbh;
	const char *be;
	const char *mountpoint;
	int mntflags;
	int deepmount;
};

static int
be_mountcheck_cb(zfs_handle_t *zfs_hdl, void *data)
{
	struct be_mountcheck_info *info;
	char *mountpoint;

	if (data == NULL)
		return (1);
	info = (struct be_mountcheck_info *)data;
	if (!zfs_is_mounted(zfs_hdl, &mountpoint))
		return (0);
	if (strcmp(mountpoint, info->path) == 0) {
		info->name = strdup(zfs_get_name(zfs_hdl));
		free(mountpoint);
		return (1);
	}
	free(mountpoint);
	return (0);
}

/*
 * Called from be_mount, uses the given zfs_handle and attempts to
 * mount it at the passed mountpoint. If the deepmount flag is set, continue
 * calling the function for each child dataset.
 */
static int
be_mount_iter(zfs_handle_t *zfs_hdl, void *data)
{
	int err;
	char *mountpoint;
	char tmp[BE_MAXPATHLEN], zfs_mnt[BE_MAXPATHLEN];
	struct be_mount_info *info;

	info = (struct be_mount_info *)data;

	if (zfs_is_mounted(zfs_hdl, &mountpoint)) {
		free(mountpoint);
		return (0);
	}

	if (zfs_prop_get_int(zfs_hdl, ZFS_PROP_CANMOUNT) == ZFS_CANMOUNT_OFF)
		return (0);

	if (zfs_prop_get(zfs_hdl, ZFS_PROP_MOUNTPOINT, zfs_mnt, BE_MAXPATHLEN,
	    NULL, NULL, 0, 1))
		return (1);

	if (strcmp("none", zfs_mnt) != 0) {
		char opt = '\0';

		mountpoint = be_mountpoint_augmented(info->lbh, zfs_mnt);

		snprintf(tmp, BE_MAXPATHLEN, "%s%s", info->mountpoint,
		    mountpoint);

		if ((err = zmount(zfs_get_name(zfs_hdl), tmp, info->mntflags,
		 __DECONST(char *, MNTTYPE_ZFS), NULL, 0, &opt, 1)) != 0) {
			switch (errno) {
			case ENAMETOOLONG:
				return (set_error(info->lbh, BE_ERR_PATHLEN));
			case ELOOP:
			case ENOENT:
			case ENOTDIR:
				return (set_error(info->lbh, BE_ERR_BADPATH));
			case EPERM:
				return (set_error(info->lbh, BE_ERR_PERMS));
			case EBUSY:
				return (set_error(info->lbh, BE_ERR_PATHBUSY));
			default:
				return (set_error(info->lbh, BE_ERR_UNKNOWN));
			}
		}
	}

	if (!info->deepmount)
		return (0);

	return (zfs_iter_filesystems(zfs_hdl, be_mount_iter, info));
}


static int
be_umount_iter(zfs_handle_t *zfs_hdl, void *data)
{

	int err;
	char *mountpoint;
	struct be_mount_info *info;

	info = (struct be_mount_info *)data;

	if((err = zfs_iter_filesystems(zfs_hdl, be_umount_iter, info)) != 0) {
		return (err);
	}

	if (!zfs_is_mounted(zfs_hdl, &mountpoint)) {
		return (0);
	}
	free(mountpoint);

	if (zfs_unmount(zfs_hdl, NULL, info->mntflags) != 0) {
		switch (errno) {
		case ENAMETOOLONG:
			return (set_error(info->lbh, BE_ERR_PATHLEN));
		case ELOOP:
		case ENOENT:
		case ENOTDIR:
			return (set_error(info->lbh, BE_ERR_BADPATH));
		case EPERM:
			return (set_error(info->lbh, BE_ERR_PERMS));
		case EBUSY:
			return (set_error(info->lbh, BE_ERR_PATHBUSY));
		default:
			return (set_error(info->lbh, BE_ERR_UNKNOWN));
		}
	}
	return (0);
}

/*
 * usage
 */
int
be_mounted_at(libbe_handle_t *lbh, const char *path, nvlist_t *details)
{
	char be[BE_MAXPATHLEN];
	zfs_handle_t *root_hdl;
	struct be_mountcheck_info info;
	prop_data_t propinfo;

	bzero(&be, BE_MAXPATHLEN);
	if ((root_hdl = zfs_open(lbh->lzh, lbh->root,
	    ZFS_TYPE_FILESYSTEM)) == NULL)
		return (BE_ERR_ZFSOPEN);

	info.path = path;
	info.name = NULL;
	zfs_iter_filesystems(root_hdl, be_mountcheck_cb, &info);
	zfs_close(root_hdl);

	if (info.name != NULL) {
		if (details != NULL) {
			if ((root_hdl = zfs_open(lbh->lzh, lbh->root,
			    ZFS_TYPE_FILESYSTEM)) == NULL) {
				free(info.name);
				return (BE_ERR_ZFSOPEN);
			}

			propinfo.lbh = lbh;
			propinfo.list = details;
			propinfo.single_object = false;
			prop_list_builder_cb(root_hdl, &propinfo);
			zfs_close(root_hdl);
		}
		free(info.name);
		return (0);
	}
	return (1);
}

/*
 * usage
 */
int
be_mount(libbe_handle_t *lbh, char *bootenv, char *mountpoint, int flags,
    char *result_loc)
{
	char be[BE_MAXPATHLEN];
	char mnt_temp[BE_MAXPATHLEN];
	int mntflags, mntdeep;
	int err;
	struct be_mount_info info;
	zfs_handle_t *zhdl;

	if ((err = be_root_concat(lbh, bootenv, be)) != 0)
		return (set_error(lbh, err));

	if ((err = be_exists(lbh, bootenv)) != 0)
		return (set_error(lbh, err));

	if (is_mounted(lbh->lzh, be, NULL))
		return (set_error(lbh, BE_ERR_MOUNTED));

	mntdeep = (flags & BE_MNT_DEEP) ? 1 : 0;
	mntflags = (flags & BE_MNT_FORCE) ? MNT_FORCE : 0;

	/* Create mountpoint if it is not specified */
	if (mountpoint == NULL) {
		strlcpy(mnt_temp, "/tmp/be_mount.XXXX", sizeof(mnt_temp));
		if (mkdtemp(mnt_temp) == NULL)
			return (set_error(lbh, BE_ERR_IO));
	}

	if ((zhdl = zfs_open(lbh->lzh, be, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (set_error(lbh, BE_ERR_ZFSOPEN));

	info.lbh = lbh;
	info.be = be;
	info.mountpoint = (mountpoint == NULL) ? mnt_temp : mountpoint;
	info.mntflags = mntflags;
	info.deepmount = mntdeep;

	if((err = be_mount_iter(zhdl, &info) != 0)) {
		zfs_close(zhdl);
		return (err);
	}
	zfs_close(zhdl);

	if (result_loc != NULL)
		strlcpy(result_loc, mountpoint == NULL ? mnt_temp : mountpoint,
		    BE_MAXPATHLEN);

	return (BE_ERR_SUCCESS);
}

/*
 * usage
 */
int
be_unmount(libbe_handle_t *lbh, char *bootenv, int flags)
{
	int err;
	char be[BE_MAXPATHLEN];
	zfs_handle_t *root_hdl;
	struct be_mount_info info;

	if ((err = be_root_concat(lbh, bootenv, be)) != 0)
		return (set_error(lbh, err));

	if ((root_hdl = zfs_open(lbh->lzh, be, ZFS_TYPE_FILESYSTEM)) == NULL)
		return (set_error(lbh, BE_ERR_ZFSOPEN));

	info.lbh = lbh;
	info.be = be;
	info.mountpoint = NULL;
	info.mntflags = (flags & BE_MNT_FORCE) ? MS_FORCE : 0;

	if ((err = be_umount_iter(root_hdl, &info)) != 0) {
		zfs_close(root_hdl);
		return (err);
	}

	zfs_close(root_hdl);
	return (BE_ERR_SUCCESS);
}

/*
 * This function will blow away the input buffer as needed if we're discovered
 * to be looking at a root-mount.  If the mountpoint is naturally beyond the
 * root, however, the buffer may be left intact and a pointer to the section
 * past altroot will be returned instead for the caller's perusal.
 */
char *
be_mountpoint_augmented(libbe_handle_t *lbh, char *mountpoint)
{

	if (lbh->altroot_len == 0)
		return (mountpoint);
	if (mountpoint == NULL || *mountpoint == '\0')
		return (mountpoint);

	if (mountpoint[lbh->altroot_len] == '\0') {
		*(mountpoint + 1) = '\0';
		return (mountpoint);
	} else
		return (mountpoint + lbh->altroot_len);
}
