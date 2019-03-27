/*-
 * Copyright (c) 2014, 2015 Marcel Moolenaar
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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "endian.h"
#include "image.h"
#include "format.h"
#include "mkimg.h"

#ifndef __has_extension
#define	__has_extension(x)	0
#endif

/*
 * General notes:
 * o   File is in network byte order.
 * o   The timestamp is seconds since 1/1/2000 12:00:00 AM UTC
 *
 * This file is divided in 3 parts:
 * 1.  Common definitions
 * 2.  Dynamic VHD support
 * 3.  Fixed VHD support
 */

/*
 * PART 1: Common definitions
 */

#define	VHD_SECTOR_SIZE	512
#define	VHD_BLOCK_SIZE	(4096 * VHD_SECTOR_SIZE)	/* 2MB blocks */

struct vhd_geom {
	uint16_t	cylinders;
	uint8_t		heads;
	uint8_t		sectors;
};

struct vhd_footer {
	uint64_t	cookie;
#define	VHD_FOOTER_COOKIE	0x636f6e6563746978ULL
	uint32_t	features;
#define	VHD_FEATURES_TEMPORARY	0x01
#define	VHD_FEATURES_RESERVED	0x02
	uint32_t	version;
#define	VHD_VERSION		0x00010000
	uint64_t	data_offset;
	uint32_t	timestamp;
	uint32_t	creator_tool;
#define	VHD_CREATOR_TOOL	0x2a696d67	/* FreeBSD mkimg */
	uint32_t	creator_version;
#define	VHD_CREATOR_VERSION	0x00020000
	uint32_t	creator_os;
#define	VHD_CREATOR_OS		0x5769326b	/* Wi2k */
	uint64_t	original_size;
	uint64_t	current_size;
	struct vhd_geom	geometry;
	uint32_t	disk_type;
#define	VHD_DISK_TYPE_FIXED	2
#define	VHD_DISK_TYPE_DYNAMIC	3
#define	VHD_DISK_TYPE_DIFF	4
	uint32_t	checksum;
	mkimg_uuid_t	id;
	uint8_t		saved_state;
	uint8_t		_reserved[427];
};
#if __has_extension(c_static_assert)
_Static_assert(sizeof(struct vhd_footer) == VHD_SECTOR_SIZE,
    "Wrong size for footer");
#endif

static uint32_t
vhd_checksum(void *buf, size_t sz)
{
	uint8_t *p = buf;
	uint32_t sum;
	size_t ofs;

	sum = 0;
	for (ofs = 0; ofs < sz; ofs++)
		sum += p[ofs];
	return (~sum);
}

static void
vhd_geometry(uint64_t image_size, struct vhd_geom *geom)
{
	lba_t imgsz;
	long cth;

	imgsz = image_size / VHD_SECTOR_SIZE;

	/* Respect command line options if possible. */
	if (nheads > 1 && nheads < 256 &&
	    nsecs > 1 && nsecs < 256 &&
	    ncyls < 65536) {
		geom->cylinders = (ncyls != 0) ? ncyls :
		    imgsz / (nheads * nsecs);
		geom->heads = nheads;
		geom->sectors = nsecs;
		return;
	}

	if (imgsz > 65536 * 16 * 255)
		imgsz = 65536 * 16 * 255;
	if (imgsz >= 65535 * 16 * 63) {
		geom->cylinders = imgsz / (16 * 255);
		geom->heads = 16;
		geom->sectors = 255;
		return;
	}
	geom->sectors = 17;
	cth = imgsz / 17;
	geom->heads = (cth + 1023) / 1024;
	if (geom->heads < 4)
		geom->heads = 4;
	if (cth >= (geom->heads * 1024) || geom->heads > 16) {
		geom->heads = 16;
		geom->sectors = 31;
		cth = imgsz / 31;
	}
	if (cth >= (geom->heads * 1024)) {
		geom->heads = 16;
		geom->sectors = 63;
		cth = imgsz / 63;
	}
	geom->cylinders = cth / geom->heads;
}

static uint64_t
vhd_resize(uint64_t origsz)
{
	struct vhd_geom geom;
	uint64_t newsz;

	/*
	 * Round the image size to the pre-determined geometry that
	 * matches the image size. This circular dependency implies
	 * that we need to loop to handle boundary conditions.
	 * The first time, newsz equals origsz and the geometry will
	 * typically yield a new size that's smaller. We keep adding
	 * cylinder's worth of sectors to the new size until its
	 * larger or equal or origsz. But during those iterations,
	 * the geometry can change, so we need to account for that.
	 */
	newsz = origsz;
	while (1) {
		vhd_geometry(newsz, &geom);
		newsz = (int64_t)geom.cylinders * geom.heads *
		    geom.sectors * VHD_SECTOR_SIZE;
		if (newsz >= origsz)
			break;
		newsz += geom.heads * geom.sectors * VHD_SECTOR_SIZE;
	}
	return (newsz);
}

static uint32_t
vhd_timestamp(void)
{
	time_t t;

	if (!unit_testing) {
		t = time(NULL);
		return (t - 0x386d4380);
	}

	return (0x01234567);
}

static void
vhd_make_footer(struct vhd_footer *footer, uint64_t image_size,
    uint32_t disk_type, uint64_t data_offset)
{
	mkimg_uuid_t id;

	memset(footer, 0, sizeof(*footer));
	be64enc(&footer->cookie, VHD_FOOTER_COOKIE);
	be32enc(&footer->features, VHD_FEATURES_RESERVED);
	be32enc(&footer->version, VHD_VERSION);
	be64enc(&footer->data_offset, data_offset);
	be32enc(&footer->timestamp, vhd_timestamp());
	be32enc(&footer->creator_tool, VHD_CREATOR_TOOL);
	be32enc(&footer->creator_version, VHD_CREATOR_VERSION);
	be32enc(&footer->creator_os, VHD_CREATOR_OS);
	be64enc(&footer->original_size, image_size);
	be64enc(&footer->current_size, image_size);
	vhd_geometry(image_size, &footer->geometry);
	be16enc(&footer->geometry.cylinders, footer->geometry.cylinders);
	be32enc(&footer->disk_type, disk_type);
	mkimg_uuid(&id);
	mkimg_uuid_enc(&footer->id, &id);
	be32enc(&footer->checksum, vhd_checksum(footer, sizeof(*footer)));
}

/*
 * PART 2: Dynamic VHD support
 *
 * Notes:
 * o   File layout:
 *	copy of disk footer
 *	dynamic disk header
 *	block allocation table (BAT)
 *	data blocks
 *	disk footer
 */

struct vhd_dyn_header {
	uint64_t	cookie;
#define	VHD_HEADER_COOKIE	0x6378737061727365ULL
	uint64_t	data_offset;
	uint64_t	table_offset;
	uint32_t	version;
	uint32_t	max_entries;
	uint32_t	block_size;
	uint32_t	checksum;
	mkimg_uuid_t	parent_id;
	uint32_t	parent_timestamp;
	char		_reserved1[4];
	uint16_t	parent_name[256];	/* UTF-16 */
	struct {
		uint32_t	code;
		uint32_t	data_space;
		uint32_t	data_length;
		uint32_t	_reserved;
		uint64_t	data_offset;
	} parent_locator[8];
	char		_reserved2[256];
};
#if __has_extension(c_static_assert)
_Static_assert(sizeof(struct vhd_dyn_header) == VHD_SECTOR_SIZE * 2,
    "Wrong size for header");
#endif

static int
vhd_dyn_resize(lba_t imgsz)
{
	uint64_t imagesz;

	imagesz = vhd_resize(imgsz * secsz);
	return (image_set_size(imagesz / secsz));
}

static int
vhd_dyn_write(int fd)
{
	struct vhd_footer footer;
	struct vhd_dyn_header header;
	uint64_t imgsz, rawsz;
	lba_t blk, blkcnt, nblks;
	uint32_t *bat;
	void *bitmap;
	size_t batsz;
	uint32_t sector;
	int bat_entries, error, entry;

	rawsz = image_get_size() * secsz;
	imgsz = (rawsz + VHD_BLOCK_SIZE - 1) & ~(VHD_BLOCK_SIZE - 1);

	vhd_make_footer(&footer, rawsz, VHD_DISK_TYPE_DYNAMIC, sizeof(footer));
	if (sparse_write(fd, &footer, sizeof(footer)) < 0)
		return (errno);

	bat_entries = imgsz / VHD_BLOCK_SIZE;
	memset(&header, 0, sizeof(header));
	be64enc(&header.cookie, VHD_HEADER_COOKIE);
	be64enc(&header.data_offset, ~0ULL);
	be64enc(&header.table_offset, sizeof(footer) + sizeof(header));
	be32enc(&header.version, VHD_VERSION);
	be32enc(&header.max_entries, bat_entries);
	be32enc(&header.block_size, VHD_BLOCK_SIZE);
	be32enc(&header.checksum, vhd_checksum(&header, sizeof(header)));
	if (sparse_write(fd, &header, sizeof(header)) < 0)
		return (errno);

	batsz = bat_entries * sizeof(uint32_t);
	batsz = (batsz + VHD_SECTOR_SIZE - 1) & ~(VHD_SECTOR_SIZE - 1);
	bat = malloc(batsz);
	if (bat == NULL)
		return (errno);
	memset(bat, 0xff, batsz);
	blkcnt = VHD_BLOCK_SIZE / secsz;
	sector = (sizeof(footer) + sizeof(header) + batsz) / VHD_SECTOR_SIZE;
	for (entry = 0; entry < bat_entries; entry++) {
		blk = entry * blkcnt;
		if (image_data(blk, blkcnt)) {
			be32enc(&bat[entry], sector);
			sector += (VHD_BLOCK_SIZE / VHD_SECTOR_SIZE) + 1;
		}
	}
	if (sparse_write(fd, bat, batsz) < 0) {
		free(bat);
		return (errno);
	}
	free(bat);

	bitmap = malloc(VHD_SECTOR_SIZE);
	if (bitmap == NULL)
		return (errno);
	memset(bitmap, 0xff, VHD_SECTOR_SIZE);

	blk = 0;
	blkcnt = VHD_BLOCK_SIZE / secsz;
	error = 0;
	nblks = rawsz / secsz;
	while (blk < nblks) {
		if (!image_data(blk, blkcnt)) {
			blk += blkcnt;
			continue;
		}
		if (sparse_write(fd, bitmap, VHD_SECTOR_SIZE) < 0) {
			error = errno;
			break;
		}
		/* Handle partial last block */
		if (blk + blkcnt > nblks)
			blkcnt = nblks - blk;
		error = image_copyout_region(fd, blk, blkcnt);
		if (error)
			break;
		blk += blkcnt;
	}
	free(bitmap);
	if (error)
		return (error);
	error = image_copyout_zeroes(fd, imgsz - rawsz);
	if (error)
		return (error);
	if (sparse_write(fd, &footer, sizeof(footer)) < 0)
		return (errno);

	return (0);
}

static struct mkimg_format vhd_dyn_format = {
	.name = "vhd",
	.description = "Virtual Hard Disk",
	.resize = vhd_dyn_resize,
	.write = vhd_dyn_write,
};

FORMAT_DEFINE(vhd_dyn_format);

/*
 * PART 3: Fixed VHD
 */

static int
vhd_fix_resize(lba_t imgsz)
{
	uint64_t imagesz;

	imagesz = vhd_resize(imgsz * secsz);
	/*
	 * Azure demands that images are a whole number of megabytes.
	 */
	imagesz = (imagesz + 0xfffffULL) & ~0xfffffULL;
	return (image_set_size(imagesz / secsz));
}

static int
vhd_fix_write(int fd)
{
	struct vhd_footer footer;
	uint64_t imagesz;
	int error;

	error = image_copyout(fd);
	if (error)
		return (error);

	imagesz = image_get_size() * secsz;
	vhd_make_footer(&footer, imagesz, VHD_DISK_TYPE_FIXED, ~0ULL);
	error = (sparse_write(fd, &footer, sizeof(footer)) < 0) ? errno : 0;
	return (error);
}

static struct mkimg_format vhd_fix_format = {
	.name = "vhdf",
	.description = "Fixed Virtual Hard Disk",
	.resize = vhd_fix_resize,
	.write = vhd_fix_write,
};

FORMAT_DEFINE(vhd_fix_format);
