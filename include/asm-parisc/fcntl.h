#ifndef _PARISC_FCNTL_H
#define _PARISC_FCNTL_H

/* open/fcntl - O_SYNC is only implemented on blocks devices and on files
   located on an ext2 file system */
#define O_APPEND	00000010
#define O_BLKSEEK	00000100 /* HPUX only */
#define O_CREAT		00000400 /* not fcntl */
#define O_EXCL		00002000 /* not fcntl */
#define O_LARGEFILE	00004000
#define O_SYNC		00100000
#define O_NONBLOCK	00200004 /* HPUX has separate NDELAY & NONBLOCK */
#define O_NOCTTY	00400000 /* not fcntl */
#define O_DSYNC		01000000 /* HPUX only */
#define O_RSYNC		02000000 /* HPUX only */
#define O_NOATIME	04000000

#define O_DIRECTORY	00010000 /* must be a directory */
#define O_NOFOLLOW	00000200 /* don't follow links */
#define O_INVISIBLE	04000000 /* invisible I/O, for DMAPI/XDSM */

#define F_GETLK64	8
#define F_SETLK64	9
#define F_SETLKW64	10

#define F_GETOWN	11	/*  for sockets. */
#define F_SETOWN	12	/*  for sockets. */
#define F_SETSIG	13	/*  for sockets. */
#define F_GETSIG	14	/*  for sockets. */

/* for posix fcntl() and lockf() */
#define F_RDLCK		01
#define F_WRLCK		02
#define F_UNLCK		03

#include <asm-generic/fcntl.h>

#endif
