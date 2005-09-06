#ifndef _X86_64_FCNTL_H
#define _X86_64_FCNTL_H

#define F_GETLK		5
#define F_SETLK		6
#define F_SETLKW	7

#define F_SETOWN	8	/*  for sockets. */
#define F_GETOWN	9	/*  for sockets. */
#define F_SETSIG	10	/*  for sockets. */
#define F_GETSIG	11	/*  for sockets. */

/* for posix fcntl() and lockf() */
#define F_RDLCK		0
#define F_WRLCK		1
#define F_UNLCK		2

/* for old implementation of bsd flock () */
#define F_EXLCK		4	/* or 3 */
#define F_SHLCK		8	/* or 4 */

/* for leases */
#define F_INPROGRESS	16

struct flock {
	short  l_type;
	short  l_whence;
	off_t l_start;
	off_t l_len;
	pid_t  l_pid;
};

#include <asm-generic/fcntl.h>

#endif /* !_X86_64_FCNTL_H */
