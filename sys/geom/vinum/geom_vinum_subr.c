/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2004, 2007 Lukas Ertl
 * Copyright (c) 2007, 2009 Ulf Lilleengen
 * Copyright (c) 1997, 1998, 1999
 *      Nan Yang Computer Services Limited.  All rights reserved.
 *
 *  Parts written by Greg Lehey
 *
 *  This software is distributed under the so-called ``Berkeley
 *  License'':
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
 *      This product includes software developed by Nan Yang Computer
 *      Services Limited.
 * 4. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even if
 * advised of the possibility of such damage.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>
#include <sys/systm.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum.h>
#include <geom/vinum/geom_vinum_share.h>

int	gv_drive_is_newer(struct gv_softc *, struct gv_drive *);
static off_t gv_plex_smallest_sd(struct gv_plex *);

void
gv_parse_config(struct gv_softc *sc, char *buf, struct gv_drive *d)
{
	char *aptr, *bptr, *cptr;
	struct gv_volume *v, *v2;
	struct gv_plex *p, *p2;
	struct gv_sd *s, *s2;
	int error, is_newer, tokens;
	char *token[GV_MAXARGS];

	is_newer = gv_drive_is_newer(sc, d);

	/* Until the end of the string *buf. */
	for (aptr = buf; *aptr != '\0'; aptr = bptr) {
		bptr = aptr;
		cptr = aptr;

		/* Separate input lines. */
		while (*bptr != '\n')
			bptr++;
		*bptr = '\0';
		bptr++;

		tokens = gv_tokenize(cptr, token, GV_MAXARGS);

		if (tokens <= 0)
			continue;

		if (!strcmp(token[0], "volume")) {
			v = gv_new_volume(tokens, token);
			if (v == NULL) {
				G_VINUM_DEBUG(0, "config parse failed volume");
				break;
			}

			v2 = gv_find_vol(sc, v->name);
			if (v2 != NULL) {
				if (is_newer) {
					v2->state = v->state;
					G_VINUM_DEBUG(2, "newer volume found!");
				}
				g_free(v);
				continue;
			}

			gv_create_volume(sc, v);

		} else if (!strcmp(token[0], "plex")) {
			p = gv_new_plex(tokens, token);
			if (p == NULL) {
				G_VINUM_DEBUG(0, "config parse failed plex");
				break;
			}

			p2 = gv_find_plex(sc, p->name);
			if (p2 != NULL) {
				/* XXX */
				if (is_newer) {
					p2->state = p->state;
					G_VINUM_DEBUG(2, "newer plex found!");
				}
				g_free(p);
				continue;
			}

			error = gv_create_plex(sc, p);
			if (error)
				continue;
			/*
			 * These flags were set in gv_create_plex() and are not
			 * needed here (on-disk config parsing).
			 */
			p->flags &= ~GV_PLEX_ADDED;

		} else if (!strcmp(token[0], "sd")) {
			s = gv_new_sd(tokens, token);

			if (s == NULL) {
				G_VINUM_DEBUG(0, "config parse failed subdisk");
				break;
			}

			s2 = gv_find_sd(sc, s->name);
			if (s2 != NULL) {
				/* XXX */
				if (is_newer) {
					s2->state = s->state;
					G_VINUM_DEBUG(2, "newer subdisk found!");
				}
				g_free(s);
				continue;
			}

			/*
			 * Signal that this subdisk was tasted, and could
			 * possibly reference a drive that isn't in our config
			 * yet.
			 */
			s->flags |= GV_SD_TASTED;

			if (s->state == GV_SD_UP)
				s->flags |= GV_SD_CANGOUP;

			error = gv_create_sd(sc, s);
			if (error)
				continue;

			/*
			 * This flag was set in gv_create_sd() and is not
			 * needed here (on-disk config parsing).
			 */
			s->flags &= ~GV_SD_NEWBORN;
			s->flags &= ~GV_SD_GROW;
		}
	}
}

/*
 * Format the vinum configuration properly.  If ondisk is non-zero then the
 * configuration is intended to be written to disk later.
 */
void
gv_format_config(struct gv_softc *sc, struct sbuf *sb, int ondisk, char *prefix)
{
	struct gv_drive *d;
	struct gv_sd *s;
	struct gv_plex *p;
	struct gv_volume *v;

	/*
	 * We don't need the drive configuration if we're not writing the
	 * config to disk.
	 */
	if (!ondisk) {
		LIST_FOREACH(d, &sc->drives, drive) {
			sbuf_printf(sb, "%sdrive %s device /dev/%s\n", prefix,
			    d->name, d->device);
		}
	}

	LIST_FOREACH(v, &sc->volumes, volume) {
		if (!ondisk)
			sbuf_printf(sb, "%s", prefix);
		sbuf_printf(sb, "volume %s", v->name);
		if (ondisk)
			sbuf_printf(sb, " state %s", gv_volstate(v->state));
		sbuf_printf(sb, "\n");
	}

	LIST_FOREACH(p, &sc->plexes, plex) {
		if (!ondisk)
			sbuf_printf(sb, "%s", prefix);
		sbuf_printf(sb, "plex name %s org %s ", p->name,
		    gv_plexorg(p->org));
		if (gv_is_striped(p))
			sbuf_printf(sb, "%ds ", p->stripesize / 512);
		if (p->vol_sc != NULL)
			sbuf_printf(sb, "vol %s", p->volume);
		if (ondisk)
			sbuf_printf(sb, " state %s", gv_plexstate(p->state));
		sbuf_printf(sb, "\n");
	}

	LIST_FOREACH(s, &sc->subdisks, sd) {
		if (!ondisk)
			sbuf_printf(sb, "%s", prefix);
		sbuf_printf(sb, "sd name %s drive %s len %jds driveoffset "
		    "%jds", s->name, s->drive, s->size / 512,
		    s->drive_offset / 512);
		if (s->plex_sc != NULL) {
			sbuf_printf(sb, " plex %s plexoffset %jds", s->plex,
			    s->plex_offset / 512);
		}
		if (ondisk)
			sbuf_printf(sb, " state %s", gv_sdstate(s->state));
		sbuf_printf(sb, "\n");
	}
}

static off_t
gv_plex_smallest_sd(struct gv_plex *p)
{
	struct gv_sd *s;
	off_t smallest;

	KASSERT(p != NULL, ("gv_plex_smallest_sd: NULL p"));

	s = LIST_FIRST(&p->subdisks);
	if (s == NULL)
		return (-1);
	smallest = s->size;
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		if (s->size < smallest)
			smallest = s->size;
	}
	return (smallest);
}

/* Walk over plexes in a volume and count how many are down. */
int
gv_plexdown(struct gv_volume *v)
{
	int plexdown;
	struct gv_plex *p;

	KASSERT(v != NULL, ("gv_plexdown: NULL v"));

	plexdown = 0;

	LIST_FOREACH(p, &v->plexes, plex) {
		if (p->state == GV_PLEX_DOWN)
			plexdown++;
	}
	return (plexdown);
}

int
gv_sd_to_plex(struct gv_sd *s, struct gv_plex *p)
{
	struct gv_sd *s2;
	off_t psizeorig, remainder, smallest;

	/* If this subdisk was already given to this plex, do nothing. */
	if (s->plex_sc == p)
		return (0);

	/* Check correct size of this subdisk. */
	s2 = LIST_FIRST(&p->subdisks);
	/* Adjust the subdisk-size if necessary. */
	if (s2 != NULL && gv_is_striped(p)) {
		/* First adjust to the stripesize. */
		remainder = s->size % p->stripesize;

		if (remainder) {
			G_VINUM_DEBUG(1, "size of sd %s is not a "
			    "multiple of plex stripesize, taking off "
			    "%jd bytes", s->name,
			    (intmax_t)remainder);
			gv_adjust_freespace(s, remainder);
		}

		smallest = gv_plex_smallest_sd(p);
		/* Then take off extra if other subdisks are smaller. */
		remainder = s->size - smallest;

		/*
		 * Don't allow a remainder below zero for running plexes, it's too
		 * painful, and if someone were to accidentally do this, the
		 * resulting array might be smaller than the original... not god 
		 */
		if (remainder < 0) {
			if (!(p->flags & GV_PLEX_NEWBORN)) {
				G_VINUM_DEBUG(0, "sd %s too small for plex %s!",
				    s->name, p->name);
				return (GV_ERR_BADSIZE);
			}
			/* Adjust other subdisks. */
			LIST_FOREACH(s2, &p->subdisks, in_plex) {
				G_VINUM_DEBUG(1, "size of sd %s is to big, "
				    "taking off %jd bytes", s->name,
				    (intmax_t)remainder);
				gv_adjust_freespace(s2, (remainder * -1));
			}
		} else if (remainder > 0) {
			G_VINUM_DEBUG(1, "size of sd %s is to big, "
			    "taking off %jd bytes", s->name,
			    (intmax_t)remainder);
			gv_adjust_freespace(s, remainder);
		}
	}

	/* Find the correct plex offset for this subdisk, if needed. */
	if (s->plex_offset == -1) {
		/* 
		 * First set it to 0 to catch the case where we had a detached
		 * subdisk that didn't get any good offset.
		 */
		s->plex_offset = 0;
		if (p->sdcount) {
			LIST_FOREACH(s2, &p->subdisks, in_plex) {
				if (gv_is_striped(p))
					s->plex_offset = p->sdcount *
					    p->stripesize;
				else
					s->plex_offset = s2->plex_offset +
					    s2->size;
			}
		}
	}

	/* There are no subdisks for this plex yet, just insert it. */
	if (LIST_EMPTY(&p->subdisks)) {
		LIST_INSERT_HEAD(&p->subdisks, s, in_plex);

	/* Insert in correct order, depending on plex_offset. */
	} else {
		LIST_FOREACH(s2, &p->subdisks, in_plex) {
			if (s->plex_offset < s2->plex_offset) {
				LIST_INSERT_BEFORE(s2, s, in_plex);
				break;
			} else if (LIST_NEXT(s2, in_plex) == NULL) {
				LIST_INSERT_AFTER(s2, s, in_plex);
				break;
			}
		}
	}

	s->plex_sc = p;
        /* Adjust the size of our plex. We check if the plex misses a subdisk,
	 * so we don't make the plex smaller than it actually should be.
	 */
	psizeorig = p->size;
	p->size = gv_plex_size(p);
	/* Make sure the size is not changed. */
	if (p->sddetached > 0) {
		if (p->size < psizeorig) {
			p->size = psizeorig;
			/* We make sure wee need another subdisk. */
			if (p->sddetached == 1)
				p->sddetached++;
		}
		p->sddetached--;
	} else {
		if ((p->org == GV_PLEX_RAID5 ||
		    p->org == GV_PLEX_STRIPED) &&
		    !(p->flags & GV_PLEX_NEWBORN) && 
		    p->state == GV_PLEX_UP) {
			s->flags |= GV_SD_GROW;
		}
		p->sdcount++;
	}

	return (0);
}

void
gv_update_vol_size(struct gv_volume *v, off_t size)
{
	if (v == NULL)
		return;
	if (v->provider != NULL) {
		g_topology_lock();
		v->provider->mediasize = size;
		g_topology_unlock();
	}
	v->size = size;
}

/* Return how many subdisks that constitute the original plex. */
int
gv_sdcount(struct gv_plex *p, int growing)
{
	struct gv_sd *s;
	int sdcount;

	sdcount = p->sdcount;
	if (growing) {
		LIST_FOREACH(s, &p->subdisks, in_plex) {
			if (s->flags & GV_SD_GROW)
				sdcount--;
		}
	}

	return (sdcount);
}

/* Calculates the plex size. */
off_t
gv_plex_size(struct gv_plex *p)
{
	struct gv_sd *s;
	off_t size;
	int sdcount;

	KASSERT(p != NULL, ("gv_plex_size: NULL p"));

	/* Adjust the size of our plex. */
	size = 0;
	sdcount = gv_sdcount(p, 1);
	switch (p->org) {
	case GV_PLEX_CONCAT:
		LIST_FOREACH(s, &p->subdisks, in_plex)
			size += s->size;
		break;
	case GV_PLEX_STRIPED:
		s = LIST_FIRST(&p->subdisks);
		size = ((s != NULL) ? (sdcount * s->size) : 0);
		break;
	case GV_PLEX_RAID5:
		s = LIST_FIRST(&p->subdisks);
		size = ((s != NULL) ? ((sdcount - 1) * s->size) : 0);
		break;
	}

	return (size);
}

/* Returns the size of a volume. */
off_t
gv_vol_size(struct gv_volume *v)
{
	struct gv_plex *p;
	off_t minplexsize;

	KASSERT(v != NULL, ("gv_vol_size: NULL v"));

	p = LIST_FIRST(&v->plexes);
	if (p == NULL)
		return (0);

	minplexsize = p->size;
	LIST_FOREACH(p, &v->plexes, in_volume) {
		if (p->size < minplexsize) {
			minplexsize = p->size;
		}
	}
	return (minplexsize);
}

void
gv_update_plex_config(struct gv_plex *p)
{
	struct gv_sd *s, *s2;
	off_t remainder;
	int required_sds, state;

	KASSERT(p != NULL, ("gv_update_plex_config: NULL p"));

	/* The plex was added to an already running volume. */
	if (p->flags & GV_PLEX_ADDED)
		gv_set_plex_state(p, GV_PLEX_DOWN, GV_SETSTATE_FORCE);

	switch (p->org) {
	case GV_PLEX_STRIPED:
		required_sds = 2;
		break;
	case GV_PLEX_RAID5:
		required_sds = 3;
		break;
	case GV_PLEX_CONCAT:
	default:
		required_sds = 0;
		break;
	}

	if (required_sds) {
		if (p->sdcount < required_sds) {
			gv_set_plex_state(p, GV_PLEX_DOWN, GV_SETSTATE_FORCE);
		}

		/*
		 * The subdisks in striped plexes must all have the same size.
		 */
		s = LIST_FIRST(&p->subdisks);
		LIST_FOREACH(s2, &p->subdisks, in_plex) {
			if (s->size != s2->size) {
				G_VINUM_DEBUG(0, "subdisk size mismatch %s"
				    "(%jd) <> %s (%jd)", s->name, s->size,
				    s2->name, s2->size);
				gv_set_plex_state(p, GV_PLEX_DOWN,
				    GV_SETSTATE_FORCE);
			}
		}

		LIST_FOREACH(s, &p->subdisks, in_plex) {
			/* Trim subdisk sizes to match the stripe size. */
			remainder = s->size % p->stripesize;
			if (remainder) {
				G_VINUM_DEBUG(1, "size of sd %s is not a "
				    "multiple of plex stripesize, taking off "
				    "%jd bytes", s->name, (intmax_t)remainder);
				gv_adjust_freespace(s, remainder);
			}
		}
	}

	p->size = gv_plex_size(p);
	if (p->sdcount == 0)
		gv_set_plex_state(p, GV_PLEX_DOWN, GV_SETSTATE_FORCE);
	else if (p->org == GV_PLEX_RAID5 && p->flags & GV_PLEX_NEWBORN) {
		LIST_FOREACH(s, &p->subdisks, in_plex)
			gv_set_sd_state(s, GV_SD_UP, GV_SETSTATE_FORCE);
		/* If added to a volume, we want the plex to be down. */
		state = (p->flags & GV_PLEX_ADDED) ? GV_PLEX_DOWN : GV_PLEX_UP;
		gv_set_plex_state(p, state, GV_SETSTATE_FORCE);
		p->flags &= ~GV_PLEX_ADDED;
	} else if (p->flags & GV_PLEX_ADDED) {
		LIST_FOREACH(s, &p->subdisks, in_plex)
			gv_set_sd_state(s, GV_SD_STALE, GV_SETSTATE_FORCE);
		gv_set_plex_state(p, GV_PLEX_DOWN, GV_SETSTATE_FORCE);
		p->flags &= ~GV_PLEX_ADDED;
	} else if (p->state == GV_PLEX_UP) {
		LIST_FOREACH(s, &p->subdisks, in_plex) {
			if (s->flags & GV_SD_GROW) {
				gv_set_plex_state(p, GV_PLEX_GROWABLE,
				    GV_SETSTATE_FORCE);
				break;
			}
		}
	}
	/* Our plex is grown up now. */
	p->flags &= ~GV_PLEX_NEWBORN;
}

/*
 * Give a subdisk to a drive, check and adjust several parameters, adjust
 * freelist.
 */
int
gv_sd_to_drive(struct gv_sd *s, struct gv_drive *d)
{
	struct gv_sd *s2;
	struct gv_freelist *fl, *fl2;
	off_t tmp;
	int i;

	fl2 = NULL;

	/* Shortcut for "referenced" drives. */
	if (d->flags & GV_DRIVE_REFERENCED) {
		s->drive_sc = d;
		return (0);
	}

	/* Check if this subdisk was already given to this drive. */
	if (s->drive_sc != NULL) {
		if (s->drive_sc == d) {
			if (!(s->flags & GV_SD_TASTED)) {
				return (0);
			}
		} else {
			G_VINUM_DEBUG(0, "error giving subdisk '%s' to '%s' "
			    "(already on '%s')", s->name, d->name,
			    s->drive_sc->name);
			return (GV_ERR_ISATTACHED);
		}
	}

	/* Preliminary checks. */
	if ((s->size > d->avail) || (d->freelist_entries == 0)) {
		G_VINUM_DEBUG(0, "not enough space on '%s' for '%s'", d->name,
		    s->name);
		return (GV_ERR_NOSPACE);
	}

	/* If no size was given for this subdisk, try to auto-size it... */
	if (s->size == -1) {
		/* Find the largest available slot. */
		LIST_FOREACH(fl, &d->freelist, freelist) {
			if (fl->size < s->size)
				continue;
			s->size = fl->size;
			s->drive_offset = fl->offset;
			fl2 = fl;
		}

		/* No good slot found? */
		if (s->size == -1) {
			G_VINUM_DEBUG(0, "unable to autosize '%s' on '%s'",
			    s->name, d->name);
			return (GV_ERR_BADSIZE);
		}

	/*
	 * ... or check if we have a free slot that's large enough for the
	 * given size.
	 */
	} else {
		i = 0;
		LIST_FOREACH(fl, &d->freelist, freelist) {
			if (fl->size < s->size)
				continue;
			/* Assign drive offset, if not given. */
			if (s->drive_offset == -1)
				s->drive_offset = fl->offset;
			fl2 = fl;
			i++;
			break;
		}

		/* Couldn't find a good free slot. */
		if (i == 0) {
			G_VINUM_DEBUG(0, "free slots to small for '%s' on '%s'",
			    s->name, d->name);
			return (GV_ERR_NOSPACE);
		}
	}

	/* No drive offset given, try to calculate it. */
	if (s->drive_offset == -1) {

		/* Add offsets and sizes from other subdisks on this drive. */
		LIST_FOREACH(s2, &d->subdisks, from_drive) {
			s->drive_offset = s2->drive_offset + s2->size;
		}

		/*
		 * If there are no other subdisks yet, then set the default
		 * offset to GV_DATA_START.
		 */
		if (s->drive_offset == -1)
			s->drive_offset = GV_DATA_START;

	/* Check if we have a free slot at the given drive offset. */
	} else {
		i = 0;
		LIST_FOREACH(fl, &d->freelist, freelist) {
			/* Yes, this subdisk fits. */
			if ((fl->offset <= s->drive_offset) &&
			    (fl->offset + fl->size >=
			    s->drive_offset + s->size)) {
				i++;
				fl2 = fl;
				break;
			}
		}

		/* Couldn't find a good free slot. */
		if (i == 0) {
			G_VINUM_DEBUG(0, "given drive_offset for '%s' won't fit "
			    "on '%s'", s->name, d->name);
			return (GV_ERR_NOSPACE);
		}
	}

	/*
	 * Now that all parameters are checked and set up, we can give the
	 * subdisk to the drive and adjust the freelist.
	 */

	/* First, adjust the freelist. */
	LIST_FOREACH(fl, &d->freelist, freelist) {
		/* Look for the free slot that we have found before. */
		if (fl != fl2)
			continue;

		/* The subdisk starts at the beginning of the free slot. */
		if (fl->offset == s->drive_offset) {
			fl->offset += s->size;
			fl->size -= s->size;

			/* The subdisk uses the whole slot, so remove it. */
			if (fl->size == 0) {
				d->freelist_entries--;
				LIST_REMOVE(fl, freelist);
			}
		/*
		 * The subdisk does not start at the beginning of the free
		 * slot.
		 */
		} else {
			tmp = fl->offset + fl->size;
			fl->size = s->drive_offset - fl->offset;

			/*
			 * The subdisk didn't use the complete rest of the free
			 * slot, so we need to split it.
			 */
			if (s->drive_offset + s->size != tmp) {
				fl2 = g_malloc(sizeof(*fl2), M_WAITOK | M_ZERO);
				fl2->offset = s->drive_offset + s->size;
				fl2->size = tmp - fl2->offset;
				LIST_INSERT_AFTER(fl, fl2, freelist);
				d->freelist_entries++;
			}
		}
		break;
	}

	/*
	 * This is the first subdisk on this drive, just insert it into the
	 * list.
	 */
	if (LIST_EMPTY(&d->subdisks)) {
		LIST_INSERT_HEAD(&d->subdisks, s, from_drive);

	/* There are other subdisks, so insert this one in correct order. */
	} else {
		LIST_FOREACH(s2, &d->subdisks, from_drive) {
			if (s->drive_offset < s2->drive_offset) {
				LIST_INSERT_BEFORE(s2, s, from_drive);
				break;
			} else if (LIST_NEXT(s2, from_drive) == NULL) {
				LIST_INSERT_AFTER(s2, s, from_drive);
				break;
			}
		}
	}

	d->sdcount++;
	d->avail -= s->size;

	s->flags &= ~GV_SD_TASTED;

	/* Link back from the subdisk to this drive. */
	s->drive_sc = d;

	return (0);
}

void
gv_free_sd(struct gv_sd *s)
{
	struct gv_drive *d;
	struct gv_freelist *fl, *fl2;

	KASSERT(s != NULL, ("gv_free_sd: NULL s"));

	d = s->drive_sc;
	if (d == NULL)
		return;

	/*
	 * First, find the free slot that's immediately before or after this
	 * subdisk.
	 */
	fl = NULL;
	LIST_FOREACH(fl, &d->freelist, freelist) {
		if (fl->offset == s->drive_offset + s->size)
			break;
		if (fl->offset + fl->size == s->drive_offset)
			break;
	}

	/* If there is no free slot behind this subdisk, so create one. */
	if (fl == NULL) {

		fl = g_malloc(sizeof(*fl), M_WAITOK | M_ZERO);
		fl->size = s->size;
		fl->offset = s->drive_offset;

		if (d->freelist_entries == 0) {
			LIST_INSERT_HEAD(&d->freelist, fl, freelist);
		} else {
			LIST_FOREACH(fl2, &d->freelist, freelist) {
				if (fl->offset < fl2->offset) {
					LIST_INSERT_BEFORE(fl2, fl, freelist);
					break;
				} else if (LIST_NEXT(fl2, freelist) == NULL) {
					LIST_INSERT_AFTER(fl2, fl, freelist);
					break;
				}
			}
		}

		d->freelist_entries++;

	/* Expand the free slot we just found. */
	} else {
		fl->size += s->size;
		if (fl->offset > s->drive_offset)
			fl->offset = s->drive_offset;
	}

	d->avail += s->size;
	d->sdcount--;
}

void
gv_adjust_freespace(struct gv_sd *s, off_t remainder)
{
	struct gv_drive *d;
	struct gv_freelist *fl, *fl2;

	KASSERT(s != NULL, ("gv_adjust_freespace: NULL s"));
	d = s->drive_sc;
	KASSERT(d != NULL, ("gv_adjust_freespace: NULL d"));

	/* First, find the free slot that's immediately after this subdisk. */
	fl = NULL;
	LIST_FOREACH(fl, &d->freelist, freelist) {
		if (fl->offset == s->drive_offset + s->size)
			break;
	}

	/* If there is no free slot behind this subdisk, so create one. */
	if (fl == NULL) {

		fl = g_malloc(sizeof(*fl), M_WAITOK | M_ZERO);
		fl->size = remainder;
		fl->offset = s->drive_offset + s->size - remainder;

		if (d->freelist_entries == 0) {
			LIST_INSERT_HEAD(&d->freelist, fl, freelist);
		} else {
			LIST_FOREACH(fl2, &d->freelist, freelist) {
				if (fl->offset < fl2->offset) {
					LIST_INSERT_BEFORE(fl2, fl, freelist);
					break;
				} else if (LIST_NEXT(fl2, freelist) == NULL) {
					LIST_INSERT_AFTER(fl2, fl, freelist);
					break;
				}
			}
		}

		d->freelist_entries++;

	/* Expand the free slot we just found. */
	} else {
		fl->offset -= remainder;
		fl->size += remainder;
	}

	s->size -= remainder;
	d->avail += remainder;
}

/* Check if the given plex is a striped one. */
int
gv_is_striped(struct gv_plex *p)
{
	KASSERT(p != NULL, ("gv_is_striped: NULL p"));
	switch(p->org) {
	case GV_PLEX_STRIPED:
	case GV_PLEX_RAID5:
		return (1);
	default:
		return (0);
	}
}

/* Find a volume by name. */
struct gv_volume *
gv_find_vol(struct gv_softc *sc, char *name)
{
	struct gv_volume *v;

	LIST_FOREACH(v, &sc->volumes, volume) {
		if (!strncmp(v->name, name, GV_MAXVOLNAME))
			return (v);
	}

	return (NULL);
}

/* Find a plex by name. */
struct gv_plex *
gv_find_plex(struct gv_softc *sc, char *name)
{
	struct gv_plex *p;

	LIST_FOREACH(p, &sc->plexes, plex) {
		if (!strncmp(p->name, name, GV_MAXPLEXNAME))
			return (p);
	}

	return (NULL);
}

/* Find a subdisk by name. */
struct gv_sd *
gv_find_sd(struct gv_softc *sc, char *name)
{
	struct gv_sd *s;

	LIST_FOREACH(s, &sc->subdisks, sd) {
		if (!strncmp(s->name, name, GV_MAXSDNAME))
			return (s);
	}

	return (NULL);
}

/* Find a drive by name. */
struct gv_drive *
gv_find_drive(struct gv_softc *sc, char *name)
{
	struct gv_drive *d;

	LIST_FOREACH(d, &sc->drives, drive) {
		if (!strncmp(d->name, name, GV_MAXDRIVENAME))
			return (d);
	}

	return (NULL);
}

/* Find a drive given a device. */
struct gv_drive *
gv_find_drive_device(struct gv_softc *sc, char *device)
{
	struct gv_drive *d;

	LIST_FOREACH(d, &sc->drives, drive) {
		if(!strcmp(d->device, device))
			return (d);
	}

	return (NULL);
}

/* Check if any consumer of the given geom is open. */
int
gv_consumer_is_open(struct g_consumer *cp)
{
	if (cp == NULL)
		return (0);

	if (cp->acr || cp->acw || cp->ace)
		return (1);

	return (0);
}

int
gv_provider_is_open(struct g_provider *pp)
{
	if (pp == NULL)
		return (0);

	if (pp->acr || pp->acw || pp->ace)
		return (1);

	return (0);
}

/*
 * Compare the modification dates of the drives.
 * Return 1 if a > b, 0 otherwise.
 */
int
gv_drive_is_newer(struct gv_softc *sc, struct gv_drive *d)
{
	struct gv_drive *d2;
	struct timeval *a, *b;

	KASSERT(!LIST_EMPTY(&sc->drives),
	    ("gv_is_drive_newer: empty drive list"));

	a = &d->hdr->label.last_update;
	LIST_FOREACH(d2, &sc->drives, drive) {
		if ((d == d2) || (d2->state != GV_DRIVE_UP) ||
		    (d2->hdr == NULL))
			continue;
		b = &d2->hdr->label.last_update;
		if (timevalcmp(a, b, >))
			return (1);
	}

	return (0);
}

/* Return the type of object identified by string 'name'. */
int
gv_object_type(struct gv_softc *sc, char *name)
{
	struct gv_drive *d;
	struct gv_plex *p;
	struct gv_sd *s;
	struct gv_volume *v;

	LIST_FOREACH(v, &sc->volumes, volume) {
		if (!strncmp(v->name, name, GV_MAXVOLNAME))
			return (GV_TYPE_VOL);
	}

	LIST_FOREACH(p, &sc->plexes, plex) {
		if (!strncmp(p->name, name, GV_MAXPLEXNAME))
			return (GV_TYPE_PLEX);
	}

	LIST_FOREACH(s, &sc->subdisks, sd) {
		if (!strncmp(s->name, name, GV_MAXSDNAME))
			return (GV_TYPE_SD);
	}

	LIST_FOREACH(d, &sc->drives, drive) {
		if (!strncmp(d->name, name, GV_MAXDRIVENAME))
			return (GV_TYPE_DRIVE);
	}

	return (GV_ERR_NOTFOUND);
}

void
gv_setup_objects(struct gv_softc *sc)
{
	struct g_provider *pp;
	struct gv_volume *v;
	struct gv_plex *p;
	struct gv_sd *s;
	struct gv_drive *d;

	LIST_FOREACH(s, &sc->subdisks, sd) {
		d = gv_find_drive(sc, s->drive);
		if (d != NULL)
			gv_sd_to_drive(s, d);
		p = gv_find_plex(sc, s->plex);
		if (p != NULL)
			gv_sd_to_plex(s, p);
		gv_update_sd_state(s);
	}

	LIST_FOREACH(p, &sc->plexes, plex) {
		gv_update_plex_config(p);
		v = gv_find_vol(sc, p->volume);
		if (v != NULL && p->vol_sc != v) {
			p->vol_sc = v;
			v->plexcount++;
			LIST_INSERT_HEAD(&v->plexes, p, in_volume);
		}
		gv_update_plex_config(p);
	}

	LIST_FOREACH(v, &sc->volumes, volume) {
		v->size = gv_vol_size(v);
		if (v->provider == NULL) {
			g_topology_lock();
			pp = g_new_providerf(sc->geom, "gvinum/%s", v->name);
			pp->mediasize = v->size;
			pp->sectorsize = 512;    /* XXX */
			g_error_provider(pp, 0);
			v->provider = pp;
			pp->private = v;
			g_topology_unlock();
		} else if (v->provider->mediasize != v->size) {
			g_topology_lock();
			v->provider->mediasize = v->size;
			g_topology_unlock();
		}
		v->flags &= ~GV_VOL_NEWBORN;
		gv_update_vol_state(v);
	}
}

void
gv_cleanup(struct gv_softc *sc)
{
	struct gv_volume *v, *v2;
	struct gv_plex *p, *p2;
	struct gv_sd *s, *s2;
	struct gv_drive *d, *d2;
	struct gv_freelist *fl, *fl2;

	mtx_lock(&sc->config_mtx);
	LIST_FOREACH_SAFE(v, &sc->volumes, volume, v2) {
		LIST_REMOVE(v, volume);
		g_free(v->wqueue);
		g_free(v);
	}
	LIST_FOREACH_SAFE(p, &sc->plexes, plex, p2) {
		LIST_REMOVE(p, plex);
		g_free(p->bqueue);
		g_free(p->rqueue);
		g_free(p->wqueue);
		g_free(p);
	}
	LIST_FOREACH_SAFE(s, &sc->subdisks, sd, s2) {
		LIST_REMOVE(s, sd);
		g_free(s);
	}
	LIST_FOREACH_SAFE(d, &sc->drives, drive, d2) {
		LIST_FOREACH_SAFE(fl, &d->freelist, freelist, fl2) {
			LIST_REMOVE(fl, freelist);
			g_free(fl);
		}
		LIST_REMOVE(d, drive);
		g_free(d->hdr);
		g_free(d);
	}
	mtx_destroy(&sc->config_mtx);
}

/* General 'attach' routine. */
int
gv_attach_plex(struct gv_plex *p, struct gv_volume *v, int rename)
{
	struct gv_sd *s;
	struct gv_softc *sc;

	g_topology_assert();

	sc = p->vinumconf;
	KASSERT(sc != NULL, ("NULL sc"));

	if (p->vol_sc != NULL) {
		G_VINUM_DEBUG(1, "unable to attach %s: already attached to %s",
		    p->name, p->volume);
		return (GV_ERR_ISATTACHED);
	}

	/* Stale all subdisks of this plex. */
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		if (s->state != GV_SD_STALE)
			gv_set_sd_state(s, GV_SD_STALE, GV_SETSTATE_FORCE);
	}
	/* Attach to volume. Make sure volume is not up and running. */
	if (gv_provider_is_open(v->provider)) {
		G_VINUM_DEBUG(1, "unable to attach %s: volume %s is busy",
		    p->name, v->name);
		return (GV_ERR_ISBUSY);
	}
	p->vol_sc = v;
	strlcpy(p->volume, v->name, sizeof(p->volume));
	v->plexcount++;
	if (rename) {
		snprintf(p->name, sizeof(p->name), "%s.p%d", v->name,
		    v->plexcount);
	}
	LIST_INSERT_HEAD(&v->plexes, p, in_volume);

	/* Get plex up again. */
	gv_update_vol_size(v, gv_vol_size(v));
	gv_set_plex_state(p, GV_PLEX_UP, 0);
	gv_save_config(p->vinumconf);
	return (0);
}

int
gv_attach_sd(struct gv_sd *s, struct gv_plex *p, off_t offset, int rename)
{
	struct gv_sd *s2;
	int error, sdcount;

	g_topology_assert();

	/* If subdisk is attached, don't do it. */
	if (s->plex_sc != NULL) {
		G_VINUM_DEBUG(1, "unable to attach %s: already attached to %s",
		    s->name, s->plex);
		return (GV_ERR_ISATTACHED);
	}

	gv_set_sd_state(s, GV_SD_STALE, GV_SETSTATE_FORCE);
	/* First check that this subdisk has a correct offset. If none other
	 * starts at the same, and it's correct module stripesize, it is */
	if (offset != -1 && offset % p->stripesize != 0)
		return (GV_ERR_BADOFFSET);
	LIST_FOREACH(s2, &p->subdisks, in_plex) {
		if (s2->plex_offset == offset)
			return (GV_ERR_BADOFFSET);
	}

	/* Attach the subdisk to the plex at given offset. */
	s->plex_offset = offset;
	strlcpy(s->plex, p->name, sizeof(s->plex));

	sdcount = p->sdcount;
	error = gv_sd_to_plex(s, p);
	if (error)
		return (error);
	gv_update_plex_config(p);

	if (rename) {
		snprintf(s->name, sizeof(s->name), "%s.s%d", s->plex,
		    p->sdcount);
	}
	if (p->vol_sc != NULL)
		gv_update_vol_size(p->vol_sc, gv_vol_size(p->vol_sc));
	gv_save_config(p->vinumconf);
	/* We don't update the subdisk state since the user might have to
	 * initiate a rebuild/sync first. */
	return (0);
}

/* Detach a plex from a volume. */
int
gv_detach_plex(struct gv_plex *p, int flags)
{
	struct gv_volume *v;

	g_topology_assert();
	v = p->vol_sc;

	if (v == NULL) {
		G_VINUM_DEBUG(1, "unable to detach %s: already detached",
		    p->name);
		return (0); /* Not an error. */
	}

	/*
	 * Only proceed if forced or volume inactive.
	 */
	if (!(flags & GV_FLAG_F) && (gv_provider_is_open(v->provider) ||
	    p->state == GV_PLEX_UP)) {
		G_VINUM_DEBUG(1, "unable to detach %s: volume %s is busy",
		    p->name, p->volume);
		return (GV_ERR_ISBUSY);
	}
	v->plexcount--;
	/* Make sure someone don't read us when gone. */
	v->last_read_plex = NULL; 
	LIST_REMOVE(p, in_volume);
	p->vol_sc = NULL;
	memset(p->volume, 0, GV_MAXVOLNAME);
	gv_update_vol_size(v, gv_vol_size(v));
	gv_save_config(p->vinumconf);
	return (0);
}

/* Detach a subdisk from a plex. */
int
gv_detach_sd(struct gv_sd *s, int flags)
{
	struct gv_plex *p;

	g_topology_assert();
	p = s->plex_sc;

	if (p == NULL) {
		G_VINUM_DEBUG(1, "unable to detach %s: already detached",
		    s->name);
		return (0); /* Not an error. */
	}

	/*
	 * Don't proceed if we're not forcing, and the plex is up, or degraded
	 * with this subdisk up.
	 */
	if (!(flags & GV_FLAG_F) && ((p->state > GV_PLEX_DEGRADED) ||
	    ((p->state == GV_PLEX_DEGRADED) && (s->state == GV_SD_UP)))) {
	    	G_VINUM_DEBUG(1, "unable to detach %s: plex %s is busy",
		    s->name, s->plex);
		return (GV_ERR_ISBUSY);
	}

	LIST_REMOVE(s, in_plex);
	s->plex_sc = NULL;
	memset(s->plex, 0, GV_MAXPLEXNAME);
	p->sddetached++;
	gv_save_config(s->vinumconf);
	return (0);
}
