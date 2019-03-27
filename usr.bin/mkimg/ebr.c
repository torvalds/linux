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

#include <mbr.h>

#include "endian.h"
#include "image.h"
#include "mkimg.h"
#include "scheme.h"

static struct mkimg_alias ebr_aliases[] = {
    {	ALIAS_FAT16B, ALIAS_INT2TYPE(DOSPTYP_FAT16) },
    {	ALIAS_FAT32, ALIAS_INT2TYPE(DOSPTYP_FAT32) },
    {	ALIAS_FREEBSD, ALIAS_INT2TYPE(DOSPTYP_386BSD) },
    {	ALIAS_NONE, 0 }
};

static lba_t
ebr_metadata(u_int where, lba_t blk)
{

	blk += (where == SCHEME_META_PART_BEFORE) ? 1 : 0;
	return (round_track(blk));
}

static void
ebr_chs(u_char *cylp, u_char *hdp, u_char *secp, lba_t lba)
{
	u_int cyl, hd, sec;

	mkimg_chs(lba, 1023, &cyl, &hd, &sec);
	*cylp = cyl;
	*hdp = hd;
	*secp = (sec & 0x3f) | ((cyl >> 2) & 0xc0);
}

static int
ebr_write(lba_t imgsz __unused, void *bootcode __unused)
{
	u_char *ebr;
	struct dos_partition *dp;
	struct part *part, *next;
	lba_t block, size;
	int error;

	ebr = malloc(secsz);
	if (ebr == NULL)
		return (ENOMEM);
	memset(ebr, 0, secsz);
	le16enc(ebr + DOSMAGICOFFSET, DOSMAGIC);

	error = 0;
	TAILQ_FOREACH(part, &partlist, link) {
		block = part->block - nsecs;
		size = round_track(part->size);
		dp = (void *)(ebr + DOSPARTOFF);
		ebr_chs(&dp->dp_scyl, &dp->dp_shd, &dp->dp_ssect, nsecs);
		dp->dp_typ = ALIAS_TYPE2INT(part->type);
		ebr_chs(&dp->dp_ecyl, &dp->dp_ehd, &dp->dp_esect,
		    part->block + size - 1);
		le32enc(&dp->dp_start, nsecs);
		le32enc(&dp->dp_size, size);

		/* Add link entry */
		next = TAILQ_NEXT(part, link);
		if (next != NULL) {
			size = round_track(next->size);
			dp++;
			ebr_chs(&dp->dp_scyl, &dp->dp_shd, &dp->dp_ssect,
			    next->block - nsecs);
			dp->dp_typ = DOSPTYP_EXT;
			ebr_chs(&dp->dp_ecyl, &dp->dp_ehd, &dp->dp_esect,
			    next->block + size - 1);
			le32enc(&dp->dp_start, next->block - nsecs);
			le32enc(&dp->dp_size, size + nsecs);
		}

		error = image_write(block, ebr, 1);
		if (error)
			break;

		memset(ebr + DOSPARTOFF, 0, 2 * DOSPARTSIZE);
	}

	free(ebr);
	return (error);
}

static struct mkimg_scheme ebr_scheme = {
	.name = "ebr",
	.description = "Extended Boot Record",
	.aliases = ebr_aliases,
	.metadata = ebr_metadata,
	.write = ebr_write,
	.nparts = 4096,
	.maxsecsz = 4096
};

SCHEME_DEFINE(ebr_scheme);
