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

static struct mkimg_alias mbr_aliases[] = {
    {	ALIAS_EBR, ALIAS_INT2TYPE(DOSPTYP_EXT) },
    {	ALIAS_EFI, ALIAS_INT2TYPE(DOSPTYP_EFI) },
    {	ALIAS_FAT16B, ALIAS_INT2TYPE(DOSPTYP_FAT16) },
    {	ALIAS_FAT32, ALIAS_INT2TYPE(DOSPTYP_FAT32) },
    {	ALIAS_FREEBSD, ALIAS_INT2TYPE(DOSPTYP_386BSD) },
    {	ALIAS_NTFS, ALIAS_INT2TYPE(DOSPTYP_NTFS) },
    {	ALIAS_PPCBOOT, ALIAS_INT2TYPE(DOSPTYP_PPCBOOT) },
    {	ALIAS_NONE, 0 }		/* Keep last! */
};

static lba_t
mbr_metadata(u_int where, lba_t blk)
{

	blk += (where == SCHEME_META_IMG_START) ? 1 : 0;
	return (round_track(blk));
}

static void
mbr_chs(u_char *cylp, u_char *hdp, u_char *secp, lba_t lba)
{
	u_int cyl, hd, sec;

	mkimg_chs(lba, 1023, &cyl, &hd, &sec);
	*cylp = cyl;
	*hdp = hd;
	*secp = (sec & 0x3f) | ((cyl >> 2) & 0xc0);
}

static int
mbr_write(lba_t imgsz __unused, void *bootcode)
{
	u_char *mbr;
	struct dos_partition *dpbase, *dp;
	struct part *part;
	lba_t size;
	int error;

	mbr = malloc(secsz);
	if (mbr == NULL)
		return (ENOMEM);
	if (bootcode != NULL) {
		memcpy(mbr, bootcode, DOSPARTOFF);
		memset(mbr + DOSPARTOFF, 0, secsz - DOSPARTOFF);
	} else
		memset(mbr, 0, secsz);
	le16enc(mbr + DOSMAGICOFFSET, DOSMAGIC);
	dpbase = (void *)(mbr + DOSPARTOFF);
	TAILQ_FOREACH(part, &partlist, link) {
		size = round_track(part->size);
		dp = dpbase + part->index;
		if (active_partition != 0)
			dp->dp_flag =
			    (part->index + 1 == active_partition) ? 0x80 : 0;
		else
			dp->dp_flag =
			    (part->index == 0 && bootcode != NULL) ? 0x80 : 0;
		mbr_chs(&dp->dp_scyl, &dp->dp_shd, &dp->dp_ssect,
		    part->block);
		dp->dp_typ = ALIAS_TYPE2INT(part->type);
		mbr_chs(&dp->dp_ecyl, &dp->dp_ehd, &dp->dp_esect,
		    part->block + size - 1);
		le32enc(&dp->dp_start, part->block);
		le32enc(&dp->dp_size, size);
	}
	error = image_write(0, mbr, 1);
	free(mbr);
	return (error);
}

static struct mkimg_scheme mbr_scheme = {
	.name = "mbr",
	.description = "Master Boot Record",
	.aliases = mbr_aliases,
	.metadata = mbr_metadata,
	.write = mbr_write,
	.bootcode = 512,
	.nparts = NDOSPART,
	.maxsecsz = 4096
};

SCHEME_DEFINE(mbr_scheme);
