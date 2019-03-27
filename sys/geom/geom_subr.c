/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Poul-Henning Kamp
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Poul-Henning Kamp
 * and NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/devicestat.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bio.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/errno.h>
#include <sys/sbuf.h>
#include <geom/geom.h>
#include <geom/geom_int.h>
#include <machine/stdarg.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#ifdef KDB
#include <sys/kdb.h>
#endif

struct class_list_head g_classes = LIST_HEAD_INITIALIZER(g_classes);
static struct g_tailq_head geoms = TAILQ_HEAD_INITIALIZER(geoms);
char *g_wait_event, *g_wait_up, *g_wait_down, *g_wait_sim;

struct g_hh00 {
	struct g_class		*mp;
	struct g_provider	*pp;
	off_t			size;
	int			error;
	int			post;
};

/*
 * This event offers a new class a chance to taste all preexisting providers.
 */
static void
g_load_class(void *arg, int flag)
{
	struct g_hh00 *hh;
	struct g_class *mp2, *mp;
	struct g_geom *gp;
	struct g_provider *pp;

	g_topology_assert();
	if (flag == EV_CANCEL)	/* XXX: can't happen ? */
		return;
	if (g_shutdown)
		return;

	hh = arg;
	mp = hh->mp;
	hh->error = 0;
	if (hh->post) {
		g_free(hh);
		hh = NULL;
	}
	g_trace(G_T_TOPOLOGY, "g_load_class(%s)", mp->name);
	KASSERT(mp->name != NULL && *mp->name != '\0',
	    ("GEOM class has no name"));
	LIST_FOREACH(mp2, &g_classes, class) {
		if (mp2 == mp) {
			printf("The GEOM class %s is already loaded.\n",
			    mp2->name);
			if (hh != NULL)
				hh->error = EEXIST;
			return;
		} else if (strcmp(mp2->name, mp->name) == 0) {
			printf("A GEOM class %s is already loaded.\n",
			    mp2->name);
			if (hh != NULL)
				hh->error = EEXIST;
			return;
		}
	}

	LIST_INIT(&mp->geom);
	LIST_INSERT_HEAD(&g_classes, mp, class);
	if (mp->init != NULL)
		mp->init(mp);
	if (mp->taste == NULL)
		return;
	LIST_FOREACH(mp2, &g_classes, class) {
		if (mp == mp2)
			continue;
		LIST_FOREACH(gp, &mp2->geom, geom) {
			LIST_FOREACH(pp, &gp->provider, provider) {
				mp->taste(mp, pp, 0);
				g_topology_assert();
			}
		}
	}
}

static int
g_unload_class(struct g_class *mp)
{
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_consumer *cp;
	int error;

	g_topology_lock();
	g_trace(G_T_TOPOLOGY, "g_unload_class(%s)", mp->name);
retry:
	G_VALID_CLASS(mp);
	LIST_FOREACH(gp, &mp->geom, geom) {
		/* We refuse to unload if anything is open */
		LIST_FOREACH(pp, &gp->provider, provider)
			if (pp->acr || pp->acw || pp->ace) {
				g_topology_unlock();
				return (EBUSY);
			}
		LIST_FOREACH(cp, &gp->consumer, consumer)
			if (cp->acr || cp->acw || cp->ace) {
				g_topology_unlock();
				return (EBUSY);
			}
		/* If the geom is withering, wait for it to finish. */
		if (gp->flags & G_GEOM_WITHER) {
			g_topology_sleep(mp, 1);
			goto retry;
		}
	}

	/*
	 * We allow unloading if we have no geoms, or a class
	 * method we can use to get rid of them.
	 */
	if (!LIST_EMPTY(&mp->geom) && mp->destroy_geom == NULL) {
		g_topology_unlock();
		return (EOPNOTSUPP);
	}

	/* Bar new entries */
	mp->taste = NULL;
	mp->config = NULL;

	LIST_FOREACH(gp, &mp->geom, geom) {
		error = mp->destroy_geom(NULL, mp, gp);
		if (error != 0) {
			g_topology_unlock();
			return (error);
		}
	}
	/* Wait for withering to finish. */
	for (;;) {
		gp = LIST_FIRST(&mp->geom);
		if (gp == NULL)
			break;
		KASSERT(gp->flags & G_GEOM_WITHER,
		   ("Non-withering geom in class %s", mp->name));
		g_topology_sleep(mp, 1);
	}
	G_VALID_CLASS(mp);
	if (mp->fini != NULL)
		mp->fini(mp);
	LIST_REMOVE(mp, class);
	g_topology_unlock();

	return (0);
}

int
g_modevent(module_t mod, int type, void *data)
{
	struct g_hh00 *hh;
	int error;
	static int g_ignition;
	struct g_class *mp;

	mp = data;
	if (mp->version != G_VERSION) {
		printf("GEOM class %s has Wrong version %x\n",
		    mp->name, mp->version);
		return (EINVAL);
	}
	if (!g_ignition) {
		g_ignition++;
		g_init();
	}
	error = EOPNOTSUPP;
	switch (type) {
	case MOD_LOAD:
		g_trace(G_T_TOPOLOGY, "g_modevent(%s, LOAD)", mp->name);
		hh = g_malloc(sizeof *hh, M_WAITOK | M_ZERO);
		hh->mp = mp;
		/*
		 * Once the system is not cold, MOD_LOAD calls will be
		 * from the userland and the g_event thread will be able
		 * to acknowledge their completion.
		 */
		if (cold) {
			hh->post = 1;
			error = g_post_event(g_load_class, hh, M_WAITOK, NULL);
		} else {
			error = g_waitfor_event(g_load_class, hh, M_WAITOK,
			    NULL);
			if (error == 0)
				error = hh->error;
			g_free(hh);
		}
		break;
	case MOD_UNLOAD:
		g_trace(G_T_TOPOLOGY, "g_modevent(%s, UNLOAD)", mp->name);
		error = g_unload_class(mp);
		if (error == 0) {
			KASSERT(LIST_EMPTY(&mp->geom),
			    ("Unloaded class (%s) still has geom", mp->name));
		}
		break;
	}
	return (error);
}

static void
g_retaste_event(void *arg, int flag)
{
	struct g_class *mp, *mp2;
	struct g_geom *gp;
	struct g_hh00 *hh;
	struct g_provider *pp;
	struct g_consumer *cp;

	g_topology_assert();
	if (flag == EV_CANCEL)  /* XXX: can't happen ? */
		return;
	if (g_shutdown || g_notaste)
		return;

	hh = arg;
	mp = hh->mp;
	hh->error = 0;
	if (hh->post) {
		g_free(hh);
		hh = NULL;
	}
	g_trace(G_T_TOPOLOGY, "g_retaste(%s)", mp->name);

	LIST_FOREACH(mp2, &g_classes, class) {
		LIST_FOREACH(gp, &mp2->geom, geom) {
			LIST_FOREACH(pp, &gp->provider, provider) {
				if (pp->acr || pp->acw || pp->ace)
					continue;
				LIST_FOREACH(cp, &pp->consumers, consumers) {
					if (cp->geom->class == mp &&
					    (cp->flags & G_CF_ORPHAN) == 0)
						break;
				}
				if (cp != NULL) {
					cp->flags |= G_CF_ORPHAN;
					g_wither_geom(cp->geom, ENXIO);
				}
				mp->taste(mp, pp, 0);
				g_topology_assert();
			}
		}
	}
}

int
g_retaste(struct g_class *mp)
{
	struct g_hh00 *hh;
	int error;

	if (mp->taste == NULL)
		return (EINVAL);

	hh = g_malloc(sizeof *hh, M_WAITOK | M_ZERO);
	hh->mp = mp;

	if (cold) {
		hh->post = 1;
		error = g_post_event(g_retaste_event, hh, M_WAITOK, NULL);
	} else {
		error = g_waitfor_event(g_retaste_event, hh, M_WAITOK, NULL);
		if (error == 0)
			error = hh->error;
		g_free(hh);
	}

	return (error);
}

struct g_geom *
g_new_geomf(struct g_class *mp, const char *fmt, ...)
{
	struct g_geom *gp;
	va_list ap;
	struct sbuf *sb;

	g_topology_assert();
	G_VALID_CLASS(mp);
	sb = sbuf_new_auto();
	va_start(ap, fmt);
	sbuf_vprintf(sb, fmt, ap);
	va_end(ap);
	sbuf_finish(sb);
	gp = g_malloc(sizeof *gp, M_WAITOK | M_ZERO);
	gp->name = g_malloc(sbuf_len(sb) + 1, M_WAITOK | M_ZERO);
	gp->class = mp;
	gp->rank = 1;
	LIST_INIT(&gp->consumer);
	LIST_INIT(&gp->provider);
	LIST_INIT(&gp->aliases);
	LIST_INSERT_HEAD(&mp->geom, gp, geom);
	TAILQ_INSERT_HEAD(&geoms, gp, geoms);
	strcpy(gp->name, sbuf_data(sb));
	sbuf_delete(sb);
	/* Fill in defaults from class */
	gp->start = mp->start;
	gp->spoiled = mp->spoiled;
	gp->attrchanged = mp->attrchanged;
	gp->providergone = mp->providergone;
	gp->dumpconf = mp->dumpconf;
	gp->access = mp->access;
	gp->orphan = mp->orphan;
	gp->ioctl = mp->ioctl;
	gp->resize = mp->resize;
	return (gp);
}

void
g_destroy_geom(struct g_geom *gp)
{
	struct g_geom_alias *gap, *gaptmp;

	g_topology_assert();
	G_VALID_GEOM(gp);
	g_trace(G_T_TOPOLOGY, "g_destroy_geom(%p(%s))", gp, gp->name);
	KASSERT(LIST_EMPTY(&gp->consumer),
	    ("g_destroy_geom(%s) with consumer(s) [%p]",
	    gp->name, LIST_FIRST(&gp->consumer)));
	KASSERT(LIST_EMPTY(&gp->provider),
	    ("g_destroy_geom(%s) with provider(s) [%p]",
	    gp->name, LIST_FIRST(&gp->provider)));
	g_cancel_event(gp);
	LIST_REMOVE(gp, geom);
	TAILQ_REMOVE(&geoms, gp, geoms);
	LIST_FOREACH_SAFE(gap, &gp->aliases, ga_next, gaptmp)
		g_free(gap);
	g_free(gp->name);
	g_free(gp);
}

/*
 * This function is called (repeatedly) until the geom has withered away.
 */
void
g_wither_geom(struct g_geom *gp, int error)
{
	struct g_provider *pp;

	g_topology_assert();
	G_VALID_GEOM(gp);
	g_trace(G_T_TOPOLOGY, "g_wither_geom(%p(%s))", gp, gp->name);
	if (!(gp->flags & G_GEOM_WITHER)) {
		gp->flags |= G_GEOM_WITHER;
		LIST_FOREACH(pp, &gp->provider, provider)
			if (!(pp->flags & G_PF_ORPHAN))
				g_orphan_provider(pp, error);
	}
	g_do_wither();
}

/*
 * Convenience function to destroy a particular provider.
 */
void
g_wither_provider(struct g_provider *pp, int error)
{

	pp->flags |= G_PF_WITHER;
	if (!(pp->flags & G_PF_ORPHAN))
		g_orphan_provider(pp, error);
}

/*
 * This function is called (repeatedly) until the has withered away.
 */
void
g_wither_geom_close(struct g_geom *gp, int error)
{
	struct g_consumer *cp;

	g_topology_assert();
	G_VALID_GEOM(gp);
	g_trace(G_T_TOPOLOGY, "g_wither_geom_close(%p(%s))", gp, gp->name);
	LIST_FOREACH(cp, &gp->consumer, consumer)
		if (cp->acr || cp->acw || cp->ace)
			g_access(cp, -cp->acr, -cp->acw, -cp->ace);
	g_wither_geom(gp, error);
}

/*
 * This function is called (repeatedly) until we cant wash away more
 * withered bits at present.
 */
void
g_wither_washer()
{
	struct g_class *mp;
	struct g_geom *gp, *gp2;
	struct g_provider *pp, *pp2;
	struct g_consumer *cp, *cp2;

	g_topology_assert();
	LIST_FOREACH(mp, &g_classes, class) {
		LIST_FOREACH_SAFE(gp, &mp->geom, geom, gp2) {
			LIST_FOREACH_SAFE(pp, &gp->provider, provider, pp2) {
				if (!(pp->flags & G_PF_WITHER))
					continue;
				if (LIST_EMPTY(&pp->consumers))
					g_destroy_provider(pp);
			}
			if (!(gp->flags & G_GEOM_WITHER))
				continue;
			LIST_FOREACH_SAFE(pp, &gp->provider, provider, pp2) {
				if (LIST_EMPTY(&pp->consumers))
					g_destroy_provider(pp);
			}
			LIST_FOREACH_SAFE(cp, &gp->consumer, consumer, cp2) {
				if (cp->acr || cp->acw || cp->ace)
					continue;
				if (cp->provider != NULL)
					g_detach(cp);
				g_destroy_consumer(cp);
			}
			if (LIST_EMPTY(&gp->provider) &&
			    LIST_EMPTY(&gp->consumer))
				g_destroy_geom(gp);
		}
	}
}

struct g_consumer *
g_new_consumer(struct g_geom *gp)
{
	struct g_consumer *cp;

	g_topology_assert();
	G_VALID_GEOM(gp);
	KASSERT(!(gp->flags & G_GEOM_WITHER),
	    ("g_new_consumer on WITHERing geom(%s) (class %s)",
	    gp->name, gp->class->name));
	KASSERT(gp->orphan != NULL,
	    ("g_new_consumer on geom(%s) (class %s) without orphan",
	    gp->name, gp->class->name));

	cp = g_malloc(sizeof *cp, M_WAITOK | M_ZERO);
	cp->geom = gp;
	cp->stat = devstat_new_entry(cp, -1, 0, DEVSTAT_ALL_SUPPORTED,
	    DEVSTAT_TYPE_DIRECT, DEVSTAT_PRIORITY_MAX);
	LIST_INSERT_HEAD(&gp->consumer, cp, consumer);
	return(cp);
}

void
g_destroy_consumer(struct g_consumer *cp)
{
	struct g_geom *gp;

	g_topology_assert();
	G_VALID_CONSUMER(cp);
	g_trace(G_T_TOPOLOGY, "g_destroy_consumer(%p)", cp);
	KASSERT (cp->provider == NULL, ("g_destroy_consumer but attached"));
	KASSERT (cp->acr == 0, ("g_destroy_consumer with acr"));
	KASSERT (cp->acw == 0, ("g_destroy_consumer with acw"));
	KASSERT (cp->ace == 0, ("g_destroy_consumer with ace"));
	g_cancel_event(cp);
	gp = cp->geom;
	LIST_REMOVE(cp, consumer);
	devstat_remove_entry(cp->stat);
	g_free(cp);
	if (gp->flags & G_GEOM_WITHER)
		g_do_wither();
}

static void
g_new_provider_event(void *arg, int flag)
{
	struct g_class *mp;
	struct g_provider *pp;
	struct g_consumer *cp, *next_cp;

	g_topology_assert();
	if (flag == EV_CANCEL)
		return;
	if (g_shutdown)
		return;
	pp = arg;
	G_VALID_PROVIDER(pp);
	KASSERT(!(pp->flags & G_PF_WITHER),
	    ("g_new_provider_event but withered"));
	LIST_FOREACH_SAFE(cp, &pp->consumers, consumers, next_cp) {
		if ((cp->flags & G_CF_ORPHAN) == 0 &&
		    cp->geom->attrchanged != NULL)
			cp->geom->attrchanged(cp, "GEOM::media");
	}
	if (g_notaste)
		return;
	LIST_FOREACH(mp, &g_classes, class) {
		if (mp->taste == NULL)
			continue;
		LIST_FOREACH(cp, &pp->consumers, consumers)
			if (cp->geom->class == mp &&
			    (cp->flags & G_CF_ORPHAN) == 0)
				break;
		if (cp != NULL)
			continue;
		mp->taste(mp, pp, 0);
		g_topology_assert();
	}
}


struct g_provider *
g_new_providerf(struct g_geom *gp, const char *fmt, ...)
{
	struct g_provider *pp;
	struct sbuf *sb;
	va_list ap;

	g_topology_assert();
	G_VALID_GEOM(gp);
	KASSERT(gp->access != NULL,
	    ("new provider on geom(%s) without ->access (class %s)",
	    gp->name, gp->class->name));
	KASSERT(gp->start != NULL,
	    ("new provider on geom(%s) without ->start (class %s)",
	    gp->name, gp->class->name));
	KASSERT(!(gp->flags & G_GEOM_WITHER),
	    ("new provider on WITHERing geom(%s) (class %s)",
	    gp->name, gp->class->name));
	sb = sbuf_new_auto();
	va_start(ap, fmt);
	sbuf_vprintf(sb, fmt, ap);
	va_end(ap);
	sbuf_finish(sb);
	pp = g_malloc(sizeof *pp + sbuf_len(sb) + 1, M_WAITOK | M_ZERO);
	pp->name = (char *)(pp + 1);
	strcpy(pp->name, sbuf_data(sb));
	sbuf_delete(sb);
	LIST_INIT(&pp->consumers);
	pp->error = ENXIO;
	pp->geom = gp;
	pp->stat = devstat_new_entry(pp, -1, 0, DEVSTAT_ALL_SUPPORTED,
	    DEVSTAT_TYPE_DIRECT, DEVSTAT_PRIORITY_MAX);
	LIST_INSERT_HEAD(&gp->provider, pp, provider);
	g_post_event(g_new_provider_event, pp, M_WAITOK, pp, gp, NULL);
	return (pp);
}

void
g_error_provider(struct g_provider *pp, int error)
{

	/* G_VALID_PROVIDER(pp);  We may not have g_topology */
	pp->error = error;
}

static void
g_resize_provider_event(void *arg, int flag)
{
	struct g_hh00 *hh;
	struct g_class *mp;
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_consumer *cp, *cp2;
	off_t size;

	g_topology_assert();
	if (g_shutdown)
		return;

	hh = arg;
	pp = hh->pp;
	size = hh->size;
	g_free(hh);

	G_VALID_PROVIDER(pp);
	KASSERT(!(pp->flags & G_PF_WITHER),
	    ("g_resize_provider_event but withered"));
	g_trace(G_T_TOPOLOGY, "g_resize_provider_event(%p)", pp);

	LIST_FOREACH_SAFE(cp, &pp->consumers, consumers, cp2) {
		gp = cp->geom;
		if (gp->resize == NULL && size < pp->mediasize) {
			/*
			 * XXX: g_dev_orphan method does deferred destroying
			 * and it is possible, that other event could already
			 * call the orphan method. Check consumer's flags to
			 * do not schedule it twice.
			 */
			if (cp->flags & G_CF_ORPHAN)
				continue;
			cp->flags |= G_CF_ORPHAN;
			cp->geom->orphan(cp);
		}
	}

	pp->mediasize = size;
	
	LIST_FOREACH_SAFE(cp, &pp->consumers, consumers, cp2) {
		gp = cp->geom;
		if ((gp->flags & G_GEOM_WITHER) == 0 && gp->resize != NULL)
			gp->resize(cp);
	}

	/*
	 * After resizing, the previously invalid GEOM class metadata
	 * might become valid.  This means we should retaste.
	 */
	LIST_FOREACH(mp, &g_classes, class) {
		if (mp->taste == NULL)
			continue;
		LIST_FOREACH(cp, &pp->consumers, consumers)
			if (cp->geom->class == mp &&
			    (cp->flags & G_CF_ORPHAN) == 0)
				break;
		if (cp != NULL)
			continue;
		mp->taste(mp, pp, 0);
		g_topology_assert();
	}
}

void
g_resize_provider(struct g_provider *pp, off_t size)
{
	struct g_hh00 *hh;

	G_VALID_PROVIDER(pp);
	if (pp->flags & G_PF_WITHER)
		return;

	if (size == pp->mediasize)
		return;

	hh = g_malloc(sizeof *hh, M_WAITOK | M_ZERO);
	hh->pp = pp;
	hh->size = size;
	g_post_event(g_resize_provider_event, hh, M_WAITOK, NULL);
}

#ifndef	_PATH_DEV
#define	_PATH_DEV	"/dev/"
#endif

struct g_provider *
g_provider_by_name(char const *arg)
{
	struct g_class *cp;
	struct g_geom *gp;
	struct g_provider *pp, *wpp;

	if (strncmp(arg, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
		arg += sizeof(_PATH_DEV) - 1;

	wpp = NULL;
	LIST_FOREACH(cp, &g_classes, class) {
		LIST_FOREACH(gp, &cp->geom, geom) {
			LIST_FOREACH(pp, &gp->provider, provider) {
				if (strcmp(arg, pp->name) != 0)
					continue;
				if ((gp->flags & G_GEOM_WITHER) == 0 &&
				    (pp->flags & G_PF_WITHER) == 0)
					return (pp);
				else
					wpp = pp;
			}
		}
	}

	return (wpp);
}

void
g_destroy_provider(struct g_provider *pp)
{
	struct g_geom *gp;

	g_topology_assert();
	G_VALID_PROVIDER(pp);
	KASSERT(LIST_EMPTY(&pp->consumers),
	    ("g_destroy_provider but attached"));
	KASSERT (pp->acr == 0, ("g_destroy_provider with acr"));
	KASSERT (pp->acw == 0, ("g_destroy_provider with acw"));
	KASSERT (pp->ace == 0, ("g_destroy_provider with ace"));
	g_cancel_event(pp);
	LIST_REMOVE(pp, provider);
	gp = pp->geom;
	devstat_remove_entry(pp->stat);
	/*
	 * If a callback was provided, send notification that the provider
	 * is now gone.
	 */
	if (gp->providergone != NULL)
		gp->providergone(pp);

	g_free(pp);
	if ((gp->flags & G_GEOM_WITHER))
		g_do_wither();
}

/*
 * We keep the "geoms" list sorted by topological order (== increasing
 * numerical rank) at all times.
 * When an attach is done, the attaching geoms rank is invalidated
 * and it is moved to the tail of the list.
 * All geoms later in the sequence has their ranks reevaluated in
 * sequence.  If we cannot assign rank to a geom because it's
 * prerequisites do not have rank, we move that element to the tail
 * of the sequence with invalid rank as well.
 * At some point we encounter our original geom and if we stil fail
 * to assign it a rank, there must be a loop and we fail back to
 * g_attach() which detach again and calls redo_rank again
 * to fix up the damage.
 * It would be much simpler code wise to do it recursively, but we
 * can't risk that on the kernel stack.
 */

static int
redo_rank(struct g_geom *gp)
{
	struct g_consumer *cp;
	struct g_geom *gp1, *gp2;
	int n, m;

	g_topology_assert();
	G_VALID_GEOM(gp);

	/* Invalidate this geoms rank and move it to the tail */
	gp1 = TAILQ_NEXT(gp, geoms);
	if (gp1 != NULL) {
		gp->rank = 0;
		TAILQ_REMOVE(&geoms, gp, geoms);
		TAILQ_INSERT_TAIL(&geoms, gp, geoms);
	} else {
		gp1 = gp;
	}

	/* re-rank the rest of the sequence */
	for (; gp1 != NULL; gp1 = gp2) {
		gp1->rank = 0;
		m = 1;
		LIST_FOREACH(cp, &gp1->consumer, consumer) {
			if (cp->provider == NULL)
				continue;
			n = cp->provider->geom->rank;
			if (n == 0) {
				m = 0;
				break;
			} else if (n >= m)
				m = n + 1;
		}
		gp1->rank = m;
		gp2 = TAILQ_NEXT(gp1, geoms);

		/* got a rank, moving on */
		if (m != 0)
			continue;

		/* no rank to original geom means loop */
		if (gp == gp1) 
			return (ELOOP);

		/* no rank, put it at the end move on */
		TAILQ_REMOVE(&geoms, gp1, geoms);
		TAILQ_INSERT_TAIL(&geoms, gp1, geoms);
	}
	return (0);
}

int
g_attach(struct g_consumer *cp, struct g_provider *pp)
{
	int error;

	g_topology_assert();
	G_VALID_CONSUMER(cp);
	G_VALID_PROVIDER(pp);
	g_trace(G_T_TOPOLOGY, "g_attach(%p, %p)", cp, pp);
	KASSERT(cp->provider == NULL, ("attach but attached"));
	cp->provider = pp;
	cp->flags &= ~G_CF_ORPHAN;
	LIST_INSERT_HEAD(&pp->consumers, cp, consumers);
	error = redo_rank(cp->geom);
	if (error) {
		LIST_REMOVE(cp, consumers);
		cp->provider = NULL;
		redo_rank(cp->geom);
	}
	return (error);
}

void
g_detach(struct g_consumer *cp)
{
	struct g_provider *pp;

	g_topology_assert();
	G_VALID_CONSUMER(cp);
	g_trace(G_T_TOPOLOGY, "g_detach(%p)", cp);
	KASSERT(cp->provider != NULL, ("detach but not attached"));
	KASSERT(cp->acr == 0, ("detach but nonzero acr"));
	KASSERT(cp->acw == 0, ("detach but nonzero acw"));
	KASSERT(cp->ace == 0, ("detach but nonzero ace"));
	KASSERT(cp->nstart == cp->nend,
	    ("detach with active requests"));
	pp = cp->provider;
	LIST_REMOVE(cp, consumers);
	cp->provider = NULL;
	if ((cp->geom->flags & G_GEOM_WITHER) ||
	    (pp->geom->flags & G_GEOM_WITHER) ||
	    (pp->flags & G_PF_WITHER))
		g_do_wither();
	redo_rank(cp->geom);
}

/*
 * g_access()
 *
 * Access-check with delta values.  The question asked is "can provider
 * "cp" change the access counters by the relative amounts dc[rwe] ?"
 */

int
g_access(struct g_consumer *cp, int dcr, int dcw, int dce)
{
	struct g_provider *pp;
	struct g_geom *gp;
	int pw, pe;
#ifdef INVARIANTS
	int sr, sw, se;
#endif
	int error;

	g_topology_assert();
	G_VALID_CONSUMER(cp);
	pp = cp->provider;
	KASSERT(pp != NULL, ("access but not attached"));
	G_VALID_PROVIDER(pp);
	gp = pp->geom;

	g_trace(G_T_ACCESS, "g_access(%p(%s), %d, %d, %d)",
	    cp, pp->name, dcr, dcw, dce);

	KASSERT(cp->acr + dcr >= 0, ("access resulting in negative acr"));
	KASSERT(cp->acw + dcw >= 0, ("access resulting in negative acw"));
	KASSERT(cp->ace + dce >= 0, ("access resulting in negative ace"));
	KASSERT(dcr != 0 || dcw != 0 || dce != 0, ("NOP access request"));
	KASSERT(gp->access != NULL, ("NULL geom->access"));

	/*
	 * If our class cares about being spoiled, and we have been, we
	 * are probably just ahead of the event telling us that.  Fail
	 * now rather than having to unravel this later.
	 */
	if (cp->geom->spoiled != NULL && (cp->flags & G_CF_SPOILED) &&
	    (dcr > 0 || dcw > 0 || dce > 0))
		return (ENXIO);

	/*
	 * A number of GEOM classes either need to perform an I/O on the first
	 * open or to acquire a different subsystem's lock.  To do that they
	 * may have to drop the topology lock.
	 * Other GEOM classes perform special actions when opening a lower rank
	 * geom for the first time.  As a result, more than one thread may
	 * end up performing the special actions.
	 * So, we prevent concurrent "first" opens by marking the consumer with
	 * special flag.
	 *
	 * Note that if the geom's access method never drops the topology lock,
	 * then we will never see G_GEOM_IN_ACCESS here.
	 */
	while ((gp->flags & G_GEOM_IN_ACCESS) != 0) {
		g_trace(G_T_ACCESS,
		    "%s: race on geom %s via provider %s and consumer of %s",
		    __func__, gp->name, pp->name, cp->geom->name);
		gp->flags |= G_GEOM_ACCESS_WAIT;
		g_topology_sleep(gp, 0);
	}

	/*
	 * Figure out what counts the provider would have had, if this
	 * consumer had (r0w0e0) at this time.
	 */
	pw = pp->acw - cp->acw;
	pe = pp->ace - cp->ace;

	g_trace(G_T_ACCESS,
    "open delta:[r%dw%de%d] old:[r%dw%de%d] provider:[r%dw%de%d] %p(%s)",
	    dcr, dcw, dce,
	    cp->acr, cp->acw, cp->ace,
	    pp->acr, pp->acw, pp->ace,
	    pp, pp->name);

	/* If foot-shooting is enabled, any open on rank#1 is OK */
	if ((g_debugflags & 16) && gp->rank == 1)
		;
	/* If we try exclusive but already write: fail */
	else if (dce > 0 && pw > 0)
		return (EPERM);
	/* If we try write but already exclusive: fail */
	else if (dcw > 0 && pe > 0)
		return (EPERM);
	/* If we try to open more but provider is error'ed: fail */
	else if ((dcr > 0 || dcw > 0 || dce > 0) && pp->error != 0) {
		printf("%s(%d): provider %s has error %d set\n",
		    __func__, __LINE__, pp->name, pp->error);
		return (pp->error);
	}

	/* Ok then... */

#ifdef INVARIANTS
	sr = cp->acr;
	sw = cp->acw;
	se = cp->ace;
#endif
	gp->flags |= G_GEOM_IN_ACCESS;
	error = gp->access(pp, dcr, dcw, dce);
	KASSERT(dcr > 0 || dcw > 0 || dce > 0 || error == 0,
	    ("Geom provider %s::%s dcr=%d dcw=%d dce=%d error=%d failed "
	    "closing ->access()", gp->class->name, pp->name, dcr, dcw,
	    dce, error));

	g_topology_assert();
	gp->flags &= ~G_GEOM_IN_ACCESS;
	KASSERT(cp->acr == sr && cp->acw == sw && cp->ace == se,
	    ("Access counts changed during geom->access"));
	if ((gp->flags & G_GEOM_ACCESS_WAIT) != 0) {
		gp->flags &= ~G_GEOM_ACCESS_WAIT;
		wakeup(gp);
	}

	if (!error) {
		/*
		 * If we open first write, spoil any partner consumers.
		 * If we close last write and provider is not errored,
		 * trigger re-taste.
		 */
		if (pp->acw == 0 && dcw != 0)
			g_spoil(pp, cp);
		else if (pp->acw != 0 && pp->acw == -dcw && pp->error == 0 &&
		    !(gp->flags & G_GEOM_WITHER))
			g_post_event(g_new_provider_event, pp, M_WAITOK, 
			    pp, NULL);

		pp->acr += dcr;
		pp->acw += dcw;
		pp->ace += dce;
		cp->acr += dcr;
		cp->acw += dcw;
		cp->ace += dce;
		if (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)
			KASSERT(pp->sectorsize > 0,
			    ("Provider %s lacks sectorsize", pp->name));
		if ((cp->geom->flags & G_GEOM_WITHER) &&
		    cp->acr == 0 && cp->acw == 0 && cp->ace == 0)
			g_do_wither();
	}
	return (error);
}

int
g_handleattr_int(struct bio *bp, const char *attribute, int val)
{

	return (g_handleattr(bp, attribute, &val, sizeof val));
}

int
g_handleattr_uint16_t(struct bio *bp, const char *attribute, uint16_t val)
{

	return (g_handleattr(bp, attribute, &val, sizeof val));
}

int
g_handleattr_off_t(struct bio *bp, const char *attribute, off_t val)
{

	return (g_handleattr(bp, attribute, &val, sizeof val));
}

int
g_handleattr_str(struct bio *bp, const char *attribute, const char *str)
{

	return (g_handleattr(bp, attribute, str, 0));
}

int
g_handleattr(struct bio *bp, const char *attribute, const void *val, int len)
{
	int error = 0;

	if (strcmp(bp->bio_attribute, attribute))
		return (0);
	if (len == 0) {
		bzero(bp->bio_data, bp->bio_length);
		if (strlcpy(bp->bio_data, val, bp->bio_length) >=
		    bp->bio_length) {
			printf("%s: %s %s bio_length %jd strlen %zu -> EFAULT\n",
			    __func__, bp->bio_to->name, attribute,
			    (intmax_t)bp->bio_length, strlen(val));
			error = EFAULT;
		}
	} else if (bp->bio_length == len) {
		bcopy(val, bp->bio_data, len);
	} else {
		printf("%s: %s %s bio_length %jd len %d -> EFAULT\n", __func__,
		    bp->bio_to->name, attribute, (intmax_t)bp->bio_length, len);
		error = EFAULT;
	}
	if (error == 0)
		bp->bio_completed = bp->bio_length;
	g_io_deliver(bp, error);
	return (1);
}

int
g_std_access(struct g_provider *pp,
	int dr __unused, int dw __unused, int de __unused)
{

	g_topology_assert();
	G_VALID_PROVIDER(pp);
        return (0);
}

void
g_std_done(struct bio *bp)
{
	struct bio *bp2;

	bp2 = bp->bio_parent;
	if (bp2->bio_error == 0)
		bp2->bio_error = bp->bio_error;
	bp2->bio_completed += bp->bio_completed;
	g_destroy_bio(bp);
	bp2->bio_inbed++;
	if (bp2->bio_children == bp2->bio_inbed)
		g_io_deliver(bp2, bp2->bio_error);
}

/* XXX: maybe this is only g_slice_spoiled */

void
g_std_spoiled(struct g_consumer *cp)
{
	struct g_geom *gp;
	struct g_provider *pp;

	g_topology_assert();
	G_VALID_CONSUMER(cp);
	g_trace(G_T_TOPOLOGY, "g_std_spoiled(%p)", cp);
	cp->flags |= G_CF_ORPHAN;
	g_detach(cp);
	gp = cp->geom;
	LIST_FOREACH(pp, &gp->provider, provider)
		g_orphan_provider(pp, ENXIO);
	g_destroy_consumer(cp);
	if (LIST_EMPTY(&gp->provider) && LIST_EMPTY(&gp->consumer))
		g_destroy_geom(gp);
	else
		gp->flags |= G_GEOM_WITHER;
}

/*
 * Spoiling happens when a provider is opened for writing, but consumers
 * which are configured by in-band data are attached (slicers for instance).
 * Since the write might potentially change the in-band data, such consumers
 * need to re-evaluate their existence after the writing session closes.
 * We do this by (offering to) tear them down when the open for write happens
 * in return for a re-taste when it closes again.
 * Together with the fact that such consumers grab an 'e' bit whenever they
 * are open, regardless of mode, this ends up DTRT.
 */

static void
g_spoil_event(void *arg, int flag)
{
	struct g_provider *pp;
	struct g_consumer *cp, *cp2;

	g_topology_assert();
	if (flag == EV_CANCEL)
		return;
	pp = arg;
	G_VALID_PROVIDER(pp);
	g_trace(G_T_TOPOLOGY, "%s %p(%s:%s:%s)", __func__, pp,
	    pp->geom->class->name, pp->geom->name, pp->name);
	for (cp = LIST_FIRST(&pp->consumers); cp != NULL; cp = cp2) {
		cp2 = LIST_NEXT(cp, consumers);
		if ((cp->flags & G_CF_SPOILED) == 0)
			continue;
		cp->flags &= ~G_CF_SPOILED;
		if (cp->geom->spoiled == NULL)
			continue;
		cp->geom->spoiled(cp);
		g_topology_assert();
	}
}

void
g_spoil(struct g_provider *pp, struct g_consumer *cp)
{
	struct g_consumer *cp2;

	g_topology_assert();
	G_VALID_PROVIDER(pp);
	G_VALID_CONSUMER(cp);

	LIST_FOREACH(cp2, &pp->consumers, consumers) {
		if (cp2 == cp)
			continue;
/*
		KASSERT(cp2->acr == 0, ("spoiling cp->acr = %d", cp2->acr));
		KASSERT(cp2->acw == 0, ("spoiling cp->acw = %d", cp2->acw));
*/
		KASSERT(cp2->ace == 0, ("spoiling cp->ace = %d", cp2->ace));
		cp2->flags |= G_CF_SPOILED;
	}
	g_post_event(g_spoil_event, pp, M_WAITOK, pp, NULL);
}

static void
g_media_changed_event(void *arg, int flag)
{
	struct g_provider *pp;
	int retaste;

	g_topology_assert();
	if (flag == EV_CANCEL)
		return;
	pp = arg;
	G_VALID_PROVIDER(pp);

	/*
	 * If provider was not open for writing, queue retaste after spoiling.
	 * If it was, retaste will happen automatically on close.
	 */
	retaste = (pp->acw == 0 && pp->error == 0 &&
	    !(pp->geom->flags & G_GEOM_WITHER));
	g_spoil_event(arg, flag);
	if (retaste)
		g_post_event(g_new_provider_event, pp, M_WAITOK, pp, NULL);
}

int
g_media_changed(struct g_provider *pp, int flag)
{
	struct g_consumer *cp;

	LIST_FOREACH(cp, &pp->consumers, consumers)
		cp->flags |= G_CF_SPOILED;
	return (g_post_event(g_media_changed_event, pp, flag, pp, NULL));
}

int
g_media_gone(struct g_provider *pp, int flag)
{
	struct g_consumer *cp;

	LIST_FOREACH(cp, &pp->consumers, consumers)
		cp->flags |= G_CF_SPOILED;
	return (g_post_event(g_spoil_event, pp, flag, pp, NULL));
}

int
g_getattr__(const char *attr, struct g_consumer *cp, void *var, int len)
{
	int error, i;

	i = len;
	error = g_io_getattr(attr, cp, &i, var);
	if (error)
		return (error);
	if (i != len)
		return (EINVAL);
	return (0);
}

static int
g_get_device_prefix_len(const char *name)
{
	int len;

	if (strncmp(name, "ada", 3) == 0)
		len = 3;
	else if (strncmp(name, "ad", 2) == 0)
		len = 2;
	else
		return (0);
	if (name[len] < '0' || name[len] > '9')
		return (0);
	do {
		len++;
	} while (name[len] >= '0' && name[len] <= '9');
	return (len);
}

int
g_compare_names(const char *namea, const char *nameb)
{
	int deva, devb;

	if (strcmp(namea, nameb) == 0)
		return (1);
	deva = g_get_device_prefix_len(namea);
	if (deva == 0)
		return (0);
	devb = g_get_device_prefix_len(nameb);
	if (devb == 0)
		return (0);
	if (strcmp(namea + deva, nameb + devb) == 0)
		return (1);
	return (0);
}

void
g_geom_add_alias(struct g_geom *gp, const char *alias)
{
	struct g_geom_alias *gap;

	gap = (struct g_geom_alias *)g_malloc(
		sizeof(struct g_geom_alias) + strlen(alias) + 1, M_WAITOK);
	strcpy((char *)(gap + 1), alias);
	gap->ga_alias = (const char *)(gap + 1);
	LIST_INSERT_HEAD(&gp->aliases, gap, ga_next);
}

#if defined(DIAGNOSTIC) || defined(DDB)
/*
 * This function walks the mesh and returns a non-zero integer if it
 * finds the argument pointer is an object. The return value indicates
 * which type of object it is believed to be. If topology is not locked,
 * this function is potentially dangerous, but we don't assert that the
 * topology lock is held when called from debugger.
 */
int
g_valid_obj(void const *ptr)
{
	struct g_class *mp;
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_provider *pp;

#ifdef KDB
	if (kdb_active == 0)
#endif
		g_topology_assert();

	LIST_FOREACH(mp, &g_classes, class) {
		if (ptr == mp)
			return (1);
		LIST_FOREACH(gp, &mp->geom, geom) {
			if (ptr == gp)
				return (2);
			LIST_FOREACH(cp, &gp->consumer, consumer)
				if (ptr == cp)
					return (3);
			LIST_FOREACH(pp, &gp->provider, provider)
				if (ptr == pp)
					return (4);
		}
	}
	return(0);
}
#endif

#ifdef DDB

#define	gprintf(...)	do {						\
	db_printf("%*s", indent, "");					\
	db_printf(__VA_ARGS__);						\
} while (0)
#define	gprintln(...)	do {						\
	gprintf(__VA_ARGS__);						\
	db_printf("\n");						\
} while (0)

#define	ADDFLAG(obj, flag, sflag)	do {				\
	if ((obj)->flags & (flag)) {					\
		if (comma)						\
			strlcat(str, ",", size);			\
		strlcat(str, (sflag), size);				\
		comma = 1;						\
	}								\
} while (0)

static char *
provider_flags_to_string(struct g_provider *pp, char *str, size_t size)
{
	int comma = 0;

	bzero(str, size);
	if (pp->flags == 0) {
		strlcpy(str, "NONE", size);
		return (str);
	}
	ADDFLAG(pp, G_PF_WITHER, "G_PF_WITHER");
	ADDFLAG(pp, G_PF_ORPHAN, "G_PF_ORPHAN");
	return (str);
}

static char *
geom_flags_to_string(struct g_geom *gp, char *str, size_t size)
{
	int comma = 0;

	bzero(str, size);
	if (gp->flags == 0) {
		strlcpy(str, "NONE", size);
		return (str);
	}
	ADDFLAG(gp, G_GEOM_WITHER, "G_GEOM_WITHER");
	return (str);
}
static void
db_show_geom_consumer(int indent, struct g_consumer *cp)
{

	if (indent == 0) {
		gprintln("consumer: %p", cp);
		gprintln("  class:    %s (%p)", cp->geom->class->name,
		    cp->geom->class);
		gprintln("  geom:     %s (%p)", cp->geom->name, cp->geom);
		if (cp->provider == NULL)
			gprintln("  provider: none");
		else {
			gprintln("  provider: %s (%p)", cp->provider->name,
			    cp->provider);
		}
		gprintln("  access:   r%dw%de%d", cp->acr, cp->acw, cp->ace);
		gprintln("  flags:    0x%04x", cp->flags);
		gprintln("  nstart:   %u", cp->nstart);
		gprintln("  nend:     %u", cp->nend);
	} else {
		gprintf("consumer: %p (%s), access=r%dw%de%d", cp,
		    cp->provider != NULL ? cp->provider->name : "none",
		    cp->acr, cp->acw, cp->ace);
		if (cp->flags)
			db_printf(", flags=0x%04x", cp->flags);
		db_printf("\n");
	}
}

static void
db_show_geom_provider(int indent, struct g_provider *pp)
{
	struct g_consumer *cp;
	char flags[64];

	if (indent == 0) {
		gprintln("provider: %s (%p)", pp->name, pp);
		gprintln("  class:        %s (%p)", pp->geom->class->name,
		    pp->geom->class);
		gprintln("  geom:         %s (%p)", pp->geom->name, pp->geom);
		gprintln("  mediasize:    %jd", (intmax_t)pp->mediasize);
		gprintln("  sectorsize:   %u", pp->sectorsize);
		gprintln("  stripesize:   %ju", (uintmax_t)pp->stripesize);
		gprintln("  stripeoffset: %ju", (uintmax_t)pp->stripeoffset);
		gprintln("  access:       r%dw%de%d", pp->acr, pp->acw,
		    pp->ace);
		gprintln("  flags:        %s (0x%04x)",
		    provider_flags_to_string(pp, flags, sizeof(flags)),
		    pp->flags);
		gprintln("  error:        %d", pp->error);
		gprintln("  nstart:       %u", pp->nstart);
		gprintln("  nend:         %u", pp->nend);
		if (LIST_EMPTY(&pp->consumers))
			gprintln("  consumers:    none");
	} else {
		gprintf("provider: %s (%p), access=r%dw%de%d",
		    pp->name, pp, pp->acr, pp->acw, pp->ace);
		if (pp->flags != 0) {
			db_printf(", flags=%s (0x%04x)",
			    provider_flags_to_string(pp, flags, sizeof(flags)),
			    pp->flags);
		}
		db_printf("\n");
	}
	if (!LIST_EMPTY(&pp->consumers)) {
		LIST_FOREACH(cp, &pp->consumers, consumers) {
			db_show_geom_consumer(indent + 2, cp);
			if (db_pager_quit)
				break;
		}
	}
}

static void
db_show_geom_geom(int indent, struct g_geom *gp)
{
	struct g_provider *pp;
	struct g_consumer *cp;
	char flags[64];

	if (indent == 0) {
		gprintln("geom: %s (%p)", gp->name, gp);
		gprintln("  class:     %s (%p)", gp->class->name, gp->class);
		gprintln("  flags:     %s (0x%04x)",
		    geom_flags_to_string(gp, flags, sizeof(flags)), gp->flags);
		gprintln("  rank:      %d", gp->rank);
		if (LIST_EMPTY(&gp->provider))
			gprintln("  providers: none");
		if (LIST_EMPTY(&gp->consumer))
			gprintln("  consumers: none");
	} else {
		gprintf("geom: %s (%p), rank=%d", gp->name, gp, gp->rank);
		if (gp->flags != 0) {
			db_printf(", flags=%s (0x%04x)",
			    geom_flags_to_string(gp, flags, sizeof(flags)),
			    gp->flags);
		}
		db_printf("\n");
	}
	if (!LIST_EMPTY(&gp->provider)) {
		LIST_FOREACH(pp, &gp->provider, provider) {
			db_show_geom_provider(indent + 2, pp);
			if (db_pager_quit)
				break;
		}
	}
	if (!LIST_EMPTY(&gp->consumer)) {
		LIST_FOREACH(cp, &gp->consumer, consumer) {
			db_show_geom_consumer(indent + 2, cp);
			if (db_pager_quit)
				break;
		}
	}
}

static void
db_show_geom_class(struct g_class *mp)
{
	struct g_geom *gp;

	db_printf("class: %s (%p)\n", mp->name, mp);
	LIST_FOREACH(gp, &mp->geom, geom) {
		db_show_geom_geom(2, gp);
		if (db_pager_quit)
			break;
	}
}

/*
 * Print the GEOM topology or the given object.
 */
DB_SHOW_COMMAND(geom, db_show_geom)
{
	struct g_class *mp;

	if (!have_addr) {
		/* No address given, print the entire topology. */
		LIST_FOREACH(mp, &g_classes, class) {
			db_show_geom_class(mp);
			db_printf("\n");
			if (db_pager_quit)
				break;
		}
	} else {
		switch (g_valid_obj((void *)addr)) {
		case 1:
			db_show_geom_class((struct g_class *)addr);
			break;
		case 2:
			db_show_geom_geom(0, (struct g_geom *)addr);
			break;
		case 3:
			db_show_geom_consumer(0, (struct g_consumer *)addr);
			break;
		case 4:
			db_show_geom_provider(0, (struct g_provider *)addr);
			break;
		default:
			db_printf("Not a GEOM object.\n");
			break;
		}
	}
}

static void
db_print_bio_cmd(struct bio *bp)
{
	db_printf("  cmd: ");
	switch (bp->bio_cmd) {
	case BIO_READ: db_printf("BIO_READ"); break;
	case BIO_WRITE: db_printf("BIO_WRITE"); break;
	case BIO_DELETE: db_printf("BIO_DELETE"); break;
	case BIO_GETATTR: db_printf("BIO_GETATTR"); break;
	case BIO_FLUSH: db_printf("BIO_FLUSH"); break;
	case BIO_CMD0: db_printf("BIO_CMD0"); break;
	case BIO_CMD1: db_printf("BIO_CMD1"); break;
	case BIO_CMD2: db_printf("BIO_CMD2"); break;
	case BIO_ZONE: db_printf("BIO_ZONE"); break;
	default: db_printf("UNKNOWN"); break;
	}
	db_printf("\n");
}

static void
db_print_bio_flags(struct bio *bp)
{
	int comma;

	comma = 0;
	db_printf("  flags: ");
	if (bp->bio_flags & BIO_ERROR) {
		db_printf("BIO_ERROR");
		comma = 1;
	}
	if (bp->bio_flags & BIO_DONE) {
		db_printf("%sBIO_DONE", (comma ? ", " : ""));
		comma = 1;
	}
	if (bp->bio_flags & BIO_ONQUEUE)
		db_printf("%sBIO_ONQUEUE", (comma ? ", " : ""));
	db_printf("\n");
}

/*
 * Print useful information in a BIO
 */
DB_SHOW_COMMAND(bio, db_show_bio)
{
	struct bio *bp;

	if (have_addr) {
		bp = (struct bio *)addr;
		db_printf("BIO %p\n", bp);
		db_print_bio_cmd(bp);
		db_print_bio_flags(bp);
		db_printf("  cflags: 0x%hx\n", bp->bio_cflags);
		db_printf("  pflags: 0x%hx\n", bp->bio_pflags);
		db_printf("  offset: %jd\n", (intmax_t)bp->bio_offset);
		db_printf("  length: %jd\n", (intmax_t)bp->bio_length);
		db_printf("  bcount: %ld\n", bp->bio_bcount);
		db_printf("  resid: %ld\n", bp->bio_resid);
		db_printf("  completed: %jd\n", (intmax_t)bp->bio_completed);
		db_printf("  children: %u\n", bp->bio_children);
		db_printf("  inbed: %u\n", bp->bio_inbed);
		db_printf("  error: %d\n", bp->bio_error);
		db_printf("  parent: %p\n", bp->bio_parent);
		db_printf("  driver1: %p\n", bp->bio_driver1);
		db_printf("  driver2: %p\n", bp->bio_driver2);
		db_printf("  caller1: %p\n", bp->bio_caller1);
		db_printf("  caller2: %p\n", bp->bio_caller2);
		db_printf("  bio_from: %p\n", bp->bio_from);
		db_printf("  bio_to: %p\n", bp->bio_to);

#if defined(BUF_TRACKING) || defined(FULL_BUF_TRACKING)
		db_printf("  bio_track_bp: %p\n", bp->bio_track_bp);
#endif
	}
}

#undef	gprintf
#undef	gprintln
#undef	ADDFLAG

#endif	/* DDB */
