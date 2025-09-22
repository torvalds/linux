/*	$OpenBSD: softraid_i386.c,v 1.4 2020/12/09 18:10:18 krw Exp $	*/

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

#include "libsa.h"
#include "disk.h"
#include "softraid_i386.h"

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
	dev_t bsd_dev;
	daddr_t off;

	/* Probe for softraid volumes. */
	SLIST_INIT(&sr_volumes);
	SLIST_INIT(&sr_keydisks);

	md = alloc(SR_META_SIZE * DEV_BSIZE);

	TAILQ_FOREACH(dip, &disklist, list) {

		/* Only check hard disks, skip those with I/O errors. */
		if ((dip->bios_info.bios_number & 0x80) == 0 ||
		    (dip->bios_info.flags & BDI_INVALID))
			continue;

		/* Make sure disklabel has been read. */
		if ((dip->bios_info.flags & (BDI_BADLABEL|BDI_GOODLABEL)) == 0)
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
			bc->sbc_disk = dip->bios_info.bios_number;
			bc->sbc_part = 'a' + i;

			bsd_dev = dip->bios_info.bsd_dev;
			bc->sbc_mm = MAKEBOOTDEV(B_TYPE(bsd_dev),
			    B_ADAPTOR(bsd_dev), B_CONTROLLER(bsd_dev),
			    B_UNIT(bsd_dev), bc->sbc_part - 'a');

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
	blk += sr_dip->disklabel.d_partitions[bv->sbv_part - 'a'].p_offset;

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
		dip->bsddev = bc->sbc_mm;
		blk += bv->sbv_data_blkno;

		/* XXX - If I/O failed we should try another chunk... */
		return dip->strategy(dip, rw, blk, size, buf, rsize);

	} else if (bv->sbv_level == 'C') {

		/* Select first online chunk. */
		SLIST_FOREACH(bc, &bv->sbv_chunks, sbc_link)
			if (bc->sbc_state == BIOC_SDONLINE)
				break;
		if (bc == NULL)
			return EIO;

		dip = (struct diskinfo *)bc->sbc_diskinfo;
		dip->bsddev = bc->sbc_mm;

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

const char *
sr_getdisklabel(struct sr_boot_volume *bv, struct disklabel *label)
{
	struct dos_partition *dp;
	struct dos_mbr mbr;
	u_int start = 0;
	char buf[DEV_BSIZE];
	int i;

	/* Check for MBR to determine partition offset. */
	bzero(&mbr, sizeof(mbr));
	sr_strategy(bv, F_READ, DOSBBSECTOR, sizeof(mbr), &mbr, NULL);
	if (mbr.dmbr_sign == DOSMBR_SIGNATURE) {

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
	sr_strategy(bv, F_READ, start + DOS_LABELSECTOR,
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
