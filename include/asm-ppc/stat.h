#ifndef _PPC_STAT_H
#define _PPC_STAT_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif /* __KERNEL__ */

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
	unsigned	st_dev;
	ino_t		st_ino;
	mode_t		st_mode;
	nlink_t		st_nlink;
	uid_t 		st_uid;
	gid_t 		st_gid;
	unsigned	st_rdev;
	off_t		st_size;
	unsigned long  	st_blksize;
	unsigned long  	st_blocks;
	unsigned long  	st_atime;
	unsigned long  	st_atime_nsec;
	unsigned long  	st_mtime;
	unsigned long  	st_mtime_nsec;
	unsigned long  	st_ctime;
	unsigned long  	st_ctime_nsec;
	unsigned long  	__unused4;
	unsigned long  	__unused5;
};

/* This matches struct stat64 in glibc2.1.
 */
struct stat64 {
	unsigned long long st_dev; 	/* Device.  */
	unsigned long long st_ino;	/* File serial number.  */
	unsigned int st_mode;		/* File mode.  */
	unsigned int st_nlink;		/* Link count.  */
	unsigned int st_uid;		/* User ID of the file's owner.  */
	unsigned int st_gid;		/* Group ID of the file's group. */
	unsigned long long st_rdev; 	/* Device number, if device.  */
	unsigned short int __pad2;
	long long st_size;		/* Size of file, in bytes.  */
	long st_blksize;		/* Optimal block size for I/O.  */

	long long st_blocks;		/* Number 512-byte blocks allocated. */
	long st_atime;			/* Time of last access.  */
	unsigned long st_atime_nsec;
	long st_mtime;			/* Time of last modification.  */
	unsigned long int st_mtime_nsec;
	long st_ctime;			/* Time of last status change.  */
	unsigned long int st_ctime_nsec;
	unsigned long int __unused4;
	unsigned long int __unused5;
};
#endif
