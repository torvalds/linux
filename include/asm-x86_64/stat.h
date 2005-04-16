#ifndef _ASM_X86_64_STAT_H
#define _ASM_X86_64_STAT_H

#define STAT_HAVE_NSEC 1

struct stat {
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
	long		st_blocks;	/* Number 512-byte blocks allocated. */

	unsigned long	st_atime;
	unsigned long 	st_atime_nsec; 
	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;
	unsigned long	st_ctime;
	unsigned long   st_ctime_nsec;
  	long		__unused[3];
};

/* For 32bit emulation */
struct __old_kernel_stat {
	unsigned short st_dev;
	unsigned short st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned int  st_size;
	unsigned int  st_atime;
	unsigned int  st_mtime;
	unsigned int  st_ctime;
};

#endif
