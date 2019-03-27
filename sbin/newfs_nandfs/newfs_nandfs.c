/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2012 Semihalf.
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

#include <sys/param.h>
#include <sys/fdcio.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/endian.h>
#include <sys/stddef.h>
#include <sys/uuid.h>
#include <sys/dirent.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgeom.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <fs/nandfs/nandfs_fs.h>
#include <dev/nand/nand_dev.h>

#define DEBUG
#undef DEBUG
#ifdef DEBUG
#define debug(fmt, args...) do { \
	printf("nandfs:" fmt "\n", ##args); } while (0)
#else
#define debug(fmt, args...)
#endif

#define NANDFS_FIRST_BLOCK	nandfs_first_block()
#define NANDFS_FIRST_CNO		1
#define NANDFS_BLOCK_BAD	1
#define NANDFS_BLOCK_GOOD	0

struct file_info {
	uint64_t	ino;
	const char	*name;
	uint32_t	mode;
	uint64_t	size;
	uint8_t		nblocks;
	uint32_t	*blocks;
	struct nandfs_inode *inode;
};

static struct file_info user_files[] = {
	{ NANDFS_ROOT_INO, NULL, S_IFDIR | 0755, 0, 1, NULL, NULL },
};

static struct file_info ifile =
	{ NANDFS_IFILE_INO, NULL, 0, 0, -1, NULL, NULL };
static struct file_info sufile =
	{ NANDFS_SUFILE_INO, NULL, 0, 0, -1, NULL, NULL };
static struct file_info cpfile =
	{ NANDFS_CPFILE_INO, NULL, 0, 0, -1, NULL, NULL };
static struct file_info datfile =
	{ NANDFS_DAT_INO, NULL, 0, 0, -1, NULL, NULL };

struct nandfs_block {
	LIST_ENTRY(nandfs_block) block_link;
	uint32_t number;
	uint64_t offset;
	void	*data;
};

static LIST_HEAD(, nandfs_block) block_head =
	LIST_HEAD_INITIALIZER(&block_head);

/* Storage geometry */
static off_t mediasize;
static ssize_t sectorsize;
static uint64_t nsegments;
static uint64_t erasesize;
static uint64_t segsize;

static struct nandfs_fsdata fsdata;
static struct nandfs_super_block super_block;

static int is_nand;

/* Nandfs parameters */
static size_t blocksize = NANDFS_DEF_BLOCKSIZE;
static long blocks_per_segment;
static long rsv_segment_percent = 5;
static time_t nandfs_time;
static uint32_t bad_segments_count = 0;
static uint32_t *bad_segments = NULL;
static uint8_t fsdata_blocks_state[NANDFS_NFSAREAS];

static u_char *volumelabel = NULL;

static struct nandfs_super_root *sr;

static uint32_t nuserfiles;
static uint32_t seg_nblocks;
static uint32_t seg_endblock;

#define SIZE_TO_BLOCK(size) howmany(size, blocksize)

static uint32_t
nandfs_first_block(void)
{
	uint32_t i, first_free, start_bad_segments = 0;

	for (i = 0; i < bad_segments_count; i++) {
		if (i == bad_segments[i])
			start_bad_segments++;
		else
			break;
	}

	first_free = SIZE_TO_BLOCK(NANDFS_DATA_OFFSET_BYTES(erasesize) +
	    (start_bad_segments * segsize));

	if (first_free < (uint32_t)blocks_per_segment)
		return (blocks_per_segment);
	else
		return (first_free);
}

static void
usage(void)
{

	fprintf(stderr,
	    "usage: newfs_nandfs [ -options ] device\n"
	    "where the options are:\n"
	    "\t-b block-size\n"
	    "\t-B blocks-per-segment\n"
	    "\t-L volume label\n"
	    "\t-m reserved-segments-percentage\n");
	exit(1);
}

static int
nandfs_log2(unsigned n)
{
	unsigned count;

	/*
	 * N.B. this function will return 0 if supplied 0.
	 */
	for (count = 0; n/2; count++)
		n /= 2;
	return count;
}

/* from NetBSD's src/sys/net/if_ethersubr.c */
static uint32_t
crc32_le(uint32_t crc, const uint8_t *buf, size_t len)
{
	static const uint32_t crctab[] = {
		0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
		0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
		0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
	};
	size_t i;

	crc = crc ^ ~0U;

	for (i = 0; i < len; i++) {
		crc ^= buf[i];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
		crc = (crc >> 4) ^ crctab[crc & 0xf];
	}

	return (crc ^ ~0U);
}

static void *
get_block(uint32_t block_nr, uint64_t offset)
{
	struct nandfs_block *block, *new_block;

	LIST_FOREACH(block, &block_head, block_link) {
		if (block->number == block_nr)
			return block->data;
	}

	debug("allocating block %x\n", block_nr);

	new_block = malloc(sizeof(*block));
	if (!new_block)
		err(1, "cannot allocate block");

	new_block->number = block_nr;
	new_block->offset = offset;
	new_block->data = malloc(blocksize);
	if (!new_block->data)
		err(1, "cannot allocate block data");

	memset(new_block->data, 0, blocksize);

	LIST_INSERT_HEAD(&block_head, new_block, block_link);

	return (new_block->data);
}

static int
nandfs_seg_usage_blk_offset(uint64_t seg, uint64_t *blk, uint64_t *offset)
{
	uint64_t off;
	uint16_t seg_size;

	seg_size = sizeof(struct nandfs_segment_usage);

	off = roundup(sizeof(struct nandfs_sufile_header), seg_size);
	off += (seg * seg_size);

	*blk = off / blocksize;
	*offset = (off % blocksize) / seg_size;
	return (0);
}

static uint32_t
segment_size(void)
{
	u_int size;

	size = sizeof(struct nandfs_segment_summary );
	size +=	seg_nblocks * sizeof(struct nandfs_binfo_v);

	if (size > blocksize)
		err(1, "segsum info bigger that blocksize");

	return (size);
}


static void
prepare_blockgrouped_file(uint32_t block)
{
	struct nandfs_block_group_desc *desc;
	uint32_t i, entries;

	desc = (struct nandfs_block_group_desc *)get_block(block, 0);
	entries = blocksize / sizeof(struct nandfs_block_group_desc);
	for (i = 0; i < entries; i++)
		desc[i].bg_nfrees = blocksize * 8;
}

static void
alloc_blockgrouped_file(uint32_t block, uint32_t entry)
{
	struct nandfs_block_group_desc *desc;
	uint32_t desc_nr;
	uint32_t *bitmap;

	desc = (struct nandfs_block_group_desc *)get_block(block, 0);
	bitmap = (uint32_t *)get_block(block + 1, 1);

	bitmap += (entry >> 5);
	if (*bitmap & (1 << (entry % 32))) {
		printf("nandfs: blockgrouped entry %d already allocated\n",
		    entry);
	}
	*bitmap |= (1 << (entry % 32));

	desc_nr = entry / (blocksize * 8);
	desc[desc_nr].bg_nfrees--;
}


static uint64_t
count_su_blocks(void)
{
	uint64_t maxblk, blk, offset, i;

	maxblk = blk = 0;

	for (i = 0; i < bad_segments_count; i++) {
		nandfs_seg_usage_blk_offset(bad_segments[i], &blk, &offset);
		debug("bad segment at block:%jx off: %jx", blk, offset);
		if (blk > maxblk)
			maxblk = blk;
	}

	debug("bad segment needs %#jx", blk);
	if (blk >= NANDFS_NDADDR) {
		printf("nandfs: file too big (%jd > %d)\n", blk, NANDFS_NDADDR);
		exit(2);
	}

	sufile.size = (blk + 1) * blocksize;
	return (blk + 1);
}

static void
count_seg_blocks(void)
{
	uint32_t i;

	for (i = 0; i < nuserfiles; i++)
		if (user_files[i].nblocks) {
			seg_nblocks += user_files[i].nblocks;
			user_files[i].blocks = malloc(user_files[i].nblocks * sizeof(uint32_t));
		}

	ifile.nblocks = 2 +
	    SIZE_TO_BLOCK(sizeof(struct nandfs_inode) * (NANDFS_USER_INO + 1));
	ifile.blocks = malloc(ifile.nblocks * sizeof(uint32_t));
	seg_nblocks += ifile.nblocks;

	cpfile.nblocks =
	    SIZE_TO_BLOCK((NANDFS_CPFILE_FIRST_CHECKPOINT_OFFSET + 1) *
	    sizeof(struct nandfs_checkpoint));
	cpfile.blocks = malloc(cpfile.nblocks * sizeof(uint32_t));
	seg_nblocks += cpfile.nblocks;

	if (!bad_segments) {
		sufile.nblocks =
		    SIZE_TO_BLOCK((NANDFS_SUFILE_FIRST_SEGMENT_USAGE_OFFSET + 1) *
		    sizeof(struct nandfs_segment_usage));
	} else {
		debug("bad blocks found: extra space for sufile");
		sufile.nblocks = count_su_blocks();
	}

	sufile.blocks = malloc(sufile.nblocks * sizeof(uint32_t));
	seg_nblocks += sufile.nblocks;

	datfile.nblocks = 2 +
	    SIZE_TO_BLOCK((seg_nblocks) * sizeof(struct nandfs_dat_entry));
	datfile.blocks = malloc(datfile.nblocks * sizeof(uint32_t));
	seg_nblocks += datfile.nblocks;
}

static void
assign_file_blocks(uint64_t start_block)
{
	uint32_t i, j;

	for (i = 0; i < nuserfiles; i++)
		for (j = 0; j < user_files[i].nblocks; j++) {
			debug("user file %d at block %d at %#jx",
			    i, j, (uintmax_t)start_block);
			user_files[i].blocks[j] = start_block++;
		}

	for (j = 0; j < ifile.nblocks; j++) {
		debug("ifile block %d at %#jx", j, (uintmax_t)start_block);
		ifile.blocks[j] = start_block++;
	}

	for (j = 0; j < cpfile.nblocks; j++) {
		debug("cpfile block %d at %#jx", j, (uintmax_t)start_block);
		cpfile.blocks[j] = start_block++;
	}

	for (j = 0; j < sufile.nblocks; j++) {
		debug("sufile block %d at %#jx", j, (uintmax_t)start_block);
		sufile.blocks[j] = start_block++;
	}

	for (j = 0; j < datfile.nblocks; j++) {
		debug("datfile block %d at %#jx", j, (uintmax_t)start_block);
		datfile.blocks[j] = start_block++;
	}

	/* add one for superroot */
	debug("sr at block %#jx", (uintmax_t)start_block);
	sr = (struct nandfs_super_root *)get_block(start_block++, 0);
	seg_endblock = start_block;
}

static void
save_datfile(void)
{

	prepare_blockgrouped_file(datfile.blocks[0]);
}

static uint64_t
update_datfile(uint64_t block)
{
	struct nandfs_dat_entry *dat;
	static uint64_t vblock = 0;
	uint64_t allocated, i, off;

	if (vblock == 0) {
		alloc_blockgrouped_file(datfile.blocks[0], vblock);
		vblock++;
	}
	allocated = vblock;
	i = vblock / (blocksize / sizeof(*dat));
	off = vblock % (blocksize / sizeof(*dat));
	vblock++;

	dat = (struct nandfs_dat_entry *)get_block(datfile.blocks[2 + i], 2 + i);

	alloc_blockgrouped_file(datfile.blocks[0], allocated);
	dat[off].de_blocknr = block;
	dat[off].de_start = NANDFS_FIRST_CNO;
	dat[off].de_end = UINTMAX_MAX;

	return (allocated);
}

static union nandfs_binfo *
update_block_info(union nandfs_binfo *binfo, struct file_info *file)
{
	nandfs_daddr_t vblock;
	uint32_t i;

	for (i = 0; i < file->nblocks; i++) {
		debug("%s: blk %x", __func__, i);
		if (file->ino != NANDFS_DAT_INO) {
			vblock = update_datfile(file->blocks[i]);
			binfo->bi_v.bi_vblocknr = vblock;
			binfo->bi_v.bi_blkoff = i;
			binfo->bi_v.bi_ino = file->ino;
			file->inode->i_db[i] = vblock;
		} else {
			binfo->bi_dat.bi_blkoff = i;
			binfo->bi_dat.bi_ino = file->ino;
			file->inode->i_db[i] = datfile.blocks[i];
		}
		binfo++;
	}

	return (binfo);
}

static void
save_segsum(struct nandfs_segment_summary *ss)
{
	union nandfs_binfo *binfo;
	struct nandfs_block *block;
	uint32_t sum_bytes, i;
	uint8_t crc_data, crc_skip;

	sum_bytes = segment_size();
	ss->ss_magic = NANDFS_SEGSUM_MAGIC;
	ss->ss_bytes = sizeof(struct nandfs_segment_summary);
	ss->ss_flags = NANDFS_SS_LOGBGN | NANDFS_SS_LOGEND | NANDFS_SS_SR;
	ss->ss_seq = 1;
	ss->ss_create = nandfs_time;

	ss->ss_next = nandfs_first_block() + blocks_per_segment;
	/* nblocks = segment blocks + segsum block + superroot */
	ss->ss_nblocks = seg_nblocks + 2;
	ss->ss_nbinfos = seg_nblocks;
	ss->ss_sumbytes = sum_bytes;

	crc_skip = sizeof(ss->ss_datasum) + sizeof(ss->ss_sumsum);
	ss->ss_sumsum = crc32_le(0, (uint8_t *)ss + crc_skip,
	    sum_bytes - crc_skip);
	crc_data = 0;

	binfo = (union nandfs_binfo *)(ss + 1);
	for (i = 0; i < nuserfiles; i++) {
		if (user_files[i].nblocks)
			binfo = update_block_info(binfo, &user_files[i]);
	}

	binfo = update_block_info(binfo, &ifile);
	binfo = update_block_info(binfo, &cpfile);
	binfo = update_block_info(binfo, &sufile);
	update_block_info(binfo, &datfile);

	/* save superroot crc */
	crc_skip = sizeof(sr->sr_sum);
	sr->sr_sum = crc32_le(0, (uint8_t *)sr + crc_skip,
	    NANDFS_SR_BYTES - crc_skip);

	/* segment checksup */
	crc_skip = sizeof(ss->ss_datasum);
	LIST_FOREACH(block, &block_head, block_link) {
		if (block->number < NANDFS_FIRST_BLOCK)
			continue;
		if (block->number == NANDFS_FIRST_BLOCK)
			crc_data = crc32_le(0,
			    (uint8_t *)block->data + crc_skip,
			    blocksize - crc_skip);
		else
			crc_data = crc32_le(crc_data, (uint8_t *)block->data,
			    blocksize);
	}
	ss->ss_datasum = crc_data;
}

static void
create_fsdata(void)
{
	struct uuid tmp;

	memset(&fsdata, 0, sizeof(struct nandfs_fsdata));

	fsdata.f_magic = NANDFS_FSDATA_MAGIC;
	fsdata.f_nsegments = nsegments;
	fsdata.f_erasesize = erasesize;
	fsdata.f_first_data_block = NANDFS_FIRST_BLOCK;
	fsdata.f_blocks_per_segment = blocks_per_segment;
	fsdata.f_r_segments_percentage = rsv_segment_percent;
	fsdata.f_rev_level = NANDFS_CURRENT_REV;
	fsdata.f_sbbytes = NANDFS_SB_BYTES;
	fsdata.f_bytes = NANDFS_FSDATA_CRC_BYTES;
	fsdata.f_ctime = nandfs_time;
	fsdata.f_log_block_size = nandfs_log2(blocksize) - 10;
	fsdata.f_errors = 1;
	fsdata.f_inode_size = sizeof(struct nandfs_inode);
	fsdata.f_dat_entry_size = sizeof(struct nandfs_dat_entry);
	fsdata.f_checkpoint_size = sizeof(struct nandfs_checkpoint);
	fsdata.f_segment_usage_size = sizeof(struct nandfs_segment_usage);

	uuidgen(&tmp, 1);
	fsdata.f_uuid = tmp;

	if (volumelabel)
		memcpy(fsdata.f_volume_name, volumelabel, 16);

	fsdata.f_sum = crc32_le(0, (const uint8_t *)&fsdata,
	    NANDFS_FSDATA_CRC_BYTES);
}

static void
save_fsdata(void *data)
{

	memcpy(data, &fsdata, sizeof(fsdata));
}

static void
create_super_block(void)
{

	memset(&super_block, 0, sizeof(struct nandfs_super_block));

	super_block.s_magic = NANDFS_SUPER_MAGIC;
	super_block.s_last_cno = NANDFS_FIRST_CNO;
	super_block.s_last_pseg = NANDFS_FIRST_BLOCK;
	super_block.s_last_seq = 1;
	super_block.s_free_blocks_count =
	    (nsegments - bad_segments_count) * blocks_per_segment;
	super_block.s_mtime = 0;
	super_block.s_wtime = nandfs_time;
	super_block.s_state = NANDFS_VALID_FS;

	super_block.s_sum = crc32_le(0, (const uint8_t *)&super_block,
	    NANDFS_SB_BYTES);
}

static void
save_super_block(void *data)
{

	memcpy(data, &super_block, sizeof(super_block));
}

static void
save_super_root(void)
{

	sr->sr_bytes = NANDFS_SR_BYTES;
	sr->sr_flags = 0;
	sr->sr_nongc_ctime = nandfs_time;
	datfile.inode = &sr->sr_dat;
	cpfile.inode = &sr->sr_cpfile;
	sufile.inode = &sr->sr_sufile;
}

static struct nandfs_dir_entry *
add_de(void *block, struct nandfs_dir_entry *de, uint64_t ino,
    const char *name, uint8_t type)
{
	uint16_t reclen;

	/* modify last de */
	de->rec_len = NANDFS_DIR_REC_LEN(de->name_len);
	de = (void *)((uint8_t *)de + de->rec_len);

	reclen = blocksize - ((uintptr_t)de - (uintptr_t)block);
	if (reclen < NANDFS_DIR_REC_LEN(strlen(name))) {
		printf("nandfs: too many dir entries for one block\n");
		return (NULL);
	}

	de->inode = ino;
	de->rec_len = reclen;
	de->name_len = strlen(name);
	de->file_type = type;
	memset(de->name, 0,
	    (strlen(name) + NANDFS_DIR_PAD - 1) & ~NANDFS_DIR_ROUND);
	memcpy(de->name, name, strlen(name));

	return (de);
}

static struct nandfs_dir_entry *
make_dir(void *block, uint64_t ino, uint64_t parent_ino)
{
	struct nandfs_dir_entry *de = (struct nandfs_dir_entry *)block;

	/* create '..' entry */
	de->inode = parent_ino;
	de->rec_len = NANDFS_DIR_REC_LEN(2);
	de->name_len = 2;
	de->file_type = DT_DIR;
	memset(de->name, 0, NANDFS_DIR_NAME_LEN(2));
	memcpy(de->name, "..", 2);

	/* create '.' entry */
	de = (void *)((uint8_t *)block + NANDFS_DIR_REC_LEN(2));
	de->inode = ino;
	de->rec_len = blocksize - NANDFS_DIR_REC_LEN(2);
	de->name_len = 1;
	de->file_type = DT_DIR;
	memset(de->name, 0, NANDFS_DIR_NAME_LEN(1));
	memcpy(de->name, ".", 1);

	return (de);
}

static void
save_root_dir(void)
{
	struct file_info *root = &user_files[0];
	struct nandfs_dir_entry *de;
	uint32_t i;
	void *block;

	block = get_block(root->blocks[0], 0);

	de = make_dir(block, root->ino, root->ino);
	for (i = 1; i < nuserfiles; i++)
		de = add_de(block, de, user_files[i].ino, user_files[i].name,
		    IFTODT(user_files[i].mode));

	root->size = ((uintptr_t)de - (uintptr_t)block) +
	    NANDFS_DIR_REC_LEN(de->name_len);
}

static void
save_sufile(void)
{
	struct nandfs_sufile_header *header;
	struct nandfs_segment_usage *su;
	uint64_t blk, i, off;
	void *block;
	int start;

	/*
	 * At the beginning just zero-out everything
	 */
	for (i = 0; i < sufile.nblocks; i++)
		get_block(sufile.blocks[i], 0);

	start = 0;

	block = get_block(sufile.blocks[start], 0);
	header = (struct nandfs_sufile_header *)block;
	header->sh_ncleansegs = nsegments - bad_segments_count - 1;
	header->sh_ndirtysegs = 1;
	header->sh_last_alloc = 1;

	su = (struct nandfs_segment_usage *)header;
	off = NANDFS_SUFILE_FIRST_SEGMENT_USAGE_OFFSET;
	/* Allocate data segment */
	su[off].su_lastmod = nandfs_time;
	/* nblocks = segment blocks + segsum block + superroot */
	su[off].su_nblocks = seg_nblocks + 2;
	su[off].su_flags = NANDFS_SEGMENT_USAGE_DIRTY;
	off++;
	/* Allocate next segment */
	su[off].su_lastmod = nandfs_time;
	su[off].su_nblocks = 0;
	su[off].su_flags = NANDFS_SEGMENT_USAGE_DIRTY;
	for (i = 0; i < bad_segments_count; i++) {
		nandfs_seg_usage_blk_offset(bad_segments[i], &blk, &off);
		debug("storing bad_segments[%jd]=%x at %jx off %jx\n", i,
		    bad_segments[i], blk, off);
		block = get_block(sufile.blocks[blk],
		    off * sizeof(struct nandfs_segment_usage *));
		su = (struct nandfs_segment_usage *)block;
		su[off].su_lastmod = nandfs_time;
		su[off].su_nblocks = 0;
		su[off].su_flags = NANDFS_SEGMENT_USAGE_ERROR;
	}
}

static void
save_cpfile(void)
{
	struct nandfs_cpfile_header *header;
	struct nandfs_checkpoint *cp, *initial_cp;
	int i, entries = blocksize / sizeof(struct nandfs_checkpoint);
	uint64_t cno;

	header = (struct nandfs_cpfile_header *)get_block(cpfile.blocks[0], 0);
	header->ch_ncheckpoints = 1;
	header->ch_nsnapshots = 0;

	cp = (struct nandfs_checkpoint *)header;

	/* fill first checkpoint data*/
	initial_cp = &cp[NANDFS_CPFILE_FIRST_CHECKPOINT_OFFSET];
	initial_cp->cp_flags = 0;
	initial_cp->cp_checkpoints_count = 0;
	initial_cp->cp_cno = NANDFS_FIRST_CNO;
	initial_cp->cp_create = nandfs_time;
	initial_cp->cp_nblk_inc = seg_endblock - 1;
	initial_cp->cp_blocks_count = seg_nblocks;
	memset(&initial_cp->cp_snapshot_list, 0,
	    sizeof(struct nandfs_snapshot_list));

	ifile.inode = &initial_cp->cp_ifile_inode;

	/* mark rest of cp as invalid */
	cno = NANDFS_FIRST_CNO + 1;
	i = NANDFS_CPFILE_FIRST_CHECKPOINT_OFFSET + 1;
	for (; i < entries; i++) {
		cp[i].cp_cno = cno++;
		cp[i].cp_flags = NANDFS_CHECKPOINT_INVALID;
	}
}

static void
init_inode(struct nandfs_inode *inode, struct file_info *file)
{

	inode->i_blocks = file->nblocks;
	inode->i_ctime = nandfs_time;
	inode->i_mtime = nandfs_time;
	inode->i_mode = file->mode & 0xffff;
	inode->i_links_count = 1;

	if (file->size > 0)
		inode->i_size = file->size;
	else
		inode->i_size = 0;

	if (file->ino == NANDFS_USER_INO)
		inode->i_flags = SF_NOUNLINK|UF_NOUNLINK;
	else
		inode->i_flags = 0;
}

static void
save_ifile(void)
{
	struct nandfs_inode *inode;
	struct file_info *file;
	uint64_t ino, blk, off;
	uint32_t i;

	prepare_blockgrouped_file(ifile.blocks[0]);
	for (i = 0; i <= NANDFS_USER_INO; i++)
		alloc_blockgrouped_file(ifile.blocks[0], i);

	for (i = 0; i < nuserfiles; i++) {
		file = &user_files[i];
		ino = file->ino;
		blk = ino / (blocksize / sizeof(*inode));
		off = ino % (blocksize / sizeof(*inode));
		inode =
		    (struct nandfs_inode *)get_block(ifile.blocks[2 + blk], 2 + blk);
		file->inode = &inode[off];
		init_inode(file->inode, file);
	}

	init_inode(ifile.inode, &ifile);
	init_inode(cpfile.inode, &cpfile);
	init_inode(sufile.inode, &sufile);
	init_inode(datfile.inode, &datfile);
}

static int
create_fs(void)
{
	uint64_t start_block;
	uint32_t segsum_size;
	char *data;
	int i;

	nuserfiles = nitems(user_files);

	/* Count and assign blocks */
	count_seg_blocks();
	segsum_size = segment_size();
	start_block = NANDFS_FIRST_BLOCK + SIZE_TO_BLOCK(segsum_size);
	assign_file_blocks(start_block);

	/* Create super root structure */
	save_super_root();

	/* Create root directory */
	save_root_dir();

	/* Fill in file contents */
	save_sufile();
	save_cpfile();
	save_ifile();
	save_datfile();

	/* Save fsdata and superblocks */
	create_fsdata();
	create_super_block();

	for (i = 0; i < NANDFS_NFSAREAS; i++) {
		if (fsdata_blocks_state[i] != NANDFS_BLOCK_GOOD)
			continue;

		data = get_block((i * erasesize)/blocksize, 0);
		save_fsdata(data);

		data = get_block((i * erasesize + NANDFS_SBLOCK_OFFSET_BYTES) /
		    blocksize, 0);
		if (blocksize > NANDFS_SBLOCK_OFFSET_BYTES)
			data += NANDFS_SBLOCK_OFFSET_BYTES;
		save_super_block(data);
		memset(data + sizeof(struct nandfs_super_block), 0xff,
		    (blocksize - sizeof(struct nandfs_super_block) -
		    NANDFS_SBLOCK_OFFSET_BYTES));
	}

	/* Save segment summary and CRCs */
	save_segsum(get_block(NANDFS_FIRST_BLOCK, 0));

	return (0);
}

static void
write_fs(int fda)
{
	struct nandfs_block *block;
	char *data;
	u_int ret;

	/* Overwrite next block with ff if not nand device */
	if (!is_nand) {
		data = get_block(seg_endblock, 0);
		memset(data, 0xff, blocksize);
	}

	LIST_FOREACH(block, &block_head, block_link) {
		lseek(fda, block->number * blocksize, SEEK_SET);
		ret = write(fda, block->data, blocksize);
		if (ret != blocksize)
			err(1, "cannot write filesystem data");
	}
}

static void
check_parameters(void)
{
	int i;

	/* check blocksize */
	if ((blocksize < NANDFS_MIN_BLOCKSIZE) || (blocksize > MAXBSIZE) ||
	    ((blocksize - 1) & blocksize)) {
		errx(1, "Bad blocksize (%zu). Must be in range [%u-%u] "
		    "and a power of two.", blocksize, NANDFS_MIN_BLOCKSIZE,
		    MAXBSIZE);
	}

	/* check blocks per segments */
	if ((blocks_per_segment < NANDFS_SEG_MIN_BLOCKS) ||
	    ((blocksize - 1) & blocksize))
		errx(1, "Bad blocks per segment (%lu). Must be greater than "
		    "%u and a power of two.", blocks_per_segment,
		    NANDFS_SEG_MIN_BLOCKS);

	/* check reserved segment percentage */
	if ((rsv_segment_percent < 1) || (rsv_segment_percent > 99))
		errx(1, "Bad reserved segment percentage. "
		    "Must in range 1..99.");

	/* check volume label */
	i = 0;
	if (volumelabel) {
		while (isalnum(volumelabel[++i]))
			;

		if (volumelabel[i] != '\0') {
			errx(1, "bad volume label. "
			    "Valid characters are alphanumerics.");
		}

		if (strlen(volumelabel) >= 16)
			errx(1, "Bad volume label. Length is longer than %d.",
			    16);
	}

	nandfs_time = time(NULL);
}

static void
print_parameters(void)
{

	printf("filesystem parameters:\n");
	printf("blocksize: %#zx sectorsize: %#zx\n", blocksize, sectorsize);
	printf("erasesize: %#jx mediasize: %#jx\n", erasesize, mediasize);
	printf("segment size: %#jx blocks per segment: %#x\n", segsize,
	    (uint32_t)blocks_per_segment);
}

/*
 * Exit with error if file system is mounted.
 */
static void
check_mounted(const char *fname, mode_t mode)
{
	struct statfs *mp;
	const char *s1, *s2;
	size_t len;
	int n, r;

	if (!(n = getmntinfo(&mp, MNT_NOWAIT)))
		err(1, "getmntinfo");

	len = strlen(_PATH_DEV);
	s1 = fname;
	if (!strncmp(s1, _PATH_DEV, len))
		s1 += len;

	r = S_ISCHR(mode) && s1 != fname && *s1 == 'r';

	for (; n--; mp++) {
		s2 = mp->f_mntfromname;

		if (!strncmp(s2, _PATH_DEV, len))
			s2 += len;
		if ((r && s2 != mp->f_mntfromname && !strcmp(s1 + 1, s2)) ||
		    !strcmp(s1, s2))
			errx(1, "%s is mounted on %s", fname, mp->f_mntonname);
	}
}

static void
calculate_geometry(int fd)
{
	struct chip_param_io chip_params;
	char ident[DISK_IDENT_SIZE];
	char medianame[MAXPATHLEN];

	/* Check storage type */
	g_get_ident(fd, ident, DISK_IDENT_SIZE);
	g_get_name(ident, medianame, MAXPATHLEN);
	debug("device name: %s", medianame);

	is_nand = (strstr(medianame, "gnand") != NULL);
	debug("is_nand = %d", is_nand);

	sectorsize = g_sectorsize(fd);
	debug("sectorsize: %#zx", sectorsize);

	/* Get storage size */
	mediasize = g_mediasize(fd);
	debug("mediasize: %#jx", mediasize);

	/* Get storage erase unit size */
	if (!is_nand)
		erasesize = NANDFS_DEF_ERASESIZE;
	else if (ioctl(fd, NAND_IO_GET_CHIP_PARAM, &chip_params) != -1)
		erasesize = chip_params.page_size * chip_params.pages_per_block;
	else
		errx(1, "Cannot ioctl(NAND_IO_GET_CHIP_PARAM)");

	debug("erasesize: %#jx", (uintmax_t)erasesize);

	if (blocks_per_segment == 0) {
		if (erasesize >= NANDFS_MIN_SEGSIZE)
			blocks_per_segment = erasesize / blocksize;
		else
			blocks_per_segment = NANDFS_MIN_SEGSIZE / blocksize;
	}

	/* Calculate number of segments */
	segsize = blocksize * blocks_per_segment;
	nsegments = ((mediasize - NANDFS_NFSAREAS * erasesize) / segsize) - 2;
	debug("segsize: %#jx", segsize);
	debug("nsegments: %#jx", nsegments);
}

static void
erase_device(int fd)
{
	int rest, failed;
	uint64_t i, nblocks;
	off_t offset;

	failed = 0;
	for (i = 0; i < NANDFS_NFSAREAS; i++) {
		debug("Deleting %jx\n", i * erasesize);
		if (g_delete(fd, i * erasesize, erasesize)) {
			printf("cannot delete %jx\n", i * erasesize);
			fsdata_blocks_state[i] = NANDFS_BLOCK_BAD;
			failed++;
		} else
			fsdata_blocks_state[i] = NANDFS_BLOCK_GOOD;
	}

	if (failed == NANDFS_NFSAREAS) {
		printf("%d first blocks not usable. Unable to create "
		    "filesystem.\n", failed);
		exit(1);
	}

	for (i = 0; i < nsegments; i++) {
		offset = NANDFS_NFSAREAS * erasesize + i * segsize;
		if (g_delete(fd, offset, segsize)) {
			printf("cannot delete segment %jx (offset %jd)\n",
			    i, offset);
			bad_segments_count++;
			bad_segments = realloc(bad_segments,
			    bad_segments_count * sizeof(uint32_t));
			bad_segments[bad_segments_count - 1] = i;
		}
	}

	if (bad_segments_count == nsegments) {
		printf("no valid segments\n");
		exit(1);
	}

	/* Delete remaining blocks at the end of device */
	rest = mediasize % segsize;
	nblocks = rest / erasesize;
	for (i = 0; i < nblocks; i++) {
		offset = (segsize * nsegments) + (i * erasesize);
		if (g_delete(fd, offset, erasesize)) {
			printf("cannot delete space after last segment "
			    "- probably a bad block\n");
		}
	}
}

static void
erase_initial(int fd)
{
	char buf[512];
	u_int i;

	memset(buf, 0xff, sizeof(buf));

	lseek(fd, 0, SEEK_SET);
	for (i = 0; i < NANDFS_NFSAREAS * erasesize; i += sizeof(buf))
		write(fd, buf, sizeof(buf));
}

static void
create_nandfs(int fd)
{

	create_fs();

	write_fs(fd);
}

static void
print_summary(void)
{

	printf("filesystem was created successfully\n");
	printf("total segments: %#jx valid segments: %#jx\n", nsegments,
	    nsegments - bad_segments_count);
	printf("total space: %ju MB free: %ju MB\n",
	    (nsegments *
	    blocks_per_segment * blocksize) / (1024 * 1024),
	    ((nsegments - bad_segments_count) *
	    blocks_per_segment * blocksize) / (1024 * 1024));
}

int
main(int argc, char *argv[])
{
	struct stat sb;
	char buf[MAXPATHLEN];
	const char opts[] = "b:B:L:m:";
	const char *fname;
	int ch, fd;

	while ((ch = getopt(argc, argv, opts)) != -1) {
		switch (ch) {
		case 'b':
			blocksize = strtol(optarg, (char **)NULL, 10);
			if (blocksize == 0)
				usage();
			break;
		case 'B':
			blocks_per_segment = strtol(optarg, (char **)NULL, 10);
			if (blocks_per_segment == 0)
				usage();
			break;
		case 'L':
			volumelabel = optarg;
			break;
		case 'm':
			rsv_segment_percent = strtol(optarg, (char **)NULL, 10);
			if (rsv_segment_percent == 0)
				usage();
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc < 1 || argc > 2)
		usage();

	/* construct proper device path */
	fname = *argv++;
	if (!strchr(fname, '/')) {
		snprintf(buf, sizeof(buf), "%s%s", _PATH_DEV, fname);
		if (!(fname = strdup(buf)))
			err(1, NULL);
	}

	fd = g_open(fname, 1);
	if (fd == -1)
		err(1, "Cannot open %s", fname);

	if (fstat(fd, &sb) == -1)
		err(1, "Cannot stat %s", fname);
	if (!S_ISCHR(sb.st_mode))
		warnx("%s is not a character device", fname);

	check_mounted(fname, sb.st_mode);

	calculate_geometry(fd);

	check_parameters();

	print_parameters();

	if (is_nand)
		erase_device(fd);
	else
		erase_initial(fd);

	create_nandfs(fd);

	print_summary();

	g_close(fd);

	return (0);
}


