/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2012 Andrey V. Elsukov <ae@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/*
 * BIOS disk device handling.
 *
 * Ideas and algorithms from:
 *
 * - NetBSD libi386/biosdisk.c
 * - FreeBSD biosboot/disk.c
 *
 */

#include <sys/disk.h>
#include <sys/limits.h>
#include <sys/queue.h>
#include <stand.h>
#include <machine/bootinfo.h>
#include <stdarg.h>
#include <stdbool.h>

#include <bootstrap.h>
#include <btxv86.h>
#include <edd.h>
#include "disk.h"
#include "libi386.h"

#define	BIOS_NUMDRIVES		0x475
#define	BIOSDISK_SECSIZE	512
#define	BUFSIZE			(1 * BIOSDISK_SECSIZE)

#define	DT_ATAPI	0x10	/* disk type for ATAPI floppies */
#define	WDMAJOR		0	/* major numbers for devices we frontend for */
#define	WFDMAJOR	1
#define	FDMAJOR		2
#define	DAMAJOR		4
#define	ACDMAJOR	117
#define	CDMAJOR		15

#ifdef DISK_DEBUG
#define	DEBUG(fmt, args...)	printf("%s: " fmt "\n", __func__, ## args)
#else
#define	DEBUG(fmt, args...)
#endif

struct specification_packet {
	uint8_t		sp_size;
	uint8_t		sp_bootmedia;
	uint8_t		sp_drive;
	uint8_t		sp_controller;
	uint32_t	sp_lba;
	uint16_t	sp_devicespec;
	uint16_t	sp_buffersegment;
	uint16_t	sp_loadsegment;
	uint16_t	sp_sectorcount;
	uint16_t	sp_cylsec;
	uint8_t		sp_head;
};

/*
 * List of BIOS devices, translation from disk unit number to
 * BIOS unit number.
 */
typedef struct bdinfo
{
	STAILQ_ENTRY(bdinfo)	bd_link;	/* link in device list */
	int		bd_unit;	/* BIOS unit number */
	int		bd_cyl;		/* BIOS geometry */
	int		bd_hds;
	int		bd_sec;
	int		bd_flags;
#define	BD_MODEINT13	0x0000
#define	BD_MODEEDD1	0x0001
#define	BD_MODEEDD3	0x0002
#define	BD_MODEEDD	(BD_MODEEDD1 | BD_MODEEDD3)
#define	BD_MODEMASK	0x0003
#define	BD_FLOPPY	0x0004
#define	BD_CDROM	0x0008
#define	BD_NO_MEDIA	0x0010
	int		bd_type;	/* BIOS 'drive type' (floppy only) */
	uint16_t	bd_sectorsize;	/* Sector size */
	uint64_t	bd_sectors;	/* Disk size */
	int		bd_open;	/* reference counter */
	void		*bd_bcache;	/* buffer cache data */
} bdinfo_t;

#define	BD_RD		0
#define	BD_WR		1

typedef STAILQ_HEAD(bdinfo_list, bdinfo) bdinfo_list_t;
static bdinfo_list_t fdinfo = STAILQ_HEAD_INITIALIZER(fdinfo);
static bdinfo_list_t cdinfo = STAILQ_HEAD_INITIALIZER(cdinfo);
static bdinfo_list_t hdinfo = STAILQ_HEAD_INITIALIZER(hdinfo);

static void bd_io_workaround(bdinfo_t *);
static int bd_io(struct disk_devdesc *, bdinfo_t *, daddr_t, int, caddr_t, int);
static bool bd_int13probe(bdinfo_t *);

static int bd_init(void);
static int cd_init(void);
static int fd_init(void);
static int bd_strategy(void *devdata, int flag, daddr_t dblk, size_t size,
    char *buf, size_t *rsize);
static int bd_realstrategy(void *devdata, int flag, daddr_t dblk, size_t size,
    char *buf, size_t *rsize);
static int bd_open(struct open_file *f, ...);
static int bd_close(struct open_file *f);
static int bd_ioctl(struct open_file *f, u_long cmd, void *data);
static int bd_print(int verbose);
static int cd_print(int verbose);
static int fd_print(int verbose);
static void bd_reset_disk(int);
static int bd_get_diskinfo_std(struct bdinfo *);

struct devsw biosfd = {
	.dv_name = "fd",
	.dv_type = DEVT_FD,
	.dv_init = fd_init,
	.dv_strategy = bd_strategy,
	.dv_open = bd_open,
	.dv_close = bd_close,
	.dv_ioctl = bd_ioctl,
	.dv_print = fd_print,
	.dv_cleanup = NULL
};

struct devsw bioscd = {
	.dv_name = "cd",
	.dv_type = DEVT_CD,
	.dv_init = cd_init,
	.dv_strategy = bd_strategy,
	.dv_open = bd_open,
	.dv_close = bd_close,
	.dv_ioctl = bd_ioctl,
	.dv_print = cd_print,
	.dv_cleanup = NULL
};

struct devsw bioshd = {
	.dv_name = "disk",
	.dv_type = DEVT_DISK,
	.dv_init = bd_init,
	.dv_strategy = bd_strategy,
	.dv_open = bd_open,
	.dv_close = bd_close,
	.dv_ioctl = bd_ioctl,
	.dv_print = bd_print,
	.dv_cleanup = NULL
};

static bdinfo_list_t *
bd_get_bdinfo_list(struct devsw *dev)
{
	if (dev->dv_type == DEVT_DISK)
		return (&hdinfo);
	if (dev->dv_type == DEVT_CD)
		return (&cdinfo);
	if (dev->dv_type == DEVT_FD)
		return (&fdinfo);
	return (NULL);
}

/* XXX this gets called way way too often, investigate */
static bdinfo_t *
bd_get_bdinfo(struct devdesc *dev)
{
	bdinfo_list_t *bdi;
	bdinfo_t *bd = NULL;
	int unit;

	bdi = bd_get_bdinfo_list(dev->d_dev);
	if (bdi == NULL)
		return (bd);

	unit = 0;
	STAILQ_FOREACH(bd, bdi, bd_link) {
		if (unit == dev->d_unit)
			return (bd);
		unit++;
	}
	return (bd);
}

/*
 * Translate between BIOS device numbers and our private unit numbers.
 */
int
bd_bios2unit(int biosdev)
{
	bdinfo_list_t *bdi[] = { &fdinfo, &cdinfo, &hdinfo, NULL };
	bdinfo_t *bd;
	int i, unit;

	DEBUG("looking for bios device 0x%x", biosdev);
	for (i = 0; bdi[i] != NULL; i++) {
		unit = 0;
		STAILQ_FOREACH(bd, bdi[i], bd_link) {
			if (bd->bd_unit == biosdev) {
				DEBUG("bd unit %d is BIOS device 0x%x", unit,
				    bd->bd_unit);
				return (unit);
			}
			unit++;
		}
	}
	return (-1);
}

int
bd_unit2bios(struct i386_devdesc *dev)
{
	bdinfo_list_t *bdi;
	bdinfo_t *bd;
	int unit;

	bdi = bd_get_bdinfo_list(dev->dd.d_dev);
	if (bdi == NULL)
		return (-1);

	unit = 0;
	STAILQ_FOREACH(bd, bdi, bd_link) {
		if (unit == dev->dd.d_unit)
			return (bd->bd_unit);
		unit++;
	}
	return (-1);
}

/*
 * Use INT13 AH=15 - Read Drive Type.
 */
static int
fd_count(void)
{
	int drive;

	for (drive = 0; drive < MAXBDDEV; drive++) {
		bd_reset_disk(drive);

		v86.ctl = V86_FLAGS;
		v86.addr = 0x13;
		v86.eax = 0x1500;
		v86.edx = drive;
		v86int();

		if (V86_CY(v86.efl))
			break;

		if ((v86.eax & 0x300) == 0)
			break;
	}

	return (drive);
}

/*
 * Quiz the BIOS for disk devices, save a little info about them.
 */
static int
fd_init(void)
{
	int unit, numfd;
	bdinfo_t *bd;

	numfd = fd_count();
	for (unit = 0; unit < numfd; unit++) {
		if ((bd = calloc(1, sizeof(*bd))) == NULL)
			break;

		bd->bd_sectorsize = BIOSDISK_SECSIZE;
		bd->bd_flags = BD_FLOPPY;
		bd->bd_unit = unit;

		/* Use std diskinfo for floppy drive */
		if (bd_get_diskinfo_std(bd) != 0) {
			free(bd);
			break;
		}
		if (bd->bd_sectors == 0)
			bd->bd_flags |= BD_NO_MEDIA;

		printf("BIOS drive %c: is %s%d\n", ('A' + unit),
		    biosfd.dv_name, unit);

		STAILQ_INSERT_TAIL(&fdinfo, bd, bd_link);
	}

	bcache_add_dev(unit);
	return (0);
}

static int
bd_init(void)
{
	int base, unit;
	bdinfo_t *bd;

	base = 0x80;
	for (unit = 0; unit < *(unsigned char *)PTOV(BIOS_NUMDRIVES); unit++) {
		/*
		 * Check the BIOS equipment list for number of fixed disks.
		 */
		if ((bd = calloc(1, sizeof(*bd))) == NULL)
			break;
		bd->bd_unit = base + unit;
		if (!bd_int13probe(bd)) {
			free(bd);
			break;
		}

		printf("BIOS drive %c: is %s%d\n", ('C' + unit),
		    bioshd.dv_name, unit);

		STAILQ_INSERT_TAIL(&hdinfo, bd, bd_link);
	}
	bcache_add_dev(unit);
	return (0);
}

/*
 * We can't quiz, we have to be told what device to use, so this function
 * doesn't do anything.  Instead, the loader calls bc_add() with the BIOS
 * device number to add.
 */
static int
cd_init(void)
{

	return (0);
}

int
bc_add(int biosdev)
{
	bdinfo_t *bd;
	struct specification_packet bc_sp;
	int nbcinfo = 0;

	if (!STAILQ_EMPTY(&cdinfo))
                return (-1);

        v86.ctl = V86_FLAGS;
        v86.addr = 0x13;
        v86.eax = 0x4b01;
        v86.edx = biosdev;
        v86.ds = VTOPSEG(&bc_sp);
        v86.esi = VTOPOFF(&bc_sp);
        v86int();
        if ((v86.eax & 0xff00) != 0)
                return (-1);

	if ((bd = calloc(1, sizeof(*bd))) == NULL)
		return (-1);

	bd->bd_flags = BD_CDROM;
        bd->bd_unit = biosdev;

	/*
	 * Ignore result from bd_int13probe(), we will use local
	 * workaround below.
	 */
	(void)bd_int13probe(bd);

	if (bd->bd_cyl == 0) {
		bd->bd_cyl = ((bc_sp.sp_cylsec & 0xc0) << 2) +
		    ((bc_sp.sp_cylsec & 0xff00) >> 8) + 1;
	}
	if (bd->bd_hds == 0)
		bd->bd_hds = bc_sp.sp_head + 1;
	if (bd->bd_sec == 0)
		bd->bd_sec = bc_sp.sp_cylsec & 0x3f;
	if (bd->bd_sectors == 0)
		bd->bd_sectors = (uint64_t)bd->bd_cyl * bd->bd_hds * bd->bd_sec;

	/* Still no size? use 7.961GB */
	if (bd->bd_sectors == 0)
		bd->bd_sectors = 4173824;

	STAILQ_INSERT_TAIL(&cdinfo, bd, bd_link);
        printf("BIOS CD is cd%d\n", nbcinfo);
        nbcinfo++;
        bcache_add_dev(nbcinfo);        /* register cd device in bcache */
        return(0);
}

/*
 * Return EDD version or 0 if EDD is not supported on this drive.
 */
static int
bd_check_extensions(int unit)
{
	/* do not use ext calls for floppy devices */
	if (unit < 0x80)
		return (0);

	/* Determine if we can use EDD with this device. */
	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0x4100;
	v86.edx = unit;
	v86.ebx = 0x55aa;
	v86int();

	if (V86_CY(v86.efl) ||			/* carry set */
	    (v86.ebx & 0xffff) != 0xaa55)	/* signature */
		return (0);

	/* extended disk access functions (AH=42h-44h,47h,48h) supported */
	if ((v86.ecx & EDD_INTERFACE_FIXED_DISK) == 0)
		return (0);

	return ((v86.eax >> 8) & 0xff);
}

static void
bd_reset_disk(int unit)
{
	/* reset disk */
	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0;
	v86.edx = unit;
	v86int();
}

/*
 * Read CHS info. Return 0 on success, error otherwise.
 */
static int
bd_get_diskinfo_std(struct bdinfo *bd)
{
	bzero(&v86, sizeof(v86));
	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0x800;
	v86.edx = bd->bd_unit;
	v86int();

	if (V86_CY(v86.efl) && ((v86.eax & 0xff00) != 0))
		return ((v86.eax & 0xff00) >> 8);

	/* return custom error on absurd sector number */
	if ((v86.ecx & 0x3f) == 0)
		return (0x60);

	bd->bd_cyl = ((v86.ecx & 0xc0) << 2) + ((v86.ecx & 0xff00) >> 8) + 1;
	/* Convert max head # -> # of heads */
	bd->bd_hds = ((v86.edx & 0xff00) >> 8) + 1;
	bd->bd_sec = v86.ecx & 0x3f;
	bd->bd_type = v86.ebx;
	bd->bd_sectors = (uint64_t)bd->bd_cyl * bd->bd_hds * bd->bd_sec;

	return (0);
}

/*
 * Read EDD info. Return 0 on success, error otherwise.
 */
static int
bd_get_diskinfo_ext(struct bdinfo *bd)
{
	struct edd_params params;
	uint64_t total;

	/* Get disk params */
	bzero(&params, sizeof(params));
	params.len = sizeof(params);
	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0x4800;
	v86.edx = bd->bd_unit;
	v86.ds = VTOPSEG(&params);
	v86.esi = VTOPOFF(&params);
	v86int();

	if (V86_CY(v86.efl) && ((v86.eax & 0xff00) != 0))
		return ((v86.eax & 0xff00) >> 8);

	/*
	 * Sector size must be a multiple of 512 bytes.
	 * An alternate test would be to check power of 2,
	 * powerof2(params.sector_size).
	 * 16K is largest read buffer we can use at this time.
	 */
	if (params.sector_size >= 512 &&
	    params.sector_size <= 16384 &&
	    (params.sector_size % BIOSDISK_SECSIZE) == 0)
		bd->bd_sectorsize = params.sector_size;

	bd->bd_cyl = params.cylinders;
	bd->bd_hds = params.heads;
	bd->bd_sec = params.sectors_per_track;

	if (params.sectors != 0) {
		total = params.sectors;
	} else {
		total = (uint64_t)params.cylinders *
		    params.heads * params.sectors_per_track;
	}
	bd->bd_sectors = total;

	return (0);
}

/*
 * Try to detect a device supported by the legacy int13 BIOS
 */
static bool
bd_int13probe(bdinfo_t *bd)
{
	int edd, ret;

	bd->bd_flags &= ~BD_NO_MEDIA;

	edd = bd_check_extensions(bd->bd_unit);
	if (edd == 0)
		bd->bd_flags |= BD_MODEINT13;
	else if (edd < 0x30)
		bd->bd_flags |= BD_MODEEDD1;
	else
		bd->bd_flags |= BD_MODEEDD3;

	/* Default sector size */
	bd->bd_sectorsize = BIOSDISK_SECSIZE;

	/*
	 * Test if the floppy device is present, so we can avoid receiving
	 * bogus information from bd_get_diskinfo_std().
	 */
	if (bd->bd_unit < 0x80) {
		/* reset disk */
		bd_reset_disk(bd->bd_unit);

		/* Get disk type */
		v86.ctl = V86_FLAGS;
		v86.addr = 0x13;
		v86.eax = 0x1500;
		v86.edx = bd->bd_unit;
		v86int();
		if (V86_CY(v86.efl) || (v86.eax & 0x300) == 0)
			return (false);
	}

	ret = 1;
	if (edd != 0)
		ret = bd_get_diskinfo_ext(bd);
	if (ret != 0 || bd->bd_sectors == 0)
		ret = bd_get_diskinfo_std(bd);

	if (ret != 0 && bd->bd_unit < 0x80) {
		/* Set defaults for 1.44 floppy */
		bd->bd_cyl = 80;
		bd->bd_hds = 2;
		bd->bd_sec = 18;
		bd->bd_sectors = 2880;
		/* Since we are there, there most likely is no media */
		bd->bd_flags |= BD_NO_MEDIA;
		ret = 0;
	}

	if (ret != 0) {
		/* CD is special case, bc_add() has its own fallback. */
		if ((bd->bd_flags & BD_CDROM) != 0)
			return (true);

		if (bd->bd_sectors != 0 && edd != 0) {
			bd->bd_sec = 63;
			bd->bd_hds = 255;
			bd->bd_cyl =
			    (bd->bd_sectors + bd->bd_sec * bd->bd_hds - 1) /
			    bd->bd_sec * bd->bd_hds;
		} else {
			const char *dv_name;

			if ((bd->bd_flags & BD_FLOPPY) != 0)
				dv_name = biosfd.dv_name;
			else if ((bd->bd_flags & BD_CDROM) != 0)
				dv_name = bioscd.dv_name;
			else
				dv_name = bioshd.dv_name;

			printf("Can not get information about %s unit %#x\n",
			    dv_name, bd->bd_unit);
			return (false);
		}
	}

	if (bd->bd_sec == 0)
		bd->bd_sec = 63;
	if (bd->bd_hds == 0)
		bd->bd_hds = 255;

	if (bd->bd_sectors == 0)
		bd->bd_sectors = (uint64_t)bd->bd_cyl * bd->bd_hds * bd->bd_sec;

	DEBUG("unit 0x%x geometry %d/%d/%d\n", bd->bd_unit, bd->bd_cyl,
	    bd->bd_hds, bd->bd_sec);

	return (true);
}

static int
bd_count(bdinfo_list_t *bdi)
{
	bdinfo_t *bd;
	int i;

	i = 0;
	STAILQ_FOREACH(bd, bdi, bd_link)
		i++;
	return (i);
}

/*
 * Print information about disks
 */
static int
bd_print_common(struct devsw *dev, bdinfo_list_t *bdi, int verbose)
{
	char line[80];
	struct disk_devdesc devd;
	bdinfo_t *bd;
	int i, ret = 0;
	char drive;

	if (STAILQ_EMPTY(bdi))
		return (0);

	printf("%s devices:", dev->dv_name);
	if ((ret = pager_output("\n")) != 0)
		return (ret);

	i = -1;
	STAILQ_FOREACH(bd, bdi, bd_link) {
		i++;

		switch (dev->dv_type) {
		case DEVT_FD:
			drive = 'A';
			break;
		case DEVT_CD:
			drive = 'C' + bd_count(&hdinfo);
			break;
		default:
			drive = 'C';
			break;
		}

		snprintf(line, sizeof(line),
		    "    %s%d:   BIOS drive %c (%s%ju X %u):\n",
		    dev->dv_name, i, drive + i,
		    (bd->bd_flags & BD_NO_MEDIA) == BD_NO_MEDIA ?
		    "no media, " : "",
		    (uintmax_t)bd->bd_sectors,
		    bd->bd_sectorsize);
		if ((ret = pager_output(line)) != 0)
			break;

		if ((bd->bd_flags & BD_NO_MEDIA) == BD_NO_MEDIA)
			continue;

		if (dev->dv_type != DEVT_DISK)
			continue;

		devd.dd.d_dev = dev;
		devd.dd.d_unit = i;
		devd.d_slice = D_SLICENONE;
		devd.d_partition = D_PARTNONE;
		if (disk_open(&devd,
		    bd->bd_sectorsize * bd->bd_sectors,
		    bd->bd_sectorsize) == 0) {
			snprintf(line, sizeof(line), "    %s%d",
			    dev->dv_name, i);
			ret = disk_print(&devd, line, verbose);
			disk_close(&devd);
			if (ret != 0)
				break;
		}
	}
	return (ret);
}

static int
fd_print(int verbose)
{
	return (bd_print_common(&biosfd, &fdinfo, verbose));
}

static int
bd_print(int verbose)
{
	return (bd_print_common(&bioshd, &hdinfo, verbose));
}

static int
cd_print(int verbose)
{
	return (bd_print_common(&bioscd, &cdinfo, verbose));
}

/*
 * Read disk size from partition.
 * This is needed to work around buggy BIOS systems returning
 * wrong (truncated) disk media size.
 * During bd_probe() we tested if the multiplication of bd_sectors
 * would overflow so it should be safe to perform here.
 */
static uint64_t
bd_disk_get_sectors(struct disk_devdesc *dev)
{
	bdinfo_t *bd;
	struct disk_devdesc disk;
	uint64_t size;

	bd = bd_get_bdinfo(&dev->dd);
	if (bd == NULL)
		return (0);

	disk.dd.d_dev = dev->dd.d_dev;
	disk.dd.d_unit = dev->dd.d_unit;
	disk.d_slice = D_SLICENONE;
	disk.d_partition = D_PARTNONE;
	disk.d_offset = 0;

	size = bd->bd_sectors * bd->bd_sectorsize;
	if (disk_open(&disk, size, bd->bd_sectorsize) == 0) {
		(void) disk_ioctl(&disk, DIOCGMEDIASIZE, &size);
		disk_close(&disk);
	}
	return (size / bd->bd_sectorsize);
}

/*
 * Attempt to open the disk described by (dev) for use by (f).
 *
 * Note that the philosophy here is "give them exactly what
 * they ask for".  This is necessary because being too "smart"
 * about what the user might want leads to complications.
 * (eg. given no slice or partition value, with a disk that is
 *  sliced - are they after the first BSD slice, or the DOS
 *  slice before it?)
 */
static int
bd_open(struct open_file *f, ...)
{
	bdinfo_t *bd;
	struct disk_devdesc *dev;
	va_list ap;
	int rc;

	va_start(ap, f);
	dev = va_arg(ap, struct disk_devdesc *);
	va_end(ap);

	bd = bd_get_bdinfo(&dev->dd);
	if (bd == NULL)
		return (EIO);

	if ((bd->bd_flags & BD_NO_MEDIA) == BD_NO_MEDIA) {
		if (!bd_int13probe(bd))
			return (EIO);
		if ((bd->bd_flags & BD_NO_MEDIA) == BD_NO_MEDIA)
			return (EIO);
	}
	if (bd->bd_bcache == NULL)
	    bd->bd_bcache = bcache_allocate();

	if (bd->bd_open == 0)
		bd->bd_sectors = bd_disk_get_sectors(dev);
	bd->bd_open++;

	rc = 0;
	if (dev->dd.d_dev->dv_type == DEVT_DISK) {
		rc = disk_open(dev, bd->bd_sectors * bd->bd_sectorsize,
		    bd->bd_sectorsize);
		if (rc != 0) {
			bd->bd_open--;
			if (bd->bd_open == 0) {
				bcache_free(bd->bd_bcache);
				bd->bd_bcache = NULL;
			}
		}
	}
	return (rc);
}

static int
bd_close(struct open_file *f)
{
	struct disk_devdesc *dev;
	bdinfo_t *bd;
	int rc = 0;

	dev = (struct disk_devdesc *)f->f_devdata;
	bd = bd_get_bdinfo(&dev->dd);
	if (bd == NULL)
		return (EIO);

	bd->bd_open--;
	if (bd->bd_open == 0) {
	    bcache_free(bd->bd_bcache);
	    bd->bd_bcache = NULL;
	}
	if (dev->dd.d_dev->dv_type == DEVT_DISK)
		rc = disk_close(dev);
	return (rc);
}

static int
bd_ioctl(struct open_file *f, u_long cmd, void *data)
{
	bdinfo_t *bd;
	struct disk_devdesc *dev;
	int rc;

	dev = (struct disk_devdesc *)f->f_devdata;
	bd = bd_get_bdinfo(&dev->dd);
	if (bd == NULL)
		return (EIO);

	if (dev->dd.d_dev->dv_type == DEVT_DISK) {
		rc = disk_ioctl(dev, cmd, data);
		if (rc != ENOTTY)
			return (rc);
	}

	switch (cmd) {
	case DIOCGSECTORSIZE:
		*(uint32_t *)data = bd->bd_sectorsize;
		break;
	case DIOCGMEDIASIZE:
		*(uint64_t *)data = bd->bd_sectors * bd->bd_sectorsize;
		break;
	default:
		return (ENOTTY);
	}
	return (0);
}

static int
bd_strategy(void *devdata, int rw, daddr_t dblk, size_t size,
    char *buf, size_t *rsize)
{
	bdinfo_t *bd;
	struct bcache_devdata bcd;
	struct disk_devdesc *dev;
	daddr_t offset;

	dev = (struct disk_devdesc *)devdata;
	bd = bd_get_bdinfo(&dev->dd);
	if (bd == NULL)
		return (EINVAL);

	bcd.dv_strategy = bd_realstrategy;
	bcd.dv_devdata = devdata;
	bcd.dv_cache = bd->bd_bcache;

	offset = 0;
	if (dev->dd.d_dev->dv_type == DEVT_DISK) {

		offset = dev->d_offset * bd->bd_sectorsize;
		offset /= BIOSDISK_SECSIZE;
	}
	return (bcache_strategy(&bcd, rw, dblk + offset, size,
	    buf, rsize));
}

static int
bd_realstrategy(void *devdata, int rw, daddr_t dblk, size_t size,
    char *buf, size_t *rsize)
{
	struct disk_devdesc *dev = (struct disk_devdesc *)devdata;
	bdinfo_t *bd;
	uint64_t disk_blocks, offset, d_offset;
	size_t blks, blkoff, bsize, bio_size, rest;
	caddr_t bbuf = NULL;
	int rc;

	bd = bd_get_bdinfo(&dev->dd);
	if (bd == NULL || (bd->bd_flags & BD_NO_MEDIA) == BD_NO_MEDIA)
		return (EIO);

	/*
	 * First make sure the IO size is a multiple of 512 bytes. While we do
	 * process partial reads below, the strategy mechanism is built
	 * assuming IO is a multiple of 512B blocks. If the request is not
	 * a multiple of 512B blocks, it has to be some sort of bug.
	 */
	if (size == 0 || (size % BIOSDISK_SECSIZE) != 0) {
		printf("bd_strategy: %d bytes I/O not multiple of %d\n",
		    size, BIOSDISK_SECSIZE);
		return (EIO);
	}

	DEBUG("open_disk %p", dev);

	offset = dblk * BIOSDISK_SECSIZE;
	dblk = offset / bd->bd_sectorsize;
	blkoff = offset % bd->bd_sectorsize;

	/*
	 * Check the value of the size argument. We do have quite small
	 * heap (64MB), but we do not know good upper limit, so we check against
	 * INT_MAX here. This will also protect us against possible overflows
	 * while translating block count to bytes.
	 */
	if (size > INT_MAX) {
		DEBUG("too large I/O: %zu bytes", size);
		return (EIO);
	}

	blks = size / bd->bd_sectorsize;
	if (blks == 0 || (size % bd->bd_sectorsize) != 0)
		blks++;

	if (dblk > dblk + blks)
		return (EIO);

	if (rsize)
		*rsize = 0;

	/*
	 * Get disk blocks, this value is either for whole disk or for
	 * partition.
	 */
	d_offset = 0;
	disk_blocks = 0;
	if (dev->dd.d_dev->dv_type == DEVT_DISK) {
		if (disk_ioctl(dev, DIOCGMEDIASIZE, &disk_blocks) == 0) {
			/* DIOCGMEDIASIZE does return bytes. */
			disk_blocks /= bd->bd_sectorsize;
		}
		d_offset = dev->d_offset;
	}
	if (disk_blocks == 0)
		disk_blocks = bd->bd_sectors - d_offset;

	/* Validate source block address. */
	if (dblk < d_offset || dblk >= d_offset + disk_blocks)
		return (EIO);

	/*
	 * Truncate if we are crossing disk or partition end.
	 */
	if (dblk + blks >= d_offset + disk_blocks) {
		blks = d_offset + disk_blocks - dblk;
		size = blks * bd->bd_sectorsize;
		DEBUG("short I/O %d", blks);
	}

	bio_size = min(BIO_BUFFER_SIZE, size);
	while (bio_size > bd->bd_sectorsize) {
		bbuf = bio_alloc(bio_size);
		if (bbuf != NULL)
			break;
		bio_size -= bd->bd_sectorsize;
	}
	if (bbuf == NULL) {
		bio_size = V86_IO_BUFFER_SIZE;
		if (bio_size / bd->bd_sectorsize == 0)
			panic("BUG: Real mode buffer is too small");

		/* Use alternate 4k buffer */
		bbuf = PTOV(V86_IO_BUFFER);
	}
	rest = size;
	rc = 0;
	while (blks > 0) {
		int x = min(blks, bio_size / bd->bd_sectorsize);

		switch (rw & F_MASK) {
		case F_READ:
			DEBUG("read %d from %lld to %p", x, dblk, buf);
			bsize = bd->bd_sectorsize * x - blkoff;
			if (rest < bsize)
				bsize = rest;

			if ((rc = bd_io(dev, bd, dblk, x, bbuf, BD_RD)) != 0) {
				rc = EIO;
				goto error;
			}

			bcopy(bbuf + blkoff, buf, bsize);
			break;
		case F_WRITE :
			DEBUG("write %d from %lld to %p", x, dblk, buf);
			if (blkoff != 0) {
				/*
				 * We got offset to sector, read 1 sector to
				 * bbuf.
				 */
				x = 1;
				bsize = bd->bd_sectorsize - blkoff;
				bsize = min(bsize, rest);
				rc = bd_io(dev, bd, dblk, x, bbuf, BD_RD);
			} else if (rest < bd->bd_sectorsize) {
				/*
				 * The remaining block is not full
				 * sector. Read 1 sector to bbuf.
				 */
				x = 1;
				bsize = rest;
				rc = bd_io(dev, bd, dblk, x, bbuf, BD_RD);
			} else {
				/* We can write full sector(s). */
				bsize = bd->bd_sectorsize * x;
			}
			/*
			 * Put your Data In, Put your Data out,
			 * Put your Data In, and shake it all about
			 */
			bcopy(buf, bbuf + blkoff, bsize);
			if ((rc = bd_io(dev, bd, dblk, x, bbuf, BD_WR)) != 0) {
				rc = EIO;
				goto error;
			}

			break;
		default:
			/* DO NOTHING */
			rc = EROFS;
			goto error;
		}

		blkoff = 0;
		buf += bsize;
		rest -= bsize;
		blks -= x;
		dblk += x;
	}

	if (rsize != NULL)
		*rsize = size;
error:
	if (bbuf != PTOV(V86_IO_BUFFER))
		bio_free(bbuf, bio_size);
	return (rc);
}

static int
bd_edd_io(bdinfo_t *bd, daddr_t dblk, int blks, caddr_t dest,
    int dowrite)
{
	static struct edd_packet packet;

	packet.len = sizeof(struct edd_packet);
	packet.count = blks;
	packet.off = VTOPOFF(dest);
	packet.seg = VTOPSEG(dest);
	packet.lba = dblk;
	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	/* Should we Write with verify ?? 0x4302 ? */
	if (dowrite == BD_WR)
		v86.eax = 0x4300;
	else
		v86.eax = 0x4200;
	v86.edx = bd->bd_unit;
	v86.ds = VTOPSEG(&packet);
	v86.esi = VTOPOFF(&packet);
	v86int();
	if (V86_CY(v86.efl))
		return (v86.eax >> 8);
	return (0);
}

static int
bd_chs_io(bdinfo_t *bd, daddr_t dblk, int blks, caddr_t dest,
    int dowrite)
{
	uint32_t x, bpc, cyl, hd, sec;

	bpc = bd->bd_sec * bd->bd_hds;	/* blocks per cylinder */
	x = dblk;
	cyl = x / bpc;			/* block # / blocks per cylinder */
	x %= bpc;				/* block offset into cylinder */
	hd = x / bd->bd_sec;		/* offset / blocks per track */
	sec = x % bd->bd_sec;		/* offset into track */

	/* correct sector number for 1-based BIOS numbering */
	sec++;

	if (cyl > 1023) {
		/* CHS doesn't support cylinders > 1023. */
		return (1);
	}

	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	if (dowrite == BD_WR)
		v86.eax = 0x300 | blks;
	else
		v86.eax = 0x200 | blks;
	v86.ecx = ((cyl & 0xff) << 8) | ((cyl & 0x300) >> 2) | sec;
	v86.edx = (hd << 8) | bd->bd_unit;
	v86.es = VTOPSEG(dest);
	v86.ebx = VTOPOFF(dest);
	v86int();
	if (V86_CY(v86.efl))
		return (v86.eax >> 8);
	return (0);
}

static void
bd_io_workaround(bdinfo_t *bd)
{
	uint8_t buf[8 * 1024];

	bd_edd_io(bd, 0xffffffff, 1, (caddr_t)buf, BD_RD);
}

static int
bd_io(struct disk_devdesc *dev, bdinfo_t *bd, daddr_t dblk, int blks,
    caddr_t dest, int dowrite)
{
	int result, retry;

	/* Just in case some idiot actually tries to read/write -1 blocks... */
	if (blks < 0)
		return (-1);

	/*
	 * Workaround for a problem with some HP ProLiant BIOS failing to work
	 * out the boot disk after installation. hrs and kuriyama discovered
	 * this problem with an HP ProLiant DL320e Gen 8 with a 3TB HDD, and
	 * discovered that an int13h call seems to cause a buffer overrun in
	 * the bios. The problem is alleviated by doing an extra read before
	 * the buggy read. It is not immediately known whether other models
	 * are similarly affected.
	 * Loop retrying the operation a couple of times.  The BIOS
	 * may also retry.
	 */
	if (dowrite == BD_RD && dblk >= 0x100000000)
		bd_io_workaround(bd);
	for (retry = 0; retry < 3; retry++) {
		if (bd->bd_flags & BD_MODEEDD)
			result = bd_edd_io(bd, dblk, blks, dest, dowrite);
		else
			result = bd_chs_io(bd, dblk, blks, dest, dowrite);

		if (result == 0) {
			if (bd->bd_flags & BD_NO_MEDIA)
				bd->bd_flags &= ~BD_NO_MEDIA;
			break;
		}

		bd_reset_disk(bd->bd_unit);

		/*
		 * Error codes:
		 * 20h	controller failure
		 * 31h	no media in drive (IBM/MS INT 13 extensions)
		 * 80h	no media in drive, VMWare (Fusion)
		 * There is no reason to repeat the IO with errors above.
		 */
		if (result == 0x20 || result == 0x31 || result == 0x80) {
			bd->bd_flags |= BD_NO_MEDIA;
			break;
		}
	}

	if (result != 0 && (bd->bd_flags & BD_NO_MEDIA) == 0) {
		if (dowrite == BD_WR) {
			printf("%s%d: Write %d sector(s) from %p (0x%x) "
			    "to %lld: 0x%x\n", dev->dd.d_dev->dv_name,
			    dev->dd.d_unit, blks, dest, VTOP(dest), dblk,
			    result);
		} else {
			printf("%s%d: Read %d sector(s) from %lld to %p "
			    "(0x%x): 0x%x\n", dev->dd.d_dev->dv_name,
			    dev->dd.d_unit, blks, dblk, dest, VTOP(dest),
			    result);
		}
	}

	return (result);
}

/*
 * Return the BIOS geometry of a given "fixed drive" in a format
 * suitable for the legacy bootinfo structure.  Since the kernel is
 * expecting raw int 0x13/0x8 values for N_BIOS_GEOM drives, we
 * prefer to get the information directly, rather than rely on being
 * able to put it together from information already maintained for
 * different purposes and for a probably different number of drives.
 *
 * For valid drives, the geometry is expected in the format (31..0)
 * "000000cc cccccccc hhhhhhhh 00ssssss"; and invalid drives are
 * indicated by returning the geometry of a "1.2M" PC-format floppy
 * disk.  And, incidentally, what is returned is not the geometry as
 * such but the highest valid cylinder, head, and sector numbers.
 */
uint32_t
bd_getbigeom(int bunit)
{

	v86.ctl = V86_FLAGS;
	v86.addr = 0x13;
	v86.eax = 0x800;
	v86.edx = 0x80 + bunit;
	v86int();
	if (V86_CY(v86.efl))
		return (0x4f010f);
	return (((v86.ecx & 0xc0) << 18) | ((v86.ecx & 0xff00) << 8) |
	    (v86.edx & 0xff00) | (v86.ecx & 0x3f));
}

/*
 * Return a suitable dev_t value for (dev).
 *
 * In the case where it looks like (dev) is a SCSI disk, we allow the number of
 * IDE disks to be specified in $num_ide_disks.  There should be a Better Way.
 */
int
bd_getdev(struct i386_devdesc *d)
{
	struct disk_devdesc *dev;
	bdinfo_t *bd;
	int	biosdev;
	int	major;
	int	rootdev;
	char	*nip, *cp;
	int	i, unit, slice, partition;

	/* XXX: Assume partition 'a'. */
	slice = 0;
	partition = 0;

	dev = (struct disk_devdesc *)d;
	bd = bd_get_bdinfo(&dev->dd);
	if (bd == NULL)
		return (-1);

	biosdev = bd_unit2bios(d);
	DEBUG("unit %d BIOS device %d", dev->dd.d_unit, biosdev);
	if (biosdev == -1)			/* not a BIOS device */
		return (-1);

	if (dev->dd.d_dev->dv_type == DEVT_DISK) {
		if (disk_open(dev, bd->bd_sectors * bd->bd_sectorsize,
		    bd->bd_sectorsize) != 0)	/* oops, not a viable device */
			return (-1);
		else
			disk_close(dev);
		slice = dev->d_slice + 1;
		partition = dev->d_partition;
	}

	if (biosdev < 0x80) {
		/* floppy (or emulated floppy) or ATAPI device */
		if (bd->bd_type == DT_ATAPI) {
			/* is an ATAPI disk */
			major = WFDMAJOR;
		} else {
			/* is a floppy disk */
			major = FDMAJOR;
		}
	} else {
		/* assume an IDE disk */
		major = WDMAJOR;
	}
	/* default root disk unit number */
	unit = biosdev & 0x7f;

	if (dev->dd.d_dev->dv_type == DEVT_CD) {
		/*
		 * XXX: Need to examine device spec here to figure out if
		 * SCSI or ATAPI.  No idea on how to figure out device number.
		 * All we can really pass to the kernel is what bus and device
		 * on which bus we were booted from, which dev_t isn't well
		 * suited to since those number don't match to unit numbers
		 * very well.  We may just need to engage in a hack where
		 * we pass -C to the boot args if we are the boot device.
		 */
		major = ACDMAJOR;
		unit = 0;       /* XXX */
	}

	/* XXX a better kludge to set the root disk unit number */
	if ((nip = getenv("root_disk_unit")) != NULL) {
		i = strtol(nip, &cp, 0);
		/* check for parse error */
		if ((cp != nip) && (*cp == 0))
			unit = i;
	}

	rootdev = MAKEBOOTDEV(major, slice, unit, partition);
	DEBUG("dev is 0x%x\n", rootdev);
	return (rootdev);
}
