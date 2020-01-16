/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation
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
#include <linux/security.h>
#include <linux/xattr.h>
#include <linux/random.h>
#include <crypto/hash_info.h>
#include <crypto/hash.h>
#include <crypto/algapi.h>

#include <linux/fscrypt.h>

#include "ubifs-media.h"

/* Version of this UBIFS implementation */
#define UBIFS_VERSION 1

/* UBIFS file system VFS magic number */
#define UBIFS_SUPER_MAGIC 0x24051905

/* Number of UBIFS blocks per VFS page */
#define UBIFS_BLOCKS_PER_PAGE (PAGE_SIZE / UBIFS_BLOCK_SIZE)
#define UBIFS_BLOCKS_PER_PAGE_SHIFT (PAGE_SHIFT - UBIFS_BLOCK_SHIFT)

/* "File system end of life" sequence number watermark */
#define SQNUM_WARN_WATERMARK 0xFFFFFFFF00000000ULL
#define SQNUM_WATERMARK      0xFFFFFFFFFF000000ULL

/*
 * Minimum amount of LEBs reserved for the index. At present the index needs at
 * least 2 LEBs: one for the index head and one for in-the-gaps method (which
 * currently does yest cater for the index head and so excludes it from
 * consideration).
 */
#define MIN_INDEX_LEBS 2

/* Minimum amount of data UBIFS writes to the flash */
#define MIN_WRITE_SZ (UBIFS_DATA_NODE_SZ + 8)

/*
 * Currently we do yest support iyesde number overlapping and re-using, so this
 * watermark defines dangerous iyesde number level. This should be fixed later,
 * although it is difficult to exceed current limit. Ayesther option is to use
 * 64-bit iyesde numbers, but this means more overhead.
 */
#define INUM_WARN_WATERMARK 0xFFF00000
#define INUM_WATERMARK      0xFFFFFF00

/* Maximum number of entries in each LPT (LEB category) heap */
#define LPT_HEAP_SZ 256

/*
 * Background thread name pattern. The numbers are UBI device and volume
 * numbers.
 */
#define BGT_NAME_PATTERN "ubifs_bgt%d_%d"

/* Maximum possible iyesde number (only 32-bit iyesdes are supported yesw) */
#define MAX_INUM 0xFFFFFFFF

/* Number of yesn-data journal heads */
#define NONDATA_JHEADS_CNT 2

/* Shorter names for journal head numbers for internal usage */
#define GCHD   UBIFS_GC_HEAD
#define BASEHD UBIFS_BASE_HEAD
#define DATAHD UBIFS_DATA_HEAD

/* 'No change' value for 'ubifs_change_lp()' */
#define LPROPS_NC 0x80000001

/*
 * There is yes yestion of truncation key because truncation yesdes do yest exist
 * in TNC. However, when replaying, it is handy to introduce fake "truncation"
 * keys for truncation yesdes because the code becomes simpler. So we define
 * %UBIFS_TRUN_KEY type.
 *
 * But otherwise, out of the journal reply scope, the truncation keys are
 * invalid.
 */
#define UBIFS_TRUN_KEY    UBIFS_KEY_TYPES_CNT
#define UBIFS_INVALID_KEY UBIFS_KEY_TYPES_CNT

/*
 * How much a directory entry/extended attribute entry adds to the parent/host
 * iyesde.
 */
#define CALC_DENT_SIZE(name_len) ALIGN(UBIFS_DENT_NODE_SZ + (name_len) + 1, 8)

/* How much an extended attribute adds to the host iyesde */
#define CALC_XATTR_BYTES(data_len) ALIGN(UBIFS_INO_NODE_SZ + (data_len) + 1, 8)

/*
 * Zyesdes which were yest touched for 'OLD_ZNODE_AGE' seconds are considered
 * "old", and zyesde which were touched last 'YOUNG_ZNODE_AGE' seconds ago are
 * considered "young". This is used by shrinker when selecting zyesde to trim
 * off.
 */
#define OLD_ZNODE_AGE 20
#define YOUNG_ZNODE_AGE 5

/*
 * Some compressors, like LZO, may end up with more data then the input buffer.
 * So UBIFS always allocates larger output buffer, to be sure the compressor
 * will yest corrupt memory in case of worst case compression.
 */
#define WORST_COMPR_FACTOR 2

#ifdef CONFIG_FS_ENCRYPTION
#define UBIFS_CIPHER_BLOCK_SIZE FS_CRYPTO_BLOCK_SIZE
#else
#define UBIFS_CIPHER_BLOCK_SIZE 0
#endif

/*
 * How much memory is needed for a buffer where we compress a data yesde.
 */
#define COMPRESSED_DATA_NODE_BUF_SZ \
	(UBIFS_DATA_NODE_SZ + UBIFS_BLOCK_SIZE * WORST_COMPR_FACTOR)

/* Maximum expected tree height for use by bottom_up_buf */
#define BOTTOM_UP_HEIGHT 64

/* Maximum number of data yesdes to bulk-read */
#define UBIFS_MAX_BULK_READ 32

#ifdef CONFIG_UBIFS_FS_AUTHENTICATION
#define UBIFS_HASH_ARR_SZ UBIFS_MAX_HASH_LEN
#define UBIFS_HMAC_ARR_SZ UBIFS_MAX_HMAC_LEN
#else
#define UBIFS_HASH_ARR_SZ 0
#define UBIFS_HMAC_ARR_SZ 0
#endif

/*
 * Lockdep classes for UBIFS iyesde @ui_mutex.
 */
enum {
	WB_MUTEX_1 = 0,
	WB_MUTEX_2 = 1,
	WB_MUTEX_3 = 2,
	WB_MUTEX_4 = 3,
};

/*
 * Zyesde flags (actually, bit numbers which store the flags).
 *
 * DIRTY_ZNODE: zyesde is dirty
 * COW_ZNODE: zyesde is being committed and a new instance of this zyesde has to
 *            be created before changing this zyesde
 * OBSOLETE_ZNODE: zyesde is obsolete, which means it was deleted, but it is
 *                 still in the commit list and the ongoing commit operation
 *                 will commit it, and delete this zyesde after it is done
 */
enum {
	DIRTY_ZNODE    = 0,
	COW_ZNODE      = 1,
	OBSOLETE_ZNODE = 2,
};

/*
 * Commit states.
 *
 * COMMIT_RESTING: commit is yest wanted
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
 * 'ubifs_scan_a_yesde()' return values.
 *
 * SCANNED_GARBAGE:  scanned garbage
 * SCANNED_EMPTY_SPACE: scanned empty space
 * SCANNED_A_NODE: scanned a valid yesde
 * SCANNED_A_CORRUPT_NODE: scanned a corrupted yesde
 * SCANNED_A_BAD_PAD_NODE: scanned a padding yesde with invalid pad length
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
 * LPT cyesde flag bits.
 *
 * DIRTY_CNODE: cyesde is dirty
 * OBSOLETE_CNODE: cyesde is being committed and has been copied (or deleted),
 *                 so it can (and must) be freed when the commit is finished
 * COW_CNODE: cyesde is being committed and must be copied before writing
 */
enum {
	DIRTY_CNODE    = 0,
	OBSOLETE_CNODE = 1,
	COW_CNODE      = 2,
};

/*
 * Dirty flag bits (lpt_drty_flgs) for LPT special yesdes.
 *
 * LTAB_DIRTY: ltab yesde is dirty
 * LSAVE_DIRTY: lsave yesde is dirty
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

/*
 * Action taken upon a failed ubifs_assert().
 * @ASSACT_REPORT: just report the failed assertion
 * @ASSACT_RO: switch to read-only mode
 * @ASSACT_PANIC: call BUG() and possible panic the kernel
 */
enum {
	ASSACT_REPORT = 0,
	ASSACT_RO,
	ASSACT_PANIC,
};

/**
 * struct ubifs_old_idx - index yesde obsoleted since last commit start.
 * @rb: rb-tree yesde
 * @lnum: LEB number of obsoleted index yesde
 * @offs: offset of obsoleted index yesde
 */
struct ubifs_old_idx {
	struct rb_yesde rb;
	int lnum;
	int offs;
};

/* The below union makes it easier to deal with keys */
union ubifs_key {
	uint8_t u8[UBIFS_SK_LEN];
	uint32_t u32[UBIFS_SK_LEN/4];
	uint64_t u64[UBIFS_SK_LEN/8];
	__le32 j32[UBIFS_SK_LEN/4];
};

/**
 * struct ubifs_scan_yesde - UBIFS scanned yesde information.
 * @list: list of scanned yesdes
 * @key: key of yesde scanned (if it has one)
 * @sqnum: sequence number
 * @type: type of yesde scanned
 * @offs: offset with LEB of yesde scanned
 * @len: length of yesde scanned
 * @yesde: raw yesde
 */
struct ubifs_scan_yesde {
	struct list_head list;
	union ubifs_key key;
	unsigned long long sqnum;
	int type;
	int offs;
	int len;
	void *yesde;
};

/**
 * struct ubifs_scan_leb - UBIFS scanned LEB information.
 * @lnum: logical eraseblock number
 * @yesdes_cnt: number of yesdes scanned
 * @yesdes: list of struct ubifs_scan_yesde
 * @endpt: end point (and therefore the start of empty space)
 * @buf: buffer containing entire LEB scanned
 */
struct ubifs_scan_leb {
	int lnum;
	int yesdes_cnt;
	struct list_head yesdes;
	int endpt;
	void *buf;
};

/**
 * struct ubifs_gced_idx_leb - garbage-collected indexing LEB.
 * @list: list
 * @lnum: LEB number
 * @unmap: OK to unmap this LEB
 *
 * This data structure is used to temporary store garbage-collected indexing
 * LEBs - they are yest released immediately, but only after the next commit.
 * This is needed to guarantee recoverability.
 */
struct ubifs_gced_idx_leb {
	struct list_head list;
	int lnum;
	int unmap;
};

/**
 * struct ubifs_iyesde - UBIFS in-memory iyesde description.
 * @vfs_iyesde: VFS iyesde description object
 * @creat_sqnum: sequence number at time of creation
 * @del_cmtyes: commit number corresponding to the time the iyesde was deleted,
 *             protected by @c->commit_sem;
 * @xattr_size: summarized size of all extended attributes in bytes
 * @xattr_cnt: count of extended attributes this iyesde has
 * @xattr_names: sum of lengths of all extended attribute names belonging to
 *               this iyesde
 * @dirty: yesn-zero if the iyesde is dirty
 * @xattr: yesn-zero if this is an extended attribute iyesde
 * @bulk_read: yesn-zero if bulk-read should be used
 * @ui_mutex: serializes iyesde write-back with the rest of VFS operations,
 *            serializes "clean <-> dirty" state changes, serializes bulk-read,
 *            protects @dirty, @bulk_read, @ui_size, and @xattr_size
 * @ui_lock: protects @synced_i_size
 * @synced_i_size: synchronized size of iyesde, i.e. the value of iyesde size
 *                 currently stored on the flash; used only for regular file
 *                 iyesdes
 * @ui_size: iyesde size used by UBIFS when writing to flash
 * @flags: iyesde flags (@UBIFS_COMPR_FL, etc)
 * @compr_type: default compression type used for this iyesde
 * @last_page_read: page number of last page read (for bulk read)
 * @read_in_a_row: number of consecutive pages read in a row (for bulk read)
 * @data_len: length of the data attached to the iyesde
 * @data: iyesde's data
 *
 * @ui_mutex exists for two main reasons. At first it prevents iyesdes from
 * being written back while UBIFS changing them, being in the middle of an VFS
 * operation. This way UBIFS makes sure the iyesde fields are consistent. For
 * example, in 'ubifs_rename()' we change 3 iyesdes simultaneously, and
 * write-back must yest write any of them before we have finished.
 *
 * The second reason is budgeting - UBIFS has to budget all operations. If an
 * operation is going to mark an iyesde dirty, it has to allocate budget for
 * this. It canyest just mark it dirty because there is yes guarantee there will
 * be eyesugh flash space to write the iyesde back later. This means UBIFS has
 * to have full control over iyesde "clean <-> dirty" transitions (and pages
 * actually). But unfortunately, VFS marks iyesdes dirty in many places, and it
 * does yest ask the file-system if it is allowed to do so (there is a yestifier,
 * but it is yest eyesugh), i.e., there is yes mechanism to synchronize with this.
 * So UBIFS has its own iyesde dirty flag and its own mutex to serialize
 * "clean <-> dirty" transitions.
 *
 * The @synced_i_size field is used to make sure we never write pages which are
 * beyond last synchronized iyesde size. See 'ubifs_writepage()' for more
 * information.
 *
 * The @ui_size is a "shadow" variable for @iyesde->i_size and UBIFS uses
 * @ui_size instead of @iyesde->i_size. The reason for this is that UBIFS canyest
 * make sure @iyesde->i_size is always changed under @ui_mutex, because it
 * canyest call 'truncate_setsize()' with @ui_mutex locked, because it would
 * deadlock with 'ubifs_writepage()' (see file.c). All the other iyesde fields
 * are changed under @ui_mutex, so they do yest need "shadow" fields. Note, one
 * could consider to rework locking and base it on "shadow" fields.
 */
struct ubifs_iyesde {
	struct iyesde vfs_iyesde;
	unsigned long long creat_sqnum;
	unsigned long long del_cmtyes;
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
 * cleaned but was yest because UBIFS was mounted read-only. The information
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
 * LPROPS_UNCAT: yest categorized
 * LPROPS_DIRTY: dirty > free, dirty >= @c->dead_wm, yest index
 * LPROPS_DIRTY_IDX: dirty + free > @c->min_idx_yesde_sze and index
 * LPROPS_FREE: free > 0, dirty < @c->dead_wm, yest empty, yest index
 * LPROPS_HEAP_CNT: number of heaps used for storing categorized LEBs
 * LPROPS_EMPTY: LEB is empty, yest taken
 * LPROPS_FREEABLE: free + dirty == leb_size, yest index, yest taken
 * LPROPS_FRDI_IDX: free + dirty == leb_size and index, may be taken
 * LPROPS_CAT_MASK: mask for the LEB categories above
 * LPROPS_TAKEN: LEB was taken (this flag is yest saved on the media)
 * LPROPS_INDEX: LEB contains indexing yesdes (this flag also exists on flash)
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
 * @total_used: total used space in bytes (does yest include index LEBs)
 * @total_dead: total dead space in bytes (does yest include index LEBs)
 * @total_dark: total dark space in bytes (does yest include index LEBs)
 *
 * The @taken_empty_lebs field counts the LEBs that are in the transient state
 * of having been "taken" for use but yest yet written to. @taken_empty_lebs is
 * needed to account correctly for @gc_lnum, otherwise @empty_lebs could be
 * used by itself (in which case 'unused_lebs' would be a better name). In the
 * case of @gc_lnum, it is "taken" at mount time or whenever a LEB is retained
 * by GC, but unlike other empty LEBs that are "taken", it may yest be written
 * straight away (i.e. before the next commit start or unmount), so either
 * @gc_lnum must be specially accounted for, or the current approach followed
 * i.e. count it under @taken_empty_lebs.
 *
 * @empty_lebs includes @taken_empty_lebs.
 *
 * @total_used, @total_dead and @total_dark fields do yest account indexing
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

struct ubifs_nyesde;

/**
 * struct ubifs_cyesde - LEB Properties Tree common yesde.
 * @parent: parent nyesde
 * @cnext: next cyesde to commit
 * @flags: flags (%DIRTY_LPT_NODE or %OBSOLETE_LPT_NODE)
 * @iip: index in parent
 * @level: level in the tree (zero for pyesdes, greater than zero for nyesdes)
 * @num: yesde number
 */
struct ubifs_cyesde {
	struct ubifs_nyesde *parent;
	struct ubifs_cyesde *cnext;
	unsigned long flags;
	int iip;
	int level;
	int num;
};

/**
 * struct ubifs_pyesde - LEB Properties Tree leaf yesde.
 * @parent: parent nyesde
 * @cnext: next cyesde to commit
 * @flags: flags (%DIRTY_LPT_NODE or %OBSOLETE_LPT_NODE)
 * @iip: index in parent
 * @level: level in the tree (always zero for pyesdes)
 * @num: yesde number
 * @lprops: LEB properties array
 */
struct ubifs_pyesde {
	struct ubifs_nyesde *parent;
	struct ubifs_cyesde *cnext;
	unsigned long flags;
	int iip;
	int level;
	int num;
	struct ubifs_lprops lprops[UBIFS_LPT_FANOUT];
};

/**
 * struct ubifs_nbranch - LEB Properties Tree internal yesde branch.
 * @lnum: LEB number of child
 * @offs: offset of child
 * @nyesde: nyesde child
 * @pyesde: pyesde child
 * @cyesde: cyesde child
 */
struct ubifs_nbranch {
	int lnum;
	int offs;
	union {
		struct ubifs_nyesde *nyesde;
		struct ubifs_pyesde *pyesde;
		struct ubifs_cyesde *cyesde;
	};
};

/**
 * struct ubifs_nyesde - LEB Properties Tree internal yesde.
 * @parent: parent nyesde
 * @cnext: next cyesde to commit
 * @flags: flags (%DIRTY_LPT_NODE or %OBSOLETE_LPT_NODE)
 * @iip: index in parent
 * @level: level in the tree (always greater than zero for nyesdes)
 * @num: yesde number
 * @nbranch: branches to child yesdes
 */
struct ubifs_nyesde {
	struct ubifs_nyesde *parent;
	struct ubifs_cyesde *cnext;
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

/* Callback used by the 'ubifs_lpt_scan_yeslock()' function */
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
 * @jhead: journal head the mutex belongs to (yeste, needed only to shut lockdep
 *         up by 'mutex_lock_nested()).
 * @sync_callback: write-buffer synchronization callback
 * @io_mutex: serializes write-buffer I/O
 * @lock: serializes @buf, @lnum, @offs, @avail, @used, @next_iyes and @iyesdes
 *        fields
 * @timer: write-buffer timer
 * @yes_timer: yesn-zero if this write-buffer does yest have a timer
 * @need_sync: yesn-zero if the timer expired and the wbuf needs sync'ing
 * @next_iyes: points to the next position of the following iyesde number
 * @iyesdes: stores the iyesde numbers of the yesdes which are in wbuf
 *
 * The write-buffer synchronization callback is called when the write-buffer is
 * synchronized in order to yestify how much space was wasted due to
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
	int jhead;
	int (*sync_callback)(struct ubifs_info *c, int lnum, int free, int pad);
	struct mutex io_mutex;
	spinlock_t lock;
	struct hrtimer timer;
	unsigned int yes_timer:1;
	unsigned int need_sync:1;
	int next_iyes;
	iyes_t *iyesdes;
};

/**
 * struct ubifs_bud - bud logical eraseblock.
 * @lnum: logical eraseblock number
 * @start: where the (uncommitted) bud data starts
 * @jhead: journal head number this bud belongs to
 * @list: link in the list buds belonging to the same journal head
 * @rb: link in the tree of all buds
 * @log_hash: the log hash from the commit start yesde up to this bud
 */
struct ubifs_bud {
	int lnum;
	int start;
	int jhead;
	struct list_head list;
	struct rb_yesde rb;
	struct shash_desc *log_hash;
};

/**
 * struct ubifs_jhead - journal head.
 * @wbuf: head's write-buffer
 * @buds_list: list of bud LEBs belonging to this journal head
 * @grouped: yesn-zero if UBIFS groups yesdes when writing to this journal head
 * @log_hash: the log hash from the commit start yesde up to this journal head
 *
 * Note, the @buds list is protected by the @c->buds_lock.
 */
struct ubifs_jhead {
	struct ubifs_wbuf wbuf;
	struct list_head buds_list;
	unsigned int grouped:1;
	struct shash_desc *log_hash;
};

/**
 * struct ubifs_zbranch - key/coordinate/length branch stored in zyesdes.
 * @key: key
 * @zyesde: zyesde address in memory
 * @lnum: LEB number of the target yesde (indexing yesde or data yesde)
 * @offs: target yesde offset within @lnum
 * @len: target yesde length
 * @hash: the hash of the target yesde
 */
struct ubifs_zbranch {
	union ubifs_key key;
	union {
		struct ubifs_zyesde *zyesde;
		void *leaf;
	};
	int lnum;
	int offs;
	int len;
	u8 hash[UBIFS_HASH_ARR_SZ];
};

/**
 * struct ubifs_zyesde - in-memory representation of an indexing yesde.
 * @parent: parent zyesde or NULL if it is the root
 * @cnext: next zyesde to commit
 * @cparent: parent yesde for this commit
 * @ciip: index in cparent's zbranch array
 * @flags: zyesde flags (%DIRTY_ZNODE, %COW_ZNODE or %OBSOLETE_ZNODE)
 * @time: last access time (seconds)
 * @level: level of the entry in the TNC tree
 * @child_cnt: count of child zyesdes
 * @iip: index in parent's zbranch array
 * @alt: lower bound of key range has altered i.e. child inserted at slot 0
 * @lnum: LEB number of the corresponding indexing yesde
 * @offs: offset of the corresponding indexing yesde
 * @len: length  of the corresponding indexing yesde
 * @zbranch: array of zyesde branches (@c->fayesut elements)
 *
 * Note! The @lnum, @offs, and @len fields are yest really needed - we have them
 * only for internal consistency check. They could be removed to save some RAM.
 */
struct ubifs_zyesde {
	struct ubifs_zyesde *parent;
	struct ubifs_zyesde *cnext;
	struct ubifs_zyesde *cparent;
	int ciip;
	unsigned long flags;
	time64_t time;
	int level;
	int child_cnt;
	int iip;
	int alt;
	int lnum;
	int offs;
	int len;
	struct ubifs_zbranch zbranch[];
};

/**
 * struct bu_info - bulk-read information.
 * @key: first data yesde key
 * @zbranch: zbranches of data yesdes to bulk read
 * @buf: buffer to read into
 * @buf_len: buffer length
 * @gc_seq: GC sequence number to detect races with GC
 * @cnt: number of data yesdes for bulk read
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
 * struct ubifs_yesde_range - yesde length range description data structure.
 * @len: fixed yesde length
 * @min_len: minimum possible yesde length
 * @max_len: maximum possible yesde length
 *
 * If @max_len is %0, the yesde has fixed length @len.
 */
struct ubifs_yesde_range {
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
 * @fast: yesn-zero if the budgeting should try to acquire budget quickly and
 *        should yest try to call write-back
 * @recalculate: yesn-zero if @idx_growth, @data_growth, and @dd_growth fields
 *               have to be re-calculated
 * @new_page: yesn-zero if the operation adds a new page
 * @dirtied_page: yesn-zero if the operation makes a page dirty
 * @new_dent: yesn-zero if the operation adds a new directory entry
 * @mod_dent: yesn-zero if the operation removes or modifies an existing
 *            directory entry
 * @new_iyes: yesn-zero if the operation adds a new iyesde
 * @new_iyes_d: how much data newly created iyesde contains
 * @dirtied_iyes: how many iyesdes the operation makes dirty
 * @dirtied_iyes_d: how much data dirtied iyesde contains
 * @idx_growth: how much the index will supposedly grow
 * @data_growth: how much new data the operation will supposedly add
 * @dd_growth: how much data that makes other data dirty the operation will
 *             supposedly add
 *
 * @idx_growth, @data_growth and @dd_growth are yest used in budget request. The
 * budgeting subsystem caches index and data growth values there to avoid
 * re-calculating them when the budget is released. However, if @idx_growth is
 * %-1, it is calculated by the release function using other fields.
 *
 * An iyesde may contain 4KiB of data at max., thus the widths of @new_iyes_d
 * is 13 bits, and @dirtied_iyes_d - 15, because up to 4 iyesdes may be made
 * dirty by the re-name operation.
 *
 * Note, UBIFS aligns yesde lengths to 8-bytes boundary, so the requester has to
 * make sure the amount of iyesde data which contribute to @new_iyes_d and
 * @dirtied_iyes_d fields are aligned.
 */
struct ubifs_budget_req {
	unsigned int fast:1;
	unsigned int recalculate:1;
#ifndef UBIFS_DEBUG
	unsigned int new_page:1;
	unsigned int dirtied_page:1;
	unsigned int new_dent:1;
	unsigned int mod_dent:1;
	unsigned int new_iyes:1;
	unsigned int new_iyes_d:13;
	unsigned int dirtied_iyes:4;
	unsigned int dirtied_iyes_d:15;
#else
	/* Not bit-fields to check for overflows */
	unsigned int new_page;
	unsigned int dirtied_page;
	unsigned int new_dent;
	unsigned int mod_dent;
	unsigned int new_iyes;
	unsigned int new_iyes_d;
	unsigned int dirtied_iyes;
	unsigned int dirtied_iyes_d;
#endif
	int idx_growth;
	int data_growth;
	int dd_growth;
};

/**
 * struct ubifs_orphan - stores the iyesde number of an orphan.
 * @rb: rb-tree yesde of rb-tree of orphans sorted by iyesde number
 * @list: list head of list of orphans in order added
 * @new_list: list head of list of orphans added since the last commit
 * @child_list: list of xattr childs if this orphan hosts xattrs, list head
 * if this orphan is a xattr, yest used otherwise.
 * @cnext: next orphan to commit
 * @dnext: next orphan to delete
 * @inum: iyesde number
 * @new: %1 => added since the last commit, otherwise %0
 * @cmt: %1 => commit pending, otherwise %0
 * @del: %1 => delete pending, otherwise %0
 */
struct ubifs_orphan {
	struct rb_yesde rb;
	struct list_head list;
	struct list_head new_list;
	struct list_head child_list;
	struct ubifs_orphan *cnext;
	struct ubifs_orphan *dnext;
	iyes_t inum;
	unsigned new:1;
	unsigned cmt:1;
	unsigned del:1;
};

/**
 * struct ubifs_mount_opts - UBIFS-specific mount options information.
 * @unmount_mode: selected unmount mode (%0 default, %1 yesrmal, %2 fast)
 * @bulk_read: enable/disable bulk-reads (%0 default, %1 disable, %2 enable)
 * @chk_data_crc: enable/disable CRC data checking when reading data yesdes
 *                (%0 default, %1 disable, %2 enable)
 * @override_compr: override default compressor (%0 - do yest override and use
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

/**
 * struct ubifs_budg_info - UBIFS budgeting information.
 * @idx_growth: amount of bytes budgeted for index growth
 * @data_growth: amount of bytes budgeted for cached data
 * @dd_growth: amount of bytes budgeted for cached data that will make
 *             other data dirty
 * @uncommitted_idx: amount of bytes were budgeted for growth of the index, but
 *                   which still have to be taken into account because the index
 *                   has yest been committed so far
 * @old_idx_sz: size of index on flash
 * @min_idx_lebs: minimum number of LEBs required for the index
 * @yesspace: yesn-zero if the file-system does yest have flash space (used as
 *           optimization)
 * @yesspace_rp: the same as @yesspace, but additionally means that even reserved
 *              pool is full
 * @page_budget: budget for a page (constant, never changed after mount)
 * @iyesde_budget: budget for an iyesde (constant, never changed after mount)
 * @dent_budget: budget for a directory entry (constant, never changed after
 *               mount)
 */
struct ubifs_budg_info {
	long long idx_growth;
	long long data_growth;
	long long dd_growth;
	long long uncommitted_idx;
	unsigned long long old_idx_sz;
	int min_idx_lebs;
	unsigned int yesspace:1;
	unsigned int yesspace_rp:1;
	int page_budget;
	int iyesde_budget;
	int dent_budget;
};

struct ubifs_debug_info;

/**
 * struct ubifs_info - UBIFS file-system description data structure
 * (per-superblock).
 * @vfs_sb: VFS @struct super_block object
 * @sup_yesde: The super block yesde as read from the device
 *
 * @highest_inum: highest used iyesde number
 * @max_sqnum: current global sequence number
 * @cmt_yes: commit number of the last successfully completed commit, protected
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
 * @space_fixup: flag indicating that free space in LEBs needs to be cleaned up
 * @double_hash: flag indicating that we can do lookups by hash
 * @encrypted: flag indicating that this file system contains encrypted files
 * @yes_chk_data_crc: do yest check CRCs when reading data yesdes (except during
 *                   recovery)
 * @bulk_read: enable bulk-reads
 * @default_compr: default compression algorithm (%UBIFS_COMPR_LZO, etc)
 * @rw_incompat: the media is yest R/W compatible
 * @assert_action: action to take when a ubifs_assert() fails
 * @authenticated: flag indigating the FS is mounted in authenticated mode
 *
 * @tnc_mutex: protects the Tree Node Cache (TNC), @zroot, @cnext, @enext, and
 *             @calc_idx_sz
 * @zroot: zbranch which points to the root index yesde and zyesde
 * @cnext: next zyesde to commit
 * @enext: next zyesde to commit to empty space
 * @gap_lebs: array of LEBs used by the in-gaps commit method
 * @cbuf: commit buffer
 * @ileb_buf: buffer for commit in-the-gaps method
 * @ileb_len: length of data in ileb_buf
 * @ihead_lnum: LEB number of index head
 * @ihead_offs: offset of index head
 * @ilebs: pre-allocated index LEBs
 * @ileb_cnt: number of pre-allocated index LEBs
 * @ileb_nxt: next pre-allocated index LEBs
 * @old_idx: tree of index yesdes obsoleted since the last commit start
 * @bottom_up_buf: a buffer which is used by 'dirty_cow_bottom_up()' in tnc.c
 *
 * @mst_yesde: master yesde
 * @mst_offs: offset of valid master yesde
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
 * @hash_len: The length of the index yesde hashes
 * @fayesut: fayesut of the index tree (number of links per indexing yesde)
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
 *                used to store indexing yesdes (@leb_size - @max_idx_yesde_sz)
 * @leb_cnt: count of logical eraseblocks
 * @max_leb_cnt: maximum count of logical eraseblocks
 * @ro_media: the underlying UBI volume is read-only
 * @ro_mount: the file-system was mounted as read-only
 * @ro_error: UBIFS switched to R/O mode because an error happened
 *
 * @dirty_pg_cnt: number of dirty pages (yest used)
 * @dirty_zn_cnt: number of dirty zyesdes
 * @clean_zn_cnt: number of clean zyesdes
 *
 * @space_lock: protects @bi and @lst
 * @lst: lprops statistics
 * @bi: budgeting information
 * @calc_idx_sz: temporary variable which is used to calculate new index size
 *               (contains accurate new index size at end of TNC commit start)
 *
 * @ref_yesde_alsz: size of the LEB reference yesde aligned to the min. flash
 *                 I/O unit
 * @mst_yesde_alsz: master yesde aligned size
 * @min_idx_yesde_sz: minimum indexing yesde aligned on 8-bytes boundary
 * @max_idx_yesde_sz: maximum indexing yesde aligned on 8-bytes boundary
 * @max_iyesde_sz: maximum possible iyesde size in bytes
 * @max_zyesde_sz: size of zyesde in bytes
 *
 * @leb_overhead: how many bytes are wasted in an LEB when it is filled with
 *                data yesdes of maximum size - used in free space reporting
 * @dead_wm: LEB dead space watermark
 * @dark_wm: LEB dark space watermark
 * @block_cnt: count of 4KiB blocks on the FS
 *
 * @ranges: UBIFS yesde length ranges
 * @ubi: UBI volume descriptor
 * @di: UBI device information
 * @vi: UBI volume information
 *
 * @orph_tree: rb-tree of orphan iyesde numbers
 * @orph_list: list of orphan iyesde numbers in order added
 * @orph_new: list of orphan iyesde numbers added since last commit
 * @orph_cnext: next orphan to commit
 * @orph_dnext: next orphan to delete
 * @orphan_lock: lock for orph_tree and orph_new
 * @orph_buf: buffer for orphan yesdes
 * @new_orphans: number of orphans since last commit
 * @cmt_orphans: number of orphans being committed
 * @tot_orphans: number of orphans in the rb_tree
 * @max_orphans: maximum number of orphans allowed
 * @ohead_lnum: orphan head LEB number
 * @ohead_offs: orphan head offset
 * @yes_orphs: yesn-zero if there are yes orphans
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
 * @gc_seq: incremented for every yesn-index LEB garbage collected
 * @gced_lnum: last yesn-index LEB that was garbage collected
 *
 * @infos_list: links all 'ubifs_info' objects
 * @umount_mutex: serializes shrinker and un-mount
 * @shrinker_run_yes: shrinker run number
 *
 * @space_bits: number of bits needed to record free or dirty space
 * @lpt_lnum_bits: number of bits needed to record a LEB number in the LPT
 * @lpt_offs_bits: number of bits needed to record an offset in the LPT
 * @lpt_spc_bits: number of bits needed to space in the LPT
 * @pcnt_bits: number of bits needed to record pyesde or nyesde number
 * @lnum_bits: number of bits needed to record LEB number
 * @nyesde_sz: size of on-flash nyesde
 * @pyesde_sz: size of on-flash pyesde
 * @ltab_sz: size of on-flash LPT lprops table
 * @lsave_sz: size of on-flash LPT save table
 * @pyesde_cnt: number of pyesdes
 * @nyesde_cnt: number of nyesdes
 * @lpt_hght: height of the LPT
 * @pyesdes_have: number of pyesdes in memory
 *
 * @lp_mutex: protects lprops table and all the other lprops-related fields
 * @lpt_lnum: LEB number of the root nyesde of the LPT
 * @lpt_offs: offset of the root nyesde of the LPT
 * @nhead_lnum: LEB number of LPT head
 * @nhead_offs: offset of LPT head
 * @lpt_drty_flgs: dirty flags for LPT special yesdes e.g. ltab
 * @dirty_nn_cnt: number of dirty nyesdes
 * @dirty_pn_cnt: number of dirty pyesdes
 * @check_lpt_free: flag that indicates LPT GC may be needed
 * @lpt_sz: LPT size
 * @lpt_yesd_buf: buffer for an on-flash nyesde or pyesde
 * @lpt_buf: buffer of LEB size used by LPT
 * @nroot: address in memory of the root nyesde of the LPT
 * @lpt_cnext: next LPT yesde to commit
 * @lpt_heap: array of heaps of categorized lprops
 * @dirty_idx: a (reverse sorted) copy of the LPROPS_DIRTY_IDX heap as at
 *             previous commit start
 * @uncat_list: list of un-categorized LEBs
 * @empty_list: list of empty LEBs
 * @freeable_list: list of freeable yesn-index LEBs (free + dirty == @leb_size)
 * @frdi_idx_list: list of freeable index LEBs (free + dirty == @leb_size)
 * @freeable_cnt: number of freeable LEBs in @freeable_list
 * @in_a_category_cnt: count of lprops which are in a certain category, which
 *                     basically meants that they were loaded from the flash
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
 * @hash_tfm: the hash transformation used for hashing yesdes
 * @hmac_tfm: the HMAC transformation for this filesystem
 * @hmac_desc_len: length of the HMAC used for authentication
 * @auth_key_name: the authentication key name
 * @auth_hash_name: the name of the hash algorithm used for authentication
 * @auth_hash_algo: the authentication hash used for this fs
 * @log_hash: the log hash from the commit start yesde up to the latest reference
 *            yesde.
 *
 * @empty: %1 if the UBI device is empty
 * @need_recovery: %1 if the file-system needs recovery
 * @replaying: %1 during journal replay
 * @mounting: %1 while mounting
 * @probing: %1 while attempting to mount if SB_SILENT mount flag is set
 * @remounting_rw: %1 while re-mounting from R/O mode to R/W mode
 * @replay_list: temporary list used during journal replay
 * @replay_buds: list of buds to replay
 * @cs_sqnum: sequence number of first yesde in the log (commit start yesde)
 * @unclean_leb_list: LEBs to recover when re-mounting R/O mounted FS to R/W
 *                    mode
 * @rcvrd_mst_yesde: recovered master yesde to write when re-mounting R/O mounted
 *                  FS to R/W mode
 * @size_tree: iyesde size information for recovery
 * @mount_opts: UBIFS-specific mount options
 *
 * @dbg: debugging-related information
 */
struct ubifs_info {
	struct super_block *vfs_sb;
	struct ubifs_sb_yesde *sup_yesde;

	iyes_t highest_inum;
	unsigned long long max_sqnum;
	unsigned long long cmt_yes;
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
	unsigned int space_fixup:1;
	unsigned int double_hash:1;
	unsigned int encrypted:1;
	unsigned int yes_chk_data_crc:1;
	unsigned int bulk_read:1;
	unsigned int default_compr:2;
	unsigned int rw_incompat:1;
	unsigned int assert_action:2;
	unsigned int authenticated:1;
	unsigned int superblock_need_write:1;

	struct mutex tnc_mutex;
	struct ubifs_zbranch zroot;
	struct ubifs_zyesde *cnext;
	struct ubifs_zyesde *enext;
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

	struct ubifs_mst_yesde *mst_yesde;
	int mst_offs;

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
	int hash_len;
	int fayesut;

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
	unsigned int ro_media:1;
	unsigned int ro_mount:1;
	unsigned int ro_error:1;

	atomic_long_t dirty_pg_cnt;
	atomic_long_t dirty_zn_cnt;
	atomic_long_t clean_zn_cnt;

	spinlock_t space_lock;
	struct ubifs_lp_stats lst;
	struct ubifs_budg_info bi;
	unsigned long long calc_idx_sz;

	int ref_yesde_alsz;
	int mst_yesde_alsz;
	int min_idx_yesde_sz;
	int max_idx_yesde_sz;
	long long max_iyesde_sz;
	int max_zyesde_sz;

	int leb_overhead;
	int dead_wm;
	int dark_wm;
	int block_cnt;

	struct ubifs_yesde_range ranges[UBIFS_NODE_TYPES_CNT];
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
	int yes_orphs;

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
	unsigned int shrinker_run_yes;

	int space_bits;
	int lpt_lnum_bits;
	int lpt_offs_bits;
	int lpt_spc_bits;
	int pcnt_bits;
	int lnum_bits;
	int nyesde_sz;
	int pyesde_sz;
	int ltab_sz;
	int lsave_sz;
	int pyesde_cnt;
	int nyesde_cnt;
	int lpt_hght;
	int pyesdes_have;

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
	void *lpt_yesd_buf;
	void *lpt_buf;
	struct ubifs_nyesde *nroot;
	struct ubifs_cyesde *lpt_cnext;
	struct ubifs_lpt_heap lpt_heap[LPROPS_HEAP_CNT];
	struct ubifs_lpt_heap dirty_idx;
	struct list_head uncat_list;
	struct list_head empty_list;
	struct list_head freeable_list;
	struct list_head frdi_idx_list;
	int freeable_cnt;
	int in_a_category_cnt;

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
	kuid_t rp_uid;
	kgid_t rp_gid;

	struct crypto_shash *hash_tfm;
	struct crypto_shash *hmac_tfm;
	int hmac_desc_len;
	char *auth_key_name;
	char *auth_hash_name;
	enum hash_algo auth_hash_algo;

	struct shash_desc *log_hash;

	/* The below fields are used only during mounting and re-mounting */
	unsigned int empty:1;
	unsigned int need_recovery:1;
	unsigned int replaying:1;
	unsigned int mounting:1;
	unsigned int remounting_rw:1;
	unsigned int probing:1;
	struct list_head replay_list;
	struct list_head replay_buds;
	unsigned long long cs_sqnum;
	struct list_head unclean_leb_list;
	struct ubifs_mst_yesde *rcvrd_mst_yesde;
	struct rb_root size_tree;
	struct ubifs_mount_opts mount_opts;

	struct ubifs_debug_info *dbg;
};

extern struct list_head ubifs_infos;
extern spinlock_t ubifs_infos_lock;
extern atomic_long_t ubifs_clean_zn_cnt;
extern const struct super_operations ubifs_super_operations;
extern const struct address_space_operations ubifs_file_address_operations;
extern const struct file_operations ubifs_file_operations;
extern const struct iyesde_operations ubifs_file_iyesde_operations;
extern const struct file_operations ubifs_dir_operations;
extern const struct iyesde_operations ubifs_dir_iyesde_operations;
extern const struct iyesde_operations ubifs_symlink_iyesde_operations;
extern struct ubifs_compressor *ubifs_compressors[UBIFS_COMPR_TYPES_CNT];

/* auth.c */
static inline int ubifs_authenticated(const struct ubifs_info *c)
{
	return (IS_ENABLED(CONFIG_UBIFS_FS_AUTHENTICATION)) && c->authenticated;
}

struct shash_desc *__ubifs_hash_get_desc(const struct ubifs_info *c);
static inline struct shash_desc *ubifs_hash_get_desc(const struct ubifs_info *c)
{
	return ubifs_authenticated(c) ? __ubifs_hash_get_desc(c) : NULL;
}

static inline int ubifs_shash_init(const struct ubifs_info *c,
				   struct shash_desc *desc)
{
	if (ubifs_authenticated(c))
		return crypto_shash_init(desc);
	else
		return 0;
}

static inline int ubifs_shash_update(const struct ubifs_info *c,
				      struct shash_desc *desc, const void *buf,
				      unsigned int len)
{
	int err = 0;

	if (ubifs_authenticated(c)) {
		err = crypto_shash_update(desc, buf, len);
		if (err < 0)
			return err;
	}

	return 0;
}

static inline int ubifs_shash_final(const struct ubifs_info *c,
				    struct shash_desc *desc, u8 *out)
{
	return ubifs_authenticated(c) ? crypto_shash_final(desc, out) : 0;
}

int __ubifs_yesde_calc_hash(const struct ubifs_info *c, const void *buf,
			  u8 *hash);
static inline int ubifs_yesde_calc_hash(const struct ubifs_info *c,
					const void *buf, u8 *hash)
{
	if (ubifs_authenticated(c))
		return __ubifs_yesde_calc_hash(c, buf, hash);
	else
		return 0;
}

int ubifs_prepare_auth_yesde(struct ubifs_info *c, void *yesde,
			     struct shash_desc *inhash);

/**
 * ubifs_check_hash - compare two hashes
 * @c: UBIFS file-system description object
 * @expected: first hash
 * @got: second hash
 *
 * Compare two hashes @expected and @got. Returns 0 when they are equal, a
 * negative error code otherwise.
 */
static inline int ubifs_check_hash(const struct ubifs_info *c,
				   const u8 *expected, const u8 *got)
{
	return crypto_memneq(expected, got, c->hash_len);
}

/**
 * ubifs_check_hmac - compare two HMACs
 * @c: UBIFS file-system description object
 * @expected: first HMAC
 * @got: second HMAC
 *
 * Compare two hashes @expected and @got. Returns 0 when they are equal, a
 * negative error code otherwise.
 */
static inline int ubifs_check_hmac(const struct ubifs_info *c,
				   const u8 *expected, const u8 *got)
{
	return crypto_memneq(expected, got, c->hmac_desc_len);
}

void ubifs_bad_hash(const struct ubifs_info *c, const void *yesde,
		    const u8 *hash, int lnum, int offs);

int __ubifs_yesde_check_hash(const struct ubifs_info *c, const void *buf,
			  const u8 *expected);
static inline int ubifs_yesde_check_hash(const struct ubifs_info *c,
					const void *buf, const u8 *expected)
{
	if (ubifs_authenticated(c))
		return __ubifs_yesde_check_hash(c, buf, expected);
	else
		return 0;
}

int ubifs_init_authentication(struct ubifs_info *c);
void __ubifs_exit_authentication(struct ubifs_info *c);
static inline void ubifs_exit_authentication(struct ubifs_info *c)
{
	if (ubifs_authenticated(c))
		__ubifs_exit_authentication(c);
}

/**
 * ubifs_branch_hash - returns a pointer to the hash of a branch
 * @c: UBIFS file-system description object
 * @br: branch to get the hash from
 *
 * This returns a pointer to the hash of a branch. Since the key already is a
 * dynamically sized object we canyest use a struct member here.
 */
static inline u8 *ubifs_branch_hash(struct ubifs_info *c,
				    struct ubifs_branch *br)
{
	return (void *)br + sizeof(*br) + c->key_len;
}

/**
 * ubifs_copy_hash - copy a hash
 * @c: UBIFS file-system description object
 * @from: source hash
 * @to: destination hash
 *
 * With authentication this copies a hash, otherwise does yesthing.
 */
static inline void ubifs_copy_hash(const struct ubifs_info *c, const u8 *from,
				   u8 *to)
{
	if (ubifs_authenticated(c))
		memcpy(to, from, c->hash_len);
}

int __ubifs_yesde_insert_hmac(const struct ubifs_info *c, void *buf,
			      int len, int ofs_hmac);
static inline int ubifs_yesde_insert_hmac(const struct ubifs_info *c, void *buf,
					  int len, int ofs_hmac)
{
	if (ubifs_authenticated(c))
		return __ubifs_yesde_insert_hmac(c, buf, len, ofs_hmac);
	else
		return 0;
}

int __ubifs_yesde_verify_hmac(const struct ubifs_info *c, const void *buf,
			     int len, int ofs_hmac);
static inline int ubifs_yesde_verify_hmac(const struct ubifs_info *c,
					 const void *buf, int len, int ofs_hmac)
{
	if (ubifs_authenticated(c))
		return __ubifs_yesde_verify_hmac(c, buf, len, ofs_hmac);
	else
		return 0;
}

/**
 * ubifs_auth_yesde_sz - returns the size of an authentication yesde
 * @c: UBIFS file-system description object
 *
 * This function returns the size of an authentication yesde which can
 * be 0 for unauthenticated filesystems or the real size of an auth yesde
 * authentication is enabled.
 */
static inline int ubifs_auth_yesde_sz(const struct ubifs_info *c)
{
	if (ubifs_authenticated(c))
		return sizeof(struct ubifs_auth_yesde) + c->hmac_desc_len;
	else
		return 0;
}
int ubifs_sb_verify_signature(struct ubifs_info *c,
			      const struct ubifs_sb_yesde *sup);
bool ubifs_hmac_zero(struct ubifs_info *c, const u8 *hmac);

int ubifs_hmac_wkm(struct ubifs_info *c, u8 *hmac);

int __ubifs_shash_copy_state(const struct ubifs_info *c, struct shash_desc *src,
			     struct shash_desc *target);
static inline int ubifs_shash_copy_state(const struct ubifs_info *c,
					   struct shash_desc *src,
					   struct shash_desc *target)
{
	if (ubifs_authenticated(c))
		return __ubifs_shash_copy_state(c, src, target);
	else
		return 0;
}

/* io.c */
void ubifs_ro_mode(struct ubifs_info *c, int err);
int ubifs_leb_read(const struct ubifs_info *c, int lnum, void *buf, int offs,
		   int len, int even_ebadmsg);
int ubifs_leb_write(struct ubifs_info *c, int lnum, const void *buf, int offs,
		    int len);
int ubifs_leb_change(struct ubifs_info *c, int lnum, const void *buf, int len);
int ubifs_leb_unmap(struct ubifs_info *c, int lnum);
int ubifs_leb_map(struct ubifs_info *c, int lnum);
int ubifs_is_mapped(const struct ubifs_info *c, int lnum);
int ubifs_wbuf_write_yeslock(struct ubifs_wbuf *wbuf, void *buf, int len);
int ubifs_wbuf_seek_yeslock(struct ubifs_wbuf *wbuf, int lnum, int offs);
int ubifs_wbuf_init(struct ubifs_info *c, struct ubifs_wbuf *wbuf);
int ubifs_read_yesde(const struct ubifs_info *c, void *buf, int type, int len,
		    int lnum, int offs);
int ubifs_read_yesde_wbuf(struct ubifs_wbuf *wbuf, void *buf, int type, int len,
			 int lnum, int offs);
int ubifs_write_yesde(struct ubifs_info *c, void *yesde, int len, int lnum,
		     int offs);
int ubifs_write_yesde_hmac(struct ubifs_info *c, void *buf, int len, int lnum,
			  int offs, int hmac_offs);
int ubifs_check_yesde(const struct ubifs_info *c, const void *buf, int lnum,
		     int offs, int quiet, int must_chk_crc);
void ubifs_init_yesde(struct ubifs_info *c, void *buf, int len, int pad);
void ubifs_crc_yesde(struct ubifs_info *c, void *buf, int len);
void ubifs_prepare_yesde(struct ubifs_info *c, void *buf, int len, int pad);
int ubifs_prepare_yesde_hmac(struct ubifs_info *c, void *yesde, int len,
			    int hmac_offs, int pad);
void ubifs_prep_grp_yesde(struct ubifs_info *c, void *yesde, int len, int last);
int ubifs_io_init(struct ubifs_info *c);
void ubifs_pad(const struct ubifs_info *c, void *buf, int pad);
int ubifs_wbuf_sync_yeslock(struct ubifs_wbuf *wbuf);
int ubifs_bg_wbufs_sync(struct ubifs_info *c);
void ubifs_wbuf_add_iyes_yeslock(struct ubifs_wbuf *wbuf, iyes_t inum);
int ubifs_sync_wbufs_by_iyesde(struct ubifs_info *c, struct iyesde *iyesde);

/* scan.c */
struct ubifs_scan_leb *ubifs_scan(const struct ubifs_info *c, int lnum,
				  int offs, void *sbuf, int quiet);
void ubifs_scan_destroy(struct ubifs_scan_leb *sleb);
int ubifs_scan_a_yesde(const struct ubifs_info *c, void *buf, int len, int lnum,
		      int offs, int quiet);
struct ubifs_scan_leb *ubifs_start_scan(const struct ubifs_info *c, int lnum,
					int offs, void *sbuf);
void ubifs_end_scan(const struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		    int lnum, int offs);
int ubifs_add_syesd(const struct ubifs_info *c, struct ubifs_scan_leb *sleb,
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
int ubifs_jnl_update(struct ubifs_info *c, const struct iyesde *dir,
		     const struct fscrypt_name *nm, const struct iyesde *iyesde,
		     int deletion, int xent);
int ubifs_jnl_write_data(struct ubifs_info *c, const struct iyesde *iyesde,
			 const union ubifs_key *key, const void *buf, int len);
int ubifs_jnl_write_iyesde(struct ubifs_info *c, const struct iyesde *iyesde);
int ubifs_jnl_delete_iyesde(struct ubifs_info *c, const struct iyesde *iyesde);
int ubifs_jnl_xrename(struct ubifs_info *c, const struct iyesde *fst_dir,
		      const struct iyesde *fst_iyesde,
		      const struct fscrypt_name *fst_nm,
		      const struct iyesde *snd_dir,
		      const struct iyesde *snd_iyesde,
		      const struct fscrypt_name *snd_nm, int sync);
int ubifs_jnl_rename(struct ubifs_info *c, const struct iyesde *old_dir,
		     const struct iyesde *old_iyesde,
		     const struct fscrypt_name *old_nm,
		     const struct iyesde *new_dir,
		     const struct iyesde *new_iyesde,
		     const struct fscrypt_name *new_nm,
		     const struct iyesde *whiteout, int sync);
int ubifs_jnl_truncate(struct ubifs_info *c, const struct iyesde *iyesde,
		       loff_t old_size, loff_t new_size);
int ubifs_jnl_delete_xattr(struct ubifs_info *c, const struct iyesde *host,
			   const struct iyesde *iyesde, const struct fscrypt_name *nm);
int ubifs_jnl_change_xattr(struct ubifs_info *c, const struct iyesde *iyesde1,
			   const struct iyesde *iyesde2);

/* budget.c */
int ubifs_budget_space(struct ubifs_info *c, struct ubifs_budget_req *req);
void ubifs_release_budget(struct ubifs_info *c, struct ubifs_budget_req *req);
void ubifs_release_dirty_iyesde_budget(struct ubifs_info *c,
				      struct ubifs_iyesde *ui);
int ubifs_budget_iyesde_op(struct ubifs_info *c, struct iyesde *iyesde,
			  struct ubifs_budget_req *req);
void ubifs_release_iyes_dirty(struct ubifs_info *c, struct iyesde *iyesde,
				struct ubifs_budget_req *req);
void ubifs_cancel_iyes_op(struct ubifs_info *c, struct iyesde *iyesde,
			 struct ubifs_budget_req *req);
long long ubifs_get_free_space(struct ubifs_info *c);
long long ubifs_get_free_space_yeslock(struct ubifs_info *c);
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
			struct ubifs_zyesde **zn, int *n);
int ubifs_tnc_lookup_nm(struct ubifs_info *c, const union ubifs_key *key,
			void *yesde, const struct fscrypt_name *nm);
int ubifs_tnc_lookup_dh(struct ubifs_info *c, const union ubifs_key *key,
			void *yesde, uint32_t secondary_hash);
int ubifs_tnc_locate(struct ubifs_info *c, const union ubifs_key *key,
		     void *yesde, int *lnum, int *offs);
int ubifs_tnc_add(struct ubifs_info *c, const union ubifs_key *key, int lnum,
		  int offs, int len, const u8 *hash);
int ubifs_tnc_replace(struct ubifs_info *c, const union ubifs_key *key,
		      int old_lnum, int old_offs, int lnum, int offs, int len);
int ubifs_tnc_add_nm(struct ubifs_info *c, const union ubifs_key *key,
		     int lnum, int offs, int len, const u8 *hash,
		     const struct fscrypt_name *nm);
int ubifs_tnc_remove(struct ubifs_info *c, const union ubifs_key *key);
int ubifs_tnc_remove_nm(struct ubifs_info *c, const union ubifs_key *key,
			const struct fscrypt_name *nm);
int ubifs_tnc_remove_dh(struct ubifs_info *c, const union ubifs_key *key,
			uint32_t cookie);
int ubifs_tnc_remove_range(struct ubifs_info *c, union ubifs_key *from_key,
			   union ubifs_key *to_key);
int ubifs_tnc_remove_iyes(struct ubifs_info *c, iyes_t inum);
struct ubifs_dent_yesde *ubifs_tnc_next_ent(struct ubifs_info *c,
					   union ubifs_key *key,
					   const struct fscrypt_name *nm);
void ubifs_tnc_close(struct ubifs_info *c);
int ubifs_tnc_has_yesde(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs, int is_idx);
int ubifs_dirty_idx_yesde(struct ubifs_info *c, union ubifs_key *key, int level,
			 int lnum, int offs);
/* Shared by tnc.c for tnc_commit.c */
void destroy_old_idx(struct ubifs_info *c);
int is_idx_yesde_in_tnc(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs);
int insert_old_idx_zyesde(struct ubifs_info *c, struct ubifs_zyesde *zyesde);
int ubifs_tnc_get_bu_keys(struct ubifs_info *c, struct bu_info *bu);
int ubifs_tnc_bulk_read(struct ubifs_info *c, struct bu_info *bu);

/* tnc_misc.c */
struct ubifs_zyesde *ubifs_tnc_levelorder_next(const struct ubifs_info *c,
					      struct ubifs_zyesde *zr,
					      struct ubifs_zyesde *zyesde);
int ubifs_search_zbranch(const struct ubifs_info *c,
			 const struct ubifs_zyesde *zyesde,
			 const union ubifs_key *key, int *n);
struct ubifs_zyesde *ubifs_tnc_postorder_first(struct ubifs_zyesde *zyesde);
struct ubifs_zyesde *ubifs_tnc_postorder_next(const struct ubifs_info *c,
					     struct ubifs_zyesde *zyesde);
long ubifs_destroy_tnc_subtree(const struct ubifs_info *c,
			       struct ubifs_zyesde *zr);
struct ubifs_zyesde *ubifs_load_zyesde(struct ubifs_info *c,
				     struct ubifs_zbranch *zbr,
				     struct ubifs_zyesde *parent, int iip);
int ubifs_tnc_read_yesde(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			void *yesde);

/* tnc_commit.c */
int ubifs_tnc_start_commit(struct ubifs_info *c, struct ubifs_zbranch *zroot);
int ubifs_tnc_end_commit(struct ubifs_info *c);

/* shrinker.c */
unsigned long ubifs_shrink_scan(struct shrinker *shrink,
				struct shrink_control *sc);
unsigned long ubifs_shrink_count(struct shrinker *shrink,
				 struct shrink_control *sc);

/* commit.c */
int ubifs_bg_thread(void *info);
void ubifs_commit_required(struct ubifs_info *c);
void ubifs_request_bg_commit(struct ubifs_info *c);
int ubifs_run_commit(struct ubifs_info *c);
void ubifs_recovery_commit(struct ubifs_info *c);
int ubifs_gc_should_commit(struct ubifs_info *c);
void ubifs_wait_for_commit(struct ubifs_info *c);

/* master.c */
int ubifs_compare_master_yesde(struct ubifs_info *c, void *m1, void *m2);
int ubifs_read_master(struct ubifs_info *c);
int ubifs_write_master(struct ubifs_info *c);

/* sb.c */
int ubifs_read_superblock(struct ubifs_info *c);
int ubifs_write_sb_yesde(struct ubifs_info *c, struct ubifs_sb_yesde *sup);
int ubifs_fixup_free_space(struct ubifs_info *c);
int ubifs_enable_encryption(struct ubifs_info *c);

/* replay.c */
int ubifs_validate_entry(struct ubifs_info *c,
			 const struct ubifs_dent_yesde *dent);
int ubifs_replay_journal(struct ubifs_info *c);

/* gc.c */
int ubifs_garbage_collect(struct ubifs_info *c, int anyway);
int ubifs_gc_start_commit(struct ubifs_info *c);
int ubifs_gc_end_commit(struct ubifs_info *c);
void ubifs_destroy_idx_gc(struct ubifs_info *c);
int ubifs_get_idx_gc_leb(struct ubifs_info *c);
int ubifs_garbage_collect_leb(struct ubifs_info *c, struct ubifs_lprops *lp);

/* orphan.c */
int ubifs_add_orphan(struct ubifs_info *c, iyes_t inum);
void ubifs_delete_orphan(struct ubifs_info *c, iyes_t inum);
int ubifs_orphan_start_commit(struct ubifs_info *c);
int ubifs_orphan_end_commit(struct ubifs_info *c);
int ubifs_mount_orphans(struct ubifs_info *c, int unclean, int read_only);
int ubifs_clear_orphans(struct ubifs_info *c);

/* lpt.c */
int ubifs_calc_lpt_geom(struct ubifs_info *c);
int ubifs_create_dflt_lpt(struct ubifs_info *c, int *main_lebs, int lpt_first,
			  int *lpt_lebs, int *big_lpt, u8 *hash);
int ubifs_lpt_init(struct ubifs_info *c, int rd, int wr);
struct ubifs_lprops *ubifs_lpt_lookup(struct ubifs_info *c, int lnum);
struct ubifs_lprops *ubifs_lpt_lookup_dirty(struct ubifs_info *c, int lnum);
int ubifs_lpt_scan_yeslock(struct ubifs_info *c, int start_lnum, int end_lnum,
			  ubifs_lpt_scan_callback scan_cb, void *data);

/* Shared by lpt.c for lpt_commit.c */
void ubifs_pack_lsave(struct ubifs_info *c, void *buf, int *lsave);
void ubifs_pack_ltab(struct ubifs_info *c, void *buf,
		     struct ubifs_lpt_lprops *ltab);
void ubifs_pack_pyesde(struct ubifs_info *c, void *buf,
		      struct ubifs_pyesde *pyesde);
void ubifs_pack_nyesde(struct ubifs_info *c, void *buf,
		      struct ubifs_nyesde *nyesde);
struct ubifs_pyesde *ubifs_get_pyesde(struct ubifs_info *c,
				    struct ubifs_nyesde *parent, int iip);
struct ubifs_nyesde *ubifs_get_nyesde(struct ubifs_info *c,
				    struct ubifs_nyesde *parent, int iip);
struct ubifs_pyesde *ubifs_pyesde_lookup(struct ubifs_info *c, int i);
int ubifs_read_nyesde(struct ubifs_info *c, struct ubifs_nyesde *parent, int iip);
void ubifs_add_lpt_dirt(struct ubifs_info *c, int lnum, int dirty);
void ubifs_add_nyesde_dirt(struct ubifs_info *c, struct ubifs_nyesde *nyesde);
uint32_t ubifs_unpack_bits(const struct ubifs_info *c, uint8_t **addr, int *pos, int nrbits);
struct ubifs_nyesde *ubifs_first_nyesde(struct ubifs_info *c, int *hght);
/* Needed only in debugging code in lpt_commit.c */
int ubifs_unpack_nyesde(const struct ubifs_info *c, void *buf,
		       struct ubifs_nyesde *nyesde);
int ubifs_lpt_calc_hash(struct ubifs_info *c, u8 *hash);

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
int ubifs_fsync(struct file *file, loff_t start, loff_t end, int datasync);
int ubifs_setattr(struct dentry *dentry, struct iattr *attr);
int ubifs_update_time(struct iyesde *iyesde, struct timespec64 *time, int flags);

/* dir.c */
struct iyesde *ubifs_new_iyesde(struct ubifs_info *c, struct iyesde *dir,
			      umode_t mode);
int ubifs_getattr(const struct path *path, struct kstat *stat,
		  u32 request_mask, unsigned int flags);
int ubifs_check_dir_empty(struct iyesde *dir);

/* xattr.c */
extern const struct xattr_handler *ubifs_xattr_handlers[];
ssize_t ubifs_listxattr(struct dentry *dentry, char *buffer, size_t size);
int ubifs_xattr_set(struct iyesde *host, const char *name, const void *value,
		    size_t size, int flags, bool check_lock);
ssize_t ubifs_xattr_get(struct iyesde *host, const char *name, void *buf,
			size_t size);

#ifdef CONFIG_UBIFS_FS_XATTR
void ubifs_evict_xattr_iyesde(struct ubifs_info *c, iyes_t xattr_inum);
int ubifs_purge_xattrs(struct iyesde *host);
#else
static inline void ubifs_evict_xattr_iyesde(struct ubifs_info *c,
					   iyes_t xattr_inum) { }
static inline int ubifs_purge_xattrs(struct iyesde *host)
{
	return 0;
}
#endif

#ifdef CONFIG_UBIFS_FS_SECURITY
extern int ubifs_init_security(struct iyesde *dentry, struct iyesde *iyesde,
			const struct qstr *qstr);
#else
static inline int ubifs_init_security(struct iyesde *dentry,
			struct iyesde *iyesde, const struct qstr *qstr)
{
	return 0;
}
#endif


/* super.c */
struct iyesde *ubifs_iget(struct super_block *sb, unsigned long inum);

/* recovery.c */
int ubifs_recover_master_yesde(struct ubifs_info *c);
int ubifs_write_rcvrd_mst_yesde(struct ubifs_info *c);
struct ubifs_scan_leb *ubifs_recover_leb(struct ubifs_info *c, int lnum,
					 int offs, void *sbuf, int jhead);
struct ubifs_scan_leb *ubifs_recover_log_leb(struct ubifs_info *c, int lnum,
					     int offs, void *sbuf);
int ubifs_recover_inl_heads(struct ubifs_info *c, void *sbuf);
int ubifs_clean_lebs(struct ubifs_info *c, void *sbuf);
int ubifs_rcvry_gc_commit(struct ubifs_info *c);
int ubifs_recover_size_accum(struct ubifs_info *c, union ubifs_key *key,
			     int deletion, loff_t new_size);
int ubifs_recover_size(struct ubifs_info *c, bool in_place);
void ubifs_destroy_size_tree(struct ubifs_info *c);

/* ioctl.c */
long ubifs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
void ubifs_set_iyesde_flags(struct iyesde *iyesde);
#ifdef CONFIG_COMPAT
long ubifs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#endif

/* compressor.c */
int __init ubifs_compressors_init(void);
void ubifs_compressors_exit(void);
void ubifs_compress(const struct ubifs_info *c, const void *in_buf, int in_len,
		    void *out_buf, int *out_len, int *compr_type);
int ubifs_decompress(const struct ubifs_info *c, const void *buf, int len,
		     void *out, int *out_len, int compr_type);

#include "debug.h"
#include "misc.h"
#include "key.h"

#ifndef CONFIG_FS_ENCRYPTION
static inline int ubifs_encrypt(const struct iyesde *iyesde,
				struct ubifs_data_yesde *dn,
				unsigned int in_len, unsigned int *out_len,
				int block)
{
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	ubifs_assert(c, 0);
	return -EOPNOTSUPP;
}
static inline int ubifs_decrypt(const struct iyesde *iyesde,
				struct ubifs_data_yesde *dn,
				unsigned int *out_len, int block)
{
	struct ubifs_info *c = iyesde->i_sb->s_fs_info;
	ubifs_assert(c, 0);
	return -EOPNOTSUPP;
}
#else
/* crypto.c */
int ubifs_encrypt(const struct iyesde *iyesde, struct ubifs_data_yesde *dn,
		  unsigned int in_len, unsigned int *out_len, int block);
int ubifs_decrypt(const struct iyesde *iyesde, struct ubifs_data_yesde *dn,
		  unsigned int *out_len, int block);
#endif

extern const struct fscrypt_operations ubifs_crypt_operations;

static inline bool ubifs_crypt_is_encrypted(const struct iyesde *iyesde)
{
	const struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);

	return ui->flags & UBIFS_CRYPT_FL;
}

/* Normal UBIFS messages */
__printf(2, 3)
void ubifs_msg(const struct ubifs_info *c, const char *fmt, ...);
__printf(2, 3)
void ubifs_err(const struct ubifs_info *c, const char *fmt, ...);
__printf(2, 3)
void ubifs_warn(const struct ubifs_info *c, const char *fmt, ...);
/*
 * A conditional variant of 'ubifs_err()' which doesn't output anything
 * if probing (ie. SB_SILENT set).
 */
#define ubifs_errc(c, fmt, ...)						\
do {									\
	if (!(c)->probing)						\
		ubifs_err(c, fmt, ##__VA_ARGS__);			\
} while (0)

#endif /* !__UBIFS_H__ */
