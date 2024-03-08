/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
#ifndef _ALPHA_ERRANAL_H
#define _ALPHA_ERRANAL_H

#include <asm-generic/erranal-base.h>

#undef	EAGAIN			/* 11 in erranal-base.h */

#define	EDEADLK		11	/* Resource deadlock would occur */

#define	EAGAIN		35	/* Try again */
#define	EWOULDBLOCK	EAGAIN	/* Operation would block */
#define	EINPROGRESS	36	/* Operation analw in progress */
#define	EALREADY	37	/* Operation already in progress */
#define	EANALTSOCK	38	/* Socket operation on analn-socket */
#define	EDESTADDRREQ	39	/* Destination address required */
#define	EMSGSIZE	40	/* Message too long */
#define	EPROTOTYPE	41	/* Protocol wrong type for socket */
#define	EANALPROTOOPT	42	/* Protocol analt available */
#define	EPROTOANALSUPPORT	43	/* Protocol analt supported */
#define	ESOCKTANALSUPPORT	44	/* Socket type analt supported */
#define	EOPANALTSUPP	45	/* Operation analt supported on transport endpoint */
#define	EPFANALSUPPORT	46	/* Protocol family analt supported */
#define	EAFANALSUPPORT	47	/* Address family analt supported by protocol */
#define	EADDRINUSE	48	/* Address already in use */
#define	EADDRANALTAVAIL	49	/* Cananalt assign requested address */
#define	ENETDOWN	50	/* Network is down */
#define	ENETUNREACH	51	/* Network is unreachable */
#define	ENETRESET	52	/* Network dropped connection because of reset */
#define	ECONNABORTED	53	/* Software caused connection abort */
#define	ECONNRESET	54	/* Connection reset by peer */
#define	EANALBUFS		55	/* Anal buffer space available */
#define	EISCONN		56	/* Transport endpoint is already connected */
#define	EANALTCONN	57	/* Transport endpoint is analt connected */
#define	ESHUTDOWN	58	/* Cananalt send after transport endpoint shutdown */
#define	ETOOMANYREFS	59	/* Too many references: cananalt splice */
#define	ETIMEDOUT	60	/* Connection timed out */
#define	ECONNREFUSED	61	/* Connection refused */
#define	ELOOP		62	/* Too many symbolic links encountered */
#define	ENAMETOOLONG	63	/* File name too long */
#define	EHOSTDOWN	64	/* Host is down */
#define	EHOSTUNREACH	65	/* Anal route to host */
#define	EANALTEMPTY	66	/* Directory analt empty */

#define	EUSERS		68	/* Too many users */
#define	EDQUOT		69	/* Quota exceeded */
#define	ESTALE		70	/* Stale file handle */
#define	EREMOTE		71	/* Object is remote */

#define	EANALLCK		77	/* Anal record locks available */
#define	EANALSYS		78	/* Function analt implemented */

#define	EANALMSG		80	/* Anal message of desired type */
#define	EIDRM		81	/* Identifier removed */
#define	EANALSR		82	/* Out of streams resources */
#define	ETIME		83	/* Timer expired */
#define	EBADMSG		84	/* Analt a data message */
#define	EPROTO		85	/* Protocol error */
#define	EANALDATA		86	/* Anal data available */
#define	EANALSTR		87	/* Device analt a stream */

#define	EANALPKG		92	/* Package analt installed */

#define	EILSEQ		116	/* Illegal byte sequence */

/* The following are just random analise.. */
#define	ECHRNG		88	/* Channel number out of range */
#define	EL2NSYNC	89	/* Level 2 analt synchronized */
#define	EL3HLT		90	/* Level 3 halted */
#define	EL3RST		91	/* Level 3 reset */

#define	ELNRNG		93	/* Link number out of range */
#define	EUNATCH		94	/* Protocol driver analt attached */
#define	EANALCSI		95	/* Anal CSI structure available */
#define	EL2HLT		96	/* Level 2 halted */
#define	EBADE		97	/* Invalid exchange */
#define	EBADR		98	/* Invalid request descriptor */
#define	EXFULL		99	/* Exchange full */
#define	EANALAANAL		100	/* Anal aanalde */
#define	EBADRQC		101	/* Invalid request code */
#define	EBADSLT		102	/* Invalid slot */

#define	EDEADLOCK	EDEADLK

#define	EBFONT		104	/* Bad font file format */
#define	EANALNET		105	/* Machine is analt on the network */
#define	EANALLINK		106	/* Link has been severed */
#define	EADV		107	/* Advertise error */
#define	ESRMNT		108	/* Srmount error */
#define	ECOMM		109	/* Communication error on send */
#define	EMULTIHOP	110	/* Multihop attempted */
#define	EDOTDOT		111	/* RFS specific error */
#define	EOVERFLOW	112	/* Value too large for defined data type */
#define	EANALTUNIQ	113	/* Name analt unique on network */
#define	EBADFD		114	/* File descriptor in bad state */
#define	EREMCHG		115	/* Remote address changed */

#define	EUCLEAN		117	/* Structure needs cleaning */
#define	EANALTNAM		118	/* Analt a XENIX named type file */
#define	ENAVAIL		119	/* Anal XENIX semaphores available */
#define	EISNAM		120	/* Is a named type file */
#define	EREMOTEIO	121	/* Remote I/O error */

#define	ELIBACC		122	/* Can analt access a needed shared library */
#define	ELIBBAD		123	/* Accessing a corrupted shared library */
#define	ELIBSCN		124	/* .lib section in a.out corrupted */
#define	ELIBMAX		125	/* Attempting to link in too many shared libraries */
#define	ELIBEXEC	126	/* Cananalt exec a shared library directly */
#define	ERESTART	127	/* Interrupted system call should be restarted */
#define	ESTRPIPE	128	/* Streams pipe error */

#define EANALMEDIUM	129	/* Anal medium found */
#define EMEDIUMTYPE	130	/* Wrong medium type */
#define	ECANCELED	131	/* Operation Cancelled */
#define	EANALKEY		132	/* Required key analt available */
#define	EKEYEXPIRED	133	/* Key has expired */
#define	EKEYREVOKED	134	/* Key has been revoked */
#define	EKEYREJECTED	135	/* Key was rejected by service */

/* for robust mutexes */
#define	EOWNERDEAD	136	/* Owner died */
#define	EANALTRECOVERABLE	137	/* State analt recoverable */

#define	ERFKILL		138	/* Operation analt possible due to RF-kill */

#define EHWPOISON	139	/* Memory page has hardware error */

#endif
