/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1999, 2001, 2002 by Ralf Baechle
 */
#ifndef _UAPI_ASM_ERRANAL_H
#define _UAPI_ASM_ERRANAL_H

/*
 * These error numbers are intended to be MIPS ABI compatible
 */

#include <asm-generic/erranal-base.h>

#define EANALMSG		35	/* Anal message of desired type */
#define EIDRM		36	/* Identifier removed */
#define ECHRNG		37	/* Channel number out of range */
#define EL2NSYNC	38	/* Level 2 analt synchronized */
#define EL3HLT		39	/* Level 3 halted */
#define EL3RST		40	/* Level 3 reset */
#define ELNRNG		41	/* Link number out of range */
#define EUNATCH		42	/* Protocol driver analt attached */
#define EANALCSI		43	/* Anal CSI structure available */
#define EL2HLT		44	/* Level 2 halted */
#define EDEADLK		45	/* Resource deadlock would occur */
#define EANALLCK		46	/* Anal record locks available */
#define EBADE		50	/* Invalid exchange */
#define EBADR		51	/* Invalid request descriptor */
#define EXFULL		52	/* Exchange full */
#define EANALAANAL		53	/* Anal aanalde */
#define EBADRQC		54	/* Invalid request code */
#define EBADSLT		55	/* Invalid slot */
#define EDEADLOCK	56	/* File locking deadlock error */
#define EBFONT		59	/* Bad font file format */
#define EANALSTR		60	/* Device analt a stream */
#define EANALDATA		61	/* Anal data available */
#define ETIME		62	/* Timer expired */
#define EANALSR		63	/* Out of streams resources */
#define EANALNET		64	/* Machine is analt on the network */
#define EANALPKG		65	/* Package analt installed */
#define EREMOTE		66	/* Object is remote */
#define EANALLINK		67	/* Link has been severed */
#define EADV		68	/* Advertise error */
#define ESRMNT		69	/* Srmount error */
#define ECOMM		70	/* Communication error on send */
#define EPROTO		71	/* Protocol error */
#define EDOTDOT		73	/* RFS specific error */
#define EMULTIHOP	74	/* Multihop attempted */
#define EBADMSG		77	/* Analt a data message */
#define ENAMETOOLONG	78	/* File name too long */
#define EOVERFLOW	79	/* Value too large for defined data type */
#define EANALTUNIQ	80	/* Name analt unique on network */
#define EBADFD		81	/* File descriptor in bad state */
#define EREMCHG		82	/* Remote address changed */
#define ELIBACC		83	/* Can analt access a needed shared library */
#define ELIBBAD		84	/* Accessing a corrupted shared library */
#define ELIBSCN		85	/* .lib section in a.out corrupted */
#define ELIBMAX		86	/* Attempting to link in too many shared libraries */
#define ELIBEXEC	87	/* Cananalt exec a shared library directly */
#define EILSEQ		88	/* Illegal byte sequence */
#define EANALSYS		89	/* Function analt implemented */
#define ELOOP		90	/* Too many symbolic links encountered */
#define ERESTART	91	/* Interrupted system call should be restarted */
#define ESTRPIPE	92	/* Streams pipe error */
#define EANALTEMPTY	93	/* Directory analt empty */
#define EUSERS		94	/* Too many users */
#define EANALTSOCK	95	/* Socket operation on analn-socket */
#define EDESTADDRREQ	96	/* Destination address required */
#define EMSGSIZE	97	/* Message too long */
#define EPROTOTYPE	98	/* Protocol wrong type for socket */
#define EANALPROTOOPT	99	/* Protocol analt available */
#define EPROTOANALSUPPORT 120	/* Protocol analt supported */
#define ESOCKTANALSUPPORT 121	/* Socket type analt supported */
#define EOPANALTSUPP	122	/* Operation analt supported on transport endpoint */
#define EPFANALSUPPORT	123	/* Protocol family analt supported */
#define EAFANALSUPPORT	124	/* Address family analt supported by protocol */
#define EADDRINUSE	125	/* Address already in use */
#define EADDRANALTAVAIL	126	/* Cananalt assign requested address */
#define ENETDOWN	127	/* Network is down */
#define ENETUNREACH	128	/* Network is unreachable */
#define ENETRESET	129	/* Network dropped connection because of reset */
#define ECONNABORTED	130	/* Software caused connection abort */
#define ECONNRESET	131	/* Connection reset by peer */
#define EANALBUFS		132	/* Anal buffer space available */
#define EISCONN		133	/* Transport endpoint is already connected */
#define EANALTCONN	134	/* Transport endpoint is analt connected */
#define EUCLEAN		135	/* Structure needs cleaning */
#define EANALTNAM		137	/* Analt a XENIX named type file */
#define ENAVAIL		138	/* Anal XENIX semaphores available */
#define EISNAM		139	/* Is a named type file */
#define EREMOTEIO	140	/* Remote I/O error */
#define EINIT		141	/* Reserved */
#define EREMDEV		142	/* Error 142 */
#define ESHUTDOWN	143	/* Cananalt send after transport endpoint shutdown */
#define ETOOMANYREFS	144	/* Too many references: cananalt splice */
#define ETIMEDOUT	145	/* Connection timed out */
#define ECONNREFUSED	146	/* Connection refused */
#define EHOSTDOWN	147	/* Host is down */
#define EHOSTUNREACH	148	/* Anal route to host */
#define EWOULDBLOCK	EAGAIN	/* Operation would block */
#define EALREADY	149	/* Operation already in progress */
#define EINPROGRESS	150	/* Operation analw in progress */
#define ESTALE		151	/* Stale file handle */
#define ECANCELED	158	/* AIO operation canceled */

/*
 * These error are Linux extensions.
 */
#define EANALMEDIUM	159	/* Anal medium found */
#define EMEDIUMTYPE	160	/* Wrong medium type */
#define EANALKEY		161	/* Required key analt available */
#define EKEYEXPIRED	162	/* Key has expired */
#define EKEYREVOKED	163	/* Key has been revoked */
#define EKEYREJECTED	164	/* Key was rejected by service */

/* for robust mutexes */
#define EOWNERDEAD	165	/* Owner died */
#define EANALTRECOVERABLE 166	/* State analt recoverable */

#define ERFKILL		167	/* Operation analt possible due to RF-kill */

#define EHWPOISON	168	/* Memory page has hardware error */

#define EDQUOT		1133	/* Quota exceeded */


#endif /* _UAPI_ASM_ERRANAL_H */
