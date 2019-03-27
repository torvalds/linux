/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004, 2007 Lukas Ertl
 * Copyright (c) 2007, 2009 Ulf Lilleengen
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

static int	gv_check_parity(struct gv_plex *, struct bio *,
		    struct gv_raid5_packet *);
static int	gv_normal_parity(struct gv_plex *, struct bio *,
		    struct gv_raid5_packet *);
static void	gv_plex_flush(struct gv_plex *);
static int	gv_plex_offset(struct gv_plex *, off_t, off_t, off_t *, off_t *,
		    int *, int);
static int 	gv_plex_normal_request(struct gv_plex *, struct bio *, off_t,
		    off_t,  caddr_t);
static void	gv_post_bio(struct gv_softc *, struct bio *);

void
gv_plex_start(struct gv_plex *p, struct bio *bp)
{
	struct bio *cbp;
	struct gv_sd *s;
	struct gv_raid5_packet *wp;
	caddr_t addr;
	off_t bcount, boff, len;

	bcount = bp->bio_length;
	addr = bp->bio_data;
	boff = bp->bio_offset;

	/* Walk over the whole length of the request, we might split it up. */
	while (bcount > 0) {
		wp = NULL;

 		/*
		 * RAID5 plexes need special treatment, as a single request
		 * might involve several read/write sub-requests.
 		 */
		if (p->org == GV_PLEX_RAID5) {
			wp = gv_raid5_start(p, bp, addr, boff, bcount);
 			if (wp == NULL)
 				return;
 
			len = wp->length;

			if (TAILQ_EMPTY(&wp->bits))
				g_free(wp);
			else if (wp->lockbase != -1)
				TAILQ_INSERT_TAIL(&p->packets, wp, list);

		/*
		 * Requests to concatenated and striped plexes go straight
		 * through.
		 */
		} else {
			len = gv_plex_normal_request(p, bp, boff, bcount, addr);
		}
		if (len < 0)
			return;
			
		bcount -= len;
		addr += len;
		boff += len;
	}

	/*
	 * Fire off all sub-requests.  We get the correct consumer (== drive)
	 * to send each request to via the subdisk that was stored in
	 * cbp->bio_caller1.
	 */
	cbp = bioq_takefirst(p->bqueue);
	while (cbp != NULL) {
		/*
		 * RAID5 sub-requests need to come in correct order, otherwise
		 * we trip over the parity, as it might be overwritten by
		 * another sub-request.  We abuse cbp->bio_caller2 to mark
		 * potential overlap situations. 
		 */
		if (cbp->bio_caller2 != NULL && gv_stripe_active(p, cbp)) {
			/* Park the bio on the waiting queue. */
			cbp->bio_pflags |= GV_BIO_ONHOLD;
			bioq_disksort(p->wqueue, cbp);
		} else {
			s = cbp->bio_caller1;
			g_io_request(cbp, s->drive_sc->consumer);
		}
		cbp = bioq_takefirst(p->bqueue);
	}
}

static int
gv_plex_offset(struct gv_plex *p, off_t boff, off_t bcount, off_t *real_off,
    off_t *real_len, int *sdno, int growing)
{
	struct gv_sd *s;
	int i, sdcount;
	off_t len_left, stripeend, stripeno, stripestart;

	switch (p->org) {
	case GV_PLEX_CONCAT:
		/*
		 * Find the subdisk where this request starts.  The subdisks in
		 * this list must be ordered by plex_offset.
		 */
		i = 0;
		LIST_FOREACH(s, &p->subdisks, in_plex) {
			if (s->plex_offset <= boff &&
			    s->plex_offset + s->size > boff) {
				*sdno = i;
				break;
			}
			i++;
		}
		if (s == NULL || s->drive_sc == NULL)
			return (GV_ERR_NOTFOUND);

		/* Calculate corresponding offsets on disk. */
		*real_off = boff - s->plex_offset;
		len_left = s->size - (*real_off);
		KASSERT(len_left >= 0, ("gv_plex_offset: len_left < 0"));
		*real_len = (bcount > len_left) ? len_left : bcount;
		break;

	case GV_PLEX_STRIPED:
		/* The number of the stripe where the request starts. */
		stripeno = boff / p->stripesize;
		KASSERT(stripeno >= 0, ("gv_plex_offset: stripeno < 0"));

		/* Take growing subdisks into account when calculating. */
		sdcount = gv_sdcount(p, (boff >= p->synced));

		if (!(boff + bcount <= p->synced) &&
		    (p->flags & GV_PLEX_GROWING) &&
		    !growing)
			return (GV_ERR_ISBUSY);
		*sdno = stripeno % sdcount;

		KASSERT(sdno >= 0, ("gv_plex_offset: sdno < 0"));
		stripestart = (stripeno / sdcount) *
		    p->stripesize;
		KASSERT(stripestart >= 0, ("gv_plex_offset: stripestart < 0"));
		stripeend = stripestart + p->stripesize;
		*real_off = boff - (stripeno * p->stripesize) +
		    stripestart;
		len_left = stripeend - *real_off;
		KASSERT(len_left >= 0, ("gv_plex_offset: len_left < 0"));

		*real_len = (bcount <= len_left) ? bcount : len_left;
		break;

	default:
		return (GV_ERR_PLEXORG);
	}
	return (0);
}

/*
 * Prepare a normal plex request.
 */
static int 
gv_plex_normal_request(struct gv_plex *p, struct bio *bp, off_t boff,
    off_t bcount,  caddr_t addr)
{
	struct gv_sd *s;
	struct bio *cbp;
	off_t real_len, real_off;
	int i, err, sdno;

	s = NULL;
	sdno = -1;
	real_len = real_off = 0;

	err = ENXIO;

	if (p == NULL || LIST_EMPTY(&p->subdisks)) 
		goto bad;

	err = gv_plex_offset(p, boff, bcount, &real_off,
	    &real_len, &sdno, (bp->bio_pflags & GV_BIO_GROW));
	/* If the request was blocked, put it into wait. */
	if (err == GV_ERR_ISBUSY) {
		bioq_disksort(p->rqueue, bp);
		return (-1); /* "Fail", and delay request. */
	}
	if (err) {
		err = ENXIO;
		goto bad;
	}
	err = ENXIO;

	/* Find the right subdisk. */
	i = 0;
	LIST_FOREACH(s, &p->subdisks, in_plex) {
		if (i == sdno)
			break;
		i++;
	}

	/* Subdisk not found. */
	if (s == NULL || s->drive_sc == NULL)
		goto bad;

	/* Now check if we can handle the request on this subdisk. */
	switch (s->state) {
	case GV_SD_UP:
		/* If the subdisk is up, just continue. */
		break;
	case GV_SD_DOWN:
		if (bp->bio_pflags & GV_BIO_INTERNAL)
			G_VINUM_DEBUG(0, "subdisk must be in the stale state in"
			    " order to perform administrative requests");
		goto bad;
	case GV_SD_STALE:
		if (!(bp->bio_pflags & GV_BIO_SYNCREQ)) {
			G_VINUM_DEBUG(0, "subdisk stale, unable to perform "
			    "regular requests");
			goto bad;
		}

		G_VINUM_DEBUG(1, "sd %s is initializing", s->name);
		gv_set_sd_state(s, GV_SD_INITIALIZING, GV_SETSTATE_FORCE);
		break;
	case GV_SD_INITIALIZING:
		if (bp->bio_cmd == BIO_READ)
			goto bad;
		break;
	default:
		/* All other subdisk states mean it's not accessible. */
		goto bad;
	}

	/* Clone the bio and adjust the offsets and sizes. */
	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		err = ENOMEM;
		goto bad;
	}
	cbp->bio_offset = real_off + s->drive_offset;
	cbp->bio_length = real_len;
	cbp->bio_data = addr;
	cbp->bio_done = gv_done;
	cbp->bio_caller1 = s;

	/* Store the sub-requests now and let others issue them. */
	bioq_insert_tail(p->bqueue, cbp); 
	return (real_len);
bad:
	G_VINUM_LOGREQ(0, bp, "plex request failed.");
	/* Building the sub-request failed. If internal BIO, do not deliver. */
	if (bp->bio_pflags & GV_BIO_INTERNAL) {
		if (bp->bio_pflags & GV_BIO_MALLOC)
			g_free(bp->bio_data);
		g_destroy_bio(bp);
		p->flags &= ~(GV_PLEX_SYNCING | GV_PLEX_REBUILDING |
		    GV_PLEX_GROWING);
		return (-1);
	}
	g_io_deliver(bp, err);
	return (-1);
}

/*
 * Handle a completed request to a striped or concatenated plex.
 */
void
gv_plex_normal_done(struct gv_plex *p, struct bio *bp)
{
	struct bio *pbp;

	pbp = bp->bio_parent;
	if (pbp->bio_error == 0)
		pbp->bio_error = bp->bio_error;
	g_destroy_bio(bp);
	pbp->bio_inbed++;
	if (pbp->bio_children == pbp->bio_inbed) {
		/* Just set it to length since multiple plexes will
		 * screw things up. */
		pbp->bio_completed = pbp->bio_length;
		if (pbp->bio_pflags & GV_BIO_SYNCREQ)
			gv_sync_complete(p, pbp);
		else if (pbp->bio_pflags & GV_BIO_GROW)
			gv_grow_complete(p, pbp);
		else
			g_io_deliver(pbp, pbp->bio_error);
	}
}

/*
 * Handle a completed request to a RAID-5 plex.
 */
void
gv_plex_raid5_done(struct gv_plex *p, struct bio *bp)
{
	struct gv_softc *sc;
	struct bio *cbp, *pbp;
	struct gv_bioq *bq, *bq2;
	struct gv_raid5_packet *wp;
	off_t completed;
	int i;

	completed = 0;
	sc = p->vinumconf;
	wp = bp->bio_caller2;

	switch (bp->bio_parent->bio_cmd) {
	case BIO_READ:
		if (wp == NULL) {
			completed = bp->bio_completed;
			break;
		}

		TAILQ_FOREACH_SAFE(bq, &wp->bits, queue, bq2) {
			if (bq->bp != bp)
				continue;
			TAILQ_REMOVE(&wp->bits, bq, queue);
			g_free(bq);
			for (i = 0; i < wp->length; i++)
				wp->data[i] ^= bp->bio_data[i];
			break;
		}
		if (TAILQ_EMPTY(&wp->bits)) {
			completed = wp->length;
			if (wp->lockbase != -1) {
				TAILQ_REMOVE(&p->packets, wp, list);
				/* Bring the waiting bios back into the game. */
				pbp = bioq_takefirst(p->wqueue);
				while (pbp != NULL) {
					gv_post_bio(sc, pbp);
					pbp = bioq_takefirst(p->wqueue);
				}
			}
			g_free(wp);
		}

		break;

 	case BIO_WRITE:
		/* XXX can this ever happen? */
		if (wp == NULL) {
			completed = bp->bio_completed;
			break;
		}

		/* Check if we need to handle parity data. */
		TAILQ_FOREACH_SAFE(bq, &wp->bits, queue, bq2) {
			if (bq->bp != bp)
				continue;
			TAILQ_REMOVE(&wp->bits, bq, queue);
			g_free(bq);
			cbp = wp->parity;
			if (cbp != NULL) {
				for (i = 0; i < wp->length; i++)
					cbp->bio_data[i] ^= bp->bio_data[i];
			}
			break;
		}

		/* Handle parity data. */
		if (TAILQ_EMPTY(&wp->bits)) {
			if (bp->bio_parent->bio_pflags & GV_BIO_CHECK)
				i = gv_check_parity(p, bp, wp);
			else
				i = gv_normal_parity(p, bp, wp);

			/* All of our sub-requests have finished. */
			if (i) {
				completed = wp->length;
				TAILQ_REMOVE(&p->packets, wp, list);
				/* Bring the waiting bios back into the game. */
				pbp = bioq_takefirst(p->wqueue);
				while (pbp != NULL) {
					gv_post_bio(sc, pbp);
					pbp = bioq_takefirst(p->wqueue);
				}
				g_free(wp);
			}
		}

		break;
	}

	pbp = bp->bio_parent;
	if (pbp->bio_error == 0)
		pbp->bio_error = bp->bio_error;
	pbp->bio_completed += completed;

	/* When the original request is finished, we deliver it. */
	pbp->bio_inbed++;
	if (pbp->bio_inbed == pbp->bio_children) {
		/* Hand it over for checking or delivery. */
		if (pbp->bio_cmd == BIO_WRITE &&
		    (pbp->bio_pflags & GV_BIO_CHECK)) {
			gv_parity_complete(p, pbp);
		} else if (pbp->bio_cmd == BIO_WRITE &&
		    (pbp->bio_pflags & GV_BIO_REBUILD)) {
			gv_rebuild_complete(p, pbp);
		} else if (pbp->bio_pflags & GV_BIO_INIT) {
			gv_init_complete(p, pbp);
		} else if (pbp->bio_pflags & GV_BIO_SYNCREQ) {
			gv_sync_complete(p, pbp);
		} else if (pbp->bio_pflags & GV_BIO_GROW) {
			gv_grow_complete(p, pbp);
		} else {
			g_io_deliver(pbp, pbp->bio_error);
		}
	}

	/* Clean up what we allocated. */
	if (bp->bio_cflags & GV_BIO_MALLOC)
		g_free(bp->bio_data);
	g_destroy_bio(bp);
}

static int
gv_check_parity(struct gv_plex *p, struct bio *bp, struct gv_raid5_packet *wp)
{
	struct bio *pbp;
	struct gv_sd *s;
	int err, finished, i;

	err = 0;
	finished = 1;

	if (wp->waiting != NULL) {
		pbp = wp->waiting;
		wp->waiting = NULL;
		s = pbp->bio_caller1;
		g_io_request(pbp, s->drive_sc->consumer);
		finished = 0;

	} else if (wp->parity != NULL) {
		pbp = wp->parity;
		wp->parity = NULL;

		/* Check if the parity is correct. */
		for (i = 0; i < wp->length; i++) {
			if (bp->bio_data[i] != pbp->bio_data[i]) {
				err = 1;
				break;
			}
		}

		/* The parity is not correct... */
		if (err) {
			bp->bio_parent->bio_error = EAGAIN;

			/* ... but we rebuild it. */
			if (bp->bio_parent->bio_pflags & GV_BIO_PARITY) {
				s = pbp->bio_caller1;
				g_io_request(pbp, s->drive_sc->consumer);
				finished = 0;
			}
		}

		/*
		 * Clean up the BIO we would have used for rebuilding the
		 * parity.
		 */
		if (finished) {
			bp->bio_parent->bio_inbed++;
			g_destroy_bio(pbp);
		}

	}

	return (finished);
}

static int
gv_normal_parity(struct gv_plex *p, struct bio *bp, struct gv_raid5_packet *wp)
{
	struct bio *cbp, *pbp;
	struct gv_sd *s;
	int finished, i;

	finished = 1;

	if (wp->waiting != NULL) {
		pbp = wp->waiting;
		wp->waiting = NULL;
		cbp = wp->parity;
		for (i = 0; i < wp->length; i++)
			cbp->bio_data[i] ^= pbp->bio_data[i];
		s = pbp->bio_caller1;
		g_io_request(pbp, s->drive_sc->consumer);
		finished = 0;

	} else if (wp->parity != NULL) {
		cbp = wp->parity;
		wp->parity = NULL;
		s = cbp->bio_caller1;
		g_io_request(cbp, s->drive_sc->consumer);
		finished = 0;
	}

	return (finished);
}

/* Flush the queue with delayed requests. */
static void
gv_plex_flush(struct gv_plex *p)
{
	struct gv_softc *sc;
	struct bio *bp;

	sc = p->vinumconf;
	bp = bioq_takefirst(p->rqueue);
	while (bp != NULL) {
		gv_plex_start(p, bp);
		bp = bioq_takefirst(p->rqueue);
	}
}

static void
gv_post_bio(struct gv_softc *sc, struct bio *bp)
{

	KASSERT(sc != NULL, ("NULL sc"));
	KASSERT(bp != NULL, ("NULL bp"));
	mtx_lock(&sc->bqueue_mtx);
	bioq_disksort(sc->bqueue_down, bp);
	wakeup(sc);
	mtx_unlock(&sc->bqueue_mtx);
}

int
gv_sync_request(struct gv_plex *from, struct gv_plex *to, off_t offset,
    off_t length, int type, caddr_t data)
{
	struct gv_softc *sc;
	struct bio *bp;

	KASSERT(from != NULL, ("NULL from"));
	KASSERT(to != NULL, ("NULL to"));
	sc = from->vinumconf;
	KASSERT(sc != NULL, ("NULL sc"));

	bp = g_new_bio();
	if (bp == NULL) {
		G_VINUM_DEBUG(0, "sync from '%s' failed at offset "
		    " %jd; out of memory", from->name, offset);
		return (ENOMEM);
	}
	bp->bio_length = length;
	bp->bio_done = gv_done;
	bp->bio_pflags |= GV_BIO_SYNCREQ;
	bp->bio_offset = offset;
	bp->bio_caller1 = from;		
	bp->bio_caller2 = to;
	bp->bio_cmd = type;
	if (data == NULL)
		data = g_malloc(length, M_WAITOK);
	bp->bio_pflags |= GV_BIO_MALLOC; /* Free on the next run. */
	bp->bio_data = data;

	/* Send down next. */
	gv_post_bio(sc, bp);
	//gv_plex_start(from, bp);
	return (0);
}

/*
 * Handle a finished plex sync bio.
 */
int
gv_sync_complete(struct gv_plex *to, struct bio *bp)
{
	struct gv_plex *from, *p;
	struct gv_sd *s;
	struct gv_volume *v;
	struct gv_softc *sc;
	off_t offset;
	int err;

	g_topology_assert_not();

	err = 0;
	KASSERT(to != NULL, ("NULL to"));
	KASSERT(bp != NULL, ("NULL bp"));
	from = bp->bio_caller2;
	KASSERT(from != NULL, ("NULL from"));
	v = to->vol_sc;
	KASSERT(v != NULL, ("NULL v"));
	sc = v->vinumconf;
	KASSERT(sc != NULL, ("NULL sc"));

	/* If it was a read, write it. */
	if (bp->bio_cmd == BIO_READ) {
		err = gv_sync_request(from, to, bp->bio_offset, bp->bio_length,
	    	    BIO_WRITE, bp->bio_data);
	/* If it was a write, read the next one. */
	} else if (bp->bio_cmd == BIO_WRITE) {
		if (bp->bio_pflags & GV_BIO_MALLOC)
			g_free(bp->bio_data);
		to->synced += bp->bio_length;
		/* If we're finished, clean up. */
		if (bp->bio_offset + bp->bio_length >= from->size) {
			G_VINUM_DEBUG(1, "syncing of %s from %s completed",
			    to->name, from->name);
			/* Update our state. */
			LIST_FOREACH(s, &to->subdisks, in_plex)
				gv_set_sd_state(s, GV_SD_UP, 0);
			gv_update_plex_state(to);
			to->flags &= ~GV_PLEX_SYNCING;
			to->synced = 0;
			gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
		} else {
			offset = bp->bio_offset + bp->bio_length;
			err = gv_sync_request(from, to, offset,
			    MIN(bp->bio_length, from->size - offset),
			    BIO_READ, NULL);
		}
	}
	g_destroy_bio(bp);
	/* Clean up if there was an error. */
	if (err) {
		to->flags &= ~GV_PLEX_SYNCING;
		G_VINUM_DEBUG(0, "error syncing plexes: error code %d", err);
	}

	/* Check if all plexes are synced, and lower refcounts. */
	g_topology_lock();
	LIST_FOREACH(p, &v->plexes, in_volume) {
		if (p->flags & GV_PLEX_SYNCING) {
			g_topology_unlock();
			return (-1);
		}
	}
	/* If we came here, all plexes are synced, and we're free. */
	gv_access(v->provider, -1, -1, 0);
	g_topology_unlock();
	G_VINUM_DEBUG(1, "plex sync completed");
	gv_volume_flush(v);
	return (0);
}

/*
 * Create a new bio struct for the next grow request.
 */
int
gv_grow_request(struct gv_plex *p, off_t offset, off_t length, int type,
    caddr_t data)
{
	struct gv_softc *sc;
	struct bio *bp;

	KASSERT(p != NULL, ("gv_grow_request: NULL p"));
	sc = p->vinumconf;
	KASSERT(sc != NULL, ("gv_grow_request: NULL sc"));

	bp = g_new_bio();
	if (bp == NULL) {
		G_VINUM_DEBUG(0, "grow of %s failed creating bio: "
		    "out of memory", p->name);
		return (ENOMEM);
	}

	bp->bio_cmd = type;
	bp->bio_done = gv_done;
	bp->bio_error = 0;
	bp->bio_caller1 = p;
	bp->bio_offset = offset;
	bp->bio_length = length;
	bp->bio_pflags |= GV_BIO_GROW;
	if (data == NULL)
		data = g_malloc(length, M_WAITOK);
	bp->bio_pflags |= GV_BIO_MALLOC;
	bp->bio_data = data;

	gv_post_bio(sc, bp);
	//gv_plex_start(p, bp);
	return (0);
}

/*
 * Finish handling of a bio to a growing plex.
 */
void
gv_grow_complete(struct gv_plex *p, struct bio *bp)
{
	struct gv_softc *sc;
	struct gv_sd *s;
	struct gv_volume *v;
	off_t origsize, offset;
	int sdcount, err;

	v = p->vol_sc;
	KASSERT(v != NULL, ("gv_grow_complete: NULL v"));
	sc = v->vinumconf;
	KASSERT(sc != NULL, ("gv_grow_complete: NULL sc"));
	err = 0;

	/* If it was a read, write it. */
	if (bp->bio_cmd == BIO_READ) {
		p->synced += bp->bio_length;
		err = gv_grow_request(p, bp->bio_offset, bp->bio_length,
		    BIO_WRITE, bp->bio_data);
	/* If it was a write, read next. */
	} else if (bp->bio_cmd == BIO_WRITE) {
		if (bp->bio_pflags & GV_BIO_MALLOC)
			g_free(bp->bio_data);

		/* Find the real size of the plex. */
		sdcount = gv_sdcount(p, 1);
		s = LIST_FIRST(&p->subdisks);
		KASSERT(s != NULL, ("NULL s"));
		origsize = (s->size * (sdcount - 1));
		if (bp->bio_offset + bp->bio_length >= origsize) {
			G_VINUM_DEBUG(1, "growing of %s completed", p->name);
			p->flags &= ~GV_PLEX_GROWING;
			LIST_FOREACH(s, &p->subdisks, in_plex) {
				s->flags &= ~GV_SD_GROW;
				gv_set_sd_state(s, GV_SD_UP, 0);
			}
			p->size = gv_plex_size(p);
			gv_update_vol_size(v, gv_vol_size(v));
			gv_set_plex_state(p, GV_PLEX_UP, 0);
			g_topology_lock();
			gv_access(v->provider, -1, -1, 0);
			g_topology_unlock();
			p->synced = 0;
			gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
			/* Issue delayed requests. */
			gv_plex_flush(p);
		} else {
			offset = bp->bio_offset + bp->bio_length;
			err = gv_grow_request(p, offset,
			   MIN(bp->bio_length, origsize - offset),
			   BIO_READ, NULL);
		}
	}
	g_destroy_bio(bp);

	if (err) {
		p->flags &= ~GV_PLEX_GROWING;
		G_VINUM_DEBUG(0, "error growing plex: error code %d", err);
	}
}


/*
 * Create an initialization BIO and send it off to the consumer. Assume that
 * we're given initialization data as parameter.
 */
void
gv_init_request(struct gv_sd *s, off_t start, caddr_t data, off_t length)
{
	struct gv_drive *d;
	struct g_consumer *cp;
	struct bio *bp, *cbp;

	KASSERT(s != NULL, ("gv_init_request: NULL s"));
	d = s->drive_sc;
	KASSERT(d != NULL, ("gv_init_request: NULL d"));
	cp = d->consumer;
	KASSERT(cp != NULL, ("gv_init_request: NULL cp"));

	bp = g_new_bio();
	if (bp == NULL) {
		G_VINUM_DEBUG(0, "subdisk '%s' init: write failed at offset %jd"
		    " (drive offset %jd); out of memory", s->name,
		    (intmax_t)s->initialized, (intmax_t)start);
		return; /* XXX: Error codes. */
	}
	bp->bio_cmd = BIO_WRITE;
	bp->bio_data = data;
	bp->bio_done = gv_done;
	bp->bio_error = 0;
	bp->bio_length = length;
	bp->bio_pflags |= GV_BIO_INIT;
	bp->bio_offset = start;
	bp->bio_caller1 = s;

	/* Then ofcourse, we have to clone it. */
	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		G_VINUM_DEBUG(0, "subdisk '%s' init: write failed at offset %jd"
		    " (drive offset %jd); out of memory", s->name,
		    (intmax_t)s->initialized, (intmax_t)start);
		return; /* XXX: Error codes. */
	}
	cbp->bio_done = gv_done;
	cbp->bio_caller1 = s;
	/* Send it off to the consumer. */
	g_io_request(cbp, cp);
}

/*
 * Handle a finished initialization BIO.
 */
void
gv_init_complete(struct gv_plex *p, struct bio *bp)
{
	struct gv_softc *sc;
	struct gv_drive *d;
	struct g_consumer *cp;
	struct gv_sd *s;
	off_t start, length;
	caddr_t data;
	int error;

	s = bp->bio_caller1;
	start = bp->bio_offset;
	length = bp->bio_length;
	error = bp->bio_error;
	data = bp->bio_data;

	KASSERT(s != NULL, ("gv_init_complete: NULL s"));
	d = s->drive_sc;
	KASSERT(d != NULL, ("gv_init_complete: NULL d"));
	cp = d->consumer;
	KASSERT(cp != NULL, ("gv_init_complete: NULL cp"));
	sc = p->vinumconf;
	KASSERT(sc != NULL, ("gv_init_complete: NULL sc"));

	g_destroy_bio(bp);

	/*
	 * First we need to find out if it was okay, and abort if it's not.
	 * Then we need to free previous buffers, find out the correct subdisk,
	 * as well as getting the correct starting point and length of the BIO.
	 */
	if (start >= s->drive_offset + s->size) {
		/* Free the data we initialized. */
		if (data != NULL)
			g_free(data);
		g_topology_assert_not();
		g_topology_lock();
		g_access(cp, 0, -1, 0);
		g_topology_unlock();
		if (error) {
			gv_set_sd_state(s, GV_SD_STALE, GV_SETSTATE_FORCE |
			    GV_SETSTATE_CONFIG);
		} else {
			gv_set_sd_state(s, GV_SD_UP, GV_SETSTATE_CONFIG);
			s->initialized = 0;
			gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
			G_VINUM_DEBUG(1, "subdisk '%s' init: finished "
			    "successfully", s->name);
		}
		return;
	}
	s->initialized += length;
	start += length;
	gv_init_request(s, start, data, length);
}

/*
 * Create a new bio struct for the next parity rebuild. Used both by internal
 * rebuild of degraded plexes as well as user initiated rebuilds/checks.
 */
void
gv_parity_request(struct gv_plex *p, int flags, off_t offset)
{
	struct gv_softc *sc;
	struct bio *bp;

	KASSERT(p != NULL, ("gv_parity_request: NULL p"));
	sc = p->vinumconf;
	KASSERT(sc != NULL, ("gv_parity_request: NULL sc"));

	bp = g_new_bio();
	if (bp == NULL) {
		G_VINUM_DEBUG(0, "rebuild of %s failed creating bio: "
		    "out of memory", p->name);
		return;
	}

	bp->bio_cmd = BIO_WRITE;
	bp->bio_done = gv_done;
	bp->bio_error = 0;
	bp->bio_length = p->stripesize;
	bp->bio_caller1 = p;

	/*
	 * Check if it's a rebuild of a degraded plex or a user request of
	 * parity rebuild.
	 */
	if (flags & GV_BIO_REBUILD)
		bp->bio_data = g_malloc(GV_DFLT_SYNCSIZE, M_WAITOK);
	else if (flags & GV_BIO_CHECK)
		bp->bio_data = g_malloc(p->stripesize, M_WAITOK | M_ZERO);
	else {
		G_VINUM_DEBUG(0, "invalid flags given in rebuild");
		return;
	}

	bp->bio_pflags = flags;
	bp->bio_pflags |= GV_BIO_MALLOC;

	/* We still have more parity to build. */
	bp->bio_offset = offset;
	gv_post_bio(sc, bp);
	//gv_plex_start(p, bp); /* Send it down to the plex. */
}

/*
 * Handle a finished parity write.
 */
void
gv_parity_complete(struct gv_plex *p, struct bio *bp)
{
	struct gv_softc *sc;
	int error, flags;

	error = bp->bio_error;
	flags = bp->bio_pflags;
	flags &= ~GV_BIO_MALLOC;

	sc = p->vinumconf;
	KASSERT(sc != NULL, ("gv_parity_complete: NULL sc"));

	/* Clean up what we allocated. */
	if (bp->bio_pflags & GV_BIO_MALLOC)
		g_free(bp->bio_data);
	g_destroy_bio(bp);

	if (error == EAGAIN) {
		G_VINUM_DEBUG(0, "parity incorrect at offset 0x%jx",
		    (intmax_t)p->synced);
	}

	/* Any error is fatal, except EAGAIN when we're rebuilding. */
	if (error && !(error == EAGAIN && (flags & GV_BIO_PARITY))) {
		/* Make sure we don't have the lock. */
		g_topology_assert_not();
		g_topology_lock();
		gv_access(p->vol_sc->provider, -1, -1, 0);
		g_topology_unlock();
		G_VINUM_DEBUG(0, "parity check on %s failed at 0x%jx "
		    "errno %d", p->name, (intmax_t)p->synced, error);
		return;
	} else {
		p->synced += p->stripesize;
	}

	if (p->synced >= p->size) {
		/* Make sure we don't have the lock. */
		g_topology_assert_not();
		g_topology_lock();
		gv_access(p->vol_sc->provider, -1, -1, 0);
		g_topology_unlock();
		/* We're finished. */
		G_VINUM_DEBUG(1, "parity operation on %s finished", p->name);
		p->synced = 0;
		gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
		return;
	}

	/* Send down next. It will determine if we need to itself. */
	gv_parity_request(p, flags, p->synced);
}

/*
 * Handle a finished plex rebuild bio.
 */
void
gv_rebuild_complete(struct gv_plex *p, struct bio *bp)
{
	struct gv_softc *sc;
	struct gv_sd *s;
	int error, flags;
	off_t offset;

	error = bp->bio_error;
	flags = bp->bio_pflags;
	offset = bp->bio_offset;
	flags &= ~GV_BIO_MALLOC;
	sc = p->vinumconf;
	KASSERT(sc != NULL, ("gv_rebuild_complete: NULL sc"));

	/* Clean up what we allocated. */
	if (bp->bio_pflags & GV_BIO_MALLOC)
		g_free(bp->bio_data);
	g_destroy_bio(bp);

	if (error) {
		g_topology_assert_not();
		g_topology_lock();
		gv_access(p->vol_sc->provider, -1, -1, 0);
		g_topology_unlock();
	
		G_VINUM_DEBUG(0, "rebuild of %s failed at offset %jd errno: %d",
		    p->name, (intmax_t)offset, error);
		p->flags &= ~GV_PLEX_REBUILDING;
		p->synced = 0;
		gv_plex_flush(p); /* Flush out remaining rebuild BIOs. */
		return;
	}

	offset += (p->stripesize * (gv_sdcount(p, 1) - 1));
	if (offset >= p->size) {
		/* We're finished. */
		g_topology_assert_not();
		g_topology_lock();
		gv_access(p->vol_sc->provider, -1, -1, 0);
		g_topology_unlock();
	
		G_VINUM_DEBUG(1, "rebuild of %s finished", p->name);
		gv_save_config(p->vinumconf);
		p->flags &= ~GV_PLEX_REBUILDING;
		p->synced = 0;
		/* Try to up all subdisks. */
		LIST_FOREACH(s, &p->subdisks, in_plex)
			gv_update_sd_state(s);
		gv_post_event(sc, GV_EVENT_SAVE_CONFIG, sc, NULL, 0, 0);
		gv_plex_flush(p); /* Flush out remaining rebuild BIOs. */
		return;
	}

	/* Send down next. It will determine if we need to itself. */
	gv_parity_request(p, flags, offset);
}
