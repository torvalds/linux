/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 *  Copyright (c) 2004, 2007 Lukas Ertl
 *  All rights reserved.
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

#include <sys/types.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum.h>
#include <geom/vinum/geom_vinum_share.h>

void	gv_lvi(struct gv_volume *, struct sbuf *, int);
void	gv_lpi(struct gv_plex *, struct sbuf *, int);
void	gv_lsi(struct gv_sd *, struct sbuf *, int);
void	gv_ldi(struct gv_drive *, struct sbuf *, int);

void
gv_list(struct g_geom *gp, struct gctl_req *req)
{
	struct gv_softc *sc;
	struct gv_drive *d;
	struct gv_plex *p;
	struct gv_sd *s;
	struct gv_volume *v;
	struct sbuf *sb;
	int *argc, i, *flags, type;
	char *arg, buf[20], *cmd;

	argc = gctl_get_paraml(req, "argc", sizeof(*argc));

	if (argc == NULL) {
		gctl_error(req, "no arguments given");
		return;
	}

	flags = gctl_get_paraml(req, "flags", sizeof(*flags));
	if (flags == NULL) {
		gctl_error(req, "no flags given");
		return;
	}

	sc = gp->softc;

	sb = sbuf_new(NULL, NULL, GV_CFG_LEN, SBUF_FIXEDLEN);

	/* Figure out which command was given. */
	cmd = gctl_get_param(req, "cmd", NULL);
	if (cmd == NULL) {
		gctl_error(req, "no command given");
		return;
	}

	/* List specific objects or everything. */
	if (!strcmp(cmd, "list") || !strcmp(cmd, "l")) {
		if (*argc) {
			for (i = 0; i < *argc; i++) {
				snprintf(buf, sizeof(buf), "argv%d", i);
				arg = gctl_get_param(req, buf, NULL);
				if (arg == NULL)
					continue;
				type = gv_object_type(sc, arg);
				switch (type) {
				case GV_TYPE_VOL:
					v = gv_find_vol(sc, arg);
					gv_lvi(v, sb, *flags);
					break;
				case GV_TYPE_PLEX:
					p = gv_find_plex(sc, arg);
					gv_lpi(p, sb, *flags);
					break;
				case GV_TYPE_SD:
					s = gv_find_sd(sc, arg);
					gv_lsi(s, sb, *flags);
					break;
				case GV_TYPE_DRIVE:
					d = gv_find_drive(sc, arg);
					gv_ldi(d, sb, *flags);
					break;
				default:
					gctl_error(req, "unknown object '%s'",
					    arg);
					break;
				}
			}
		} else {
			gv_ld(gp, req, sb);
			sbuf_printf(sb, "\n");
			gv_lv(gp, req, sb);
			sbuf_printf(sb, "\n");
			gv_lp(gp, req, sb);
			sbuf_printf(sb, "\n");
			gv_ls(gp, req, sb);
		}

	/* List drives. */
	} else if (!strcmp(cmd, "ld")) {
		if (*argc) {
			for (i = 0; i < *argc; i++) {
				snprintf(buf, sizeof(buf), "argv%d", i);
				arg = gctl_get_param(req, buf, NULL);
				if (arg == NULL)
					continue;
				type = gv_object_type(sc, arg);
				if (type != GV_TYPE_DRIVE) {
					gctl_error(req, "'%s' is not a drive",
					    arg);
					continue;
				} else {
					d = gv_find_drive(sc, arg);
					gv_ldi(d, sb, *flags);
				}
			}
		} else
			gv_ld(gp, req, sb);

	/* List volumes. */
	} else if (!strcmp(cmd, "lv")) {
		if (*argc) {
			for (i = 0; i < *argc; i++) {
				snprintf(buf, sizeof(buf), "argv%d", i);
				arg = gctl_get_param(req, buf, NULL);
				if (arg == NULL)
					continue;
				type = gv_object_type(sc, arg);
				if (type != GV_TYPE_VOL) {
					gctl_error(req, "'%s' is not a volume",
					    arg);
					continue;
				} else {
					v = gv_find_vol(sc, arg);
					gv_lvi(v, sb, *flags);
				}
			}
		} else
			gv_lv(gp, req, sb);

	/* List plexes. */
	} else if (!strcmp(cmd, "lp")) {
		if (*argc) {
			for (i = 0; i < *argc; i++) {
				snprintf(buf, sizeof(buf), "argv%d", i);
				arg = gctl_get_param(req, buf, NULL);
				if (arg == NULL)
					continue;
				type = gv_object_type(sc, arg);
				if (type != GV_TYPE_PLEX) {
					gctl_error(req, "'%s' is not a plex",
					    arg);
					continue;
				} else {
					p = gv_find_plex(sc, arg);
					gv_lpi(p, sb, *flags);
				}
			}
		} else
			gv_lp(gp, req, sb);

	/* List subdisks. */
	} else if (!strcmp(cmd, "ls")) {
		if (*argc) {
			for (i = 0; i < *argc; i++) {
				snprintf(buf, sizeof(buf), "argv%d", i);
				arg = gctl_get_param(req, buf, NULL);
				if (arg == NULL)
					continue;
				type = gv_object_type(sc, arg);
				if (type != GV_TYPE_SD) {
					gctl_error(req, "'%s' is not a subdisk",
					    arg);
					continue;
				} else {
					s = gv_find_sd(sc, arg);
					gv_lsi(s, sb, *flags);
				}
			}
		} else
			gv_ls(gp, req, sb);

	} else
		gctl_error(req, "unknown command '%s'", cmd);

	sbuf_finish(sb);
	gctl_set_param(req, "config", sbuf_data(sb), sbuf_len(sb) + 1);
	sbuf_delete(sb);
}

/* List one or more volumes. */
void
gv_lv(struct g_geom *gp, struct gctl_req *req, struct sbuf *sb)
{
	struct gv_softc *sc;
	struct gv_volume *v;
	int i, *flags;

	sc = gp->softc;
	i = 0;

	LIST_FOREACH(v, &sc->volumes, volume)
		i++;
	
	sbuf_printf(sb, "%d volume%s:\n", i, i == 1 ? "" : "s");

	if (i) {
		flags = gctl_get_paraml(req, "flags", sizeof(*flags));
		LIST_FOREACH(v, &sc->volumes, volume)
			gv_lvi(v, sb, *flags);
	}
}

/* List a single volume. */
void
gv_lvi(struct gv_volume *v, struct sbuf *sb, int flags)
{
	struct gv_plex *p;
	int i;

	if (flags & GV_FLAG_V) {
		sbuf_printf(sb, "Volume %s:\tSize: %jd bytes (%jd MB)\n",
		    v->name, (intmax_t)v->size, (intmax_t)v->size / MEGABYTE);
		sbuf_printf(sb, "\t\tState: %s\n", gv_volstate(v->state));
	} else {
		sbuf_printf(sb, "V %-21s State: %s\tPlexes: %7d\tSize: %s\n",
		    v->name, gv_volstate(v->state), v->plexcount,
		    gv_roughlength(v->size, 0));
	}

	if (flags & GV_FLAG_VV) {
		i = 0;
		LIST_FOREACH(p, &v->plexes, in_volume) {
			sbuf_printf(sb, "\t\tPlex %2d:\t%s\t(%s), %s\n", i,
			    p->name, gv_plexstate(p->state),
			    gv_roughlength(p->size, 0));
			i++;
		}
	}

	if (flags & GV_FLAG_R) {
		LIST_FOREACH(p, &v->plexes, in_volume)
			gv_lpi(p, sb, flags);
	}
}

/* List one or more plexes. */
void
gv_lp(struct g_geom *gp, struct gctl_req *req, struct sbuf *sb)
{
	struct gv_softc *sc;
	struct gv_plex *p;
	int i, *flags;

	sc = gp->softc;
	i = 0;

	LIST_FOREACH(p, &sc->plexes, plex)
		i++;

	sbuf_printf(sb, "%d plex%s:\n", i, i == 1 ? "" : "es");

	if (i) {
		flags = gctl_get_paraml(req, "flags", sizeof(*flags));
		LIST_FOREACH(p, &sc->plexes, plex)
			gv_lpi(p, sb, *flags);
	}
}

/* List a single plex. */
void
gv_lpi(struct gv_plex *p, struct sbuf *sb, int flags)
{
	struct gv_sd *s;
	int i;

	if (flags & GV_FLAG_V) {
		sbuf_printf(sb, "Plex %s:\tSize:\t%9jd bytes (%jd MB)\n",
		    p->name, (intmax_t)p->size, (intmax_t)p->size / MEGABYTE);
		sbuf_printf(sb, "\t\tSubdisks: %8d\n", p->sdcount);
		sbuf_printf(sb, "\t\tState: %s\n", gv_plexstate(p->state));
		if ((p->flags & GV_PLEX_SYNCING) ||
		    (p->flags & GV_PLEX_GROWING) ||
		    (p->flags & GV_PLEX_REBUILDING)) {
			sbuf_printf(sb, "\t\tSynced: ");
			sbuf_printf(sb, "%16jd bytes (%d%%)\n",
			    (intmax_t)p->synced,
			    (p->size > 0) ? (int)((p->synced * 100) / p->size) :
			    0);
		}
		sbuf_printf(sb, "\t\tOrganization: %s", gv_plexorg(p->org));
		if (gv_is_striped(p)) {
			sbuf_printf(sb, "\tStripe size: %s\n",
			    gv_roughlength(p->stripesize, 1));
		}
		sbuf_printf(sb, "\t\tFlags: %d\n", p->flags);
		if (p->vol_sc != NULL) {
			sbuf_printf(sb, "\t\tPart of volume %s\n", p->volume);
		}
	} else {
		sbuf_printf(sb, "P %-18s %2s State: ", p->name,
		gv_plexorg_short(p->org));
		if ((p->flags & GV_PLEX_SYNCING) ||
		    (p->flags & GV_PLEX_GROWING) ||
		    (p->flags & GV_PLEX_REBUILDING)) {
			sbuf_printf(sb, "S %d%%\t", (int)((p->synced * 100) /
			    p->size));
		} else {
			sbuf_printf(sb, "%s\t", gv_plexstate(p->state));
		}
		sbuf_printf(sb, "Subdisks: %5d\tSize: %s\n", p->sdcount,
		    gv_roughlength(p->size, 0));
	}

	if (flags & GV_FLAG_VV) {
		i = 0;
		LIST_FOREACH(s, &p->subdisks, in_plex) {
			sbuf_printf(sb, "\t\tSubdisk %d:\t%s\n", i, s->name);
			sbuf_printf(sb, "\t\t  state: %s\tsize %11jd "
			    "(%jd MB)\n", gv_sdstate(s->state),
			    (intmax_t)s->size, (intmax_t)s->size / MEGABYTE);
			if (p->org == GV_PLEX_CONCAT) {
				sbuf_printf(sb, "\t\t\toffset %9jd (0x%jx)\n",
				    (intmax_t)s->plex_offset,
				    (intmax_t)s->plex_offset);
			}
			i++;
		}
	}

	if (flags & GV_FLAG_R) {
		LIST_FOREACH(s, &p->subdisks, in_plex)
			gv_lsi(s, sb, flags);
	}
}

/* List one or more subdisks. */
void
gv_ls(struct g_geom *gp, struct gctl_req *req, struct sbuf *sb)
{
	struct gv_softc *sc;
	struct gv_sd *s;
	int i, *flags;

	sc = gp->softc;
	i = 0;

	LIST_FOREACH(s, &sc->subdisks, sd)
		i++;
	
	sbuf_printf(sb, "%d subdisk%s:\n", i, i == 1 ? "" : "s");

	if (i) {
		flags = gctl_get_paraml(req, "flags", sizeof(*flags));
		LIST_FOREACH(s, &sc->subdisks, sd)
			gv_lsi(s, sb, *flags);
	}
}

/* List a single subdisk. */
void
gv_lsi(struct gv_sd *s, struct sbuf *sb, int flags)
{
	if (flags & GV_FLAG_V) {
		sbuf_printf(sb, "Subdisk %s:\n", s->name);
		sbuf_printf(sb, "\t\tSize: %16jd bytes (%jd MB)\n",
		    (intmax_t)s->size, (intmax_t)s->size / MEGABYTE);
		sbuf_printf(sb, "\t\tState: %s\n", gv_sdstate(s->state));

		if (s->state == GV_SD_INITIALIZING ||
		    s->state == GV_SD_REVIVING) {
			if (s->state == GV_SD_INITIALIZING)
				sbuf_printf(sb, "\t\tInitialized: ");
			else
				sbuf_printf(sb, "\t\tRevived: ");
				
			sbuf_printf(sb, "%16jd bytes (%d%%)\n",
			    (intmax_t)s->initialized,
			    (int)((s->initialized * 100) / s->size));
		}

		if (s->plex_sc != NULL) {
			sbuf_printf(sb, "\t\tPlex %s at offset %jd (%s)\n",
			    s->plex, (intmax_t)s->plex_offset,
			    gv_roughlength(s->plex_offset, 1));
		}

		sbuf_printf(sb, "\t\tDrive %s (%s) at offset %jd (%s)\n",
		    s->drive,
		    s->drive_sc == NULL ? "*missing*" : s->drive_sc->name,
		    (intmax_t)s->drive_offset,
		    gv_roughlength(s->drive_offset, 1));
		sbuf_printf(sb, "\t\tFlags: %d\n", s->flags);
	} else {
		sbuf_printf(sb, "S %-21s State: ", s->name);
		if (s->state == GV_SD_INITIALIZING ||
		    s->state == GV_SD_REVIVING) {
			if (s->state == GV_SD_INITIALIZING)
				sbuf_printf(sb, "I ");
			else
				sbuf_printf(sb, "R ");
			sbuf_printf(sb, "%d%%\t",
			    (int)((s->initialized * 100) / s->size));
		} else {
			sbuf_printf(sb, "%s\t", gv_sdstate(s->state));
		}
		sbuf_printf(sb, "D: %-12s Size: %s\n", s->drive,
		    gv_roughlength(s->size, 0));
	}
}

/* List one or more drives. */
void
gv_ld(struct g_geom *gp, struct gctl_req *req, struct sbuf *sb)
{
	struct gv_softc *sc;
	struct gv_drive *d;
	int i, *flags;

	sc = gp->softc;
	i = 0;

	LIST_FOREACH(d, &sc->drives, drive)
		i++;
	
	sbuf_printf(sb, "%d drive%s:\n", i, i == 1 ? "" : "s");

	if (i) {
		flags = gctl_get_paraml(req, "flags", sizeof(*flags));
		LIST_FOREACH(d, &sc->drives, drive)
			gv_ldi(d, sb, *flags);
	}
}

/* List a single drive. */
void
gv_ldi(struct gv_drive *d, struct sbuf *sb, int flags)
{
	struct gv_freelist *fl;
	struct gv_sd *s;

	/* Verbose listing. */
	if (flags & GV_FLAG_V) {
		sbuf_printf(sb, "Drive %s:\tDevice %s\n", d->name, d->device);
		sbuf_printf(sb, "\t\tSize: %16jd bytes (%jd MB)\n",
		    (intmax_t)d->size, (intmax_t)d->size / MEGABYTE);
		sbuf_printf(sb, "\t\tUsed: %16jd bytes (%jd MB)\n",
		    (intmax_t)d->size - d->avail,
		    (intmax_t)(d->size - d->avail) / MEGABYTE);
		sbuf_printf(sb, "\t\tAvailable: %11jd bytes (%jd MB)\n",
		    (intmax_t)d->avail, (intmax_t)d->avail / MEGABYTE);
		sbuf_printf(sb, "\t\tState: %s\n", gv_drivestate(d->state));
		sbuf_printf(sb, "\t\tFlags: %d\n", d->flags);

		/* Be very verbose. */
		if (flags & GV_FLAG_VV) {
			sbuf_printf(sb, "\t\tFree list contains %d entries:\n",
			    d->freelist_entries);
			sbuf_printf(sb, "\t\t   Offset\t     Size\n");
			LIST_FOREACH(fl, &d->freelist, freelist)
				sbuf_printf(sb, "\t\t%9jd\t%9jd\n",
				    (intmax_t)fl->offset, (intmax_t)fl->size);
		}
	} else {
		sbuf_printf(sb, "D %-21s State: %s\t/dev/%s\tA: %jd/%jd MB "
		    "(%d%%)\n", d->name, gv_drivestate(d->state), d->device,
		    (intmax_t)d->avail / MEGABYTE, (intmax_t)d->size / MEGABYTE,
		    d->size > 0 ? (int)((d->avail * 100) / d->size) : 0);
	}

	/* Recursive listing. */
	if (flags & GV_FLAG_R) {
		LIST_FOREACH(s, &d->subdisks, from_drive)
			gv_lsi(s, sb, flags);
	}
}
