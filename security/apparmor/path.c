/*
 * AppArmor security module
 *
 * This file contains AppArmor function for pathnames
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/magic.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/nsproxy.h>
#include <linux/path.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs_struct.h>

#include "include/apparmor.h"
#include "include/path.h"
#include "include/policy.h"


/* modified from dcache.c */
static int prepend(char **buffer, int buflen, const char *str, int namelen)
{
	buflen -= namelen;
	if (buflen < 0)
		return -ENAMETOOLONG;
	*buffer -= namelen;
	memcpy(*buffer, str, namelen);
	return 0;
}

#define CHROOT_NSCONNECT (PATH_CHROOT_REL | PATH_CHROOT_NSCONNECT)

/**
 * d_namespace_path - lookup a name associated with a given path
 * @path: path to lookup  (NOT NULL)
 * @buf:  buffer to store path to  (NOT NULL)
 * @buflen: length of @buf
 * @name: Returns - pointer for start of path name with in @buf (NOT NULL)
 * @flags: flags controlling path lookup
 *
 * Handle path name lookup.
 *
 * Returns: %0 else error code if path lookup fails
 *          When no error the path name is returned in @name which points to
 *          to a position in @buf
 */
static int d_namespace_path(struct path *path, char *buf, int buflen,
			    char **name, int flags)
{
	char *res;
	int error = 0;
	int connected = 1;

	if (path->mnt->mnt_flags & MNT_INTERNAL) {
		/* it's not mounted anywhere */
		res = dentry_path(path->dentry, buf, buflen);
		*name = res;
		if (IS_ERR(res)) {
			*name = buf;
			return PTR_ERR(res);
		}
		if (path->dentry->d_sb->s_magic == PROC_SUPER_MAGIC &&
		    strncmp(*name, "/sys/", 5) == 0) {
			/* TODO: convert over to using a per namespace
			 * control instead of hard coded /proc
			 */
			return prepend(name, *name - buf, "/proc", 5);
		}
		return 0;
	}

	/* resolve paths relative to chroot?*/
	if (flags & PATH_CHROOT_REL) {
		struct path root;
		get_fs_root(current->fs, &root);
		res = __d_path(path, &root, buf, buflen);
		path_put(&root);
	} else {
		res = d_absolute_path(path, buf, buflen);
		if (!our_mnt(path->mnt))
			connected = 0;
	}

	/* handle error conditions - and still allow a partial path to
	 * be returned.
	 */
	if (!res || IS_ERR(res)) {
		if (PTR_ERR(res) == -ENAMETOOLONG)
			return -ENAMETOOLONG;
		connected = 0;
		res = dentry_path_raw(path->dentry, buf, buflen);
		if (IS_ERR(res)) {
			error = PTR_ERR(res);
			*name = buf;
			goto out;
		};
	} else if (!our_mnt(path->mnt))
		connected = 0;

	*name = res;

	/* Handle two cases:
	 * 1. A deleted dentry && profile is not allowing mediation of deleted
	 * 2. On some filesystems, newly allocated dentries appear to the
	 *    security_path hooks as a deleted dentry except without an inode
	 *    allocated.
	 */
	if (d_unlinked(path->dentry) && path->dentry->d_inode &&
	    !(flags & PATH_MEDIATE_DELETED)) {
			error = -ENOENT;
			goto out;
	}

	/* If the path is not connected to the expected root,
	 * check if it is a sysctl and handle specially else remove any
	 * leading / that __d_path may have returned.
	 * Unless
	 *     specifically directed to connect the path,
	 * OR
	 *     if in a chroot and doing chroot relative paths and the path
	 *     resolves to the namespace root (would be connected outside
	 *     of chroot) and specifically directed to connect paths to
	 *     namespace root.
	 */
	if (!connected) {
		if (!(flags & PATH_CONNECT_PATH) &&
			   !(((flags & CHROOT_NSCONNECT) == CHROOT_NSCONNECT) &&
			     our_mnt(path->mnt))) {
			/* disconnected path, don't return pathname starting
			 * with '/'
			 */
			error = -EACCES;
			if (*res == '/')
				*name = res + 1;
		}
	}

out:
	return error;
}

/**
 * get_name_to_buffer - get the pathname to a buffer ensure dir / is appended
 * @path: path to get name for  (NOT NULL)
 * @flags: flags controlling path lookup
 * @buffer: buffer to put name in  (NOT NULL)
 * @size: size of buffer
 * @name: Returns - contains position of path name in @buffer (NOT NULL)
 *
 * Returns: %0 else error on failure
 */
static int get_name_to_buffer(struct path *path, int flags, char *buffer,
			      int size, char **name, const char **info)
{
	int adjust = (flags & PATH_IS_DIR) ? 1 : 0;
	int error = d_namespace_path(path, buffer, size - adjust, name, flags);

	if (!error && (flags & PATH_IS_DIR) && (*name)[1] != '\0')
		/*
		 * Append "/" to the pathname.  The root directory is a special
		 * case; it already ends in slash.
		 */
		strcpy(&buffer[size - 2], "/");

	if (info && error) {
		if (error == -ENOENT)
			*info = "Failed name lookup - deleted entry";
		else if (error == -ESTALE)
			*info = "Failed name lookup - disconnected path";
		else if (error == -ENAMETOOLONG)
			*info = "Failed name lookup - name too long";
		else
			*info = "Failed name lookup";
	}

	return error;
}

/**
 * aa_path_name - compute the pathname of a file
 * @path: path the file  (NOT NULL)
 * @flags: flags controlling path name generation
 * @buffer: buffer that aa_get_name() allocated  (NOT NULL)
 * @name: Returns - the generated path name if !error (NOT NULL)
 * @info: Returns - information on why the path lookup failed (MAYBE NULL)
 *
 * @name is a pointer to the beginning of the pathname (which usually differs
 * from the beginning of the buffer), or NULL.  If there is an error @name
 * may contain a partial or invalid name that can be used for audit purposes,
 * but it can not be used for mediation.
 *
 * We need PATH_IS_DIR to indicate whether the file is a directory or not
 * because the file may not yet exist, and so we cannot check the inode's
 * file type.
 *
 * Returns: %0 else error code if could retrieve name
 */
int aa_path_name(struct path *path, int flags, char **buffer, const char **name,
		 const char **info)
{
	char *buf, *str = NULL;
	int size = 256;
	int error;

	*name = NULL;
	*buffer = NULL;
	for (;;) {
		/* freed by caller */
		buf = kmalloc(size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		error = get_name_to_buffer(path, flags, buf, size, &str, info);
		if (error != -ENAMETOOLONG)
			break;

		kfree(buf);
		size <<= 1;
		if (size > aa_g_path_max)
			return -ENAMETOOLONG;
		*info = NULL;
	}
	*buffer = buf;
	*name = str;

	return error;
}
