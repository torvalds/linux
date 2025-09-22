/*	$OpenBSD: softraid_arm64.c,v 1.4 2022/08/15 13:13:41 kn Exp $	*/

/*
 * Copyright (c) 2012 Joel Sing <jsing@openbsd.org>
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

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/disklabel.h>
#include <sys/reboot.h>

#include <dev/biovar.h>
#include <dev/softraidvar.h>

#include <lib/libsa/aes_xts.h>
#include <lib/libsa/softraid.h>
#include <lib/libz/zlib.h>

#include <efi.h>

#include "libsa.h"
#include "disk.h"
#include "efidev.h"
#include "softraid_arm64.h"

static int gpt_chk_mbr(struct dos_partition *, u_int64_t);
static uint64_t findopenbsd_gpt(struct sr_boot_volume *, const char **);

void
srprobe_meta_opt_load(struct sr_metadata *sm, struct sr_meta_opt_head *som)
{
	struct sr_meta_opt_hdr	*omh;
	struct sr_meta_opt_item *omi;
#if 0
	u_int8_t checksum[MD5_DIGEST_LENGTH];
#endif
	int			i;

	/* Process optional metadata. */
	omh = (struct sr_meta_opt_hdr *)((u_int8_t *)(sm + 1) +
	    sizeof(struct sr_meta_chunk) * sm->ssdi.ssd_chunk_no);
	for (i = 0; i < sm->ssdi.ssd_opt_no; i++) {

#ifdef BIOS_DEBUG
		printf("Found optional metadata of type %u, length %u\n",
		    omh->som_type, omh->som_length);
#endif

		/* Unsupported old fixed length optional metadata. */
		if (omh->som_length == 0) {
			omh = (struct sr_meta_opt_hdr *)((void *)omh +
			    SR_OLD_META_OPT_SIZE);
			continue;
		}

		/* Load variable length optional metadata. */
		omi = alloc(sizeof(struct sr_meta_opt_item));
		bzero(omi, sizeof(struct sr_meta_opt_item));
		SLIST_INSERT_HEAD(som, omi, omi_link);
		omi->omi_som = alloc(omh->som_length);
		bzero(omi->omi_som, omh->som_length);
		bcopy(omh, omi->omi_som, omh->som_length);

#if 0
		/* XXX - Validate checksum. */
		bcopy(&omi->omi_som->som_checksum, &checksum,
		    MD5_DIGEST_LENGTH);
		bzero(&omi->omi_som->som_checksum, MD5_DIGEST_LENGTH);
		sr_checksum(sc, omi->omi_som,
		    &omi->omi_som->som_checksum, omh->som_length);
		if (bcmp(&checksum, &omi->omi_som->som_checksum,
		    sizeof(checksum)))
			panic("%s: invalid optional metadata checksum",
			    DEVNAME(sc));
#endif

		omh = (struct sr_meta_opt_hdr *)((void *)omh +
		    omh->som_length);
	}
}

void
srprobe_keydisk_load(struct sr_metadata *sm)
{
	struct sr_meta_opt_hdr	*omh;
	struct sr_meta_keydisk	*skm;
	struct sr_boot_keydisk	*kd;
	int i;

	/* Process optional metadata. */
	omh = (struct sr_meta_opt_hdr *)((u_int8_t *)(sm + 1) +
	    sizeof(struct sr_meta_chunk) * sm->ssdi.ssd_chunk_no);
	for (i = 0; i < sm->ssdi.ssd_opt_no; i++) {

		/* Unsupported old fixed length optional metadata. */
		if (omh->som_length == 0) {
			omh = (struct sr_meta_opt_hdr *)((void *)omh +
			    SR_OLD_META_OPT_SIZE);
			continue;
		}

		if (omh->som_type != SR_OPT_KEYDISK) {
			omh = (struct sr_meta_opt_hdr *)((void *)omh +
			    omh->som_length);
			continue;
		}

		kd = alloc(sizeof(struct sr_boot_keydisk));
		bcopy(&sm->ssdi.ssd_uuid, &kd->kd_uuid, sizeof(kd->kd_uuid));
		skm = (struct sr_meta_keydisk*)omh;
		bcopy(&skm->skm_maskkey, &kd->kd_key, sizeof(kd->kd_key));
		SLIST_INSERT_HEAD(&sr_keydisks, kd, kd_link);
	}
}

void
srprobe(void)
{
	struct sr_boot_volume *bv, *bv1, *bv2;
	struct sr_boot_chunk *bc, *bc1, *bc2;
	struct sr_meta_chunk *mc;
	struct sr_metadata *md;
	struct diskinfo *dip;
	struct partition *pp;
	int i, error, volno;
	daddr_t off;

	/* Probe for softraid volumes. */
	SLIST_INIT(&sr_volumes);
	SLIST_INIT(&sr_keydisks);

	md = alloc(SR_META_SIZE * DEV_BSIZE);

	TAILQ_FOREACH(dip, &disklist, list) {

		/* Make sure disklabel has been read. */
		if ((dip->flags & DISKINFO_FLAG_GOODLABEL) == 0)
			continue;

		for (i = 0; i < MAXPARTITIONS; i++) {

			pp = &dip->disklabel.d_partitions[i];
			if (pp->p_fstype != FS_RAID || pp->p_size == 0)
				continue;

			/* Read softraid metadata. */
			bzero(md, SR_META_SIZE * DEV_BSIZE);
			off = DL_SECTOBLK(&dip->disklabel, DL_GETPOFFSET(pp));
			off += SR_META_OFFSET;
			error = dip->diskio(F_READ, dip, off, SR_META_SIZE, md);
			if (error)
				continue;

			/* Is this valid softraid metadata? */
			if (md->ssdi.ssd_magic != SR_MAGIC)
				continue;

			/* XXX - validate checksum. */

			/* Handle key disks separately... */
			if (md->ssdi.ssd_level == SR_KEYDISK_LEVEL) {
				srprobe_keydisk_load(md);
				continue;
			}

			/* Locate chunk-specific metadata for this chunk. */
			mc = (struct sr_meta_chunk *)(md + 1);
			mc += md->ssdi.ssd_chunk_id;

			bc = alloc(sizeof(struct sr_boot_chunk));
			bc->sbc_diskinfo = dip;
			bc->sbc_disk = 0;
			bc->sbc_part = 'a' + i;

			bc->sbc_mm = 0;

			bc->sbc_chunk_id = md->ssdi.ssd_chunk_id;
			bc->sbc_ondisk = md->ssd_ondisk;
			bc->sbc_state = mc->scm_status;

			SLIST_FOREACH(bv, &sr_volumes, sbv_link) {
				if (bcmp(&md->ssdi.ssd_uuid, &bv->sbv_uuid,
				    sizeof(md->ssdi.ssd_uuid)) == 0)
					break;
			}

			if (bv == NULL) {
				bv = alloc(sizeof(struct sr_boot_volume));
				bzero(bv, sizeof(struct sr_boot_volume));
				bv->sbv_level = md->ssdi.ssd_level;
				bv->sbv_volid = md->ssdi.ssd_volid;
				bv->sbv_chunk_no = md->ssdi.ssd_chunk_no;
				bv->sbv_flags = md->ssdi.ssd_vol_flags;
				bv->sbv_size = md->ssdi.ssd_size;
				bv->sbv_secsize = md->ssdi.ssd_secsize;
				bv->sbv_data_blkno = md->ssd_data_blkno;
				bcopy(&md->ssdi.ssd_uuid, &bv->sbv_uuid,
				    sizeof(md->ssdi.ssd_uuid));
				SLIST_INIT(&bv->sbv_chunks);
				SLIST_INIT(&bv->sbv_meta_opt);

				/* Load optional metadata for this volume. */
				srprobe_meta_opt_load(md, &bv->sbv_meta_opt);

				/* Maintain volume order. */
				bv2 = NULL;
				SLIST_FOREACH(bv1, &sr_volumes, sbv_link) {
					if (bv1->sbv_volid > bv->sbv_volid)
						break;
					bv2 = bv1;
				}
				if (bv2 == NULL)
					SLIST_INSERT_HEAD(&sr_volumes, bv,
					    sbv_link);
				else
					SLIST_INSERT_AFTER(bv2, bv, sbv_link);
			}

			/* Maintain chunk order. */
			bc2 = NULL;
			SLIST_FOREACH(bc1, &bv->sbv_chunks, sbc_link) {
				if (bc1->sbc_chunk_id > bc->sbc_chunk_id)
					break;
				bc2 = bc1;
			}
			if (bc2 == NULL)
				SLIST_INSERT_HEAD(&bv->sbv_chunks,
				    bc, sbc_link);
			else
				SLIST_INSERT_AFTER(bc2, bc, sbc_link);

			bv->sbv_chunks_found++;
		}
	}

	/*
	 * Assemble RAID volumes.
	 */
	volno = 0;
	SLIST_FOREACH(bv, &sr_volumes, sbv_link) {

		/* Skip if this is a hotspare "volume". */
		if (bv->sbv_level == SR_HOTSPARE_LEVEL &&
		    bv->sbv_chunk_no == 1)
			continue;

		/* Determine current ondisk version. */
		bv->sbv_ondisk = 0;
		SLIST_FOREACH(bc, &bv->sbv_chunks, sbc_link) {
			if (bc->sbc_ondisk > bv->sbv_ondisk)
				bv->sbv_ondisk = bc->sbc_ondisk;
		}
		SLIST_FOREACH(bc, &bv->sbv_chunks, sbc_link) {
			if (bc->sbc_ondisk != bv->sbv_ondisk)
				bc->sbc_state = BIOC_SDOFFLINE;
		}

		/* XXX - Check for duplicate chunks. */

		/*
		 * Validate that volume has sufficient chunks for
		 * read-only access.
		 *
		 * XXX - check chunk states.
		 */
		bv->sbv_state = BIOC_SVOFFLINE;
		switch (bv->sbv_level) {
		case 0:
		case 'C':
		case 'c':
			if (bv->sbv_chunk_no == bv->sbv_chunks_found)
				bv->sbv_state = BIOC_SVONLINE;
			break;

		case 1:
		case 0x1C:
			if (bv->sbv_chunk_no == bv->sbv_chunks_found)
				bv->sbv_state = BIOC_SVONLINE;
			else if (bv->sbv_chunks_found > 0)
				bv->sbv_state = BIOC_SVDEGRADED;
			break;
		}

		bv->sbv_unit = volno++;
		if (bv->sbv_state != BIOC_SVOFFLINE)
			printf(" sr%d%s", bv->sbv_unit,
			    bv->sbv_flags & BIOC_SCBOOTABLE ? "*" : "");
	}

	explicit_bzero(md, SR_META_SIZE * DEV_BSIZE);
	free(md, SR_META_SIZE * DEV_BSIZE);
}

int
sr_strategy(struct sr_boot_volume *bv, int rw, daddr_t blk, size_t size,
    void *buf, size_t *rsize)
{
	struct diskinfo *sr_dip, *dip;
	struct sr_boot_chunk *bc;
	struct aes_xts_ctx ctx;
	size_t i, j, nsect;
	daddr_t blkno;
	u_char iv[8];
	u_char *bp;
	int err;

	/* We only support read-only softraid. */
	if (rw != F_READ)
		return ENOTSUP;

	/* Partition offset within softraid volume. */
	sr_dip = (struct diskinfo *)bv->sbv_diskinfo;
	blk += DL_SECTOBLK(&sr_dip->disklabel,
	    sr_dip->disklabel.d_partitions[bv->sbv_part - 'a'].p_offset);

	if (bv->sbv_level == 0) {
		return ENOTSUP;
	} else if (bv->sbv_level == 1) {

		/* Select first online chunk. */
		SLIST_FOREACH(bc, &bv->sbv_chunks, sbc_link)
			if (bc->sbc_state == BIOC_SDONLINE)
				break;
		if (bc == NULL)
			return EIO;

		dip = (struct diskinfo *)bc->sbc_diskinfo;
		blk += bv->sbv_data_blkno;

		/* XXX - If I/O failed we should try another chunk... */
		return dip->strategy(dip, rw, blk, size, buf, rsize);

	} else if (bv->sbv_level == 'C' || bv->sbv_level == 0x1C) {

		/* Select first online chunk. */
		SLIST_FOREACH(bc, &bv->sbv_chunks, sbc_link)
			if (bc->sbc_state == BIOC_SDONLINE)
				break;
		if (bc == NULL)
			return EIO;

		dip = (struct diskinfo *)bc->sbc_diskinfo;

		/* XXX - select correct key. */
		aes_xts_setkey(&ctx, (u_char *)bv->sbv_keys, 64);

		nsect = (size + DEV_BSIZE - 1) / DEV_BSIZE;
		for (i = 0; i < nsect; i++) {
			blkno = blk + i;
			bp = ((u_char *)buf) + i * DEV_BSIZE;
			err = dip->strategy(dip, rw, bv->sbv_data_blkno + blkno,
			    DEV_BSIZE, bp, NULL);
			if (err != 0)
				return err;

			bcopy(&blkno, iv, sizeof(blkno));
			aes_xts_reinit(&ctx, iv);
			for (j = 0; j < DEV_BSIZE; j += AES_XTS_BLOCKSIZE)
				aes_xts_decrypt(&ctx, bp + j);
		}
		if (rsize != NULL)
			*rsize = nsect * DEV_BSIZE;

		return err;

	} else
		return ENOTSUP;
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

static uint64_t
findopenbsd_gpt(struct sr_boot_volume *bv, const char **err)
{
	struct			 gpt_header gh;
	int			 i, part, found;
	uint64_t		 lba;
	uint32_t		 orig_csum, new_csum;
	uint32_t		 ghsize, ghpartsize, ghpartnum, ghpartspersec;
	uint32_t		 gpsectors;
	const char		 openbsd_uuid_code[] = GPT_UUID_OPENBSD;
	struct gpt_partition	 gp;
	static struct uuid	*openbsd_uuid = NULL, openbsd_uuid_space;
	u_char		 	*buf;

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

	if (bv->sbv_secsize > 4096) {
		*err = "disk sector > 4096 bytes\n";
		return (-1);
	}
	buf = alloc(bv->sbv_secsize);
	if (buf == NULL) {
		*err = "out of memory\n";
		return (-1);
	}
	bzero(buf, bv->sbv_secsize);

	/* GPT Header */
	lba = GPTSECTOR;
	sr_strategy(bv, F_READ, lba * (bv->sbv_secsize / DEV_BSIZE), DEV_BSIZE,
	    buf, NULL);
	memcpy(&gh, buf, sizeof(gh));

	/* Check signature */
	if (letoh64(gh.gh_sig) != GPTSIGNATURE) {
		*err = "bad GPT signature\n";
		free(buf, bv->sbv_secsize);
		return (-1);
	}

	if (letoh32(gh.gh_rev) != GPTREVISION) {
		*err = "bad GPT revision\n";
		free(buf, bv->sbv_secsize);
		return (-1);
	}

	ghsize = letoh32(gh.gh_size);
	if (ghsize < GPTMINHDRSIZE || ghsize > sizeof(struct gpt_header)) {
		*err = "bad GPT header size\n";
		free(buf, bv->sbv_secsize);
		return (-1);
	}

	/* Check checksum */
	orig_csum = gh.gh_csum;
	gh.gh_csum = 0;
	new_csum = crc32(0, (unsigned char *)&gh, ghsize);
	gh.gh_csum = orig_csum;
	if (letoh32(orig_csum) != new_csum) {
		*err = "bad GPT header checksum\n";
		free(buf, bv->sbv_secsize);
		return (-1);
	}

	lba = letoh64(gh.gh_part_lba);
	ghpartsize = letoh32(gh.gh_part_size);
	ghpartspersec = bv->sbv_secsize / ghpartsize;
	ghpartnum = letoh32(gh.gh_part_num);
	gpsectors = (ghpartnum + ghpartspersec - 1) / ghpartspersec;
	new_csum = crc32(0L, Z_NULL, 0);
	found = 0;
	for (i = 0; i < gpsectors; i++, lba++) {
		sr_strategy(bv, F_READ, lba * (bv->sbv_secsize / DEV_BSIZE),
		    bv->sbv_secsize, buf, NULL);
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

	free(buf, bv->sbv_secsize);

	if (new_csum != letoh32(gh.gh_part_csum)) {
		*err = "bad GPT entries checksum\n";
		return (-1);
	}
	if (found)
		return (letoh64(gp.gp_lba_start));

	return (-1);
}

const char *
sr_getdisklabel(struct sr_boot_volume *bv, struct disklabel *label)
{
	struct dos_partition *dp;
	struct dos_mbr mbr;
	const char *err = NULL;
	u_int start = 0;
	char buf[DEV_BSIZE];
	int i;

	/* Check for MBR to determine partition offset. */
	bzero(&mbr, sizeof(mbr));
	sr_strategy(bv, F_READ, DOSBBSECTOR, sizeof(mbr), &mbr, NULL);
	if (gpt_chk_mbr(mbr.dmbr_parts, bv->sbv_size /
		    (bv->sbv_secsize / DEV_BSIZE)) == 0) {
		start = findopenbsd_gpt(bv, &err);
		if (start == (u_int)-1) {
			if (err != NULL)
				return (err);
			return "no OpenBSD partition\n";
		}
	} else if (mbr.dmbr_sign == DOSMBR_SIGNATURE) {

		/* Search for OpenBSD partition */
		for (i = 0; i < NDOSPART; i++) {
			dp = &mbr.dmbr_parts[i];
			if (!dp->dp_size)
				continue;
			if (dp->dp_typ == DOSPTYP_OPENBSD) {
				start = dp->dp_start;
				break;
			}
		}
	}

	/* Read the disklabel. */
	sr_strategy(bv, F_READ,
	    start * (bv->sbv_secsize / DEV_BSIZE) + DOS_LABELSECTOR,
	    sizeof(struct disklabel), buf, NULL);

#ifdef BIOS_DEBUG
	printf("sr_getdisklabel: magic %lx\n",
	    ((struct disklabel *)buf)->d_magic);
	for (i = 0; i < MAXPARTITIONS; i++)
		printf("part %c: type = %d, size = %d, offset = %d\n", 'a' + i,
		    (int)((struct disklabel *)buf)->d_partitions[i].p_fstype,
		    (int)((struct disklabel *)buf)->d_partitions[i].p_size,
		    (int)((struct disklabel *)buf)->d_partitions[i].p_offset);
#endif

	/* Fill in disklabel */
	return (getdisklabel(buf, label));
}

int
sropen(struct open_file *f, ...)
{
	struct diskinfo *dip = NULL;
	struct sr_boot_volume *bv;
	va_list ap;
	u_int unit, part;

	va_start(ap, f);
	unit = va_arg(ap, u_int);
	part = va_arg(ap, u_int);
	va_end(ap);

	/* Create a fake diskinfo for this softraid volume. */
	SLIST_FOREACH(bv, &sr_volumes, sbv_link)
		if (bv->sbv_unit == unit)
			break;
	if (bv == NULL) {
		printf("Unknown device: sr%d\n", unit);
		return EADAPT;
	}

	if ((bv->sbv_level == 'C' || bv->sbv_level == 0x1C)
	    && bv->sbv_keys == NULL)
		if (sr_crypto_unlock_volume(bv) != 0)
			return EPERM;

	if (bv->sbv_diskinfo == NULL) {
		dip = alloc(sizeof(struct diskinfo));
		bzero(dip, sizeof(*dip));
		dip->diskio = srdiskio;
		dip->strategy = srstrategy;
		bv->sbv_diskinfo = dip;
		dip->sr_vol = bv;
	}

	dip = bv->sbv_diskinfo;

	if ((dip->flags & DISKINFO_FLAG_GOODLABEL) == 0) {
		/* Attempt to read disklabel. */
		bv->sbv_part = 'c';
		if (sr_getdisklabel(bv, &dip->disklabel))
			return ERDLAB;
		dip->flags |= DISKINFO_FLAG_GOODLABEL;
	}

	bv->sbv_part = part + 'a';

	bootdev_dip = dip;
	f->f_devdata = dip;

	return 0;
}

int
srstrategy(void *devdata, int rw, daddr_t blk, size_t size, void *buf,
    size_t *rsize)
{
	struct diskinfo *dip = (struct diskinfo *)devdata;
	return sr_strategy(dip->sr_vol, rw, blk, size, buf, rsize);
}

int
srdiskio(int rw, struct diskinfo *dip, u_int off, int nsect, void *buf)
{
	return dip->diskio(rw, dip, off, nsect, buf);
}

int
srclose(struct open_file *f)
{
	f->f_devdata = NULL;

	return 0;
}

int
srioctl(struct open_file *f, u_long cmd, void *data)
{
	return 0;
}
