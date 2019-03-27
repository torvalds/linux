/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993, 1994
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

#if 0
#ifndef lint
static char sccsid[] = "@(#)regular.c	8.3 (Berkeley) 4/2/94";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <capsicum_helpers.h>
#include <err.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "extern.h"

static u_char *remmap(u_char *, int, off_t);
static void segv_handler(int);
#define MMAP_CHUNK (8*1024*1024)

#define ROUNDPAGE(i) ((i) & ~pagemask)

void
c_regular(int fd1, const char *file1, off_t skip1, off_t len1,
    int fd2, const char *file2, off_t skip2, off_t len2)
{
	struct sigaction act, oact;
	cap_rights_t rights;
	u_char ch, *p1, *p2, *m1, *m2, *e1, *e2;
	off_t byte, length, line;
	off_t pagemask, off1, off2;
	size_t pagesize;
	int dfound;

	if (skip1 > len1)
		eofmsg(file1);
	len1 -= skip1;
	if (skip2 > len2)
		eofmsg(file2);
	len2 -= skip2;

	if (sflag && len1 != len2)
		exit(DIFF_EXIT);

	pagesize = getpagesize();
	pagemask = (off_t)pagesize - 1;
	off1 = ROUNDPAGE(skip1);
	off2 = ROUNDPAGE(skip2);

	length = MIN(len1, len2);

	if ((m1 = remmap(NULL, fd1, off1)) == NULL) {
		c_special(fd1, file1, skip1, fd2, file2, skip2);
		return;
	}

	if ((m2 = remmap(NULL, fd2, off2)) == NULL) {
		munmap(m1, MMAP_CHUNK);
		c_special(fd1, file1, skip1, fd2, file2, skip2);
		return;
	}

	if (caph_rights_limit(fd1, cap_rights_init(&rights, CAP_MMAP_R)) < 0)
		err(1, "unable to limit rights for %s", file1);
	if (caph_rights_limit(fd2, cap_rights_init(&rights, CAP_MMAP_R)) < 0)
		err(1, "unable to limit rights for %s", file2);
	if (caph_enter() < 0)
		err(ERR_EXIT, "unable to enter capability mode");

	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NODEFER;
	act.sa_handler = segv_handler;
	if (sigaction(SIGSEGV, &act, &oact))
		err(ERR_EXIT, "sigaction()");

	dfound = 0;
	e1 = m1 + MMAP_CHUNK;
	e2 = m2 + MMAP_CHUNK;
	p1 = m1 + (skip1 - off1);
	p2 = m2 + (skip2 - off2);

	for (byte = line = 1; length--; ++byte) {
		if ((ch = *p1) != *p2) {
			if (xflag) {
				dfound = 1;
				(void)printf("%08llx %02x %02x\n",
				    (long long)byte - 1, ch, *p2);
			} else if (lflag) {
				dfound = 1;
				(void)printf("%6lld %3o %3o\n",
				    (long long)byte, ch, *p2);
			} else
				diffmsg(file1, file2, byte, line);
				/* NOTREACHED */
		}
		if (ch == '\n')
			++line;
		if (++p1 == e1) {
			off1 += MMAP_CHUNK;
			if ((p1 = m1 = remmap(m1, fd1, off1)) == NULL) {
				munmap(m2, MMAP_CHUNK);
				err(ERR_EXIT, "remmap %s", file1);
			}
			e1 = m1 + MMAP_CHUNK;
		}
		if (++p2 == e2) {
			off2 += MMAP_CHUNK;
			if ((p2 = m2 = remmap(m2, fd2, off2)) == NULL) {
				munmap(m1, MMAP_CHUNK);
				err(ERR_EXIT, "remmap %s", file2);
			}
			e2 = m2 + MMAP_CHUNK;
		}
	}
	munmap(m1, MMAP_CHUNK);
	munmap(m2, MMAP_CHUNK);

	if (sigaction(SIGSEGV, &oact, NULL))
		err(ERR_EXIT, "sigaction()");

	if (len1 != len2)
		eofmsg (len1 > len2 ? file2 : file1);
	if (dfound)
		exit(DIFF_EXIT);
}

static u_char *
remmap(u_char *mem, int fd, off_t offset)
{
	if (mem != NULL)
		munmap(mem, MMAP_CHUNK);
	mem = mmap(NULL, MMAP_CHUNK, PROT_READ, MAP_SHARED, fd, offset);
	if (mem == MAP_FAILED)
		return (NULL);
	madvise(mem, MMAP_CHUNK, MADV_SEQUENTIAL);
	return (mem);
}

static void
segv_handler(int sig __unused) {
	static const char msg[] = "cmp: Input/output error (caught SIGSEGV)\n";

	write(STDERR_FILENO, msg, sizeof(msg));
	_exit(EXIT_FAILURE);
}
