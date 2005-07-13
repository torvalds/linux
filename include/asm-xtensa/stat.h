/*
 * include/asm-xtensa/stat.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_STAT_H
#define _XTENSA_STAT_H

#include <linux/types.h>

struct __old_kernel_stat {
	unsigned short st_dev;
	unsigned short st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned long  st_size;
	unsigned long  st_atime;
	unsigned long  st_mtime;
	unsigned long  st_ctime;
};

#define STAT_HAVE_NSEC 1

struct stat {
	unsigned short st_dev;
	unsigned short __pad1;
	unsigned long st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned short __pad2;
	unsigned long  st_size;
	unsigned long  st_blksize;
	unsigned long  st_blocks;
	unsigned long  st_atime;
	unsigned long  st_atime_nsec;
	unsigned long  st_mtime;
	unsigned long  st_mtime_nsec;
	unsigned long  st_ctime;
	unsigned long  st_ctime_nsec;
	unsigned long  __unused4;
	unsigned long  __unused5;
};

/* This matches struct stat64 in glibc-2.2.3. */

struct stat64  {
#ifdef __XTENSA_EL__
	unsigned short	st_dev;		/* Device */
	unsigned char	__pad0[10];
#else
	unsigned char	__pad0[6];
	unsigned short	st_dev;
	unsigned char	__pad1[2];
#endif

#define STAT64_HAS_BROKEN_ST_INO	1
	unsigned long __st_ino;		/* 32bit file serial number. */

	unsigned int  st_mode;		/* File mode. */
	unsigned int  st_nlink;		/* Link count. */
	unsigned int  st_uid;		/* User ID of the file's owner. */
	unsigned int  st_gid;		/* Group ID of the file's group. */

#ifdef __XTENSA_EL__
	unsigned short	st_rdev;	/* Device number, if device. */
	unsigned char	__pad3[10];
#else
	unsigned char	__pad2[6];
	unsigned short	st_rdev;
	unsigned char	__pad3[2];
#endif

	long long int  st_size;		/* Size of file, in bytes. */
	long int st_blksize;		/* Optimal block size for I/O. */

#ifdef __XTENSA_EL__
	unsigned long  st_blocks;	/* Number 512-byte blocks allocated. */
	unsigned long  __pad4;
#else
	unsigned long  __pad4;
	unsigned long  st_blocks;
#endif

	unsigned long  __pad5;
	long int st_atime;		/* Time of last access. */
	unsigned long  st_atime_nsec;
	long int st_mtime;		/* Time of last modification. */
	unsigned long  st_mtime_nsec;
	long int  st_ctime;		/* Time of last status change. */
	unsigned long  st_ctime_nsec;
	unsigned long long int st_ino;	/* File serial number. */
};

#endif	/* _XTENSA_STAT_H */
