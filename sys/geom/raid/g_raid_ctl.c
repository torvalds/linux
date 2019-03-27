/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Alexander Motin <mav@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/bitstring.h>
#include <vm/uma.h>
#include <machine/atomic.h>
#include <geom/geom.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <geom/raid/g_raid.h>
#include "g_raid_md_if.h"


static struct g_raid_softc *
g_raid_find_node(struct g_class *mp, const char *name)
{
	struct g_raid_softc *sc;
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_raid_volume *vol;

	/* Look for geom with specified name. */
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_stopping != 0)
			continue;
		if (strcasecmp(sc->sc_name, name) == 0)
			return (sc);
	}

	/* Look for provider with specified name. */
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_stopping != 0)
			continue;
		LIST_FOREACH(pp, &gp->provider, provider) {
			if (strcmp(pp->name, name) == 0)
				return (sc);
			if (strncmp(pp->name, "raid/", 5) == 0 &&
			    strcmp(pp->name + 5, name) == 0)
				return (sc);
		}
	}

	/* Look for volume with specified name. */
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if (sc->sc_stopping != 0)
			continue;
		TAILQ_FOREACH(vol, &sc->sc_volumes, v_next) {
			if (strcmp(vol->v_name, name) == 0)
				return (sc);
		}
	}
	return (NULL);
}

static void
g_raid_ctl_label(struct gctl_req *req, struct g_class *mp)
{
	struct g_geom *geom;
	struct g_raid_softc *sc;
	const char *format;
	int *nargs;
	int crstatus, ctlstatus;
	char buf[64];

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs < 4) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	format = gctl_get_asciiparam(req, "arg0");
	if (format == NULL) {
		gctl_error(req, "No format received.");
		return;
	}
	crstatus = g_raid_create_node_format(format, req, &geom);
	if (crstatus == G_RAID_MD_TASTE_FAIL) {
		gctl_error(req, "Failed to create array with format '%s'.",
		    format);
		return;
	}
	sc = (struct g_raid_softc *)geom->softc;
	g_topology_unlock();
	sx_xlock(&sc->sc_lock);
	ctlstatus = G_RAID_MD_CTL(sc->sc_md, req);
	if (ctlstatus < 0) {
		gctl_error(req, "Command failed: %d.", ctlstatus);
		if (crstatus == G_RAID_MD_TASTE_NEW)
			g_raid_destroy_node(sc, 0);
	} else {
		if (crstatus == G_RAID_MD_TASTE_NEW)
			snprintf(buf, sizeof(buf), "%s created\n", sc->sc_name);
		else
			snprintf(buf, sizeof(buf), "%s reused\n", sc->sc_name);
		gctl_set_param_err(req, "output", buf, strlen(buf) + 1);
	}
	sx_xunlock(&sc->sc_lock);
	g_topology_lock();
}

static void
g_raid_ctl_stop(struct gctl_req *req, struct g_class *mp)
{
	struct g_raid_softc *sc;
	const char *nodename;
	int *nargs, *force;
	int error, how;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs != 1) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	nodename = gctl_get_asciiparam(req, "arg0");
	if (nodename == NULL) {
		gctl_error(req, "No array name received.");
		return;
	}
	sc = g_raid_find_node(mp, nodename);
	if (sc == NULL) {
		gctl_error(req, "Array '%s' not found.", nodename);
		return;
	}
	force = gctl_get_paraml(req, "force", sizeof(*force));
	if (force != NULL && *force)
		how = G_RAID_DESTROY_HARD;
	else
		how = G_RAID_DESTROY_SOFT;
	g_topology_unlock();
	sx_xlock(&sc->sc_lock);
	error = g_raid_destroy(sc, how);
	if (error != 0)
		gctl_error(req, "Array is busy.");
	g_topology_lock();
}

static void
g_raid_ctl_other(struct gctl_req *req, struct g_class *mp)
{
	struct g_raid_softc *sc;
	const char *nodename;
	int *nargs;
	int ctlstatus;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs < 1) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	nodename = gctl_get_asciiparam(req, "arg0");
	if (nodename == NULL) {
		gctl_error(req, "No array name received.");
		return;
	}
	sc = g_raid_find_node(mp, nodename);
	if (sc == NULL) {
		gctl_error(req, "Array '%s' not found.", nodename);
		return;
	}
	g_topology_unlock();
	sx_xlock(&sc->sc_lock);
	if (sc->sc_md != NULL) {
		ctlstatus = G_RAID_MD_CTL(sc->sc_md, req);
		if (ctlstatus < 0)
			gctl_error(req, "Command failed: %d.", ctlstatus);
	}
	sx_xunlock(&sc->sc_lock);
	g_topology_lock();
}

void
g_raid_ctl(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}
	if (*version != G_RAID_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync.");
		return;
	}

	if (strcmp(verb, "label") == 0)
		g_raid_ctl_label(req, mp);
	else if (strcmp(verb, "stop") == 0)
		g_raid_ctl_stop(req, mp);
	else
		g_raid_ctl_other(req, mp);
}
