/*-
 * Copyright (c) 2002 Alexey Zelkin <phantom@FreeBSD.org>
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

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Try setlocale() for locale with given name.
 */

struct locdef {
	int		catid;
	const char	*catname;
} locales[_LC_LAST] = {
	{ LC_ALL,	"LC_ALL" },
	{ LC_COLLATE,	"LC_COLLATE" },
	{ LC_CTYPE,	"LC_CTYPE" },
	{ LC_MONETARY,	"LC_MONETARY" },
	{ LC_NUMERIC,	"LC_NUMERIC" },
	{ LC_TIME,	"LC_TIME" },
	{ LC_MESSAGES,	"LC_MESSAGES" }
};

int
main(int argc, char *argv[])
{
	int i, result;
	const char *localename;

	if (argc != 2) {
		(void)fprintf(stderr, "usage: localeck <locale_name>\n");
		exit(1);
	}

	localename = argv[1];
	result = 0;

	for (i = 0; i < _LC_LAST; i++) {
		if (setlocale(locales[i].catid, localename) == NULL) {
			printf("setlocale(%s, %s) failed\n", locales[i].catname,
			    localename);
			result++;
		}
	}
	return (result);
}
