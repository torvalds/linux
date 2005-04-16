/* $Id: bsderrno.h,v 1.1 1996/12/26 13:25:21 davem Exp $
 * bsderrno.h: Error numbers for NetBSD binary compatibility
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_BSDERRNO_H
#define _SPARC64_BSDERRNO_H

#define BSD_EPERM         1      /* Operation not permitted */
#define BSD_ENOENT        2      /* No such file or directory */
#define BSD_ESRCH         3      /* No such process */
#define BSD_EINTR         4      /* Interrupted system call */
#define BSD_EIO           5      /* Input/output error */
#define BSD_ENXIO         6      /* Device not configured */
#define BSD_E2BIG         7      /* Argument list too long */
#define BSD_ENOEXEC       8      /* Exec format error */
#define BSD_EBADF         9      /* Bad file descriptor */
#define BSD_ECHILD        10     /* No child processes */
#define BSD_EDEADLK       11     /* Resource deadlock avoided */
#define BSD_ENOMEM        12     /* Cannot allocate memory */
#define BSD_EACCES        13     /* Permission denied */
#define BSD_EFAULT        14     /* Bad address */
#define BSD_ENOTBLK       15     /* Block device required */
#define BSD_EBUSY         16     /* Device busy */
#define BSD_EEXIST        17     /* File exists */
#define BSD_EXDEV         18     /* Cross-device link */
#define BSD_ENODEV        19     /* Operation not supported by device */
#define BSD_ENOTDIR       20     /* Not a directory */
#define BSD_EISDIR        21     /* Is a directory */
#define BSD_EINVAL        22     /* Invalid argument */
#define BSD_ENFILE        23     /* Too many open files in system */
#define BSD_EMFILE        24     /* Too many open files */
#define BSD_ENOTTY        25     /* Inappropriate ioctl for device */
#define BSD_ETXTBSY       26     /* Text file busy */
#define BSD_EFBIG         27     /* File too large */
#define BSD_ENOSPC        28     /* No space left on device */
#define BSD_ESPIPE        29     /* Illegal seek */
#define BSD_EROFS         30     /* Read-only file system */
#define BSD_EMLINK        31     /* Too many links */
#define BSD_EPIPE         32     /* Broken pipe */
#define BSD_EDOM          33     /* Numerical argument out of domain */
#define BSD_ERANGE        34     /* Result too large */
#define BSD_EAGAIN        35     /* Resource temporarily unavailable */
#define BSD_EWOULDBLOCK   EAGAIN /* Operation would block */
#define BSD_EINPROGRESS   36     /* Operation now in progress */
#define BSD_EALREADY      37     /* Operation already in progress */
#define BSD_ENOTSOCK      38     /* Socket operation on non-socket */
#define BSD_EDESTADDRREQ  39     /* Destination address required */
#define BSD_EMSGSIZE      40     /* Message too long */
#define BSD_EPROTOTYPE    41     /* Protocol wrong type for socket */
#define BSD_ENOPROTOOPT   42     /* Protocol not available */
#define BSD_EPROTONOSUPPORT  43  /* Protocol not supported */
#define BSD_ESOCKTNOSUPPORT  44  /* Socket type not supported */
#define BSD_EOPNOTSUPP    45     /* Operation not supported */
#define BSD_EPFNOSUPPORT  46     /* Protocol family not supported */
#define BSD_EAFNOSUPPORT  47     /* Address family not supported by protocol family */
#define BSD_EADDRINUSE    48     /* Address already in use */
#define BSD_EADDRNOTAVAIL 49     /* Can't assign requested address */
#define BSD_ENETDOWN      50     /* Network is down */
#define BSD_ENETUNREACH   51     /* Network is unreachable */
#define BSD_ENETRESET     52     /* Network dropped connection on reset */
#define BSD_ECONNABORTED  53     /* Software caused connection abort */
#define BSD_ECONNRESET    54     /* Connection reset by peer */
#define BSD_ENOBUFS       55     /* No buffer space available */
#define BSD_EISCONN       56     /* Socket is already connected */
#define BSD_ENOTCONN      57     /* Socket is not connected */
#define BSD_ESHUTDOWN     58     /* Can't send after socket shutdown */
#define BSD_ETOOMANYREFS  59     /* Too many references: can't splice */
#define BSD_ETIMEDOUT     60     /* Operation timed out */
#define BSD_ECONNREFUSED  61     /* Connection refused */
#define BSD_ELOOP         62     /* Too many levels of symbolic links */
#define BSD_ENAMETOOLONG  63     /* File name too long */
#define BSD_EHOSTDOWN     64     /* Host is down */
#define BSD_EHOSTUNREACH  65     /* No route to host */
#define BSD_ENOTEMPTY     66     /* Directory not empty */
#define BSD_EPROCLIM      67     /* Too many processes */
#define BSD_EUSERS        68     /* Too many users */
#define BSD_EDQUOT        69     /* Disc quota exceeded */
#define BSD_ESTALE        70     /* Stale NFS file handle */
#define BSD_EREMOTE       71     /* Too many levels of remote in path */
#define BSD_EBADRPC       72     /* RPC struct is bad */
#define BSD_ERPCMISMATCH  73     /* RPC version wrong */
#define BSD_EPROGUNAVAIL  74     /* RPC prog. not avail */
#define BSD_EPROGMISMATCH 75     /* Program version wrong */
#define BSD_EPROCUNAVAIL  76     /* Bad procedure for program */
#define BSD_ENOLCK        77     /* No locks available */
#define BSD_ENOSYS        78     /* Function not implemented */
#define BSD_EFTYPE        79     /* Inappropriate file type or format */
#define BSD_EAUTH         80     /* Authentication error */
#define BSD_ENEEDAUTH     81     /* Need authenticator */
#define BSD_ELAST         81     /* Must be equal largest errno */

#endif /* !(_SPARC64_BSDERRNO_H) */
