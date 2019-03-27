/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1999 by Andrey A. Chernov, Moscow, Russia.
 * (C) 18 Sep 1999, Alex G. Bulushev (bag@demos.su)
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
 * KOI8-R -> CP855 conversion filter (Russian character sets)
 */

#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int length = 66;
int lines;

unsigned char koi2855 [] = {
0xc4, 0xb3, 0xda, 0xbf, 0xc0, 0xd9, 0xc3, 0xb4,
0xc2, 0xc1, 0xc5, 0xdf, 0xdc, 0xdb, 0xdb, 0xdb,
0xb0, 0xb1, 0xb2, 0xda, 0xfe, 0x2e, 0xad, 0x3d,
0xae, 0xaf, 0xff, 0xd9, 0xcf, 0x32, 0x2e, 0x25,
0xcd, 0xba, 0xc9, 0x84, 0xc9, 0xc9, 0xbb, 0xbb,
0xbb, 0xc8, 0xc8, 0xc8, 0xbc, 0xbc, 0xbc, 0xcc,
0xcc, 0xcc, 0xb9, 0x85, 0xb9, 0xb9, 0xcb, 0xcb,
0xcb, 0xca, 0xca, 0xca, 0xce, 0xce, 0xce, 0x43,
0x9c, 0xa0, 0xa2, 0xa4, 0xa6, 0xa8, 0xaa, 0xac,
0xb5, 0xb7, 0xbd, 0xc6, 0xd0, 0xd2, 0xd4, 0xd6,
0xd8, 0xde, 0xe1, 0xe3, 0xe5, 0xe7, 0xe9, 0xeb,
0xed, 0xf1, 0xf3, 0xf5, 0xf7, 0xf9, 0xfb, 0x9e,
0x9d, 0xa1, 0xa3, 0xa5, 0xa7, 0xa9, 0xab, 0xad,
0xb6, 0xb8, 0xbe, 0xc7, 0xd1, 0xd3, 0xd5, 0xd7,
0xdd, 0xe0, 0xe2, 0xe4, 0xe6, 0xe8, 0xea, 0xec,
0xee, 0xf2, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0x9f
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
			putchar(koi2855[c & 0x7F]);
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
