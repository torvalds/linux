/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 *
 * $FreeBSD$
 */

#ifndef _SHA_H_
#define _SHA_H_		1

#include <sys/cdefs.h>
#include <sys/types.h>		/* XXX switch to machine/ansi.h and __ types */

#define	SHA_CBLOCK	64
#define	SHA_LBLOCK	16
#define	SHA_BLOCK	16
#define	SHA_LAST_BLOCK  56
#define	SHA_LENGTH_BLOCK 8
#define	SHA_DIGEST_LENGTH 20

typedef struct SHAstate_st {
	u_int32_t h0, h1, h2, h3, h4;
	u_int32_t Nl, Nh;
	u_int32_t data[SHA_LBLOCK];
	int num;
} SHA_CTX;
#define	SHA1_CTX	SHA_CTX

__BEGIN_DECLS

/* Ensure libmd symbols do not clash with libcrypto */

#ifndef SHA_Init
#define SHA_Init	_libmd_SHA_Init
#endif
#ifndef SHA_Update
#define SHA_Update	_libmd_SHA_Update
#endif
#ifndef SHA_Final
#define SHA_Final	_libmd_SHA_Final
#endif
#ifndef SHA_End
#define SHA_End		_libmd_SHA_End
#endif
#ifndef SHA_Fd
#define SHA_Fd		_libmd_SHA_Fd
#endif
#ifndef SHA_FdChunk
#define SHA_FdChunk	_libmd_SHA_FdChunk
#endif
#ifndef SHA_File
#define SHA_File	_libmd_SHA_File
#endif
#ifndef SHA_FileChunk
#define SHA_FileChunk	_libmd_SHA_FileChunk
#endif
#ifndef SHA_Data
#define SHA_Data	_libmd_SHA_Data
#endif

#ifndef SHA_Transform
#define SHA_Transform	_libmd_SHA_Transform
#endif
#ifndef SHA_version
#define SHA_version	_libmd_SHA_version
#endif
#ifndef sha_block
#define sha_block	_libmd_sha_block
#endif

#ifndef SHA1_Init
#define SHA1_Init	_libmd_SHA1_Init
#endif
#ifndef SHA1_Update
#define SHA1_Update	_libmd_SHA1_Update
#endif
#ifndef SHA1_Final
#define SHA1_Final	_libmd_SHA1_Final
#endif
#ifndef SHA1_End
#define SHA1_End	_libmd_SHA1_End
#endif
#ifndef SHA1_Fd
#define SHA1_Fd		_libmd_SHA1_Fd
#endif
#ifndef SHA1_FdChunk
#define SHA1_FdChunk	_libmd_SHA1_FdChunk
#endif
#ifndef SHA1_File
#define SHA1_File	_libmd_SHA1_File
#endif
#ifndef SHA1_FileChunk
#define SHA1_FileChunk	_libmd_SHA1_FileChunk
#endif
#ifndef SHA1_Data
#define SHA1_Data	_libmd_SHA1_Data
#endif

#ifndef SHA1_Transform
#define SHA1_Transform	_libmd_SHA1_Transform
#endif
#ifndef SHA1_version
#define SHA1_version	_libmd_SHA1_version
#endif
#ifndef sha1_block
#define sha1_block	_libmd_sha1_block
#endif

void	SHA_Init(SHA_CTX *c);
void	SHA_Update(SHA_CTX *c, const void *data, size_t len);
void	SHA_Final(unsigned char *md, SHA_CTX *c);
char   *SHA_End(SHA_CTX *, char *);
char   *SHA_Fd(int, char *);
char   *SHA_FdChunk(int, char *, off_t, off_t);
char   *SHA_File(const char *, char *);
char   *SHA_FileChunk(const char *, char *, off_t, off_t);
char   *SHA_Data(const void *, unsigned int, char *);

void	SHA1_Init(SHA_CTX *c);
void	SHA1_Update(SHA_CTX *c, const void *data, size_t len);
void	SHA1_Final(unsigned char *md, SHA_CTX *c);
char   *SHA1_End(SHA_CTX *, char *);
char   *SHA1_Fd(int, char *);
char   *SHA1_FdChunk(int, char *, off_t, off_t);
char   *SHA1_File(const char *, char *);
char   *SHA1_FileChunk(const char *, char *, off_t, off_t);
char   *SHA1_Data(const void *, unsigned int, char *);
__END_DECLS

#endif /* !_SHA_H_ */
