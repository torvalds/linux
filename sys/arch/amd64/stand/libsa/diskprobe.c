/*	$OpenBSD: diskprobe.c,v 1.28 2024/06/04 20:31:35 krw Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/* We want the disk type names from disklabel.h */
#undef DKTYPENAMES

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/reboot.h>
#include <sys/disklabel.h>
#include <sys/hibernate.h>

#include <lib/libz/zlib.h>
#include <machine/biosvar.h>
#include <stand/boot/bootarg.h>

#include "disk.h"
#include "biosdev.h"
#include "libsa.h"

#ifdef SOFTRAID
#include "softraid_amd64.h"
#endif

#define MAX_CKSUMLEN MAXBSIZE / DEV_BSIZE	/* Max # of blks to cksum */

/* Local Prototypes */
static int disksum(int);

int bootdev_has_hibernate(void);		/* export for loadfile() */

/* List of disk devices we found/probed */
struct disklist_lh disklist;

/* Pointer to boot device */
struct diskinfo *bootdev_dip;

extern int debug;
extern int bios_bootdev;
extern int bios_cddev;

static void
diskinfo_init(struct diskinfo *dip)
{
	bzero(dip, sizeof(*dip));
	dip->diskio = biosd_diskio;
	dip->strategy = biosstrategy;
}

/* Probe for all BIOS floppies */
static void
floppyprobe(void)
{
	struct diskinfo *dip;
	int i;

	/* Floppies */
	for (i = 0; i < 4; i++) {
		dip = alloc(sizeof(struct diskinfo));
		diskinfo_init(dip);

		if (bios_getdiskinfo(i, &dip->bios_info)) {
#ifdef BIOS_DEBUG
			if (debug)
				printf(" <!fd%u>", i);
#endif
			free(dip, sizeof(*dip));
			break;
		}

		printf(" fd%u", i);

		/* Fill out best we can - (fd?) */
		dip->bios_info.bsd_dev = MAKEBOOTDEV(2, 0, 0, i, RAW_PART);

		/*
		 * Delay reading the disklabel until we're sure we want
		 * to boot from the floppy. Doing this avoids a delay
		 * (sometimes very long) when trying to read the label
		 * and the drive is unplugged.
		 */
		dip->bios_info.flags |= BDI_BADLABEL;

		/* Add to queue of disks */
		TAILQ_INSERT_TAIL(&disklist, dip, list);
	}
}

/* Probe for all BIOS hard disks */
static void
hardprobe(void)
{
	struct diskinfo *dip;
	int i;
	u_int bsdunit, type;
	u_int scsi = 0, ide = 0;
	const char *dc = (const char *)((0x40 << 4) + 0x75);

	/* Hard disks */
	for (i = 0x80; i < (0x80 + *dc); i++) {
		dip = alloc(sizeof(struct diskinfo));
		diskinfo_init(dip);

		if (bios_getdiskinfo(i, &dip->bios_info)) {
#ifdef BIOS_DEBUG
			if (debug)
				printf(" <!hd%u>", i&0x7f);
#endif
			free(dip, sizeof(*dip));
			break;
		}

		printf(" hd%u%s", i&0x7f, (dip->bios_info.bios_edd > 0?"+":""));

		/* Try to find the label, to figure out device type */
		if ((bios_getdisklabel(&dip->bios_info, &dip->disklabel)) ) {
			printf("*");
			bsdunit = ide++;
			type = 0;	/* XXX let it be IDE */
		} else {
			/* Best guess */
			switch (dip->disklabel.d_type) {
			case DTYPE_SCSI:
				type = 4;
				bsdunit = scsi++;
				dip->bios_info.flags |= BDI_GOODLABEL;
				break;

			case DTYPE_ESDI:
			case DTYPE_ST506:
				type = 0;
				bsdunit = ide++;
				dip->bios_info.flags |= BDI_GOODLABEL;
				break;

			default:
				dip->bios_info.flags |= BDI_BADLABEL;
				type = 0;	/* XXX Suggest IDE */
				bsdunit = ide++;
			}
		}

		dip->bios_info.checksum = 0; /* just in case */
		/* Fill out best we can */
		dip->bsddev = dip->bios_info.bsd_dev =
		    MAKEBOOTDEV(type, 0, 0, bsdunit, RAW_PART);
		check_hibernate(dip);

		/* Add to queue of disks */
		TAILQ_INSERT_TAIL(&disklist, dip, list);
	}
}

/* Probe for all BIOS supported disks */
u_int32_t bios_cksumlen;
void
diskprobe(void)
{
	struct diskinfo *dip;
	int i;

	/* These get passed to kernel */
	bios_diskinfo_t *bios_diskinfo;

	/* Init stuff */
	TAILQ_INIT(&disklist);

	/* Do probes */
	floppyprobe();
#ifdef BIOS_DEBUG
	if (debug)
		printf(";");
#endif
	hardprobe();

#ifdef SOFTRAID
	srprobe();
#endif

	/* Checksumming of hard disks */
	for (i = 0; disksum(i++) && i < MAX_CKSUMLEN; )
		;
	bios_cksumlen = i;

	/* Get space for passing bios_diskinfo stuff to kernel */
	for (i = 0, dip = TAILQ_FIRST(&disklist); dip;
	    dip = TAILQ_NEXT(dip, list))
		i++;
	bios_diskinfo = alloc(++i * sizeof(bios_diskinfo_t));

	/* Copy out the bios_diskinfo stuff */
	for (i = 0, dip = TAILQ_FIRST(&disklist); dip;
	    dip = TAILQ_NEXT(dip, list))
		bios_diskinfo[i++] = dip->bios_info;

	bios_diskinfo[i++].bios_number = -1;
	/* Register for kernel use */
	addbootarg(BOOTARG_CKSUMLEN, sizeof(u_int32_t), &bios_cksumlen);
	addbootarg(BOOTARG_DISKINFO, i * sizeof(bios_diskinfo_t),
	    bios_diskinfo);
}

void
cdprobe(void)
{
	struct diskinfo *dip;
	int cddev = bios_cddev & 0xff;

	/* Another BIOS boot device... */

	if (bios_cddev == -1)			/* Not been set, so don't use */
		return;

	dip = alloc(sizeof(struct diskinfo));
	diskinfo_init(dip);

	printf(" cd0");

	dip->bios_info.bios_number = cddev;
	dip->bios_info.bios_edd = 1;		/* Use the LBA calls */
	dip->bios_info.flags |= BDI_GOODLABEL | BDI_EL_TORITO;
	dip->bios_info.checksum = 0;		 /* just in case */
	dip->bios_info.bsd_dev =
	    MAKEBOOTDEV(6, 0, 0, 0, RAW_PART);

	/* Create an imaginary disk label */
	dip->disklabel.d_secsize = 2048;
	dip->disklabel.d_ntracks = 1;
	dip->disklabel.d_nsectors = 100;
	dip->disklabel.d_ncylinders = 1;
	dip->disklabel.d_secpercyl = dip->disklabel.d_ntracks *
	    dip->disklabel.d_nsectors;

	strncpy(dip->disklabel.d_typename, "ATAPI CD-ROM",
	    sizeof(dip->disklabel.d_typename));
	dip->disklabel.d_type = DTYPE_ATAPI;

	strncpy(dip->disklabel.d_packname, "fictitious",
	    sizeof(dip->disklabel.d_packname));
	DL_SETDSIZE(&dip->disklabel, 100);

	/* 'a' partition covering the "whole" disk */
	DL_SETPOFFSET(&dip->disklabel.d_partitions[0], 0);
	DL_SETPSIZE(&dip->disklabel.d_partitions[0], 100);
	dip->disklabel.d_partitions[0].p_fstype = FS_UNUSED;

	/* The raw partition is special */
	DL_SETPOFFSET(&dip->disklabel.d_partitions[RAW_PART], 0);
	DL_SETPSIZE(&dip->disklabel.d_partitions[RAW_PART], 100);
	dip->disklabel.d_partitions[RAW_PART].p_fstype = FS_UNUSED;

	dip->disklabel.d_npartitions = MAXPARTITIONS;

	dip->disklabel.d_magic = DISKMAGIC;
	dip->disklabel.d_magic2 = DISKMAGIC;
	dip->disklabel.d_checksum = dkcksum(&dip->disklabel);

	/* Add to queue of disks */
	TAILQ_INSERT_TAIL(&disklist, dip, list);
}

/* Find info on given BIOS disk */
struct diskinfo *
dklookup(int dev)
{
	struct diskinfo *dip;

	for (dip = TAILQ_FIRST(&disklist); dip; dip = TAILQ_NEXT(dip, list))
		if (dip->bios_info.bios_number == dev)
			return dip;

	return NULL;
}

void
dump_diskinfo(void)
{
	struct diskinfo *dip;

	printf("Disk\tBIOS#\tType\tCyls\tHeads\tSecs\tFlags\tChecksum\n");
	for (dip = TAILQ_FIRST(&disklist); dip; dip = TAILQ_NEXT(dip, list)) {
		bios_diskinfo_t *bdi = &dip->bios_info;
		int d = bdi->bios_number;
		int u = d & 0x7f;
		char c;

		if (bdi->flags & BDI_EL_TORITO) {
			c = 'c';
			u = 0;
		} else {
		    	c = (d & 0x80) ? 'h' : 'f';
		}

		printf("%cd%d\t0x%x\t%s\t%d\t%d\t%d\t0x%x\t0x%x\n",
		    c, u, d,
		    (bdi->flags & BDI_BADLABEL)?"*none*":"label",
		    bdi->bios_cylinders, bdi->bios_heads, bdi->bios_sectors,
		    bdi->flags, bdi->checksum);
	}
}

/* Find BIOS portion on given BIOS disk
 * XXX - Use dklookup() instead.
 */
bios_diskinfo_t *
bios_dklookup(int dev)
{
	struct diskinfo *dip;

	dip = dklookup(dev);
	if (dip)
		return &dip->bios_info;

	return NULL;
}

/*
 * Checksum one more block on all harddrives
 *
 * Use the adler32() function from libz,
 * as it is quick, small, and available.
 */
int
disksum(int blk)
{
	struct diskinfo *dip, *dip2;
	int st, reprobe = 0;
	char buf[DEV_BSIZE];

	for (dip = TAILQ_FIRST(&disklist); dip; dip = TAILQ_NEXT(dip, list)) {
		bios_diskinfo_t *bdi = &dip->bios_info;

		/* Skip this disk if it is not a HD or has had an I/O error */
		if (!(bdi->bios_number & 0x80) || bdi->flags & BDI_INVALID)
			continue;

		/* Adler32 checksum */
		st = dip->diskio(F_READ, dip, blk, 1, buf);
		if (st) {
			bdi->flags |= BDI_INVALID;
			continue;
		}
		bdi->checksum = adler32(bdi->checksum, buf, DEV_BSIZE);

		for (dip2 = TAILQ_FIRST(&disklist); dip2 != dip;
				dip2 = TAILQ_NEXT(dip2, list)) {
			bios_diskinfo_t *bd = &dip2->bios_info;
			if ((bd->bios_number & 0x80) &&
			    !(bd->flags & BDI_INVALID) &&
			    bdi->checksum == bd->checksum)
				reprobe = 1;
		}
	}

	return reprobe;
}

int
bootdev_has_hibernate(void)
{
	return ((bootdev_dip->bios_info.flags & BDI_HIBVALID)? 1 : 0);
}

void
check_hibernate(struct diskinfo *dip)
{
	uint8_t buf[DEV_BSIZE];
	daddr_t sec;
	int error;
	union hibernate_info *hib = (union hibernate_info *)&buf;

	/* read hibernate */
	if (dip->disklabel.d_partitions[1].p_fstype != FS_SWAP ||
	    DL_GETPSIZE(&dip->disklabel.d_partitions[1]) == 0)
		return;

	sec = DL_GETPOFFSET(&dip->disklabel.d_partitions[1]) +
	    DL_GETPSIZE(&dip->disklabel.d_partitions[1]) - 1;

	error = dip->strategy(dip, F_READ, DL_SECTOBLK(&dip->disklabel, sec),
	    sizeof buf, &buf, NULL);
	if (error == 0 && hib->magic == HIBERNATE_MAGIC)
		dip->bios_info.flags |= BDI_HIBVALID; /* Hibernate present */
}
