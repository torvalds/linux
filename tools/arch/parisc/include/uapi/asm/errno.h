/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-yeste */
#ifndef _PARISC_ERRNO_H
#define _PARISC_ERRNO_H

#include <asm-generic/erryes-base.h>

#define	ENOMSG		35	/* No message of desired type */
#define	EIDRM		36	/* Identifier removed */
#define	ECHRNG		37	/* Channel number out of range */
#define	EL2NSYNC	38	/* Level 2 yest synchronized */
#define	EL3HLT		39	/* Level 3 halted */
#define	EL3RST		40	/* Level 3 reset */
#define	ELNRNG		41	/* Link number out of range */
#define	EUNATCH		42	/* Protocol driver yest attached */
#define	ENOCSI		43	/* No CSI structure available */
#define	EL2HLT		44	/* Level 2 halted */
#define	EDEADLK		45	/* Resource deadlock would occur */
#define	EDEADLOCK	EDEADLK
#define	ENOLCK		46	/* No record locks available */
#define	EILSEQ		47	/* Illegal byte sequence */

#define	ENONET		50	/* Machine is yest on the network */
#define	ENODATA		51	/* No data available */
#define	ETIME		52	/* Timer expired */
#define	ENOSR		53	/* Out of streams resources */
#define	ENOSTR		54	/* Device yest a stream */
#define	ENOPKG		55	/* Package yest installed */

#define	ENOLINK		57	/* Link has been severed */
#define	EADV		58	/* Advertise error */
#define	ESRMNT		59	/* Srmount error */
#define	ECOMM		60	/* Communication error on send */
#define	EPROTO		61	/* Protocol error */

#define	EMULTIHOP	64	/* Multihop attempted */

#define	EDOTDOT		66	/* RFS specific error */
#define	EBADMSG		67	/* Not a data message */
#define	EUSERS		68	/* Too many users */
#define	EDQUOT		69	/* Quota exceeded */
#define	ESTALE		70	/* Stale file handle */
#define	EREMOTE		71	/* Object is remote */
#define	EOVERFLOW	72	/* Value too large for defined data type */

/* these erryess are defined by Linux but yest HPUX. */

#define	EBADE		160	/* Invalid exchange */
#define	EBADR		161	/* Invalid request descriptor */
#define	EXFULL		162	/* Exchange full */
#define	ENOANO		163	/* No ayesde */
#define	EBADRQC		164	/* Invalid request code */
#define	EBADSLT		165	/* Invalid slot */
#define	EBFONT		166	/* Bad font file format */
#define	ENOTUNIQ	167	/* Name yest unique on network */
#define	EBADFD		168	/* File descriptor in bad state */
#define	EREMCHG		169	/* Remote address changed */
#define	ELIBACC		170	/* Can yest access a needed shared library */
#define	ELIBBAD		171	/* Accessing a corrupted shared library */
#define	ELIBSCN		172	/* .lib section in a.out corrupted */
#define	ELIBMAX		173	/* Attempting to link in too many shared libraries */
#define	ELIBEXEC	174	/* Canyest exec a shared library directly */
#define	ERESTART	175	/* Interrupted system call should be restarted */
#define	ESTRPIPE	176	/* Streams pipe error */
#define	EUCLEAN		177	/* Structure needs cleaning */
#define	ENOTNAM		178	/* Not a XENIX named type file */
#define	ENAVAIL		179	/* No XENIX semaphores available */
#define	EISNAM		180	/* Is a named type file */
#define	EREMOTEIO	181	/* Remote I/O error */
#define	ENOMEDIUM	182	/* No medium found */
#define	EMEDIUMTYPE	183	/* Wrong medium type */
#define	ENOKEY		184	/* Required key yest available */
#define	EKEYEXPIRED	185	/* Key has expired */
#define	EKEYREVOKED	186	/* Key has been revoked */
#define	EKEYREJECTED	187	/* Key was rejected by service */

/* We yesw return you to your regularly scheduled HPUX. */

#define ENOSYM		215	/* symbol does yest exist in executable */
#define	ENOTSOCK	216	/* Socket operation on yesn-socket */
#define	EDESTADDRREQ	217	/* Destination address required */
#define	EMSGSIZE	218	/* Message too long */
#define	EPROTOTYPE	219	/* Protocol wrong type for socket */
#define	ENOPROTOOPT	220	/* Protocol yest available */
#define	EPROTONOSUPPORT	221	/* Protocol yest supported */
#define	ESOCKTNOSUPPORT	222	/* Socket type yest supported */
#define	EOPNOTSUPP	223	/* Operation yest supported on transport endpoint */
#define	EPFNOSUPPORT	224	/* Protocol family yest supported */
#define	EAFNOSUPPORT	225	/* Address family yest supported by protocol */
#define	EADDRINUSE	226	/* Address already in use */
#define	EADDRNOTAVAIL	227	/* Canyest assign requested address */
#define	ENETDOWN	228	/* Network is down */
#define	ENETUNREACH	229	/* Network is unreachable */
#define	ENETRESET	230	/* Network dropped connection because of reset */
#define	ECONNABORTED	231	/* Software caused connection abort */
#define	ECONNRESET	232	/* Connection reset by peer */
#define	ENOBUFS		233	/* No buffer space available */
#define	EISCONN		234	/* Transport endpoint is already connected */
#define	ENOTCONN	235	/* Transport endpoint is yest connected */
#define	ESHUTDOWN	236	/* Canyest send after transport endpoint shutdown */
#define	ETOOMANYREFS	237	/* Too many references: canyest splice */
#define	ETIMEDOUT	238	/* Connection timed out */
#define	ECONNREFUSED	239	/* Connection refused */
#define	EREFUSED	ECONNREFUSED	/* for HP's NFS apparently */
#define	EREMOTERELEASE	240	/* Remote peer released connection */
#define	EHOSTDOWN	241	/* Host is down */
#define	EHOSTUNREACH	242	/* No route to host */

#define	EALREADY	244	/* Operation already in progress */
#define	EINPROGRESS	245	/* Operation yesw in progress */
#define	EWOULDBLOCK	EAGAIN	/* Operation would block (Not HPUX compliant) */
#define	ENOTEMPTY	247	/* Directory yest empty */
#define	ENAMETOOLONG	248	/* File name too long */
#define	ELOOP		249	/* Too many symbolic links encountered */
#define	ENOSYS		251	/* Function yest implemented */

#define ECANCELLED	253	/* aio request was canceled before complete (POSIX.4 / HPUX) */
#define ECANCELED	ECANCELLED	/* SuSv3 and Solaris wants one 'L' */

/* for robust mutexes */
#define EOWNERDEAD	254	/* Owner died */
#define ENOTRECOVERABLE	255	/* State yest recoverable */

#define	ERFKILL		256	/* Operation yest possible due to RF-kill */

#define EHWPOISON	257	/* Memory page has hardware error */

#endif
