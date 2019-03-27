/*-
 * Copyright 2005 Colin Percival
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

#ifndef _SHA256_H_
#define _SHA256_H_

#ifndef _KERNEL
#include <sys/types.h>
#endif

#define SHA256_BLOCK_LENGTH		64
#define SHA256_DIGEST_LENGTH		32
#define SHA256_DIGEST_STRING_LENGTH	(SHA256_DIGEST_LENGTH * 2 + 1)

typedef struct SHA256Context {
	uint32_t state[8];
	uint64_t count;
	uint8_t buf[SHA256_BLOCK_LENGTH];
} SHA256_CTX;

__BEGIN_DECLS

/* Ensure libmd symbols do not clash with libcrypto */

#ifndef SHA256_Init
#define SHA256_Init		_libmd_SHA256_Init
#endif
#ifndef SHA256_Update
#define SHA256_Update		_libmd_SHA256_Update
#endif
#ifndef SHA256_Final
#define SHA256_Final		_libmd_SHA256_Final
#endif
#ifndef SHA256_End
#define SHA256_End		_libmd_SHA256_End
#endif
#ifndef SHA256_Fd
#define SHA256_Fd		_libmd_SHA256_Fd
#endif
#ifndef SHA256_FdChunk
#define SHA256_FdChunk		_libmd_SHA256_FdChunk
#endif
#ifndef SHA256_File
#define SHA256_File		_libmd_SHA256_File
#endif
#ifndef SHA256_FileChunk
#define SHA256_FileChunk	_libmd_SHA256_FileChunk
#endif
#ifndef SHA256_Data
#define SHA256_Data		_libmd_SHA256_Data
#endif

#ifndef SHA256_Transform
#define SHA256_Transform	_libmd_SHA256_Transform
#endif
#ifndef SHA256_version
#define SHA256_version		_libmd_SHA256_version
#endif

void	SHA256_Init(SHA256_CTX *);
void	SHA256_Update(SHA256_CTX *, const void *, size_t);
void	SHA256_Final(unsigned char [__min_size(SHA256_DIGEST_LENGTH)],
    SHA256_CTX *);
#ifndef _KERNEL
char   *SHA256_End(SHA256_CTX *, char *);
char   *SHA256_Data(const void *, unsigned int, char *);
char   *SHA256_Fd(int, char *);
char   *SHA256_FdChunk(int, char *, off_t, off_t);
char   *SHA256_File(const char *, char *);
char   *SHA256_FileChunk(const char *, char *, off_t, off_t);
#endif
__END_DECLS

#endif /* !_SHA256_H_ */
