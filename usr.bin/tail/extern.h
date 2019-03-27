/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
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
 *
 *	@(#)extern.h	8.1 (Berkeley) 6/6/93
 *
 * $FreeBSD$
 */

#define	WR(p, size) do { \
	ssize_t res; \
	res = write(STDOUT_FILENO, p, size); \
	if (res != (ssize_t)size) { \
		if (res == -1) \
			oerr(); \
		else \
			errx(1, "stdout"); \
	} \
} while (0)

#define TAILMAPLEN (4<<20)

struct mapinfo {
	off_t	mapoff;
	off_t	maxoff;
	size_t	maplen;
	char	*start;
	int	fd;
};

struct file_info {
	FILE *fp;
	char *file_name;
	struct stat st;
};

typedef struct file_info file_info_t;

enum STYLE { NOTSET = 0, FBYTES, FLINES, RBYTES, RLINES, REVERSE };

void follow(file_info_t *, enum STYLE, off_t);
void forward(FILE *, const char *, enum STYLE, off_t, struct stat *);
void reverse(FILE *, const char *, enum STYLE, off_t, struct stat *);

int bytes(FILE *, const char *, off_t);
int lines(FILE *, const char *, off_t);

void ierr(const char *);
void oerr(void);
int mapprint(struct mapinfo *, off_t, off_t);
int maparound(struct mapinfo *, off_t);
void printfn(const char *, int);

extern int Fflag, fflag, qflag, rflag, rval, no_files;
