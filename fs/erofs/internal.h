/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             https://www.huawei.com/
 * Copyright (C) 2021, Alibaba Cloud
 */
#ifndef __EROFS_INTERNAL_H
#define __EROFS_INTERNAL_H

#include <linux/fs.h>
#include <linux/dax.h>
#include <linux/dcache.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
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
#define DBG_BUGON               BUG_ON
#else
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
	struct file *bdev_file;
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
	bool flatdev;
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
	struct inode *inode;	/* anonymous inode for the blob */

	/* used for share domain mode */
	struct erofs_domain *domain;
	struct list_head node;
	refcount_t ref;
	char *name;
};

struct erofs_xattr_prefix_item {
	struct erofs_xattr_long_prefix *prefix;
	u8 infix_len;
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
#endif	/* CONFIG_EROFS_FS_ZIP */
	struct inode *packed_inode;
	struct erofs_dev_context *devs;
	struct dax_device *dax_dev;
	u64 dax_part_off;
	u64 total_blocks;
	u32 primarydevice_blocks;

	u32 meta_blkaddr;
#ifdef CONFIG_EROFS_FS_XATTR
	u32 xattr_blkaddr;
	u32 xattr_prefix_start;
	u8 xattr_prefix_count;
	struct erofs_xattr_prefix_item *xattr_prefixes;
	unsigned int xattr_filter_reserved;
#endif
	u16 device_id_mask;	/* valid bits of device id to be used */

	unsigned char islotbits;	/* inode slot unit size in bit shift */
	unsigned char blkszbits;	/* filesystem block size in bit shift */

	u32 sb_size;			/* total superblock size */
	u32 build_time_nsec;
	u64 build_time;

	/* what we really care is nid, rather than ino.. */
	erofs_nid_t root_nid;
	erofs_nid_t packed_nid;
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

/* basic unit of the workstation of a super_block */
struct erofs_workgroup {
	pgoff_t index;
	struct lockref lockref;
};

enum erofs_kmap_type {
	EROFS_NO_KMAP,		/* don't map the buffer */
	EROFS_KMAP,		/* use kmap_local_page() to map the buffer */
};

struct erofs_buf {
	struct inode *inode;
	struct page *page;
	void *base;
	enum erofs_kmap_type kmap_type;
};
#define __EROFS_BUF_INITIALIZER	((struct erofs_buf){ .page = NULL })

#define erofs_blknr(sb, addr)	((addr) >> (sb)->s_blocksize_bits)
#define erofs_blkoff(sb, addr)	((addr) & ((sb)->s_blocksize - 1))
#define erofs_pos(sb, blk)	((erofs_off_t)(blk) << (sb)->s_blocksize_bits)
#define erofs_iblks(i)	(round_up((i)->i_size, i_blocksize(i)) >> (i)->i_blkbits)

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
EROFS_FEATURE_FUNCS(xattr_prefixes, incompat, INCOMPAT_XATTR_PREFIXES)
EROFS_FEATURE_FUNCS(sb_chksum, compat, COMPAT_SB_CHKSUM)
EROFS_FEATURE_FUNCS(xattr_filter, compat, COMPAT_XATTR_FILTER)

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

	unsigned int xattr_name_filter;
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

#define EROFS_I(ptr)	container_of(ptr, struct erofs_inode, vfs_inode)

static inline erofs_off_t erofs_iloc(struct inode *inode)
{
	struct erofs_sb_info *sbi = EROFS_I_SB(inode);

	return erofs_pos(inode->i_sb, sbi->meta_blkaddr) +
		(EROFS_I(inode)->nid << sbi->islotbits);
}

static inline unsigned int erofs_inode_version(unsigned int ifmt)
{
	return (ifmt >> EROFS_I_VERSION_BIT) & EROFS_I_VERSION_MASK;
}

static inline unsigned int erofs_inode_datalayout(unsigned int ifmt)
{
	return (ifmt >> EROFS_I_DATALAYOUT_BIT) & EROFS_I_DATALAYOUT_MASK;
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

/* Has a disk mapping */
#define EROFS_MAP_MAPPED	0x0001
/* Located in metadata (could be copied from bd_inode) */
#define EROFS_MAP_META		0x0002
/* The extent is encoded */
#define EROFS_MAP_ENCODED	0x0004
/* The length of extent is full */
#define EROFS_MAP_FULL_MAPPED	0x0008
/* Located in the special packed inode */
#define EROFS_MAP_FRAGMENT	0x0010
/* The extent refers to partial decompressed data */
#define EROFS_MAP_PARTIAL_REF	0x0020

struct erofs_map_blocks {
	struct erofs_buf buf;

	erofs_off_t m_pa, m_la;
	u64 m_plen, m_llen;

	unsigned short m_deviceid;
	char m_algorithmformat;
	unsigned int m_flags;
};

/*
 * Used to get the exact decompressed length, e.g. fiemap (consider lookback
 * approach instead if possible since it's more metadata lightweight.)
 */
#define EROFS_GET_BLOCKS_FIEMAP		0x0001
/* Used to map the whole extent if non-negligible data is requested for LZMA */
#define EROFS_GET_BLOCKS_READMORE	0x0002
/* Used to map tail extent for tailpacking inline or fragment pcluster */
#define EROFS_GET_BLOCKS_FINDTAIL	0x0004

enum {
	Z_EROFS_COMPRESSION_SHIFTED = Z_EROFS_COMPRESSION_MAX,
	Z_EROFS_COMPRESSION_INTERLACED,
	Z_EROFS_COMPRESSION_RUNTIME_MAX
};

struct erofs_map_dev {
	struct erofs_fscache *m_fscache;
	struct block_device *m_bdev;
	struct dax_device *m_daxdev;
	u64 m_dax_part_off;

	erofs_off_t m_pa;
	unsigned int m_deviceid;
};

extern const struct super_operations erofs_sops;

extern const struct address_space_operations erofs_raw_access_aops;
extern const struct address_space_operations z_erofs_aops;
extern const struct address_space_operations erofs_fscache_access_aops;

extern const struct inode_operations erofs_generic_iops;
extern const struct inode_operations erofs_symlink_iops;
extern const struct inode_operations erofs_fast_symlink_iops;
extern const struct inode_operations erofs_dir_iops;

extern const struct file_operations erofs_file_fops;
extern const struct file_operations erofs_dir_fops;

extern const struct iomap_ops z_erofs_iomap_report_ops;

/* flags for erofs_fscache_register_cookie() */
#define EROFS_REG_COOKIE_SHARE		0x0001
#define EROFS_REG_COOKIE_NEED_NOEXIST	0x0002

void *erofs_read_metadata(struct super_block *sb, struct erofs_buf *buf,
			  erofs_off_t *offset, int *lengthp);
void erofs_unmap_metabuf(struct erofs_buf *buf);
void erofs_put_metabuf(struct erofs_buf *buf);
void *erofs_bread(struct erofs_buf *buf, erofs_blk_t blkaddr,
		  enum erofs_kmap_type type);
void erofs_init_metabuf(struct erofs_buf *buf, struct super_block *sb);
void *erofs_read_metabuf(struct erofs_buf *buf, struct super_block *sb,
			 erofs_blk_t blkaddr, enum erofs_kmap_type type);
int erofs_map_dev(struct super_block *sb, struct erofs_map_dev *dev);
int erofs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		 u64 start, u64 len);
int erofs_map_blocks(struct inode *inode, struct erofs_map_blocks *map);
struct inode *erofs_iget(struct super_block *sb, erofs_nid_t nid);
int erofs_getattr(struct mnt_idmap *idmap, const struct path *path,
		  struct kstat *stat, u32 request_mask,
		  unsigned int query_flags);
int erofs_namei(struct inode *dir, const struct qstr *name,
		erofs_nid_t *nid, unsigned int *d_type);

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

int erofs_register_sysfs(struct super_block *sb);
void erofs_unregister_sysfs(struct super_block *sb);
int __init erofs_init_sysfs(void);
void erofs_exit_sysfs(void);

struct page *erofs_allocpage(struct page **pagepool, gfp_t gfp);
static inline void erofs_pagepool_add(struct page **pagepool, struct page *page)
{
	set_page_private(page, (unsigned long)*pagepool);
	*pagepool = page;
}
void erofs_release_pages(struct page **pagepool);

#ifdef CONFIG_EROFS_FS_ZIP
void erofs_workgroup_put(struct erofs_workgroup *grp);
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
int erofs_try_to_free_all_cached_folios(struct erofs_sb_info *sbi,
					struct erofs_workgroup *egrp);
int z_erofs_map_blocks_iter(struct inode *inode, struct erofs_map_blocks *map,
			    int flags);
void *erofs_get_pcpubuf(unsigned int requiredpages);
void erofs_put_pcpubuf(void *ptr);
int erofs_pcpubuf_growsize(unsigned int nrpages);
void __init erofs_pcpubuf_init(void);
void erofs_pcpubuf_exit(void);
int erofs_init_managed_cache(struct super_block *sb);
int z_erofs_parse_cfgs(struct super_block *sb, struct erofs_super_block *dsb);
#else
static inline void erofs_shrinker_register(struct super_block *sb) {}
static inline void erofs_shrinker_unregister(struct super_block *sb) {}
static inline int erofs_init_shrinker(void) { return 0; }
static inline void erofs_exit_shrinker(void) {}
static inline int z_erofs_init_zip_subsystem(void) { return 0; }
static inline void z_erofs_exit_zip_subsystem(void) {}
static inline void erofs_pcpubuf_init(void) {}
static inline void erofs_pcpubuf_exit(void) {}
static inline int erofs_init_managed_cache(struct super_block *sb) { return 0; }
#endif	/* !CONFIG_EROFS_FS_ZIP */

#ifdef CONFIG_EROFS_FS_ZIP_LZMA
int __init z_erofs_lzma_init(void);
void z_erofs_lzma_exit(void);
#else
static inline int z_erofs_lzma_init(void) { return 0; }
static inline int z_erofs_lzma_exit(void) { return 0; }
#endif	/* !CONFIG_EROFS_FS_ZIP_LZMA */

#ifdef CONFIG_EROFS_FS_ZIP_DEFLATE
int __init z_erofs_deflate_init(void);
void z_erofs_deflate_exit(void);
#else
static inline int z_erofs_deflate_init(void) { return 0; }
static inline int z_erofs_deflate_exit(void) { return 0; }
#endif	/* !CONFIG_EROFS_FS_ZIP_DEFLATE */

#ifdef CONFIG_EROFS_FS_ONDEMAND
int erofs_fscache_register_fs(struct super_block *sb);
void erofs_fscache_unregister_fs(struct super_block *sb);

struct erofs_fscache *erofs_fscache_register_cookie(struct super_block *sb,
					char *name, unsigned int flags);
void erofs_fscache_unregister_cookie(struct erofs_fscache *fscache);
struct bio *erofs_fscache_bio_alloc(struct erofs_map_dev *mdev);
void erofs_fscache_submit_bio(struct bio *bio);
#else
static inline int erofs_fscache_register_fs(struct super_block *sb)
{
	return -EOPNOTSUPP;
}
static inline void erofs_fscache_unregister_fs(struct super_block *sb) {}

static inline
struct erofs_fscache *erofs_fscache_register_cookie(struct super_block *sb,
					char *name, unsigned int flags)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void erofs_fscache_unregister_cookie(struct erofs_fscache *fscache)
{
}
static inline struct bio *erofs_fscache_bio_alloc(struct erofs_map_dev *mdev) { return NULL; }
static inline void erofs_fscache_submit_bio(struct bio *bio) {}
#endif

#define EFSCORRUPTED    EUCLEAN         /* Filesystem is corrupted */

#endif	/* __EROFS_INTERNAL_H */
