/*	$OpenBSD: efidev.c,v 1.43 2025/09/17 20:23:58 deraadt Exp $	*/

/*
 * Copyright (c) 1996 Michael Shalayeff
 * Copyright (c) 2003 Tobias Weingartner
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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
#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/disklabel.h>
#include <lib/libz/zlib.h>
#include <isofs/cd9660/iso.h>

#include "libsa.h"
#include "disk.h"

#ifdef SOFTRAID
#include <dev/softraidvar.h>
#include <lib/libsa/softraid.h>
#include "softraid_amd64.h"
#endif

#include <efi.h>

extern int debug;
extern EFI_BOOT_SERVICES *BS;

#include "efidev.h"
#include "biosdev.h"	/* for dklookup() */

#define EFI_BLKSPERSEC(_ed)	((_ed)->blkio->Media->BlockSize / DEV_BSIZE)
#define EFI_SECTOBLK(_ed, _n)	((_n) * EFI_BLKSPERSEC(_ed))

struct efi_diskinfo {
	EFI_BLOCK_IO		*blkio;
	UINT32			 mediaid;
};

int bios_bootdev;
static EFI_STATUS
		 efid_io(int, efi_diskinfo_t, u_int, int, void *);
static int	 efid_diskio(int, struct diskinfo *, u_int, int, void *);
static int	 efi_getdisklabel_cd9660(efi_diskinfo_t, struct disklabel *);
static u_int	 findopenbsd(efi_diskinfo_t, const char **);
static u_int	 findopenbsd_gpt(efi_diskinfo_t, const char **);
static int	 gpt_chk_mbr(struct dos_partition *, u_int64_t);

void
efid_init(struct diskinfo *dip, void *handle)
{
	EFI_BLOCK_IO		*blkio = handle;

	memset(dip, 0, sizeof(struct diskinfo));
	dip->efi_info = alloc(sizeof(struct efi_diskinfo));
	dip->efi_info->blkio = blkio;
	dip->efi_info->mediaid = blkio->Media->MediaId;
	dip->diskio = efid_diskio;
	dip->strategy = efistrategy;
}

static EFI_STATUS
efid_io(int rw, efi_diskinfo_t ed, u_int off, int nsect, void *buf)
{
	u_int blks, start, end;
	EFI_PHYSICAL_ADDRESS addr;
	EFI_STATUS status;
	caddr_t data;
	size_t size;

	/* block count of the intrinsic block size in DEV_BSIZE */
	blks = EFI_BLKSPERSEC(ed);
	if (blks == 0)
		/* block size < 512.  HP Stream 13 actually has such a disk. */
		return (EFI_UNSUPPORTED);

	start = off / blks;
	end = (off + nsect + blks - 1) / blks;
	size = (end - start) * ed->blkio->Media->BlockSize;

	status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(size), &addr);
	if (EFI_ERROR(status))
		goto on_eio;
	data = (caddr_t)(uintptr_t)addr;

	switch (rw) {
	case F_READ:
		status = ed->blkio->ReadBlocks(ed->blkio, ed->mediaid, start,
		    size, data);
		if (EFI_ERROR(status))
			goto on_eio;
		memcpy(buf, data + DEV_BSIZE * (off - start * blks),
		    DEV_BSIZE * nsect);
		break;
	case F_WRITE:
		if (ed->blkio->Media->ReadOnly)
			goto on_eio;
		if (off % blks != 0 || nsect % blks != 0) {
			status = ed->blkio->ReadBlocks(ed->blkio, ed->mediaid,
			    start, size, data);
			if (EFI_ERROR(status))
				goto on_eio;
		}
		memcpy(data + DEV_BSIZE * (off - start * blks), buf,
		    DEV_BSIZE * nsect);
		status = ed->blkio->WriteBlocks(ed->blkio, ed->mediaid, start,
		    size, data);
		if (EFI_ERROR(status))
			goto on_eio;
		break;
	}

on_eio:
	BS->FreePages(addr, EFI_SIZE_TO_PAGES(size));

	return (status);
}

static int
efid_diskio(int rw, struct diskinfo *dip, u_int off, int nsect, void *buf)
{
	EFI_STATUS status;

	status = efid_io(rw, dip->efi_info, off, nsect, buf);

	return ((EFI_ERROR(status))? -1 : 0);
}

/*
 * Returns 0 if the MBR with the provided partition array is a GPT protective
 * MBR, and returns 1 otherwise. A GPT protective MBR would have one and only
 * one MBR partition, an EFI partition that either covers the whole disk or as
 * much of it as is possible with a 32bit size field.
 *
 * Taken from kern/subr_disk.c.
 *
 * NOTE: MS always uses a size of UINT32_MAX for the EFI partition!**
 */
static int
gpt_chk_mbr(struct dos_partition *dp, u_int64_t dsize)
{
	struct dos_partition *dp2;
	int efi, found, i;
	u_int32_t psize;

	found = efi = 0;
	for (dp2=dp, i=0; i < NDOSPART; i++, dp2++) {
		if (dp2->dp_typ == DOSPTYP_UNUSED)
			continue;
		found++;
		if (dp2->dp_typ != DOSPTYP_EFI)
			continue;
		if (letoh32(dp2->dp_start) != GPTSECTOR)
			continue;
		psize = letoh32(dp2->dp_size);
		if (psize <= (dsize - GPTSECTOR) || psize == UINT32_MAX)
			efi++;
	}
	if (found == 1 && efi == 1)
		return (0);

	return (1);
}

/*
 * Try to find the disk address of the first MBR OpenBSD partition.
 *
 * N.B.: must boot from a partition within first 2^32-1 sectors!
 *
 * Called only if the MBR on sector 0 is *not* a protective MBR
 * and *does* have a valid signature.
 *
 * We don't check the signatures of EBR's, and they cannot be
 * protective MBR's so there is no need to check for that.
 */
static u_int
findopenbsd(efi_diskinfo_t ed, const char **err)
{
	EFI_STATUS status;
	struct dos_mbr mbr;
	struct dos_partition *dp;
	u_int mbroff = DOSBBSECTOR;
	u_int mbr_eoff = DOSBBSECTOR;	/* Offset of MBR extended partition. */
	int i, maxebr = DOS_MAXEBR, nextebr;

again:
	if (!maxebr--) {
		*err = "too many extended partitions";
		return (-1);
	}

	/* Read MBR */
	bzero(&mbr, sizeof(mbr));
	status = efid_io(F_READ, ed, mbroff, 1, &mbr);
	if (EFI_ERROR(status)) {
		*err = "Disk I/O Error";
		return (-1);
	}

	/* Search for OpenBSD partition */
	nextebr = 0;
	for (i = 0; i < NDOSPART; i++) {
		dp = &mbr.dmbr_parts[i];
		if (!dp->dp_size)
			continue;
#ifdef BIOS_DEBUG
		if (debug)
			printf("found partition %u: "
			    "type %u (0x%x) offset %u (0x%x)\n",
			    (int)(dp - mbr.dmbr_parts),
			    dp->dp_typ, dp->dp_typ,
			    dp->dp_start, dp->dp_start);
#endif
		if (dp->dp_typ == DOSPTYP_OPENBSD) {
			if (dp->dp_start > (dp->dp_start + mbroff))
				continue;
			return (dp->dp_start + mbroff);
		}

		/*
		 * Record location of next ebr if and only if this is the first
		 * extended partition in this boot record!
		 */
		if (!nextebr && (dp->dp_typ == DOSPTYP_EXTEND ||
		    dp->dp_typ == DOSPTYP_EXTENDL)) {
			nextebr = dp->dp_start + mbr_eoff;
			if (nextebr < dp->dp_start)
				nextebr = (u_int)-1;
			if (mbr_eoff == DOSBBSECTOR)
				mbr_eoff = dp->dp_start;
		}
	}

	if (nextebr && nextebr != (u_int)-1) {
		mbroff = nextebr;
		goto again;
	}

	return (-1);
}

/*
 * Try to find the disk address of the first GPT OpenBSD partition.
 *
 * N.B.: must boot from a partition within first 2^32-1 sectors!
 *
 * Called only if the MBR on sector 0 *is* a protective MBR
 * with a valid signature and sector 1 is a valid GPT header.
 */
static u_int
findopenbsd_gpt(efi_diskinfo_t ed, const char **err)
{
	EFI_STATUS		 status;
	struct			 gpt_header gh;
	int			 i, part, found;
	uint64_t		 lba;
	uint32_t		 orig_csum, new_csum;
	uint32_t		 ghsize, ghpartsize, ghpartnum, ghpartspersec;
	uint32_t		 gpsectors;
	const char		 openbsd_uuid_code[] = GPT_UUID_OPENBSD;
	struct gpt_partition	 gp;
	static struct uuid	*openbsd_uuid = NULL, openbsd_uuid_space;
	static u_char		 buf[4096];

	/* Prepare OpenBSD UUID */
	if (openbsd_uuid == NULL) {
		/* XXX: should be replaced by uuid_dec_be() */
		memcpy(&openbsd_uuid_space, openbsd_uuid_code,
		    sizeof(openbsd_uuid_space));
		openbsd_uuid_space.time_low =
		    betoh32(openbsd_uuid_space.time_low);
		openbsd_uuid_space.time_mid =
		    betoh16(openbsd_uuid_space.time_mid);
		openbsd_uuid_space.time_hi_and_version =
		    betoh16(openbsd_uuid_space.time_hi_and_version);

		openbsd_uuid = &openbsd_uuid_space;
	}

	if (EFI_BLKSPERSEC(ed) > 8) {
		*err = "disk sector > 4096 bytes\n";
		return (-1);
	}

	/* GPT Header */
	lba = GPTSECTOR;
	status = efid_io(F_READ, ed, EFI_SECTOBLK(ed, lba), EFI_BLKSPERSEC(ed),
	    buf);
	if (EFI_ERROR(status)) {
		*err = "Disk I/O Error";
		return (-1);
	}
	memcpy(&gh, buf, sizeof(gh));

	/* Check signature */
	if (letoh64(gh.gh_sig) != GPTSIGNATURE) {
		*err = "bad GPT signature\n";
		return (-1);
	}

	if (letoh32(gh.gh_rev) != GPTREVISION) {
		*err = "bad GPT revision\n";
		return (-1);
	}

	ghsize = letoh32(gh.gh_size);
	if (ghsize < GPTMINHDRSIZE || ghsize > sizeof(struct gpt_header)) {
		*err = "bad GPT header size\n";
		return (-1);
	}

	/* Check checksum */
	orig_csum = gh.gh_csum;
	gh.gh_csum = 0;
	new_csum = crc32(0, (unsigned char *)&gh, ghsize);
	gh.gh_csum = orig_csum;
	if (letoh32(orig_csum) != new_csum) {
		*err = "bad GPT header checksum\n";
		return (-1);
	}

	lba = letoh64(gh.gh_part_lba);
	ghpartsize = letoh32(gh.gh_part_size);
	ghpartspersec = ed->blkio->Media->BlockSize / ghpartsize;
	ghpartnum = letoh32(gh.gh_part_num);
	gpsectors = (ghpartnum + ghpartspersec - 1) / ghpartspersec;
	new_csum = crc32(0L, Z_NULL, 0);
	found = 0;
	for (i = 0; i < gpsectors; i++, lba++) {
		status = efid_io(F_READ, ed, EFI_SECTOBLK(ed, lba),
		    EFI_BLKSPERSEC(ed), buf);
		if (EFI_ERROR(status)) {
			*err = "Disk I/O Error";
			return (-1);
		}
		for (part = 0; part < ghpartspersec; part++) {
			if (ghpartnum == 0)
				break;
			new_csum = crc32(new_csum, buf + part * sizeof(gp),
			    sizeof(gp));
			ghpartnum--;
			if (found)
				continue;
			memcpy(&gp, buf + part * sizeof(gp), sizeof(gp));
			if (memcmp(&gp.gp_type, openbsd_uuid,
			    sizeof(struct uuid)) == 0)
				found = 1;
		}
	}
	if (new_csum != letoh32(gh.gh_part_csum)) {
		*err = "bad GPT entries checksum\n";
		return (-1);
	}
	if (found) {
		lba = letoh64(gp.gp_lba_start);
		/* Bootloaders do not current handle addresses > UINT_MAX! */
		if (lba > UINT_MAX || EFI_SECTOBLK(ed, lba) > UINT_MAX) {
			*err = "OpenBSD Partition LBA > 2**32 - 1";
			return (-1);
		}
		return (u_int)lba;
	}

	return (-1);
}

const char *
efi_getdisklabel(efi_diskinfo_t ed, struct disklabel *label)
{
	u_int start = 0;
	uint8_t buf[DEV_BSIZE];
	struct dos_partition dosparts[NDOSPART];
	EFI_STATUS status;
	const char *err = NULL;
	int error;

	/*
	 * Read sector 0. Ensure it has a valid MBR signature.
	 *
	 * If it's a protective MBR then try to find the disklabel via
	 * GPT. If it's not a protective MBR, try to find the disklabel
	 * via MBR.
	 */
	memset(buf, 0, sizeof(buf));
	status = efid_io(F_READ, ed, DOSBBSECTOR, 1, buf);
	if (EFI_ERROR(status))
		return ("Disk I/O Error");

	/* Check MBR signature. */
	if (buf[510] != 0x55 || buf[511] != 0xaa) {
		if (efi_getdisklabel_cd9660(ed, label) == 0)
			return (NULL);
		return ("invalid MBR signature");
	}

	memcpy(dosparts, buf+DOSPARTOFF, sizeof(dosparts));

	/* check for GPT protective MBR. */
	if (gpt_chk_mbr(dosparts, ed->blkio->Media->LastBlock + 1) == 0) {
		start = findopenbsd_gpt(ed, &err);
		if (start == (u_int)-1) {
			if (err != NULL)
				return (err);
			return ("no OpenBSD GPT partition");
		}
	} else {
		start = findopenbsd(ed, &err);
		if (start == (u_int)-1) {
			if (err != NULL)
				return (err);
			return "no OpenBSD MBR partition\n";
		}
	}

	/* Load BSD disklabel */
#ifdef BIOS_DEBUG
	if (debug)
		printf("loading disklabel @ %u\n", start + DOS_LABELSECTOR);
#endif
	/* read disklabel */
	error = efid_io(F_READ, ed, EFI_SECTOBLK(ed, start) + DOS_LABELSECTOR,
	    1, buf);

	if (error)
		return "failed to read disklabel";

	/* Fill in disklabel */
	return (getdisklabel(buf, label));
}

static int
efi_getdisklabel_cd9660(efi_diskinfo_t ed, struct disklabel *label)
{
	uint8_t		 buf[DEV_BSIZE];
	EFI_STATUS	 status;

	status = efid_io(F_READ, ed, 64, 1, buf);
	if (EFI_ERROR(status))
		return -1;
	if (buf[0] != ISO_VD_PRIMARY || bcmp(buf + 1, ISO_STANDARD_ID, 5) != 0)
		return -1;

	/* Create an imaginary disk label */
	label->d_secsize = 2048;
	label->d_ntracks = 1;
	label->d_nsectors = 100;
	label->d_ncylinders = 1;
	label->d_secpercyl = label->d_ntracks * label->d_nsectors;

	strncpy(label->d_typename, "ATAPI CD-ROM", sizeof(label->d_typename));
	label->d_type = DTYPE_ATAPI;

	strncpy(label->d_packname, "fictitious", sizeof(label->d_packname));
	DL_SETDSIZE(label, 100);

	/* 'a' partition covering the "whole" disk */
	DL_SETPOFFSET(&label->d_partitions[0], 0);
	DL_SETPSIZE(&label->d_partitions[0], 100);
	label->d_partitions[0].p_fstype = FS_UNUSED;

	/* The raw partition is special */
	DL_SETPOFFSET(&label->d_partitions[RAW_PART], 0);
	DL_SETPSIZE(&label->d_partitions[RAW_PART], 100);
	label->d_partitions[RAW_PART].p_fstype = FS_UNUSED;

	label->d_npartitions = MAXPARTITIONS;

	label->d_magic = DISKMAGIC;
	label->d_magic2 = DISKMAGIC;
	label->d_checksum = dkcksum(label);

	return (0);
}

int
efiopen(struct open_file *f, ...)
{
#ifdef SOFTRAID
	struct sr_boot_volume *bv;
#endif
	register char *cp, **file;
	dev_t maj, unit, part;
	struct diskinfo *dip;
	int biosdev, devlen;
#if 0
	const char *st;
#endif
	va_list ap;
	char *dev;

	va_start(ap, f);
	cp = *(file = va_arg(ap, char **));
	va_end(ap);

#ifdef EFI_DEBUG
	if (debug)
		printf("%s\n", cp);
#endif

	f->f_devdata = NULL;

	/* Search for device specification. */
	dev = cp;
	if (cp[4] == ':')
		devlen = 2;
	else if (cp[5] == ':')
		devlen = 3;
	else
		return ENOENT;
	cp += devlen;

	/* Get unit. */
	if ('0' <= *cp && *cp <= '9')
		unit = *cp++ - '0';
	else {
		printf("Bad unit number\n");
		return EUNIT;
	}

	/* Get partition. */
	part = DL_PARTNAME2NUM(*cp++);
	if (part == -1) {
		printf("Bad partition\n");
		return EPART;
	}

	/* Get filename. */
	cp++;	/* skip ':' */
	if (*cp != 0)
		*file = cp;
	else
		f->f_flags |= F_RAW;

#ifdef SOFTRAID
	/* Intercept softraid disks. */
	if (strncmp("sr", dev, 2) == 0) {
		/* We only support read-only softraid. */
		f->f_flags |= F_NOWRITE;

		/* Create a fake diskinfo for this softraid volume. */
		SLIST_FOREACH(bv, &sr_volumes, sbv_link)
			if (bv->sbv_unit == unit)
				break;
		if (bv == NULL) {
			printf("Unknown device: sr%d\n", unit);
			return EADAPT;
		}

		if ((bv->sbv_level == 'C' || bv->sbv_level == 0x1C) &&
		    bv->sbv_keys == NULL) {
			if (sr_crypto_unlock_volume(bv) != 0)
				return EPERM;
		}

		if (bv->sbv_diskinfo == NULL) {
			dip = alloc(sizeof(struct diskinfo));
			bzero(dip, sizeof(*dip));
			dip->diskio = efid_diskio;
			dip->strategy = efistrategy;
			bv->sbv_diskinfo = dip;
			dip->sr_vol = bv;
			dip->bios_info.flags |= BDI_BADLABEL;
		}

		dip = bv->sbv_diskinfo;

		if (dip->bios_info.flags & BDI_BADLABEL) {
			/* Attempt to read disklabel. */
			bv->sbv_part = 'c';
			if (sr_getdisklabel(bv, &dip->disklabel))
				return ERDLAB;
			dip->bios_info.flags &= ~BDI_BADLABEL;
			check_hibernate(dip);
		}

		bv->sbv_part = DL_PARTNUM2NAME(part);

		bootdev_dip = dip;
		f->f_devdata = dip;

		return 0;
	}
#endif
	for (maj = 0; maj < nbdevs &&
	    strncmp(dev, bdevs[maj], devlen); maj++);
	if (maj >= nbdevs) {
		printf("Unknown device: ");
		for (cp = *file; *cp != ':'; cp++)
			putchar(*cp);
		putchar('\n');
		return EADAPT;
	}

	biosdev = unit;
	switch (maj) {
	case 0:  /* wd */
	case 4:  /* sd */
	case 17: /* hd */
		biosdev |= 0x80;
		break;
	case 2:  /* fd */
		break;
	case 6:  /* cd */
		biosdev |= 0xe0;
		break;
	default:
		return ENXIO;
	}

	/* Find device */
	dip = dklookup(biosdev);
	if (dip == NULL)
		return ENXIO;
	bootdev_dip = dip;

	/* Fix up bootdev */
	{ dev_t bsd_dev;
		bsd_dev = dip->bios_info.bsd_dev;
		dip->bsddev = MAKEBOOTDEV(B_TYPE(bsd_dev), B_ADAPTOR(bsd_dev),
		    B_CONTROLLER(bsd_dev), unit, part);
		dip->bootdev = MAKEBOOTDEV(B_TYPE(bsd_dev), B_ADAPTOR(bsd_dev),
		    B_CONTROLLER(bsd_dev), B_UNIT(bsd_dev), part);
	}

#if 0
	dip->bios_info.bsd_dev = dip->bootdev;
	bootdev = dip->bootdev;
#endif

#ifdef EFI_DEBUG
	if (debug) {
		printf("BIOS geometry: heads=%u, s/t=%u; EDD=%d\n",
		    dip->bios_info.bios_heads, dip->bios_info.bios_sectors,
		    dip->bios_info.bios_edd);
	}
#endif

#if 0
/*
 * XXX In UEFI, media change can be detected by MediaID
 */
	/* Try for disklabel again (might be removable media) */
	if (dip->bios_info.flags & BDI_BADLABEL) {
		st = efi_getdisklabel(dip->efi_info, &dip->disklabel);
#ifdef EFI_DEBUG
		if (debug && st)
			printf("%s\n", st);
#endif
		if (!st) {
			dip->bios_info.flags &= ~BDI_BADLABEL;
			dip->bios_info.flags |= BDI_GOODLABEL;
		} else
			return ERDLAB;
	}
#endif
	f->f_devdata = dip;

	return 0;
}

int
efistrategy(void *devdata, int rw, daddr_t blk, size_t size, void *buf,
    size_t *rsize)
{
	struct diskinfo *dip = (struct diskinfo *)devdata;
	u_int8_t error = 0;
	size_t nsect;

#ifdef SOFTRAID
	/* Intercept strategy for softraid volumes. */
	if (dip->sr_vol)
		return sr_strategy(dip->sr_vol, rw, blk, size, buf, rsize);
#endif
	nsect = (size + DEV_BSIZE - 1) / DEV_BSIZE;
	blk += DL_SECTOBLK(&dip->disklabel,
	    dip->disklabel.d_partitions[B_PARTITION(dip->bsddev)].p_offset);

	if (blk < 0)
		error = EINVAL;
	else
		error = dip->diskio(rw, dip, blk, nsect, buf);

#ifdef EFI_DEBUG
	if (debug) {
		if (error != 0)
			printf("=0x%x(%s)", error, error);
		putchar('\n');
	}
#endif
	if (rsize != NULL)
		*rsize = nsect * DEV_BSIZE;

	return (error);
}

int
eficlose(struct open_file *f)
{
	f->f_devdata = NULL;

	return 0;
}

int
efiioctl(struct open_file *f, u_long cmd, void *data)
{

	return 0;
}

void
efi_dump_diskinfo(void)
{
	efi_diskinfo_t	 ed;
	struct diskinfo	*dip;
	bios_diskinfo_t *bdi;
	uint64_t	 siz;
	const char	*sizu;

	printf("Disk\tBlkSiz\tIoAlign\tSize\tFlags\tChecksum\n");
	TAILQ_FOREACH(dip, &disklist, list) {
		bdi = &dip->bios_info;
		ed = dip->efi_info;

		siz = (ed->blkio->Media->LastBlock + 1) *
		    ed->blkio->Media->BlockSize;
		siz /= 1024 * 1024;
		if (siz < 10000)
			sizu = "MB";
		else {
			siz /= 1024;
			sizu = "GB";
		}

		printf("%cd%d\t%u\t%u\t%u%s\t0x%x\t0x%x\t%s\n",
		    (B_TYPE(bdi->bsd_dev) == 6)? 'c' : 'h',
		    (bdi->bios_number & 0x1f),
		    ed->blkio->Media->BlockSize,
		    ed->blkio->Media->IoAlign, (unsigned)siz, sizu,
		    bdi->flags, bdi->checksum,
		    (ed->blkio->Media->RemovableMedia)? "Removable" : "");
	}
}
