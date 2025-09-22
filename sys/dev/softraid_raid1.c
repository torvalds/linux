/* $OpenBSD: softraid_raid1.c,v 1.67 2021/05/16 15:12:37 deraadt Exp $ */
/*
 * Copyright (c) 2007 Marco Peereboom <marco@peereboom.us>
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

/* RAID 1 functions. */
int	sr_raid1_create(struct sr_discipline *, struct bioc_createraid *,
	    int, int64_t);
int	sr_raid1_assemble(struct sr_discipline *, struct bioc_createraid *,
	    int, void *);
int	sr_raid1_init(struct sr_discipline *sd);
int	sr_raid1_rw(struct sr_workunit *);
int	sr_raid1_wu_done(struct sr_workunit *);
void	sr_raid1_set_chunk_state(struct sr_discipline *, int, int);
void	sr_raid1_set_vol_state(struct sr_discipline *);

/* Discipline initialisation. */
void
sr_raid1_discipline_init(struct sr_discipline *sd)
{
	/* Fill out discipline members. */
	sd->sd_type = SR_MD_RAID1;
	strlcpy(sd->sd_name, "RAID 1", sizeof(sd->sd_name));
	sd->sd_capabilities = SR_CAP_SYSTEM_DISK | SR_CAP_AUTO_ASSEMBLE |
	    SR_CAP_REBUILD | SR_CAP_REDUNDANT;
	sd->sd_max_wu = SR_RAID1_NOWU;

	/* Setup discipline specific function pointers. */
	sd->sd_assemble = sr_raid1_assemble;
	sd->sd_create = sr_raid1_create;
	sd->sd_scsi_rw = sr_raid1_rw;
	sd->sd_scsi_wu_done = sr_raid1_wu_done;
	sd->sd_set_chunk_state = sr_raid1_set_chunk_state;
	sd->sd_set_vol_state = sr_raid1_set_vol_state;
}

int
sr_raid1_create(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, int64_t coerced_size)
{
	if (no_chunk < 2) {
		sr_error(sd->sd_sc, "%s requires two or more chunks",
		    sd->sd_name);
		return EINVAL;
	}

	sd->sd_meta->ssdi.ssd_size = coerced_size;

	return sr_raid1_init(sd);
}

int
sr_raid1_assemble(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, void *data)
{
	return sr_raid1_init(sd);
}

int
sr_raid1_init(struct sr_discipline *sd)
{
	sd->sd_max_ccb_per_wu = sd->sd_meta->ssdi.ssd_chunk_no;

	return 0;
}

void
sr_raid1_set_chunk_state(struct sr_discipline *sd, int c, int new_state)
{
	int			old_state, s;

	DNPRINTF(SR_D_STATE, "%s: %s: %s: sr_raid1_set_chunk_state %d -> %d\n",
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
		switch (new_state) {
		case BIOC_SDREBUILD:
		case BIOC_SDHOTSPARE:
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SDSCRUB:
		if (new_state == BIOC_SDONLINE) {
			;
		} else
			goto die;
		break;

	case BIOC_SDREBUILD:
		switch (new_state) {
		case BIOC_SDONLINE:
			break;
		case BIOC_SDOFFLINE:
			/* Abort rebuild since the rebuild chunk disappeared. */
			sd->sd_reb_abort = 1;
			break;
		default:
			goto die;
		}
		break;

	case BIOC_SDHOTSPARE:
		switch (new_state) {
		case BIOC_SDOFFLINE:
		case BIOC_SDREBUILD:
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
sr_raid1_set_vol_state(struct sr_discipline *sd)
{
	int			states[SR_MAX_STATES];
	int			new_state, i, s, nd;
	int			old_state = sd->sd_vol_status;

	DNPRINTF(SR_D_STATE, "%s: %s: sr_raid1_set_vol_state\n",
	    DEVNAME(sd->sd_sc), sd->sd_meta->ssd_devname);

	nd = sd->sd_meta->ssdi.ssd_chunk_no;

#ifdef SR_DEBUG
	for (i = 0; i < nd; i++)
		DNPRINTF(SR_D_STATE, "%s: chunk %d status = %u\n",
		    DEVNAME(sd->sd_sc), i,
		    sd->sd_vol.sv_chunks[i]->src_meta.scm_status);
#endif

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
	else if (states[BIOC_SDONLINE] == 0)
		new_state = BIOC_SVOFFLINE;
	else if (states[BIOC_SDSCRUB] != 0)
		new_state = BIOC_SVSCRUB;
	else if (states[BIOC_SDREBUILD] != 0)
		new_state = BIOC_SVREBUILD;
	else if (states[BIOC_SDOFFLINE] != 0)
		new_state = BIOC_SVDEGRADED;
	else {
		DNPRINTF(SR_D_STATE, "%s: invalid volume state, old state "
		    "was %d\n", DEVNAME(sd->sd_sc), old_state);
		panic("invalid volume state");
	}

	DNPRINTF(SR_D_STATE, "%s: %s: sr_raid1_set_vol_state %d -> %d\n",
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
		    DEVNAME(sd->sd_sc),
		    sd->sd_meta->ssd_devname,
		    old_state, new_state);
		/* NOTREACHED */
	}

	sd->sd_vol_status = new_state;

	/* If we have just become degraded, look for a hotspare. */
	if (new_state == BIOC_SVDEGRADED)
		task_add(systq, &sd->sd_hotspare_rebuild_task);
}

int
sr_raid1_rw(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_ccb		*ccb;
	struct sr_chunk		*scp;
	int			ios, chunk, i, rt;
	daddr_t			blkno;

	/* blkno and scsi error will be handled by sr_validate_io */
	if (sr_validate_io(wu, &blkno, "sr_raid1_rw"))
		goto bad;

	if (xs->flags & SCSI_DATA_IN)
		ios = 1;
	else
		ios = sd->sd_meta->ssdi.ssd_chunk_no;

	for (i = 0; i < ios; i++) {
		if (xs->flags & SCSI_DATA_IN) {
			rt = 0;
ragain:
			/* interleave reads */
			chunk = sd->mds.mdd_raid1.sr1_counter++ %
			    sd->sd_meta->ssdi.ssd_chunk_no;
			scp = sd->sd_vol.sv_chunks[chunk];
			switch (scp->src_meta.scm_status) {
			case BIOC_SDONLINE:
			case BIOC_SDSCRUB:
				break;

			case BIOC_SDOFFLINE:
			case BIOC_SDREBUILD:
			case BIOC_SDHOTSPARE:
				if (rt++ < sd->sd_meta->ssdi.ssd_chunk_no)
					goto ragain;

				/* FALLTHROUGH */
			default:
				/* volume offline */
				printf("%s: is offline, cannot read\n",
				    DEVNAME(sd->sd_sc));
				goto bad;
			}
		} else {
			/* writes go on all working disks */
			chunk = i;
			scp = sd->sd_vol.sv_chunks[chunk];
			switch (scp->src_meta.scm_status) {
			case BIOC_SDONLINE:
			case BIOC_SDSCRUB:
			case BIOC_SDREBUILD:
				break;

			case BIOC_SDHOTSPARE: /* should never happen */
			case BIOC_SDOFFLINE:
				continue;

			default:
				goto bad;
			}
		}

		ccb = sr_ccb_rw(sd, chunk, blkno, xs->datalen, xs->data,
		    xs->flags, 0);
		if (!ccb) {
			/* should never happen but handle more gracefully */
			printf("%s: %s: too many ccbs queued\n",
			    DEVNAME(sd->sd_sc),
			    sd->sd_meta->ssd_devname);
			goto bad;
		}
		sr_wu_enqueue_ccb(wu, ccb);
	}

	sr_schedule_wu(wu);

	return (0);

bad:
	/* wu is unwound by sr_wu_put */
	return (1);
}

int
sr_raid1_wu_done(struct sr_workunit *wu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;

	/* If at least one I/O succeeded, we are okay. */
	if (wu->swu_ios_succeeded > 0) {
		xs->error = XS_NOERROR;
		return SR_WU_OK;
	}

	/* If all I/O failed, retry reads and give up on writes. */
	if (xs->flags & SCSI_DATA_IN) {
		printf("%s: retrying read on block %lld\n",
		    sd->sd_meta->ssd_devname, (long long)wu->swu_blk_start);
		if (wu->swu_cb_active == 1)
			panic("%s: sr_raid1_intr_cb",
			    DEVNAME(sd->sd_sc));
		sr_wu_release_ccbs(wu);
		wu->swu_state = SR_WU_RESTART;
		if (sd->sd_scsi_rw(wu) == 0)
			return SR_WU_RESTART;
	} else {
		printf("%s: permanently failing write on block %lld\n",
		    sd->sd_meta->ssd_devname, (long long)wu->swu_blk_start);
	}

	wu->swu_state = SR_WU_FAILED;
	xs->error = XS_DRIVER_STUFFUP;

	return SR_WU_FAILED;
}
