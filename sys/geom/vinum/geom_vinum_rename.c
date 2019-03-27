/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 2005 Chris Jones
 *  All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Chris Jones
 * thanks to the support of Google's Summer of Code program and
 * mentoring by Lukas Ertl.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/libkern.h>
#include <sys/malloc.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum.h>

void
gv_rename(struct g_geom *gp, struct gctl_req *req)
{
	struct gv_softc *sc;
	struct gv_volume *v;
	struct gv_plex *p;
	struct gv_sd *s;
	struct gv_drive *d;
	char *newname, *object, *name;
	int *flags, type;

	sc = gp->softc;

	flags = gctl_get_paraml(req, "flags", sizeof(*flags));
	if (flags == NULL) {
		gctl_error(req, "no flags given");
		return;
	}

	newname = gctl_get_param(req, "newname", NULL);
	if (newname == NULL) {
		gctl_error(req, "no new name given");
		return;
	}

	object = gctl_get_param(req, "object", NULL);
	if (object == NULL) {
		gctl_error(req, "no object given");
		return;
	}

	type = gv_object_type(sc, object);
	switch (type) {
	case GV_TYPE_VOL:
		v = gv_find_vol(sc, object);
		if (v == NULL) 	{
			gctl_error(req, "unknown volume '%s'", object);
			return;
		}
		name = g_malloc(GV_MAXVOLNAME, M_WAITOK | M_ZERO);
		strlcpy(name, newname, GV_MAXVOLNAME);
		gv_post_event(sc, GV_EVENT_RENAME_VOL, v, name, *flags, 0);
		break;
	case GV_TYPE_PLEX:
		p = gv_find_plex(sc, object);
		if (p == NULL) {
			gctl_error(req, "unknown plex '%s'", object);
			return;
		}
		name = g_malloc(GV_MAXPLEXNAME, M_WAITOK | M_ZERO);
		strlcpy(name, newname, GV_MAXPLEXNAME);
		gv_post_event(sc, GV_EVENT_RENAME_PLEX, p, name, *flags, 0);
		break;
	case GV_TYPE_SD:
		s = gv_find_sd(sc, object);
		if (s == NULL) {
			gctl_error(req, "unknown subdisk '%s'", object);
			return;
		}
		name = g_malloc(GV_MAXSDNAME, M_WAITOK | M_ZERO);
		strlcpy(name, newname, GV_MAXSDNAME);
		gv_post_event(sc, GV_EVENT_RENAME_SD, s, name, *flags, 0);
		break;
	case GV_TYPE_DRIVE:
		d = gv_find_drive(sc, object);
		if (d == NULL) {
			gctl_error(req, "unknown drive '%s'", object);
			return;
		}
		name = g_malloc(GV_MAXDRIVENAME, M_WAITOK | M_ZERO);
		strlcpy(name, newname, GV_MAXDRIVENAME);
		gv_post_event(sc, GV_EVENT_RENAME_DRIVE, d, name, *flags, 0);
		break;
	default:
		gctl_error(req, "unknown object '%s'", object);
		return;
	}
}

int
gv_rename_drive(struct gv_softc *sc, struct gv_drive *d, char *newname,
    int flags)
{
	struct gv_sd *s;

	KASSERT(d != NULL, ("gv_rename_drive: NULL d"));

	if (gv_object_type(sc, newname) != GV_ERR_NOTFOUND) {
		G_VINUM_DEBUG(1, "drive name '%s' already in use", newname);
		return (GV_ERR_NAMETAKEN);
	}

	strlcpy(d->name, newname, sizeof(d->name));
	if (d->hdr != NULL)
		strlcpy(d->hdr->label.name, newname, sizeof(d->hdr->label.name));

	LIST_FOREACH(s, &d->subdisks, from_drive)
		strlcpy(s->drive, d->name, sizeof(s->drive));

	return (0);
}

int
gv_rename_plex(struct gv_softc *sc, struct gv_plex *p, char *newname, int flags)
{
	char newsd[GV_MAXSDNAME];
	struct gv_sd *s;
	char *ptr;
	int err;

	KASSERT(p != NULL, ("gv_rename_plex: NULL p"));

	if (gv_object_type(sc, newname) != GV_ERR_NOTFOUND) {
		G_VINUM_DEBUG(1, "plex name '%s' already in use", newname);
		return (GV_ERR_NAMETAKEN);
	}

	/*
	 * Locate the plex number part of the plex names.
	 * XXX: might be a good idea to sanitize input a bit more
	 */
	ptr = strrchr(newname, '.');
	if (ptr == NULL) {
		G_VINUM_DEBUG(0, "proposed plex name '%s' is not a valid plex "
		    "name", newname);
		return (GV_ERR_INVNAME);
	}

	strlcpy(p->name, newname, sizeof(p->name));

	/* Fix up references and potentially rename subdisks. */
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		strlcpy(s->plex, p->name, sizeof(s->plex));
		if (flags & GV_FLAG_R) {
			/*
			 * Look for the two last dots in the string, and assume
			 * that the old value was ok.
			 */
			ptr = strrchr(s->name, '.');
			if (ptr == NULL)
				return (GV_ERR_INVNAME);
			ptr++;
			snprintf(newsd, sizeof(newsd), "%s.%s", p->name, ptr);
			err = gv_rename_sd(sc, s, newsd, flags);
			if (err)
				return (err);
		}
	}
	return (0);
}

/*
 * gv_rename_sd: renames a subdisk.  Note that the 'flags' argument is ignored,
 * since there are no structures below a subdisk.  Similarly, we don't have to
 * clean up any references elsewhere to the subdisk's name.
 */
int
gv_rename_sd(struct gv_softc *sc, struct gv_sd *s, char *newname, int flags)
{
	char *dot1, *dot2;

	KASSERT(s != NULL, ("gv_rename_sd: NULL s"));

	if (gv_object_type(sc, newname) != GV_ERR_NOTFOUND) {
		G_VINUM_DEBUG(1, "subdisk name %s already in use", newname);
		return (GV_ERR_NAMETAKEN);
	}

	/* Locate the sd number part of the sd names. */
	dot1 = strchr(newname, '.');
	if (dot1 == NULL || (dot2 = strchr(dot1 +  1, '.')) == NULL) {
		G_VINUM_DEBUG(0, "proposed sd name '%s' is not a valid sd name",
		    newname);
		return (GV_ERR_INVNAME);
	}
	strlcpy(s->name, newname, sizeof(s->name));
	return (0);
}

int
gv_rename_vol(struct gv_softc *sc, struct gv_volume *v, char *newname,
    int flags)
{
	struct g_provider *pp;
	struct gv_plex *p;
	char newplex[GV_MAXPLEXNAME], *ptr;
	int err;

	KASSERT(v != NULL, ("gv_rename_vol: NULL v"));
	pp = v->provider;
	KASSERT(pp != NULL, ("gv_rename_vol: NULL pp"));

	if (gv_object_type(sc, newname) != GV_ERR_NOTFOUND) {
		G_VINUM_DEBUG(1, "volume name %s already in use", newname);
		return (GV_ERR_NAMETAKEN);
	}

	/* Rename the volume. */
	strlcpy(v->name, newname, sizeof(v->name));

	/* Fix up references and potentially rename plexes. */
	LIST_FOREACH(p, &v->plexes, in_volume) {
		strlcpy(p->volume, v->name, sizeof(p->volume));
		if (flags & GV_FLAG_R) {
			/*
			 * Look for the last dot in the string, and assume that
			 * the old value was ok.
			 */
			ptr = strrchr(p->name, '.');
			ptr++;
			snprintf(newplex, sizeof(newplex), "%s.%s", v->name, ptr);
			err = gv_rename_plex(sc, p, newplex, flags);
			if (err)
				return (err);
		}
	}

	return (0);
}
