/*	$OpenBSD: setenv.c,v 1.20 2022/08/08 22:40:03 millert Exp $ */
/*
 * Copyright (c) 1987 Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char **lastenv;				/* last value of environ */

/*
 * putenv --
 *	Add a name=value string directly to the environmental, replacing
 *	any current value.
 */
int
putenv(char *str)
{
	char **P, *cp;
	size_t cnt = 0;
	int offset = 0;

	for (cp = str; *cp && *cp != '='; ++cp)
		;
	if (cp == str || *cp != '=') {
		/* '=' is the first character of string or is missing. */
		errno = EINVAL;
		return (-1);
	}

	if (__findenv(str, (int)(cp - str), &offset) != NULL) {
		environ[offset++] = str;
		/* could be set multiple times */
		while (__findenv(str, (int)(cp - str), &offset)) {
			for (P = &environ[offset];; ++P)
				if (!(*P = *(P + 1)))
					break;
		}
		return (0);
	}

	/* create new slot for string */
	if (environ != NULL) {
		for (P = environ; *P != NULL; P++)
			;
		cnt = P - environ;
	}
	P = reallocarray(lastenv, cnt + 2, sizeof(char *));
	if (!P)
		return (-1);
	if (lastenv != environ && environ != NULL)
		memcpy(P, environ, cnt * sizeof(char *));
	lastenv = environ = P;
	environ[cnt] = str;
	environ[cnt + 1] = NULL;
	return (0);
}
DEF_WEAK(putenv);

/*
 * setenv --
 *	Set the value of the environmental variable "name" to be
 *	"value".  If rewrite is set, replace any current value.
 */
int
setenv(const char *name, const char *value, int rewrite)
{
	char *C, **P;
	const char *np;
	int l_value, offset = 0;

	if (!name || !*name) {
		errno = EINVAL;
		return (-1);
	}
	for (np = name; *np && *np != '='; ++np)
		;
	if (*np) {
		errno = EINVAL;
		return (-1);			/* has `=' in name */
	}

	l_value = strlen(value);
	if ((C = __findenv(name, (int)(np - name), &offset)) != NULL) {
		int tmpoff = offset + 1;
		if (!rewrite)
			return (0);
#if 0 /* XXX - existing entry may not be writable */
		if (strlen(C) >= l_value) {	/* old larger; copy over */
			while ((*C++ = *value++))
				;
			return (0);
		}
#endif
		/* could be set multiple times */
		while (__findenv(name, (int)(np - name), &tmpoff)) {
			for (P = &environ[tmpoff];; ++P)
				if (!(*P = *(P + 1)))
					break;
		}
	} else {					/* create new slot */
		size_t cnt = 0;

		if (environ != NULL) {
			for (P = environ; *P != NULL; P++)
				;
			cnt = P - environ;
		}
		P = reallocarray(lastenv, cnt + 2, sizeof(char *));
		if (!P)
			return (-1);
		if (lastenv != environ && environ != NULL)
			memcpy(P, environ, cnt * sizeof(char *));
		lastenv = environ = P;
		offset = cnt;
		environ[cnt + 1] = NULL;
	}
	if (!(environ[offset] =			/* name + `=' + value */
	    malloc((int)(np - name) + l_value + 2)))
		return (-1);
	for (C = environ[offset]; (*C = *name++) && *C != '='; ++C)
		;
	for (*C++ = '='; (*C++ = *value++); )
		;
	return (0);
}
DEF_WEAK(setenv);

/*
 * unsetenv(name) --
 *	Delete environmental variable "name".
 */
int
unsetenv(const char *name)
{
	char **P;
	const char *np;
	int offset = 0;

	if (!name || !*name) {
		errno = EINVAL;
		return (-1);
	}
	for (np = name; *np && *np != '='; ++np)
		;
	if (*np) {
		errno = EINVAL;
		return (-1);			/* has `=' in name */
	}

	/* could be set multiple times */
	while (__findenv(name, (int)(np - name), &offset)) {
		for (P = &environ[offset];; ++P)
			if (!(*P = *(P + 1)))
				break;
	}
	return (0);
}
DEF_WEAK(unsetenv);
