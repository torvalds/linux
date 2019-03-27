/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 Fabio Checconi
 * Copyright (c) 2009-2010 Luigi Rizzo, Universita` di Pisa
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef	_G_SCHED_H_
#define	_G_SCHED_H_

/*
 * $Id$
 * $FreeBSD$
 *
 * Header for the geom_sched class (userland library and kernel part).
 * See g_sched.c for documentation.
 * The userland code only needs the three G_SCHED_* values below.
 */

#define	G_SCHED_CLASS_NAME	"SCHED"
#define	G_SCHED_VERSION		0
#define	G_SCHED_SUFFIX		".sched."

#ifdef _KERNEL
#define	G_SCHED_DEBUG(lvl, ...)	do {				\
	if (me.gs_debug >= (lvl)) {				\
		printf("GEOM_SCHED");				\
		if (me.gs_debug > 0)				\
			printf("[%u]", lvl);			\
		printf(": ");					\
		printf(__VA_ARGS__);				\
		printf("\n");					\
	}							\
} while (0)

#define	G_SCHED_LOGREQ(bp, ...)	do {				\
	if (me.gs_debug >= 2) {					\
		printf("GEOM_SCHED[2]: ");			\
		printf(__VA_ARGS__);				\
		printf(" ");					\
		g_print_bio(bp);				\
		printf("\n");					\
	}							\
} while (0)

LIST_HEAD(g_hash, g_sched_class);

/*
 * Descriptor of a scheduler.
 * In addition to the obvious fields, sc_flushing and sc_pending
 * support dynamic switching of scheduling algorithm.
 * Normally, sc_flushing is 0, and requests that are scheduled are
 * also added to the sc_pending queue, and removed when we receive
 * the 'done' event.
 *
 * When we are transparently inserted on an existing provider,
 * sc_proxying is set. The detach procedure is slightly different.
 *
 * When switching schedulers, sc_flushing is set so requests bypass us,
 * and at the same time we update the pointer in the pending bios
 * to ignore us when they return up.
 * XXX it would be more efficient to implement sc_pending with
 * a generation number: the softc generation is increased when
 * we change scheduling algorithm, we store the current generation
 * number in the pending bios, and when they come back we ignore
 * the done() call if the generation number do not match.
 */
struct g_sched_softc {
	/*
	 * Generic fields used by any scheduling algorithm:
	 * a mutex, the class descriptor, flags, list of pending
	 * requests (used when flushing the module) and support
	 * for hash tables where we store per-flow queues.
	 */
	struct mtx	sc_mtx;
	struct g_gsched	*sc_gsched;	/* Scheduler descriptor. */
	int		sc_pending;	/* Pending requests. */
	int		sc_flags;	/* Various flags. */

	/*
	 * Hash tables to store per-flow queues are generally useful
	 * so we handle them in the common code.
	 * sc_hash and sc_mask are parameters of the hash table,
	 * the last two fields are used to periodically remove
	 * expired items from the hash table.
	 */
	struct g_hash	*sc_hash;
	u_long		sc_mask;
	int		sc_flush_ticks;	/* Next tick for a flush. */
	int		sc_flush_bucket; /* Next bucket to flush. */

	/*
	 * Pointer to the algorithm's private data, which is the value
	 * returned by sc_gsched->gs_init() . A NULL here means failure.
	 * XXX intptr_t might be more appropriate.
	 */
	void		*sc_data;
};

#define	G_SCHED_PROXYING	1
#define	G_SCHED_FLUSHING	2

#endif	/* _KERNEL */

#endif	/* _G_SCHED_H_ */
