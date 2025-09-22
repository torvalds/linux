/* $OpenBSD: softraid_raid5.c,v 1.32 2021/05/16 15:12:37 deraadt Exp $ */
/*
 * Copyright (c) 2014 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2009 Marco Peereboom <marco@peereboom.us>
 * Copyright (c) 2009 Jordan Hargrave <jordan@openbsd.org>
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
#include <sys/task.h>
#include <sys/pool.h>
#include <sys/conf.h>
#include <sys/uio.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_disk.h>

#include <dev/softraidvar.h>

/* RAID 5 functions. */
int	sr_raid5_create(struct sr_discipline *, struct bioc_createraid *,
	    int, int64_t);
int	sr_raid5_assemble(struct sr_discipline *, struct bioc_createraid *,
	    int, void *);
int	sr_raid5_init(struct sr_discipline *);
int	sr_raid5_rw(struct sr_workunit *);
int	sr_raid5_openings(struct sr_discipline *);
void	sr_raid5_intr(struct buf *);
int	sr_raid5_wu_done(struct sr_workunit *);
void	sr_raid5_set_chunk_state(struct sr_discipline *, int, int);
void	sr_raid5_set_vol_state(struct sr_discipline *);

int	sr_raid5_addio(struct sr_workunit *wu, int, daddr_t, long,
	    void *, int, int, void *);
int	sr_raid5_regenerate(struct sr_workunit *, int, daddr_t, long,
	    void *);
int	sr_raid5_write(struct sr_workunit *, struct sr_workunit *, int, int,
	    daddr_t, long, void *, int, int);
void	sr_raid5_xor(void *, void *, int);

void	sr_raid5_rebuild(struct sr_discipline *);
void	sr_raid5_scrub(struct sr_discipline *);

/* discipline initialisation. */
void
sr_raid5_discipline_init(struct sr_discipline *sd)
{
	/* Fill out discipline members. */
	sd->sd_type = SR_MD_RAID5;
	strlcpy(sd->sd_name, "RAID 5", sizeof(sd->sd_name));
	sd->sd_capabilities = SR_CAP_SYSTEM_DISK | SR_CAP_AUTO_ASSEMBLE |
	    SR_CAP_REBUILD | SR_CAP_REDUNDANT;
	sd->sd_max_wu = SR_RAID5_NOWU + 2;	/* Two for scrub/rebuild. */

	/* Setup discipline specific function pointers. */
	sd->sd_assemble = sr_raid5_assemble;
	sd->sd_create = sr_raid5_create;
	sd->sd_openings = sr_raid5_openings;
	sd->sd_rebuild = sr_raid5_rebuild;
	sd->sd_scsi_rw = sr_raid5_rw;
	sd->sd_scsi_intr = sr_raid5_intr;
	sd->sd_scsi_wu_done = sr_raid5_wu_done;
	sd->sd_set_chunk_state = sr_raid5_set_chunk_state;
	sd->sd_set_vol_state = sr_raid5_set_vol_state;
}

int
sr_raid5_create(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, int64_t coerced_size)
{
	if (no_chunk < 3) {
		sr_error(sd->sd_sc, "%s requires three or more chunks",
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
	    DEV_BSHIFT) - 1)) * (no_chunk - 1);

	return sr_raid5_init(sd);
}

int
sr_raid5_assemble(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, void *data)
{
	return sr_raid5_init(sd);
}

int
sr_raid5_init(struct sr_discipline *sd)
{
	/* Initialise runtime values. */
	sd->mds.mdd_raid5.sr5_strip_bits =
	    sr_validate_stripsize(sd->sd_meta->ssdi.ssd_strip_size);
	if (sd->mds.mdd_raid5.sr5_strip_bits == -1) {
		sr_error(sd->sd_sc, "invalid strip size");
		return EINVAL;
	}

	sd->sd_max_ccb_per_wu = sd->sd_meta->ssdi.ssd_chunk_no;

	return 0;
}

int
sr_raid5_openings(struct sr_discipline *sd)
{
	/* Two work units per I/O, two for rebuild/scrub. */
	return ((sd->sd_max_wu - 2) >> 1);
}

void
sr_raid5_set_chunk_state(struct sr_discipline *sd, int c, int new_state)
{
	int			old_state, s;

	DNPRINTF(SR_D_STATE, "%s: %s: %s: sr_raid_set_chunk_state %d -> %d\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    sd->sd_vol.sv_chunks[c]->src_meta.scmi.scm_devname, c, new_state);

	/* ok to go to splbio since this only happens in error path */
	s = splbio();
	old_state = sd->sd_vol.sv_chunks[c]->src_meta.scm_status;

	/* multiple IOs to the same chunk that fail will come through here */
	if (old_state == new_state)
		goto done;

	switch (old_state) {
	case BIOC_SDONLINE:
		switch (new_state) {
		case BIOC_SDOFFLINE:
		case BIOC_SDSCRUB:
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SDOFFLINE:
		if (new_state == BIOC_SDREBUILD) {
			;
		} else
			goto die;
		break;

	case BIOC_SDSCRUB:
		switch (new_state) {
		case BIOC_SDONLINE:
		case BIOC_SDOFFLINE:
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SDREBUILD:
		switch (new_state) {
		case BIOC_SDONLINE:
		case BIOC_SDOFFLINE:
			break;
		default:
			goto die;
		}
		break;

	default:
die:
		splx(s); /* XXX */
		panic("%s: %s: %s: invalid chunk state transition %d -> %d",
		    DEVNAME(sd->sd_sc),
		    sd->sd_meta->ssd_devname,
		    sd->sd_vol.sv_chunks[c]->src_meta.scmi.scm_devname,
		    old_state, new_state);
		/* NOTREACHED */
	}

	sd->sd_vol.sv_chunks[c]->src_meta.scm_status = new_state;
	sd->sd_set_vol_state(sd);

	sd->sd_must_flush = 1;
	task_add(systq, &sd->sd_meta_save_task);
done:
	splx(s);
}

void
sr_raid5_set_vol_state(struct sr_discipline *sd)
{
	int			states[SR_MAX_STATES];
	int			new_state, i, s, nd;
	int			old_state = sd->sd_vol_status;

	DNPRINTF(SR_D_STATE, "%s: %s: sr_raid_set_vol_state\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname);

	nd = sd->sd_meta->ssdi.ssd_chunk_no;

	for (i = 0; i < SR_MAX_STATES; i++)
		states[i] = 0;

	for (i = 0; i < nd; i++) {
		s = sd->sd_vol.sv_chunks[i]->src_meta.scm_status;
		if (s >= SR_MAX_STATES)
			panic("%s: %s: %s: invalid chunk state",
			    DEVNAME(sd->sd_sc),
			    sd->sd_meta->ssd_devname,
			    sd->sd_vol.sv_chunks[i]->src_meta.scmi.scm_devname);
		states[s]++;
	}

	if (states[BIOC_SDONLINE] == nd)
		new_state = BIOC_SVONLINE;
	else if (states[BIOC_SDONLINE] < nd - 1)
		new_state = BIOC_SVOFFLINE;
	else if (states[BIOC_SDSCRUB] != 0)
		new_state = BIOC_SVSCRUB;
	else if (states[BIOC_SDREBUILD] != 0)
		new_state = BIOC_SVREBUILD;
	else if (states[BIOC_SDONLINE] == nd - 1)
		new_state = BIOC_SVDEGRADED;
	else {
#ifdef SR_DEBUG
		DNPRINTF(SR_D_STATE, "%s: invalid volume state, old state "
		    "was %d\n", DEVNAME(sd->sd_sc), old_state);
		for (i = 0; i < nd; i++)
			DNPRINTF(SR_D_STATE, "%s: chunk %d status = %d\n",
			    DEVNAME(sd->sd_sc), i,
			    sd->sd_vol.sv_chunks[i]->src_meta.scm_status);
#endif
		panic("invalid volume state");
	}

	DNPRINTF(SR_D_STATE, "%s: %s: sr_raid5_set_vol_state %d -> %d\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    old_state, new_state);

	switch (old_state) {
	case BIOC_SVONLINE:
		switch (new_state) {
		case BIOC_SVONLINE: /* can go to same state */
		case BIOC_SVOFFLINE:
		case BIOC_SVDEGRADED:
		case BIOC_SVREBUILD: /* happens on boot */
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SVOFFLINE:
		/* XXX this might be a little too much */
		goto die;

	case BIOC_SVDEGRADED:
		switch (new_state) {
		case BIOC_SVOFFLINE:
		case BIOC_SVREBUILD:
		case BIOC_SVDEGRADED: /* can go to the same state */
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SVBUILDING:
		switch (new_state) {
		case BIOC_SVONLINE:
		case BIOC_SVOFFLINE:
		case BIOC_SVBUILDING: /* can go to the same state */
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SVSCRUB:
		switch (new_state) {
		case BIOC_SVONLINE:
		case BIOC_SVOFFLINE:
		case BIOC_SVDEGRADED:
		case BIOC_SVSCRUB: /* can go to same state */
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SVREBUILD:
		switch (new_state) {
		case BIOC_SVONLINE:
		case BIOC_SVOFFLINE:
		case BIOC_SVDEGRADED:
		case BIOC_SVREBUILD: /* can go to the same state */
			break;
		default:
			goto die;
		}
		break;

	default:
die:
		panic("%s: %s: invalid volume state transition %d -> %d",
		    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
		    old_state, new_state);
		/* NOTREACHED */
	}

	sd->sd_vol_status = new_state;
}

static inline int
sr_raid5_chunk_online(struct sr_discipline *sd, int chunk)
{
	switch (sd->sd_vol.sv_chunks[chunk]->src_meta.scm_status) {
	case BIOC_SDONLINE:
	case BIOC_SDSCRUB:
		return 1;
	default:
		return 0;
	}
}

static inline int
sr_raid5_chunk_rebuild(struct sr_discipline *sd, int chunk)
{
	switch (sd->sd_vol.sv_chunks[chunk]->src_meta.scm_status) {
	case BIOC_SDREBUILD:
		return 1;
	default:
		return 0;
	}
}

int
sr_raid5_rw(struct sr_workunit *wu)
{
	struct sr_workunit	*wu_r = NULL;
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_chunk		*scp;
	daddr_t			blkno, lba;
	int64_t			chunk_offs, lbaoffs, offset, strip_offs;
	int64_t			strip_bits, strip_no, strip_size;
	int64_t			chunk, no_chunk;
	int64_t			parity, row_size;
	long			length, datalen;
	void			*data;
	int			s;

	/* blkno and scsi error will be handled by sr_validate_io */
	if (sr_validate_io(wu, &blkno, "sr_raid5_rw"))
		goto bad;

	DNPRINTF(SR_D_DIS, "%s: %s sr_raid5_rw %s: blkno %lld size %d\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    (xs->flags & SCSI_DATA_IN) ? "read" : "write",
	    (long long)blkno, xs->datalen);

	strip_size = sd->sd_meta->ssdi.ssd_strip_size;
	strip_bits = sd->mds.mdd_raid5.sr5_strip_bits;
	no_chunk = sd->sd_meta->ssdi.ssd_chunk_no - 1;
	row_size = (no_chunk << strip_bits) >> DEV_BSHIFT;

	data = xs->data;
	datalen = xs->datalen;
	lbaoffs	= blkno << DEV_BSHIFT;

	if (xs->flags & SCSI_DATA_OUT) {
		if ((wu_r = sr_scsi_wu_get(sd, SCSI_NOSLEEP)) == NULL){
			printf("%s: %s failed to get read work unit",
			    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname);
			goto bad;
		}
		wu_r->swu_state = SR_WU_INPROGRESS;
		wu_r->swu_flags |= SR_WUF_DISCIPLINE;
	}

	wu->swu_blk_start = 0;
	while (datalen != 0) {
		strip_no = lbaoffs >> strip_bits;
		strip_offs = lbaoffs & (strip_size - 1);
		chunk_offs = (strip_no / no_chunk) << strip_bits;
		offset = chunk_offs + strip_offs;

		/* get size remaining in this stripe */
		length = MIN(strip_size - strip_offs, datalen);

		/*
		 * Map disk offset to data and parity chunks, using a left
		 * asymmetric algorithm for the parity assignment.
		 */
		chunk = strip_no % no_chunk;
		parity = no_chunk - ((strip_no / no_chunk) % (no_chunk + 1));
		if (chunk >= parity)
			chunk++;

		lba = offset >> DEV_BSHIFT;

		/* XXX big hammer.. exclude I/O from entire stripe */
		if (wu->swu_blk_start == 0)
			wu->swu_blk_start = (strip_no / no_chunk) * row_size;
		wu->swu_blk_end = (strip_no / no_chunk) * row_size +
		    (row_size - 1);

		scp = sd->sd_vol.sv_chunks[chunk];
		if (xs->flags & SCSI_DATA_IN) {
			switch (scp->src_meta.scm_status) {
			case BIOC_SDONLINE:
			case BIOC_SDSCRUB:
				/*
				 * Chunk is online, issue a single read
				 * request.
				 */
				if (sr_raid5_addio(wu, chunk, lba, length,
				    data, xs->flags, 0, NULL))
					goto bad;
				break;
			case BIOC_SDOFFLINE:
			case BIOC_SDREBUILD:
			case BIOC_SDHOTSPARE:
				if (sr_raid5_regenerate(wu, chunk, lba,
				    length, data))
					goto bad;
				break;
			default:
				printf("%s: is offline, can't read\n",
				    DEVNAME(sd->sd_sc));
				goto bad;
			}
		} else {
			if (sr_raid5_write(wu, wu_r, chunk, parity, lba,
			    length, data, xs->flags, 0))
				goto bad;
		}

		/* advance to next block */
		lbaoffs += length;
		datalen -= length;
		data += length;
	}

	s = splbio();
	if (wu_r) {
		if (wu_r->swu_io_count > 0) {
			/* collide write request with reads */
			wu_r->swu_blk_start = wu->swu_blk_start;
			wu_r->swu_blk_end = wu->swu_blk_end;

			wu->swu_state = SR_WU_DEFERRED;
			wu_r->swu_collider = wu;
			TAILQ_INSERT_TAIL(&sd->sd_wu_defq, wu, swu_link);

			wu = wu_r;
		} else {
			sr_scsi_wu_put(sd, wu_r);
		}
	}
	splx(s);

	sr_schedule_wu(wu);

	return (0);

bad:
	/* wu is unwound by sr_wu_put */
	if (wu_r)
		sr_scsi_wu_put(sd, wu_r);
	return (1);
}

int
sr_raid5_regenerate(struct sr_workunit *wu, int chunk, daddr_t blkno,
    long len, void *data)
{
	struct sr_discipline	*sd = wu->swu_dis;
	int			i;

	/*
	 * Regenerate a block on a RAID 5 volume by xoring the data and parity
	 * from all of the remaining online chunks. This requires the parity
	 * to already be correct.
	 */

	DNPRINTF(SR_D_DIS, "%s: %s sr_raid5_regenerate chunk %d offline, "
	    "regenerating block %llu\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname, chunk, blkno);

	memset(data, 0, len);
	for (i = 0; i < sd->sd_meta->ssdi.ssd_chunk_no; i++) {
		if (i == chunk)
			continue;
		if (!sr_raid5_chunk_online(sd, i))
			goto bad;
		if (sr_raid5_addio(wu, i, blkno, len, NULL, SCSI_DATA_IN,
		    0, data))
			goto bad;
	}
	return (0);

bad:
	return (1);
}

int
sr_raid5_write(struct sr_workunit *wu, struct sr_workunit *wu_r, int chunk,
    int parity, daddr_t blkno, long len, void *data, int xsflags,
    int ccbflags)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	void			*xorbuf;
	int			chunk_online, chunk_rebuild;
	int			parity_online, parity_rebuild;
	int			other_offline = 0, other_rebuild = 0;
	int			i;

	/*
	 * Perform a write to a RAID 5 volume. This write routine does not
	 * require the parity to already be correct and will operate on a
	 * uninitialised volume.
	 *
	 * There are four possible cases:
	 *
	 * 1) All data chunks and parity are online. In this case we read the
	 *    data from all data chunks, except the one we are writing to, in
	 *    order to calculate and write the new parity.
	 *
	 * 2) The parity chunk is offline. In this case we only need to write
	 *    to the data chunk. No parity calculation is required.
	 *
	 * 3) The data chunk is offline. In this case we read the data from all
	 *    online chunks in order to calculate and write the new parity.
	 *    This is the same as (1) except we do not write the data chunk.
	 *
	 * 4) A different data chunk is offline. The new parity is calculated
	 *    by taking the existing parity, xoring the original data and
	 *    xoring in the new data. This requires that the parity already be
	 *    correct, which it will be if any of the data chunks has
	 *    previously been written.
	 *
	 * There is an additional complication introduced by a chunk that is
	 * being rebuilt. If this is the data or parity chunk, then we want
	 * to write to it as per normal. If it is another data chunk then we
	 * need to presume that it has not yet been regenerated and use the
	 * same method as detailed in (4) above.
	 */

	DNPRINTF(SR_D_DIS, "%s: %s sr_raid5_write chunk %i parity %i "
	    "blkno %llu\n", DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    chunk, parity, (unsigned long long)blkno);

	chunk_online = sr_raid5_chunk_online(sd, chunk);
	chunk_rebuild = sr_raid5_chunk_rebuild(sd, chunk);
	parity_online = sr_raid5_chunk_online(sd, parity);
	parity_rebuild = sr_raid5_chunk_rebuild(sd, parity);

	for (i = 0; i < sd->sd_meta->ssdi.ssd_chunk_no; i++) {
		if (i == chunk || i == parity)
			continue;
		if (sr_raid5_chunk_rebuild(sd, i))
			other_rebuild = 1;
		else if (!sr_raid5_chunk_online(sd, i))
			other_offline = 1;
	}

	DNPRINTF(SR_D_DIS, "%s: %s chunk online %d, parity online %d, "
	    "other offline %d\n", DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    chunk_online, parity_online, other_offline);

	if (!parity_online && !parity_rebuild)
		goto data_write;

	xorbuf = sr_block_get(sd, len);
	if (xorbuf == NULL)
		goto bad;
	memcpy(xorbuf, data, len);

	if (other_offline || other_rebuild) {

		/*
		 * XXX - If we can guarantee that this LBA has been scrubbed
		 * then we can also take this faster path.
		 */

		/* Read in existing data and existing parity. */
		if (sr_raid5_addio(wu_r, chunk, blkno, len, NULL,
		    SCSI_DATA_IN, 0, xorbuf))
			goto bad;
		if (sr_raid5_addio(wu_r, parity, blkno, len, NULL,
		    SCSI_DATA_IN, 0, xorbuf))
			goto bad;

	} else {

		/* Read in existing data from all other chunks. */
		for (i = 0; i < sd->sd_meta->ssdi.ssd_chunk_no; i++) {
			if (i == chunk || i == parity)
				continue;
			if (sr_raid5_addio(wu_r, i, blkno, len, NULL,
			    SCSI_DATA_IN, 0, xorbuf))
				goto bad;
		}

	}

	/* Write new parity. */
	if (sr_raid5_addio(wu, parity, blkno, len, xorbuf, xs->flags,
	    SR_CCBF_FREEBUF, NULL))
		goto bad;

data_write:
	/* Write new data. */
	if (chunk_online || chunk_rebuild)
		if (sr_raid5_addio(wu, chunk, blkno, len, data, xs->flags,
		    0, NULL))
			goto bad;

	return (0);

bad:
	return (1);
}

void
sr_raid5_intr(struct buf *bp)
{
	struct sr_ccb		*ccb = (struct sr_ccb *)bp;
	struct sr_workunit	*wu = ccb->ccb_wu;
	struct sr_discipline	*sd = wu->swu_dis;
	int			s;

	DNPRINTF(SR_D_INTR, "%s: sr_raid5_intr bp %p xs %p\n",
	    DEVNAME(sd->sd_sc), bp, wu->swu_xs);

	s = splbio();
	sr_ccb_done(ccb);

	/* XXX - Should this be done via the taskq? */

	/* XOR data to result. */
	if (ccb->ccb_state == SR_CCB_OK && ccb->ccb_opaque)
		sr_raid5_xor(ccb->ccb_opaque, ccb->ccb_buf.b_data,
		    ccb->ccb_buf.b_bcount);

	/* Free allocated data buffer. */
	if (ccb->ccb_flags & SR_CCBF_FREEBUF) {
		sr_block_put(sd, ccb->ccb_buf.b_data, ccb->ccb_buf.b_bcount);
		ccb->ccb_buf.b_data = NULL;
	}

	sr_wu_done(wu);
	splx(s);
}

int
sr_raid5_wu_done(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;

	/* XXX - we have no way of propagating errors... */
	if (wu->swu_flags & (SR_WUF_DISCIPLINE | SR_WUF_REBUILD))
		return SR_WU_OK;

	/* XXX - This is insufficient for RAID 5. */
	if (wu->swu_ios_succeeded > 0) {
		xs->error = XS_NOERROR;
		return SR_WU_OK;
	}

	if (xs->flags & SCSI_DATA_IN) {
		printf("%s: retrying read on block %lld\n",
		    sd->sd_meta->ssd_devname, (long long)wu->swu_blk_start);
		sr_wu_release_ccbs(wu);
		wu->swu_state = SR_WU_RESTART;
		if (sd->sd_scsi_rw(wu) == 0)
			return SR_WU_RESTART;
	} else {
		/* XXX - retry write if we just went from online to degraded. */
		printf("%s: permanently fail write on block %lld\n",
		    sd->sd_meta->ssd_devname, (long long)wu->swu_blk_start);
	}

	wu->swu_state = SR_WU_FAILED;
	xs->error = XS_DRIVER_STUFFUP;

	return SR_WU_FAILED;
}

int
sr_raid5_addio(struct sr_workunit *wu, int chunk, daddr_t blkno,
    long len, void *data, int xsflags, int ccbflags, void *xorbuf)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct sr_ccb		*ccb;

	DNPRINTF(SR_D_DIS, "sr_raid5_addio: %s chunk %d block %lld "
	    "length %ld %s\n", (xsflags & SCSI_DATA_IN) ? "read" : "write",
	    chunk, (long long)blkno, len, xorbuf ? "X0R" : "-");

	/* Allocate temporary buffer. */
	if (data == NULL) {
		data = sr_block_get(sd, len);
		if (data == NULL)
			return (-1);
		ccbflags |= SR_CCBF_FREEBUF;
	}

	ccb = sr_ccb_rw(sd, chunk, blkno, len, data, xsflags, ccbflags);
	if (ccb == NULL) {
		if (ccbflags & SR_CCBF_FREEBUF)
			sr_block_put(sd, data, len);
		return (-1);
	}
	ccb->ccb_opaque = xorbuf;
	sr_wu_enqueue_ccb(wu, ccb);

	return (0);
}

void
sr_raid5_xor(void *a, void *b, int len)
{
	uint32_t		*xa = a, *xb = b;

	len >>= 2;
	while (len--)
		*xa++ ^= *xb++;
}

void
sr_raid5_rebuild(struct sr_discipline *sd)
{
	int64_t strip_no, strip_size, strip_bits, i, restart;
	int64_t chunk_count, chunk_strips, chunk_lba, chunk_size, row_size;
	struct sr_workunit *wu_r, *wu_w;
	int s, slept, percent = 0, old_percent = -1;
	int rebuild_chunk = -1;
	void *xorbuf;

	/* Find the rebuild chunk. */
	for (i = 0; i < sd->sd_meta->ssdi.ssd_chunk_no; i++) {
		if (sr_raid5_chunk_rebuild(sd, i)) {
			rebuild_chunk = i;
			break;
		}
	}
	if (rebuild_chunk == -1)
		goto bad;

	strip_size = sd->sd_meta->ssdi.ssd_strip_size;
	strip_bits = sd->mds.mdd_raid5.sr5_strip_bits;
	chunk_count = sd->sd_meta->ssdi.ssd_chunk_no - 1;
	chunk_size = sd->sd_meta->ssdi.ssd_size / chunk_count;
	chunk_strips = (chunk_size << DEV_BSHIFT) >> strip_bits;
	row_size = (chunk_count << strip_bits) >> DEV_BSHIFT;

	DNPRINTF(SR_D_REBUILD, "%s: %s sr_raid5_rebuild volume size = %lld, "
	    "chunk count = %lld, chunk size = %lld, chunk strips = %lld, "
	    "row size = %lld\n", DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname,
	    sd->sd_meta->ssdi.ssd_size, chunk_count, chunk_size, chunk_strips,
	    row_size);

	restart = sd->sd_meta->ssd_rebuild / row_size;
	if (restart > chunk_strips) {
		printf("%s: bogus rebuild restart offset, starting from 0\n",
		    DEVNAME(sd->sd_sc));
		restart = 0;
	}
	if (restart != 0) {
		percent = sr_rebuild_percent(sd);
		printf("%s: resuming rebuild on %s at %d%%\n",
		    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname, percent);
	}

	for (strip_no = restart; strip_no < chunk_strips; strip_no++) {
		chunk_lba = (strip_size >> DEV_BSHIFT) * strip_no;

		DNPRINTF(SR_D_REBUILD, "%s: %s rebuild strip %lld, "
		    "chunk lba = %lld\n", DEVNAME(sd->sd_sc),
		    sd->sd_meta->ssd_devname, strip_no, chunk_lba);

		wu_w = sr_scsi_wu_get(sd, 0);
		wu_r = sr_scsi_wu_get(sd, 0);

		xorbuf = sr_block_get(sd, strip_size);
		if (xorbuf == NULL)
			goto bad;
		if (sr_raid5_regenerate(wu_r, rebuild_chunk, chunk_lba,
		    strip_size, xorbuf))
			goto bad;
		if (sr_raid5_addio(wu_w, rebuild_chunk, chunk_lba, strip_size,
		    xorbuf, SCSI_DATA_OUT, SR_CCBF_FREEBUF, NULL))
			goto bad;

		/* Collide write work unit with read work unit. */
		wu_r->swu_state = SR_WU_INPROGRESS;
		wu_r->swu_flags |= SR_WUF_REBUILD;
		wu_w->swu_state = SR_WU_DEFERRED;
		wu_w->swu_flags |= SR_WUF_REBUILD | SR_WUF_WAKEUP;
		wu_r->swu_collider = wu_w;

		/* Block I/O to this strip while we rebuild it. */
		wu_r->swu_blk_start = (strip_no / chunk_count) * row_size;
		wu_r->swu_blk_end = wu_r->swu_blk_start + row_size - 1;
		wu_w->swu_blk_start = wu_r->swu_blk_start;
		wu_w->swu_blk_end = wu_r->swu_blk_end;

		DNPRINTF(SR_D_REBUILD, "%s: %s rebuild swu_blk_start = %lld, "
		    "swu_blk_end = %lld\n", DEVNAME(sd->sd_sc),
		    sd->sd_meta->ssd_devname,
		    wu_r->swu_blk_start, wu_r->swu_blk_end);

		s = splbio();
		TAILQ_INSERT_TAIL(&sd->sd_wu_defq, wu_w, swu_link);
		splx(s);

		sr_schedule_wu(wu_r);

		slept = 0;
		while ((wu_w->swu_flags & SR_WUF_REBUILDIOCOMP) == 0) {
			tsleep_nsec(wu_w, PRIBIO, "sr_rebuild", INFSLP);
			slept = 1;
		}
		if (!slept) {
			tsleep_nsec(sd->sd_sc, PWAIT, "sr_yield",
			    MSEC_TO_NSEC(1));
		}

		sr_scsi_wu_put(sd, wu_r);
		sr_scsi_wu_put(sd, wu_w);

		sd->sd_meta->ssd_rebuild = chunk_lba * chunk_count;

		percent = sr_rebuild_percent(sd);
		if (percent != old_percent && strip_no != chunk_strips - 1) {
			if (sr_meta_save(sd, SR_META_DIRTY))
				printf("%s: could not save metadata to %s\n",
				    DEVNAME(sd->sd_sc),
				    sd->sd_meta->ssd_devname);
			old_percent = percent;
		}

		if (sd->sd_reb_abort)
			goto abort;
	}

	DNPRINTF(SR_D_REBUILD, "%s: %s rebuild complete\n", DEVNAME(sd->sd_sc),
	    sd->sd_meta->ssd_devname);

	/* all done */
	sd->sd_meta->ssd_rebuild = 0;
	for (i = 0; i < sd->sd_meta->ssdi.ssd_chunk_no; i++) {
		if (sd->sd_vol.sv_chunks[i]->src_meta.scm_status ==
		    BIOC_SDREBUILD) {
			sd->sd_set_chunk_state(sd, i, BIOC_SDONLINE);
			break;
		}
	}

	return;

abort:
	if (sr_meta_save(sd, SR_META_DIRTY))
		printf("%s: could not save metadata to %s\n",
		    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname);
bad:
	return;
}

#if 0
void
sr_raid5_scrub(struct sr_discipline *sd)
{
	int64_t strip_no, strip_size, no_chunk, parity, max_strip, strip_bits;
	int64_t i;
	struct sr_workunit *wu_r, *wu_w;
	int s, slept;
	void *xorbuf;

	wu_w = sr_scsi_wu_get(sd, 0);
	wu_r = sr_scsi_wu_get(sd, 0);

	no_chunk = sd->sd_meta->ssdi.ssd_chunk_no - 1;
	strip_size = sd->sd_meta->ssdi.ssd_strip_size;
	strip_bits = sd->mds.mdd_raid5.sr5_strip_bits;
	max_strip = sd->sd_meta->ssdi.ssd_size >> strip_bits;

	for (strip_no = 0; strip_no < max_strip; strip_no++) {
		parity = no_chunk - ((strip_no / no_chunk) % (no_chunk + 1));

		xorbuf = sr_block_get(sd, strip_size);
		for (i = 0; i <= no_chunk; i++) {
			if (i != parity)
				sr_raid5_addio(wu_r, i, 0xBADCAFE, strip_size,
				    NULL, SCSI_DATA_IN, 0, xorbuf);
		}
		sr_raid5_addio(wu_w, parity, 0xBADCAFE, strip_size, xorbuf,
		    SCSI_DATA_OUT, SR_CCBF_FREEBUF, NULL);

		wu_r->swu_flags |= SR_WUF_REBUILD;

		/* Collide wu_w with wu_r */
		wu_w->swu_state = SR_WU_DEFERRED;
		wu_w->swu_flags |= SR_WUF_REBUILD | SR_WUF_WAKEUP;
		wu_r->swu_collider = wu_w;

		s = splbio();
		TAILQ_INSERT_TAIL(&sd->sd_wu_defq, wu_w, swu_link);
		splx(s);

		wu_r->swu_state = SR_WU_INPROGRESS;
		sr_schedule_wu(wu_r);

		slept = 0;
		while ((wu_w->swu_flags & SR_WUF_REBUILDIOCOMP) == 0) {
			tsleep_nsec(wu_w, PRIBIO, "sr_scrub", INFSLP);
			slept = 1;
		}
		if (!slept) {
			tsleep_nsec(sd->sd_sc, PWAIT, "sr_yield",
			    MSEC_TO_NSEC(1));
		}
	}
}
#endif
