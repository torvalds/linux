/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 */
#ifndef __EROFS_INTERNAL_H
#define __EROFS_INTERNAL_H

#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "erofs_fs.h"

/* redefine pr_fmt "erofs: " */
#undef pr_fmt
#define pr_fmt(fmt) "erofs: " fmt

__printf(3, 4) void _erofs_err(struct super_block *sb,
			       const char *function, const char *fmt, ...);
#define erofs_err(sb, fmt, ...)	\
	_erofs_err(sb, __func__, fmt "\n", ##__VA_ARGS__)
__printf(3, 4) void _erofs_info(struct super_block *sb,
			       const char *function, const char *fmt, ...);
#define erofs_info(sb, fmt, ...) \
	_erofs_info(sb, __func__, fmt "\n", ##__VA_ARGS__)
#ifdef CONFIG_EROFS_FS_DEBUG
#define erofs_dbg(x, ...)       pr_debug(x "\n", ##__VA_ARGS__)
#define DBG_BUGON               BUG_ON
#else
#define erofs_dbg(x, ...)       ((void)0)
#define DBG_BUGON(x)            ((void)(x))
#endif	/* !CONFIG_EROFS_FS_DEBUG */

/* EROFS_SUPER_MAGIC_V1 to represent the whole file system */
#define EROFS_SUPER_MAGIC   EROFS_SUPER_MAGIC_V1

typedef u64 erofs_nid_t;
typedef u64 erofs_off_t;
/* data type for filesystem-wide blocks number */
typedef u32 erofs_blk_t;

struct erofs_fs_context {
#ifdef CONFIG_EROFS_FS_ZIP
	/* current strategy of how to use managed cache */
	unsigned char cache_strategy;
	/* strategy of sync decompression (false - auto, true - force on) */
	bool readahead_sync_decompress;

	/* threshold for decompression synchronously */
	unsigned int max_sync_decompress_pages;
#endif
	unsigned int mount_opt;
};

/* all filesystem-wide lz4 configurations */
struct erofs_sb_lz4_info {
	/* # of pages needed for EROFS lz4 rolling decompression */
	u16 max_distance_pages;
	/* maximum possible blocks for pclusters in the filesystem */
	u16 max_pclusterblks;
};

struct erofs_sb_info {
#ifdef CONFIG_EROFS_FS_ZIP
	/* list for all registered superblocks, mainly for shrinker */
	struct list_head list;
	struct mutex umount_mutex;

	/* managed XArray arranged in physical block number */
	struct xarray managed_pslots;

	unsigned int shrinker_run_no;
	u16 available_compr_algs;

	/* pseudo inode to manage cached pages */
	struct inode *managed_cache;

	struct erofs_sb_lz4_info lz4;
#endif	/* CONFIG_EROFS_FS_ZIP */
	u32 blocks;
	u32 meta_blkaddr;
#ifdef CONFIG_EROFS_FS_XATTR
	u32 xattr_blkaddr;
#endif

	/* inode slot unit size in bit shift */
	unsigned char islotbits;

	u32 sb_size;			/* total superblock size */
	u32 build_time_nsec;
	u64 build_time;

	/* what we really care is nid, rather than ino.. */
	erofs_nid_t root_nid;
	/* used for statfs, f_files - f_favail */
	u64 inos;

	u8 uuid[16];                    /* 128-bit uuid for volume */
	u8 volume_name[16];             /* volume name */
	u32 feature_compat;
	u32 feature_incompat;

	struct erofs_fs_context ctx;	/* options */
};

#define EROFS_SB(sb) ((struct erofs_sb_info *)(sb)->s_fs_info)
#define EROFS_I_SB(inode) ((struct erofs_sb_info *)(inode)->i_sb->s_fs_info)

/* Mount flags set via mount options or defaults */
#define EROFS_MOUNT_XATTR_USER		0x00000010
#define EROFS_MOUNT_POSIX_ACL		0x00000020

#define clear_opt(ctx, option)	((ctx)->mount_opt &= ~EROFS_MOUNT_##option)
#define set_opt(ctx, option)	((ctx)->mount_opt |= EROFS_MOUNT_##option)
#define test_opt(ctx, option)	((ctx)->mount_opt & EROFS_MOUNT_##option)

enum {
	EROFS_ZIP_CACHE_DISABLED,
	EROFS_ZIP_CACHE_READAHEAD,
	EROFS_ZIP_CACHE_READAROUND
};

#ifdef CONFIG_EROFS_FS_ZIP
#define EROFS_LOCKED_MAGIC     (INT_MIN | 0xE0F510CCL)

/* basic unit of the workstation of a super_block */
struct erofs_workgroup {
	/* the workgroup index in the workstation */
	pgoff_t index;

	/* overall workgroup reference count */
	atomic_t refcount;
};

#if defined(CONFIG_SMP)
static inline bool erofs_workgroup_try_to_freeze(struct erofs_workgroup *grp,
						 int val)
{
	preempt_disable();
	if (val != atomic_cmpxchg(&grp->refcount, val, EROFS_LOCKED_MAGIC)) {
		preempt_enable();
		return false;
	}
	return true;
}

static inline void erofs_workgroup_unfreeze(struct erofs_workgroup *grp,
					    int orig_val)
{
	/*
	 * other observers should notice all modifications
	 * in the freezing period.
	 */
	smp_mb();
	atomic_set(&grp->refcount, orig_val);
	preempt_enable();
}

static inline int erofs_wait_on_workgroup_freezed(struct erofs_workgroup *grp)
{
	return atomic_cond_read_relaxed(&grp->refcount,
					VAL != EROFS_LOCKED_MAGIC);
}
#else
static inline bool erofs_workgroup_try_to_freeze(struct erofs_workgroup *grp,
						 int val)
{
	preempt_disable();
	/* no need to spin on UP platforms, let's just disable preemption. */
	if (val != atomic_read(&grp->refcount)) {
		preempt_enable();
		return false;
	}
	return true;
}

static inline void erofs_workgroup_unfreeze(struct erofs_workgroup *grp,
					    int orig_val)
{
	preempt_enable();
}

static inline int erofs_wait_on_workgroup_freezed(struct erofs_workgroup *grp)
{
	int v = atomic_read(&grp->refcount);

	/* workgroup is never freezed on uniprocessor systems */
	DBG_BUGON(v == EROFS_LOCKED_MAGIC);
	return v;
}
#endif	/* !CONFIG_SMP */
#endif	/* !CONFIG_EROFS_FS_ZIP */

/* we strictly follow PAGE_SIZE and no buffer head yet */
#define LOG_BLOCK_SIZE		PAGE_SHIFT

#undef LOG_SECTORS_PER_BLOCK
#define LOG_SECTORS_PER_BLOCK	(PAGE_SHIFT - 9)

#undef SECTORS_PER_BLOCK
#define SECTORS_PER_BLOCK	(1 << SECTORS_PER_BLOCK)

#define EROFS_BLKSIZ		(1 << LOG_BLOCK_SIZE)

#if (EROFS_BLKSIZ % 4096 || !EROFS_BLKSIZ)
#error erofs cannot be used in this platform
#endif

#define ROOT_NID(sb)		((sb)->root_nid)

#define erofs_blknr(addr)       ((addr) / EROFS_BLKSIZ)
#define erofs_blkoff(addr)      ((addr) % EROFS_BLKSIZ)
#define blknr_to_addr(nr)       ((erofs_off_t)(nr) * EROFS_BLKSIZ)

static inline erofs_off_t iloc(struct erofs_sb_info *sbi, erofs_nid_t nid)
{
	return blknr_to_addr(sbi->meta_blkaddr) + (nid << sbi->islotbits);
}

#define EROFS_FEATURE_FUNCS(name, compat, feature) \
static inline bool erofs_sb_has_##name(struct erofs_sb_info *sbi) \
{ \
	return sbi->feature_##compat & EROFS_FEATURE_##feature; \
}

EROFS_FEATURE_FUNCS(lz4_0padding, incompat, INCOMPAT_LZ4_0PADDING)
EROFS_FEATURE_FUNCS(compr_cfgs, incompat, INCOMPAT_COMPR_CFGS)
EROFS_FEATURE_FUNCS(big_pcluster, incompat, INCOMPAT_BIG_PCLUSTER)
EROFS_FEATURE_FUNCS(sb_chksum, compat, COMPAT_SB_CHKSUM)

/* atomic flag definitions */
#define EROFS_I_EA_INITED_BIT	0
#define EROFS_I_Z_INITED_BIT	1

/* bitlock definitions (arranged in reverse order) */
#define EROFS_I_BL_XATTR_BIT	(BITS_PER_LONG - 1)
#define EROFS_I_BL_Z_BIT	(BITS_PER_LONG - 2)

struct erofs_inode {
	erofs_nid_t nid;

	/* atomic flags (including bitlocks) */
	unsigned long flags;

	unsigned char datalayout;
	unsigned char inode_isize;
	unsigned int xattr_isize;

	unsigned int xattr_shared_count;
	unsigned int *xattr_shared_xattrs;

	union {
		erofs_blk_t raw_blkaddr;
#ifdef CONFIG_EROFS_FS_ZIP
		struct {
			unsigned short z_advise;
			unsigned char  z_algorithmtype[2];
			unsigned char  z_logical_clusterbits;
		};
#endif	/* CONFIG_EROFS_FS_ZIP */
	};
	/* the corresponding vfs inode */
	struct inode vfs_inode;
};

#define EROFS_I(ptr)	\
	container_of(ptr, struct erofs_inode, vfs_inode)

static inline unsigned long erofs_inode_datablocks(struct inode *inode)
{
	/* since i_size cannot be changed */
	return DIV_ROUND_UP(inode->i_size, EROFS_BLKSIZ);
}

static inline unsigned int erofs_bitrange(unsigned int value, unsigned int bit,
					  unsigned int bits)
{

	return (value >> bit) & ((1 << bits) - 1);
}


static inline unsigned int erofs_inode_version(unsigned int value)
{
	return erofs_bitrange(value, EROFS_I_VERSION_BIT,
			      EROFS_I_VERSION_BITS);
}

static inline unsigned int erofs_inode_datalayout(unsigned int value)
{
	return erofs_bitrange(value, EROFS_I_DATALAYOUT_BIT,
			      EROFS_I_DATALAYOUT_BITS);
}

extern const struct super_operations erofs_sops;

extern const struct address_space_operations erofs_raw_access_aops;
extern const struct address_space_operations z_erofs_aops;

/*
 * Logical to physical block mapping
 *
 * Different with other file systems, it is used for 2 access modes:
 *
 * 1) RAW access mode:
 *
 * Users pass a valid (m_lblk, m_lofs -- usually 0) pair,
 * and get the valid m_pblk, m_pofs and the longest m_len(in bytes).
 *
 * Note that m_lblk in the RAW access mode refers to the number of
 * the compressed ondisk block rather than the uncompressed
 * in-memory block for the compressed file.
 *
 * m_pofs equals to m_lofs except for the inline data page.
 *
 * 2) Normal access mode:
 *
 * If the inode is not compressed, it has no difference with
 * the RAW access mode. However, if the inode is compressed,
 * users should pass a valid (m_lblk, m_lofs) pair, and get
 * the needed m_pblk, m_pofs, m_len to get the compressed data
 * and the updated m_lblk, m_lofs which indicates the start
 * of the corresponding uncompressed data in the file.
 */
enum {
	BH_Zipped = BH_PrivateStart,
	BH_FullMapped,
};

/* Has a disk mapping */
#define EROFS_MAP_MAPPED	(1 << BH_Mapped)
/* Located in metadata (could be copied from bd_inode) */
#define EROFS_MAP_META		(1 << BH_Meta)
/* The extent has been compressed */
#define EROFS_MAP_ZIPPED	(1 << BH_Zipped)
/* The length of extent is full */
#define EROFS_MAP_FULL_MAPPED	(1 << BH_FullMapped)

struct erofs_map_blocks {
	erofs_off_t m_pa, m_la;
	u64 m_plen, m_llen;

	unsigned int m_flags;

	struct page *mpage;
};

/* Flags used by erofs_map_blocks_flatmode() */
#define EROFS_GET_BLOCKS_RAW    0x0001

/* zmap.c */
#ifdef CONFIG_EROFS_FS_ZIP
int z_erofs_fill_inode(struct inode *inode);
int z_erofs_map_blocks_iter(struct inode *inode,
			    struct erofs_map_blocks *map,
			    int flags);
#else
static inline int z_erofs_fill_inode(struct inode *inode) { return -EOPNOTSUPP; }
static inline int z_erofs_map_blocks_iter(struct inode *inode,
					  struct erofs_map_blocks *map,
					  int flags)
{
	return -EOPNOTSUPP;
}
#endif	/* !CONFIG_EROFS_FS_ZIP */

/* data.c */
struct page *erofs_get_meta_page(struct super_block *sb, erofs_blk_t blkaddr);

/* inode.c */
static inline unsigned long erofs_inode_hash(erofs_nid_t nid)
{
#if BITS_PER_LONG == 32
	return (nid >> 32) ^ (nid & 0xffffffff);
#else
	return nid;
#endif
}

extern const struct inode_operations erofs_generic_iops;
extern const struct inode_operations erofs_symlink_iops;
extern const struct inode_operations erofs_fast_symlink_iops;

struct inode *erofs_iget(struct super_block *sb, erofs_nid_t nid, bool dir);
int erofs_getattr(const struct path *path, struct kstat *stat,
		  u32 request_mask, unsigned int query_flags);

/* namei.c */
extern const struct inode_operations erofs_dir_iops;

int erofs_namei(struct inode *dir, struct qstr *name,
		erofs_nid_t *nid, unsigned int *d_type);

/* dir.c */
extern const struct file_operations erofs_dir_fops;

static inline void *erofs_vm_map_ram(struct page **pages, unsigned int count)
{
	int retried = 0;

	while (1) {
		void *p = vm_map_ram(pages, count, -1);

		/* retry two more times (totally 3 times) */
		if (p || ++retried >= 3)
			return p;
		vm_unmap_aliases();
	}
	return NULL;
}

/* pcpubuf.c */
void *erofs_get_pcpubuf(unsigned int requiredpages);
void erofs_put_pcpubuf(void *ptr);
int erofs_pcpubuf_growsize(unsigned int nrpages);
void erofs_pcpubuf_init(void);
void erofs_pcpubuf_exit(void);

/* utils.c / zdata.c */
struct page *erofs_allocpage(struct list_head *pool, gfp_t gfp);

#ifdef CONFIG_EROFS_FS_ZIP
int erofs_workgroup_put(struct erofs_workgroup *grp);
struct erofs_workgroup *erofs_find_workgroup(struct super_block *sb,
					     pgoff_t index);
struct erofs_workgroup *erofs_insert_workgroup(struct super_block *sb,
					       struct erofs_workgroup *grp);
void erofs_workgroup_free_rcu(struct erofs_workgroup *grp);
void erofs_shrinker_register(struct super_block *sb);
void erofs_shrinker_unregister(struct super_block *sb);
int __init erofs_init_shrinker(void);
void erofs_exit_shrinker(void);
int __init z_erofs_init_zip_subsystem(void);
void z_erofs_exit_zip_subsystem(void);
int erofs_try_to_free_all_cached_pages(struct erofs_sb_info *sbi,
				       struct erofs_workgroup *egrp);
int erofs_try_to_free_cached_page(struct address_space *mapping,
				  struct page *page);
int z_erofs_load_lz4_config(struct super_block *sb,
			    struct erofs_super_block *dsb,
			    struct z_erofs_lz4_cfgs *lz4, int len);
#else
static inline void erofs_shrinker_register(struct super_block *sb) {}
static inline void erofs_shrinker_unregister(struct super_block *sb) {}
static inline int erofs_init_shrinker(void) { return 0; }
static inline void erofs_exit_shrinker(void) {}
static inline int z_erofs_init_zip_subsystem(void) { return 0; }
static inline void z_erofs_exit_zip_subsystem(void) {}
static inline int z_erofs_load_lz4_config(struct super_block *sb,
				  struct erofs_super_block *dsb,
				  struct z_erofs_lz4_cfgs *lz4, int len)
{
	if (lz4 || dsb->u1.lz4_max_distance) {
		erofs_err(sb, "lz4 algorithm isn't enabled");
		return -EINVAL;
	}
	return 0;
}
#endif	/* !CONFIG_EROFS_FS_ZIP */

#define EFSCORRUPTED    EUCLEAN         /* Filesystem is corrupted */

#endif	/* __EROFS_INTERNAL_H */

