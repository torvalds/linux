/*
 * include/asm-xtensa/fcntl.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 1998 by Ralf Baechle
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_FCNTL_H
#define _XTENSA_FCNTL_H

/* open/fcntl - O_SYNC is only implemented on blocks devices and on files
   located on an ext2 file system */
#define O_APPEND	0x0008
#define O_SYNC		0x0010
#define O_NONBLOCK	0x0080
#define O_CREAT         0x0100	/* not fcntl */
#define O_EXCL		0x0400	/* not fcntl */
#define O_NOCTTY	0x0800	/* not fcntl */
#define FASYNC		0x1000	/* fcntl, for BSD compatibility */
#define O_LARGEFILE	0x2000	/* allow large file opens - currently ignored */
#define O_DIRECT	0x8000	/* direct disk access hint - currently ignored*/
#define O_NOATIME	0x100000

#define F_GETLK		14
#define F_GETLK64       15
#define F_SETLK		6
#define F_SETLKW	7
#define F_SETLK64       16
#define F_SETLKW64      17

#define F_SETOWN	24	/*  for sockets. */
#define F_GETOWN	23	/*  for sockets. */

typedef struct flock {
	short l_type;
	short l_whence;
	__kernel_off_t l_start;
	__kernel_off_t l_len;
	long  l_sysid;
	__kernel_pid_t l_pid;
	long  pad[4];
} flock_t;

struct flock64 {
	short  l_type;
	short  l_whence;
	__kernel_off_t l_start;
	__kernel_off_t l_len;
	pid_t  l_pid;
};

#define HAVE_ARCH_STRUCT_FLOCK
#define HAVE_ARCH_STRUCT_FLOCK64

#include <asm-generic/fcntl.h>

#endif /* _XTENSA_FCNTL_H */
