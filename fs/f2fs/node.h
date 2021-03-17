/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fs/f2fs/node.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 */
/* start node id of a node block dedicated to the given node id */
#define	START_NID(nid) (((nid) / NAT_ENTRY_PER_BLOCK) * NAT_ENTRY_PER_BLOCK)

/* node block offset on the NAT area dedicated to the given start node id */
#define	NAT_BLOCK_OFFSET(start_nid) ((start_nid) / NAT_ENTRY_PER_BLOCK)

/* # of pages to perform synchronous readahead before building free nids */
#define FREE_NID_PAGES	8
#define MAX_FREE_NIDS	(NAT_ENTRY_PER_BLOCK * FREE_NID_PAGES)

/* size of free nid batch when shrinking */
#define SHRINK_NID_BATCH_SIZE	8

#define DEF_RA_NID_PAGES	0	/* # of nid pages to be readaheaded */

/* maximum readahead size for node during getting data blocks */
#define MAX_RA_NODE		128

/* control the memory footprint threshold (10MB per 1GB ram) */
#define DEF_RAM_THRESHOLD	1

/* control dirty nats ratio threshold (default: 10% over max nid count) */
#define DEF_DIRTY_NAT_RATIO_THRESHOLD		10
/* control total # of nats */
#define DEF_NAT_CACHE_THRESHOLD			100000

/* vector size for gang look-up from nat cache that consists of radix tree */
#define NATVEC_SIZE	64
#define SETVEC_SIZE	32

/* return value for read_node_page */
#define LOCKED_PAGE	1

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
	unsigned char mask = 0x01 << type;
	if (set)
		ne->ni.flag |= mask;
	else
		ne->ni.flag &= ~mask;
}

static inline bool get_nat_flag(struct nat_entry *ne, unsigned int type)
{
	unsigned char mask = 0x01 << type;
	return ne->ni.flag & mask;
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
	return NM_I(sbi)->nat_cnt[DIRTY_NAT] >= NM_I(sbi)->max_nid *
					NM_I(sbi)->dirty_nats_ratio / 100;
}

static inline bool excess_cached_nats(struct f2fs_sb_info *sbi)
{
	return NM_I(sbi)->nat_cnt[TOTAL_NAT] >= DEF_NAT_CACHE_THRESHOLD;
}

static inline bool excess_dirty_nodes(struct f2fs_sb_info *sbi)
{
	return get_pages(sbi, F2FS_DIRTY_NODES) >= sbi->blocks_per_seg * 8;
}

enum mem_type {
	FREE_NIDS,	/* indicates the free nid list */
	NAT_ENTRIES,	/* indicates the cached nat entry */
	DIRTY_DENTS,	/* indicates dirty dentry pages */
	INO_ENTRIES,	/* indicates inode entries */
	EXTENT_CACHE,	/* indicates extent cache */
	INMEM_PAGES,	/* indicates inmemory pages */
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
	block_off = NAT_BLOCK_OFFSET(start);

	block_addr = (pgoff_t)(nm_i->nat_blkaddr +
		(block_off << 1) -
		(block_off & (sbi->blocks_per_seg - 1)));

	if (f2fs_test_bit(block_off, nm_i->nat_bitmap))
		block_addr += sbi->blocks_per_seg;

	return block_addr;
}

static inline pgoff_t next_nat_addr(struct f2fs_sb_info *sbi,
						pgoff_t block_addr)
{
	struct f2fs_nm_info *nm_i = NM_I(sbi);

	block_addr -= nm_i->nat_blkaddr;
	block_addr ^= 1 << sbi->log_blocks_per_seg;
	return block_addr + nm_i->nat_blkaddr;
}

static inline void set_to_next_nat(struct f2fs_nm_info *nm_i, nid_t start_nid)
{
	unsigned int block_off = NAT_BLOCK_OFFSET(start_nid);

	f2fs_change_bit(block_off, nm_i->nat_bitmap);
#ifdef CONFIG_F2FS_CHECK_FS
	f2fs_change_bit(block_off, nm_i->nat_bitmap_mir);
#endif
}

static inline nid_t ino_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	return le32_to_cpu(rn->footer.ino);
}

static inline nid_t nid_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	return le32_to_cpu(rn->footer.nid);
}

static inline unsigned int ofs_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	unsigned flag = le32_to_cpu(rn->footer.flag);
	return flag >> OFFSET_BIT_SHIFT;
}

static inline __u64 cpver_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	return le64_to_cpu(rn->footer.cp_ver);
}

static inline block_t next_blkaddr_of_node(struct page *node_page)
{
	struct f2fs_node *rn = F2FS_NODE(node_page);
	return le32_to_cpu(rn->footer.next_blkaddr);
}

static inline void fill_node_footer(struct page *page, nid_t nid,
				nid_t ino, unsigned int ofs, bool reset)
{
	struct f2fs_node *rn = F2FS_NODE(page);
	unsigned int old_flag = 0;

	if (reset)
		memset(rn, 0, sizeof(*rn));
	else
		old_flag = le32_to_cpu(rn->footer.flag);

	rn->footer.nid = cpu_to_le32(nid);
	rn->footer.ino = cpu_to_le32(ino);

	/* should remain old flag bits such as COLD_BIT_SHIFT */
	rn->footer.flag = cpu_to_le32((ofs << OFFSET_BIT_SHIFT) |
					(old_flag & OFFSET_BIT_MASK));
}

static inline void copy_node_footer(struct page *dst, struct page *src)
{
	struct f2fs_node *src_rn = F2FS_NODE(src);
	struct f2fs_node *dst_rn = F2FS_NODE(dst);
	memcpy(&dst_rn->footer, &src_rn->footer, sizeof(struct node_footer));
}

static inline void fill_node_footer_blkaddr(struct page *page, block_t blkaddr)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(F2FS_P_SB(page));
	struct f2fs_node *rn = F2FS_NODE(page);
	__u64 cp_ver = cur_cp_version(ckpt);

	if (__is_set_ckpt_flags(ckpt, CP_CRC_RECOVERY_FLAG))
		cp_ver |= (cur_cp_crc(ckpt) << 32);

	rn->footer.cp_ver = cpu_to_le64(cp_ver);
	rn->footer.next_blkaddr = cpu_to_le32(blkaddr);
}

static inline bool is_recoverable_dnode(struct page *page)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(F2FS_P_SB(page));
	__u64 cp_ver = cur_cp_version(ckpt);

	/* Don't care crc part, if fsck.f2fs sets it. */
	if (__is_set_ckpt_flags(ckpt, CP_NOCRC_RECOVERY_FLAG))
		return (cp_ver << 32) == (cpver_of_node(page) << 32);

	if (__is_set_ckpt_flags(ckpt, CP_CRC_RECOVERY_FLAG))
		cp_ver |= (cur_cp_crc(ckpt) << 32);

	return cp_ver == cpver_of_node(page);
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
static inline bool IS_DNODE(struct page *node_page)
{
	unsigned int ofs = ofs_of_node(node_page);

	if (f2fs_has_xattr_block(ofs))
		return true;

	if (ofs == 3 || ofs == 4 + NIDS_PER_BLOCK ||
			ofs == 5 + 2 * NIDS_PER_BLOCK)
		return false;
	if (ofs >= 6 + 2 * NIDS_PER_BLOCK) {
		ofs -= 6 + 2 * NIDS_PER_BLOCK;
		if (!((long int)ofs % (NIDS_PER_BLOCK + 1)))
			return false;
	}
	return true;
}

static inline int set_nid(struct page *p, int off, nid_t nid, bool i)
{
	struct f2fs_node *rn = F2FS_NODE(p);

	f2fs_wait_on_page_writeback(p, NODE, true, true);

	if (i)
		rn->i.i_nid[off - NODE_DIR1_BLOCK] = cpu_to_le32(nid);
	else
		rn->in.nid[off] = cpu_to_le32(nid);
	return set_page_dirty(p);
}

static inline nid_t get_nid(struct page *p, int off, bool i)
{
	struct f2fs_node *rn = F2FS_NODE(p);

	if (i)
		return le32_to_cpu(rn->i.i_nid[off - NODE_DIR1_BLOCK]);
	return le32_to_cpu(rn->in.nid[off]);
}

/*
 * Coldness identification:
 *  - Mark cold files in f2fs_inode_info
 *  - Mark cold node blocks in their node footer
 *  - Mark cold data pages in page cache
 */
static inline int is_cold_data(struct page *page)
{
	return PageChecked(page);
}

static inline void set_cold_data(struct page *page)
{
	SetPageChecked(page);
}

static inline void clear_cold_data(struct page *page)
{
	ClearPageChecked(page);
}

static inline int is_node(struct page *page, int type)
{
	struct f2fs_node *rn = F2FS_NODE(page);
	return le32_to_cpu(rn->footer.flag) & (1 << type);
}

#define is_cold_node(page)	is_node(page, COLD_BIT_SHIFT)
#define is_fsync_dnode(page)	is_node(page, FSYNC_BIT_SHIFT)
#define is_dent_dnode(page)	is_node(page, DENT_BIT_SHIFT)

static inline int is_inline_node(struct page *page)
{
	return PageChecked(page);
}

static inline void set_inline_node(struct page *page)
{
	SetPageChecked(page);
}

static inline void clear_inline_node(struct page *page)
{
	ClearPageChecked(page);
}

static inline void set_cold_node(struct page *page, bool is_dir)
{
	struct f2fs_node *rn = F2FS_NODE(page);
	unsigned int flag = le32_to_cpu(rn->footer.flag);

	if (is_dir)
		flag &= ~(0x1 << COLD_BIT_SHIFT);
	else
		flag |= (0x1 << COLD_BIT_SHIFT);
	rn->footer.flag = cpu_to_le32(flag);
}

static inline void set_mark(struct page *page, int mark, int type)
{
	struct f2fs_node *rn = F2FS_NODE(page);
	unsigned int flag = le32_to_cpu(rn->footer.flag);
	if (mark)
		flag |= (0x1 << type);
	else
		flag &= ~(0x1 << type);
	rn->footer.flag = cpu_to_le32(flag);

#ifdef CONFIG_F2FS_CHECK_FS
	f2fs_inode_chksum_set(F2FS_P_SB(page), page);
#endif
}
#define set_dentry_mark(page, mark)	set_mark(page, mark, DENT_BIT_SHIFT)
#define set_fsync_mark(page, mark)	set_mark(page, mark, FSYNC_BIT_SHIFT)
