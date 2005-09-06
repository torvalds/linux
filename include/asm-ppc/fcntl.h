#ifndef _PPC_FCNTL_H
#define _PPC_FCNTL_H

/* open/fcntl - O_SYNC is only implemented on blocks devices and on files
   located on an ext2 file system */
#define O_CREAT		   0100	/* not fcntl */
#define O_EXCL		   0200	/* not fcntl */
#define O_NOCTTY	   0400	/* not fcntl */
#define O_TRUNC		  01000	/* not fcntl */
#define O_APPEND	  02000
#define O_NONBLOCK	  04000
#define O_NDELAY	O_NONBLOCK
#define O_SYNC		 010000
#define FASYNC		 020000	/* fcntl, for BSD compatibility */
#define O_DIRECTORY      040000	/* must be a directory */
#define O_NOFOLLOW      0100000	/* don't follow links */
#define O_LARGEFILE     0200000
#define O_DIRECT	0400000	/* direct disk access hint */
#define O_NOATIME	01000000

#define F_GETLK		5
#define F_SETLK		6
#define F_SETLKW	7

#define F_SETOWN	8	/*  for sockets. */
#define F_GETOWN	9	/*  for sockets. */
#define F_SETSIG	10	/*  for sockets. */
#define F_GETSIG	11	/*  for sockets. */

#define F_GETLK64	12	/*  using 'struct flock64' */
#define F_SETLK64	13
#define F_SETLKW64	14

/* for posix fcntl() and lockf() */
#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2

/* for old implementation of bsd flock () */
#define F_EXLCK		4	/* or 3 */
#define F_SHLCK		8	/* or 4 */

/* for leases */
#define F_INPROGRESS	16

#ifdef __KERNEL__
#define F_POSIX		1
#define F_FLOCK		2
#define F_BROKEN	4	/* broken flock() emulation */
#endif /* __KERNEL__ */

struct flock {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	pid_t l_pid;
};

struct flock64 {
	short  l_type;
	short  l_whence;
	loff_t l_start;
	loff_t l_len;
	pid_t  l_pid;
};

#include <asm-generic/fcntl.h>

#endif
