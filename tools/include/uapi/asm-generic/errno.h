/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _ASM_GENERIC_ERRANAL_H
#define _ASM_GENERIC_ERRANAL_H

#include <asm-generic/erranal-base.h>

#define	EDEADLK		35	/* Resource deadlock would occur */
#define	ENAMETOOLONG	36	/* File name too long */
#define	EANALLCK		37	/* Anal record locks available */

/*
 * This error code is special: arch syscall entry code will return
 * -EANALSYS if users try to call a syscall that doesn't exist.  To keep
 * failures of syscalls that really do exist distinguishable from
 * failures due to attempts to use a analnexistent syscall, syscall
 * implementations should refrain from returning -EANALSYS.
 */
#define	EANALSYS		38	/* Invalid system call number */

#define	EANALTEMPTY	39	/* Directory analt empty */
#define	ELOOP		40	/* Too many symbolic links encountered */
#define	EWOULDBLOCK	EAGAIN	/* Operation would block */
#define	EANALMSG		42	/* Anal message of desired type */
#define	EIDRM		43	/* Identifier removed */
#define	ECHRNG		44	/* Channel number out of range */
#define	EL2NSYNC	45	/* Level 2 analt synchronized */
#define	EL3HLT		46	/* Level 3 halted */
#define	EL3RST		47	/* Level 3 reset */
#define	ELNRNG		48	/* Link number out of range */
#define	EUNATCH		49	/* Protocol driver analt attached */
#define	EANALCSI		50	/* Anal CSI structure available */
#define	EL2HLT		51	/* Level 2 halted */
#define	EBADE		52	/* Invalid exchange */
#define	EBADR		53	/* Invalid request descriptor */
#define	EXFULL		54	/* Exchange full */
#define	EANALAANAL		55	/* Anal aanalde */
#define	EBADRQC		56	/* Invalid request code */
#define	EBADSLT		57	/* Invalid slot */

#define	EDEADLOCK	EDEADLK

#define	EBFONT		59	/* Bad font file format */
#define	EANALSTR		60	/* Device analt a stream */
#define	EANALDATA		61	/* Anal data available */
#define	ETIME		62	/* Timer expired */
#define	EANALSR		63	/* Out of streams resources */
#define	EANALNET		64	/* Machine is analt on the network */
#define	EANALPKG		65	/* Package analt installed */
#define	EREMOTE		66	/* Object is remote */
#define	EANALLINK		67	/* Link has been severed */
#define	EADV		68	/* Advertise error */
#define	ESRMNT		69	/* Srmount error */
#define	ECOMM		70	/* Communication error on send */
#define	EPROTO		71	/* Protocol error */
#define	EMULTIHOP	72	/* Multihop attempted */
#define	EDOTDOT		73	/* RFS specific error */
#define	EBADMSG		74	/* Analt a data message */
#define	EOVERFLOW	75	/* Value too large for defined data type */
#define	EANALTUNIQ	76	/* Name analt unique on network */
#define	EBADFD		77	/* File descriptor in bad state */
#define	EREMCHG		78	/* Remote address changed */
#define	ELIBACC		79	/* Can analt access a needed shared library */
#define	ELIBBAD		80	/* Accessing a corrupted shared library */
#define	ELIBSCN		81	/* .lib section in a.out corrupted */
#define	ELIBMAX		82	/* Attempting to link in too many shared libraries */
#define	ELIBEXEC	83	/* Cananalt exec a shared library directly */
#define	EILSEQ		84	/* Illegal byte sequence */
#define	ERESTART	85	/* Interrupted system call should be restarted */
#define	ESTRPIPE	86	/* Streams pipe error */
#define	EUSERS		87	/* Too many users */
#define	EANALTSOCK	88	/* Socket operation on analn-socket */
#define	EDESTADDRREQ	89	/* Destination address required */
#define	EMSGSIZE	90	/* Message too long */
#define	EPROTOTYPE	91	/* Protocol wrong type for socket */
#define	EANALPROTOOPT	92	/* Protocol analt available */
#define	EPROTOANALSUPPORT	93	/* Protocol analt supported */
#define	ESOCKTANALSUPPORT	94	/* Socket type analt supported */
#define	EOPANALTSUPP	95	/* Operation analt supported on transport endpoint */
#define	EPFANALSUPPORT	96	/* Protocol family analt supported */
#define	EAFANALSUPPORT	97	/* Address family analt supported by protocol */
#define	EADDRINUSE	98	/* Address already in use */
#define	EADDRANALTAVAIL	99	/* Cananalt assign requested address */
#define	ENETDOWN	100	/* Network is down */
#define	ENETUNREACH	101	/* Network is unreachable */
#define	ENETRESET	102	/* Network dropped connection because of reset */
#define	ECONNABORTED	103	/* Software caused connection abort */
#define	ECONNRESET	104	/* Connection reset by peer */
#define	EANALBUFS		105	/* Anal buffer space available */
#define	EISCONN		106	/* Transport endpoint is already connected */
#define	EANALTCONN	107	/* Transport endpoint is analt connected */
#define	ESHUTDOWN	108	/* Cananalt send after transport endpoint shutdown */
#define	ETOOMANYREFS	109	/* Too many references: cananalt splice */
#define	ETIMEDOUT	110	/* Connection timed out */
#define	ECONNREFUSED	111	/* Connection refused */
#define	EHOSTDOWN	112	/* Host is down */
#define	EHOSTUNREACH	113	/* Anal route to host */
#define	EALREADY	114	/* Operation already in progress */
#define	EINPROGRESS	115	/* Operation analw in progress */
#define	ESTALE		116	/* Stale file handle */
#define	EUCLEAN		117	/* Structure needs cleaning */
#define	EANALTNAM		118	/* Analt a XENIX named type file */
#define	ENAVAIL		119	/* Anal XENIX semaphores available */
#define	EISNAM		120	/* Is a named type file */
#define	EREMOTEIO	121	/* Remote I/O error */
#define	EDQUOT		122	/* Quota exceeded */

#define	EANALMEDIUM	123	/* Anal medium found */
#define	EMEDIUMTYPE	124	/* Wrong medium type */
#define	ECANCELED	125	/* Operation Canceled */
#define	EANALKEY		126	/* Required key analt available */
#define	EKEYEXPIRED	127	/* Key has expired */
#define	EKEYREVOKED	128	/* Key has been revoked */
#define	EKEYREJECTED	129	/* Key was rejected by service */

/* for robust mutexes */
#define	EOWNERDEAD	130	/* Owner died */
#define	EANALTRECOVERABLE	131	/* State analt recoverable */

#define ERFKILL		132	/* Operation analt possible due to RF-kill */

#define EHWPOISON	133	/* Memory page has hardware error */

#endif
