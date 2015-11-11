/*
 * linux/fs/befs/datastream.c
 *
 * Copyright (C) 2001 Will Dyson <will_dyson@pobox.com>
 *
 * Based on portions of file.c by Makoto Kato <m_kato@ga2.so-net.ne.jp>
 *
 * Many thanks to Dominic Giampaolo, author of "Practical File System
 * Design with the Be File System", for such a helpful book.
 *
 */

#include <linux/kernel.h>
#include <linux/buffer_head.h>
#include <linux/string.h>

#include "befs.h"
#include "datastream.h"
#include "io.h"

const befs_inode_addr BAD_IADDR = { 0, 0, 0 };

static int befs_find_brun_direct(struct super_block *sb,
				 befs_data_stream * data,
				 befs_blocknr_t blockno, befs_block_run * run);

static int befs_find_brun_indirect(struct super_block *sb,
				   befs_data_stream * data,
				   befs_blocknr_t blockno,
				   befs_block_run * run);

static int befs_find_brun_dblindirect(struct super_block *sb,
				      befs_data_stream * data,
				      befs_blocknr_t blockno,
				      befs_block_run * run);

/**
 * befs_read_datastream - get buffer_head containing data, starting from pos.
 * @sb: Filesystem superblock
 * @ds: datastrem to find data with
 * @pos: start of data
 * @off: offset of data in buffer_head->b_data
 *
 * Returns pointer to buffer_head containing data starting with offset @off,
 * if you don't need to know offset just set @off = NULL.
 */
struct buffer_head *
befs_read_datastream(struct super_block *sb, befs_data_stream * ds,
		     befs_off_t pos, uint * off)
{
	struct buffer_head *bh = NULL;
	befs_block_run run;
	befs_blocknr_t block;	/* block coresponding to pos */

	befs_debug(sb, "---> %s %llu", __func__, pos);
	block = pos >> BEFS_SB(sb)->block_shift;
	if (off)
		*off = pos - (block << BEFS_SB(sb)->block_shift);

	if (befs_fblock2brun(sb, ds, block, &run) != BEFS_OK) {
		befs_error(sb, "BeFS: Error finding disk addr of block %lu",
			   (unsigned long)block);
		befs_debug(sb, "<--- %s ERROR", __func__);
		return NULL;
	}
	bh = befs_bread_iaddr(sb, run);
	if (!bh) {
		befs_error(sb, "BeFS: Error reading block %lu from datastream",
			   (unsigned long)block);
		return NULL;
	}

	befs_debug(sb, "<--- %s read data, starting at %llu", __func__, pos);

	return bh;
}

/*
 * Takes a file position and gives back a brun who's starting block
 * is block number fblock of the file.
 * 
 * Returns BEFS_OK or BEFS_ERR.
 * 
 * Calls specialized functions for each of the three possible
 * datastream regions.
 *
 * 2001-11-15 Will Dyson
 */
int
befs_fblock2brun(struct super_block *sb, befs_data_stream * data,
		 befs_blocknr_t fblock, befs_block_run * run)
{
	int err;
	befs_off_t pos = fblock << BEFS_SB(sb)->block_shift;

	if (pos < data->max_direct_range) {
		err = befs_find_brun_direct(sb, data, fblock, run);

	} else if (pos < data->max_indirect_range) {
		err = befs_find_brun_indirect(sb, data, fblock, run);

	} else if (pos < data->max_double_indirect_range) {
		err = befs_find_brun_dblindirect(sb, data, fblock, run);

	} else {
		befs_error(sb,
			   "befs_fblock2brun() was asked to find block %lu, "
			   "which is not mapped by the datastream\n",
			   (unsigned long)fblock);
		err = BEFS_ERR;
	}
	return err;
}

/**
 * befs_read_lsmylink - read long symlink from datastream.
 * @sb: Filesystem superblock 
 * @ds: Datastrem to read from
 * @buff: Buffer in which to place long symlink data
 * @len: Length of the long symlink in bytes
 *
 * Returns the number of bytes read
 */
size_t
befs_read_lsymlink(struct super_block * sb, befs_data_stream * ds, void *buff,
		   befs_off_t len)
{
	befs_off_t bytes_read = 0;	/* bytes readed */
	u16 plen;
	struct buffer_head *bh = NULL;
	befs_debug(sb, "---> %s length: %llu", __func__, len);

	while (bytes_read < len) {
		bh = befs_read_datastream(sb, ds, bytes_read, NULL);
		if (!bh) {
			befs_error(sb, "BeFS: Error reading datastream block "
				   "starting from %llu", bytes_read);
			befs_debug(sb, "<--- %s ERROR", __func__);
			return bytes_read;

		}
		plen = ((bytes_read + BEFS_SB(sb)->block_size) < len) ?
		    BEFS_SB(sb)->block_size : len - bytes_read;
		memcpy(buff + bytes_read, bh->b_data, plen);
		brelse(bh);
		bytes_read += plen;
	}

	befs_debug(sb, "<--- %s read %u bytes", __func__, (unsigned int)
		   bytes_read);
	return bytes_read;
}

/**
 * befs_count_blocks - blocks used by a file
 * @sb: Filesystem superblock
 * @ds: Datastream of the file
 *
 * Counts the number of fs blocks that the file represented by
 * inode occupies on the filesystem, counting both regular file
 * data and filesystem metadata (and eventually attribute data
 * when we support attributes)
*/

befs_blocknr_t
befs_count_blocks(struct super_block * sb, befs_data_stream * ds)
{
	befs_blocknr_t blocks;
	befs_blocknr_t datablocks;	/* File data blocks */
	befs_blocknr_t metablocks;	/* FS metadata blocks */
	struct befs_sb_info *befs_sb = BEFS_SB(sb);

	befs_debug(sb, "---> %s", __func__);

	datablocks = ds->size >> befs_sb->block_shift;
	if (ds->size & (befs_sb->block_size - 1))
		datablocks += 1;

	metablocks = 1;		/* Start with 1 block for inode */

	/* Size of indirect block */
	if (ds->size > ds->max_direct_range)
		metablocks += ds->indirect.len;

	/*
	   Double indir block, plus all the indirect blocks it mapps
	   In the double-indirect range, all block runs of data are
	   BEFS_DBLINDIR_BRUN_LEN blocks long. Therefore, we know 
	   how many data block runs are in the double-indirect region,
	   and from that we know how many indirect blocks it takes to
	   map them. We assume that the indirect blocks are also
	   BEFS_DBLINDIR_BRUN_LEN blocks long.
	 */
	if (ds->size > ds->max_indirect_range && ds->max_indirect_range != 0) {
		uint dbl_bytes;
		uint dbl_bruns;
		uint indirblocks;

		dbl_bytes =
		    ds->max_double_indirect_range - ds->max_indirect_range;
		dbl_bruns =
		    dbl_bytes / (befs_sb->block_size * BEFS_DBLINDIR_BRUN_LEN);
		indirblocks = dbl_bruns / befs_iaddrs_per_block(sb);

		metablocks += ds->double_indirect.len;
		metablocks += indirblocks;
	}

	blocks = datablocks + metablocks;
	befs_debug(sb, "<--- %s %u blocks", __func__, (unsigned int)blocks);

	return blocks;
}

/*
	Finds the block run that starts at file block number blockno
	in the file represented by the datastream data, if that 
	blockno is in the direct region of the datastream.
	
	sb: the superblock
	data: the datastream
	blockno: the blocknumber to find
	run: The found run is passed back through this pointer
	
	Return value is BEFS_OK if the blockrun is found, BEFS_ERR
	otherwise.
	
	Algorithm:
	Linear search. Checks each element of array[] to see if it
	contains the blockno-th filesystem block. This is necessary
	because the block runs map variable amounts of data. Simply
	keeps a count of the number of blocks searched so far (sum),
	incrementing this by the length of each block run as we come
	across it. Adds sum to *count before returning (this is so
	you can search multiple arrays that are logicaly one array,
	as in the indirect region code).
	
	When/if blockno is found, if blockno is inside of a block 
	run as stored on disk, we offset the start and length members
	of the block run, so that blockno is the start and len is
	still valid (the run ends in the same place).
	
	2001-11-15 Will Dyson
*/
static int
befs_find_brun_direct(struct super_block *sb, befs_data_stream * data,
		      befs_blocknr_t blockno, befs_block_run * run)
{
	int i;
	befs_block_run *array = data->direct;
	befs_blocknr_t sum;
	befs_blocknr_t max_block =
	    data->max_direct_range >> BEFS_SB(sb)->block_shift;

	befs_debug(sb, "---> %s, find %lu", __func__, (unsigned long)blockno);

	if (blockno > max_block) {
		befs_error(sb, "%s passed block outside of direct region",
			   __func__);
		return BEFS_ERR;
	}

	for (i = 0, sum = 0; i < BEFS_NUM_DIRECT_BLOCKS;
	     sum += array[i].len, i++) {
		if (blockno >= sum && blockno < sum + (array[i].len)) {
			int offset = blockno - sum;
			run->allocation_group = array[i].allocation_group;
			run->start = array[i].start + offset;
			run->len = array[i].len - offset;

			befs_debug(sb, "---> %s, "
				   "found %lu at direct[%d]", __func__,
				   (unsigned long)blockno, i);
			return BEFS_OK;
		}
	}

	befs_debug(sb, "---> %s ERROR", __func__);
	return BEFS_ERR;
}

/*
	Finds the block run that starts at file block number blockno
	in the file represented by the datastream data, if that 
	blockno is in the indirect region of the datastream.
	
	sb: the superblock
	data: the datastream
	blockno: the blocknumber to find
	run: The found run is passed back through this pointer
	
	Return value is BEFS_OK if the blockrun is found, BEFS_ERR
	otherwise.
	
	Algorithm:
	For each block in the indirect run of the datastream, read
	it in and search through it for	search_blk.
	
	XXX:
	Really should check to make sure blockno is inside indirect
	region.
	
	2001-11-15 Will Dyson
*/
static int
befs_find_brun_indirect(struct super_block *sb,
			befs_data_stream * data, befs_blocknr_t blockno,
			befs_block_run * run)
{
	int i, j;
	befs_blocknr_t sum = 0;
	befs_blocknr_t indir_start_blk;
	befs_blocknr_t search_blk;
	struct buffer_head *indirblock;
	befs_disk_block_run *array;

	befs_block_run indirect = data->indirect;
	befs_blocknr_t indirblockno = iaddr2blockno(sb, &indirect);
	int arraylen = befs_iaddrs_per_block(sb);

	befs_debug(sb, "---> %s, find %lu", __func__, (unsigned long)blockno);

	indir_start_blk = data->max_direct_range >> BEFS_SB(sb)->block_shift;
	search_blk = blockno - indir_start_blk;

	/* Examine blocks of the indirect run one at a time */
	for (i = 0; i < indirect.len; i++) {
		indirblock = befs_bread(sb, indirblockno + i);
		if (indirblock == NULL) {
			befs_debug(sb, "---> %s failed to read "
				   "disk block %lu from the indirect brun",
				   __func__, (unsigned long)indirblockno + i);
			return BEFS_ERR;
		}

		array = (befs_disk_block_run *) indirblock->b_data;

		for (j = 0; j < arraylen; ++j) {
			int len = fs16_to_cpu(sb, array[j].len);

			if (search_blk >= sum && search_blk < sum + len) {
				int offset = search_blk - sum;
				run->allocation_group =
				    fs32_to_cpu(sb, array[j].allocation_group);
				run->start =
				    fs16_to_cpu(sb, array[j].start) + offset;
				run->len =
				    fs16_to_cpu(sb, array[j].len) - offset;

				brelse(indirblock);
				befs_debug(sb,
					   "<--- %s found file block "
					   "%lu at indirect[%d]", __func__,
					   (unsigned long)blockno,
					   j + (i * arraylen));
				return BEFS_OK;
			}
			sum += len;
		}

		brelse(indirblock);
	}

	/* Only fallthrough is an error */
	befs_error(sb, "BeFS: %s failed to find "
		   "file block %lu", __func__, (unsigned long)blockno);

	befs_debug(sb, "<--- %s ERROR", __func__);
	return BEFS_ERR;
}

/*
	Finds the block run that starts at file block number blockno
	in the file represented by the datastream data, if that 
	blockno is in the double-indirect region of the datastream.
	
	sb: the superblock
	data: the datastream
	blockno: the blocknumber to find
	run: The found run is passed back through this pointer
	
	Return value is BEFS_OK if the blockrun is found, BEFS_ERR
	otherwise.
	
	Algorithm:
	The block runs in the double-indirect region are different.
	They are always allocated 4 fs blocks at a time, so each
	block run maps a constant amount of file data. This means
	that we can directly calculate how many block runs into the
	double-indirect region we need to go to get to the one that
	maps a particular filesystem block.
	
	We do this in two stages. First we calculate which of the
	inode addresses in the double-indirect block will point us
	to the indirect block that contains the mapping for the data,
	then we calculate which of the inode addresses in that 
	indirect block maps the data block we are after.
	
	Oh, and once we've done that, we actually read in the blocks 
	that contain the inode addresses we calculated above. Even 
	though the double-indirect run may be several blocks long, 
	we can calculate which of those blocks will contain the index
	we are after and only read that one. We then follow it to 
	the indirect block and perform a  similar process to find
	the actual block run that maps the data block we are interested
	in.
	
	Then we offset the run as in befs_find_brun_array() and we are 
	done.
	
	2001-11-15 Will Dyson
*/
static int
befs_find_brun_dblindirect(struct super_block *sb,
			   befs_data_stream * data, befs_blocknr_t blockno,
			   befs_block_run * run)
{
	int dblindir_indx;
	int indir_indx;
	int offset;
	int dbl_which_block;
	int which_block;
	int dbl_block_indx;
	int block_indx;
	off_t dblindir_leftover;
	befs_blocknr_t blockno_at_run_start;
	struct buffer_head *dbl_indir_block;
	struct buffer_head *indir_block;
	befs_block_run indir_run;
	befs_disk_inode_addr *iaddr_array = NULL;
	struct befs_sb_info *befs_sb = BEFS_SB(sb);

	befs_blocknr_t indir_start_blk =
	    data->max_indirect_range >> befs_sb->block_shift;

	off_t dbl_indir_off = blockno - indir_start_blk;

	/* number of data blocks mapped by each of the iaddrs in
	 * the indirect block pointed to by the double indirect block
	 */
	size_t iblklen = BEFS_DBLINDIR_BRUN_LEN;

	/* number of data blocks mapped by each of the iaddrs in
	 * the double indirect block
	 */
	size_t diblklen = iblklen * befs_iaddrs_per_block(sb)
	    * BEFS_DBLINDIR_BRUN_LEN;

	befs_debug(sb, "---> %s find %lu", __func__, (unsigned long)blockno);

	/* First, discover which of the double_indir->indir blocks
	 * contains pos. Then figure out how much of pos that
	 * accounted for. Then discover which of the iaddrs in
	 * the indirect block contains pos.
	 */

	dblindir_indx = dbl_indir_off / diblklen;
	dblindir_leftover = dbl_indir_off % diblklen;
	indir_indx = dblindir_leftover / diblklen;

	/* Read double indirect block */
	dbl_which_block = dblindir_indx / befs_iaddrs_per_block(sb);
	if (dbl_which_block > data->double_indirect.len) {
		befs_error(sb, "The double-indirect index calculated by "
			   "%s, %d, is outside the range "
			   "of the double-indirect block", __func__,
			   dblindir_indx);
		return BEFS_ERR;
	}

	dbl_indir_block =
	    befs_bread(sb, iaddr2blockno(sb, &data->double_indirect) +
					dbl_which_block);
	if (dbl_indir_block == NULL) {
		befs_error(sb, "%s couldn't read the "
			   "double-indirect block at blockno %lu", __func__,
			   (unsigned long)
			   iaddr2blockno(sb, &data->double_indirect) +
			   dbl_which_block);
		brelse(dbl_indir_block);
		return BEFS_ERR;
	}

	dbl_block_indx =
	    dblindir_indx - (dbl_which_block * befs_iaddrs_per_block(sb));
	iaddr_array = (befs_disk_inode_addr *) dbl_indir_block->b_data;
	indir_run = fsrun_to_cpu(sb, iaddr_array[dbl_block_indx]);
	brelse(dbl_indir_block);
	iaddr_array = NULL;

	/* Read indirect block */
	which_block = indir_indx / befs_iaddrs_per_block(sb);
	if (which_block > indir_run.len) {
		befs_error(sb, "The indirect index calculated by "
			   "%s, %d, is outside the range "
			   "of the indirect block", __func__, indir_indx);
		return BEFS_ERR;
	}

	indir_block =
	    befs_bread(sb, iaddr2blockno(sb, &indir_run) + which_block);
	if (indir_block == NULL) {
		befs_error(sb, "%s couldn't read the indirect block "
			   "at blockno %lu", __func__, (unsigned long)
			   iaddr2blockno(sb, &indir_run) + which_block);
		brelse(indir_block);
		return BEFS_ERR;
	}

	block_indx = indir_indx - (which_block * befs_iaddrs_per_block(sb));
	iaddr_array = (befs_disk_inode_addr *) indir_block->b_data;
	*run = fsrun_to_cpu(sb, iaddr_array[block_indx]);
	brelse(indir_block);
	iaddr_array = NULL;

	blockno_at_run_start = indir_start_blk;
	blockno_at_run_start += diblklen * dblindir_indx;
	blockno_at_run_start += iblklen * indir_indx;
	offset = blockno - blockno_at_run_start;

	run->start += offset;
	run->len -= offset;

	befs_debug(sb, "Found file block %lu in double_indirect[%d][%d],"
		   " double_indirect_leftover = %lu", (unsigned long)
		   blockno, dblindir_indx, indir_indx, dblindir_leftover);

	return BEFS_OK;
}
