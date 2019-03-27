/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Poul-Henning Kamp
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/devicestat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgeom.h>

/************************************************************/
static uint npages, spp;
static int pagesize, statsfd = -1;
static u_char *statp;

void
geom_stats_close(void)
{
	if (statsfd == -1)
		return;
	munmap(statp, npages *pagesize);
	statp = NULL;
	close (statsfd);
	statsfd = -1;
}

void
geom_stats_resync(void)
{
	void *p;

	if (statsfd == -1)
		return;
	for (;;) {
		p = mmap(statp, (npages + 1) * pagesize,
		    PROT_READ, MAP_SHARED, statsfd, 0);
		if (p == MAP_FAILED)
			break;
		else
			statp = p;
		npages++;
	}
}

int
geom_stats_open(void)
{
	int error;
	void *p;

	if (statsfd != -1)
		return (EBUSY);
	statsfd = open(_PATH_DEV DEVSTAT_DEVICE_NAME, O_RDONLY);
	if (statsfd < 0)
		return (errno);
	pagesize = getpagesize();
	spp = pagesize / sizeof(struct devstat);
	p = mmap(NULL, pagesize, PROT_READ, MAP_SHARED, statsfd, 0);
	if (p == MAP_FAILED) {
		error = errno;
		close(statsfd);
		statsfd = -1;
		errno = error;
		return (error);
	}
	statp = p;
	npages = 1;
	geom_stats_resync();
	return (0);
}

struct snapshot {
	u_char		*ptr;
	uint		pages;
	uint		pagesize;
	uint		perpage;
	struct timespec	time;
	/* used by getnext: */
	uint		u, v;
};

void *
geom_stats_snapshot_get(void)
{
	struct snapshot *sp;

	sp = malloc(sizeof *sp);
	if (sp == NULL)
		return (NULL);
	memset(sp, 0, sizeof *sp);
	sp->ptr = malloc(pagesize * npages);
	if (sp->ptr == NULL) {
		free(sp);
		return (NULL);
	}
	memset(sp->ptr, 0, pagesize * npages); 	/* page in, cache */
	clock_gettime(CLOCK_REALTIME, &sp->time);
	memset(sp->ptr, 0, pagesize * npages); 	/* page in, cache */
	memcpy(sp->ptr, statp, pagesize * npages);
	sp->pages = npages;
	sp->perpage = spp;
	sp->pagesize = pagesize;
	return (sp);
}

void
geom_stats_snapshot_free(void *arg)
{
	struct snapshot *sp;

	sp = arg;
	free(sp->ptr);
	free(sp);
}

void
geom_stats_snapshot_timestamp(void *arg, struct timespec *tp)
{
	struct snapshot *sp;

	sp = arg;
	*tp = sp->time;
}

void
geom_stats_snapshot_reset(void *arg)
{
	struct snapshot *sp;

	sp = arg;
	sp->u = sp->v = 0;
}

struct devstat *
geom_stats_snapshot_next(void *arg)
{
	struct devstat *gsp;
	struct snapshot *sp;

	sp = arg;
	gsp = (struct devstat *)
	    (sp->ptr + sp->u * pagesize + sp->v * sizeof *gsp);
	if (++sp->v >= sp->perpage) {
		if (++sp->u >= sp->pages)
			return (NULL);
		else
			sp->v = 0;
	}
	return (gsp);
}
