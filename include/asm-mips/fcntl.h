/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2003 Ralf Baechle
 */
#ifndef _ASM_FCNTL_H
#define _ASM_FCNTL_H

#define O_APPEND	0x0008
#define O_SYNC		0x0010
#define O_NONBLOCK	0x0080
#define O_CREAT         0x0100	/* not fcntl */
#define O_EXCL		0x0400	/* not fcntl */
#define O_NOCTTY	0x0800	/* not fcntl */
#define FASYNC		0x1000	/* fcntl, for BSD compatibility */
#define O_LARGEFILE	0x2000	/* allow large file opens */
#define O_DIRECT	0x8000	/* direct disk access hint */

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

/* for posix fcntl() and lockf() */
#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2

/* for old implementation of bsd flock () */
#define F_EXLCK		4	/* or 3 */
#define F_SHLCK		8	/* or 4 */

/* for leases */
#define F_INPROGRESS	16

/*
 * The flavours of struct flock.  "struct flock" is the ABI compliant
 * variant.  Finally struct flock64 is the LFS variant of struct flock.  As
 * a historic accident and inconsistence with the ABI definition it doesn't
 * contain all the same fields as struct flock.
 */

#ifndef __mips64

struct flock {
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

#include <asm-generic/fcntl.h>

#endif /* _ASM_FCNTL_H */
