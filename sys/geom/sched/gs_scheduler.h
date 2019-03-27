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

/*
 * $Id$
 * $FreeBSD$
 *
 * Prototypes for GEOM-based disk scheduling algorithms.
 * See g_sched.c for generic documentation.
 *
 * This file is used by the kernel modules implementing the various
 * scheduling algorithms. They should provide all the methods
 * defined in struct g_gsched, and also invoke the macro
 *	DECLARE_GSCHED_MODULE
 * which registers the scheduling algorithm with the geom_sched module.
 *
 * The various scheduling algorithms do not need to know anything
 * about geom, they only need to handle the 'bio' requests they
 * receive, pass them down when needed, and use the locking interface
 * defined below.
 */

#ifndef	_G_GSCHED_H_
#define	_G_GSCHED_H_

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <geom/geom.h>
#include "g_sched.h"

/*
 * This is the interface exported to scheduling modules.
 *
 * gs_init() is called when our scheduling algorithm
 *    starts being used by a geom 'sched'
 *
 * gs_fini() is called when the algorithm is released.
 *
 * gs_start() is called when a new request comes in. It should
 *    enqueue the request and return 0 if success, or return non-zero
 *    in case of failure (meaning the request is passed down).
 *    The scheduler can use bio->bio_caller1 to store a non-null
 *    pointer meaning the request is under its control.
 *
 * gs_next() is called in a loop by g_sched_dispatch(), right after
 *    gs_start(), or on timeouts or 'done' events. It should return
 *    immediately, either a pointer to the bio to be served or NULL
 *    if no bio should be served now.  If force is specified, a
 *    work-conserving behavior is expected.
 *
 * gs_done() is called when a request under service completes.
 *    In turn the scheduler may decide to call the dispatch loop
 *    to serve other pending requests (or make sure there is a pending
 *    timeout to avoid stalls).
 *
 * gs_init_class() is called when a new client (as determined by
 *    the classifier) starts being used.
 *
 * gs_hash_unref() is called right before the class hashtable is
 *    destroyed; after this call, the scheduler is supposed to hold no
 *    more references to the elements in the table.
 */

/* Forward declarations for prototypes. */
struct g_geom;
struct g_sched_class;

typedef void *gs_init_t (struct g_geom *geom);
typedef void gs_fini_t (void *data);
typedef int gs_start_t (void *data, struct bio *bio);
typedef void gs_done_t (void *data, struct bio *bio);
typedef struct bio *gs_next_t (void *data, int force);
typedef int gs_init_class_t (void *data, void *priv);
typedef void gs_fini_class_t (void *data, void *priv);
typedef void gs_hash_unref_t (void *data);

struct g_gsched {
	const char	*gs_name;
	int		gs_refs;
	int		gs_priv_size;

	gs_init_t	*gs_init;
	gs_fini_t	*gs_fini;
	gs_start_t	*gs_start;
	gs_done_t	*gs_done;
	gs_next_t	*gs_next;
	g_dumpconf_t	*gs_dumpconf;

	gs_init_class_t	*gs_init_class;
	gs_fini_class_t	*gs_fini_class;
	gs_hash_unref_t *gs_hash_unref;

	LIST_ENTRY(g_gsched) glist;
};

#define	KTR_GSCHED	KTR_SPARE4

MALLOC_DECLARE(M_GEOM_SCHED);

/*
 * Basic classification mechanism.  Each request is associated to
 * a g_sched_class, and each scheduler has the opportunity to set
 * its own private data for the given (class, geom) pair.  The
 * private data have a base type of g_sched_private, and are
 * extended at the end with the actual private fields of each
 * scheduler.
 */
struct g_sched_class {
	int	gsc_refs;
	int	gsc_expire;
	u_long	gsc_key;
	LIST_ENTRY(g_sched_class) gsc_clist;

	void	*gsc_priv[0];
};

/*
 * Manipulate the classifier's data.  g_sched_get_class() gets a reference
 * to the class corresponding to bp in gp, allocating and initializing
 * it if necessary.  g_sched_put_class() releases the reference.
 * The returned value points to the private data for the class.
 */
void *g_sched_get_class(struct g_geom *gp, struct bio *bp);
void g_sched_put_class(struct g_geom *gp, void *priv);

static inline struct g_sched_class *
g_sched_priv2class(void *priv)
{

	return ((struct g_sched_class *)((u_long)priv -
	    offsetof(struct g_sched_class, gsc_priv)));
}

static inline void
g_sched_priv_ref(void *priv)
{
	struct g_sched_class *gsc;

	gsc = g_sched_priv2class(priv);
	gsc->gsc_refs++;
}

/*
 * Locking interface.  When each operation registered with the
 * scheduler is invoked, a per-instance lock is taken to protect
 * the data associated with it.  If the scheduler needs something
 * else to access the same data (e.g., a callout) it must use
 * these functions.
 */
void g_sched_lock(struct g_geom *gp);
void g_sched_unlock(struct g_geom *gp);

/*
 * Restart request dispatching.  Must be called with the per-instance
 * mutex held.
 */
void g_sched_dispatch(struct g_geom *geom);

/*
 * Simple gathering of statistical data, used by schedulers to collect
 * info on process history.  Just keep an exponential average of the
 * samples, with some extra bits of precision.
 */
struct g_savg {
	uint64_t	gs_avg;
	unsigned int	gs_smpl;
};

static inline void
g_savg_add_sample(struct g_savg *ss, uint64_t sample)
{

	/* EMA with alpha = 0.125, fixed point, 3 bits of precision. */
	ss->gs_avg = sample + ss->gs_avg - (ss->gs_avg >> 3);
	ss->gs_smpl = 1 + ss->gs_smpl - (ss->gs_smpl >> 3);
}

static inline int
g_savg_valid(struct g_savg *ss)
{

	/* We want at least 8 samples to deem an average as valid. */
	return (ss->gs_smpl > 7);
}

static inline uint64_t
g_savg_read(struct g_savg *ss)
{

	return (ss->gs_avg / ss->gs_smpl);
}

/*
 * Declaration of a scheduler module.
 */
int g_gsched_modevent(module_t mod, int cmd, void *arg);

#define	DECLARE_GSCHED_MODULE(name, gsched)			\
	static moduledata_t name##_mod = {			\
		#name,						\
		g_gsched_modevent,				\
		gsched,						\
	};							\
	DECLARE_MODULE(name, name##_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE); \
	MODULE_DEPEND(name, geom_sched, 0, 0, 0);

#endif	/* _KERNEL */

#endif	/* _G_GSCHED_H_ */
