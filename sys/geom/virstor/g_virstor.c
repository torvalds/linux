/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2007 Ivan Voras <ivoras@freebsd.org>
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

/* Implementation notes:
 * - "Components" are wrappers around providers that make up the
 *   virtual storage (i.e. a virstor has "physical" components)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sx.h>
#include <sys/bio.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/mutex.h>
#include <vm/uma.h>
#include <geom/geom.h>

#include <geom/virstor/g_virstor.h>
#include <geom/virstor/g_virstor_md.h>

FEATURE(g_virstor, "GEOM virtual storage support");

/* Declare malloc(9) label */
static MALLOC_DEFINE(M_GVIRSTOR, "gvirstor", "GEOM_VIRSTOR Data");

/* GEOM class methods */
static g_init_t g_virstor_init;
static g_fini_t g_virstor_fini;
static g_taste_t g_virstor_taste;
static g_ctl_req_t g_virstor_config;
static g_ctl_destroy_geom_t g_virstor_destroy_geom;

/* Declare & initialize class structure ("geom class") */
struct g_class g_virstor_class = {
	.name =		G_VIRSTOR_CLASS_NAME,
	.version =	G_VERSION,
	.init =		g_virstor_init,
	.fini =		g_virstor_fini,
	.taste =	g_virstor_taste,
	.ctlreq =	g_virstor_config,
	.destroy_geom = g_virstor_destroy_geom
	/* The .dumpconf and the rest are only usable for a geom instance, so
	 * they will be set when such instance is created. */
};

/* Declare sysctl's and loader tunables */
SYSCTL_DECL(_kern_geom);
static SYSCTL_NODE(_kern_geom, OID_AUTO, virstor, CTLFLAG_RW, 0,
    "GEOM_GVIRSTOR information");

static u_int g_virstor_debug = 2; /* XXX: lower to 2 when released to public */
SYSCTL_UINT(_kern_geom_virstor, OID_AUTO, debug, CTLFLAG_RWTUN, &g_virstor_debug,
    0, "Debug level (2=production, 5=normal, 15=excessive)");

static u_int g_virstor_chunk_watermark = 100;
SYSCTL_UINT(_kern_geom_virstor, OID_AUTO, chunk_watermark, CTLFLAG_RWTUN,
    &g_virstor_chunk_watermark, 0,
    "Minimum number of free chunks before issuing administrative warning");

static u_int g_virstor_component_watermark = 1;
SYSCTL_UINT(_kern_geom_virstor, OID_AUTO, component_watermark, CTLFLAG_RWTUN,
    &g_virstor_component_watermark, 0,
    "Minimum number of free components before issuing administrative warning");

static int read_metadata(struct g_consumer *, struct g_virstor_metadata *);
static void write_metadata(struct g_consumer *, struct g_virstor_metadata *);
static int clear_metadata(struct g_virstor_component *);
static int add_provider_to_geom(struct g_virstor_softc *, struct g_provider *,
    struct g_virstor_metadata *);
static struct g_geom *create_virstor_geom(struct g_class *,
    struct g_virstor_metadata *);
static void virstor_check_and_run(struct g_virstor_softc *);
static u_int virstor_valid_components(struct g_virstor_softc *);
static int virstor_geom_destroy(struct g_virstor_softc *, boolean_t,
    boolean_t);
static void remove_component(struct g_virstor_softc *,
    struct g_virstor_component *, boolean_t);
static void bioq_dismantle(struct bio_queue_head *);
static int allocate_chunk(struct g_virstor_softc *,
    struct g_virstor_component **, u_int *, u_int *);
static void delay_destroy_consumer(void *, int);
static void dump_component(struct g_virstor_component *comp);
#if 0
static void dump_me(struct virstor_map_entry *me, unsigned int nr);
#endif

static void virstor_ctl_stop(struct gctl_req *, struct g_class *);
static void virstor_ctl_add(struct gctl_req *, struct g_class *);
static void virstor_ctl_remove(struct gctl_req *, struct g_class *);
static struct g_virstor_softc * virstor_find_geom(const struct g_class *,
    const char *);
static void update_metadata(struct g_virstor_softc *);
static void fill_metadata(struct g_virstor_softc *, struct g_virstor_metadata *,
    u_int, u_int);

static void g_virstor_orphan(struct g_consumer *);
static int g_virstor_access(struct g_provider *, int, int, int);
static void g_virstor_start(struct bio *);
static void g_virstor_dumpconf(struct sbuf *, const char *, struct g_geom *,
    struct g_consumer *, struct g_provider *);
static void g_virstor_done(struct bio *);

static void invalid_call(void);
/*
 * Initialise GEOM class (per-class callback)
 */
static void
g_virstor_init(struct g_class *mp __unused)
{

	/* Catch map struct size mismatch at compile time; Map entries must
	 * fit into MAXPHYS exactly, with no wasted space. */
	CTASSERT(VIRSTOR_MAP_BLOCK_ENTRIES*VIRSTOR_MAP_ENTRY_SIZE == MAXPHYS);

	/* Init UMA zones, TAILQ's, other global vars */
}

/*
 * Finalise GEOM class (per-class callback)
 */
static void
g_virstor_fini(struct g_class *mp __unused)
{

	/* Deinit UMA zones & global vars */
}

/*
 * Config (per-class callback)
 */
static void
g_virstor_config(struct gctl_req *req, struct g_class *cp, char const *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "Failed to get 'version' argument");
		return;
	}
	if (*version != G_VIRSTOR_VERSION) {
		gctl_error(req, "Userland and kernel versions out of sync");
		return;
	}

	g_topology_unlock();
	if (strcmp(verb, "add") == 0)
		virstor_ctl_add(req, cp);
	else if (strcmp(verb, "stop") == 0 || strcmp(verb, "destroy") == 0)
		virstor_ctl_stop(req, cp);
	else if (strcmp(verb, "remove") == 0)
		virstor_ctl_remove(req, cp);
	else
		gctl_error(req, "unknown verb: '%s'", verb);
	g_topology_lock();
}

/*
 * "stop" verb from userland
 */
static void
virstor_ctl_stop(struct gctl_req *req, struct g_class *cp)
{
	int *force, *nargs;
	int i;

	nargs = gctl_get_paraml(req, "nargs", sizeof *nargs);
	if (nargs == NULL) {
		gctl_error(req, "Error fetching argument '%s'", "nargs");
		return;
	}
	if (*nargs < 1) {
		gctl_error(req, "Invalid number of arguments");
		return;
	}
	force = gctl_get_paraml(req, "force", sizeof *force);
	if (force == NULL) {
		gctl_error(req, "Error fetching argument '%s'", "force");
		return;
	}

	g_topology_lock();
	for (i = 0; i < *nargs; i++) {
		char param[8];
		const char *name;
		struct g_virstor_softc *sc;
		int error;

		sprintf(param, "arg%d", i);
		name = gctl_get_asciiparam(req, param);
		if (name == NULL) {
			gctl_error(req, "No 'arg%d' argument", i);
			g_topology_unlock();
			return;
		}
		sc = virstor_find_geom(cp, name);
		if (sc == NULL) {
			gctl_error(req, "Don't know anything about '%s'", name);
			g_topology_unlock();
			return;
		}

		LOG_MSG(LVL_INFO, "Stopping %s by the userland command",
		    sc->geom->name);
		update_metadata(sc);
		if ((error = virstor_geom_destroy(sc, TRUE, TRUE)) != 0) {
			LOG_MSG(LVL_ERROR, "Cannot destroy %s: %d",
			    sc->geom->name, error);
		}
	}
	g_topology_unlock();
}

/*
 * "add" verb from userland - add new component(s) to the structure.
 * This will be done all at once in here, without going through the
 * .taste function for new components.
 */
static void
virstor_ctl_add(struct gctl_req *req, struct g_class *cp)
{
	/* Note: while this is going on, I/O is being done on
	 * the g_up and g_down threads. The idea is to make changes
	 * to softc members in a way that can atomically activate
	 * them all at once. */
	struct g_virstor_softc *sc;
	int *hardcode, *nargs;
	const char *geom_name;	/* geom to add a component to */
	struct g_consumer *fcp;
	struct g_virstor_bio_q *bq;
	u_int added;
	int error;
	int i;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "Error fetching argument '%s'", "nargs");
		return;
	}
	if (*nargs < 2) {
		gctl_error(req, "Invalid number of arguments");
		return;
	}
	hardcode = gctl_get_paraml(req, "hardcode", sizeof(*hardcode));
	if (hardcode == NULL) {
		gctl_error(req, "Error fetching argument '%s'", "hardcode");
		return;
	}

	/* Find "our" geom */
	geom_name = gctl_get_asciiparam(req, "arg0");
	if (geom_name == NULL) {
		gctl_error(req, "Error fetching argument '%s'", "geom_name (arg0)");
		return;
	}
	sc = virstor_find_geom(cp, geom_name);
	if (sc == NULL) {
		gctl_error(req, "Don't know anything about '%s'", geom_name);
		return;
	}

	if (virstor_valid_components(sc) != sc->n_components) {
		LOG_MSG(LVL_ERROR, "Cannot add components to incomplete "
		    "virstor %s", sc->geom->name);
		gctl_error(req, "Virstor %s is incomplete", sc->geom->name);
		return;
	}

	fcp = sc->components[0].gcons;
	added = 0;
	g_topology_lock();
	for (i = 1; i < *nargs; i++) {
		struct g_virstor_metadata md;
		char aname[8];
		const char *prov_name;
		struct g_provider *pp;
		struct g_consumer *cp;
		u_int nc;
		u_int j;

		snprintf(aname, sizeof aname, "arg%d", i);
		prov_name = gctl_get_asciiparam(req, aname);
		if (prov_name == NULL) {
			gctl_error(req, "Error fetching argument '%s'", aname);
			g_topology_unlock();
			return;
		}
		if (strncmp(prov_name, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
			prov_name += sizeof(_PATH_DEV) - 1;

		pp = g_provider_by_name(prov_name);
		if (pp == NULL) {
			/* This is the most common error so be verbose about it */
			if (added != 0) {
				gctl_error(req, "Invalid provider: '%s' (added"
				    " %u components)", prov_name, added);
				update_metadata(sc);
			} else {
				gctl_error(req, "Invalid provider: '%s'",
				    prov_name);
			}
			g_topology_unlock();
			return;
		}
		cp = g_new_consumer(sc->geom);
		if (cp == NULL) {
			gctl_error(req, "Cannot create consumer");
			g_topology_unlock();
			return;
		}
		error = g_attach(cp, pp);
		if (error != 0) {
			gctl_error(req, "Cannot attach a consumer to %s",
			    pp->name);
			g_destroy_consumer(cp);
			g_topology_unlock();
			return;
		}
		if (fcp->acr != 0 || fcp->acw != 0 || fcp->ace != 0) {
			error = g_access(cp, fcp->acr, fcp->acw, fcp->ace);
			if (error != 0) {
				gctl_error(req, "Access request failed for %s",
				    pp->name);
				g_destroy_consumer(cp);
				g_topology_unlock();
				return;
			}
		}
		if (fcp->provider->sectorsize != pp->sectorsize) {
			gctl_error(req, "Sector size doesn't fit for %s",
			    pp->name);
			g_destroy_consumer(cp);
			g_topology_unlock();
			return;
		}
		for (j = 0; j < sc->n_components; j++) {
			if (strcmp(sc->components[j].gcons->provider->name,
			    pp->name) == 0) {
				gctl_error(req, "Component %s already in %s",
				    pp->name, sc->geom->name);
				g_destroy_consumer(cp);
				g_topology_unlock();
				return;
			}
		}
		sc->components = realloc(sc->components,
		    sizeof(*sc->components) * (sc->n_components + 1),
		    M_GVIRSTOR, M_WAITOK);

		nc = sc->n_components;
		sc->components[nc].gcons = cp;
		sc->components[nc].sc = sc;
		sc->components[nc].index = nc;
		sc->components[nc].chunk_count = cp->provider->mediasize /
		    sc->chunk_size;
		sc->components[nc].chunk_next = 0;
		sc->components[nc].chunk_reserved = 0;

		if (sc->components[nc].chunk_count < 4) {
			gctl_error(req, "Provider too small: %s",
			    cp->provider->name);
			g_destroy_consumer(cp);
			g_topology_unlock();
			return;
		}
		fill_metadata(sc, &md, nc, *hardcode);
		write_metadata(cp, &md);
		/* The new component becomes visible when n_components is
		 * incremented */
		sc->n_components++;
		added++;

	}
	/* This call to update_metadata() is critical. In case there's a
	 * power failure in the middle of it and some components are updated
	 * while others are not, there will be trouble on next .taste() iff
	 * a non-updated component is detected first */
	update_metadata(sc);
	g_topology_unlock();
	LOG_MSG(LVL_INFO, "Added %d component(s) to %s", added,
	    sc->geom->name);
	/* Fire off BIOs previously queued because there wasn't any
	 * physical space left. If the BIOs still can't be satisfied
	 * they will again be added to the end of the queue (during
	 * which the mutex will be recursed) */
	bq = malloc(sizeof(*bq), M_GVIRSTOR, M_WAITOK);
	bq->bio = NULL;
	mtx_lock(&sc->delayed_bio_q_mtx);
	/* First, insert a sentinel to the queue end, so we don't
	 * end up in an infinite loop if there's still no free
	 * space available. */
	STAILQ_INSERT_TAIL(&sc->delayed_bio_q, bq, linkage);
	while (!STAILQ_EMPTY(&sc->delayed_bio_q)) {
		bq = STAILQ_FIRST(&sc->delayed_bio_q);
		if (bq->bio != NULL) {
			g_virstor_start(bq->bio);
			STAILQ_REMOVE_HEAD(&sc->delayed_bio_q, linkage);
			free(bq, M_GVIRSTOR);
		} else {
			STAILQ_REMOVE_HEAD(&sc->delayed_bio_q, linkage);
			free(bq, M_GVIRSTOR);
			break;
		}
	}
	mtx_unlock(&sc->delayed_bio_q_mtx);

}

/*
 * Find a geom handled by the class
 */
static struct g_virstor_softc *
virstor_find_geom(const struct g_class *cp, const char *name)
{
	struct g_geom *gp;

	LIST_FOREACH(gp, &cp->geom, geom) {
		if (strcmp(name, gp->name) == 0)
			return (gp->softc);
	}
	return (NULL);
}

/*
 * Update metadata on all components to reflect the current state
 * of these fields:
 *    - chunk_next
 *    - flags
 *    - md_count
 * Expects things to be set up so write_metadata() can work, i.e.
 * the topology lock must be held.
 */
static void
update_metadata(struct g_virstor_softc *sc)
{
	struct g_virstor_metadata md;
	u_int n;

	if (virstor_valid_components(sc) != sc->n_components)
		return; /* Incomplete device */
	LOG_MSG(LVL_DEBUG, "Updating metadata on components for %s",
	    sc->geom->name);
	/* Update metadata on components */
	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__,
	    sc->geom->class->name, sc->geom->name);
	g_topology_assert();
	for (n = 0; n < sc->n_components; n++) {
		read_metadata(sc->components[n].gcons, &md);
		md.chunk_next = sc->components[n].chunk_next;
		md.flags = sc->components[n].flags;
		md.md_count = sc->n_components;
		write_metadata(sc->components[n].gcons, &md);
	}
}

/*
 * Fills metadata (struct md) from information stored in softc and the nc'th
 * component of virstor
 */
static void
fill_metadata(struct g_virstor_softc *sc, struct g_virstor_metadata *md,
    u_int nc, u_int hardcode)
{
	struct g_virstor_component *c;

	bzero(md, sizeof *md);
	c = &sc->components[nc];

	strncpy(md->md_magic, G_VIRSTOR_MAGIC, sizeof md->md_magic);
	md->md_version = G_VIRSTOR_VERSION;
	strncpy(md->md_name, sc->geom->name, sizeof md->md_name);
	md->md_id = sc->id;
	md->md_virsize = sc->virsize;
	md->md_chunk_size = sc->chunk_size;
	md->md_count = sc->n_components;

	if (hardcode) {
		strncpy(md->provider, c->gcons->provider->name,
		    sizeof md->provider);
	}
	md->no = nc;
	md->provsize = c->gcons->provider->mediasize;
	md->chunk_count = c->chunk_count;
	md->chunk_next = c->chunk_next;
	md->chunk_reserved = c->chunk_reserved;
	md->flags = c->flags;
}

/*
 * Remove a component from virstor device.
 * Can only be done if the component is unallocated.
 */
static void
virstor_ctl_remove(struct gctl_req *req, struct g_class *cp)
{
	/* As this is executed in parallel to I/O, operations on virstor
	 * structures must be as atomic as possible. */
	struct g_virstor_softc *sc;
	int *nargs;
	const char *geom_name;
	u_int removed;
	int i;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "Error fetching argument '%s'", "nargs");
		return;
	}
	if (*nargs < 2) {
		gctl_error(req, "Invalid number of arguments");
		return;
	}
	/* Find "our" geom */
	geom_name = gctl_get_asciiparam(req, "arg0");
	if (geom_name == NULL) {
		gctl_error(req, "Error fetching argument '%s'",
		    "geom_name (arg0)");
		return;
	}
	sc = virstor_find_geom(cp, geom_name);
	if (sc == NULL) {
		gctl_error(req, "Don't know anything about '%s'", geom_name);
		return;
	}

	if (virstor_valid_components(sc) != sc->n_components) {
		LOG_MSG(LVL_ERROR, "Cannot remove components from incomplete "
		    "virstor %s", sc->geom->name);
		gctl_error(req, "Virstor %s is incomplete", sc->geom->name);
		return;
	}

	removed = 0;
	for (i = 1; i < *nargs; i++) {
		char param[8];
		const char *prov_name;
		int j, found;
		struct g_virstor_component *newcomp, *compbak;

		sprintf(param, "arg%d", i);
		prov_name = gctl_get_asciiparam(req, param);
		if (prov_name == NULL) {
			gctl_error(req, "Error fetching argument '%s'", param);
			return;
		}
		if (strncmp(prov_name, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
			prov_name += sizeof(_PATH_DEV) - 1;

		found = -1;
		for (j = 0; j < sc->n_components; j++) {
			if (strcmp(sc->components[j].gcons->provider->name,
			    prov_name) == 0) {
				found = j;
				break;
			}
		}
		if (found == -1) {
			LOG_MSG(LVL_ERROR, "No %s component in %s",
			    prov_name, sc->geom->name);
			continue;
		}

		compbak = sc->components;
		newcomp = malloc(sc->n_components * sizeof(*sc->components),
		    M_GVIRSTOR, M_WAITOK | M_ZERO);
		bcopy(sc->components, newcomp, found * sizeof(*sc->components));
		bcopy(&sc->components[found + 1], newcomp + found,
		    found * sizeof(*sc->components));
		if ((sc->components[j].flags & VIRSTOR_PROVIDER_ALLOCATED) != 0) {
			LOG_MSG(LVL_ERROR, "Allocated provider %s cannot be "
			    "removed from %s",
			    prov_name, sc->geom->name);
			free(newcomp, M_GVIRSTOR);
			/* We'll consider this non-fatal error */
			continue;
		}
		/* Renumerate unallocated components */
		for (j = 0; j < sc->n_components-1; j++) {
			if ((sc->components[j].flags &
			    VIRSTOR_PROVIDER_ALLOCATED) == 0) {
				sc->components[j].index = j;
			}
		}
		/* This is the critical section. If a component allocation
		 * event happens while both variables are not yet set,
		 * there will be trouble. Something will panic on encountering
		 * NULL sc->components[x].gcomp member.
		 * Luckily, component allocation happens very rarely and
		 * removing components is an abnormal action in any case. */
		sc->components = newcomp;
		sc->n_components--;
		/* End critical section */

		g_topology_lock();
		if (clear_metadata(&compbak[found]) != 0) {
			LOG_MSG(LVL_WARNING, "Trouble ahead: cannot clear "
			    "metadata on %s", prov_name);
		}
		g_detach(compbak[found].gcons);
		g_destroy_consumer(compbak[found].gcons);
		g_topology_unlock();

		free(compbak, M_GVIRSTOR);

		removed++;
	}

	/* This call to update_metadata() is critical. In case there's a
	 * power failure in the middle of it and some components are updated
	 * while others are not, there will be trouble on next .taste() iff
	 * a non-updated component is detected first */
	g_topology_lock();
	update_metadata(sc);
	g_topology_unlock();
	LOG_MSG(LVL_INFO, "Removed %d component(s) from %s", removed,
	    sc->geom->name);
}

/*
 * Clear metadata sector on component
 */
static int
clear_metadata(struct g_virstor_component *comp)
{
	char *buf;
	int error;

	LOG_MSG(LVL_INFO, "Clearing metadata on %s",
	    comp->gcons->provider->name);
	g_topology_assert();
	error = g_access(comp->gcons, 0, 1, 0);
	if (error != 0)
		return (error);
	buf = malloc(comp->gcons->provider->sectorsize, M_GVIRSTOR,
	    M_WAITOK | M_ZERO);
	error = g_write_data(comp->gcons,
	    comp->gcons->provider->mediasize -
	    comp->gcons->provider->sectorsize,
	    buf,
	    comp->gcons->provider->sectorsize);
	free(buf, M_GVIRSTOR);
	g_access(comp->gcons, 0, -1, 0);
	return (error);
}

/*
 * Destroy geom forcibly.
 */
static int
g_virstor_destroy_geom(struct gctl_req *req __unused, struct g_class *mp,
    struct g_geom *gp)
{
	struct g_virstor_softc *sc;
	int exitval;

	sc = gp->softc;
	KASSERT(sc != NULL, ("%s: NULL sc", __func__));
	
	exitval = 0;
	LOG_MSG(LVL_DEBUG, "%s called for %s, sc=%p", __func__, gp->name,
	    gp->softc);

	if (sc != NULL) {
#ifdef INVARIANTS
		char *buf;
		int error;
		off_t off;
		int isclean, count;
		int n;

		LOG_MSG(LVL_INFO, "INVARIANTS detected");
		LOG_MSG(LVL_INFO, "Verifying allocation "
		    "table for %s", sc->geom->name);
		count = 0;
		for (n = 0; n < sc->chunk_count; n++) {
			if (sc->map[n].flags || VIRSTOR_MAP_ALLOCATED != 0)
				count++;
		}
		LOG_MSG(LVL_INFO, "Device %s has %d allocated chunks",
		    sc->geom->name, count);
		n = off = count = 0;
		isclean = 1;
		if (virstor_valid_components(sc) != sc->n_components) {
			/* This is a incomplete virstor device (not all
			 * components have been found) */
			LOG_MSG(LVL_ERROR, "Device %s is incomplete",
			    sc->geom->name);
			goto bailout;
		}
		error = g_access(sc->components[0].gcons, 1, 0, 0);
		KASSERT(error == 0, ("%s: g_access failed (%d)", __func__,
		    error));
		/* Compare the whole on-disk allocation table with what's
		 * currently in memory */
		while (n < sc->chunk_count) {
			buf = g_read_data(sc->components[0].gcons, off,
			    sc->sectorsize, &error);
			KASSERT(buf != NULL, ("g_read_data returned NULL (%d) "
			    "for read at %jd", error, off));
			if (bcmp(buf, &sc->map[n], sc->sectorsize) != 0) {
				LOG_MSG(LVL_ERROR, "ERROR in allocation table, "
				    "entry %d, offset %jd", n, off);
				isclean = 0;
				count++;
			}
			n += sc->me_per_sector;
			off += sc->sectorsize;
			g_free(buf);
		}
		error = g_access(sc->components[0].gcons, -1, 0, 0);
		KASSERT(error == 0, ("%s: g_access failed (%d) on exit",
		    __func__, error));
		if (isclean != 1) {
			LOG_MSG(LVL_ERROR, "ALLOCATION TABLE CORRUPTED FOR %s "
			    "(%d sectors don't match, max %zu allocations)",
			    sc->geom->name, count,
			    count * sc->me_per_sector);
		} else {
			LOG_MSG(LVL_INFO, "Allocation table ok for %s",
			    sc->geom->name);
		}
bailout:
#endif
		update_metadata(sc);
		virstor_geom_destroy(sc, FALSE, FALSE);
		exitval = EAGAIN;
	} else
		exitval = 0;
	return (exitval);
}

/*
 * Taste event (per-class callback)
 * Examines a provider and creates geom instances if needed
 */
static struct g_geom *
g_virstor_taste(struct g_class *mp, struct g_provider *pp, int flags)
{
	struct g_virstor_metadata md;
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_virstor_softc *sc;
	int error;

	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, pp->name);
	g_topology_assert();
	LOG_MSG(LVL_DEBUG, "Tasting %s", pp->name);

	/* We need a dummy geom to attach a consumer to the given provider */
	gp = g_new_geomf(mp, "virstor:taste.helper");
	gp->start = (void *)invalid_call;	/* XXX: hacked up so the        */
	gp->access = (void *)invalid_call;	/* compiler doesn't complain.   */
	gp->orphan = (void *)invalid_call;	/* I really want these to fail. */

	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	error = read_metadata(cp, &md);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);

	if (error != 0)
		return (NULL);

	if (strcmp(md.md_magic, G_VIRSTOR_MAGIC) != 0)
		return (NULL);
	if (md.md_version != G_VIRSTOR_VERSION) {
		LOG_MSG(LVL_ERROR, "Kernel module version invalid "
		    "to handle %s (%s) : %d should be %d",
		    md.md_name, pp->name, md.md_version, G_VIRSTOR_VERSION);
		return (NULL);
	}
	if (md.provsize != pp->mediasize)
		return (NULL);

	/* If the provider name is hardcoded, use the offered provider only
	 * if it's been offered with its proper name (the one used in
	 * the label command). */
	if (md.provider[0] != '\0' &&
	    !g_compare_names(md.provider, pp->name))
		return (NULL);

	/* Iterate all geoms this class already knows about to see if a new
	 * geom instance of this class needs to be created (in case the provider
	 * is first from a (possibly) multi-consumer geom) or it just needs
	 * to be added to an existing instance. */
	sc = NULL;
	gp = NULL;
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if (strcmp(md.md_name, sc->geom->name) != 0)
			continue;
		if (md.md_id != sc->id)
			continue;
		break;
	}
	if (gp != NULL) { /* We found an existing geom instance; add to it */
		LOG_MSG(LVL_INFO, "Adding %s to %s", pp->name, md.md_name);
		error = add_provider_to_geom(sc, pp, &md);
		if (error != 0) {
			LOG_MSG(LVL_ERROR, "Error adding %s to %s (error %d)",
			    pp->name, md.md_name, error);
			return (NULL);
		}
	} else { /* New geom instance needs to be created */
		gp = create_virstor_geom(mp, &md);
		if (gp == NULL) {
			LOG_MSG(LVL_ERROR, "Error creating new instance of "
			    "class %s: %s", mp->name, md.md_name);
			LOG_MSG(LVL_DEBUG, "Error creating %s at %s",
			    md.md_name, pp->name);
			return (NULL);
		}
		sc = gp->softc;
		LOG_MSG(LVL_INFO, "Adding %s to %s (first found)", pp->name,
		    md.md_name);
		error = add_provider_to_geom(sc, pp, &md);
		if (error != 0) {
			LOG_MSG(LVL_ERROR, "Error adding %s to %s (error %d)",
			    pp->name, md.md_name, error);
			virstor_geom_destroy(sc, TRUE, FALSE);
			return (NULL);
		}
	}

	return (gp);
}

/*
 * Destroyes consumer passed to it in arguments. Used as a callback
 * on g_event queue.
 */
static void
delay_destroy_consumer(void *arg, int flags __unused)
{
	struct g_consumer *c = arg;
	KASSERT(c != NULL, ("%s: invalid consumer", __func__));
	LOG_MSG(LVL_DEBUG, "Consumer %s destroyed with delay",
	    c->provider->name);
	g_detach(c);
	g_destroy_consumer(c);
}

/*
 * Remove a component (consumer) from geom instance; If it's the first
 * component being removed, orphan the provider to announce geom's being
 * dismantled
 */
static void
remove_component(struct g_virstor_softc *sc, struct g_virstor_component *comp,
    boolean_t delay)
{
	struct g_consumer *c;

	KASSERT(comp->gcons != NULL, ("Component with no consumer in %s",
	    sc->geom->name));
	c = comp->gcons;

	comp->gcons = NULL;
	KASSERT(c->provider != NULL, ("%s: no provider", __func__));
	LOG_MSG(LVL_DEBUG, "Component %s removed from %s", c->provider->name,
	    sc->geom->name);
	if (sc->provider != NULL) {
		LOG_MSG(LVL_INFO, "Removing provider %s", sc->provider->name);
		g_wither_provider(sc->provider, ENXIO);
		sc->provider = NULL;
	}

	if (c->acr > 0 || c->acw > 0 || c->ace > 0)
		g_access(c, -c->acr, -c->acw, -c->ace);
	if (delay) {
		/* Destroy consumer after it's tasted */
		g_post_event(delay_destroy_consumer, c, M_WAITOK, NULL);
	} else {
		g_detach(c);
		g_destroy_consumer(c);
	}
}

/*
 * Destroy geom - called internally
 * See g_virstor_destroy_geom for the other one
 */
static int
virstor_geom_destroy(struct g_virstor_softc *sc, boolean_t force,
    boolean_t delay)
{
	struct g_provider *pp;
	struct g_geom *gp;
	u_int n;

	g_topology_assert();

	if (sc == NULL)
		return (ENXIO);

	pp = sc->provider;
	if (pp != NULL && (pp->acr != 0 || pp->acw != 0 || pp->ace != 0)) {
		LOG_MSG(force ? LVL_WARNING : LVL_ERROR,
		    "Device %s is still open.", pp->name);
		if (!force)
			return (EBUSY);
	}

	for (n = 0; n < sc->n_components; n++) {
		if (sc->components[n].gcons != NULL)
			remove_component(sc, &sc->components[n], delay);
	}

	gp = sc->geom;
	gp->softc = NULL;

	KASSERT(sc->provider == NULL, ("Provider still exists for %s",
	    gp->name));

	/* XXX: This might or might not work, since we're called with
	 * the topology lock held. Also, it might panic the kernel if
	 * the error'd BIO is in softupdates code. */
	mtx_lock(&sc->delayed_bio_q_mtx);
	while (!STAILQ_EMPTY(&sc->delayed_bio_q)) {
		struct g_virstor_bio_q *bq;
		bq = STAILQ_FIRST(&sc->delayed_bio_q);
		bq->bio->bio_error = ENOSPC;
		g_io_deliver(bq->bio, EIO);
		STAILQ_REMOVE_HEAD(&sc->delayed_bio_q, linkage);
		free(bq, M_GVIRSTOR);
	}
	mtx_unlock(&sc->delayed_bio_q_mtx);
	mtx_destroy(&sc->delayed_bio_q_mtx);

	free(sc->map, M_GVIRSTOR);
	free(sc->components, M_GVIRSTOR);
	bzero(sc, sizeof *sc);
	free(sc, M_GVIRSTOR);

	pp = LIST_FIRST(&gp->provider); /* We only offer one provider */
	if (pp == NULL || (pp->acr == 0 && pp->acw == 0 && pp->ace == 0))
		LOG_MSG(LVL_DEBUG, "Device %s destroyed", gp->name);

	g_wither_geom(gp, ENXIO);

	return (0);
}

/*
 * Utility function: read metadata & decode. Wants topology lock to be
 * held.
 */
static int
read_metadata(struct g_consumer *cp, struct g_virstor_metadata *md)
{
	struct g_provider *pp;
	char *buf;
	int error;

	g_topology_assert();
	error = g_access(cp, 1, 0, 0);
	if (error != 0)
		return (error);
	pp = cp->provider;
	g_topology_unlock();
	buf = g_read_data(cp, pp->mediasize - pp->sectorsize, pp->sectorsize,
	    &error);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (buf == NULL)
		return (error);

	virstor_metadata_decode(buf, md);
	g_free(buf);

	return (0);
}

/**
 * Utility function: encode & write metadata. Assumes topology lock is
 * held.
 *
 * There is no useful way of recovering from errors in this function,
 * not involving panicking the kernel. If the metadata cannot be written
 * the most we can do is notify the operator and hope he spots it and
 * replaces the broken drive.
 */
static void
write_metadata(struct g_consumer *cp, struct g_virstor_metadata *md)
{
	struct g_provider *pp;
	char *buf;
	int error;

	KASSERT(cp != NULL && md != NULL && cp->provider != NULL,
	    ("Something's fishy in %s", __func__));
	LOG_MSG(LVL_DEBUG, "Writing metadata on %s", cp->provider->name);
	g_topology_assert();
	error = g_access(cp, 0, 1, 0);
	if (error != 0) {
		LOG_MSG(LVL_ERROR, "g_access(0,1,0) failed for %s: %d",
		    cp->provider->name, error);
		return;
	}
	pp = cp->provider;

	buf = malloc(pp->sectorsize, M_GVIRSTOR, M_WAITOK);
	bzero(buf, pp->sectorsize);
	virstor_metadata_encode(md, buf);
	g_topology_unlock();
	error = g_write_data(cp, pp->mediasize - pp->sectorsize, buf,
	    pp->sectorsize);
	g_topology_lock();
	g_access(cp, 0, -1, 0);
	free(buf, M_GVIRSTOR);

	if (error != 0)
		LOG_MSG(LVL_ERROR, "Error %d writing metadata to %s",
		    error, cp->provider->name);
}

/*
 * Creates a new instance of this GEOM class, initialise softc
 */
static struct g_geom *
create_virstor_geom(struct g_class *mp, struct g_virstor_metadata *md)
{
	struct g_geom *gp;
	struct g_virstor_softc *sc;

	LOG_MSG(LVL_DEBUG, "Creating geom instance for %s (id=%u)",
	    md->md_name, md->md_id);

	if (md->md_count < 1 || md->md_chunk_size < 1 ||
	    md->md_virsize < md->md_chunk_size) {
		/* This is bogus configuration, and probably means data is
		 * somehow corrupted. Panic, maybe? */
		LOG_MSG(LVL_ERROR, "Nonsensical metadata information for %s",
		    md->md_name);
		return (NULL);
	}

	/* Check if it's already created */
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc != NULL && strcmp(sc->geom->name, md->md_name) == 0) {
			LOG_MSG(LVL_WARNING, "Geom %s already exists",
			    md->md_name);
			if (sc->id != md->md_id) {
				LOG_MSG(LVL_ERROR,
				    "Some stale or invalid components "
				    "exist for virstor device named %s. "
				    "You will need to <CLEAR> all stale "
				    "components and maybe reconfigure "
				    "the virstor device. Tune "
				    "kern.geom.virstor.debug sysctl up "
				    "for more information.",
				    sc->geom->name);
			}
			return (NULL);
		}
	}
	gp = g_new_geomf(mp, "%s", md->md_name);
	gp->softc = NULL; /* to circumevent races that test softc */

	gp->start = g_virstor_start;
	gp->spoiled = g_virstor_orphan;
	gp->orphan = g_virstor_orphan;
	gp->access = g_virstor_access;
	gp->dumpconf = g_virstor_dumpconf;

	sc = malloc(sizeof(*sc), M_GVIRSTOR, M_WAITOK | M_ZERO);
	sc->id = md->md_id;
	sc->n_components = md->md_count;
	sc->components = malloc(sizeof(struct g_virstor_component) * md->md_count,
	    M_GVIRSTOR, M_WAITOK | M_ZERO);
	sc->chunk_size = md->md_chunk_size;
	sc->virsize = md->md_virsize;
	STAILQ_INIT(&sc->delayed_bio_q);
	mtx_init(&sc->delayed_bio_q_mtx, "gvirstor_delayed_bio_q_mtx",
	    "gvirstor", MTX_DEF | MTX_RECURSE);

	sc->geom = gp;
	sc->provider = NULL; /* virstor_check_and_run will create it */
	gp->softc = sc;

	LOG_MSG(LVL_ANNOUNCE, "Device %s created", sc->geom->name);

	return (gp);
}

/*
 * Add provider to a GEOM class instance
 */
static int
add_provider_to_geom(struct g_virstor_softc *sc, struct g_provider *pp,
    struct g_virstor_metadata *md)
{
	struct g_virstor_component *component;
	struct g_consumer *cp, *fcp;
	struct g_geom *gp;
	int error;

	if (md->no >= sc->n_components)
		return (EINVAL);

	/* "Current" compontent */
	component = &(sc->components[md->no]);
	if (component->gcons != NULL)
		return (EEXIST);

	gp = sc->geom;
	fcp = LIST_FIRST(&gp->consumer);

	cp = g_new_consumer(gp);
	error = g_attach(cp, pp);

	if (error != 0) {
		g_destroy_consumer(cp);
		return (error);
	}

	if (fcp != NULL) {
		if (fcp->provider->sectorsize != pp->sectorsize) {
			/* TODO: this can be made to work */
			LOG_MSG(LVL_ERROR, "Provider %s of %s has invalid "
			    "sector size (%d)", pp->name, sc->geom->name,
			    pp->sectorsize);
			return (EINVAL);
		}
		if (fcp->acr > 0 || fcp->acw || fcp->ace > 0) {
			/* Replicate access permissions from first "live" consumer
			 * to the new one */
			error = g_access(cp, fcp->acr, fcp->acw, fcp->ace);
			if (error != 0) {
				g_detach(cp);
				g_destroy_consumer(cp);
				return (error);
			}
		}
	}

	/* Bring up a new component */
	cp->private = component;
	component->gcons = cp;
	component->sc = sc;
	component->index = md->no;
	component->chunk_count = md->chunk_count;
	component->chunk_next = md->chunk_next;
	component->chunk_reserved = md->chunk_reserved;
	component->flags = md->flags;

	LOG_MSG(LVL_DEBUG, "%s attached to %s", pp->name, sc->geom->name);

	virstor_check_and_run(sc);
	return (0);
}

/*
 * Check if everything's ready to create the geom provider & device entry,
 * create and start provider.
 * Called ultimately by .taste, from g_event thread
 */
static void
virstor_check_and_run(struct g_virstor_softc *sc)
{
	off_t off;
	size_t n, count;
	int index;
	int error;

	if (virstor_valid_components(sc) != sc->n_components)
		return;

	if (virstor_valid_components(sc) == 0) {
		/* This is actually a candidate for panic() */
		LOG_MSG(LVL_ERROR, "No valid components for %s?",
		    sc->provider->name);
		return;
	}

	sc->sectorsize = sc->components[0].gcons->provider->sectorsize;

	/* Initialise allocation map from the first consumer */
	sc->chunk_count = sc->virsize / sc->chunk_size;
	if (sc->chunk_count * (off_t)sc->chunk_size != sc->virsize) {
		LOG_MSG(LVL_WARNING, "Device %s truncated to %ju bytes",
		    sc->provider->name,
		    sc->chunk_count * (off_t)sc->chunk_size);
	}
	sc->map_size = sc->chunk_count * sizeof *(sc->map);
	/* The following allocation is in order of 4MB - 8MB */
	sc->map = malloc(sc->map_size, M_GVIRSTOR, M_WAITOK);
	KASSERT(sc->map != NULL, ("%s: Memory allocation error (%zu bytes) for %s",
	    __func__, sc->map_size, sc->provider->name));
	sc->map_sectors = sc->map_size / sc->sectorsize;

	count = 0;
	for (n = 0; n < sc->n_components; n++)
		count += sc->components[n].chunk_count;
	LOG_MSG(LVL_INFO, "Device %s has %zu physical chunks and %zu virtual "
	    "(%zu KB chunks)",
	    sc->geom->name, count, sc->chunk_count, sc->chunk_size / 1024);

	error = g_access(sc->components[0].gcons, 1, 0, 0);
	if (error != 0) {
		LOG_MSG(LVL_ERROR, "Cannot acquire read access for %s to "
		    "read allocation map for %s",
		    sc->components[0].gcons->provider->name,
		    sc->geom->name);
		return;
	}
	/* Read in the allocation map */
	LOG_MSG(LVL_DEBUG, "Reading map for %s from %s", sc->geom->name,
	    sc->components[0].gcons->provider->name);
	off = count = n = 0;
	while (count < sc->map_size) {
		struct g_virstor_map_entry *mapbuf;
		size_t bs;

		bs = MIN(MAXPHYS, sc->map_size - count);
		if (bs % sc->sectorsize != 0) {
			/* Check for alignment errors */
			bs = rounddown(bs, sc->sectorsize);
			if (bs == 0)
				break;
			LOG_MSG(LVL_ERROR, "Trouble: map is not sector-aligned "
			    "for %s on %s", sc->geom->name,
			    sc->components[0].gcons->provider->name);
		}
		mapbuf = g_read_data(sc->components[0].gcons, off, bs, &error);
		if (mapbuf == NULL) {
			free(sc->map, M_GVIRSTOR);
			LOG_MSG(LVL_ERROR, "Error reading allocation map "
			    "for %s from %s (offset %ju) (error %d)",
			    sc->geom->name,
			    sc->components[0].gcons->provider->name,
			    off, error);
			return;
		}

		bcopy(mapbuf, &sc->map[n], bs);
		off += bs;
		count += bs;
		n += bs / sizeof *(sc->map);
		g_free(mapbuf);
	}
	g_access(sc->components[0].gcons, -1, 0, 0);
	LOG_MSG(LVL_DEBUG, "Read map for %s", sc->geom->name);

	/* find first component with allocatable chunks */
	index = -1;
	for (n = 0; n < sc->n_components; n++) {
		if (sc->components[n].chunk_next <
		    sc->components[n].chunk_count) {
			index = n;
			break;
		}
	}
	if (index == -1)
		/* not found? set it to the last component and handle it
		 * later */
		index = sc->n_components - 1;

	if (index >= sc->n_components - g_virstor_component_watermark - 1) {
		LOG_MSG(LVL_WARNING, "Device %s running out of components "
		    "(%d/%u: %s)", sc->geom->name,
		    index+1,
		    sc->n_components,
		    sc->components[index].gcons->provider->name);
	}
	sc->curr_component = index;

	if (sc->components[index].chunk_next >=
	    sc->components[index].chunk_count - g_virstor_chunk_watermark) {
		LOG_MSG(LVL_WARNING,
		    "Component %s of %s is running out of free space "
		    "(%u chunks left)",
		    sc->components[index].gcons->provider->name,
		    sc->geom->name, sc->components[index].chunk_count -
		    sc->components[index].chunk_next);
	}

	sc->me_per_sector = sc->sectorsize / sizeof *(sc->map);
	if (sc->sectorsize % sizeof *(sc->map) != 0) {
		LOG_MSG(LVL_ERROR,
		    "%s: Map entries don't fit exactly in a sector (%s)",
		    __func__, sc->geom->name);
		return;
	}

	/* Recalculate allocated chunks in components & at the same time
	 * verify map data is sane. We could trust metadata on this, but
	 * we want to make sure. */
	for (n = 0; n < sc->n_components; n++)
		sc->components[n].chunk_next = sc->components[n].chunk_reserved;

	for (n = 0; n < sc->chunk_count; n++) {
		if (sc->map[n].provider_no >= sc->n_components ||
			sc->map[n].provider_chunk >=
			sc->components[sc->map[n].provider_no].chunk_count) {
			LOG_MSG(LVL_ERROR, "%s: Invalid entry %u in map for %s",
			    __func__, (u_int)n, sc->geom->name);
			LOG_MSG(LVL_ERROR, "%s: provider_no: %u, n_components: %u"
			    " provider_chunk: %u, chunk_count: %u", __func__,
			    sc->map[n].provider_no, sc->n_components,
			    sc->map[n].provider_chunk,
			    sc->components[sc->map[n].provider_no].chunk_count);
			return;
		}
		if (sc->map[n].flags & VIRSTOR_MAP_ALLOCATED)
			sc->components[sc->map[n].provider_no].chunk_next++;
	}

	sc->provider = g_new_providerf(sc->geom, "virstor/%s",
	    sc->geom->name);

	sc->provider->sectorsize = sc->sectorsize;
	sc->provider->mediasize = sc->virsize;
	g_error_provider(sc->provider, 0);

	LOG_MSG(LVL_INFO, "%s activated", sc->provider->name);
	LOG_MSG(LVL_DEBUG, "%s starting with current component %u, starting "
	    "chunk %u", sc->provider->name, sc->curr_component,
	    sc->components[sc->curr_component].chunk_next);
}

/*
 * Returns count of active providers in this geom instance
 */
static u_int
virstor_valid_components(struct g_virstor_softc *sc)
{
	unsigned int nc, i;

	nc = 0;
	KASSERT(sc != NULL, ("%s: softc is NULL", __func__));
	KASSERT(sc->components != NULL, ("%s: sc->components is NULL", __func__));
	for (i = 0; i < sc->n_components; i++)
		if (sc->components[i].gcons != NULL)
			nc++;
	return (nc);
}

/*
 * Called when the consumer gets orphaned (?)
 */
static void
g_virstor_orphan(struct g_consumer *cp)
{
	struct g_virstor_softc *sc;
	struct g_virstor_component *comp;
	struct g_geom *gp;

	g_topology_assert();
	gp = cp->geom;
	sc = gp->softc;
	if (sc == NULL)
		return;

	comp = cp->private;
	KASSERT(comp != NULL, ("%s: No component in private part of consumer",
	    __func__));
	remove_component(sc, comp, FALSE);
	if (virstor_valid_components(sc) == 0)
		virstor_geom_destroy(sc, TRUE, FALSE);
}

/*
 * Called to notify geom when it's been opened, and for what intent
 */
static int
g_virstor_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_consumer *c;
	struct g_virstor_softc *sc;
	struct g_geom *gp;
	int error;

	KASSERT(pp != NULL, ("%s: NULL provider", __func__));
	gp = pp->geom;
	KASSERT(gp != NULL, ("%s: NULL geom", __func__));
	sc = gp->softc;

	if (sc == NULL) {
		/* It seems that .access can be called with negative dr,dw,dx
		 * in this case but I want to check for myself */
		LOG_MSG(LVL_WARNING, "access(%d, %d, %d) for %s",
		    dr, dw, de, pp->name);
		/* This should only happen when geom is withered so
		 * allow only negative requests */
		KASSERT(dr <= 0 && dw <= 0 && de <= 0,
		    ("%s: Positive access for %s", __func__, pp->name));
		if (pp->acr + dr == 0 && pp->acw + dw == 0 && pp->ace + de == 0)
			LOG_MSG(LVL_DEBUG, "Device %s definitely destroyed",
			    pp->name);
		return (0);
	}

	/* Grab an exclusive bit to propagate on our consumers on first open */
	if (pp->acr == 0 && pp->acw == 0 && pp->ace == 0)
		de++;
	/* ... drop it on close */
	if (pp->acr + dr == 0 && pp->acw + dw == 0 && pp->ace + de == 0) {
		de--;
		update_metadata(sc);	/* Writes statistical information */
	}

	error = ENXIO;
	LIST_FOREACH(c, &gp->consumer, consumer) {
		KASSERT(c != NULL, ("%s: consumer is NULL", __func__));
		error = g_access(c, dr, dw, de);
		if (error != 0) {
			struct g_consumer *c2;

			/* Backout earlier changes */
			LIST_FOREACH(c2, &gp->consumer, consumer) {
				if (c2 == c) /* all eariler components fixed */
					return (error);
				g_access(c2, -dr, -dw, -de);
			}
		}
	}

	return (error);
}

/*
 * Generate XML dump of current state
 */
static void
g_virstor_dumpconf(struct sbuf *sb, const char *indent, struct g_geom *gp,
    struct g_consumer *cp, struct g_provider *pp)
{
	struct g_virstor_softc *sc;

	g_topology_assert();
	sc = gp->softc;

	if (sc == NULL || pp != NULL)
		return;

	if (cp != NULL) {
		/* For each component */
		struct g_virstor_component *comp;

		comp = cp->private;
		if (comp == NULL)
			return;
		sbuf_printf(sb, "%s<ComponentIndex>%u</ComponentIndex>\n",
		    indent, comp->index);
		sbuf_printf(sb, "%s<ChunkCount>%u</ChunkCount>\n",
		    indent, comp->chunk_count);
		sbuf_printf(sb, "%s<ChunksUsed>%u</ChunksUsed>\n",
		    indent, comp->chunk_next);
		sbuf_printf(sb, "%s<ChunksReserved>%u</ChunksReserved>\n",
		    indent, comp->chunk_reserved);
		sbuf_printf(sb, "%s<StorageFree>%u%%</StorageFree>\n",
		    indent,
		    comp->chunk_next > 0 ? 100 -
		    ((comp->chunk_next + comp->chunk_reserved) * 100) /
		    comp->chunk_count : 100);
	} else {
		/* For the whole thing */
		u_int count, used, i;
		off_t size;

		count = used = size = 0;
		for (i = 0; i < sc->n_components; i++) {
			if (sc->components[i].gcons != NULL) {
				count += sc->components[i].chunk_count;
				used += sc->components[i].chunk_next +
				    sc->components[i].chunk_reserved;
				size += sc->components[i].gcons->
				    provider->mediasize;
			}
		}

		sbuf_printf(sb, "%s<Status>"
		    "Components=%u, Online=%u</Status>\n", indent,
		    sc->n_components, virstor_valid_components(sc));
		sbuf_printf(sb, "%s<State>%u%% physical free</State>\n",
		    indent, 100-(used * 100) / count);
		sbuf_printf(sb, "%s<ChunkSize>%zu</ChunkSize>\n", indent,
		    sc->chunk_size);
		sbuf_printf(sb, "%s<PhysicalFree>%u%%</PhysicalFree>\n",
		    indent, used > 0 ? 100 - (used * 100) / count : 100);
		sbuf_printf(sb, "%s<ChunkPhysicalCount>%u</ChunkPhysicalCount>\n",
		    indent, count);
		sbuf_printf(sb, "%s<ChunkVirtualCount>%zu</ChunkVirtualCount>\n",
		    indent, sc->chunk_count);
		sbuf_printf(sb, "%s<PhysicalBacking>%zu%%</PhysicalBacking>\n",
		    indent,
		    (count * 100) / sc->chunk_count);
		sbuf_printf(sb, "%s<PhysicalBackingSize>%jd</PhysicalBackingSize>\n",
		    indent, size);
		sbuf_printf(sb, "%s<VirtualSize>%jd</VirtualSize>\n", indent,
		    sc->virsize);
	}
}

/*
 * GEOM .done handler
 * Can't use standard handler because one requested IO may
 * fork into additional data IOs
 */
static void
g_virstor_done(struct bio *b)
{
	struct g_virstor_softc *sc;
	struct bio *parent_b;

	parent_b = b->bio_parent;
	sc = parent_b->bio_to->geom->softc;

	if (b->bio_error != 0) {
		LOG_MSG(LVL_ERROR, "Error %d for offset=%ju, length=%ju, %s",
		    b->bio_error, b->bio_offset, b->bio_length,
		    b->bio_to->name);
		if (parent_b->bio_error == 0)
			parent_b->bio_error = b->bio_error;
	}

	parent_b->bio_inbed++;
	parent_b->bio_completed += b->bio_completed;

	if (parent_b->bio_children == parent_b->bio_inbed) {
		parent_b->bio_completed = parent_b->bio_length;
		g_io_deliver(parent_b, parent_b->bio_error);
	}
	g_destroy_bio(b);
}

/*
 * I/O starts here
 * Called in g_down thread
 */
static void
g_virstor_start(struct bio *b)
{
	struct g_virstor_softc *sc;
	struct g_virstor_component *comp;
	struct bio *cb;
	struct g_provider *pp;
	char *addr;
	off_t offset, length;
	struct bio_queue_head bq;
	size_t chunk_size;	/* cached for convenience */
	u_int count;

	pp = b->bio_to;
	sc = pp->geom->softc;
	KASSERT(sc != NULL, ("%s: no softc (error=%d, device=%s)", __func__,
	    b->bio_to->error, b->bio_to->name));

	LOG_REQ(LVL_MOREDEBUG, b, "%s", __func__);

	switch (b->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
	case BIO_DELETE:
		break;
	default:
		g_io_deliver(b, EOPNOTSUPP);
		return;
	}

	LOG_MSG(LVL_DEBUG2, "BIO arrived, size=%ju", b->bio_length);
	bioq_init(&bq);

	chunk_size = sc->chunk_size;
	addr = b->bio_data;
	offset = b->bio_offset;	/* virtual offset and length */
	length = b->bio_length;

	while (length > 0) {
		size_t chunk_index, in_chunk_offset, in_chunk_length;
		struct virstor_map_entry *me;

		chunk_index = offset / chunk_size; /* round downwards */
		in_chunk_offset = offset % chunk_size;
		in_chunk_length = min(length, chunk_size - in_chunk_offset);
		LOG_MSG(LVL_DEBUG, "Mapped %s(%ju, %ju) to (%zu,%zu,%zu)",
		    b->bio_cmd == BIO_READ ? "R" : "W",
		    offset, length,
		    chunk_index, in_chunk_offset, in_chunk_length);
		me = &sc->map[chunk_index];

		if (b->bio_cmd == BIO_READ || b->bio_cmd == BIO_DELETE) {
			if ((me->flags & VIRSTOR_MAP_ALLOCATED) == 0) {
				/* Reads from unallocated chunks return zeroed
				 * buffers */
				if (b->bio_cmd == BIO_READ)
					bzero(addr, in_chunk_length);
			} else {
				comp = &sc->components[me->provider_no];

				cb = g_clone_bio(b);
				if (cb == NULL) {
					bioq_dismantle(&bq);
					if (b->bio_error == 0)
						b->bio_error = ENOMEM;
					g_io_deliver(b, b->bio_error);
					return;
				}
				cb->bio_to = comp->gcons->provider;
				cb->bio_done = g_virstor_done;
				cb->bio_offset =
				    (off_t)me->provider_chunk * (off_t)chunk_size
				    + in_chunk_offset;
				cb->bio_length = in_chunk_length;
				cb->bio_data = addr;
				cb->bio_caller1 = comp;
				bioq_disksort(&bq, cb);
			}
		} else { /* handle BIO_WRITE */
			KASSERT(b->bio_cmd == BIO_WRITE,
			    ("%s: Unknown command %d", __func__,
			    b->bio_cmd));

			if ((me->flags & VIRSTOR_MAP_ALLOCATED) == 0) {
				/* We have a virtual chunk, represented by
				 * the "me" entry, but it's not yet allocated
				 * (tied to) a physical chunk. So do it now. */
				struct virstor_map_entry *data_me;
				u_int phys_chunk, comp_no;
				off_t s_offset;
				int error;

				error = allocate_chunk(sc, &comp, &comp_no,
				    &phys_chunk);
				if (error != 0) {
					/* We cannot allocate a physical chunk
					 * to satisfy this request, so we'll
					 * delay it to when we can...
					 * XXX: this will prevent the fs from
					 * being umounted! */
					struct g_virstor_bio_q *biq;
					biq = malloc(sizeof *biq, M_GVIRSTOR,
					    M_NOWAIT);
					if (biq == NULL) {
						bioq_dismantle(&bq);
						if (b->bio_error == 0)
							b->bio_error = ENOMEM;
						g_io_deliver(b, b->bio_error);
						return;
					}
					biq->bio = b;
					mtx_lock(&sc->delayed_bio_q_mtx);
					STAILQ_INSERT_TAIL(&sc->delayed_bio_q,
					    biq, linkage);
					mtx_unlock(&sc->delayed_bio_q_mtx);
					LOG_MSG(LVL_WARNING, "Delaying BIO "
					    "(size=%ju) until free physical "
					    "space can be found on %s",
					    b->bio_length,
					    sc->provider->name);
					return;
				}
				LOG_MSG(LVL_DEBUG, "Allocated chunk %u on %s "
				    "for %s",
				    phys_chunk,
				    comp->gcons->provider->name,
				    sc->provider->name);

				me->provider_no = comp_no;
				me->provider_chunk = phys_chunk;
				me->flags |= VIRSTOR_MAP_ALLOCATED;

				cb = g_clone_bio(b);
				if (cb == NULL) {
					me->flags &= ~VIRSTOR_MAP_ALLOCATED;
					me->provider_no = 0;
					me->provider_chunk = 0;
					bioq_dismantle(&bq);
					if (b->bio_error == 0)
						b->bio_error = ENOMEM;
					g_io_deliver(b, b->bio_error);
					return;
				}

				/* The allocation table is stored continuously
				 * at the start of the drive. We need to
				 * calculate the offset of the sector that holds
				 * this map entry both on the drive and in the
				 * map array.
				 * sc_offset will end up pointing to the drive
				 * sector. */
				s_offset = chunk_index * sizeof *me;
				s_offset = rounddown(s_offset, sc->sectorsize);

				/* data_me points to map entry sector
				 * in memory (analogous to offset) */
				data_me = &sc->map[rounddown(chunk_index,
				    sc->me_per_sector)];

				/* Commit sector with map entry to storage */
				cb->bio_to = sc->components[0].gcons->provider;
				cb->bio_done = g_virstor_done;
				cb->bio_offset = s_offset;
				cb->bio_data = (char *)data_me;
				cb->bio_length = sc->sectorsize;
				cb->bio_caller1 = &sc->components[0];
				bioq_disksort(&bq, cb);
			}

			comp = &sc->components[me->provider_no];
			cb = g_clone_bio(b);
			if (cb == NULL) {
				bioq_dismantle(&bq);
				if (b->bio_error == 0)
					b->bio_error = ENOMEM;
				g_io_deliver(b, b->bio_error);
				return;
			}
			/* Finally, handle the data */
			cb->bio_to = comp->gcons->provider;
			cb->bio_done = g_virstor_done;
			cb->bio_offset = (off_t)me->provider_chunk*(off_t)chunk_size +
			    in_chunk_offset;
			cb->bio_length = in_chunk_length;
			cb->bio_data = addr;
			cb->bio_caller1 = comp;
			bioq_disksort(&bq, cb);
		}
		addr += in_chunk_length;
		length -= in_chunk_length;
		offset += in_chunk_length;
	}

	/* Fire off bio's here */
	count = 0;
	for (cb = bioq_first(&bq); cb != NULL; cb = bioq_first(&bq)) {
		bioq_remove(&bq, cb);
		LOG_REQ(LVL_MOREDEBUG, cb, "Firing request");
		comp = cb->bio_caller1;
		cb->bio_caller1 = NULL;
		LOG_MSG(LVL_DEBUG, " firing bio, offset=%ju, length=%ju",
		    cb->bio_offset, cb->bio_length);
		g_io_request(cb, comp->gcons);
		count++;
	}
	if (count == 0) { /* We handled everything locally */
		b->bio_completed = b->bio_length;
		g_io_deliver(b, 0);
	}

}

/*
 * Allocate a chunk from a physical provider. Returns physical component,
 * chunk index relative to the component and the component's index.
 */
static int
allocate_chunk(struct g_virstor_softc *sc, struct g_virstor_component **comp,
    u_int *comp_no_p, u_int *chunk)
{
	u_int comp_no;

	KASSERT(sc->curr_component < sc->n_components,
	    ("%s: Invalid curr_component: %u",  __func__, sc->curr_component));

	comp_no = sc->curr_component;
	*comp = &sc->components[comp_no];
	dump_component(*comp);
	if ((*comp)->chunk_next >= (*comp)->chunk_count) {
		/* This component is full. Allocate next component */
		if (comp_no >= sc->n_components-1) {
			LOG_MSG(LVL_ERROR, "All physical space allocated for %s",
			    sc->geom->name);
			return (-1);
		}
		(*comp)->flags &= ~VIRSTOR_PROVIDER_CURRENT;
		sc->curr_component = ++comp_no;

		*comp = &sc->components[comp_no];
		if (comp_no >= sc->n_components - g_virstor_component_watermark-1)
			LOG_MSG(LVL_WARNING, "Device %s running out of components "
			    "(switching to %u/%u: %s)", sc->geom->name,
			    comp_no+1, sc->n_components,
			    (*comp)->gcons->provider->name);
		/* Take care not to overwrite reserved chunks */
		if ( (*comp)->chunk_reserved > 0 &&
		    (*comp)->chunk_next < (*comp)->chunk_reserved)
			(*comp)->chunk_next = (*comp)->chunk_reserved;

		(*comp)->flags |=
		    VIRSTOR_PROVIDER_ALLOCATED | VIRSTOR_PROVIDER_CURRENT;
		dump_component(*comp);
		*comp_no_p = comp_no;
		*chunk = (*comp)->chunk_next++;
	} else {
		*comp_no_p = comp_no;
		*chunk = (*comp)->chunk_next++;
	}
	return (0);
}

/* Dump a component */
static void
dump_component(struct g_virstor_component *comp)
{

	if (g_virstor_debug < LVL_DEBUG2)
		return;
	printf("Component %d: %s\n", comp->index, comp->gcons->provider->name);
	printf("  chunk_count: %u\n", comp->chunk_count);
	printf("   chunk_next: %u\n", comp->chunk_next);
	printf("        flags: %u\n", comp->flags);
}

#if 0
/* Dump a map entry */
static void
dump_me(struct virstor_map_entry *me, unsigned int nr)
{
	if (g_virstor_debug < LVL_DEBUG)
		return;
	printf("VIRT. CHUNK #%d: ", nr);
	if ((me->flags & VIRSTOR_MAP_ALLOCATED) == 0)
		printf("(unallocated)\n");
	else
		printf("allocated at provider %u, provider_chunk %u\n",
		    me->provider_no, me->provider_chunk);
}
#endif

/*
 * Dismantle bio_queue and destroy its components
 */
static void
bioq_dismantle(struct bio_queue_head *bq)
{
	struct bio *b;

	for (b = bioq_first(bq); b != NULL; b = bioq_first(bq)) {
		bioq_remove(bq, b);
		g_destroy_bio(b);
	}
}

/*
 * The function that shouldn't be called.
 * When this is called, the stack is already garbled because of
 * argument mismatch. There's nothing to do now but panic, which is
 * accidentally the whole purpose of this function.
 * Motivation: to guard from accidentally calling geom methods when
 * they shouldn't be called. (see g_..._taste)
 */
static void
invalid_call(void)
{
	panic("invalid_call() has just been called. Something's fishy here.");
}

DECLARE_GEOM_CLASS(g_virstor_class, g_virstor); /* Let there be light */
MODULE_VERSION(geom_virstor, 0);
