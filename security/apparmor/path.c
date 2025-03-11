// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor function for pathnames
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
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
static int disconnect(const struct path *path, char *buf, char **name,
		      int flags, const char *disconnected)
{
	int error = 0;

	if (!(flags & PATH_CONNECT_PATH) &&
	    !(((flags & CHROOT_NSCONNECT) == CHROOT_NSCONNECT) &&
	      our_mnt(path->mnt))) {
		/* disconnected path, don't return pathname starting
		 * with '/'
		 */
		error = -EACCES;
		if (**name == '/')
			*name = *name + 1;
	} else {
		if (**name != '/')
			/* CONNECT_PATH with missing root */
			error = prepend(name, *name - buf, "/", 1);
		if (!error && disconnected)
			error = prepend(name, *name - buf, disconnected,
					strlen(disconnected));
	}

	return error;
}

/**
 * d_namespace_path - lookup a name associated with a given path
 * @path: path to lookup  (NOT NULL)
 * @buf:  buffer to store path to  (NOT NULL)
 * @name: Returns - pointer for start of path name with in @buf (NOT NULL)
 * @flags: flags controlling path lookup
 * @disconnected: string to prefix to disconnected paths
 *
 * Handle path name lookup.
 *
 * Returns: %0 else error code if path lookup fails
 *          When no error the path name is returned in @name which points to
 *          a position in @buf
 */
static int d_namespace_path(const struct path *path, char *buf, char **name,
			    int flags, const char *disconnected)
{
	char *res;
	int error = 0;
	int connected = 1;
	int isdir = (flags & PATH_IS_DIR) ? 1 : 0;
	int buflen = aa_g_path_max - isdir;

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
			error = prepend(name, *name - buf, "/proc", 5);
			goto out;
		} else
			error = disconnect(path, buf, name, flags,
					   disconnected);
		goto out;
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
	if (IS_ERR_OR_NULL(res)) {
		if (PTR_ERR(res) == -ENAMETOOLONG) {
			error = -ENAMETOOLONG;
			*name = buf;
			goto out;
		}
		connected = 0;
		res = dentry_path_raw(path->dentry, buf, buflen);
		if (IS_ERR(res)) {
			error = PTR_ERR(res);
			*name = buf;
			goto out;
		}
	} else if (!our_mnt(path->mnt))
		connected = 0;

	*name = res;

	if (!connected)
		error = disconnect(path, buf, name, flags, disconnected);

	/* Handle two cases:
	 * 1. A deleted dentry && profile is not allowing mediation of deleted
	 * 2. On some filesystems, newly allocated dentries appear to the
	 *    security_path hooks as a deleted dentry except without an inode
	 *    allocated.
	 */
	if (d_unlinked(path->dentry) && d_is_positive(path->dentry) &&
	    !(flags & (PATH_MEDIATE_DELETED | PATH_DELEGATE_DELETED))) {
			error = -ENOENT;
			goto out;
	}

out:
	/*
	 * Append "/" to the pathname.  The root directory is a special
	 * case; it already ends in slash.
	 */
	if (!error && isdir && ((*name)[1] != '\0' || (*name)[0] != '/'))
		strcpy(&buf[aa_g_path_max - 2], "/");

	return error;
}

/**
 * aa_path_name - get the pathname to a buffer ensure dir / is appended
 * @path: path the file  (NOT NULL)
 * @flags: flags controlling path name generation
 * @buffer: buffer to put name in (NOT NULL)
 * @name: Returns - the generated path name if !error (NOT NULL)
 * @info: Returns - information on why the path lookup failed (MAYBE NULL)
 * @disconnected: string to prepend to disconnected paths
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
int aa_path_name(const struct path *path, int flags, char *buffer,
		 const char **name, const char **info, const char *disconnected)
{
	char *str = NULL;
	int error = d_namespace_path(path, buffer, &str, flags, disconnected);

	if (info && error) {
		if (error == -ENOENT)
			*info = "Failed name lookup - deleted entry";
		else if (error == -EACCES)
			*info = "Failed name lookup - disconnected path";
		else if (error == -ENAMETOOLONG)
			*info = "Failed name lookup - name too long";
		else
			*info = "Failed name lookup";
	}

	*name = str;

	return error;
}
