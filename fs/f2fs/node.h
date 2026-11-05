/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fs/f2fs/node.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
/* start node id of a node block dedicated to the given node id */
static inline nid_t f2fs_start_nid(struct f2fs_sb_info *sbi, nid_t nid)
{
	unsigned int entries = NAT_ENTRY_PER_BLOCK(sbi);

	return (nid / entries) * entries;
}

/* node block offset on the NAT area dedicated to the given start node id */
#define	NAT_BLOCK_OFFSET(sbi, start_nid) \
	((start_nid) / NAT_ENTRY_PER_BLOCK(sbi))

/* # of blocks to perform synchronous readahead before building free nids */
#define FREE_NID_BLOCKS	8
#define MAX_FREE_NIDS(sbi)	((unsigned long)NAT_ENTRY_PER_BLOCK(sbi) * \
				 FREE_NID_BLOCKS)

/* size of free nid batch when shrinking */
#define SHRINK_NID_BATCH_SIZE	8

#define DEF_RA_NID_BLOCKS	0	/* # of nid blocks to be readaheaded */

/* maximum readahead size for node during getting data blocks */
#define MAX_RA_NODE		128

/* control the memory footprint threshold (10MB per 1GB ram) */
#define DEF_RAM_THRESHOLD	1

/* control dirty nats ratio threshold (default: 10% over max nid count) */
#define DEF_DIRTY_NAT_RATIO_THRESHOLD		10
/* control total # of nats */
#define DEF_NAT_CACHE_THRESHOLD			100000

/* control total # of node writes used for roll-forward recovery */
#define DEF_RF_NODE_BLOCKS			0

/* vector size for gang look-up from nat cache that consists of radix tree */
#define NAT_VEC_SIZE	32

/* return value for read_node_cache */
#define LOCKED_CACHE	1

/* check pinned file's alignment status of physical blocks */
#define FILE_NOT_ALIGNED	1

/* For flag in struct node_info */
enum {
	IS_CHECKPOINTED,	/* is it checkpointed before? */
	HAS_FSYNCED_INODE,	/* is the inode fsynced before? */
	HAS_LAST_FSYNC,		/* has the latest node fsync mark? */
	IS_DIRTY,		/* this nat entry is dirty? */
	IS_PREALLOC,		/* nat entry is preallocated */
};

/*
 * For node information
 */
struct node_info {
	nid_t nid;		/* node id */
	nid_t ino;		/* inode number of the node's owner */
	block_t	blk_addr;	/* block address of the node */
	unsigned char version;	/* version of the node */
	unsigned char flag;	/* for node information bits */
};

struct nat_entry {
	struct list_head list;	/* for clean or dirty nat list */
	struct node_info ni;	/* in-memory node information */
};

#define nat_get_nid(nat)		((nat)->ni.nid)
#define nat_set_nid(nat, n)		((nat)->ni.nid = (n))
#define nat_get_blkaddr(nat)		((nat)->ni.blk_addr)
#define nat_set_blkaddr(nat, b)		((nat)->ni.blk_addr = (b))
#define nat_get_ino(nat)		((nat)->ni.ino)
#define nat_set_ino(nat, i)		((nat)->ni.ino = (i))
#define nat_get_version(nat)		((nat)->ni.version)
#define nat_set_version(nat, v)		((nat)->ni.version = (v))

#define inc_node_version(version)	(++(version))

static inline void copy_node_info(struct node_info *dst,
						struct node_info *src)
{
	dst->nid = src->nid;
	dst->ino = src->ino;
	dst->blk_addr = src->blk_addr;
	dst->version = src->version;
	/* should not copy flag here */
}

static inline void set_nat_flag(struct nat_entry *ne,
				unsigned int type, bool set)
{
	if (set)
		ne->ni.flag |= BIT(type);
	else
		ne->ni.flag &= ~BIT(type);
}

static inline bool get_nat_flag(struct nat_entry *ne, unsigned int type)
{
	return ne->ni.flag & BIT(type);
}

static inline void nat_reset_flag(struct nat_entry *ne)
{
	/* these states can be set only after checkpoint was done */
	set_nat_flag(ne, IS_CHECKPOINTED, true);
	set_nat_flag(ne, HAS_FSYNCED_INODE, false);
	set_nat_flag(ne, HAS_LAST_FSYNC, true);
}

static inline void node_info_from_raw_nat(struct node_info *ni,
						struct f2fs_nat_entry *raw_ne)
{
	ni->ino = le32_to_cpu(raw_ne->ino);
	ni->blk_addr = le32_to_cpu(raw_ne->block_addr);
	ni->version = raw_ne->version;
}

static inline void raw_nat_from_node_info(struct f2fs_nat_entry *raw_ne,
						struct node_info *ni)
{
	raw_ne->ino = cpu_to_le32(ni->ino);
	raw_ne->block_addr = cpu_to_le32(ni->blk_addr);
	raw_ne->version = ni->version;
}

static inline bool excess_dirty_nats(struct f2fs_sb_info *sbi)
{
	/* nat_cnt[] is heuristic accounting sampled locklessly here. */
	return data_race(READ_ONCE(NM_I(sbi)->nat_cnt[DIRTY_NAT])) >=
					NM_I(sbi)->max_nid *
					NM_I(sbi)->dirty_nats_ratio / 100;
}

static inline bool excess_cached_nats(struct f2fs_sb_info *sbi)
{
	/* nat_cnt[] is heuristic accounting sampled locklessly here. */
	return data_race(READ_ONCE(NM_I(sbi)->nat_cnt[TOTAL_NAT])) >=
					DEF_NAT_CACHE_THRESHOLD;
}

enum mem_type {
	FREE_NIDS,	/* indicates the free nid list */
	NAT_ENTRIES,	/* indicates the cached nat entry */
	DIRTY_DENTS,	/* indicates dirty dentry pages */
	INO_ENTRIES,	/* indicates inode entries */
	READ_EXTENT_CACHE,	/* indicates read extent cache */
	AGE_EXTENT_CACHE,	/* indicates age extent cache */
	DISCARD_CACHE,	/* indicates memory of cached discard cmds */
	COMPRESS_BLOCK,	/* indicates memory of cached compressed blocks */
	META_BLOCK,	/* indicates memory of cached meta blocks */
	NODE_BLOCK,	/* indicates memory of cached node blocks */
	BASE_CHECK,	/* check kernel status */
};

struct nat_entry_set {
	struct list_head set_list;	/* link with other nat sets */
	struct list_head entry_list;	/* link with dirty nat entries */
	nid_t set;			/* set number*/
	unsigned int entry_cnt;		/* the # of nat entries in set */
};

struct free_nid {
	struct list_head list;	/* for free node id list */
	nid_t nid;		/* node id */
	int state;		/* in use or not: FREE_NID or PREALLOC_NID */
};

static inline void next_free_nid(struct f2fs_sb_info *sbi, nid_t *nid)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	struct free_nid *fnid;

	spin_lock(&nm_i->nid_list_lock);
	if (nm_i->nid_cnt[FREE_NID] <= 0) {
		spin_unlock(&nm_i->nid_list_lock);
		return;
	}
	fnid = list_first_entry(&nm_i->free_nid_list, struct free_nid, list);
	*nid = fnid->nid;
	spin_unlock(&nm_i->nid_list_lock);
}

/*
 * inline functions
 */
static inline void get_nat_bitmap(struct f2fs_sb_info *sbi, void *addr)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);

#ifdef CONFIG_F2FS_CHECK_FS
	if (memcmp(nm_i->nat_bitmap, nm_i->nat_bitmap_mir,
						nm_i->bitmap_size))
		f2fs_bug_on(sbi, 1);
#endif
	memcpy(addr, nm_i->nat_bitmap, nm_i->bitmap_size);
}

static inline pgoff_t current_nat_addr(struct f2fs_sb_info *sbi, nid_t start)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);
	pgoff_t block_off;
	pgoff_t block_addr;

	/*
	 * block_off = segment_off * 512 + off_in_segment
	 * OLD = (segment_off * 512) * 2 + off_in_segment
	 * NEW = 2 * (segment_off * 512 + off_in_segment) - off_in_segment
	 */
	block_off = NAT_BLOCK_OFFSET(sbi, start);

	block_addr = (pgoff_t)(nm_i->nat_blkaddr +
		(block_off << 1) -
		(block_off & (BLKS_PER_SEG(sbi) - 1)));

	if (f2fs_test_bit(block_off, nm_i->nat_bitmap))
		block_addr += BLKS_PER_SEG(sbi);

	return block_addr;
}

static inline pgoff_t next_nat_addr(struct f2fs_sb_info *sbi,
						pgoff_t block_addr)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);

	block_addr -= nm_i->nat_blkaddr;
	block_addr ^= BIT(sbi->log_blocks_per_seg);
	return block_addr + nm_i->nat_blkaddr;
}

static inline void set_to_next_nat(struct f2fs_sb_info *sbi,
				   struct f2fs_nm_info *nm_i, nid_t start_nid)
{
	unsigned int block_off = NAT_BLOCK_OFFSET(sbi, start_nid);

	f2fs_change_bit(block_off, nm_i->nat_bitmap);
#ifdef CONFIG_F2FS_CHECK_FS
	f2fs_change_bit(block_off, nm_i->nat_bitmap_mir);
#endif
}

static inline nid_t ino_of_node(struct f2fs_sb_info *sbi,
				const struct f2fs_cached_block *entry)
{
	return le32_to_cpu(F2FS_NODE_FOOTER(sbi, entry)->ino);
}

static inline nid_t nid_of_node(struct f2fs_sb_info *sbi,
				const struct f2fs_cached_block *entry)
{
	return le32_to_cpu(F2FS_NODE_FOOTER(sbi, entry)->nid);
}

static inline unsigned int ofs_of_node(struct f2fs_sb_info *sbi,
				const struct f2fs_cached_block *entry)
{
	unsigned int flag = le32_to_cpu(F2FS_NODE_FOOTER(sbi, entry)->flag);
	return flag >> OFFSET_BIT_SHIFT;
}

static inline __u64 cpver_of_node(struct f2fs_sb_info *sbi,
				const struct f2fs_cached_block *entry)
{
	return le64_to_cpu(F2FS_NODE_FOOTER(sbi, entry)->cp_ver);
}

static inline block_t next_blkaddr_of_node(struct f2fs_sb_info *sbi,
				const struct f2fs_cached_block *entry)
{
	return le32_to_cpu(F2FS_NODE_FOOTER(sbi, entry)->next_blkaddr);
}

static inline void fill_node_footer(struct f2fs_sb_info *sbi,
				struct f2fs_cached_block *entry, nid_t nid,
				nid_t ino, unsigned int ofs, bool reset)
{
	struct f2fs_node *rn = CACHED_NODE(entry);
	struct node_footer *footer = F2FS_NODE_FOOTER(sbi, entry);
	unsigned int old_flag = 0;

	if (reset)
		memset(rn, 0, F2FS_BLKSIZE(sbi));
	else
		old_flag = le32_to_cpu(footer->flag);

	memset(footer, 0, sizeof(*footer));
	footer->nid = cpu_to_le32(nid);
	footer->ino = cpu_to_le32(ino);

	/* should remain old flag bits such as COLD_BIT_SHIFT */
	footer->flag = cpu_to_le32((ofs << OFFSET_BIT_SHIFT) |
					(old_flag & OFFSET_BIT_MASK));
}

static inline void copy_node_footer(struct f2fs_sb_info *sbi,
		struct f2fs_cached_block *dst,
		const struct f2fs_cached_block *src)
{
	memcpy(F2FS_NODE_FOOTER(sbi, dst), F2FS_NODE_FOOTER(sbi, src),
	       sizeof(struct node_footer));
}

static inline void fill_node_footer_blkaddr(struct f2fs_sb_info *sbi,
				struct f2fs_cached_block *entry, block_t blkaddr)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	struct node_footer *footer = F2FS_NODE_FOOTER(sbi, entry);
	__u64 cp_ver = cur_cp_version(ckpt);

	if (__is_set_ckpt_flags(ckpt, CP_CRC_RECOVERY_FLAG))
		cp_ver |= (cur_cp_crc(ckpt) << 32);

	footer->cp_ver = cpu_to_le64(cp_ver);
	footer->next_blkaddr = cpu_to_le32(blkaddr);
}

static inline bool is_recoverable_dnode(struct f2fs_sb_info *sbi,
				const struct f2fs_cached_block *entry)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	__u64 cp_ver = cur_cp_version(ckpt);

	/* Don't care crc part, if fsck.f2fs sets it. */
	if (__is_set_ckpt_flags(ckpt, CP_NOCRC_RECOVERY_FLAG))
		return (cp_ver << 32) == (cpver_of_node(sbi, entry) << 32);

	if (__is_set_ckpt_flags(ckpt, CP_CRC_RECOVERY_FLAG))
		cp_ver |= (cur_cp_crc(ckpt) << 32);

	return cp_ver == cpver_of_node(sbi, entry);
}

/*
 * f2fs assigns the following node offsets described as (num).
 * N = NIDS_PER_BLOCK
 *
 *  Inode block (0)
 *    |- direct node (1)
 *    |- direct node (2)
 *    |- indirect node (3)
 *    |            `- direct node (4 => 4 + N - 1)
 *    |- indirect node (4 + N)
 *    |            `- direct node (5 + N => 5 + 2N - 1)
 *    `- double indirect node (5 + 2N)
 *                 `- indirect node (6 + 2N)
 *                       `- direct node
 *                 ......
 *                 `- indirect node ((6 + 2N) + x(N + 1))
 *                       `- direct node
 *                 ......
 *                 `- indirect node ((6 + 2N) + (N - 1)(N + 1))
 *                       `- direct node
 */
static inline bool IS_DNODE(struct f2fs_sb_info *sbi,
				const struct f2fs_cached_block *entry)
{
	unsigned int ofs = ofs_of_node(sbi, entry);

	if (f2fs_has_xattr_block(ofs))
		return true;

	if (ofs == 3 || ofs == 4 + NIDS_PER_BLOCK(sbi) ||
	    ofs == 5 + 2 * NIDS_PER_BLOCK(sbi))
		return false;
	if (ofs >= 6 + 2 * NIDS_PER_BLOCK(sbi)) {
		ofs -= 6 + 2 * NIDS_PER_BLOCK(sbi);
		if (!((long)ofs % (NIDS_PER_BLOCK(sbi) + 1)))
			return false;
	}
	return true;
}

static inline bool set_nid(struct f2fs_sb_info *sbi,
			struct f2fs_cached_block *entry, int off, nid_t nid, bool i)
{
	struct f2fs_node *rn = CACHED_NODE(entry);
	__le32 *inode_nids = F2FS_INODE_NIDS(sbi, entry);
	__le32 *addr = i ? &inode_nids[off - NODE_DIR1_BLOCK(sbi)] : &rn->in.nid[off];

	f2fs_cache_wait_writeback(entry);
	if (*addr == cpu_to_le32(nid))
		return false;

	*addr = cpu_to_le32(nid);
	f2fs_mark_cache_dirty(entry);
	return true;
}

static inline nid_t get_nid(struct f2fs_sb_info *sbi,
			const struct f2fs_cached_block *entry, int off, bool i)
{
	struct f2fs_node *rn = CACHED_NODE(entry);
	const __le32 *inode_nids = F2FS_INODE_NIDS(sbi, entry);
	int nid_index = off - NODE_DIR1_BLOCK(sbi);

	if (i)
		return le32_to_cpu(inode_nids[nid_index]);
	return le32_to_cpu(rn->in.nid[off]);
}

/*
 * Coldness identification:
 *  - Mark cold files in f2fs_inode_info
 *  - Mark cold node blocks in their node footer
 *  - Mark cold data pages in page cache
 */

static inline int is_node(struct f2fs_sb_info *sbi,
			const struct f2fs_cached_block *entry, int type)
{
	return le32_to_cpu(F2FS_NODE_FOOTER(sbi, entry)->flag) & BIT(type);
}

#define is_cold_node(sbi, entry)	is_node(sbi, entry, COLD_BIT_SHIFT)
#define is_fsync_dnode(sbi, entry)	is_node(sbi, entry, FSYNC_BIT_SHIFT)
#define is_dent_dnode(sbi, entry)	is_node(sbi, entry, DENT_BIT_SHIFT)

static inline void __set_mark(struct f2fs_sb_info *sbi,
			struct f2fs_cached_block *entry, bool mark, int type)
{
	struct node_footer *footer = F2FS_NODE_FOOTER(sbi, entry);
	unsigned int flag = le32_to_cpu(footer->flag);

	if (mark)
		flag |= BIT(type);
	else
		flag &= ~BIT(type);
	footer->flag = cpu_to_le32(flag);
}

static inline void set_cold_node(struct f2fs_sb_info *sbi,
			struct f2fs_cached_block *entry, bool is_dir)
{
	__set_mark(sbi, entry, !is_dir, COLD_BIT_SHIFT);
}

static inline void set_mark(struct f2fs_sb_info *sbi,
			struct f2fs_cached_block *entry, bool mark, int type)
{
	__set_mark(sbi, entry, mark, type);
#ifdef CONFIG_F2FS_CHECK_FS
	f2fs_inode_chksum_set(sbi, entry);
#endif
}

#define set_dentry_mark(sbi, entry, mark)	set_mark(sbi, entry, mark, DENT_BIT_SHIFT)
#define set_fsync_mark(sbi, entry, mark)	set_mark(sbi, entry, mark, FSYNC_BIT_SHIFT)
