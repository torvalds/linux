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
#include <stdlib.h>
#include <string.h>

#include "endian.h"
#include "image.h"
#include "format.h"
#include "mkimg.h"

#define	VMDK_IMAGE_ROUND	1048576
#define	VMDK_MIN_GRAIN_SIZE	8192
#define	VMDK_SECTOR_SIZE	512

struct vmdk_header {
	uint32_t	magic;
#define	VMDK_MAGIC		0x564d444b
	uint32_t	version;
#define	VMDK_VERSION		1
	uint32_t	flags;
#define	VMDK_FLAGS_NL_TEST	(1 << 0)
#define	VMDK_FLAGS_RGT_USED	(1 << 1)
#define	VMDK_FLAGS_COMPRESSED	(1 << 16)
#define	VMDK_FLAGS_MARKERS	(1 << 17)
	uint64_t	capacity;
	uint64_t	grain_size;
	uint64_t	desc_offset;
	uint64_t	desc_size;
	uint32_t	ngtes;
#define	VMDK_NGTES		512
	uint64_t	rgd_offset;
	uint64_t	gd_offset;
	uint64_t	overhead;
	uint8_t		unclean;
	uint32_t	nl_test;
#define	VMDK_NL_TEST		0x0a200d0a
	uint16_t	compress;
#define	VMDK_COMPRESS_NONE	0
#define	VMDK_COMPRESS_DEFLATE	1
	char		padding[433];
} __attribute__((__packed__));

static const char desc_fmt[] =
    "# Disk DescriptorFile\n"
    "version=%d\n"
    "CID=%08x\n"
    "parentCID=ffffffff\n"
    "createType=\"monolithicSparse\"\n"
    "# Extent description\n"
    "RW %ju SPARSE \"%s\"\n"
    "# The Disk Data Base\n"
    "#DDB\n"
    "ddb.adapterType = \"ide\"\n"
    "ddb.geometry.cylinders = \"%u\"\n"
    "ddb.geometry.heads = \"%u\"\n"
    "ddb.geometry.sectors = \"%u\"\n";

static uint64_t grainsz;

static int
vmdk_resize(lba_t imgsz)
{
	uint64_t imagesz;

	imagesz = imgsz * secsz;
	imagesz = (imagesz + VMDK_IMAGE_ROUND - 1) & ~(VMDK_IMAGE_ROUND - 1);
	grainsz = (blksz < VMDK_MIN_GRAIN_SIZE) ? VMDK_MIN_GRAIN_SIZE : blksz;

	if (verbose)
		fprintf(stderr, "VMDK: image size = %ju, grain size = %ju\n",
		    (uintmax_t)imagesz, (uintmax_t)grainsz);

	grainsz /= VMDK_SECTOR_SIZE;
	return (image_set_size(imagesz / secsz));
}

static int
vmdk_write(int fd)
{
	struct vmdk_header hdr;
	uint32_t *gt, *gd, *rgd;
	char *buf, *desc;
	off_t cur, lim;
	uint64_t imagesz;
	lba_t blkofs, blkcnt;
	size_t gdsz, gtsz;
	uint32_t sec, cursec;
	int error, desc_len, n, ngrains, ngts;

	imagesz = (image_get_size() * secsz) / VMDK_SECTOR_SIZE;

	memset(&hdr, 0, sizeof(hdr));
	le32enc(&hdr.magic, VMDK_MAGIC);
	le32enc(&hdr.version, VMDK_VERSION);
	le32enc(&hdr.flags, VMDK_FLAGS_NL_TEST | VMDK_FLAGS_RGT_USED);
	le64enc(&hdr.capacity, imagesz);
	le64enc(&hdr.grain_size, grainsz);

	n = asprintf(&desc, desc_fmt, 1 /*version*/, 0 /*CID*/,
	    (uintmax_t)imagesz /*size*/, "" /*name*/,
	    ncyls /*cylinders*/, nheads /*heads*/, nsecs /*sectors*/);
	if (n == -1)
		return (ENOMEM);

	desc_len = (n + VMDK_SECTOR_SIZE - 1) & ~(VMDK_SECTOR_SIZE - 1);
	desc = realloc(desc, desc_len);
	memset(desc + n, 0, desc_len - n);

	le64enc(&hdr.desc_offset, 1);
	le64enc(&hdr.desc_size, desc_len / VMDK_SECTOR_SIZE);
	le32enc(&hdr.ngtes, VMDK_NGTES);

	sec = desc_len / VMDK_SECTOR_SIZE + 1;

	ngrains = imagesz / grainsz;
	ngts = (ngrains + VMDK_NGTES - 1) / VMDK_NGTES;
	gdsz = (ngts * sizeof(uint32_t) + VMDK_SECTOR_SIZE - 1) &
	    ~(VMDK_SECTOR_SIZE - 1);

	gd = calloc(1, gdsz);
	if (gd == NULL) {
		free(desc);
		return (ENOMEM);
	}
	le64enc(&hdr.gd_offset, sec);
	sec += gdsz / VMDK_SECTOR_SIZE;
	for (n = 0; n < ngts; n++) {
		le32enc(gd + n, sec);
		sec += VMDK_NGTES * sizeof(uint32_t) / VMDK_SECTOR_SIZE;
	}

	rgd = calloc(1, gdsz);
	if (rgd == NULL) {
		free(gd);
		free(desc);
		return (ENOMEM);
	}
	le64enc(&hdr.rgd_offset, sec);
	sec += gdsz / VMDK_SECTOR_SIZE;
	for (n = 0; n < ngts; n++) {
		le32enc(rgd + n, sec);
		sec += VMDK_NGTES * sizeof(uint32_t) / VMDK_SECTOR_SIZE;
	}

	sec = (sec + grainsz - 1) & ~(grainsz - 1);

	if (verbose)
		fprintf(stderr, "VMDK: overhead = %ju\n",
		    (uintmax_t)(sec * VMDK_SECTOR_SIZE));

	le64enc(&hdr.overhead, sec);
	be32enc(&hdr.nl_test, VMDK_NL_TEST);

	gt = calloc(ngts, VMDK_NGTES * sizeof(uint32_t));
	if (gt == NULL) {
		free(rgd);
		free(gd);
		free(desc);
		return (ENOMEM);
	}
	gtsz = ngts * VMDK_NGTES * sizeof(uint32_t);

	cursec = sec;
	blkcnt = (grainsz * VMDK_SECTOR_SIZE) / secsz;
	for (n = 0; n < ngrains; n++) {
		blkofs = n * blkcnt;
		if (image_data(blkofs, blkcnt)) {
			le32enc(gt + n, cursec);
			cursec += grainsz;
		}
	}

	error = 0;
	if (!error && sparse_write(fd, &hdr, VMDK_SECTOR_SIZE) < 0)
		error = errno;
	if (!error && sparse_write(fd, desc, desc_len) < 0)
		error = errno;
	if (!error && sparse_write(fd, gd, gdsz) < 0)
		error = errno;
	if (!error && sparse_write(fd, gt, gtsz) < 0)
		error = errno;
	if (!error && sparse_write(fd, rgd, gdsz) < 0)
		error = errno;
	if (!error && sparse_write(fd, gt, gtsz) < 0)
		error = errno;
	free(gt);
	free(rgd);
	free(gd);
	free(desc);
	if (error)
		return (error);

	cur = VMDK_SECTOR_SIZE + desc_len + (gdsz + gtsz) * 2;
	lim = sec * VMDK_SECTOR_SIZE;
	if (cur < lim) {
		buf = calloc(1, VMDK_SECTOR_SIZE);
		if (buf == NULL)
			error = ENOMEM;
		while (!error && cur < lim) {
			if (sparse_write(fd, buf, VMDK_SECTOR_SIZE) < 0)
				error = errno;
			cur += VMDK_SECTOR_SIZE;
		}
		if (buf != NULL)
			free(buf);
	}
	if (error)
		return (error);

	blkcnt = (grainsz * VMDK_SECTOR_SIZE) / secsz;
	for (n = 0; n < ngrains; n++) {
		blkofs = n * blkcnt;
		if (image_data(blkofs, blkcnt)) {
			error = image_copyout_region(fd, blkofs, blkcnt);
			if (error)
				return (error);
		}
	}
	return (image_copyout_done(fd));
}

static struct mkimg_format vmdk_format = {
	.name = "vmdk",
	.description = "Virtual Machine Disk",
	.resize = vmdk_resize,
	.write = vmdk_write,
};

FORMAT_DEFINE(vmdk_format);
