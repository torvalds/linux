/*
 * security/tomoyo/realpath.c
 *
 * Pathname calculation functions for TOMOYO.
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 */

#include <linux/types.h>
#include <linux/mount.h>
#include <linux/mnt_namespace.h>
#include <linux/fs_struct.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include "common.h"

/**
 * tomoyo_encode: Convert binary string to ascii string.
 *
 * @buffer:  Buffer for ASCII string.
 * @buflen:  Size of @buffer.
 * @str:     Binary string.
 *
 * Returns 0 on success, -ENOMEM otherwise.
 */
int tomoyo_encode(char *buffer, int buflen, const char *str)
{
	while (1) {
		const unsigned char c = *(unsigned char *) str++;

		if (tomoyo_is_valid(c)) {
			if (--buflen <= 0)
				break;
			*buffer++ = (char) c;
			if (c != '\\')
				continue;
			if (--buflen <= 0)
				break;
			*buffer++ = (char) c;
			continue;
		}
		if (!c) {
			if (--buflen <= 0)
				break;
			*buffer = '\0';
			return 0;
		}
		buflen -= 4;
		if (buflen <= 0)
			break;
		*buffer++ = '\\';
		*buffer++ = (c >> 6) + '0';
		*buffer++ = ((c >> 3) & 7) + '0';
		*buffer++ = (c & 7) + '0';
	}
	return -ENOMEM;
}

/**
 * tomoyo_realpath_from_path2 - Returns realpath(3) of the given dentry but ignores chroot'ed root.
 *
 * @path:        Pointer to "struct path".
 * @newname:     Pointer to buffer to return value in.
 * @newname_len: Size of @newname.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * If dentry is a directory, trailing '/' is appended.
 * Characters out of 0x20 < c < 0x7F range are converted to
 * \ooo style octal string.
 * Character \ is converted to \\ string.
 */
int tomoyo_realpath_from_path2(struct path *path, char *newname,
			       int newname_len)
{
	int error = -ENOMEM;
	struct dentry *dentry = path->dentry;
	char *sp;

	if (!dentry || !path->mnt || !newname || newname_len <= 2048)
		return -EINVAL;
	if (dentry->d_op && dentry->d_op->d_dname) {
		/* For "socket:[\$]" and "pipe:[\$]". */
		static const int offset = 1536;
		sp = dentry->d_op->d_dname(dentry, newname + offset,
					   newname_len - offset);
	} else {
		struct path ns_root = {.mnt = NULL, .dentry = NULL};

		spin_lock(&dcache_lock);
		/* go to whatever namespace root we are under */
		sp = __d_path(path, &ns_root, newname, newname_len);
		spin_unlock(&dcache_lock);
		/* Prepend "/proc" prefix if using internal proc vfs mount. */
		if (!IS_ERR(sp) && (path->mnt->mnt_flags & MNT_INTERNAL) &&
		    (path->mnt->mnt_sb->s_magic == PROC_SUPER_MAGIC)) {
			sp -= 5;
			if (sp >= newname)
				memcpy(sp, "/proc", 5);
			else
				sp = ERR_PTR(-ENOMEM);
		}
	}
	if (IS_ERR(sp))
		error = PTR_ERR(sp);
	else
		error = tomoyo_encode(newname, sp - newname, sp);
	/* Append trailing '/' if dentry is a directory. */
	if (!error && dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode)
	    && *newname) {
		sp = newname + strlen(newname);
		if (*(sp - 1) != '/') {
			if (sp < newname + newname_len - 4) {
				*sp++ = '/';
				*sp = '\0';
			} else {
				error = -ENOMEM;
			}
		}
	}
	if (error)
		tomoyo_warn_oom(__func__);
	return error;
}

/**
 * tomoyo_realpath_from_path - Returns realpath(3) of the given pathname but ignores chroot'ed root.
 *
 * @path: Pointer to "struct path".
 *
 * Returns the realpath of the given @path on success, NULL otherwise.
 *
 * These functions use kzalloc(), so the caller must call kfree()
 * if these functions didn't return NULL.
 */
char *tomoyo_realpath_from_path(struct path *path)
{
	char *buf = kzalloc(sizeof(struct tomoyo_page_buffer), GFP_NOFS);

	BUILD_BUG_ON(TOMOYO_MAX_PATHNAME_LEN > PATH_MAX);
	BUILD_BUG_ON(sizeof(struct tomoyo_page_buffer)
		     <= TOMOYO_MAX_PATHNAME_LEN - 1);
	if (!buf)
		return NULL;
	if (tomoyo_realpath_from_path2(path, buf,
				       TOMOYO_MAX_PATHNAME_LEN - 1) == 0)
		return buf;
	kfree(buf);
	return NULL;
}

/**
 * tomoyo_realpath - Get realpath of a pathname.
 *
 * @pathname: The pathname to solve.
 *
 * Returns the realpath of @pathname on success, NULL otherwise.
 */
char *tomoyo_realpath(const char *pathname)
{
	struct path path;

	if (pathname && kern_path(pathname, LOOKUP_FOLLOW, &path) == 0) {
		char *buf = tomoyo_realpath_from_path(&path);
		path_put(&path);
		return buf;
	}
	return NULL;
}

/**
 * tomoyo_realpath_nofollow - Get realpath of a pathname.
 *
 * @pathname: The pathname to solve.
 *
 * Returns the realpath of @pathname on success, NULL otherwise.
 */
char *tomoyo_realpath_nofollow(const char *pathname)
{
	struct path path;

	if (pathname && kern_path(pathname, 0, &path) == 0) {
		char *buf = tomoyo_realpath_from_path(&path);
		path_put(&path);
		return buf;
	}
	return NULL;
}
