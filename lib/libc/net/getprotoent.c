/*	$OpenBSD: getprotoent.c,v 1.13 2015/09/14 07:38:38 guenther Exp $ */
/*
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
setprotoent_r(int f, struct protoent_data *pd)
{
	if (pd->fp == NULL)
		pd->fp = fopen(_PATH_PROTOCOLS, "re" );
	else
		rewind(pd->fp);
	pd->stayopen |= f;
}
DEF_WEAK(setprotoent_r);

void
endprotoent_r(struct protoent_data *pd)
{
	if (pd->fp) {
		fclose(pd->fp);
		pd->fp = NULL;
	}
	free(pd->aliases);
	pd->aliases = NULL;
	pd->maxaliases = 0;
	free(pd->line);
	pd->line = NULL;
	pd->stayopen = 0;
}
DEF_WEAK(endprotoent_r);

int
getprotoent_r(struct protoent *pe, struct protoent_data *pd)
{
	char *p, *cp, **q, *endp;
	size_t len;
	long l;
	int serrno;

	if (pd->fp == NULL && (pd->fp = fopen(_PATH_PROTOCOLS, "re" )) == NULL)
		return (-1);
again:
	if ((p = fgetln(pd->fp, &len)) == NULL)
		return (-1);
	if (len == 0 || *p == '#' || *p == '\n')
		goto again;
	if (p[len-1] == '\n')
		len--;
	if ((cp = memchr(p, '#', len)) != NULL)
		len = cp - p;
	cp = realloc(pd->line, len + 1);
	if (cp == NULL)
		return (-1);
	pd->line = pe->p_name = memcpy(cp, p, len);
	cp[len] = '\0';
	cp = strpbrk(cp, " \t");
	if (cp == NULL)
		goto again;
	*cp++ = '\0';
	while (*cp == ' ' || *cp == '\t')
		cp++;
	p = strpbrk(cp, " \t");
	if (p != NULL)
		*p++ = '\0';
	l = strtol(cp, &endp, 10);
	if (endp == cp || *endp != '\0' || l < 0 || l >= INT_MAX)
		goto again;
	pe->p_proto = l;
	if (pd->aliases == NULL) {
		pd->maxaliases = 5;
		pd->aliases = calloc(pd->maxaliases, sizeof(char *));
		if (pd->aliases == NULL) {
			serrno = errno;
			endprotoent_r(pd);
			errno = serrno;
			return (-1);
		}
	}
	q = pe->p_aliases = pd->aliases;
	if (p != NULL) {
		cp = p;
		while (cp && *cp) {
			if (*cp == ' ' || *cp == '\t') {
				cp++;
				continue;
			}
			if (q == &pe->p_aliases[pd->maxaliases - 1]) {
				p = reallocarray(pe->p_aliases,
				    pd->maxaliases, 2 * sizeof(char *));
				if (p == NULL) {
					serrno = errno;
					endprotoent_r(pd);
					errno = serrno;
					return (-1);
				}
				pd->maxaliases *= 2;
				q = (char **)p + (q - pe->p_aliases);
				pe->p_aliases = pd->aliases = (char **)p;
			}
			*q++ = cp;
			cp = strpbrk(cp, " \t");
			if (cp != NULL)
				*cp++ = '\0';
		}
	}
	*q = NULL;
	return (0);
}
DEF_WEAK(getprotoent_r);

struct protoent_data _protoent_data;	/* shared with getproto{,name}.c */

void
setprotoent(int f)
{
	setprotoent_r(f, &_protoent_data);
}

void
endprotoent(void)
{
	endprotoent_r(&_protoent_data);
}

struct protoent *
getprotoent(void)
{
	static struct protoent proto;

	if (getprotoent_r(&proto, &_protoent_data) != 0)
		return (NULL);
	return (&proto);
}
