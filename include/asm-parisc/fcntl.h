#ifndef _PARISC_FCNTL_H
#define _PARISC_FCNTL_H

/* open/fcntl - O_SYNC is only implemented on blocks devices and on files
   located on an ext2 file system */
#define O_ACCMODE	00000003
#define O_RDONLY	00000000
#define O_WRONLY	00000001
#define O_RDWR		00000002
#define O_APPEND	00000010
#define O_BLKSEEK	00000100 /* HPUX only */
#define O_CREAT		00000400 /* not fcntl */
#define O_TRUNC		00001000 /* not fcntl */
#define O_EXCL		00002000 /* not fcntl */
#define O_LARGEFILE	00004000
#define O_SYNC		00100000
#define O_NONBLOCK	00200004 /* HPUX has separate NDELAY & NONBLOCK */
#define O_NDELAY	O_NONBLOCK
#define O_NOCTTY	00400000 /* not fcntl */
#define O_DSYNC		01000000 /* HPUX only */
#define O_RSYNC		02000000 /* HPUX only */
#define O_NOATIME	04000000

#define FASYNC		00020000 /* fcntl, for BSD compatibility */
#define O_DIRECT	00040000 /* direct disk access hint - currently ignored */
#define O_DIRECTORY	00010000 /* must be a directory */
#define O_NOFOLLOW	00000200 /* don't follow links */
#define O_INVISIBLE	04000000 /* invisible I/O, for DMAPI/XDSM */

#define F_DUPFD		0	/* dup */
#define F_GETFD		1	/* get f_flags */
#define F_SETFD		2	/* set f_flags */
#define F_GETFL		3	/* more flags (cloexec) */
#define F_SETFL		4
#define F_GETLK		5
#define F_SETLK		6
#define F_SETLKW	7
#define F_GETLK64	8
#define F_SETLK64	9
#define F_SETLKW64	10

#define F_GETOWN	11	/*  for sockets. */
#define F_SETOWN	12	/*  for sockets. */
#define F_SETSIG	13	/*  for sockets. */
#define F_GETSIG	14	/*  for sockets. */

/* for F_[GET|SET]FL */
#define FD_CLOEXEC	1	/* actually anything with low bit set goes */

/* for posix fcntl() and lockf() */
#define F_RDLCK		01
#define F_WRLCK		02
#define F_UNLCK		03

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

struct flock {
	short l_type;
	short l_whence;
	off_t l_start;
	off_t l_len;
	pid_t l_pid;
};

struct flock64 {
	short l_type;
	short l_whence;
	loff_t l_start;
	loff_t l_len;
	pid_t l_pid;
};

#define F_LINUX_SPECIFIC_BASE  1024

#endif
