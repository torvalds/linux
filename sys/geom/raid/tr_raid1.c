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
#include <sys/bio.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/kobj.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <geom/geom.h>
#include "geom/raid/g_raid.h"
#include "g_raid_tr_if.h"

SYSCTL_DECL(_kern_geom_raid_raid1);

#define RAID1_REBUILD_SLAB	(1 << 20) /* One transation in a rebuild */
static int g_raid1_rebuild_slab = RAID1_REBUILD_SLAB;
SYSCTL_UINT(_kern_geom_raid_raid1, OID_AUTO, rebuild_slab_size, CTLFLAG_RWTUN,
    &g_raid1_rebuild_slab, 0,
    "Amount of the disk to rebuild each read/write cycle of the rebuild.");

#define RAID1_REBUILD_FAIR_IO 20 /* use 1/x of the available I/O */
static int g_raid1_rebuild_fair_io = RAID1_REBUILD_FAIR_IO;
SYSCTL_UINT(_kern_geom_raid_raid1, OID_AUTO, rebuild_fair_io, CTLFLAG_RWTUN,
    &g_raid1_rebuild_fair_io, 0,
    "Fraction of the I/O bandwidth to use when disk busy for rebuild.");

#define RAID1_REBUILD_CLUSTER_IDLE 100
static int g_raid1_rebuild_cluster_idle = RAID1_REBUILD_CLUSTER_IDLE;
SYSCTL_UINT(_kern_geom_raid_raid1, OID_AUTO, rebuild_cluster_idle, CTLFLAG_RWTUN,
    &g_raid1_rebuild_cluster_idle, 0,
    "Number of slabs to do each time we trigger a rebuild cycle");

#define RAID1_REBUILD_META_UPDATE 1024 /* update meta data every 1GB or so */
static int g_raid1_rebuild_meta_update = RAID1_REBUILD_META_UPDATE;
SYSCTL_UINT(_kern_geom_raid_raid1, OID_AUTO, rebuild_meta_update, CTLFLAG_RWTUN,
    &g_raid1_rebuild_meta_update, 0,
    "When to update the meta data.");

static MALLOC_DEFINE(M_TR_RAID1, "tr_raid1_data", "GEOM_RAID RAID1 data");

#define TR_RAID1_NONE 0
#define TR_RAID1_REBUILD 1
#define TR_RAID1_RESYNC 2

#define TR_RAID1_F_DOING_SOME	0x1
#define TR_RAID1_F_LOCKED	0x2
#define TR_RAID1_F_ABORT	0x4

struct g_raid_tr_raid1_object {
	struct g_raid_tr_object	 trso_base;
	int			 trso_starting;
	int			 trso_stopping;
	int			 trso_type;
	int			 trso_recover_slabs; /* slabs before rest */
	int			 trso_fair_io;
	int			 trso_meta_update;
	int			 trso_flags;
	struct g_raid_subdisk	*trso_failed_sd; /* like per volume */
	void			*trso_buffer;	 /* Buffer space */
	struct bio		 trso_bio;
};

static g_raid_tr_taste_t g_raid_tr_taste_raid1;
static g_raid_tr_event_t g_raid_tr_event_raid1;
static g_raid_tr_start_t g_raid_tr_start_raid1;
static g_raid_tr_stop_t g_raid_tr_stop_raid1;
static g_raid_tr_iostart_t g_raid_tr_iostart_raid1;
static g_raid_tr_iodone_t g_raid_tr_iodone_raid1;
static g_raid_tr_kerneldump_t g_raid_tr_kerneldump_raid1;
static g_raid_tr_locked_t g_raid_tr_locked_raid1;
static g_raid_tr_idle_t g_raid_tr_idle_raid1;
static g_raid_tr_free_t g_raid_tr_free_raid1;

static kobj_method_t g_raid_tr_raid1_methods[] = {
	KOBJMETHOD(g_raid_tr_taste,	g_raid_tr_taste_raid1),
	KOBJMETHOD(g_raid_tr_event,	g_raid_tr_event_raid1),
	KOBJMETHOD(g_raid_tr_start,	g_raid_tr_start_raid1),
	KOBJMETHOD(g_raid_tr_stop,	g_raid_tr_stop_raid1),
	KOBJMETHOD(g_raid_tr_iostart,	g_raid_tr_iostart_raid1),
	KOBJMETHOD(g_raid_tr_iodone,	g_raid_tr_iodone_raid1),
	KOBJMETHOD(g_raid_tr_kerneldump, g_raid_tr_kerneldump_raid1),
	KOBJMETHOD(g_raid_tr_locked,	g_raid_tr_locked_raid1),
	KOBJMETHOD(g_raid_tr_idle,	g_raid_tr_idle_raid1),
	KOBJMETHOD(g_raid_tr_free,	g_raid_tr_free_raid1),
	{ 0, 0 }
};

static struct g_raid_tr_class g_raid_tr_raid1_class = {
	"RAID1",
	g_raid_tr_raid1_methods,
	sizeof(struct g_raid_tr_raid1_object),
	.trc_enable = 1,
	.trc_priority = 100,
	.trc_accept_unmapped = 1
};

static void g_raid_tr_raid1_rebuild_abort(struct g_raid_tr_object *tr);
static void g_raid_tr_raid1_maybe_rebuild(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd);

static int
g_raid_tr_taste_raid1(struct g_raid_tr_object *tr, struct g_raid_volume *vol)
{
	struct g_raid_tr_raid1_object *trs;

	trs = (struct g_raid_tr_raid1_object *)tr;
	if (tr->tro_volume->v_raid_level != G_RAID_VOLUME_RL_RAID1 ||
	    (tr->tro_volume->v_raid_level_qualifier != G_RAID_VOLUME_RLQ_R1SM &&
	     tr->tro_volume->v_raid_level_qualifier != G_RAID_VOLUME_RLQ_R1MM))
		return (G_RAID_TR_TASTE_FAIL);
	trs->trso_starting = 1;
	return (G_RAID_TR_TASTE_SUCCEED);
}

static int
g_raid_tr_update_state_raid1(struct g_raid_volume *vol,
    struct g_raid_subdisk *sd)
{
	struct g_raid_tr_raid1_object *trs;
	struct g_raid_softc *sc;
	struct g_raid_subdisk *tsd, *bestsd;
	u_int s;
	int i, na, ns;

	sc = vol->v_softc;
	trs = (struct g_raid_tr_raid1_object *)vol->v_tr;
	if (trs->trso_stopping &&
	    (trs->trso_flags & TR_RAID1_F_DOING_SOME) == 0)
		s = G_RAID_VOLUME_S_STOPPED;
	else if (trs->trso_starting)
		s = G_RAID_VOLUME_S_STARTING;
	else {
		/* Make sure we have at least one ACTIVE disk. */
		na = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_ACTIVE);
		if (na == 0) {
			/*
			 * Critical situation! We have no any active disk!
			 * Choose the best disk we have to make it active.
			 */
			bestsd = &vol->v_subdisks[0];
			for (i = 1; i < vol->v_disks_count; i++) {
				tsd = &vol->v_subdisks[i];
				if (tsd->sd_state > bestsd->sd_state)
					bestsd = tsd;
				else if (tsd->sd_state == bestsd->sd_state &&
				    (tsd->sd_state == G_RAID_SUBDISK_S_REBUILD ||
				     tsd->sd_state == G_RAID_SUBDISK_S_RESYNC) &&
				    tsd->sd_rebuild_pos > bestsd->sd_rebuild_pos)
					bestsd = tsd;
			}
			if (bestsd->sd_state >= G_RAID_SUBDISK_S_UNINITIALIZED) {
				/* We found reasonable candidate. */
				G_RAID_DEBUG1(1, sc,
				    "Promote subdisk %s:%d from %s to ACTIVE.",
				    vol->v_name, bestsd->sd_pos,
				    g_raid_subdisk_state2str(bestsd->sd_state));
				g_raid_change_subdisk_state(bestsd,
				    G_RAID_SUBDISK_S_ACTIVE);
				g_raid_write_metadata(sc,
				    vol, bestsd, bestsd->sd_disk);
			}
		}
		na = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_ACTIVE);
		ns = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_STALE) +
		     g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_RESYNC);
		if (na == vol->v_disks_count)
			s = G_RAID_VOLUME_S_OPTIMAL;
		else if (na + ns == vol->v_disks_count)
			s = G_RAID_VOLUME_S_SUBOPTIMAL;
		else if (na > 0)
			s = G_RAID_VOLUME_S_DEGRADED;
		else
			s = G_RAID_VOLUME_S_BROKEN;
		g_raid_tr_raid1_maybe_rebuild(vol->v_tr, sd);
	}
	if (s != vol->v_state) {
		g_raid_event_send(vol, G_RAID_VOLUME_S_ALIVE(s) ?
		    G_RAID_VOLUME_E_UP : G_RAID_VOLUME_E_DOWN,
		    G_RAID_EVENT_VOLUME);
		g_raid_change_volume_state(vol, s);
		if (!trs->trso_starting && !trs->trso_stopping)
			g_raid_write_metadata(sc, vol, NULL, NULL);
	}
	return (0);
}

static void
g_raid_tr_raid1_fail_disk(struct g_raid_softc *sc, struct g_raid_subdisk *sd,
    struct g_raid_disk *disk)
{
	/*
	 * We don't fail the last disk in the pack, since it still has decent
	 * data on it and that's better than failing the disk if it is the root
	 * file system.
	 *
	 * XXX should this be controlled via a tunable?  It makes sense for
	 * the volume that has / on it.  I can't think of a case where we'd
	 * want the volume to go away on this kind of event.
	 */
	if (g_raid_nsubdisks(sd->sd_volume, G_RAID_SUBDISK_S_ACTIVE) == 1 &&
	    g_raid_get_subdisk(sd->sd_volume, G_RAID_SUBDISK_S_ACTIVE) == sd)
		return;
	g_raid_fail_disk(sc, sd, disk);
}

static void
g_raid_tr_raid1_rebuild_some(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1_object *trs;
	struct g_raid_subdisk *sd, *good_sd;
	struct bio *bp;

	trs = (struct g_raid_tr_raid1_object *)tr;
	if (trs->trso_flags & TR_RAID1_F_DOING_SOME)
		return;
	sd = trs->trso_failed_sd;
	good_sd = g_raid_get_subdisk(sd->sd_volume, G_RAID_SUBDISK_S_ACTIVE);
	if (good_sd == NULL) {
		g_raid_tr_raid1_rebuild_abort(tr);
		return;
	}
	bp = &trs->trso_bio;
	memset(bp, 0, sizeof(*bp));
	bp->bio_offset = sd->sd_rebuild_pos;
	bp->bio_length = MIN(g_raid1_rebuild_slab,
	    sd->sd_size - sd->sd_rebuild_pos);
	bp->bio_data = trs->trso_buffer;
	bp->bio_cmd = BIO_READ;
	bp->bio_cflags = G_RAID_BIO_FLAG_SYNC;
	bp->bio_caller1 = good_sd;
	trs->trso_flags |= TR_RAID1_F_DOING_SOME;
	trs->trso_flags |= TR_RAID1_F_LOCKED;
	g_raid_lock_range(sd->sd_volume,	/* Lock callback starts I/O */
	   bp->bio_offset, bp->bio_length, NULL, bp);
}

static void
g_raid_tr_raid1_rebuild_done(struct g_raid_tr_raid1_object *trs)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;

	vol = trs->trso_base.tro_volume;
	sd = trs->trso_failed_sd;
	g_raid_write_metadata(vol->v_softc, vol, sd, sd->sd_disk);
	free(trs->trso_buffer, M_TR_RAID1);
	trs->trso_buffer = NULL;
	trs->trso_flags &= ~TR_RAID1_F_DOING_SOME;
	trs->trso_type = TR_RAID1_NONE;
	trs->trso_recover_slabs = 0;
	trs->trso_failed_sd = NULL;
	g_raid_tr_update_state_raid1(vol, NULL);
}

static void
g_raid_tr_raid1_rebuild_finish(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1_object *trs;
	struct g_raid_subdisk *sd;

	trs = (struct g_raid_tr_raid1_object *)tr;
	sd = trs->trso_failed_sd;
	G_RAID_DEBUG1(0, tr->tro_volume->v_softc,
	    "Subdisk %s:%d-%s rebuild completed.",
	    sd->sd_volume->v_name, sd->sd_pos,
	    sd->sd_disk ? g_raid_get_diskname(sd->sd_disk) : "[none]");
	g_raid_change_subdisk_state(sd, G_RAID_SUBDISK_S_ACTIVE);
	sd->sd_rebuild_pos = 0;
	g_raid_tr_raid1_rebuild_done(trs);
}

static void
g_raid_tr_raid1_rebuild_abort(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1_object *trs;
	struct g_raid_subdisk *sd;
	struct g_raid_volume *vol;
	off_t len;

	vol = tr->tro_volume;
	trs = (struct g_raid_tr_raid1_object *)tr;
	sd = trs->trso_failed_sd;
	if (trs->trso_flags & TR_RAID1_F_DOING_SOME) {
		G_RAID_DEBUG1(1, vol->v_softc,
		    "Subdisk %s:%d-%s rebuild is aborting.",
		    sd->sd_volume->v_name, sd->sd_pos,
		    sd->sd_disk ? g_raid_get_diskname(sd->sd_disk) : "[none]");
		trs->trso_flags |= TR_RAID1_F_ABORT;
	} else {
		G_RAID_DEBUG1(0, vol->v_softc,
		    "Subdisk %s:%d-%s rebuild aborted.",
		    sd->sd_volume->v_name, sd->sd_pos,
		    sd->sd_disk ? g_raid_get_diskname(sd->sd_disk) : "[none]");
		trs->trso_flags &= ~TR_RAID1_F_ABORT;
		if (trs->trso_flags & TR_RAID1_F_LOCKED) {
			trs->trso_flags &= ~TR_RAID1_F_LOCKED;
			len = MIN(g_raid1_rebuild_slab,
			    sd->sd_size - sd->sd_rebuild_pos);
			g_raid_unlock_range(tr->tro_volume,
			    sd->sd_rebuild_pos, len);
		}
		g_raid_tr_raid1_rebuild_done(trs);
	}
}

static void
g_raid_tr_raid1_rebuild_start(struct g_raid_tr_object *tr)
{
	struct g_raid_volume *vol;
	struct g_raid_tr_raid1_object *trs;
	struct g_raid_subdisk *sd, *fsd;

	vol = tr->tro_volume;
	trs = (struct g_raid_tr_raid1_object *)tr;
	if (trs->trso_failed_sd) {
		G_RAID_DEBUG1(1, vol->v_softc,
		    "Already rebuild in start rebuild. pos %jd\n",
		    (intmax_t)trs->trso_failed_sd->sd_rebuild_pos);
		return;
	}
	sd = g_raid_get_subdisk(vol, G_RAID_SUBDISK_S_ACTIVE);
	if (sd == NULL) {
		G_RAID_DEBUG1(1, vol->v_softc,
		    "No active disk to rebuild.  night night.");
		return;
	}
	fsd = g_raid_get_subdisk(vol, G_RAID_SUBDISK_S_RESYNC);
	if (fsd == NULL)
		fsd = g_raid_get_subdisk(vol, G_RAID_SUBDISK_S_REBUILD);
	if (fsd == NULL) {
		fsd = g_raid_get_subdisk(vol, G_RAID_SUBDISK_S_STALE);
		if (fsd != NULL) {
			fsd->sd_rebuild_pos = 0;
			g_raid_change_subdisk_state(fsd,
			    G_RAID_SUBDISK_S_RESYNC);
			g_raid_write_metadata(vol->v_softc, vol, fsd, NULL);
		} else {
			fsd = g_raid_get_subdisk(vol,
			    G_RAID_SUBDISK_S_UNINITIALIZED);
			if (fsd == NULL)
				fsd = g_raid_get_subdisk(vol,
				    G_RAID_SUBDISK_S_NEW);
			if (fsd != NULL) {
				fsd->sd_rebuild_pos = 0;
				g_raid_change_subdisk_state(fsd,
				    G_RAID_SUBDISK_S_REBUILD);
				g_raid_write_metadata(vol->v_softc,
				    vol, fsd, NULL);
			}
		}
	}
	if (fsd == NULL) {
		G_RAID_DEBUG1(1, vol->v_softc,
		    "No failed disk to rebuild.  night night.");
		return;
	}
	trs->trso_failed_sd = fsd;
	G_RAID_DEBUG1(0, vol->v_softc,
	    "Subdisk %s:%d-%s rebuild start at %jd.",
	    fsd->sd_volume->v_name, fsd->sd_pos,
	    fsd->sd_disk ? g_raid_get_diskname(fsd->sd_disk) : "[none]",
	    trs->trso_failed_sd->sd_rebuild_pos);
	trs->trso_type = TR_RAID1_REBUILD;
	trs->trso_buffer = malloc(g_raid1_rebuild_slab, M_TR_RAID1, M_WAITOK);
	trs->trso_meta_update = g_raid1_rebuild_meta_update;
	g_raid_tr_raid1_rebuild_some(tr);
}


static void
g_raid_tr_raid1_maybe_rebuild(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd)
{
	struct g_raid_volume *vol;
	struct g_raid_tr_raid1_object *trs;
	int na, nr;
	
	/*
	 * If we're stopping, don't do anything.  If we don't have at least one
	 * good disk and one bad disk, we don't do anything.  And if there's a
	 * 'good disk' stored in the trs, then we're in progress and we punt.
	 * If we make it past all these checks, we need to rebuild.
	 */
	vol = tr->tro_volume;
	trs = (struct g_raid_tr_raid1_object *)tr;
	if (trs->trso_stopping)
		return;
	na = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_ACTIVE);
	nr = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_REBUILD) +
	    g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_RESYNC);
	switch(trs->trso_type) {
	case TR_RAID1_NONE:
		if (na == 0)
			return;
		if (nr == 0) {
			nr = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_NEW) +
			    g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_STALE) +
			    g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_UNINITIALIZED);
			if (nr == 0)
				return;
		}
		g_raid_tr_raid1_rebuild_start(tr);
		break;
	case TR_RAID1_REBUILD:
		if (na == 0 || nr == 0 || trs->trso_failed_sd == sd)
			g_raid_tr_raid1_rebuild_abort(tr);
		break;
	case TR_RAID1_RESYNC:
		break;
	}
}

static int
g_raid_tr_event_raid1(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd, u_int event)
{

	g_raid_tr_update_state_raid1(tr->tro_volume, sd);
	return (0);
}

static int
g_raid_tr_start_raid1(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1_object *trs;
	struct g_raid_volume *vol;

	trs = (struct g_raid_tr_raid1_object *)tr;
	vol = tr->tro_volume;
	trs->trso_starting = 0;
	g_raid_tr_update_state_raid1(vol, NULL);
	return (0);
}

static int
g_raid_tr_stop_raid1(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1_object *trs;
	struct g_raid_volume *vol;

	trs = (struct g_raid_tr_raid1_object *)tr;
	vol = tr->tro_volume;
	trs->trso_starting = 0;
	trs->trso_stopping = 1;
	g_raid_tr_update_state_raid1(vol, NULL);
	return (0);
}

/*
 * Select the disk to read from.  Take into account: subdisk state, running
 * error recovery, average disk load, head position and possible cache hits.
 */
#define ABS(x)		(((x) >= 0) ? (x) : (-(x)))
static struct g_raid_subdisk *
g_raid_tr_raid1_select_read_disk(struct g_raid_volume *vol, struct bio *bp,
    u_int mask)
{
	struct g_raid_subdisk *sd, *best;
	int i, prio, bestprio;

	best = NULL;
	bestprio = INT_MAX;
	for (i = 0; i < vol->v_disks_count; i++) {
		sd = &vol->v_subdisks[i];
		if (sd->sd_state != G_RAID_SUBDISK_S_ACTIVE &&
		    ((sd->sd_state != G_RAID_SUBDISK_S_REBUILD &&
		      sd->sd_state != G_RAID_SUBDISK_S_RESYNC) ||
		     bp->bio_offset + bp->bio_length > sd->sd_rebuild_pos))
			continue;
		if ((mask & (1 << i)) != 0)
			continue;
		prio = G_RAID_SUBDISK_LOAD(sd);
		prio += min(sd->sd_recovery, 255) << 22;
		prio += (G_RAID_SUBDISK_S_ACTIVE - sd->sd_state) << 16;
		/* If disk head is precisely in position - highly prefer it. */
		if (G_RAID_SUBDISK_POS(sd) == bp->bio_offset)
			prio -= 2 * G_RAID_SUBDISK_LOAD_SCALE;
		else
		/* If disk head is close to position - prefer it. */
		if (ABS(G_RAID_SUBDISK_POS(sd) - bp->bio_offset) <
		    G_RAID_SUBDISK_TRACK_SIZE)
			prio -= 1 * G_RAID_SUBDISK_LOAD_SCALE;
		if (prio < bestprio) {
			best = sd;
			bestprio = prio;
		}
	}
	return (best);
}

static void
g_raid_tr_iostart_raid1_read(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_subdisk *sd;
	struct bio *cbp;

	sd = g_raid_tr_raid1_select_read_disk(tr->tro_volume, bp, 0);
	KASSERT(sd != NULL, ("No active disks in volume %s.",
		tr->tro_volume->v_name));

	cbp = g_clone_bio(bp);
	if (cbp == NULL) {
		g_raid_iodone(bp, ENOMEM);
		return;
	}

	g_raid_subdisk_iostart(sd, cbp);
}

static void
g_raid_tr_iostart_raid1_write(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct bio_queue_head queue;
	struct bio *cbp;
	int i;

	vol = tr->tro_volume;

	/*
	 * Allocate all bios before sending any request, so we can return
	 * ENOMEM in nice and clean way.
	 */
	bioq_init(&queue);
	for (i = 0; i < vol->v_disks_count; i++) {
		sd = &vol->v_subdisks[i];
		switch (sd->sd_state) {
		case G_RAID_SUBDISK_S_ACTIVE:
			break;
		case G_RAID_SUBDISK_S_REBUILD:
			/*
			 * When rebuilding, only part of this subdisk is
			 * writable, the rest will be written as part of the
			 * that process.
			 */
			if (bp->bio_offset >= sd->sd_rebuild_pos)
				continue;
			break;
		case G_RAID_SUBDISK_S_STALE:
		case G_RAID_SUBDISK_S_RESYNC:
			/*
			 * Resyncing still writes on the theory that the
			 * resync'd disk is very close and writing it will
			 * keep it that way better if we keep up while
			 * resyncing.
			 */
			break;
		default:
			continue;
		}
		cbp = g_clone_bio(bp);
		if (cbp == NULL)
			goto failure;
		cbp->bio_caller1 = sd;
		bioq_insert_tail(&queue, cbp);
	}
	while ((cbp = bioq_takefirst(&queue)) != NULL) {
		sd = cbp->bio_caller1;
		cbp->bio_caller1 = NULL;
		g_raid_subdisk_iostart(sd, cbp);
	}
	return;
failure:
	while ((cbp = bioq_takefirst(&queue)) != NULL)
		g_destroy_bio(cbp);
	if (bp->bio_error == 0)
		bp->bio_error = ENOMEM;
	g_raid_iodone(bp, bp->bio_error);
}

static void
g_raid_tr_iostart_raid1(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_volume *vol;
	struct g_raid_tr_raid1_object *trs;

	vol = tr->tro_volume;
	trs = (struct g_raid_tr_raid1_object *)tr;
	if (vol->v_state != G_RAID_VOLUME_S_OPTIMAL &&
	    vol->v_state != G_RAID_VOLUME_S_SUBOPTIMAL &&
	    vol->v_state != G_RAID_VOLUME_S_DEGRADED) {
		g_raid_iodone(bp, EIO);
		return;
	}
	/*
	 * If we're rebuilding, squeeze in rebuild activity every so often,
	 * even when the disk is busy.  Be sure to only count real I/O
	 * to the disk.  All 'SPECIAL' I/O is traffic generated to the disk
	 * by this module.
	 */
	if (trs->trso_failed_sd != NULL &&
	    !(bp->bio_cflags & G_RAID_BIO_FLAG_SPECIAL)) {
		/* Make this new or running now round short. */
		trs->trso_recover_slabs = 0;
		if (--trs->trso_fair_io <= 0) {
			trs->trso_fair_io = g_raid1_rebuild_fair_io;
			g_raid_tr_raid1_rebuild_some(tr);
		}
	}
	switch (bp->bio_cmd) {
	case BIO_READ:
		g_raid_tr_iostart_raid1_read(tr, bp);
		break;
	case BIO_WRITE:
	case BIO_DELETE:
		g_raid_tr_iostart_raid1_write(tr, bp);
		break;
	case BIO_FLUSH:
		g_raid_tr_flush_common(tr, bp);
		break;
	default:
		KASSERT(1 == 0, ("Invalid command here: %u (volume=%s)",
		    bp->bio_cmd, vol->v_name));
		break;
	}
}

static void
g_raid_tr_iodone_raid1(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd, struct bio *bp)
{
	struct bio *cbp;
	struct g_raid_subdisk *nsd;
	struct g_raid_volume *vol;
	struct bio *pbp;
	struct g_raid_tr_raid1_object *trs;
	uintptr_t *mask;
	int error, do_write;

	trs = (struct g_raid_tr_raid1_object *)tr;
	vol = tr->tro_volume;
	if (bp->bio_cflags & G_RAID_BIO_FLAG_SYNC) {
		/*
		 * This operation is part of a rebuild or resync operation.
		 * See what work just got done, then schedule the next bit of
		 * work, if any.  Rebuild/resync is done a little bit at a
		 * time.  Either when a timeout happens, or after we get a
		 * bunch of I/Os to the disk (to make sure an active system
		 * will complete in a sane amount of time).
		 *
		 * We are setup to do differing amounts of work for each of
		 * these cases.  so long as the slabs is smallish (less than
		 * 50 or so, I'd guess, but that's just a WAG), we shouldn't
		 * have any bio starvation issues.  For active disks, we do
		 * 5MB of data, for inactive ones, we do 50MB.
		 */
		if (trs->trso_type == TR_RAID1_REBUILD) {
			if (bp->bio_cmd == BIO_READ) {

				/* Immediately abort rebuild, if requested. */
				if (trs->trso_flags & TR_RAID1_F_ABORT) {
					trs->trso_flags &= ~TR_RAID1_F_DOING_SOME;
					g_raid_tr_raid1_rebuild_abort(tr);
					return;
				}

				/* On read error, skip and cross fingers. */
				if (bp->bio_error != 0) {
					G_RAID_LOGREQ(0, bp,
					    "Read error during rebuild (%d), "
					    "possible data loss!",
					    bp->bio_error);
					goto rebuild_round_done;
				}

				/*
				 * The read operation finished, queue the
				 * write and get out.
				 */
				G_RAID_LOGREQ(4, bp, "rebuild read done. %d",
				    bp->bio_error);
				bp->bio_cmd = BIO_WRITE;
				bp->bio_cflags = G_RAID_BIO_FLAG_SYNC;
				G_RAID_LOGREQ(4, bp, "Queueing rebuild write.");
				g_raid_subdisk_iostart(trs->trso_failed_sd, bp);
			} else {
				/*
				 * The write operation just finished.  Do
				 * another.  We keep cloning the master bio
				 * since it has the right buffers allocated to
				 * it.
				 */
				G_RAID_LOGREQ(4, bp,
				    "rebuild write done. Error %d",
				    bp->bio_error);
				nsd = trs->trso_failed_sd;
				if (bp->bio_error != 0 ||
				    trs->trso_flags & TR_RAID1_F_ABORT) {
					if ((trs->trso_flags &
					    TR_RAID1_F_ABORT) == 0) {
						g_raid_tr_raid1_fail_disk(sd->sd_softc,
						    nsd, nsd->sd_disk);
					}
					trs->trso_flags &= ~TR_RAID1_F_DOING_SOME;
					g_raid_tr_raid1_rebuild_abort(tr);
					return;
				}
rebuild_round_done:
				nsd = trs->trso_failed_sd;
				trs->trso_flags &= ~TR_RAID1_F_LOCKED;
				g_raid_unlock_range(sd->sd_volume,
				    bp->bio_offset, bp->bio_length);
				nsd->sd_rebuild_pos += bp->bio_length;
				if (nsd->sd_rebuild_pos >= nsd->sd_size) {
					g_raid_tr_raid1_rebuild_finish(tr);
					return;
				}

				/* Abort rebuild if we are stopping */
				if (trs->trso_stopping) {
					trs->trso_flags &= ~TR_RAID1_F_DOING_SOME;
					g_raid_tr_raid1_rebuild_abort(tr);
					return;
				}

				if (--trs->trso_meta_update <= 0) {
					g_raid_write_metadata(vol->v_softc,
					    vol, nsd, nsd->sd_disk);
					trs->trso_meta_update =
					    g_raid1_rebuild_meta_update;
				}
				trs->trso_flags &= ~TR_RAID1_F_DOING_SOME;
				if (--trs->trso_recover_slabs <= 0)
					return;
				g_raid_tr_raid1_rebuild_some(tr);
			}
		} else if (trs->trso_type == TR_RAID1_RESYNC) {
			/*
			 * read good sd, read bad sd in parallel.  when both
			 * done, compare the buffers.  write good to the bad
			 * if different.  do the next bit of work.
			 */
			panic("Somehow, we think we're doing a resync");
		}
		return;
	}
	pbp = bp->bio_parent;
	pbp->bio_inbed++;
	if (bp->bio_cmd == BIO_READ && bp->bio_error != 0) {
		/*
		 * Read failed on first drive.  Retry the read error on
		 * another disk drive, if available, before erroring out the
		 * read.
		 */
		sd->sd_disk->d_read_errs++;
		G_RAID_LOGREQ(0, bp,
		    "Read error (%d), %d read errors total",
		    bp->bio_error, sd->sd_disk->d_read_errs);

		/*
		 * If there are too many read errors, we move to degraded.
		 * XXX Do we want to FAIL the drive (eg, make the user redo
		 * everything to get it back in sync), or just degrade the
		 * drive, which kicks off a resync?
		 */
		do_write = 1;
		if (sd->sd_disk->d_read_errs > g_raid_read_err_thresh) {
			g_raid_tr_raid1_fail_disk(sd->sd_softc, sd, sd->sd_disk);
			if (pbp->bio_children == 1)
				do_write = 0;
		}

		/*
		 * Find the other disk, and try to do the I/O to it.
		 */
		mask = (uintptr_t *)(&pbp->bio_driver2);
		if (pbp->bio_children == 1) {
			/* Save original subdisk. */
			pbp->bio_driver1 = do_write ? sd : NULL;
			*mask = 0;
		}
		*mask |= 1 << sd->sd_pos;
		nsd = g_raid_tr_raid1_select_read_disk(vol, pbp, *mask);
		if (nsd != NULL && (cbp = g_clone_bio(pbp)) != NULL) {
			g_destroy_bio(bp);
			G_RAID_LOGREQ(2, cbp, "Retrying read from %d",
			    nsd->sd_pos);
			if (pbp->bio_children == 2 && do_write) {
				sd->sd_recovery++;
				cbp->bio_caller1 = nsd;
				pbp->bio_pflags = G_RAID_BIO_FLAG_LOCKED;
				/* Lock callback starts I/O */
				g_raid_lock_range(sd->sd_volume,
				    cbp->bio_offset, cbp->bio_length, pbp, cbp);
			} else {
				g_raid_subdisk_iostart(nsd, cbp);
			}
			return;
		}
		/*
		 * We can't retry.  Return the original error by falling
		 * through.  This will happen when there's only one good disk.
		 * We don't need to fail the raid, since its actual state is
		 * based on the state of the subdisks.
		 */
		G_RAID_LOGREQ(2, bp, "Couldn't retry read, failing it");
	}
	if (bp->bio_cmd == BIO_READ &&
	    bp->bio_error == 0 &&
	    pbp->bio_children > 1 &&
	    pbp->bio_driver1 != NULL) {
		/*
		 * If it was a read, and bio_children is >1, then we just
		 * recovered the data from the second drive.  We should try to
		 * write that data to the first drive if sector remapping is
		 * enabled.  A write should put the data in a new place on the
		 * disk, remapping the bad sector.  Do we need to do that by
		 * queueing a request to the main worker thread?  It doesn't
		 * affect the return code of this current read, and can be
		 * done at our leisure.  However, to make the code simpler, it
		 * is done synchronously.
		 */
		G_RAID_LOGREQ(3, bp, "Recovered data from other drive");
		cbp = g_clone_bio(pbp);
		if (cbp != NULL) {
			g_destroy_bio(bp);
			cbp->bio_cmd = BIO_WRITE;
			cbp->bio_cflags = G_RAID_BIO_FLAG_REMAP;
			G_RAID_LOGREQ(2, cbp,
			    "Attempting bad sector remap on failing drive.");
			g_raid_subdisk_iostart(pbp->bio_driver1, cbp);
			return;
		}
	}
	if (pbp->bio_pflags & G_RAID_BIO_FLAG_LOCKED) {
		/*
		 * We're done with a recovery, mark the range as unlocked.
		 * For any write errors, we aggressively fail the disk since
		 * there was both a READ and a WRITE error at this location.
		 * Both types of errors generally indicates the drive is on
		 * the verge of total failure anyway.  Better to stop trusting
		 * it now.  However, we need to reset error to 0 in that case
		 * because we're not failing the original I/O which succeeded.
		 */
		if (bp->bio_cmd == BIO_WRITE && bp->bio_error) {
			G_RAID_LOGREQ(0, bp, "Remap write failed: "
			    "failing subdisk.");
			g_raid_tr_raid1_fail_disk(sd->sd_softc, sd, sd->sd_disk);
			bp->bio_error = 0;
		}
		if (pbp->bio_driver1 != NULL) {
			((struct g_raid_subdisk *)pbp->bio_driver1)
			    ->sd_recovery--;
		}
		G_RAID_LOGREQ(2, bp, "REMAP done %d.", bp->bio_error);
		g_raid_unlock_range(sd->sd_volume, bp->bio_offset,
		    bp->bio_length);
	}
	if (pbp->bio_cmd != BIO_READ) {
		if (pbp->bio_inbed == 1 || pbp->bio_error != 0)
			pbp->bio_error = bp->bio_error;
		if (pbp->bio_cmd == BIO_WRITE && bp->bio_error != 0) {
			G_RAID_LOGREQ(0, bp, "Write failed: failing subdisk.");
			g_raid_tr_raid1_fail_disk(sd->sd_softc, sd, sd->sd_disk);
		}
		error = pbp->bio_error;
	} else
		error = bp->bio_error;
	g_destroy_bio(bp);
	if (pbp->bio_children == pbp->bio_inbed) {
		pbp->bio_completed = pbp->bio_length;
		g_raid_iodone(pbp, error);
	}
}

static int
g_raid_tr_kerneldump_raid1(struct g_raid_tr_object *tr,
    void *virtual, vm_offset_t physical, off_t offset, size_t length)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	int error, i, ok;

	vol = tr->tro_volume;
	error = 0;
	ok = 0;
	for (i = 0; i < vol->v_disks_count; i++) {
		sd = &vol->v_subdisks[i];
		switch (sd->sd_state) {
		case G_RAID_SUBDISK_S_ACTIVE:
			break;
		case G_RAID_SUBDISK_S_REBUILD:
			/*
			 * When rebuilding, only part of this subdisk is
			 * writable, the rest will be written as part of the
			 * that process.
			 */
			if (offset >= sd->sd_rebuild_pos)
				continue;
			break;
		case G_RAID_SUBDISK_S_STALE:
		case G_RAID_SUBDISK_S_RESYNC:
			/*
			 * Resyncing still writes on the theory that the
			 * resync'd disk is very close and writing it will
			 * keep it that way better if we keep up while
			 * resyncing.
			 */
			break;
		default:
			continue;
		}
		error = g_raid_subdisk_kerneldump(sd,
		    virtual, physical, offset, length);
		if (error == 0)
			ok++;
	}
	return (ok > 0 ? 0 : error);
}

static int
g_raid_tr_locked_raid1(struct g_raid_tr_object *tr, void *argp)
{
	struct bio *bp;
	struct g_raid_subdisk *sd;

	bp = (struct bio *)argp;
	sd = (struct g_raid_subdisk *)bp->bio_caller1;
	g_raid_subdisk_iostart(sd, bp);

	return (0);
}

static int
g_raid_tr_idle_raid1(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1_object *trs;

	trs = (struct g_raid_tr_raid1_object *)tr;
	trs->trso_fair_io = g_raid1_rebuild_fair_io;
	trs->trso_recover_slabs = g_raid1_rebuild_cluster_idle;
	if (trs->trso_type == TR_RAID1_REBUILD)
		g_raid_tr_raid1_rebuild_some(tr);
	return (0);
}

static int
g_raid_tr_free_raid1(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1_object *trs;

	trs = (struct g_raid_tr_raid1_object *)tr;

	if (trs->trso_buffer != NULL) {
		free(trs->trso_buffer, M_TR_RAID1);
		trs->trso_buffer = NULL;
	}
	return (0);
}

G_RAID_TR_DECLARE(raid1, "RAID1");
