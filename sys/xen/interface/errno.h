#ifndef __XEN_PUBLIC_ERRNO_H__

#ifndef __ASSEMBLY__

#define XEN_ERRNO(name, value) XEN_##name = value,
enum xen_errno {

#else /* !__ASSEMBLY__ */

#define XEN_ERRNO(name, value) .equ XEN_##name, value

#endif /* __ASSEMBLY__ */

/* ` enum neg_errnoval {  [ -Efoo for each Efoo in the list below ]  } */
/* ` enum errnoval { */

#endif /* __XEN_PUBLIC_ERRNO_H__ */

#ifdef XEN_ERRNO

/*
 * Values originating from x86 Linux. Please consider using respective
 * values when adding new definitions here.
 *
 * The set of identifiers to be added here shouldn't extend beyond what
 * POSIX mandates (see e.g.
 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html)
 * with the exception that we support some optional (XSR) values
 * specified there (but no new ones should be added).
 */

XEN_ERRNO(EPERM,	 1)	/* Operation not permitted */
XEN_ERRNO(ENOENT,	 2)	/* No such file or directory */
XEN_ERRNO(ESRCH,	 3)	/* No such process */
#ifdef __XEN__ /* Internal only, should never be exposed to the guest. */
XEN_ERRNO(EINTR,	 4)	/* Interrupted system call */
#endif
XEN_ERRNO(EIO,		 5)	/* I/O error */
XEN_ERRNO(ENXIO,	 6)	/* No such device or address */
XEN_ERRNO(E2BIG,	 7)	/* Arg list too long */
XEN_ERRNO(ENOEXEC,	 8)	/* Exec format error */
XEN_ERRNO(EBADF,	 9)	/* Bad file number */
XEN_ERRNO(ECHILD,	10)	/* No child processes */
XEN_ERRNO(EAGAIN,	11)	/* Try again */
XEN_ERRNO(ENOMEM,	12)	/* Out of memory */
XEN_ERRNO(EACCES,	13)	/* Permission denied */
XEN_ERRNO(EFAULT,	14)	/* Bad address */
XEN_ERRNO(EBUSY,	16)	/* Device or resource busy */
XEN_ERRNO(EEXIST,	17)	/* File exists */
XEN_ERRNO(EXDEV,	18)	/* Cross-device link */
XEN_ERRNO(ENODEV,	19)	/* No such device */
XEN_ERRNO(EINVAL,	22)	/* Invalid argument */
XEN_ERRNO(ENFILE,	23)	/* File table overflow */
XEN_ERRNO(EMFILE,	24)	/* Too many open files */
XEN_ERRNO(ENOSPC,	28)	/* No space left on device */
XEN_ERRNO(EMLINK,	31)	/* Too many links */
XEN_ERRNO(EDOM,		33)	/* Math argument out of domain of func */
XEN_ERRNO(ERANGE,	34)	/* Math result not representable */
XEN_ERRNO(EDEADLK,	35)	/* Resource deadlock would occur */
XEN_ERRNO(ENAMETOOLONG,	36)	/* File name too long */
XEN_ERRNO(ENOLCK,	37)	/* No record locks available */
XEN_ERRNO(ENOSYS,	38)	/* Function not implemented */
XEN_ERRNO(ENODATA,	61)	/* No data available */
XEN_ERRNO(ETIME,	62)	/* Timer expired */
XEN_ERRNO(EBADMSG,	74)	/* Not a data message */
XEN_ERRNO(EOVERFLOW,	75)	/* Value too large for defined data type */
XEN_ERRNO(EILSEQ,	84)	/* Illegal byte sequence */
#ifdef __XEN__ /* Internal only, should never be exposed to the guest. */
XEN_ERRNO(ERESTART,	85)	/* Interrupted system call should be restarted */
#endif
XEN_ERRNO(ENOTSOCK,	88)	/* Socket operation on non-socket */
XEN_ERRNO(EOPNOTSUPP,	95)	/* Operation not supported on transport endpoint */
XEN_ERRNO(EADDRINUSE,	98)	/* Address already in use */
XEN_ERRNO(EADDRNOTAVAIL, 99)	/* Cannot assign requested address */
XEN_ERRNO(ENOBUFS,	105)	/* No buffer space available */
XEN_ERRNO(EISCONN,	106)	/* Transport endpoint is already connected */
XEN_ERRNO(ENOTCONN,	107)	/* Transport endpoint is not connected */
XEN_ERRNO(ETIMEDOUT,	110)	/* Connection timed out */

#undef XEN_ERRNO
#endif /* XEN_ERRNO */

#ifndef __XEN_PUBLIC_ERRNO_H__
#define __XEN_PUBLIC_ERRNO_H__

/* ` } */

#ifndef __ASSEMBLY__
};
#endif

#define	XEN_EWOULDBLOCK	XEN_EAGAIN	/* Operation would block */
#define	XEN_EDEADLOCK	XEN_EDEADLK	/* Resource deadlock would occur */

#endif /*  __XEN_PUBLIC_ERRNO_H__ */
