/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1993-98 by Andrey A. Chernov, Moscow, Russia.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

/*
 * KOI8-R -> CP866 conversion filter (Russian character sets)
 */

#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int length = 66;
int lines;

char *koi2alt[] = {
/*         0      1      2      3      4      5      6      7   */
/*         8      9      A      B      C      D      E      F   */
/* 8 */ "\xc4","\xb3","\xda","\xbf","\xc0","\xd9","\xc3","\xb4",
	"\xc2","\xc1","\xc5","\xdf","\xdc","\xdb","\xdd","\xde",
/* 9 */ "\xb0","\xb1","\xb2","\xb3","\xfe","\xf9","\xfb","-\b~",
	"<\b_",">\b_","\xff","\xb3","\xf8","2\b-","\xfa",":\b-",
/* A */ "\xcd","\xba","\xd5","\xf1","\xd6","\xc9","\xb8","\xb7",
	"\xbb","\xd4","\xd3","\xc8","\xbe","\xbd","\xbc","\xc6",
/* B */ "\xc7","\xcc","\xb5","\xf0","\xb6","\xb9","\xd1","\xd2",
	"\xcb","\xcf","\xd0","\xca","\xd8","\xd7","\xce","c\b_",
/* C */ "\xee","\xa0","\xa1","\xe6","\xa4","\xa5","\xe4","\xa3",
	"\xe5","\xa8","\xa9","\xaa","\xab","\xac","\xad","\xae",
/* D */ "\xaf","\xef","\xe0","\xe1","\xe2","\xe3","\xa6","\xa2",
	"\xec","\xeb","\xa7","\xe8","\xed","\xe9","\xe7","\xea",
/* E */ "\x9e","\x80","\x81","\x96","\x84","\x85","\x94","\x83",
	"\x95","\x88","\x89","\x8a","\x8b","\x8c","\x8d","\x8e",
/* F */ "\x8f","\x9f","\x90","\x91","\x92","\x93","\x86","\x82",
	"\x9c","\x9b","\x87","\x98","\x9d","\x99","\x97","\x9a"
};

int main(int argc, char *argv[])
{
	int c, i;
	char *cp;

	while (--argc) {
		if (*(cp = *++argv) == '-') {
			switch (*++cp) {
			case 'l':
				if ((i = atoi(++cp)) > 0)
					length = i;
				break;
			}
		}
	}

	while ((c = getchar()) != EOF) {
		if (c == '\031') {
			if ((c = getchar()) == '\1') {
				lines = 0;
				fflush(stdout);
				kill(getpid(), SIGSTOP);
				continue;
			} else {
				ungetc(c, stdin);
				c = '\031';
			}
		} else if (c & 0x80) {
			fputs(koi2alt[c & 0x7F], stdout);
			continue;
		} else if (c == '\n')
			lines++;
		else if (c == '\f')
			lines = length;
		putchar(c);
		if (lines >= length) {
			lines = 0;
			fflush(stdout);
		}
	}
	return 0;
}
