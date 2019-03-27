/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Bernd Walter.  All rights reserved.
 * Copyright (c) 2006 M. Warner Losh.
 * Copyright (c) 2017 Marius Strobl <marius@FreeBSD.org>
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
 * Portions of this software may have been developed with reference to
 * the SD Simplified Specification.  The following disclaimer may apply:
 *
 * The following conditions apply to the release of the simplified
 * specification ("Simplified Specification") by the SD Card Association and
 * the SD Group. The Simplified Specification is a subset of the complete SD
 * Specification which is owned by the SD Card Association and the SD
 * Group. This Simplified Specification is provided on a non-confidential
 * basis subject to the disclaimers below. Any implementation of the
 * Simplified Specification may require a license from the SD Card
 * Association, SD Group, SD-3C LLC or other third parties.
 *
 * Disclaimers:
 *
 * The information contained in the Simplified Specification is presented only
 * as a standard specification for SD Cards and SD Host/Ancillary products and
 * is provided "AS-IS" without any representations or warranties of any
 * kind. No responsibility is assumed by the SD Group, SD-3C LLC or the SD
 * Card Association for any damages, any infringements of patents or other
 * right of the SD Group, SD-3C LLC, the SD Card Association or any third
 * parties, which may result from its use. No license is granted by
 * implication, estoppel or otherwise under any patent or other rights of the
 * SD Group, SD-3C LLC, the SD Card Association or any third party. Nothing
 * herein shall be construed as an obligation by the SD Group, the SD-3C LLC
 * or the SD Card Association to disclose or distribute any technical
 * information, know-how or other confidential information to any third party.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/slicer.h>
#include <sys/sysctl.h>
#include <sys/time.h>

#include <geom/geom.h>
#include <geom/geom_disk.h>

#include <dev/mmc/bridge.h>
#include <dev/mmc/mmc_ioctl.h>
#include <dev/mmc/mmc_subr.h>
#include <dev/mmc/mmcbrvar.h>
#include <dev/mmc/mmcreg.h>
#include <dev/mmc/mmcvar.h>

#include "mmcbus_if.h"

#if __FreeBSD_version < 800002
#define	kproc_create	kthread_create
#define	kproc_exit	kthread_exit
#endif

#define	MMCSD_CMD_RETRIES	5

#define	MMCSD_FMT_BOOT		"mmcsd%dboot"
#define	MMCSD_FMT_GP		"mmcsd%dgp"
#define	MMCSD_FMT_RPMB		"mmcsd%drpmb"
#define	MMCSD_LABEL_ENH		"enh"

#define	MMCSD_PART_NAMELEN	(16 + 1)

struct mmcsd_softc;

struct mmcsd_part {
	struct mtx disk_mtx;
	struct mtx ioctl_mtx;
	struct mmcsd_softc *sc;
	struct disk *disk;
	struct proc *p;
	struct bio_queue_head bio_queue;
	daddr_t eblock, eend;	/* Range remaining after the last erase. */
	u_int cnt;
	u_int type;
	int running;
	int suspend;
	int ioctl;
	bool ro;
	char name[MMCSD_PART_NAMELEN];
};

struct mmcsd_softc {
	device_t dev;
	device_t mmcbus;
	struct mmcsd_part *part[MMC_PART_MAX];
	enum mmc_card_mode mode;
	u_int max_data;		/* Maximum data size [blocks] */
	u_int erase_sector;	/* Device native erase sector size [blocks] */
	uint8_t	high_cap;	/* High Capacity device (block addressed) */
	uint8_t part_curr;	/* Partition currently switched to */
	uint8_t ext_csd[MMC_EXTCSD_SIZE];
	uint16_t rca;
	uint32_t flags;
#define	MMCSD_INAND_CMD38	0x0001
#define	MMCSD_USE_TRIM		0x0002
#define	MMCSD_FLUSH_CACHE	0x0004
#define	MMCSD_DIRTY		0x0008
	uint32_t cmd6_time;	/* Generic switch timeout [us] */
	uint32_t part_time;	/* Partition switch timeout [us] */
	off_t enh_base;		/* Enhanced user data area slice base ... */
	off_t enh_size;		/* ... and size [bytes] */
	int log_count;
	struct timeval log_time;
	struct cdev *rpmb_dev;
};

static const char *errmsg[] =
{
	"None",
	"Timeout",
	"Bad CRC",
	"Fifo",
	"Failed",
	"Invalid",
	"NO MEMORY"
};

static SYSCTL_NODE(_hw, OID_AUTO, mmcsd, CTLFLAG_RD, NULL, "mmcsd driver");

static int mmcsd_cache = 1;
SYSCTL_INT(_hw_mmcsd, OID_AUTO, cache, CTLFLAG_RDTUN, &mmcsd_cache, 0,
    "Device R/W cache enabled if present");

#define	LOG_PPS		5 /* Log no more than 5 errors per second. */

/* bus entry points */
static int mmcsd_attach(device_t dev);
static int mmcsd_detach(device_t dev);
static int mmcsd_probe(device_t dev);
static int mmcsd_shutdown(device_t dev);

/* disk routines */
static int mmcsd_close(struct disk *dp);
static int mmcsd_dump(void *arg, void *virtual, vm_offset_t physical,
    off_t offset, size_t length);
static int mmcsd_getattr(struct bio *);
static int mmcsd_ioctl_disk(struct disk *disk, u_long cmd, void *data,
    int fflag, struct thread *td);
static void mmcsd_strategy(struct bio *bp);
static void mmcsd_task(void *arg);

/* RMPB cdev interface */
static int mmcsd_ioctl_rpmb(struct cdev *dev, u_long cmd, caddr_t data,
    int fflag, struct thread *td);

static void mmcsd_add_part(struct mmcsd_softc *sc, u_int type,
    const char *name, u_int cnt, off_t media_size, bool ro);
static int mmcsd_bus_bit_width(device_t dev);
static daddr_t mmcsd_delete(struct mmcsd_part *part, struct bio *bp);
static const char *mmcsd_errmsg(int e);
static int mmcsd_flush_cache(struct mmcsd_softc *sc);
static int mmcsd_ioctl(struct mmcsd_part *part, u_long cmd, void *data,
    int fflag, struct thread *td);
static int mmcsd_ioctl_cmd(struct mmcsd_part *part, struct mmc_ioc_cmd *mic,
    int fflag);
static uintmax_t mmcsd_pretty_size(off_t size, char *unit);
static daddr_t mmcsd_rw(struct mmcsd_part *part, struct bio *bp);
static int mmcsd_set_blockcount(struct mmcsd_softc *sc, u_int count, bool rel);
static int mmcsd_slicer(device_t dev, const char *provider,
    struct flash_slice *slices, int *nslices);
static int mmcsd_switch_part(device_t bus, device_t dev, uint16_t rca,
    u_int part);

#define	MMCSD_DISK_LOCK(_part)		mtx_lock(&(_part)->disk_mtx)
#define	MMCSD_DISK_UNLOCK(_part)	mtx_unlock(&(_part)->disk_mtx)
#define	MMCSD_DISK_LOCK_INIT(_part)					\
	mtx_init(&(_part)->disk_mtx, (_part)->name, "mmcsd disk", MTX_DEF)
#define	MMCSD_DISK_LOCK_DESTROY(_part)	mtx_destroy(&(_part)->disk_mtx);
#define	MMCSD_DISK_ASSERT_LOCKED(_part)					\
	mtx_assert(&(_part)->disk_mtx, MA_OWNED);
#define	MMCSD_DISK_ASSERT_UNLOCKED(_part)				\
	mtx_assert(&(_part)->disk_mtx, MA_NOTOWNED);

#define	MMCSD_IOCTL_LOCK(_part)		mtx_lock(&(_part)->ioctl_mtx)
#define	MMCSD_IOCTL_UNLOCK(_part)	mtx_unlock(&(_part)->ioctl_mtx)
#define	MMCSD_IOCTL_LOCK_INIT(_part)					\
	mtx_init(&(_part)->ioctl_mtx, (_part)->name, "mmcsd IOCTL", MTX_DEF)
#define	MMCSD_IOCTL_LOCK_DESTROY(_part)	mtx_destroy(&(_part)->ioctl_mtx);
#define	MMCSD_IOCTL_ASSERT_LOCKED(_part)				\
	mtx_assert(&(_part)->ioctl_mtx, MA_OWNED);
#define	MMCSD_IOCLT_ASSERT_UNLOCKED(_part)				\
	mtx_assert(&(_part)->ioctl_mtx, MA_NOTOWNED);

static int
mmcsd_probe(device_t dev)
{

	device_quiet(dev);
	device_set_desc(dev, "MMC/SD Memory Card");
	return (0);
}

static int
mmcsd_attach(device_t dev)
{
	device_t mmcbus;
	struct mmcsd_softc *sc;
	const uint8_t *ext_csd;
	off_t erase_size, sector_size, size, wp_size;
	uintmax_t bytes;
	int err, i;
	uint32_t quirks;
	uint8_t rev;
	bool comp, ro;
	char unit[2];

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->mmcbus = mmcbus = device_get_parent(dev);
	sc->mode = mmc_get_card_type(dev);
	/*
	 * Note that in principle with an SDHCI-like re-tuning implementation,
	 * the maximum data size can change at runtime due to a device removal/
	 * insertion that results in switches to/from a transfer mode involving
	 * re-tuning, iff there are multiple devices on a given bus.  Until now
	 * mmc(4) lacks support for rescanning already attached buses, however,
	 * and sdhci(4) to date has no support for shared buses in the first
	 * place either.
	 */
	sc->max_data = mmc_get_max_data(dev);
	sc->high_cap = mmc_get_high_cap(dev);
	sc->rca = mmc_get_rca(dev);
	sc->cmd6_time = mmc_get_cmd6_timeout(dev);
	quirks = mmc_get_quirks(dev);

	/* Only MMC >= 4.x devices support EXT_CSD. */
	if (mmc_get_spec_vers(dev) >= 4) {
		MMCBUS_ACQUIRE_BUS(mmcbus, dev);
		err = mmc_send_ext_csd(mmcbus, dev, sc->ext_csd);
		MMCBUS_RELEASE_BUS(mmcbus, dev);
		if (err != MMC_ERR_NONE) {
			device_printf(dev, "Error reading EXT_CSD %s\n",
			    mmcsd_errmsg(err));
			return (ENXIO);
		}
	}
	ext_csd = sc->ext_csd;

	if ((quirks & MMC_QUIRK_INAND_CMD38) != 0) {
		if (mmc_get_spec_vers(dev) < 4) {
			device_printf(dev,
			    "MMC_QUIRK_INAND_CMD38 set but no EXT_CSD\n");
			return (EINVAL);
		}
		sc->flags |= MMCSD_INAND_CMD38;
	}

	/*
	 * EXT_CSD_SEC_FEATURE_SUPPORT_GB_CL_EN denotes support for both
	 * insecure and secure TRIM.
	 */
	if ((ext_csd[EXT_CSD_SEC_FEATURE_SUPPORT] &
	    EXT_CSD_SEC_FEATURE_SUPPORT_GB_CL_EN) != 0 &&
	    (quirks & MMC_QUIRK_BROKEN_TRIM) == 0) {
		if (bootverbose)
			device_printf(dev, "taking advantage of TRIM\n");
		sc->flags |= MMCSD_USE_TRIM;
		sc->erase_sector = 1;
	} else
		sc->erase_sector = mmc_get_erase_sector(dev);

	/*
	 * Enhanced user data area and general purpose partitions are only
	 * supported in revision 1.4 (EXT_CSD_REV == 4) and later, the RPMB
	 * partition in revision 1.5 (MMC v4.41, EXT_CSD_REV == 5) and later.
	 */
	rev = ext_csd[EXT_CSD_REV];

	/*
	 * With revision 1.5 (MMC v4.5, EXT_CSD_REV == 6) and later, take
	 * advantage of the device R/W cache if present and useage is not
	 * disabled.
	 */
	if (rev >= 6 && mmcsd_cache != 0) {
		size = le32dec(&ext_csd[EXT_CSD_CACHE_SIZE]);
		if (bootverbose)
			device_printf(dev, "cache size %juKB\n", size);
		if (size > 0) {
			MMCBUS_ACQUIRE_BUS(mmcbus, dev);
			err = mmc_switch(mmcbus, dev, sc->rca,
			    EXT_CSD_CMD_SET_NORMAL, EXT_CSD_CACHE_CTRL,
			    EXT_CSD_CACHE_CTRL_CACHE_EN, sc->cmd6_time, true);
			MMCBUS_RELEASE_BUS(mmcbus, dev);
			if (err != MMC_ERR_NONE)
				device_printf(dev, "failed to enable cache\n");
			else
				sc->flags |= MMCSD_FLUSH_CACHE;
		}
	}

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
	sector_size = mmc_get_sector_size(dev);
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
		if (size != mmc_get_media_size(dev) * sector_size) {
			sc->enh_size = size;
			sc->enh_base =
			    le32dec(&ext_csd[EXT_CSD_ENH_START_ADDR]) *
			    (sc->high_cap == 0 ? MMC_SECTOR_SIZE : 1);
		} else if (bootverbose)
			device_printf(dev,
			    "enhanced user data area spans entire device\n");
	}

	/*
	 * Add default partition.  This may be the only one or the user
	 * data area in case partitions are supported.
	 */
	ro = mmc_get_read_only(dev);
	mmcsd_add_part(sc, EXT_CSD_PART_CONFIG_ACC_DEFAULT, "mmcsd",
	    device_get_unit(dev), mmc_get_media_size(dev) * sector_size, ro);

	if (mmc_get_spec_vers(dev) < 3)
		return (0);

	/* Belatedly announce enhanced user data slice. */
	if (sc->enh_size != 0) {
		bytes = mmcsd_pretty_size(size, unit);
		printf(FLASH_SLICES_FMT ": %ju%sB enhanced user data area "
		    "slice offset 0x%jx at %s\n", device_get_nameunit(dev),
		    MMCSD_LABEL_ENH, bytes, unit, (uintmax_t)sc->enh_base,
		    device_get_nameunit(dev));
	}

	/*
	 * Determine partition switch timeout (provided in units of 10 ms)
	 * and ensure it's at least 300 ms as some eMMC chips lie.
	 */
	sc->part_time = max(ext_csd[EXT_CSD_PART_SWITCH_TO] * 10 * 1000,
	    300 * 1000);

	/* Add boot partitions, which are of a fixed multiple of 128 KB. */
	size = ext_csd[EXT_CSD_BOOT_SIZE_MULT] * MMC_BOOT_RPMB_BLOCK_SIZE;
	if (size > 0 && (mmcbr_get_caps(mmcbus) & MMC_CAP_BOOT_NOACC) == 0) {
		mmcsd_add_part(sc, EXT_CSD_PART_CONFIG_ACC_BOOT0,
		    MMCSD_FMT_BOOT, 0, size,
		    ro | ((ext_csd[EXT_CSD_BOOT_WP_STATUS] &
		    EXT_CSD_BOOT_WP_STATUS_BOOT0_MASK) != 0));
		mmcsd_add_part(sc, EXT_CSD_PART_CONFIG_ACC_BOOT1,
		    MMCSD_FMT_BOOT, 1, size,
		    ro | ((ext_csd[EXT_CSD_BOOT_WP_STATUS] &
		    EXT_CSD_BOOT_WP_STATUS_BOOT1_MASK) != 0));
	}

	/* Add RPMB partition, which also is of a fixed multiple of 128 KB. */
	size = ext_csd[EXT_CSD_RPMB_MULT] * MMC_BOOT_RPMB_BLOCK_SIZE;
	if (rev >= 5 && size > 0)
		mmcsd_add_part(sc, EXT_CSD_PART_CONFIG_ACC_RPMB,
		    MMCSD_FMT_RPMB, 0, size, ro);

	if (rev <= 3 || comp == FALSE)
		return (0);

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
			mmcsd_add_part(sc, EXT_CSD_PART_CONFIG_ACC_GP0 + i,
			    MMCSD_FMT_GP, i, size * erase_size * wp_size, ro);
		}
	}
	return (0);
}

static uintmax_t
mmcsd_pretty_size(off_t size, char *unit)
{
	uintmax_t bytes;
	int i;

	/*
	 * Display in most natural units.  There's no card < 1MB.  However,
	 * RPMB partitions occasionally are smaller than that, though.  The
	 * SD standard goes to 2 GiB due to its reliance on FAT, but the data
	 * format supports up to 4 GiB and some card makers push it up to this
	 * limit.  The SDHC standard only goes to 32 GiB due to FAT32, but the
	 * data format supports up to 2 TiB however.  2048 GB isn't too ugly,
	 * so we note it in passing here and don't add the code to print TB).
	 * Since these cards are sold in terms of MB and GB not MiB and GiB,
	 * report them like that.  We also round to the nearest unit, since
	 * many cards are a few percent short, even of the power of 10 size.
	 */
	bytes = size;
	unit[0] = unit[1] = '\0';
	for (i = 0; i <= 2 && bytes >= 1000; i++) {
		bytes = (bytes + 1000 / 2 - 1) / 1000;
		switch (i) {
		case 0:
			unit[0] = 'k';
			break;
		case 1:
			unit[0] = 'M';
			break;
		case 2:
			unit[0] = 'G';
			break;
		default:
			break;
		}
	}
	return (bytes);
}

static struct cdevsw mmcsd_rpmb_cdevsw = {
	.d_version	= D_VERSION,
	.d_name		= "mmcsdrpmb",
	.d_ioctl	= mmcsd_ioctl_rpmb
};

static void
mmcsd_add_part(struct mmcsd_softc *sc, u_int type, const char *name, u_int cnt,
    off_t media_size, bool ro)
{
	struct make_dev_args args;
	device_t dev, mmcbus;
	const char *ext;
	const uint8_t *ext_csd;
	struct mmcsd_part *part;
	struct disk *d;
	uintmax_t bytes;
	u_int gp;
	uint32_t speed;
	uint8_t extattr;
	bool enh;
	char unit[2];

	dev = sc->dev;
	mmcbus = sc->mmcbus;
	part = sc->part[type] = malloc(sizeof(*part), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	part->sc = sc;
	part->cnt = cnt;
	part->type = type;
	part->ro = ro;
	snprintf(part->name, sizeof(part->name), name, device_get_unit(dev));

	MMCSD_IOCTL_LOCK_INIT(part);

	/*
	 * For the RPMB partition, allow IOCTL access only.
	 * NB: If ever attaching RPMB partitions to disk(9), the re-tuning
	 *     implementation and especially its pausing need to be revisited,
	 *     because then re-tuning requests may be issued by the IOCTL half
	 *     of this driver while re-tuning is already paused by the disk(9)
	 *     one and vice versa.
	 */
	if (type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		make_dev_args_init(&args);
		args.mda_flags = MAKEDEV_CHECKNAME | MAKEDEV_WAITOK;
		args.mda_devsw = &mmcsd_rpmb_cdevsw;
		args.mda_uid = UID_ROOT;
		args.mda_gid = GID_OPERATOR;
		args.mda_mode = 0640;
		args.mda_si_drv1 = part;
		if (make_dev_s(&args, &sc->rpmb_dev, "%s", part->name) != 0) {
			device_printf(dev, "Failed to make RPMB device\n");
			free(part, M_DEVBUF);
			return;
		}
	} else {
		MMCSD_DISK_LOCK_INIT(part);

		d = part->disk = disk_alloc();
		d->d_close = mmcsd_close;
		d->d_strategy = mmcsd_strategy;
		d->d_ioctl = mmcsd_ioctl_disk;
		d->d_dump = mmcsd_dump;
		d->d_getattr = mmcsd_getattr;
		d->d_name = part->name;
		d->d_drv1 = part;
		d->d_sectorsize = mmc_get_sector_size(dev);
		d->d_maxsize = sc->max_data * d->d_sectorsize;
		d->d_mediasize = media_size;
		d->d_stripesize = sc->erase_sector * d->d_sectorsize;
		d->d_unit = cnt;
		d->d_flags = DISKFLAG_CANDELETE;
		if ((sc->flags & MMCSD_FLUSH_CACHE) != 0)
			d->d_flags |= DISKFLAG_CANFLUSHCACHE;
		d->d_delmaxsize = mmc_get_erase_sector(dev) * d->d_sectorsize;
		strlcpy(d->d_ident, mmc_get_card_sn_string(dev),
		    sizeof(d->d_ident));
		strlcpy(d->d_descr, mmc_get_card_id_string(dev),
		    sizeof(d->d_descr));
		d->d_rotation_rate = DISK_RR_NON_ROTATING;

		disk_create(d, DISK_VERSION);
		bioq_init(&part->bio_queue);

		part->running = 1;
		kproc_create(&mmcsd_task, part, &part->p, 0, 0,
		    "%s%d: mmc/sd card", part->name, cnt);
	}

	bytes = mmcsd_pretty_size(media_size, unit);
	if (type == EXT_CSD_PART_CONFIG_ACC_DEFAULT) {
		speed = mmcbr_get_clock(mmcbus);
		printf("%s%d: %ju%sB <%s>%s at %s %d.%01dMHz/%dbit/%d-block\n",
		    part->name, cnt, bytes, unit, mmc_get_card_id_string(dev),
		    ro ? " (read-only)" : "", device_get_nameunit(mmcbus),
		    speed / 1000000, (speed / 100000) % 10,
		    mmcsd_bus_bit_width(dev), sc->max_data);
	} else if (type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		printf("%s: %ju%sB partion %d%s at %s\n", part->name, bytes,
		    unit, type, ro ? " (read-only)" : "",
		    device_get_nameunit(dev));
	} else {
		enh = false;
		ext = NULL;
		extattr = 0;
		if (type >= EXT_CSD_PART_CONFIG_ACC_GP0 &&
		    type <= EXT_CSD_PART_CONFIG_ACC_GP3) {
			ext_csd = sc->ext_csd;
			gp = type - EXT_CSD_PART_CONFIG_ACC_GP0;
			if ((ext_csd[EXT_CSD_PART_SUPPORT] &
			    EXT_CSD_PART_SUPPORT_ENH_ATTR_EN) != 0 &&
			    (ext_csd[EXT_CSD_PART_ATTR] &
			    (EXT_CSD_PART_ATTR_ENH_GP0 << gp)) != 0)
				enh = true;
			else if ((ext_csd[EXT_CSD_PART_SUPPORT] &
			    EXT_CSD_PART_SUPPORT_EXT_ATTR_EN) != 0) {
				extattr = (ext_csd[EXT_CSD_EXT_PART_ATTR +
				    (gp / 2)] >> (4 * (gp % 2))) & 0xF;
				switch (extattr) {
					case EXT_CSD_EXT_PART_ATTR_DEFAULT:
						break;
					case EXT_CSD_EXT_PART_ATTR_SYSTEMCODE:
						ext = "system code";
						break;
					case EXT_CSD_EXT_PART_ATTR_NPERSISTENT:
						ext = "non-persistent";
						break;
					default:
						ext = "reserved";
						break;
				}
			}
		}
		if (ext == NULL)
			printf("%s%d: %ju%sB partion %d%s%s at %s\n",
			    part->name, cnt, bytes, unit, type, enh ?
			    " enhanced" : "", ro ? " (read-only)" : "",
			    device_get_nameunit(dev));
		else
			printf("%s%d: %ju%sB partion %d extended 0x%x "
			    "(%s)%s at %s\n", part->name, cnt, bytes, unit,
			    type, extattr, ext, ro ? " (read-only)" : "",
			    device_get_nameunit(dev));
	}
}

static int
mmcsd_slicer(device_t dev, const char *provider,
    struct flash_slice *slices, int *nslices)
{
	char name[MMCSD_PART_NAMELEN];
	struct mmcsd_softc *sc;
	struct mmcsd_part *part;

	*nslices = 0;
	if (slices == NULL)
		return (ENOMEM);

	sc = device_get_softc(dev);
	if (sc->enh_size == 0)
		return (ENXIO);

	part = sc->part[EXT_CSD_PART_CONFIG_ACC_DEFAULT];
	snprintf(name, sizeof(name), "%s%d", part->disk->d_name,
	    part->disk->d_unit);
	if (strcmp(name, provider) != 0)
		return (ENXIO);

	*nslices = 1;
	slices[0].base = sc->enh_base;
	slices[0].size = sc->enh_size;
	slices[0].label = MMCSD_LABEL_ENH;
	return (0);
}

static int
mmcsd_detach(device_t dev)
{
	struct mmcsd_softc *sc = device_get_softc(dev);
	struct mmcsd_part *part;
	int i;

	for (i = 0; i < MMC_PART_MAX; i++) {
		part = sc->part[i];
		if (part != NULL) {
			if (part->disk != NULL) {
				MMCSD_DISK_LOCK(part);
				part->suspend = 0;
				if (part->running > 0) {
					/* kill thread */
					part->running = 0;
					wakeup(part);
					/* wait for thread to finish. */
					while (part->running != -1)
						msleep(part, &part->disk_mtx, 0,
						    "mmcsd disk detach", 0);
				}
				MMCSD_DISK_UNLOCK(part);
			}
			MMCSD_IOCTL_LOCK(part);
			while (part->ioctl > 0)
				msleep(part, &part->ioctl_mtx, 0,
				    "mmcsd IOCTL detach", 0);
			part->ioctl = -1;
			MMCSD_IOCTL_UNLOCK(part);
		}
	}

	if (sc->rpmb_dev != NULL)
		destroy_dev(sc->rpmb_dev);

	for (i = 0; i < MMC_PART_MAX; i++) {
		part = sc->part[i];
		if (part != NULL) {
			if (part->disk != NULL) {
				/* Flush the request queue. */
				bioq_flush(&part->bio_queue, NULL, ENXIO);
				/* kill disk */
				disk_destroy(part->disk);

				MMCSD_DISK_LOCK_DESTROY(part);
			}
			MMCSD_IOCTL_LOCK_DESTROY(part);
			free(part, M_DEVBUF);
		}
	}
	if (mmcsd_flush_cache(sc) != MMC_ERR_NONE)
		device_printf(dev, "failed to flush cache\n");
	return (0);
}

static int
mmcsd_shutdown(device_t dev)
{
	struct mmcsd_softc *sc = device_get_softc(dev);

	if (mmcsd_flush_cache(sc) != MMC_ERR_NONE)
		device_printf(dev, "failed to flush cache\n");
	return (0);
}

static int
mmcsd_suspend(device_t dev)
{
	struct mmcsd_softc *sc = device_get_softc(dev);
	struct mmcsd_part *part;
	int i;

	for (i = 0; i < MMC_PART_MAX; i++) {
		part = sc->part[i];
		if (part != NULL) {
			if (part->disk != NULL) {
				MMCSD_DISK_LOCK(part);
				part->suspend = 1;
				if (part->running > 0) {
					/* kill thread */
					part->running = 0;
					wakeup(part);
					/* wait for thread to finish. */
					while (part->running != -1)
						msleep(part, &part->disk_mtx, 0,
						    "mmcsd disk suspension", 0);
				}
				MMCSD_DISK_UNLOCK(part);
			}
			MMCSD_IOCTL_LOCK(part);
			while (part->ioctl > 0)
				msleep(part, &part->ioctl_mtx, 0,
				    "mmcsd IOCTL suspension", 0);
			part->ioctl = -1;
			MMCSD_IOCTL_UNLOCK(part);
		}
	}
	if (mmcsd_flush_cache(sc) != MMC_ERR_NONE)
		device_printf(dev, "failed to flush cache\n");
	return (0);
}

static int
mmcsd_resume(device_t dev)
{
	struct mmcsd_softc *sc = device_get_softc(dev);
	struct mmcsd_part *part;
	int i;

	for (i = 0; i < MMC_PART_MAX; i++) {
		part = sc->part[i];
		if (part != NULL) {
			if (part->disk != NULL) {
				MMCSD_DISK_LOCK(part);
				part->suspend = 0;
				if (part->running <= 0) {
					part->running = 1;
					MMCSD_DISK_UNLOCK(part);
					kproc_create(&mmcsd_task, part,
					    &part->p, 0, 0, "%s%d: mmc/sd card",
					    part->name, part->cnt);
				} else
					MMCSD_DISK_UNLOCK(part);
			}
			MMCSD_IOCTL_LOCK(part);
			part->ioctl = 0;
			MMCSD_IOCTL_UNLOCK(part);
		}
	}
	return (0);
}

static int
mmcsd_close(struct disk *dp)
{
	struct mmcsd_softc *sc;

	if ((dp->d_flags & DISKFLAG_OPEN) != 0) {
		sc = ((struct mmcsd_part *)dp->d_drv1)->sc;
		if (mmcsd_flush_cache(sc) != MMC_ERR_NONE)
			device_printf(sc->dev, "failed to flush cache\n");
	}
	return (0);
}

static void
mmcsd_strategy(struct bio *bp)
{
	struct mmcsd_part *part;

	part = bp->bio_disk->d_drv1;
	MMCSD_DISK_LOCK(part);
	if (part->running > 0 || part->suspend > 0) {
		bioq_disksort(&part->bio_queue, bp);
		MMCSD_DISK_UNLOCK(part);
		wakeup(part);
	} else {
		MMCSD_DISK_UNLOCK(part);
		biofinish(bp, NULL, ENXIO);
	}
}

static int
mmcsd_ioctl_rpmb(struct cdev *dev, u_long cmd, caddr_t data,
    int fflag, struct thread *td)
{

	return (mmcsd_ioctl(dev->si_drv1, cmd, data, fflag, td));
}

static int
mmcsd_ioctl_disk(struct disk *disk, u_long cmd, void *data, int fflag,
    struct thread *td)
{

	return (mmcsd_ioctl(disk->d_drv1, cmd, data, fflag, td));
}

static int
mmcsd_ioctl(struct mmcsd_part *part, u_long cmd, void *data, int fflag,
    struct thread *td)
{
	struct mmc_ioc_cmd *mic;
	struct mmc_ioc_multi_cmd *mimc;
	int i, err;
	u_long cnt, size;

	if ((fflag & FREAD) == 0)
		return (EBADF);

	err = priv_check(td, PRIV_DRIVER);
	if (err != 0)
		return (err);

	err = 0;
	switch (cmd) {
	case MMC_IOC_CMD:
		mic = data;
		err = mmcsd_ioctl_cmd(part, mic, fflag);
		break;
	case MMC_IOC_MULTI_CMD:
		mimc = data;
		if (mimc->num_of_cmds == 0)
			break;
		if (mimc->num_of_cmds > MMC_IOC_MAX_CMDS)
			return (EINVAL);
		cnt = mimc->num_of_cmds;
		size = sizeof(*mic) * cnt;
		mic = malloc(size, M_TEMP, M_WAITOK);
		err = copyin((const void *)mimc->cmds, mic, size);
		if (err == 0) {
			for (i = 0; i < cnt; i++) {
				err = mmcsd_ioctl_cmd(part, &mic[i], fflag);
				if (err != 0)
					break;
			}
		}
		free(mic, M_TEMP);
		break;
	default:
		return (ENOIOCTL);
	}
	return (err);
}

static int
mmcsd_ioctl_cmd(struct mmcsd_part *part, struct mmc_ioc_cmd *mic, int fflag)
{
	struct mmc_command cmd;
	struct mmc_data data;
	struct mmcsd_softc *sc;
	device_t dev, mmcbus;
	void *dp;
	u_long len;
	int err, retries;
	uint32_t status;
	uint16_t rca;

	if ((fflag & FWRITE) == 0 && mic->write_flag != 0)
		return (EBADF);

	if (part->ro == TRUE && mic->write_flag != 0)
		return (EROFS);

	/*
	 * We don't need to explicitly lock against the disk(9) half of this
	 * driver as MMCBUS_ACQUIRE_BUS() will serialize us.  However, it's
	 * necessary to protect against races with detachment and suspension,
	 * especially since it's required to switch away from RPMB partitions
	 * again after an access (see mmcsd_switch_part()).
	 */
	MMCSD_IOCTL_LOCK(part);
	while (part->ioctl != 0) {
		if (part->ioctl < 0) {
			MMCSD_IOCTL_UNLOCK(part);
			return (ENXIO);
		}
		msleep(part, &part->ioctl_mtx, 0, "mmcsd IOCTL", 0);
	}
	part->ioctl = 1;
	MMCSD_IOCTL_UNLOCK(part);

	err = 0;
	dp = NULL;
	len = mic->blksz * mic->blocks;
	if (len > MMC_IOC_MAX_BYTES) {
		err = EOVERFLOW;
		goto out;
	}
	if (len != 0) {
		dp = malloc(len, M_TEMP, M_WAITOK);
		err = copyin((void *)(uintptr_t)mic->data_ptr, dp, len);
		if (err != 0)
			goto out;
	}
	memset(&cmd, 0, sizeof(cmd));
	memset(&data, 0, sizeof(data));
	cmd.opcode = mic->opcode;
	cmd.arg = mic->arg;
	cmd.flags = mic->flags;
	if (len != 0) {
		data.len = len;
		data.data = dp;
		data.flags = mic->write_flag != 0 ? MMC_DATA_WRITE :
		    MMC_DATA_READ;
		cmd.data = &data;
	}
	sc = part->sc;
	rca = sc->rca;
	if (mic->is_acmd == 0) {
		/* Enforce/patch/restrict RCA-based commands */
		switch (cmd.opcode) {
		case MMC_SET_RELATIVE_ADDR:
		case MMC_SELECT_CARD:
			err = EPERM;
			goto out;
		case MMC_STOP_TRANSMISSION:
			if ((cmd.arg & 0x1) == 0)
				break;
			/* FALLTHROUGH */
		case MMC_SLEEP_AWAKE:
		case MMC_SEND_CSD:
		case MMC_SEND_CID:
		case MMC_SEND_STATUS:
		case MMC_GO_INACTIVE_STATE:
		case MMC_FAST_IO:
		case MMC_APP_CMD:
			cmd.arg = (cmd.arg & 0x0000FFFF) | (rca << 16);
			break;
		default:
			break;
		}
		/*
		 * No partition switching in userland; it's almost impossible
		 * to recover from that, especially if things go wrong.
		 */
		if (cmd.opcode == MMC_SWITCH_FUNC && dp != NULL &&
		    (((uint8_t *)dp)[EXT_CSD_PART_CONFIG] &
		    EXT_CSD_PART_CONFIG_ACC_MASK) != part->type) {
			err = EINVAL;
			goto out;
		}
	}
	dev = sc->dev;
	mmcbus = sc->mmcbus;
	MMCBUS_ACQUIRE_BUS(mmcbus, dev);
	err = mmcsd_switch_part(mmcbus, dev, rca, part->type);
	if (err != MMC_ERR_NONE)
		goto release;
	if (part->type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		err = mmcsd_set_blockcount(sc, mic->blocks,
		    mic->write_flag & (1 << 31));
		if (err != MMC_ERR_NONE)
			goto switch_back;
	}
	if (mic->write_flag != 0)
		sc->flags |= MMCSD_DIRTY;
	if (mic->is_acmd != 0)
		(void)mmc_wait_for_app_cmd(mmcbus, dev, rca, &cmd, 0);
	else
		(void)mmc_wait_for_cmd(mmcbus, dev, &cmd, 0);
	if (part->type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		/*
		 * If the request went to the RPMB partition, try to ensure
		 * that the command actually has completed.
		 */
		retries = MMCSD_CMD_RETRIES;
		do {
			err = mmc_send_status(mmcbus, dev, rca, &status);
			if (err != MMC_ERR_NONE)
				break;
			if (R1_STATUS(status) == 0 &&
			    R1_CURRENT_STATE(status) != R1_STATE_PRG)
				break;
			DELAY(1000);
		} while (retries-- > 0);
	}
	/*
	 * If EXT_CSD was changed, our copy is outdated now.  Specifically,
	 * the upper bits of EXT_CSD_PART_CONFIG used in mmcsd_switch_part(),
	 * so retrieve EXT_CSD again.
	 */
	if (cmd.opcode == MMC_SWITCH_FUNC) {
		err = mmc_send_ext_csd(mmcbus, dev, sc->ext_csd);
		if (err != MMC_ERR_NONE)
			goto release;
	}
switch_back:
	if (part->type == EXT_CSD_PART_CONFIG_ACC_RPMB) {
		/*
		 * If the request went to the RPMB partition, always switch
		 * back to the default partition (see mmcsd_switch_part()).
		 */
		err = mmcsd_switch_part(mmcbus, dev, rca,
		    EXT_CSD_PART_CONFIG_ACC_DEFAULT);
		if (err != MMC_ERR_NONE)
			goto release;
	}
	MMCBUS_RELEASE_BUS(mmcbus, dev);
	if (cmd.error != MMC_ERR_NONE) {
		switch (cmd.error) {
		case MMC_ERR_TIMEOUT:
			err = ETIMEDOUT;
			break;
		case MMC_ERR_BADCRC:
			err = EILSEQ;
			break;
		case MMC_ERR_INVALID:
			err = EINVAL;
			break;
		case MMC_ERR_NO_MEMORY:
			err = ENOMEM;
			break;
		default:
			err = EIO;
			break;
		}
		goto out;
	}
	memcpy(mic->response, cmd.resp, 4 * sizeof(uint32_t));
	if (mic->write_flag == 0 && len != 0) {
		err = copyout(dp, (void *)(uintptr_t)mic->data_ptr, len);
		if (err != 0)
			goto out;
	}
	goto out;

release:
	MMCBUS_RELEASE_BUS(mmcbus, dev);
	err = EIO;

out:
	MMCSD_IOCTL_LOCK(part);
	part->ioctl = 0;
	MMCSD_IOCTL_UNLOCK(part);
	wakeup(part);
	if (dp != NULL)
		free(dp, M_TEMP);
	return (err);
}

static int
mmcsd_getattr(struct bio *bp)
{
	struct mmcsd_part *part;
	device_t dev;

	if (strcmp(bp->bio_attribute, "MMC::device") == 0) {
		if (bp->bio_length != sizeof(dev))
			return (EFAULT);
		part = bp->bio_disk->d_drv1;
		dev = part->sc->dev;
		bcopy(&dev, bp->bio_data, sizeof(dev));
		bp->bio_completed = bp->bio_length;
		return (0);
	}
	return (-1);
}

static int
mmcsd_set_blockcount(struct mmcsd_softc *sc, u_int count, bool reliable)
{
	struct mmc_command cmd;
	struct mmc_request req;

	memset(&req, 0, sizeof(req));
	memset(&cmd, 0, sizeof(cmd));
	cmd.mrq = &req;
	req.cmd = &cmd;
	cmd.opcode = MMC_SET_BLOCK_COUNT;
	cmd.arg = count & 0x0000FFFF;
	if (reliable)
		cmd.arg |= 1 << 31;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	MMCBUS_WAIT_FOR_REQUEST(sc->mmcbus, sc->dev, &req);
	return (cmd.error);
}

static int
mmcsd_switch_part(device_t bus, device_t dev, uint16_t rca, u_int part)
{
	struct mmcsd_softc *sc;
	int err;
	uint8_t	value;

	sc = device_get_softc(dev);

	if (sc->mode == mode_sd)
		return (MMC_ERR_NONE);

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
	if (part == EXT_CSD_PART_CONFIG_ACC_RPMB)
		MMCBUS_RETUNE_PAUSE(sc->mmcbus, sc->dev, true);

	if (sc->part_curr == part)
		return (MMC_ERR_NONE);

	value = (sc->ext_csd[EXT_CSD_PART_CONFIG] &
	    ~EXT_CSD_PART_CONFIG_ACC_MASK) | part;
	/* Jump! */
	err = mmc_switch(bus, dev, rca, EXT_CSD_CMD_SET_NORMAL,
	    EXT_CSD_PART_CONFIG, value, sc->part_time, true);
	if (err != MMC_ERR_NONE) {
		if (part == EXT_CSD_PART_CONFIG_ACC_RPMB)
			MMCBUS_RETUNE_UNPAUSE(sc->mmcbus, sc->dev);
		return (err);
	}

	sc->ext_csd[EXT_CSD_PART_CONFIG] = value;
	if (sc->part_curr == EXT_CSD_PART_CONFIG_ACC_RPMB)
		MMCBUS_RETUNE_UNPAUSE(sc->mmcbus, sc->dev);
	sc->part_curr = part;
	return (MMC_ERR_NONE);
}

static const char *
mmcsd_errmsg(int e)
{

	if (e < 0 || e > MMC_ERR_MAX)
		return "Bad error code";
	return (errmsg[e]);
}

static daddr_t
mmcsd_rw(struct mmcsd_part *part, struct bio *bp)
{
	daddr_t block, end;
	struct mmc_command cmd;
	struct mmc_command stop;
	struct mmc_request req;
	struct mmc_data data;
	struct mmcsd_softc *sc;
	device_t dev, mmcbus;
	u_int numblocks, sz;
	char *vaddr;

	sc = part->sc;
	dev = sc->dev;
	mmcbus = sc->mmcbus;

	block = bp->bio_pblkno;
	sz = part->disk->d_sectorsize;
	end = bp->bio_pblkno + (bp->bio_bcount / sz);
	while (block < end) {
		vaddr = bp->bio_data + (block - bp->bio_pblkno) * sz;
		numblocks = min(end - block, sc->max_data);
		memset(&req, 0, sizeof(req));
		memset(&cmd, 0, sizeof(cmd));
		memset(&stop, 0, sizeof(stop));
		memset(&data, 0, sizeof(data));
		cmd.mrq = &req;
		req.cmd = &cmd;
		cmd.data = &data;
		if (bp->bio_cmd == BIO_READ) {
			if (numblocks > 1)
				cmd.opcode = MMC_READ_MULTIPLE_BLOCK;
			else
				cmd.opcode = MMC_READ_SINGLE_BLOCK;
		} else {
			sc->flags |= MMCSD_DIRTY;
			if (numblocks > 1)
				cmd.opcode = MMC_WRITE_MULTIPLE_BLOCK;
			else
				cmd.opcode = MMC_WRITE_BLOCK;
		}
		cmd.arg = block;
		if (sc->high_cap == 0)
			cmd.arg <<= 9;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
		data.data = vaddr;
		data.mrq = &req;
		if (bp->bio_cmd == BIO_READ)
			data.flags = MMC_DATA_READ;
		else
			data.flags = MMC_DATA_WRITE;
		data.len = numblocks * sz;
		if (numblocks > 1) {
			data.flags |= MMC_DATA_MULTI;
			stop.opcode = MMC_STOP_TRANSMISSION;
			stop.arg = 0;
			stop.flags = MMC_RSP_R1B | MMC_CMD_AC;
			stop.mrq = &req;
			req.stop = &stop;
		}
		MMCBUS_WAIT_FOR_REQUEST(mmcbus, dev, &req);
		if (req.cmd->error != MMC_ERR_NONE) {
			if (ppsratecheck(&sc->log_time, &sc->log_count,
			    LOG_PPS))
				device_printf(dev, "Error indicated: %d %s\n",
				    req.cmd->error,
				    mmcsd_errmsg(req.cmd->error));
			break;
		}
		block += numblocks;
	}
	return (block);
}

static daddr_t
mmcsd_delete(struct mmcsd_part *part, struct bio *bp)
{
	daddr_t block, end, start, stop;
	struct mmc_command cmd;
	struct mmc_request req;
	struct mmcsd_softc *sc;
	device_t dev, mmcbus;
	u_int erase_sector, sz;
	int err;
	bool use_trim;

	sc = part->sc;
	dev = sc->dev;
	mmcbus = sc->mmcbus;

	block = bp->bio_pblkno;
	sz = part->disk->d_sectorsize;
	end = bp->bio_pblkno + (bp->bio_bcount / sz);
	use_trim = sc->flags & MMCSD_USE_TRIM;
	if (use_trim == true) {
		start = block;
		stop = end;
	} else {
		/* Coalesce with the remainder of the previous request. */
		if (block > part->eblock && block <= part->eend)
			block = part->eblock;
		if (end >= part->eblock && end < part->eend)
			end = part->eend;
		/* Safely round to the erase sector boundaries. */
		erase_sector = sc->erase_sector;
		start = block + erase_sector - 1;	 /* Round up. */
		start -= start % erase_sector;
		stop = end;				/* Round down. */
		stop -= end % erase_sector;
		/*
		 * We can't erase an area smaller than an erase sector, so
		 * store it for later.
		 */
		if (start >= stop) {
			part->eblock = block;
			part->eend = end;
			return (end);
		}
	}

	if ((sc->flags & MMCSD_INAND_CMD38) != 0) {
		err = mmc_switch(mmcbus, dev, sc->rca, EXT_CSD_CMD_SET_NORMAL,
		    EXT_CSD_INAND_CMD38, use_trim == true ?
		    EXT_CSD_INAND_CMD38_TRIM : EXT_CSD_INAND_CMD38_ERASE,
		    sc->cmd6_time, true);
		if (err != MMC_ERR_NONE) {
			device_printf(dev,
			    "Setting iNAND erase command failed %s\n",
			    mmcsd_errmsg(err));
			return (block);
		}
	}

	/*
	 * Pause re-tuning so it won't interfere with the order of erase
	 * commands.  Note that these latter don't use the data lines, so
	 * re-tuning shouldn't actually become necessary during erase.
	 */
	MMCBUS_RETUNE_PAUSE(mmcbus, dev, false);
	/* Set erase start position. */
	memset(&req, 0, sizeof(req));
	memset(&cmd, 0, sizeof(cmd));
	cmd.mrq = &req;
	req.cmd = &cmd;
	if (sc->mode == mode_sd)
		cmd.opcode = SD_ERASE_WR_BLK_START;
	else
		cmd.opcode = MMC_ERASE_GROUP_START;
	cmd.arg = start;
	if (sc->high_cap == 0)
		cmd.arg <<= 9;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	MMCBUS_WAIT_FOR_REQUEST(mmcbus, dev, &req);
	if (req.cmd->error != MMC_ERR_NONE) {
		device_printf(dev, "Setting erase start position failed %s\n",
		    mmcsd_errmsg(req.cmd->error));
		block = bp->bio_pblkno;
		goto unpause;
	}
	/* Set erase stop position. */
	memset(&req, 0, sizeof(req));
	memset(&cmd, 0, sizeof(cmd));
	req.cmd = &cmd;
	if (sc->mode == mode_sd)
		cmd.opcode = SD_ERASE_WR_BLK_END;
	else
		cmd.opcode = MMC_ERASE_GROUP_END;
	cmd.arg = stop;
	if (sc->high_cap == 0)
		cmd.arg <<= 9;
	cmd.arg--;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	MMCBUS_WAIT_FOR_REQUEST(mmcbus, dev, &req);
	if (req.cmd->error != MMC_ERR_NONE) {
		device_printf(dev, "Setting erase stop position failed %s\n",
		    mmcsd_errmsg(req.cmd->error));
		block = bp->bio_pblkno;
		goto unpause;
	}
	/* Erase range. */
	memset(&req, 0, sizeof(req));
	memset(&cmd, 0, sizeof(cmd));
	req.cmd = &cmd;
	cmd.opcode = MMC_ERASE;
	cmd.arg = use_trim == true ? MMC_ERASE_TRIM : MMC_ERASE_ERASE;
	cmd.flags = MMC_RSP_R1B | MMC_CMD_AC;
	MMCBUS_WAIT_FOR_REQUEST(mmcbus, dev, &req);
	if (req.cmd->error != MMC_ERR_NONE) {
		device_printf(dev, "Issuing erase command failed %s\n",
		    mmcsd_errmsg(req.cmd->error));
		block = bp->bio_pblkno;
		goto unpause;
	}
	if (use_trim == false) {
		/* Store one of the remaining parts for the next call. */
		if (bp->bio_pblkno >= part->eblock || block == start) {
			part->eblock = stop;	/* Predict next forward. */
			part->eend = end;
		} else {
			part->eblock = block;	/* Predict next backward. */
			part->eend = start;
		}
	}
	block = end;
unpause:
	MMCBUS_RETUNE_UNPAUSE(mmcbus, dev);
	return (block);
}

static int
mmcsd_dump(void *arg, void *virtual, vm_offset_t physical, off_t offset,
    size_t length)
{
	struct bio bp;
	daddr_t block, end;
	struct disk *disk;
	struct mmcsd_softc *sc;
	struct mmcsd_part *part;
	device_t dev, mmcbus;
	int err;

	disk = arg;
	part = disk->d_drv1;
	sc = part->sc;

	/* length zero is special and really means flush buffers to media */
	if (length == 0) {
		err = mmcsd_flush_cache(sc);
		if (err != MMC_ERR_NONE)
			return (EIO);
		return (0);
	}

	dev = sc->dev;
	mmcbus = sc->mmcbus;

	g_reset_bio(&bp);
	bp.bio_disk = disk;
	bp.bio_pblkno = offset / disk->d_sectorsize;
	bp.bio_bcount = length;
	bp.bio_data = virtual;
	bp.bio_cmd = BIO_WRITE;
	end = bp.bio_pblkno + bp.bio_bcount / disk->d_sectorsize;
	MMCBUS_ACQUIRE_BUS(mmcbus, dev);
	err = mmcsd_switch_part(mmcbus, dev, sc->rca, part->type);
	if (err != MMC_ERR_NONE) {
		if (ppsratecheck(&sc->log_time, &sc->log_count, LOG_PPS))
			device_printf(dev, "Partition switch error\n");
		MMCBUS_RELEASE_BUS(mmcbus, dev);
		return (EIO);
	}
	block = mmcsd_rw(part, &bp);
	MMCBUS_RELEASE_BUS(mmcbus, dev);
	return ((end < block) ? EIO : 0);
}

static void
mmcsd_task(void *arg)
{
	daddr_t block, end;
	struct mmcsd_part *part;
	struct mmcsd_softc *sc;
	struct bio *bp;
	device_t dev, mmcbus;
	int err, sz;

	part = arg;
	sc = part->sc;
	dev = sc->dev;
	mmcbus = sc->mmcbus;

	while (1) {
		MMCSD_DISK_LOCK(part);
		do {
			if (part->running == 0)
				goto out;
			bp = bioq_takefirst(&part->bio_queue);
			if (bp == NULL)
				msleep(part, &part->disk_mtx, PRIBIO,
				    "mmcsd disk jobqueue", 0);
		} while (bp == NULL);
		MMCSD_DISK_UNLOCK(part);
		if (__predict_false(bp->bio_cmd == BIO_FLUSH)) {
			if (mmcsd_flush_cache(sc) != MMC_ERR_NONE) {
				bp->bio_error = EIO;
				bp->bio_flags |= BIO_ERROR;
			}
			biodone(bp);
			continue;
		}
		if (bp->bio_cmd != BIO_READ && part->ro) {
			bp->bio_error = EROFS;
			bp->bio_resid = bp->bio_bcount;
			bp->bio_flags |= BIO_ERROR;
			biodone(bp);
			continue;
		}
		MMCBUS_ACQUIRE_BUS(mmcbus, dev);
		sz = part->disk->d_sectorsize;
		block = bp->bio_pblkno;
		end = bp->bio_pblkno + (bp->bio_bcount / sz);
		err = mmcsd_switch_part(mmcbus, dev, sc->rca, part->type);
		if (err != MMC_ERR_NONE) {
			if (ppsratecheck(&sc->log_time, &sc->log_count,
			    LOG_PPS))
				device_printf(dev, "Partition switch error\n");
			goto release;
		}
		if (bp->bio_cmd == BIO_READ || bp->bio_cmd == BIO_WRITE) {
			/* Access to the remaining erase block obsoletes it. */
			if (block < part->eend && end > part->eblock)
				part->eblock = part->eend = 0;
			block = mmcsd_rw(part, bp);
		} else if (bp->bio_cmd == BIO_DELETE) {
			block = mmcsd_delete(part, bp);
		}
release:
		MMCBUS_RELEASE_BUS(mmcbus, dev);
		if (block < end) {
			bp->bio_error = EIO;
			bp->bio_resid = (end - block) * sz;
			bp->bio_flags |= BIO_ERROR;
		} else {
			bp->bio_resid = 0;
		}
		biodone(bp);
	}
out:
	/* tell parent we're done */
	part->running = -1;
	MMCSD_DISK_UNLOCK(part);
	wakeup(part);

	kproc_exit(0);
}

static int
mmcsd_bus_bit_width(device_t dev)
{

	if (mmc_get_bus_width(dev) == bus_width_1)
		return (1);
	if (mmc_get_bus_width(dev) == bus_width_4)
		return (4);
	return (8);
}

static int
mmcsd_flush_cache(struct mmcsd_softc *sc)
{
	device_t dev, mmcbus;
	int err;

	if ((sc->flags & MMCSD_FLUSH_CACHE) == 0)
		return (MMC_ERR_NONE);

	dev = sc->dev;
	mmcbus = sc->mmcbus;
	MMCBUS_ACQUIRE_BUS(mmcbus, dev);
	if ((sc->flags & MMCSD_DIRTY) == 0) {
		MMCBUS_RELEASE_BUS(mmcbus, dev);
		return (MMC_ERR_NONE);
	}
	err = mmc_switch(mmcbus, dev, sc->rca, EXT_CSD_CMD_SET_NORMAL,
	    EXT_CSD_FLUSH_CACHE, EXT_CSD_FLUSH_CACHE_FLUSH, 60 * 1000, true);
	if (err == MMC_ERR_NONE)
		sc->flags &= ~MMCSD_DIRTY;
	MMCBUS_RELEASE_BUS(mmcbus, dev);
	return (err);
}

static device_method_t mmcsd_methods[] = {
	DEVMETHOD(device_probe, mmcsd_probe),
	DEVMETHOD(device_attach, mmcsd_attach),
	DEVMETHOD(device_detach, mmcsd_detach),
	DEVMETHOD(device_shutdown, mmcsd_shutdown),
	DEVMETHOD(device_suspend, mmcsd_suspend),
	DEVMETHOD(device_resume, mmcsd_resume),
	DEVMETHOD_END
};

static driver_t mmcsd_driver = {
	"mmcsd",
	mmcsd_methods,
	sizeof(struct mmcsd_softc),
};
static devclass_t mmcsd_devclass;

static int
mmcsd_handler(module_t mod __unused, int what, void *arg __unused)
{

	switch (what) {
	case MOD_LOAD:
		flash_register_slicer(mmcsd_slicer, FLASH_SLICES_TYPE_MMC,
		    TRUE);
		return (0);
	case MOD_UNLOAD:
		flash_register_slicer(NULL, FLASH_SLICES_TYPE_MMC, TRUE);
		return (0);
	}
	return (0);
}

DRIVER_MODULE(mmcsd, mmc, mmcsd_driver, mmcsd_devclass, mmcsd_handler, NULL);
MODULE_DEPEND(mmcsd, g_flashmap, 0, 0, 0);
MMC_DEPEND(mmcsd);
