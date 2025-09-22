/*	$OpenBSD: gmon.c,v 1.40 2025/08/19 02:34:31 jsg Exp $ */
/*-
 * Copyright (c) 1983, 1992, 1993
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

/*
 * Copyright (c) 2003, 2004 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Nathan J. Williams for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/time.h>
#include <sys/gmon.h>
#include <sys/mman.h>
#include <sys/sysctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

struct gmonparam _gmonparam = { GMON_PROF_OFF };

#include <pthread.h>
#include <thread_private.h>

static SLIST_HEAD(, gmonparam) _gmonfree = SLIST_HEAD_INITIALIZER(_gmonfree);
static SLIST_HEAD(, gmonparam) _gmoninuse = SLIST_HEAD_INITIALIZER(_gmoninuse);
_THREAD_PRIVATE_MUTEX(_gmonlock);
static pthread_key_t _gmonkey;

static int	s_scale;
/* see profil(2) where this is describe (incorrectly) */
#define		SCALE_1_TO_1	0x10000L

#define ERR(s) write(STDERR_FILENO, s, sizeof(s))

PROTO_NORMAL(_gmon_alloc);
PROTO_NORMAL(moncontrol);

#define PAGESIZE	(1UL << _MAX_PAGE_SHIFT)
#define PAGEMASK	(PAGESIZE - 1)
#define PAGEROUND(x)	(((x) + (PAGEMASK)) & ~PAGEMASK)

static void
_gmon_destructor(void *arg)
{
	struct gmonparam *p = arg;

	_THREAD_PRIVATE_MUTEX_LOCK(_gmonlock);
	SLIST_REMOVE(&_gmoninuse, p, gmonparam, list);
	SLIST_INSERT_HEAD(&_gmonfree, p, list);
	_THREAD_PRIVATE_MUTEX_UNLOCK(_gmonlock);

	pthread_setspecific(_gmonkey, NULL);
}

void
_monstartup(u_long lowpc, u_long highpc)
{
	int o;
	struct gmonparam *p = &_gmonparam;
	char *profdir = NULL;
	char *a;

	/*
	 * round lowpc and highpc to multiples of the density we're using
	 * so the rest of the scaling (here and in gprof) stays in ints.
	 */
	p->lowpc = ROUNDDOWN(lowpc, HISTFRACTION * sizeof(HISTCOUNTER));
	p->highpc = ROUNDUP(highpc, HISTFRACTION * sizeof(HISTCOUNTER));
	p->textsize = p->highpc - p->lowpc;
	p->kcountsize = p->textsize / HISTFRACTION;
	p->hashfraction = HASHFRACTION;
	p->fromssize = p->textsize / p->hashfraction;
	p->tolimit = p->textsize * ARCDENSITY / 100;
	if (p->tolimit < MINARCS)
		p->tolimit = MINARCS;
	else if (p->tolimit > MAXARCS)
		p->tolimit = MAXARCS;
	p->tossize = p->tolimit * sizeof(struct tostruct);

	p->outbuflen = sizeof(struct gmonhdr) + p->kcountsize +
	    MAXARCS * sizeof(struct rawarc);

	/* Create a contig output buffer, with froms/tos tables after */
	a = mmap(NULL,
	    PAGEROUND(p->outbuflen) + _ALIGN(p->fromssize) + _ALIGN(p->tossize),
	    PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
	if (a == MAP_FAILED) {
		ERR("_monstartup: out of memory\n");
		return;
	}
	p->outbuf = a;
	p->kcount = (void *)(a + sizeof(struct gmonhdr));
	p->rawarcs = (void *)(a + sizeof(struct gmonhdr) + p->kcountsize);
	p->froms = (void *)(a + PAGEROUND(p->outbuflen));
	p->tos = (void *)(a + PAGEROUND(p->outbuflen) + _ALIGN(p->fromssize));

	o = p->highpc - p->lowpc;
	if (p->kcountsize < o) {
#ifndef notdef
		s_scale = ((float)p->kcountsize / o ) * SCALE_1_TO_1;
#else /* avoid floating point */
		int quot = o / p->kcountsize;

		if (quot >= 0x10000)
			s_scale = 1;
		else if (quot >= 0x100)
			s_scale = 0x10000 / quot;
		else if (o >= 0x800000)
			s_scale = 0x1000000 / (o / (p->kcountsize >> 8));
		else
			s_scale = 0x1000000 / ((o << 8) / p->kcountsize);
#endif
	} else
		s_scale = SCALE_1_TO_1;

	if (issetugid() == 0)
		profdir = getenv("PROFDIR");
	if (profdir)
		p->dirfd = open(profdir, O_DIRECTORY, 0);
	else
		p->dirfd = -1;

	pthread_key_create(&_gmonkey, _gmon_destructor);

	moncontrol(1);

	if (p->dirfd != -1)
		close(p->dirfd);
}

struct gmonparam *
_gmon_alloc(void)
{
	struct gmonparam *p;
	char *a;

	if (_gmonparam.state == GMON_PROF_OFF)
		return NULL;

	_THREAD_PRIVATE_MUTEX_LOCK(_gmonlock);
	p = SLIST_FIRST(&_gmonfree);
	if (p != NULL) {
		SLIST_REMOVE_HEAD(&_gmonfree, list);
		SLIST_INSERT_HEAD(&_gmoninuse, p, list);
	} else {
		_THREAD_PRIVATE_MUTEX_UNLOCK(_gmonlock);
		a = mmap(NULL,
			 _ALIGN(sizeof(*p)) + _ALIGN(_gmonparam.fromssize) +
			 _ALIGN(_gmonparam.tossize),
			 PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
		if (a == MAP_FAILED) {
			pthread_setspecific(_gmonkey, NULL);
			ERR("_gmon_alloc: out of memory\n");
			return NULL;
		}
		p = (struct gmonparam *)a;
		*p = _gmonparam;
		p->kcount = NULL;
		p->kcountsize = 0;
		p->froms = (void *)(a + _ALIGN(sizeof(*p)));
		p->tos = (void *)(a + _ALIGN(sizeof(*p)) +
		    _ALIGN(_gmonparam.fromssize));
		_THREAD_PRIVATE_MUTEX_LOCK(_gmonlock);
		SLIST_INSERT_HEAD(&_gmoninuse, p, list);
	}
	_THREAD_PRIVATE_MUTEX_UNLOCK(_gmonlock);
	pthread_setspecific(_gmonkey, p);

	return p;
}
DEF_WEAK(_gmon_alloc);

static void
_gmon_merge_two(struct gmonparam *p, struct gmonparam *q)
{
	u_long fromindex, selfpc, endfrom;
	u_short *frompcindex, qtoindex, toindex;
	long count;
	struct tostruct *top;

	endfrom = (q->fromssize / sizeof(*q->froms));
	for (fromindex = 0; fromindex < endfrom; fromindex++) {
		if (q->froms[fromindex] == 0)
			continue;
		for (qtoindex = q->froms[fromindex]; qtoindex != 0;
		     qtoindex = q->tos[qtoindex].link) {
			selfpc = q->tos[qtoindex].selfpc;
			count = q->tos[qtoindex].count;
			/* cribbed from mcount */
			frompcindex = &p->froms[fromindex];
			toindex = *frompcindex;
			if (toindex == 0) {
				/*
				 *      first time traversing this arc
				 */
				toindex = ++p->tos[0].link;
				if (toindex >= p->tolimit)
					/* halt further profiling */
					goto overflow;

				*frompcindex = (u_short)toindex;
				top = &p->tos[(size_t)toindex];
				top->selfpc = selfpc;
				top->count = count;
				top->link = 0;
				goto done;
			}
			top = &p->tos[(size_t)toindex];
			if (top->selfpc == selfpc) {
				/*
				 * arc at front of chain; usual case.
				 */
				top->count+= count;
				goto done;
			}
			/*
			 * have to go looking down chain for it.
			 * top points to what we are looking at,
			 * we know it is not at the head of the chain.
			 */
			for (; /* goto done */; ) {
				if (top->link == 0) {
					/*
					 * top is end of the chain and
					 * none of the chain had
					 * top->selfpc == selfpc.  so
					 * we allocate a new tostruct
					 * and link it to the head of
					 * the chain.
					 */
					toindex = ++p->tos[0].link;
					if (toindex >= p->tolimit)
						goto overflow;
					top = &p->tos[(size_t)toindex];
					top->selfpc = selfpc;
					top->count = count;
					top->link = *frompcindex;
					*frompcindex = (u_short)toindex;
					goto done;
				}
				/*
				 * otherwise, check the next arc on the chain.
				 */
				top = &p->tos[top->link];
				if (top->selfpc == selfpc) {
					/*
					 * there it is.
					 * add to its count.
					 */
					top->count += count;
					goto done;
				}

			}

		done: ;
		}

	}
overflow: ;

}

static void
_gmon_merge(void)
{
	struct gmonparam *q;

	_THREAD_PRIVATE_MUTEX_LOCK(_gmonlock);

	SLIST_FOREACH(q, &_gmonfree, list)
		_gmon_merge_two(&_gmonparam, q);

	SLIST_FOREACH(q, &_gmoninuse, list) {
		q->state = GMON_PROF_OFF;
		_gmon_merge_two(&_gmonparam, q);
	}

	_THREAD_PRIVATE_MUTEX_UNLOCK(_gmonlock);
}


void
_mcleanup(void)
{
	int fromindex, endfrom, totarc = 0, toindex;
	u_long frompc;
	struct rawarc rawarc;
	struct gmonparam *p = &_gmonparam;
	struct gmonhdr *hdr;
	struct clockinfo clockinfo;
	const int mib[2] = { CTL_KERN, KERN_CLOCKRATE };
	size_t size;
#ifdef DEBUG
	int log, len;
	char dbuf[200];
#endif

	if (p->state == GMON_PROF_ERROR)
		ERR("_mcleanup: tos overflow\n");

	/*
	 * There is nothing we can do if sysctl(2) fails or if
	 * clockinfo.hz is unset.
	 */
	size = sizeof(clockinfo);
	if (sysctl(mib, 2, &clockinfo, &size, NULL, 0) == -1) {
		clockinfo.profhz = 0;
	} else if (clockinfo.profhz == 0) {
		clockinfo.profhz = clockinfo.hz;	/* best guess */
	}

	moncontrol(0);

#ifdef DEBUG
	log = open("gmon.log", O_CREAT|O_TRUNC|O_WRONLY, 0664);
	if (log == -1) {
		perror("mcount: gmon.log");
		close(fd);
		return;
	}
	snprintf(dbuf, sizeof dbuf, "[mcleanup1] kcount 0x%x ssiz %d\n",
	    p->kcount, p->kcountsize);
	write(log, dbuf, strlen(dbuf));
#endif

	_gmon_merge();

	hdr = (struct gmonhdr *)p->outbuf;
	hdr->lpc = p->lowpc;
	hdr->hpc = p->highpc;
	hdr->ncnt = p->kcountsize + sizeof(*hdr);
	hdr->version = GMONVERSION;
	hdr->profrate = clockinfo.profhz;
	endfrom = p->fromssize / sizeof(*p->froms);

	for (fromindex = 0; fromindex < endfrom; fromindex++) {
		if (p->froms[fromindex] == 0)
			continue;
		frompc = p->lowpc;
		frompc += fromindex * p->hashfraction * sizeof(*p->froms);
		for (toindex = p->froms[fromindex]; toindex != 0;
		     toindex = p->tos[toindex].link) {
#ifdef DEBUG
			(void) snprintf(dbuf, sizeof dbuf,
			"[mcleanup2] frompc 0x%x selfpc 0x%x count %d\n" ,
				frompc, p->tos[toindex].selfpc,
				p->tos[toindex].count);
			write(log, dbuf, strlen(dbuf));
#endif
			rawarc.raw_frompc = frompc;
			rawarc.raw_selfpc = p->tos[toindex].selfpc;
			rawarc.raw_count = p->tos[toindex].count;
			memcpy(&p->rawarcs[totarc * sizeof(struct rawarc)],
			    &rawarc, sizeof rawarc);
			totarc++;
			if (totarc >= MAXARCS)
				goto donearcs;
		}
	}
donearcs:
	/*
	 * Update field in header.  Kernel will use this to write
	 * out a smaller amount of arcs than originally allocated
	 */
	hdr->totarc = totarc * sizeof(struct rawarc);
}

/*
 * Control profiling
 *	profiling is what mcount checks to see if
 *	all the data structures are ready.
 */
void
moncontrol(int mode)
{
	struct gmonparam *p = &_gmonparam;

	if (mode) {
		/* start */
		profil(p->outbuf, p->outbuflen, p->kcountsize, p->lowpc,
		    s_scale, p->dirfd);
		p->state = GMON_PROF_ON;
	} else {
		/* stop */
		profil(NULL, 0, 0, 0, 0, -1);
		p->state = GMON_PROF_OFF;
	}
}
DEF_WEAK(moncontrol);
