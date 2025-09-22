/*	$OpenBSD: efidev.c,v 1.13 2023/10/26 14:08:48 jsg Exp $	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
 * Copyright (c) 2016 Mark Kettenis
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

#include <efi.h>

extern EFI_BOOT_SERVICES *BS;

extern int debug;

#include "disk.h"
#include "efidev.h"

#define EFI_BLKSPERSEC(_ed)	((_ed)->blkio->Media->BlockSize / DEV_BSIZE)
#define EFI_SECTOBLK(_ed, _n)	((_n) * EFI_BLKSPERSEC(_ed))

static EFI_STATUS
		 efid_io(int, efi_diskinfo_t, u_int, int, void *);
static int	 efid_diskio(int, struct diskinfo *, u_int, int, void *);
const char *	 efi_getdisklabel(efi_diskinfo_t, struct disklabel *);
static int	 efi_getdisklabel_cd9660(efi_diskinfo_t, struct disklabel *);
static u_int	 findopenbsd(efi_diskinfo_t, const char **);
static u_int	 findopenbsd_gpt(efi_diskinfo_t, const char **);
static int	 gpt_chk_mbr(struct dos_partition *, u_int64_t);

void
efid_init(struct diskinfo *dip, void *handle)
{
	EFI_BLOCK_IO		*blkio = handle;

	memset(dip, 0, sizeof(struct diskinfo));
	dip->ed.blkio = blkio;
	dip->ed.mediaid = blkio->Media->MediaId;
	dip->diskio = efid_diskio;
	dip->strategy = efistrategy;

	if (efi_getdisklabel(&dip->ed, &dip->disklabel) == NULL)
		dip->flags |= DISKINFO_FLAG_GOODLABEL;
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

	status = efid_io(rw, &dip->ed, off, nsect, buf);

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
	struct diskinfo *dip = NULL;
	va_list ap;
	u_int unit, part;
	int i = 0;

	va_start(ap, f);
	unit = va_arg(ap, u_int);
	part = va_arg(ap, u_int);
	va_end(ap);

	if (part >= MAXPARTITIONS)
		return (ENXIO);

	TAILQ_FOREACH(dip, &disklist, list) {
		if (i == unit)
			break;
		i++;
	}

	if (dip == NULL)
		return (ENXIO);

	if ((dip->flags & DISKINFO_FLAG_GOODLABEL) == 0)
		return (ENXIO);

	dip->part = part;
	bootdev_dip = dip;
	f->f_devdata = dip;

	return 0;
}

int
efistrategy(void *devdata, int rw, daddr_t blk, size_t size, void *buf,
    size_t *rsize)
{
	struct diskinfo *dip = (struct diskinfo *)devdata;
	int error = 0;
	size_t nsect;

	nsect = (size + DEV_BSIZE - 1) / DEV_BSIZE;
	blk += DL_SECTOBLK(&dip->disklabel,
	    dip->disklabel.d_partitions[dip->part].p_offset);

	if (blk < 0)
		error = EINVAL;
	else
		error = efid_diskio(rw, dip, blk, nsect, buf);

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

/*
 * load a file from the EFI System Partition
 */

static EFI_GUID lip_guid = LOADED_IMAGE_PROTOCOL;
static EFI_GUID sfsp_guid = SIMPLE_FILE_SYSTEM_PROTOCOL;
static EFI_GUID fi_guid = EFI_FILE_INFO_ID;

int
esp_open(char *path, struct open_file *f)
{
	extern EFI_HANDLE IH;
	extern EFI_BOOT_SERVICES *BS;

	EFI_LOADED_IMAGE *li = NULL;
	EFI_FILE_IO_INTERFACE *ESPVolume;
	CHAR16 *fname;
	EFI_FILE_HANDLE VH, FH;
	UINTN pathlen, i;
	EFI_STATUS status;

	if (strcmp("esp", f->f_dev->dv_name) != 0)
		return ENXIO;

	if (IH == NULL)
		return ENXIO;

	/* get the loaded image protocol interface */
	status = BS->HandleProtocol(IH, &lip_guid, (void **)&li);
	if (status != EFI_SUCCESS)
		return ENXIO;

	/* get a fs handle */
	status = BS->HandleProtocol(li->DeviceHandle, &sfsp_guid,
	    (void *)&ESPVolume);
	if (status != EFI_SUCCESS)
		return ENXIO;

	status = ESPVolume->OpenVolume(ESPVolume, &VH);
	if (status != EFI_SUCCESS)
		return ENOENT;

	pathlen = strlen(path) + 1;
	fname = alloc(pathlen * sizeof(*fname));
	if (fname == NULL)
		return ENOMEM;

	/* No AsciiStrToUnicodeStrS */
	for (i = 0; i < pathlen; i++)
		fname[i] = path[i];

	status = VH->Open(VH, &FH, fname, EFI_FILE_MODE_READ,
	    EFI_FILE_READ_ONLY /*| EFI_FILE_HIDDEN*/ | EFI_FILE_SYSTEM);
	free(fname, pathlen * sizeof(*fname));
	if (status != EFI_SUCCESS)
		return ENOENT;

	f->f_fsdata = FH;
	return (0);
}

int
esp_close(struct open_file *f)
{
	EFI_FILE_HANDLE FH = f->f_fsdata;
	FH->Close(FH);
	return 0;
}

int
esp_read(struct open_file *f, void *addr, size_t size, size_t *resid)
{
	EFI_FILE_HANDLE FH = f->f_fsdata;
	UINT64 readlen = size;
	EFI_STATUS status;

	status = FH->Read(FH, &readlen, addr);
	if (status != EFI_SUCCESS)
		return (EIO);

	*resid = size - readlen;
	return (0);
}

int
esp_write(struct open_file *f, void *start, size_t size, size_t *resid)
{
	return (EROFS);
}

off_t
esp_seek(struct open_file *f, off_t offset, int where)
{
	EFI_FILE_HANDLE FH = f->f_fsdata;
	UINT64 position;
	EFI_STATUS status;

	switch(where) {
	case SEEK_CUR:
		status = FH->GetPosition(FH, &position);
		if (status != EFI_SUCCESS) {
			errno = EIO;
			return ((off_t)-1);
		}

		position += offset;
		break;
	case SEEK_SET:
		position = offset;
		break;
	case SEEK_END:
		position = 0xFFFFFFFFFFFFFFFF;
		break;
	default:
		errno = EINVAL;
		return ((off_t)-1);
	}

	status = FH->SetPosition(FH, position);
	if (status != EFI_SUCCESS) {
		errno = EIO;
		return ((off_t)-1);
	}

	return (0);
}

int
esp_stat(struct open_file *f, struct stat *sb)
{

	EFI_FILE_HANDLE FH = f->f_fsdata;
	EFI_FILE_INFO fi;
	EFI_FILE_INFO *fip = &fi;
	UINTN filen = sizeof(fi);
	EFI_STATUS status;
	ssize_t rv = -1;

	sb->st_mode = 0444;
	sb->st_nlink = 1;
	sb->st_uid = 0;
	sb->st_gid = 0;

	status = FH->GetInfo(FH, &fi_guid, &filen, fip);
	switch (status) {
	case EFI_SUCCESS:
		sb->st_size = fip->FileSize;
		return (0);
	case EFI_BUFFER_TOO_SMALL:
		break;
	default:
		return (EIO);
	}

	fip = alloc(filen);
	if (fip == NULL)
		return (ENOMEM);

	status = FH->GetInfo(FH, &fi_guid, &filen, fip);
	if (status != EFI_SUCCESS)
		goto done;

	sb->st_size = fip->FileSize;

done:
	free(fip, filen);
	return (rv);
}

int
esp_readdir(struct open_file *f, char *name)
{
	return EOPNOTSUPP;
}

int
espopen(struct open_file *f, ...)
{
        u_int unit;
        va_list ap;

        va_start(ap, f);
        unit = va_arg(ap, u_int);
        va_end(ap);

        if (unit != 0)
                return 1;

        return 0;
}

int
espclose(struct open_file *f)
{
	return 0;
}

int
espioctl(struct open_file *f, u_long cmd, void *data)
{
        return EOPNOTSUPP;
}

int
espstrategy(void *devdata, int rw, daddr_t blk, size_t size, void *buf,
    size_t *rsize)
{
	return EOPNOTSUPP;
}
