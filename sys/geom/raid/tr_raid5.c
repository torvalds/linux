/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Alexander Motin <mav@FreeBSD.org>
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

static MALLOC_DEFINE(M_TR_RAID5, "tr_raid5_data", "GEOM_RAID RAID5 data");

#define TR_RAID5_NONE 0
#define TR_RAID5_REBUILD 1
#define TR_RAID5_RESYNC 2

#define TR_RAID5_F_DOING_SOME	0x1
#define TR_RAID5_F_LOCKED	0x2
#define TR_RAID5_F_ABORT	0x4

struct g_raid_tr_raid5_object {
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

static g_raid_tr_taste_t g_raid_tr_taste_raid5;
static g_raid_tr_event_t g_raid_tr_event_raid5;
static g_raid_tr_start_t g_raid_tr_start_raid5;
static g_raid_tr_stop_t g_raid_tr_stop_raid5;
static g_raid_tr_iostart_t g_raid_tr_iostart_raid5;
static g_raid_tr_iodone_t g_raid_tr_iodone_raid5;
static g_raid_tr_kerneldump_t g_raid_tr_kerneldump_raid5;
static g_raid_tr_locked_t g_raid_tr_locked_raid5;
static g_raid_tr_free_t g_raid_tr_free_raid5;

static kobj_method_t g_raid_tr_raid5_methods[] = {
	KOBJMETHOD(g_raid_tr_taste,	g_raid_tr_taste_raid5),
	KOBJMETHOD(g_raid_tr_event,	g_raid_tr_event_raid5),
	KOBJMETHOD(g_raid_tr_start,	g_raid_tr_start_raid5),
	KOBJMETHOD(g_raid_tr_stop,	g_raid_tr_stop_raid5),
	KOBJMETHOD(g_raid_tr_iostart,	g_raid_tr_iostart_raid5),
	KOBJMETHOD(g_raid_tr_iodone,	g_raid_tr_iodone_raid5),
	KOBJMETHOD(g_raid_tr_kerneldump, g_raid_tr_kerneldump_raid5),
	KOBJMETHOD(g_raid_tr_locked,	g_raid_tr_locked_raid5),
	KOBJMETHOD(g_raid_tr_free,	g_raid_tr_free_raid5),
	{ 0, 0 }
};

static struct g_raid_tr_class g_raid_tr_raid5_class = {
	"RAID5",
	g_raid_tr_raid5_methods,
	sizeof(struct g_raid_tr_raid5_object),
	.trc_enable = 1,
	.trc_priority = 100
};

static int
g_raid_tr_taste_raid5(struct g_raid_tr_object *tr, struct g_raid_volume *vol)
{
	struct g_raid_tr_raid5_object *trs;
	u_int qual;

	trs = (struct g_raid_tr_raid5_object *)tr;
	qual = tr->tro_volume->v_raid_level_qualifier;
	if (tr->tro_volume->v_raid_level == G_RAID_VOLUME_RL_RAID4 &&
	    (qual == G_RAID_VOLUME_RLQ_R4P0 ||
	     qual == G_RAID_VOLUME_RLQ_R4PN)) {
		/* RAID4 */
	} else if ((tr->tro_volume->v_raid_level == G_RAID_VOLUME_RL_RAID5 ||
	     tr->tro_volume->v_raid_level == G_RAID_VOLUME_RL_RAID5E ||
	     tr->tro_volume->v_raid_level == G_RAID_VOLUME_RL_RAID5EE ||
	     tr->tro_volume->v_raid_level == G_RAID_VOLUME_RL_RAID5R ||
	     tr->tro_volume->v_raid_level == G_RAID_VOLUME_RL_RAID6 ||
	     tr->tro_volume->v_raid_level == G_RAID_VOLUME_RL_RAIDMDF) &&
	    (qual == G_RAID_VOLUME_RLQ_R5RA ||
	     qual == G_RAID_VOLUME_RLQ_R5RS ||
	     qual == G_RAID_VOLUME_RLQ_R5LA ||
	     qual == G_RAID_VOLUME_RLQ_R5LS)) {
		/* RAID5/5E/5EE/5R/6/MDF */
	} else
		return (G_RAID_TR_TASTE_FAIL);
	trs->trso_starting = 1;
	return (G_RAID_TR_TASTE_SUCCEED);
}

static int
g_raid_tr_update_state_raid5(struct g_raid_volume *vol,
    struct g_raid_subdisk *sd)
{
	struct g_raid_tr_raid5_object *trs;
	struct g_raid_softc *sc;
	u_int s;
	int na, ns, nu;

	sc = vol->v_softc;
	trs = (struct g_raid_tr_raid5_object *)vol->v_tr;
	if (trs->trso_stopping &&
	    (trs->trso_flags & TR_RAID5_F_DOING_SOME) == 0)
		s = G_RAID_VOLUME_S_STOPPED;
	else if (trs->trso_starting)
		s = G_RAID_VOLUME_S_STARTING;
	else {
		na = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_ACTIVE);
		ns = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_STALE) +
		     g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_RESYNC);
		nu = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_UNINITIALIZED);
		if (na == vol->v_disks_count)
			s = G_RAID_VOLUME_S_OPTIMAL;
		else if (na + ns == vol->v_disks_count ||
		    na + ns + nu == vol->v_disks_count /* XXX: Temporary. */)
			s = G_RAID_VOLUME_S_SUBOPTIMAL;
		else if (na == vol->v_disks_count - 1 ||
		    na + ns + nu == vol->v_disks_count)
			s = G_RAID_VOLUME_S_DEGRADED;
		else
			s = G_RAID_VOLUME_S_BROKEN;
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

static int
g_raid_tr_event_raid5(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd, u_int event)
{

	g_raid_tr_update_state_raid5(tr->tro_volume, sd);
	return (0);
}

static int
g_raid_tr_start_raid5(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid5_object *trs;
	struct g_raid_volume *vol;

	trs = (struct g_raid_tr_raid5_object *)tr;
	trs->trso_starting = 0;
	vol = tr->tro_volume;
	vol->v_read_only = 1;
	g_raid_tr_update_state_raid5(vol, NULL);
	return (0);
}

static int
g_raid_tr_stop_raid5(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid5_object *trs;
	struct g_raid_volume *vol;

	trs = (struct g_raid_tr_raid5_object *)tr;
	vol = tr->tro_volume;
	trs->trso_starting = 0;
	trs->trso_stopping = 1;
	g_raid_tr_update_state_raid5(vol, NULL);
	return (0);
}

static void
g_raid_tr_iostart_raid5_read(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct bio_queue_head queue;
	struct bio *cbp;
	char *addr;
	off_t offset, start, length, nstripe, remain;
	int no, pno, ddisks, pdisks, protate, pleft;
	u_int strip_size, lvl, qual;

	vol = tr->tro_volume;
	addr = bp->bio_data;
	strip_size = vol->v_strip_size;
	lvl = tr->tro_volume->v_raid_level;
	qual = tr->tro_volume->v_raid_level_qualifier;
	protate = tr->tro_volume->v_rotate_parity;

	/* Stripe number. */
	nstripe = bp->bio_offset / strip_size;
	/* Start position in stripe. */
	start = bp->bio_offset % strip_size;
	/* Number of data and parity disks. */
	if (lvl == G_RAID_VOLUME_RL_RAIDMDF)
		pdisks = tr->tro_volume->v_mdf_pdisks;
	else if (lvl == G_RAID_VOLUME_RL_RAID5EE ||
	    lvl == G_RAID_VOLUME_RL_RAID6)
		pdisks = 2;
	else
		pdisks = 1;
	ddisks = vol->v_disks_count - pdisks;
	/* Parity disk number. */
	if (lvl == G_RAID_VOLUME_RL_RAID4) {
		if (qual == 0)		/* P0 */
			pno = 0;
		else			/* PN */
			pno = ddisks;
		pleft = -1;
	} else {
		pno = (nstripe / (ddisks * protate)) % vol->v_disks_count;
		pleft = protate - (nstripe / ddisks) % protate;
		if (qual >= 2) {	/* PN/Left */
			pno = ddisks - pno;
			if (pno < 0)
				pno += vol->v_disks_count;
		}
	}
	/* Data disk number. */
	no = nstripe % ddisks;
	if (lvl == G_RAID_VOLUME_RL_RAID4) {
		if (qual == 0)
			no += pdisks;
	} else if (qual & 1) {	/* Continuation/Symmetric */
		no = (pno + pdisks + no) % vol->v_disks_count;
	} else if (no >= pno)	/* Restart/Asymmetric */
		no += pdisks;
	else
		no += imax(0, pno + pdisks - vol->v_disks_count);
	/* Stripe start position in disk. */
	offset = (nstripe / ddisks) * strip_size;
	/* Length of data to operate. */
	remain = bp->bio_length;

	bioq_init(&queue);
	do {
		length = MIN(strip_size - start, remain);
		cbp = g_clone_bio(bp);
		if (cbp == NULL)
			goto failure;
		cbp->bio_offset = offset + start;
		cbp->bio_data = addr;
		cbp->bio_length = length;
		cbp->bio_caller1 = &vol->v_subdisks[no];
		bioq_insert_tail(&queue, cbp);
		no++;
		if (lvl == G_RAID_VOLUME_RL_RAID4) {
			no %= vol->v_disks_count;
			if (no == pno)
				no = (no + pdisks) % vol->v_disks_count;
		} else if (qual & 1) {	/* Continuation/Symmetric */
			no %= vol->v_disks_count;
			if (no == pno) {
				if ((--pleft) <= 0) {
					pleft += protate;
					if (qual < 2)	/* P0/Right */
						pno++;
					else		/* PN/Left */
						pno += vol->v_disks_count - 1;
					pno %= vol->v_disks_count;
				}
				no = (pno + pdisks) % vol->v_disks_count;
				offset += strip_size;
			}
		} else {		/* Restart/Asymmetric */
			if (no == pno)
				no += pdisks;
			if (no >= vol->v_disks_count) {
				no -= vol->v_disks_count;
				if ((--pleft) <= 0) {
					pleft += protate;
					if (qual < 2)	/* P0/Right */
						pno++;
					else		/* PN/Left */
						pno += vol->v_disks_count - 1;
					pno %= vol->v_disks_count;
				}
				if (no == pno)
					no += pdisks;
				else
					no += imax(0, pno + pdisks - vol->v_disks_count);
				offset += strip_size;
			}
		}
		remain -= length;
		addr += length;
		start = 0;
	} while (remain > 0);
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
g_raid_tr_iostart_raid5(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_volume *vol;

	vol = tr->tro_volume;
	if (vol->v_state < G_RAID_VOLUME_S_SUBOPTIMAL) {
		g_raid_iodone(bp, EIO);
		return;
	}
	switch (bp->bio_cmd) {
	case BIO_READ:
		g_raid_tr_iostart_raid5_read(tr, bp);
		break;
	case BIO_WRITE:
	case BIO_DELETE:
	case BIO_FLUSH:
		g_raid_iodone(bp, ENODEV);
		break;
	default:
		KASSERT(1 == 0, ("Invalid command here: %u (volume=%s)",
		    bp->bio_cmd, vol->v_name));
		break;
	}
}

static void
g_raid_tr_iodone_raid5(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd, struct bio *bp)
{
	struct bio *pbp;

	pbp = bp->bio_parent;
	if (pbp->bio_error == 0)
		pbp->bio_error = bp->bio_error;
	pbp->bio_inbed++;
	g_destroy_bio(bp);
	if (pbp->bio_children == pbp->bio_inbed) {
		pbp->bio_completed = pbp->bio_length;
		g_raid_iodone(pbp, pbp->bio_error);
	}
}

static int
g_raid_tr_kerneldump_raid5(struct g_raid_tr_object *tr,
    void *virtual, vm_offset_t physical, off_t offset, size_t length)
{

	return (ENODEV);
}

static int
g_raid_tr_locked_raid5(struct g_raid_tr_object *tr, void *argp)
{
	struct bio *bp;
	struct g_raid_subdisk *sd;

	bp = (struct bio *)argp;
	sd = (struct g_raid_subdisk *)bp->bio_caller1;
	g_raid_subdisk_iostart(sd, bp);

	return (0);
}

static int
g_raid_tr_free_raid5(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_raid5_object *trs;

	trs = (struct g_raid_tr_raid5_object *)tr;

	if (trs->trso_buffer != NULL) {
		free(trs->trso_buffer, M_TR_RAID5);
		trs->trso_buffer = NULL;
	}
	return (0);
}

G_RAID_TR_DECLARE(raid5, "RAID5");
