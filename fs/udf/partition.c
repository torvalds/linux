/*
 * partition.c
 *
 * PURPOSE
 *      Partition handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *      This file is distributed under the terms of the GNU General Public
 *      License (GPL). Copies of the GPL can be obtained from:
 *              ftp://prep.ai.mit.edu/pub/gnu/GPL
 *      Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-2001 Ben Fennema
 *
 * HISTORY
 *
 * 12/06/98 blf  Created file.
 *
 */

#include "udfdecl.h"
#include "udf_sb.h"
#include "udf_i.h"

#include <linux/fs.h>
#include <linux/string.h>
#include <linux/udf_fs.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>

inline uint32_t udf_get_pblock(struct super_block *sb, uint32_t block,
			       uint16_t partition, uint32_t offset)
{
	if (partition >= UDF_SB_NUMPARTS(sb)) {
		udf_debug("block=%d, partition=%d, offset=%d: invalid partition\n",
			  block, partition, offset);
		return 0xFFFFFFFF;
	}
	if (UDF_SB_PARTFUNC(sb, partition))
		return UDF_SB_PARTFUNC(sb, partition)(sb, block, partition, offset);
	else
		return UDF_SB_PARTROOT(sb, partition) + block + offset;
}

uint32_t udf_get_pblock_virt15(struct super_block *sb, uint32_t block,
			       uint16_t partition, uint32_t offset)
{
	struct buffer_head *bh = NULL;
	uint32_t newblock;
	uint32_t index;
	uint32_t loc;

	index = (sb->s_blocksize - UDF_SB_TYPEVIRT(sb,partition).s_start_offset) / sizeof(uint32_t);

	if (block > UDF_SB_TYPEVIRT(sb,partition).s_num_entries) {
		udf_debug("Trying to access block beyond end of VAT (%d max %d)\n",
			  block, UDF_SB_TYPEVIRT(sb,partition).s_num_entries);
		return 0xFFFFFFFF;
	}

	if (block >= index) {
		block -= index;
		newblock = 1 + (block / (sb->s_blocksize / sizeof(uint32_t)));
		index = block % (sb->s_blocksize / sizeof(uint32_t));
	} else {
		newblock = 0;
		index = UDF_SB_TYPEVIRT(sb,partition).s_start_offset / sizeof(uint32_t) + block;
	}

	loc = udf_block_map(UDF_SB_VAT(sb), newblock);

	if (!(bh = sb_bread(sb, loc))) {
		udf_debug("get_pblock(UDF_VIRTUAL_MAP:%p,%d,%d) VAT: %d[%d]\n",
			  sb, block, partition, loc, index);
		return 0xFFFFFFFF;
	}

	loc = le32_to_cpu(((__le32 *)bh->b_data)[index]);

	brelse(bh);

	if (UDF_I_LOCATION(UDF_SB_VAT(sb)).partitionReferenceNum == partition) {
		udf_debug("recursive call to udf_get_pblock!\n");
		return 0xFFFFFFFF;
	}

	return udf_get_pblock(sb, loc,
			      UDF_I_LOCATION(UDF_SB_VAT(sb)).partitionReferenceNum,
			      offset);
}

inline uint32_t udf_get_pblock_virt20(struct super_block * sb, uint32_t block,
				      uint16_t partition, uint32_t offset)
{
	return udf_get_pblock_virt15(sb, block, partition, offset);
}

uint32_t udf_get_pblock_spar15(struct super_block * sb, uint32_t block,
			       uint16_t partition, uint32_t offset)
{
	int i;
	struct sparingTable *st = NULL;
	uint32_t packet = (block + offset) & ~(UDF_SB_TYPESPAR(sb,partition).s_packet_len - 1);

	for (i = 0; i < 4; i++) {
		if (UDF_SB_TYPESPAR(sb,partition).s_spar_map[i] != NULL) {
			st = (struct sparingTable *)UDF_SB_TYPESPAR(sb,partition).s_spar_map[i]->b_data;
			break;
		}
	}

	if (st) {
		for (i = 0; i < le16_to_cpu(st->reallocationTableLen); i++) {
			if (le32_to_cpu(st->mapEntry[i].origLocation) >= 0xFFFFFFF0) {
				break;
			} else if (le32_to_cpu(st->mapEntry[i].origLocation) == packet) {
				return le32_to_cpu(st->mapEntry[i].mappedLocation) +
					((block + offset) & (UDF_SB_TYPESPAR(sb,partition).s_packet_len - 1));
			} else if (le32_to_cpu(st->mapEntry[i].origLocation) > packet) {
				break;
			}
		}
	}

	return UDF_SB_PARTROOT(sb,partition) + block + offset;
}

int udf_relocate_blocks(struct super_block *sb, long old_block, long *new_block)
{
	struct udf_sparing_data *sdata;
	struct sparingTable *st = NULL;
	struct sparingEntry mapEntry;
	uint32_t packet;
	int i, j, k, l;

	for (i = 0; i < UDF_SB_NUMPARTS(sb); i++) {
		if (old_block > UDF_SB_PARTROOT(sb,i) &&
		    old_block < UDF_SB_PARTROOT(sb,i) + UDF_SB_PARTLEN(sb,i)) {
			sdata = &UDF_SB_TYPESPAR(sb,i);
			packet = (old_block - UDF_SB_PARTROOT(sb,i)) & ~(sdata->s_packet_len - 1);

			for (j = 0; j < 4; j++) {
				if (UDF_SB_TYPESPAR(sb,i).s_spar_map[j] != NULL) {
					st = (struct sparingTable *)sdata->s_spar_map[j]->b_data;
					break;
				}
			}

			if (!st)
				return 1;

			for (k = 0; k < le16_to_cpu(st->reallocationTableLen); k++) {
				if (le32_to_cpu(st->mapEntry[k].origLocation) == 0xFFFFFFFF) {
					for (; j < 4; j++) {
						if (sdata->s_spar_map[j]) {
							st = (struct sparingTable *)sdata->s_spar_map[j]->b_data;
							st->mapEntry[k].origLocation = cpu_to_le32(packet);
							udf_update_tag((char *)st, sizeof(struct sparingTable) + le16_to_cpu(st->reallocationTableLen) * sizeof(struct sparingEntry));
							mark_buffer_dirty(sdata->s_spar_map[j]);
						}
					}
					*new_block = le32_to_cpu(st->mapEntry[k].mappedLocation) +
						((old_block - UDF_SB_PARTROOT(sb,i)) & (sdata->s_packet_len - 1));
					return 0;
				} else if (le32_to_cpu(st->mapEntry[k].origLocation) == packet) {
					*new_block = le32_to_cpu(st->mapEntry[k].mappedLocation) +
						((old_block - UDF_SB_PARTROOT(sb,i)) & (sdata->s_packet_len - 1));
					return 0;
				} else if (le32_to_cpu(st->mapEntry[k].origLocation) > packet) {
					break;
				}
			}

			for (l = k; l < le16_to_cpu(st->reallocationTableLen); l++) {
				if (le32_to_cpu(st->mapEntry[l].origLocation) == 0xFFFFFFFF) {
					for (; j < 4; j++) {
						if (sdata->s_spar_map[j]) {
							st = (struct sparingTable *)sdata->s_spar_map[j]->b_data;
							mapEntry = st->mapEntry[l];
							mapEntry.origLocation = cpu_to_le32(packet);
							memmove(&st->mapEntry[k + 1], &st->mapEntry[k], (l - k) * sizeof(struct sparingEntry));
							st->mapEntry[k] = mapEntry;
							udf_update_tag((char *)st, sizeof(struct sparingTable) + le16_to_cpu(st->reallocationTableLen) * sizeof(struct sparingEntry));
							mark_buffer_dirty(sdata->s_spar_map[j]);
						}
					}
					*new_block = le32_to_cpu(st->mapEntry[k].mappedLocation) +
						((old_block - UDF_SB_PARTROOT(sb,i)) & (sdata->s_packet_len - 1));
					return 0;
				}
			}

			return 1;
		} /* if old_block */
	}

	if (i == UDF_SB_NUMPARTS(sb)) {
		/* outside of partitions */
		/* for now, fail =) */
		return 1;
	}

	return 0;
}
