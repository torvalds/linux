/*	$OpenBSD: getprotoname.c,v 1.8 2015/09/14 07:38:38 guenther Exp $ */
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

#include <netdb.h>
#include <stdio.h>
#include <string.h>

int
getprotobyname_r(const char *name, struct protoent *pe,
    struct protoent_data *pd)
{
	char **cp;
	int error;

	setprotoent_r(pd->stayopen, pd);
	while ((error = getprotoent_r(pe, pd)) == 0) {
		if (strcmp(pe->p_name, name) == 0)
			break;
		for (cp = pe->p_aliases; *cp != 0; cp++)
			if (strcmp(*cp, name) == 0)
				goto found;
	}
found:
	if (!pd->stayopen && pd->fp != NULL) {
		fclose(pd->fp);
		pd->fp = NULL;
	}
	return (error);
}
DEF_WEAK(getprotobyname_r);

struct protoent *
getprotobyname(const char *name)
{
	extern struct protoent_data _protoent_data;
	static struct protoent proto;

	if (getprotobyname_r(name, &proto, &_protoent_data) != 0)
		return (NULL);
	return (&proto);
}
DEF_WEAK(getprotobyname);
