/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1992, 1993
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

__FBSDID("$FreeBSD$");

#ifdef lint
static const char sccsid[] = "@(#)swap.c	8.3 (Berkeley) 4/29/95";
#endif

/*
 * swapinfo - based on a program of the same name by Kevin Lahey
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <kvm.h>
#include <nlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <err.h>

#include "systat.h"
#include "extern.h"

kvm_t	*kd;

static char *header;
static long blocksize;
static int dlen, odlen;
static int hlen;
static int ulen, oulen;
static int pagesize;

WINDOW *
openswap(void)
{
	return (subwin(stdscr, LINES-3-1, 0, MAINWIN_ROW, 0));
}

void
closeswap(WINDOW *w)
{
	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
	delwin(w);
}

/*
 * The meat of all the swap stuff is stolen from pstat(8)'s
 * swapmode(), which is based on a program called swapinfo written by
 * Kevin Lahey <kml@rokkaku.atl.ga.us>.
 */

#define NSWAP	16

static struct kvm_swap kvmsw[NSWAP];
static int kvnsw, okvnsw;

static void calclens(void);

#define CONVERT(v)	((int)((int64_t)(v) * pagesize / blocksize))

static void
calclens(void)
{
	int i, n;
	int len;

	dlen = sizeof("Disk");
	for (i = 0; i < kvnsw; ++i) {
		len = strlen(kvmsw[i].ksw_devname);
		if (dlen < len)
			dlen = len;
	}

	ulen = sizeof("Used");
	for (n = CONVERT(kvmsw[kvnsw].ksw_used), len = 2; n /= 10; ++len);
	if (ulen < len)
		ulen = len;
}

int
initswap(void)
{
	static int once = 0;

	if (once)
		return (1);

	header = getbsize(&hlen, &blocksize);
	pagesize = getpagesize();

	if ((kvnsw = kvm_getswapinfo(kd, kvmsw, NSWAP, 0)) < 0) {
		error("systat: kvm_getswapinfo failed");
		return (0);
	}
	okvnsw = kvnsw;

	calclens();
	odlen = dlen;
	oulen = ulen;

	once = 1;
	return (1);
}

void
fetchswap(void)
{

	okvnsw = kvnsw;
	if ((kvnsw = kvm_getswapinfo(kd, kvmsw, NSWAP, 0)) < 0) {
		error("systat: kvm_getswapinfo failed");
		return;
	}

	odlen = dlen;
	oulen = ulen;
	calclens();
}

void
labelswap(void)
{
	const char *name;
	int i;

	fetchswap();

	werase(wnd);

	mvwprintw(wnd, 0, 0, "%*s%*s%*s %s",
	    -dlen, "Disk", hlen, header, ulen, "Used",
	    "/0%  /10  /20  /30  /40  /50  /60  /70  /80  /90  /100");

	for (i = 0; i <= kvnsw; ++i) {
		if (i == kvnsw) {
			if (kvnsw == 1)
				break;
			name = "Total";
		} else
			name = kvmsw[i].ksw_devname;
		mvwprintw(wnd, i + 1, 0, "%*s", -dlen, name);
	}
}

void
showswap(void)
{
	int count;
	int i;

	if (kvnsw != okvnsw || dlen != odlen || ulen != oulen)
		labelswap();

	for (i = 0; i <= kvnsw; ++i) {
		if (i == kvnsw) {
			if (kvnsw == 1)
				break;
		}

		if (kvmsw[i].ksw_total == 0) {
			mvwprintw(
			    wnd,
			    i + 1,
			    dlen + hlen + ulen + 1,
			    "(swap not configured)"
			);
			continue;
		}

		wmove(wnd, i + 1, dlen);

		wprintw(wnd, "%*d", hlen, CONVERT(kvmsw[i].ksw_total));
		wprintw(wnd, "%*d", ulen, CONVERT(kvmsw[i].ksw_used));

		count = 50.0 * kvmsw[i].ksw_used / kvmsw[i].ksw_total + 1;

		waddch(wnd, ' ');
		while (count--)
			waddch(wnd, 'X');
		wclrtoeol(wnd);
	}
}
