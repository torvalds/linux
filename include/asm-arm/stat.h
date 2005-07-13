#ifndef _ASMARM_STAT_H
#define _ASMARM_STAT_H

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

#define STAT_HAVE_NSEC 

struct stat {
#if defined(__ARMEB__)
	unsigned short st_dev;
	unsigned short __pad1;
#else
	unsigned long  st_dev;
#endif
	unsigned long  st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
#if defined(__ARMEB__)
	unsigned short st_rdev;
	unsigned short __pad2;
#else
	unsigned long  st_rdev;
#endif
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

/* This matches struct stat64 in glibc2.1, hence the absolutely
 * insane amounts of padding around dev_t's.
 * Note: The kernel zero's the padded region because glibc might read them
 * in the hope that the kernel has stretched to using larger sizes.
 */
struct stat64 {
	unsigned long long	st_dev;
	unsigned char   __pad0[4];

#define STAT64_HAS_BROKEN_ST_INO	1
	unsigned long	__st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;

	unsigned long	st_uid;
	unsigned long	st_gid;

	unsigned long long	st_rdev;
	unsigned char   __pad3[4];

	long long	st_size;
	unsigned long	st_blksize;

#if defined(__ARMEB__)
	unsigned long   __pad4;		/* Future possible st_blocks hi bits */
	unsigned long   st_blocks;	/* Number 512-byte blocks allocated. */
#else /* Must be little */
	unsigned long   st_blocks;	/* Number 512-byte blocks allocated. */
	unsigned long   __pad4;		/* Future possible st_blocks hi bits */
#endif

	unsigned long	st_atime;
	unsigned long	st_atime_nsec;

	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;

	unsigned long	st_ctime;
	unsigned long	st_ctime_nsec;

	unsigned long long	st_ino;
} __attribute__((packed));

#endif
