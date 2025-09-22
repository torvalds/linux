/* $OpenBSD: softraid_raid6.c,v 1.74 2025/06/13 13:00:49 jsg Exp $ */
/*
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
#include <sys/conf.h>
#include <sys/uio.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_disk.h>

#include <dev/softraidvar.h>

uint8_t *gf_map[256];
uint8_t	gf_pow[768];
int	gf_log[256];

/* RAID 6 functions. */
int	sr_raid6_create(struct sr_discipline *, struct bioc_createraid *,
	    int, int64_t);
int	sr_raid6_assemble(struct sr_discipline *, struct bioc_createraid *,
	    int, void *);
int	sr_raid6_init(struct sr_discipline *);
int	sr_raid6_rw(struct sr_workunit *);
int	sr_raid6_openings(struct sr_discipline *);
void	sr_raid6_intr(struct buf *);
int	sr_raid6_wu_done(struct sr_workunit *);
void	sr_raid6_set_chunk_state(struct sr_discipline *, int, int);
void	sr_raid6_set_vol_state(struct sr_discipline *);

void	sr_raid6_xorp(void *, void *, int);
void	sr_raid6_xorq(void *, void *, int, int);
int	sr_raid6_addio(struct sr_workunit *wu, int, daddr_t, long,
	    void *, int, int, void *, void *, int);

void	gf_init(void);
uint8_t gf_inv(uint8_t);
int	gf_premul(uint8_t);
uint8_t gf_mul(uint8_t, uint8_t);

#define SR_NOFAIL		0x00
#define SR_FAILX		(1L << 0)
#define SR_FAILY		(1L << 1)
#define SR_FAILP		(1L << 2)
#define SR_FAILQ		(1L << 3)

struct sr_raid6_opaque {
	int	gn;
	void	*pbuf;
	void	*qbuf;
};

/* discipline initialisation. */
void
sr_raid6_discipline_init(struct sr_discipline *sd)
{
	/* Initialize GF256 tables. */
	gf_init();

	/* Fill out discipline members. */
	sd->sd_type = SR_MD_RAID6;
	strlcpy(sd->sd_name, "RAID 6", sizeof(sd->sd_name));
	sd->sd_capabilities = SR_CAP_SYSTEM_DISK | SR_CAP_AUTO_ASSEMBLE |
	    SR_CAP_REDUNDANT;
	sd->sd_max_wu = SR_RAID6_NOWU;

	/* Setup discipline specific function pointers. */
	sd->sd_assemble = sr_raid6_assemble;
	sd->sd_create = sr_raid6_create;
	sd->sd_openings = sr_raid6_openings;
	sd->sd_scsi_rw = sr_raid6_rw;
	sd->sd_scsi_intr = sr_raid6_intr;
	sd->sd_scsi_wu_done = sr_raid6_wu_done;
	sd->sd_set_chunk_state = sr_raid6_set_chunk_state;
	sd->sd_set_vol_state = sr_raid6_set_vol_state;
}

int
sr_raid6_create(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, int64_t coerced_size)
{
	if (no_chunk < 4) {
		sr_error(sd->sd_sc, "%s requires four or more chunks",
		    sd->sd_name);
		return EINVAL;
	}

	/*
	 * XXX add variable strip size later even though MAXPHYS is really
	 * the clever value, users like * to tinker with that type of stuff.
	 */
	sd->sd_meta->ssdi.ssd_strip_size = MAXPHYS;
	sd->sd_meta->ssdi.ssd_size = (coerced_size &
	    ~(((u_int64_t)sd->sd_meta->ssdi.ssd_strip_size >>
	    DEV_BSHIFT) - 1)) * (no_chunk - 2);

	return sr_raid6_init(sd);
}

int
sr_raid6_assemble(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, void *data)
{
	return sr_raid6_init(sd);
}

int
sr_raid6_init(struct sr_discipline *sd)
{
	/* Initialise runtime values. */
	sd->mds.mdd_raid6.sr6_strip_bits =
	    sr_validate_stripsize(sd->sd_meta->ssdi.ssd_strip_size);
	if (sd->mds.mdd_raid6.sr6_strip_bits == -1) {
		sr_error(sd->sd_sc, "invalid strip size");
		return EINVAL;
	}

	/* only if stripsize <= MAXPHYS */
	sd->sd_max_ccb_per_wu = max(6, 2 * sd->sd_meta->ssdi.ssd_chunk_no);

	return 0;
}

int
sr_raid6_openings(struct sr_discipline *sd)
{
	return (sd->sd_max_wu >> 1); /* 2 wu's per IO */
}

void
sr_raid6_set_chunk_state(struct sr_discipline *sd, int c, int new_state)
{
	int			old_state, s;

	/* XXX this is for RAID 0 */
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
sr_raid6_set_vol_state(struct sr_discipline *sd)
{
	int			states[SR_MAX_STATES];
	int			new_state, i, s, nd;
	int			old_state = sd->sd_vol_status;

	/* XXX this is for RAID 0 */

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
	else if (states[BIOC_SDONLINE] < nd - 2)
		new_state = BIOC_SVOFFLINE;
	else if (states[BIOC_SDSCRUB] != 0)
		new_state = BIOC_SVSCRUB;
	else if (states[BIOC_SDREBUILD] != 0)
		new_state = BIOC_SVREBUILD;
	else if (states[BIOC_SDONLINE] < nd)
		new_state = BIOC_SVDEGRADED;
	else {
		printf("old_state = %d, ", old_state);
		for (i = 0; i < nd; i++)
			printf("%d = %d, ", i,
			    sd->sd_vol.sv_chunks[i]->src_meta.scm_status);
		panic("invalid new_state");
	}

	DNPRINTF(SR_D_STATE, "%s: %s: sr_raid_set_vol_state %d -> %d\n",
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

/*  modes:
 *   readq: sr_raid6_addio(i, lba, length, NULL, SCSI_DATA_IN,
 *		0, qbuf, NULL, 0);
 *   readp: sr_raid6_addio(i, lba, length, NULL, SCSI_DATA_IN,
 *		0, pbuf, NULL, 0);
 *   readx: sr_raid6_addio(i, lba, length, NULL, SCSI_DATA_IN,
 *		0, pbuf, qbuf, gf_pow[i]);
 */

int
sr_raid6_rw(struct sr_workunit *wu)
{
	struct sr_workunit	*wu_r = NULL;
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_chunk		*scp;
	int			s, fail, i, gxinv, pxinv;
	daddr_t			blkno, lba;
	int64_t			chunk_offs, lbaoffs, offset, strip_offs;
	int64_t			strip_no, strip_size, strip_bits, row_size;
	int64_t			fchunk, no_chunk, chunk, qchunk, pchunk;
	long			length, datalen;
	void			*pbuf, *data, *qbuf;

	/* blkno and scsi error will be handled by sr_validate_io */
	if (sr_validate_io(wu, &blkno, "sr_raid6_rw"))
		goto bad;

	strip_size = sd->sd_meta->ssdi.ssd_strip_size;
	strip_bits = sd->mds.mdd_raid6.sr6_strip_bits;
	no_chunk = sd->sd_meta->ssdi.ssd_chunk_no - 2;
	row_size = (no_chunk << strip_bits) >> DEV_BSHIFT;

	data = xs->data;
	datalen = xs->datalen;
	lbaoffs	= blkno << DEV_BSHIFT;

	if (xs->flags & SCSI_DATA_OUT) {
		if ((wu_r = sr_scsi_wu_get(sd, SCSI_NOSLEEP)) == NULL){
			printf("%s: can't get wu_r", DEVNAME(sd->sd_sc));
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

		/* map disk offset to parity/data drive */
		chunk = strip_no % no_chunk;

		qchunk = (no_chunk + 1) - ((strip_no / no_chunk) % (no_chunk+2));
		if (qchunk == 0)
			pchunk = no_chunk + 1;
		else
			pchunk = qchunk - 1;
		if (chunk >= pchunk)
			chunk++;
		if (chunk >= qchunk)
			chunk++;

		lba = offset >> DEV_BSHIFT;

		/* XXX big hammer.. exclude I/O from entire stripe */
		if (wu->swu_blk_start == 0)
			wu->swu_blk_start = (strip_no / no_chunk) * row_size;
		wu->swu_blk_end = (strip_no / no_chunk) * row_size + (row_size - 1);

		fail = 0;
		fchunk = -1;

		/* Get disk-fail flags */
		for (i=0; i< no_chunk+2; i++) {
			scp = sd->sd_vol.sv_chunks[i];
			switch (scp->src_meta.scm_status) {
			case BIOC_SDOFFLINE:
			case BIOC_SDREBUILD:
			case BIOC_SDHOTSPARE:
				if (i == qchunk)
					fail |= SR_FAILQ;
				else if (i == pchunk)
					fail |= SR_FAILP;
				else if (i == chunk)
					fail |= SR_FAILX;
				else {
					/* dual data-disk failure */
					fail |= SR_FAILY;
					fchunk = i;
				}
				break;
			}
		}
		if (xs->flags & SCSI_DATA_IN) {
			if (!(fail & SR_FAILX)) {
				/* drive is good. issue single read request */
				if (sr_raid6_addio(wu, chunk, lba, length,
				    data, xs->flags, 0, NULL, NULL, 0))
					goto bad;
			} else if (fail & SR_FAILP) {
				/* Dx, P failed */
				printf("Disk %llx offline, "
				    "regenerating Dx+P\n", chunk);

				gxinv = gf_inv(gf_pow[chunk]);

				/* Calculate: Dx = (Q^Dz*gz)*inv(gx) */
				memset(data, 0, length);
				if (sr_raid6_addio(wu, qchunk, lba, length,
				    NULL, SCSI_DATA_IN, 0, NULL, data, gxinv))
					goto bad;

				/* Read Dz * gz * inv(gx) */
				for (i = 0; i < no_chunk+2; i++) {
					if  (i == qchunk || i == pchunk || i == chunk)
						continue;

					if (sr_raid6_addio(wu, i, lba, length,
					    NULL, SCSI_DATA_IN, 0, NULL, data,
					    gf_mul(gf_pow[i], gxinv)))
						goto bad;
				}

				/* data will contain correct value on completion */
			} else if (fail & SR_FAILY) {
				/* Dx, Dy failed */
				printf("Disk %llx & %llx offline, "
				    "regenerating Dx+Dy\n", chunk, fchunk);

				gxinv = gf_inv(gf_pow[chunk] ^ gf_pow[fchunk]);
				pxinv = gf_mul(gf_pow[fchunk], gxinv);

				/* read Q * inv(gx + gy) */
				memset(data, 0, length);
				if (sr_raid6_addio(wu, qchunk, lba, length,
				    NULL, SCSI_DATA_IN, 0, NULL, data, gxinv))
					goto bad;

				/* read P * gy * inv(gx + gy) */
				if (sr_raid6_addio(wu, pchunk, lba, length,
				    NULL, SCSI_DATA_IN, 0, NULL, data, pxinv))
					goto bad;

				/* Calculate: Dx*gx^Dy*gy = Q^(Dz*gz) ; Dx^Dy = P^Dz
				 *   Q:  sr_raid6_xorp(qbuf, --, length);
				 *   P:  sr_raid6_xorp(pbuf, --, length);
				 *   Dz: sr_raid6_xorp(pbuf, --, length);
				 *	 sr_raid6_xorq(qbuf, --, length, gf_pow[i]);
				 */
				for (i = 0; i < no_chunk+2; i++) {
					if (i == qchunk || i == pchunk ||
					    i == chunk || i == fchunk)
						continue;

					/* read Dz * (gz + gy) * inv(gx + gy) */
					if (sr_raid6_addio(wu, i, lba, length,
					    NULL, SCSI_DATA_IN, 0, NULL, data,
					    pxinv ^ gf_mul(gf_pow[i], gxinv)))
						goto bad;
				}
			} else {
				/* Two cases: single disk (Dx) or (Dx+Q)
				 *   Dx = Dz ^ P (same as RAID5)
				 */
				printf("Disk %llx offline, "
				    "regenerating Dx%s\n", chunk,
				    fail & SR_FAILQ ? "+Q" : " single");

				/* Calculate: Dx = P^Dz
				 *   P:  sr_raid6_xorp(data, ---, length);
				 *   Dz: sr_raid6_xorp(data, ---, length);
				 */
				memset(data, 0, length);
				for (i = 0; i < no_chunk+2; i++) {
					if (i != chunk && i != qchunk) {
						/* Read Dz */
						if (sr_raid6_addio(wu, i, lba,
						    length, NULL, SCSI_DATA_IN,
						    0, data, NULL, 0))
							goto bad;
					}
				}

				/* data will contain correct value on completion */
			}
		} else {
			/* XXX handle writes to failed/offline disk? */
			if (fail & (SR_FAILX|SR_FAILQ|SR_FAILP))
				goto bad;

			/*
			 * initialize pbuf with contents of new data to be
			 * written. This will be XORed with old data and old
			 * parity in the intr routine. The result in pbuf
			 * is the new parity data.
			 */
			qbuf = sr_block_get(sd, length);
			if (qbuf == NULL)
				goto bad;

			pbuf = sr_block_get(sd, length);
			if (pbuf == NULL)
				goto bad;

			/* Calculate P = Dn; Q = gn * Dn */
			if (gf_premul(gf_pow[chunk]))
				goto bad;
			sr_raid6_xorp(pbuf, data, length);
			sr_raid6_xorq(qbuf, data, length, gf_pow[chunk]);

			/* Read old data: P ^= Dn' ; Q ^= (gn * Dn') */
			if (sr_raid6_addio(wu_r, chunk, lba, length, NULL,
				SCSI_DATA_IN, 0, pbuf, qbuf, gf_pow[chunk]))
				goto bad;

			/* Read old xor-parity: P ^= P' */
			if (sr_raid6_addio(wu_r, pchunk, lba, length, NULL,
				SCSI_DATA_IN, 0, pbuf, NULL, 0))
				goto bad;

			/* Read old q-parity: Q ^= Q' */
			if (sr_raid6_addio(wu_r, qchunk, lba, length, NULL,
				SCSI_DATA_IN, 0, qbuf, NULL, 0))
				goto bad;

			/* write new data */
			if (sr_raid6_addio(wu, chunk, lba, length, data,
			    xs->flags, 0, NULL, NULL, 0))
				goto bad;

			/* write new xor-parity */
			if (sr_raid6_addio(wu, pchunk, lba, length, pbuf,
			    xs->flags, SR_CCBF_FREEBUF, NULL, NULL, 0))
				goto bad;

			/* write new q-parity */
			if (sr_raid6_addio(wu, qchunk, lba, length, qbuf,
			    xs->flags, SR_CCBF_FREEBUF, NULL, NULL, 0))
				goto bad;
		}

		/* advance to next block */
		lbaoffs += length;
		datalen -= length;
		data += length;
	}

	s = splbio();
	if (wu_r) {
		/* collide write request with reads */
		wu_r->swu_blk_start = wu->swu_blk_start;
		wu_r->swu_blk_end = wu->swu_blk_end;

		wu->swu_state = SR_WU_DEFERRED;
		wu_r->swu_collider = wu;
		TAILQ_INSERT_TAIL(&sd->sd_wu_defq, wu, swu_link);

		wu = wu_r;
	}
	splx(s);

	sr_schedule_wu(wu);

	return (0);
bad:
	/* XXX - can leak pbuf/qbuf on error. */
	/* wu is unwound by sr_wu_put */
	if (wu_r)
		sr_scsi_wu_put(sd, wu_r);
	return (1);
}

void
sr_raid6_intr(struct buf *bp)
{
	struct sr_ccb		*ccb = (struct sr_ccb *)bp;
	struct sr_workunit	*wu = ccb->ccb_wu;
	struct sr_discipline	*sd = wu->swu_dis;
	struct sr_raid6_opaque  *pq = ccb->ccb_opaque;
	int			s;

	DNPRINTF(SR_D_INTR, "%s: sr_raid6_intr bp %p xs %p\n",
	    DEVNAME(sd->sd_sc), bp, wu->swu_xs);

	s = splbio();
	sr_ccb_done(ccb);

	/* XOR data to result. */
	if (ccb->ccb_state == SR_CCB_OK && pq) {
		if (pq->pbuf)
			/* Calculate xor-parity */
			sr_raid6_xorp(pq->pbuf, ccb->ccb_buf.b_data,
			    ccb->ccb_buf.b_bcount);
		if (pq->qbuf)
			/* Calculate q-parity */
			sr_raid6_xorq(pq->qbuf, ccb->ccb_buf.b_data,
			    ccb->ccb_buf.b_bcount, pq->gn);
		free(pq, M_DEVBUF, 0);
		ccb->ccb_opaque = NULL;
	}

	/* Free allocated data buffer. */
	if (ccb->ccb_flags & SR_CCBF_FREEBUF) {
		sr_block_put(sd, ccb->ccb_buf.b_data, ccb->ccb_buf.b_bcount);
		ccb->ccb_buf.b_data = NULL;
	}

	sr_wu_done(wu);
	splx(s);
}

int
sr_raid6_wu_done(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;

	/* XXX - we have no way of propagating errors... */
	if (wu->swu_flags & SR_WUF_DISCIPLINE)
		return SR_WU_OK;

	/* XXX - This is insufficient for RAID 6. */
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
		printf("%s: permanently fail write on block %lld\n",
		    sd->sd_meta->ssd_devname, (long long)wu->swu_blk_start);
	}

	wu->swu_state = SR_WU_FAILED;
	xs->error = XS_DRIVER_STUFFUP;

	return SR_WU_FAILED;
}

int
sr_raid6_addio(struct sr_workunit *wu, int chunk, daddr_t blkno,
    long len, void *data, int xsflags, int ccbflags, void *pbuf,
    void *qbuf, int gn)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct sr_ccb		*ccb;
	struct sr_raid6_opaque  *pqbuf;

	DNPRINTF(SR_D_DIS, "sr_raid6_addio: %s %d.%lld %ld %p:%p\n",
	    (xsflags & SCSI_DATA_IN) ? "read" : "write", chunk,
	    (long long)blkno, len, pbuf, qbuf);

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
	if (pbuf || qbuf) {
		/* XXX - can leak data and ccb on failure. */
		if (qbuf && gf_premul(gn))
			return (-1);

		/* XXX - should be preallocated? */
		pqbuf = malloc(sizeof(struct sr_raid6_opaque),
		    M_DEVBUF, M_ZERO | M_NOWAIT);
		if (pqbuf == NULL) {
			sr_ccb_put(ccb);
			return (-1);
		}
		pqbuf->pbuf = pbuf;
		pqbuf->qbuf = qbuf;
		pqbuf->gn = gn;
		ccb->ccb_opaque = pqbuf;
	}
	sr_wu_enqueue_ccb(wu, ccb);

	return (0);
}

/* Perform RAID6 parity calculation.
 *   P=xor parity, Q=GF256 parity, D=data, gn=disk# */
void
sr_raid6_xorp(void *p, void *d, int len)
{
	uint32_t *pbuf = p, *data = d;

	len >>= 2;
	while (len--)
		*pbuf++ ^= *data++;
}

void
sr_raid6_xorq(void *q, void *d, int len, int gn)
{
	uint32_t	*qbuf = q, *data = d, x;
	uint8_t		*gn_map = gf_map[gn];

	len >>= 2;
	while (len--) {
		x = *data++;
		*qbuf++ ^= (((uint32_t)gn_map[x & 0xff]) |
			    ((uint32_t)gn_map[(x >> 8) & 0xff] << 8) |
			    ((uint32_t)gn_map[(x >> 16) & 0xff] << 16) |
			    ((uint32_t)gn_map[(x >> 24) & 0xff] << 24));
	}
}

/* Create GF256 log/pow tables: polynomial = 0x11D */
void
gf_init(void)
{
	int i;
	uint8_t p = 1;

	/* use 2N pow table to avoid using % in multiply */
	for (i=0; i<256; i++) {
		gf_log[p] = i;
		gf_pow[i] = gf_pow[i+255] = p;
		p = ((p << 1) ^ ((p & 0x80) ? 0x1D : 0x00));
	}
	gf_log[0] = 512;
}

uint8_t
gf_inv(uint8_t a)
{
	return gf_pow[255 - gf_log[a]];
}

uint8_t
gf_mul(uint8_t a, uint8_t b)
{
	return gf_pow[gf_log[a] + gf_log[b]];
}

/* Precalculate multiplication tables for drive gn */
int
gf_premul(uint8_t gn)
{
	int i;

	if (gf_map[gn] != NULL)
		return (0);

	if ((gf_map[gn] = malloc(256, M_DEVBUF, M_ZERO | M_NOWAIT)) == NULL)
		return (-1);

	for (i=0; i<256; i++)
		gf_map[gn][i] = gf_pow[gf_log[i] + gf_log[gn]];
	return (0);
}
