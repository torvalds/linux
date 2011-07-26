/*
 * Copyright 1996, 1997, 1998 Hans Reiser, see reiserfs/README for licensing and copyright details
 */

				/* this file has an amazingly stupid
				   name, yura please fix it to be
				   reiserfs.h, and merge all the rest
				   of our .h files that are in this
				   directory into it.  */

#ifndef _LINUX_REISER_FS_H
#define _LINUX_REISER_FS_H

#include <linux/types.h>
#include <linux/magic.h>

#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <asm/unaligned.h>
#include <linux/bitops.h>
#include <linux/proc_fs.h>
#include <linux/buffer_head.h>
#include <linux/reiserfs_fs_i.h>
#include <linux/reiserfs_fs_sb.h>
#endif

/*
 *  include/linux/reiser_fs.h
 *
 *  Reiser File System constants and structures
 *
 */

/* ioctl's command */
#define REISERFS_IOC_UNPACK		_IOW(0xCD,1,long)
/* define following flags to be the same as in ext2, so that chattr(1),
   lsattr(1) will work with us. */
#define REISERFS_IOC_GETFLAGS		FS_IOC_GETFLAGS
#define REISERFS_IOC_SETFLAGS		FS_IOC_SETFLAGS
#define REISERFS_IOC_GETVERSION		FS_IOC_GETVERSION
#define REISERFS_IOC_SETVERSION		FS_IOC_SETVERSION

#ifdef __KERNEL__
/* the 32 bit compat definitions with int argument */
#define REISERFS_IOC32_UNPACK		_IOW(0xCD, 1, int)
#define REISERFS_IOC32_GETFLAGS		FS_IOC32_GETFLAGS
#define REISERFS_IOC32_SETFLAGS		FS_IOC32_SETFLAGS
#define REISERFS_IOC32_GETVERSION	FS_IOC32_GETVERSION
#define REISERFS_IOC32_SETVERSION	FS_IOC32_SETVERSION

/*
 * Locking primitives. The write lock is a per superblock
 * special mutex that has properties close to the Big Kernel Lock
 * which was used in the previous locking scheme.
 */
void reiserfs_write_lock(struct super_block *s);
void reiserfs_write_unlock(struct super_block *s);
int reiserfs_write_lock_once(struct super_block *s);
void reiserfs_write_unlock_once(struct super_block *s, int lock_depth);

#ifdef CONFIG_REISERFS_CHECK
void reiserfs_lock_check_recursive(struct super_block *s);
#else
static inline void reiserfs_lock_check_recursive(struct super_block *s) { }
#endif

/*
 * Several mutexes depend on the write lock.
 * However sometimes we want to relax the write lock while we hold
 * these mutexes, according to the release/reacquire on schedule()
 * properties of the Bkl that were used.
 * Reiserfs performances and locking were based on this scheme.
 * Now that the write lock is a mutex and not the bkl anymore, doing so
 * may result in a deadlock:
 *
 * A acquire write_lock
 * A acquire j_commit_mutex
 * A release write_lock and wait for something
 * B acquire write_lock
 * B can't acquire j_commit_mutex and sleep
 * A can't acquire write lock anymore
 * deadlock
 *
 * What we do here is avoiding such deadlock by playing the same game
 * than the Bkl: if we can't acquire a mutex that depends on the write lock,
 * we release the write lock, wait a bit and then retry.
 *
 * The mutexes concerned by this hack are:
 * - The commit mutex of a journal list
 * - The flush mutex
 * - The journal lock
 * - The inode mutex
 */
static inline void reiserfs_mutex_lock_safe(struct mutex *m,
			       struct super_block *s)
{
	reiserfs_lock_check_recursive(s);
	reiserfs_write_unlock(s);
	mutex_lock(m);
	reiserfs_write_lock(s);
}

static inline void
reiserfs_mutex_lock_nested_safe(struct mutex *m, unsigned int subclass,
			       struct super_block *s)
{
	reiserfs_lock_check_recursive(s);
	reiserfs_write_unlock(s);
	mutex_lock_nested(m, subclass);
	reiserfs_write_lock(s);
}

static inline void
reiserfs_down_read_safe(struct rw_semaphore *sem, struct super_block *s)
{
	reiserfs_lock_check_recursive(s);
	reiserfs_write_unlock(s);
	down_read(sem);
	reiserfs_write_lock(s);
}

/*
 * When we schedule, we usually want to also release the write lock,
 * according to the previous bkl based locking scheme of reiserfs.
 */
static inline void reiserfs_cond_resched(struct super_block *s)
{
	if (need_resched()) {
		reiserfs_write_unlock(s);
		schedule();
		reiserfs_write_lock(s);
	}
}

struct fid;

/* in reading the #defines, it may help to understand that they employ
   the following abbreviations:

   B = Buffer
   I = Item header
   H = Height within the tree (should be changed to LEV)
   N = Number of the item in the node
   STAT = stat data
   DEH = Directory Entry Header
   EC = Entry Count
   E = Entry number
   UL = Unsigned Long
   BLKH = BLocK Header
   UNFM = UNForMatted node
   DC = Disk Child
   P = Path

   These #defines are named by concatenating these abbreviations,
   where first comes the arguments, and last comes the return value,
   of the macro.

*/

#define USE_INODE_GENERATION_COUNTER

#define REISERFS_PREALLOCATE
#define DISPLACE_NEW_PACKING_LOCALITIES
#define PREALLOCATION_SIZE 9

/* n must be power of 2 */
#define _ROUND_UP(x,n) (((x)+(n)-1u) & ~((n)-1u))

// to be ok for alpha and others we have to align structures to 8 byte
// boundary.
// FIXME: do not change 4 by anything else: there is code which relies on that
#define ROUND_UP(x) _ROUND_UP(x,8LL)

/* debug levels.  Right now, CONFIG_REISERFS_CHECK means print all debug
** messages.
*/
#define REISERFS_DEBUG_CODE 5	/* extra messages to help find/debug errors */

void __reiserfs_warning(struct super_block *s, const char *id,
			 const char *func, const char *fmt, ...);
#define reiserfs_warning(s, id, fmt, args...) \
	 __reiserfs_warning(s, id, __func__, fmt, ##args)
/* assertions handling */

/** always check a condition and panic if it's false. */
#define __RASSERT(cond, scond, format, args...)			\
do {									\
	if (!(cond))							\
		reiserfs_panic(NULL, "assertion failure", "(" #cond ") at " \
			       __FILE__ ":%i:%s: " format "\n",		\
			       in_interrupt() ? -1 : task_pid_nr(current), \
			       __LINE__, __func__ , ##args);		\
} while (0)

#define RASSERT(cond, format, args...) __RASSERT(cond, #cond, format, ##args)

#if defined( CONFIG_REISERFS_CHECK )
#define RFALSE(cond, format, args...) __RASSERT(!(cond), "!(" #cond ")", format, ##args)
#else
#define RFALSE( cond, format, args... ) do {;} while( 0 )
#endif

#define CONSTF __attribute_const__
/*
 * Disk Data Structures
 */

/***************************************************************************/
/*                             SUPER BLOCK                                 */
/***************************************************************************/

/*
 * Structure of super block on disk, a version of which in RAM is often accessed as REISERFS_SB(s)->s_rs
 * the version in RAM is part of a larger structure containing fields never written to disk.
 */
#define UNSET_HASH 0		// read_super will guess about, what hash names
		     // in directories were sorted with
#define TEA_HASH  1
#define YURA_HASH 2
#define R5_HASH   3
#define DEFAULT_HASH R5_HASH

struct journal_params {
	__le32 jp_journal_1st_block;	/* where does journal start from on its
					 * device */
	__le32 jp_journal_dev;	/* journal device st_rdev */
	__le32 jp_journal_size;	/* size of the journal */
	__le32 jp_journal_trans_max;	/* max number of blocks in a transaction. */
	__le32 jp_journal_magic;	/* random value made on fs creation (this
					 * was sb_journal_block_count) */
	__le32 jp_journal_max_batch;	/* max number of blocks to batch into a
					 * trans */
	__le32 jp_journal_max_commit_age;	/* in seconds, how old can an async
						 * commit be */
	__le32 jp_journal_max_trans_age;	/* in seconds, how old can a transaction
						 * be */
};

/* this is the super from 3.5.X, where X >= 10 */
struct reiserfs_super_block_v1 {
	__le32 s_block_count;	/* blocks count         */
	__le32 s_free_blocks;	/* free blocks count    */
	__le32 s_root_block;	/* root block number    */
	struct journal_params s_journal;
	__le16 s_blocksize;	/* block size */
	__le16 s_oid_maxsize;	/* max size of object id array, see
				 * get_objectid() commentary  */
	__le16 s_oid_cursize;	/* current size of object id array */
	__le16 s_umount_state;	/* this is set to 1 when filesystem was
				 * umounted, to 2 - when not */
	char s_magic[10];	/* reiserfs magic string indicates that
				 * file system is reiserfs:
				 * "ReIsErFs" or "ReIsEr2Fs" or "ReIsEr3Fs" */
	__le16 s_fs_state;	/* it is set to used by fsck to mark which
				 * phase of rebuilding is done */
	__le32 s_hash_function_code;	/* indicate, what hash function is being use
					 * to sort names in a directory*/
	__le16 s_tree_height;	/* height of disk tree */
	__le16 s_bmap_nr;	/* amount of bitmap blocks needed to address
				 * each block of file system */
	__le16 s_version;	/* this field is only reliable on filesystem
				 * with non-standard journal */
	__le16 s_reserved_for_journal;	/* size in blocks of journal area on main
					 * device, we need to keep after
					 * making fs with non-standard journal */
} __attribute__ ((__packed__));

#define SB_SIZE_V1 (sizeof(struct reiserfs_super_block_v1))

/* this is the on disk super block */
struct reiserfs_super_block {
	struct reiserfs_super_block_v1 s_v1;
	__le32 s_inode_generation;
	__le32 s_flags;		/* Right now used only by inode-attributes, if enabled */
	unsigned char s_uuid[16];	/* filesystem unique identifier */
	unsigned char s_label[16];	/* filesystem volume label */
	__le16 s_mnt_count;		/* Count of mounts since last fsck */
	__le16 s_max_mnt_count;		/* Maximum mounts before check */
	__le32 s_lastcheck;		/* Timestamp of last fsck */
	__le32 s_check_interval;	/* Interval between checks */
	char s_unused[76];	/* zero filled by mkreiserfs and
				 * reiserfs_convert_objectid_map_v1()
				 * so any additions must be updated
				 * there as well. */
} __attribute__ ((__packed__));

#define SB_SIZE (sizeof(struct reiserfs_super_block))

#define REISERFS_VERSION_1 0
#define REISERFS_VERSION_2 2

// on-disk super block fields converted to cpu form
#define SB_DISK_SUPER_BLOCK(s) (REISERFS_SB(s)->s_rs)
#define SB_V1_DISK_SUPER_BLOCK(s) (&(SB_DISK_SUPER_BLOCK(s)->s_v1))
#define SB_BLOCKSIZE(s) \
        le32_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_blocksize))
#define SB_BLOCK_COUNT(s) \
        le32_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_block_count))
#define SB_FREE_BLOCKS(s) \
        le32_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_free_blocks))
#define SB_REISERFS_MAGIC(s) \
        (SB_V1_DISK_SUPER_BLOCK(s)->s_magic)
#define SB_ROOT_BLOCK(s) \
        le32_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_root_block))
#define SB_TREE_HEIGHT(s) \
        le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_tree_height))
#define SB_REISERFS_STATE(s) \
        le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_umount_state))
#define SB_VERSION(s) le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_version))
#define SB_BMAP_NR(s) le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_bmap_nr))

#define PUT_SB_BLOCK_COUNT(s, val) \
   do { SB_V1_DISK_SUPER_BLOCK(s)->s_block_count = cpu_to_le32(val); } while (0)
#define PUT_SB_FREE_BLOCKS(s, val) \
   do { SB_V1_DISK_SUPER_BLOCK(s)->s_free_blocks = cpu_to_le32(val); } while (0)
#define PUT_SB_ROOT_BLOCK(s, val) \
   do { SB_V1_DISK_SUPER_BLOCK(s)->s_root_block = cpu_to_le32(val); } while (0)
#define PUT_SB_TREE_HEIGHT(s, val) \
   do { SB_V1_DISK_SUPER_BLOCK(s)->s_tree_height = cpu_to_le16(val); } while (0)
#define PUT_SB_REISERFS_STATE(s, val) \
   do { SB_V1_DISK_SUPER_BLOCK(s)->s_umount_state = cpu_to_le16(val); } while (0)
#define PUT_SB_VERSION(s, val) \
   do { SB_V1_DISK_SUPER_BLOCK(s)->s_version = cpu_to_le16(val); } while (0)
#define PUT_SB_BMAP_NR(s, val) \
   do { SB_V1_DISK_SUPER_BLOCK(s)->s_bmap_nr = cpu_to_le16 (val); } while (0)

#define SB_ONDISK_JP(s) (&SB_V1_DISK_SUPER_BLOCK(s)->s_journal)
#define SB_ONDISK_JOURNAL_SIZE(s) \
         le32_to_cpu ((SB_ONDISK_JP(s)->jp_journal_size))
#define SB_ONDISK_JOURNAL_1st_BLOCK(s) \
         le32_to_cpu ((SB_ONDISK_JP(s)->jp_journal_1st_block))
#define SB_ONDISK_JOURNAL_DEVICE(s) \
         le32_to_cpu ((SB_ONDISK_JP(s)->jp_journal_dev))
#define SB_ONDISK_RESERVED_FOR_JOURNAL(s) \
         le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_reserved_for_journal))

#define is_block_in_log_or_reserved_area(s, block) \
         block >= SB_JOURNAL_1st_RESERVED_BLOCK(s) \
         && block < SB_JOURNAL_1st_RESERVED_BLOCK(s) +  \
         ((!is_reiserfs_jr(SB_DISK_SUPER_BLOCK(s)) ? \
         SB_ONDISK_JOURNAL_SIZE(s) + 1 : SB_ONDISK_RESERVED_FOR_JOURNAL(s)))

int is_reiserfs_3_5(struct reiserfs_super_block *rs);
int is_reiserfs_3_6(struct reiserfs_super_block *rs);
int is_reiserfs_jr(struct reiserfs_super_block *rs);

/* ReiserFS leaves the first 64k unused, so that partition labels have
   enough space.  If someone wants to write a fancy bootloader that
   needs more than 64k, let us know, and this will be increased in size.
   This number must be larger than than the largest block size on any
   platform, or code will break.  -Hans */
#define REISERFS_DISK_OFFSET_IN_BYTES (64 * 1024)
#define REISERFS_FIRST_BLOCK unused_define
#define REISERFS_JOURNAL_OFFSET_IN_BYTES REISERFS_DISK_OFFSET_IN_BYTES

/* the spot for the super in versions 3.5 - 3.5.10 (inclusive) */
#define REISERFS_OLD_DISK_OFFSET_IN_BYTES (8 * 1024)

/* reiserfs internal error code (used by search_by_key and fix_nodes)) */
#define CARRY_ON      0
#define REPEAT_SEARCH -1
#define IO_ERROR      -2
#define NO_DISK_SPACE -3
#define NO_BALANCING_NEEDED  (-4)
#define NO_MORE_UNUSED_CONTIGUOUS_BLOCKS (-5)
#define QUOTA_EXCEEDED -6

typedef __u32 b_blocknr_t;
typedef __le32 unp_t;

struct unfm_nodeinfo {
	unp_t unfm_nodenum;
	unsigned short unfm_freespace;
};

/* there are two formats of keys: 3.5 and 3.6
 */
#define KEY_FORMAT_3_5 0
#define KEY_FORMAT_3_6 1

/* there are two stat datas */
#define STAT_DATA_V1 0
#define STAT_DATA_V2 1

static inline struct reiserfs_inode_info *REISERFS_I(const struct inode *inode)
{
	return container_of(inode, struct reiserfs_inode_info, vfs_inode);
}

static inline struct reiserfs_sb_info *REISERFS_SB(const struct super_block *sb)
{
	return sb->s_fs_info;
}

/* Don't trust REISERFS_SB(sb)->s_bmap_nr, it's a u16
 * which overflows on large file systems. */
static inline __u32 reiserfs_bmap_count(struct super_block *sb)
{
	return (SB_BLOCK_COUNT(sb) - 1) / (sb->s_blocksize * 8) + 1;
}

static inline int bmap_would_wrap(unsigned bmap_nr)
{
	return bmap_nr > ((1LL << 16) - 1);
}

/** this says about version of key of all items (but stat data) the
    object consists of */
#define get_inode_item_key_version( inode )                                    \
    ((REISERFS_I(inode)->i_flags & i_item_key_version_mask) ? KEY_FORMAT_3_6 : KEY_FORMAT_3_5)

#define set_inode_item_key_version( inode, version )                           \
         ({ if((version)==KEY_FORMAT_3_6)                                      \
                REISERFS_I(inode)->i_flags |= i_item_key_version_mask;      \
            else                                                               \
                REISERFS_I(inode)->i_flags &= ~i_item_key_version_mask; })

#define get_inode_sd_version(inode)                                            \
    ((REISERFS_I(inode)->i_flags & i_stat_data_version_mask) ? STAT_DATA_V2 : STAT_DATA_V1)

#define set_inode_sd_version(inode, version)                                   \
         ({ if((version)==STAT_DATA_V2)                                        \
                REISERFS_I(inode)->i_flags |= i_stat_data_version_mask;     \
            else                                                               \
                REISERFS_I(inode)->i_flags &= ~i_stat_data_version_mask; })

/* This is an aggressive tail suppression policy, I am hoping it
   improves our benchmarks. The principle behind it is that percentage
   space saving is what matters, not absolute space saving.  This is
   non-intuitive, but it helps to understand it if you consider that the
   cost to access 4 blocks is not much more than the cost to access 1
   block, if you have to do a seek and rotate.  A tail risks a
   non-linear disk access that is significant as a percentage of total
   time cost for a 4 block file and saves an amount of space that is
   less significant as a percentage of space, or so goes the hypothesis.
   -Hans */
#define STORE_TAIL_IN_UNFM_S1(n_file_size,n_tail_size,n_block_size) \
(\
  (!(n_tail_size)) || \
  (((n_tail_size) > MAX_DIRECT_ITEM_LEN(n_block_size)) || \
   ( (n_file_size) >= (n_block_size) * 4 ) || \
   ( ( (n_file_size) >= (n_block_size) * 3 ) && \
     ( (n_tail_size) >=   (MAX_DIRECT_ITEM_LEN(n_block_size))/4) ) || \
   ( ( (n_file_size) >= (n_block_size) * 2 ) && \
     ( (n_tail_size) >=   (MAX_DIRECT_ITEM_LEN(n_block_size))/2) ) || \
   ( ( (n_file_size) >= (n_block_size) ) && \
     ( (n_tail_size) >=   (MAX_DIRECT_ITEM_LEN(n_block_size) * 3)/4) ) ) \
)

/* Another strategy for tails, this one means only create a tail if all the
   file would fit into one DIRECT item.
   Primary intention for this one is to increase performance by decreasing
   seeking.
*/
#define STORE_TAIL_IN_UNFM_S2(n_file_size,n_tail_size,n_block_size) \
(\
  (!(n_tail_size)) || \
  (((n_file_size) > MAX_DIRECT_ITEM_LEN(n_block_size)) ) \
)

/*
 * values for s_umount_state field
 */
#define REISERFS_VALID_FS    1
#define REISERFS_ERROR_FS    2

//
// there are 5 item types currently
//
#define TYPE_STAT_DATA 0
#define TYPE_INDIRECT 1
#define TYPE_DIRECT 2
#define TYPE_DIRENTRY 3
#define TYPE_MAXTYPE 3
#define TYPE_ANY 15		// FIXME: comment is required

/***************************************************************************/
/*                       KEY & ITEM HEAD                                   */
/***************************************************************************/

//
// directories use this key as well as old files
//
struct offset_v1 {
	__le32 k_offset;
	__le32 k_uniqueness;
} __attribute__ ((__packed__));

struct offset_v2 {
	__le64 v;
} __attribute__ ((__packed__));

static inline __u16 offset_v2_k_type(const struct offset_v2 *v2)
{
	__u8 type = le64_to_cpu(v2->v) >> 60;
	return (type <= TYPE_MAXTYPE) ? type : TYPE_ANY;
}

static inline void set_offset_v2_k_type(struct offset_v2 *v2, int type)
{
	v2->v =
	    (v2->v & cpu_to_le64(~0ULL >> 4)) | cpu_to_le64((__u64) type << 60);
}

static inline loff_t offset_v2_k_offset(const struct offset_v2 *v2)
{
	return le64_to_cpu(v2->v) & (~0ULL >> 4);
}

static inline void set_offset_v2_k_offset(struct offset_v2 *v2, loff_t offset)
{
	offset &= (~0ULL >> 4);
	v2->v = (v2->v & cpu_to_le64(15ULL << 60)) | cpu_to_le64(offset);
}

/* Key of an item determines its location in the S+tree, and
   is composed of 4 components */
struct reiserfs_key {
	__le32 k_dir_id;	/* packing locality: by default parent
				   directory object id */
	__le32 k_objectid;	/* object identifier */
	union {
		struct offset_v1 k_offset_v1;
		struct offset_v2 k_offset_v2;
	} __attribute__ ((__packed__)) u;
} __attribute__ ((__packed__));

struct in_core_key {
	__u32 k_dir_id;		/* packing locality: by default parent
				   directory object id */
	__u32 k_objectid;	/* object identifier */
	__u64 k_offset;
	__u8 k_type;
};

struct cpu_key {
	struct in_core_key on_disk_key;
	int version;
	int key_length;		/* 3 in all cases but direct2indirect and
				   indirect2direct conversion */
};

/* Our function for comparing keys can compare keys of different
   lengths.  It takes as a parameter the length of the keys it is to
   compare.  These defines are used in determining what is to be passed
   to it as that parameter. */
#define REISERFS_FULL_KEY_LEN     4
#define REISERFS_SHORT_KEY_LEN    2

/* The result of the key compare */
#define FIRST_GREATER 1
#define SECOND_GREATER -1
#define KEYS_IDENTICAL 0
#define KEY_FOUND 1
#define KEY_NOT_FOUND 0

#define KEY_SIZE (sizeof(struct reiserfs_key))
#define SHORT_KEY_SIZE (sizeof (__u32) + sizeof (__u32))

/* return values for search_by_key and clones */
#define ITEM_FOUND 1
#define ITEM_NOT_FOUND 0
#define ENTRY_FOUND 1
#define ENTRY_NOT_FOUND 0
#define DIRECTORY_NOT_FOUND -1
#define REGULAR_FILE_FOUND -2
#define DIRECTORY_FOUND -3
#define BYTE_FOUND 1
#define BYTE_NOT_FOUND 0
#define FILE_NOT_FOUND -1

#define POSITION_FOUND 1
#define POSITION_NOT_FOUND 0

// return values for reiserfs_find_entry and search_by_entry_key
#define NAME_FOUND 1
#define NAME_NOT_FOUND 0
#define GOTO_PREVIOUS_ITEM 2
#define NAME_FOUND_INVISIBLE 3

/*  Everything in the filesystem is stored as a set of items.  The
    item head contains the key of the item, its free space (for
    indirect items) and specifies the location of the item itself
    within the block.  */

struct item_head {
	/* Everything in the tree is found by searching for it based on
	 * its key.*/
	struct reiserfs_key ih_key;
	union {
		/* The free space in the last unformatted node of an
		   indirect item if this is an indirect item.  This
		   equals 0xFFFF iff this is a direct item or stat data
		   item. Note that the key, not this field, is used to
		   determine the item type, and thus which field this
		   union contains. */
		__le16 ih_free_space_reserved;
		/* Iff this is a directory item, this field equals the
		   number of directory entries in the directory item. */
		__le16 ih_entry_count;
	} __attribute__ ((__packed__)) u;
	__le16 ih_item_len;	/* total size of the item body */
	__le16 ih_item_location;	/* an offset to the item body
					 * within the block */
	__le16 ih_version;	/* 0 for all old items, 2 for new
				   ones. Highest bit is set by fsck
				   temporary, cleaned after all
				   done */
} __attribute__ ((__packed__));
/* size of item header     */
#define IH_SIZE (sizeof(struct item_head))

#define ih_free_space(ih)            le16_to_cpu((ih)->u.ih_free_space_reserved)
#define ih_version(ih)               le16_to_cpu((ih)->ih_version)
#define ih_entry_count(ih)           le16_to_cpu((ih)->u.ih_entry_count)
#define ih_location(ih)              le16_to_cpu((ih)->ih_item_location)
#define ih_item_len(ih)              le16_to_cpu((ih)->ih_item_len)

#define put_ih_free_space(ih, val)   do { (ih)->u.ih_free_space_reserved = cpu_to_le16(val); } while(0)
#define put_ih_version(ih, val)      do { (ih)->ih_version = cpu_to_le16(val); } while (0)
#define put_ih_entry_count(ih, val)  do { (ih)->u.ih_entry_count = cpu_to_le16(val); } while (0)
#define put_ih_location(ih, val)     do { (ih)->ih_item_location = cpu_to_le16(val); } while (0)
#define put_ih_item_len(ih, val)     do { (ih)->ih_item_len = cpu_to_le16(val); } while (0)

#define unreachable_item(ih) (ih_version(ih) & (1 << 15))

#define get_ih_free_space(ih) (ih_version (ih) == KEY_FORMAT_3_6 ? 0 : ih_free_space (ih))
#define set_ih_free_space(ih,val) put_ih_free_space((ih), ((ih_version(ih) == KEY_FORMAT_3_6) ? 0 : (val)))

/* these operate on indirect items, where you've got an array of ints
** at a possibly unaligned location.  These are a noop on ia32
** 
** p is the array of __u32, i is the index into the array, v is the value
** to store there.
*/
#define get_block_num(p, i) get_unaligned_le32((p) + (i))
#define put_block_num(p, i, v) put_unaligned_le32((v), (p) + (i))

//
// in old version uniqueness field shows key type
//
#define V1_SD_UNIQUENESS 0
#define V1_INDIRECT_UNIQUENESS 0xfffffffe
#define V1_DIRECT_UNIQUENESS 0xffffffff
#define V1_DIRENTRY_UNIQUENESS 500
#define V1_ANY_UNIQUENESS 555	// FIXME: comment is required

//
// here are conversion routines
//
static inline int uniqueness2type(__u32 uniqueness) CONSTF;
static inline int uniqueness2type(__u32 uniqueness)
{
	switch ((int)uniqueness) {
	case V1_SD_UNIQUENESS:
		return TYPE_STAT_DATA;
	case V1_INDIRECT_UNIQUENESS:
		return TYPE_INDIRECT;
	case V1_DIRECT_UNIQUENESS:
		return TYPE_DIRECT;
	case V1_DIRENTRY_UNIQUENESS:
		return TYPE_DIRENTRY;
	case V1_ANY_UNIQUENESS:
	default:
		return TYPE_ANY;
	}
}

static inline __u32 type2uniqueness(int type) CONSTF;
static inline __u32 type2uniqueness(int type)
{
	switch (type) {
	case TYPE_STAT_DATA:
		return V1_SD_UNIQUENESS;
	case TYPE_INDIRECT:
		return V1_INDIRECT_UNIQUENESS;
	case TYPE_DIRECT:
		return V1_DIRECT_UNIQUENESS;
	case TYPE_DIRENTRY:
		return V1_DIRENTRY_UNIQUENESS;
	case TYPE_ANY:
	default:
		return V1_ANY_UNIQUENESS;
	}
}

//
// key is pointer to on disk key which is stored in le, result is cpu,
// there is no way to get version of object from key, so, provide
// version to these defines
//
static inline loff_t le_key_k_offset(int version,
				     const struct reiserfs_key *key)
{
	return (version == KEY_FORMAT_3_5) ?
	    le32_to_cpu(key->u.k_offset_v1.k_offset) :
	    offset_v2_k_offset(&(key->u.k_offset_v2));
}

static inline loff_t le_ih_k_offset(const struct item_head *ih)
{
	return le_key_k_offset(ih_version(ih), &(ih->ih_key));
}

static inline loff_t le_key_k_type(int version, const struct reiserfs_key *key)
{
	return (version == KEY_FORMAT_3_5) ?
	    uniqueness2type(le32_to_cpu(key->u.k_offset_v1.k_uniqueness)) :
	    offset_v2_k_type(&(key->u.k_offset_v2));
}

static inline loff_t le_ih_k_type(const struct item_head *ih)
{
	return le_key_k_type(ih_version(ih), &(ih->ih_key));
}

static inline void set_le_key_k_offset(int version, struct reiserfs_key *key,
				       loff_t offset)
{
	(version == KEY_FORMAT_3_5) ? (void)(key->u.k_offset_v1.k_offset = cpu_to_le32(offset)) :	/* jdm check */
	    (void)(set_offset_v2_k_offset(&(key->u.k_offset_v2), offset));
}

static inline void set_le_ih_k_offset(struct item_head *ih, loff_t offset)
{
	set_le_key_k_offset(ih_version(ih), &(ih->ih_key), offset);
}

static inline void set_le_key_k_type(int version, struct reiserfs_key *key,
				     int type)
{
	(version == KEY_FORMAT_3_5) ?
	    (void)(key->u.k_offset_v1.k_uniqueness =
		   cpu_to_le32(type2uniqueness(type)))
	    : (void)(set_offset_v2_k_type(&(key->u.k_offset_v2), type));
}

static inline void set_le_ih_k_type(struct item_head *ih, int type)
{
	set_le_key_k_type(ih_version(ih), &(ih->ih_key), type);
}

static inline int is_direntry_le_key(int version, struct reiserfs_key *key)
{
	return le_key_k_type(version, key) == TYPE_DIRENTRY;
}

static inline int is_direct_le_key(int version, struct reiserfs_key *key)
{
	return le_key_k_type(version, key) == TYPE_DIRECT;
}

static inline int is_indirect_le_key(int version, struct reiserfs_key *key)
{
	return le_key_k_type(version, key) == TYPE_INDIRECT;
}

static inline int is_statdata_le_key(int version, struct reiserfs_key *key)
{
	return le_key_k_type(version, key) == TYPE_STAT_DATA;
}

//
// item header has version.
//
static inline int is_direntry_le_ih(struct item_head *ih)
{
	return is_direntry_le_key(ih_version(ih), &ih->ih_key);
}

static inline int is_direct_le_ih(struct item_head *ih)
{
	return is_direct_le_key(ih_version(ih), &ih->ih_key);
}

static inline int is_indirect_le_ih(struct item_head *ih)
{
	return is_indirect_le_key(ih_version(ih), &ih->ih_key);
}

static inline int is_statdata_le_ih(struct item_head *ih)
{
	return is_statdata_le_key(ih_version(ih), &ih->ih_key);
}

//
// key is pointer to cpu key, result is cpu
//
static inline loff_t cpu_key_k_offset(const struct cpu_key *key)
{
	return key->on_disk_key.k_offset;
}

static inline loff_t cpu_key_k_type(const struct cpu_key *key)
{
	return key->on_disk_key.k_type;
}

static inline void set_cpu_key_k_offset(struct cpu_key *key, loff_t offset)
{
	key->on_disk_key.k_offset = offset;
}

static inline void set_cpu_key_k_type(struct cpu_key *key, int type)
{
	key->on_disk_key.k_type = type;
}

static inline void cpu_key_k_offset_dec(struct cpu_key *key)
{
	key->on_disk_key.k_offset--;
}

#define is_direntry_cpu_key(key) (cpu_key_k_type (key) == TYPE_DIRENTRY)
#define is_direct_cpu_key(key) (cpu_key_k_type (key) == TYPE_DIRECT)
#define is_indirect_cpu_key(key) (cpu_key_k_type (key) == TYPE_INDIRECT)
#define is_statdata_cpu_key(key) (cpu_key_k_type (key) == TYPE_STAT_DATA)

/* are these used ? */
#define is_direntry_cpu_ih(ih) (is_direntry_cpu_key (&((ih)->ih_key)))
#define is_direct_cpu_ih(ih) (is_direct_cpu_key (&((ih)->ih_key)))
#define is_indirect_cpu_ih(ih) (is_indirect_cpu_key (&((ih)->ih_key)))
#define is_statdata_cpu_ih(ih) (is_statdata_cpu_key (&((ih)->ih_key)))

#define I_K_KEY_IN_ITEM(ih, key, n_blocksize) \
    (!COMP_SHORT_KEYS(ih, key) && \
	  I_OFF_BYTE_IN_ITEM(ih, k_offset(key), n_blocksize))

/* maximal length of item */
#define MAX_ITEM_LEN(block_size) (block_size - BLKH_SIZE - IH_SIZE)
#define MIN_ITEM_LEN 1

/* object identifier for root dir */
#define REISERFS_ROOT_OBJECTID 2
#define REISERFS_ROOT_PARENT_OBJECTID 1

extern struct reiserfs_key root_key;

/* 
 * Picture represents a leaf of the S+tree
 *  ______________________________________________________
 * |      |  Array of     |                   |           |
 * |Block |  Object-Item  |      F r e e      |  Objects- |
 * | head |  Headers      |     S p a c e     |   Items   |
 * |______|_______________|___________________|___________|
 */

/* Header of a disk block.  More precisely, header of a formatted leaf
   or internal node, and not the header of an unformatted node. */
struct block_head {
	__le16 blk_level;	/* Level of a block in the tree. */
	__le16 blk_nr_item;	/* Number of keys/items in a block. */
	__le16 blk_free_space;	/* Block free space in bytes. */
	__le16 blk_reserved;
	/* dump this in v4/planA */
	struct reiserfs_key blk_right_delim_key;	/* kept only for compatibility */
};

#define BLKH_SIZE                     (sizeof(struct block_head))
#define blkh_level(p_blkh)            (le16_to_cpu((p_blkh)->blk_level))
#define blkh_nr_item(p_blkh)          (le16_to_cpu((p_blkh)->blk_nr_item))
#define blkh_free_space(p_blkh)       (le16_to_cpu((p_blkh)->blk_free_space))
#define blkh_reserved(p_blkh)         (le16_to_cpu((p_blkh)->blk_reserved))
#define set_blkh_level(p_blkh,val)    ((p_blkh)->blk_level = cpu_to_le16(val))
#define set_blkh_nr_item(p_blkh,val)  ((p_blkh)->blk_nr_item = cpu_to_le16(val))
#define set_blkh_free_space(p_blkh,val) ((p_blkh)->blk_free_space = cpu_to_le16(val))
#define set_blkh_reserved(p_blkh,val) ((p_blkh)->blk_reserved = cpu_to_le16(val))
#define blkh_right_delim_key(p_blkh)  ((p_blkh)->blk_right_delim_key)
#define set_blkh_right_delim_key(p_blkh,val)  ((p_blkh)->blk_right_delim_key = val)

/*
 * values for blk_level field of the struct block_head
 */

#define FREE_LEVEL 0		/* when node gets removed from the tree its
				   blk_level is set to FREE_LEVEL. It is then
				   used to see whether the node is still in the
				   tree */

#define DISK_LEAF_NODE_LEVEL  1	/* Leaf node level. */

/* Given the buffer head of a formatted node, resolve to the block head of that node. */
#define B_BLK_HEAD(bh)			((struct block_head *)((bh)->b_data))
/* Number of items that are in buffer. */
#define B_NR_ITEMS(bh)			(blkh_nr_item(B_BLK_HEAD(bh)))
#define B_LEVEL(bh)			(blkh_level(B_BLK_HEAD(bh)))
#define B_FREE_SPACE(bh)		(blkh_free_space(B_BLK_HEAD(bh)))

#define PUT_B_NR_ITEMS(bh, val)		do { set_blkh_nr_item(B_BLK_HEAD(bh), val); } while (0)
#define PUT_B_LEVEL(bh, val)		do { set_blkh_level(B_BLK_HEAD(bh), val); } while (0)
#define PUT_B_FREE_SPACE(bh, val)	do { set_blkh_free_space(B_BLK_HEAD(bh), val); } while (0)

/* Get right delimiting key. -- little endian */
#define B_PRIGHT_DELIM_KEY(bh)		(&(blk_right_delim_key(B_BLK_HEAD(bh))))

/* Does the buffer contain a disk leaf. */
#define B_IS_ITEMS_LEVEL(bh)		(B_LEVEL(bh) == DISK_LEAF_NODE_LEVEL)

/* Does the buffer contain a disk internal node */
#define B_IS_KEYS_LEVEL(bh)      (B_LEVEL(bh) > DISK_LEAF_NODE_LEVEL \
					    && B_LEVEL(bh) <= MAX_HEIGHT)

/***************************************************************************/
/*                             STAT DATA                                   */
/***************************************************************************/

//
// old stat data is 32 bytes long. We are going to distinguish new one by
// different size
//
struct stat_data_v1 {
	__le16 sd_mode;		/* file type, permissions */
	__le16 sd_nlink;	/* number of hard links */
	__le16 sd_uid;		/* owner */
	__le16 sd_gid;		/* group */
	__le32 sd_size;		/* file size */
	__le32 sd_atime;	/* time of last access */
	__le32 sd_mtime;	/* time file was last modified  */
	__le32 sd_ctime;	/* time inode (stat data) was last changed (except changes to sd_atime and sd_mtime) */
	union {
		__le32 sd_rdev;
		__le32 sd_blocks;	/* number of blocks file uses */
	} __attribute__ ((__packed__)) u;
	__le32 sd_first_direct_byte;	/* first byte of file which is stored
					   in a direct item: except that if it
					   equals 1 it is a symlink and if it
					   equals ~(__u32)0 there is no
					   direct item.  The existence of this
					   field really grates on me. Let's
					   replace it with a macro based on
					   sd_size and our tail suppression
					   policy.  Someday.  -Hans */
} __attribute__ ((__packed__));

#define SD_V1_SIZE              (sizeof(struct stat_data_v1))
#define stat_data_v1(ih)        (ih_version (ih) == KEY_FORMAT_3_5)
#define sd_v1_mode(sdp)         (le16_to_cpu((sdp)->sd_mode))
#define set_sd_v1_mode(sdp,v)   ((sdp)->sd_mode = cpu_to_le16(v))
#define sd_v1_nlink(sdp)        (le16_to_cpu((sdp)->sd_nlink))
#define set_sd_v1_nlink(sdp,v)  ((sdp)->sd_nlink = cpu_to_le16(v))
#define sd_v1_uid(sdp)          (le16_to_cpu((sdp)->sd_uid))
#define set_sd_v1_uid(sdp,v)    ((sdp)->sd_uid = cpu_to_le16(v))
#define sd_v1_gid(sdp)          (le16_to_cpu((sdp)->sd_gid))
#define set_sd_v1_gid(sdp,v)    ((sdp)->sd_gid = cpu_to_le16(v))
#define sd_v1_size(sdp)         (le32_to_cpu((sdp)->sd_size))
#define set_sd_v1_size(sdp,v)   ((sdp)->sd_size = cpu_to_le32(v))
#define sd_v1_atime(sdp)        (le32_to_cpu((sdp)->sd_atime))
#define set_sd_v1_atime(sdp,v)  ((sdp)->sd_atime = cpu_to_le32(v))
#define sd_v1_mtime(sdp)        (le32_to_cpu((sdp)->sd_mtime))
#define set_sd_v1_mtime(sdp,v)  ((sdp)->sd_mtime = cpu_to_le32(v))
#define sd_v1_ctime(sdp)        (le32_to_cpu((sdp)->sd_ctime))
#define set_sd_v1_ctime(sdp,v)  ((sdp)->sd_ctime = cpu_to_le32(v))
#define sd_v1_rdev(sdp)         (le32_to_cpu((sdp)->u.sd_rdev))
#define set_sd_v1_rdev(sdp,v)   ((sdp)->u.sd_rdev = cpu_to_le32(v))
#define sd_v1_blocks(sdp)       (le32_to_cpu((sdp)->u.sd_blocks))
#define set_sd_v1_blocks(sdp,v) ((sdp)->u.sd_blocks = cpu_to_le32(v))
#define sd_v1_first_direct_byte(sdp) \
                                (le32_to_cpu((sdp)->sd_first_direct_byte))
#define set_sd_v1_first_direct_byte(sdp,v) \
                                ((sdp)->sd_first_direct_byte = cpu_to_le32(v))

/* inode flags stored in sd_attrs (nee sd_reserved) */

/* we want common flags to have the same values as in ext2,
   so chattr(1) will work without problems */
#define REISERFS_IMMUTABLE_FL FS_IMMUTABLE_FL
#define REISERFS_APPEND_FL    FS_APPEND_FL
#define REISERFS_SYNC_FL      FS_SYNC_FL
#define REISERFS_NOATIME_FL   FS_NOATIME_FL
#define REISERFS_NODUMP_FL    FS_NODUMP_FL
#define REISERFS_SECRM_FL     FS_SECRM_FL
#define REISERFS_UNRM_FL      FS_UNRM_FL
#define REISERFS_COMPR_FL     FS_COMPR_FL
#define REISERFS_NOTAIL_FL    FS_NOTAIL_FL

/* persistent flags that file inherits from the parent directory */
#define REISERFS_INHERIT_MASK ( REISERFS_IMMUTABLE_FL |	\
				REISERFS_SYNC_FL |	\
				REISERFS_NOATIME_FL |	\
				REISERFS_NODUMP_FL |	\
				REISERFS_SECRM_FL |	\
				REISERFS_COMPR_FL |	\
				REISERFS_NOTAIL_FL )

/* Stat Data on disk (reiserfs version of UFS disk inode minus the
   address blocks) */
struct stat_data {
	__le16 sd_mode;		/* file type, permissions */
	__le16 sd_attrs;	/* persistent inode flags */
	__le32 sd_nlink;	/* number of hard links */
	__le64 sd_size;		/* file size */
	__le32 sd_uid;		/* owner */
	__le32 sd_gid;		/* group */
	__le32 sd_atime;	/* time of last access */
	__le32 sd_mtime;	/* time file was last modified  */
	__le32 sd_ctime;	/* time inode (stat data) was last changed (except changes to sd_atime and sd_mtime) */
	__le32 sd_blocks;
	union {
		__le32 sd_rdev;
		__le32 sd_generation;
		//__le32 sd_first_direct_byte;
		/* first byte of file which is stored in a
		   direct item: except that if it equals 1
		   it is a symlink and if it equals
		   ~(__u32)0 there is no direct item.  The
		   existence of this field really grates
		   on me. Let's replace it with a macro
		   based on sd_size and our tail
		   suppression policy? */
	} __attribute__ ((__packed__)) u;
} __attribute__ ((__packed__));
//
// this is 44 bytes long
//
#define SD_SIZE (sizeof(struct stat_data))
#define SD_V2_SIZE              SD_SIZE
#define stat_data_v2(ih)        (ih_version (ih) == KEY_FORMAT_3_6)
#define sd_v2_mode(sdp)         (le16_to_cpu((sdp)->sd_mode))
#define set_sd_v2_mode(sdp,v)   ((sdp)->sd_mode = cpu_to_le16(v))
/* sd_reserved */
/* set_sd_reserved */
#define sd_v2_nlink(sdp)        (le32_to_cpu((sdp)->sd_nlink))
#define set_sd_v2_nlink(sdp,v)  ((sdp)->sd_nlink = cpu_to_le32(v))
#define sd_v2_size(sdp)         (le64_to_cpu((sdp)->sd_size))
#define set_sd_v2_size(sdp,v)   ((sdp)->sd_size = cpu_to_le64(v))
#define sd_v2_uid(sdp)          (le32_to_cpu((sdp)->sd_uid))
#define set_sd_v2_uid(sdp,v)    ((sdp)->sd_uid = cpu_to_le32(v))
#define sd_v2_gid(sdp)          (le32_to_cpu((sdp)->sd_gid))
#define set_sd_v2_gid(sdp,v)    ((sdp)->sd_gid = cpu_to_le32(v))
#define sd_v2_atime(sdp)        (le32_to_cpu((sdp)->sd_atime))
#define set_sd_v2_atime(sdp,v)  ((sdp)->sd_atime = cpu_to_le32(v))
#define sd_v2_mtime(sdp)        (le32_to_cpu((sdp)->sd_mtime))
#define set_sd_v2_mtime(sdp,v)  ((sdp)->sd_mtime = cpu_to_le32(v))
#define sd_v2_ctime(sdp)        (le32_to_cpu((sdp)->sd_ctime))
#define set_sd_v2_ctime(sdp,v)  ((sdp)->sd_ctime = cpu_to_le32(v))
#define sd_v2_blocks(sdp)       (le32_to_cpu((sdp)->sd_blocks))
#define set_sd_v2_blocks(sdp,v) ((sdp)->sd_blocks = cpu_to_le32(v))
#define sd_v2_rdev(sdp)         (le32_to_cpu((sdp)->u.sd_rdev))
#define set_sd_v2_rdev(sdp,v)   ((sdp)->u.sd_rdev = cpu_to_le32(v))
#define sd_v2_generation(sdp)   (le32_to_cpu((sdp)->u.sd_generation))
#define set_sd_v2_generation(sdp,v) ((sdp)->u.sd_generation = cpu_to_le32(v))
#define sd_v2_attrs(sdp)         (le16_to_cpu((sdp)->sd_attrs))
#define set_sd_v2_attrs(sdp,v)   ((sdp)->sd_attrs = cpu_to_le16(v))

/***************************************************************************/
/*                      DIRECTORY STRUCTURE                                */
/***************************************************************************/
/* 
   Picture represents the structure of directory items
   ________________________________________________
   |  Array of     |   |     |        |       |   |
   | directory     |N-1| N-2 | ....   |   1st |0th|
   | entry headers |   |     |        |       |   |
   |_______________|___|_____|________|_______|___|
                    <----   directory entries         ------>

 First directory item has k_offset component 1. We store "." and ".."
 in one item, always, we never split "." and ".." into differing
 items.  This makes, among other things, the code for removing
 directories simpler. */
#define SD_OFFSET  0
#define SD_UNIQUENESS 0
#define DOT_OFFSET 1
#define DOT_DOT_OFFSET 2
#define DIRENTRY_UNIQUENESS 500

/* */
#define FIRST_ITEM_OFFSET 1

/*
   Q: How to get key of object pointed to by entry from entry?  

   A: Each directory entry has its header. This header has deh_dir_id and deh_objectid fields, those are key
      of object, entry points to */

/* NOT IMPLEMENTED:   
   Directory will someday contain stat data of object */

struct reiserfs_de_head {
	__le32 deh_offset;	/* third component of the directory entry key */
	__le32 deh_dir_id;	/* objectid of the parent directory of the object, that is referenced
				   by directory entry */
	__le32 deh_objectid;	/* objectid of the object, that is referenced by directory entry */
	__le16 deh_location;	/* offset of name in the whole item */
	__le16 deh_state;	/* whether 1) entry contains stat data (for future), and 2) whether
				   entry is hidden (unlinked) */
} __attribute__ ((__packed__));
#define DEH_SIZE                  sizeof(struct reiserfs_de_head)
#define deh_offset(p_deh)         (le32_to_cpu((p_deh)->deh_offset))
#define deh_dir_id(p_deh)         (le32_to_cpu((p_deh)->deh_dir_id))
#define deh_objectid(p_deh)       (le32_to_cpu((p_deh)->deh_objectid))
#define deh_location(p_deh)       (le16_to_cpu((p_deh)->deh_location))
#define deh_state(p_deh)          (le16_to_cpu((p_deh)->deh_state))

#define put_deh_offset(p_deh,v)   ((p_deh)->deh_offset = cpu_to_le32((v)))
#define put_deh_dir_id(p_deh,v)   ((p_deh)->deh_dir_id = cpu_to_le32((v)))
#define put_deh_objectid(p_deh,v) ((p_deh)->deh_objectid = cpu_to_le32((v)))
#define put_deh_location(p_deh,v) ((p_deh)->deh_location = cpu_to_le16((v)))
#define put_deh_state(p_deh,v)    ((p_deh)->deh_state = cpu_to_le16((v)))

/* empty directory contains two entries "." and ".." and their headers */
#define EMPTY_DIR_SIZE \
(DEH_SIZE * 2 + ROUND_UP (strlen (".")) + ROUND_UP (strlen ("..")))

/* old format directories have this size when empty */
#define EMPTY_DIR_SIZE_V1 (DEH_SIZE * 2 + 3)

#define DEH_Statdata 0		/* not used now */
#define DEH_Visible 2

/* 64 bit systems (and the S/390) need to be aligned explicitly -jdm */
#if BITS_PER_LONG == 64 || defined(__s390__) || defined(__hppa__)
#   define ADDR_UNALIGNED_BITS  (3)
#endif

/* These are only used to manipulate deh_state.
 * Because of this, we'll use the ext2_ bit routines,
 * since they are little endian */
#ifdef ADDR_UNALIGNED_BITS

#   define aligned_address(addr)           ((void *)((long)(addr) & ~((1UL << ADDR_UNALIGNED_BITS) - 1)))
#   define unaligned_offset(addr)          (((int)((long)(addr) & ((1 << ADDR_UNALIGNED_BITS) - 1))) << 3)

#   define set_bit_unaligned(nr, addr)	\
	__test_and_set_bit_le((nr) + unaligned_offset(addr), aligned_address(addr))
#   define clear_bit_unaligned(nr, addr)	\
	__test_and_clear_bit_le((nr) + unaligned_offset(addr), aligned_address(addr))
#   define test_bit_unaligned(nr, addr)	\
	test_bit_le((nr) + unaligned_offset(addr), aligned_address(addr))

#else

#   define set_bit_unaligned(nr, addr)	__test_and_set_bit_le(nr, addr)
#   define clear_bit_unaligned(nr, addr)	__test_and_clear_bit_le(nr, addr)
#   define test_bit_unaligned(nr, addr)	test_bit_le(nr, addr)

#endif

#define mark_de_with_sd(deh)        set_bit_unaligned (DEH_Statdata, &((deh)->deh_state))
#define mark_de_without_sd(deh)     clear_bit_unaligned (DEH_Statdata, &((deh)->deh_state))
#define mark_de_visible(deh)	    set_bit_unaligned (DEH_Visible, &((deh)->deh_state))
#define mark_de_hidden(deh)	    clear_bit_unaligned (DEH_Visible, &((deh)->deh_state))

#define de_with_sd(deh)		    test_bit_unaligned (DEH_Statdata, &((deh)->deh_state))
#define de_visible(deh)	    	    test_bit_unaligned (DEH_Visible, &((deh)->deh_state))
#define de_hidden(deh)	    	    !test_bit_unaligned (DEH_Visible, &((deh)->deh_state))

extern void make_empty_dir_item_v1(char *body, __le32 dirid, __le32 objid,
				   __le32 par_dirid, __le32 par_objid);
extern void make_empty_dir_item(char *body, __le32 dirid, __le32 objid,
				__le32 par_dirid, __le32 par_objid);

/* array of the entry headers */
 /* get item body */
#define B_I_PITEM(bh,ih) ( (bh)->b_data + ih_location(ih) )
#define B_I_DEH(bh,ih) ((struct reiserfs_de_head *)(B_I_PITEM(bh,ih)))

/* length of the directory entry in directory item. This define
   calculates length of i-th directory entry using directory entry
   locations from dir entry head. When it calculates length of 0-th
   directory entry, it uses length of whole item in place of entry
   location of the non-existent following entry in the calculation.
   See picture above.*/
/*
#define I_DEH_N_ENTRY_LENGTH(ih,deh,i) \
((i) ? (deh_location((deh)-1) - deh_location((deh))) : (ih_item_len((ih)) - deh_location((deh))))
*/
static inline int entry_length(const struct buffer_head *bh,
			       const struct item_head *ih, int pos_in_item)
{
	struct reiserfs_de_head *deh;

	deh = B_I_DEH(bh, ih) + pos_in_item;
	if (pos_in_item)
		return deh_location(deh - 1) - deh_location(deh);

	return ih_item_len(ih) - deh_location(deh);
}

/* number of entries in the directory item, depends on ENTRY_COUNT being at the start of directory dynamic data. */
#define I_ENTRY_COUNT(ih) (ih_entry_count((ih)))

/* name by bh, ih and entry_num */
#define B_I_E_NAME(bh,ih,entry_num) ((char *)(bh->b_data + ih_location(ih) + deh_location(B_I_DEH(bh,ih)+(entry_num))))

// two entries per block (at least)
#define REISERFS_MAX_NAME(block_size) 255

/* this structure is used for operations on directory entries. It is
   not a disk structure. */
/* When reiserfs_find_entry or search_by_entry_key find directory
   entry, they return filled reiserfs_dir_entry structure */
struct reiserfs_dir_entry {
	struct buffer_head *de_bh;
	int de_item_num;
	struct item_head *de_ih;
	int de_entry_num;
	struct reiserfs_de_head *de_deh;
	int de_entrylen;
	int de_namelen;
	char *de_name;
	unsigned long *de_gen_number_bit_string;

	__u32 de_dir_id;
	__u32 de_objectid;

	struct cpu_key de_entry_key;
};

/* these defines are useful when a particular member of a reiserfs_dir_entry is needed */

/* pointer to file name, stored in entry */
#define B_I_DEH_ENTRY_FILE_NAME(bh,ih,deh) (B_I_PITEM (bh, ih) + deh_location(deh))

/* length of name */
#define I_DEH_N_ENTRY_FILE_NAME_LENGTH(ih,deh,entry_num) \
(I_DEH_N_ENTRY_LENGTH (ih, deh, entry_num) - (de_with_sd (deh) ? SD_SIZE : 0))

/* hash value occupies bits from 7 up to 30 */
#define GET_HASH_VALUE(offset) ((offset) & 0x7fffff80LL)
/* generation number occupies 7 bits starting from 0 up to 6 */
#define GET_GENERATION_NUMBER(offset) ((offset) & 0x7fLL)
#define MAX_GENERATION_NUMBER  127

#define SET_GENERATION_NUMBER(offset,gen_number) (GET_HASH_VALUE(offset)|(gen_number))

/*
 * Picture represents an internal node of the reiserfs tree
 *  ______________________________________________________
 * |      |  Array of     |  Array of         |  Free     |
 * |block |    keys       |  pointers         | space     |
 * | head |      N        |      N+1          |           |
 * |______|_______________|___________________|___________|
 */

/***************************************************************************/
/*                      DISK CHILD                                         */
/***************************************************************************/
/* Disk child pointer: The pointer from an internal node of the tree
   to a node that is on disk. */
struct disk_child {
	__le32 dc_block_number;	/* Disk child's block number. */
	__le16 dc_size;		/* Disk child's used space.   */
	__le16 dc_reserved;
};

#define DC_SIZE (sizeof(struct disk_child))
#define dc_block_number(dc_p)	(le32_to_cpu((dc_p)->dc_block_number))
#define dc_size(dc_p)		(le16_to_cpu((dc_p)->dc_size))
#define put_dc_block_number(dc_p, val)   do { (dc_p)->dc_block_number = cpu_to_le32(val); } while(0)
#define put_dc_size(dc_p, val)   do { (dc_p)->dc_size = cpu_to_le16(val); } while(0)

/* Get disk child by buffer header and position in the tree node. */
#define B_N_CHILD(bh, n_pos)  ((struct disk_child *)\
((bh)->b_data + BLKH_SIZE + B_NR_ITEMS(bh) * KEY_SIZE + DC_SIZE * (n_pos)))

/* Get disk child number by buffer header and position in the tree node. */
#define B_N_CHILD_NUM(bh, n_pos) (dc_block_number(B_N_CHILD(bh, n_pos)))
#define PUT_B_N_CHILD_NUM(bh, n_pos, val) \
				(put_dc_block_number(B_N_CHILD(bh, n_pos), val))

 /* maximal value of field child_size in structure disk_child */
 /* child size is the combined size of all items and their headers */
#define MAX_CHILD_SIZE(bh) ((int)( (bh)->b_size - BLKH_SIZE ))

/* amount of used space in buffer (not including block head) */
#define B_CHILD_SIZE(cur) (MAX_CHILD_SIZE(cur)-(B_FREE_SPACE(cur)))

/* max and min number of keys in internal node */
#define MAX_NR_KEY(bh) ( (MAX_CHILD_SIZE(bh)-DC_SIZE)/(KEY_SIZE+DC_SIZE) )
#define MIN_NR_KEY(bh)    (MAX_NR_KEY(bh)/2)

/***************************************************************************/
/*                      PATH STRUCTURES AND DEFINES                        */
/***************************************************************************/

/* Search_by_key fills up the path from the root to the leaf as it descends the tree looking for the
   key.  It uses reiserfs_bread to try to find buffers in the cache given their block number.  If it
   does not find them in the cache it reads them from disk.  For each node search_by_key finds using
   reiserfs_bread it then uses bin_search to look through that node.  bin_search will find the
   position of the block_number of the next node if it is looking through an internal node.  If it
   is looking through a leaf node bin_search will find the position of the item which has key either
   equal to given key, or which is the maximal key less than the given key. */

struct path_element {
	struct buffer_head *pe_buffer;	/* Pointer to the buffer at the path in the tree. */
	int pe_position;	/* Position in the tree node which is placed in the */
	/* buffer above.                                  */
};

#define MAX_HEIGHT 5		/* maximal height of a tree. don't change this without changing JOURNAL_PER_BALANCE_CNT */
#define EXTENDED_MAX_HEIGHT         7	/* Must be equals MAX_HEIGHT + FIRST_PATH_ELEMENT_OFFSET */
#define FIRST_PATH_ELEMENT_OFFSET   2	/* Must be equal to at least 2. */

#define ILLEGAL_PATH_ELEMENT_OFFSET 1	/* Must be equal to FIRST_PATH_ELEMENT_OFFSET - 1 */
#define MAX_FEB_SIZE 6		/* this MUST be MAX_HEIGHT + 1. See about FEB below */

/* We need to keep track of who the ancestors of nodes are.  When we
   perform a search we record which nodes were visited while
   descending the tree looking for the node we searched for. This list
   of nodes is called the path.  This information is used while
   performing balancing.  Note that this path information may become
   invalid, and this means we must check it when using it to see if it
   is still valid. You'll need to read search_by_key and the comments
   in it, especially about decrement_counters_in_path(), to understand
   this structure.  

Paths make the code so much harder to work with and debug.... An
enormous number of bugs are due to them, and trying to write or modify
code that uses them just makes my head hurt.  They are based on an
excessive effort to avoid disturbing the precious VFS code.:-( The
gods only know how we are going to SMP the code that uses them.
znodes are the way! */

#define PATH_READA	0x1	/* do read ahead */
#define PATH_READA_BACK 0x2	/* read backwards */

struct treepath {
	int path_length;	/* Length of the array above.   */
	int reada;
	struct path_element path_elements[EXTENDED_MAX_HEIGHT];	/* Array of the path elements.  */
	int pos_in_item;
};

#define pos_in_item(path) ((path)->pos_in_item)

#define INITIALIZE_PATH(var) \
struct treepath var = {.path_length = ILLEGAL_PATH_ELEMENT_OFFSET, .reada = 0,}

/* Get path element by path and path position. */
#define PATH_OFFSET_PELEMENT(path, n_offset)  ((path)->path_elements + (n_offset))

/* Get buffer header at the path by path and path position. */
#define PATH_OFFSET_PBUFFER(path, n_offset)   (PATH_OFFSET_PELEMENT(path, n_offset)->pe_buffer)

/* Get position in the element at the path by path and path position. */
#define PATH_OFFSET_POSITION(path, n_offset) (PATH_OFFSET_PELEMENT(path, n_offset)->pe_position)

#define PATH_PLAST_BUFFER(path) (PATH_OFFSET_PBUFFER((path), (path)->path_length))
				/* you know, to the person who didn't
				   write this the macro name does not
				   at first suggest what it does.
				   Maybe POSITION_FROM_PATH_END? Or
				   maybe we should just focus on
				   dumping paths... -Hans */
#define PATH_LAST_POSITION(path) (PATH_OFFSET_POSITION((path), (path)->path_length))

#define PATH_PITEM_HEAD(path)    B_N_PITEM_HEAD(PATH_PLAST_BUFFER(path), PATH_LAST_POSITION(path))

/* in do_balance leaf has h == 0 in contrast with path structure,
   where root has level == 0. That is why we need these defines */
#define PATH_H_PBUFFER(path, h) PATH_OFFSET_PBUFFER (path, path->path_length - (h))	/* tb->S[h] */
#define PATH_H_PPARENT(path, h) PATH_H_PBUFFER (path, (h) + 1)	/* tb->F[h] or tb->S[0]->b_parent */
#define PATH_H_POSITION(path, h) PATH_OFFSET_POSITION (path, path->path_length - (h))
#define PATH_H_B_ITEM_ORDER(path, h) PATH_H_POSITION(path, h + 1)	/* tb->S[h]->b_item_order */

#define PATH_H_PATH_OFFSET(path, n_h) ((path)->path_length - (n_h))

#define get_last_bh(path) PATH_PLAST_BUFFER(path)
#define get_ih(path) PATH_PITEM_HEAD(path)
#define get_item_pos(path) PATH_LAST_POSITION(path)
#define get_item(path) ((void *)B_N_PITEM(PATH_PLAST_BUFFER(path), PATH_LAST_POSITION (path)))
#define item_moved(ih,path) comp_items(ih, path)
#define path_changed(ih,path) comp_items (ih, path)

/***************************************************************************/
/*                       MISC                                              */
/***************************************************************************/

/* Size of pointer to the unformatted node. */
#define UNFM_P_SIZE (sizeof(unp_t))
#define UNFM_P_SHIFT 2

// in in-core inode key is stored on le form
#define INODE_PKEY(inode) ((struct reiserfs_key *)(REISERFS_I(inode)->i_key))

#define MAX_UL_INT 0xffffffff
#define MAX_INT    0x7ffffff
#define MAX_US_INT 0xffff

// reiserfs version 2 has max offset 60 bits. Version 1 - 32 bit offset
#define U32_MAX (~(__u32)0)

static inline loff_t max_reiserfs_offset(struct inode *inode)
{
	if (get_inode_item_key_version(inode) == KEY_FORMAT_3_5)
		return (loff_t) U32_MAX;

	return (loff_t) ((~(__u64) 0) >> 4);
}

/*#define MAX_KEY_UNIQUENESS	MAX_UL_INT*/
#define MAX_KEY_OBJECTID	MAX_UL_INT

#define MAX_B_NUM  MAX_UL_INT
#define MAX_FC_NUM MAX_US_INT

/* the purpose is to detect overflow of an unsigned short */
#define REISERFS_LINK_MAX (MAX_US_INT - 1000)

/* The following defines are used in reiserfs_insert_item and reiserfs_append_item  */
#define REISERFS_KERNEL_MEM		0	/* reiserfs kernel memory mode  */
#define REISERFS_USER_MEM		1	/* reiserfs user memory mode            */

#define fs_generation(s) (REISERFS_SB(s)->s_generation_counter)
#define get_generation(s) atomic_read (&fs_generation(s))
#define FILESYSTEM_CHANGED_TB(tb)  (get_generation((tb)->tb_sb) != (tb)->fs_gen)
#define __fs_changed(gen,s) (gen != get_generation (s))
#define fs_changed(gen,s)		\
({					\
	reiserfs_cond_resched(s);	\
	__fs_changed(gen, s);		\
})

/***************************************************************************/
/*                  FIXATE NODES                                           */
/***************************************************************************/

#define VI_TYPE_LEFT_MERGEABLE 1
#define VI_TYPE_RIGHT_MERGEABLE 2

/* To make any changes in the tree we always first find node, that
   contains item to be changed/deleted or place to insert a new
   item. We call this node S. To do balancing we need to decide what
   we will shift to left/right neighbor, or to a new node, where new
   item will be etc. To make this analysis simpler we build virtual
   node. Virtual node is an array of items, that will replace items of
   node S. (For instance if we are going to delete an item, virtual
   node does not contain it). Virtual node keeps information about
   item sizes and types, mergeability of first and last items, sizes
   of all entries in directory item. We use this array of items when
   calculating what we can shift to neighbors and how many nodes we
   have to have if we do not any shiftings, if we shift to left/right
   neighbor or to both. */
struct virtual_item {
	int vi_index;		// index in the array of item operations
	unsigned short vi_type;	// left/right mergeability
	unsigned short vi_item_len;	/* length of item that it will have after balancing */
	struct item_head *vi_ih;
	const char *vi_item;	// body of item (old or new)
	const void *vi_new_data;	// 0 always but paste mode
	void *vi_uarea;		// item specific area
};

struct virtual_node {
	char *vn_free_ptr;	/* this is a pointer to the free space in the buffer */
	unsigned short vn_nr_item;	/* number of items in virtual node */
	short vn_size;		/* size of node , that node would have if it has unlimited size and no balancing is performed */
	short vn_mode;		/* mode of balancing (paste, insert, delete, cut) */
	short vn_affected_item_num;
	short vn_pos_in_item;
	struct item_head *vn_ins_ih;	/* item header of inserted item, 0 for other modes */
	const void *vn_data;
	struct virtual_item *vn_vi;	/* array of items (including a new one, excluding item to be deleted) */
};

/* used by directory items when creating virtual nodes */
struct direntry_uarea {
	int flags;
	__u16 entry_count;
	__u16 entry_sizes[1];
} __attribute__ ((__packed__));

/***************************************************************************/
/*                  TREE BALANCE                                           */
/***************************************************************************/

/* This temporary structure is used in tree balance algorithms, and
   constructed as we go to the extent that its various parts are
   needed.  It contains arrays of nodes that can potentially be
   involved in the balancing of node S, and parameters that define how
   each of the nodes must be balanced.  Note that in these algorithms
   for balancing the worst case is to need to balance the current node
   S and the left and right neighbors and all of their parents plus
   create a new node.  We implement S1 balancing for the leaf nodes
   and S0 balancing for the internal nodes (S1 and S0 are defined in
   our papers.)*/

#define MAX_FREE_BLOCK 7	/* size of the array of buffers to free at end of do_balance */

/* maximum number of FEB blocknrs on a single level */
#define MAX_AMOUNT_NEEDED 2

/* someday somebody will prefix every field in this struct with tb_ */
struct tree_balance {
	int tb_mode;
	int need_balance_dirty;
	struct super_block *tb_sb;
	struct reiserfs_transaction_handle *transaction_handle;
	struct treepath *tb_path;
	struct buffer_head *L[MAX_HEIGHT];	/* array of left neighbors of nodes in the path */
	struct buffer_head *R[MAX_HEIGHT];	/* array of right neighbors of nodes in the path */
	struct buffer_head *FL[MAX_HEIGHT];	/* array of fathers of the left  neighbors      */
	struct buffer_head *FR[MAX_HEIGHT];	/* array of fathers of the right neighbors      */
	struct buffer_head *CFL[MAX_HEIGHT];	/* array of common parents of center node and its left neighbor  */
	struct buffer_head *CFR[MAX_HEIGHT];	/* array of common parents of center node and its right neighbor */

	struct buffer_head *FEB[MAX_FEB_SIZE];	/* array of empty buffers. Number of buffers in array equals
						   cur_blknum. */
	struct buffer_head *used[MAX_FEB_SIZE];
	struct buffer_head *thrown[MAX_FEB_SIZE];
	int lnum[MAX_HEIGHT];	/* array of number of items which must be
				   shifted to the left in order to balance the
				   current node; for leaves includes item that
				   will be partially shifted; for internal
				   nodes, it is the number of child pointers
				   rather than items. It includes the new item
				   being created. The code sometimes subtracts
				   one to get the number of wholly shifted
				   items for other purposes. */
	int rnum[MAX_HEIGHT];	/* substitute right for left in comment above */
	int lkey[MAX_HEIGHT];	/* array indexed by height h mapping the key delimiting L[h] and
				   S[h] to its item number within the node CFL[h] */
	int rkey[MAX_HEIGHT];	/* substitute r for l in comment above */
	int insert_size[MAX_HEIGHT];	/* the number of bytes by we are trying to add or remove from
					   S[h]. A negative value means removing.  */
	int blknum[MAX_HEIGHT];	/* number of nodes that will replace node S[h] after
				   balancing on the level h of the tree.  If 0 then S is
				   being deleted, if 1 then S is remaining and no new nodes
				   are being created, if 2 or 3 then 1 or 2 new nodes is
				   being created */

	/* fields that are used only for balancing leaves of the tree */
	int cur_blknum;		/* number of empty blocks having been already allocated                 */
	int s0num;		/* number of items that fall into left most  node when S[0] splits     */
	int s1num;		/* number of items that fall into first  new node when S[0] splits     */
	int s2num;		/* number of items that fall into second new node when S[0] splits     */
	int lbytes;		/* number of bytes which can flow to the left neighbor from the        left    */
	/* most liquid item that cannot be shifted from S[0] entirely         */
	/* if -1 then nothing will be partially shifted */
	int rbytes;		/* number of bytes which will flow to the right neighbor from the right        */
	/* most liquid item that cannot be shifted from S[0] entirely         */
	/* if -1 then nothing will be partially shifted                           */
	int s1bytes;		/* number of bytes which flow to the first  new node when S[0] splits   */
	/* note: if S[0] splits into 3 nodes, then items do not need to be cut  */
	int s2bytes;
	struct buffer_head *buf_to_free[MAX_FREE_BLOCK];	/* buffers which are to be freed after do_balance finishes by unfix_nodes */
	char *vn_buf;		/* kmalloced memory. Used to create
				   virtual node and keep map of
				   dirtied bitmap blocks */
	int vn_buf_size;	/* size of the vn_buf */
	struct virtual_node *tb_vn;	/* VN starts after bitmap of bitmap blocks */

	int fs_gen;		/* saved value of `reiserfs_generation' counter
				   see FILESYSTEM_CHANGED() macro in reiserfs_fs.h */
#ifdef DISPLACE_NEW_PACKING_LOCALITIES
	struct in_core_key key;	/* key pointer, to pass to block allocator or
				   another low-level subsystem */
#endif
};

/* These are modes of balancing */

/* When inserting an item. */
#define M_INSERT	'i'
/* When inserting into (directories only) or appending onto an already
   existent item. */
#define M_PASTE		'p'
/* When deleting an item. */
#define M_DELETE	'd'
/* When truncating an item or removing an entry from a (directory) item. */
#define M_CUT 		'c'

/* used when balancing on leaf level skipped (in reiserfsck) */
#define M_INTERNAL	'n'

/* When further balancing is not needed, then do_balance does not need
   to be called. */
#define M_SKIP_BALANCING 		's'
#define M_CONVERT	'v'

/* modes of leaf_move_items */
#define LEAF_FROM_S_TO_L 0
#define LEAF_FROM_S_TO_R 1
#define LEAF_FROM_R_TO_L 2
#define LEAF_FROM_L_TO_R 3
#define LEAF_FROM_S_TO_SNEW 4

#define FIRST_TO_LAST 0
#define LAST_TO_FIRST 1

/* used in do_balance for passing parent of node information that has
   been gotten from tb struct */
struct buffer_info {
	struct tree_balance *tb;
	struct buffer_head *bi_bh;
	struct buffer_head *bi_parent;
	int bi_position;
};

static inline struct super_block *sb_from_tb(struct tree_balance *tb)
{
	return tb ? tb->tb_sb : NULL;
}

static inline struct super_block *sb_from_bi(struct buffer_info *bi)
{
	return bi ? sb_from_tb(bi->tb) : NULL;
}

/* there are 4 types of items: stat data, directory item, indirect, direct.
+-------------------+------------+--------------+------------+
|	            |  k_offset  | k_uniqueness | mergeable? |
+-------------------+------------+--------------+------------+
|     stat data     |	0        |      0       |   no       |
+-------------------+------------+--------------+------------+
| 1st directory item| DOT_OFFSET |DIRENTRY_UNIQUENESS|   no       | 
| non 1st directory | hash value |              |   yes      |
|     item          |            |              |            |
+-------------------+------------+--------------+------------+
| indirect item     | offset + 1 |TYPE_INDIRECT |   if this is not the first indirect item of the object
+-------------------+------------+--------------+------------+
| direct item       | offset + 1 |TYPE_DIRECT   | if not this is not the first direct item of the object
+-------------------+------------+--------------+------------+
*/

struct item_operations {
	int (*bytes_number) (struct item_head * ih, int block_size);
	void (*decrement_key) (struct cpu_key *);
	int (*is_left_mergeable) (struct reiserfs_key * ih,
				  unsigned long bsize);
	void (*print_item) (struct item_head *, char *item);
	void (*check_item) (struct item_head *, char *item);

	int (*create_vi) (struct virtual_node * vn, struct virtual_item * vi,
			  int is_affected, int insert_size);
	int (*check_left) (struct virtual_item * vi, int free,
			   int start_skip, int end_skip);
	int (*check_right) (struct virtual_item * vi, int free);
	int (*part_size) (struct virtual_item * vi, int from, int to);
	int (*unit_num) (struct virtual_item * vi);
	void (*print_vi) (struct virtual_item * vi);
};

extern struct item_operations *item_ops[TYPE_ANY + 1];

#define op_bytes_number(ih,bsize)                    item_ops[le_ih_k_type (ih)]->bytes_number (ih, bsize)
#define op_is_left_mergeable(key,bsize)              item_ops[le_key_k_type (le_key_version (key), key)]->is_left_mergeable (key, bsize)
#define op_print_item(ih,item)                       item_ops[le_ih_k_type (ih)]->print_item (ih, item)
#define op_check_item(ih,item)                       item_ops[le_ih_k_type (ih)]->check_item (ih, item)
#define op_create_vi(vn,vi,is_affected,insert_size)  item_ops[le_ih_k_type ((vi)->vi_ih)]->create_vi (vn,vi,is_affected,insert_size)
#define op_check_left(vi,free,start_skip,end_skip) item_ops[(vi)->vi_index]->check_left (vi, free, start_skip, end_skip)
#define op_check_right(vi,free)                      item_ops[(vi)->vi_index]->check_right (vi, free)
#define op_part_size(vi,from,to)                     item_ops[(vi)->vi_index]->part_size (vi, from, to)
#define op_unit_num(vi)				     item_ops[(vi)->vi_index]->unit_num (vi)
#define op_print_vi(vi)                              item_ops[(vi)->vi_index]->print_vi (vi)

#define COMP_SHORT_KEYS comp_short_keys

/* number of blocks pointed to by the indirect item */
#define I_UNFM_NUM(ih)	(ih_item_len(ih) / UNFM_P_SIZE)

/* the used space within the unformatted node corresponding to pos within the item pointed to by ih */
#define I_POS_UNFM_SIZE(ih,pos,size) (((pos) == I_UNFM_NUM(ih) - 1 ) ? (size) - ih_free_space(ih) : (size))

/* number of bytes contained by the direct item or the unformatted nodes the indirect item points to */

/* get the item header */
#define B_N_PITEM_HEAD(bh,item_num) ( (struct item_head * )((bh)->b_data + BLKH_SIZE) + (item_num) )

/* get key */
#define B_N_PDELIM_KEY(bh,item_num) ( (struct reiserfs_key * )((bh)->b_data + BLKH_SIZE) + (item_num) )

/* get the key */
#define B_N_PKEY(bh,item_num) ( &(B_N_PITEM_HEAD(bh,item_num)->ih_key) )

/* get item body */
#define B_N_PITEM(bh,item_num) ( (bh)->b_data + ih_location(B_N_PITEM_HEAD((bh),(item_num))))

/* get the stat data by the buffer header and the item order */
#define B_N_STAT_DATA(bh,nr) \
( (struct stat_data *)((bh)->b_data + ih_location(B_N_PITEM_HEAD((bh),(nr))) ) )

    /* following defines use reiserfs buffer header and item header */

/* get stat-data */
#define B_I_STAT_DATA(bh, ih) ( (struct stat_data * )((bh)->b_data + ih_location(ih)) )

// this is 3976 for size==4096
#define MAX_DIRECT_ITEM_LEN(size) ((size) - BLKH_SIZE - 2*IH_SIZE - SD_SIZE - UNFM_P_SIZE)

/* indirect items consist of entries which contain blocknrs, pos
   indicates which entry, and B_I_POS_UNFM_POINTER resolves to the
   blocknr contained by the entry pos points to */
#define B_I_POS_UNFM_POINTER(bh,ih,pos) le32_to_cpu(*(((unp_t *)B_I_PITEM(bh,ih)) + (pos)))
#define PUT_B_I_POS_UNFM_POINTER(bh,ih,pos, val) do {*(((unp_t *)B_I_PITEM(bh,ih)) + (pos)) = cpu_to_le32(val); } while (0)

struct reiserfs_iget_args {
	__u32 objectid;
	__u32 dirid;
};

/***************************************************************************/
/*                    FUNCTION DECLARATIONS                                */
/***************************************************************************/

#define get_journal_desc_magic(bh) (bh->b_data + bh->b_size - 12)

#define journal_trans_half(blocksize) \
	((blocksize - sizeof (struct reiserfs_journal_desc) + sizeof (__u32) - 12) / sizeof (__u32))

/* journal.c see journal.c for all the comments here */

/* first block written in a commit.  */
struct reiserfs_journal_desc {
	__le32 j_trans_id;	/* id of commit */
	__le32 j_len;		/* length of commit. len +1 is the commit block */
	__le32 j_mount_id;	/* mount id of this trans */
	__le32 j_realblock[1];	/* real locations for each block */
};

#define get_desc_trans_id(d)   le32_to_cpu((d)->j_trans_id)
#define get_desc_trans_len(d)  le32_to_cpu((d)->j_len)
#define get_desc_mount_id(d)   le32_to_cpu((d)->j_mount_id)

#define set_desc_trans_id(d,val)       do { (d)->j_trans_id = cpu_to_le32 (val); } while (0)
#define set_desc_trans_len(d,val)      do { (d)->j_len = cpu_to_le32 (val); } while (0)
#define set_desc_mount_id(d,val)       do { (d)->j_mount_id = cpu_to_le32 (val); } while (0)

/* last block written in a commit */
struct reiserfs_journal_commit {
	__le32 j_trans_id;	/* must match j_trans_id from the desc block */
	__le32 j_len;		/* ditto */
	__le32 j_realblock[1];	/* real locations for each block */
};

#define get_commit_trans_id(c) le32_to_cpu((c)->j_trans_id)
#define get_commit_trans_len(c)        le32_to_cpu((c)->j_len)
#define get_commit_mount_id(c) le32_to_cpu((c)->j_mount_id)

#define set_commit_trans_id(c,val)     do { (c)->j_trans_id = cpu_to_le32 (val); } while (0)
#define set_commit_trans_len(c,val)    do { (c)->j_len = cpu_to_le32 (val); } while (0)

/* this header block gets written whenever a transaction is considered fully flushed, and is more recent than the
** last fully flushed transaction.  fully flushed means all the log blocks and all the real blocks are on disk,
** and this transaction does not need to be replayed.
*/
struct reiserfs_journal_header {
	__le32 j_last_flush_trans_id;	/* id of last fully flushed transaction */
	__le32 j_first_unflushed_offset;	/* offset in the log of where to start replay after a crash */
	__le32 j_mount_id;
	/* 12 */ struct journal_params jh_journal;
};

/* biggest tunable defines are right here */
#define JOURNAL_BLOCK_COUNT 8192	/* number of blocks in the journal */
#define JOURNAL_TRANS_MAX_DEFAULT 1024	/* biggest possible single transaction, don't change for now (8/3/99) */
#define JOURNAL_TRANS_MIN_DEFAULT 256
#define JOURNAL_MAX_BATCH_DEFAULT   900	/* max blocks to batch into one transaction, don't make this any bigger than 900 */
#define JOURNAL_MIN_RATIO 2
#define JOURNAL_MAX_COMMIT_AGE 30
#define JOURNAL_MAX_TRANS_AGE 30
#define JOURNAL_PER_BALANCE_CNT (3 * (MAX_HEIGHT-2) + 9)
#define JOURNAL_BLOCKS_PER_OBJECT(sb)  (JOURNAL_PER_BALANCE_CNT * 3 + \
					 2 * (REISERFS_QUOTA_INIT_BLOCKS(sb) + \
					      REISERFS_QUOTA_TRANS_BLOCKS(sb)))

#ifdef CONFIG_QUOTA
/* We need to update data and inode (atime) */
#define REISERFS_QUOTA_TRANS_BLOCKS(s) (REISERFS_SB(s)->s_mount_opt & (1<<REISERFS_QUOTA) ? 2 : 0)
/* 1 balancing, 1 bitmap, 1 data per write + stat data update */
#define REISERFS_QUOTA_INIT_BLOCKS(s) (REISERFS_SB(s)->s_mount_opt & (1<<REISERFS_QUOTA) ? \
(DQUOT_INIT_ALLOC*(JOURNAL_PER_BALANCE_CNT+2)+DQUOT_INIT_REWRITE+1) : 0)
/* same as with INIT */
#define REISERFS_QUOTA_DEL_BLOCKS(s) (REISERFS_SB(s)->s_mount_opt & (1<<REISERFS_QUOTA) ? \
(DQUOT_DEL_ALLOC*(JOURNAL_PER_BALANCE_CNT+2)+DQUOT_DEL_REWRITE+1) : 0)
#else
#define REISERFS_QUOTA_TRANS_BLOCKS(s) 0
#define REISERFS_QUOTA_INIT_BLOCKS(s) 0
#define REISERFS_QUOTA_DEL_BLOCKS(s) 0
#endif

/* both of these can be as low as 1, or as high as you want.  The min is the
** number of 4k bitmap nodes preallocated on mount. New nodes are allocated
** as needed, and released when transactions are committed.  On release, if 
** the current number of nodes is > max, the node is freed, otherwise, 
** it is put on a free list for faster use later.
*/
#define REISERFS_MIN_BITMAP_NODES 10
#define REISERFS_MAX_BITMAP_NODES 100

#define JBH_HASH_SHIFT 13	/* these are based on journal hash size of 8192 */
#define JBH_HASH_MASK 8191

#define _jhashfn(sb,block)	\
	(((unsigned long)sb>>L1_CACHE_SHIFT) ^ \
	 (((block)<<(JBH_HASH_SHIFT - 6)) ^ ((block) >> 13) ^ ((block) << (JBH_HASH_SHIFT - 12))))
#define journal_hash(t,sb,block) ((t)[_jhashfn((sb),(block)) & JBH_HASH_MASK])

// We need these to make journal.c code more readable
#define journal_find_get_block(s, block) __find_get_block(SB_JOURNAL(s)->j_dev_bd, block, s->s_blocksize)
#define journal_getblk(s, block) __getblk(SB_JOURNAL(s)->j_dev_bd, block, s->s_blocksize)
#define journal_bread(s, block) __bread(SB_JOURNAL(s)->j_dev_bd, block, s->s_blocksize)

enum reiserfs_bh_state_bits {
	BH_JDirty = BH_PrivateStart,	/* buffer is in current transaction */
	BH_JDirty_wait,
	BH_JNew,		/* disk block was taken off free list before
				 * being in a finished transaction, or
				 * written to disk. Can be reused immed. */
	BH_JPrepared,
	BH_JRestore_dirty,
	BH_JTest,		// debugging only will go away
};

BUFFER_FNS(JDirty, journaled);
TAS_BUFFER_FNS(JDirty, journaled);
BUFFER_FNS(JDirty_wait, journal_dirty);
TAS_BUFFER_FNS(JDirty_wait, journal_dirty);
BUFFER_FNS(JNew, journal_new);
TAS_BUFFER_FNS(JNew, journal_new);
BUFFER_FNS(JPrepared, journal_prepared);
TAS_BUFFER_FNS(JPrepared, journal_prepared);
BUFFER_FNS(JRestore_dirty, journal_restore_dirty);
TAS_BUFFER_FNS(JRestore_dirty, journal_restore_dirty);
BUFFER_FNS(JTest, journal_test);
TAS_BUFFER_FNS(JTest, journal_test);

/*
** transaction handle which is passed around for all journal calls
*/
struct reiserfs_transaction_handle {
	struct super_block *t_super;	/* super for this FS when journal_begin was
					   called. saves calls to reiserfs_get_super
					   also used by nested transactions to make
					   sure they are nesting on the right FS
					   _must_ be first in the handle
					 */
	int t_refcount;
	int t_blocks_logged;	/* number of blocks this writer has logged */
	int t_blocks_allocated;	/* number of blocks this writer allocated */
	unsigned int t_trans_id;	/* sanity check, equals the current trans id */
	void *t_handle_save;	/* save existing current->journal_info */
	unsigned displace_new_blocks:1;	/* if new block allocation occurres, that block
					   should be displaced from others */
	struct list_head t_list;
};

/* used to keep track of ordered and tail writes, attached to the buffer
 * head through b_journal_head.
 */
struct reiserfs_jh {
	struct reiserfs_journal_list *jl;
	struct buffer_head *bh;
	struct list_head list;
};

void reiserfs_free_jh(struct buffer_head *bh);
int reiserfs_add_tail_list(struct inode *inode, struct buffer_head *bh);
int reiserfs_add_ordered_list(struct inode *inode, struct buffer_head *bh);
int journal_mark_dirty(struct reiserfs_transaction_handle *,
		       struct super_block *, struct buffer_head *bh);

static inline int reiserfs_file_data_log(struct inode *inode)
{
	if (reiserfs_data_log(inode->i_sb) ||
	    (REISERFS_I(inode)->i_flags & i_data_log))
		return 1;
	return 0;
}

static inline int reiserfs_transaction_running(struct super_block *s)
{
	struct reiserfs_transaction_handle *th = current->journal_info;
	if (th && th->t_super == s)
		return 1;
	if (th && th->t_super == NULL)
		BUG();
	return 0;
}

static inline int reiserfs_transaction_free_space(struct reiserfs_transaction_handle *th)
{
	return th->t_blocks_allocated - th->t_blocks_logged;
}

struct reiserfs_transaction_handle *reiserfs_persistent_transaction(struct
								    super_block
								    *,
								    int count);
int reiserfs_end_persistent_transaction(struct reiserfs_transaction_handle *);
int reiserfs_commit_page(struct inode *inode, struct page *page,
			 unsigned from, unsigned to);
int reiserfs_flush_old_commits(struct super_block *);
int reiserfs_commit_for_inode(struct inode *);
int reiserfs_inode_needs_commit(struct inode *);
void reiserfs_update_inode_transaction(struct inode *);
void reiserfs_wait_on_write_block(struct super_block *s);
void reiserfs_block_writes(struct reiserfs_transaction_handle *th);
void reiserfs_allow_writes(struct super_block *s);
void reiserfs_check_lock_depth(struct super_block *s, char *caller);
int reiserfs_prepare_for_journal(struct super_block *, struct buffer_head *bh,
				 int wait);
void reiserfs_restore_prepared_buffer(struct super_block *,
				      struct buffer_head *bh);
int journal_init(struct super_block *, const char *j_dev_name, int old_format,
		 unsigned int);
int journal_release(struct reiserfs_transaction_handle *, struct super_block *);
int journal_release_error(struct reiserfs_transaction_handle *,
			  struct super_block *);
int journal_end(struct reiserfs_transaction_handle *, struct super_block *,
		unsigned long);
int journal_end_sync(struct reiserfs_transaction_handle *, struct super_block *,
		     unsigned long);
int journal_mark_freed(struct reiserfs_transaction_handle *,
		       struct super_block *, b_blocknr_t blocknr);
int journal_transaction_should_end(struct reiserfs_transaction_handle *, int);
int reiserfs_in_journal(struct super_block *sb, unsigned int bmap_nr,
			 int bit_nr, int searchall, b_blocknr_t *next);
int journal_begin(struct reiserfs_transaction_handle *,
		  struct super_block *sb, unsigned long);
int journal_join_abort(struct reiserfs_transaction_handle *,
		       struct super_block *sb, unsigned long);
void reiserfs_abort_journal(struct super_block *sb, int errno);
void reiserfs_abort(struct super_block *sb, int errno, const char *fmt, ...);
int reiserfs_allocate_list_bitmaps(struct super_block *s,
				   struct reiserfs_list_bitmap *, unsigned int);

void add_save_link(struct reiserfs_transaction_handle *th,
		   struct inode *inode, int truncate);
int remove_save_link(struct inode *inode, int truncate);

/* objectid.c */
__u32 reiserfs_get_unused_objectid(struct reiserfs_transaction_handle *th);
void reiserfs_release_objectid(struct reiserfs_transaction_handle *th,
			       __u32 objectid_to_release);
int reiserfs_convert_objectid_map_v1(struct super_block *);

/* stree.c */
int B_IS_IN_TREE(const struct buffer_head *);
extern void copy_item_head(struct item_head *to,
			   const struct item_head *from);

// first key is in cpu form, second - le
extern int comp_short_keys(const struct reiserfs_key *le_key,
			   const struct cpu_key *cpu_key);
extern void le_key2cpu_key(struct cpu_key *to, const struct reiserfs_key *from);

// both are in le form
extern int comp_le_keys(const struct reiserfs_key *,
			const struct reiserfs_key *);
extern int comp_short_le_keys(const struct reiserfs_key *,
			      const struct reiserfs_key *);

//
// get key version from on disk key - kludge
//
static inline int le_key_version(const struct reiserfs_key *key)
{
	int type;

	type = offset_v2_k_type(&(key->u.k_offset_v2));
	if (type != TYPE_DIRECT && type != TYPE_INDIRECT
	    && type != TYPE_DIRENTRY)
		return KEY_FORMAT_3_5;

	return KEY_FORMAT_3_6;

}

static inline void copy_key(struct reiserfs_key *to,
			    const struct reiserfs_key *from)
{
	memcpy(to, from, KEY_SIZE);
}

int comp_items(const struct item_head *stored_ih, const struct treepath *path);
const struct reiserfs_key *get_rkey(const struct treepath *chk_path,
				    const struct super_block *sb);
int search_by_key(struct super_block *, const struct cpu_key *,
		  struct treepath *, int);
#define search_item(s,key,path) search_by_key (s, key, path, DISK_LEAF_NODE_LEVEL)
int search_for_position_by_key(struct super_block *sb,
			       const struct cpu_key *cpu_key,
			       struct treepath *search_path);
extern void decrement_bcount(struct buffer_head *bh);
void decrement_counters_in_path(struct treepath *search_path);
void pathrelse(struct treepath *search_path);
int reiserfs_check_path(struct treepath *p);
void pathrelse_and_restore(struct super_block *s, struct treepath *search_path);

int reiserfs_insert_item(struct reiserfs_transaction_handle *th,
			 struct treepath *path,
			 const struct cpu_key *key,
			 struct item_head *ih,
			 struct inode *inode, const char *body);

int reiserfs_paste_into_item(struct reiserfs_transaction_handle *th,
			     struct treepath *path,
			     const struct cpu_key *key,
			     struct inode *inode,
			     const char *body, int paste_size);

int reiserfs_cut_from_item(struct reiserfs_transaction_handle *th,
			   struct treepath *path,
			   struct cpu_key *key,
			   struct inode *inode,
			   struct page *page, loff_t new_file_size);

int reiserfs_delete_item(struct reiserfs_transaction_handle *th,
			 struct treepath *path,
			 const struct cpu_key *key,
			 struct inode *inode, struct buffer_head *un_bh);

void reiserfs_delete_solid_item(struct reiserfs_transaction_handle *th,
				struct inode *inode, struct reiserfs_key *key);
int reiserfs_delete_object(struct reiserfs_transaction_handle *th,
			   struct inode *inode);
int reiserfs_do_truncate(struct reiserfs_transaction_handle *th,
			 struct inode *inode, struct page *,
			 int update_timestamps);

#define i_block_size(inode) ((inode)->i_sb->s_blocksize)
#define file_size(inode) ((inode)->i_size)
#define tail_size(inode) (file_size (inode) & (i_block_size (inode) - 1))

#define tail_has_to_be_packed(inode) (have_large_tails ((inode)->i_sb)?\
!STORE_TAIL_IN_UNFM_S1(file_size (inode), tail_size(inode), inode->i_sb->s_blocksize):have_small_tails ((inode)->i_sb)?!STORE_TAIL_IN_UNFM_S2(file_size (inode), tail_size(inode), inode->i_sb->s_blocksize):0 )

void padd_item(char *item, int total_length, int length);

/* inode.c */
/* args for the create parameter of reiserfs_get_block */
#define GET_BLOCK_NO_CREATE 0	/* don't create new blocks or convert tails */
#define GET_BLOCK_CREATE 1	/* add anything you need to find block */
#define GET_BLOCK_NO_HOLE 2	/* return -ENOENT for file holes */
#define GET_BLOCK_READ_DIRECT 4	/* read the tail if indirect item not found */
#define GET_BLOCK_NO_IMUX     8	/* i_mutex is not held, don't preallocate */
#define GET_BLOCK_NO_DANGLE   16	/* don't leave any transactions running */

void reiserfs_read_locked_inode(struct inode *inode,
				struct reiserfs_iget_args *args);
int reiserfs_find_actor(struct inode *inode, void *p);
int reiserfs_init_locked_inode(struct inode *inode, void *p);
void reiserfs_evict_inode(struct inode *inode);
int reiserfs_write_inode(struct inode *inode, struct writeback_control *wbc);
int reiserfs_get_block(struct inode *inode, sector_t block,
		       struct buffer_head *bh_result, int create);
struct dentry *reiserfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
				     int fh_len, int fh_type);
struct dentry *reiserfs_fh_to_parent(struct super_block *sb, struct fid *fid,
				     int fh_len, int fh_type);
int reiserfs_encode_fh(struct dentry *dentry, __u32 * data, int *lenp,
		       int connectable);

int reiserfs_truncate_file(struct inode *, int update_timestamps);
void make_cpu_key(struct cpu_key *cpu_key, struct inode *inode, loff_t offset,
		  int type, int key_length);
void make_le_item_head(struct item_head *ih, const struct cpu_key *key,
		       int version,
		       loff_t offset, int type, int length, int entry_count);
struct inode *reiserfs_iget(struct super_block *s, const struct cpu_key *key);

struct reiserfs_security_handle;
int reiserfs_new_inode(struct reiserfs_transaction_handle *th,
		       struct inode *dir, int mode,
		       const char *symname, loff_t i_size,
		       struct dentry *dentry, struct inode *inode,
		       struct reiserfs_security_handle *security);

void reiserfs_update_sd_size(struct reiserfs_transaction_handle *th,
			     struct inode *inode, loff_t size);

static inline void reiserfs_update_sd(struct reiserfs_transaction_handle *th,
				      struct inode *inode)
{
	reiserfs_update_sd_size(th, inode, inode->i_size);
}

void sd_attrs_to_i_attrs(__u16 sd_attrs, struct inode *inode);
void i_attrs_to_sd_attrs(struct inode *inode, __u16 * sd_attrs);
int reiserfs_setattr(struct dentry *dentry, struct iattr *attr);

int __reiserfs_write_begin(struct page *page, unsigned from, unsigned len);

/* namei.c */
void set_de_name_and_namelen(struct reiserfs_dir_entry *de);
int search_by_entry_key(struct super_block *sb, const struct cpu_key *key,
			struct treepath *path, struct reiserfs_dir_entry *de);
struct dentry *reiserfs_get_parent(struct dentry *);

#ifdef CONFIG_REISERFS_PROC_INFO
int reiserfs_proc_info_init(struct super_block *sb);
int reiserfs_proc_info_done(struct super_block *sb);
int reiserfs_proc_info_global_init(void);
int reiserfs_proc_info_global_done(void);

#define PROC_EXP( e )   e

#define __PINFO( sb ) REISERFS_SB(sb) -> s_proc_info_data
#define PROC_INFO_MAX( sb, field, value )								\
    __PINFO( sb ).field =												\
        max( REISERFS_SB( sb ) -> s_proc_info_data.field, value )
#define PROC_INFO_INC( sb, field ) ( ++ ( __PINFO( sb ).field ) )
#define PROC_INFO_ADD( sb, field, val ) ( __PINFO( sb ).field += ( val ) )
#define PROC_INFO_BH_STAT( sb, bh, level )							\
    PROC_INFO_INC( sb, sbk_read_at[ ( level ) ] );						\
    PROC_INFO_ADD( sb, free_at[ ( level ) ], B_FREE_SPACE( bh ) );	\
    PROC_INFO_ADD( sb, items_at[ ( level ) ], B_NR_ITEMS( bh ) )
#else
static inline int reiserfs_proc_info_init(struct super_block *sb)
{
	return 0;
}

static inline int reiserfs_proc_info_done(struct super_block *sb)
{
	return 0;
}

static inline int reiserfs_proc_info_global_init(void)
{
	return 0;
}

static inline int reiserfs_proc_info_global_done(void)
{
	return 0;
}

#define PROC_EXP( e )
#define VOID_V ( ( void ) 0 )
#define PROC_INFO_MAX( sb, field, value ) VOID_V
#define PROC_INFO_INC( sb, field ) VOID_V
#define PROC_INFO_ADD( sb, field, val ) VOID_V
#define PROC_INFO_BH_STAT(sb, bh, n_node_level) VOID_V
#endif

/* dir.c */
extern const struct inode_operations reiserfs_dir_inode_operations;
extern const struct inode_operations reiserfs_symlink_inode_operations;
extern const struct inode_operations reiserfs_special_inode_operations;
extern const struct file_operations reiserfs_dir_operations;
int reiserfs_readdir_dentry(struct dentry *, void *, filldir_t, loff_t *);

/* tail_conversion.c */
int direct2indirect(struct reiserfs_transaction_handle *, struct inode *,
		    struct treepath *, struct buffer_head *, loff_t);
int indirect2direct(struct reiserfs_transaction_handle *, struct inode *,
		    struct page *, struct treepath *, const struct cpu_key *,
		    loff_t, char *);
void reiserfs_unmap_buffer(struct buffer_head *);

/* file.c */
extern const struct inode_operations reiserfs_file_inode_operations;
extern const struct file_operations reiserfs_file_operations;
extern const struct address_space_operations reiserfs_address_space_operations;

/* fix_nodes.c */

int fix_nodes(int n_op_mode, struct tree_balance *tb,
	      struct item_head *ins_ih, const void *);
void unfix_nodes(struct tree_balance *);

/* prints.c */
void __reiserfs_panic(struct super_block *s, const char *id,
		      const char *function, const char *fmt, ...)
    __attribute__ ((noreturn));
#define reiserfs_panic(s, id, fmt, args...) \
	__reiserfs_panic(s, id, __func__, fmt, ##args)
void __reiserfs_error(struct super_block *s, const char *id,
		      const char *function, const char *fmt, ...);
#define reiserfs_error(s, id, fmt, args...) \
	 __reiserfs_error(s, id, __func__, fmt, ##args)
void reiserfs_info(struct super_block *s, const char *fmt, ...);
void reiserfs_debug(struct super_block *s, int level, const char *fmt, ...);
void print_indirect_item(struct buffer_head *bh, int item_num);
void store_print_tb(struct tree_balance *tb);
void print_cur_tb(char *mes);
void print_de(struct reiserfs_dir_entry *de);
void print_bi(struct buffer_info *bi, char *mes);
#define PRINT_LEAF_ITEMS 1	/* print all items */
#define PRINT_DIRECTORY_ITEMS 2	/* print directory items */
#define PRINT_DIRECT_ITEMS 4	/* print contents of direct items */
void print_block(struct buffer_head *bh, ...);
void print_bmap(struct super_block *s, int silent);
void print_bmap_block(int i, char *data, int size, int silent);
/*void print_super_block (struct super_block * s, char * mes);*/
void print_objectid_map(struct super_block *s);
void print_block_head(struct buffer_head *bh, char *mes);
void check_leaf(struct buffer_head *bh);
void check_internal(struct buffer_head *bh);
void print_statistics(struct super_block *s);
char *reiserfs_hashname(int code);

/* lbalance.c */
int leaf_move_items(int shift_mode, struct tree_balance *tb, int mov_num,
		    int mov_bytes, struct buffer_head *Snew);
int leaf_shift_left(struct tree_balance *tb, int shift_num, int shift_bytes);
int leaf_shift_right(struct tree_balance *tb, int shift_num, int shift_bytes);
void leaf_delete_items(struct buffer_info *cur_bi, int last_first, int first,
		       int del_num, int del_bytes);
void leaf_insert_into_buf(struct buffer_info *bi, int before,
			  struct item_head *inserted_item_ih,
			  const char *inserted_item_body, int zeros_number);
void leaf_paste_in_buffer(struct buffer_info *bi, int pasted_item_num,
			  int pos_in_item, int paste_size, const char *body,
			  int zeros_number);
void leaf_cut_from_buffer(struct buffer_info *bi, int cut_item_num,
			  int pos_in_item, int cut_size);
void leaf_paste_entries(struct buffer_info *bi, int item_num, int before,
			int new_entry_count, struct reiserfs_de_head *new_dehs,
			const char *records, int paste_size);
/* ibalance.c */
int balance_internal(struct tree_balance *, int, int, struct item_head *,
		     struct buffer_head **);

/* do_balance.c */
void do_balance_mark_leaf_dirty(struct tree_balance *tb,
				struct buffer_head *bh, int flag);
#define do_balance_mark_internal_dirty do_balance_mark_leaf_dirty
#define do_balance_mark_sb_dirty do_balance_mark_leaf_dirty

void do_balance(struct tree_balance *tb, struct item_head *ih,
		const char *body, int flag);
void reiserfs_invalidate_buffer(struct tree_balance *tb,
				struct buffer_head *bh);

int get_left_neighbor_position(struct tree_balance *tb, int h);
int get_right_neighbor_position(struct tree_balance *tb, int h);
void replace_key(struct tree_balance *tb, struct buffer_head *, int,
		 struct buffer_head *, int);
void make_empty_node(struct buffer_info *);
struct buffer_head *get_FEB(struct tree_balance *);

/* bitmap.c */

/* structure contains hints for block allocator, and it is a container for
 * arguments, such as node, search path, transaction_handle, etc. */
struct __reiserfs_blocknr_hint {
	struct inode *inode;	/* inode passed to allocator, if we allocate unf. nodes */
	sector_t block;		/* file offset, in blocks */
	struct in_core_key key;
	struct treepath *path;	/* search path, used by allocator to deternine search_start by
				 * various ways */
	struct reiserfs_transaction_handle *th;	/* transaction handle is needed to log super blocks and
						 * bitmap blocks changes  */
	b_blocknr_t beg, end;
	b_blocknr_t search_start;	/* a field used to transfer search start value (block number)
					 * between different block allocator procedures
					 * (determine_search_start() and others) */
	int prealloc_size;	/* is set in determine_prealloc_size() function, used by underlayed
				 * function that do actual allocation */

	unsigned formatted_node:1;	/* the allocator uses different polices for getting disk space for
					 * formatted/unformatted blocks with/without preallocation */
	unsigned preallocate:1;
};

typedef struct __reiserfs_blocknr_hint reiserfs_blocknr_hint_t;

int reiserfs_parse_alloc_options(struct super_block *, char *);
void reiserfs_init_alloc_options(struct super_block *s);

/*
 * given a directory, this will tell you what packing locality
 * to use for a new object underneat it.  The locality is returned
 * in disk byte order (le).
 */
__le32 reiserfs_choose_packing(struct inode *dir);

int reiserfs_init_bitmap_cache(struct super_block *sb);
void reiserfs_free_bitmap_cache(struct super_block *sb);
void reiserfs_cache_bitmap_metadata(struct super_block *sb, struct buffer_head *bh, struct reiserfs_bitmap_info *info);
struct buffer_head *reiserfs_read_bitmap_block(struct super_block *sb, unsigned int bitmap);
int is_reusable(struct super_block *s, b_blocknr_t block, int bit_value);
void reiserfs_free_block(struct reiserfs_transaction_handle *th, struct inode *,
			 b_blocknr_t, int for_unformatted);
int reiserfs_allocate_blocknrs(reiserfs_blocknr_hint_t *, b_blocknr_t *, int,
			       int);
static inline int reiserfs_new_form_blocknrs(struct tree_balance *tb,
					     b_blocknr_t * new_blocknrs,
					     int amount_needed)
{
	reiserfs_blocknr_hint_t hint = {
		.th = tb->transaction_handle,
		.path = tb->tb_path,
		.inode = NULL,
		.key = tb->key,
		.block = 0,
		.formatted_node = 1
	};
	return reiserfs_allocate_blocknrs(&hint, new_blocknrs, amount_needed,
					  0);
}

static inline int reiserfs_new_unf_blocknrs(struct reiserfs_transaction_handle
					    *th, struct inode *inode,
					    b_blocknr_t * new_blocknrs,
					    struct treepath *path,
					    sector_t block)
{
	reiserfs_blocknr_hint_t hint = {
		.th = th,
		.path = path,
		.inode = inode,
		.block = block,
		.formatted_node = 0,
		.preallocate = 0
	};
	return reiserfs_allocate_blocknrs(&hint, new_blocknrs, 1, 0);
}

#ifdef REISERFS_PREALLOCATE
static inline int reiserfs_new_unf_blocknrs2(struct reiserfs_transaction_handle
					     *th, struct inode *inode,
					     b_blocknr_t * new_blocknrs,
					     struct treepath *path,
					     sector_t block)
{
	reiserfs_blocknr_hint_t hint = {
		.th = th,
		.path = path,
		.inode = inode,
		.block = block,
		.formatted_node = 0,
		.preallocate = 1
	};
	return reiserfs_allocate_blocknrs(&hint, new_blocknrs, 1, 0);
}

void reiserfs_discard_prealloc(struct reiserfs_transaction_handle *th,
			       struct inode *inode);
void reiserfs_discard_all_prealloc(struct reiserfs_transaction_handle *th);
#endif

/* hashes.c */
__u32 keyed_hash(const signed char *msg, int len);
__u32 yura_hash(const signed char *msg, int len);
__u32 r5_hash(const signed char *msg, int len);

#define reiserfs_set_le_bit		__set_bit_le
#define reiserfs_test_and_set_le_bit	__test_and_set_bit_le
#define reiserfs_clear_le_bit		__clear_bit_le
#define reiserfs_test_and_clear_le_bit	__test_and_clear_bit_le
#define reiserfs_test_le_bit		test_bit_le
#define reiserfs_find_next_zero_le_bit	find_next_zero_bit_le

/* sometimes reiserfs_truncate may require to allocate few new blocks
   to perform indirect2direct conversion. People probably used to
   think, that truncate should work without problems on a filesystem
   without free disk space. They may complain that they can not
   truncate due to lack of free disk space. This spare space allows us
   to not worry about it. 500 is probably too much, but it should be
   absolutely safe */
#define SPARE_SPACE 500

/* prototypes from ioctl.c */
long reiserfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long reiserfs_compat_ioctl(struct file *filp,
		   unsigned int cmd, unsigned long arg);
int reiserfs_unpack(struct inode *inode, struct file *filp);

#endif /* __KERNEL__ */

#endif				/* _LINUX_REISER_FS_H */
