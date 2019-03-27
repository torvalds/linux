/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>	/* powerof2() */
#include <sys/queue.h>

#include <bitstring.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pjdlog.h>

#include "activemap.h"

#ifndef	PJDLOG_ASSERT
#include <assert.h>
#define	PJDLOG_ASSERT(...)	assert(__VA_ARGS__)
#endif

#define	ACTIVEMAP_MAGIC	0xac71e4
struct activemap {
	int		 am_magic;	/* Magic value. */
	off_t		 am_mediasize;	/* Media size in bytes. */
	uint32_t	 am_extentsize;	/* Extent size in bytes,
					   must be power of 2. */
	uint8_t		 am_extentshift;/* 2 ^ extentbits == extentsize */
	int		 am_nextents;	/* Number of extents. */
	size_t		 am_mapsize;	/* Bitmap size in bytes. */
	uint16_t	*am_memtab;	/* An array that holds number of pending
					   writes per extent. */
	bitstr_t	*am_diskmap;	/* On-disk bitmap of dirty extents. */
	bitstr_t	*am_memmap;	/* In-memory bitmap of dirty extents. */
	size_t		 am_diskmapsize; /* Map size rounded up to sector size. */
	uint64_t	 am_ndirty;	/* Number of dirty regions. */
	bitstr_t	*am_syncmap;	/* Bitmap of extents to sync. */
	off_t		 am_syncoff;	/* Next synchronization offset. */
	TAILQ_HEAD(skeepdirty, keepdirty) am_keepdirty; /* List of extents that
					   we keep dirty to reduce bitmap
					   updates. */
	int		 am_nkeepdirty;	/* Number of am_keepdirty elements. */
	int		 am_nkeepdirty_limit; /* Maximum number of am_keepdirty
					         elements. */
};

struct keepdirty {
	int	kd_extent;
	TAILQ_ENTRY(keepdirty) kd_next;
};

/*
 * Helper function taken from sys/systm.h to calculate extentshift.
 */
static uint32_t
bitcount32(uint32_t x)
{

	x = (x & 0x55555555) + ((x & 0xaaaaaaaa) >> 1);
	x = (x & 0x33333333) + ((x & 0xcccccccc) >> 2);
	x = (x + (x >> 4)) & 0x0f0f0f0f;
	x = (x + (x >> 8));
	x = (x + (x >> 16)) & 0x000000ff;
	return (x);
}

static __inline int
off2ext(const struct activemap *amp, off_t offset)
{
	int extent;

	PJDLOG_ASSERT(offset >= 0 && offset < amp->am_mediasize);
	extent = (offset >> amp->am_extentshift);
	PJDLOG_ASSERT(extent >= 0 && extent < amp->am_nextents);
	return (extent);
}

static __inline off_t
ext2off(const struct activemap *amp, int extent)
{
	off_t offset;

	PJDLOG_ASSERT(extent >= 0 && extent < amp->am_nextents);
	offset = ((off_t)extent << amp->am_extentshift);
	PJDLOG_ASSERT(offset >= 0 && offset < amp->am_mediasize);
	return (offset);
}

/*
 * Function calculates number of requests needed to synchronize the given
 * extent.
 */
static __inline int
ext2reqs(const struct activemap *amp, int ext)
{
	off_t left;

	if (ext < amp->am_nextents - 1)
		return (((amp->am_extentsize - 1) / MAXPHYS) + 1);

	PJDLOG_ASSERT(ext == amp->am_nextents - 1);
	left = amp->am_mediasize % amp->am_extentsize;
	if (left == 0)
		left = amp->am_extentsize;
	return (((left - 1) / MAXPHYS) + 1);
}

/*
 * Initialize activemap structure and allocate memory for internal needs.
 * Function returns 0 on success and -1 if any of the allocations failed.
 */
int
activemap_init(struct activemap **ampp, uint64_t mediasize, uint32_t extentsize,
    uint32_t sectorsize, uint32_t keepdirty)
{
	struct activemap *amp;

	PJDLOG_ASSERT(ampp != NULL);
	PJDLOG_ASSERT(mediasize > 0);
	PJDLOG_ASSERT(extentsize > 0);
	PJDLOG_ASSERT(powerof2(extentsize));
	PJDLOG_ASSERT(sectorsize > 0);
	PJDLOG_ASSERT(powerof2(sectorsize));
	PJDLOG_ASSERT(keepdirty > 0);

	amp = malloc(sizeof(*amp));
	if (amp == NULL)
		return (-1);

	amp->am_mediasize = mediasize;
	amp->am_nkeepdirty_limit = keepdirty;
	amp->am_extentsize = extentsize;
	amp->am_extentshift = bitcount32(extentsize - 1);
	amp->am_nextents = ((mediasize - 1) / extentsize) + 1;
	amp->am_mapsize = bitstr_size(amp->am_nextents);
	amp->am_diskmapsize = roundup2(amp->am_mapsize, sectorsize);
	amp->am_ndirty = 0;
	amp->am_syncoff = -2;
	TAILQ_INIT(&amp->am_keepdirty);
	amp->am_nkeepdirty = 0;

	amp->am_memtab = calloc(amp->am_nextents, sizeof(amp->am_memtab[0]));
	amp->am_diskmap = calloc(1, amp->am_diskmapsize);
	amp->am_memmap = bit_alloc(amp->am_nextents);
	amp->am_syncmap = bit_alloc(amp->am_nextents);

	/*
	 * Check to see if any of the allocations above failed.
	 */
	if (amp->am_memtab == NULL || amp->am_diskmap == NULL ||
	    amp->am_memmap == NULL || amp->am_syncmap == NULL) {
		if (amp->am_memtab != NULL)
			free(amp->am_memtab);
		if (amp->am_diskmap != NULL)
			free(amp->am_diskmap);
		if (amp->am_memmap != NULL)
			free(amp->am_memmap);
		if (amp->am_syncmap != NULL)
			free(amp->am_syncmap);
		amp->am_magic = 0;
		free(amp);
		errno = ENOMEM;
		return (-1);
	}

	amp->am_magic = ACTIVEMAP_MAGIC;
	*ampp = amp;

	return (0);
}

static struct keepdirty *
keepdirty_find(struct activemap *amp, int extent)
{
	struct keepdirty *kd;

	TAILQ_FOREACH(kd, &amp->am_keepdirty, kd_next) {
		if (kd->kd_extent == extent)
			break;
	}
	return (kd);
}

static bool
keepdirty_add(struct activemap *amp, int extent)
{
	struct keepdirty *kd;

	kd = keepdirty_find(amp, extent);
	if (kd != NULL) {
		/*
		 * Only move element at the beginning.
		 */
		TAILQ_REMOVE(&amp->am_keepdirty, kd, kd_next);
		TAILQ_INSERT_HEAD(&amp->am_keepdirty, kd, kd_next);
		return (false);
	}
	/*
	 * Add new element, but first remove the most unused one if
	 * we have too many.
	 */
	if (amp->am_nkeepdirty >= amp->am_nkeepdirty_limit) {
		kd = TAILQ_LAST(&amp->am_keepdirty, skeepdirty);
		PJDLOG_ASSERT(kd != NULL);
		TAILQ_REMOVE(&amp->am_keepdirty, kd, kd_next);
		amp->am_nkeepdirty--;
		PJDLOG_ASSERT(amp->am_nkeepdirty > 0);
	}
	if (kd == NULL)
		kd = malloc(sizeof(*kd));
	/* We can ignore allocation failure. */
	if (kd != NULL) {
		kd->kd_extent = extent;
		amp->am_nkeepdirty++;
		TAILQ_INSERT_HEAD(&amp->am_keepdirty, kd, kd_next);
	}

	return (true);
}

static void
keepdirty_fill(struct activemap *amp)
{
	struct keepdirty *kd;

	TAILQ_FOREACH(kd, &amp->am_keepdirty, kd_next)
		bit_set(amp->am_diskmap, kd->kd_extent);
}

static void
keepdirty_free(struct activemap *amp)
{
	struct keepdirty *kd;

	while ((kd = TAILQ_FIRST(&amp->am_keepdirty)) != NULL) {
		TAILQ_REMOVE(&amp->am_keepdirty, kd, kd_next);
		amp->am_nkeepdirty--;
		free(kd);
	}
	PJDLOG_ASSERT(amp->am_nkeepdirty == 0);
}

/*
 * Function frees resources allocated by activemap_init() function.
 */
void
activemap_free(struct activemap *amp)
{

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);

	amp->am_magic = 0;

	keepdirty_free(amp);
	free(amp->am_memtab);
	free(amp->am_diskmap);
	free(amp->am_memmap);
	free(amp->am_syncmap);
}

/*
 * Function should be called before we handle write requests. It updates
 * internal structures and returns true if on-disk metadata should be updated.
 */
bool
activemap_write_start(struct activemap *amp, off_t offset, off_t length)
{
	bool modified;
	off_t end;
	int ext;

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);
	PJDLOG_ASSERT(length > 0);

	modified = false;
	end = offset + length - 1;

	for (ext = off2ext(amp, offset); ext <= off2ext(amp, end); ext++) {
		/*
		 * If the number of pending writes is increased from 0,
		 * we have to mark the extent as dirty also in on-disk bitmap.
		 * By returning true we inform the caller that on-disk bitmap
		 * was modified and has to be flushed to disk.
		 */
		if (amp->am_memtab[ext]++ == 0) {
			PJDLOG_ASSERT(!bit_test(amp->am_memmap, ext));
			bit_set(amp->am_memmap, ext);
			amp->am_ndirty++;
		}
		if (keepdirty_add(amp, ext))
			modified = true;
	}

	return (modified);
}

/*
 * Function should be called after receiving write confirmation. It updates
 * internal structures and returns true if on-disk metadata should be updated.
 */
bool
activemap_write_complete(struct activemap *amp, off_t offset, off_t length)
{
	bool modified;
	off_t end;
	int ext;

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);
	PJDLOG_ASSERT(length > 0);

	modified = false;
	end = offset + length - 1;

	for (ext = off2ext(amp, offset); ext <= off2ext(amp, end); ext++) {
		/*
		 * If the number of pending writes goes down to 0, we have to
		 * mark the extent as clean also in on-disk bitmap.
		 * By returning true we inform the caller that on-disk bitmap
		 * was modified and has to be flushed to disk.
		 */
		PJDLOG_ASSERT(amp->am_memtab[ext] > 0);
		PJDLOG_ASSERT(bit_test(amp->am_memmap, ext));
		if (--amp->am_memtab[ext] == 0) {
			bit_clear(amp->am_memmap, ext);
			amp->am_ndirty--;
			if (keepdirty_find(amp, ext) == NULL)
				modified = true;
		}
	}

	return (modified);
}

/*
 * Function should be called after finishing synchronization of one extent.
 * It returns true if on-disk metadata should be updated.
 */
bool
activemap_extent_complete(struct activemap *amp, int extent)
{
	bool modified;
	int reqs;

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);
	PJDLOG_ASSERT(extent >= 0 && extent < amp->am_nextents);

	modified = false;

	reqs = ext2reqs(amp, extent);
	PJDLOG_ASSERT(amp->am_memtab[extent] >= reqs);
	amp->am_memtab[extent] -= reqs;
	PJDLOG_ASSERT(bit_test(amp->am_memmap, extent));
	if (amp->am_memtab[extent] == 0) {
		bit_clear(amp->am_memmap, extent);
		amp->am_ndirty--;
		modified = true;
	}

	return (modified);
}

/*
 * Function returns number of dirty regions.
 */
uint64_t
activemap_ndirty(const struct activemap *amp)
{

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);

	return (amp->am_ndirty);
}

/*
 * Function compare on-disk bitmap and in-memory bitmap and returns true if
 * they differ and should be flushed to the disk.
 */
bool
activemap_differ(const struct activemap *amp)
{

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);

	return (memcmp(amp->am_diskmap, amp->am_memmap,
	    amp->am_mapsize) != 0);
}

/*
 * Function returns number of bytes used by bitmap.
 */
size_t
activemap_size(const struct activemap *amp)
{

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);

	return (amp->am_mapsize);
}

/*
 * Function returns number of bytes needed for storing on-disk bitmap.
 * This is the same as activemap_size(), but rounded up to sector size.
 */
size_t
activemap_ondisk_size(const struct activemap *amp)
{

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);

	return (amp->am_diskmapsize);
}

/*
 * Function copies the given buffer read from disk to the internal bitmap.
 */
void
activemap_copyin(struct activemap *amp, const unsigned char *buf, size_t size)
{
	int ext;

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);
	PJDLOG_ASSERT(size >= amp->am_mapsize);

	memcpy(amp->am_diskmap, buf, amp->am_mapsize);
	memcpy(amp->am_memmap, buf, amp->am_mapsize);
	memcpy(amp->am_syncmap, buf, amp->am_mapsize);

	bit_ffs(amp->am_memmap, amp->am_nextents, &ext);
	if (ext == -1) {
		/* There are no dirty extents, so we can leave now. */
		return;
	}
	/*
	 * Set synchronization offset to the first dirty extent.
	 */
	activemap_sync_rewind(amp);
	/*
	 * We have dirty extents and we want them to stay that way until
	 * we synchronize, so we set number of pending writes to number
	 * of requests needed to synchronize one extent.
	 */
	amp->am_ndirty = 0;
	for (; ext < amp->am_nextents; ext++) {
		if (bit_test(amp->am_memmap, ext)) {
			amp->am_memtab[ext] = ext2reqs(amp, ext);
			amp->am_ndirty++;
		}
	}
}

/*
 * Function merges the given bitmap with existing one.
 */
void
activemap_merge(struct activemap *amp, const unsigned char *buf, size_t size)
{
	bitstr_t *remmap = __DECONST(bitstr_t *, buf);
	int ext;

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);
	PJDLOG_ASSERT(size >= amp->am_mapsize);

	bit_ffs(remmap, amp->am_nextents, &ext);
	if (ext == -1) {
		/* There are no dirty extents, so we can leave now. */
		return;
	}
	/*
	 * We have dirty extents and we want them to stay that way until
	 * we synchronize, so we set number of pending writes to number
	 * of requests needed to synchronize one extent.
	 */
	for (; ext < amp->am_nextents; ext++) {
		/* Local extent already dirty. */
		if (bit_test(amp->am_syncmap, ext))
			continue;
		/* Remote extent isn't dirty. */
		if (!bit_test(remmap, ext))
			continue;
		bit_set(amp->am_syncmap, ext);
		bit_set(amp->am_memmap, ext);
		bit_set(amp->am_diskmap, ext);
		if (amp->am_memtab[ext] == 0)
			amp->am_ndirty++;
		amp->am_memtab[ext] = ext2reqs(amp, ext);
	}
	/*
	 * Set synchronization offset to the first dirty extent.
	 */
	activemap_sync_rewind(amp);
}

/*
 * Function returns pointer to internal bitmap that should be written to disk.
 */
const unsigned char *
activemap_bitmap(struct activemap *amp, size_t *sizep)
{

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);

	if (sizep != NULL)
		*sizep = amp->am_diskmapsize;
	memcpy(amp->am_diskmap, amp->am_memmap, amp->am_mapsize);
	keepdirty_fill(amp);
	return ((const unsigned char *)amp->am_diskmap);
}

/*
 * Function calculates size needed to store bitmap on disk.
 */
size_t
activemap_calc_ondisk_size(uint64_t mediasize, uint32_t extentsize,
    uint32_t sectorsize)
{
	uint64_t nextents, mapsize;

	PJDLOG_ASSERT(mediasize > 0);
	PJDLOG_ASSERT(extentsize > 0);
	PJDLOG_ASSERT(powerof2(extentsize));
	PJDLOG_ASSERT(sectorsize > 0);
	PJDLOG_ASSERT(powerof2(sectorsize));

	nextents = ((mediasize - 1) / extentsize) + 1;
	mapsize = bitstr_size(nextents);
	return (roundup2(mapsize, sectorsize));
}

/*
 * Set synchronization offset to the first dirty extent.
 */
void
activemap_sync_rewind(struct activemap *amp)
{
	int ext;

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);

	bit_ffs(amp->am_syncmap, amp->am_nextents, &ext);
	if (ext == -1) {
		/* There are no extents to synchronize. */
		amp->am_syncoff = -2;
		return;
	}
	/*
	 * Mark that we want to start synchronization from the beginning.
	 */
	amp->am_syncoff = -1;
}

/*
 * Return next offset of where we should synchronize.
 */
off_t
activemap_sync_offset(struct activemap *amp, off_t *lengthp, int *syncextp)
{
	off_t syncoff, left;
	int ext;

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);
	PJDLOG_ASSERT(lengthp != NULL);
	PJDLOG_ASSERT(syncextp != NULL);

	*syncextp = -1;

	if (amp->am_syncoff == -2)
		return (-1);

	if (amp->am_syncoff >= 0 &&
	    (amp->am_syncoff + MAXPHYS >= amp->am_mediasize ||
	     off2ext(amp, amp->am_syncoff) !=
	     off2ext(amp, amp->am_syncoff + MAXPHYS))) {
		/*
		 * We are about to change extent, so mark previous one as clean.
		 */
		ext = off2ext(amp, amp->am_syncoff);
		bit_clear(amp->am_syncmap, ext);
		*syncextp = ext;
		amp->am_syncoff = -1;
	}

	if (amp->am_syncoff == -1) {
		/*
		 * Let's find first extent to synchronize.
		 */
		bit_ffs(amp->am_syncmap, amp->am_nextents, &ext);
		if (ext == -1) {
			amp->am_syncoff = -2;
			return (-1);
		}
		amp->am_syncoff = ext2off(amp, ext);
	} else {
		/*
		 * We don't change extent, so just increase offset.
		 */
		amp->am_syncoff += MAXPHYS;
		if (amp->am_syncoff >= amp->am_mediasize) {
			amp->am_syncoff = -2;
			return (-1);
		}
	}

	syncoff = amp->am_syncoff;
	left = ext2off(amp, off2ext(amp, syncoff)) +
	    amp->am_extentsize - syncoff;
	if (syncoff + left > amp->am_mediasize)
		left = amp->am_mediasize - syncoff;
	if (left > MAXPHYS)
		left = MAXPHYS;

	PJDLOG_ASSERT(left >= 0 && left <= MAXPHYS);
	PJDLOG_ASSERT(syncoff >= 0 && syncoff < amp->am_mediasize);
	PJDLOG_ASSERT(syncoff + left >= 0 &&
	    syncoff + left <= amp->am_mediasize);

	*lengthp = left;
	return (syncoff);
}

/*
 * Mark extent(s) containing the given region for synchronization.
 * Most likely one of the components is unavailable.
 */
bool
activemap_need_sync(struct activemap *amp, off_t offset, off_t length)
{
	bool modified;
	off_t end;
	int ext;

	PJDLOG_ASSERT(amp->am_magic == ACTIVEMAP_MAGIC);

	modified = false;
	end = offset + length - 1;

	for (ext = off2ext(amp, offset); ext <= off2ext(amp, end); ext++) {
		if (bit_test(amp->am_syncmap, ext)) {
			/* Already marked for synchronization. */
			PJDLOG_ASSERT(bit_test(amp->am_memmap, ext));
			continue;
		}
		bit_set(amp->am_syncmap, ext);
		if (!bit_test(amp->am_memmap, ext)) {
			bit_set(amp->am_memmap, ext);
			amp->am_ndirty++;
		}
		amp->am_memtab[ext] += ext2reqs(amp, ext);
		modified = true;
	}

	return (modified);
}

void
activemap_dump(const struct activemap *amp)
{
	int bit;

	printf("M: ");
	for (bit = 0; bit < amp->am_nextents; bit++)
		printf("%d", bit_test(amp->am_memmap, bit) ? 1 : 0);
	printf("\n");
	printf("D: ");
	for (bit = 0; bit < amp->am_nextents; bit++)
		printf("%d", bit_test(amp->am_diskmap, bit) ? 1 : 0);
	printf("\n");
	printf("S: ");
	for (bit = 0; bit < amp->am_nextents; bit++)
		printf("%d", bit_test(amp->am_syncmap, bit) ? 1 : 0);
	printf("\n");
}
