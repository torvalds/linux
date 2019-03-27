/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Jeremie Le Hen <jlh@FreeBSD.org>
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

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *
stream_name(FILE *s)
{

	if (s == stdin)
		return "stdin";
	if (s == stdout)
		return "stdout";
	if (s == stderr)
		return "stderr";
	/* This should not happen. */
	abort();
}

static void
change_buf(FILE *s, const char *bufmode)
{
	char *unit;
	size_t bufsize;
	int mode;

	bufsize = 0;
	if (bufmode[0] == '0' && bufmode[1] == '\0')
		mode = _IONBF;
	else if (bufmode[0] == 'L' && bufmode[1] == '\0')
		mode = _IOLBF;
	else if (bufmode[0] == 'B' && bufmode[1] == '\0') {
		mode = _IOFBF;
		bufsize = 0;
	} else {
		/*
		 * This library being preloaded, depending on libutil
		 * would lead to excessive namespace pollution.
		 * Thus we do not use expand_number().
		 */
		errno = 0;
		bufsize = strtol(bufmode, &unit, 0);
		if (errno == EINVAL || errno == ERANGE || unit == bufmode)
			warn("Wrong buffer mode '%s' for %s", bufmode,
			    stream_name(s));
		switch (*unit) {
		case 'G':
			bufsize *= 1024 * 1024 * 1024;
			break;
		case 'M':
			bufsize *= 1024 * 1024;
			break;
		case 'k':
			bufsize *= 1024;
			break;
		case '\0':
			break;
		default:
			warnx("Unknown suffix '%c' for %s", *unit,
			    stream_name(s));
			return;
		}
		mode = _IOFBF;
	}
	if (setvbuf(s, NULL, mode, bufsize) != 0)
		warn("Cannot set buffer mode '%s' for %s", bufmode,
		    stream_name(s));
}

__attribute__ ((constructor)) static void
stdbuf(void)
{
	char *i_mode, *o_mode, *e_mode;

	i_mode = getenv("_STDBUF_I");
	o_mode = getenv("_STDBUF_O");
	e_mode = getenv("_STDBUF_E");

	if (e_mode != NULL)
		change_buf(stderr, e_mode);
	if (i_mode != NULL)
		change_buf(stdin, i_mode);
	if (o_mode != NULL)
		change_buf(stdout, o_mode);
}
