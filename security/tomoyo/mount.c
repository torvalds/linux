/*
 * security/tomoyo/mount.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/slab.h>
#include "common.h"

/* String table for special mount operations. */
static const char * const tomoyo_mounts[TOMOYO_MAX_SPECIAL_MOUNT] = {
	[TOMOYO_MOUNT_BIND]            = "--bind",
	[TOMOYO_MOUNT_MOVE]            = "--move",
	[TOMOYO_MOUNT_REMOUNT]         = "--remount",
	[TOMOYO_MOUNT_MAKE_UNBINDABLE] = "--make-unbindable",
	[TOMOYO_MOUNT_MAKE_PRIVATE]    = "--make-private",
	[TOMOYO_MOUNT_MAKE_SLAVE]      = "--make-slave",
	[TOMOYO_MOUNT_MAKE_SHARED]     = "--make-shared",
};

/**
 * tomoyo_audit_mount_log - Audit mount log.
 *
 * @r: Pointer to "struct tomoyo_request_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_audit_mount_log(struct tomoyo_request_info *r)
{
	return tomoyo_supervisor(r, "file mount %s %s %s 0x%lX\n",
				 r->param.mount.dev->name,
				 r->param.mount.dir->name,
				 r->param.mount.type->name,
				 r->param.mount.flags);
}

/**
 * tomoyo_check_mount_acl - Check permission for path path path number operation.
 *
 * @r:   Pointer to "struct tomoyo_request_info".
 * @ptr: Pointer to "struct tomoyo_acl_info".
 *
 * Returns true if granted, false otherwise.
 */
static bool tomoyo_check_mount_acl(struct tomoyo_request_info *r,
				   const struct tomoyo_acl_info *ptr)
{
	const struct tomoyo_mount_acl *acl =
		container_of(ptr, typeof(*acl), head);
	return tomoyo_compare_number_union(r->param.mount.flags,
					   &acl->flags) &&
		tomoyo_compare_name_union(r->param.mount.type,
					  &acl->fs_type) &&
		tomoyo_compare_name_union(r->param.mount.dir,
					  &acl->dir_name) &&
		(!r->param.mount.need_dev ||
		 tomoyo_compare_name_union(r->param.mount.dev,
					   &acl->dev_name));
}

/**
 * tomoyo_mount_acl - Check permission for mount() operation.
 *
 * @r:        Pointer to "struct tomoyo_request_info".
 * @dev_name: Name of device file. Maybe NULL.
 * @dir:      Pointer to "struct path".
 * @type:     Name of filesystem type.
 * @flags:    Mount options.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_mount_acl(struct tomoyo_request_info *r, char *dev_name,
			    struct path *dir, const char *type,
			    unsigned long flags)
{
	struct tomoyo_obj_info obj = { };
	struct path path;
	struct file_system_type *fstype = NULL;
	const char *requested_type = NULL;
	const char *requested_dir_name = NULL;
	const char *requested_dev_name = NULL;
	struct tomoyo_path_info rtype;
	struct tomoyo_path_info rdev;
	struct tomoyo_path_info rdir;
	int need_dev = 0;
	int error = -ENOMEM;
	r->obj = &obj;

	/* Get fstype. */
	requested_type = tomoyo_encode(type);
	if (!requested_type)
		goto out;
	rtype.name = requested_type;
	tomoyo_fill_path_info(&rtype);

	/* Get mount point. */
	obj.path2 = *dir;
	requested_dir_name = tomoyo_realpath_from_path(dir);
	if (!requested_dir_name) {
		error = -ENOMEM;
		goto out;
	}
	rdir.name = requested_dir_name;
	tomoyo_fill_path_info(&rdir);

	/* Compare fs name. */
	if (type == tomoyo_mounts[TOMOYO_MOUNT_REMOUNT]) {
		/* dev_name is ignored. */
	} else if (type == tomoyo_mounts[TOMOYO_MOUNT_MAKE_UNBINDABLE] ||
		   type == tomoyo_mounts[TOMOYO_MOUNT_MAKE_PRIVATE] ||
		   type == tomoyo_mounts[TOMOYO_MOUNT_MAKE_SLAVE] ||
		   type == tomoyo_mounts[TOMOYO_MOUNT_MAKE_SHARED]) {
		/* dev_name is ignored. */
	} else if (type == tomoyo_mounts[TOMOYO_MOUNT_BIND] ||
		   type == tomoyo_mounts[TOMOYO_MOUNT_MOVE]) {
		need_dev = -1; /* dev_name is a directory */
	} else {
		fstype = get_fs_type(type);
		if (!fstype) {
			error = -ENODEV;
			goto out;
		}
		if (fstype->fs_flags & FS_REQUIRES_DEV)
			/* dev_name is a block device file. */
			need_dev = 1;
	}
	if (need_dev) {
		/* Get mount point or device file. */
		if (!dev_name || kern_path(dev_name, LOOKUP_FOLLOW, &path)) {
			error = -ENOENT;
			goto out;
		}
		obj.path1 = path;
		requested_dev_name = tomoyo_realpath_from_path(&path);
		if (!requested_dev_name) {
			error = -ENOENT;
			goto out;
		}
	} else {
		/* Map dev_name to "<NULL>" if no dev_name given. */
		if (!dev_name)
			dev_name = "<NULL>";
		requested_dev_name = tomoyo_encode(dev_name);
		if (!requested_dev_name) {
			error = -ENOMEM;
			goto out;
		}
	}
	rdev.name = requested_dev_name;
	tomoyo_fill_path_info(&rdev);
	r->param_type = TOMOYO_TYPE_MOUNT_ACL;
	r->param.mount.need_dev = need_dev;
	r->param.mount.dev = &rdev;
	r->param.mount.dir = &rdir;
	r->param.mount.type = &rtype;
	r->param.mount.flags = flags;
	do {
		tomoyo_check_acl(r, tomoyo_check_mount_acl);
		error = tomoyo_audit_mount_log(r);
	} while (error == TOMOYO_RETRY_REQUEST);
 out:
	kfree(requested_dev_name);
	kfree(requested_dir_name);
	if (fstype)
		put_filesystem(fstype);
	kfree(requested_type);
	/* Drop refcount obtained by kern_path(). */
	if (obj.path1.dentry)
		path_put(&obj.path1);
	return error;
}

/**
 * tomoyo_mount_permission - Check permission for mount() operation.
 *
 * @dev_name:  Name of device file. Maybe NULL.
 * @path:      Pointer to "struct path".
 * @type:      Name of filesystem type. Maybe NULL.
 * @flags:     Mount options.
 * @data_page: Optional data. Maybe NULL.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_mount_permission(char *dev_name, struct path *path,
			    const char *type, unsigned long flags,
			    void *data_page)
{
	struct tomoyo_request_info r;
	int error;
	int idx;

	if (tomoyo_init_request_info(&r, NULL, TOMOYO_MAC_FILE_MOUNT)
	    == TOMOYO_CONFIG_DISABLED)
		return 0;
	if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags &= ~MS_MGC_MSK;
	if (flags & MS_REMOUNT) {
		type = tomoyo_mounts[TOMOYO_MOUNT_REMOUNT];
		flags &= ~MS_REMOUNT;
	}
	if (flags & MS_MOVE) {
		type = tomoyo_mounts[TOMOYO_MOUNT_MOVE];
		flags &= ~MS_MOVE;
	}
	if (flags & MS_BIND) {
		type = tomoyo_mounts[TOMOYO_MOUNT_BIND];
		flags &= ~MS_BIND;
	}
	if (flags & MS_UNBINDABLE) {
		type = tomoyo_mounts[TOMOYO_MOUNT_MAKE_UNBINDABLE];
		flags &= ~MS_UNBINDABLE;
	}
	if (flags & MS_PRIVATE) {
		type = tomoyo_mounts[TOMOYO_MOUNT_MAKE_PRIVATE];
		flags &= ~MS_PRIVATE;
	}
	if (flags & MS_SLAVE) {
		type = tomoyo_mounts[TOMOYO_MOUNT_MAKE_SLAVE];
		flags &= ~MS_SLAVE;
	}
	if (flags & MS_SHARED) {
		type = tomoyo_mounts[TOMOYO_MOUNT_MAKE_SHARED];
		flags &= ~MS_SHARED;
	}
	if (!type)
		type = "<NULL>";
	idx = tomoyo_read_lock();
	error = tomoyo_mount_acl(&r, dev_name, path, type, flags);
	tomoyo_read_unlock(idx);
	return error;
}
