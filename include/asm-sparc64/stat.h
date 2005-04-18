/* $Id: stat.h,v 1.7 2000/08/04 05:35:55 davem Exp $ */
#ifndef _SPARC64_STAT_H
#define _SPARC64_STAT_H

#include <linux/types.h>

struct stat {
	unsigned   st_dev;
	ino_t   st_ino;
	mode_t  st_mode;
	short   st_nlink;
	uid_t   st_uid;
	gid_t   st_gid;
	unsigned   st_rdev;
	off_t   st_size;
	time_t  st_atime;
	time_t  st_mtime;
	time_t  st_ctime;
	off_t   st_blksize;
	off_t   st_blocks;
	unsigned long  __unused4[2];
};

struct stat64 {
	unsigned long	st_dev;
	unsigned long	st_ino;
	unsigned long	st_nlink;

	unsigned int	st_mode;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	__pad0;

	unsigned long	st_rdev;
	long		st_size;
	long		st_blksize;
	long		st_blocks;

	unsigned long	st_atime;
	unsigned long	st_atime_nsec;
	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;
	unsigned long	st_ctime;
	unsigned long	st_ctime_nsec;
	long		__unused[3];
};

#endif
