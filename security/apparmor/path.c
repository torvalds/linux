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
#include <linux/mnt_namespace.h>
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
	struct path root, tmp;
	char *res;
	int connected, error = 0;

	/* Get the root we want to resolve too, released below */
	if (flags & PATH_CHROOT_REL) {
		/* resolve paths relative to chroot */
		get_fs_root(current->fs, &root);
	} else {
		/* resolve paths relative to namespace */
		root.mnt = current->nsproxy->mnt_ns->root;
		root.dentry = root.mnt->mnt_root;
		path_get(&root);
	}

	tmp = root;
	res = __d_path(path, &tmp, buf, buflen);

	*name = res;
	/* handle error conditions - and still allow a partial path to
	 * be returned.
	 */
	if (IS_ERR(res)) {
		error = PTR_ERR(res);
		*name = buf;
		goto out;
	}

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

	/* Determine if the path is connected to the expected root */
	connected = tmp.dentry == root.dentry && tmp.mnt == root.mnt;

	/* If the path is not connected,
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
		/* is the disconnect path a sysctl? */
		if (tmp.dentry->d_sb->s_magic == PROC_SUPER_MAGIC &&
		    strncmp(*name, "/sys/", 5) == 0) {
			/* TODO: convert over to using a per namespace
			 * control instead of hard coded /proc
			 */
			error = prepend(name, *name - buf, "/proc", 5);
		} else if (!(flags & PATH_CONNECT_PATH) &&
			   !(((flags & CHROOT_NSCONNECT) == CHROOT_NSCONNECT) &&
			     (tmp.mnt == current->nsproxy->mnt_ns->root &&
			      tmp.dentry == tmp.mnt->mnt_root))) {
			/* disconnected path, don't return pathname starting
			 * with '/'
			 */
			error = -ESTALE;
			if (*res == '/')
				*name = res + 1;
		}
	}

out:
	path_put(&root);

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
			      int size, char **name)
{
	int adjust = (flags & PATH_IS_DIR) ? 1 : 0;
	int error = d_namespace_path(path, buffer, size - adjust, name, flags);

	if (!error && (flags & PATH_IS_DIR) && (*name)[1] != '\0')
		/*
		 * Append "/" to the pathname.  The root directory is a special
		 * case; it already ends in slash.
		 */
		strcpy(&buffer[size - 2], "/");

	return error;
}

/**
 * aa_get_name - compute the pathname of a file
 * @path: path the file  (NOT NULL)
 * @flags: flags controlling path name generation
 * @buffer: buffer that aa_get_name() allocated  (NOT NULL)
 * @name: Returns - the generated path name if !error (NOT NULL)
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
int aa_get_name(struct path *path, int flags, char **buffer, const char **name)
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

		error = get_name_to_buffer(path, flags, buf, size, &str);
		if (error != -ENAMETOOLONG)
			break;

		kfree(buf);
		size <<= 1;
		if (size > aa_g_path_max)
			return -ENAMETOOLONG;
	}
	*buffer = buf;
	*name = str;

	return error;
}
