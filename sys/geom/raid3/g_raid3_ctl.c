/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <geom/raid3/g_raid3.h>


static struct g_raid3_softc *
g_raid3_find_device(struct g_class *mp, const char *name)
{
	struct g_raid3_softc *sc;
	struct g_geom *gp;

	g_topology_lock();
	LIST_FOREACH(gp, &mp->geom, geom) {
		sc = gp->softc;
		if (sc == NULL)
			continue;
		if ((sc->sc_flags & G_RAID3_DEVICE_FLAG_DESTROY) != 0)
			continue;
		if (strcmp(gp->name, name) == 0 ||
		    strcmp(sc->sc_name, name) == 0) {
			g_topology_unlock();
			sx_xlock(&sc->sc_lock);
			return (sc);
		}
	}
	g_topology_unlock();
	return (NULL);
}

static struct g_raid3_disk *
g_raid3_find_disk(struct g_raid3_softc *sc, const char *name)
{
	struct g_raid3_disk *disk;
	u_int n;

	sx_assert(&sc->sc_lock, SX_XLOCKED);
	if (strncmp(name, "/dev/", 5) == 0)
		name += 5;
	for (n = 0; n < sc->sc_ndisks; n++) {
		disk = &sc->sc_disks[n];
		if (disk->d_state == G_RAID3_DISK_STATE_NODISK)
			continue;
		if (disk->d_consumer == NULL)
			continue;
		if (disk->d_consumer->provider == NULL)
			continue;
		if (strcmp(disk->d_consumer->provider->name, name) == 0)
			return (disk);
	}
	return (NULL);
}

static void
g_raid3_ctl_configure(struct gctl_req *req, struct g_class *mp)
{
	struct g_raid3_softc *sc;
	struct g_raid3_disk *disk;
	const char *name;
	int *nargs, do_sync = 0, dirty = 1;
	int *autosync, *noautosync;
	int *failsync, *nofailsync;
	int *round_robin, *noround_robin;
	int *verify, *noverify;
	u_int n;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs != 1) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	autosync = gctl_get_paraml(req, "autosync", sizeof(*autosync));
	if (autosync == NULL) {
		gctl_error(req, "No '%s' argument.", "autosync");
		return;
	}
	noautosync = gctl_get_paraml(req, "noautosync", sizeof(*noautosync));
	if (noautosync == NULL) {
		gctl_error(req, "No '%s' argument.", "noautosync");
		return;
	}
	if (*autosync && *noautosync) {
		gctl_error(req, "'%s' and '%s' specified.", "autosync",
		    "noautosync");
		return;
	}
	failsync = gctl_get_paraml(req, "failsync", sizeof(*failsync));
	if (failsync == NULL) {
		gctl_error(req, "No '%s' argument.", "failsync");
		return;
	}
	nofailsync = gctl_get_paraml(req, "nofailsync", sizeof(*nofailsync));
	if (nofailsync == NULL) {
		gctl_error(req, "No '%s' argument.", "nofailsync");
		return;
	}
	if (*failsync && *nofailsync) {
		gctl_error(req, "'%s' and '%s' specified.", "failsync",
		    "nofailsync");
		return;
	}
	round_robin = gctl_get_paraml(req, "round_robin", sizeof(*round_robin));
	if (round_robin == NULL) {
		gctl_error(req, "No '%s' argument.", "round_robin");
		return;
	}
	noround_robin = gctl_get_paraml(req, "noround_robin",
	    sizeof(*noround_robin));
	if (noround_robin == NULL) {
		gctl_error(req, "No '%s' argument.", "noround_robin");
		return;
	}
	if (*round_robin && *noround_robin) {
		gctl_error(req, "'%s' and '%s' specified.", "round_robin",
		    "noround_robin");
		return;
	}
	verify = gctl_get_paraml(req, "verify", sizeof(*verify));
	if (verify == NULL) {
		gctl_error(req, "No '%s' argument.", "verify");
		return;
	}
	noverify = gctl_get_paraml(req, "noverify", sizeof(*noverify));
	if (noverify == NULL) {
		gctl_error(req, "No '%s' argument.", "noverify");
		return;
	}
	if (*verify && *noverify) {
		gctl_error(req, "'%s' and '%s' specified.", "verify",
		    "noverify");
		return;
	}
	if (!*autosync && !*noautosync && !*failsync && !*nofailsync &&
	    !*round_robin && !*noround_robin && !*verify && !*noverify) {
		gctl_error(req, "Nothing has changed.");
		return;
	}
	name = gctl_get_asciiparam(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg%u' argument.", 0);
		return;
	}
	sc = g_raid3_find_device(mp, name);
	if (sc == NULL) {
		gctl_error(req, "No such device: %s.", name);
		return;
	}
	if (g_raid3_ndisks(sc, -1) < sc->sc_ndisks) {
		gctl_error(req, "Not all disks connected.");
		sx_xunlock(&sc->sc_lock);
		return;
	}
	if ((sc->sc_flags & G_RAID3_DEVICE_FLAG_NOAUTOSYNC) != 0) {
		if (*autosync) {
			sc->sc_flags &= ~G_RAID3_DEVICE_FLAG_NOAUTOSYNC;
			do_sync = 1;
		}
	} else {
		if (*noautosync)
			sc->sc_flags |= G_RAID3_DEVICE_FLAG_NOAUTOSYNC;
	}
	if ((sc->sc_flags & G_RAID3_DEVICE_FLAG_NOFAILSYNC) != 0) {
		if (*failsync)
			sc->sc_flags &= ~G_RAID3_DEVICE_FLAG_NOFAILSYNC;
	} else {
		if (*nofailsync) {
			sc->sc_flags |= G_RAID3_DEVICE_FLAG_NOFAILSYNC;
			dirty = 0;
		}
	}
	if ((sc->sc_flags & G_RAID3_DEVICE_FLAG_VERIFY) != 0) {
		if (*noverify)
			sc->sc_flags &= ~G_RAID3_DEVICE_FLAG_VERIFY;
	} else {
		if (*verify)
			sc->sc_flags |= G_RAID3_DEVICE_FLAG_VERIFY;
	}
	if ((sc->sc_flags & G_RAID3_DEVICE_FLAG_ROUND_ROBIN) != 0) {
		if (*noround_robin)
			sc->sc_flags &= ~G_RAID3_DEVICE_FLAG_ROUND_ROBIN;
	} else {
		if (*round_robin)
			sc->sc_flags |= G_RAID3_DEVICE_FLAG_ROUND_ROBIN;
	}
	if ((sc->sc_flags & G_RAID3_DEVICE_FLAG_VERIFY) != 0 &&
	    (sc->sc_flags & G_RAID3_DEVICE_FLAG_ROUND_ROBIN) != 0) {
		/*
		 * VERIFY and ROUND-ROBIN options are mutally exclusive.
		 */
		sc->sc_flags &= ~G_RAID3_DEVICE_FLAG_ROUND_ROBIN;
	}
	for (n = 0; n < sc->sc_ndisks; n++) {
		disk = &sc->sc_disks[n];
		if (do_sync) {
			if (disk->d_state == G_RAID3_DISK_STATE_SYNCHRONIZING)
				disk->d_flags &= ~G_RAID3_DISK_FLAG_FORCE_SYNC;
		}
		if (!dirty)
			disk->d_flags &= ~G_RAID3_DISK_FLAG_DIRTY;
		g_raid3_update_metadata(disk);
		if (do_sync) {
			if (disk->d_state == G_RAID3_DISK_STATE_STALE) {
				/*
				 * XXX: This is probably possible that this
				 *      component will not be retasted.
				 */
				g_raid3_event_send(disk,
				    G_RAID3_DISK_STATE_DISCONNECTED,
				    G_RAID3_EVENT_DONTWAIT);
			}
		}
	}
	sx_xunlock(&sc->sc_lock);
}

static void
g_raid3_ctl_rebuild(struct gctl_req *req, struct g_class *mp)
{
	struct g_raid3_metadata md;
	struct g_raid3_softc *sc;
	struct g_raid3_disk *disk;
	struct g_provider *pp;
	const char *name;
	int error, *nargs;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs != 2) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	name = gctl_get_asciiparam(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg%u' argument.", 0);
		return;
	}
	sc = g_raid3_find_device(mp, name);
	if (sc == NULL) {
		gctl_error(req, "No such device: %s.", name);
		return;
	}
	name = gctl_get_asciiparam(req, "arg1");
	if (name == NULL) {
		gctl_error(req, "No 'arg%u' argument.", 1);
		sx_xunlock(&sc->sc_lock);
		return;
	}
	disk = g_raid3_find_disk(sc, name);
	if (disk == NULL) {
		gctl_error(req, "No such provider: %s.", name);
		sx_xunlock(&sc->sc_lock);
		return;
	}
	if (disk->d_state == G_RAID3_DISK_STATE_ACTIVE &&
	    g_raid3_ndisks(sc, G_RAID3_DISK_STATE_ACTIVE) < sc->sc_ndisks) {
		gctl_error(req, "There is one stale disk already.");
		sx_xunlock(&sc->sc_lock);
		return;
	}
	/*
	 * Do rebuild by resetting syncid and disconnecting disk.
	 * It'll be retasted, connected to the device and synchronized.
	 */
	disk->d_sync.ds_syncid = 0;
	if ((sc->sc_flags & G_RAID3_DEVICE_FLAG_NOAUTOSYNC) != 0)
		disk->d_flags |= G_RAID3_DISK_FLAG_FORCE_SYNC;
	g_raid3_update_metadata(disk);
	pp = disk->d_consumer->provider;
	g_topology_lock();
	error = g_raid3_read_metadata(disk->d_consumer, &md);
	g_topology_unlock();
	g_raid3_event_send(disk, G_RAID3_DISK_STATE_DISCONNECTED,
	    G_RAID3_EVENT_WAIT);
	if (error != 0) {
		gctl_error(req, "Cannot read metadata from %s.", pp->name);
		sx_xunlock(&sc->sc_lock);
		return;
	}
	error = g_raid3_add_disk(sc, pp, &md);
	if (error != 0)
		gctl_error(req, "Cannot reconnect component %s.", pp->name);
	sx_xunlock(&sc->sc_lock);
}

static void
g_raid3_ctl_stop(struct gctl_req *req, struct g_class *mp)
{
	struct g_raid3_softc *sc;
	int *force, *nargs, error;
	const char *name;
	char param[16];
	u_int i;
	int how;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs < 1) {
		gctl_error(req, "Missing device(s).");
		return;
	}
	force = gctl_get_paraml(req, "force", sizeof(*force));
	if (force == NULL) {
		gctl_error(req, "No '%s' argument.", "force");
		return;
	}
	if (*force)
		how = G_RAID3_DESTROY_HARD;
	else
		how = G_RAID3_DESTROY_SOFT;

	for (i = 0; i < (u_int)*nargs; i++) {
		snprintf(param, sizeof(param), "arg%u", i);
		name = gctl_get_asciiparam(req, param);
		if (name == NULL) {
			gctl_error(req, "No 'arg%u' argument.", i);
			return;
		}
		sc = g_raid3_find_device(mp, name);
		if (sc == NULL) {
			gctl_error(req, "No such device: %s.", name);
			return;
		}
		g_cancel_event(sc);
		error = g_raid3_destroy(sc, how);
		if (error != 0) {
			gctl_error(req, "Cannot destroy device %s (error=%d).",
			    sc->sc_geom->name, error);
			sx_xunlock(&sc->sc_lock);
			return;
		}
		/* No need to unlock, because lock is already dead. */
	}
}

static void
g_raid3_ctl_insert_orphan(struct g_consumer *cp)
{

	KASSERT(1 == 0, ("%s called while inserting %s.", __func__,
	    cp->provider->name));
}

static void
g_raid3_ctl_insert(struct gctl_req *req, struct g_class *mp)
{
	struct g_raid3_metadata md;
	struct g_raid3_softc *sc;
	struct g_raid3_disk *disk;
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_consumer *cp;
	const char *name;
	u_char *sector;
	off_t compsize;
	intmax_t *no;
	int *hardcode, *nargs, error, autono;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs != 2) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	hardcode = gctl_get_paraml(req, "hardcode", sizeof(*hardcode));
	if (hardcode == NULL) {
		gctl_error(req, "No '%s' argument.", "hardcode");
		return;
	}
	name = gctl_get_asciiparam(req, "arg1");
	if (name == NULL) {
		gctl_error(req, "No 'arg%u' argument.", 1);
		return;
	}
	if (gctl_get_param(req, "number", NULL) != NULL)
		no = gctl_get_paraml(req, "number", sizeof(*no));
	else
		no = NULL;
	if (strncmp(name, "/dev/", 5) == 0)
		name += 5;
	g_topology_lock();
	pp = g_provider_by_name(name);
	if (pp == NULL) {
		g_topology_unlock();
		gctl_error(req, "Invalid provider.");
		return;
	}
	gp = g_new_geomf(mp, "raid3:insert");
	gp->orphan = g_raid3_ctl_insert_orphan;
	cp = g_new_consumer(gp);
	error = g_attach(cp, pp);
	if (error != 0) {
		g_topology_unlock();
		gctl_error(req, "Cannot attach to %s.", pp->name);
		goto end;
	}
	error = g_access(cp, 0, 1, 1);
	if (error != 0) {
		g_topology_unlock();
		gctl_error(req, "Cannot access %s.", pp->name);
		goto end;
	}
	g_topology_unlock();
	name = gctl_get_asciiparam(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg%u' argument.", 0);
		goto end;
	}
	sc = g_raid3_find_device(mp, name);
	if (sc == NULL) {
		gctl_error(req, "No such device: %s.", name);
		goto end;
	}
	if (no != NULL) {
		if (*no < 0 || *no >= sc->sc_ndisks) {
			sx_xunlock(&sc->sc_lock);
			gctl_error(req, "Invalid component number.");
			goto end;
		}
		disk = &sc->sc_disks[*no];
		if (disk->d_state != G_RAID3_DISK_STATE_NODISK) {
			sx_xunlock(&sc->sc_lock);
			gctl_error(req, "Component %jd is already connected.",
			    *no);
			goto end;
		}
	} else {
		disk = NULL;
		for (autono = 0; autono < sc->sc_ndisks && disk == NULL; autono++)
			if (sc->sc_disks[autono].d_state ==
			    G_RAID3_DISK_STATE_NODISK)
				disk = &sc->sc_disks[autono];
		if (disk == NULL) {
			sx_xunlock(&sc->sc_lock);
			gctl_error(req, "No disconnected components.");
			goto end;
		}
	}
	if (((sc->sc_sectorsize / (sc->sc_ndisks - 1)) % pp->sectorsize) != 0) {
		sx_xunlock(&sc->sc_lock);
		gctl_error(req,
		    "Cannot insert provider %s, because of its sector size.",
		    pp->name);
		goto end;
	}
	compsize = sc->sc_mediasize / (sc->sc_ndisks - 1);
	if (compsize > pp->mediasize - pp->sectorsize) {
		sx_xunlock(&sc->sc_lock);
		gctl_error(req, "Provider %s too small.", pp->name);
		goto end;
	}
	if (compsize < pp->mediasize - pp->sectorsize) {
		gctl_error(req,
		    "warning: %s: only %jd bytes from %jd bytes used.",
		    pp->name, (intmax_t)compsize,
		    (intmax_t)(pp->mediasize - pp->sectorsize));
	}
	g_raid3_fill_metadata(disk, &md);
	sx_xunlock(&sc->sc_lock);
	md.md_syncid = 0;
	md.md_dflags = 0;
	if (*hardcode)
		strlcpy(md.md_provider, pp->name, sizeof(md.md_provider));
	else
		bzero(md.md_provider, sizeof(md.md_provider));
	md.md_provsize = pp->mediasize;
	sector = g_malloc(pp->sectorsize, M_WAITOK);
	raid3_metadata_encode(&md, sector);
	error = g_write_data(cp, pp->mediasize - pp->sectorsize, sector,
	    pp->sectorsize);
	g_free(sector);
	if (error != 0)
		gctl_error(req, "Cannot store metadata on %s.", pp->name);
end:
	g_topology_lock();
	if (cp->acw > 0)
		g_access(cp, 0, -1, -1);
	if (cp->provider != NULL)
		g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	g_topology_unlock();
}

static void
g_raid3_ctl_remove(struct gctl_req *req, struct g_class *mp)
{
	struct g_raid3_softc *sc;
	struct g_raid3_disk *disk;
	const char *name;
	intmax_t *no;
	int *nargs;

	nargs = gctl_get_paraml(req, "nargs", sizeof(*nargs));
	if (nargs == NULL) {
		gctl_error(req, "No '%s' argument.", "nargs");
		return;
	}
	if (*nargs != 1) {
		gctl_error(req, "Invalid number of arguments.");
		return;
	}
	no = gctl_get_paraml(req, "number", sizeof(*no));
	if (no == NULL) {
		gctl_error(req, "No '%s' argument.", "no");
		return;
	}
	name = gctl_get_asciiparam(req, "arg0");
	if (name == NULL) {
		gctl_error(req, "No 'arg%u' argument.", 0);
		return;
	}
	sc = g_raid3_find_device(mp, name);
	if (sc == NULL) {
		gctl_error(req, "No such device: %s.", name);
		return;
	}
	if (*no >= sc->sc_ndisks) {
		sx_xunlock(&sc->sc_lock);
		gctl_error(req, "Invalid component number.");
		return;
	}
	disk = &sc->sc_disks[*no];
	switch (disk->d_state) {
	case G_RAID3_DISK_STATE_ACTIVE:
		/*
		 * When replacing ACTIVE component, all the rest has to be also
		 * ACTIVE.
		 */
		if (g_raid3_ndisks(sc, G_RAID3_DISK_STATE_ACTIVE) <
		    sc->sc_ndisks) {
			gctl_error(req, "Cannot replace component number %jd.",
			    *no);
			break;
		}
		/* FALLTHROUGH */
	case G_RAID3_DISK_STATE_STALE:
	case G_RAID3_DISK_STATE_SYNCHRONIZING:
		if (g_raid3_clear_metadata(disk) != 0) {
			gctl_error(req, "Cannot clear metadata on %s.",
			    g_raid3_get_diskname(disk));
		} else {
			g_raid3_event_send(disk,
			    G_RAID3_DISK_STATE_DISCONNECTED,
			    G_RAID3_EVENT_DONTWAIT);
		}
		break;
	case G_RAID3_DISK_STATE_NODISK:
		break;
	default:
		gctl_error(req, "Cannot replace component number %jd.", *no);
		break;
	}
	sx_xunlock(&sc->sc_lock);
}

void
g_raid3_config(struct gctl_req *req, struct g_class *mp, const char *verb)
{
	uint32_t *version;

	g_topology_assert();

	version = gctl_get_paraml(req, "version", sizeof(*version));
	if (version == NULL) {
		gctl_error(req, "No '%s' argument.", "version");
		return;
	}
	if (*version != G_RAID3_VERSION) {
		gctl_error(req, "Userland and kernel parts are out of sync.");
		return;
	}

	g_topology_unlock();
	if (strcmp(verb, "configure") == 0)
		g_raid3_ctl_configure(req, mp);
	else if (strcmp(verb, "insert") == 0)
		g_raid3_ctl_insert(req, mp);
	else if (strcmp(verb, "rebuild") == 0)
		g_raid3_ctl_rebuild(req, mp);
	else if (strcmp(verb, "remove") == 0)
		g_raid3_ctl_remove(req, mp);
	else if (strcmp(verb, "stop") == 0)
		g_raid3_ctl_stop(req, mp);
	else
		gctl_error(req, "Unknown verb.");
	g_topology_lock();
}
