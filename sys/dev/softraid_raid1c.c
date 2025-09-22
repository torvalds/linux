/* $OpenBSD: softraid_raid1c.c,v 1.6 2021/10/24 14:50:42 tobhe Exp $ */
/*
 * Copyright (c) 2007 Marco Peereboom <marco@peereboom.us>
 * Copyright (c) 2008 Hans-Joerg Hoexer <hshoexer@openbsd.org>
 * Copyright (c) 2008 Damien Miller <djm@mindrot.org>
 * Copyright (c) 2009 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2020 Stefan Sperling <stsp@openbsd.org>
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

#include <crypto/cryptodev.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_disk.h>

#include <dev/softraidvar.h>

/* RAID 1C functions. */
int	sr_raid1c_create(struct sr_discipline *, struct bioc_createraid *,
	    int, int64_t);
int	sr_raid1c_add_offline_chunks(struct sr_discipline *, int);
int	sr_raid1c_assemble(struct sr_discipline *, struct bioc_createraid *,
	    int, void *);
int	sr_raid1c_alloc_resources(struct sr_discipline *);
void	sr_raid1c_free_resources(struct sr_discipline *sd);
int	sr_raid1c_ioctl(struct sr_discipline *sd, struct bioc_discipline *bd);
int	sr_raid1c_meta_opt_handler(struct sr_discipline *,
	    struct sr_meta_opt_hdr *);
int	sr_raid1c_rw(struct sr_workunit *);
int	sr_raid1c_dev_rw(struct sr_workunit *, struct sr_crypto_wu *);
void	sr_raid1c_done(struct sr_workunit *wu);

/* RAID1 functions */
extern int	sr_raid1_init(struct sr_discipline *sd);
extern int	sr_raid1_assemble(struct sr_discipline *,
		    struct bioc_createraid *, int, void *);
extern int	sr_raid1_wu_done(struct sr_workunit *);
extern void	sr_raid1_set_chunk_state(struct sr_discipline *, int, int);
extern void	sr_raid1_set_vol_state(struct sr_discipline *);

/* CRYPTO raid functions */
extern struct sr_crypto_wu *sr_crypto_prepare(struct sr_workunit *,
		    struct sr_crypto *, int);
extern int	sr_crypto_meta_create(struct sr_discipline *,
		    struct sr_crypto *, struct bioc_createraid *);
extern int	sr_crypto_set_key(struct sr_discipline *,
		    struct sr_crypto *, struct bioc_createraid *, int, void *);
extern int	sr_crypto_alloc_resources_internal(struct sr_discipline *,
		    struct sr_crypto *);
extern void	sr_crypto_free_resources_internal(struct sr_discipline *,
		    struct sr_crypto *);
extern int	sr_crypto_ioctl_internal(struct sr_discipline *,
		    struct sr_crypto *, struct bioc_discipline *);
int		sr_crypto_meta_opt_handler_internal(struct sr_discipline *,
		    struct sr_crypto *, struct sr_meta_opt_hdr *);
void		sr_crypto_done_internal(struct sr_workunit *,
		    struct sr_crypto *);

/* Discipline initialisation. */
void
sr_raid1c_discipline_init(struct sr_discipline *sd)
{
	int i;

	/* Fill out discipline members. */
	sd->sd_wu_size = sizeof(struct sr_crypto_wu);
	sd->sd_type = SR_MD_RAID1C;
	strlcpy(sd->sd_name, "RAID 1C", sizeof(sd->sd_name));
	sd->sd_capabilities = SR_CAP_SYSTEM_DISK | SR_CAP_AUTO_ASSEMBLE |
	    SR_CAP_REBUILD | SR_CAP_REDUNDANT;
	sd->sd_max_wu = SR_RAID1C_NOWU;

	for (i = 0; i < SR_CRYPTO_MAXKEYS; i++)
		sd->mds.mdd_raid1c.sr1c_crypto.scr_sid[i] = (u_int64_t)-1;

	/* Setup discipline specific function pointers. */
	sd->sd_alloc_resources = sr_raid1c_alloc_resources;
	sd->sd_assemble = sr_raid1c_assemble;
	sd->sd_create = sr_raid1c_create;
	sd->sd_free_resources = sr_raid1c_free_resources;
	sd->sd_ioctl_handler = sr_raid1c_ioctl;
	sd->sd_meta_opt_handler = sr_raid1c_meta_opt_handler;
	sd->sd_scsi_rw = sr_raid1c_rw;
	sd->sd_scsi_done = sr_raid1c_done;
	sd->sd_scsi_wu_done = sr_raid1_wu_done;
	sd->sd_set_chunk_state = sr_raid1_set_chunk_state;
	sd->sd_set_vol_state = sr_raid1_set_vol_state;
}

int
sr_raid1c_create(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, int64_t coerced_size)
{
	int rv;

	if (no_chunk < 2) {
		sr_error(sd->sd_sc, "%s requires two or more chunks",
		    sd->sd_name);
		return EINVAL;
	}

	sd->sd_meta->ssdi.ssd_size = coerced_size;

	rv = sr_raid1_init(sd);
	if (rv)
		return rv;

	return sr_crypto_meta_create(sd, &sd->mds.mdd_raid1c.sr1c_crypto, bc);
}

int
sr_raid1c_add_offline_chunks(struct sr_discipline *sd, int no_chunk)
{
	struct sr_chunk	*ch_entry, *ch_prev;
	struct sr_chunk **chunks;
	int c;

	chunks = mallocarray(sd->sd_meta->ssdi.ssd_chunk_no,
	    sizeof(struct sr_chunk *), M_DEVBUF, M_WAITOK | M_ZERO);

	for (c = 0; c < no_chunk; c++)
		chunks[c] = sd->sd_vol.sv_chunks[c];

	for (c = no_chunk; c < sd->sd_meta->ssdi.ssd_chunk_no; c++) {
		ch_prev = chunks[c - 1];
		ch_entry = malloc(sizeof(struct sr_chunk), M_DEVBUF,
		    M_WAITOK | M_ZERO);
		ch_entry->src_meta.scm_status = BIOC_SDOFFLINE;
		ch_entry->src_dev_mm = NODEV;
		SLIST_INSERT_AFTER(ch_prev, ch_entry, src_link);
		chunks[c] = ch_entry;
	}

	free(sd->sd_vol.sv_chunks, M_DEVBUF,
	    sizeof(struct sr_chunk *) * no_chunk);
	sd->sd_vol.sv_chunks = chunks;

	return (0);
}

int
sr_raid1c_assemble(struct sr_discipline *sd, struct bioc_createraid *bc,
    int no_chunk, void *data)
{
	struct sr_raid1c *mdd_raid1c = &sd->mds.mdd_raid1c;
	int rv;

	/* Create NODEV place-holders for missing chunks. */
	if (no_chunk < sd->sd_meta->ssdi.ssd_chunk_no) {
		rv = sr_raid1c_add_offline_chunks(sd, no_chunk);
		if (rv)
			return (rv);
	}

	rv = sr_raid1_assemble(sd, bc, no_chunk, NULL);
	if (rv)
		return (rv);

	return sr_crypto_set_key(sd, &mdd_raid1c->sr1c_crypto, bc,
	    no_chunk, data);
}

int
sr_raid1c_ioctl(struct sr_discipline *sd, struct bioc_discipline *bd)
{
	struct sr_raid1c *mdd_raid1c = &sd->mds.mdd_raid1c;
	return sr_crypto_ioctl_internal(sd, &mdd_raid1c->sr1c_crypto, bd);
}

int
sr_raid1c_alloc_resources(struct sr_discipline *sd)
{
	struct sr_raid1c *mdd_raid1c = &sd->mds.mdd_raid1c;
	return sr_crypto_alloc_resources_internal(sd, &mdd_raid1c->sr1c_crypto);
}

void
sr_raid1c_free_resources(struct sr_discipline *sd)
{
	struct sr_raid1c *mdd_raid1c = &sd->mds.mdd_raid1c;
	sr_crypto_free_resources_internal(sd, &mdd_raid1c->sr1c_crypto);
}

int
sr_raid1c_dev_rw(struct sr_workunit *wu, struct sr_crypto_wu *crwu)
{
	struct sr_discipline	*sd = wu->swu_dis;
	struct scsi_xfer	*xs = wu->swu_xs;
	struct sr_raid1c	*mdd_raid1c = &sd->mds.mdd_raid1c;
	struct sr_ccb		*ccb;
	struct uio		*uio;
	struct sr_chunk		*scp;
	int			ios, chunk, i, rt;
	daddr_t			blkno;

	blkno = wu->swu_blk_start;

	if (xs->flags & SCSI_DATA_IN)
		ios = 1;
	else
		ios = sd->sd_meta->ssdi.ssd_chunk_no;

	for (i = 0; i < ios; i++) {
		if (xs->flags & SCSI_DATA_IN) {
			rt = 0;
ragain:
			/* interleave reads */
			chunk = mdd_raid1c->sr1c_raid1.sr1_counter++ %
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
				if (ISSET(wu->swu_flags, SR_WUF_REBUILD))
					continue;
				break;

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
		if (!ISSET(xs->flags, SCSI_DATA_IN) &&
		    !ISSET(wu->swu_flags, SR_WUF_REBUILD)) {
			uio = crwu->cr_crp->crp_buf;
			ccb->ccb_buf.b_data = uio->uio_iov->iov_base;
			ccb->ccb_opaque = crwu;
		}
		sr_wu_enqueue_ccb(wu, ccb);
	}

	sr_schedule_wu(wu);

	return (0);

bad:
	return (EINVAL);
}

int
sr_raid1c_meta_opt_handler(struct sr_discipline *sd, struct sr_meta_opt_hdr *om)
{
	struct sr_raid1c *mdd_raid1c = &sd->mds.mdd_raid1c;
	return sr_crypto_meta_opt_handler_internal(sd,
	    &mdd_raid1c->sr1c_crypto, om);
}

int
sr_raid1c_rw(struct sr_workunit *wu)
{
	struct sr_crypto_wu	*crwu;
	struct sr_raid1c	*mdd_raid1c;
	daddr_t			blkno;
	int			rv, err;
	int			s;

	DNPRINTF(SR_D_DIS, "%s: sr_raid1c_rw wu %p\n",
	    DEVNAME(wu->swu_dis->sd_sc), wu);

	if (sr_validate_io(wu, &blkno, "sr_raid1c_rw"))
		return (1);
	
	if (ISSET(wu->swu_xs->flags, SCSI_DATA_OUT) &&
	    !ISSET(wu->swu_flags, SR_WUF_REBUILD)) {
		mdd_raid1c = &wu->swu_dis->mds.mdd_raid1c;
		crwu = sr_crypto_prepare(wu, &mdd_raid1c->sr1c_crypto, 1);
		rv = crypto_invoke(crwu->cr_crp);

		DNPRINTF(SR_D_INTR, "%s: sr_raid1c_rw: wu %p xs: %p\n",
		    DEVNAME(wu->swu_dis->sd_sc), wu, wu->swu_xs);

		if (rv) {
			/* fail io */
			wu->swu_xs->error = XS_DRIVER_STUFFUP;
			s = splbio();
			sr_scsi_done(wu->swu_dis, wu->swu_xs);
			splx(s);
		}

		if ((err = sr_raid1c_dev_rw(wu, crwu)) != 0)
			return (err);
	} else
		rv = sr_raid1c_dev_rw(wu, NULL);

	return (rv);
}

void
sr_raid1c_done(struct sr_workunit *wu)
{
	struct sr_raid1c *mdd_raid1c = &wu->swu_dis->mds.mdd_raid1c;
	sr_crypto_done_internal(wu, &mdd_raid1c->sr1c_crypto);
}
