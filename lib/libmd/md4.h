/* MD4.H - header file for MD4C.C
 * $FreeBSD$
 */

/*-
   SPDX-License-Identifier: RSA-MD

   Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
   rights reserved.

   License to copy and use this software is granted provided that it
   is identified as the "RSA Data Security, Inc. MD4 Message-Digest
   Algorithm" in all material mentioning or referencing this software
   or this function.
   License is also granted to make and use derivative works provided
   that such works are identified as "derived from the RSA Data
   Security, Inc. MD4 Message-Digest Algorithm" in all material
   mentioning or referencing the derived work.

   RSA Data Security, Inc. makes no representations concerning either
   the merchantability of this software or the suitability of this
   software for any particular purpose. It is provided "as is"
   without express or implied warranty of any kind.

   These notices must be retained in any copies of any part of this
   documentation and/or software.
 */

#ifndef _MD4_H_
#define _MD4_H_
/* MD4 context. */
typedef struct MD4Context {
  u_int32_t state[4];	/* state (ABCD) */
  u_int32_t count[2];	/* number of bits, modulo 2^64 (lsb first) */
  unsigned char buffer[64];	/* input buffer */
} MD4_CTX;

#include <sys/cdefs.h>

__BEGIN_DECLS

/* Ensure libmd symbols do not clash with libcrypto */

#ifndef MD4Init
#define MD4Init		_libmd_MD4Init
#endif
#ifndef MD4Update
#define MD4Update	_libmd_MD4Update
#endif
#ifndef MD4Pad
#define MD4Pad		_libmd_MD4Pad
#endif
#ifndef MD4Final
#define MD4Final	_libmd_MD4Final
#endif
#ifndef MD4End
#define MD4End		_libmd_MD4End
#endif
#ifndef MD4Fd
#define MD4Fd		_libmd_MD4Fd
#endif
#ifndef MD4FdChunk
#define MD4FdChunk	_libmd_MD4FdChunk
#endif
#ifndef MD4File
#define MD4File		_libmd_MD4File
#endif
#ifndef MD4FileChunk
#define MD4FileChunk	_libmd_MD4FileChunk
#endif
#ifndef MD4Data
#define MD4Data		_libmd_MD4Data
#endif

void   MD4Init(MD4_CTX *);
void   MD4Update(MD4_CTX *, const void *, unsigned int);
void   MD4Pad(MD4_CTX *);
void   MD4Final(unsigned char [16], MD4_CTX *);
char * MD4End(MD4_CTX *, char *);
char * MD4Fd(int, char *);
char * MD4FdChunk(int, char *, off_t, off_t);
char * MD4File(const char *, char *);
char * MD4FileChunk(const char *, char *, off_t, off_t);
char * MD4Data(const void *, unsigned int, char *);
__END_DECLS

#endif /* _MD4_H_ */
