/*	$OpenBSD: getcom.c,v 1.17 2018/02/07 20:22:23 tedu Exp $	*/
/*	$NetBSD: getcom.c,v 1.3 1995/03/21 15:07:30 cgd Exp $	*/

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

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "extern.h"

char   *
getcom(char *buf, int size, const char *prompt, const char *error)
{
	for (;;) {
		fputs(prompt, stdout);
		if (fgets(buf, size, stdin) == NULL) {
			if (feof(stdin))
				die(0);
			clearerr(stdin);
			continue;
		}
		while (isspace((unsigned char)*buf))
			buf++;
		if (*buf)
			break;
		if (error)
			puts(error);
	}
	/* If we didn't get to the end of the line, don't read it in next time */
	if (buf[strlen(buf) - 1] != '\n') {
		int i;
		while ((i = getchar()) != '\n' && i != EOF)
			continue;
	}
	return (buf);
}


/*
 * shifts to UPPERCASE if flag > 0, lowercase if flag < 0,
 * and leaves it unchanged if flag = 0
 */
char   *
getword(char *buf1, char *buf2, int flag)
{
	int cnt;

	cnt = 1;
	while (isspace((unsigned char)*buf1))
		buf1++;
	if (*buf1 != ',' && *buf1 != '.') {
		if (!*buf1) {
			*buf2 = '\0';
			return (0);
		}
		while (cnt < WORDLEN && *buf1 && !isspace((unsigned char)*buf1) &&
		       *buf1 != ',' && *buf1 != '.')
			if (flag < 0) {
				if (isupper((unsigned char)*buf1)) {
					*buf2++ = tolower((unsigned char)*buf1++);
					cnt++;
				} else {
					*buf2++ = *buf1++;
					cnt++;
				}
			} else if (flag > 0) {
				if (islower((unsigned char)*buf1)) {
					*buf2++ = toupper((unsigned char)*buf1++);
					cnt++;
				} else {
					*buf2++ = *buf1++;
					cnt++;
				}
			} else {
				*buf2++ = *buf1++;
				cnt++;
			}
		if (cnt == WORDLEN)
			while (*buf1 && !isspace((unsigned char)*buf1))
				buf1++;
	} else
		*buf2++ = *buf1++;
	*buf2 = '\0';
	while (isspace((unsigned char)*buf1))
		buf1++;
	return (*buf1 ? buf1 : NULL);
}
