/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Ruslan Ermilov
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

#ifndef _RUNEFILE_H_
#define	_RUNEFILE_H_

#include <sys/types.h>

#ifndef _CACHED_RUNES
#define	_CACHED_RUNES	(1 << 8)
#endif

typedef struct {
	int32_t		min;
	int32_t		max;
	int32_t		map;
} _FileRuneEntry;

typedef struct {
	char		magic[8];
	char		encoding[32];

	uint32_t	runetype[_CACHED_RUNES];
	int32_t		maplower[_CACHED_RUNES];
	int32_t		mapupper[_CACHED_RUNES];

	int32_t		runetype_ext_nranges;
	int32_t		maplower_ext_nranges;
	int32_t		mapupper_ext_nranges;

	int32_t		variable_len;
} _FileRuneLocale;

#define	_FILE_RUNE_MAGIC_1	"RuneMag1"

#endif	/* !_RUNEFILE_H_ */
