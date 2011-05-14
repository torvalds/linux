/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

#ifndef __UBIFS_H__
#define __UBIFS_H__

#include <asm/div64.h>
#include <linux/statfs.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/mtd/ubi.h>
#include <linux/pagemap.h>
#include <linux/backing-dev.h>
#include "ubifs-media.h"

/* Version of this UBIFS implementation */
#define UBIFS_VERSION 1

/* Normal UBIFS messages */
#define ubifs_msg(fmt, ...) \
		printk(KERN_NOTICE "UBIFS: " fmt "\n", ##__VA_ARGS__)
/* UBIFS error messages */
#define ubifs_err(fmt, ...)                                                  \
	printk(KERN_ERR "UBIFS error (pid %d): %s: " fmt "\n", current->pid, \
	       __func__, ##__VA_ARGS__)
/* UBIFS warning messages */
#define ubifs_warn(fmt, ...)                                         \
	printk(KERN_WARNING "UBIFS warning (pid %d): %s: " fmt "\n", \
	       current->pid, __func__, ##__VA_ARGS__)

/* UBIFS file system VFS magic number */
#define UBIFS_SUPER_MAGIC 0x24051905

/* Number of UBIFS blocks per VFS page */
#define UBIFS_BLOCKS_PER_PAGE (PAGE_CACHE_SIZE / UBIFS_BLOCK_SIZE)
#define UBIFS_BLOCKS_PER_PAGE_SHIFT (PAGE_CACHE_SHIFT - UBIFS_BLOCK_SHIFT)

/* "File system end of life" sequence number watermark */
#define SQNUM_WARN_WATERMARK 0xFFFFFFFF00000000ULL
#define SQNUM_WATERMARK      0xFFFFFFFFFF000000ULL

/*
 * Minimum amount of LEBs reserved for the index. At present the index needs at
 * least 2 LEBs: one for the index head and one for in-the-gaps method (which
 * currently does not cater for the index head and so excludes it from
 * consideration).
 */
#define MIN_INDEX_LEBS 2

/* Minimum amount of data UBIFS writes to the flash */
#define MIN_WRITE_SZ (UBIFS_DATA_NODE_SZ + 8)

/*
 * Currently we do not support inode number overlapping and re-using, so this
 * watermark defines dangerous inode number level. This should be fixed later,
 * although it is difficult to exceed current limit. Another option is to use
 * 64-bit inode numbers, but this means more overhead.
 */
#define INUM_WARN_WATERMARK 0xFFF00000
#define INUM_WATERMARK      0xFFFFFF00

/* Largest key size supported in this implementation */
#define CUR_MAX_KEY_LEN UBIFS_SK_LEN

/* Maximum number of entries in each LPT (LEB category) heap */
#define LPT_HEAP_SZ 256

/*
 * Background thread name pattern. The numbers are UBI device and volume
 * numbers.
 */
#define BGT_NAME_PATTERN "ubifs_bgt%d_%d"

/* Write-buffer synchronization timeout interval in seconds */
#define WBUF_TIMEOUT_SOFTLIMIT 3
#define WBUF_TIMEOUT_HARDLIMIT 5

/* Maximum possible inode number (only 32-bit inodes are supported now) */
#define MAX_INUM 0xFFFFFFFF

/* Number of non-data journal heads */
#define NONDATA_JHEADS_CNT 2

/* Shorter names for journal head numbers for internal usage */
#define GCHD   UBIFS_GC_HEAD
#define BASEHD UBIFS_BASE_HEAD
#define DATAHD UBIFS_DATA_HEAD

/* 'No change' value for 'ubifs_change_lp()' */
#define LPROPS_NC 0x80000001

/*
 * There is no notion of truncation key because truncation nodes do not exist
 * in TNC. However, when replaying, it is handy to introduce fake "truncation"
 * keys for truncation nodes because the code becomes simpler. So we define
 * %UBIFS_TRUN_KEY type.
 *
 * But otherwise, out of the journal reply scope, the truncation keys are
 * invalid.
 */
#define UBIFS_TRUN_KEY    UBIFS_KEY_TYPES_CNT
#define UBIFS_INVALID_KEY UBIFS_KEY_TYPES_CNT

/*
 * How much a directory entry/extended attribute entry adds to the parent/host
 * inode.
 */
#define CALC_DENT_SIZE(name_len) ALIGN(UBIFS_DENT_NODE_SZ + (name_len) + 1, 8)

/* How much an extended attribute adds to the host inode */
#define CALC_XATTR_BYTES(data_len) ALIGN(UBIFS_INO_NODE_SZ + (data_len) + 1, 8)

/*
 * Znodes which were not touched for 'OLD_ZNODE_AGE' seconds are considered
 * "old", and znode which were touched last 'YOUNG_ZNODE_AGE' seconds ago are
 * considered "young". This is used by shrinker when selecting znode to trim
 * off.
 */
#define OLD_ZNODE_AGE 20
#define YOUNG_ZNODE_AGE 5

/*
 * Some compressors, like LZO, may end up with more data then the input buffer.
 * So UBIFS always allocates larger output buffer, to be sure the compressor
 * will not corrupt memory in case of worst case compression.
 */
#define WORST_COMPR_FACTOR 2

/*
 * How much memory is needed for a buffer where we comress a data node.
 */
#define COMPRESSED_DATA_NODE_BUF_SZ \
	(UBIFS_DATA_NODE_SZ + UBIFS_BLOCK_SIZE * WORST_COMPR_FACTOR)

/* Maximum expected tree height for use by bottom_up_buf */
#define BOTTOM_UP_HEIGHT 64

/* Maximum number of data nodes to bulk-read */
#define UBIFS_MAX_BULK_READ 32

/*
 * Lockdep classes for UBIFS inode @ui_mutex.
 */
enum {
	WB_MUTEX_1 = 0,
	WB_MUTEX_2 = 1,
	WB_MUTEX_3 = 2,
};

/*
 * Znode flags (actually, bit numbers which store the flags).
 *
 * DIRTY_ZNODE: znode is dirty
 * COW_ZNODE: znode is being committed and a new instance of this znode has to
 *            be created before changing this znode
 * OBSOLETE_ZNODE: znode is obsolete, which means it was deleted, but it is
 *                 still in the commit list and the ongoing commit operation
 *                 will commit it, and delete this znode after it is done
 */
enum {
	DIRTY_ZNODE    = 0,
	COW_ZNODE      = 1,
	OBSOLETE_ZNODE = 2,
};

/*
 * Commit states.
 *
 * COMMIT_RESTING: commit is not wanted
 * COMMIT_BACKGROUND: background commit has been requested
 * COMMIT_REQUIRED: commit is required
 * COMMIT_RUNNING_BACKGROUND: background commit is running
 * COMMIT_RUNNING_REQUIRED: commit is running and it is required
 * COMMIT_BROKEN: commit failed
 */
enum {
	COMMIT_RESTING = 0,
	COMMIT_BACKGROUND,
	COMMIT_REQUIRED,
	COMMIT_RUNNING_BACKGROUND,
	COMMIT_RUNNING_REQUIRED,
	COMMIT_BROKEN,
};

/*
 * 'ubifs_scan_a_node()' return values.
 *
 * SCANNED_GARBAGE:  scanned garbage
 * SCANNED_EMPTY_SPACE: scanned empty space
 * SCANNED_A_NODE: scanned a valid node
 * SCANNED_A_CORRUPT_NODE: scanned a corrupted node
 * SCANNED_A_BAD_PAD_NODE: scanned a padding node with invalid pad length
 *
 * Greater than zero means: 'scanned that number of padding bytes'
 */
enum {
	SCANNED_GARBAGE        = 0,
	SCANNED_EMPTY_SPACE    = -1,
	SCANNED_A_NODE         = -2,
	SCANNED_A_CORRUPT_NODE = -3,
	SCANNED_A_BAD_PAD_NODE = -4,
};

/*
 * LPT cnode flag bits.
 *
 * DIRTY_CNODE: cnode is dirty
 * COW_CNODE: cnode is being committed and must be copied before writing
 * OBSOLETE_CNODE: cnode is being committed and has been copied (or deleted),
 * so it can (and must) be freed when the commit is finished
 */
enum {
	DIRTY_CNODE    = 0,
	COW_CNODE      = 1,
	OBSOLETE_CNODE = 2,
};

/*
 * Dirty flag bits (lpt_drty_flgs) for LPT special nodes.
 *
 * LTAB_DIRTY: ltab node is dirty
 * LSAVE_DIRTY: lsave node is dirty
 */
enum {
	LTAB_DIRTY  = 1,
	LSAVE_DIRTY = 2,
};

/*
 * Return codes used by the garbage collector.
 * @LEB_FREED: the logical eraseblock was freed and is ready to use
 * @LEB_FREED_IDX: indexing LEB was freed and can be used only after the commit
 * @LEB_RETAINED: the logical eraseblock was freed and retained for GC purposes
 */
enum {
	LEB_FREED,
	LEB_FREED_IDX,
	LEB_RETAINED,
};

/**
 * struct ubifs_old_idx - index node obsoleted since last commit start.
 * @rb: rb-tree node
 * @lnum: LEB number of obsoleted index node
 * @offs: offset of obsoleted index node
 */
struct ubifs_old_idx {
	struct rb_node rb;
	int lnum;
	int offs;
};

/* The below union makes it easier to deal with keys */
union ubifs_key {
	uint8_t u8[CUR_MAX_KEY_LEN];
	uint32_t u32[CUR_MAX_KEY_LEN/4];
	uint64_t u64[CUR_MAX_KEY_LEN/8];
	__le32 j32[CUR_MAX_KEY_LEN/4];
};

/**
 * struct ubifs_scan_node - UBIFS scanned node information.
 * @list: list of scanned nodes
 * @key: key of node scanned (if it has one)
 * @sqnum: sequence number
 * @type: type of node scanned
 * @offs: offset with LEB of node scanned
 * @len: length of node scanned
 * @node: raw node
 */
struct ubifs_scan_node {
	struct list_head list;
	union ubifs_key key;
	unsigned long long sqnum;
	int type;
	int offs;
	int len;
	void *node;
};

/**
 * struct ubifs_scan_leb - UBIFS scanned LEB information.
 * @lnum: logical eraseblock number
 * @nodes_cnt: number of nodes scanned
 * @nodes: list of struct ubifs_scan_node
 * @endpt: end point (and therefore the start of empty space)
 * @ecc: read returned -EBADMSG
 * @buf: buffer containing entire LEB scanned
 */
struct ubifs_scan_leb {
	int lnum;
	int nodes_cnt;
	struct list_head nodes;
	int endpt;
	int ecc;
	void *buf;
};

/**
 * struct ubifs_gced_idx_leb - garbage-collected indexing LEB.
 * @list: list
 * @lnum: LEB number
 * @unmap: OK to unmap this LEB
 *
 * This data structure is used to temporary store garbage-collected indexing
 * LEBs - they are not released immediately, but only after the next commit.
 * This is needed to guarantee recoverability.
 */
struct ubifs_gced_idx_leb {
	struct list_head list;
	int lnum;
	int unmap;
};

/**
 * struct ubifs_inode - UBIFS in-memory inode description.
 * @vfs_inode: VFS inode description object
 * @creat_sqnum: sequence number at time of creation
 * @del_cmtno: commit number corresponding to the time the inode was deleted,
 *             protected by @c->commit_sem;
 * @xattr_size: summarized size of all extended attributes in bytes
 * @xattr_cnt: count of extended attributes this inode has
 * @xattr_names: sum of lengths of all extended attribute names belonging to
 *               this inode
 * @dirty: non-zero if the inode is dirty
 * @xattr: non-zero if this is an extended attribute inode
 * @bulk_read: non-zero if bulk-read should be used
 * @ui_mutex: serializes inode write-back with the rest of VFS operations,
 *            serializes "clean <-> dirty" state changes, serializes bulk-read,
 *            protects @dirty, @bulk_read, @ui_size, and @xattr_size
 * @ui_lock: protects @synced_i_size
 * @synced_i_size: synchronized size of inode, i.e. the value of inode size
 *                 currently stored on the flash; used only for regular file
 *                 inodes
 * @ui_size: inode size used by UBIFS when writing to flash
 * @flags: inode flags (@UBIFS_COMPR_FL, etc)
 * @compr_type: default compression type used for this inode
 * @last_page_read: page number of last page read (for bulk read)
 * @read_in_a_row: number of consecutive pages read in a row (for bulk read)
 * @data_len: length of the data attached to the inode
 * @data: inode's data
 *
 * @ui_mutex exists for two main reasons. At first it prevents inodes from
 * being written back while UBIFS changing them, being in the middle of an VFS
 * operation. This way UBIFS makes sure the inode fields are consistent. For
 * example, in 'ubifs_rename()' we change 3 inodes simultaneously, and
 * write-back must not write any of them before we have finished.
 *
 * The second reason is budgeting - UBIFS has to budget all operations. If an
 * operation is going to mark an inode dirty, it has to allocate budget for
 * this. It cannot just mark it dirty because there is no guarantee there will
 * be enough flash space to write the inode back later. This means UBIFS has
 * to have full control over inode "clean <-> dirty" transitions (and pages
 * actually). But unfortunately, VFS marks inodes dirty in many places, and it
 * does not ask the file-system if it is allowed to do so (there is a notifier,
 * but it is not enough), i.e., there is no mechanism to synchronize with this.
 * So UBIFS has its own inode dirty flag and its own mutex to serialize
 * "clean <-> dirty" transitions.
 *
 * The @synced_i_size field is used to make sure we never write pages which are
 * beyond last synchronized inode size. See 'ubifs_writepage()' for more
 * information.
 *
 * The @ui_size is a "shadow" variable for @inode->i_size and UBIFS uses
 * @ui_size instead of @inode->i_size. The reason for this is that UBIFS cannot
 * make sure @inode->i_size is always changed under @ui_mutex, because it
 * cannot call 'truncate_setsize()' with @ui_mutex locked, because it would deadlock
 * with 'ubifs_writepage()' (see file.c). All the other inode fields are
 * changed under @ui_mutex, so they do not need "shadow" fields. Note, one
 * could consider to rework locking and base it on "shadow" fields.
 */
struct ubifs_inode {
	struct inode vfs_inode;
	unsigned long long creat_sqnum;
	unsigned long long del_cmtno;
	unsigned int xattr_size;
	unsigned int xattr_cnt;
	unsigned int xattr_names;
	unsigned int dirty:1;
	unsigned int xattr:1;
	unsigned int bulk_read:1;
	unsigned int compr_type:2;
	struct mutex ui_mutex;
	spinlock_t ui_lock;
	loff_t synced_i_size;
	loff_t ui_size;
	int flags;
	pgoff_t last_page_read;
	pgoff_t read_in_a_row;
	int data_len;
	void *data;
};

/**
 * struct ubifs_unclean_leb - records a LEB recovered under read-only mode.
 * @list: list
 * @lnum: LEB number of recovered LEB
 * @endpt: offset where recovery ended
 *
 * This structure records a LEB identified during recovery that needs to be
 * cleaned but was not because UBIFS was mounted read-only. The information
 * is used to clean the LEB when remounting to read-write mode.
 */
struct ubifs_unclean_leb {
	struct list_head list;
	int lnum;
	int endpt;
};

/*
 * LEB properties flags.
 *
 * LPROPS_UNCAT: not categorized
 * LPROPS_DIRTY: dirty > free, dirty >= @c->dead_wm, not index
 * LPROPS_DIRTY_IDX: dirty + free > @c->min_idx_node_sze and index
 * LPROPS_FREE: free > 0, dirty < @c->dead_wm, not empty, not index
 * LPROPS_HEAP_CNT: number of heaps used for storing categorized LEBs
 * LPROPS_EMPTY: LEB is empty, not taken
 * LPROPS_FREEABLE: free + dirty == leb_size, not index, not taken
 * LPROPS_FRDI_IDX: free + dirty == leb_size and index, may be taken
 * LPROPS_CAT_MASK: mask for the LEB categories above
 * LPROPS_TAKEN: LEB was taken (this flag is not saved on the media)
 * LPROPS_INDEX: LEB contains indexing nodes (this flag also exists on flash)
 */
enum {
	LPROPS_UNCAT     =  0,
	LPROPS_DIRTY     =  1,
	LPROPS_DIRTY_IDX =  2,
	LPROPS_FREE      =  3,
	LPROPS_HEAP_CNT  =  3,
	LPROPS_EMPTY     =  4,
	LPROPS_FREEABLE  =  5,
	LPROPS_FRDI_IDX  =  6,
	LPROPS_CAT_MASK  = 15,
	LPROPS_TAKEN     = 16,
	LPROPS_INDEX     = 32,
};

/**
 * struct ubifs_lprops - logical eraseblock properties.
 * @free: amount of free space in bytes
 * @dirty: amount of dirty space in bytes
 * @flags: LEB properties flags (see above)
 * @lnum: LEB number
 * @list: list of same-category lprops (for LPROPS_EMPTY and LPROPS_FREEABLE)
 * @hpos: heap position in heap of same-category lprops (other categories)
 */
struct ubifs_lprops {
	int free;
	int dirty;
	int flags;
	int lnum;
	union {
		struct list_head list;
		int hpos;
	};
};

/**
 * struct ubifs_lpt_lprops - LPT logical eraseblock properties.
 * @free: amount of free space in bytes
 * @dirty: amount of dirty space in bytes
 * @tgc: trivial GC flag (1 => unmap after commit end)
 * @cmt: commit flag (1 => reserved for commit)
 */
struct ubifs_lpt_lprops {
	int free;
	int dirty;
	unsigned tgc:1;
	unsigned cmt:1;
};

/**
 * struct ubifs_lp_stats - statistics of eraseblocks in the main area.
 * @empty_lebs: number of empty LEBs
 * @taken_empty_lebs: number of taken LEBs
 * @idx_lebs: number of indexing LEBs
 * @total_free: total free space in bytes (includes all LEBs)
 * @total_dirty: total dirty space in bytes (includes all LEBs)
 * @total_used: total used space in bytes (does not include index LEBs)
 * @total_dead: total dead space in bytes (does not include index LEBs)
 * @total_dark: total dark space in bytes (does not include index LEBs)
 *
 * The @taken_empty_lebs field counts the LEBs that are in the transient state
 * of having been "taken" for use but not yet written to. @taken_empty_lebs is
 * needed to account correctly for @gc_lnum, otherwise @empty_lebs could be
 * used by itself (in which case 'unused_lebs' would be a better name). In the
 * case of @gc_lnum, it is "taken" at mount time or whenever a LEB is retained
 * by GC, but unlike other empty LEBs that are "taken", it may not be written
 * straight away (i.e. before the next commit start or unmount), so either
 * @gc_lnum must be specially accounted for, or the current approach followed
 * i.e. count it under @taken_empty_lebs.
 *
 * @empty_lebs includes @taken_empty_lebs.
 *
 * @total_used, @total_dead and @total_dark fields do not account indexing
 * LEBs.
 */
struct ubifs_lp_stats {
	int empty_lebs;
	int taken_empty_lebs;
	int idx_lebs;
	long long total_free;
	long long total_dirty;
	long long total_used;
	long long total_dead;
	long long total_dark;
};

struct ubifs_nnode;

/**
 * struct ubifs_cnode - LEB Properties Tree common node.
 * @parent: parent nnode
 * @cnext: next cnode to commit
 * @flags: flags (%DIRTY_LPT_NODE or %OBSOLETE_LPT_NODE)
 * @iip: index in parent
 * @level: level in the tree (zero for pnodes, greater than zero for nnodes)
 * @num: node number
 */
struct ubifs_cnode {
	struct ubifs_nnode *parent;
	struct ubifs_cnode *cnext;
	unsigned long flags;
	int iip;
	int level;
	int num;
};

/**
 * struct ubifs_pnode - LEB Properties Tree leaf node.
 * @parent: parent nnode
 * @cnext: next cnode to commit
 * @flags: flags (%DIRTY_LPT_NODE or %OBSOLETE_LPT_NODE)
 * @iip: index in parent
 * @level: level in the tree (always zero for pnodes)
 * @num: node number
 * @lprops: LEB properties array
 */
struct ubifs_pnode {
	struct ubifs_nnode *parent;
	struct ubifs_cnode *cnext;
	unsigned long flags;
	int iip;
	int level;
	int num;
	struct ubifs_lprops lprops[UBIFS_LPT_FANOUT];
};

/**
 * struct ubifs_nbranch - LEB Properties Tree internal node branch.
 * @lnum: LEB number of child
 * @offs: offset of child
 * @nnode: nnode child
 * @pnode: pnode child
 * @cnode: cnode child
 */
struct ubifs_nbranch {
	int lnum;
	int offs;
	union {
		struct ubifs_nnode *nnode;
		struct ubifs_pnode *pnode;
		struct ubifs_cnode *cnode;
	};
};

/**
 * struct ubifs_nnode - LEB Properties Tree internal node.
 * @parent: parent nnode
 * @cnext: next cnode to commit
 * @flags: flags (%DIRTY_LPT_NODE or %OBSOLETE_LPT_NODE)
 * @iip: index in parent
 * @level: level in the tree (always greater than zero for nnodes)
 * @num: node number
 * @nbranch: branches to child nodes
 */
struct ubifs_nnode {
	struct ubifs_nnode *parent;
	struct ubifs_cnode *cnext;
	unsigned long flags;
	int iip;
	int level;
	int num;
	struct ubifs_nbranch nbranch[UBIFS_LPT_FANOUT];
};

/**
 * struct ubifs_lpt_heap - heap of categorized lprops.
 * @arr: heap array
 * @cnt: number in heap
 * @max_cnt: maximum number allowed in heap
 *
 * There are %LPROPS_HEAP_CNT heaps.
 */
struct ubifs_lpt_heap {
	struct ubifs_lprops **arr;
	int cnt;
	int max_cnt;
};

/*
 * Return codes for LPT scan callback function.
 *
 * LPT_SCAN_CONTINUE: continue scanning
 * LPT_SCAN_ADD: add the LEB properties scanned to the tree in memory
 * LPT_SCAN_STOP: stop scanning
 */
enum {
	LPT_SCAN_CONTINUE = 0,
	LPT_SCAN_ADD = 1,
	LPT_SCAN_STOP = 2,
};

struct ubifs_info;

/* Callback used by the 'ubifs_lpt_scan_nolock()' function */
typedef int (*ubifs_lpt_scan_callback)(struct ubifs_info *c,
				       const struct ubifs_lprops *lprops,
				       int in_tree, void *data);

/**
 * struct ubifs_wbuf - UBIFS write-buffer.
 * @c: UBIFS file-system description object
 * @buf: write-buffer (of min. flash I/O unit size)
 * @lnum: logical eraseblock number the write-buffer points to
 * @offs: write-buffer offset in this logical eraseblock
 * @avail: number of bytes available in the write-buffer
 * @used:  number of used bytes in the write-buffer
 * @size: write-buffer size (in [@c->min_io_size, @c->max_write_size] range)
 * @dtype: type of data stored in this LEB (%UBI_LONGTERM, %UBI_SHORTTERM,
 * %UBI_UNKNOWN)
 * @jhead: journal head the mutex belongs to (note, needed only to shut lockdep
 *         up by 'mutex_lock_nested()).
 * @sync_callback: write-buffer synchronization callback
 * @io_mutex: serializes write-buffer I/O
 * @lock: serializes @buf, @lnum, @offs, @avail, @used, @next_ino and @inodes
 *        fields
 * @softlimit: soft write-buffer timeout interval
 * @delta: hard and soft timeouts delta (the timer expire inteval is @softlimit
 *         and @softlimit + @delta)
 * @timer: write-buffer timer
 * @no_timer: non-zero if this write-buffer does not have a timer
 * @need_sync: non-zero if the timer expired and the wbuf needs sync'ing
 * @next_ino: points to the next position of the following inode number
 * @inodes: stores the inode numbers of the nodes which are in wbuf
 *
 * The write-buffer synchronization callback is called when the write-buffer is
 * synchronized in order to notify how much space was wasted due to
 * write-buffer padding and how much free space is left in the LEB.
 *
 * Note: the fields @buf, @lnum, @offs, @avail and @used can be read under
 * spin-lock or mutex because they are written under both mutex and spin-lock.
 * @buf is appended to under mutex but overwritten under both mutex and
 * spin-lock. Thus the data between @buf and @buf + @used can be read under
 * spinlock.
 */
struct ubifs_wbuf {
	struct ubifs_info *c;
	void *buf;
	int lnum;
	int offs;
	int avail;
	int used;
	int size;
	int dtype;
	int jhead;
	int (*sync_callback)(struct ubifs_info *c, int lnum, int free, int pad);
	struct mutex io_mutex;
	spinlock_t lock;
	ktime_t softlimit;
	unsigned long long delta;
	struct hrtimer timer;
	unsigned int no_timer:1;
	unsigned int need_sync:1;
	int next_ino;
	ino_t *inodes;
};

/**
 * struct ubifs_bud - bud logical eraseblock.
 * @lnum: logical eraseblock number
 * @start: where the (uncommitted) bud data starts
 * @jhead: journal head number this bud belongs to
 * @list: link in the list buds belonging to the same journal head
 * @rb: link in the tree of all buds
 */
struct ubifs_bud {
	int lnum;
	int start;
	int jhead;
	struct list_head list;
	struct rb_node rb;
};

/**
 * struct ubifs_jhead - journal head.
 * @wbuf: head's write-buffer
 * @buds_list: list of bud LEBs belonging to this journal head
 *
 * Note, the @buds list is protected by the @c->buds_lock.
 */
struct ubifs_jhead {
	struct ubifs_wbuf wbuf;
	struct list_head buds_list;
};

/**
 * struct ubifs_zbranch - key/coordinate/length branch stored in znodes.
 * @key: key
 * @znode: znode address in memory
 * @lnum: LEB number of the target node (indexing node or data node)
 * @offs: target node offset within @lnum
 * @len: target node length
 */
struct ubifs_zbranch {
	union ubifs_key key;
	union {
		struct ubifs_znode *znode;
		void *leaf;
	};
	int lnum;
	int offs;
	int len;
};

/**
 * struct ubifs_znode - in-memory representation of an indexing node.
 * @parent: parent znode or NULL if it is the root
 * @cnext: next znode to commit
 * @flags: znode flags (%DIRTY_ZNODE, %COW_ZNODE or %OBSOLETE_ZNODE)
 * @time: last access time (seconds)
 * @level: level of the entry in the TNC tree
 * @child_cnt: count of child znodes
 * @iip: index in parent's zbranch array
 * @alt: lower bound of key range has altered i.e. child inserted at slot 0
 * @lnum: LEB number of the corresponding indexing node
 * @offs: offset of the corresponding indexing node
 * @len: length  of the corresponding indexing node
 * @zbranch: array of znode branches (@c->fanout elements)
 */
struct ubifs_znode {
	struct ubifs_znode *parent;
	struct ubifs_znode *cnext;
	unsigned long flags;
	unsigned long time;
	int level;
	int child_cnt;
	int iip;
	int alt;
#ifdef CONFIG_UBIFS_FS_DEBUG
	int lnum, offs, len;
#endif
	struct ubifs_zbranch zbranch[];
};

/**
 * struct bu_info - bulk-read information.
 * @key: first data node key
 * @zbranch: zbranches of data nodes to bulk read
 * @buf: buffer to read into
 * @buf_len: buffer length
 * @gc_seq: GC sequence number to detect races with GC
 * @cnt: number of data nodes for bulk read
 * @blk_cnt: number of data blocks including holes
 * @oef: end of file reached
 */
struct bu_info {
	union ubifs_key key;
	struct ubifs_zbranch zbranch[UBIFS_MAX_BULK_READ];
	void *buf;
	int buf_len;
	int gc_seq;
	int cnt;
	int blk_cnt;
	int eof;
};

/**
 * struct ubifs_node_range - node length range description data structure.
 * @len: fixed node length
 * @min_len: minimum possible node length
 * @max_len: maximum possible node length
 *
 * If @max_len is %0, the node has fixed length @len.
 */
struct ubifs_node_range {
	union {
		int len;
		int min_len;
	};
	int max_len;
};

/**
 * struct ubifs_compressor - UBIFS compressor description structure.
 * @compr_type: compressor type (%UBIFS_COMPR_LZO, etc)
 * @cc: cryptoapi compressor handle
 * @comp_mutex: mutex used during compression
 * @decomp_mutex: mutex used during decompression
 * @name: compressor name
 * @capi_name: cryptoapi compressor name
 */
struct ubifs_compressor {
	int compr_type;
	struct crypto_comp *cc;
	struct mutex *comp_mutex;
	struct mutex *decomp_mutex;
	const char *name;
	const char *capi_name;
};

/**
 * struct ubifs_budget_req - budget requirements of an operation.
 *
 * @fast: non-zero if the budgeting should try to acquire budget quickly and
 *        should not try to call write-back
 * @recalculate: non-zero if @idx_growth, @data_growth, and @dd_growth fields
 *               have to be re-calculated
 * @new_page: non-zero if the operation adds a new page
 * @dirtied_page: non-zero if the operation makes a page dirty
 * @new_dent: non-zero if the operation adds a new directory entry
 * @mod_dent: non-zero if the operation removes or modifies an existing
 *            directory entry
 * @new_ino: non-zero if the operation adds a new inode
 * @new_ino_d: now much data newly created inode contains
 * @dirtied_ino: how many inodes the operation makes dirty
 * @dirtied_ino_d: now much data dirtied inode contains
 * @idx_growth: how much the index will supposedly grow
 * @data_growth: how much new data the operation will supposedly add
 * @dd_growth: how much data that makes other data dirty the operation will
 *             supposedly add
 *
 * @idx_growth, @data_growth and @dd_growth are not used in budget request. The
 * budgeting subsystem caches index and data growth values there to avoid
 * re-calculating them when the budget is released. However, if @idx_growth is
 * %-1, it is calculated by the release function using other fields.
 *
 * An inode may contain 4KiB of data at max., thus the widths of @new_ino_d
 * is 13 bits, and @dirtied_ino_d - 15, because up to 4 inodes may be made
 * dirty by the re-name operation.
 *
 * Note, UBIFS aligns node lengths to 8-bytes boundary, so the requester has to
 * make sure the amount of inode data which contribute to @new_ino_d and
 * @dirtied_ino_d fields are aligned.
 */
struct ubifs_budget_req {
	unsigned int fast:1;
	unsigned int recalculate:1;
#ifndef UBIFS_DEBUG
	unsigned int new_page:1;
	unsigned int dirtied_page:1;
	unsigned int new_dent:1;
	unsigned int mod_dent:1;
	unsigned int new_ino:1;
	unsigned int new_ino_d:13;
	unsigned int dirtied_ino:4;
	unsigned int dirtied_ino_d:15;
#else
	/* Not bit-fields to check for overflows */
	unsigned int new_page;
	unsigned int dirtied_page;
	unsigned int new_dent;
	unsigned int mod_dent;
	unsigned int new_ino;
	unsigned int new_ino_d;
	unsigned int dirtied_ino;
	unsigned int dirtied_ino_d;
#endif
	int idx_growth;
	int data_growth;
	int dd_growth;
};

/**
 * struct ubifs_orphan - stores the inode number of an orphan.
 * @rb: rb-tree node of rb-tree of orphans sorted by inode number
 * @list: list head of list of orphans in order added
 * @new_list: list head of list of orphans added since the last commit
 * @cnext: next orphan to commit
 * @dnext: next orphan to delete
 * @inum: inode number
 * @new: %1 => added since the last commit, otherwise %0
 */
struct ubifs_orphan {
	struct rb_node rb;
	struct list_head list;
	struct list_head new_list;
	struct ubifs_orphan *cnext;
	struct ubifs_orphan *dnext;
	ino_t inum;
	int new;
};

/**
 * struct ubifs_mount_opts - UBIFS-specific mount options information.
 * @unmount_mode: selected unmount mode (%0 default, %1 normal, %2 fast)
 * @bulk_read: enable/disable bulk-reads (%0 default, %1 disabe, %2 enable)
 * @chk_data_crc: enable/disable CRC data checking when reading data nodes
 *                (%0 default, %1 disabe, %2 enable)
 * @override_compr: override default compressor (%0 - do not override and use
 *                  superblock compressor, %1 - override and use compressor
 *                  specified in @compr_type)
 * @compr_type: compressor type to override the superblock compressor with
 *              (%UBIFS_COMPR_NONE, etc)
 */
struct ubifs_mount_opts {
	unsigned int unmount_mode:2;
	unsigned int bulk_read:2;
	unsigned int chk_data_crc:2;
	unsigned int override_compr:1;
	unsigned int compr_type:2;
};

struct ubifs_debug_info;

/**
 * struct ubifs_info - UBIFS file-system description data structure
 * (per-superblock).
 * @vfs_sb: VFS @struct super_block object
 * @bdi: backing device info object to make VFS happy and disable read-ahead
 *
 * @highest_inum: highest used inode number
 * @max_sqnum: current global sequence number
 * @cmt_no: commit number of the last successfully completed commit, protected
 *          by @commit_sem
 * @cnt_lock: protects @highest_inum and @max_sqnum counters
 * @fmt_version: UBIFS on-flash format version
 * @ro_compat_version: R/O compatibility version
 * @uuid: UUID from super block
 *
 * @lhead_lnum: log head logical eraseblock number
 * @lhead_offs: log head offset
 * @ltail_lnum: log tail logical eraseblock number (offset is always 0)
 * @log_mutex: protects the log, @lhead_lnum, @lhead_offs, @ltail_lnum, and
 *             @bud_bytes
 * @min_log_bytes: minimum required number of bytes in the log
 * @cmt_bud_bytes: used during commit to temporarily amount of bytes in
 *                 committed buds
 *
 * @buds: tree of all buds indexed by bud LEB number
 * @bud_bytes: how many bytes of flash is used by buds
 * @buds_lock: protects the @buds tree, @bud_bytes, and per-journal head bud
 *             lists
 * @jhead_cnt: count of journal heads
 * @jheads: journal heads (head zero is base head)
 * @max_bud_bytes: maximum number of bytes allowed in buds
 * @bg_bud_bytes: number of bud bytes when background commit is initiated
 * @old_buds: buds to be released after commit ends
 * @max_bud_cnt: maximum number of buds
 *
 * @commit_sem: synchronizes committer with other processes
 * @cmt_state: commit state
 * @cs_lock: commit state lock
 * @cmt_wq: wait queue to sleep on if the log is full and a commit is running
 *
 * @big_lpt: flag that LPT is too big to write whole during commit
 * @no_chk_data_crc: do not check CRCs when reading data nodes (except during
 *                   recovery)
 * @bulk_read: enable bulk-reads
 * @default_compr: default compression algorithm (%UBIFS_COMPR_LZO, etc)
 * @rw_incompat: the media is not R/W compatible
 *
 * @tnc_mutex: protects the Tree Node Cache (TNC), @zroot, @cnext, @enext, and
 *             @calc_idx_sz
 * @zroot: zbranch which points to the root index node and znode
 * @cnext: next znode to commit
 * @enext: next znode to commit to empty space
 * @gap_lebs: array of LEBs used by the in-gaps commit method
 * @cbuf: commit buffer
 * @ileb_buf: buffer for commit in-the-gaps method
 * @ileb_len: length of data in ileb_buf
 * @ihead_lnum: LEB number of index head
 * @ihead_offs: offset of index head
 * @ilebs: pre-allocated index LEBs
 * @ileb_cnt: number of pre-allocated index LEBs
 * @ileb_nxt: next pre-allocated index LEBs
 * @old_idx: tree of index nodes obsoleted since the last commit start
 * @bottom_up_buf: a buffer which is used by 'dirty_cow_bottom_up()' in tnc.c
 *
 * @mst_node: master node
 * @mst_offs: offset of valid master node
 * @mst_mutex: protects the master node area, @mst_node, and @mst_offs
 *
 * @max_bu_buf_len: maximum bulk-read buffer length
 * @bu_mutex: protects the pre-allocated bulk-read buffer and @c->bu
 * @bu: pre-allocated bulk-read information
 *
 * @write_reserve_mutex: protects @write_reserve_buf
 * @write_reserve_buf: on the write path we allocate memory, which might
 *                     sometimes be unavailable, in which case we use this
 *                     write reserve buffer
 *
 * @log_lebs: number of logical eraseblocks in the log
 * @log_bytes: log size in bytes
 * @log_last: last LEB of the log
 * @lpt_lebs: number of LEBs used for lprops table
 * @lpt_first: first LEB of the lprops table area
 * @lpt_last: last LEB of the lprops table area
 * @orph_lebs: number of LEBs used for the orphan area
 * @orph_first: first LEB of the orphan area
 * @orph_last: last LEB of the orphan area
 * @main_lebs: count of LEBs in the main area
 * @main_first: first LEB of the main area
 * @main_bytes: main area size in bytes
 *
 * @key_hash_type: type of the key hash
 * @key_hash: direntry key hash function
 * @key_fmt: key format
 * @key_len: key length
 * @fanout: fanout of the index tree (number of links per indexing node)
 *
 * @min_io_size: minimal input/output unit size
 * @min_io_shift: number of bits in @min_io_size minus one
 * @max_write_size: maximum amount of bytes the underlying flash can write at a
 *                  time (MTD write buffer size)
 * @max_write_shift: number of bits in @max_write_size minus one
 * @leb_size: logical eraseblock size in bytes
 * @leb_start: starting offset of logical eraseblocks within physical
 *             eraseblocks
 * @half_leb_size: half LEB size
 * @idx_leb_size: how many bytes of an LEB are effectively available when it is
 *                used to store indexing nodes (@leb_size - @max_idx_node_sz)
 * @leb_cnt: count of logical eraseblocks
 * @max_leb_cnt: maximum count of logical eraseblocks
 * @old_leb_cnt: count of logical eraseblocks before re-size
 * @ro_media: the underlying UBI volume is read-only
 * @ro_mount: the file-system was mounted as read-only
 * @ro_error: UBIFS switched to R/O mode because an error happened
 *
 * @dirty_pg_cnt: number of dirty pages (not used)
 * @dirty_zn_cnt: number of dirty znodes
 * @clean_zn_cnt: number of clean znodes
 *
 * @budg_idx_growth: amount of bytes budgeted for index growth
 * @budg_data_growth: amount of bytes budgeted for cached data
 * @budg_dd_growth: amount of bytes budgeted for cached data that will make
 *                  other data dirty
 * @budg_uncommitted_idx: amount of bytes were budgeted for growth of the index,
 *                        but which still have to be taken into account because
 *                        the index has not been committed so far
 * @space_lock: protects @budg_idx_growth, @budg_data_growth, @budg_dd_growth,
 *              @budg_uncommited_idx, @min_idx_lebs, @old_idx_sz, @lst,
 *              @nospace, and @nospace_rp;
 * @min_idx_lebs: minimum number of LEBs required for the index
 * @old_idx_sz: size of index on flash
 * @calc_idx_sz: temporary variable which is used to calculate new index size
 *               (contains accurate new index size at end of TNC commit start)
 * @lst: lprops statistics
 * @nospace: non-zero if the file-system does not have flash space (used as
 *           optimization)
 * @nospace_rp: the same as @nospace, but additionally means that even reserved
 *              pool is full
 *
 * @page_budget: budget for a page
 * @inode_budget: budget for an inode
 * @dent_budget: budget for a directory entry
 *
 * @ref_node_alsz: size of the LEB reference node aligned to the min. flash
 * I/O unit
 * @mst_node_alsz: master node aligned size
 * @min_idx_node_sz: minimum indexing node aligned on 8-bytes boundary
 * @max_idx_node_sz: maximum indexing node aligned on 8-bytes boundary
 * @max_inode_sz: maximum possible inode size in bytes
 * @max_znode_sz: size of znode in bytes
 *
 * @leb_overhead: how many bytes are wasted in an LEB when it is filled with
 *                data nodes of maximum size - used in free space reporting
 * @dead_wm: LEB dead space watermark
 * @dark_wm: LEB dark space watermark
 * @block_cnt: count of 4KiB blocks on the FS
 *
 * @ranges: UBIFS node length ranges
 * @ubi: UBI volume descriptor
 * @di: UBI device information
 * @vi: UBI volume information
 *
 * @orph_tree: rb-tree of orphan inode numbers
 * @orph_list: list of orphan inode numbers in order added
 * @orph_new: list of orphan inode numbers added since last commit
 * @orph_cnext: next orphan to commit
 * @orph_dnext: next orphan to delete
 * @orphan_lock: lock for orph_tree and orph_new
 * @orph_buf: buffer for orphan nodes
 * @new_orphans: number of orphans since last commit
 * @cmt_orphans: number of orphans being committed
 * @tot_orphans: number of orphans in the rb_tree
 * @max_orphans: maximum number of orphans allowed
 * @ohead_lnum: orphan head LEB number
 * @ohead_offs: orphan head offset
 * @no_orphs: non-zero if there are no orphans
 *
 * @bgt: UBIFS background thread
 * @bgt_name: background thread name
 * @need_bgt: if background thread should run
 * @need_wbuf_sync: if write-buffers have to be synchronized
 *
 * @gc_lnum: LEB number used for garbage collection
 * @sbuf: a buffer of LEB size used by GC and replay for scanning
 * @idx_gc: list of index LEBs that have been garbage collected
 * @idx_gc_cnt: number of elements on the idx_gc list
 * @gc_seq: incremented for every non-index LEB garbage collected
 * @gced_lnum: last non-index LEB that was garbage collected
 *
 * @infos_list: links all 'ubifs_info' objects
 * @umount_mutex: serializes shrinker and un-mount
 * @shrinker_run_no: shrinker run number
 *
 * @space_bits: number of bits needed to record free or dirty space
 * @lpt_lnum_bits: number of bits needed to record a LEB number in the LPT
 * @lpt_offs_bits: number of bits needed to record an offset in the LPT
 * @lpt_spc_bits: number of bits needed to space in the LPT
 * @pcnt_bits: number of bits needed to record pnode or nnode number
 * @lnum_bits: number of bits needed to record LEB number
 * @nnode_sz: size of on-flash nnode
 * @pnode_sz: size of on-flash pnode
 * @ltab_sz: size of on-flash LPT lprops table
 * @lsave_sz: size of on-flash LPT save table
 * @pnode_cnt: number of pnodes
 * @nnode_cnt: number of nnodes
 * @lpt_hght: height of the LPT
 * @pnodes_have: number of pnodes in memory
 *
 * @lp_mutex: protects lprops table and all the other lprops-related fields
 * @lpt_lnum: LEB number of the root nnode of the LPT
 * @lpt_offs: offset of the root nnode of the LPT
 * @nhead_lnum: LEB number of LPT head
 * @nhead_offs: offset of LPT head
 * @lpt_drty_flgs: dirty flags for LPT special nodes e.g. ltab
 * @dirty_nn_cnt: number of dirty nnodes
 * @dirty_pn_cnt: number of dirty pnodes
 * @check_lpt_free: flag that indicates LPT GC may be needed
 * @lpt_sz: LPT size
 * @lpt_nod_buf: buffer for an on-flash nnode or pnode
 * @lpt_buf: buffer of LEB size used by LPT
 * @nroot: address in memory of the root nnode of the LPT
 * @lpt_cnext: next LPT node to commit
 * @lpt_heap: array of heaps of categorized lprops
 * @dirty_idx: a (reverse sorted) copy of the LPROPS_DIRTY_IDX heap as at
 *             previous commit start
 * @uncat_list: list of un-categorized LEBs
 * @empty_list: list of empty LEBs
 * @freeable_list: list of freeable non-index LEBs (free + dirty == @leb_size)
 * @frdi_idx_list: list of freeable index LEBs (free + dirty == @leb_size)
 * @freeable_cnt: number of freeable LEBs in @freeable_list
 *
 * @ltab_lnum: LEB number of LPT's own lprops table
 * @ltab_offs: offset of LPT's own lprops table
 * @ltab: LPT's own lprops table
 * @ltab_cmt: LPT's own lprops table (commit copy)
 * @lsave_cnt: number of LEB numbers in LPT's save table
 * @lsave_lnum: LEB number of LPT's save table
 * @lsave_offs: offset of LPT's save table
 * @lsave: LPT's save table
 * @lscan_lnum: LEB number of last LPT scan
 *
 * @rp_size: size of the reserved pool in bytes
 * @report_rp_size: size of the reserved pool reported to user-space
 * @rp_uid: reserved pool user ID
 * @rp_gid: reserved pool group ID
 *
 * @empty: %1 if the UBI device is empty
 * @need_recovery: %1 if the file-system needs recovery
 * @replaying: %1 during journal replay
 * @mounting: %1 while mounting
 * @remounting_rw: %1 while re-mounting from R/O mode to R/W mode
 * @replay_tree: temporary tree used during journal replay
 * @replay_list: temporary list used during journal replay
 * @replay_buds: list of buds to replay
 * @cs_sqnum: sequence number of first node in the log (commit start node)
 * @replay_sqnum: sequence number of node currently being replayed
 * @unclean_leb_list: LEBs to recover when re-mounting R/O mounted FS to R/W
 *                    mode
 * @rcvrd_mst_node: recovered master node to write when re-mounting R/O mounted
 *                  FS to R/W mode
 * @size_tree: inode size information for recovery
 * @mount_opts: UBIFS-specific mount options
 *
 * @dbg: debugging-related information
 */
struct ubifs_info {
	struct super_block *vfs_sb;
	struct backing_dev_info bdi;

	ino_t highest_inum;
	unsigned long long max_sqnum;
	unsigned long long cmt_no;
	spinlock_t cnt_lock;
	int fmt_version;
	int ro_compat_version;
	unsigned char uuid[16];

	int lhead_lnum;
	int lhead_offs;
	int ltail_lnum;
	struct mutex log_mutex;
	int min_log_bytes;
	long long cmt_bud_bytes;

	struct rb_root buds;
	long long bud_bytes;
	spinlock_t buds_lock;
	int jhead_cnt;
	struct ubifs_jhead *jheads;
	long long max_bud_bytes;
	long long bg_bud_bytes;
	struct list_head old_buds;
	int max_bud_cnt;

	struct rw_semaphore commit_sem;
	int cmt_state;
	spinlock_t cs_lock;
	wait_queue_head_t cmt_wq;

	unsigned int big_lpt:1;
	unsigned int no_chk_data_crc:1;
	unsigned int bulk_read:1;
	unsigned int default_compr:2;
	unsigned int rw_incompat:1;

	struct mutex tnc_mutex;
	struct ubifs_zbranch zroot;
	struct ubifs_znode *cnext;
	struct ubifs_znode *enext;
	int *gap_lebs;
	void *cbuf;
	void *ileb_buf;
	int ileb_len;
	int ihead_lnum;
	int ihead_offs;
	int *ilebs;
	int ileb_cnt;
	int ileb_nxt;
	struct rb_root old_idx;
	int *bottom_up_buf;

	struct ubifs_mst_node *mst_node;
	int mst_offs;
	struct mutex mst_mutex;

	int max_bu_buf_len;
	struct mutex bu_mutex;
	struct bu_info bu;

	struct mutex write_reserve_mutex;
	void *write_reserve_buf;

	int log_lebs;
	long long log_bytes;
	int log_last;
	int lpt_lebs;
	int lpt_first;
	int lpt_last;
	int orph_lebs;
	int orph_first;
	int orph_last;
	int main_lebs;
	int main_first;
	long long main_bytes;

	uint8_t key_hash_type;
	uint32_t (*key_hash)(const char *str, int len);
	int key_fmt;
	int key_len;
	int fanout;

	int min_io_size;
	int min_io_shift;
	int max_write_size;
	int max_write_shift;
	int leb_size;
	int leb_start;
	int half_leb_size;
	int idx_leb_size;
	int leb_cnt;
	int max_leb_cnt;
	int old_leb_cnt;
	unsigned int ro_media:1;
	unsigned int ro_mount:1;
	unsigned int ro_error:1;

	atomic_long_t dirty_pg_cnt;
	atomic_long_t dirty_zn_cnt;
	atomic_long_t clean_zn_cnt;

	long long budg_idx_growth;
	long long budg_data_growth;
	long long budg_dd_growth;
	long long budg_uncommitted_idx;
	spinlock_t space_lock;
	int min_idx_lebs;
	unsigned long long old_idx_sz;
	unsigned long long calc_idx_sz;
	struct ubifs_lp_stats lst;
	unsigned int nospace:1;
	unsigned int nospace_rp:1;

	int page_budget;
	int inode_budget;
	int dent_budget;

	int ref_node_alsz;
	int mst_node_alsz;
	int min_idx_node_sz;
	int max_idx_node_sz;
	long long max_inode_sz;
	int max_znode_sz;

	int leb_overhead;
	int dead_wm;
	int dark_wm;
	int block_cnt;

	struct ubifs_node_range ranges[UBIFS_NODE_TYPES_CNT];
	struct ubi_volume_desc *ubi;
	struct ubi_device_info di;
	struct ubi_volume_info vi;

	struct rb_root orph_tree;
	struct list_head orph_list;
	struct list_head orph_new;
	struct ubifs_orphan *orph_cnext;
	struct ubifs_orphan *orph_dnext;
	spinlock_t orphan_lock;
	void *orph_buf;
	int new_orphans;
	int cmt_orphans;
	int tot_orphans;
	int max_orphans;
	int ohead_lnum;
	int ohead_offs;
	int no_orphs;

	struct task_struct *bgt;
	char bgt_name[sizeof(BGT_NAME_PATTERN) + 9];
	int need_bgt;
	int need_wbuf_sync;

	int gc_lnum;
	void *sbuf;
	struct list_head idx_gc;
	int idx_gc_cnt;
	int gc_seq;
	int gced_lnum;

	struct list_head infos_list;
	struct mutex umount_mutex;
	unsigned int shrinker_run_no;

	int space_bits;
	int lpt_lnum_bits;
	int lpt_offs_bits;
	int lpt_spc_bits;
	int pcnt_bits;
	int lnum_bits;
	int nnode_sz;
	int pnode_sz;
	int ltab_sz;
	int lsave_sz;
	int pnode_cnt;
	int nnode_cnt;
	int lpt_hght;
	int pnodes_have;

	struct mutex lp_mutex;
	int lpt_lnum;
	int lpt_offs;
	int nhead_lnum;
	int nhead_offs;
	int lpt_drty_flgs;
	int dirty_nn_cnt;
	int dirty_pn_cnt;
	int check_lpt_free;
	long long lpt_sz;
	void *lpt_nod_buf;
	void *lpt_buf;
	struct ubifs_nnode *nroot;
	struct ubifs_cnode *lpt_cnext;
	struct ubifs_lpt_heap lpt_heap[LPROPS_HEAP_CNT];
	struct ubifs_lpt_heap dirty_idx;
	struct list_head uncat_list;
	struct list_head empty_list;
	struct list_head freeable_list;
	struct list_head frdi_idx_list;
	int freeable_cnt;

	int ltab_lnum;
	int ltab_offs;
	struct ubifs_lpt_lprops *ltab;
	struct ubifs_lpt_lprops *ltab_cmt;
	int lsave_cnt;
	int lsave_lnum;
	int lsave_offs;
	int *lsave;
	int lscan_lnum;

	long long rp_size;
	long long report_rp_size;
	uid_t rp_uid;
	gid_t rp_gid;

	/* The below fields are used only during mounting and re-mounting */
	unsigned int empty:1;
	unsigned int need_recovery:1;
	unsigned int replaying:1;
	unsigned int mounting:1;
	unsigned int remounting_rw:1;
	struct rb_root replay_tree;
	struct list_head replay_list;
	struct list_head replay_buds;
	unsigned long long cs_sqnum;
	unsigned long long replay_sqnum;
	struct list_head unclean_leb_list;
	struct ubifs_mst_node *rcvrd_mst_node;
	struct rb_root size_tree;
	struct ubifs_mount_opts mount_opts;

#ifdef CONFIG_UBIFS_FS_DEBUG
	struct ubifs_debug_info *dbg;
#endif
};

extern struct list_head ubifs_infos;
extern spinlock_t ubifs_infos_lock;
extern atomic_long_t ubifs_clean_zn_cnt;
extern struct kmem_cache *ubifs_inode_slab;
extern const struct super_operations ubifs_super_operations;
extern const struct address_space_operations ubifs_file_address_operations;
extern const struct file_operations ubifs_file_operations;
extern const struct inode_operations ubifs_file_inode_operations;
extern const struct file_operations ubifs_dir_operations;
extern const struct inode_operations ubifs_dir_inode_operations;
extern const struct inode_operations ubifs_symlink_inode_operations;
extern struct backing_dev_info ubifs_backing_dev_info;
extern struct ubifs_compressor *ubifs_compressors[UBIFS_COMPR_TYPES_CNT];

/* io.c */
void ubifs_ro_mode(struct ubifs_info *c, int err);
int ubifs_wbuf_write_nolock(struct ubifs_wbuf *wbuf, void *buf, int len);
int ubifs_wbuf_seek_nolock(struct ubifs_wbuf *wbuf, int lnum, int offs,
			   int dtype);
int ubifs_wbuf_init(struct ubifs_info *c, struct ubifs_wbuf *wbuf);
int ubifs_read_node(const struct ubifs_info *c, void *buf, int type, int len,
		    int lnum, int offs);
int ubifs_read_node_wbuf(struct ubifs_wbuf *wbuf, void *buf, int type, int len,
			 int lnum, int offs);
int ubifs_write_node(struct ubifs_info *c, void *node, int len, int lnum,
		     int offs, int dtype);
int ubifs_check_node(const struct ubifs_info *c, const void *buf, int lnum,
		     int offs, int quiet, int must_chk_crc);
void ubifs_prepare_node(struct ubifs_info *c, void *buf, int len, int pad);
void ubifs_prep_grp_node(struct ubifs_info *c, void *node, int len, int last);
int ubifs_io_init(struct ubifs_info *c);
void ubifs_pad(const struct ubifs_info *c, void *buf, int pad);
int ubifs_wbuf_sync_nolock(struct ubifs_wbuf *wbuf);
int ubifs_bg_wbufs_sync(struct ubifs_info *c);
void ubifs_wbuf_add_ino_nolock(struct ubifs_wbuf *wbuf, ino_t inum);
int ubifs_sync_wbufs_by_inode(struct ubifs_info *c, struct inode *inode);

/* scan.c */
struct ubifs_scan_leb *ubifs_scan(const struct ubifs_info *c, int lnum,
				  int offs, void *sbuf, int quiet);
void ubifs_scan_destroy(struct ubifs_scan_leb *sleb);
int ubifs_scan_a_node(const struct ubifs_info *c, void *buf, int len, int lnum,
		      int offs, int quiet);
struct ubifs_scan_leb *ubifs_start_scan(const struct ubifs_info *c, int lnum,
					int offs, void *sbuf);
void ubifs_end_scan(const struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		    int lnum, int offs);
int ubifs_add_snod(const struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		   void *buf, int offs);
void ubifs_scanned_corruption(const struct ubifs_info *c, int lnum, int offs,
			      void *buf);

/* log.c */
void ubifs_add_bud(struct ubifs_info *c, struct ubifs_bud *bud);
void ubifs_create_buds_lists(struct ubifs_info *c);
int ubifs_add_bud_to_log(struct ubifs_info *c, int jhead, int lnum, int offs);
struct ubifs_bud *ubifs_search_bud(struct ubifs_info *c, int lnum);
struct ubifs_wbuf *ubifs_get_wbuf(struct ubifs_info *c, int lnum);
int ubifs_log_start_commit(struct ubifs_info *c, int *ltail_lnum);
int ubifs_log_end_commit(struct ubifs_info *c, int new_ltail_lnum);
int ubifs_log_post_commit(struct ubifs_info *c, int old_ltail_lnum);
int ubifs_consolidate_log(struct ubifs_info *c);

/* journal.c */
int ubifs_jnl_update(struct ubifs_info *c, const struct inode *dir,
		     const struct qstr *nm, const struct inode *inode,
		     int deletion, int xent);
int ubifs_jnl_write_data(struct ubifs_info *c, const struct inode *inode,
			 const union ubifs_key *key, const void *buf, int len);
int ubifs_jnl_write_inode(struct ubifs_info *c, const struct inode *inode);
int ubifs_jnl_delete_inode(struct ubifs_info *c, const struct inode *inode);
int ubifs_jnl_rename(struct ubifs_info *c, const struct inode *old_dir,
		     const struct dentry *old_dentry,
		     const struct inode *new_dir,
		     const struct dentry *new_dentry, int sync);
int ubifs_jnl_truncate(struct ubifs_info *c, const struct inode *inode,
		       loff_t old_size, loff_t new_size);
int ubifs_jnl_delete_xattr(struct ubifs_info *c, const struct inode *host,
			   const struct inode *inode, const struct qstr *nm);
int ubifs_jnl_change_xattr(struct ubifs_info *c, const struct inode *inode1,
			   const struct inode *inode2);

/* budget.c */
int ubifs_budget_space(struct ubifs_info *c, struct ubifs_budget_req *req);
void ubifs_release_budget(struct ubifs_info *c, struct ubifs_budget_req *req);
void ubifs_release_dirty_inode_budget(struct ubifs_info *c,
				      struct ubifs_inode *ui);
int ubifs_budget_inode_op(struct ubifs_info *c, struct inode *inode,
			  struct ubifs_budget_req *req);
void ubifs_release_ino_dirty(struct ubifs_info *c, struct inode *inode,
				struct ubifs_budget_req *req);
void ubifs_cancel_ino_op(struct ubifs_info *c, struct inode *inode,
			 struct ubifs_budget_req *req);
long long ubifs_get_free_space(struct ubifs_info *c);
long long ubifs_get_free_space_nolock(struct ubifs_info *c);
int ubifs_calc_min_idx_lebs(struct ubifs_info *c);
void ubifs_convert_page_budget(struct ubifs_info *c);
long long ubifs_reported_space(const struct ubifs_info *c, long long free);
long long ubifs_calc_available(const struct ubifs_info *c, int min_idx_lebs);

/* find.c */
int ubifs_find_free_space(struct ubifs_info *c, int min_space, int *offs,
			  int squeeze);
int ubifs_find_free_leb_for_idx(struct ubifs_info *c);
int ubifs_find_dirty_leb(struct ubifs_info *c, struct ubifs_lprops *ret_lp,
			 int min_space, int pick_free);
int ubifs_find_dirty_idx_leb(struct ubifs_info *c);
int ubifs_save_dirty_idx_lnums(struct ubifs_info *c);

/* tnc.c */
int ubifs_lookup_level0(struct ubifs_info *c, const union ubifs_key *key,
			struct ubifs_znode **zn, int *n);
int ubifs_tnc_lookup_nm(struct ubifs_info *c, const union ubifs_key *key,
			void *node, const struct qstr *nm);
int ubifs_tnc_locate(struct ubifs_info *c, const union ubifs_key *key,
		     void *node, int *lnum, int *offs);
int ubifs_tnc_add(struct ubifs_info *c, const union ubifs_key *key, int lnum,
		  int offs, int len);
int ubifs_tnc_replace(struct ubifs_info *c, const union ubifs_key *key,
		      int old_lnum, int old_offs, int lnum, int offs, int len);
int ubifs_tnc_add_nm(struct ubifs_info *c, const union ubifs_key *key,
		     int lnum, int offs, int len, const struct qstr *nm);
int ubifs_tnc_remove(struct ubifs_info *c, const union ubifs_key *key);
int ubifs_tnc_remove_nm(struct ubifs_info *c, const union ubifs_key *key,
			const struct qstr *nm);
int ubifs_tnc_remove_range(struct ubifs_info *c, union ubifs_key *from_key,
			   union ubifs_key *to_key);
int ubifs_tnc_remove_ino(struct ubifs_info *c, ino_t inum);
struct ubifs_dent_node *ubifs_tnc_next_ent(struct ubifs_info *c,
					   union ubifs_key *key,
					   const struct qstr *nm);
void ubifs_tnc_close(struct ubifs_info *c);
int ubifs_tnc_has_node(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs, int is_idx);
int ubifs_dirty_idx_node(struct ubifs_info *c, union ubifs_key *key, int level,
			 int lnum, int offs);
/* Shared by tnc.c for tnc_commit.c */
void destroy_old_idx(struct ubifs_info *c);
int is_idx_node_in_tnc(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs);
int insert_old_idx_znode(struct ubifs_info *c, struct ubifs_znode *znode);
int ubifs_tnc_get_bu_keys(struct ubifs_info *c, struct bu_info *bu);
int ubifs_tnc_bulk_read(struct ubifs_info *c, struct bu_info *bu);

/* tnc_misc.c */
struct ubifs_znode *ubifs_tnc_levelorder_next(struct ubifs_znode *zr,
					      struct ubifs_znode *znode);
int ubifs_search_zbranch(const struct ubifs_info *c,
			 const struct ubifs_znode *znode,
			 const union ubifs_key *key, int *n);
struct ubifs_znode *ubifs_tnc_postorder_first(struct ubifs_znode *znode);
struct ubifs_znode *ubifs_tnc_postorder_next(struct ubifs_znode *znode);
long ubifs_destroy_tnc_subtree(struct ubifs_znode *zr);
struct ubifs_znode *ubifs_load_znode(struct ubifs_info *c,
				     struct ubifs_zbranch *zbr,
				     struct ubifs_znode *parent, int iip);
int ubifs_tnc_read_node(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			void *node);

/* tnc_commit.c */
int ubifs_tnc_start_commit(struct ubifs_info *c, struct ubifs_zbranch *zroot);
int ubifs_tnc_end_commit(struct ubifs_info *c);

/* shrinker.c */
int ubifs_shrinker(struct shrinker *shrink, int nr_to_scan, gfp_t gfp_mask);

/* commit.c */
int ubifs_bg_thread(void *info);
void ubifs_commit_required(struct ubifs_info *c);
void ubifs_request_bg_commit(struct ubifs_info *c);
int ubifs_run_commit(struct ubifs_info *c);
void ubifs_recovery_commit(struct ubifs_info *c);
int ubifs_gc_should_commit(struct ubifs_info *c);
void ubifs_wait_for_commit(struct ubifs_info *c);

/* master.c */
int ubifs_read_master(struct ubifs_info *c);
int ubifs_write_master(struct ubifs_info *c);

/* sb.c */
int ubifs_read_superblock(struct ubifs_info *c);
struct ubifs_sb_node *ubifs_read_sb_node(struct ubifs_info *c);
int ubifs_write_sb_node(struct ubifs_info *c, struct ubifs_sb_node *sup);

/* replay.c */
int ubifs_validate_entry(struct ubifs_info *c,
			 const struct ubifs_dent_node *dent);
int ubifs_replay_journal(struct ubifs_info *c);

/* gc.c */
int ubifs_garbage_collect(struct ubifs_info *c, int anyway);
int ubifs_gc_start_commit(struct ubifs_info *c);
int ubifs_gc_end_commit(struct ubifs_info *c);
void ubifs_destroy_idx_gc(struct ubifs_info *c);
int ubifs_get_idx_gc_leb(struct ubifs_info *c);
int ubifs_garbage_collect_leb(struct ubifs_info *c, struct ubifs_lprops *lp);

/* orphan.c */
int ubifs_add_orphan(struct ubifs_info *c, ino_t inum);
void ubifs_delete_orphan(struct ubifs_info *c, ino_t inum);
int ubifs_orphan_start_commit(struct ubifs_info *c);
int ubifs_orphan_end_commit(struct ubifs_info *c);
int ubifs_mount_orphans(struct ubifs_info *c, int unclean, int read_only);
int ubifs_clear_orphans(struct ubifs_info *c);

/* lpt.c */
int ubifs_calc_lpt_geom(struct ubifs_info *c);
int ubifs_create_dflt_lpt(struct ubifs_info *c, int *main_lebs, int lpt_first,
			  int *lpt_lebs, int *big_lpt);
int ubifs_lpt_init(struct ubifs_info *c, int rd, int wr);
struct ubifs_lprops *ubifs_lpt_lookup(struct ubifs_info *c, int lnum);
struct ubifs_lprops *ubifs_lpt_lookup_dirty(struct ubifs_info *c, int lnum);
int ubifs_lpt_scan_nolock(struct ubifs_info *c, int start_lnum, int end_lnum,
			  ubifs_lpt_scan_callback scan_cb, void *data);

/* Shared by lpt.c for lpt_commit.c */
void ubifs_pack_lsave(struct ubifs_info *c, void *buf, int *lsave);
void ubifs_pack_ltab(struct ubifs_info *c, void *buf,
		     struct ubifs_lpt_lprops *ltab);
void ubifs_pack_pnode(struct ubifs_info *c, void *buf,
		      struct ubifs_pnode *pnode);
void ubifs_pack_nnode(struct ubifs_info *c, void *buf,
		      struct ubifs_nnode *nnode);
struct ubifs_pnode *ubifs_get_pnode(struct ubifs_info *c,
				    struct ubifs_nnode *parent, int iip);
struct ubifs_nnode *ubifs_get_nnode(struct ubifs_info *c,
				    struct ubifs_nnode *parent, int iip);
int ubifs_read_nnode(struct ubifs_info *c, struct ubifs_nnode *parent, int iip);
void ubifs_add_lpt_dirt(struct ubifs_info *c, int lnum, int dirty);
void ubifs_add_nnode_dirt(struct ubifs_info *c, struct ubifs_nnode *nnode);
uint32_t ubifs_unpack_bits(uint8_t **addr, int *pos, int nrbits);
struct ubifs_nnode *ubifs_first_nnode(struct ubifs_info *c, int *hght);
/* Needed only in debugging code in lpt_commit.c */
int ubifs_unpack_nnode(const struct ubifs_info *c, void *buf,
		       struct ubifs_nnode *nnode);

/* lpt_commit.c */
int ubifs_lpt_start_commit(struct ubifs_info *c);
int ubifs_lpt_end_commit(struct ubifs_info *c);
int ubifs_lpt_post_commit(struct ubifs_info *c);
void ubifs_lpt_free(struct ubifs_info *c, int wr_only);

/* lprops.c */
const struct ubifs_lprops *ubifs_change_lp(struct ubifs_info *c,
					   const struct ubifs_lprops *lp,
					   int free, int dirty, int flags,
					   int idx_gc_cnt);
void ubifs_get_lp_stats(struct ubifs_info *c, struct ubifs_lp_stats *lst);
void ubifs_add_to_cat(struct ubifs_info *c, struct ubifs_lprops *lprops,
		      int cat);
void ubifs_replace_cat(struct ubifs_info *c, struct ubifs_lprops *old_lprops,
		       struct ubifs_lprops *new_lprops);
void ubifs_ensure_cat(struct ubifs_info *c, struct ubifs_lprops *lprops);
int ubifs_categorize_lprops(const struct ubifs_info *c,
			    const struct ubifs_lprops *lprops);
int ubifs_change_one_lp(struct ubifs_info *c, int lnum, int free, int dirty,
			int flags_set, int flags_clean, int idx_gc_cnt);
int ubifs_update_one_lp(struct ubifs_info *c, int lnum, int free, int dirty,
			int flags_set, int flags_clean);
int ubifs_read_one_lp(struct ubifs_info *c, int lnum, struct ubifs_lprops *lp);
const struct ubifs_lprops *ubifs_fast_find_free(struct ubifs_info *c);
const struct ubifs_lprops *ubifs_fast_find_empty(struct ubifs_info *c);
const struct ubifs_lprops *ubifs_fast_find_freeable(struct ubifs_info *c);
const struct ubifs_lprops *ubifs_fast_find_frdi_idx(struct ubifs_info *c);
int ubifs_calc_dark(const struct ubifs_info *c, int spc);

/* file.c */
int ubifs_fsync(struct file *file, int datasync);
int ubifs_setattr(struct dentry *dentry, struct iattr *attr);

/* dir.c */
struct inode *ubifs_new_inode(struct ubifs_info *c, const struct inode *dir,
			      int mode);
int ubifs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		  struct kstat *stat);

/* xattr.c */
int ubifs_setxattr(struct dentry *dentry, const char *name,
		   const void *value, size_t size, int flags);
ssize_t ubifs_getxattr(struct dentry *dentry, const char *name, void *buf,
		       size_t size);
ssize_t ubifs_listxattr(struct dentry *dentry, char *buffer, size_t size);
int ubifs_removexattr(struct dentry *dentry, const char *name);

/* super.c */
struct inode *ubifs_iget(struct super_block *sb, unsigned long inum);

/* recovery.c */
int ubifs_recover_master_node(struct ubifs_info *c);
int ubifs_write_rcvrd_mst_node(struct ubifs_info *c);
struct ubifs_scan_leb *ubifs_recover_leb(struct ubifs_info *c, int lnum,
					 int offs, void *sbuf, int grouped);
struct ubifs_scan_leb *ubifs_recover_log_leb(struct ubifs_info *c, int lnum,
					     int offs, void *sbuf);
int ubifs_recover_inl_heads(const struct ubifs_info *c, void *sbuf);
int ubifs_clean_lebs(const struct ubifs_info *c, void *sbuf);
int ubifs_rcvry_gc_commit(struct ubifs_info *c);
int ubifs_recover_size_accum(struct ubifs_info *c, union ubifs_key *key,
			     int deletion, loff_t new_size);
int ubifs_recover_size(struct ubifs_info *c);
void ubifs_destroy_size_tree(struct ubifs_info *c);

/* ioctl.c */
long ubifs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
void ubifs_set_inode_flags(struct inode *inode);
#ifdef CONFIG_COMPAT
long ubifs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#endif

/* compressor.c */
int __init ubifs_compressors_init(void);
void ubifs_compressors_exit(void);
void ubifs_compress(const void *in_buf, int in_len, void *out_buf, int *out_len,
		    int *compr_type);
int ubifs_decompress(const void *buf, int len, void *out, int *out_len,
		     int compr_type);

#include "debug.h"
#include "misc.h"
#include "key.h"

#endif /* !__UBIFS_H__ */
