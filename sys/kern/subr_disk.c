/*	$OpenBSD: subr_disk.c,v 1.282 2025/09/17 18:54:49 deraadt Exp $	*/
/*	$NetBSD: subr_disk.c,v 1.17 1996/03/16 23:17:08 christos Exp $	*/

/*
 * Copyright (c) 1995 Jason R. Thorpe.  All rights reserved.
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ufs_disksubr.c	8.5 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/buf.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/time.h>
#include <sys/disklabel.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/reboot.h>
#include <sys/dkio.h>
#include <sys/vnode.h>
#include <sys/task.h>
#include <sys/stdint.h>

#include <sys/socket.h>

#include <net/if.h>

#include <dev/cons.h>

#include <lib/libz/zlib.h>

#include "softraid.h"

#ifdef DEBUG
#define DPRINTF(x...)	printf(x)
#else
#define DPRINTF(x...)
#endif

/*
 * A global list of all disks attached to the system.  May grow or
 * shrink over time.
 */
struct	disklist_head disklist;	/* TAILQ_HEAD */
int	disk_count;		/* number of drives in global disklist */
int	disk_change;		/* set if a disk has been attached/detached
				 * since last we looked at this variable. This
				 * is reset by hw_sysctl()
				 */

#define DUID_SIZE 8

u_char	bootduid[DUID_SIZE];	/* DUID of boot disk. */
u_char	rootduid[DUID_SIZE];	/* DUID of root disk. */

struct device *rootdv;

/* softraid callback, do not use! */
void (*softraid_disk_attach)(struct disk *, int);

void sr_map_root(void);

struct disk_attach_task {
	struct task task;
	struct disk *dk;
};

void disk_attach_callback(void *);

int spoofgpt(struct buf *, void (*)(struct buf *), const uint8_t *,
    struct disklabel *, daddr_t *);
void spoofmbr(struct buf *, void (*)(struct buf *), const uint8_t *,
    struct disklabel *, daddr_t *);
void spooffat(const uint8_t *, struct disklabel *, daddr_t *);

int gpt_chk_mbr(struct dos_partition *, uint64_t);
int gpt_get_hdr(struct buf *, void (*)(struct buf *), struct disklabel *,
    uint64_t, struct gpt_header *);
int gpt_get_parts(struct buf *, void (*)(struct buf *),
    struct disklabel *, const struct gpt_header *, struct gpt_partition **);
int gpt_get_fstype(const struct uuid *);
int mbr_get_fstype(const uint8_t);

int duid_equal(u_char *, u_char *);

/*
 * Compute checksum for disk label.
 */
u_int
dkcksum(struct disklabel *lp)
{
	u_int16_t *start, *end;
	u_int16_t sum = 0;

	start = (u_int16_t *)lp;
	end = (u_int16_t *)&lp->d_partitions[lp->d_npartitions];
	while (start < end)
		sum ^= *start++;
	return (sum);
}

int
initdisklabel(struct disklabel *lp)
{
	int i;

	/* minimal requirements for archetypal disk label */
	if (lp->d_secsize < DEV_BSIZE)
		lp->d_secsize = DEV_BSIZE;
	if (DL_GETDSIZE(lp) == 0)
		DL_SETDSIZE(lp, MAXDISKSIZE);
	if (lp->d_secpercyl == 0)
		return (ERANGE);
	lp->d_npartitions = MAXPARTITIONS;
	for (i = 0; i < RAW_PART; i++) {
		DL_SETPSIZE(&lp->d_partitions[i], 0);
		DL_SETPOFFSET(&lp->d_partitions[i], 0);
	}
	if (DL_GETPSIZE(&lp->d_partitions[RAW_PART]) == 0)
		DL_SETPSIZE(&lp->d_partitions[RAW_PART], DL_GETDSIZE(lp));
	DL_SETPOFFSET(&lp->d_partitions[RAW_PART], 0);
	DL_SETBSTART(lp, 0);
	DL_SETBEND(lp, DL_GETDSIZE(lp));
	lp->d_version = 1;
	return (0);
}

/*
 * Check an incoming block to make sure it is a disklabel, convert it to
 * a newer version if needed, etc etc.
 */
int
checkdisklabel(dev_t dev, void *rlp, struct disklabel *lp, u_int64_t boundstart,
    u_int64_t boundend)
{
	struct disklabel *dlp = rlp;
	struct partition *pp;
	u_int64_t disksize;
	int error = 0;
	int i;

	if (dlp->d_magic != DISKMAGIC || dlp->d_magic2 != DISKMAGIC)
		error = ENOENT;	/* no disk label */
	else if (dlp->d_npartitions > MAXPARTITIONS)
		error = E2BIG;	/* too many partitions */
	else if (dlp->d_secpercyl == 0)
		error = EINVAL;	/* invalid label */
	else if (dlp->d_secsize == 0)
		error = ENOSPC;	/* disk too small */
	else if (dkcksum(dlp) != 0)
		error = EINVAL;	/* incorrect checksum */
	else if (dlp->d_version == 0)
		error = EINVAL;	/* version too old to understand */

	if (error) {
		u_int16_t *start, *end, sum = 0;

		/* If it is byte-swapped, attempt to convert it */
		if (swap32(dlp->d_magic) != DISKMAGIC ||
		    swap32(dlp->d_magic2) != DISKMAGIC ||
		    swap16(dlp->d_npartitions) > MAXPARTITIONS)
			return (error);

		/*
		 * Need a byte-swap aware dkcksum variant
		 * inlined, because dkcksum uses a sub-field
		 */
		start = (u_int16_t *)dlp;
		end = (u_int16_t *)&dlp->d_partitions[
		    swap16(dlp->d_npartitions)];
		while (start < end)
			sum ^= *start++;
		if (sum != 0)
			return (error);

		dlp->d_magic = swap32(dlp->d_magic);
		dlp->d_type = swap16(dlp->d_type);

		/* d_typename and d_packname are strings */

		dlp->d_secsize = swap32(dlp->d_secsize);
		dlp->d_nsectors = swap32(dlp->d_nsectors);
		dlp->d_ntracks = swap32(dlp->d_ntracks);
		dlp->d_ncylinders = swap32(dlp->d_ncylinders);
		dlp->d_secpercyl = swap32(dlp->d_secpercyl);
		dlp->d_secperunit = swap32(dlp->d_secperunit);

		/* d_uid is a string */

		dlp->d_acylinders = swap32(dlp->d_acylinders);

		dlp->d_flags = swap32(dlp->d_flags);

		dlp->d_secperunith = swap16(dlp->d_secperunith);
		dlp->d_version = swap16(dlp->d_version);

		for (i = 0; i < NSPARE; i++)
			dlp->d_spare[i] = swap32(dlp->d_spare[i]);

		dlp->d_magic2 = swap32(dlp->d_magic2);

		dlp->d_npartitions = swap16(dlp->d_npartitions);

		for (i = 0; i < MAXPARTITIONS; i++) {
			pp = &dlp->d_partitions[i];
			pp->p_size = swap32(pp->p_size);
			pp->p_offset = swap32(pp->p_offset);
			pp->p_offseth = swap16(pp->p_offseth);
			pp->p_sizeh = swap16(pp->p_sizeh);
			pp->p_cpg = swap16(pp->p_cpg);
		}

		dlp->d_checksum = 0;
		dlp->d_checksum = dkcksum(dlp);
		error = 0;
	}

	/* XXX should verify lots of other fields and whine a lot */

	/* Initial passed in lp contains the real disk size. */
	disksize = DL_GETDSIZE(lp);

	if (lp != dlp)
		*lp = *dlp;

#ifdef DEBUG
	if (DL_GETDSIZE(lp) != disksize)
		printf("on-disk disklabel has incorrect disksize (%llu)\n",
		    DL_GETDSIZE(lp));
	if (DL_GETPSIZE(&lp->d_partitions[RAW_PART]) != disksize)
		printf("on-disk disklabel RAW_PART has incorrect size (%llu)\n",
		    DL_GETPSIZE(&lp->d_partitions[RAW_PART]));
	if (DL_GETPOFFSET(&lp->d_partitions[RAW_PART]) != 0)
		printf("on-disk disklabel RAW_PART offset != 0 (%llu)\n",
		    DL_GETPOFFSET(&lp->d_partitions[RAW_PART]));
#endif
	DL_SETDSIZE(lp, disksize);
	DL_SETPSIZE(&lp->d_partitions[RAW_PART], disksize);
	DL_SETPOFFSET(&lp->d_partitions[RAW_PART], 0);
	DL_SETBSTART(lp, boundstart);
	DL_SETBEND(lp, boundend < DL_GETDSIZE(lp) ? boundend : DL_GETDSIZE(lp));

	lp->d_checksum = 0;
	lp->d_checksum = dkcksum(lp);
	return (0);
}

/*
 * Read a disk sector.
 */
int
readdisksector(struct buf *bp, void (*strat)(struct buf *),
    struct disklabel *lp, u_int64_t sector)
{
	bp->b_blkno = DL_SECTOBLK(lp, sector);
	bp->b_bcount = lp->d_secsize;
	bp->b_error = 0;
	CLR(bp->b_flags, B_READ | B_WRITE | B_DONE | B_ERROR);
	SET(bp->b_flags, B_BUSY | B_READ | B_RAW);

	(*strat)(bp);

	return (biowait(bp));
}

int
readdoslabel(struct buf *bp, void (*strat)(struct buf *), struct disklabel *lp,
    daddr_t *partoffp, int spoofonly)
{
	uint8_t			 dosbb[DEV_BSIZE];
	struct disklabel	*nlp, *rlp;
	daddr_t			 partoff;
	int			 error;

#ifdef DEBUG
	char			 devname[32];
	const char		*blkname;

	blkname = findblkname(major(bp->b_dev));
	if (blkname == NULL)
		 blkname = findblkname(major(chrtoblk(bp->b_dev)));
	if (blkname == NULL)
		snprintf(devname, sizeof(devname), "<%d, %d>", major(bp->b_dev),
		    minor(bp->b_dev));
	else
		snprintf(devname, sizeof(devname), "%s%d", blkname,
		    DISKUNIT(bp->b_dev));

	printf("readdoslabel enter: %s, spoofonly %d, partoffp %sNULL\n",
	    devname, spoofonly, (partoffp == NULL) ? "" : "not ");
#endif /* DEBUG */

	error = readdisksector(bp, strat, lp, DOSBBSECTOR);
	if (error) {
		DPRINTF("readdoslabel return: %s, %d -- lp unchanged, "
		    "DOSBBSECTOR read error\n", devname, error);
		return error;
	}
	memcpy(dosbb, bp->b_data, sizeof(dosbb));

	nlp = malloc(sizeof(*nlp), M_DEVBUF, M_WAITOK);
	*nlp = *lp;
	memset(nlp->d_partitions, 0, sizeof(nlp->d_partitions));
	nlp->d_partitions[RAW_PART] = lp->d_partitions[RAW_PART];
	nlp->d_magic = 0;

	error = spoofgpt(bp, strat, dosbb, nlp, &partoff);
	if (error) {
		free(nlp, M_DEVBUF, sizeof(*nlp));
		return error;
	}
	if (nlp->d_magic != DISKMAGIC)
		spoofmbr(bp, strat, dosbb, nlp, &partoff);
	if (nlp->d_magic != DISKMAGIC)
		spooffat(dosbb, nlp, &partoff);
	if (nlp->d_magic != DISKMAGIC) {
		DPRINTF("readdoslabel: N/A -- label partition @ "
		    "daddr_t 0 (default)\n");
		partoff = 0;
	}

	if (partoffp != NULL) {
		/*
		 * If a non-zero value is returned writedisklabel() exits with
		 * EIO. If 0 is returned the label sector is read from disk and
		 * lp is copied into it. So leave lp alone!
		 */
		if (partoff == -1) {
			DPRINTF("readdoslabel return: %s, ENXIO, lp "
			    "unchanged, *partoffp unchanged\n", devname);
			free(nlp, M_DEVBUF, sizeof(*nlp));
			return ENXIO;
		}
		*partoffp = partoff;
		DPRINTF("readdoslabel return: %s, 0, lp unchanged, "
		    "*partoffp set to %lld\n", devname, *partoffp);
		free(nlp, M_DEVBUF, sizeof(*nlp));
		return 0;
	}

	nlp->d_magic = lp->d_magic;
	*lp = *nlp;
	free(nlp, M_DEVBUF, sizeof(*nlp));

	lp->d_checksum = 0;
	lp->d_checksum = dkcksum(lp);

	if (spoofonly || partoff == -1) {
		DPRINTF("readdoslabel return: %s, 0, lp spoofed\n",
		    devname);
		return 0;
	}

	partoff += DOS_LABELSECTOR;
	error = readdisksector(bp, strat, lp, DL_BLKTOSEC(lp, partoff));
	if (error) {
		DPRINTF("readdoslabel return: %s, %d, lp read failed\n",
		    devname, error);
		return bp->b_error;
	}

	rlp = (struct disklabel *)(bp->b_data + DL_BLKOFFSET(lp, partoff));
	error = checkdisklabel(bp->b_dev, rlp, lp, DL_GETBSTART(rlp),
	    DL_GETBEND(rlp));

	DPRINTF("readdoslabel return: %s, %d, checkdisklabel() of daddr_t "
	    "%lld %s\n", devname, error, partoff, error ? "failed" : "ok");

	return error;
}

/*
 * Return the index into dp[] of the EFI GPT (0xEE) partition, or -1 if no such
 * partition exists.
 *
 * Copied into sbin/fdisk/mbr.c.
 */
int
gpt_chk_mbr(struct dos_partition *dp, uint64_t dsize)
{
	struct dos_partition *dp2;
	int efi, eficnt, found, i;
	uint32_t psize;

	found = efi = eficnt = 0;
	for (dp2 = dp, i = 0; i < NDOSPART; i++, dp2++) {
		if (dp2->dp_typ == DOSPTYP_UNUSED)
			continue;
		found++;
		if (dp2->dp_typ != DOSPTYP_EFI)
			continue;
		if (letoh32(dp2->dp_start) != GPTSECTOR)
			continue;
		psize = letoh32(dp2->dp_size);
		if (psize <= (dsize - GPTSECTOR) || psize == UINT32_MAX) {
			efi = i;
			eficnt++;
		}
	}
	if (found == 1 && eficnt == 1)
		return (efi);

	return (-1);
}

int
gpt_get_hdr(struct buf *bp, void (*strat)(struct buf *), struct disklabel *lp,
    uint64_t sector, struct gpt_header *gh)
{
	struct gpt_header	ngh;
	int			error;
	uint64_t		lbaend, lbastart;
	uint32_t		csum;
	uint32_t		size, partsize;


	error = readdisksector(bp, strat, lp, sector);
	if (error)
		return error;

	memcpy(&ngh, bp->b_data, sizeof(ngh));

	size = letoh32(ngh.gh_size);
	partsize = letoh32(ngh.gh_part_size);
	lbaend = letoh64(ngh.gh_lba_end);
	lbastart = letoh64(ngh.gh_lba_start);

	csum = ngh.gh_csum;
	ngh.gh_csum = 0;
	ngh.gh_csum = htole32(crc32(0, (unsigned char *)&ngh, GPTMINHDRSIZE));

	if (letoh64(ngh.gh_sig) == GPTSIGNATURE &&
	    letoh32(ngh.gh_rev) == GPTREVISION &&
	    size == GPTMINHDRSIZE && lbastart <= lbaend &&
	    partsize == GPTMINPARTSIZE && lp->d_secsize % partsize == 0 &&
	    csum == ngh.gh_csum)
		*gh = ngh;
	else
		memset(gh, 0, sizeof(*gh));

	return 0;
}

int
gpt_get_parts(struct buf *bp, void (*strat)(struct buf *), struct disklabel *lp,
    const struct gpt_header *gh, struct gpt_partition **gp)
{
	uint8_t			*ngp;
	int			 error, i;
	uint64_t		 bytes, partlba, sectors;
	uint32_t		 partnum, partsize, partcsum;

	partlba = letoh64(gh->gh_part_lba);
	partnum = letoh32(gh->gh_part_num);
	partsize = letoh32(gh->gh_part_size);

	sectors = ((uint64_t)partnum * partsize + lp->d_secsize - 1) /
	    lp->d_secsize;

	ngp = mallocarray(sectors, lp->d_secsize, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ngp == NULL) {
		*gp = NULL;
		return ENOMEM;
	}
	bytes = sectors * lp->d_secsize;

	for (i = 0; i < sectors; i++) {
		error = readdisksector(bp, strat, lp, partlba + i);
		if (error) {
			free(ngp, M_DEVBUF, bytes);
			*gp = NULL;
			return error;
		}
		memcpy(ngp + i * lp->d_secsize, bp->b_data, lp->d_secsize);
	}

	partcsum = htole32(crc32(0, ngp, partnum * partsize));
	if (partcsum != gh->gh_part_csum) {
		DPRINTF("invalid %s GPT partition array @ %llu\n",
		    (letoh64(gh->gh_lba_self) == GPTSECTOR) ? "Primary" :
		    "Secondary", partlba);
		free(ngp, M_DEVBUF, bytes);
		*gp = NULL;
	} else {
		*gp = (struct gpt_partition *)ngp;
	}

	return 0;
}

/* LE format! */
#define GPT_UUID_UNUSED \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define GPT_UUID_MICROSOFT_BASIC_DATA \
    { 0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44, \
      0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7 }
#define GPT_UUID_CHROMEOS_ROOTFS \
    { 0x02, 0xe2, 0xb8, 0x3c, 0x7e, 0x3b, 0xdd, 0x47, \
      0x8a, 0x3c, 0x7f, 0xf2, 0xa1, 0x3c, 0xfc, 0xec }
#define GPT_UUID_LINUX_FILES \
    { 0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47, \
      0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4 }
#define GPT_UUID_MAC_OS_X_HFS \
    { 0x00, 0x53, 0x46, 0x48, 0x00, 0x00, 0xaa, 0x11,\
      0xaa, 0x11, 0x00, 0x30, 0x65, 0x43, 0xec, 0xac }
#define GPT_UUID_BIOS_BOOT \
    { 0x48, 0x61, 0x68, 0x21, 0x49, 0x64, 0x6f, 0x6e, \
      0x74, 0x4e, 0x65, 0x65, 0x64, 0x45, 0x46, 0x49 }

/* XXX Temporary LE versions needed until MI GPT boot code is adjusted. */
#define GPT_LEUUID_EFI_SYSTEM \
    { 0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, \
      0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b }
#define GPT_LEUUID_OPENBSD \
    { 0xa0, 0xc7, 0x4c, 0x82, 0xa8, 0x36, 0xe3, 0x11, \
      0x89, 0x0a, 0x95, 0x25, 0x19, 0xad, 0x3f, 0x61 }

int
gpt_get_fstype(const struct uuid *uuid_part)
{
	unsigned int		i;
	struct partfs {
		uint8_t gptype[16];
		int fstype;
	} knownfs[] = {
		{ GPT_UUID_UNUSED,		FS_UNUSED },
		{ GPT_LEUUID_OPENBSD,		FS_BSDFFS },
		{ GPT_UUID_MICROSOFT_BASIC_DATA,FS_MSDOS  },
		{ GPT_UUID_CHROMEOS_ROOTFS,	FS_EXT2FS },
		{ GPT_UUID_LINUX_FILES,		FS_EXT2FS },
		{ GPT_UUID_MAC_OS_X_HFS,	FS_HFS	  },
		{ GPT_LEUUID_EFI_SYSTEM,	FS_MSDOS  },
		{ GPT_UUID_BIOS_BOOT,		FS_BOOT   }
	};

	for (i = 0; i < nitems(knownfs); i++) {
		if (!memcmp(uuid_part, knownfs[i].gptype, sizeof(struct uuid)))
		    return knownfs[i].fstype;
	}

	return FS_OTHER;
}

int
spoofgpt(struct buf *bp, void (*strat)(struct buf *), const uint8_t *dosbb,
    struct disklabel *lp, daddr_t *partoffp)
{
	struct dos_partition	 dp[NDOSPART];
	struct gpt_header	 gh;
	struct gpt_partition	*gp;
	struct partition	*pp;
	uint64_t		 lbaend, lbastart, labelsec;
	uint64_t		 gpbytes, end, start;
	daddr_t			 partoff;
	unsigned int		 i, n;
	int			 error, fstype, obsdfound;
	uint32_t		 partnum;
	uint16_t		 sig;

	gp = NULL;
	gpbytes = 0;

	memcpy(dp, dosbb + DOSPARTOFF, sizeof(dp));
	memcpy(&sig, dosbb + DOSMBR_SIGNATURE_OFF, sizeof(sig));

	if (letoh16(sig) != DOSMBR_SIGNATURE ||
	    gpt_chk_mbr(dp, DL_GETDSIZE(lp)) == -1)
		return 0;

	error = gpt_get_hdr(bp, strat, lp, GPTSECTOR, &gh);
	if (error == 0 && letoh64(gh.gh_sig) == GPTSIGNATURE)
		error = gpt_get_parts(bp, strat, lp, &gh, &gp);

	if (error || letoh64(gh.gh_sig) != GPTSIGNATURE || gp == NULL) {
		error = gpt_get_hdr(bp, strat, lp, DL_GETDSIZE(lp) - 1, &gh);
		if (error == 0 && letoh64(gh.gh_sig) == GPTSIGNATURE)
			error = gpt_get_parts(bp, strat, lp, &gh, &gp);
	}

	if (error)
		return error;
	if (gp == NULL)
		return ENXIO;

	lbastart = letoh64(gh.gh_lba_start);
	lbaend = letoh64(gh.gh_lba_end);
	partnum = letoh32(gh.gh_part_num);

	n = 'i' - 'a';	/* Start spoofing at 'i', a.k.a. 8. */

	DL_SETBSTART(lp, lbastart);
	DL_SETBEND(lp, lbaend + 1);
	partoff = DL_SECTOBLK(lp, lbastart);
	obsdfound = 0;
	for (i = 0; i < partnum; i++) {
		fstype = gpt_get_fstype(&gp[i].gp_type);
		if (fstype == FS_UNUSED)
			continue;
		if (fstype == FS_OTHER) {
			DPRINTF("spoofgpt: Skipping partition %u "
			    "(unknown filesystem)\n", i);
			continue;
		}

		start = letoh64(gp[i].gp_lba_start);
		if (start > lbaend || start < lbastart)
			continue;

		end = letoh64(gp[i].gp_lba_end);
		if (start > end)
			continue;

		if (obsdfound && fstype == FS_BSDFFS)
			continue;

		if (fstype == FS_BSDFFS) {
			obsdfound = 1;
			partoff = DL_SECTOBLK(lp, start);
			labelsec = DL_BLKTOSEC(lp, partoff + DOS_LABELSECTOR);
			if (labelsec > ((end < lbaend) ? end : lbaend))
				partoff = -1;
			DL_SETBSTART(lp, start);
			DL_SETBEND(lp, end + 1);
			continue;
		}

		if (partoff != -1) {
			labelsec = DL_BLKTOSEC(lp, partoff + DOS_LABELSECTOR);
			if (labelsec >= start && labelsec <= end)
				partoff = -1;
		}

		if (n < MAXPARTITIONS && end <= lbaend) {
			pp = &lp->d_partitions[n];
			n++;
			pp->p_fstype = fstype;
			DL_SETPOFFSET(pp, start);
			DL_SETPSIZE(pp, end - start + 1);
		}
	}

	lp->d_magic = DISKMAGIC;
	*partoffp = partoff;
	free(gp, M_DEVBUF, gpbytes);

#ifdef DEBUG
	printf("readdoslabel: GPT -- ");
	if (partoff == -1)
		printf("no label partition\n");
	else if (obsdfound == 0)
	    printf("label partition @ daddr_t %lld (free space)\n", partoff);
	else
	    printf("label partition @ daddr_t %lld (A6)\n", partoff);
#endif	/* DEBUG */

	return 0;
}

int
mbr_get_fstype(const uint8_t dp_typ)
{
	switch (dp_typ) {
	case DOSPTYP_OPENBSD:
		return FS_BSDFFS;
	case DOSPTYP_UNUSED:
		return FS_UNUSED;
	case DOSPTYP_LINUX:
		return FS_EXT2FS;
	case DOSPTYP_NTFS:
		return FS_NTFS;
	case DOSPTYP_EFISYS:
	case DOSPTYP_FAT12:
	case DOSPTYP_FAT16S:
	case DOSPTYP_FAT16B:
	case DOSPTYP_FAT16L:
	case DOSPTYP_FAT32:
	case DOSPTYP_FAT32L:
		return FS_MSDOS;
	case DOSPTYP_EFI:
	case DOSPTYP_EXTEND:
	case DOSPTYP_EXTENDL:
	default:
		return FS_OTHER;
	}
}

void
spoofmbr(struct buf *bp, void (*strat)(struct buf *), const uint8_t *dosbb,
    struct disklabel *lp, daddr_t *partoffp)
{
	struct dos_partition	 dp[NDOSPART];
	struct partition	*pp;
	uint64_t		 sector = DOSBBSECTOR;
	uint64_t		 start, end;
	daddr_t			 labeloff, partoff;
	unsigned int		 i, n, parts;
	int			 wander = 1, ebr = 0;
	int			 error, obsdfound;
	uint32_t		 extoff = 0;
	uint16_t		 sig;
	uint8_t			 fstype;

	memcpy(&sig, dosbb + DOSMBR_SIGNATURE_OFF, sizeof(sig));
	if (letoh16(sig) != DOSMBR_SIGNATURE)
		return;
	memcpy(dp, dosbb + DOSPARTOFF, sizeof(dp));

	obsdfound = 0;
	partoff = 0;
	parts = 0;
	n = 'i' - 'a';
	while (wander && ebr < DOS_MAXEBR) {
		ebr++;
		wander = 0;
		if (sector < extoff)
			sector = extoff;

		error = 0;
		if (sector != DOSBBSECTOR) {
			error = readdisksector(bp, strat, lp, sector);
			if (error)
				break;
			memcpy(&sig, bp->b_data + DOSMBR_SIGNATURE_OFF,
			    sizeof(sig));
			if (letoh16(sig) != DOSMBR_SIGNATURE)
				break;
			memcpy(dp, bp->b_data + DOSPARTOFF, sizeof(dp));
		}

		for (i = 0; i < NDOSPART; i++) {
			if (letoh32(dp[i].dp_size) == 0)
				continue;
			if (obsdfound && dp[i].dp_typ == DOSPTYP_OPENBSD)
				continue;

			if (dp[i].dp_typ != DOSPTYP_OPENBSD) {
				if (letoh32(dp[i].dp_start) > DL_GETDSIZE(lp))
					continue;
				if (letoh32(dp[i].dp_size) > DL_GETDSIZE(lp))
					continue;
			}

			start = sector + letoh32(dp[i].dp_start);
			end = start + letoh32(dp[i].dp_size);

			parts++;
			if (obsdfound == 0) {
				labeloff = partoff + DOS_LABELSECTOR;
				if (labeloff >= DL_SECTOBLK(lp, start) &&
				    labeloff < DL_SECTOBLK(lp, end))
					partoff = -1;
			}

			switch (dp[i].dp_typ) {
			case DOSPTYP_OPENBSD:
				obsdfound = 1;
				partoff = DL_SECTOBLK(lp, start);
				labeloff = partoff + DOS_LABELSECTOR;
				if (labeloff >= DL_SECTOBLK(lp, end))
					partoff = -1;
				DL_SETBSTART(lp, start);
				DL_SETBEND(lp, end);
				continue;
			case DOSPTYP_EFI:
				continue;
			case DOSPTYP_EXTEND:
			case DOSPTYP_EXTENDL:
				sector = start + extoff;
				if (extoff == 0) {
					extoff = start;
					sector = 0;
				}
				wander = 1;
				continue;
			default:
				break;
			}

			fstype = mbr_get_fstype(dp[i].dp_typ);
			if (n < MAXPARTITIONS) {
				pp = &lp->d_partitions[n++];
				pp->p_fstype = fstype;
				if (start)
					DL_SETPOFFSET(pp, start);
				DL_SETPSIZE(pp, end - start);
			}
		}
	}

	if (parts > 0) {
		lp->d_magic = DISKMAGIC;
		*partoffp = partoff;
#ifdef DEBUG
	printf("readdoslabel: MBR -- ");
	if (partoff == -1)
		printf("no label partition\n");
	else if (obsdfound == 0)
	    printf("label partition @ daddr_t %lld (free space)\n", partoff);
	else
	    printf("label partition @ daddr_t %lld (A6)\n", partoff);
#endif	/* DEBUG */
	}
}

void
spooffat(const uint8_t *dosbb, struct disklabel *lp, daddr_t *partoffp)
{
	uint16_t		secsize;

#define	VALID_JMP(_p) (((_p)[0] == 0xeb && (_p)[2] == 0x90) || (_p)[0] == 0xe9)
#define	VALID_FAT(_p) ((_p)[16] == 1 || (_p)[16] == 2)
#define	VALID_SEC(_s) ((_s) >= DEV_BSIZE && (_s) <= 4096 && ((_s) % 512 == 0))

	memcpy(&secsize, dosbb + 11, sizeof(secsize));
	secsize = letoh16(secsize);

	if (VALID_JMP(dosbb) && VALID_SEC(secsize) && VALID_FAT(dosbb)) {
		lp->d_partitions['i' - 'a'] = lp->d_partitions[RAW_PART];
		lp->d_partitions['i' - 'a'].p_fstype = FS_MSDOS;
		*partoffp = -1;
		lp->d_magic = DISKMAGIC;
		DPRINTF("readdoslabel: FAT -- no label partition\n");
	}
}

/*
 * Check new disk label for sensibility before setting it.
 */
int
setdisklabel(struct disklabel *olp, struct disklabel *nlp, u_int64_t openmask)
{
	struct partition *opp, *npp;
	struct disk *dk;
	int i;

	/* sanity clause */
	if (nlp->d_secpercyl == 0 || nlp->d_secsize == 0 ||
	    (nlp->d_secsize % DEV_BSIZE) != 0)
		return (EINVAL);

	/* special case to allow disklabel to be invalidated */
	if (nlp->d_magic == 0xffffffff) {
		*olp = *nlp;
		return (0);
	}

	if (nlp->d_magic != DISKMAGIC || nlp->d_magic2 != DISKMAGIC ||
	    dkcksum(nlp) != 0)
		return (EINVAL);

	/* XXX missing check if other dos partitions will be overwritten */

	for (i = 0; i < MAXPARTITIONS; i++) {
		opp = &olp->d_partitions[i];
		npp = &nlp->d_partitions[i];
		if ((openmask & (1 << i)) &&
		    (DL_GETPOFFSET(npp) != DL_GETPOFFSET(opp) ||
		    DL_GETPSIZE(npp) < DL_GETPSIZE(opp)))
			return (EBUSY);
		/*
		 * Copy internally-set partition information
		 * if new label doesn't include it.		XXX
		 */
		if (npp->p_fstype == FS_UNUSED && opp->p_fstype != FS_UNUSED) {
			npp->p_fragblock = opp->p_fragblock;
			npp->p_cpg = opp->p_cpg;
		}
	}

	/* Generate a UID if the disklabel does not already have one. */
	if (duid_iszero(nlp->d_uid)) {
		do {
			arc4random_buf(nlp->d_uid, sizeof(nlp->d_uid));
			TAILQ_FOREACH(dk, &disklist, dk_link)
				if (dk->dk_label &&
				    duid_equal(dk->dk_label->d_uid, nlp->d_uid))
					break;
		} while (dk != NULL || duid_iszero(nlp->d_uid));
	}

	/* Preserve the disk size and RAW_PART values. */
	DL_SETDSIZE(nlp, DL_GETDSIZE(olp));
	npp = &nlp->d_partitions[RAW_PART];
	DL_SETPOFFSET(npp, 0);
	DL_SETPSIZE(npp, DL_GETDSIZE(nlp));

	nlp->d_checksum = 0;
	nlp->d_checksum = dkcksum(nlp);
	*olp = *nlp;

	disk_change = 1;

	return (0);
}

/*
 * Determine the size of the transfer, and make sure it is within the
 * boundaries of the partition. Adjust transfer if needed, and signal errors or
 * early completion.
 */
int
bounds_check_with_label(struct buf *bp, struct disklabel *lp)
{
	struct partition *p = &lp->d_partitions[DISKPART(bp->b_dev)];
	daddr_t partblocks, sz;

	/* Avoid division by zero, negative offsets, and negative sizes. */
	if (lp->d_secpercyl == 0 || bp->b_blkno < 0 || bp->b_bcount < 0)
		goto bad;

	/* Ensure transfer is a whole number of aligned sectors. */
	if ((bp->b_blkno % DL_BLKSPERSEC(lp)) != 0 ||
	    (bp->b_bcount % lp->d_secsize) != 0)
		goto bad;

	/* Ensure transfer starts within partition boundary. */
	partblocks = DL_SECTOBLK(lp, DL_GETPSIZE(p));
	if (bp->b_blkno > partblocks)
		goto bad;

	/* If exactly at end of partition or null transfer, return EOF. */
	if (bp->b_blkno == partblocks || bp->b_bcount == 0)
		goto done;

	/* Truncate request if it extends past the end of the partition. */
	sz = bp->b_bcount >> DEV_BSHIFT;
	if (sz > partblocks - bp->b_blkno) {
		sz = partblocks - bp->b_blkno;
		bp->b_bcount = sz << DEV_BSHIFT;
	}

	return (0);

 bad:
	bp->b_error = EINVAL;
	bp->b_flags |= B_ERROR;
 done:
	bp->b_resid = bp->b_bcount;
	return (-1);
}

/*
 * Disk error is the preface to plaintive error messages
 * about failing disk transfers.  It prints messages of the form

hp0g: hard error reading fsbn 12345 of 12344-12347 (hp0 bn %d cn %d tn %d sn %d)

 * if the offset of the error in the transfer and a disk label
 * are both available.  blkdone should be -1 if the position of the error
 * is unknown; the disklabel pointer may be null from drivers that have not
 * been converted to use them.  The message is printed with printf
 * if pri is LOG_PRINTF, otherwise it uses log at the specified priority.
 * The message should be completed (with at least a newline) with printf
 * or addlog, respectively.  There is no trailing space.
 */
void
diskerr(struct buf *bp, char *dname, char *what, int pri, int blkdone,
    struct disklabel *lp)
{
	int unit = DISKUNIT(bp->b_dev), part = DISKPART(bp->b_dev);
	int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2)));
	char partname = DL_PARTNUM2NAME(part);
	daddr_t sn;

	if (pri != LOG_PRINTF) {
		log(pri, "%s", "");
		pr = addlog;
	} else
		pr = printf;
	(*pr)("%s%d%c: %s %sing fsbn ", dname, unit, partname, what,
	    bp->b_flags & B_READ ? "read" : "writ");
	sn = bp->b_blkno;
	if (bp->b_bcount <= DEV_BSIZE)
		(*pr)("%lld", (long long)sn);
	else {
		if (blkdone >= 0) {
			sn += blkdone;
			(*pr)("%lld of ", (long long)sn);
		}
		(*pr)("%lld-%lld", (long long)bp->b_blkno,
		    (long long)(bp->b_blkno + (bp->b_bcount - 1) / DEV_BSIZE));
	}
	if (lp && (blkdone >= 0 || bp->b_bcount <= lp->d_secsize)) {
		sn += DL_SECTOBLK(lp, DL_GETPOFFSET(&lp->d_partitions[part]));
		(*pr)(" (%s%d bn %lld; cn %lld", dname, unit, (long long)sn,
		    (long long)(sn / DL_SECTOBLK(lp, lp->d_secpercyl)));
		sn %= DL_SECTOBLK(lp, lp->d_secpercyl);
		(*pr)(" tn %lld sn %lld)",
		    (long long)(sn / DL_SECTOBLK(lp, lp->d_nsectors)),
		    (long long)(sn % DL_SECTOBLK(lp, lp->d_nsectors)));
	}
}

/*
 * Initialize the disklist.  Called by main() before autoconfiguration.
 */
void
disk_init(void)
{

	TAILQ_INIT(&disklist);
	disk_count = disk_change = 0;
}

int
disk_construct(struct disk *diskp)
{
	rw_init_flags(&diskp->dk_lock, "dklk", RWL_IS_VNODE);
	mtx_init(&diskp->dk_mtx, IPL_BIO);

	diskp->dk_flags |= DKF_CONSTRUCTED;

	return (0);
}

/*
 * Attach a disk.
 */
void
disk_attach(struct device *dv, struct disk *diskp)
{
	int majdev;

	KERNEL_ASSERT_LOCKED();

	if (!ISSET(diskp->dk_flags, DKF_CONSTRUCTED))
		disk_construct(diskp);

	/*
	 * Allocate and initialize the disklabel structures.  Note that
	 * it's not safe to sleep here, since we're probably going to be
	 * called during autoconfiguration.
	 */
	diskp->dk_label = malloc(sizeof(struct disklabel), M_DEVBUF,
	    M_NOWAIT|M_ZERO);
	if (diskp->dk_label == NULL)
		panic("disk_attach: can't allocate storage for disklabel");

	/*
	 * Set the attached timestamp.
	 */
	microuptime(&diskp->dk_attachtime);

	/*
	 * Link into the disklist.
	 */
	TAILQ_INSERT_TAIL(&disklist, diskp, dk_link);
	++disk_count;
	disk_change = 1;

	/*
	 * Store device structure and number for later use.
	 */
	diskp->dk_device = dv;
	diskp->dk_devno = NODEV;
	if (dv != NULL) {
		majdev = findblkmajor(dv);
		if (majdev >= 0)
			diskp->dk_devno =
			    MAKEDISKDEV(majdev, dv->dv_unit, RAW_PART);

		if (diskp->dk_devno != NODEV) {
			struct disk_attach_task *dat;

			dat = malloc(sizeof(*dat), M_TEMP, M_WAITOK);

			/* XXX: Assumes dk is part of the device softc. */
			device_ref(dv);
			dat->dk = diskp;

			task_set(&dat->task, disk_attach_callback, dat);
			task_add(systq, &dat->task);
		}
	}

	if (softraid_disk_attach)
		softraid_disk_attach(diskp, 1);
}

void
disk_attach_callback(void *xdat)
{
	struct disk_attach_task *dat = xdat;
	struct disk *dk = dat->dk;
	struct disklabel *dl;
	char errbuf[100];

	free(dat, M_TEMP, sizeof(*dat));

	if (dk->dk_flags & (DKF_OPENED | DKF_NOLABELREAD))
		goto done;

	/* Read disklabel. */
	dl = malloc(sizeof(*dl), M_DEVBUF, M_WAITOK);
	if (disk_readlabel(dl, dk->dk_devno, errbuf, sizeof(errbuf)) == NULL)
		enqueue_randomness(dl->d_checksum);
	free(dl, M_DEVBUF, sizeof(*dl));

done:
	dk->dk_flags |= DKF_OPENED;
	device_unref(dk->dk_device);
	wakeup(dk);
}

/*
 * Detach a disk.
 */
void
disk_detach(struct disk *diskp)
{
	KERNEL_ASSERT_LOCKED();

	if (softraid_disk_attach)
		softraid_disk_attach(diskp, -1);

	/*
	 * Free the space used by the disklabel structures.
	 */
	free(diskp->dk_label, M_DEVBUF, sizeof(*diskp->dk_label));

	/*
	 * Remove from the disklist.
	 */
	TAILQ_REMOVE(&disklist, diskp, dk_link);
	disk_change = 1;
	if (--disk_count < 0)
		panic("disk_detach: disk_count < 0");
}

int
disk_openpart(struct disk *dk, int part, int fmt, int haslabel)
{
	KASSERT(part >= 0 && part < MAXPARTITIONS);

	/* Unless opening the raw partition, check that the partition exists. */
	if (part != RAW_PART && (!haslabel ||
	    part >= dk->dk_label->d_npartitions ||
	    dk->dk_label->d_partitions[part].p_fstype == FS_UNUSED))
		return (ENXIO);

	/* Ensure the partition doesn't get changed under our feet. */
	switch (fmt) {
	case S_IFCHR:
		dk->dk_copenmask |= (1 << part);
		break;
	case S_IFBLK:
		dk->dk_bopenmask |= (1 << part);
		break;
	}
	dk->dk_openmask = dk->dk_copenmask | dk->dk_bopenmask;

	return (0);
}

void
disk_closepart(struct disk *dk, int part, int fmt)
{
	KASSERT(part >= 0 && part < MAXPARTITIONS);

	switch (fmt) {
	case S_IFCHR:
		dk->dk_copenmask &= ~(1 << part);
		break;
	case S_IFBLK:
		dk->dk_bopenmask &= ~(1 << part);
		break;
	}
	dk->dk_openmask = dk->dk_copenmask | dk->dk_bopenmask;
}

void
disk_gone(int (*open)(dev_t, int, int, struct proc *), int unit)
{
	int bmaj, cmaj, mn;

	/* Locate the lowest minor number to be detached. */
	mn = DISKMINOR(unit, 0);

	for (bmaj = 0; bmaj < nblkdev; bmaj++)
		if (bdevsw[bmaj].d_open == open)
			vdevgone(bmaj, mn, mn + MAXPARTITIONS - 1, VBLK);
	for (cmaj = 0; cmaj < nchrdev; cmaj++)
		if (cdevsw[cmaj].d_open == open)
			vdevgone(cmaj, mn, mn + MAXPARTITIONS - 1, VCHR);
}

/*
 * Increment a disk's busy counter.  If the counter is going from
 * 0 to 1, set the timestamp.
 */
void
disk_busy(struct disk *diskp)
{

	/*
	 * XXX We'd like to use something as accurate as microtime(),
	 * but that doesn't depend on the system TOD clock.
	 */
	mtx_enter(&diskp->dk_mtx);
	if (diskp->dk_busy++ == 0)
		microuptime(&diskp->dk_timestamp);
	mtx_leave(&diskp->dk_mtx);
}

/*
 * Decrement a disk's busy counter, increment the byte count, total busy
 * time, and reset the timestamp.
 */
void
disk_unbusy(struct disk *diskp, long bcount, daddr_t blkno, int read)
{
	struct timeval dv_time, diff_time;

	mtx_enter(&diskp->dk_mtx);

	if (diskp->dk_busy-- == 0)
		printf("disk_unbusy: %s: dk_busy < 0\n", diskp->dk_name);

	microuptime(&dv_time);

	timersub(&dv_time, &diskp->dk_timestamp, &diff_time);
	timeradd(&diskp->dk_time, &diff_time, &diskp->dk_time);

	diskp->dk_timestamp = dv_time;
	if (bcount > 0) {
		if (read) {
			diskp->dk_rbytes += bcount;
			diskp->dk_rxfer++;
		} else {
			diskp->dk_wbytes += bcount;
			diskp->dk_wxfer++;
		}
	} else
		diskp->dk_seek++;

	mtx_leave(&diskp->dk_mtx);

	enqueue_randomness(bcount ^ diff_time.tv_usec ^
	    (blkno >> 32) ^ (blkno & 0xffffffff));
}

int
disk_lock(struct disk *dk)
{
	return (rw_enter(&dk->dk_lock, RW_WRITE|RW_INTR));
}

void
disk_lock_nointr(struct disk *dk)
{
	rw_enter_write(&dk->dk_lock);
}

void
disk_unlock(struct disk *dk)
{
	rw_exit_write(&dk->dk_lock);
}

int
dk_mountroot(void)
{
	char errbuf[100];
	int part = DISKPART(rootdev);
	int (*mountrootfn)(void);
	struct disklabel *dl;
	char *error;

	dl = malloc(sizeof(*dl), M_DEVBUF, M_WAITOK);
	error = disk_readlabel(dl, rootdev, errbuf, sizeof(errbuf));
	if (error)
		panic("%s", error);

	if (DL_GETPSIZE(&dl->d_partitions[part]) == 0)
		panic("root filesystem has size 0");
	switch (dl->d_partitions[part].p_fstype) {
#ifdef EXT2FS
	case FS_EXT2FS:
		{
		extern int ext2fs_mountroot(void);
		mountrootfn = ext2fs_mountroot;
		}
		break;
#endif
#ifdef FFS
	case FS_BSDFFS:
		{
		extern int ffs_mountroot(void);
		mountrootfn = ffs_mountroot;
		}
		break;
#endif
#ifdef CD9660
	case FS_ISO9660:
		{
		extern int cd9660_mountroot(void);
		mountrootfn = cd9660_mountroot;
		}
		break;
#endif
	default:
#ifdef FFS
		{
		extern int ffs_mountroot(void);

		printf("filesystem type %d not known.. assuming ffs\n",
		    dl->d_partitions[part].p_fstype);
		mountrootfn = ffs_mountroot;
		}
#else
		panic("disk 0x%x filesystem type %d not known",
		    rootdev, dl->d_partitions[part].p_fstype);
#endif
	}
	free(dl, M_DEVBUF, sizeof(*dl));

	return (*mountrootfn)();
}

struct device *
getdisk(char *str, int len, int defpart, dev_t *devp)
{
	struct device *dv;

	if ((dv = parsedisk(str, len, defpart, devp)) == NULL) {
		printf("use one of: exit");
		TAILQ_FOREACH(dv, &alldevs, dv_list) {
			if (dv->dv_class == DV_DISK)
				printf(" %s[a-p]", dv->dv_xname);
#if defined(NFSCLIENT)
			if (dv->dv_class == DV_IFNET)
				printf(" %s", dv->dv_xname);
#endif
		}
		printf("\n");
	}
	return (dv);
}

struct device *
parsedisk(char *str, int len, int defpart, dev_t *devp)
{
	struct device *dv;
	int majdev, part = defpart;
	char c;

	if (len == 0)
		return (NULL);
	c = str[len-1];
	part = DL_PARTNAME2NUM(c);
	if (part == -1 || part >= MAXPARTITIONS) {
		part = defpart;
	} else
		len -=1;

	TAILQ_FOREACH(dv, &alldevs, dv_list) {
		if (dv->dv_class == DV_DISK &&
		    strncmp(str, dv->dv_xname, len) == 0 &&
		    dv->dv_xname[len] == '\0') {
			majdev = findblkmajor(dv);
			if (majdev < 0)
				return NULL;
			*devp = MAKEDISKDEV(majdev, dv->dv_unit, part);
			break;
		}
#if defined(NFSCLIENT)
		if (dv->dv_class == DV_IFNET &&
		    strncmp(str, dv->dv_xname, len) == 0 &&
		    dv->dv_xname[len] == '\0') {
			*devp = NODEV;
			break;
		}
#endif
	}

	return (dv);
}

void
setroot(struct device *bootdv, int part, int exitflags)
{
	int majdev, unit, len, s, slept = 0;
	dev_t *swp;
	struct device *dv;
	dev_t nrootdev, nswapdev = NODEV, temp = NODEV;
	struct ifnet *ifp = NULL;
	struct disk *dk;
	char buf[128];
#if defined(NFSCLIENT)
	extern char *nfsbootdevname;
#endif

	/* Ensure that all disk attach callbacks have completed. */
	do {
		TAILQ_FOREACH(dk, &disklist, dk_link) {
			if (dk->dk_devno != NODEV &&
			    (dk->dk_flags & DKF_OPENED) == 0) {
				tsleep_nsec(dk, 0, "dkopen", SEC_TO_NSEC(1));
				slept++;
				break;
			}
		}
	} while (dk != NULL && slept < 5);

	if (slept == 5) {
		printf("disklabels not read:");
		TAILQ_FOREACH(dk, &disklist, dk_link)
			if (dk->dk_devno != NODEV &&
			    (dk->dk_flags & DKF_OPENED) == 0)
				printf(" %s", dk->dk_name);
		printf("\n");
	}

	if (duid_iszero(bootduid)) {
		/* Locate DUID for boot disk since it was not provided. */
		TAILQ_FOREACH(dk, &disklist, dk_link)
			if (dk->dk_device == bootdv)
				break;
		if (dk)
			bcopy(dk->dk_label->d_uid, bootduid, sizeof(bootduid));
	} else if (bootdv == NULL) {
		/* Locate boot disk based on the provided DUID. */
		TAILQ_FOREACH(dk, &disklist, dk_link)
			if (duid_equal(dk->dk_label->d_uid, bootduid))
				break;
		if (dk)
			bootdv = dk->dk_device;
	}
	bcopy(bootduid, rootduid, sizeof(rootduid));

#if NSOFTRAID > 0
	sr_map_root();
#endif

	/*
	 * If `swap generic' and we couldn't determine boot device,
	 * ask the user.
	 */
	dk = NULL;
	if (mountroot == NULL && bootdv == NULL)
		boothowto |= RB_ASKNAME;
	if (boothowto & RB_ASKNAME) {
		while (1) {
			printf("root device");
			if (bootdv != NULL) {
				printf(" (default %s", bootdv->dv_xname);
				if (bootdv->dv_class == DV_DISK)
					printf("%c", DL_PARTNUM2NAME(part));
				printf(")");
			}
			printf(": ");
			s = splhigh();
			cnpollc(1);
			len = getsn(buf, sizeof(buf));
			cnpollc(0);
			splx(s);
			if (strcmp(buf, "exit") == 0)
				reboot(exitflags);
			if (len == 0 && bootdv != NULL) {
				strlcpy(buf, bootdv->dv_xname, sizeof buf);
				//strlcat(buf, DL_PARTNUM2NAME(part), sizeof buf);
				len = strlen(buf);
			}
			if (len > 0 && buf[len - 1] == '*') {
				buf[--len] = '\0';
				dv = getdisk(buf, len, part, &nrootdev);
				if (dv != NULL) {
					rootdv = dv;
					nswapdev = nrootdev;
					goto gotswap;
				}
			}
			dv = getdisk(buf, len, part, &nrootdev);
			if (dv != NULL) {
				rootdv = dv;
				break;
			}
		}

		if (rootdv->dv_class == DV_IFNET)
			goto gotswap;

		/* try to build swap device out of new root device */
		while (1) {
			printf("swap device");
			if (rootdv != NULL)
				printf(" (default %s%s)", rootdv->dv_xname,
				    rootdv->dv_class == DV_DISK ? "b" : "");
			printf(": ");
			s = splhigh();
			cnpollc(1);
			len = getsn(buf, sizeof(buf));
			cnpollc(0);
			splx(s);
			if (strcmp(buf, "exit") == 0)
				reboot(exitflags);
			if (len == 0 && rootdv != NULL) {
				switch (rootdv->dv_class) {
				case DV_IFNET:
					nswapdev = NODEV;
					break;
				case DV_DISK:
					nswapdev = MAKEDISKDEV(major(nrootdev),
					    DISKUNIT(nrootdev), 1);
					if (nswapdev == nrootdev)
						continue;
					break;
				default:
					break;
				}
				break;
			}
			dv = getdisk(buf, len, 1, &nswapdev);
			if (dv) {
				if (dv->dv_class == DV_IFNET)
					nswapdev = NODEV;
				if (nswapdev == nrootdev)
					continue;
				break;
			}
		}
gotswap:
		rootdev = nrootdev;
		dumpdev = nswapdev;
		swdevt[0] = nswapdev;
		swdevt[1] = NODEV;
#if defined(NFSCLIENT)
	} else if (mountroot == nfs_mountroot) {
		rootdv = bootdv;
		rootdev = dumpdev = swapdev = NODEV;
#endif
	} else if (mountroot == NULL && rootdev == NODEV) {
		/*
		 * `swap generic'
		 */
		rootdv = bootdv;

		if (bootdv->dv_class == DV_DISK) {
			if (!duid_iszero(rootduid)) {
				TAILQ_FOREACH(dk, &disklist, dk_link)
					if (dk->dk_label && duid_equal(
					    dk->dk_label->d_uid, rootduid))
						break;
				if (dk == NULL)
					panic("root device (%s) not found",
					    duid_format(rootduid));
				rootdv = dk->dk_device;
			}
		}

		majdev = findblkmajor(rootdv);
		if (majdev >= 0) {
			/*
			 * Root and swap are on the disk.
			 * Assume swap is on partition b.
			 */
			rootdev = MAKEDISKDEV(majdev, rootdv->dv_unit, part);
			nswapdev = MAKEDISKDEV(majdev, rootdv->dv_unit, 1);
		} else {
			/*
			 * Root and swap are on a net.
			 */
			nswapdev = NODEV;
		}
		dumpdev = nswapdev;
		swdevt[0] = nswapdev;
		/* swdevt[1] = NODEV; */
	} else {
		/* Completely pre-configured, but we want rootdv .. */
		majdev = major(rootdev);
		if (findblkname(majdev) == NULL)
			return;
		unit = DISKUNIT(rootdev);
		part = DISKPART(rootdev);
		snprintf(buf, sizeof buf, "%s%d%c",
		    findblkname(majdev), unit, DL_PARTNUM2NAME(part));
		rootdv = parsedisk(buf, strlen(buf), 0, &nrootdev);
		if (rootdv == NULL)
			panic("root device (%s) not found", buf);
	}

	if (bootdv != NULL && bootdv->dv_class == DV_IFNET)
		ifp = if_unit(bootdv->dv_xname);

	if (ifp) {
		if_addgroup(ifp, "netboot");
		if_put(ifp);
	}

	switch (rootdv->dv_class) {
#if defined(NFSCLIENT)
	case DV_IFNET:
		mountroot = nfs_mountroot;
		nfsbootdevname = rootdv->dv_xname;
		return;
#endif
	case DV_DISK:
		mountroot = dk_mountroot;
		part = DISKPART(rootdev);
		break;
	default:
		printf("can't figure root, hope your kernel is right\n");
		return;
	}

	printf("root on %s%c", rootdv->dv_xname, DL_PARTNUM2NAME(part));

	if (dk && dk->dk_device == rootdv)
		printf(" (%s.%c)", duid_format(rootduid), DL_PARTNUM2NAME(part));

	/*
	 * Make the swap partition on the root drive the primary swap.
	 */
	for (swp = swdevt; *swp != NODEV; swp++) {
		if (major(rootdev) == major(*swp) &&
		    DISKUNIT(rootdev) == DISKUNIT(*swp)) {
			temp = swdevt[0];
			swdevt[0] = *swp;
			*swp = temp;
			break;
		}
	}
	if (*swp != NODEV) {
		/*
		 * If dumpdev was the same as the old primary swap device,
		 * move it to the new primary swap device.
		 */
		if (temp == dumpdev)
			dumpdev = swdevt[0];
	}
	if (swdevt[0] != NODEV)
		printf(" swap on %s%d%c", findblkname(major(swdevt[0])),
		    DISKUNIT(swdevt[0]), DL_PARTNUM2NAME(DISKPART(swdevt[0])));
	if (dumpdev != NODEV)
		printf(" dump on %s%d%c", findblkname(major(dumpdev)),
		    DISKUNIT(dumpdev), DL_PARTNUM2NAME(DISKPART(dumpdev)));
	printf("\n");
}

extern const struct nam2blk nam2blk[];

int
findblkmajor(struct device *dv)
{
	char buf[16], *p;
	int i;

	if (strlcpy(buf, dv->dv_xname, sizeof buf) >= sizeof buf)
		return (-1);
	for (p = buf; *p; p++)
		if (*p >= '0' && *p <= '9')
			*p = '\0';

	for (i = 0; nam2blk[i].name; i++)
		if (!strcmp(buf, nam2blk[i].name))
			return (nam2blk[i].maj);
	return (-1);
}

char *
findblkname(int maj)
{
	int i;

	for (i = 0; nam2blk[i].name; i++)
		if (nam2blk[i].maj == maj)
			return (nam2blk[i].name);
	return (NULL);
}

char *
disk_readlabel(struct disklabel *dl, dev_t dev, char *errbuf, size_t errsize)
{
	struct vnode *vn;
	dev_t chrdev, rawdev;
	int error;

	chrdev = blktochr(dev);
	rawdev = MAKEDISKDEV(major(chrdev), DISKUNIT(chrdev), RAW_PART);

#ifdef DEBUG
	printf("dev=0x%x chrdev=0x%x rawdev=0x%x\n", dev, chrdev, rawdev);
#endif

	if (cdevvp(rawdev, &vn)) {
		snprintf(errbuf, errsize,
		    "cannot obtain vnode for 0x%x/0x%x", dev, rawdev);
		return (errbuf);
	}

	error = VOP_OPEN(vn, FREAD, NOCRED, curproc);
	if (error) {
		snprintf(errbuf, errsize,
		    "cannot open disk, 0x%x/0x%x, error %d",
		    dev, rawdev, error);
		goto done;
	}

	error = VOP_IOCTL(vn, DIOCGDINFO, (caddr_t)dl, FREAD, NOCRED, curproc);
	if (error) {
		snprintf(errbuf, errsize,
		    "cannot read disk label, 0x%x/0x%x, error %d",
		    dev, rawdev, error);
	}
done:
	VOP_CLOSE(vn, FREAD, NOCRED, curproc);
	vput(vn);
	if (error)
		return (errbuf);
	return (NULL);
}

int
disk_map(const char *path, char *mappath, int size, int flags)
{
	struct disk *dk, *mdk;
	u_char uid[8];
	char c, part;
	int i, partno;

	/*
	 * Attempt to map a request for a disklabel UID to the correct device.
	 * We should be supplied with a disklabel UID which has the following
	 * format:
	 *
	 * [disklabel uid] . [partition]
	 *
	 * Alternatively, if the DM_OPENPART flag is set the disklabel UID can
	 * based passed on its own.
	 */

	if (strchr(path, '/') != NULL)
		return -1;

	/* Verify that the device name is properly formed. */
	if (!((strlen(path) == 16 && (flags & DM_OPENPART)) ||
	    (strlen(path) == 18 && path[16] == '.')))
		return -1;

	/* Get partition. */
	if (flags & DM_OPENPART) {
		partno = RAW_PART;
		part = DL_PARTNUM2NAME(partno);
	} else {
		part = path[17];
		partno = DL_PARTNAME2NUM(part);
	}
	if (partno == -1)
		return -1;

	/* Derive label UID. */
	memset(uid, 0, sizeof(uid));
	for (i = 0; i < 16; i++) {
		c = path[i];
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'a' && c <= 'f')
			c -= ('a' - 10);
		else
			return -1;

		uid[i / 2] <<= 4;
		uid[i / 2] |= c & 0xf;
	}

	mdk = NULL;
	TAILQ_FOREACH(dk, &disklist, dk_link) {
		if (dk->dk_label && memcmp(dk->dk_label->d_uid, uid,
		    sizeof(dk->dk_label->d_uid)) == 0) {
			/* Fail if there are duplicate UIDs! */
			if (mdk != NULL)
				return -1;
			mdk = dk;
		}
	}

	if (mdk == NULL || mdk->dk_name == NULL)
		return -1;

	snprintf(mappath, size, "/dev/%s%s%c",
	    (flags & DM_OPENBLCK) ? "" : "r", mdk->dk_name, part);

	return 0;
}

/*
 * Lookup a disk device and verify that it has completed attaching.
 */
struct device *
disk_lookup(struct cfdriver *cd, int unit)
{
	struct device *dv;
	struct disk *dk;

	dv = device_lookup(cd, unit);
	if (dv == NULL)
		return (NULL);

	TAILQ_FOREACH(dk, &disklist, dk_link)
		if (dk->dk_device == dv)
			break;

	if (dk == NULL) {
		device_unref(dv);
		return (NULL);
	}

	return (dv);
}

int
duid_equal(u_char *duid1, u_char *duid2)
{
	return (memcmp(duid1, duid2, DUID_SIZE) == 0);
}

int
duid_iszero(u_char *duid)
{
	u_char zeroduid[DUID_SIZE];

	memset(zeroduid, 0, sizeof(zeroduid));

	return (duid_equal(duid, zeroduid));
}

const char *
duid_format(u_char *duid)
{
	static char duid_str[17];

	KERNEL_ASSERT_LOCKED();

	snprintf(duid_str, sizeof(duid_str),
	    "%02x%02x%02x%02x%02x%02x%02x%02x",
	    duid[0], duid[1], duid[2], duid[3],
	    duid[4], duid[5], duid[6], duid[7]);

	return (duid_str);
}
