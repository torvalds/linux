/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2003 Ralf Baechle
 */
#ifndef _ASM_FCNTL_H
#define _ASM_FCNTL_H

/* open/fcntl - O_SYNC is only implemented on blocks devices and on files
   located on an ext2 file system */
#define O_ACCMODE	0x0003
#define O_RDONLY	0x0000
#define O_WRONLY	0x0001
#define O_RDWR		0x0002
#define O_APPEND	0x0008
#define O_SYNC		0x0010
#define O_NONBLOCK	0x0080
#define O_CREAT         0x0100	/* not fcntl */
#define O_TRUNC		0x0200	/* not fcntl */
#define O_EXCL		0x0400	/* not fcntl */
#define O_NOCTTY	0x0800	/* not fcntl */
#define FASYNC		0x1000	/* fcntl, for BSD compatibility */
#define O_LARGEFILE	0x2000	/* allow large file opens */
#define O_DIRECT	0x8000	/* direct disk access hint */
#define O_DIRECTORY	0x10000	/* must be a directory */
#define O_NOFOLLOW	0x20000	/* don't follow links */
#define O_NOATIME	0x40000

#define O_NDELAY	O_NONBLOCK

#define F_DUPFD		0	/* dup */
#define F_GETFD		1	/* get close_on_exec */
#define F_SETFD		2	/* set/clear close_on_exec */
#define F_GETFL		3	/* get file->f_flags */
#define F_SETFL		4	/* set file->f_flags */
#define F_GETLK		14
#define F_SETLK		6
#define F_SETLKW	7

#define F_SETOWN	24	/*  for sockets. */
#define F_GETOWN	23	/*  for sockets. */
#define F_SETSIG	10	/*  for sockets. */
#define F_GETSIG	11	/*  for sockets. */

#ifndef __mips64
#define F_GETLK64	33	/*  using 'struct flock64' */
#define F_SETLK64	34
#define F_SETLKW64	35
#endif

/* for F_[GET|SET]FL */
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */

/* for posix fcntl() and lockf() */
#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2

/* for old implementation of bsd flock () */
#define F_EXLCK		4	/* or 3 */
#define F_SHLCK		8	/* or 4 */

/* for leases */
#define F_INPROGRESS	16

/* operations for bsd flock(), also used by the kernel implementation */
#define LOCK_SH		1	/* shared lock */
#define LOCK_EX		2	/* exclusive lock */
#define LOCK_NB		4	/* or'd with one of the above to prevent
				   blocking */
#define LOCK_UN		8	/* remove lock */

#define LOCK_MAND	32	/* This is a mandatory flock */
#define LOCK_READ	64	/* ... Which allows concurrent read operations */
#define LOCK_WRITE	128	/* ... Which allows concurrent write operations */
#define LOCK_RW		192	/* ... Which allows concurrent read & write ops */

/*
 * The flavours of struct flock.  "struct flock" is the ABI compliant
 * variant.  Finally struct flock64 is the LFS variant of struct flock.  As
 * a historic accident and inconsistence with the ABI definition it doesn't
 * contain all the same fields as struct flock.
 */

#ifndef __mips64

typedef struct flock {
	short	l_type;
	short	l_whence;
	__kernel_off_t l_start;
	__kernel_off_t l_len;
	long	l_sysid;
	__kernel_pid_t l_pid;
	long	pad[4];
} flock_t;

typedef struct flock64 {
	short	l_type;
	short	l_whence;
	loff_t	l_start;
	loff_t	l_len;
	pid_t	l_pid;
} flock64_t;

#else /* 64-bit definitions */

typedef struct flock {
	short	l_type;
	short	l_whence;
	__kernel_off_t l_start;
	__kernel_off_t l_len;
	__kernel_pid_t l_pid;
} flock_t;

#ifdef __KERNEL__
#define flock64		flock
#endif

#endif

#define F_LINUX_SPECIFIC_BASE	1024

#endif /* _ASM_FCNTL_H */
