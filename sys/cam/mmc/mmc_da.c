/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Bernd Walter <tisco@FreeBSD.org>
 * Copyright (c) 2006 M. Warner Losh <imp@FreeBSD.org>
 * Copyright (c) 2009 Alexander Motin <mav@FreeBSD.org>
 * Copyright (c) 2015-2017 Ilya Bakulin <kibab@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Some code derived from the sys/dev/mmc and sys/cam/ata
 * Thanks to Warner Losh <imp@FreeBSD.org>, Alexander Motin <mav@FreeBSD.org>
 * Bernd Walter <tisco@FreeBSD.org>, and other authors.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

//#include "opt_sdda.h"

#include <sys/param.h>

#ifdef _KERNEL
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/endian.h>
#include <sys/taskqueue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>
#include <sys/devicestat.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/cons.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <geom/geom_disk.h>
#include <machine/_inttypes.h>  /* for PRIu64 */
#endif /* _KERNEL */

#ifndef _KERNEL
#include <stdio.h>
#include <string.h>
#endif /* _KERNEL */

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_queue.h>
#include <cam/cam_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_xpt_internal.h>
#include <cam/cam_debug.h>


#include <cam/mmc/mmc_all.h>

#include <machine/md_var.h>	/* geometry translation */

#ifdef _KERNEL

typedef enum {
	SDDA_FLAG_OPEN		= 0x0002,
	SDDA_FLAG_DIRTY		= 0x0004
} sdda_flags;

typedef enum {
	SDDA_STATE_INIT,
	SDDA_STATE_INVALID,
	SDDA_STATE_NORMAL,
	SDDA_STATE_PART_SWITCH,
} sdda_state;

#define	SDDA_FMT_BOOT		"sdda%dboot"
#define	SDDA_FMT_GP		"sdda%dgp"
#define	SDDA_FMT_RPMB		"sdda%drpmb"
#define	SDDA_LABEL_ENH		"enh"

#define	SDDA_PART_NAMELEN	(16 + 1)

struct sdda_softc;

struct sdda_part {
	struct disk *disk;
	struct bio_queue_head bio_queue;
	sdda_flags flags;
	struct sdda_softc *sc;
	u_int cnt;
	u_int type;
	bool ro;
	char name[SDDA_PART_NAMELEN];
};

struct sdda_softc {
	int	 outstanding_cmds;	/* Number of active commands */
	int	 refcount;		/* Active xpt_action() calls */
	sdda_state state;
	struct mmc_data *mmcdata;
	struct cam_periph *periph;
//	sdda_quirks quirks;
	struct task start_init_task;
	uint32_t raw_csd[4];
	uint8_t raw_ext_csd[512]; /* MMC only? */
	struct mmc_csd csd;
	struct mmc_cid cid;
	struct mmc_scr scr;
	/* Calculated from CSD */
	uint64_t sector_count;
	uint64_t mediasize;

	/* Calculated from CID */
	char card_id_string[64];/* Formatted CID info (serial, MFG, etc) */
	char card_sn_string[16];/* Formatted serial # for disk->d_ident */
	/* Determined from CSD + is highspeed card*/
	uint32_t card_f_max;

	/* Generic switch timeout */
	uint32_t cmd6_time;
	/* MMC partitions support */
	struct sdda_part *part[MMC_PART_MAX];
	uint8_t part_curr;	/* Partition currently switched to */
	uint8_t part_requested; /* What partition we're currently switching to */
	uint32_t part_time;	/* Partition switch timeout [us] */
	off_t enh_base;		/* Enhanced user data area slice base ... */
	off_t enh_size;		/* ... and size [bytes] */
	int log_count;
	struct timeval log_time;
};

#define ccb_bp		ppriv_ptr1

static	disk_strategy_t	sddastrategy;
static	periph_init_t	sddainit;
static	void		sddaasync(void *callback_arg, u_int32_t code,
				struct cam_path *path, void *arg);
static	periph_ctor_t	sddaregister;
static	periph_dtor_t	sddacleanup;
static	periph_start_t	sddastart;
static	periph_oninv_t	sddaoninvalidate;
static	void		sddadone(struct cam_periph *periph,
			       union ccb *done_ccb);
static  int		sddaerror(union ccb *ccb, u_int32_t cam_flags,
				u_int32_t sense_flags);

static uint16_t get_rca(struct cam_periph *periph);
static void sdda_start_init(void *context, union ccb *start_ccb);
static void sdda_start_init_task(void *context, int pending);
static void sdda_process_mmc_partitions(struct cam_periph *periph, union ccb *start_ccb);
static uint32_t sdda_get_host_caps(struct cam_periph *periph, union ccb *ccb);
static void sdda_init_switch_part(struct cam_periph *periph, union ccb *start_ccb, u_int part);
static int mmc_select_card(struct cam_periph *periph, union ccb *ccb, uint32_t rca);
static inline uint32_t mmc_get_sector_size(struct cam_periph *periph) {return MMC_SECTOR_SIZE;}

/* TODO: actually issue GET_TRAN_SETTINGS to get R/O status */
static inline bool sdda_get_read_only(struct cam_periph *periph, union ccb *start_ccb)
{

	return (false);
}

static uint32_t mmc_get_spec_vers(struct cam_periph *periph);
static uint64_t mmc_get_media_size(struct cam_periph *periph);
static uint32_t mmc_get_cmd6_timeout(struct cam_periph *periph);
static void sdda_add_part(struct cam_periph *periph, u_int type,
    const char *name, u_int cnt, off_t media_size, bool ro);

static struct periph_driver sddadriver =
{
	sddainit, "sdda",
	TAILQ_HEAD_INITIALIZER(sddadriver.units), /* generation */ 0
};

PERIPHDRIVER_DECLARE(sdda, sddadriver);

static MALLOC_DEFINE(M_SDDA, "sd_da", "sd_da buffers");

static const int exp[8] = {
	1, 10, 100, 1000, 10000, 100000, 1000000, 10000000
};

static const int mant[16] = {
	0, 10, 12, 13, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 70, 80
};

static const int cur_min[8] = {
	500, 1000, 5000, 10000, 25000, 35000, 60000, 100000
};

static const int cur_max[8] = {
	1000, 5000, 10000, 25000, 35000, 45000, 800000, 200000
};

static uint16_t
get_rca(struct cam_periph *periph) {
	return periph->path->device->mmc_ident_data.card_rca;
}

static uint32_t
mmc_get_bits(uint32_t *bits, int bit_len, int start, int size)
{
	const int i = (bit_len / 32) - (start / 32) - 1;
	const int shift = start & 31;
	uint32_t retval = bits[i] >> shift;
	if (size + shift > 32)
		retval |= bits[i - 1] << (32 - shift);
	return (retval & ((1llu << size) - 1));
}


static void
mmc_decode_csd_sd(uint32_t *raw_csd, struct mmc_csd *csd)
{
	int v;
	int m;
	int e;

	memset(csd, 0, sizeof(*csd));
	csd->csd_structure = v = mmc_get_bits(raw_csd, 128, 126, 2);
	if (v == 0) {
		m = mmc_get_bits(raw_csd, 128, 115, 4);
		e = mmc_get_bits(raw_csd, 128, 112, 3);
		csd->tacc = (exp[e] * mant[m] + 9) / 10;
		csd->nsac = mmc_get_bits(raw_csd, 128, 104, 8) * 100;
		m = mmc_get_bits(raw_csd, 128, 99, 4);
		e = mmc_get_bits(raw_csd, 128, 96, 3);
		csd->tran_speed = exp[e] * 10000 * mant[m];
		csd->ccc = mmc_get_bits(raw_csd, 128, 84, 12);
		csd->read_bl_len = 1 << mmc_get_bits(raw_csd, 128, 80, 4);
		csd->read_bl_partial = mmc_get_bits(raw_csd, 128, 79, 1);
		csd->write_blk_misalign = mmc_get_bits(raw_csd, 128, 78, 1);
		csd->read_blk_misalign = mmc_get_bits(raw_csd, 128, 77, 1);
		csd->dsr_imp = mmc_get_bits(raw_csd, 128, 76, 1);
		csd->vdd_r_curr_min = cur_min[mmc_get_bits(raw_csd, 128, 59, 3)];
		csd->vdd_r_curr_max = cur_max[mmc_get_bits(raw_csd, 128, 56, 3)];
		csd->vdd_w_curr_min = cur_min[mmc_get_bits(raw_csd, 128, 53, 3)];
		csd->vdd_w_curr_max = cur_max[mmc_get_bits(raw_csd, 128, 50, 3)];
		m = mmc_get_bits(raw_csd, 128, 62, 12);
		e = mmc_get_bits(raw_csd, 128, 47, 3);
		csd->capacity = ((1 + m) << (e + 2)) * csd->read_bl_len;
		csd->erase_blk_en = mmc_get_bits(raw_csd, 128, 46, 1);
		csd->erase_sector = mmc_get_bits(raw_csd, 128, 39, 7) + 1;
		csd->wp_grp_size = mmc_get_bits(raw_csd, 128, 32, 7);
		csd->wp_grp_enable = mmc_get_bits(raw_csd, 128, 31, 1);
		csd->r2w_factor = 1 << mmc_get_bits(raw_csd, 128, 26, 3);
		csd->write_bl_len = 1 << mmc_get_bits(raw_csd, 128, 22, 4);
		csd->write_bl_partial = mmc_get_bits(raw_csd, 128, 21, 1);
	} else if (v == 1) {
		m = mmc_get_bits(raw_csd, 128, 115, 4);
		e = mmc_get_bits(raw_csd, 128, 112, 3);
		csd->tacc = (exp[e] * mant[m] + 9) / 10;
		csd->nsac = mmc_get_bits(raw_csd, 128, 104, 8) * 100;
		m = mmc_get_bits(raw_csd, 128, 99, 4);
		e = mmc_get_bits(raw_csd, 128, 96, 3);
		csd->tran_speed = exp[e] * 10000 * mant[m];
		csd->ccc = mmc_get_bits(raw_csd, 128, 84, 12);
		csd->read_bl_len = 1 << mmc_get_bits(raw_csd, 128, 80, 4);
		csd->read_bl_partial = mmc_get_bits(raw_csd, 128, 79, 1);
		csd->write_blk_misalign = mmc_get_bits(raw_csd, 128, 78, 1);
		csd->read_blk_misalign = mmc_get_bits(raw_csd, 128, 77, 1);
		csd->dsr_imp = mmc_get_bits(raw_csd, 128, 76, 1);
		csd->capacity = ((uint64_t)mmc_get_bits(raw_csd, 128, 48, 22) + 1) *
		    512 * 1024;
		csd->erase_blk_en = mmc_get_bits(raw_csd, 128, 46, 1);
		csd->erase_sector = mmc_get_bits(raw_csd, 128, 39, 7) + 1;
		csd->wp_grp_size = mmc_get_bits(raw_csd, 128, 32, 7);
		csd->wp_grp_enable = mmc_get_bits(raw_csd, 128, 31, 1);
		csd->r2w_factor = 1 << mmc_get_bits(raw_csd, 128, 26, 3);
		csd->write_bl_len = 1 << mmc_get_bits(raw_csd, 128, 22, 4);
		csd->write_bl_partial = mmc_get_bits(raw_csd, 128, 21, 1);
	} else
		panic("unknown SD CSD version");
}

static void
mmc_decode_csd_mmc(uint32_t *raw_csd, struct mmc_csd *csd)
{
	int m;
	int e;

	memset(csd, 0, sizeof(*csd));
	csd->csd_structure = mmc_get_bits(raw_csd, 128, 126, 2);
	csd->spec_vers = mmc_get_bits(raw_csd, 128, 122, 4);
	m = mmc_get_bits(raw_csd, 128, 115, 4);
	e = mmc_get_bits(raw_csd, 128, 112, 3);
	csd->tacc = exp[e] * mant[m] + 9 / 10;
	csd->nsac = mmc_get_bits(raw_csd, 128, 104, 8) * 100;
	m = mmc_get_bits(raw_csd, 128, 99, 4);
	e = mmc_get_bits(raw_csd, 128, 96, 3);
	csd->tran_speed = exp[e] * 10000 * mant[m];
	csd->ccc = mmc_get_bits(raw_csd, 128, 84, 12);
	csd->read_bl_len = 1 << mmc_get_bits(raw_csd, 128, 80, 4);
	csd->read_bl_partial = mmc_get_bits(raw_csd, 128, 79, 1);
	csd->write_blk_misalign = mmc_get_bits(raw_csd, 128, 78, 1);
	csd->read_blk_misalign = mmc_get_bits(raw_csd, 128, 77, 1);
	csd->dsr_imp = mmc_get_bits(raw_csd, 128, 76, 1);
	csd->vdd_r_curr_min = cur_min[mmc_get_bits(raw_csd, 128, 59, 3)];
	csd->vdd_r_curr_max = cur_max[mmc_get_bits(raw_csd, 128, 56, 3)];
	csd->vdd_w_curr_min = cur_min[mmc_get_bits(raw_csd, 128, 53, 3)];
	csd->vdd_w_curr_max = cur_max[mmc_get_bits(raw_csd, 128, 50, 3)];
	m = mmc_get_bits(raw_csd, 128, 62, 12);
	e = mmc_get_bits(raw_csd, 128, 47, 3);
	csd->capacity = ((1 + m) << (e + 2)) * csd->read_bl_len;
	csd->erase_blk_en = 0;
	csd->erase_sector = (mmc_get_bits(raw_csd, 128, 42, 5) + 1) *
	    (mmc_get_bits(raw_csd, 128, 37, 5) + 1);
	csd->wp_grp_size = mmc_get_bits(raw_csd, 128, 32, 5);
	csd->wp_grp_enable = mmc_get_bits(raw_csd, 128, 31, 1);
	csd->r2w_factor = 1 << mmc_get_bits(raw_csd, 128, 26, 3);
	csd->write_bl_len = 1 << mmc_get_bits(raw_csd, 128, 22, 4);
	csd->write_bl_partial = mmc_get_bits(raw_csd, 128, 21, 1);
}

static void
mmc_decode_cid_sd(uint32_t *raw_cid, struct mmc_cid *cid)
{
	int i;

	/* There's no version info, so we take it on faith */
	memset(cid, 0, sizeof(*cid));
	cid->mid = mmc_get_bits(raw_cid, 128, 120, 8);
	cid->oid = mmc_get_bits(raw_cid, 128, 104, 16);
	for (i = 0; i < 5; i++)
		cid->pnm[i] = mmc_get_bits(raw_cid, 128, 96 - i * 8, 8);
	cid->pnm[5] = 0;
	cid->prv = mmc_get_bits(raw_cid, 128, 56, 8);
	cid->psn = mmc_get_bits(raw_cid, 128, 24, 32);
	cid->mdt_year = mmc_get_bits(raw_cid, 128, 12, 8) + 2000;
	cid->mdt_month = mmc_get_bits(raw_cid, 128, 8, 4);
}

static void
mmc_decode_cid_mmc(uint32_t *raw_cid, struct mmc_cid *cid)
{
	int i;

	/* There's no version info, so we take it on faith */
	memset(cid, 0, sizeof(*cid));
	cid->mid = mmc_get_bits(raw_cid, 128, 120, 8);
	cid->oid = mmc_get_bits(raw_cid, 128, 104, 8);
	for (i = 0; i < 6; i++)
		cid->pnm[i] = mmc_get_bits(raw_cid, 128, 96 - i * 8, 8);
	cid->pnm[6] = 0;
	cid->prv = mmc_get_bits(raw_cid, 128, 48, 8);
	cid->psn = mmc_get_bits(raw_cid, 128, 16, 32);
	cid->mdt_month = mmc_get_bits(raw_cid, 128, 12, 4);
	cid->mdt_year = mmc_get_bits(raw_cid, 128, 8, 4) + 1997;
}

static void
mmc_format_card_id_string(struct sdda_softc *sc, struct mmc_params *mmcp)
{
	char oidstr[8];
	uint8_t c1;
	uint8_t c2;

	/*
	 * Format a card ID string for use by the mmcsd driver, it's what
	 * appears between the <> in the following:
	 * mmcsd0: 968MB <SD SD01G 8.0 SN 2686905 Mfg 08/2008 by 3 TN> at mmc0
	 * 22.5MHz/4bit/128-block
	 *
	 * Also format just the card serial number, which the mmcsd driver will
	 * use as the disk->d_ident string.
	 *
	 * The card_id_string in mmc_ivars is currently allocated as 64 bytes,
	 * and our max formatted length is currently 55 bytes if every field
	 * contains the largest value.
	 *
	 * Sometimes the oid is two printable ascii chars; when it's not,
	 * format it as 0xnnnn instead.
	 */
	c1 = (sc->cid.oid >> 8) & 0x0ff;
	c2 = sc->cid.oid & 0x0ff;
	if (c1 > 0x1f && c1 < 0x7f && c2 > 0x1f && c2 < 0x7f)
		snprintf(oidstr, sizeof(oidstr), "%c%c", c1, c2);
	else
		snprintf(oidstr, sizeof(oidstr), "0x%04x", sc->cid.oid);
	snprintf(sc->card_sn_string, sizeof(sc->card_sn_string),
	    "%08X", sc->cid.psn);
	snprintf(sc->card_id_string, sizeof(sc->card_id_string),
                 "%s%s %s %d.%d SN %08X MFG %02d/%04d by %d %s",
                 mmcp->card_features & CARD_FEATURE_MMC ? "MMC" : "SD",
                 mmcp->card_features & CARD_FEATURE_SDHC ? "HC" : "",
                 sc->cid.pnm, sc->cid.prv >> 4, sc->cid.prv & 0x0f,
                 sc->cid.psn, sc->cid.mdt_month, sc->cid.mdt_year,
                 sc->cid.mid, oidstr);
}

static int
sddaopen(struct disk *dp)
{
	struct sdda_part *part;
	struct cam_periph *periph;
	struct sdda_softc *softc;
	int error;

	part = (struct sdda_part *)dp->d_drv1;
	softc = part->sc;
	periph = softc->periph;
	if (cam_periph_acquire(periph) != 0) {
		return(ENXIO);
	}

	cam_periph_lock(periph);
	if ((error = cam_periph_hold(periph, PRIBIO|PCATCH)) != 0) {
		cam_periph_unlock(periph);
		cam_periph_release(periph);
		return (error);
	}

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sddaopen\n"));

	part->flags |= SDDA_FLAG_OPEN;

	cam_periph_unhold(periph);
	cam_periph_unlock(periph);
	return (0);
}

static int
sddaclose(struct disk *dp)
{
	struct sdda_part *part;
	struct	cam_periph *periph;
	struct	sdda_softc *softc;

	part = (struct sdda_part *)dp->d_drv1;
	softc = part->sc;
	periph = softc->periph;
	part->flags &= ~SDDA_FLAG_OPEN;

	cam_periph_lock(periph);

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sddaclose\n"));

	while (softc->refcount != 0)
		cam_periph_sleep(periph, &softc->refcount, PRIBIO, "sddaclose", 1);
	cam_periph_unlock(periph);
	cam_periph_release(periph);
	return (0);
}

static void
sddaschedule(struct cam_periph *periph)
{
	struct sdda_softc *softc = (struct sdda_softc *)periph->softc;
	struct sdda_part *part;
	struct bio *bp;
	int i;

	/* Check if we have more work to do. */
	/* Find partition that has outstanding commands. Prefer current partition. */
	bp = bioq_first(&softc->part[softc->part_curr]->bio_queue);
	if (bp == NULL) {
		for (i = 0; i < MMC_PART_MAX; i++) {
			if ((part = softc->part[i]) != NULL &&
			    (bp = bioq_first(&softc->part[i]->bio_queue)) != NULL)
				break;
		}
	}
	if (bp != NULL) {
		xpt_schedule(periph, CAM_PRIORITY_NORMAL);
	}
}

/*
 * Actually translate the requested transfer into one the physical driver
 * can understand.  The transfer is described by a buf and will include
 * only one physical transfer.
 */
static void
sddastrategy(struct bio *bp)
{
	struct cam_periph *periph;
	struct sdda_part *part;
	struct sdda_softc *softc;

	part = (struct sdda_part *)bp->bio_disk->d_drv1;
	softc = part->sc;
	periph = softc->periph;

	cam_periph_lock(periph);

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sddastrategy(%p)\n", bp));

	/*
	 * If the device has been made invalid, error out
	 */
	if ((periph->flags & CAM_PERIPH_INVALID) != 0) {
		cam_periph_unlock(periph);
		biofinish(bp, NULL, ENXIO);
		return;
	}

	/*
	 * Place it in the queue of disk activities for this disk
	 */
	bioq_disksort(&part->bio_queue, bp);

	/*
	 * Schedule ourselves for performing the work.
	 */
	sddaschedule(periph);
	cam_periph_unlock(periph);

	return;
}

static void
sddainit(void)
{
	cam_status status;

	/*
	 * Install a global async callback.  This callback will
	 * receive async callbacks like "new device found".
	 */
	status = xpt_register_async(AC_FOUND_DEVICE, sddaasync, NULL, NULL);

	if (status != CAM_REQ_CMP) {
		printf("sdda: Failed to attach master async callback "
		       "due to status 0x%x!\n", status);
	}
}

/*
 * Callback from GEOM, called when it has finished cleaning up its
 * resources.
 */
static void
sddadiskgonecb(struct disk *dp)
{
	struct cam_periph *periph;
	struct sdda_part *part;

	part = (struct sdda_part *)dp->d_drv1;
	periph = part->sc->periph;
        CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sddadiskgonecb\n"));

	cam_periph_release(periph);
}

static void
sddaoninvalidate(struct cam_periph *periph)
{
	struct sdda_softc *softc;
	struct sdda_part *part;

	softc = (struct sdda_softc *)periph->softc;

        CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sddaoninvalidate\n"));

	/*
	 * De-register any async callbacks.
	 */
	xpt_register_async(0, sddaasync, periph, periph->path);

	/*
	 * Return all queued I/O with ENXIO.
	 * XXX Handle any transactions queued to the card
	 *     with XPT_ABORT_CCB.
	 */
        CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("bioq_flush start\n"));
	for (int i = 0; i < MMC_PART_MAX; i++) {
		if ((part = softc->part[i]) != NULL) {
			bioq_flush(&part->bio_queue, NULL, ENXIO);
			disk_gone(part->disk);
		}
	}
        CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("bioq_flush end\n"));

}

static void
sddacleanup(struct cam_periph *periph)
{
	struct sdda_softc *softc;
	struct sdda_part *part;
	int i;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sddacleanup\n"));
	softc = (struct sdda_softc *)periph->softc;

	cam_periph_unlock(periph);

	for (i = 0; i < MMC_PART_MAX; i++) {
		if ((part = softc->part[i]) != NULL) {
			disk_destroy(part->disk);
			free(part, M_DEVBUF);
			softc->part[i] = NULL;
		}
	}
	free(softc, M_DEVBUF);
	cam_periph_lock(periph);
}

static void
sddaasync(void *callback_arg, u_int32_t code,
	struct cam_path *path, void *arg)
{
	struct ccb_getdev cgd;
	struct cam_periph *periph;
	struct sdda_softc *softc;

	periph = (struct cam_periph *)callback_arg;
        CAM_DEBUG(path, CAM_DEBUG_TRACE, ("sddaasync(code=%d)\n", code));
	switch (code) {
	case AC_FOUND_DEVICE:
	{
                CAM_DEBUG(path, CAM_DEBUG_TRACE, ("=> AC_FOUND_DEVICE\n"));
		struct ccb_getdev *cgd;
		cam_status status;

		cgd = (struct ccb_getdev *)arg;
		if (cgd == NULL)
			break;

		if (cgd->protocol != PROTO_MMCSD)
			break;

                if (!(path->device->mmc_ident_data.card_features & CARD_FEATURE_MEMORY)) {
                        CAM_DEBUG(path, CAM_DEBUG_TRACE, ("No memory on the card!\n"));
                        break;
                }

		/*
		 * Allocate a peripheral instance for
		 * this device and start the probe
		 * process.
		 */
		status = cam_periph_alloc(sddaregister, sddaoninvalidate,
					  sddacleanup, sddastart,
					  "sdda", CAM_PERIPH_BIO,
					  path, sddaasync,
					  AC_FOUND_DEVICE, cgd);

		if (status != CAM_REQ_CMP
		 && status != CAM_REQ_INPROG)
			printf("sddaasync: Unable to attach to new device "
				"due to status 0x%x\n", status);
		break;
	}
	case AC_GETDEV_CHANGED:
	{
		CAM_DEBUG(path, CAM_DEBUG_TRACE, ("=> AC_GETDEV_CHANGED\n"));
		softc = (struct sdda_softc *)periph->softc;
		xpt_setup_ccb(&cgd.ccb_h, periph->path, CAM_PRIORITY_NORMAL);
		cgd.ccb_h.func_code = XPT_GDEV_TYPE;
		xpt_action((union ccb *)&cgd);
		cam_periph_async(periph, code, path, arg);
		break;
	}
	case AC_ADVINFO_CHANGED:
	{
		uintptr_t buftype;
		int i;

		CAM_DEBUG(path, CAM_DEBUG_TRACE, ("=> AC_ADVINFO_CHANGED\n"));
		buftype = (uintptr_t)arg;
		if (buftype == CDAI_TYPE_PHYS_PATH) {
			struct sdda_softc *softc;
			struct sdda_part *part;

			softc = periph->softc;
			for (i = 0; i < MMC_PART_MAX; i++) {
				if ((part = softc->part[i]) != NULL) {
					disk_attr_changed(part->disk, "GEOM::physpath",
					    M_NOWAIT);
				}
			}
		}
		break;
	}
	default:
		CAM_DEBUG(path, CAM_DEBUG_TRACE, ("=> default?!\n"));
		cam_periph_async(periph, code, path, arg);
		break;
	}
}


static int
sddagetattr(struct bio *bp)
{
	struct cam_periph *periph;
	struct sdda_softc *softc;
	struct sdda_part *part;
	int ret;

	part = (struct sdda_part *)bp->bio_disk->d_drv1;
	softc = part->sc;
	periph = softc->periph;
	cam_periph_lock(periph);
	ret = xpt_getattr(bp->bio_data, bp->bio_length, bp->bio_attribute,
	    periph->path);
	cam_periph_unlock(periph);
	if (ret == 0)
		bp->bio_completed = bp->bio_length;
	return (ret);
}

static cam_status
sddaregister(struct cam_periph *periph, void *arg)
{
	struct sdda_softc *softc;
	struct ccb_getdev *cgd;
	union ccb *request_ccb;	/* CCB representing the probe request */

        CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sddaregister\n"));
	cgd = (struct ccb_getdev *)arg;
	if (cgd == NULL) {
		printf("sddaregister: no getdev CCB, can't register device\n");
		return (CAM_REQ_CMP_ERR);
	}

	softc = (struct sdda_softc *)malloc(sizeof(*softc), M_DEVBUF,
	    M_NOWAIT|M_ZERO);

	if (softc == NULL) {
		printf("sddaregister: Unable to probe new device. "
		    "Unable to allocate softc\n");
		return (CAM_REQ_CMP_ERR);
	}

	softc->state = SDDA_STATE_INIT;
	softc->mmcdata =
		(struct mmc_data *)malloc(sizeof(struct mmc_data), M_DEVBUF, M_NOWAIT|M_ZERO);
	periph->softc = softc;
	softc->periph = periph;

	request_ccb = (union ccb*) arg;
	xpt_schedule(periph, CAM_PRIORITY_XPT);
	TASK_INIT(&softc->start_init_task, 0, sdda_start_init_task, periph);
	taskqueue_enqueue(taskqueue_thread, &softc->start_init_task);

	return (CAM_REQ_CMP);
}

static int
mmc_exec_app_cmd(struct cam_periph *periph, union ccb *ccb,
	struct mmc_command *cmd) {
	int err;

	/* Send APP_CMD first */
	memset(&ccb->mmcio.cmd, 0, sizeof(struct mmc_command));
	memset(&ccb->mmcio.stop, 0, sizeof(struct mmc_command));
	cam_fill_mmcio(&ccb->mmcio,
		       /*retries*/ 0,
		       /*cbfcnp*/ NULL,
		       /*flags*/ CAM_DIR_NONE,
		       /*mmc_opcode*/ MMC_APP_CMD,
		       /*mmc_arg*/ get_rca(periph) << 16,
		       /*mmc_flags*/ MMC_RSP_R1 | MMC_CMD_AC,
		       /*mmc_data*/ NULL,
		       /*timeout*/ 0);

	err = cam_periph_runccb(ccb, sddaerror, CAM_FLAG_NONE, /*sense_flags*/0, NULL);
	if (err != 0)
		return err;
	if (!(ccb->mmcio.cmd.resp[0] & R1_APP_CMD))
		return MMC_ERR_FAILED;

	/* Now exec actual command */
	int flags = 0;
	if (cmd->data != NULL) {
		ccb->mmcio.cmd.data = cmd->data;
		if (cmd->data->flags & MMC_DATA_READ)
			flags |= CAM_DIR_IN;
		if (cmd->data->flags & MMC_DATA_WRITE)
			flags |= CAM_DIR_OUT;
	} else flags = CAM_DIR_NONE;

	cam_fill_mmcio(&ccb->mmcio,
		       /*retries*/ 0,
		       /*cbfcnp*/ NULL,
		       /*flags*/ flags,
		       /*mmc_opcode*/ cmd->opcode,
		       /*mmc_arg*/ cmd->arg,
		       /*mmc_flags*/ cmd->flags,
		       /*mmc_data*/ cmd->data,
		       /*timeout*/ 0);

	err = cam_periph_runccb(ccb, sddaerror, CAM_FLAG_NONE, /*sense_flags*/0, NULL);
	memcpy(cmd->resp, ccb->mmcio.cmd.resp, sizeof(cmd->resp));
	cmd->error = ccb->mmcio.cmd.error;
	if (err != 0)
		return err;
	return 0;
}

static int
mmc_app_get_scr(struct cam_periph *periph, union ccb *ccb, uint32_t *rawscr) {
	int err;
	struct mmc_command cmd;
	struct mmc_data d;

	memset(&cmd, 0, sizeof(cmd));
	memset(&d, 0, sizeof(d));

	memset(rawscr, 0, 8);
	cmd.opcode = ACMD_SEND_SCR;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	cmd.arg = 0;

	d.data = rawscr;
	d.len = 8;
	d.flags = MMC_DATA_READ;
	cmd.data = &d;

	err = mmc_exec_app_cmd(periph, ccb, &cmd);
	rawscr[0] = be32toh(rawscr[0]);
	rawscr[1] = be32toh(rawscr[1]);
	return (err);
}

static int
mmc_send_ext_csd(struct cam_periph *periph, union ccb *ccb,
		 uint8_t *rawextcsd, size_t buf_len) {
	int err;
	struct mmc_data d;

	KASSERT(buf_len == 512, ("Buffer for ext csd must be 512 bytes"));
	d.data = rawextcsd;
	d.len = buf_len;
	d.flags = MMC_DATA_READ;
	memset(d.data, 0, d.len);

	cam_fill_mmcio(&ccb->mmcio,
		       /*retries*/ 0,
		       /*cbfcnp*/ NULL,
		       /*flags*/ CAM_DIR_IN,
		       /*mmc_opcode*/ MMC_SEND_EXT_CSD,
		       /*mmc_arg*/ 0,
		       /*mmc_flags*/ MMC_RSP_R1 | MMC_CMD_ADTC,
		       /*mmc_data*/ &d,
		       /*timeout*/ 0);

	err = cam_periph_runccb(ccb, sddaerror, CAM_FLAG_NONE, /*sense_flags*/0, NULL);
	if (err != 0)
		return (err);
	return (MMC_ERR_NONE);
}

static void
mmc_app_decode_scr(uint32_t *raw_scr, struct mmc_scr *scr)
{
	unsigned int scr_struct;

	memset(scr, 0, sizeof(*scr));

	scr_struct = mmc_get_bits(raw_scr, 64, 60, 4);
	if (scr_struct != 0) {
		printf("Unrecognised SCR structure version %d\n",
		    scr_struct);
		return;
	}
	scr->sda_vsn = mmc_get_bits(raw_scr, 64, 56, 4);
	scr->bus_widths = mmc_get_bits(raw_scr, 64, 48, 4);
}

static inline void
mmc_switch_fill_mmcio(union ccb *ccb,
    uint8_t set, uint8_t index, uint8_t value, u_int timeout)
{
	int arg = (MMC_SWITCH_FUNC_WR << 24) |
	    (index << 16) |
	    (value << 8) |
	    set;

	cam_fill_mmcio(&ccb->mmcio,
		       /*retries*/ 0,
		       /*cbfcnp*/ NULL,
		       /*flags*/ CAM_DIR_NONE,
		       /*mmc_opcode*/ MMC_SWITCH_FUNC,
		       /*mmc_arg*/ arg,
		       /*mmc_flags*/ MMC_RSP_R1B | MMC_CMD_AC,
		       /*mmc_data*/ NULL,
		       /*timeout*/ timeout);
}

static int
mmc_select_card(struct cam_periph *periph, union ccb *ccb, uint32_t rca)
{
	int flags;

	flags = (rca ? MMC_RSP_R1B : MMC_RSP_NONE) | MMC_CMD_AC;
	cam_fill_mmcio(&ccb->mmcio,
		       /*retries*/ 0,
		       /*cbfcnp*/ NULL,
		       /*flags*/ CAM_DIR_IN,
		       /*mmc_opcode*/ MMC_SELECT_CARD,
		       /*mmc_arg*/ rca << 16,
		       /*mmc_flags*/ flags,
		       /*mmc_data*/ NULL,
		       /*timeout*/ 0);

	cam_periph_runccb(ccb, sddaerror, CAM_FLAG_NONE, /*sense_flags*/0, NULL);

	if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)) {
		if (ccb->mmcio.cmd.error != 0) {
			CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_PERIPH,
				  ("%s: MMC_SELECT command failed", __func__));
			return EIO;
		}
		return 0; /* Normal return */
	} else {
		CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_PERIPH,
			  ("%s: CAM request failed\n", __func__));
		return EIO;
	}
}

static int
mmc_switch(struct cam_periph *periph, union ccb *ccb,
    uint8_t set, uint8_t index, uint8_t value, u_int timeout)
{

	mmc_switch_fill_mmcio(ccb, set, index, value, timeout);
	cam_periph_runccb(ccb, sddaerror, CAM_FLAG_NONE, /*sense_flags*/0, NULL);

	if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)) {
		if (ccb->mmcio.cmd.error != 0) {
			CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_PERIPH,
				  ("%s: MMC command failed", __func__));
			return (EIO);
		}
		return (0); /* Normal return */
	} else {
		CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_PERIPH,
			  ("%s: CAM request failed\n", __func__));
		return (EIO);
	}

}

static uint32_t
mmc_get_spec_vers(struct cam_periph *periph) {
	struct sdda_softc *softc = (struct sdda_softc *)periph->softc;

	return (softc->csd.spec_vers);
}

static uint64_t
mmc_get_media_size(struct cam_periph *periph) {
	struct sdda_softc *softc = (struct sdda_softc *)periph->softc;

	return (softc->mediasize);
}

static uint32_t
mmc_get_cmd6_timeout(struct cam_periph *periph)
{
	struct sdda_softc *softc = (struct sdda_softc *)periph->softc;

	if (mmc_get_spec_vers(periph) >= 6)
		return (softc->raw_ext_csd[EXT_CSD_GEN_CMD6_TIME] * 10);
	return (500 * 1000);
}

static int
mmc_sd_switch(struct cam_periph *periph, union ccb *ccb,
	      uint8_t mode, uint8_t grp, uint8_t value,
	      uint8_t *res) {

	struct mmc_data mmc_d;
	uint32_t arg;

	memset(res, 0, 64);
	mmc_d.len = 64;
	mmc_d.data = res;
	mmc_d.flags = MMC_DATA_READ;

	arg = mode << 31;			/* 0 - check, 1 - set */
	arg |= 0x00FFFFFF;
	arg &= ~(0xF << (grp * 4));
	arg |= value << (grp * 4);

	cam_fill_mmcio(&ccb->mmcio,
		       /*retries*/ 0,
		       /*cbfcnp*/ NULL,
		       /*flags*/ CAM_DIR_IN,
		       /*mmc_opcode*/ SD_SWITCH_FUNC,
		       /*mmc_arg*/ arg,
		       /*mmc_flags*/ MMC_RSP_R1 | MMC_CMD_ADTC,
		       /*mmc_data*/ &mmc_d,
		       /*timeout*/ 0);

	cam_periph_runccb(ccb, sddaerror, CAM_FLAG_NONE, /*sense_flags*/0, NULL);

	if (((ccb->ccb_h.status & CAM_STATUS_MASK) == CAM_REQ_CMP)) {
		if (ccb->mmcio.cmd.error != 0) {
			CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_PERIPH,
				  ("%s: MMC command failed", __func__));
			return EIO;
		}
		return 0; /* Normal return */
	} else {
		CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_PERIPH,
			  ("%s: CAM request failed\n", __func__));
		return EIO;
	}
}

static int
mmc_set_timing(struct cam_periph *periph,
	       union ccb *ccb,
	       enum mmc_bus_timing timing)
{
	u_char switch_res[64];
	int err;
	uint8_t	value;
	struct sdda_softc *softc = (struct sdda_softc *)periph->softc;
	struct mmc_params *mmcp = &periph->path->device->mmc_ident_data;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
		  ("mmc_set_timing(timing=%d)", timing));
	switch (timing) {
	case bus_timing_normal:
		value = 0;
		break;
	case bus_timing_hs:
		value = 1;
		break;
	default:
		return (MMC_ERR_INVALID);
	}
	if (mmcp->card_features & CARD_FEATURE_MMC) {
		err = mmc_switch(periph, ccb, EXT_CSD_CMD_SET_NORMAL,
		    EXT_CSD_HS_TIMING, value, softc->cmd6_time);
	} else {
		err = mmc_sd_switch(periph, ccb, SD_SWITCH_MODE_SET, SD_SWITCH_GROUP1, value, switch_res);
	}

	/* Set high-speed timing on the host */
	struct ccb_trans_settings_mmc *cts;
	cts = &ccb->cts.proto_specific.mmc;
	ccb->ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
	ccb->ccb_h.flags = CAM_DIR_NONE;
	ccb->ccb_h.retry_count = 0;
	ccb->ccb_h.timeout = 100;
	ccb->ccb_h.cbfcnp = NULL;
	cts->ios.timing = timing;
	cts->ios_valid = MMC_BT;
	xpt_action(ccb);

	return (err);
}

static void
sdda_start_init_task(void *context, int pending) {
	union ccb *new_ccb;
	struct cam_periph *periph;

	periph = (struct cam_periph *)context;
	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sdda_start_init_task\n"));
	new_ccb = xpt_alloc_ccb();
	xpt_setup_ccb(&new_ccb->ccb_h, periph->path,
		      CAM_PRIORITY_NONE);

	cam_periph_lock(periph);
	sdda_start_init(context, new_ccb);
	cam_periph_unlock(periph);
	xpt_free_ccb(new_ccb);
}

static void
sdda_set_bus_width(struct cam_periph *periph, union ccb *ccb, int width) {
	struct sdda_softc *softc = (struct sdda_softc *)periph->softc;
	struct mmc_params *mmcp = &periph->path->device->mmc_ident_data;
	int err;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sdda_set_bus_width\n"));

	/* First set for the card, then for the host */
	if (mmcp->card_features & CARD_FEATURE_MMC) {
		uint8_t	value;
		switch (width) {
		case bus_width_1:
			value = EXT_CSD_BUS_WIDTH_1;
			break;
		case bus_width_4:
			value = EXT_CSD_BUS_WIDTH_4;
			break;
		case bus_width_8:
			value = EXT_CSD_BUS_WIDTH_8;
			break;
		default:
			panic("Invalid bus width %d", width);
		}
		err = mmc_switch(periph, ccb, EXT_CSD_CMD_SET_NORMAL,
		    EXT_CSD_BUS_WIDTH, value, softc->cmd6_time);
	} else {
		/* For SD cards we send ACMD6 with the required bus width in arg */
		struct mmc_command cmd;
		memset(&cmd, 0, sizeof(struct mmc_command));
		cmd.opcode = ACMD_SET_BUS_WIDTH;
		cmd.arg = width;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
		err = mmc_exec_app_cmd(periph, ccb, &cmd);
	}

	if (err != MMC_ERR_NONE) {
		CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH, ("Error %d when setting bus width on the card\n", err));
		return;
	}
	/* Now card is done, set the host to the same width */
	struct ccb_trans_settings_mmc *cts;
	cts = &ccb->cts.proto_specific.mmc;
	ccb->ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
	ccb->ccb_h.flags = CAM_DIR_NONE;
	ccb->ccb_h.retry_count = 0;
	ccb->ccb_h.timeout = 100;
	ccb->ccb_h.cbfcnp = NULL;
	cts->ios.bus_width = width;
	cts->ios_valid = MMC_BW;
	xpt_action(ccb);
}

static inline const char
*part_type(u_int type)
{

	switch (type) {
	case EXT_CSD_PART_CONFIG_ACC_RPMB:
		return ("RPMB");
	case EXT_CSD_PART_CONFIG_ACC_DEFAULT:
		return ("default");
	case EXT_CSD_PART_CONFIG_ACC_BOOT0:
		return ("boot0");
	case EXT_CSD_PART_CONFIG_ACC_BOOT1:
		return ("boot1");
	case EXT_CSD_PART_CONFIG_ACC_GP0:
	case EXT_CSD_PART_CONFIG_ACC_GP1:
	case EXT_CSD_PART_CONFIG_ACC_GP2:
	case EXT_CSD_PART_CONFIG_ACC_GP3:
		return ("general purpose");
	default:
		return ("(unknown type)");
	}
}

static inline const char
*bus_width_str(enum mmc_bus_width w)
{

	switch (w) {
	case bus_width_1:
		return ("1-bit");
	case bus_width_4:
		return ("4-bit");
	case bus_width_8:
		return ("8-bit");
	}
}

static uint32_t
sdda_get_host_caps(struct cam_periph *periph, union ccb *ccb)
{
	struct ccb_trans_settings_mmc *cts;

	cts = &ccb->cts.proto_specific.mmc;

	ccb->ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
	ccb->ccb_h.flags = CAM_DIR_NONE;
	ccb->ccb_h.retry_count = 0;
	ccb->ccb_h.timeout = 100;
	ccb->ccb_h.cbfcnp = NULL;
	xpt_action(ccb);

	if (ccb->ccb_h.status != CAM_REQ_CMP)
		panic("Cannot get host caps");
	return (cts->host_caps);
}

static void
sdda_start_init(void *context, union ccb *start_ccb)
{
	struct cam_periph *periph = (struct cam_periph *)context;
	struct ccb_trans_settings_mmc *cts;
	uint32_t host_caps;
	uint32_t sec_count;
	int err;
	int host_f_max;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sdda_start_init\n"));
	/* periph was held for us when this task was enqueued */
	if ((periph->flags & CAM_PERIPH_INVALID) != 0) {
		cam_periph_release(periph);
		return;
	}

	struct sdda_softc *softc = (struct sdda_softc *)periph->softc;
	//struct ccb_mmcio *mmcio = &start_ccb->mmcio;
	struct mmc_params *mmcp = &periph->path->device->mmc_ident_data;
	struct cam_ed *device = periph->path->device;

	if (mmcp->card_features & CARD_FEATURE_MMC) {
		mmc_decode_csd_mmc(mmcp->card_csd, &softc->csd);
		mmc_decode_cid_mmc(mmcp->card_cid, &softc->cid);
		if (mmc_get_spec_vers(periph) >= 4) {
			err = mmc_send_ext_csd(periph, start_ccb,
					       (uint8_t *)&softc->raw_ext_csd,
					       sizeof(softc->raw_ext_csd));
			if (err != 0) {
				CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH,
				    ("Cannot read EXT_CSD, err %d", err));
				return;
			}
		}
	} else {
		mmc_decode_csd_sd(mmcp->card_csd, &softc->csd);
		mmc_decode_cid_sd(mmcp->card_cid, &softc->cid);
	}

	softc->sector_count = softc->csd.capacity / 512;
	softc->mediasize = softc->csd.capacity;
	softc->cmd6_time = mmc_get_cmd6_timeout(periph);

	/* MMC >= 4.x have EXT_CSD that has its own opinion about capacity */
	if (mmc_get_spec_vers(periph) >= 4) {
		sec_count = softc->raw_ext_csd[EXT_CSD_SEC_CNT] +
		    (softc->raw_ext_csd[EXT_CSD_SEC_CNT + 1] << 8) +
		    (softc->raw_ext_csd[EXT_CSD_SEC_CNT + 2] << 16) +
		    (softc->raw_ext_csd[EXT_CSD_SEC_CNT + 3] << 24);
		if (sec_count != 0) {
			softc->sector_count = sec_count;
			softc->mediasize = softc->sector_count * 512;
			/* FIXME: there should be a better name for this option...*/
			mmcp->card_features |= CARD_FEATURE_SDHC;
		}

	}
	CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH,
	    ("Capacity: %"PRIu64", sectors: %"PRIu64"\n",
		softc->mediasize,
		softc->sector_count));
	mmc_format_card_id_string(softc, mmcp);

	/* Update info for CAM */
	device->serial_num_len = strlen(softc->card_sn_string);
	device->serial_num = (u_int8_t *)malloc((device->serial_num_len + 1),
	    M_CAMXPT, M_NOWAIT);
	strlcpy(device->serial_num, softc->card_sn_string, device->serial_num_len);

	device->device_id_len = strlen(softc->card_id_string);
	device->device_id = (u_int8_t *)malloc((device->device_id_len + 1),
	    M_CAMXPT, M_NOWAIT);
	strlcpy(device->device_id, softc->card_id_string, device->device_id_len);

	strlcpy(mmcp->model, softc->card_id_string, sizeof(mmcp->model));

	/* Set the clock frequency that the card can handle */
	cts = &start_ccb->cts.proto_specific.mmc;

	/* First, get the host's max freq */
	start_ccb->ccb_h.func_code = XPT_GET_TRAN_SETTINGS;
	start_ccb->ccb_h.flags = CAM_DIR_NONE;
	start_ccb->ccb_h.retry_count = 0;
	start_ccb->ccb_h.timeout = 100;
	start_ccb->ccb_h.cbfcnp = NULL;
	xpt_action(start_ccb);

	if (start_ccb->ccb_h.status != CAM_REQ_CMP)
		panic("Cannot get max host freq");
	host_f_max = cts->host_f_max;
	host_caps = cts->host_caps;
	if (cts->ios.bus_width != bus_width_1)
		panic("Bus width in ios is not 1-bit");

	/* Now check if the card supports High-speed */
	softc->card_f_max = softc->csd.tran_speed;

	if (host_caps & MMC_CAP_HSPEED) {
		/* Find out if the card supports High speed timing */
		if (mmcp->card_features & CARD_FEATURE_SD20) {
			/* Get and decode SCR */
			uint32_t rawscr[2];
			uint8_t res[64];
			if (mmc_app_get_scr(periph, start_ccb, rawscr)) {
				CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH, ("Cannot get SCR\n"));
				goto finish_hs_tests;
			}
			mmc_app_decode_scr(rawscr, &softc->scr);

			if ((softc->scr.sda_vsn >= 1) && (softc->csd.ccc & (1<<10))) {
				mmc_sd_switch(periph, start_ccb, SD_SWITCH_MODE_CHECK,
					      SD_SWITCH_GROUP1, SD_SWITCH_NOCHANGE, res);
				if (res[13] & 2) {
					CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH, ("Card supports HS\n"));
					softc->card_f_max = SD_HS_MAX;
				}

				/*
				 * We deselect then reselect the card here.  Some cards
				 * become unselected and timeout with the above two
				 * commands, although the state tables / diagrams in the
				 * standard suggest they go back to the transfer state.
				 * Other cards don't become deselected, and if we
				 * attempt to blindly re-select them, we get timeout
				 * errors from some controllers.  So we deselect then
				 * reselect to handle all situations.
				 */
				mmc_select_card(periph, start_ccb, 0);
				mmc_select_card(periph, start_ccb, get_rca(periph));
			} else {
				CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH, ("Not trying the switch\n"));
				goto finish_hs_tests;
			}
		}

		if (mmcp->card_features & CARD_FEATURE_MMC && mmc_get_spec_vers(periph) >= 4) {
			if (softc->raw_ext_csd[EXT_CSD_CARD_TYPE]
			    & EXT_CSD_CARD_TYPE_HS_52)
				softc->card_f_max = MMC_TYPE_HS_52_MAX;
			else if (softc->raw_ext_csd[EXT_CSD_CARD_TYPE]
				 & EXT_CSD_CARD_TYPE_HS_26)
				softc->card_f_max = MMC_TYPE_HS_26_MAX;
		}
	}
	int f_max;
finish_hs_tests:
	f_max = min(host_f_max, softc->card_f_max);
	CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH, ("Set SD freq to %d MHz (min out of host f=%d MHz and card f=%d MHz)\n", f_max  / 1000000, host_f_max / 1000000, softc->card_f_max / 1000000));

	/* Enable high-speed timing on the card */
	if (f_max > 25000000) {
		err = mmc_set_timing(periph, start_ccb, bus_timing_hs);
		if (err != MMC_ERR_NONE) {
			CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("Cannot switch card to high-speed mode"));
			f_max = 25000000;
		}
	}
	/* Set frequency on the controller */
	start_ccb->ccb_h.func_code = XPT_SET_TRAN_SETTINGS;
	start_ccb->ccb_h.flags = CAM_DIR_NONE;
	start_ccb->ccb_h.retry_count = 0;
	start_ccb->ccb_h.timeout = 100;
	start_ccb->ccb_h.cbfcnp = NULL;
	cts->ios.clock = f_max;
	cts->ios_valid = MMC_CLK;
	xpt_action(start_ccb);

	/* Set bus width */
	enum mmc_bus_width desired_bus_width = bus_width_1;
	enum mmc_bus_width max_host_bus_width =
		(host_caps & MMC_CAP_8_BIT_DATA ? bus_width_8 :
		 host_caps & MMC_CAP_4_BIT_DATA ? bus_width_4 : bus_width_1);
	enum mmc_bus_width max_card_bus_width = bus_width_1;
	if (mmcp->card_features & CARD_FEATURE_SD20 &&
	    softc->scr.bus_widths & SD_SCR_BUS_WIDTH_4)
		max_card_bus_width = bus_width_4;
	/*
	 * Unlike SD, MMC cards don't have any information about supported bus width...
	 * So we need to perform read/write test to find out the width.
	 */
	/* TODO: figure out bus width for MMC; use 8-bit for now (to test on BBB) */
	if (mmcp->card_features & CARD_FEATURE_MMC)
		max_card_bus_width = bus_width_8;

	desired_bus_width = min(max_host_bus_width, max_card_bus_width);
	CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH,
		  ("Set bus width to %s (min of host %s and card %s)\n",
		   bus_width_str(desired_bus_width),
		   bus_width_str(max_host_bus_width),
		   bus_width_str(max_card_bus_width)));
	sdda_set_bus_width(periph, start_ccb, desired_bus_width);

	softc->state = SDDA_STATE_NORMAL;

	/* MMC partitions support */
	if (mmcp->card_features & CARD_FEATURE_MMC && mmc_get_spec_vers(periph) >= 4) {
		sdda_process_mmc_partitions(periph, start_ccb);
	} else if (mmcp->card_features & CARD_FEATURE_SD20) {
		/* For SD[HC] cards, just add one partition that is the whole card */
		sdda_add_part(periph, 0, "sdda",
		    periph->unit_number,
		    mmc_get_media_size(periph),
		    sdda_get_read_only(periph, start_ccb));
		softc->part_curr = 0;
	}

	xpt_announce_periph(periph, softc->card_id_string);
	/*
	 * Add async callbacks for bus reset and bus device reset calls.
	 * I don't bother checking if this fails as, in most cases,
	 * the system will function just fine without them and the only
	 * alternative would be to not attach the device on failure.
	 */
	xpt_register_async(AC_LOST_DEVICE | AC_GETDEV_CHANGED |
	    AC_ADVINFO_CHANGED, sddaasync, periph, periph->path);
}

static void
sdda_add_part(struct cam_periph *periph, u_int type, const char *name,
    u_int cnt, off_t media_size, bool ro)
{
	struct sdda_softc *sc = (struct sdda_softc *)periph->softc;
	struct sdda_part *part;
	struct ccb_pathinq cpi;
	u_int maxio;

	CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH,
	    ("Partition type '%s', size %ju %s\n",
	    part_type(type),
	    media_size,
	    ro ? "(read-only)" : ""));

	part = sc->part[type] = malloc(sizeof(*part), M_DEVBUF,
	    M_WAITOK | M_ZERO);

	part->cnt = cnt;
	part->type = type;
	part->ro = ro;
	part->sc = sc;
	snprintf(part->name, sizeof(part->name), name, periph->unit_number);

	/*
	 * Due to the nature of RPMB partition it doesn't make much sense
	 * to add it as a disk. It would be more appropriate to create a
	 * userland tool to operate on the partition or leverage the existing
	 * tools from sysutils/mmc-utils.
	 */
	if (type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		/* TODO: Create device, assign IOCTL handler */
		CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH,
		    ("Don't know what to do with RPMB partitions yet\n"));
		return;
	}

	bioq_init(&part->bio_queue);

	bzero(&cpi, sizeof(cpi));
	xpt_setup_ccb(&cpi.ccb_h, periph->path, CAM_PRIORITY_NONE);
	cpi.ccb_h.func_code = XPT_PATH_INQ;
	xpt_action((union ccb *)&cpi);

	/*
	 * Register this media as a disk
	 */
	(void)cam_periph_hold(periph, PRIBIO);
	cam_periph_unlock(periph);

	part->disk = disk_alloc();
	part->disk->d_rotation_rate = DISK_RR_NON_ROTATING;
	part->disk->d_devstat = devstat_new_entry(part->name,
	    cnt, 512,
	    DEVSTAT_ALL_SUPPORTED,
	    DEVSTAT_TYPE_DIRECT | XPORT_DEVSTAT_TYPE(cpi.transport),
	    DEVSTAT_PRIORITY_DISK);

	part->disk->d_open = sddaopen;
	part->disk->d_close = sddaclose;
	part->disk->d_strategy = sddastrategy;
	part->disk->d_getattr = sddagetattr;
//	sc->disk->d_dump = sddadump;
	part->disk->d_gone = sddadiskgonecb;
	part->disk->d_name = part->name;
	part->disk->d_drv1 = part;
	maxio = cpi.maxio;		/* Honor max I/O size of SIM */
	if (maxio == 0)
		maxio = DFLTPHYS;	/* traditional default */
	else if (maxio > MAXPHYS)
		maxio = MAXPHYS;	/* for safety */
	part->disk->d_maxsize = maxio;
	part->disk->d_unit = cnt;
	part->disk->d_flags = 0;
	strlcpy(part->disk->d_descr, sc->card_id_string,
	    MIN(sizeof(part->disk->d_descr), sizeof(sc->card_id_string)));
	strlcpy(part->disk->d_ident, sc->card_sn_string,
	    MIN(sizeof(part->disk->d_ident), sizeof(sc->card_sn_string)));
	part->disk->d_hba_vendor = cpi.hba_vendor;
	part->disk->d_hba_device = cpi.hba_device;
	part->disk->d_hba_subvendor = cpi.hba_subvendor;
	part->disk->d_hba_subdevice = cpi.hba_subdevice;

	part->disk->d_sectorsize = mmc_get_sector_size(periph);
	part->disk->d_mediasize = media_size;
	part->disk->d_stripesize = 0;
	part->disk->d_fwsectors = 0;
	part->disk->d_fwheads = 0;

	/*
	 * Acquire a reference to the periph before we register with GEOM.
	 * We'll release this reference once GEOM calls us back (via
	 * sddadiskgonecb()) telling us that our provider has been freed.
	 */
	if (cam_periph_acquire(periph) != 0) {
		xpt_print(periph->path, "%s: lost periph during "
		    "registration!\n", __func__);
		cam_periph_lock(periph);
		return;
	}
	disk_create(part->disk, DISK_VERSION);
	cam_periph_lock(periph);
	cam_periph_unhold(periph);
}

/*
 * For MMC cards, process EXT_CSD and add partitions that are supported by
 * this device.
 */
static void
sdda_process_mmc_partitions(struct cam_periph *periph, union ccb *ccb)
{
	struct sdda_softc *sc = (struct sdda_softc *)periph->softc;
	struct mmc_params *mmcp = &periph->path->device->mmc_ident_data;
	off_t erase_size, sector_size, size, wp_size;
	int i;
	const uint8_t *ext_csd;
	uint8_t rev;
	bool comp, ro;

	ext_csd = sc->raw_ext_csd;

	/*
	 * Enhanced user data area and general purpose partitions are only
	 * supported in revision 1.4 (EXT_CSD_REV == 4) and later, the RPMB
	 * partition in revision 1.5 (MMC v4.41, EXT_CSD_REV == 5) and later.
	 */
	rev = ext_csd[EXT_CSD_REV];

	/*
	 * Ignore user-creatable enhanced user data area and general purpose
	 * partitions partitions as long as partitioning hasn't been finished.
	 */
	comp = (ext_csd[EXT_CSD_PART_SET] & EXT_CSD_PART_SET_COMPLETED) != 0;

	/*
	 * Add enhanced user data area slice, unless it spans the entirety of
	 * the user data area.  The enhanced area is of a multiple of high
	 * capacity write protect groups ((ERASE_GRP_SIZE + HC_WP_GRP_SIZE) *
	 * 512 KB) and its offset given in either sectors or bytes, depending
	 * on whether it's a high capacity device or not.
	 * NB: The slicer and its slices need to be registered before adding
	 *     the disk for the corresponding user data area as re-tasting is
	 *     racy.
	 */
	sector_size = mmc_get_sector_size(periph);
	size = ext_csd[EXT_CSD_ENH_SIZE_MULT] +
		(ext_csd[EXT_CSD_ENH_SIZE_MULT + 1] << 8) +
		(ext_csd[EXT_CSD_ENH_SIZE_MULT + 2] << 16);
	if (rev >= 4 && comp == TRUE && size > 0 &&
	    (ext_csd[EXT_CSD_PART_SUPPORT] &
		EXT_CSD_PART_SUPPORT_ENH_ATTR_EN) != 0 &&
	    (ext_csd[EXT_CSD_PART_ATTR] & (EXT_CSD_PART_ATTR_ENH_USR)) != 0) {
		erase_size = ext_csd[EXT_CSD_ERASE_GRP_SIZE] * 1024 *
			MMC_SECTOR_SIZE;
		wp_size = ext_csd[EXT_CSD_HC_WP_GRP_SIZE];
		size *= erase_size * wp_size;
		if (size != mmc_get_media_size(periph) * sector_size) {
			sc->enh_size = size;
			sc->enh_base = (ext_csd[EXT_CSD_ENH_START_ADDR] +
			    (ext_csd[EXT_CSD_ENH_START_ADDR + 1] << 8) +
			    (ext_csd[EXT_CSD_ENH_START_ADDR + 2] << 16) +
			    (ext_csd[EXT_CSD_ENH_START_ADDR + 3] << 24)) *
				((mmcp->card_features & CARD_FEATURE_SDHC) ? 1: MMC_SECTOR_SIZE);
		} else
			CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH,
			    ("enhanced user data area spans entire device"));
	}

	/*
	 * Add default partition.  This may be the only one or the user
	 * data area in case partitions are supported.
	 */
	ro = sdda_get_read_only(periph, ccb);
	sdda_add_part(periph, EXT_CSD_PART_CONFIG_ACC_DEFAULT, "sdda",
	    periph->unit_number, mmc_get_media_size(periph), ro);
	sc->part_curr = EXT_CSD_PART_CONFIG_ACC_DEFAULT;

	if (mmc_get_spec_vers(periph) < 3)
		return;

	/* Belatedly announce enhanced user data slice. */
	if (sc->enh_size != 0) {
		CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH,
		    ("enhanced user data area off 0x%jx size %ju bytes\n",
			sc->enh_base, sc->enh_size));
	}

	/*
	 * Determine partition switch timeout (provided in units of 10 ms)
	 * and ensure it's at least 300 ms as some eMMC chips lie.
	 */
	sc->part_time = max(ext_csd[EXT_CSD_PART_SWITCH_TO] * 10 * 1000,
	    300 * 1000);

	/* Add boot partitions, which are of a fixed multiple of 128 KB. */
	size = ext_csd[EXT_CSD_BOOT_SIZE_MULT] * MMC_BOOT_RPMB_BLOCK_SIZE;
	if (size > 0 && (sdda_get_host_caps(periph, ccb) & MMC_CAP_BOOT_NOACC) == 0) {
		sdda_add_part(periph, EXT_CSD_PART_CONFIG_ACC_BOOT0,
		    SDDA_FMT_BOOT, 0, size,
		    ro | ((ext_csd[EXT_CSD_BOOT_WP_STATUS] &
		    EXT_CSD_BOOT_WP_STATUS_BOOT0_MASK) != 0));
		sdda_add_part(periph, EXT_CSD_PART_CONFIG_ACC_BOOT1,
		    SDDA_FMT_BOOT, 1, size,
		    ro | ((ext_csd[EXT_CSD_BOOT_WP_STATUS] &
		    EXT_CSD_BOOT_WP_STATUS_BOOT1_MASK) != 0));
	}

	/* Add RPMB partition, which also is of a fixed multiple of 128 KB. */
	size = ext_csd[EXT_CSD_RPMB_MULT] * MMC_BOOT_RPMB_BLOCK_SIZE;
	if (rev >= 5 && size > 0)
		sdda_add_part(periph, EXT_CSD_PART_CONFIG_ACC_RPMB,
		    SDDA_FMT_RPMB, 0, size, ro);

	if (rev <= 3 || comp == FALSE)
		return;

	/*
	 * Add general purpose partitions, which are of a multiple of high
	 * capacity write protect groups, too.
	 */
	if ((ext_csd[EXT_CSD_PART_SUPPORT] & EXT_CSD_PART_SUPPORT_EN) != 0) {
		erase_size = ext_csd[EXT_CSD_ERASE_GRP_SIZE] * 1024 *
			MMC_SECTOR_SIZE;
		wp_size = ext_csd[EXT_CSD_HC_WP_GRP_SIZE];
		for (i = 0; i < MMC_PART_GP_MAX; i++) {
			size = ext_csd[EXT_CSD_GP_SIZE_MULT + i * 3] +
				(ext_csd[EXT_CSD_GP_SIZE_MULT + i * 3 + 1] << 8) +
				(ext_csd[EXT_CSD_GP_SIZE_MULT + i * 3 + 2] << 16);
			if (size == 0)
				continue;
			sdda_add_part(periph, EXT_CSD_PART_CONFIG_ACC_GP0 + i,
			    SDDA_FMT_GP, i, size * erase_size * wp_size, ro);
		}
	}
}

/*
 * We cannot just call mmc_switch() since it will sleep, and we are in
 * GEOM context and cannot sleep. Instead, create an MMCIO request to switch
 * partitions and send it to h/w, and upon completion resume processing
 * the I/O queue.
 * This function cannot fail, instead check switch errors in sddadone().
 */
static void
sdda_init_switch_part(struct cam_periph *periph, union ccb *start_ccb, u_int part) {
	struct sdda_softc *sc = (struct sdda_softc *)periph->softc;
	uint8_t value;

	sc->part_requested = part;

	value = (sc->raw_ext_csd[EXT_CSD_PART_CONFIG] &
	    ~EXT_CSD_PART_CONFIG_ACC_MASK) | part;

	mmc_switch_fill_mmcio(start_ccb, EXT_CSD_CMD_SET_NORMAL,
	    EXT_CSD_PART_CONFIG, value, sc->part_time);
	start_ccb->ccb_h.cbfcnp = sddadone;

	sc->outstanding_cmds++;
	cam_periph_unlock(periph);
	xpt_action(start_ccb);
	cam_periph_lock(periph);
}

/* Called with periph lock held! */
static void
sddastart(struct cam_periph *periph, union ccb *start_ccb)
{
	struct bio *bp;
	struct sdda_softc *softc = (struct sdda_softc *)periph->softc;
	struct sdda_part *part;
	struct mmc_params *mmcp = &periph->path->device->mmc_ident_data;
	int part_index;

	CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("sddastart\n"));

	if (softc->state != SDDA_STATE_NORMAL) {
		CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("device is not in SDDA_STATE_NORMAL yet\n"));
		xpt_release_ccb(start_ccb);
		return;
	}

	/* Find partition that has outstanding commands.  Prefer current partition. */
	part = softc->part[softc->part_curr];
	bp = bioq_first(&part->bio_queue);
	if (bp == NULL) {
		for (part_index = 0; part_index < MMC_PART_MAX; part_index++) {
			if ((part = softc->part[part_index]) != NULL &&
			    (bp = bioq_first(&softc->part[part_index]->bio_queue)) != NULL)
				break;
		}
	}
	if (bp == NULL) {
		xpt_release_ccb(start_ccb);
		return;
	}
	if (part_index != softc->part_curr) {
		CAM_DEBUG(periph->path, CAM_DEBUG_PERIPH,
		    ("Partition  %d -> %d\n", softc->part_curr, part_index));
		/*
		 * According to section "6.2.2 Command restrictions" of the eMMC
		 * specification v5.1, CMD19/CMD21 aren't allowed to be used with
		 * RPMB partitions.  So we pause re-tuning along with triggering
		 * it up-front to decrease the likelihood of re-tuning becoming
		 * necessary while accessing an RPMB partition.  Consequently, an
		 * RPMB partition should immediately be switched away from again
		 * after an access in order to allow for re-tuning to take place
		 * anew.
		 */
		/* TODO: pause retune if switching to RPMB partition */
		softc->state = SDDA_STATE_PART_SWITCH;
		sdda_init_switch_part(periph, start_ccb, part_index);
		return;
	}

	bioq_remove(&part->bio_queue, bp);

	switch (bp->bio_cmd) {
	case BIO_WRITE:
		CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("BIO_WRITE\n"));
		part->flags |= SDDA_FLAG_DIRTY;
		/* FALLTHROUGH */
	case BIO_READ:
	{
		struct ccb_mmcio *mmcio;
		uint64_t blockno = bp->bio_pblkno;
		uint16_t count = bp->bio_bcount / 512;
		uint16_t opcode;

		if (bp->bio_cmd == BIO_READ)
			CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("BIO_READ\n"));
		CAM_DEBUG(periph->path, CAM_DEBUG_TRACE,
		    ("Block %"PRIu64" cnt %u\n", blockno, count));

		/* Construct new MMC command */
		if (bp->bio_cmd == BIO_READ) {
			if (count > 1)
				opcode = MMC_READ_MULTIPLE_BLOCK;
			else
				opcode = MMC_READ_SINGLE_BLOCK;
		} else {
			if (count > 1)
				opcode = MMC_WRITE_MULTIPLE_BLOCK;
			else
				opcode = MMC_WRITE_BLOCK;
		}

		start_ccb->ccb_h.func_code = XPT_MMC_IO;
		start_ccb->ccb_h.flags = (bp->bio_cmd == BIO_READ ? CAM_DIR_IN : CAM_DIR_OUT);
		start_ccb->ccb_h.retry_count = 0;
		start_ccb->ccb_h.timeout = 15 * 1000;
		start_ccb->ccb_h.cbfcnp = sddadone;

		mmcio = &start_ccb->mmcio;
		mmcio->cmd.opcode = opcode;
		mmcio->cmd.arg = blockno;
		if (!(mmcp->card_features & CARD_FEATURE_SDHC))
			mmcio->cmd.arg <<= 9;

		mmcio->cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
		mmcio->cmd.data = softc->mmcdata;
		mmcio->cmd.data->data = bp->bio_data;
		mmcio->cmd.data->len = 512 * count;
		mmcio->cmd.data->flags = (bp->bio_cmd == BIO_READ ? MMC_DATA_READ : MMC_DATA_WRITE);
		/* Direct h/w to issue CMD12 upon completion */
		if (count > 1) {
			mmcio->cmd.data->flags |= MMC_DATA_MULTI;
			mmcio->stop.opcode = MMC_STOP_TRANSMISSION;
			mmcio->stop.flags = MMC_RSP_R1B | MMC_CMD_AC;
			mmcio->stop.arg = 0;
		}

		break;
	}
	case BIO_FLUSH:
		CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("BIO_FLUSH\n"));
		sddaschedule(periph);
		break;
	case BIO_DELETE:
		CAM_DEBUG(periph->path, CAM_DEBUG_TRACE, ("BIO_DELETE\n"));
		sddaschedule(periph);
		break;
	}
	start_ccb->ccb_h.ccb_bp = bp;
	softc->outstanding_cmds++;
	softc->refcount++;
	cam_periph_unlock(periph);
	xpt_action(start_ccb);
	cam_periph_lock(periph);

	/* May have more work to do, so ensure we stay scheduled */
	sddaschedule(periph);
}

static void
sddadone(struct cam_periph *periph, union ccb *done_ccb)
{
	struct bio *bp;
	struct sdda_softc *softc;
	struct ccb_mmcio *mmcio;
	struct cam_path *path;
	uint32_t card_status;
	int error = 0;

	softc = (struct sdda_softc *)periph->softc;
	mmcio = &done_ccb->mmcio;
	path = done_ccb->ccb_h.path;

	CAM_DEBUG(path, CAM_DEBUG_TRACE, ("sddadone\n"));
//        cam_periph_lock(periph);
	if ((done_ccb->ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) {
		CAM_DEBUG(path, CAM_DEBUG_TRACE, ("Error!!!\n"));
		if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
			cam_release_devq(path,
			    /*relsim_flags*/0,
			    /*reduction*/0,
			    /*timeout*/0,
			    /*getcount_only*/0);
		error = 5; /* EIO */
	} else {
		if ((done_ccb->ccb_h.status & CAM_DEV_QFRZN) != 0)
			panic("REQ_CMP with QFRZN");
		error = 0;
	}

	card_status = mmcio->cmd.resp[0];
	CAM_DEBUG(path, CAM_DEBUG_TRACE,
	    ("Card status: %08x\n", R1_STATUS(card_status)));
	CAM_DEBUG(path, CAM_DEBUG_TRACE,
	    ("Current state: %d\n", R1_CURRENT_STATE(card_status)));

	/* Process result of switching MMC partitions */
	if (softc->state == SDDA_STATE_PART_SWITCH) {
		CAM_DEBUG(path, CAM_DEBUG_TRACE,
		    ("Compteting partition switch to %d\n", softc->part_requested));
		softc->outstanding_cmds--;
		/* Complete partition switch */
		softc->state = SDDA_STATE_NORMAL;
		if (error != MMC_ERR_NONE) {
			/* TODO: Unpause retune if accessing RPMB */
			xpt_release_ccb(done_ccb);
			xpt_schedule(periph, CAM_PRIORITY_NORMAL);
			return;
		}

		softc->raw_ext_csd[EXT_CSD_PART_CONFIG] =
		    (softc->raw_ext_csd[EXT_CSD_PART_CONFIG] &
			~EXT_CSD_PART_CONFIG_ACC_MASK) | softc->part_requested;
		/* TODO: Unpause retune if accessing RPMB */
		softc->part_curr = softc->part_requested;
		xpt_release_ccb(done_ccb);

		/* Return to processing BIO requests */
		xpt_schedule(periph, CAM_PRIORITY_NORMAL);
		return;
	}

	bp = (struct bio *)done_ccb->ccb_h.ccb_bp;
	bp->bio_error = error;
	if (error != 0) {
		bp->bio_resid = bp->bio_bcount;
		bp->bio_flags |= BIO_ERROR;
	} else {
		/* XXX: How many bytes remaining? */
		bp->bio_resid = 0;
		if (bp->bio_resid > 0)
			bp->bio_flags |= BIO_ERROR;
	}

	softc->outstanding_cmds--;
	xpt_release_ccb(done_ccb);
	/*
	 * Release the periph refcount taken in sddastart() for each CCB.
	 */
	KASSERT(softc->refcount >= 1, ("sddadone softc %p refcount %d", softc, softc->refcount));
	softc->refcount--;
	biodone(bp);
}

static int
sddaerror(union ccb *ccb, u_int32_t cam_flags, u_int32_t sense_flags)
{
	return(cam_periph_error(ccb, cam_flags, sense_flags));
}
#endif /* _KERNEL */
