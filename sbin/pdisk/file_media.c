/*	$OpenBSD: file_media.c,v 1.48 2016/01/30 17:21:10 krw Exp $	*/

/*
 * file_media.c -
 *
 * Written by Eryk Vershen
 */

/*
 * Copyright 1997,1998 by Apple Computer, Inc.
 *              All Rights Reserved
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appears in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * APPLE COMPUTER DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL APPLE COMPUTER BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
 * NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>		/* DEV_BSIZE */
#include <sys/queue.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "partition_map.h"
#include "file_media.h"

struct ddmap_ondisk {
    uint8_t	ddBlock[4];
    uint8_t	ddSize[2];
    uint8_t	ddType[2];
};

struct block0_ondisk {
    uint8_t	sbSig[2];
    uint8_t	sbBlkSize[2];
    uint8_t	sbBlkCount[4];
    uint8_t	sbDevType[2];
    uint8_t	sbDevId[2];
    uint8_t	sbData[4];
    uint8_t	sbDrvrCount[2];
    uint8_t	sbDDMap[64];	/* ddmap_ondisk[8] */
    uint8_t	reserved[430];
};

struct dpme_ondisk {
    uint8_t	dpme_signature[2];
    uint8_t	dpme_reserved_1[2];
    uint8_t	dpme_map_entries[4];
    uint8_t	dpme_pblock_start[4];
    uint8_t	dpme_pblocks[4];
    uint8_t	dpme_name[DPISTRLEN];
    uint8_t	dpme_type[DPISTRLEN];
    uint8_t	dpme_lblock_start[4];
    uint8_t	dpme_lblocks[4];
    uint8_t	dpme_flags[4];
    uint8_t	dpme_boot_block[4];
    uint8_t	dpme_boot_bytes[4];
    uint8_t	dpme_load_addr[4];
    uint8_t	dpme_reserved_2[4];
    uint8_t	dpme_goto_addr[4];
    uint8_t	dpme_reserved_3[4];
    uint8_t	dpme_checksum[4];
    uint8_t	dpme_processor_id[16];
    uint8_t	dpme_reserved_4[376];
};

static int	read_block(int, uint64_t, void *);
static int	write_block(int, uint64_t, void *);

static int
read_block(int fd, uint64_t sector, void *address)
{
	ssize_t off;

	off = pread(fd, address, DEV_BSIZE, sector * DEV_BSIZE);
	if (off == DEV_BSIZE)
		return 1;

	if (off == 0)
		fprintf(stderr, "end of file encountered");
	else if (off == -1)
		warn("reading file failed");
	else
		fprintf(stderr, "short read");

	return 0;
}

static int
write_block(int fd, uint64_t sector, void *address)
{
	ssize_t off;

	off = pwrite(fd, address, DEV_BSIZE, sector * DEV_BSIZE);
	if (off == DEV_BSIZE)
		return 1;

	warn("writing to file failed");
	return 0;
}

int
read_block0(int fd, struct partition_map *map)
{
	struct block0_ondisk *block0_ondisk;
	struct ddmap_ondisk ddmap_ondisk;
	int i;

	block0_ondisk = malloc(sizeof(struct block0_ondisk));
	if (block0_ondisk == NULL)
		errx(1, "No memory to read block0");

	if (read_block(fd, 0, block0_ondisk) == 0)
		return 0;

	memcpy(&map->sbSig, block0_ondisk->sbSig,
	    sizeof(map->sbSig));
	map->sbSig = betoh16(map->sbSig);
	memcpy(&map->sbBlkSize, block0_ondisk->sbBlkSize,
	    sizeof(map->sbBlkSize));
	map->sbBlkSize = betoh16(map->sbBlkSize);
	memcpy(&map->sbBlkCount, block0_ondisk->sbBlkCount,
	    sizeof(map->sbBlkCount));
	map->sbBlkCount = betoh32(map->sbBlkCount);
	memcpy(&map->sbDevType, block0_ondisk->sbDevType,
	    sizeof(map->sbDevType));
	map->sbDevType = betoh16(map->sbDevType);
	memcpy(&map->sbDevId, block0_ondisk->sbDevId,
	    sizeof(map->sbDevId));
	map->sbDevId = betoh16(map->sbDevId);
	memcpy(&map->sbData, block0_ondisk->sbData,
	    sizeof(map->sbData));
	map->sbData = betoh32(map->sbData);
	memcpy(&map->sbDrvrCount, block0_ondisk->sbDrvrCount,
	    sizeof(map->sbDrvrCount));
	map->sbDrvrCount = betoh16(map->sbDrvrCount);

	for (i = 0; i < 8; i++) {
		memcpy(&ddmap_ondisk,
		    map->sbDDMap+i*sizeof(struct ddmap_ondisk),
		    sizeof(ddmap_ondisk));
		memcpy(&map->sbDDMap[i].ddBlock, &ddmap_ondisk.ddBlock,
		    sizeof(map->sbDDMap[i].ddBlock));
		map->sbDDMap[i].ddBlock =
		    betoh32(map->sbDDMap[i].ddBlock);
		memcpy(&map->sbDDMap[i].ddSize, &ddmap_ondisk.ddSize,
		    sizeof(map->sbDDMap[i].ddSize));
		map->sbDDMap[i].ddSize = betoh16(map->sbDDMap[i].ddSize);
		memcpy(&map->sbDDMap[i].ddType, &ddmap_ondisk.ddType,
		    sizeof(map->sbDDMap[i].ddType));
		map->sbDDMap[i].ddType = betoh32(map->sbDDMap[i].ddType);
	}

	free(block0_ondisk);
	return 1;
}

int
write_block0(int fd, struct partition_map *map)
{
	struct block0_ondisk *block0_ondisk;
	struct ddmap_ondisk ddmap_ondisk;
	int i, rslt;
	uint32_t tmp32;
	uint16_t tmp16;

	block0_ondisk = malloc(sizeof(struct block0_ondisk));
	if (block0_ondisk == NULL)
		errx(1, "No memory to write block 0");

	tmp16 = htobe16(map->sbSig);
	memcpy(block0_ondisk->sbSig, &tmp16,
	    sizeof(block0_ondisk->sbSig));
	tmp16 = htobe16(map->sbBlkSize);
	memcpy(block0_ondisk->sbBlkSize, &tmp16,
	    sizeof(block0_ondisk->sbBlkSize));
	tmp32 = htobe32(map->sbBlkCount);
	memcpy(block0_ondisk->sbBlkCount, &tmp32,
	    sizeof(block0_ondisk->sbBlkCount));
	tmp16 = htobe16(map->sbDevType);
	memcpy(block0_ondisk->sbDevType, &tmp16,
	    sizeof(block0_ondisk->sbDevType));
	tmp16 = htobe16(map->sbDevId);
	memcpy(block0_ondisk->sbDevId, &tmp16,
	    sizeof(block0_ondisk->sbDevId));
	tmp32 = htobe32(map->sbData);
	memcpy(block0_ondisk->sbData, &tmp32,
	    sizeof(block0_ondisk->sbData));
	tmp16 = htobe16(map->sbDrvrCount);
	memcpy(block0_ondisk->sbDrvrCount, &tmp16,
	    sizeof(block0_ondisk->sbDrvrCount));

	for (i = 0; i < 8; i++) {
		tmp32 = htobe32(map->sbDDMap[i].ddBlock);
		memcpy(ddmap_ondisk.ddBlock, &tmp32,
		    sizeof(ddmap_ondisk.ddBlock));
		tmp16 = htobe16(map->sbDDMap[i].ddSize);
		memcpy(&ddmap_ondisk.ddSize, &tmp16,
		    sizeof(ddmap_ondisk.ddSize));
		tmp16 = betoh32(map->sbDDMap[i].ddType);
		memcpy(&ddmap_ondisk.ddType, &tmp16,
		    sizeof(ddmap_ondisk.ddType));
		memcpy(map->sbDDMap+i*sizeof(struct ddmap_ondisk),
		    &ddmap_ondisk, sizeof(ddmap_ondisk));
	}

	rslt = write_block(fd, 0, block0_ondisk);
	free(block0_ondisk);
	return rslt;
}

int
read_dpme(int fd, uint64_t sector, struct entry *entry)
{
	struct dpme_ondisk *dpme_ondisk;

	dpme_ondisk = malloc(sizeof(struct dpme_ondisk));
	if (dpme_ondisk == NULL)
		errx(1, "No memory to read dpme");

	if (read_block(fd, sector, dpme_ondisk) == 0)
		return 0;

	memcpy(&entry->dpme_signature, dpme_ondisk->dpme_signature,
	    sizeof(entry->dpme_signature));
	memcpy(&entry->dpme_map_entries, dpme_ondisk->dpme_map_entries,
	    sizeof(entry->dpme_map_entries));
	memcpy(&entry->dpme_pblock_start, dpme_ondisk->dpme_pblock_start,
	    sizeof(entry->dpme_pblock_start));
	memcpy(&entry->dpme_pblocks, dpme_ondisk->dpme_pblocks,
	    sizeof(entry->dpme_pblocks));
	memcpy(&entry->dpme_lblock_start, dpme_ondisk->dpme_lblock_start,
	    sizeof(entry->dpme_lblock_start));
	memcpy(&entry->dpme_lblocks, dpme_ondisk->dpme_lblocks,
	    sizeof(entry->dpme_lblocks));
	memcpy(&entry->dpme_flags, dpme_ondisk->dpme_flags,
	    sizeof(entry->dpme_flags));
	memcpy(&entry->dpme_boot_block, dpme_ondisk->dpme_boot_block,
	    sizeof(entry->dpme_boot_block));
	memcpy(&entry->dpme_boot_bytes, dpme_ondisk->dpme_boot_bytes,
	    sizeof(entry->dpme_boot_bytes));
	memcpy(&entry->dpme_load_addr, dpme_ondisk->dpme_load_addr,
	    sizeof(entry->dpme_load_addr));
	memcpy(&entry->dpme_goto_addr, dpme_ondisk->dpme_goto_addr,
	    sizeof(entry->dpme_goto_addr));
	memcpy(&entry->dpme_checksum, dpme_ondisk->dpme_checksum,
	    sizeof(entry->dpme_checksum));

	entry->dpme_signature = betoh16(entry->dpme_signature);
	entry->dpme_map_entries = betoh32(entry->dpme_map_entries);
	entry->dpme_pblock_start = betoh32(entry->dpme_pblock_start);
	entry->dpme_pblocks = betoh32(entry->dpme_pblocks);
	entry->dpme_lblock_start = betoh32(entry->dpme_lblock_start);
	entry->dpme_lblocks = betoh32(entry->dpme_lblocks);
	entry->dpme_flags = betoh32(entry->dpme_flags);
	entry->dpme_boot_block = betoh32(entry->dpme_boot_block);
	entry->dpme_boot_bytes = betoh32(entry->dpme_boot_bytes);
	entry->dpme_load_addr = betoh32(entry->dpme_load_addr);
	entry->dpme_goto_addr = betoh32(entry->dpme_goto_addr);
	entry->dpme_checksum = betoh32(entry->dpme_checksum);

	memcpy(entry->dpme_reserved_1, dpme_ondisk->dpme_reserved_1,
	    sizeof(entry->dpme_reserved_1));
	memcpy(entry->dpme_reserved_2, dpme_ondisk->dpme_reserved_2,
	    sizeof(entry->dpme_reserved_2));
	memcpy(entry->dpme_reserved_3, dpme_ondisk->dpme_reserved_3,
	    sizeof(entry->dpme_reserved_3));
	memcpy(entry->dpme_reserved_4, dpme_ondisk->dpme_reserved_4,
	    sizeof(entry->dpme_reserved_4));

	strlcpy(entry->dpme_name, dpme_ondisk->dpme_name,
	    sizeof(entry->dpme_name));
	strlcpy(entry->dpme_type, dpme_ondisk->dpme_type,
	    sizeof(entry->dpme_type));
	strlcpy(entry->dpme_processor_id, dpme_ondisk->dpme_processor_id,
	    sizeof(entry->dpme_processor_id));

	free(dpme_ondisk);
	return 1;
}

int
write_dpme(int fd, uint64_t sector, struct entry *entry)
{
	struct dpme_ondisk *dpme_ondisk;
	int rslt;
	uint32_t tmp32;
	uint16_t tmp16;

	dpme_ondisk = malloc(sizeof(struct dpme_ondisk));
	if (dpme_ondisk == NULL)
		errx(1, "No memory to write dpme");

	memcpy(dpme_ondisk->dpme_name, entry->dpme_name,
	    sizeof(dpme_ondisk->dpme_name));
	memcpy(dpme_ondisk->dpme_type, entry->dpme_type,
	    sizeof(dpme_ondisk->dpme_type));
	memcpy(dpme_ondisk->dpme_processor_id, entry->dpme_processor_id,
	    sizeof(dpme_ondisk->dpme_processor_id));

	memcpy(dpme_ondisk->dpme_reserved_1, entry->dpme_reserved_1,
	    sizeof(dpme_ondisk->dpme_reserved_1));
	memcpy(dpme_ondisk->dpme_reserved_2, entry->dpme_reserved_2,
	    sizeof(dpme_ondisk->dpme_reserved_2));
	memcpy(dpme_ondisk->dpme_reserved_3, entry->dpme_reserved_3,
	    sizeof(dpme_ondisk->dpme_reserved_3));
	memcpy(dpme_ondisk->dpme_reserved_4, entry->dpme_reserved_4,
	    sizeof(dpme_ondisk->dpme_reserved_4));

	tmp16 = htobe16(entry->dpme_signature);
	memcpy(dpme_ondisk->dpme_signature, &tmp16,
	    sizeof(dpme_ondisk->dpme_signature));
	tmp32 = htobe32(entry->dpme_map_entries);
	memcpy(dpme_ondisk->dpme_map_entries, &tmp32,
	    sizeof(dpme_ondisk->dpme_map_entries));
	tmp32 = htobe32(entry->dpme_pblock_start);
	memcpy(dpme_ondisk->dpme_pblock_start, &tmp32,
	    sizeof(dpme_ondisk->dpme_pblock_start));
	tmp32 = htobe32(entry->dpme_pblocks);
	memcpy(dpme_ondisk->dpme_pblocks, &tmp32,
	    sizeof(dpme_ondisk->dpme_pblocks));
	tmp32 = htobe32(entry->dpme_lblock_start);
	memcpy(dpme_ondisk->dpme_lblock_start, &tmp32,
	    sizeof(dpme_ondisk->dpme_lblock_start));
	tmp32 = betoh32(entry->dpme_lblocks);
	memcpy(dpme_ondisk->dpme_lblocks, &tmp32,
	    sizeof(dpme_ondisk->dpme_lblocks));
	tmp32 = betoh32(entry->dpme_flags);
	memcpy(dpme_ondisk->dpme_flags, &tmp32,
	    sizeof(dpme_ondisk->dpme_flags));
	tmp32 = htobe32(entry->dpme_boot_block);
	memcpy(dpme_ondisk->dpme_boot_block, &tmp32,
	    sizeof(dpme_ondisk->dpme_boot_block));
	tmp32 = htobe32(entry->dpme_boot_bytes);
	memcpy(dpme_ondisk->dpme_boot_bytes, &tmp32,
	    sizeof(dpme_ondisk->dpme_boot_bytes));
	tmp32 = betoh32(entry->dpme_load_addr);
	memcpy(dpme_ondisk->dpme_load_addr, &tmp32,
	    sizeof(dpme_ondisk->dpme_load_addr));
	tmp32 = betoh32(entry->dpme_goto_addr);
	memcpy(dpme_ondisk->dpme_goto_addr, &tmp32,
	    sizeof(dpme_ondisk->dpme_goto_addr));
	tmp32 = betoh32(entry->dpme_checksum);
	memcpy(dpme_ondisk->dpme_checksum, &tmp32,
	    sizeof(dpme_ondisk->dpme_checksum));

	rslt = write_block(fd, sector, dpme_ondisk);
	free(dpme_ondisk);
	return rslt;
}
