/*
    FUSE: Filesystem in Userspace
    Copyright (C) 2001-2005  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

/* This file defines the kernel interface of FUSE */

#include <asm/types.h>

/** Version number of this interface */
#define FUSE_KERNEL_VERSION 5

/** Minor version number of this interface */
#define FUSE_KERNEL_MINOR_VERSION 1

/** The node ID of the root inode */
#define FUSE_ROOT_ID 1

struct fuse_attr {
	__u64	ino;
	__u64	size;
	__u64	blocks;
	__u64	atime;
	__u64	mtime;
	__u64	ctime;
	__u32	atimensec;
	__u32	mtimensec;
	__u32	ctimensec;
	__u32	mode;
	__u32	nlink;
	__u32	uid;
	__u32	gid;
	__u32	rdev;
};

