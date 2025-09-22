/*	$OpenBSD: strtofflags.c,v 1.7 2015/08/20 21:49:29 deraadt Exp $	*/

/*-
 * Copyright (c) 1993
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
#include <sys/stat.h>

#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static const struct {
	char *name;
	u_int32_t flag;
	int invert;
} mapping[] = {
	/* shorter names per flag first, all prefixed by "no" */
	{ "nosappnd",		SF_APPEND,	0 },
	{ "nosappend",		SF_APPEND,	0 },
	{ "noarch",		SF_ARCHIVED,	0 },
	{ "noarchived",		SF_ARCHIVED,	0 },
	{ "noschg",		SF_IMMUTABLE,	0 },
	{ "noschange",		SF_IMMUTABLE,	0 },
	{ "nosimmutable",	SF_IMMUTABLE,	0 },
#ifdef __FreeBSD__
	{ "nosunlnk",		SF_NOUNLINK,	0 },
	{ "nosunlink",		SF_NOUNLINK,	0 },
#endif
	{ "nouappnd",		UF_APPEND,	0 },
	{ "nouappend",		UF_APPEND,	0 },
	{ "nouchg",		UF_IMMUTABLE,	0 },
	{ "nouchange",		UF_IMMUTABLE,	0 },
	{ "nouimmutable",	UF_IMMUTABLE,	0 },
	{ "nodump",		UF_NODUMP,	1 },
	{ "noopaque",		UF_OPAQUE,	0 },
#ifdef __FreeBSD__
	{ "nouunlnk",		UF_NOUNLINK,	0 },
	{ "nouunlink",		UF_NOUNLINK,	0 }
#endif
};
#define longestflaglen	12
#define nmappings	(sizeof(mapping) / sizeof(mapping[0]))

/*
 * fflagstostr --
 *	Convert file flags to a comma-separated string.  If no flags
 *	are set, return the empty string.
 */
char *
fflagstostr(u_int32_t flags)
{
	char *string;
	char *sp, *dp;
	u_int32_t setflags;
	int i;

	if ((string = calloc(nmappings, longestflaglen + 1)) == NULL)
		return (NULL);

	setflags = flags;
	dp = string;
	for (i = 0; i < nmappings; i++) {
		if (setflags & mapping[i].flag) {
			if (dp > string)
				*dp++ = ',';
			for (sp = mapping[i].invert ? mapping[i].name :
			    mapping[i].name + 2; *sp; *dp++ = *sp++) ;
			setflags &= ~mapping[i].flag;
		}
	}
	*dp = '\0';
	return (string);
}

/*
 * strtofflags --
 *	Take string of arguments and return file flags.  Return 0 on
 *	success, 1 on failure.  On failure, stringp is set to point
 *	to the offending token.
 */
int
strtofflags(char **stringp, u_int32_t *setp, u_int32_t *clrp)
{
	char *string, *p;
	int i;

	if (setp)
		*setp = 0;
	if (clrp)
		*clrp = 0;
	string = *stringp;
	while ((p = strsep(&string, "\t ,")) != NULL) {
		*stringp = p;
		if (*p == '\0')
			continue;
		for (i = 0; i < nmappings; i++) {
			if (strcmp(p, mapping[i].name + 2) == 0) {
				if (mapping[i].invert) {
					if (clrp)
						*clrp |= mapping[i].flag;
				} else {
					if (setp)
						*setp |= mapping[i].flag;
				}
				break;
			} else if (strcmp(p, mapping[i].name) == 0) {
				if (mapping[i].invert) {
					if (setp)
						*setp |= mapping[i].flag;
				} else {
					if (clrp)
						*clrp |= mapping[i].flag;
				}
				break;
			}
		}
		if (i == nmappings)
			return 1;
	}
	return 0;
}
