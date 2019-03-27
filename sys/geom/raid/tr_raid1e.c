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

#define N	2

SYSCTL_DECL(_kern_geom_raid_raid1e);

#define RAID1E_REBUILD_SLAB	(1 << 20) /* One transation in a rebuild */
static int g_raid1e_rebuild_slab = RAID1E_REBUILD_SLAB;
SYSCTL_UINT(_kern_geom_raid_raid1e, OID_AUTO, rebuild_slab_size, CTLFLAG_RWTUN,
    &g_raid1e_rebuild_slab, 0,
    "Amount of the disk to rebuild each read/write cycle of the rebuild.");

#define RAID1E_REBUILD_FAIR_IO 20 /* use 1/x of the available I/O */
static int g_raid1e_rebuild_fair_io = RAID1E_REBUILD_FAIR_IO;
SYSCTL_UINT(_kern_geom_raid_raid1e, OID_AUTO, rebuild_fair_io, CTLFLAG_RWTUN,
    &g_raid1e_rebuild_fair_io, 0,
    "Fraction of the I/O bandwidth to use when disk busy for rebuild.");

#define RAID1E_REBUILD_CLUSTER_IDLE 100
static int g_raid1e_rebuild_cluster_idle = RAID1E_REBUILD_CLUSTER_IDLE;
SYSCTL_UINT(_kern_geom_raid_raid1e, OID_AUTO, rebuild_cluster_idle, CTLFLAG_RWTUN,
    &g_raid1e_rebuild_cluster_idle, 0,
    "Number of slabs to do each time we trigger a rebuild cycle");

#define RAID1E_REBUILD_META_UPDATE 1024 /* update meta data every 1GB or so */
static int g_raid1e_rebuild_meta_update = RAID1E_REBUILD_META_UPDATE;
SYSCTL_UINT(_kern_geom_raid_raid1e, OID_AUTO, rebuild_meta_update, CTLFLAG_RWTUN,
    &g_raid1e_rebuild_meta_update, 0,
    "When to update the meta data.");

static MALLOC_DEFINE(M_TR_RAID1E, "tr_raid1e_data", "GEOM_RAID RAID1E data");

#define TR_RAID1E_NONE 0
#define TR_RAID1E_REBUILD 1
#define TR_RAID1E_RESYNC 2

#define TR_RAID1E_F_DOING_SOME	0x1
#define TR_RAID1E_F_LOCKED	0x2
#define TR_RAID1E_F_ABORT	0x4

struct g_raid_tr_raid1e_object {
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
	off_t			 trso_lock_pos; /* Locked range start. */
	off_t			 trso_lock_len; /* Locked range length. */
	struct bio		 trso_bio;
};

static g_raid_tr_taste_t g_raid_tr_taste_raid1e;
static g_raid_tr_event_t g_raid_tr_event_raid1e;
static g_raid_tr_start_t g_raid_tr_start_raid1e;
static g_raid_tr_stop_t g_raid_tr_stop_raid1e;
static g_raid_tr_iostart_t g_raid_tr_iostart_raid1e;
static g_raid_tr_iodone_t g_raid_tr_iodone_raid1e;
static g_raid_tr_kerneldump_t g_raid_tr_kerneldump_raid1e;
static g_raid_tr_locked_t g_raid_tr_locked_raid1e;
static g_raid_tr_idle_t g_raid_tr_idle_raid1e;
static g_raid_tr_free_t g_raid_tr_free_raid1e;

static kobj_method_t g_raid_tr_raid1e_methods[] = {
	KOBJMETHOD(g_raid_tr_taste,	g_raid_tr_taste_raid1e),
	KOBJMETHOD(g_raid_tr_event,	g_raid_tr_event_raid1e),
	KOBJMETHOD(g_raid_tr_start,	g_raid_tr_start_raid1e),
	KOBJMETHOD(g_raid_tr_stop,	g_raid_tr_stop_raid1e),
	KOBJMETHOD(g_raid_tr_iostart,	g_raid_tr_iostart_raid1e),
	KOBJMETHOD(g_raid_tr_iodone,	g_raid_tr_iodone_raid1e),
	KOBJMETHOD(g_raid_tr_kerneldump, g_raid_tr_kerneldump_raid1e),
	KOBJMETHOD(g_raid_tr_locked,	g_raid_tr_locked_raid1e),
	KOBJMETHOD(g_raid_tr_idle,	g_raid_tr_idle_raid1e),
	KOBJMETHOD(g_raid_tr_free,	g_raid_tr_free_raid1e),
	{ 0, 0 }
};

static struct g_raid_tr_class g_raid_tr_raid1e_class = {
	"RAID1E",
	g_raid_tr_raid1e_methods,
	sizeof(struct g_raid_tr_raid1e_object),
	.trc_enable = 1,
	.trc_priority = 200,
	.trc_accept_unmapped = 1
};

static void g_raid_tr_raid1e_rebuild_abort(struct g_raid_tr_object *tr);
static void g_raid_tr_raid1e_maybe_rebuild(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd);
static int g_raid_tr_raid1e_select_read_disk(struct g_raid_volume *vol,
    int no, off_t off, off_t len, u_int mask);

static inline void
V2P(struct g_raid_volume *vol, off_t virt,
    int *disk, off_t *offset, off_t *start)
{
	off_t nstrip;
	u_int strip_size;

	strip_size = vol->v_strip_size;
	/* Strip number. */
	nstrip = virt / strip_size;
	/* Start position in strip. */
	*start = virt % strip_size;
	/* Disk number. */
	*disk = (nstrip * N) % vol->v_disks_count;
	/* Strip start position in disk. */
	*offset = ((nstrip * N) / vol->v_disks_count) * strip_size;
}

static inline void
P2V(struct g_raid_volume *vol, int disk, off_t offset,
    off_t *virt, int *copy)
{
	off_t nstrip, start;
	u_int strip_size;

	strip_size = vol->v_strip_size;
	/* Start position in strip. */
	start = offset % strip_size;
	/* Physical strip number. */
	nstrip = (offset / strip_size) * vol->v_disks_count + disk;
	/* Number of physical strip (copy) inside virtual strip. */
	*copy = nstrip % N;
	/* Offset in virtual space. */
	*virt = (nstrip / N) * strip_size + start;
}

static int
g_raid_tr_taste_raid1e(struct g_raid_tr_object *tr, struct g_raid_volume *vol)
{
	struct g_raid_tr_raid1e_object *trs;

	trs = (struct g_raid_tr_raid1e_object *)tr;
	if (tr->tro_volume->v_raid_level != G_RAID_VOLUME_RL_RAID1E ||
	    tr->tro_volume->v_raid_level_qualifier != G_RAID_VOLUME_RLQ_R1EA)
		return (G_RAID_TR_TASTE_FAIL);
	trs->trso_starting = 1;
	return (G_RAID_TR_TASTE_SUCCEED);
}

static int
g_raid_tr_update_state_raid1e_even(struct g_raid_volume *vol)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd, *bestsd, *worstsd;
	int i, j, state, sstate;

	sc = vol->v_softc;
	state = G_RAID_VOLUME_S_OPTIMAL;
	for (i = 0; i < vol->v_disks_count / N; i++) {
		bestsd = &vol->v_subdisks[i * N];
		for (j = 1; j < N; j++) {
			sd = &vol->v_subdisks[i * N + j];
			if (sd->sd_state > bestsd->sd_state)
				bestsd = sd;
			else if (sd->sd_state == bestsd->sd_state &&
			    (sd->sd_state == G_RAID_SUBDISK_S_REBUILD ||
			     sd->sd_state == G_RAID_SUBDISK_S_RESYNC) &&
			    sd->sd_rebuild_pos > bestsd->sd_rebuild_pos)
				bestsd = sd;
		}
		if (bestsd->sd_state >= G_RAID_SUBDISK_S_UNINITIALIZED &&
		    bestsd->sd_state != G_RAID_SUBDISK_S_ACTIVE) {
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
		worstsd = &vol->v_subdisks[i * N];
		for (j = 1; j < N; j++) {
			sd = &vol->v_subdisks[i * N + j];
			if (sd->sd_state < worstsd->sd_state)
				worstsd = sd;
		}
		if (worstsd->sd_state == G_RAID_SUBDISK_S_ACTIVE)
			sstate = G_RAID_VOLUME_S_OPTIMAL;
		else if (worstsd->sd_state >= G_RAID_SUBDISK_S_STALE)
			sstate = G_RAID_VOLUME_S_SUBOPTIMAL;
		else if (bestsd->sd_state == G_RAID_SUBDISK_S_ACTIVE)
			sstate = G_RAID_VOLUME_S_DEGRADED;
		else
			sstate = G_RAID_VOLUME_S_BROKEN;
		if (sstate < state)
			state = sstate;
	}
	return (state);
}

static int
g_raid_tr_update_state_raid1e_odd(struct g_raid_volume *vol)
{
	struct g_raid_softc *sc;
	struct g_raid_subdisk *sd, *bestsd, *worstsd;
	int i, j, state, sstate;

	sc = vol->v_softc;
	if (g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_ACTIVE) ==
	    vol->v_disks_count)
		return (G_RAID_VOLUME_S_OPTIMAL);
	for (i = 0; i < vol->v_disks_count; i++) {
		sd = &vol->v_subdisks[i];
		if (sd->sd_state == G_RAID_SUBDISK_S_UNINITIALIZED) {
			/* We found reasonable candidate. */
			G_RAID_DEBUG1(1, sc,
			    "Promote subdisk %s:%d from %s to STALE.",
			    vol->v_name, sd->sd_pos,
			    g_raid_subdisk_state2str(sd->sd_state));
			g_raid_change_subdisk_state(sd,
			    G_RAID_SUBDISK_S_STALE);
			g_raid_write_metadata(sc, vol, sd, sd->sd_disk);
		}
	}
	state = G_RAID_VOLUME_S_OPTIMAL;
	for (i = 0; i < vol->v_disks_count; i++) {
		bestsd = &vol->v_subdisks[i];
		worstsd = &vol->v_subdisks[i];
		for (j = 1; j < N; j++) {
			sd = &vol->v_subdisks[(i + j) % vol->v_disks_count];
			if (sd->sd_state > bestsd->sd_state)
				bestsd = sd;
			else if (sd->sd_state == bestsd->sd_state &&
			    (sd->sd_state == G_RAID_SUBDISK_S_REBUILD ||
			     sd->sd_state == G_RAID_SUBDISK_S_RESYNC) &&
			    sd->sd_rebuild_pos > bestsd->sd_rebuild_pos)
				bestsd = sd;
			if (sd->sd_state < worstsd->sd_state)
				worstsd = sd;
		}
		if (worstsd->sd_state == G_RAID_SUBDISK_S_ACTIVE)
			sstate = G_RAID_VOLUME_S_OPTIMAL;
		else if (worstsd->sd_state >= G_RAID_SUBDISK_S_STALE)
			sstate = G_RAID_VOLUME_S_SUBOPTIMAL;
		else if (bestsd->sd_state >= G_RAID_SUBDISK_S_STALE)
			sstate = G_RAID_VOLUME_S_DEGRADED;
		else
			sstate = G_RAID_VOLUME_S_BROKEN;
		if (sstate < state)
			state = sstate;
	}
	return (state);
}

static int
g_raid_tr_update_state_raid1e(struct g_raid_volume *vol,
    struct g_raid_subdisk *sd)
{
	struct g_raid_tr_raid1e_object *trs;
	struct g_raid_softc *sc;
	u_int s;

	sc = vol->v_softc;
	trs = (struct g_raid_tr_raid1e_object *)vol->v_tr;
	if (trs->trso_stopping &&
	    (trs->trso_flags & TR_RAID1E_F_DOING_SOME) == 0)
		s = G_RAID_VOLUME_S_STOPPED;
	else if (trs->trso_starting)
		s = G_RAID_VOLUME_S_STARTING;
	else {
		if ((vol->v_disks_count % N) == 0)
			s = g_raid_tr_update_state_raid1e_even(vol);
		else
			s = g_raid_tr_update_state_raid1e_odd(vol);
	}
	if (s != vol->v_state) {
		g_raid_event_send(vol, G_RAID_VOLUME_S_ALIVE(s) ?
		    G_RAID_VOLUME_E_UP : G_RAID_VOLUME_E_DOWN,
		    G_RAID_EVENT_VOLUME);
		g_raid_change_volume_state(vol, s);
		if (!trs->trso_starting && !trs->trso_stopping)
			g_raid_write_metadata(sc, vol, NULL, NULL);
	}
	if (!trs->trso_starting && !trs->trso_stopping)
		g_raid_tr_raid1e_maybe_rebuild(vol->v_tr, sd);
	return (0);
}

static void
g_raid_tr_raid1e_fail_disk(struct g_raid_softc *sc, struct g_raid_subdisk *sd,
    struct g_raid_disk *disk)
{
	struct g_raid_volume *vol;

	vol = sd->sd_volume;
	/*
	 * We don't fail the last disk in the pack, since it still has decent
	 * data on it and that's better than failing the disk if it is the root
	 * file system.
	 *
	 * XXX should this be controlled via a tunable?  It makes sense for
	 * the volume that has / on it.  I can't think of a case where we'd
	 * want the volume to go away on this kind of event.
	 */
	if ((g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_ACTIVE) +
	     g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_RESYNC) +
	     g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_STALE) +
	     g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_UNINITIALIZED) <
	     vol->v_disks_count) &&
	    (sd->sd_state >= G_RAID_SUBDISK_S_UNINITIALIZED))
		return;
	g_raid_fail_disk(sc, sd, disk);
}

static void
g_raid_tr_raid1e_rebuild_done(struct g_raid_tr_raid1e_object *trs)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;

	vol = trs->trso_base.tro_volume;
	sd = trs->trso_failed_sd;
	g_raid_write_metadata(vol->v_softc, vol, sd, sd->sd_disk);
	free(trs->trso_buffer, M_TR_RAID1E);
	trs->trso_buffer = NULL;
	trs->trso_flags &= ~TR_RAID1E_F_DOING_SOME;
	trs->trso_type = TR_RAID1E_NONE;
	trs->trso_recover_slabs = 0;
	trs->trso_failed_sd = NULL;
	g_raid_tr_update_state_raid1e(vol, NULL);
}

static void
g_raid_tr_raid1e_rebuild_finish(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1e_object *trs;
	struct g_raid_subdisk *sd;

	trs = (struct g_raid_tr_raid1e_object *)tr;
	sd = trs->trso_failed_sd;
	G_RAID_DEBUG1(0, tr->tro_volume->v_softc,
	    "Subdisk %s:%d-%s rebuild completed.",
	    sd->sd_volume->v_name, sd->sd_pos,
	    sd->sd_disk ? g_raid_get_diskname(sd->sd_disk) : "[none]");
	g_raid_change_subdisk_state(sd, G_RAID_SUBDISK_S_ACTIVE);
	sd->sd_rebuild_pos = 0;
	g_raid_tr_raid1e_rebuild_done(trs);
}

static void
g_raid_tr_raid1e_rebuild_abort(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1e_object *trs;
	struct g_raid_subdisk *sd;
	struct g_raid_volume *vol;

	vol = tr->tro_volume;
	trs = (struct g_raid_tr_raid1e_object *)tr;
	sd = trs->trso_failed_sd;
	if (trs->trso_flags & TR_RAID1E_F_DOING_SOME) {
		G_RAID_DEBUG1(1, vol->v_softc,
		    "Subdisk %s:%d-%s rebuild is aborting.",
		    sd->sd_volume->v_name, sd->sd_pos,
		    sd->sd_disk ? g_raid_get_diskname(sd->sd_disk) : "[none]");
		trs->trso_flags |= TR_RAID1E_F_ABORT;
	} else {
		G_RAID_DEBUG1(0, vol->v_softc,
		    "Subdisk %s:%d-%s rebuild aborted.",
		    sd->sd_volume->v_name, sd->sd_pos,
		    sd->sd_disk ? g_raid_get_diskname(sd->sd_disk) : "[none]");
		trs->trso_flags &= ~TR_RAID1E_F_ABORT;
		if (trs->trso_flags & TR_RAID1E_F_LOCKED) {
			trs->trso_flags &= ~TR_RAID1E_F_LOCKED;
			g_raid_unlock_range(tr->tro_volume,
			    trs->trso_lock_pos, trs->trso_lock_len);
		}
		g_raid_tr_raid1e_rebuild_done(trs);
	}
}

static void
g_raid_tr_raid1e_rebuild_some(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1e_object *trs;
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct bio *bp;
	off_t len, virtual, vend, offset, start;
	int disk, copy, best;

	trs = (struct g_raid_tr_raid1e_object *)tr;
	if (trs->trso_flags & TR_RAID1E_F_DOING_SOME)
		return;
	vol = tr->tro_volume;
	sc = vol->v_softc;
	sd = trs->trso_failed_sd;

	while (1) {
		if (sd->sd_rebuild_pos >= sd->sd_size) {
			g_raid_tr_raid1e_rebuild_finish(tr);
			return;
		}
		/* Get virtual offset from physical rebuild position. */
		P2V(vol, sd->sd_pos, sd->sd_rebuild_pos, &virtual, &copy);
		/* Get physical offset back to get first stripe position. */
		V2P(vol, virtual, &disk, &offset, &start);
		/* Calculate contignous data length. */
		len = MIN(g_raid1e_rebuild_slab,
		    sd->sd_size - sd->sd_rebuild_pos);
		if ((vol->v_disks_count % N) != 0)
			len = MIN(len, vol->v_strip_size - start);
		/* Find disk with most accurate data. */
		best = g_raid_tr_raid1e_select_read_disk(vol, disk,
		    offset + start, len, 0);
		if (best < 0) {
			/* There is no any valid disk. */
			g_raid_tr_raid1e_rebuild_abort(tr);
			return;
		} else if (best != copy) {
			/* Some other disk has better data. */
			break;
		}
		/* We have the most accurate data. Skip the range. */
		G_RAID_DEBUG1(3, sc, "Skipping rebuild for range %ju - %ju",
		    sd->sd_rebuild_pos, sd->sd_rebuild_pos + len);
		sd->sd_rebuild_pos += len;
	}

	bp = &trs->trso_bio;
	memset(bp, 0, sizeof(*bp));
	bp->bio_offset = offset + start +
	    ((disk + best >= vol->v_disks_count) ? vol->v_strip_size : 0);
	bp->bio_length = len;
	bp->bio_data = trs->trso_buffer;
	bp->bio_cmd = BIO_READ;
	bp->bio_cflags = G_RAID_BIO_FLAG_SYNC;
	bp->bio_caller1 = &vol->v_subdisks[(disk + best) % vol->v_disks_count];
	G_RAID_LOGREQ(3, bp, "Queueing rebuild read");
	/*
	 * If we are crossing stripe boundary, correct affected virtual
	 * range we should lock.
	 */
	if (start + len > vol->v_strip_size) {
		P2V(vol, sd->sd_pos, sd->sd_rebuild_pos + len, &vend, &copy);
		len = vend - virtual;
	}
	trs->trso_flags |= TR_RAID1E_F_DOING_SOME;
	trs->trso_flags |= TR_RAID1E_F_LOCKED;
	trs->trso_lock_pos = virtual;
	trs->trso_lock_len = len;
	/* Lock callback starts I/O */
	g_raid_lock_range(sd->sd_volume, virtual, len, NULL, bp);
}

static void
g_raid_tr_raid1e_rebuild_start(struct g_raid_tr_object *tr)
{
	struct g_raid_volume *vol;
	struct g_raid_tr_raid1e_object *trs;
	struct g_raid_subdisk *sd;

	vol = tr->tro_volume;
	trs = (struct g_raid_tr_raid1e_object *)tr;
	if (trs->trso_failed_sd) {
		G_RAID_DEBUG1(1, vol->v_softc,
		    "Already rebuild in start rebuild. pos %jd\n",
		    (intmax_t)trs->trso_failed_sd->sd_rebuild_pos);
		return;
	}
	sd = g_raid_get_subdisk(vol, G_RAID_SUBDISK_S_RESYNC);
	if (sd == NULL)
		sd = g_raid_get_subdisk(vol, G_RAID_SUBDISK_S_REBUILD);
	if (sd == NULL) {
		sd = g_raid_get_subdisk(vol, G_RAID_SUBDISK_S_STALE);
		if (sd != NULL) {
			sd->sd_rebuild_pos = 0;
			g_raid_change_subdisk_state(sd,
			    G_RAID_SUBDISK_S_RESYNC);
			g_raid_write_metadata(vol->v_softc, vol, sd, NULL);
		} else {
			sd = g_raid_get_subdisk(vol,
			    G_RAID_SUBDISK_S_UNINITIALIZED);
			if (sd == NULL)
				sd = g_raid_get_subdisk(vol,
				    G_RAID_SUBDISK_S_NEW);
			if (sd != NULL) {
				sd->sd_rebuild_pos = 0;
				g_raid_change_subdisk_state(sd,
				    G_RAID_SUBDISK_S_REBUILD);
				g_raid_write_metadata(vol->v_softc,
				    vol, sd, NULL);
			}
		}
	}
	if (sd == NULL) {
		G_RAID_DEBUG1(1, vol->v_softc,
		    "No failed disk to rebuild.  night night.");
		return;
	}
	trs->trso_failed_sd = sd;
	G_RAID_DEBUG1(0, vol->v_softc,
	    "Subdisk %s:%d-%s rebuild start at %jd.",
	    sd->sd_volume->v_name, sd->sd_pos,
	    sd->sd_disk ? g_raid_get_diskname(sd->sd_disk) : "[none]",
	    trs->trso_failed_sd->sd_rebuild_pos);
	trs->trso_type = TR_RAID1E_REBUILD;
	trs->trso_buffer = malloc(g_raid1e_rebuild_slab, M_TR_RAID1E, M_WAITOK);
	trs->trso_meta_update = g_raid1e_rebuild_meta_update;
	g_raid_tr_raid1e_rebuild_some(tr);
}

static void
g_raid_tr_raid1e_maybe_rebuild(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd)
{
	struct g_raid_volume *vol;
	struct g_raid_tr_raid1e_object *trs;
	int nr;
	
	vol = tr->tro_volume;
	trs = (struct g_raid_tr_raid1e_object *)tr;
	if (trs->trso_stopping)
		return;
	nr = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_REBUILD) +
	    g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_RESYNC);
	switch(trs->trso_type) {
	case TR_RAID1E_NONE:
		if (vol->v_state < G_RAID_VOLUME_S_DEGRADED)
			return;
		if (nr == 0) {
			nr = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_NEW) +
			    g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_STALE) +
			    g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_UNINITIALIZED);
			if (nr == 0)
				return;
		}
		g_raid_tr_raid1e_rebuild_start(tr);
		break;
	case TR_RAID1E_REBUILD:
		if (vol->v_state < G_RAID_VOLUME_S_DEGRADED || nr == 0 ||
		    trs->trso_failed_sd == sd)
			g_raid_tr_raid1e_rebuild_abort(tr);
		break;
	case TR_RAID1E_RESYNC:
		break;
	}
}

static int
g_raid_tr_event_raid1e(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd, u_int event)
{

	g_raid_tr_update_state_raid1e(tr->tro_volume, sd);
	return (0);
}

static int
g_raid_tr_start_raid1e(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1e_object *trs;
	struct g_raid_volume *vol;

	trs = (struct g_raid_tr_raid1e_object *)tr;
	vol = tr->tro_volume;
	trs->trso_starting = 0;
	g_raid_tr_update_state_raid1e(vol, NULL);
	return (0);
}

static int
g_raid_tr_stop_raid1e(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1e_object *trs;
	struct g_raid_volume *vol;

	trs = (struct g_raid_tr_raid1e_object *)tr;
	vol = tr->tro_volume;
	trs->trso_starting = 0;
	trs->trso_stopping = 1;
	g_raid_tr_update_state_raid1e(vol, NULL);
	return (0);
}

/*
 * Select the disk to read from.  Take into account: subdisk state, running
 * error recovery, average disk load, head position and possible cache hits.
 */
#define ABS(x)		(((x) >= 0) ? (x) : (-(x)))
static int
g_raid_tr_raid1e_select_read_disk(struct g_raid_volume *vol,
    int no, off_t off, off_t len, u_int mask)
{
	struct g_raid_subdisk *sd;
	off_t offset;
	int i, best, prio, bestprio;

	best = -1;
	bestprio = INT_MAX;
	for (i = 0; i < N; i++) {
		sd = &vol->v_subdisks[(no + i) % vol->v_disks_count];
		offset = off;
		if (no + i >= vol->v_disks_count)
			offset += vol->v_strip_size;

		prio = G_RAID_SUBDISK_LOAD(sd);
		if ((mask & (1 << sd->sd_pos)) != 0)
			continue;
		switch (sd->sd_state) {
		case G_RAID_SUBDISK_S_ACTIVE:
			break;
		case G_RAID_SUBDISK_S_RESYNC:
			if (offset + off < sd->sd_rebuild_pos)
				break;
			/* FALLTHROUGH */
		case G_RAID_SUBDISK_S_STALE:
			prio += i << 24;
			break;
		case G_RAID_SUBDISK_S_REBUILD:
			if (offset + off < sd->sd_rebuild_pos)
				break;
			/* FALLTHROUGH */
		default:
			continue;
		}
		prio += min(sd->sd_recovery, 255) << 16;
		/* If disk head is precisely in position - highly prefer it. */
		if (G_RAID_SUBDISK_POS(sd) == offset)
			prio -= 2 * G_RAID_SUBDISK_LOAD_SCALE;
		else
		/* If disk head is close to position - prefer it. */
		if (ABS(G_RAID_SUBDISK_POS(sd) - offset) <
		    G_RAID_SUBDISK_TRACK_SIZE)
			prio -= 1 * G_RAID_SUBDISK_LOAD_SCALE;
		if (prio < bestprio) {
			bestprio = prio;
			best = i;
		}
	}
	return (best);
}

static void
g_raid_tr_iostart_raid1e_read(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct bio_queue_head queue;
	struct bio *cbp;
	char *addr;
	off_t offset, start, length, remain;
	u_int no, strip_size;
	int best;

	vol = tr->tro_volume;
	if ((bp->bio_flags & BIO_UNMAPPED) != 0)
		addr = NULL;
	else
		addr = bp->bio_data;
	strip_size = vol->v_strip_size;
	V2P(vol, bp->bio_offset, &no, &offset, &start);
	remain = bp->bio_length;
	bioq_init(&queue);
	while (remain > 0) {
		length = MIN(strip_size - start, remain);
		best = g_raid_tr_raid1e_select_read_disk(vol,
		    no, offset, length, 0);
		KASSERT(best >= 0, ("No readable disk in volume %s!",
		    vol->v_name));
		no += best;
		if (no >= vol->v_disks_count) {
			no -= vol->v_disks_count;
			offset += strip_size;
		}
		cbp = g_clone_bio(bp);
		if (cbp == NULL)
			goto failure;
		cbp->bio_offset = offset + start;
		cbp->bio_length = length;
		if ((bp->bio_flags & BIO_UNMAPPED) != 0) {
			cbp->bio_ma_offset += (uintptr_t)addr;
			cbp->bio_ma += cbp->bio_ma_offset / PAGE_SIZE;
			cbp->bio_ma_offset %= PAGE_SIZE;
			cbp->bio_ma_n = round_page(cbp->bio_ma_offset +
			    cbp->bio_length) / PAGE_SIZE;
		} else
			cbp->bio_data = addr;
		cbp->bio_caller1 = &vol->v_subdisks[no];
		bioq_insert_tail(&queue, cbp);
		no += N - best;
		if (no >= vol->v_disks_count) {
			no -= vol->v_disks_count;
			offset += strip_size;
		}
		remain -= length;
		addr += length;
		start = 0;
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
g_raid_tr_iostart_raid1e_write(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct bio_queue_head queue;
	struct bio *cbp;
	char *addr;
	off_t offset, start, length, remain;
	u_int no, strip_size;
	int i;

	vol = tr->tro_volume;
	if ((bp->bio_flags & BIO_UNMAPPED) != 0)
		addr = NULL;
	else
		addr = bp->bio_data;
	strip_size = vol->v_strip_size;
	V2P(vol, bp->bio_offset, &no, &offset, &start);
	remain = bp->bio_length;
	bioq_init(&queue);
	while (remain > 0) {
		length = MIN(strip_size - start, remain);
		for (i = 0; i < N; i++) {
			sd = &vol->v_subdisks[no];
			switch (sd->sd_state) {
			case G_RAID_SUBDISK_S_ACTIVE:
			case G_RAID_SUBDISK_S_STALE:
			case G_RAID_SUBDISK_S_RESYNC:
				break;
			case G_RAID_SUBDISK_S_REBUILD:
				if (offset + start >= sd->sd_rebuild_pos)
					goto nextdisk;
				break;
			default:
				goto nextdisk;
			}
			cbp = g_clone_bio(bp);
			if (cbp == NULL)
				goto failure;
			cbp->bio_offset = offset + start;
			cbp->bio_length = length;
			if ((bp->bio_flags & BIO_UNMAPPED) != 0 &&
			    bp->bio_cmd != BIO_DELETE) {
				cbp->bio_ma_offset += (uintptr_t)addr;
				cbp->bio_ma += cbp->bio_ma_offset / PAGE_SIZE;
				cbp->bio_ma_offset %= PAGE_SIZE;
				cbp->bio_ma_n = round_page(cbp->bio_ma_offset +
				    cbp->bio_length) / PAGE_SIZE;
			} else
				cbp->bio_data = addr;
			cbp->bio_caller1 = sd;
			bioq_insert_tail(&queue, cbp);
nextdisk:
			if (++no >= vol->v_disks_count) {
				no = 0;
				offset += strip_size;
			}
		}
		remain -= length;
		if (bp->bio_cmd != BIO_DELETE)
			addr += length;
		start = 0;
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
g_raid_tr_iostart_raid1e(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_volume *vol;
	struct g_raid_tr_raid1e_object *trs;

	vol = tr->tro_volume;
	trs = (struct g_raid_tr_raid1e_object *)tr;
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
			trs->trso_fair_io = g_raid1e_rebuild_fair_io;
			g_raid_tr_raid1e_rebuild_some(tr);
		}
	}
	switch (bp->bio_cmd) {
	case BIO_READ:
		g_raid_tr_iostart_raid1e_read(tr, bp);
		break;
	case BIO_WRITE:
	case BIO_DELETE:
		g_raid_tr_iostart_raid1e_write(tr, bp);
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
g_raid_tr_iodone_raid1e(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd, struct bio *bp)
{
	struct bio *cbp;
	struct g_raid_subdisk *nsd;
	struct g_raid_volume *vol;
	struct bio *pbp;
	struct g_raid_tr_raid1e_object *trs;
	off_t virtual, offset, start;
	uintptr_t mask;
	int error, do_write, copy, disk, best;

	trs = (struct g_raid_tr_raid1e_object *)tr;
	vol = tr->tro_volume;
	if (bp->bio_cflags & G_RAID_BIO_FLAG_SYNC) {
		if (trs->trso_type == TR_RAID1E_REBUILD) {
			nsd = trs->trso_failed_sd;
			if (bp->bio_cmd == BIO_READ) {

				/* Immediately abort rebuild, if requested. */
				if (trs->trso_flags & TR_RAID1E_F_ABORT) {
					trs->trso_flags &= ~TR_RAID1E_F_DOING_SOME;
					g_raid_tr_raid1e_rebuild_abort(tr);
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
				G_RAID_LOGREQ(3, bp, "Rebuild read done: %d",
				    bp->bio_error);
				bp->bio_cmd = BIO_WRITE;
				bp->bio_cflags = G_RAID_BIO_FLAG_SYNC;
				bp->bio_offset = nsd->sd_rebuild_pos;
				G_RAID_LOGREQ(3, bp, "Queueing rebuild write.");
				g_raid_subdisk_iostart(nsd, bp);
			} else {
				/*
				 * The write operation just finished.  Do
				 * another.  We keep cloning the master bio
				 * since it has the right buffers allocated to
				 * it.
				 */
				G_RAID_LOGREQ(3, bp, "Rebuild write done: %d",
				    bp->bio_error);
				if (bp->bio_error != 0 ||
				    trs->trso_flags & TR_RAID1E_F_ABORT) {
					if ((trs->trso_flags &
					    TR_RAID1E_F_ABORT) == 0) {
						g_raid_tr_raid1e_fail_disk(sd->sd_softc,
						    nsd, nsd->sd_disk);
					}
					trs->trso_flags &= ~TR_RAID1E_F_DOING_SOME;
					g_raid_tr_raid1e_rebuild_abort(tr);
					return;
				}
rebuild_round_done:
				trs->trso_flags &= ~TR_RAID1E_F_LOCKED;
				g_raid_unlock_range(tr->tro_volume,
				    trs->trso_lock_pos, trs->trso_lock_len);
				nsd->sd_rebuild_pos += bp->bio_length;
				if (nsd->sd_rebuild_pos >= nsd->sd_size) {
					g_raid_tr_raid1e_rebuild_finish(tr);
					return;
				}

				/* Abort rebuild if we are stopping */
				if (trs->trso_stopping) {
					trs->trso_flags &= ~TR_RAID1E_F_DOING_SOME;
					g_raid_tr_raid1e_rebuild_abort(tr);
					return;
				}

				if (--trs->trso_meta_update <= 0) {
					g_raid_write_metadata(vol->v_softc,
					    vol, nsd, nsd->sd_disk);
					trs->trso_meta_update =
					    g_raid1e_rebuild_meta_update;
					/* Compensate short rebuild I/Os. */
					if ((vol->v_disks_count % N) != 0 &&
					    vol->v_strip_size <
					     g_raid1e_rebuild_slab) {
						trs->trso_meta_update *=
						    g_raid1e_rebuild_slab;
						trs->trso_meta_update /=
						    vol->v_strip_size;
					}
				}
				trs->trso_flags &= ~TR_RAID1E_F_DOING_SOME;
				if (--trs->trso_recover_slabs <= 0)
					return;
				/* Run next rebuild iteration. */
				g_raid_tr_raid1e_rebuild_some(tr);
			}
		} else if (trs->trso_type == TR_RAID1E_RESYNC) {
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
	mask = (intptr_t)bp->bio_caller2;
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
		do_write = 0;
		if (sd->sd_disk->d_read_errs > g_raid_read_err_thresh)
			g_raid_tr_raid1e_fail_disk(sd->sd_softc, sd, sd->sd_disk);
		else if (mask == 0)
			do_write = 1;

		/* Restore what we were doing. */
		P2V(vol, sd->sd_pos, bp->bio_offset, &virtual, &copy);
		V2P(vol, virtual, &disk, &offset, &start);

		/* Find the other disk, and try to do the I/O to it. */
		mask |= 1 << copy;
		best = g_raid_tr_raid1e_select_read_disk(vol,
		    disk, offset, start, mask);
		if (best >= 0 && (cbp = g_clone_bio(pbp)) != NULL) {
			disk += best;
			if (disk >= vol->v_disks_count) {
				disk -= vol->v_disks_count;
				offset += vol->v_strip_size;
			}
			cbp->bio_offset = offset + start;
			cbp->bio_length = bp->bio_length;
			cbp->bio_data = bp->bio_data;
			cbp->bio_ma = bp->bio_ma;
			cbp->bio_ma_offset = bp->bio_ma_offset;
			cbp->bio_ma_n = bp->bio_ma_n;
			g_destroy_bio(bp);
			nsd = &vol->v_subdisks[disk];
			G_RAID_LOGREQ(2, cbp, "Retrying read from %d",
			    nsd->sd_pos);
			if (do_write)
				mask |= 1 << 31;
			if ((mask & (1U << 31)) != 0)
				sd->sd_recovery++;
			cbp->bio_caller2 = (void *)mask;
			if (do_write) {
				cbp->bio_caller1 = nsd;
				/* Lock callback starts I/O */
				g_raid_lock_range(sd->sd_volume,
				    virtual, cbp->bio_length, pbp, cbp);
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
	    (mask & (1U << 31)) != 0) {
		G_RAID_LOGREQ(3, bp, "Recovered data from other drive");

		/* Restore what we were doing. */
		P2V(vol, sd->sd_pos, bp->bio_offset, &virtual, &copy);
		V2P(vol, virtual, &disk, &offset, &start);

		/* Find best disk to write. */
		best = g_raid_tr_raid1e_select_read_disk(vol,
		    disk, offset, start, ~mask);
		if (best >= 0 && (cbp = g_clone_bio(pbp)) != NULL) {
			disk += best;
			if (disk >= vol->v_disks_count) {
				disk -= vol->v_disks_count;
				offset += vol->v_strip_size;
			}
			cbp->bio_offset = offset + start;
			cbp->bio_cmd = BIO_WRITE;
			cbp->bio_cflags = G_RAID_BIO_FLAG_REMAP;
			cbp->bio_caller2 = (void *)mask;
			g_destroy_bio(bp);
			G_RAID_LOGREQ(2, cbp,
			    "Attempting bad sector remap on failing drive.");
			g_raid_subdisk_iostart(&vol->v_subdisks[disk], cbp);
			return;
		}
	}
	if ((mask & (1U << 31)) != 0) {
		/*
		 * We're done with a recovery, mark the range as unlocked.
		 * For any write errors, we aggressively fail the disk since
		 * there was both a READ and a WRITE error at this location.
		 * Both types of errors generally indicates the drive is on
		 * the verge of total failure anyway.  Better to stop trusting
		 * it now.  However, we need to reset error to 0 in that case
		 * because we're not failing the original I/O which succeeded.
		 */

		/* Restore what we were doing. */
		P2V(vol, sd->sd_pos, bp->bio_offset, &virtual, &copy);
		V2P(vol, virtual, &disk, &offset, &start);

		for (copy = 0; copy < N; copy++) {
			if ((mask & (1 << copy) ) != 0)
				vol->v_subdisks[(disk + copy) %
				    vol->v_disks_count].sd_recovery--;
		}

		if (bp->bio_cmd == BIO_WRITE && bp->bio_error) {
			G_RAID_LOGREQ(0, bp, "Remap write failed: "
			    "failing subdisk.");
			g_raid_tr_raid1e_fail_disk(sd->sd_softc, sd, sd->sd_disk);
			bp->bio_error = 0;
		}
		G_RAID_LOGREQ(2, bp, "REMAP done %d.", bp->bio_error);
		g_raid_unlock_range(sd->sd_volume, virtual, bp->bio_length);
	}
	if (pbp->bio_cmd != BIO_READ) {
		if (pbp->bio_inbed == 1 || pbp->bio_error != 0)
			pbp->bio_error = bp->bio_error;
		if (pbp->bio_cmd == BIO_WRITE && bp->bio_error != 0) {
			G_RAID_LOGREQ(0, bp, "Write failed: failing subdisk.");
			g_raid_tr_raid1e_fail_disk(sd->sd_softc, sd, sd->sd_disk);
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
g_raid_tr_kerneldump_raid1e(struct g_raid_tr_object *tr,
    void *virtual, vm_offset_t physical, off_t boffset, size_t blength)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct bio_queue_head queue;
	char *addr;
	off_t offset, start, length, remain;
	u_int no, strip_size;
	int i, error;

	vol = tr->tro_volume;
	addr = virtual;
	strip_size = vol->v_strip_size;
	V2P(vol, boffset, &no, &offset, &start);
	remain = blength;
	bioq_init(&queue);
	while (remain > 0) {
		length = MIN(strip_size - start, remain);
		for (i = 0; i < N; i++) {
			sd = &vol->v_subdisks[no];
			switch (sd->sd_state) {
			case G_RAID_SUBDISK_S_ACTIVE:
			case G_RAID_SUBDISK_S_STALE:
			case G_RAID_SUBDISK_S_RESYNC:
				break;
			case G_RAID_SUBDISK_S_REBUILD:
				if (offset + start >= sd->sd_rebuild_pos)
					goto nextdisk;
				break;
			default:
				goto nextdisk;
			}
			error = g_raid_subdisk_kerneldump(sd,
			    addr, 0, offset + start, length);
			if (error != 0)
				return (error);
nextdisk:
			if (++no >= vol->v_disks_count) {
				no = 0;
				offset += strip_size;
			}
		}
		remain -= length;
		addr += length;
		start = 0;
	}
	return (0);
}

static int
g_raid_tr_locked_raid1e(struct g_raid_tr_object *tr, void *argp)
{
	struct bio *bp;
	struct g_raid_subdisk *sd;

	bp = (struct bio *)argp;
	sd = (struct g_raid_subdisk *)bp->bio_caller1;
	g_raid_subdisk_iostart(sd, bp);

	return (0);
}

static int
g_raid_tr_idle_raid1e(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1e_object *trs;
	struct g_raid_volume *vol;

	vol = tr->tro_volume;
	trs = (struct g_raid_tr_raid1e_object *)tr;
	trs->trso_fair_io = g_raid1e_rebuild_fair_io;
	trs->trso_recover_slabs = g_raid1e_rebuild_cluster_idle;
	/* Compensate short rebuild I/Os. */
	if ((vol->v_disks_count % N) != 0 &&
	    vol->v_strip_size < g_raid1e_rebuild_slab) {
		trs->trso_recover_slabs *= g_raid1e_rebuild_slab;
		trs->trso_recover_slabs /= vol->v_strip_size;
	}
	if (trs->trso_type == TR_RAID1E_REBUILD)
		g_raid_tr_raid1e_rebuild_some(tr);
	return (0);
}

static int
g_raid_tr_free_raid1e(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid1e_object *trs;

	trs = (struct g_raid_tr_raid1e_object *)tr;

	if (trs->trso_buffer != NULL) {
		free(trs->trso_buffer, M_TR_RAID1E);
		trs->trso_buffer = NULL;
	}
	return (0);
}

G_RAID_TR_DECLARE(raid1e, "RAID1E");
