/*-
 * Copyright (c) 2014 Juniper Networks, Inc.
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

#include <sys/errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <bsd.h>

#include "endian.h"
#include "image.h"
#include "mkimg.h"
#include "scheme.h"

static struct mkimg_alias bsd_aliases[] = {
    {	ALIAS_FREEBSD_NANDFS, ALIAS_INT2TYPE(FS_NANDFS) },
    {	ALIAS_FREEBSD_SWAP, ALIAS_INT2TYPE(FS_SWAP) },
    {	ALIAS_FREEBSD_UFS, ALIAS_INT2TYPE(FS_BSDFFS) },
    {	ALIAS_FREEBSD_VINUM, ALIAS_INT2TYPE(FS_VINUM) },
    {	ALIAS_FREEBSD_ZFS, ALIAS_INT2TYPE(FS_ZFS) },
    {	ALIAS_NONE, 0 }
};

static lba_t
bsd_metadata(u_int where, lba_t blk)
{

	if (where == SCHEME_META_IMG_START)
		blk += BSD_BOOTBLOCK_SIZE / secsz;
	else if (where == SCHEME_META_IMG_END)
		blk = round_cylinder(blk);
	else
		blk = round_block(blk);
	return (blk);
}

static int
bsd_write(lba_t imgsz, void *bootcode)
{
	u_char *buf, *p;
	struct disklabel *d;
	struct partition *dp;
	struct part *part;
	int bsdparts, error, n;
	uint16_t checksum;

	buf = malloc(BSD_BOOTBLOCK_SIZE);
	if (buf == NULL)
		return (ENOMEM);
	if (bootcode != NULL) {
		memcpy(buf, bootcode, BSD_BOOTBLOCK_SIZE);
		memset(buf + secsz, 0, sizeof(struct disklabel));
	} else
		memset(buf, 0, BSD_BOOTBLOCK_SIZE);

	bsdparts = nparts + 1;	/* Account for c partition */
	if (bsdparts < BSD_NPARTS_MIN)
		bsdparts = BSD_NPARTS_MIN;

	d = (void *)(buf + secsz);
	le32enc(&d->d_magic, BSD_MAGIC);
	le32enc(&d->d_secsize, secsz);
	le32enc(&d->d_nsectors, nsecs);
	le32enc(&d->d_ntracks, nheads);
	le32enc(&d->d_ncylinders, ncyls);
	le32enc(&d->d_secpercyl, nsecs * nheads);
	le32enc(&d->d_secperunit, imgsz);
	le16enc(&d->d_rpm, 3600);
	le32enc(&d->d_magic2, BSD_MAGIC);
	le16enc(&d->d_npartitions, bsdparts);
	le32enc(&d->d_bbsize, BSD_BOOTBLOCK_SIZE);

	dp = &d->d_partitions[BSD_PART_RAW];
	le32enc(&dp->p_size, imgsz);
	TAILQ_FOREACH(part, &partlist, link) {
		n = part->index + ((part->index >= BSD_PART_RAW) ? 1 : 0);
		dp = &d->d_partitions[n];
		le32enc(&dp->p_size, part->size);
		le32enc(&dp->p_offset, part->block);
		le32enc(&dp->p_fsize, 0);
		dp->p_fstype = ALIAS_TYPE2INT(part->type);
		dp->p_frag = 0;
		le16enc(&dp->p_cpg, 0);
	}

	dp = &d->d_partitions[bsdparts];
	checksum = 0;
	for (p = (void *)d; p < (u_char *)dp; p += 2)
		checksum ^= le16dec(p);
	le16enc(&d->d_checksum, checksum);

	error = image_write(0, buf, BSD_BOOTBLOCK_SIZE / secsz);
	free(buf);
	return (error);
}

static struct mkimg_scheme bsd_scheme = {
	.name = "bsd",
	.description = "BSD disk label",
	.aliases = bsd_aliases,
	.metadata = bsd_metadata,
	.write = bsd_write,
	.nparts = BSD_NPARTS_MAX - 1,
	.bootcode = BSD_BOOTBLOCK_SIZE,
	.maxsecsz = 512
};

SCHEME_DEFINE(bsd_scheme);
