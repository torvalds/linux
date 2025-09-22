/*	$OpenBSD: getsubopt.c,v 1.4 2005/08/08 08:05:36 espie Exp $	*/

/*-
 * Copyright (c) 1990, 1993
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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/*
 * The SVID interface to getsubopt provides no way of figuring out which
 * part of the suboptions list wasn't matched.  This makes error messages
 * tricky...  The extern variable suboptarg is a pointer to the token
 * which didn't match.
 */
char *suboptarg;

int
getsubopt(char **optionp, char * const *tokens, char **valuep)
{
	int cnt;
	char *p;

	suboptarg = *valuep = NULL;

	if (!optionp || !*optionp)
		return(-1);

	/* skip leading white-space, commas */
	for (p = *optionp; *p && (*p == ',' || *p == ' ' || *p == '\t'); ++p);

	if (!*p) {
		*optionp = p;
		return(-1);
	}

	/* save the start of the token, and skip the rest of the token. */
	for (suboptarg = p;
	    *++p && *p != ',' && *p != '=' && *p != ' ' && *p != '\t';);

	if (*p) {
		/*
		 * If there's an equals sign, set the value pointer, and
		 * skip over the value part of the token.  Terminate the
		 * token.
		 */
		if (*p == '=') {
			*p = '\0';
			for (*valuep = ++p;
			    *p && *p != ',' && *p != ' ' && *p != '\t'; ++p);
			if (*p) 
				*p++ = '\0';
		} else
			*p++ = '\0';
		/* Skip any whitespace or commas after this token. */
		for (; *p && (*p == ',' || *p == ' ' || *p == '\t'); ++p);
	}

	/* set optionp for next round. */
	*optionp = p;

	for (cnt = 0; *tokens; ++tokens, ++cnt)
		if (!strcmp(suboptarg, *tokens))
			return(cnt);
	return(-1);
}
