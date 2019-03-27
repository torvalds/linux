/*-
 * Copyright (c) 2015 Allan Jude <allanjude@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 * $FreeBSD$
 */

#ifndef _SHA512T_H_
#define _SHA512T_H_

#include "sha512.h"

#ifndef _KERNEL
#include <sys/types.h>
#endif

#define SHA512_224_DIGEST_LENGTH	28
#define SHA512_224_DIGEST_STRING_LENGTH	(SHA512_224_DIGEST_LENGTH * 2 + 1)
#define SHA512_256_DIGEST_LENGTH	32
#define SHA512_256_DIGEST_STRING_LENGTH	(SHA512_256_DIGEST_LENGTH * 2 + 1)

__BEGIN_DECLS

/* Ensure libmd symbols do not clash with libcrypto */
#ifndef SHA512_224_Init
#define SHA512_224_Init		_libmd_SHA512_224_Init
#endif
#ifndef SHA512_224_Update
#define SHA512_224_Update	_libmd_SHA512_224_Update
#endif
#ifndef SHA512_224_Final
#define SHA512_224_Final	_libmd_SHA512_224_Final
#endif
#ifndef SHA512_224_End
#define SHA512_224_End		_libmd_SHA512_224_End
#endif
#ifndef SHA512_224_Fd
#define SHA512_224_Fd		_libmd_SHA512_224_Fd
#endif
#ifndef SHA512_224_FdChunk
#define SHA512_224_FdChunk	_libmd_SHA512_224_FdChunk
#endif
#ifndef SHA512_224_File
#define SHA512_224_File		_libmd_SHA512_224_File
#endif
#ifndef SHA512_224_FileChunk
#define SHA512_224_FileChunk	_libmd_SHA512_224_FileChunk
#endif
#ifndef SHA512_224_Data
#define SHA512_224_Data		_libmd_SHA512_224_Data
#endif

#ifndef SHA512_224_Transform
#define SHA512_224_Transform	_libmd_SHA512_224_Transform
#endif
#ifndef SHA512_224_version
#define SHA512_224_version	_libmd_SHA512_224_version
#endif

#ifndef SHA512_256_Init
#define SHA512_256_Init		_libmd_SHA512_256_Init
#endif
#ifndef SHA512_256_Update
#define SHA512_256_Update	_libmd_SHA512_256_Update
#endif
#ifndef SHA512_256_Final
#define SHA512_256_Final	_libmd_SHA512_256_Final
#endif
#ifndef SHA512_256_End
#define SHA512_256_End		_libmd_SHA512_256_End
#endif
#ifndef SHA512_256_Fd
#define SHA512_256_Fd		_libmd_SHA512_256_Fd
#endif
#ifndef SHA512_256_FdChunk
#define SHA512_256_FdChunk	_libmd_SHA512_256_FdChunk
#endif
#ifndef SHA512_256_File
#define SHA512_256_File		_libmd_SHA512_256_File
#endif
#ifndef SHA512_256_FileChunk
#define SHA512_256_FileChunk	_libmd_SHA512_256_FileChunk
#endif
#ifndef SHA512_256_Data
#define SHA512_256_Data		_libmd_SHA512_256_Data
#endif

#ifndef SHA512_256_Transform
#define SHA512_256_Transform	_libmd_SHA512_256_Transform
#endif
#ifndef SHA512_256_version
#define SHA512_256_version	_libmd_SHA512_256_version
#endif

void	SHA512_224_Init(SHA512_CTX *);
void	SHA512_224_Update(SHA512_CTX *, const void *, size_t);
void	SHA512_224_Final(unsigned char [__min_size(SHA512_224_DIGEST_LENGTH)],
    SHA512_CTX *);
#ifndef _KERNEL
char   *SHA512_224_End(SHA512_CTX *, char *);
char   *SHA512_224_Data(const void *, unsigned int, char *);
char   *SHA512_224_Fd(int, char *);
char   *SHA512_224_FdChunk(int, char *, off_t, off_t);
char   *SHA512_224_File(const char *, char *);
char   *SHA512_224_FileChunk(const char *, char *, off_t, off_t);
#endif
void	SHA512_256_Init(SHA512_CTX *);
void	SHA512_256_Update(SHA512_CTX *, const void *, size_t);
void	SHA512_256_Final(unsigned char [__min_size(SHA512_256_DIGEST_LENGTH)],
    SHA512_CTX *);
#ifndef _KERNEL
char   *SHA512_256_End(SHA512_CTX *, char *);
char   *SHA512_256_Data(const void *, unsigned int, char *);
char   *SHA512_256_Fd(int, char *);
char   *SHA512_256_FdChunk(int, char *, off_t, off_t);
char   *SHA512_256_File(const char *, char *);
char   *SHA512_256_FileChunk(const char *, char *, off_t, off_t);
#endif

__END_DECLS

#endif /* !_SHA512T_H_ */
