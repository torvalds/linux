/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Special types used by various syscalls for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_TYPES_H
#define _NOLIBC_TYPES_H

#include "std.h"
#include <linux/time.h>
#include <linux/stat.h>


/* Only the generic macros and types may be defined here. The arch-specific
 * ones such as the O_RDONLY and related macros used by fcntl() and open(), or
 * the layout of sys_stat_struct must not be defined here.
 */

/* stat flags (WARNING, octal here). We need to check for an existing
 * definition because linux/stat.h may omit to define those if it finds
 * that any glibc header was already included.
 */
#if !defined(S_IFMT)
#define S_IFDIR        0040000
#define S_IFCHR        0020000
#define S_IFBLK        0060000
#define S_IFREG        0100000
#define S_IFIFO        0010000
#define S_IFLNK        0120000
#define S_IFSOCK       0140000
#define S_IFMT         0170000

#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define S_ISCHR(mode)  (((mode) & S_IFMT) == S_IFCHR)
#define S_ISBLK(mode)  (((mode) & S_IFMT) == S_IFBLK)
#define S_ISREG(mode)  (((mode) & S_IFMT) == S_IFREG)
#define S_ISFIFO(mode) (((mode) & S_IFMT) == S_IFIFO)
#define S_ISLNK(mode)  (((mode) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100

#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010

#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001
#endif

/* dirent types */
#define DT_UNKNOWN     0x0
#define DT_FIFO        0x1
#define DT_CHR         0x2
#define DT_DIR         0x4
#define DT_BLK         0x6
#define DT_REG         0x8
#define DT_LNK         0xa
#define DT_SOCK        0xc

/* commonly an fd_set represents 256 FDs */
#ifndef FD_SETSIZE
#define FD_SETSIZE     256
#endif

/* PATH_MAX and MAXPATHLEN are often used and found with plenty of different
 * values.
 */
#ifndef PATH_MAX
#define PATH_MAX       4096
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN     (PATH_MAX)
#endif

/* whence values for lseek() */
#define SEEK_SET       0
#define SEEK_CUR       1
#define SEEK_END       2

/* Macros used on waitpid()'s return status */
#define WEXITSTATUS(status) (((status) & 0xff00) >> 8)
#define WIFEXITED(status)   (((status) & 0x7f) == 0)
#define WTERMSIG(status)    ((status) & 0x7f)
#define WIFSIGNALED(status) ((status) - 1 < 0xff)

/* waitpid() flags */
#define WNOHANG      1

/* standard exit() codes */
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define FD_SETIDXMASK (8 * sizeof(unsigned long))
#define FD_SETBITMASK (8 * sizeof(unsigned long)-1)

/* for select() */
typedef struct {
	unsigned long fds[(FD_SETSIZE + FD_SETBITMASK) / FD_SETIDXMASK];
} fd_set;

#define FD_CLR(fd, set) do {						\
		fd_set *__set = (set);					\
		int __fd = (fd);					\
		if (__fd >= 0)						\
			__set->fds[__fd / FD_SETIDXMASK] &=		\
				~(1U << (__fd & FX_SETBITMASK));	\
	} while (0)

#define FD_SET(fd, set) do {						\
		fd_set *__set = (set);					\
		int __fd = (fd);					\
		if (__fd >= 0)						\
			__set->fds[__fd / FD_SETIDXMASK] |=		\
				1 << (__fd & FD_SETBITMASK);		\
	} while (0)

#define FD_ISSET(fd, set) ({						\
			fd_set *__set = (set);				\
			int __fd = (fd);				\
		int __r = 0;						\
		if (__fd >= 0)						\
			__r = !!(__set->fds[__fd / FD_SETIDXMASK] &	\
1U << (__fd & FD_SET_BITMASK));						\
		__r;							\
	})

#define FD_ZERO(set) do {						\
		fd_set *__set = (set);					\
		int __idx;						\
		int __size = (FD_SETSIZE+FD_SETBITMASK) / FD_SETIDXMASK;\
		for (__idx = 0; __idx < __size; __idx++)		\
			__set->fds[__idx] = 0;				\
	} while (0)

/* for poll() */
#define POLLIN          0x0001
#define POLLPRI         0x0002
#define POLLOUT         0x0004
#define POLLERR         0x0008
#define POLLHUP         0x0010
#define POLLNVAL        0x0020

struct pollfd {
	int fd;
	short int events;
	short int revents;
};

/* for getdents64() */
struct linux_dirent64 {
	uint64_t       d_ino;
	int64_t        d_off;
	unsigned short d_reclen;
	unsigned char  d_type;
	char           d_name[];
};

/* needed by wait4() */
struct rusage {
	struct timeval ru_utime;
	struct timeval ru_stime;
	long   ru_maxrss;
	long   ru_ixrss;
	long   ru_idrss;
	long   ru_isrss;
	long   ru_minflt;
	long   ru_majflt;
	long   ru_nswap;
	long   ru_inblock;
	long   ru_oublock;
	long   ru_msgsnd;
	long   ru_msgrcv;
	long   ru_nsignals;
	long   ru_nvcsw;
	long   ru_nivcsw;
};

/* The format of the struct as returned by the libc to the application, which
 * significantly differs from the format returned by the stat() syscall flavours.
 */
struct stat {
	dev_t     st_dev;     /* ID of device containing file */
	ino_t     st_ino;     /* inode number */
	mode_t    st_mode;    /* protection */
	nlink_t   st_nlink;   /* number of hard links */
	uid_t     st_uid;     /* user ID of owner */
	gid_t     st_gid;     /* group ID of owner */
	dev_t     st_rdev;    /* device ID (if special file) */
	off_t     st_size;    /* total size, in bytes */
	blksize_t st_blksize; /* blocksize for file system I/O */
	blkcnt_t  st_blocks;  /* number of 512B blocks allocated */
	union { time_t st_atime; struct timespec st_atim; }; /* time of last access */
	union { time_t st_mtime; struct timespec st_mtim; }; /* time of last modification */
	union { time_t st_ctime; struct timespec st_ctim; }; /* time of last status change */
};

/* WARNING, it only deals with the 4096 first majors and 256 first minors */
#define makedev(major, minor) ((dev_t)((((major) & 0xfff) << 8) | ((minor) & 0xff)))
#define major(dev) ((unsigned int)(((dev) >> 8) & 0xfff))
#define minor(dev) ((unsigned int)(((dev) & 0xff))

#ifndef offsetof
#define offsetof(TYPE, FIELD) ((size_t) &((TYPE *)0)->FIELD)
#endif

#ifndef container_of
#define container_of(PTR, TYPE, FIELD) ({			\
	__typeof__(((TYPE *)0)->FIELD) *__FIELD_PTR = (PTR);	\
	(TYPE *)((char *) __FIELD_PTR - offsetof(TYPE, FIELD));	\
})
#endif

/* make sure to include all global symbols */
#include "nolibc.h"

#endif /* _NOLIBC_TYPES_H */
