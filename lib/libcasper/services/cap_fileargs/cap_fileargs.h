/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Mariusz Zaborski <oshogbo@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef _FILEARGS_H_
#define	_FILEARGS_H_

#include <sys/dnv.h>
#include <sys/nv.h>

#include <stdbool.h>

#ifdef WITH_CASPER
struct fileargs;
typedef struct fileargs fileargs_t;

fileargs_t *fileargs_init(int argc, char *argv[], int flags, mode_t mode,
    cap_rights_t *rightsp);
fileargs_t *fileargs_cinit(cap_channel_t *cas, int argc, char *argv[],
    int flags, mode_t mode, cap_rights_t *rightsp);
fileargs_t *fileargs_initnv(nvlist_t *limits);
fileargs_t *fileargs_cinitnv(cap_channel_t *cas, nvlist_t *limits);
int fileargs_open(fileargs_t *fa, const char *name);
void fileargs_free(fileargs_t *fa);
FILE *fileargs_fopen(fileargs_t *fa, const char *name, const char *mode);
#else
typedef struct fileargs {
	int	fa_flags;
	mode_t	fa_mode;
} fileargs_t;

static inline fileargs_t *
fileargs_init(int argc __unused, char *argv[] __unused, int flags, mode_t mode,
    cap_rights_t *rightsp __unused) {
	fileargs_t *fa;

	fa = malloc(sizeof(*fa));
	if (fa != NULL) {
		fa->fa_flags = flags;
		fa->fa_mode = mode;
	}

	return (fa);
}

static inline fileargs_t *
fileargs_cinit(cap_channel_t *cas __unused, int argc, char *argv[], int flags,
    mode_t mode, cap_rights_t *rightsp)
{

	return (fileargs_init(argc, argv, flags, mode, rightsp));
}

static inline fileargs_t *
fileargs_initnv(nvlist_t *limits)
{
	fileargs_t *fa;

	fa = fileargs_init(0, NULL,
	    nvlist_get_number(limits, "flags"),
	    dnvlist_get_number(limits, "mode", 0),
	    NULL);
	nvlist_destroy(limits);

	return (fa);
}

static inline fileargs_t *
fileargs_cinitnv(cap_channel_t *cas __unused, nvlist_t *limits)
{

	return (fileargs_initnv(limits));
}

#define	fileargs_open(fa, name)							\
	open(name, fa->fa_flags, fa->fa_mode)
#define	fileargs_fopen(fa, name, mode)						\
	fopen(name, mode)
#define	fileargs_free(fa)	(free(fa))
#endif

#endif	/* !_FILEARGS_H_ */
