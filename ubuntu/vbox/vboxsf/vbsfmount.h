/* $Id: vbsfmount.h $ */
/** @file
 * vboxsf - VBox Linux Shared Folders VFS, mount(2) parameter structure.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GA_INCLUDED_SRC_linux_sharedfolders_vbsfmount_h
#define GA_INCLUDED_SRC_linux_sharedfolders_vbsfmount_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Linux constraints the size of data mount argument to PAGE_SIZE - 1. */
#define MAX_HOST_NAME  256
#define MAX_NLS_NAME    32

#define VBSF_MOUNT_SIGNATURE_BYTE_0 '\377'
#define VBSF_MOUNT_SIGNATURE_BYTE_1 '\376'
#define VBSF_MOUNT_SIGNATURE_BYTE_2 '\375'

struct vbsf_mount_info_new {
	/*
	 * The old version of the mount_info struct started with a
	 * char name[MAX_HOST_NAME] field, where name cannot be '\0'.
	 * So the new version of the mount_info struct starts with a
	 * nullchar field which is always 0 so that we can detect and
	 * reject the old structure being passed.
	 */
	char nullchar;
	char signature[3];	/* signature */
	int length;		/* length of the whole structure */
	char name[MAX_HOST_NAME];	/* share name */
	char nls_name[MAX_NLS_NAME];	/* name of an I/O charset */
	int uid;		/* user ID for all entries, default 0=root */
	int gid;		/* group ID for all entries, default 0=root */
	int ttl;		/* time to live */
	int dmode;		/* mode for directories if != 0xffffffff */
	int fmode;		/* mode for regular files if != 0xffffffff */
	int dmask;		/* umask applied to directories */
	int fmask;		/* umask applied to regular files */
	char tag[32];		/**< Mount tag for VBoxService automounter.  @since 6.0 */
};

struct vbsf_mount_opts {
	int uid;
	int gid;
	int ttl;
	int dmode;
	int fmode;
	int dmask;
	int fmask;
	int ronly;
	int sloppy;
	int noexec;
	int nodev;
	int nosuid;
	int remount;
	char nls_name[MAX_NLS_NAME];
	char *convertcp;
};

/** Completes the mount operation by adding the new mount point to mtab if required. */
int vbsfmount_complete(const char *host_name, const char *mount_point,
		       unsigned long flags, struct vbsf_mount_opts *opts);

#endif /* !GA_INCLUDED_SRC_linux_sharedfolders_vbsfmount_h */
