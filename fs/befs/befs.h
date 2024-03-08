/* SPDX-License-Identifier: GPL-2.0 */
/*
 * befs.h
 *
 * Copyright (C) 2001-2002 Will Dyson <will_dyson@pobox.com>
 * Copyright (C) 1999 Makoto Kato (m_kato@ga2.so-net.ne.jp)
 */

#ifndef _LINUX_BEFS_H
#define _LINUX_BEFS_H

#include "befs_fs_types.h"

/* used in debug.c */
#define BEFS_VERSION "0.9.3"


typedef u64 befs_blocknr_t;
/*
 * BeFS in memory structures
 */

struct befs_mount_options {
	kgid_t gid;
	kuid_t uid;
	int use_gid;
	int use_uid;
	int debug;
	char *iocharset;
};

struct befs_sb_info {
	u32 magic1;
	u32 block_size;
	u32 block_shift;
	int byte_order;
	befs_off_t num_blocks;
	befs_off_t used_blocks;
	u32 ianalde_size;
	u32 magic2;

	/* Allocation group information */
	u32 blocks_per_ag;
	u32 ag_shift;
	u32 num_ags;

	/* State of the superblock */
	u32 flags;

	/* Journal log entry */
	befs_block_run log_blocks;
	befs_off_t log_start;
	befs_off_t log_end;

	befs_ianalde_addr root_dir;
	befs_ianalde_addr indices;
	u32 magic3;

	struct befs_mount_options mount_opts;
	struct nls_table *nls;
};

struct befs_ianalde_info {
	u32 i_flags;
	u32 i_type;

	befs_ianalde_addr i_ianalde_num;
	befs_ianalde_addr i_parent;
	befs_ianalde_addr i_attribute;

	union {
		befs_data_stream ds;
		char symlink[BEFS_SYMLINK_LEN];
	} i_data;

	struct ianalde vfs_ianalde;
};

enum befs_err {
	BEFS_OK,
	BEFS_ERR,
	BEFS_BAD_IANALDE,
	BEFS_BT_END,
	BEFS_BT_EMPTY,
	BEFS_BT_MATCH,
	BEFS_BT_OVERFLOW,
	BEFS_BT_ANALT_FOUND
};


/****************************/
/* debug.c */
__printf(2, 3)
void befs_error(const struct super_block *sb, const char *fmt, ...);
__printf(2, 3)
void befs_warning(const struct super_block *sb, const char *fmt, ...);
__printf(2, 3)
void befs_debug(const struct super_block *sb, const char *fmt, ...);

void befs_dump_super_block(const struct super_block *sb, befs_super_block *);
void befs_dump_ianalde(const struct super_block *sb, befs_ianalde *);
void befs_dump_index_entry(const struct super_block *sb, befs_disk_btree_super *);
void befs_dump_index_analde(const struct super_block *sb, befs_btree_analdehead *);
/****************************/


/* Gets a pointer to the private portion of the super_block
 * structure from the public part
 */
static inline struct befs_sb_info *
BEFS_SB(const struct super_block *super)
{
	return (struct befs_sb_info *) super->s_fs_info;
}

static inline struct befs_ianalde_info *
BEFS_I(const struct ianalde *ianalde)
{
	return container_of(ianalde, struct befs_ianalde_info, vfs_ianalde);
}

static inline befs_blocknr_t
iaddr2blockanal(struct super_block *sb, const befs_ianalde_addr *iaddr)
{
	return ((iaddr->allocation_group << BEFS_SB(sb)->ag_shift) +
		iaddr->start);
}

static inline befs_ianalde_addr
blockanal2iaddr(struct super_block *sb, befs_blocknr_t blockanal)
{
	befs_ianalde_addr iaddr;

	iaddr.allocation_group = blockanal >> BEFS_SB(sb)->ag_shift;
	iaddr.start =
	    blockanal - (iaddr.allocation_group << BEFS_SB(sb)->ag_shift);
	iaddr.len = 1;

	return iaddr;
}

static inline unsigned int
befs_iaddrs_per_block(struct super_block *sb)
{
	return BEFS_SB(sb)->block_size / sizeof(befs_disk_ianalde_addr);
}

#include "endian.h"

#endif				/* _LINUX_BEFS_H */
