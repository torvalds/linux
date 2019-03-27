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
#include <stdio.h>
#include <string.h>

#include <vtoc.h>

#include "endian.h"
#include "image.h"
#include "mkimg.h"
#include "scheme.h"

static struct mkimg_alias vtoc8_aliases[] = {
    {	ALIAS_FREEBSD_NANDFS, ALIAS_INT2TYPE(VTOC_TAG_FREEBSD_NANDFS) },
    {	ALIAS_FREEBSD_SWAP, ALIAS_INT2TYPE(VTOC_TAG_FREEBSD_SWAP) },
    {	ALIAS_FREEBSD_UFS, ALIAS_INT2TYPE(VTOC_TAG_FREEBSD_UFS) },
    {	ALIAS_FREEBSD_VINUM, ALIAS_INT2TYPE(VTOC_TAG_FREEBSD_VINUM) },
    {	ALIAS_FREEBSD_ZFS, ALIAS_INT2TYPE(VTOC_TAG_FREEBSD_NANDFS) },
    {	ALIAS_NONE, 0 }
};

static lba_t
vtoc8_metadata(u_int where, lba_t blk)
{

	blk += (where == SCHEME_META_IMG_START) ? 1 : 0;
	return (round_cylinder(blk));
}

static int
vtoc8_write(lba_t imgsz, void *bootcode __unused)
{
	struct vtoc8 vtoc8;
	struct part *part;
	u_char *p;
	int error, n;
	uint16_t ofs, sum;

	imgsz = (lba_t)ncyls * nheads * nsecs;

	memset(&vtoc8, 0, sizeof(vtoc8));
	sprintf(vtoc8.ascii, "FreeBSD%lldM",
	    (long long)(imgsz * secsz / 1048576));
	be32enc(&vtoc8.version, VTOC_VERSION);
	be16enc(&vtoc8.nparts, VTOC8_NPARTS);
	be32enc(&vtoc8.sanity, VTOC_SANITY);
	be16enc(&vtoc8.rpm, 3600);
	be16enc(&vtoc8.physcyls, ncyls);
	be16enc(&vtoc8.ncyls, ncyls);
	be16enc(&vtoc8.altcyls, 0);
	be16enc(&vtoc8.nheads, nheads);
	be16enc(&vtoc8.nsecs, nsecs);
	be16enc(&vtoc8.magic, VTOC_MAGIC);

	be32enc(&vtoc8.map[VTOC_RAW_PART].nblks, imgsz);
	TAILQ_FOREACH(part, &partlist, link) {
		n = part->index + ((part->index >= VTOC_RAW_PART) ? 1 : 0);
		be16enc(&vtoc8.part[n].tag, ALIAS_TYPE2INT(part->type));
		be32enc(&vtoc8.map[n].cyl, part->block / (nsecs * nheads));
		be32enc(&vtoc8.map[n].nblks, part->size);
	}

	/* Calculate checksum. */
	sum = 0;
	p = (void *)&vtoc8;
	for (ofs = 0; ofs < sizeof(vtoc8) - 2; ofs += 2)
		sum ^= be16dec(p + ofs);
	be16enc(&vtoc8.cksum, sum);

	error = image_write(0, &vtoc8, 1);
	return (error);
}

static struct mkimg_scheme vtoc8_scheme = {
	.name = "vtoc8",
	.description = "SMI VTOC8 disk labels",
	.aliases = vtoc8_aliases,
	.metadata = vtoc8_metadata,
	.write = vtoc8_write,
	.nparts = VTOC8_NPARTS - 1,
	.maxsecsz = 512
};

SCHEME_DEFINE(vtoc8_scheme);
