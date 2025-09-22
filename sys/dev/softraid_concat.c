/* $OpenBSD: softraid_concat.c,v 1.27 2020/04/25 14:37:43 krw Exp $ */
/*
 * Copyright (c) 2008 Marco Peereboom <marco@peereboom.us>
 * Copyright (c) 2011 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bio.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/queue.h>
#include <sys/sensors.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_disk.h>

#include <dev/softraidvar.h>

/* CONCAT functions. */
int	sr_concat_create(struct sr_discipline *, struct bioc_createraid *,
	    int, int64_t);
int	sr_concat_assemble(struct sr_discipline *, struct bioc_createraid *,
	    int, void *);
int	sr_concat_init(struct sr_discipline *);
int	sr_concat_rw(struct sr_workunit *);

/* Discipline initialisation. */
void
sr_concat_discipline_init(struct sr_discipline *sd)
{
	/* Fill out discipline members. */
	sd->sd_type = SR_MD_CONCAT;
	strlcpy(sd->sd_name, "CONCAT", sizeof(sd->sd_name));
	sd->sd_capabilities = SR_CAP_SYSTEM_DISK | SR_CAP_AUTO_ASSEMBLE |
	    SR_CAP_NON_COERCED;
	sd->sd_max_wu = SR_CONCAT_NOWU;

	/* Setup discipline specific function pointers. */
	sd->sd_assemble = sr_concat_assemble;
	sd->sd_create = sr_concat_create;
	sd->sd_scsi_rw = sr_concat_rw;
}

int
sr_concat_create(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, int64_t coerced_size)
{
	int i;

	if (no_chunk < 1) {
		sr_error(sd->sd_sc, "%s requires one or more chunks",
		    sd->sd_name);
		return EINVAL;
        }

	sd->sd_meta->ssdi.ssd_size = 0;
	for (i = 0; i < no_chunk; i++) {
		sd->sd_meta->ssdi.ssd_size +=
		    sd->sd_vol.sv_chunks[i]->src_size;
	}

	return sr_concat_init(sd);
}

int
sr_concat_assemble(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, void *data)
{
	return sr_concat_init(sd);
}

int
sr_concat_init(struct sr_discipline *sd)
{
	sd->sd_max_ccb_per_wu = SR_CONCAT_NOWU * sd->sd_meta->ssdi.ssd_chunk_no;

	return 0;
}

int
sr_concat_rw(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_ccb		*ccb;
	struct sr_chunk		*scp;
	daddr_t			blkno;
	int64_t			lbaoffs, offset;
	int64_t			no_chunk, chunkend, chunk, chunksize;
	int64_t			length, leftover;
	u_int8_t		*data;

	/* blkno and scsi error will be handled by sr_validate_io */
	if (sr_validate_io(wu, &blkno, "sr_concat_rw"))
		goto bad;

	no_chunk = sd->sd_meta->ssdi.ssd_chunk_no;

	DNPRINTF(SR_D_DIS, "%s: %s: front end io: blkno %lld size %d\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    (long long)blkno, xs->datalen);

	/* All offsets are in bytes. */
	lbaoffs = blkno << DEV_BSHIFT;
	leftover = xs->datalen;
	data = xs->data;
	for (;;) {

		chunkend = 0;
		offset = lbaoffs;
		for (chunk = 0; chunk < no_chunk; chunk++) {
			chunksize = sd->sd_vol.sv_chunks[chunk]->src_size <<
			    DEV_BSHIFT;
			chunkend += chunksize;
			if (lbaoffs < chunkend)
				break;
			offset -= chunksize;
		}
		if (lbaoffs > chunkend)
			goto bad;

		length = MIN(MIN(leftover, chunkend - lbaoffs), MAXPHYS);

		/* make sure chunk is online */
		scp = sd->sd_vol.sv_chunks[chunk];
		if (scp->src_meta.scm_status != BIOC_SDONLINE)
			goto bad;

		DNPRINTF(SR_D_DIS, "%s: %s %s io lbaoffs %lld "
		    "chunk %lld chunkend %lld offset %lld length %lld "
		    "leftover %lld data %p\n",
		    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname, sd->sd_name,
		    lbaoffs, chunk, chunkend, offset, length, leftover, data);

		blkno = offset >> DEV_BSHIFT;
		ccb = sr_ccb_rw(sd, chunk, blkno, length, data, xs->flags, 0);
		if (!ccb) {
			/* should never happen but handle more gracefully */
			printf("%s: %s: too many ccbs queued\n",
			    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname);
			goto bad;
		}
		sr_wu_enqueue_ccb(wu, ccb);

		leftover -= length;
		if (leftover == 0)
			break;
		data += length;
		lbaoffs += length;
	}

	sr_schedule_wu(wu);

	return (0);

bad:
	/* wu is unwound by sr_wu_put */
	return (1);
}
