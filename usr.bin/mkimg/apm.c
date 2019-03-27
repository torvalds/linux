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

#include <apm.h>

#include "endian.h"
#include "image.h"
#include "mkimg.h"
#include "scheme.h"

static struct mkimg_alias apm_aliases[] = {
    {	ALIAS_FREEBSD, ALIAS_PTR2TYPE(APM_ENT_TYPE_FREEBSD) },
    {	ALIAS_FREEBSD_BOOT, ALIAS_PTR2TYPE(APM_ENT_TYPE_APPLE_BOOT) },
    {	ALIAS_FREEBSD_NANDFS, ALIAS_PTR2TYPE(APM_ENT_TYPE_FREEBSD_NANDFS) },
    {	ALIAS_FREEBSD_SWAP, ALIAS_PTR2TYPE(APM_ENT_TYPE_FREEBSD_SWAP) },
    {	ALIAS_FREEBSD_UFS, ALIAS_PTR2TYPE(APM_ENT_TYPE_FREEBSD_UFS) },
    {	ALIAS_FREEBSD_VINUM, ALIAS_PTR2TYPE(APM_ENT_TYPE_FREEBSD_VINUM) },
    {	ALIAS_FREEBSD_ZFS, ALIAS_PTR2TYPE(APM_ENT_TYPE_FREEBSD_ZFS) },
    {	ALIAS_NONE, 0 }
};

static lba_t
apm_metadata(u_int where, lba_t blk)
{

	blk += (where == SCHEME_META_IMG_START) ? nparts + 2 : 0;
	return (round_block(blk));
}

static int
apm_write(lba_t imgsz, void *bootcode __unused)
{
	u_char *buf;
	struct apm_ddr *ddr;
	struct apm_ent *ent;
	struct part *part;
	int error;

	buf = calloc(nparts + 2, secsz);
	if (buf == NULL)
		return (ENOMEM);
	ddr = (void *)buf;
	be16enc(&ddr->ddr_sig, APM_DDR_SIG);
	be16enc(&ddr->ddr_blksize, secsz);
	be32enc(&ddr->ddr_blkcount, imgsz);

	/* partition entry for the partition table itself. */
	ent = (void *)(buf + secsz);
	be16enc(&ent->ent_sig, APM_ENT_SIG);
	be32enc(&ent->ent_pmblkcnt, nparts + 1);
	be32enc(&ent->ent_start, 1);
	be32enc(&ent->ent_size, nparts + 1);
	strncpy(ent->ent_type, APM_ENT_TYPE_SELF, sizeof(ent->ent_type));
	strncpy(ent->ent_name, "Apple", sizeof(ent->ent_name));

	TAILQ_FOREACH(part, &partlist, link) {
		ent = (void *)(buf + (part->index + 2) * secsz);
		be16enc(&ent->ent_sig, APM_ENT_SIG);
		be32enc(&ent->ent_pmblkcnt, nparts + 1);
		be32enc(&ent->ent_start, part->block);
		be32enc(&ent->ent_size, part->size);
		strncpy(ent->ent_type, ALIAS_TYPE2PTR(part->type),
		    sizeof(ent->ent_type));
		if (part->label != NULL)
			strncpy(ent->ent_name, part->label,
			    sizeof(ent->ent_name));
	}

	error = image_write(0, buf, nparts + 2);
	free(buf);
	return (error);
}

static struct mkimg_scheme apm_scheme = {
	.name = "apm",
	.description = "Apple Partition Map",
	.aliases = apm_aliases,
	.metadata = apm_metadata,
	.write = apm_write,
	.nparts = 4096,
	.labellen = APM_ENT_NAMELEN - 1,
	.maxsecsz = 4096
};

SCHEME_DEFINE(apm_scheme);
