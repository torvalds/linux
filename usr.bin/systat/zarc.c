/*-
 * Copyright (c) 2014
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

#include <sys/types.h>
#include <sys/sysctl.h>

#include <inttypes.h>
#include <string.h>

#include "systat.h"
#include "extern.h"

struct zfield{
	uint64_t arcstats;
	uint64_t arcstats_demand_data;
	uint64_t arcstats_demand_metadata;
	uint64_t arcstats_prefetch_data;
	uint64_t arcstats_prefetch_metadata;
	uint64_t zfetchstats;
	uint64_t arcstats_l2;
	uint64_t vdev_cache_stats;
};

static struct zarcstats {
	struct zfield hits;
	struct zfield misses;
} curstat, initstat, oldstat;

static void
getinfo(struct zarcstats *ls);

WINDOW *
openzarc(void)
{
	return (subwin(stdscr, LINES-3-1, 0, MAINWIN_ROW, 0));
}

void
closezarc(WINDOW *w)
{
	if (w == NULL)
		return;
	wclear(w);
	wrefresh(w);
	delwin(w);
}

void
labelzarc(void)
{
	wmove(wnd, 0, 0); wclrtoeol(wnd);
	mvwprintw(wnd, 0, 31+1, "%4.4s %7.7s %7.7s %12.12s %12.12s",
		"rate", "hits", "misses", "total hits", "total misses");
#define L(row, str) mvwprintw(wnd, row, 5, str); \
	mvwprintw(wnd, row, 31, ":"); \
	mvwprintw(wnd, row, 31+4, "%%")
	L(1, "arcstats");
	L(2, "arcstats.demand_data");
	L(3, "arcstats.demand_metadata");
	L(4, "arcstats.prefetch_data");
	L(5, "arcstats.prefetch_metadata");
	L(6, "zfetchstats");
	L(7, "arcstats.l2");
	L(8, "vdev_cache_stats");
#undef L
}

static int calc(uint64_t hits, uint64_t misses)
{
    if( hits )
	return 100 * hits / ( hits + misses );
    else
	return 0;
}

static void
domode(struct zarcstats *delta, struct zarcstats *rate)
{
#define DO(stat) \
	delta->hits.stat = (curstat.hits.stat - oldstat.hits.stat); \
	delta->misses.stat = (curstat.misses.stat - oldstat.misses.stat); \
	rate->hits.stat = calc(delta->hits.stat, delta->misses.stat)
	DO(arcstats);
	DO(arcstats_demand_data);
	DO(arcstats_demand_metadata);
	DO(arcstats_prefetch_data);
	DO(arcstats_prefetch_metadata);
	DO(zfetchstats);
	DO(arcstats_l2);
	DO(vdev_cache_stats);
	DO(arcstats);
	DO(arcstats_demand_data);
	DO(arcstats_demand_metadata);
	DO(arcstats_prefetch_data);
	DO(arcstats_prefetch_metadata);
	DO(zfetchstats);
	DO(arcstats_l2);
	DO(vdev_cache_stats);
#undef DO
}

void
showzarc(void)
{
	struct zarcstats delta, rate;

	memset(&delta, 0, sizeof delta);
	memset(&rate, 0, sizeof rate);

	domode(&delta, &rate);

#define DO(stat, row, col, fmt) \
	mvwprintw(wnd, row, col, fmt, stat)
#define	R(row, stat) DO(rate.hits.stat, row, 31+1, "%3"PRIu64)
#define	H(row, stat) DO(delta.hits.stat, row, 31+1+5, "%7"PRIu64); \
	DO(curstat.hits.stat, row, 31+1+5+8+8, "%12"PRIu64)
#define	M(row, stat) DO(delta.misses.stat, row, 31+1+5+8, "%7"PRIu64); \
	DO(curstat.misses.stat, row, 31+1+5+8+8+13, "%12"PRIu64)
#define	E(row, stat) R(row, stat); H(row, stat); M(row, stat); 
	E(1, arcstats);
	E(2, arcstats_demand_data);
	E(3, arcstats_demand_metadata);
	E(4, arcstats_prefetch_data);
	E(5, arcstats_prefetch_metadata);
	E(6, zfetchstats);
	E(7, arcstats_l2);
	E(8, vdev_cache_stats);
#undef DO
#undef E
#undef M
#undef H
#undef R
}

int
initzarc(void)
{
	getinfo(&initstat);
	curstat = oldstat = initstat;
	return 1;
}

void
resetzarc(void)
{
	initzarc();
}

static void
getinfo(struct zarcstats *ls)
{
	size_t size = sizeof( ls->hits.arcstats );
	if ( sysctlbyname("kstat.zfs.misc.arcstats.hits",
		&ls->hits.arcstats, &size, NULL, 0 ) != 0 )
		return;
	GETSYSCTL("kstat.zfs.misc.arcstats.misses",
		ls->misses.arcstats);
	GETSYSCTL("kstat.zfs.misc.arcstats.demand_data_hits",
		ls->hits.arcstats_demand_data);
	GETSYSCTL("kstat.zfs.misc.arcstats.demand_data_misses",
		ls->misses.arcstats_demand_data);
	GETSYSCTL("kstat.zfs.misc.arcstats.demand_metadata_hits",
		ls->hits.arcstats_demand_metadata);
	GETSYSCTL("kstat.zfs.misc.arcstats.demand_metadata_misses",
		ls->misses.arcstats_demand_metadata);
	GETSYSCTL("kstat.zfs.misc.arcstats.prefetch_data_hits",
		ls->hits.arcstats_prefetch_data);
	GETSYSCTL("kstat.zfs.misc.arcstats.prefetch_data_misses",
		ls->misses.arcstats_prefetch_data);
	GETSYSCTL("kstat.zfs.misc.arcstats.prefetch_metadata_hits",
		ls->hits.arcstats_prefetch_metadata);
	GETSYSCTL("kstat.zfs.misc.arcstats.prefetch_metadata_misses",
		ls->misses.arcstats_prefetch_metadata);
	GETSYSCTL("kstat.zfs.misc.zfetchstats.hits",
		ls->hits.zfetchstats);
	GETSYSCTL("kstat.zfs.misc.zfetchstats.misses",
		ls->misses.zfetchstats);
	GETSYSCTL("kstat.zfs.misc.arcstats.l2_hits",
		ls->hits.arcstats_l2);
	GETSYSCTL("kstat.zfs.misc.arcstats.l2_misses",
		ls->misses.arcstats_l2);
	GETSYSCTL("kstat.zfs.misc.vdev_cache_stats.hits",
		ls->hits.vdev_cache_stats);
	GETSYSCTL("kstat.zfs.misc.vdev_cache_stats.misses",
		ls->misses.vdev_cache_stats);
}

void
fetchzarc(void)
{
	oldstat = curstat;
	getinfo(&curstat);
}
