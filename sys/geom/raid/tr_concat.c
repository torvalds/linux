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
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <geom/geom.h>
#include "geom/raid/g_raid.h"
#include "g_raid_tr_if.h"

static MALLOC_DEFINE(M_TR_CONCAT, "tr_concat_data", "GEOM_RAID CONCAT data");

struct g_raid_tr_concat_object {
	struct g_raid_tr_object	 trso_base;
	int			 trso_starting;
	int			 trso_stopped;
};

static g_raid_tr_taste_t g_raid_tr_taste_concat;
static g_raid_tr_event_t g_raid_tr_event_concat;
static g_raid_tr_start_t g_raid_tr_start_concat;
static g_raid_tr_stop_t g_raid_tr_stop_concat;
static g_raid_tr_iostart_t g_raid_tr_iostart_concat;
static g_raid_tr_iodone_t g_raid_tr_iodone_concat;
static g_raid_tr_kerneldump_t g_raid_tr_kerneldump_concat;
static g_raid_tr_free_t g_raid_tr_free_concat;

static kobj_method_t g_raid_tr_concat_methods[] = {
	KOBJMETHOD(g_raid_tr_taste,	g_raid_tr_taste_concat),
	KOBJMETHOD(g_raid_tr_event,	g_raid_tr_event_concat),
	KOBJMETHOD(g_raid_tr_start,	g_raid_tr_start_concat),
	KOBJMETHOD(g_raid_tr_stop,	g_raid_tr_stop_concat),
	KOBJMETHOD(g_raid_tr_iostart,	g_raid_tr_iostart_concat),
	KOBJMETHOD(g_raid_tr_iodone,	g_raid_tr_iodone_concat),
	KOBJMETHOD(g_raid_tr_kerneldump,	g_raid_tr_kerneldump_concat),
	KOBJMETHOD(g_raid_tr_free,	g_raid_tr_free_concat),
	{ 0, 0 }
};

static struct g_raid_tr_class g_raid_tr_concat_class = {
	"CONCAT",
	g_raid_tr_concat_methods,
	sizeof(struct g_raid_tr_concat_object),
	.trc_enable = 1,
	.trc_priority = 50,
	.trc_accept_unmapped = 1
};

static int
g_raid_tr_taste_concat(struct g_raid_tr_object *tr, struct g_raid_volume *volume)
{
	struct g_raid_tr_concat_object *trs;

	trs = (struct g_raid_tr_concat_object *)tr;
	if (tr->tro_volume->v_raid_level != G_RAID_VOLUME_RL_SINGLE &&
	    tr->tro_volume->v_raid_level != G_RAID_VOLUME_RL_CONCAT &&
	    !(tr->tro_volume->v_disks_count == 1 &&
	      tr->tro_volume->v_raid_level != G_RAID_VOLUME_RL_UNKNOWN))
		return (G_RAID_TR_TASTE_FAIL);
	trs->trso_starting = 1;
	return (G_RAID_TR_TASTE_SUCCEED);
}

static int
g_raid_tr_update_state_concat(struct g_raid_volume *vol)
{
	struct g_raid_tr_concat_object *trs;
	struct g_raid_softc *sc;
	off_t size;
	u_int s;
	int i, n, f;

	sc = vol->v_softc;
	trs = (struct g_raid_tr_concat_object *)vol->v_tr;
	if (trs->trso_stopped)
		s = G_RAID_VOLUME_S_STOPPED;
	else if (trs->trso_starting)
		s = G_RAID_VOLUME_S_STARTING;
	else {
		n = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_ACTIVE);
		f = g_raid_nsubdisks(vol, G_RAID_SUBDISK_S_FAILED);
		if (n + f == vol->v_disks_count) {
			if (f == 0)
				s = G_RAID_VOLUME_S_OPTIMAL;
			else
				s = G_RAID_VOLUME_S_SUBOPTIMAL;
		} else
			s = G_RAID_VOLUME_S_BROKEN;
	}
	if (s != vol->v_state) {

		/*
		 * Some metadata modules may not know CONCAT volume
		 * mediasize until all disks connected. Recalculate.
		 */
		if (vol->v_raid_level == G_RAID_VOLUME_RL_CONCAT &&
		    G_RAID_VOLUME_S_ALIVE(s) &&
		    !G_RAID_VOLUME_S_ALIVE(vol->v_state)) {
			size = 0;
			for (i = 0; i < vol->v_disks_count; i++) {
				if (vol->v_subdisks[i].sd_state !=
				    G_RAID_SUBDISK_S_NONE)
					size += vol->v_subdisks[i].sd_size;
			}
			vol->v_mediasize = size;
		}

		g_raid_event_send(vol, G_RAID_VOLUME_S_ALIVE(s) ?
		    G_RAID_VOLUME_E_UP : G_RAID_VOLUME_E_DOWN,
		    G_RAID_EVENT_VOLUME);
		g_raid_change_volume_state(vol, s);
		if (!trs->trso_starting && !trs->trso_stopped)
			g_raid_write_metadata(sc, vol, NULL, NULL);
	}
	return (0);
}

static int
g_raid_tr_event_concat(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd, u_int event)
{
	struct g_raid_tr_concat_object *trs;
	struct g_raid_softc *sc;
	struct g_raid_volume *vol;
	int state;

	trs = (struct g_raid_tr_concat_object *)tr;
	vol = tr->tro_volume;
	sc = vol->v_softc;

	state = sd->sd_state;
	if (state != G_RAID_SUBDISK_S_NONE &&
	    state != G_RAID_SUBDISK_S_FAILED &&
	    state != G_RAID_SUBDISK_S_ACTIVE) {
		G_RAID_DEBUG1(1, sc,
		    "Promote subdisk %s:%d from %s to ACTIVE.",
		    vol->v_name, sd->sd_pos,
		    g_raid_subdisk_state2str(sd->sd_state));
		g_raid_change_subdisk_state(sd, G_RAID_SUBDISK_S_ACTIVE);
	}
	if (state != sd->sd_state &&
	    !trs->trso_starting && !trs->trso_stopped)
		g_raid_write_metadata(sc, vol, sd, NULL);
	g_raid_tr_update_state_concat(vol);
	return (0);
}

static int
g_raid_tr_start_concat(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_concat_object *trs;
	struct g_raid_volume *vol;

	trs = (struct g_raid_tr_concat_object *)tr;
	vol = tr->tro_volume;
	trs->trso_starting = 0;
	g_raid_tr_update_state_concat(vol);
	return (0);
}

static int
g_raid_tr_stop_concat(struct g_raid_tr_object *tr)
{
	struct g_raid_tr_concat_object *trs;
	struct g_raid_volume *vol;

	trs = (struct g_raid_tr_concat_object *)tr;
	vol = tr->tro_volume;
	trs->trso_starting = 0;
	trs->trso_stopped = 1;
	g_raid_tr_update_state_concat(vol);
	return (0);
}

static void
g_raid_tr_iostart_concat(struct g_raid_tr_object *tr, struct bio *bp)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct bio_queue_head queue;
	struct bio *cbp;
	char *addr;
	off_t offset, length, remain;
	u_int no;

	vol = tr->tro_volume;
	if (vol->v_state != G_RAID_VOLUME_S_OPTIMAL &&
	    vol->v_state != G_RAID_VOLUME_S_SUBOPTIMAL) {
		g_raid_iodone(bp, EIO);
		return;
	}
	if (bp->bio_cmd == BIO_FLUSH) {
		g_raid_tr_flush_common(tr, bp);
		return;
	}

	offset = bp->bio_offset;
	remain = bp->bio_length;
	if ((bp->bio_flags & BIO_UNMAPPED) != 0)
		addr = NULL;
	else
		addr = bp->bio_data;
	no = 0;
	while (no < vol->v_disks_count &&
	    offset >= vol->v_subdisks[no].sd_size) {
		offset -= vol->v_subdisks[no].sd_size;
		no++;
	}
	KASSERT(no < vol->v_disks_count,
	    ("Request starts after volume end (%ju)", bp->bio_offset));
	bioq_init(&queue);
	do {
		sd = &vol->v_subdisks[no];
		length = MIN(sd->sd_size - offset, remain);
		cbp = g_clone_bio(bp);
		if (cbp == NULL)
			goto failure;
		cbp->bio_offset = offset;
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
		remain -= length;
		if (bp->bio_cmd != BIO_DELETE)
			addr += length;
		offset = 0;
		no++;
		KASSERT(no < vol->v_disks_count || remain == 0,
		    ("Request ends after volume end (%ju, %ju)",
			bp->bio_offset, bp->bio_length));
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

static int
g_raid_tr_kerneldump_concat(struct g_raid_tr_object *tr,
    void *virtual, vm_offset_t physical, off_t boffset, size_t blength)
{
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	char *addr;
	off_t offset, length, remain;
	int error, no;

	vol = tr->tro_volume;
	if (vol->v_state != G_RAID_VOLUME_S_OPTIMAL)
		return (ENXIO);

	offset = boffset;
	remain = blength;
	addr = virtual;
	no = 0;
	while (no < vol->v_disks_count &&
	    offset >= vol->v_subdisks[no].sd_size) {
		offset -= vol->v_subdisks[no].sd_size;
		no++;
	}
	KASSERT(no < vol->v_disks_count,
	    ("Request starts after volume end (%ju)", boffset));
	do {
		sd = &vol->v_subdisks[no];
		length = MIN(sd->sd_size - offset, remain);
		error = g_raid_subdisk_kerneldump(&vol->v_subdisks[no],
		    addr, 0, offset, length);
		if (error != 0)
			return (error);
		remain -= length;
		addr += length;
		offset = 0;
		no++;
		KASSERT(no < vol->v_disks_count || remain == 0,
		    ("Request ends after volume end (%ju, %zu)",
			boffset, blength));
	} while (remain > 0);
	return (0);
}

static void
g_raid_tr_iodone_concat(struct g_raid_tr_object *tr,
    struct g_raid_subdisk *sd,struct bio *bp)
{
	struct bio *pbp;

	pbp = bp->bio_parent;
	if (pbp->bio_error == 0)
		pbp->bio_error = bp->bio_error;
	g_destroy_bio(bp);
	pbp->bio_inbed++;
	if (pbp->bio_children == pbp->bio_inbed) {
		pbp->bio_completed = pbp->bio_length;
		g_raid_iodone(pbp, pbp->bio_error);
	}
}

static int
g_raid_tr_free_concat(struct g_raid_tr_object *tr)
{

	return (0);
}

G_RAID_TR_DECLARE(concat, "CONCAT");
