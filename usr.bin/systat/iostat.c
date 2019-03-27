/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998 Kenneth D. Merry.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
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
static const char sccsid[] = "@(#)iostat.c	8.1 (Berkeley) 6/6/93";
#endif

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/resource.h>

#include <devstat.h>
#include <err.h>
#include <nlist.h>
#include <paths.h>
#include <stdlib.h>
#include <string.h>

#include "systat.h"
#include "extern.h"
#include "devs.h"

struct statinfo cur, last;

static  int linesperregion;
static  double etime;
static  int numbers = 0;		/* default display bar graphs */
static  int kbpt = 0;			/* default ms/seek shown */

static int barlabels(int);
static void histogram(long double, int, double);
static int numlabels(int);
static int devstats(int, int, int);
static void stat1(int, int);

WINDOW *
openiostat(void)
{
	return (subwin(stdscr, LINES-3-1, 0, MAINWIN_ROW, 0));
}

void
closeiostat(WINDOW *w)
{
	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
	delwin(w);
}

int
initiostat(void)
{
	if ((num_devices = devstat_getnumdevs(NULL)) < 0)
		return(0);

	cur.dinfo = calloc(1, sizeof(struct devinfo));
	last.dinfo = calloc(1, sizeof(struct devinfo));

	/*
	 * This value for maxshowdevs (100) is bogus.  I'm not sure exactly
	 * how to calculate it, though.
	 */
	if (dsinit(100, &cur, &last, NULL) != 1)
		return(0);

	return(1);
}

void
fetchiostat(void)
{
	struct devinfo *tmp_dinfo;
	size_t len;

	len = sizeof(cur.cp_time);
	if (sysctlbyname("kern.cp_time", &cur.cp_time, &len, NULL, 0)
	    || len != sizeof(cur.cp_time)) {
		perror("kern.cp_time");
		exit (1);
	}
	tmp_dinfo = last.dinfo;
	last.dinfo = cur.dinfo;
	cur.dinfo = tmp_dinfo;

	last.snap_time = cur.snap_time;

	/*
	 * Here what we want to do is refresh our device stats.
	 * getdevs() returns 1 when the device list has changed.
	 * If the device list has changed, we want to go through
	 * the selection process again, in case a device that we
	 * were previously displaying has gone away.
	 */
	switch (devstat_getdevs(NULL, &cur)) {
	case -1:
		errx(1, "%s", devstat_errbuf);
		break;
	case 1:
		cmdiostat("refresh", NULL);
		break;
	default:
		break;
	}
	num_devices = cur.dinfo->numdevs;
	generation = cur.dinfo->generation;

}

#define	INSET	10

void
labeliostat(void)
{
	int row;

	row = 0;
	wmove(wnd, row, 0); wclrtobot(wnd);
	mvwaddstr(wnd, row++, INSET,
	    "/0%  /10  /20  /30  /40  /50  /60  /70  /80  /90  /100");
	mvwaddstr(wnd, row++, 0, "cpu  user|");
	mvwaddstr(wnd, row++, 0, "     nice|");
	mvwaddstr(wnd, row++, 0, "   system|");
	mvwaddstr(wnd, row++, 0, "interrupt|");
	mvwaddstr(wnd, row++, 0, "     idle|");
	if (numbers)
		row = numlabels(row + 1);
	else
		row = barlabels(row + 1);
}

static int
numlabels(int row)
{
	int i, _col, regions, ndrives;
	char tmpstr[10];

#define COLWIDTH	17
#define DRIVESPERLINE	((getmaxx(wnd) - 1 - INSET) / COLWIDTH)
	for (ndrives = 0, i = 0; i < num_devices; i++)
		if (dev_select[i].selected)
			ndrives++;
	regions = howmany(ndrives, DRIVESPERLINE);
	/*
	 * Deduct -regions for blank line after each scrolling region.
	 */
	linesperregion = (getmaxy(wnd) - 1 - row - regions) / regions;
	/*
	 * Minimum region contains space for two
	 * label lines and one line of statistics.
	 */
	if (linesperregion < 3)
		linesperregion = 3;
	_col = INSET;
	for (i = 0; i < num_devices; i++)
		if (dev_select[i].selected) {
			if (_col + COLWIDTH >= getmaxx(wnd) - 1 - INSET) {
				_col = INSET, row += linesperregion + 1;
				if (row > getmaxy(wnd) - 1 - (linesperregion + 1))
					break;
			}
			sprintf(tmpstr, "%s%d", dev_select[i].device_name,
				dev_select[i].unit_number);
			mvwaddstr(wnd, row, _col + 4, tmpstr);
			mvwaddstr(wnd, row + 1, _col, "  KB/t tps  MB/s ");
			_col += COLWIDTH;
		}
	if (_col)
		row += linesperregion + 1;
	return (row);
}

static int
barlabels(int row)
{
	int i;
	char tmpstr[10];

	mvwaddstr(wnd, row++, INSET,
	    "/0%  /10  /20  /30  /40  /50  /60  /70  /80  /90  /100");
	linesperregion = 2 + kbpt;
	for (i = 0; i < num_devices; i++)
		if (dev_select[i].selected) {
			if (row > getmaxy(wnd) - 1 - linesperregion)
				break;
			sprintf(tmpstr, "%s%d", dev_select[i].device_name,
				dev_select[i].unit_number);
			mvwprintw(wnd, row++, 0, "%-5.5s MB/s|",
				  tmpstr);
			mvwaddstr(wnd, row++, 0, "      tps|");
			if (kbpt)
				mvwaddstr(wnd, row++, 0, "     KB/t|");
		}
	return (row);
}

void
showiostat(void)
{
	long t;
	int i, row, _col;

#define X(fld)	t = cur.fld[i]; cur.fld[i] -= last.fld[i]; last.fld[i] = t
	etime = 0;
	for(i = 0; i < CPUSTATES; i++) {
		X(cp_time);
		etime += cur.cp_time[i];
	}
	if (etime == 0.0)
		etime = 1.0;
	etime /= hertz;
	row = 1;
	for (i = 0; i < CPUSTATES; i++)
		stat1(row++, i);
	if (!numbers) {
		row += 2;
		for (i = 0; i < num_devices; i++)
			if (dev_select[i].selected) {
				if (row > getmaxy(wnd) - linesperregion)
					break;
				row = devstats(row, INSET, i);
			}
		return;
	}
	_col = INSET;
	wmove(wnd, row + linesperregion, 0);
	wdeleteln(wnd);
	wmove(wnd, row + 3, 0);
	winsertln(wnd);
	for (i = 0; i < num_devices; i++)
		if (dev_select[i].selected) {
			if (_col + COLWIDTH >= getmaxx(wnd) - 1 - INSET) {
				_col = INSET, row += linesperregion + 1;
				if (row > getmaxy(wnd) - 1 - (linesperregion + 1))
					break;
				wmove(wnd, row + linesperregion, 0);
				wdeleteln(wnd);
				wmove(wnd, row + 3, 0);
				winsertln(wnd);
			}
			(void) devstats(row + 3, _col, i);
			_col += COLWIDTH;
		}
}

static int
devstats(int row, int _col, int dn)
{
	long double transfers_per_second;
	long double kb_per_transfer, mb_per_second;
	long double busy_seconds;
	int di;

	di = dev_select[dn].position;

	busy_seconds = cur.snap_time - last.snap_time;

	if (devstat_compute_statistics(&cur.dinfo->devices[di],
	    &last.dinfo->devices[di], busy_seconds,
	    DSM_KB_PER_TRANSFER, &kb_per_transfer,
	    DSM_TRANSFERS_PER_SECOND, &transfers_per_second,
	    DSM_MB_PER_SECOND, &mb_per_second, DSM_NONE) != 0)
		errx(1, "%s", devstat_errbuf);

	if (numbers) {
		mvwprintw(wnd, row, _col, " %5.2Lf %3.0Lf %5.2Lf ",
			 kb_per_transfer, transfers_per_second,
			 mb_per_second);
		return(row);
	}
	wmove(wnd, row++, _col);
	histogram(mb_per_second, 50, .5);
	wmove(wnd, row++, _col);
	histogram(transfers_per_second, 50, .5);
	if (kbpt) {
		wmove(wnd, row++, _col);
		histogram(kb_per_transfer, 50, .5);
	}

	return(row);

}

static void
stat1(int row, int o)
{
	int i;
	double dtime;

	dtime = 0.0;
	for (i = 0; i < CPUSTATES; i++)
		dtime += cur.cp_time[i];
	if (dtime == 0.0)
		dtime = 1.0;
	wmove(wnd, row, INSET);
#define CPUSCALE	0.5
	histogram(100.0 * cur.cp_time[o] / dtime, 50, CPUSCALE);
}

static void
histogram(long double val, int colwidth, double scale)
{
	char buf[10];
	int k;
	int v = (int)(val * scale) + 0.5;

	k = MIN(v, colwidth);
	if (v > colwidth) {
		snprintf(buf, sizeof(buf), "%5.2Lf", val);
		k -= strlen(buf);
		while (k--)
			waddch(wnd, 'X');
		waddstr(wnd, buf);
		return;
	}
	while (k--)
		waddch(wnd, 'X');
	wclrtoeol(wnd);
}

int
cmdiostat(const char *cmd, const char *args)
{

	if (prefix(cmd, "kbpt"))
		kbpt = !kbpt;
	else if (prefix(cmd, "numbers"))
		numbers = 1;
	else if (prefix(cmd, "bars"))
		numbers = 0;
	else if (!dscmd(cmd, args, 100, &cur))
		return (0);
	wclear(wnd);
	labeliostat();
	refresh();
	return (1);
}
