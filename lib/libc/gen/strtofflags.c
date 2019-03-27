/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#include <sys/cdefs.h>
__SCCSID("@(#)stat_flags.c	8.1 (Berkeley) 5/31/93");
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define longestflaglen	12
static struct {
	char name[longestflaglen + 1];
	char invert;
	u_long flag;
} const mapping[] = {
	/* shorter names per flag first, all prefixed by "no" */
	{ "nosappnd",		0, SF_APPEND	},
	{ "nosappend",		0, SF_APPEND	},
	{ "noarch",		0, SF_ARCHIVED	},
	{ "noarchived",		0, SF_ARCHIVED	},
	{ "noschg",		0, SF_IMMUTABLE	},
	{ "noschange",		0, SF_IMMUTABLE	},
	{ "nosimmutable",	0, SF_IMMUTABLE	},
	{ "nosunlnk",		0, SF_NOUNLINK	},
	{ "nosunlink",		0, SF_NOUNLINK	},
#ifdef SF_SNAPSHOT
	{ "nosnapshot",		0, SF_SNAPSHOT	},
#endif
	{ "nouappnd",		0, UF_APPEND	},
	{ "nouappend",		0, UF_APPEND	},
	{ "nouarch", 		0, UF_ARCHIVE	},
	{ "nouarchive",		0, UF_ARCHIVE	},
	{ "nohidden",		0, UF_HIDDEN	},
	{ "nouhidden",		0, UF_HIDDEN	},
	{ "nouchg",		0, UF_IMMUTABLE	},
	{ "nouchange",		0, UF_IMMUTABLE	},
	{ "nouimmutable",	0, UF_IMMUTABLE	},
	{ "nodump",		1, UF_NODUMP	},
	{ "nouunlnk",		0, UF_NOUNLINK	},
	{ "nouunlink",		0, UF_NOUNLINK	},
	{ "nooffline",		0, UF_OFFLINE	},
	{ "nouoffline",		0, UF_OFFLINE	},
	{ "noopaque",		0, UF_OPAQUE	},
	{ "nordonly",		0, UF_READONLY	},
	{ "nourdonly",		0, UF_READONLY	},
	{ "noreadonly",		0, UF_READONLY	},
	{ "noureadonly",	0, UF_READONLY	},
	{ "noreparse",		0, UF_REPARSE	},
	{ "noureparse",		0, UF_REPARSE	},
	{ "nosparse",		0, UF_SPARSE	},
	{ "nousparse",		0, UF_SPARSE	},
	{ "nosystem",		0, UF_SYSTEM	},
	{ "nousystem",		0, UF_SYSTEM	}
};
#define nmappings	(sizeof(mapping) / sizeof(mapping[0]))

/*
 * fflagstostr --
 *	Convert file flags to a comma-separated string.  If no flags
 *	are set, return the empty string.
 */
char *
fflagstostr(u_long flags)
{
	char *string;
	const char *sp;
	char *dp;
	u_long setflags;
	u_int i;

	if ((string = (char *)malloc(nmappings * (longestflaglen + 1))) == NULL)
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
strtofflags(char **stringp, u_long *setp, u_long *clrp)
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
