/*	$OpenBSD: dump.c,v 1.75 2016/02/23 02:39:54 krw Exp $	*/

/*
 * dump.c - dumping partition maps
 *
 * Written by Eryk Vershen
 */

/*
 * Copyright 1996,1997,1998 by Apple Computer, Inc.
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

#include <sys/queue.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

#include "partition_map.h"
#include "dump.h"
#include "io.h"

void	dump_block(unsigned char *, int);
void	dump_block_zero(struct partition_map *);
void	dump_partition_entry(struct entry *, int, int, int);
int	get_max_base_or_length(struct partition_map *);
int	get_max_name_string_length(struct partition_map *);
int	get_max_type_string_length(struct partition_map *);

void
dump_block_zero(struct partition_map *map)
{
	char buf[FMT_SCALED_STRSIZE];
	struct ddmap  *m;
	int i;

	printf("\nDevice block size=%u, Number of Blocks=%u",
	       map->sbBlkSize, map->sbBlkCount);
	if (fmt_scaled((long long)map->sbBlkCount * map->sbBlkSize, buf) == 0)
		printf(" (%s)\n", buf);
	else
		printf("\n");

	printf("DeviceType=0x%x, DeviceId=0x%x\n", map->sbDevType,
	    map->sbDevId);
	if (map->sbDrvrCount > 0) {
		printf("Drivers-\n");
		m = map->sbDDMap;
		for (i = 0; i < map->sbDrvrCount; i++) {
			printf("%d: %3u @ %u, ", i + 1, m[i].ddSize,
			    m[i].ddBlock);
			printf("type=0x%x\n", m[i].ddType);
		}
	}
	printf("\n");
}


void
dump_partition_map(struct partition_map *map)
{
	struct entry *entry;
	int digits, max_type_length, max_name_length;

	printf("\nPartition map (with %d byte blocks) on '%s'\n",
	       map->sbBlkSize, map->name);

	digits = number_of_digits(get_max_base_or_length(map));
	if (digits < 6)
		digits = 6;
	max_type_length = get_max_type_string_length(map);
	if (max_type_length < 4)
		max_type_length = 4;
	max_name_length = get_max_name_string_length(map);
	if (max_name_length < 6)
		max_name_length = 6;
	printf(" #: %*s %-*s %*s   %-*s\n", max_type_length, "type",
	    max_name_length, "name", digits, "length", digits, "base");

	LIST_FOREACH(entry, &map->disk_order, disk_entry) {
		dump_partition_entry(entry, max_type_length,
		    max_name_length, digits);
	}
	dump_block_zero(map);
}


void
dump_partition_entry(struct entry *entry, int type_length, int name_length,
    int digits)
{
	char buf[FMT_SCALED_STRSIZE];

	printf("%2ld: %*.32s", entry->disk_address, type_length,
	    entry->dpme_type);
	printf("%c%-*.32s ", contains_driver(entry) ? '*' : ' ',
	    name_length, entry->dpme_name);

	printf("%*u @ %-*u", digits, entry->dpme_pblocks, digits,
	    entry->dpme_pblock_start);

	if (fmt_scaled((long long)entry->dpme_pblocks *
	    entry->the_map->sbBlkSize, buf) == 0)
		printf("(%s)\n", buf);
	else
		printf("\n");
}


void
show_data_structures(struct partition_map *map)
{
	struct entry *entry;
	struct ddmap *m;
	int i;

	printf("Header:\n");
	printf("map %d blocks out of %d,  media %lu blocks (%d byte blocks)\n",
	    map->blocks_in_map, map->maximum_in_map, map->media_size,
	    map->sbBlkSize);
	printf("Map is%s writable", rflag ? " not" : "");
	printf(" and has%s been changed\n", (map->changed) ? "" : " not");
	printf("\n");

	printf("Block0:\n");
	printf("signature 0x%x", map->sbSig);
	printf("Block size=%u, Number of Blocks=%u\n", map->sbBlkSize,
	    map->sbBlkCount);
	printf("DeviceType=0x%x, DeviceId=0x%x, sbData=0x%x\n", map->sbDevType,
	    map->sbDevId, map->sbData);
	if (map->sbDrvrCount == 0) {
		printf("No drivers\n");
	} else {
		printf("%u driver%s-\n", map->sbDrvrCount,
		    (map->sbDrvrCount > 1) ? "s" : "");
		m = map->sbDDMap;
		for (i = 0; i < map->sbDrvrCount; i++) {
			printf("%u: @ %u for %u, type=0x%x\n", i + 1,
			    m[i].ddBlock, m[i].ddSize, m[i].ddType);
		}
	}
	printf("\n");
	printf(" #:                 type  length   base    "
	       "flags     (      logical      )\n");
	LIST_FOREACH(entry, &map->disk_order, disk_entry) {
		printf("%2ld: %20.32s ", entry->disk_address, entry->dpme_type);
		printf("%7u @ %-7u ", entry->dpme_pblocks,
		    entry->dpme_pblock_start);
		printf("%c%c%c%c%c%c%c%c%c ",
		       (entry->dpme_flags & DPME_VALID) ? 'V' : '.',
		       (entry->dpme_flags & DPME_ALLOCATED) ? 'A' : '.',
		       (entry->dpme_flags & DPME_IN_USE) ? 'I' : '.',
		       (entry->dpme_flags & DPME_BOOTABLE) ? 'B' : '.',
		       (entry->dpme_flags & DPME_READABLE) ? 'R' : '.',
		       (entry->dpme_flags & DPME_WRITABLE) ? 'W' : '.',
		       (entry->dpme_flags & DPME_OS_PIC_CODE) ? 'P' : '.',
		       (entry->dpme_flags & DPME_OS_SPECIFIC_2) ? '2' : '.',
		       (entry->dpme_flags & DPME_OS_SPECIFIC_1) ? '1' : '.');
		printf("( %7u @ %-7u )\n", entry->dpme_lblocks,
		    entry->dpme_lblock_start);
	}
	printf("\n");
	printf(" #:  booter   bytes      load_address      "
	    "goto_address checksum processor\n");
	LIST_FOREACH(entry, &map->disk_order, disk_entry) {
		printf("%2ld: ", entry->disk_address);
		printf("%7u ", entry->dpme_boot_block);
		printf("%7u ", entry->dpme_boot_bytes);
		printf("%8x ", entry->dpme_load_addr);
		printf("%8x ", entry->dpme_goto_addr);
		printf("%8x ", entry->dpme_checksum);
		printf("%.32s", entry->dpme_processor_id);
		printf("\n");
	}
	printf("\n");
}


void
full_dump_partition_entry(struct partition_map *map, int ix)
{
	struct entry *entry;
	int i;
	uint32_t t;

	entry = find_entry_by_disk_address(ix, map);
	if (entry == NULL) {
		printf("No such partition\n");
		return;
	}
	printf("             signature: 0x%x\n", entry->dpme_signature);
	printf(" number of map entries: %u\n", entry->dpme_map_entries);
	printf("        physical start: %10u  length: %10u\n",
	    entry->dpme_pblock_start, entry->dpme_pblocks);
	printf("         logical start: %10u  length: %10u\n",
	    entry->dpme_lblock_start, entry->dpme_lblocks);

	printf("                 flags: 0x%x\n", entry->dpme_flags);
	printf("                        ");
	if (entry->dpme_flags & DPME_VALID)
		printf("valid ");
	if (entry->dpme_flags & DPME_ALLOCATED)
		printf("alloc ");
	if (entry->dpme_flags & DPME_IN_USE)
		printf("in-use ");
	if (entry->dpme_flags & DPME_BOOTABLE)
		printf("boot ");
	if (entry->dpme_flags & DPME_READABLE)
		printf("read ");
	if (entry->dpme_flags & DPME_WRITABLE)
		printf("write ");
	if (entry->dpme_flags & DPME_OS_PIC_CODE)
		printf("pic ");
	t = entry->dpme_flags >> 7;
	for (i = 7; i <= 31; i++) {
		if (t & 0x1)
			printf("%d ", i);
		t = t >> 1;
	}
	printf("\n");

	printf("                  name: '%.32s'\n", entry->dpme_name);
	printf("                  type: '%.32s'\n", entry->dpme_type);
	printf("      boot start block: %10u\n", entry->dpme_boot_block);
	printf("boot length (in bytes): %10u\n", entry->dpme_boot_bytes);
	printf("          load address: 0x%08x\n", entry->dpme_load_addr);
	printf("         start address: 0x%08x\n", entry->dpme_goto_addr);
	printf("              checksum: 0x%08x\n", entry->dpme_checksum);
	printf("             processor: '%.32s'\n", entry->dpme_processor_id);
	printf("dpme_reserved_1 -");
	dump_block(entry->dpme_reserved_1, sizeof(entry->dpme_reserved_1));
	printf("dpme_reserved_2 -");
	dump_block(entry->dpme_reserved_2, sizeof(entry->dpme_reserved_2));
	printf("dpme_reserved_3 -");
	dump_block(entry->dpme_reserved_3, sizeof(entry->dpme_reserved_3));
	printf("dpme_reserved_4 -");
	dump_block(entry->dpme_reserved_4, sizeof(entry->dpme_reserved_4));
}


void
dump_block(unsigned char *addr, int len)
{
	int i, j, limit1, limit;

#define LINE_LEN 16
#define UNIT_LEN  4
#define OTHER_LEN  8

	for (i = 0; i < len; i = limit) {
		limit1 = i + LINE_LEN;
		if (limit1 > len)
			limit = len;
		else
			limit = limit1;
		printf("\n%03x: ", i);
		for (j = i; j < limit1; j++) {
			if (j % UNIT_LEN == 0)
				printf(" ");
			if (j < limit)
				printf("%02x", addr[j]);
			else
				printf("  ");
		}
		printf(" ");
		for (j = i; j < limit; j++) {
			if (j % OTHER_LEN == 0)
				printf(" ");
			if (addr[j] < ' ')
				printf(".");
			else
				printf("%c", addr[j]);
		}
	}
	printf("\n");
}

void
full_dump_block_zero(struct partition_map *map)
{
	struct ddmap *m;
	int i;

	m = map->sbDDMap;

	printf("             signature: 0x%x\n", map->sbSig);
	printf("       size of a block: %u\n", map->sbBlkSize);
	printf("      number of blocks: %u\n", map->sbBlkCount);
	printf("           device type: 0x%x\n", map->sbDevType);
	printf("             device id: 0x%x\n", map->sbDevId);
	printf("                  data: 0x%x\n", map->sbData);
	printf("          driver count: %u\n", map->sbDrvrCount);
	for (i = 0; i < 8; i++) {
		if (m[i].ddBlock == 0 && m[i].ddSize == 0 && m[i].ddType == 0)
			break;
		printf("      driver %3u block: %u\n", i + 1, m[i].ddBlock);
		printf("        size in blocks: %u\n", m[i].ddSize);
		printf("           driver type: 0x%x\n", m[i].ddType);
	}
	printf("remainder of block -");
	dump_block(map->sbReserved, sizeof(map->sbReserved));
}

int
get_max_type_string_length(struct partition_map *map)
{
	struct entry *entry;
	int max, length;

	max = 0;

	LIST_FOREACH(entry, &map->disk_order, disk_entry) {
		length = strnlen(entry->dpme_type, DPISTRLEN);
		if (length > max)
			max = length;
	}

	return max;
}

int
get_max_name_string_length(struct partition_map *map)
{
	struct entry *entry;
	int max, length;

	max = 0;

	LIST_FOREACH(entry, &map->disk_order, disk_entry) {
		length = strnlen(entry->dpme_name, DPISTRLEN);
		if (length > max)
			max = length;
	}

	return max;
}

int
get_max_base_or_length(struct partition_map *map)
{
	struct entry *entry;
	int max;

	max = 0;

	LIST_FOREACH(entry, &map->disk_order, disk_entry) {
		if (entry->dpme_pblock_start > max)
			max = entry->dpme_pblock_start;
		if (entry->dpme_pblocks > max)
			max = entry->dpme_pblocks;
	}

	return max;
}
