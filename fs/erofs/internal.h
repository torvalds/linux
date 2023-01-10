/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
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
#include <linux/iomap.h>
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

struct erofs_device_info {
	char *path;
	struct erofs_fscache *fscache;
	struct block_device *bdev;
	struct dax_device *dax_dev;
	u64 dax_part_off;

	u32 blocks;
	u32 mapped_blkaddr;
};

enum {
	EROFS_SYNC_DECOMPRESS_AUTO,
	EROFS_SYNC_DECOMPRESS_FORCE_ON,
	EROFS_SYNC_DECOMPRESS_FORCE_OFF
};

struct erofs_mount_opts {
#ifdef CONFIG_EROFS_FS_ZIP
	/* current strategy of how to use managed cache */
	unsigned char cache_strategy;
	/* strategy of sync decompression (0 - auto, 1 - force on, 2 - force off) */
	unsigned int sync_decompress;

	/* threshold for decompression synchronously */
	unsigned int max_sync_decompress_pages;
#endif
	unsigned int mount_opt;
};

struct erofs_dev_context {
	struct idr tree;
	struct rw_semaphore rwsem;

	unsigned int extra_devices;
};

struct erofs_fs_context {
	struct erofs_mount_opts opt;
	struct erofs_dev_context *devs;
	char *fsid;
	char *domain_id;
};

/* all filesystem-wide lz4 configurations */
struct erofs_sb_lz4_info {
	/* # of pages needed for EROFS lz4 rolling decompression */
	u16 max_distance_pages;
	/* maximum possible blocks for pclusters in the filesystem */
	u16 max_pclusterblks;
};

struct erofs_domain {
	refcount_t ref;
	struct list_head list;
	struct fscache_volume *volume;
	char *domain_id;
};

struct erofs_fscache {
	struct fscache_cookie *cookie;
	struct inode *inode;
	struct inode *anon_inode;
	struct erofs_domain *domain;
	char *name;
};

struct erofs_sb_info {
	struct erofs_mount_opts opt;	/* options */
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
	struct inode *packed_inode;
#endif	/* CONFIG_EROFS_FS_ZIP */
	struct erofs_dev_context *devs;
	struct dax_device *dax_dev;
	u64 dax_part_off;
	u64 total_blocks;
	u32 primarydevice_blocks;

	u32 meta_blkaddr;
#ifdef CONFIG_EROFS_FS_XATTR
	u32 xattr_blkaddr;
#endif
	u16 device_id_mask;	/* valid bits of device id to be used */

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

	/* sysfs support */
	struct kobject s_kobj;		/* /sys/fs/erofs/<devname> */
	struct completion s_kobj_unregister;

	/* fscache support */
	struct fscache_volume *volume;
	struct erofs_fscache *s_fscache;
	struct erofs_domain *domain;
	char *fsid;
	char *domain_id;
};

#define EROFS_SB(sb) ((struct erofs_sb_info *)(sb)->s_fs_info)
#define EROFS_I_SB(inode) ((struct erofs_sb_info *)(inode)->i_sb->s_fs_info)

/* Mount flags set via mount options or defaults */
#define EROFS_MOUNT_XATTR_USER		0x00000010
#define EROFS_MOUNT_POSIX_ACL		0x00000020
#define EROFS_MOUNT_DAX_ALWAYS		0x00000040
#define EROFS_MOUNT_DAX_NEVER		0x00000080

#define clear_opt(opt, option)	((opt)->mount_opt &= ~EROFS_MOUNT_##option)
#define set_opt(opt, option)	((opt)->mount_opt |= EROFS_MOUNT_##option)
#define test_opt(opt, option)	((opt)->mount_opt & EROFS_MOUNT_##option)

static inline bool erofs_is_fscache_mode(struct super_block *sb)
{
	return IS_ENABLED(CONFIG_EROFS_FS_ONDEMAND) && !sb->s_bdev;
}

enum {
	EROFS_ZIP_CACHE_DISABLED,
	EROFS_ZIP_CACHE_READAHEAD,
	EROFS_ZIP_CACHE_READAROUND
};

#define EROFS_LOCKED_MAGIC     (INT_MIN | 0xE0F510CCL)

/* basic unit of the workstation of a super_block */
struct erofs_workgroup {
	/* the workgroup index in the workstation */
	pgoff_t index;

	/* overall workgroup reference count */
	atomic_t refcount;
};

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

enum erofs_kmap_type {
	EROFS_NO_KMAP,		/* don't map the buffer */
	EROFS_KMAP,		/* use kmap_local_page() to map the buffer */
};

struct erofs_buf {
	struct page *page;
	void *base;
	enum erofs_kmap_type kmap_type;
};
#define __EROFS_BUF_INITIALIZER	((struct erofs_buf){ .page = NULL })

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

EROFS_FEATURE_FUNCS(zero_padding, incompat, INCOMPAT_ZERO_PADDING)
EROFS_FEATURE_FUNCS(compr_cfgs, incompat, INCOMPAT_COMPR_CFGS)
EROFS_FEATURE_FUNCS(big_pcluster, incompat, INCOMPAT_BIG_PCLUSTER)
EROFS_FEATURE_FUNCS(chunked_file, incompat, INCOMPAT_CHUNKED_FILE)
EROFS_FEATURE_FUNCS(device_table, incompat, INCOMPAT_DEVICE_TABLE)
EROFS_FEATURE_FUNCS(compr_head2, incompat, INCOMPAT_COMPR_HEAD2)
EROFS_FEATURE_FUNCS(ztailpacking, incompat, INCOMPAT_ZTAILPACKING)
EROFS_FEATURE_FUNCS(fragments, incompat, INCOMPAT_FRAGMENTS)
EROFS_FEATURE_FUNCS(dedupe, incompat, INCOMPAT_DEDUPE)
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
	unsigned short xattr_isize;

	unsigned int xattr_shared_count;
	unsigned int *xattr_shared_xattrs;

	union {
		erofs_blk_t raw_blkaddr;
		struct {
			unsigned short	chunkformat;
			unsigned char	chunkbits;
		};
#ifdef CONFIG_EROFS_FS_ZIP
		struct {
			unsigned short z_advise;
			unsigned char  z_algorithmtype[2];
			unsigned char  z_logical_clusterbits;
			unsigned long  z_tailextent_headlcn;
			union {
				struct {
					erofs_off_t    z_idataoff;
					unsigned short z_idata_size;
				};
				erofs_off_t z_fragmentoff;
			};
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

/*
 * Different from grab_cache_page_nowait(), reclaiming is never triggered
 * when allocating new pages.
 */
static inline
struct page *erofs_grab_cache_page_nowait(struct address_space *mapping,
					  pgoff_t index)
{
	return pagecache_get_page(mapping, index,
			FGP_LOCK|FGP_CREAT|FGP_NOFS|FGP_NOWAIT,
			readahead_gfp_mask(mapping) & ~__GFP_RECLAIM);
}

extern const struct super_operations erofs_sops;
extern struct file_system_type erofs_fs_type;

extern const struct address_space_operations erofs_raw_access_aops;
extern const struct address_space_operations z_erofs_aops;

enum {
	BH_Encoded = BH_PrivateStart,
	BH_FullMapped,
	BH_Fragment,
	BH_Partialref,
};

/* Has a disk mapping */
#define EROFS_MAP_MAPPED	(1 << BH_Mapped)
/* Located in metadata (could be copied from bd_inode) */
#define EROFS_MAP_META		(1 << BH_Meta)
/* The extent is encoded */
#define EROFS_MAP_ENCODED	(1 << BH_Encoded)
/* The length of extent is full */
#define EROFS_MAP_FULL_MAPPED	(1 << BH_FullMapped)
/* Located in the special packed inode */
#define EROFS_MAP_FRAGMENT	(1 << BH_Fragment)
/* The extent refers to partial decompressed data */
#define EROFS_MAP_PARTIAL_REF	(1 << BH_Partialref)

struct erofs_map_blocks {
	struct erofs_buf buf;

	erofs_off_t m_pa, m_la;
	u64 m_plen, m_llen;

	unsigned short m_deviceid;
	char m_algorithmformat;
	unsigned int m_flags;
};

/* Flags used by erofs_map_blocks_flatmode() */
#define EROFS_GET_BLOCKS_RAW    0x0001
/*
 * Used to get the exact decompressed length, e.g. fiemap (consider lookback
 * approach instead if possible since it's more metadata lightweight.)
 */
#define EROFS_GET_BLOCKS_FIEMAP	0x0002
/* Used to map the whole extent if non-negligible data is requested for LZMA */
#define EROFS_GET_BLOCKS_READMORE	0x0004
/* Used to map tail extent for tailpacking inline or fragment pcluster */
#define EROFS_GET_BLOCKS_FINDTAIL	0x0008

enum {
	Z_EROFS_COMPRESSION_SHIFTED = Z_EROFS_COMPRESSION_MAX,
	Z_EROFS_COMPRESSION_INTERLACED,
	Z_EROFS_COMPRESSION_RUNTIME_MAX
};

/* zmap.c */
extern const struct iomap_ops z_erofs_iomap_report_ops;

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

struct erofs_map_dev {
	struct erofs_fscache *m_fscache;
	struct block_device *m_bdev;
	struct dax_device *m_daxdev;
	u64 m_dax_part_off;

	erofs_off_t m_pa;
	unsigned int m_deviceid;
};

/* data.c */
extern const struct file_operations erofs_file_fops;
void erofs_unmap_metabuf(struct erofs_buf *buf);
void erofs_put_metabuf(struct erofs_buf *buf);
void *erofs_bread(struct erofs_buf *buf, struct inode *inode,
		  erofs_blk_t blkaddr, enum erofs_kmap_type type);
void *erofs_read_metabuf(struct erofs_buf *buf, struct super_block *sb,
			 erofs_blk_t blkaddr, enum erofs_kmap_type type);
int erofs_map_dev(struct super_block *sb, struct erofs_map_dev *dev);
int erofs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		 u64 start, u64 len);
int erofs_map_blocks(struct inode *inode,
		     struct erofs_map_blocks *map, int flags);

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

struct inode *erofs_iget(struct super_block *sb, erofs_nid_t nid);
int erofs_getattr(struct user_namespace *mnt_userns, const struct path *path,
		  struct kstat *stat, u32 request_mask,
		  unsigned int query_flags);

/* namei.c */
extern const struct inode_operations erofs_dir_iops;

int erofs_namei(struct inode *dir, const struct qstr *name,
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

/* sysfs.c */
int erofs_register_sysfs(struct super_block *sb);
void erofs_unregister_sysfs(struct super_block *sb);
int __init erofs_init_sysfs(void);
void erofs_exit_sysfs(void);

/* utils.c / zdata.c */
struct page *erofs_allocpage(struct page **pagepool, gfp_t gfp);
static inline void erofs_pagepool_add(struct page **pagepool,
		struct page *page)
{
	set_page_private(page, (unsigned long)*pagepool);
	*pagepool = page;
}
void erofs_release_pages(struct page **pagepool);

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
int erofs_try_to_free_cached_page(struct page *page);
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

#ifdef CONFIG_EROFS_FS_ZIP_LZMA
int z_erofs_lzma_init(void);
void z_erofs_lzma_exit(void);
int z_erofs_load_lzma_config(struct super_block *sb,
			     struct erofs_super_block *dsb,
			     struct z_erofs_lzma_cfgs *lzma, int size);
#else
static inline int z_erofs_lzma_init(void) { return 0; }
static inline int z_erofs_lzma_exit(void) { return 0; }
static inline int z_erofs_load_lzma_config(struct super_block *sb,
			     struct erofs_super_block *dsb,
			     struct z_erofs_lzma_cfgs *lzma, int size) {
	if (lzma) {
		erofs_err(sb, "lzma algorithm isn't enabled");
		return -EINVAL;
	}
	return 0;
}
#endif	/* !CONFIG_EROFS_FS_ZIP */

/* flags for erofs_fscache_register_cookie() */
#define EROFS_REG_COOKIE_NEED_INODE	1
#define EROFS_REG_COOKIE_NEED_NOEXIST	2

/* fscache.c */
#ifdef CONFIG_EROFS_FS_ONDEMAND
int erofs_fscache_register_fs(struct super_block *sb);
void erofs_fscache_unregister_fs(struct super_block *sb);

struct erofs_fscache *erofs_fscache_register_cookie(struct super_block *sb,
						    char *name,
						    unsigned int flags);
void erofs_fscache_unregister_cookie(struct erofs_fscache *fscache);

extern const struct address_space_operations erofs_fscache_access_aops;
#else
static inline int erofs_fscache_register_fs(struct super_block *sb)
{
	return -EOPNOTSUPP;
}
static inline void erofs_fscache_unregister_fs(struct super_block *sb) {}

static inline
struct erofs_fscache *erofs_fscache_register_cookie(struct super_block *sb,
						     char *name,
						     unsigned int flags)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void erofs_fscache_unregister_cookie(struct erofs_fscache *fscache)
{
}
#endif

#define EFSCORRUPTED    EUCLEAN         /* Filesystem is corrupted */

#endif	/* __EROFS_INTERNAL_H */
