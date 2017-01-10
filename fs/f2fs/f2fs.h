/*
 * fs/f2fs/f2fs.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _LINUX_F2FS_H
#define _LINUX_F2FS_H

#include <linux/types.h>
#include <linux/page-flags.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/crc32.h>
#include <linux/magic.h>
#include <linux/kobject.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/fscrypto.h>
#include <crypto/hash.h>

#ifdef CONFIG_F2FS_CHECK_FS
#define f2fs_bug_on(sbi, condition)	BUG_ON(condition)
#else
#define f2fs_bug_on(sbi, condition)					\
	do {								\
		if (unlikely(condition)) {				\
			WARN_ON(1);					\
			set_sbi_flag(sbi, SBI_NEED_FSCK);		\
		}							\
	} while (0)
#endif

#ifdef CONFIG_F2FS_FAULT_INJECTION
enum {
	FAULT_KMALLOC,
	FAULT_PAGE_ALLOC,
	FAULT_ALLOC_NID,
	FAULT_ORPHAN,
	FAULT_BLOCK,
	FAULT_DIR_DEPTH,
	FAULT_EVICT_INODE,
	FAULT_IO,
	FAULT_CHECKPOINT,
	FAULT_MAX,
};

struct f2fs_fault_info {
	atomic_t inject_ops;
	unsigned int inject_rate;
	unsigned int inject_type;
};

extern char *fault_name[FAULT_MAX];
#define IS_FAULT_SET(fi, type) (fi->inject_type & (1 << (type)))
#endif

/*
 * For mount options
 */
#define F2FS_MOUNT_BG_GC		0x00000001
#define F2FS_MOUNT_DISABLE_ROLL_FORWARD	0x00000002
#define F2FS_MOUNT_DISCARD		0x00000004
#define F2FS_MOUNT_NOHEAP		0x00000008
#define F2FS_MOUNT_XATTR_USER		0x00000010
#define F2FS_MOUNT_POSIX_ACL		0x00000020
#define F2FS_MOUNT_DISABLE_EXT_IDENTIFY	0x00000040
#define F2FS_MOUNT_INLINE_XATTR		0x00000080
#define F2FS_MOUNT_INLINE_DATA		0x00000100
#define F2FS_MOUNT_INLINE_DENTRY	0x00000200
#define F2FS_MOUNT_FLUSH_MERGE		0x00000400
#define F2FS_MOUNT_NOBARRIER		0x00000800
#define F2FS_MOUNT_FASTBOOT		0x00001000
#define F2FS_MOUNT_EXTENT_CACHE		0x00002000
#define F2FS_MOUNT_FORCE_FG_GC		0x00004000
#define F2FS_MOUNT_DATA_FLUSH		0x00008000
#define F2FS_MOUNT_FAULT_INJECTION	0x00010000
#define F2FS_MOUNT_ADAPTIVE		0x00020000
#define F2FS_MOUNT_LFS			0x00040000

#define clear_opt(sbi, option)	(sbi->mount_opt.opt &= ~F2FS_MOUNT_##option)
#define set_opt(sbi, option)	(sbi->mount_opt.opt |= F2FS_MOUNT_##option)
#define test_opt(sbi, option)	(sbi->mount_opt.opt & F2FS_MOUNT_##option)

#define ver_after(a, b)	(typecheck(unsigned long long, a) &&		\
		typecheck(unsigned long long, b) &&			\
		((long long)((a) - (b)) > 0))

typedef u32 block_t;	/*
			 * should not change u32, since it is the on-disk block
			 * address format, __le32.
			 */
typedef u32 nid_t;

struct f2fs_mount_info {
	unsigned int	opt;
};

#define F2FS_FEATURE_ENCRYPT	0x0001
#define F2FS_FEATURE_BLKZONED	0x0002

#define F2FS_HAS_FEATURE(sb, mask)					\
	((F2FS_SB(sb)->raw_super->feature & cpu_to_le32(mask)) != 0)
#define F2FS_SET_FEATURE(sb, mask)					\
	F2FS_SB(sb)->raw_super->feature |= cpu_to_le32(mask)
#define F2FS_CLEAR_FEATURE(sb, mask)					\
	F2FS_SB(sb)->raw_super->feature &= ~cpu_to_le32(mask)

/*
 * For checkpoint manager
 */
enum {
	NAT_BITMAP,
	SIT_BITMAP
};

enum {
	CP_UMOUNT,
	CP_FASTBOOT,
	CP_SYNC,
	CP_RECOVERY,
	CP_DISCARD,
};

#define DEF_BATCHED_TRIM_SECTIONS	2
#define BATCHED_TRIM_SEGMENTS(sbi)	\
		(SM_I(sbi)->trim_sections * (sbi)->segs_per_sec)
#define BATCHED_TRIM_BLOCKS(sbi)	\
		(BATCHED_TRIM_SEGMENTS(sbi) << (sbi)->log_blocks_per_seg)

#define DISCARD_ISSUE_RATE	8
#define DEF_CP_INTERVAL			60	/* 60 secs */
#define DEF_IDLE_INTERVAL		5	/* 5 secs */

struct cp_control {
	int reason;
	__u64 trim_start;
	__u64 trim_end;
	__u64 trim_minlen;
	__u64 trimmed;
};

/*
 * For CP/NAT/SIT/SSA readahead
 */
enum {
	META_CP,
	META_NAT,
	META_SIT,
	META_SSA,
	META_POR,
};

/* for the list of ino */
enum {
	ORPHAN_INO,		/* for orphan ino list */
	APPEND_INO,		/* for append ino list */
	UPDATE_INO,		/* for update ino list */
	MAX_INO_ENTRY,		/* max. list */
};

struct ino_entry {
	struct list_head list;	/* list head */
	nid_t ino;		/* inode number */
};

/* for the list of inodes to be GCed */
struct inode_entry {
	struct list_head list;	/* list head */
	struct inode *inode;	/* vfs inode pointer */
};

/* for the list of blockaddresses to be discarded */
struct discard_entry {
	struct list_head list;	/* list head */
	block_t blkaddr;	/* block address to be discarded */
	int len;		/* # of consecutive blocks of the discard */
};

enum {
	D_PREP,
	D_SUBMIT,
	D_DONE,
};

struct discard_cmd {
	struct list_head list;		/* command list */
	struct completion wait;		/* compleation */
	block_t lstart;			/* logical start address */
	block_t len;			/* length */
	struct bio *bio;		/* bio */
	int state;			/* state */
};

struct discard_cmd_control {
	struct task_struct *f2fs_issue_discard;	/* discard thread */
	struct list_head discard_entry_list;	/* 4KB discard entry list */
	int nr_discards;			/* # of discards in the list */
	struct list_head discard_cmd_list;	/* discard cmd list */
	wait_queue_head_t discard_wait_queue;	/* waiting queue for wake-up */
	struct mutex cmd_lock;
	int max_discards;			/* max. discards to be issued */
};

/* for the list of fsync inodes, used only during recovery */
struct fsync_inode_entry {
	struct list_head list;	/* list head */
	struct inode *inode;	/* vfs inode pointer */
	block_t blkaddr;	/* block address locating the last fsync */
	block_t last_dentry;	/* block address locating the last dentry */
};

#define nats_in_cursum(jnl)		(le16_to_cpu(jnl->n_nats))
#define sits_in_cursum(jnl)		(le16_to_cpu(jnl->n_sits))

#define nat_in_journal(jnl, i)		(jnl->nat_j.entries[i].ne)
#define nid_in_journal(jnl, i)		(jnl->nat_j.entries[i].nid)
#define sit_in_journal(jnl, i)		(jnl->sit_j.entries[i].se)
#define segno_in_journal(jnl, i)	(jnl->sit_j.entries[i].segno)

#define MAX_NAT_JENTRIES(jnl)	(NAT_JOURNAL_ENTRIES - nats_in_cursum(jnl))
#define MAX_SIT_JENTRIES(jnl)	(SIT_JOURNAL_ENTRIES - sits_in_cursum(jnl))

static inline int update_nats_in_cursum(struct f2fs_journal *journal, int i)
{
	int before = nats_in_cursum(journal);
	journal->n_nats = cpu_to_le16(before + i);
	return before;
}

static inline int update_sits_in_cursum(struct f2fs_journal *journal, int i)
{
	int before = sits_in_cursum(journal);
	journal->n_sits = cpu_to_le16(before + i);
	return before;
}

static inline bool __has_cursum_space(struct f2fs_journal *journal,
							int size, int type)
{
	if (type == NAT_JOURNAL)
		return size <= MAX_NAT_JENTRIES(journal);
	return size <= MAX_SIT_JENTRIES(journal);
}

/*
 * ioctl commands
 */
#define F2FS_IOC_GETFLAGS		FS_IOC_GETFLAGS
#define F2FS_IOC_SETFLAGS		FS_IOC_SETFLAGS
#define F2FS_IOC_GETVERSION		FS_IOC_GETVERSION

#define F2FS_IOCTL_MAGIC		0xf5
#define F2FS_IOC_START_ATOMIC_WRITE	_IO(F2FS_IOCTL_MAGIC, 1)
#define F2FS_IOC_COMMIT_ATOMIC_WRITE	_IO(F2FS_IOCTL_MAGIC, 2)
#define F2FS_IOC_START_VOLATILE_WRITE	_IO(F2FS_IOCTL_MAGIC, 3)
#define F2FS_IOC_RELEASE_VOLATILE_WRITE	_IO(F2FS_IOCTL_MAGIC, 4)
#define F2FS_IOC_ABORT_VOLATILE_WRITE	_IO(F2FS_IOCTL_MAGIC, 5)
#define F2FS_IOC_GARBAGE_COLLECT	_IO(F2FS_IOCTL_MAGIC, 6)
#define F2FS_IOC_WRITE_CHECKPOINT	_IO(F2FS_IOCTL_MAGIC, 7)
#define F2FS_IOC_DEFRAGMENT		_IO(F2FS_IOCTL_MAGIC, 8)
#define F2FS_IOC_MOVE_RANGE		_IOWR(F2FS_IOCTL_MAGIC, 9,	\
						struct f2fs_move_range)

#define F2FS_IOC_SET_ENCRYPTION_POLICY	FS_IOC_SET_ENCRYPTION_POLICY
#define F2FS_IOC_GET_ENCRYPTION_POLICY	FS_IOC_GET_ENCRYPTION_POLICY
#define F2FS_IOC_GET_ENCRYPTION_PWSALT	FS_IOC_GET_ENCRYPTION_PWSALT

/*
 * should be same as XFS_IOC_GOINGDOWN.
 * Flags for going down operation used by FS_IOC_GOINGDOWN
 */
#define F2FS_IOC_SHUTDOWN	_IOR('X', 125, __u32)	/* Shutdown */
#define F2FS_GOING_DOWN_FULLSYNC	0x0	/* going down with full sync */
#define F2FS_GOING_DOWN_METASYNC	0x1	/* going down with metadata */
#define F2FS_GOING_DOWN_NOSYNC		0x2	/* going down */
#define F2FS_GOING_DOWN_METAFLUSH	0x3	/* going down with meta flush */

#if defined(__KERNEL__) && defined(CONFIG_COMPAT)
/*
 * ioctl commands in 32 bit emulation
 */
#define F2FS_IOC32_GETFLAGS		FS_IOC32_GETFLAGS
#define F2FS_IOC32_SETFLAGS		FS_IOC32_SETFLAGS
#define F2FS_IOC32_GETVERSION		FS_IOC32_GETVERSION
#endif

struct f2fs_defragment {
	u64 start;
	u64 len;
};

struct f2fs_move_range {
	u32 dst_fd;		/* destination fd */
	u64 pos_in;		/* start position in src_fd */
	u64 pos_out;		/* start position in dst_fd */
	u64 len;		/* size to move */
};

/*
 * For INODE and NODE manager
 */
/* for directory operations */
struct f2fs_dentry_ptr {
	struct inode *inode;
	const void *bitmap;
	struct f2fs_dir_entry *dentry;
	__u8 (*filename)[F2FS_SLOT_LEN];
	int max;
};

static inline void make_dentry_ptr(struct inode *inode,
		struct f2fs_dentry_ptr *d, void *src, int type)
{
	d->inode = inode;

	if (type == 1) {
		struct f2fs_dentry_block *t = (struct f2fs_dentry_block *)src;
		d->max = NR_DENTRY_IN_BLOCK;
		d->bitmap = &t->dentry_bitmap;
		d->dentry = t->dentry;
		d->filename = t->filename;
	} else {
		struct f2fs_inline_dentry *t = (struct f2fs_inline_dentry *)src;
		d->max = NR_INLINE_DENTRY;
		d->bitmap = &t->dentry_bitmap;
		d->dentry = t->dentry;
		d->filename = t->filename;
	}
}

/*
 * XATTR_NODE_OFFSET stores xattrs to one node block per file keeping -1
 * as its node offset to distinguish from index node blocks.
 * But some bits are used to mark the node block.
 */
#define XATTR_NODE_OFFSET	((((unsigned int)-1) << OFFSET_BIT_SHIFT) \
				>> OFFSET_BIT_SHIFT)
enum {
	ALLOC_NODE,			/* allocate a new node page if needed */
	LOOKUP_NODE,			/* look up a node without readahead */
	LOOKUP_NODE_RA,			/*
					 * look up a node with readahead called
					 * by get_data_block.
					 */
};

#define F2FS_LINK_MAX	0xffffffff	/* maximum link count per file */

#define MAX_DIR_RA_PAGES	4	/* maximum ra pages of dir */

/* vector size for gang look-up from extent cache that consists of radix tree */
#define EXT_TREE_VEC_SIZE	64

/* for in-memory extent cache entry */
#define F2FS_MIN_EXTENT_LEN	64	/* minimum extent length */

/* number of extent info in extent cache we try to shrink */
#define EXTENT_CACHE_SHRINK_NUMBER	128

struct extent_info {
	unsigned int fofs;		/* start offset in a file */
	u32 blk;			/* start block address of the extent */
	unsigned int len;		/* length of the extent */
};

struct extent_node {
	struct rb_node rb_node;		/* rb node located in rb-tree */
	struct list_head list;		/* node in global extent list of sbi */
	struct extent_info ei;		/* extent info */
	struct extent_tree *et;		/* extent tree pointer */
};

struct extent_tree {
	nid_t ino;			/* inode number */
	struct rb_root root;		/* root of extent info rb-tree */
	struct extent_node *cached_en;	/* recently accessed extent node */
	struct extent_info largest;	/* largested extent info */
	struct list_head list;		/* to be used by sbi->zombie_list */
	rwlock_t lock;			/* protect extent info rb-tree */
	atomic_t node_cnt;		/* # of extent node in rb-tree*/
};

/*
 * This structure is taken from ext4_map_blocks.
 *
 * Note that, however, f2fs uses NEW and MAPPED flags for f2fs_map_blocks().
 */
#define F2FS_MAP_NEW		(1 << BH_New)
#define F2FS_MAP_MAPPED		(1 << BH_Mapped)
#define F2FS_MAP_UNWRITTEN	(1 << BH_Unwritten)
#define F2FS_MAP_FLAGS		(F2FS_MAP_NEW | F2FS_MAP_MAPPED |\
				F2FS_MAP_UNWRITTEN)

struct f2fs_map_blocks {
	block_t m_pblk;
	block_t m_lblk;
	unsigned int m_len;
	unsigned int m_flags;
	pgoff_t *m_next_pgofs;		/* point next possible non-hole pgofs */
};

/* for flag in get_data_block */
#define F2FS_GET_BLOCK_READ		0
#define F2FS_GET_BLOCK_DIO		1
#define F2FS_GET_BLOCK_FIEMAP		2
#define F2FS_GET_BLOCK_BMAP		3
#define F2FS_GET_BLOCK_PRE_DIO		4
#define F2FS_GET_BLOCK_PRE_AIO		5

/*
 * i_advise uses FADVISE_XXX_BIT. We can add additional hints later.
 */
#define FADVISE_COLD_BIT	0x01
#define FADVISE_LOST_PINO_BIT	0x02
#define FADVISE_ENCRYPT_BIT	0x04
#define FADVISE_ENC_NAME_BIT	0x08
#define FADVISE_KEEP_SIZE_BIT	0x10

#define file_is_cold(inode)	is_file(inode, FADVISE_COLD_BIT)
#define file_wrong_pino(inode)	is_file(inode, FADVISE_LOST_PINO_BIT)
#define file_set_cold(inode)	set_file(inode, FADVISE_COLD_BIT)
#define file_lost_pino(inode)	set_file(inode, FADVISE_LOST_PINO_BIT)
#define file_clear_cold(inode)	clear_file(inode, FADVISE_COLD_BIT)
#define file_got_pino(inode)	clear_file(inode, FADVISE_LOST_PINO_BIT)
#define file_is_encrypt(inode)	is_file(inode, FADVISE_ENCRYPT_BIT)
#define file_set_encrypt(inode)	set_file(inode, FADVISE_ENCRYPT_BIT)
#define file_clear_encrypt(inode) clear_file(inode, FADVISE_ENCRYPT_BIT)
#define file_enc_name(inode)	is_file(inode, FADVISE_ENC_NAME_BIT)
#define file_set_enc_name(inode) set_file(inode, FADVISE_ENC_NAME_BIT)
#define file_keep_isize(inode)	is_file(inode, FADVISE_KEEP_SIZE_BIT)
#define file_set_keep_isize(inode) set_file(inode, FADVISE_KEEP_SIZE_BIT)

#define DEF_DIR_LEVEL		0

struct f2fs_inode_info {
	struct inode vfs_inode;		/* serve a vfs inode */
	unsigned long i_flags;		/* keep an inode flags for ioctl */
	unsigned char i_advise;		/* use to give file attribute hints */
	unsigned char i_dir_level;	/* use for dentry level for large dir */
	unsigned int i_current_depth;	/* use only in directory structure */
	unsigned int i_pino;		/* parent inode number */
	umode_t i_acl_mode;		/* keep file acl mode temporarily */

	/* Use below internally in f2fs*/
	unsigned long flags;		/* use to pass per-file flags */
	struct rw_semaphore i_sem;	/* protect fi info */
	atomic_t dirty_pages;		/* # of dirty pages */
	f2fs_hash_t chash;		/* hash value of given file name */
	unsigned int clevel;		/* maximum level of given file name */
	nid_t i_xattr_nid;		/* node id that contains xattrs */
	unsigned long long xattr_ver;	/* cp version of xattr modification */
	loff_t	last_disk_size;		/* lastly written file size */

	struct list_head dirty_list;	/* dirty list for dirs and files */
	struct list_head gdirty_list;	/* linked in global dirty list */
	struct list_head inmem_pages;	/* inmemory pages managed by f2fs */
	struct mutex inmem_lock;	/* lock for inmemory pages */
	struct extent_tree *extent_tree;	/* cached extent_tree entry */
	struct rw_semaphore dio_rwsem[2];/* avoid racing between dio and gc */
};

static inline void get_extent_info(struct extent_info *ext,
					struct f2fs_extent *i_ext)
{
	ext->fofs = le32_to_cpu(i_ext->fofs);
	ext->blk = le32_to_cpu(i_ext->blk);
	ext->len = le32_to_cpu(i_ext->len);
}

static inline void set_raw_extent(struct extent_info *ext,
					struct f2fs_extent *i_ext)
{
	i_ext->fofs = cpu_to_le32(ext->fofs);
	i_ext->blk = cpu_to_le32(ext->blk);
	i_ext->len = cpu_to_le32(ext->len);
}

static inline void set_extent_info(struct extent_info *ei, unsigned int fofs,
						u32 blk, unsigned int len)
{
	ei->fofs = fofs;
	ei->blk = blk;
	ei->len = len;
}

static inline bool __is_extent_same(struct extent_info *ei1,
						struct extent_info *ei2)
{
	return (ei1->fofs == ei2->fofs && ei1->blk == ei2->blk &&
						ei1->len == ei2->len);
}

static inline bool __is_extent_mergeable(struct extent_info *back,
						struct extent_info *front)
{
	return (back->fofs + back->len == front->fofs &&
			back->blk + back->len == front->blk);
}

static inline bool __is_back_mergeable(struct extent_info *cur,
						struct extent_info *back)
{
	return __is_extent_mergeable(back, cur);
}

static inline bool __is_front_mergeable(struct extent_info *cur,
						struct extent_info *front)
{
	return __is_extent_mergeable(cur, front);
}

extern void f2fs_mark_inode_dirty_sync(struct inode *, bool);
static inline void __try_update_largest_extent(struct inode *inode,
			struct extent_tree *et, struct extent_node *en)
{
	if (en->ei.len > et->largest.len) {
		et->largest = en->ei;
		f2fs_mark_inode_dirty_sync(inode, true);
	}
}

enum nid_list {
	FREE_NID_LIST,
	ALLOC_NID_LIST,
	MAX_NID_LIST,
};

struct f2fs_nm_info {
	block_t nat_blkaddr;		/* base disk address of NAT */
	nid_t max_nid;			/* maximum possible node ids */
	nid_t available_nids;		/* # of available node ids */
	nid_t next_scan_nid;		/* the next nid to be scanned */
	unsigned int ram_thresh;	/* control the memory footprint */
	unsigned int ra_nid_pages;	/* # of nid pages to be readaheaded */
	unsigned int dirty_nats_ratio;	/* control dirty nats ratio threshold */

	/* NAT cache management */
	struct radix_tree_root nat_root;/* root of the nat entry cache */
	struct radix_tree_root nat_set_root;/* root of the nat set cache */
	struct rw_semaphore nat_tree_lock;	/* protect nat_tree_lock */
	struct list_head nat_entries;	/* cached nat entry list (clean) */
	unsigned int nat_cnt;		/* the # of cached nat entries */
	unsigned int dirty_nat_cnt;	/* total num of nat entries in set */

	/* free node ids management */
	struct radix_tree_root free_nid_root;/* root of the free_nid cache */
	struct list_head nid_list[MAX_NID_LIST];/* lists for free nids */
	unsigned int nid_cnt[MAX_NID_LIST];	/* the number of free node id */
	spinlock_t nid_list_lock;	/* protect nid lists ops */
	struct mutex build_lock;	/* lock for build free nids */

	/* for checkpoint */
	char *nat_bitmap;		/* NAT bitmap pointer */
#ifdef CONFIG_F2FS_CHECK_FS
	char *nat_bitmap_mir;		/* NAT bitmap mirror */
#endif
	int bitmap_size;		/* bitmap size */
};

/*
 * this structure is used as one of function parameters.
 * all the information are dedicated to a given direct node block determined
 * by the data offset in a file.
 */
struct dnode_of_data {
	struct inode *inode;		/* vfs inode pointer */
	struct page *inode_page;	/* its inode page, NULL is possible */
	struct page *node_page;		/* cached direct node page */
	nid_t nid;			/* node id of the direct node block */
	unsigned int ofs_in_node;	/* data offset in the node page */
	bool inode_page_locked;		/* inode page is locked or not */
	bool node_changed;		/* is node block changed */
	char cur_level;			/* level of hole node page */
	char max_level;			/* level of current page located */
	block_t	data_blkaddr;		/* block address of the node block */
};

static inline void set_new_dnode(struct dnode_of_data *dn, struct inode *inode,
		struct page *ipage, struct page *npage, nid_t nid)
{
	memset(dn, 0, sizeof(*dn));
	dn->inode = inode;
	dn->inode_page = ipage;
	dn->node_page = npage;
	dn->nid = nid;
}

/*
 * For SIT manager
 *
 * By default, there are 6 active log areas across the whole main area.
 * When considering hot and cold data separation to reduce cleaning overhead,
 * we split 3 for data logs and 3 for node logs as hot, warm, and cold types,
 * respectively.
 * In the current design, you should not change the numbers intentionally.
 * Instead, as a mount option such as active_logs=x, you can use 2, 4, and 6
 * logs individually according to the underlying devices. (default: 6)
 * Just in case, on-disk layout covers maximum 16 logs that consist of 8 for
 * data and 8 for node logs.
 */
#define	NR_CURSEG_DATA_TYPE	(3)
#define NR_CURSEG_NODE_TYPE	(3)
#define NR_CURSEG_TYPE	(NR_CURSEG_DATA_TYPE + NR_CURSEG_NODE_TYPE)

enum {
	CURSEG_HOT_DATA	= 0,	/* directory entry blocks */
	CURSEG_WARM_DATA,	/* data blocks */
	CURSEG_COLD_DATA,	/* multimedia or GCed data blocks */
	CURSEG_HOT_NODE,	/* direct node blocks of directory files */
	CURSEG_WARM_NODE,	/* direct node blocks of normal files */
	CURSEG_COLD_NODE,	/* indirect node blocks */
	NO_CHECK_TYPE,
};

struct flush_cmd {
	struct completion wait;
	struct llist_node llnode;
	int ret;
};

struct flush_cmd_control {
	struct task_struct *f2fs_issue_flush;	/* flush thread */
	wait_queue_head_t flush_wait_queue;	/* waiting queue for wake-up */
	atomic_t submit_flush;			/* # of issued flushes */
	struct llist_head issue_list;		/* list for command issue */
	struct llist_node *dispatch_list;	/* list for command dispatch */
};

struct f2fs_sm_info {
	struct sit_info *sit_info;		/* whole segment information */
	struct free_segmap_info *free_info;	/* free segment information */
	struct dirty_seglist_info *dirty_info;	/* dirty segment information */
	struct curseg_info *curseg_array;	/* active segment information */

	block_t seg0_blkaddr;		/* block address of 0'th segment */
	block_t main_blkaddr;		/* start block address of main area */
	block_t ssa_blkaddr;		/* start block address of SSA area */

	unsigned int segment_count;	/* total # of segments */
	unsigned int main_segments;	/* # of segments in main area */
	unsigned int reserved_segments;	/* # of reserved segments */
	unsigned int ovp_segments;	/* # of overprovision segments */

	/* a threshold to reclaim prefree segments */
	unsigned int rec_prefree_segments;

	/* for batched trimming */
	unsigned int trim_sections;		/* # of sections to trim */

	struct list_head sit_entry_set;	/* sit entry set list */

	unsigned int ipu_policy;	/* in-place-update policy */
	unsigned int min_ipu_util;	/* in-place-update threshold */
	unsigned int min_fsync_blocks;	/* threshold for fsync */

	/* for flush command control */
	struct flush_cmd_control *fcc_info;

	/* for discard command control */
	struct discard_cmd_control *dcc_info;
};

/*
 * For superblock
 */
/*
 * COUNT_TYPE for monitoring
 *
 * f2fs monitors the number of several block types such as on-writeback,
 * dirty dentry blocks, dirty node blocks, and dirty meta blocks.
 */
#define WB_DATA_TYPE(p)	(__is_cp_guaranteed(p) ? F2FS_WB_CP_DATA : F2FS_WB_DATA)
enum count_type {
	F2FS_DIRTY_DENTS,
	F2FS_DIRTY_DATA,
	F2FS_DIRTY_NODES,
	F2FS_DIRTY_META,
	F2FS_INMEM_PAGES,
	F2FS_DIRTY_IMETA,
	F2FS_WB_CP_DATA,
	F2FS_WB_DATA,
	NR_COUNT_TYPE,
};

/*
 * The below are the page types of bios used in submit_bio().
 * The available types are:
 * DATA			User data pages. It operates as async mode.
 * NODE			Node pages. It operates as async mode.
 * META			FS metadata pages such as SIT, NAT, CP.
 * NR_PAGE_TYPE		The number of page types.
 * META_FLUSH		Make sure the previous pages are written
 *			with waiting the bio's completion
 * ...			Only can be used with META.
 */
#define PAGE_TYPE_OF_BIO(type)	((type) > META ? META : (type))
enum page_type {
	DATA,
	NODE,
	META,
	NR_PAGE_TYPE,
	META_FLUSH,
	INMEM,		/* the below types are used by tracepoints only. */
	INMEM_DROP,
	INMEM_REVOKE,
	IPU,
	OPU,
};

struct f2fs_io_info {
	struct f2fs_sb_info *sbi;	/* f2fs_sb_info pointer */
	enum page_type type;	/* contains DATA/NODE/META/META_FLUSH */
	int op;			/* contains REQ_OP_ */
	int op_flags;		/* req_flag_bits */
	block_t new_blkaddr;	/* new block address to be written */
	block_t old_blkaddr;	/* old block address before Cow */
	struct page *page;	/* page to be written */
	struct page *encrypted_page;	/* encrypted page */
};

#define is_read_io(rw) (rw == READ)
struct f2fs_bio_info {
	struct f2fs_sb_info *sbi;	/* f2fs superblock */
	struct bio *bio;		/* bios to merge */
	sector_t last_block_in_bio;	/* last block number */
	struct f2fs_io_info fio;	/* store buffered io info. */
	struct rw_semaphore io_rwsem;	/* blocking op for bio */
};

#define FDEV(i)				(sbi->devs[i])
#define RDEV(i)				(raw_super->devs[i])
struct f2fs_dev_info {
	struct block_device *bdev;
	char path[MAX_PATH_LEN];
	unsigned int total_segments;
	block_t start_blk;
	block_t end_blk;
#ifdef CONFIG_BLK_DEV_ZONED
	unsigned int nr_blkz;			/* Total number of zones */
	u8 *blkz_type;				/* Array of zones type */
#endif
};

enum inode_type {
	DIR_INODE,			/* for dirty dir inode */
	FILE_INODE,			/* for dirty regular/symlink inode */
	DIRTY_META,			/* for all dirtied inode metadata */
	NR_INODE_TYPE,
};

/* for inner inode cache management */
struct inode_management {
	struct radix_tree_root ino_root;	/* ino entry array */
	spinlock_t ino_lock;			/* for ino entry lock */
	struct list_head ino_list;		/* inode list head */
	unsigned long ino_num;			/* number of entries */
};

/* For s_flag in struct f2fs_sb_info */
enum {
	SBI_IS_DIRTY,				/* dirty flag for checkpoint */
	SBI_IS_CLOSE,				/* specify unmounting */
	SBI_NEED_FSCK,				/* need fsck.f2fs to fix */
	SBI_POR_DOING,				/* recovery is doing or not */
	SBI_NEED_SB_WRITE,			/* need to recover superblock */
	SBI_NEED_CP,				/* need to checkpoint */
};

enum {
	CP_TIME,
	REQ_TIME,
	MAX_TIME,
};

#ifdef CONFIG_F2FS_FS_ENCRYPTION
#define F2FS_KEY_DESC_PREFIX "f2fs:"
#define F2FS_KEY_DESC_PREFIX_SIZE 5
#endif
struct f2fs_sb_info {
	struct super_block *sb;			/* pointer to VFS super block */
	struct proc_dir_entry *s_proc;		/* proc entry */
	struct f2fs_super_block *raw_super;	/* raw super block pointer */
	int valid_super_block;			/* valid super block no */
	unsigned long s_flag;				/* flags for sbi */

#ifdef CONFIG_F2FS_FS_ENCRYPTION
	u8 key_prefix[F2FS_KEY_DESC_PREFIX_SIZE];
	u8 key_prefix_size;
#endif

#ifdef CONFIG_BLK_DEV_ZONED
	unsigned int blocks_per_blkz;		/* F2FS blocks per zone */
	unsigned int log_blocks_per_blkz;	/* log2 F2FS blocks per zone */
#endif

	/* for node-related operations */
	struct f2fs_nm_info *nm_info;		/* node manager */
	struct inode *node_inode;		/* cache node blocks */

	/* for segment-related operations */
	struct f2fs_sm_info *sm_info;		/* segment manager */

	/* for bio operations */
	struct f2fs_bio_info read_io;			/* for read bios */
	struct f2fs_bio_info write_io[NR_PAGE_TYPE];	/* for write bios */
	struct mutex wio_mutex[NODE + 1];	/* bio ordering for NODE/DATA */
	int write_io_size_bits;			/* Write IO size bits */
	mempool_t *write_io_dummy;		/* Dummy pages */

	/* for checkpoint */
	struct f2fs_checkpoint *ckpt;		/* raw checkpoint pointer */
	int cur_cp_pack;			/* remain current cp pack */
	spinlock_t cp_lock;			/* for flag in ckpt */
	struct inode *meta_inode;		/* cache meta blocks */
	struct mutex cp_mutex;			/* checkpoint procedure lock */
	struct rw_semaphore cp_rwsem;		/* blocking FS operations */
	struct rw_semaphore node_write;		/* locking node writes */
	wait_queue_head_t cp_wait;
	unsigned long last_time[MAX_TIME];	/* to store time in jiffies */
	long interval_time[MAX_TIME];		/* to store thresholds */

	struct inode_management im[MAX_INO_ENTRY];      /* manage inode cache */

	/* for orphan inode, use 0'th array */
	unsigned int max_orphans;		/* max orphan inodes */

	/* for inode management */
	struct list_head inode_list[NR_INODE_TYPE];	/* dirty inode list */
	spinlock_t inode_lock[NR_INODE_TYPE];	/* for dirty inode list lock */

	/* for extent tree cache */
	struct radix_tree_root extent_tree_root;/* cache extent cache entries */
	struct rw_semaphore extent_tree_lock;	/* locking extent radix tree */
	struct list_head extent_list;		/* lru list for shrinker */
	spinlock_t extent_lock;			/* locking extent lru list */
	atomic_t total_ext_tree;		/* extent tree count */
	struct list_head zombie_list;		/* extent zombie tree list */
	atomic_t total_zombie_tree;		/* extent zombie tree count */
	atomic_t total_ext_node;		/* extent info count */

	/* basic filesystem units */
	unsigned int log_sectors_per_block;	/* log2 sectors per block */
	unsigned int log_blocksize;		/* log2 block size */
	unsigned int blocksize;			/* block size */
	unsigned int root_ino_num;		/* root inode number*/
	unsigned int node_ino_num;		/* node inode number*/
	unsigned int meta_ino_num;		/* meta inode number*/
	unsigned int log_blocks_per_seg;	/* log2 blocks per segment */
	unsigned int blocks_per_seg;		/* blocks per segment */
	unsigned int segs_per_sec;		/* segments per section */
	unsigned int secs_per_zone;		/* sections per zone */
	unsigned int total_sections;		/* total section count */
	unsigned int total_node_count;		/* total node block count */
	unsigned int total_valid_node_count;	/* valid node block count */
	loff_t max_file_blocks;			/* max block index of file */
	int active_logs;			/* # of active logs */
	int dir_level;				/* directory level */

	block_t user_block_count;		/* # of user blocks */
	block_t total_valid_block_count;	/* # of valid blocks */
	block_t discard_blks;			/* discard command candidats */
	block_t last_valid_block_count;		/* for recovery */
	u32 s_next_generation;			/* for NFS support */

	/* # of pages, see count_type */
	atomic_t nr_pages[NR_COUNT_TYPE];
	/* # of allocated blocks */
	struct percpu_counter alloc_valid_block_count;

	/* valid inode count */
	struct percpu_counter total_valid_inode_count;

	struct f2fs_mount_info mount_opt;	/* mount options */

	/* for cleaning operations */
	struct mutex gc_mutex;			/* mutex for GC */
	struct f2fs_gc_kthread	*gc_thread;	/* GC thread */
	unsigned int cur_victim_sec;		/* current victim section num */

	/* maximum # of trials to find a victim segment for SSR and GC */
	unsigned int max_victim_search;

	/*
	 * for stat information.
	 * one is for the LFS mode, and the other is for the SSR mode.
	 */
#ifdef CONFIG_F2FS_STAT_FS
	struct f2fs_stat_info *stat_info;	/* FS status information */
	unsigned int segment_count[2];		/* # of allocated segments */
	unsigned int block_count[2];		/* # of allocated blocks */
	atomic_t inplace_count;		/* # of inplace update */
	atomic64_t total_hit_ext;		/* # of lookup extent cache */
	atomic64_t read_hit_rbtree;		/* # of hit rbtree extent node */
	atomic64_t read_hit_largest;		/* # of hit largest extent node */
	atomic64_t read_hit_cached;		/* # of hit cached extent node */
	atomic_t inline_xattr;			/* # of inline_xattr inodes */
	atomic_t inline_inode;			/* # of inline_data inodes */
	atomic_t inline_dir;			/* # of inline_dentry inodes */
	atomic_t aw_cnt;			/* # of atomic writes */
	atomic_t max_aw_cnt;			/* max # of atomic writes */
	int bg_gc;				/* background gc calls */
	unsigned int ndirty_inode[NR_INODE_TYPE];	/* # of dirty inodes */
#endif
	unsigned int last_victim[2];		/* last victim segment # */
	spinlock_t stat_lock;			/* lock for stat operations */

	/* For sysfs suppport */
	struct kobject s_kobj;
	struct completion s_kobj_unregister;

	/* For shrinker support */
	struct list_head s_list;
	int s_ndevs;				/* number of devices */
	struct f2fs_dev_info *devs;		/* for device list */
	struct mutex umount_mutex;
	unsigned int shrinker_run_no;

	/* For write statistics */
	u64 sectors_written_start;
	u64 kbytes_written;

	/* Reference to checksum algorithm driver via cryptoapi */
	struct crypto_shash *s_chksum_driver;

	/* For fault injection */
#ifdef CONFIG_F2FS_FAULT_INJECTION
	struct f2fs_fault_info fault_info;
#endif
};

#ifdef CONFIG_F2FS_FAULT_INJECTION
static inline bool time_to_inject(struct f2fs_sb_info *sbi, int type)
{
	struct f2fs_fault_info *ffi = &sbi->fault_info;

	if (!ffi->inject_rate)
		return false;

	if (!IS_FAULT_SET(ffi, type))
		return false;

	atomic_inc(&ffi->inject_ops);
	if (atomic_read(&ffi->inject_ops) >= ffi->inject_rate) {
		atomic_set(&ffi->inject_ops, 0);
		printk("%sF2FS-fs : inject %s in %pF\n",
				KERN_INFO,
				fault_name[type],
				__builtin_return_address(0));
		return true;
	}
	return false;
}
#endif

/* For write statistics. Suppose sector size is 512 bytes,
 * and the return value is in kbytes. s is of struct f2fs_sb_info.
 */
#define BD_PART_WRITTEN(s)						 \
(((u64)part_stat_read(s->sb->s_bdev->bd_part, sectors[1]) -		 \
		s->sectors_written_start) >> 1)

static inline void f2fs_update_time(struct f2fs_sb_info *sbi, int type)
{
	sbi->last_time[type] = jiffies;
}

static inline bool f2fs_time_over(struct f2fs_sb_info *sbi, int type)
{
	struct timespec ts = {sbi->interval_time[type], 0};
	unsigned long interval = timespec_to_jiffies(&ts);

	return time_after(jiffies, sbi->last_time[type] + interval);
}

static inline bool is_idle(struct f2fs_sb_info *sbi)
{
	struct block_device *bdev = sbi->sb->s_bdev;
	struct request_queue *q = bdev_get_queue(bdev);
	struct request_list *rl = &q->root_rl;

	if (rl->count[BLK_RW_SYNC] || rl->count[BLK_RW_ASYNC])
		return 0;

	return f2fs_time_over(sbi, REQ_TIME);
}

/*
 * Inline functions
 */
static inline u32 f2fs_crc32(struct f2fs_sb_info *sbi, const void *address,
			   unsigned int length)
{
	SHASH_DESC_ON_STACK(shash, sbi->s_chksum_driver);
	u32 *ctx = (u32 *)shash_desc_ctx(shash);
	int err;

	shash->tfm = sbi->s_chksum_driver;
	shash->flags = 0;
	*ctx = F2FS_SUPER_MAGIC;

	err = crypto_shash_update(shash, address, length);
	BUG_ON(err);

	return *ctx;
}

static inline bool f2fs_crc_valid(struct f2fs_sb_info *sbi, __u32 blk_crc,
				  void *buf, size_t buf_size)
{
	return f2fs_crc32(sbi, buf, buf_size) == blk_crc;
}

static inline struct f2fs_inode_info *F2FS_I(struct inode *inode)
{
	return container_of(inode, struct f2fs_inode_info, vfs_inode);
}

static inline struct f2fs_sb_info *F2FS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct f2fs_sb_info *F2FS_I_SB(struct inode *inode)
{
	return F2FS_SB(inode->i_sb);
}

static inline struct f2fs_sb_info *F2FS_M_SB(struct address_space *mapping)
{
	return F2FS_I_SB(mapping->host);
}

static inline struct f2fs_sb_info *F2FS_P_SB(struct page *page)
{
	return F2FS_M_SB(page->mapping);
}

static inline struct f2fs_super_block *F2FS_RAW_SUPER(struct f2fs_sb_info *sbi)
{
	return (struct f2fs_super_block *)(sbi->raw_super);
}

static inline struct f2fs_checkpoint *F2FS_CKPT(struct f2fs_sb_info *sbi)
{
	return (struct f2fs_checkpoint *)(sbi->ckpt);
}

static inline struct f2fs_node *F2FS_NODE(struct page *page)
{
	return (struct f2fs_node *)page_address(page);
}

static inline struct f2fs_inode *F2FS_INODE(struct page *page)
{
	return &((struct f2fs_node *)page_address(page))->i;
}

static inline struct f2fs_nm_info *NM_I(struct f2fs_sb_info *sbi)
{
	return (struct f2fs_nm_info *)(sbi->nm_info);
}

static inline struct f2fs_sm_info *SM_I(struct f2fs_sb_info *sbi)
{
	return (struct f2fs_sm_info *)(sbi->sm_info);
}

static inline struct sit_info *SIT_I(struct f2fs_sb_info *sbi)
{
	return (struct sit_info *)(SM_I(sbi)->sit_info);
}

static inline struct free_segmap_info *FREE_I(struct f2fs_sb_info *sbi)
{
	return (struct free_segmap_info *)(SM_I(sbi)->free_info);
}

static inline struct dirty_seglist_info *DIRTY_I(struct f2fs_sb_info *sbi)
{
	return (struct dirty_seglist_info *)(SM_I(sbi)->dirty_info);
}

static inline struct address_space *META_MAPPING(struct f2fs_sb_info *sbi)
{
	return sbi->meta_inode->i_mapping;
}

static inline struct address_space *NODE_MAPPING(struct f2fs_sb_info *sbi)
{
	return sbi->node_inode->i_mapping;
}

static inline bool is_sbi_flag_set(struct f2fs_sb_info *sbi, unsigned int type)
{
	return test_bit(type, &sbi->s_flag);
}

static inline void set_sbi_flag(struct f2fs_sb_info *sbi, unsigned int type)
{
	set_bit(type, &sbi->s_flag);
}

static inline void clear_sbi_flag(struct f2fs_sb_info *sbi, unsigned int type)
{
	clear_bit(type, &sbi->s_flag);
}

static inline unsigned long long cur_cp_version(struct f2fs_checkpoint *cp)
{
	return le64_to_cpu(cp->checkpoint_ver);
}

static inline bool __is_set_ckpt_flags(struct f2fs_checkpoint *cp, unsigned int f)
{
	unsigned int ckpt_flags = le32_to_cpu(cp->ckpt_flags);

	return ckpt_flags & f;
}

static inline bool is_set_ckpt_flags(struct f2fs_sb_info *sbi, unsigned int f)
{
	return __is_set_ckpt_flags(F2FS_CKPT(sbi), f);
}

static inline void __set_ckpt_flags(struct f2fs_checkpoint *cp, unsigned int f)
{
	unsigned int ckpt_flags;

	ckpt_flags = le32_to_cpu(cp->ckpt_flags);
	ckpt_flags |= f;
	cp->ckpt_flags = cpu_to_le32(ckpt_flags);
}

static inline void set_ckpt_flags(struct f2fs_sb_info *sbi, unsigned int f)
{
	spin_lock(&sbi->cp_lock);
	__set_ckpt_flags(F2FS_CKPT(sbi), f);
	spin_unlock(&sbi->cp_lock);
}

static inline void __clear_ckpt_flags(struct f2fs_checkpoint *cp, unsigned int f)
{
	unsigned int ckpt_flags;

	ckpt_flags = le32_to_cpu(cp->ckpt_flags);
	ckpt_flags &= (~f);
	cp->ckpt_flags = cpu_to_le32(ckpt_flags);
}

static inline void clear_ckpt_flags(struct f2fs_sb_info *sbi, unsigned int f)
{
	spin_lock(&sbi->cp_lock);
	__clear_ckpt_flags(F2FS_CKPT(sbi), f);
	spin_unlock(&sbi->cp_lock);
}

static inline void f2fs_lock_op(struct f2fs_sb_info *sbi)
{
	down_read(&sbi->cp_rwsem);
}

static inline void f2fs_unlock_op(struct f2fs_sb_info *sbi)
{
	up_read(&sbi->cp_rwsem);
}

static inline void f2fs_lock_all(struct f2fs_sb_info *sbi)
{
	down_write(&sbi->cp_rwsem);
}

static inline void f2fs_unlock_all(struct f2fs_sb_info *sbi)
{
	up_write(&sbi->cp_rwsem);
}

static inline int __get_cp_reason(struct f2fs_sb_info *sbi)
{
	int reason = CP_SYNC;

	if (test_opt(sbi, FASTBOOT))
		reason = CP_FASTBOOT;
	if (is_sbi_flag_set(sbi, SBI_IS_CLOSE))
		reason = CP_UMOUNT;
	return reason;
}

static inline bool __remain_node_summaries(int reason)
{
	return (reason == CP_UMOUNT || reason == CP_FASTBOOT);
}

static inline bool __exist_node_summaries(struct f2fs_sb_info *sbi)
{
	return (is_set_ckpt_flags(sbi, CP_UMOUNT_FLAG) ||
			is_set_ckpt_flags(sbi, CP_FASTBOOT_FLAG));
}

/*
 * Check whether the given nid is within node id range.
 */
static inline int check_nid_range(struct f2fs_sb_info *sbi, nid_t nid)
{
	if (unlikely(nid < F2FS_ROOT_INO(sbi)))
		return -EINVAL;
	if (unlikely(nid >= NM_I(sbi)->max_nid))
		return -EINVAL;
	return 0;
}

#define F2FS_DEFAULT_ALLOCATED_BLOCKS	1

/*
 * Check whether the inode has blocks or not
 */
static inline int F2FS_HAS_BLOCKS(struct inode *inode)
{
	if (F2FS_I(inode)->i_xattr_nid)
		return inode->i_blocks > F2FS_DEFAULT_ALLOCATED_BLOCKS + 1;
	else
		return inode->i_blocks > F2FS_DEFAULT_ALLOCATED_BLOCKS;
}

static inline bool f2fs_has_xattr_block(unsigned int ofs)
{
	return ofs == XATTR_NODE_OFFSET;
}

static inline void f2fs_i_blocks_write(struct inode *, blkcnt_t, bool);
static inline bool inc_valid_block_count(struct f2fs_sb_info *sbi,
				 struct inode *inode, blkcnt_t *count)
{
	blkcnt_t diff;

#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (time_to_inject(sbi, FAULT_BLOCK))
		return false;
#endif
	/*
	 * let's increase this in prior to actual block count change in order
	 * for f2fs_sync_file to avoid data races when deciding checkpoint.
	 */
	percpu_counter_add(&sbi->alloc_valid_block_count, (*count));

	spin_lock(&sbi->stat_lock);
	sbi->total_valid_block_count += (block_t)(*count);
	if (unlikely(sbi->total_valid_block_count > sbi->user_block_count)) {
		diff = sbi->total_valid_block_count - sbi->user_block_count;
		*count -= diff;
		sbi->total_valid_block_count = sbi->user_block_count;
		if (!*count) {
			spin_unlock(&sbi->stat_lock);
			percpu_counter_sub(&sbi->alloc_valid_block_count, diff);
			return false;
		}
	}
	spin_unlock(&sbi->stat_lock);

	f2fs_i_blocks_write(inode, *count, true);
	return true;
}

static inline void dec_valid_block_count(struct f2fs_sb_info *sbi,
						struct inode *inode,
						blkcnt_t count)
{
	spin_lock(&sbi->stat_lock);
	f2fs_bug_on(sbi, sbi->total_valid_block_count < (block_t) count);
	f2fs_bug_on(sbi, inode->i_blocks < count);
	sbi->total_valid_block_count -= (block_t)count;
	spin_unlock(&sbi->stat_lock);
	f2fs_i_blocks_write(inode, count, false);
}

static inline void inc_page_count(struct f2fs_sb_info *sbi, int count_type)
{
	atomic_inc(&sbi->nr_pages[count_type]);

	if (count_type == F2FS_DIRTY_DATA || count_type == F2FS_INMEM_PAGES ||
		count_type == F2FS_WB_CP_DATA || count_type == F2FS_WB_DATA)
		return;

	set_sbi_flag(sbi, SBI_IS_DIRTY);
}

static inline void inode_inc_dirty_pages(struct inode *inode)
{
	atomic_inc(&F2FS_I(inode)->dirty_pages);
	inc_page_count(F2FS_I_SB(inode), S_ISDIR(inode->i_mode) ?
				F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA);
}

static inline void dec_page_count(struct f2fs_sb_info *sbi, int count_type)
{
	atomic_dec(&sbi->nr_pages[count_type]);
}

static inline void inode_dec_dirty_pages(struct inode *inode)
{
	if (!S_ISDIR(inode->i_mode) && !S_ISREG(inode->i_mode) &&
			!S_ISLNK(inode->i_mode))
		return;

	atomic_dec(&F2FS_I(inode)->dirty_pages);
	dec_page_count(F2FS_I_SB(inode), S_ISDIR(inode->i_mode) ?
				F2FS_DIRTY_DENTS : F2FS_DIRTY_DATA);
}

static inline s64 get_pages(struct f2fs_sb_info *sbi, int count_type)
{
	return atomic_read(&sbi->nr_pages[count_type]);
}

static inline int get_dirty_pages(struct inode *inode)
{
	return atomic_read(&F2FS_I(inode)->dirty_pages);
}

static inline int get_blocktype_secs(struct f2fs_sb_info *sbi, int block_type)
{
	unsigned int pages_per_sec = sbi->segs_per_sec * sbi->blocks_per_seg;
	unsigned int segs = (get_pages(sbi, block_type) + pages_per_sec - 1) >>
						sbi->log_blocks_per_seg;

	return segs / sbi->segs_per_sec;
}

static inline block_t valid_user_blocks(struct f2fs_sb_info *sbi)
{
	return sbi->total_valid_block_count;
}

static inline block_t discard_blocks(struct f2fs_sb_info *sbi)
{
	return sbi->discard_blks;
}

static inline unsigned long __bitmap_size(struct f2fs_sb_info *sbi, int flag)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);

	/* return NAT or SIT bitmap */
	if (flag == NAT_BITMAP)
		return le32_to_cpu(ckpt->nat_ver_bitmap_bytesize);
	else if (flag == SIT_BITMAP)
		return le32_to_cpu(ckpt->sit_ver_bitmap_bytesize);

	return 0;
}

static inline block_t __cp_payload(struct f2fs_sb_info *sbi)
{
	return le32_to_cpu(F2FS_RAW_SUPER(sbi)->cp_payload);
}

static inline void *__bitmap_ptr(struct f2fs_sb_info *sbi, int flag)
{
	struct f2fs_checkpoint *ckpt = F2FS_CKPT(sbi);
	int offset;

	if (__cp_payload(sbi) > 0) {
		if (flag == NAT_BITMAP)
			return &ckpt->sit_nat_version_bitmap;
		else
			return (unsigned char *)ckpt + F2FS_BLKSIZE;
	} else {
		offset = (flag == NAT_BITMAP) ?
			le32_to_cpu(ckpt->sit_ver_bitmap_bytesize) : 0;
		return &ckpt->sit_nat_version_bitmap + offset;
	}
}

static inline block_t __start_cp_addr(struct f2fs_sb_info *sbi)
{
	block_t start_addr = le32_to_cpu(F2FS_RAW_SUPER(sbi)->cp_blkaddr);

	if (sbi->cur_cp_pack == 2)
		start_addr += sbi->blocks_per_seg;
	return start_addr;
}

static inline block_t __start_cp_next_addr(struct f2fs_sb_info *sbi)
{
	block_t start_addr = le32_to_cpu(F2FS_RAW_SUPER(sbi)->cp_blkaddr);

	if (sbi->cur_cp_pack == 1)
		start_addr += sbi->blocks_per_seg;
	return start_addr;
}

static inline void __set_cp_next_pack(struct f2fs_sb_info *sbi)
{
	sbi->cur_cp_pack = (sbi->cur_cp_pack == 1) ? 2 : 1;
}

static inline block_t __start_sum_addr(struct f2fs_sb_info *sbi)
{
	return le32_to_cpu(F2FS_CKPT(sbi)->cp_pack_start_sum);
}

static inline bool inc_valid_node_count(struct f2fs_sb_info *sbi,
						struct inode *inode)
{
	block_t	valid_block_count;
	unsigned int valid_node_count;

	spin_lock(&sbi->stat_lock);

	valid_block_count = sbi->total_valid_block_count + 1;
	if (unlikely(valid_block_count > sbi->user_block_count)) {
		spin_unlock(&sbi->stat_lock);
		return false;
	}

	valid_node_count = sbi->total_valid_node_count + 1;
	if (unlikely(valid_node_count > sbi->total_node_count)) {
		spin_unlock(&sbi->stat_lock);
		return false;
	}

	if (inode)
		f2fs_i_blocks_write(inode, 1, true);

	sbi->total_valid_node_count++;
	sbi->total_valid_block_count++;
	spin_unlock(&sbi->stat_lock);

	percpu_counter_inc(&sbi->alloc_valid_block_count);
	return true;
}

static inline void dec_valid_node_count(struct f2fs_sb_info *sbi,
						struct inode *inode)
{
	spin_lock(&sbi->stat_lock);

	f2fs_bug_on(sbi, !sbi->total_valid_block_count);
	f2fs_bug_on(sbi, !sbi->total_valid_node_count);
	f2fs_bug_on(sbi, !inode->i_blocks);

	f2fs_i_blocks_write(inode, 1, false);
	sbi->total_valid_node_count--;
	sbi->total_valid_block_count--;

	spin_unlock(&sbi->stat_lock);
}

static inline unsigned int valid_node_count(struct f2fs_sb_info *sbi)
{
	return sbi->total_valid_node_count;
}

static inline void inc_valid_inode_count(struct f2fs_sb_info *sbi)
{
	percpu_counter_inc(&sbi->total_valid_inode_count);
}

static inline void dec_valid_inode_count(struct f2fs_sb_info *sbi)
{
	percpu_counter_dec(&sbi->total_valid_inode_count);
}

static inline s64 valid_inode_count(struct f2fs_sb_info *sbi)
{
	return percpu_counter_sum_positive(&sbi->total_valid_inode_count);
}

static inline struct page *f2fs_grab_cache_page(struct address_space *mapping,
						pgoff_t index, bool for_write)
{
#ifdef CONFIG_F2FS_FAULT_INJECTION
	struct page *page = find_lock_page(mapping, index);
	if (page)
		return page;

	if (time_to_inject(F2FS_M_SB(mapping), FAULT_PAGE_ALLOC))
		return NULL;
#endif
	if (!for_write)
		return grab_cache_page(mapping, index);
	return grab_cache_page_write_begin(mapping, index, AOP_FLAG_NOFS);
}

static inline void f2fs_copy_page(struct page *src, struct page *dst)
{
	char *src_kaddr = kmap(src);
	char *dst_kaddr = kmap(dst);

	memcpy(dst_kaddr, src_kaddr, PAGE_SIZE);
	kunmap(dst);
	kunmap(src);
}

static inline void f2fs_put_page(struct page *page, int unlock)
{
	if (!page)
		return;

	if (unlock) {
		f2fs_bug_on(F2FS_P_SB(page), !PageLocked(page));
		unlock_page(page);
	}
	put_page(page);
}

static inline void f2fs_put_dnode(struct dnode_of_data *dn)
{
	if (dn->node_page)
		f2fs_put_page(dn->node_page, 1);
	if (dn->inode_page && dn->node_page != dn->inode_page)
		f2fs_put_page(dn->inode_page, 0);
	dn->node_page = NULL;
	dn->inode_page = NULL;
}

static inline struct kmem_cache *f2fs_kmem_cache_create(const char *name,
					size_t size)
{
	return kmem_cache_create(name, size, 0, SLAB_RECLAIM_ACCOUNT, NULL);
}

static inline void *f2fs_kmem_cache_alloc(struct kmem_cache *cachep,
						gfp_t flags)
{
	void *entry;

	entry = kmem_cache_alloc(cachep, flags);
	if (!entry)
		entry = kmem_cache_alloc(cachep, flags | __GFP_NOFAIL);
	return entry;
}

static inline struct bio *f2fs_bio_alloc(int npages)
{
	struct bio *bio;

	/* No failure on bio allocation */
	bio = bio_alloc(GFP_NOIO, npages);
	if (!bio)
		bio = bio_alloc(GFP_NOIO | __GFP_NOFAIL, npages);
	return bio;
}

static inline void f2fs_radix_tree_insert(struct radix_tree_root *root,
				unsigned long index, void *item)
{
	while (radix_tree_insert(root, index, item))
		cond_resched();
}

#define RAW_IS_INODE(p)	((p)->footer.nid == (p)->footer.ino)

static inline bool IS_INODE(struct page *page)
{
	struct f2fs_node *p = F2FS_NODE(page);
	return RAW_IS_INODE(p);
}

static inline __le32 *blkaddr_in_node(struct f2fs_node *node)
{
	return RAW_IS_INODE(node) ? node->i.i_addr : node->dn.addr;
}

static inline block_t datablock_addr(struct page *node_page,
		unsigned int offset)
{
	struct f2fs_node *raw_node;
	__le32 *addr_array;
	raw_node = F2FS_NODE(node_page);
	addr_array = blkaddr_in_node(raw_node);
	return le32_to_cpu(addr_array[offset]);
}

static inline int f2fs_test_bit(unsigned int nr, char *addr)
{
	int mask;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	return mask & *addr;
}

static inline void f2fs_set_bit(unsigned int nr, char *addr)
{
	int mask;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	*addr |= mask;
}

static inline void f2fs_clear_bit(unsigned int nr, char *addr)
{
	int mask;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	*addr &= ~mask;
}

static inline int f2fs_test_and_set_bit(unsigned int nr, char *addr)
{
	int mask;
	int ret;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	ret = mask & *addr;
	*addr |= mask;
	return ret;
}

static inline int f2fs_test_and_clear_bit(unsigned int nr, char *addr)
{
	int mask;
	int ret;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	ret = mask & *addr;
	*addr &= ~mask;
	return ret;
}

static inline void f2fs_change_bit(unsigned int nr, char *addr)
{
	int mask;

	addr += (nr >> 3);
	mask = 1 << (7 - (nr & 0x07));
	*addr ^= mask;
}

/* used for f2fs_inode_info->flags */
enum {
	FI_NEW_INODE,		/* indicate newly allocated inode */
	FI_DIRTY_INODE,		/* indicate inode is dirty or not */
	FI_AUTO_RECOVER,	/* indicate inode is recoverable */
	FI_DIRTY_DIR,		/* indicate directory has dirty pages */
	FI_INC_LINK,		/* need to increment i_nlink */
	FI_ACL_MODE,		/* indicate acl mode */
	FI_NO_ALLOC,		/* should not allocate any blocks */
	FI_FREE_NID,		/* free allocated nide */
	FI_NO_EXTENT,		/* not to use the extent cache */
	FI_INLINE_XATTR,	/* used for inline xattr */
	FI_INLINE_DATA,		/* used for inline data*/
	FI_INLINE_DENTRY,	/* used for inline dentry */
	FI_APPEND_WRITE,	/* inode has appended data */
	FI_UPDATE_WRITE,	/* inode has in-place-update data */
	FI_NEED_IPU,		/* used for ipu per file */
	FI_ATOMIC_FILE,		/* indicate atomic file */
	FI_ATOMIC_COMMIT,	/* indicate the state of atomical committing */
	FI_VOLATILE_FILE,	/* indicate volatile file */
	FI_FIRST_BLOCK_WRITTEN,	/* indicate #0 data block was written */
	FI_DROP_CACHE,		/* drop dirty page cache */
	FI_DATA_EXIST,		/* indicate data exists */
	FI_INLINE_DOTS,		/* indicate inline dot dentries */
	FI_DO_DEFRAG,		/* indicate defragment is running */
	FI_DIRTY_FILE,		/* indicate regular/symlink has dirty pages */
};

static inline void __mark_inode_dirty_flag(struct inode *inode,
						int flag, bool set)
{
	switch (flag) {
	case FI_INLINE_XATTR:
	case FI_INLINE_DATA:
	case FI_INLINE_DENTRY:
		if (set)
			return;
	case FI_DATA_EXIST:
	case FI_INLINE_DOTS:
		f2fs_mark_inode_dirty_sync(inode, true);
	}
}

static inline void set_inode_flag(struct inode *inode, int flag)
{
	if (!test_bit(flag, &F2FS_I(inode)->flags))
		set_bit(flag, &F2FS_I(inode)->flags);
	__mark_inode_dirty_flag(inode, flag, true);
}

static inline int is_inode_flag_set(struct inode *inode, int flag)
{
	return test_bit(flag, &F2FS_I(inode)->flags);
}

static inline void clear_inode_flag(struct inode *inode, int flag)
{
	if (test_bit(flag, &F2FS_I(inode)->flags))
		clear_bit(flag, &F2FS_I(inode)->flags);
	__mark_inode_dirty_flag(inode, flag, false);
}

static inline void set_acl_inode(struct inode *inode, umode_t mode)
{
	F2FS_I(inode)->i_acl_mode = mode;
	set_inode_flag(inode, FI_ACL_MODE);
	f2fs_mark_inode_dirty_sync(inode, false);
}

static inline void f2fs_i_links_write(struct inode *inode, bool inc)
{
	if (inc)
		inc_nlink(inode);
	else
		drop_nlink(inode);
	f2fs_mark_inode_dirty_sync(inode, true);
}

static inline void f2fs_i_blocks_write(struct inode *inode,
					blkcnt_t diff, bool add)
{
	bool clean = !is_inode_flag_set(inode, FI_DIRTY_INODE);
	bool recover = is_inode_flag_set(inode, FI_AUTO_RECOVER);

	inode->i_blocks = add ? inode->i_blocks + diff :
				inode->i_blocks - diff;
	f2fs_mark_inode_dirty_sync(inode, true);
	if (clean || recover)
		set_inode_flag(inode, FI_AUTO_RECOVER);
}

static inline void f2fs_i_size_write(struct inode *inode, loff_t i_size)
{
	bool clean = !is_inode_flag_set(inode, FI_DIRTY_INODE);
	bool recover = is_inode_flag_set(inode, FI_AUTO_RECOVER);

	if (i_size_read(inode) == i_size)
		return;

	i_size_write(inode, i_size);
	f2fs_mark_inode_dirty_sync(inode, true);
	if (clean || recover)
		set_inode_flag(inode, FI_AUTO_RECOVER);
}

static inline void f2fs_i_depth_write(struct inode *inode, unsigned int depth)
{
	F2FS_I(inode)->i_current_depth = depth;
	f2fs_mark_inode_dirty_sync(inode, true);
}

static inline void f2fs_i_xnid_write(struct inode *inode, nid_t xnid)
{
	F2FS_I(inode)->i_xattr_nid = xnid;
	f2fs_mark_inode_dirty_sync(inode, true);
}

static inline void f2fs_i_pino_write(struct inode *inode, nid_t pino)
{
	F2FS_I(inode)->i_pino = pino;
	f2fs_mark_inode_dirty_sync(inode, true);
}

static inline void get_inline_info(struct inode *inode, struct f2fs_inode *ri)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);

	if (ri->i_inline & F2FS_INLINE_XATTR)
		set_bit(FI_INLINE_XATTR, &fi->flags);
	if (ri->i_inline & F2FS_INLINE_DATA)
		set_bit(FI_INLINE_DATA, &fi->flags);
	if (ri->i_inline & F2FS_INLINE_DENTRY)
		set_bit(FI_INLINE_DENTRY, &fi->flags);
	if (ri->i_inline & F2FS_DATA_EXIST)
		set_bit(FI_DATA_EXIST, &fi->flags);
	if (ri->i_inline & F2FS_INLINE_DOTS)
		set_bit(FI_INLINE_DOTS, &fi->flags);
}

static inline void set_raw_inline(struct inode *inode, struct f2fs_inode *ri)
{
	ri->i_inline = 0;

	if (is_inode_flag_set(inode, FI_INLINE_XATTR))
		ri->i_inline |= F2FS_INLINE_XATTR;
	if (is_inode_flag_set(inode, FI_INLINE_DATA))
		ri->i_inline |= F2FS_INLINE_DATA;
	if (is_inode_flag_set(inode, FI_INLINE_DENTRY))
		ri->i_inline |= F2FS_INLINE_DENTRY;
	if (is_inode_flag_set(inode, FI_DATA_EXIST))
		ri->i_inline |= F2FS_DATA_EXIST;
	if (is_inode_flag_set(inode, FI_INLINE_DOTS))
		ri->i_inline |= F2FS_INLINE_DOTS;
}

static inline int f2fs_has_inline_xattr(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_INLINE_XATTR);
}

static inline unsigned int addrs_per_inode(struct inode *inode)
{
	if (f2fs_has_inline_xattr(inode))
		return DEF_ADDRS_PER_INODE - F2FS_INLINE_XATTR_ADDRS;
	return DEF_ADDRS_PER_INODE;
}

static inline void *inline_xattr_addr(struct page *page)
{
	struct f2fs_inode *ri = F2FS_INODE(page);
	return (void *)&(ri->i_addr[DEF_ADDRS_PER_INODE -
					F2FS_INLINE_XATTR_ADDRS]);
}

static inline int inline_xattr_size(struct inode *inode)
{
	if (f2fs_has_inline_xattr(inode))
		return F2FS_INLINE_XATTR_ADDRS << 2;
	else
		return 0;
}

static inline int f2fs_has_inline_data(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_INLINE_DATA);
}

static inline void f2fs_clear_inline_inode(struct inode *inode)
{
	clear_inode_flag(inode, FI_INLINE_DATA);
	clear_inode_flag(inode, FI_DATA_EXIST);
}

static inline int f2fs_exist_data(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_DATA_EXIST);
}

static inline int f2fs_has_inline_dots(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_INLINE_DOTS);
}

static inline bool f2fs_is_atomic_file(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_ATOMIC_FILE);
}

static inline bool f2fs_is_commit_atomic_write(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_ATOMIC_COMMIT);
}

static inline bool f2fs_is_volatile_file(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_VOLATILE_FILE);
}

static inline bool f2fs_is_first_block_written(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_FIRST_BLOCK_WRITTEN);
}

static inline bool f2fs_is_drop_cache(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_DROP_CACHE);
}

static inline void *inline_data_addr(struct page *page)
{
	struct f2fs_inode *ri = F2FS_INODE(page);
	return (void *)&(ri->i_addr[1]);
}

static inline int f2fs_has_inline_dentry(struct inode *inode)
{
	return is_inode_flag_set(inode, FI_INLINE_DENTRY);
}

static inline void f2fs_dentry_kunmap(struct inode *dir, struct page *page)
{
	if (!f2fs_has_inline_dentry(dir))
		kunmap(page);
}

static inline int is_file(struct inode *inode, int type)
{
	return F2FS_I(inode)->i_advise & type;
}

static inline void set_file(struct inode *inode, int type)
{
	F2FS_I(inode)->i_advise |= type;
	f2fs_mark_inode_dirty_sync(inode, true);
}

static inline void clear_file(struct inode *inode, int type)
{
	F2FS_I(inode)->i_advise &= ~type;
	f2fs_mark_inode_dirty_sync(inode, true);
}

static inline bool f2fs_skip_inode_update(struct inode *inode, int dsync)
{
	if (dsync) {
		struct f2fs_sb_info *sbi = F2FS_I_SB(inode);
		bool ret;

		spin_lock(&sbi->inode_lock[DIRTY_META]);
		ret = list_empty(&F2FS_I(inode)->gdirty_list);
		spin_unlock(&sbi->inode_lock[DIRTY_META]);
		return ret;
	}
	if (!is_inode_flag_set(inode, FI_AUTO_RECOVER) ||
			file_keep_isize(inode) ||
			i_size_read(inode) & PAGE_MASK)
		return false;
	return F2FS_I(inode)->last_disk_size == i_size_read(inode);
}

static inline int f2fs_readonly(struct super_block *sb)
{
	return sb->s_flags & MS_RDONLY;
}

static inline bool f2fs_cp_error(struct f2fs_sb_info *sbi)
{
	return is_set_ckpt_flags(sbi, CP_ERROR_FLAG);
}

static inline bool is_dot_dotdot(const struct qstr *str)
{
	if (str->len == 1 && str->name[0] == '.')
		return true;

	if (str->len == 2 && str->name[0] == '.' && str->name[1] == '.')
		return true;

	return false;
}

static inline bool f2fs_may_extent_tree(struct inode *inode)
{
	if (!test_opt(F2FS_I_SB(inode), EXTENT_CACHE) ||
			is_inode_flag_set(inode, FI_NO_EXTENT))
		return false;

	return S_ISREG(inode->i_mode);
}

static inline void *f2fs_kmalloc(struct f2fs_sb_info *sbi,
					size_t size, gfp_t flags)
{
#ifdef CONFIG_F2FS_FAULT_INJECTION
	if (time_to_inject(sbi, FAULT_KMALLOC))
		return NULL;
#endif
	return kmalloc(size, flags);
}

static inline void *f2fs_kvmalloc(size_t size, gfp_t flags)
{
	void *ret;

	ret = kmalloc(size, flags | __GFP_NOWARN);
	if (!ret)
		ret = __vmalloc(size, flags, PAGE_KERNEL);
	return ret;
}

static inline void *f2fs_kvzalloc(size_t size, gfp_t flags)
{
	void *ret;

	ret = kzalloc(size, flags | __GFP_NOWARN);
	if (!ret)
		ret = __vmalloc(size, flags | __GFP_ZERO, PAGE_KERNEL);
	return ret;
}

#define get_inode_mode(i) \
	((is_inode_flag_set(i, FI_ACL_MODE)) ? \
	 (F2FS_I(i)->i_acl_mode) : ((i)->i_mode))

/* get offset of first page in next direct node */
#define PGOFS_OF_NEXT_DNODE(pgofs, inode)				\
	((pgofs < ADDRS_PER_INODE(inode)) ? ADDRS_PER_INODE(inode) :	\
	(pgofs - ADDRS_PER_INODE(inode) + ADDRS_PER_BLOCK) /	\
	ADDRS_PER_BLOCK * ADDRS_PER_BLOCK + ADDRS_PER_INODE(inode))

/*
 * file.c
 */
int f2fs_sync_file(struct file *, loff_t, loff_t, int);
void truncate_data_blocks(struct dnode_of_data *);
int truncate_blocks(struct inode *, u64, bool);
int f2fs_truncate(struct inode *);
int f2fs_getattr(struct vfsmount *, struct dentry *, struct kstat *);
int f2fs_setattr(struct dentry *, struct iattr *);
int truncate_hole(struct inode *, pgoff_t, pgoff_t);
int truncate_data_blocks_range(struct dnode_of_data *, int);
long f2fs_ioctl(struct file *, unsigned int, unsigned long);
long f2fs_compat_ioctl(struct file *, unsigned int, unsigned long);

/*
 * inode.c
 */
void f2fs_set_inode_flags(struct inode *);
struct inode *f2fs_iget(struct super_block *, unsigned long);
struct inode *f2fs_iget_retry(struct super_block *, unsigned long);
int try_to_free_nats(struct f2fs_sb_info *, int);
int update_inode(struct inode *, struct page *);
int update_inode_page(struct inode *);
int f2fs_write_inode(struct inode *, struct writeback_control *);
void f2fs_evict_inode(struct inode *);
void handle_failed_inode(struct inode *);

/*
 * namei.c
 */
struct dentry *f2fs_get_parent(struct dentry *child);

/*
 * dir.c
 */
void set_de_type(struct f2fs_dir_entry *, umode_t);
unsigned char get_de_type(struct f2fs_dir_entry *);
struct f2fs_dir_entry *find_target_dentry(struct fscrypt_name *,
			f2fs_hash_t, int *, struct f2fs_dentry_ptr *);
int f2fs_fill_dentries(struct dir_context *, struct f2fs_dentry_ptr *,
			unsigned int, struct fscrypt_str *);
void do_make_empty_dir(struct inode *, struct inode *,
			struct f2fs_dentry_ptr *);
struct page *init_inode_metadata(struct inode *, struct inode *,
		const struct qstr *, const struct qstr *, struct page *);
void update_parent_metadata(struct inode *, struct inode *, unsigned int);
int room_for_filename(const void *, int, int);
void f2fs_drop_nlink(struct inode *, struct inode *);
struct f2fs_dir_entry *__f2fs_find_entry(struct inode *, struct fscrypt_name *,
							struct page **);
struct f2fs_dir_entry *f2fs_find_entry(struct inode *, const struct qstr *,
							struct page **);
struct f2fs_dir_entry *f2fs_parent_dir(struct inode *, struct page **);
ino_t f2fs_inode_by_name(struct inode *, const struct qstr *, struct page **);
void f2fs_set_link(struct inode *, struct f2fs_dir_entry *,
				struct page *, struct inode *);
int update_dent_inode(struct inode *, struct inode *, const struct qstr *);
void f2fs_update_dentry(nid_t ino, umode_t mode, struct f2fs_dentry_ptr *,
			const struct qstr *, f2fs_hash_t , unsigned int);
int f2fs_add_regular_entry(struct inode *, const struct qstr *,
			const struct qstr *, struct inode *, nid_t, umode_t);
int __f2fs_do_add_link(struct inode *, struct fscrypt_name*, struct inode *,
			nid_t, umode_t);
int __f2fs_add_link(struct inode *, const struct qstr *, struct inode *, nid_t,
			umode_t);
void f2fs_delete_entry(struct f2fs_dir_entry *, struct page *, struct inode *,
							struct inode *);
int f2fs_do_tmpfile(struct inode *, struct inode *);
bool f2fs_empty_dir(struct inode *);

static inline int f2fs_add_link(struct dentry *dentry, struct inode *inode)
{
	return __f2fs_add_link(d_inode(dentry->d_parent), &dentry->d_name,
				inode, inode->i_ino, inode->i_mode);
}

/*
 * super.c
 */
int f2fs_inode_dirtied(struct inode *, bool);
void f2fs_inode_synced(struct inode *);
int f2fs_commit_super(struct f2fs_sb_info *, bool);
int f2fs_sync_fs(struct super_block *, int);
extern __printf(3, 4)
void f2fs_msg(struct super_block *, const char *, const char *, ...);
int sanity_check_ckpt(struct f2fs_sb_info *sbi);

/*
 * hash.c
 */
f2fs_hash_t f2fs_dentry_hash(const struct qstr *);

/*
 * node.c
 */
struct dnode_of_data;
struct node_info;

bool available_free_memory(struct f2fs_sb_info *, int);
int need_dentry_mark(struct f2fs_sb_info *, nid_t);
bool is_checkpointed_node(struct f2fs_sb_info *, nid_t);
bool need_inode_block_update(struct f2fs_sb_info *, nid_t);
void get_node_info(struct f2fs_sb_info *, nid_t, struct node_info *);
pgoff_t get_next_page_offset(struct dnode_of_data *, pgoff_t);
int get_dnode_of_data(struct dnode_of_data *, pgoff_t, int);
int truncate_inode_blocks(struct inode *, pgoff_t);
int truncate_xattr_node(struct inode *, struct page *);
int wait_on_node_pages_writeback(struct f2fs_sb_info *, nid_t);
int remove_inode_page(struct inode *);
struct page *new_inode_page(struct inode *);
struct page *new_node_page(struct dnode_of_data *, unsigned int, struct page *);
void ra_node_page(struct f2fs_sb_info *, nid_t);
struct page *get_node_page(struct f2fs_sb_info *, pgoff_t);
struct page *get_node_page_ra(struct page *, int);
void move_node_page(struct page *, int);
int fsync_node_pages(struct f2fs_sb_info *, struct inode *,
			struct writeback_control *, bool);
int sync_node_pages(struct f2fs_sb_info *, struct writeback_control *);
void build_free_nids(struct f2fs_sb_info *, bool);
bool alloc_nid(struct f2fs_sb_info *, nid_t *);
void alloc_nid_done(struct f2fs_sb_info *, nid_t);
void alloc_nid_failed(struct f2fs_sb_info *, nid_t);
int try_to_free_nids(struct f2fs_sb_info *, int);
void recover_inline_xattr(struct inode *, struct page *);
void recover_xattr_data(struct inode *, struct page *, block_t);
int recover_inode_page(struct f2fs_sb_info *, struct page *);
int restore_node_summary(struct f2fs_sb_info *, unsigned int,
				struct f2fs_summary_block *);
void flush_nat_entries(struct f2fs_sb_info *);
int build_node_manager(struct f2fs_sb_info *);
void destroy_node_manager(struct f2fs_sb_info *);
int __init create_node_manager_caches(void);
void destroy_node_manager_caches(void);

/*
 * segment.c
 */
void register_inmem_page(struct inode *, struct page *);
void drop_inmem_pages(struct inode *);
int commit_inmem_pages(struct inode *);
void f2fs_balance_fs(struct f2fs_sb_info *, bool);
void f2fs_balance_fs_bg(struct f2fs_sb_info *);
int f2fs_issue_flush(struct f2fs_sb_info *);
int create_flush_cmd_control(struct f2fs_sb_info *);
void destroy_flush_cmd_control(struct f2fs_sb_info *, bool);
void invalidate_blocks(struct f2fs_sb_info *, block_t);
bool is_checkpointed_data(struct f2fs_sb_info *, block_t);
void refresh_sit_entry(struct f2fs_sb_info *, block_t, block_t);
void f2fs_wait_discard_bio(struct f2fs_sb_info *, block_t);
void clear_prefree_segments(struct f2fs_sb_info *, struct cp_control *);
void release_discard_addrs(struct f2fs_sb_info *);
int npages_for_summary_flush(struct f2fs_sb_info *, bool);
void allocate_new_segments(struct f2fs_sb_info *);
int f2fs_trim_fs(struct f2fs_sb_info *, struct fstrim_range *);
bool exist_trim_candidates(struct f2fs_sb_info *, struct cp_control *);
struct page *get_sum_page(struct f2fs_sb_info *, unsigned int);
void update_meta_page(struct f2fs_sb_info *, void *, block_t);
void write_meta_page(struct f2fs_sb_info *, struct page *);
void write_node_page(unsigned int, struct f2fs_io_info *);
void write_data_page(struct dnode_of_data *, struct f2fs_io_info *);
void rewrite_data_page(struct f2fs_io_info *);
void __f2fs_replace_block(struct f2fs_sb_info *, struct f2fs_summary *,
					block_t, block_t, bool, bool);
void f2fs_replace_block(struct f2fs_sb_info *, struct dnode_of_data *,
				block_t, block_t, unsigned char, bool, bool);
void allocate_data_block(struct f2fs_sb_info *, struct page *,
		block_t, block_t *, struct f2fs_summary *, int);
void f2fs_wait_on_page_writeback(struct page *, enum page_type, bool);
void f2fs_wait_on_encrypted_page_writeback(struct f2fs_sb_info *, block_t);
void write_data_summaries(struct f2fs_sb_info *, block_t);
void write_node_summaries(struct f2fs_sb_info *, block_t);
int lookup_journal_in_cursum(struct f2fs_journal *, int, unsigned int, int);
void flush_sit_entries(struct f2fs_sb_info *, struct cp_control *);
int build_segment_manager(struct f2fs_sb_info *);
void destroy_segment_manager(struct f2fs_sb_info *);
int __init create_segment_manager_caches(void);
void destroy_segment_manager_caches(void);

/*
 * checkpoint.c
 */
void f2fs_stop_checkpoint(struct f2fs_sb_info *, bool);
struct page *grab_meta_page(struct f2fs_sb_info *, pgoff_t);
struct page *get_meta_page(struct f2fs_sb_info *, pgoff_t);
struct page *get_tmp_page(struct f2fs_sb_info *, pgoff_t);
bool is_valid_blkaddr(struct f2fs_sb_info *, block_t, int);
int ra_meta_pages(struct f2fs_sb_info *, block_t, int, int, bool);
void ra_meta_pages_cond(struct f2fs_sb_info *, pgoff_t);
long sync_meta_pages(struct f2fs_sb_info *, enum page_type, long);
void add_ino_entry(struct f2fs_sb_info *, nid_t, int type);
void remove_ino_entry(struct f2fs_sb_info *, nid_t, int type);
void release_ino_entry(struct f2fs_sb_info *, bool);
bool exist_written_data(struct f2fs_sb_info *, nid_t, int);
int f2fs_sync_inode_meta(struct f2fs_sb_info *);
int acquire_orphan_inode(struct f2fs_sb_info *);
void release_orphan_inode(struct f2fs_sb_info *);
void add_orphan_inode(struct inode *);
void remove_orphan_inode(struct f2fs_sb_info *, nid_t);
int recover_orphan_inodes(struct f2fs_sb_info *);
int get_valid_checkpoint(struct f2fs_sb_info *);
void update_dirty_page(struct inode *, struct page *);
void remove_dirty_inode(struct inode *);
int sync_dirty_inodes(struct f2fs_sb_info *, enum inode_type);
int write_checkpoint(struct f2fs_sb_info *, struct cp_control *);
void init_ino_entry_info(struct f2fs_sb_info *);
int __init create_checkpoint_caches(void);
void destroy_checkpoint_caches(void);

/*
 * data.c
 */
void f2fs_submit_merged_bio(struct f2fs_sb_info *, enum page_type, int);
void f2fs_submit_merged_bio_cond(struct f2fs_sb_info *, struct inode *,
				struct page *, nid_t, enum page_type, int);
void f2fs_flush_merged_bios(struct f2fs_sb_info *);
int f2fs_submit_page_bio(struct f2fs_io_info *);
int f2fs_submit_page_mbio(struct f2fs_io_info *);
struct block_device *f2fs_target_device(struct f2fs_sb_info *,
				block_t, struct bio *);
int f2fs_target_device_index(struct f2fs_sb_info *, block_t);
void set_data_blkaddr(struct dnode_of_data *);
void f2fs_update_data_blkaddr(struct dnode_of_data *, block_t);
int reserve_new_blocks(struct dnode_of_data *, blkcnt_t);
int reserve_new_block(struct dnode_of_data *);
int f2fs_get_block(struct dnode_of_data *, pgoff_t);
int f2fs_preallocate_blocks(struct kiocb *, struct iov_iter *);
int f2fs_reserve_block(struct dnode_of_data *, pgoff_t);
struct page *get_read_data_page(struct inode *, pgoff_t, int, bool);
struct page *find_data_page(struct inode *, pgoff_t);
struct page *get_lock_data_page(struct inode *, pgoff_t, bool);
struct page *get_new_data_page(struct inode *, struct page *, pgoff_t, bool);
int do_write_data_page(struct f2fs_io_info *);
int f2fs_map_blocks(struct inode *, struct f2fs_map_blocks *, int, int);
int f2fs_fiemap(struct inode *inode, struct fiemap_extent_info *, u64, u64);
void f2fs_set_page_dirty_nobuffers(struct page *);
void f2fs_invalidate_page(struct page *, unsigned int, unsigned int);
int f2fs_release_page(struct page *, gfp_t);
#ifdef CONFIG_MIGRATION
int f2fs_migrate_page(struct address_space *, struct page *, struct page *,
				enum migrate_mode);
#endif

/*
 * gc.c
 */
int start_gc_thread(struct f2fs_sb_info *);
void stop_gc_thread(struct f2fs_sb_info *);
block_t start_bidx_of_node(unsigned int, struct inode *);
int f2fs_gc(struct f2fs_sb_info *, bool, bool);
void build_gc_manager(struct f2fs_sb_info *);

/*
 * recovery.c
 */
int recover_fsync_data(struct f2fs_sb_info *, bool);
bool space_for_roll_forward(struct f2fs_sb_info *);

/*
 * debug.c
 */
#ifdef CONFIG_F2FS_STAT_FS
struct f2fs_stat_info {
	struct list_head stat_list;
	struct f2fs_sb_info *sbi;
	int all_area_segs, sit_area_segs, nat_area_segs, ssa_area_segs;
	int main_area_segs, main_area_sections, main_area_zones;
	unsigned long long hit_largest, hit_cached, hit_rbtree;
	unsigned long long hit_total, total_ext;
	int ext_tree, zombie_tree, ext_node;
	int ndirty_node, ndirty_dent, ndirty_meta, ndirty_data, ndirty_imeta;
	int inmem_pages;
	unsigned int ndirty_dirs, ndirty_files, ndirty_all;
	int nats, dirty_nats, sits, dirty_sits, free_nids, alloc_nids;
	int total_count, utilization;
	int bg_gc, nr_wb_cp_data, nr_wb_data;
	int inline_xattr, inline_inode, inline_dir, orphans;
	int aw_cnt, max_aw_cnt;
	unsigned int valid_count, valid_node_count, valid_inode_count, discard_blks;
	unsigned int bimodal, avg_vblocks;
	int util_free, util_valid, util_invalid;
	int rsvd_segs, overp_segs;
	int dirty_count, node_pages, meta_pages;
	int prefree_count, call_count, cp_count, bg_cp_count;
	int tot_segs, node_segs, data_segs, free_segs, free_secs;
	int bg_node_segs, bg_data_segs;
	int tot_blks, data_blks, node_blks;
	int bg_data_blks, bg_node_blks;
	int curseg[NR_CURSEG_TYPE];
	int cursec[NR_CURSEG_TYPE];
	int curzone[NR_CURSEG_TYPE];

	unsigned int segment_count[2];
	unsigned int block_count[2];
	unsigned int inplace_count;
	unsigned long long base_mem, cache_mem, page_mem;
};

static inline struct f2fs_stat_info *F2FS_STAT(struct f2fs_sb_info *sbi)
{
	return (struct f2fs_stat_info *)sbi->stat_info;
}

#define stat_inc_cp_count(si)		((si)->cp_count++)
#define stat_inc_bg_cp_count(si)	((si)->bg_cp_count++)
#define stat_inc_call_count(si)		((si)->call_count++)
#define stat_inc_bggc_count(sbi)	((sbi)->bg_gc++)
#define stat_inc_dirty_inode(sbi, type)	((sbi)->ndirty_inode[type]++)
#define stat_dec_dirty_inode(sbi, type)	((sbi)->ndirty_inode[type]--)
#define stat_inc_total_hit(sbi)		(atomic64_inc(&(sbi)->total_hit_ext))
#define stat_inc_rbtree_node_hit(sbi)	(atomic64_inc(&(sbi)->read_hit_rbtree))
#define stat_inc_largest_node_hit(sbi)	(atomic64_inc(&(sbi)->read_hit_largest))
#define stat_inc_cached_node_hit(sbi)	(atomic64_inc(&(sbi)->read_hit_cached))
#define stat_inc_inline_xattr(inode)					\
	do {								\
		if (f2fs_has_inline_xattr(inode))			\
			(atomic_inc(&F2FS_I_SB(inode)->inline_xattr));	\
	} while (0)
#define stat_dec_inline_xattr(inode)					\
	do {								\
		if (f2fs_has_inline_xattr(inode))			\
			(atomic_dec(&F2FS_I_SB(inode)->inline_xattr));	\
	} while (0)
#define stat_inc_inline_inode(inode)					\
	do {								\
		if (f2fs_has_inline_data(inode))			\
			(atomic_inc(&F2FS_I_SB(inode)->inline_inode));	\
	} while (0)
#define stat_dec_inline_inode(inode)					\
	do {								\
		if (f2fs_has_inline_data(inode))			\
			(atomic_dec(&F2FS_I_SB(inode)->inline_inode));	\
	} while (0)
#define stat_inc_inline_dir(inode)					\
	do {								\
		if (f2fs_has_inline_dentry(inode))			\
			(atomic_inc(&F2FS_I_SB(inode)->inline_dir));	\
	} while (0)
#define stat_dec_inline_dir(inode)					\
	do {								\
		if (f2fs_has_inline_dentry(inode))			\
			(atomic_dec(&F2FS_I_SB(inode)->inline_dir));	\
	} while (0)
#define stat_inc_seg_type(sbi, curseg)					\
		((sbi)->segment_count[(curseg)->alloc_type]++)
#define stat_inc_block_count(sbi, curseg)				\
		((sbi)->block_count[(curseg)->alloc_type]++)
#define stat_inc_inplace_blocks(sbi)					\
		(atomic_inc(&(sbi)->inplace_count))
#define stat_inc_atomic_write(inode)					\
		(atomic_inc(&F2FS_I_SB(inode)->aw_cnt));
#define stat_dec_atomic_write(inode)					\
		(atomic_dec(&F2FS_I_SB(inode)->aw_cnt));
#define stat_update_max_atomic_write(inode)				\
	do {								\
		int cur = atomic_read(&F2FS_I_SB(inode)->aw_cnt);	\
		int max = atomic_read(&F2FS_I_SB(inode)->max_aw_cnt);	\
		if (cur > max)						\
			atomic_set(&F2FS_I_SB(inode)->max_aw_cnt, cur);	\
	} while (0)
#define stat_inc_seg_count(sbi, type, gc_type)				\
	do {								\
		struct f2fs_stat_info *si = F2FS_STAT(sbi);		\
		(si)->tot_segs++;					\
		if (type == SUM_TYPE_DATA) {				\
			si->data_segs++;				\
			si->bg_data_segs += (gc_type == BG_GC) ? 1 : 0;	\
		} else {						\
			si->node_segs++;				\
			si->bg_node_segs += (gc_type == BG_GC) ? 1 : 0;	\
		}							\
	} while (0)

#define stat_inc_tot_blk_count(si, blks)				\
	(si->tot_blks += (blks))

#define stat_inc_data_blk_count(sbi, blks, gc_type)			\
	do {								\
		struct f2fs_stat_info *si = F2FS_STAT(sbi);		\
		stat_inc_tot_blk_count(si, blks);			\
		si->data_blks += (blks);				\
		si->bg_data_blks += (gc_type == BG_GC) ? (blks) : 0;	\
	} while (0)

#define stat_inc_node_blk_count(sbi, blks, gc_type)			\
	do {								\
		struct f2fs_stat_info *si = F2FS_STAT(sbi);		\
		stat_inc_tot_blk_count(si, blks);			\
		si->node_blks += (blks);				\
		si->bg_node_blks += (gc_type == BG_GC) ? (blks) : 0;	\
	} while (0)

int f2fs_build_stats(struct f2fs_sb_info *);
void f2fs_destroy_stats(struct f2fs_sb_info *);
int __init f2fs_create_root_stats(void);
void f2fs_destroy_root_stats(void);
#else
#define stat_inc_cp_count(si)
#define stat_inc_bg_cp_count(si)
#define stat_inc_call_count(si)
#define stat_inc_bggc_count(si)
#define stat_inc_dirty_inode(sbi, type)
#define stat_dec_dirty_inode(sbi, type)
#define stat_inc_total_hit(sb)
#define stat_inc_rbtree_node_hit(sb)
#define stat_inc_largest_node_hit(sbi)
#define stat_inc_cached_node_hit(sbi)
#define stat_inc_inline_xattr(inode)
#define stat_dec_inline_xattr(inode)
#define stat_inc_inline_inode(inode)
#define stat_dec_inline_inode(inode)
#define stat_inc_inline_dir(inode)
#define stat_dec_inline_dir(inode)
#define stat_inc_atomic_write(inode)
#define stat_dec_atomic_write(inode)
#define stat_update_max_atomic_write(inode)
#define stat_inc_seg_type(sbi, curseg)
#define stat_inc_block_count(sbi, curseg)
#define stat_inc_inplace_blocks(sbi)
#define stat_inc_seg_count(sbi, type, gc_type)
#define stat_inc_tot_blk_count(si, blks)
#define stat_inc_data_blk_count(sbi, blks, gc_type)
#define stat_inc_node_blk_count(sbi, blks, gc_type)

static inline int f2fs_build_stats(struct f2fs_sb_info *sbi) { return 0; }
static inline void f2fs_destroy_stats(struct f2fs_sb_info *sbi) { }
static inline int __init f2fs_create_root_stats(void) { return 0; }
static inline void f2fs_destroy_root_stats(void) { }
#endif

extern const struct file_operations f2fs_dir_operations;
extern const struct file_operations f2fs_file_operations;
extern const struct inode_operations f2fs_file_inode_operations;
extern const struct address_space_operations f2fs_dblock_aops;
extern const struct address_space_operations f2fs_node_aops;
extern const struct address_space_operations f2fs_meta_aops;
extern const struct inode_operations f2fs_dir_inode_operations;
extern const struct inode_operations f2fs_symlink_inode_operations;
extern const struct inode_operations f2fs_encrypted_symlink_inode_operations;
extern const struct inode_operations f2fs_special_inode_operations;
extern struct kmem_cache *inode_entry_slab;

/*
 * inline.c
 */
bool f2fs_may_inline_data(struct inode *);
bool f2fs_may_inline_dentry(struct inode *);
void read_inline_data(struct page *, struct page *);
bool truncate_inline_inode(struct page *, u64);
int f2fs_read_inline_data(struct inode *, struct page *);
int f2fs_convert_inline_page(struct dnode_of_data *, struct page *);
int f2fs_convert_inline_inode(struct inode *);
int f2fs_write_inline_data(struct inode *, struct page *);
bool recover_inline_data(struct inode *, struct page *);
struct f2fs_dir_entry *find_in_inline_dir(struct inode *,
				struct fscrypt_name *, struct page **);
int make_empty_inline_dir(struct inode *inode, struct inode *, struct page *);
int f2fs_add_inline_entry(struct inode *, const struct qstr *,
		const struct qstr *, struct inode *, nid_t, umode_t);
void f2fs_delete_inline_entry(struct f2fs_dir_entry *, struct page *,
						struct inode *, struct inode *);
bool f2fs_empty_inline_dir(struct inode *);
int f2fs_read_inline_dir(struct file *, struct dir_context *,
						struct fscrypt_str *);
int f2fs_inline_data_fiemap(struct inode *,
		struct fiemap_extent_info *, __u64, __u64);

/*
 * shrinker.c
 */
unsigned long f2fs_shrink_count(struct shrinker *, struct shrink_control *);
unsigned long f2fs_shrink_scan(struct shrinker *, struct shrink_control *);
void f2fs_join_shrinker(struct f2fs_sb_info *);
void f2fs_leave_shrinker(struct f2fs_sb_info *);

/*
 * extent_cache.c
 */
unsigned int f2fs_shrink_extent_tree(struct f2fs_sb_info *, int);
bool f2fs_init_extent_tree(struct inode *, struct f2fs_extent *);
void f2fs_drop_extent_tree(struct inode *);
unsigned int f2fs_destroy_extent_node(struct inode *);
void f2fs_destroy_extent_tree(struct inode *);
bool f2fs_lookup_extent_cache(struct inode *, pgoff_t, struct extent_info *);
void f2fs_update_extent_cache(struct dnode_of_data *);
void f2fs_update_extent_cache_range(struct dnode_of_data *dn,
						pgoff_t, block_t, unsigned int);
void init_extent_cache_info(struct f2fs_sb_info *);
int __init create_extent_cache(void);
void destroy_extent_cache(void);

/*
 * crypto support
 */
static inline bool f2fs_encrypted_inode(struct inode *inode)
{
	return file_is_encrypt(inode);
}

static inline void f2fs_set_encrypted_inode(struct inode *inode)
{
#ifdef CONFIG_F2FS_FS_ENCRYPTION
	file_set_encrypt(inode);
#endif
}

static inline bool f2fs_bio_encrypted(struct bio *bio)
{
	return bio->bi_private != NULL;
}

static inline int f2fs_sb_has_crypto(struct super_block *sb)
{
	return F2FS_HAS_FEATURE(sb, F2FS_FEATURE_ENCRYPT);
}

static inline int f2fs_sb_mounted_blkzoned(struct super_block *sb)
{
	return F2FS_HAS_FEATURE(sb, F2FS_FEATURE_BLKZONED);
}

#ifdef CONFIG_BLK_DEV_ZONED
static inline int get_blkz_type(struct f2fs_sb_info *sbi,
			struct block_device *bdev, block_t blkaddr)
{
	unsigned int zno = blkaddr >> sbi->log_blocks_per_blkz;
	int i;

	for (i = 0; i < sbi->s_ndevs; i++)
		if (FDEV(i).bdev == bdev)
			return FDEV(i).blkz_type[zno];
	return -EINVAL;
}
#endif

static inline bool f2fs_discard_en(struct f2fs_sb_info *sbi)
{
	struct request_queue *q = bdev_get_queue(sbi->sb->s_bdev);

	return blk_queue_discard(q) || f2fs_sb_mounted_blkzoned(sbi->sb);
}

static inline void set_opt_mode(struct f2fs_sb_info *sbi, unsigned int mt)
{
	clear_opt(sbi, ADAPTIVE);
	clear_opt(sbi, LFS);

	switch (mt) {
	case F2FS_MOUNT_ADAPTIVE:
		set_opt(sbi, ADAPTIVE);
		break;
	case F2FS_MOUNT_LFS:
		set_opt(sbi, LFS);
		break;
	}
}

static inline bool f2fs_may_encrypt(struct inode *inode)
{
#ifdef CONFIG_F2FS_FS_ENCRYPTION
	umode_t mode = inode->i_mode;

	return (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode));
#else
	return 0;
#endif
}

#ifndef CONFIG_F2FS_FS_ENCRYPTION
#define fscrypt_set_d_op(i)
#define fscrypt_get_ctx			fscrypt_notsupp_get_ctx
#define fscrypt_release_ctx		fscrypt_notsupp_release_ctx
#define fscrypt_encrypt_page		fscrypt_notsupp_encrypt_page
#define fscrypt_decrypt_page		fscrypt_notsupp_decrypt_page
#define fscrypt_decrypt_bio_pages	fscrypt_notsupp_decrypt_bio_pages
#define fscrypt_pullback_bio_page	fscrypt_notsupp_pullback_bio_page
#define fscrypt_restore_control_page	fscrypt_notsupp_restore_control_page
#define fscrypt_zeroout_range		fscrypt_notsupp_zeroout_range
#define fscrypt_ioctl_set_policy	fscrypt_notsupp_ioctl_set_policy
#define fscrypt_ioctl_get_policy	fscrypt_notsupp_ioctl_get_policy
#define fscrypt_has_permitted_context	fscrypt_notsupp_has_permitted_context
#define fscrypt_inherit_context		fscrypt_notsupp_inherit_context
#define fscrypt_get_encryption_info	fscrypt_notsupp_get_encryption_info
#define fscrypt_put_encryption_info	fscrypt_notsupp_put_encryption_info
#define fscrypt_setup_filename		fscrypt_notsupp_setup_filename
#define fscrypt_free_filename		fscrypt_notsupp_free_filename
#define fscrypt_fname_encrypted_size	fscrypt_notsupp_fname_encrypted_size
#define fscrypt_fname_alloc_buffer	fscrypt_notsupp_fname_alloc_buffer
#define fscrypt_fname_free_buffer	fscrypt_notsupp_fname_free_buffer
#define fscrypt_fname_disk_to_usr	fscrypt_notsupp_fname_disk_to_usr
#define fscrypt_fname_usr_to_disk	fscrypt_notsupp_fname_usr_to_disk
#endif
#endif
