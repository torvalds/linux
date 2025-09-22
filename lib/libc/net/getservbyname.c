/*	$OpenBSD: getservbyname.c,v 1.11 2015/09/14 07:38:38 guenther Exp $ */
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
getservbyname_r(const char *name, const char *proto, struct servent *se,
    struct servent_data *sd)
{
	char **cp;
	int error;

	setservent_r(sd->stayopen, sd);
	while ((error = getservent_r(se, sd)) == 0) {
		if (strcmp(name, se->s_name) == 0)
			goto gotname;
		for (cp = se->s_aliases; *cp; cp++)
			if (strcmp(name, *cp) == 0)
				goto gotname;
		continue;
gotname:
		if (proto == 0 || strcmp(se->s_proto, proto) == 0)
			break;
	}
	if (!sd->stayopen && sd->fp != NULL) {
		fclose(sd->fp);
		sd->fp = NULL;
	}
	return (error);
}
DEF_WEAK(getservbyname_r);

struct servent *
getservbyname(const char *name, const char *proto)
{
	extern struct servent_data _servent_data;
	static struct servent serv;

	if (getservbyname_r(name, proto, &serv, &_servent_data) != 0)
		return (NULL);
	return (&serv);
}
DEF_WEAK(getservbyname);
