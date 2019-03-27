/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998 Softweyr LLC.  All rights reserved.
 *
 * strtok_r, from Berkeley strtok
 * Oct 13, 1998 by Wes Peters <wes@softweyr.com>
 *
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notices, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SOFTWEYR LLC, THE REGENTS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL SOFTWEYR LLC, THE
 * REGENTS, OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)strtok.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stddef.h>
#ifdef DEBUG_STRTOK
#include <stdio.h>
#endif
#include <string.h>

char	*__strtok_r(char *, const char *, char **);

__weak_reference(__strtok_r, strtok_r);

char *
__strtok_r(char *s, const char *delim, char **last)
{
	char *spanp, *tok;
	int c, sc;

	if (s == NULL && (s = *last) == NULL)
		return (NULL);

	/*
	 * Skip (span) leading delimiters (s += strspn(s, delim), sort of).
	 */
cont:
	c = *s++;
	for (spanp = (char *)delim; (sc = *spanp++) != 0;) {
		if (c == sc)
			goto cont;
	}

	if (c == 0) {		/* no non-delimiter characters */
		*last = NULL;
		return (NULL);
	}
	tok = s - 1;

	/*
	 * Scan token (scan for delimiters: s += strcspn(s, delim), sort of).
	 * Note that delim must have one NUL; we stop if we see that, too.
	 */
	for (;;) {
		c = *s++;
		spanp = (char *)delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0)
					s = NULL;
				else
					s[-1] = '\0';
				*last = s;
				return (tok);
			}
		} while (sc != 0);
	}
	/* NOTREACHED */
}

char *
strtok(char *s, const char *delim)
{
	static char *last;

	return (__strtok_r(s, delim, &last));
}

#ifdef DEBUG_STRTOK
/*
 * Test the tokenizer.
 */
int
main(void)
{
	char blah[80], test[80];
	char *brkb, *brkt, *phrase, *sep, *word;

	sep = "\\/:;=-";
	phrase = "foo";

	printf("String tokenizer test:\n");
	strcpy(test, "This;is.a:test:of=the/string\\tokenizer-function.");
	for (word = strtok(test, sep); word; word = strtok(NULL, sep))
		printf("Next word is \"%s\".\n", word);
	strcpy(test, "This;is.a:test:of=the/string\\tokenizer-function.");

	for (word = strtok_r(test, sep, &brkt); word;
	    word = strtok_r(NULL, sep, &brkt)) {
		strcpy(blah, "blah:blat:blab:blag");

		for (phrase = strtok_r(blah, sep, &brkb); phrase;
		    phrase = strtok_r(NULL, sep, &brkb))
			printf("So far we're at %s:%s\n", word, phrase);
	}

	return (0);
}

#endif /* DEBUG_STRTOK */
