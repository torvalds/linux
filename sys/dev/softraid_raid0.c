/* $OpenBSD: softraid_raid0.c,v 1.53 2020/03/25 21:29:04 tobhe Exp $ */
/*
 * Copyright (c) 2008 Marco Peereboom <marco@peereboom.us>
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
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/disk.h>
#include <sys/rwlock.h>
#include <sys/queue.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/sensors.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/uio.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_disk.h>

#include <dev/softraidvar.h>

/* RAID 0 functions. */
int	sr_raid0_create(struct sr_discipline *, struct bioc_createraid *,
	    int, int64_t);
int	sr_raid0_assemble(struct sr_discipline *, struct bioc_createraid *,
	    int, void *);
int	sr_raid0_init(struct sr_discipline *);
int	sr_raid0_rw(struct sr_workunit *);

/* Discipline initialisation. */
void
sr_raid0_discipline_init(struct sr_discipline *sd)
{

	/* Fill out discipline members. */
	sd->sd_type = SR_MD_RAID0;
	strlcpy(sd->sd_name, "RAID 0", sizeof(sd->sd_name));
	sd->sd_capabilities = SR_CAP_SYSTEM_DISK | SR_CAP_AUTO_ASSEMBLE;
	sd->sd_max_wu = SR_RAID0_NOWU;

	/* Setup discipline specific function pointers. */
	sd->sd_assemble = sr_raid0_assemble;
	sd->sd_create = sr_raid0_create;
	sd->sd_scsi_rw = sr_raid0_rw;
}

int
sr_raid0_create(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, int64_t coerced_size)
{
	if (no_chunk < 2) {
		sr_error(sd->sd_sc, "%s requires two or more chunks",
		    sd->sd_name);
		return EINVAL;
        }

	/*
	 * XXX add variable strip size later even though MAXPHYS is really
	 * the clever value, users like to tinker with that type of stuff.
	 */
	sd->sd_meta->ssdi.ssd_strip_size = MAXPHYS;
	sd->sd_meta->ssdi.ssd_size = (coerced_size &
	    ~(((u_int64_t)sd->sd_meta->ssdi.ssd_strip_size >>
	    DEV_BSHIFT) - 1)) * no_chunk;

	return sr_raid0_init(sd);
}

int
sr_raid0_assemble(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunks, void *data)
{
	return sr_raid0_init(sd);
}

int
sr_raid0_init(struct sr_discipline *sd)
{
	/* Initialise runtime values. */
	sd->mds.mdd_raid0.sr0_strip_bits =
	    sr_validate_stripsize(sd->sd_meta->ssdi.ssd_strip_size);
	if (sd->mds.mdd_raid0.sr0_strip_bits == -1) {
		sr_error(sd->sd_sc, "%s: invalid strip size", sd->sd_name);
		return EINVAL;
	}
	sd->sd_max_ccb_per_wu =
	    (MAXPHYS / sd->sd_meta->ssdi.ssd_strip_size + 1) *
	    SR_RAID0_NOWU * sd->sd_meta->ssdi.ssd_chunk_no;

	return 0;
}

int
sr_raid0_rw(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_ccb		*ccb;
	struct sr_chunk		*scp;
	daddr_t			blkno;
	int64_t			chunkoffs, lbaoffs, offset, stripoffs;
	int64_t			strip_bits, strip_no, strip_size;
	int64_t			chunk, no_chunk;
	int64_t			length, leftover;
	u_int8_t		*data;

	/* blkno and scsi error will be handled by sr_validate_io */
	if (sr_validate_io(wu, &blkno, "sr_raid0_rw"))
		goto bad;

	strip_size = sd->sd_meta->ssdi.ssd_strip_size;
	strip_bits = sd->mds.mdd_raid0.sr0_strip_bits;
	no_chunk = sd->sd_meta->ssdi.ssd_chunk_no;

	DNPRINTF(SR_D_DIS, "%s: %s: front end io: blkno %lld size %d\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    (long long)blkno, xs->datalen);

	/* all offs are in bytes */
	lbaoffs = blkno << DEV_BSHIFT;
	strip_no = lbaoffs >> strip_bits;
	chunk = strip_no % no_chunk;
	stripoffs = lbaoffs & (strip_size - 1);
	chunkoffs = (strip_no / no_chunk) << strip_bits;
	offset = chunkoffs + stripoffs;
	length = MIN(xs->datalen, strip_size - stripoffs);
	leftover = xs->datalen;
	data = xs->data;
	for (;;) {
		/* make sure chunk is online */
		scp = sd->sd_vol.sv_chunks[chunk];
		if (scp->src_meta.scm_status != BIOC_SDONLINE)
			goto bad;

		DNPRINTF(SR_D_DIS, "%s: %s %s io lbaoffs %lld "
		    "strip_no %lld chunk %lld stripoffs %lld "
		    "chunkoffs %lld offset %lld length %lld "
		    "leftover %lld data %p\n",
		    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname, sd->sd_name,
		    lbaoffs, strip_no, chunk, stripoffs, chunkoffs, offset,
		    length, leftover, data);

		blkno = offset >> DEV_BSHIFT;
		ccb = sr_ccb_rw(sd, chunk, blkno, length, data, xs->flags, 0);
		if (!ccb) {
			/* should never happen but handle more gracefully */
			printf("%s: %s: too many ccbs queued\n",
			    DEVNAME(sd->sd_sc),
			    sd->sd_meta->ssd_devname);
			goto bad;
		}
		sr_wu_enqueue_ccb(wu, ccb);

		leftover -= length;
		if (leftover == 0)
			break;

		data += length;
		if (++chunk > no_chunk - 1) {
			chunk = 0;
			offset += length;
		} else if (wu->swu_io_count == 1)
			offset -= stripoffs;
		length = MIN(leftover,strip_size);
	}

	sr_schedule_wu(wu);

	return (0);

bad:
	/* wu is unwound by sr_wu_put */
	return (1);
}
