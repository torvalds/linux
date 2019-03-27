/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2007 Lukas Ertl
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

#include <sys/param.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <geom/geom.h>
#include <geom/vinum/geom_vinum_var.h>
#include <geom/vinum/geom_vinum_raid5.h>
#include <geom/vinum/geom_vinum.h>

static int		gv_raid5_offset(struct gv_plex *, off_t, off_t,
			    off_t *, off_t *, int *, int *, int);
static struct bio *	gv_raid5_clone_bio(struct bio *, struct gv_sd *,
			    struct gv_raid5_packet *, caddr_t, int);
static int	gv_raid5_request(struct gv_plex *, struct gv_raid5_packet *,
		    struct bio *, caddr_t, off_t, off_t, int *);
static int	gv_raid5_check(struct gv_plex *, struct gv_raid5_packet *,
		    struct bio *, caddr_t, off_t, off_t);
static int	gv_raid5_rebuild(struct gv_plex *, struct gv_raid5_packet *,
		    struct bio *, caddr_t, off_t, off_t);

struct gv_raid5_packet *
gv_raid5_start(struct gv_plex *p, struct bio *bp, caddr_t addr, off_t boff,
    off_t bcount)
{
	struct bio *cbp;
	struct gv_raid5_packet *wp, *wp2;
	struct gv_bioq *bq, *bq2;
	int err, delay;

	delay = 0;
	wp = g_malloc(sizeof(*wp), M_WAITOK | M_ZERO);
	wp->bio = bp;
	wp->waiting = NULL;
	wp->parity = NULL;
	TAILQ_INIT(&wp->bits);

	if (bp->bio_pflags & GV_BIO_REBUILD)
		err = gv_raid5_rebuild(p, wp, bp, addr, boff, bcount);
	else if (bp->bio_pflags & GV_BIO_CHECK)
		err = gv_raid5_check(p, wp, bp, addr, boff, bcount);
	else
		err = gv_raid5_request(p, wp, bp, addr, boff, bcount, &delay);

	/* Means we have a delayed request. */
	if (delay) {
		g_free(wp);
		return (NULL);
	}
	
	/*
	 * Building the sub-request failed, we probably need to clean up a lot.
	 */
	if (err) {
		G_VINUM_LOGREQ(0, bp, "raid5 plex request failed.");
		TAILQ_FOREACH_SAFE(bq, &wp->bits, queue, bq2) {
			TAILQ_REMOVE(&wp->bits, bq, queue);
			g_free(bq);
		}
		if (wp->waiting != NULL) {
			if (wp->waiting->bio_cflags & GV_BIO_MALLOC)
				g_free(wp->waiting->bio_data);
			g_destroy_bio(wp->waiting);
		}
		if (wp->parity != NULL) {
			if (wp->parity->bio_cflags & GV_BIO_MALLOC)
				g_free(wp->parity->bio_data);
			g_destroy_bio(wp->parity);
		}
		g_free(wp);

		TAILQ_FOREACH_SAFE(wp, &p->packets, list, wp2) {
			if (wp->bio != bp)
				continue;

			TAILQ_REMOVE(&p->packets, wp, list);
			TAILQ_FOREACH_SAFE(bq, &wp->bits, queue, bq2) {
				TAILQ_REMOVE(&wp->bits, bq, queue);
				g_free(bq);
			}
			g_free(wp);
		}

		cbp = bioq_takefirst(p->bqueue);
		while (cbp != NULL) {
			if (cbp->bio_cflags & GV_BIO_MALLOC)
				g_free(cbp->bio_data);
			g_destroy_bio(cbp);
			cbp = bioq_takefirst(p->bqueue);
		}

		/* If internal, stop and reset state. */
		if (bp->bio_pflags & GV_BIO_INTERNAL) {
			if (bp->bio_pflags & GV_BIO_MALLOC)
				g_free(bp->bio_data);
			g_destroy_bio(bp);
			/* Reset flags. */
			p->flags &= ~(GV_PLEX_SYNCING | GV_PLEX_REBUILDING |
			    GV_PLEX_GROWING);
			return (NULL);
		}
		g_io_deliver(bp, err);
		return (NULL);
	}

	return (wp);
}

/*
 * Check if the stripe that the work packet wants is already being used by
 * some other work packet.
 */
int
gv_stripe_active(struct gv_plex *p, struct bio *bp)
{
	struct gv_raid5_packet *wp, *owp;
	int overlap;

	wp = bp->bio_caller2;
	if (wp->lockbase == -1)
		return (0);

	overlap = 0;
	TAILQ_FOREACH(owp, &p->packets, list) {
		if (owp == wp)
			break;
		if ((wp->lockbase >= owp->lockbase) &&
		    (wp->lockbase <= owp->lockbase + owp->length)) {
			overlap++;
			break;
		}
		if ((wp->lockbase <= owp->lockbase) &&
		    (wp->lockbase + wp->length >= owp->lockbase)) {
			overlap++;
			break;
		}
	}

	return (overlap);
}

static int
gv_raid5_check(struct gv_plex *p, struct gv_raid5_packet *wp, struct bio *bp,
    caddr_t addr, off_t boff, off_t bcount)
{
	struct gv_sd *parity, *s;
	struct gv_bioq *bq;
	struct bio *cbp;
	int i, psdno;
	off_t real_len, real_off;

	if (p == NULL || LIST_EMPTY(&p->subdisks))
		return (ENXIO);

	gv_raid5_offset(p, boff, bcount, &real_off, &real_len, NULL, &psdno, 1);

	/* Find the right subdisk. */
	parity = NULL;
	i = 0;
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		if (i == psdno) {
			parity = s;
			break;
		}
		i++;
	}

	/* Parity stripe not found. */
	if (parity == NULL)
		return (ENXIO);

	if (parity->state != GV_SD_UP)
		return (ENXIO);

	wp->length = real_len;
	wp->data = addr;
	wp->lockbase = real_off;

	/* Read all subdisks. */
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		/* Skip the parity subdisk. */
		if (s == parity)
			continue;
		/* Skip growing subdisks. */
		if (s->flags & GV_SD_GROW)
			continue;

		cbp = gv_raid5_clone_bio(bp, s, wp, NULL, 1);
		if (cbp == NULL)
			return (ENOMEM);
		cbp->bio_cmd = BIO_READ;

		bioq_insert_tail(p->bqueue, cbp);

		bq = g_malloc(sizeof(*bq), M_WAITOK | M_ZERO);
		bq->bp = cbp;
		TAILQ_INSERT_TAIL(&wp->bits, bq, queue);
	}

	/* Read the parity data. */
	cbp = gv_raid5_clone_bio(bp, parity, wp, NULL, 1);
	if (cbp == NULL)
		return (ENOMEM);
	cbp->bio_cmd = BIO_READ;
	wp->waiting = cbp;

	/*
	 * In case we want to rebuild the parity, create an extra BIO to write
	 * it out.  It also acts as buffer for the XOR operations.
	 */
	cbp = gv_raid5_clone_bio(bp, parity, wp, addr, 1);
	if (cbp == NULL)
		return (ENOMEM);
	wp->parity = cbp;

	return (0);
}

/* Rebuild a degraded RAID5 plex. */
static int
gv_raid5_rebuild(struct gv_plex *p, struct gv_raid5_packet *wp, struct bio *bp,
    caddr_t addr, off_t boff, off_t bcount)
{
	struct gv_sd *broken, *s;
	struct gv_bioq *bq;
	struct bio *cbp;
	off_t real_len, real_off;

	if (p == NULL || LIST_EMPTY(&p->subdisks))
		return (ENXIO);

	gv_raid5_offset(p, boff, bcount, &real_off, &real_len, NULL, NULL, 1);

	/* Find the right subdisk. */
	broken = NULL;
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		if (s->state != GV_SD_UP)
			broken = s;
	}

	/* Broken stripe not found. */
	if (broken == NULL)
		return (ENXIO);

	switch (broken->state) {
	case GV_SD_UP:
		return (EINVAL);

	case GV_SD_STALE:
		if (!(bp->bio_pflags & GV_BIO_REBUILD))
			return (ENXIO);

		G_VINUM_DEBUG(1, "sd %s is reviving", broken->name);
		gv_set_sd_state(broken, GV_SD_REVIVING, GV_SETSTATE_FORCE);
		/* Set this bit now, but should be set at end. */
		broken->flags |= GV_SD_CANGOUP;
		break;

	case GV_SD_REVIVING:
		break;

	default:
		/* All other subdisk states mean it's not accessible. */
		return (ENXIO);
	}

	wp->length = real_len;
	wp->data = addr;
	wp->lockbase = real_off;

	KASSERT(wp->length >= 0, ("gv_rebuild_raid5: wp->length < 0"));

	/* Read all subdisks. */
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		/* Skip the broken subdisk. */
		if (s == broken)
			continue;

		/* Skip growing subdisks. */
		if (s->flags & GV_SD_GROW)
			continue;

		cbp = gv_raid5_clone_bio(bp, s, wp, NULL, 1);
		if (cbp == NULL)
			return (ENOMEM);
		cbp->bio_cmd = BIO_READ;

		bioq_insert_tail(p->bqueue, cbp);

		bq = g_malloc(sizeof(*bq), M_WAITOK | M_ZERO);
		bq->bp = cbp;
		TAILQ_INSERT_TAIL(&wp->bits, bq, queue);
	}

	/* Write the parity data. */
	cbp = gv_raid5_clone_bio(bp, broken, wp, NULL, 1);
	if (cbp == NULL)
		return (ENOMEM);
	wp->parity = cbp;

	p->synced = boff;

	/* Post notification that we're finished. */
	return (0);
}

/* Build a request group to perform (part of) a RAID5 request. */
static int
gv_raid5_request(struct gv_plex *p, struct gv_raid5_packet *wp,
    struct bio *bp, caddr_t addr, off_t boff, off_t bcount, int *delay)
{
	struct g_geom *gp;
	struct gv_sd *broken, *original, *parity, *s;
	struct gv_bioq *bq;
	struct bio *cbp;
	int i, psdno, sdno, type, grow;
	off_t real_len, real_off;

	gp = bp->bio_to->geom;

	if (p == NULL || LIST_EMPTY(&p->subdisks))
		return (ENXIO);

	/* We are optimistic and assume that this request will be OK. */
#define	REQ_TYPE_NORMAL		0
#define	REQ_TYPE_DEGRADED	1
#define	REQ_TYPE_NOPARITY	2

	type = REQ_TYPE_NORMAL;
	original = parity = broken = NULL;

	/* XXX: The resize won't crash with rebuild or sync, but we should still
	 * be aware of it. Also this should perhaps be done on rebuild/check as
	 * well?
	 */
	/* If we're over, we must use the old. */ 
	if (boff >= p->synced) {
		grow = 1;
	/* Or if over the resized offset, we use all drives. */
	} else if (boff + bcount <= p->synced) {
		grow = 0;
	/* Else, we're in the middle, and must wait a bit. */
	} else {
		bioq_disksort(p->rqueue, bp);
		*delay = 1;
		return (0);
	}
	gv_raid5_offset(p, boff, bcount, &real_off, &real_len,
	    &sdno, &psdno, grow);

	/* Find the right subdisks. */
	i = 0;
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		if (i == sdno)
			original = s;
		if (i == psdno)
			parity = s;
		if (s->state != GV_SD_UP)
			broken = s;
		i++;
	}

	if ((original == NULL) || (parity == NULL))
		return (ENXIO);

	/* Our data stripe is missing. */
	if (original->state != GV_SD_UP)
		type = REQ_TYPE_DEGRADED;

	/* If synchronizing request, just write it if disks are stale. */
	if (original->state == GV_SD_STALE && parity->state == GV_SD_STALE &&
	    bp->bio_pflags & GV_BIO_SYNCREQ && bp->bio_cmd == BIO_WRITE) {
		type = REQ_TYPE_NORMAL;
	/* Our parity stripe is missing. */
	} else if (parity->state != GV_SD_UP) {
		/* We cannot take another failure if we're already degraded. */
		if (type != REQ_TYPE_NORMAL)
			return (ENXIO);
		else
			type = REQ_TYPE_NOPARITY;
	}

	wp->length = real_len;
	wp->data = addr;
	wp->lockbase = real_off;

	KASSERT(wp->length >= 0, ("gv_build_raid5_request: wp->length < 0"));

	if ((p->flags & GV_PLEX_REBUILDING) && (boff + real_len < p->synced))
		type = REQ_TYPE_NORMAL;

	if ((p->flags & GV_PLEX_REBUILDING) && (boff + real_len >= p->synced)) {
		bioq_disksort(p->rqueue, bp);
		*delay = 1;
		return (0);
	}

	switch (bp->bio_cmd) {
	case BIO_READ:
		/*
		 * For a degraded read we need to read in all stripes except
		 * the broken one plus the parity stripe and then recalculate
		 * the desired data.
		 */
		if (type == REQ_TYPE_DEGRADED) {
			bzero(wp->data, wp->length);
			LIST_FOREACH(s, &p->subdisks, in_plex) {
				/* Skip the broken subdisk. */
				if (s == broken)
					continue;
				/* Skip growing if within offset. */
				if (grow && s->flags & GV_SD_GROW)
					continue;
				cbp = gv_raid5_clone_bio(bp, s, wp, NULL, 1);
				if (cbp == NULL)
					return (ENOMEM);

				bioq_insert_tail(p->bqueue, cbp);

				bq = g_malloc(sizeof(*bq), M_WAITOK | M_ZERO);
				bq->bp = cbp;
				TAILQ_INSERT_TAIL(&wp->bits, bq, queue);
			}

		/* A normal read can be fulfilled with the original subdisk. */
		} else {
			cbp = gv_raid5_clone_bio(bp, original, wp, addr, 0);
			if (cbp == NULL)
				return (ENOMEM);

			bioq_insert_tail(p->bqueue, cbp);
		}
		wp->lockbase = -1;

		break;

	case BIO_WRITE:
		/*
		 * A degraded write means we cannot write to the original data
		 * subdisk.  Thus we need to read in all valid stripes,
		 * recalculate the parity from the original data, and then
		 * write the parity stripe back out.
		 */
		if (type == REQ_TYPE_DEGRADED) {
			/* Read all subdisks. */
			LIST_FOREACH(s, &p->subdisks, in_plex) {
				/* Skip the broken and the parity subdisk. */
				if ((s == broken) || (s == parity))
					continue;
				/* Skip growing if within offset. */
				if (grow && s->flags & GV_SD_GROW)
					continue;

				cbp = gv_raid5_clone_bio(bp, s, wp, NULL, 1);
				if (cbp == NULL)
					return (ENOMEM);
				cbp->bio_cmd = BIO_READ;

				bioq_insert_tail(p->bqueue, cbp);

				bq = g_malloc(sizeof(*bq), M_WAITOK | M_ZERO);
				bq->bp = cbp;
				TAILQ_INSERT_TAIL(&wp->bits, bq, queue);
			}

			/* Write the parity data. */
			cbp = gv_raid5_clone_bio(bp, parity, wp, NULL, 1);
			if (cbp == NULL)
				return (ENOMEM);
			bcopy(addr, cbp->bio_data, wp->length);
			wp->parity = cbp;

		/*
		 * When the parity stripe is missing we just write out the data.
		 */
		} else if (type == REQ_TYPE_NOPARITY) {
			cbp = gv_raid5_clone_bio(bp, original, wp, addr, 1);
			if (cbp == NULL)
				return (ENOMEM);

			bioq_insert_tail(p->bqueue, cbp);

			bq = g_malloc(sizeof(*bq), M_WAITOK | M_ZERO);
			bq->bp = cbp;
			TAILQ_INSERT_TAIL(&wp->bits, bq, queue);

		/*
		 * A normal write request goes to the original subdisk, then we
		 * read in all other stripes, recalculate the parity and write
		 * out the parity again.
		 */
		} else {
			/* Read old parity. */
			cbp = gv_raid5_clone_bio(bp, parity, wp, NULL, 1);
			if (cbp == NULL)
				return (ENOMEM);
			cbp->bio_cmd = BIO_READ;

			bioq_insert_tail(p->bqueue, cbp);

			bq = g_malloc(sizeof(*bq), M_WAITOK | M_ZERO);
			bq->bp = cbp;
			TAILQ_INSERT_TAIL(&wp->bits, bq, queue);

			/* Read old data. */
			cbp = gv_raid5_clone_bio(bp, original, wp, NULL, 1);
			if (cbp == NULL)
				return (ENOMEM);
			cbp->bio_cmd = BIO_READ;

			bioq_insert_tail(p->bqueue, cbp);

			bq = g_malloc(sizeof(*bq), M_WAITOK | M_ZERO);
			bq->bp = cbp;
			TAILQ_INSERT_TAIL(&wp->bits, bq, queue);

			/* Write new data. */
			cbp = gv_raid5_clone_bio(bp, original, wp, addr, 1);
			if (cbp == NULL)
				return (ENOMEM);

			/*
			 * We must not write the new data until the old data
			 * was read, so hold this BIO back until we're ready
			 * for it.
			 */
			wp->waiting = cbp;

			/* The final bio for the parity. */
			cbp = gv_raid5_clone_bio(bp, parity, wp, NULL, 1);
			if (cbp == NULL)
				return (ENOMEM);

			/* Remember that this is the BIO for the parity data. */
			wp->parity = cbp;
		}
		break;

	default:
		return (EINVAL);
	}

	return (0);
}

/*
 * Calculate the offsets in the various subdisks for a RAID5 request. Also take
 * care of new subdisks in an expanded RAID5 array. 
 * XXX: This assumes that the new subdisks are inserted after the others (which
 * is okay as long as plex_offset is larger). If subdisks are inserted into the
 * plexlist before, we get problems.
 */
static int
gv_raid5_offset(struct gv_plex *p, off_t boff, off_t bcount, off_t *real_off,
    off_t *real_len, int *sdno, int *psdno, int growing)
{
	struct gv_sd *s;
	int sd, psd, sdcount;
	off_t len_left, stripeend, stripeoff, stripestart;

	sdcount = p->sdcount;
	if (growing) {
		LIST_FOREACH(s, &p->subdisks, in_plex) {
			if (s->flags & GV_SD_GROW)
				sdcount--;
		}
	}

	/* The number of the subdisk containing the parity stripe. */
	psd = sdcount - 1 - ( boff / (p->stripesize * (sdcount - 1))) %
	    sdcount;
	KASSERT(psdno >= 0, ("gv_raid5_offset: psdno < 0"));

	/* Offset of the start address from the start of the stripe. */
	stripeoff = boff % (p->stripesize * (sdcount - 1));
	KASSERT(stripeoff >= 0, ("gv_raid5_offset: stripeoff < 0"));

	/* The number of the subdisk where the stripe resides. */
	sd = stripeoff / p->stripesize;
	KASSERT(sdno >= 0, ("gv_raid5_offset: sdno < 0"));

	/* At or past parity subdisk. */
	if (sd >= psd)
		sd++;

	/* The offset of the stripe on this subdisk. */
	stripestart = (boff - stripeoff) / (sdcount - 1);
	KASSERT(stripestart >= 0, ("gv_raid5_offset: stripestart < 0"));

	stripeoff %= p->stripesize;

	/* The offset of the request on this subdisk. */
	*real_off = stripestart + stripeoff;

	stripeend = stripestart + p->stripesize;
	len_left = stripeend - *real_off;
	KASSERT(len_left >= 0, ("gv_raid5_offset: len_left < 0"));

	*real_len = (bcount <= len_left) ? bcount : len_left;

	if (sdno != NULL)
		*sdno = sd;
	if (psdno != NULL)
		*psdno = psd;

	return (0);
}

static struct bio *
gv_raid5_clone_bio(struct bio *bp, struct gv_sd *s, struct gv_raid5_packet *wp,
    caddr_t addr, int use_wp)
{
	struct bio *cbp;

	cbp = g_clone_bio(bp);
	if (cbp == NULL)
		return (NULL);
	if (addr == NULL) {
		cbp->bio_data = g_malloc(wp->length, M_WAITOK | M_ZERO);
		cbp->bio_cflags |= GV_BIO_MALLOC;
	} else
		cbp->bio_data = addr;
	cbp->bio_offset = wp->lockbase + s->drive_offset;
	cbp->bio_length = wp->length;
	cbp->bio_done = gv_done;
	cbp->bio_caller1 = s;
	if (use_wp)
		cbp->bio_caller2 = wp;

	return (cbp);
}
