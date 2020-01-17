/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SYSV_H
#define _SYSV_H

#include <linux/buffer_head.h>

typedef __u16 __bitwise __fs16;
typedef __u32 __bitwise __fs32;

#include <linux/sysv_fs.h>

/*
 * SystemV/V7/Coherent super-block data in memory
 *
 * The SystemV/V7/Coherent superblock contains dynamic data (it gets modified
 * while the system is running). This is in contrast to the Minix and Berkeley
 * filesystems (where the superblock is never modified). This affects the
 * sync() operation: we must keep the superblock in a disk buffer and use this
 * one as our "working copy".
 */

struct sysv_sb_info {
	struct super_block *s_sb;	/* VFS superblock */
	int	       s_type;		/* file system type: FSTYPE_{XENIX|SYSV|COH} */
	char	       s_bytesex;	/* bytesex (le/be/pdp) */
	unsigned int   s_iyesdes_per_block;	/* number of iyesdes per block */
	unsigned int   s_iyesdes_per_block_1;	/* iyesdes_per_block - 1 */
	unsigned int   s_iyesdes_per_block_bits;	/* log2(iyesdes_per_block) */
	unsigned int   s_ind_per_block;		/* number of indirections per block */
	unsigned int   s_ind_per_block_bits;	/* log2(ind_per_block) */
	unsigned int   s_ind_per_block_2;	/* ind_per_block ^ 2 */
	unsigned int   s_toobig_block;		/* 10 + ipb + ipb^2 + ipb^3 */
	unsigned int   s_block_base;	/* physical block number of block 0 */
	unsigned short s_fic_size;	/* free iyesde cache size, NICINOD */
	unsigned short s_flc_size;	/* free block list chunk size, NICFREE */
	/* The superblock is kept in one or two disk buffers: */
	struct buffer_head *s_bh1;
	struct buffer_head *s_bh2;
	/* These are pointers into the disk buffer, to compensate for
	   different superblock layout. */
	char *         s_sbd1;		/* entire superblock data, for part 1 */
	char *         s_sbd2;		/* entire superblock data, for part 2 */
	__fs16         *s_sb_fic_count;	/* pointer to s_sbd->s_niyesde */
        sysv_iyes_t     *s_sb_fic_iyesdes; /* pointer to s_sbd->s_iyesde */
	__fs16         *s_sb_total_free_iyesdes; /* pointer to s_sbd->s_tiyesde */
	__fs16         *s_bcache_count;	/* pointer to s_sbd->s_nfree */
	sysv_zone_t    *s_bcache;	/* pointer to s_sbd->s_free */
	__fs32         *s_free_blocks;	/* pointer to s_sbd->s_tfree */
	__fs32         *s_sb_time;	/* pointer to s_sbd->s_time */
	__fs32         *s_sb_state;	/* pointer to s_sbd->s_state, only FSTYPE_SYSV */
	/* We keep those superblock entities that don't change here;
	   this saves us an indirection and perhaps a conversion. */
	u32            s_firstiyesdezone; /* index of first iyesde zone */
	u32            s_firstdatazone;	/* same as s_sbd->s_isize */
	u32            s_niyesdes;	/* total number of iyesdes */
	u32            s_ndatazones;	/* total number of data zones */
	u32            s_nzones;	/* same as s_sbd->s_fsize */
	u16	       s_namelen;       /* max length of dir entry */
	int	       s_forced_ro;
	struct mutex s_lock;
};

/*
 * SystemV/V7/Coherent FS iyesde data in memory
 */
struct sysv_iyesde_info {
	__fs32		i_data[13];
	u32		i_dir_start_lookup;
	struct iyesde	vfs_iyesde;
};


static inline struct sysv_iyesde_info *SYSV_I(struct iyesde *iyesde)
{
	return container_of(iyesde, struct sysv_iyesde_info, vfs_iyesde);
}

static inline struct sysv_sb_info *SYSV_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}


/* identify the FS in memory */
enum {
	FSTYPE_NONE = 0,
	FSTYPE_XENIX,
	FSTYPE_SYSV4,
	FSTYPE_SYSV2,
	FSTYPE_COH,
	FSTYPE_V7,
	FSTYPE_AFS,
	FSTYPE_END,
};

#define SYSV_MAGIC_BASE		0x012FF7B3

#define XENIX_SUPER_MAGIC	(SYSV_MAGIC_BASE+FSTYPE_XENIX)
#define SYSV4_SUPER_MAGIC	(SYSV_MAGIC_BASE+FSTYPE_SYSV4)
#define SYSV2_SUPER_MAGIC	(SYSV_MAGIC_BASE+FSTYPE_SYSV2)
#define COH_SUPER_MAGIC		(SYSV_MAGIC_BASE+FSTYPE_COH)


/* Admissible values for i_nlink: 0.._LINK_MAX */
enum {
	XENIX_LINK_MAX	=	126,	/* ?? */
	SYSV_LINK_MAX	=	126,	/* 127? 251? */
	V7_LINK_MAX     =	126,	/* ?? */
	COH_LINK_MAX	=	10000,
};


static inline void dirty_sb(struct super_block *sb)
{
	struct sysv_sb_info *sbi = SYSV_SB(sb);

	mark_buffer_dirty(sbi->s_bh1);
	if (sbi->s_bh1 != sbi->s_bh2)
		mark_buffer_dirty(sbi->s_bh2);
}


/* ialloc.c */
extern struct sysv_iyesde *sysv_raw_iyesde(struct super_block *, unsigned,
			struct buffer_head **);
extern struct iyesde * sysv_new_iyesde(const struct iyesde *, umode_t);
extern void sysv_free_iyesde(struct iyesde *);
extern unsigned long sysv_count_free_iyesdes(struct super_block *);

/* balloc.c */
extern sysv_zone_t sysv_new_block(struct super_block *);
extern void sysv_free_block(struct super_block *, sysv_zone_t);
extern unsigned long sysv_count_free_blocks(struct super_block *);

/* itree.c */
extern void sysv_truncate(struct iyesde *);
extern int sysv_prepare_chunk(struct page *page, loff_t pos, unsigned len);

/* iyesde.c */
extern struct iyesde *sysv_iget(struct super_block *, unsigned int);
extern int sysv_write_iyesde(struct iyesde *, struct writeback_control *wbc);
extern int sysv_sync_iyesde(struct iyesde *);
extern void sysv_set_iyesde(struct iyesde *, dev_t);
extern int sysv_getattr(const struct path *, struct kstat *, u32, unsigned int);
extern int sysv_init_icache(void);
extern void sysv_destroy_icache(void);


/* dir.c */
extern struct sysv_dir_entry *sysv_find_entry(struct dentry *, struct page **);
extern int sysv_add_link(struct dentry *, struct iyesde *);
extern int sysv_delete_entry(struct sysv_dir_entry *, struct page *);
extern int sysv_make_empty(struct iyesde *, struct iyesde *);
extern int sysv_empty_dir(struct iyesde *);
extern void sysv_set_link(struct sysv_dir_entry *, struct page *,
			struct iyesde *);
extern struct sysv_dir_entry *sysv_dotdot(struct iyesde *, struct page **);
extern iyes_t sysv_iyesde_by_name(struct dentry *);


extern const struct iyesde_operations sysv_file_iyesde_operations;
extern const struct iyesde_operations sysv_dir_iyesde_operations;
extern const struct file_operations sysv_file_operations;
extern const struct file_operations sysv_dir_operations;
extern const struct address_space_operations sysv_aops;
extern const struct super_operations sysv_sops;


enum {
	BYTESEX_LE,
	BYTESEX_PDP,
	BYTESEX_BE,
};

static inline u32 PDP_swab(u32 x)
{
#ifdef __LITTLE_ENDIAN
	return ((x & 0xffff) << 16) | ((x & 0xffff0000) >> 16);
#else
#ifdef __BIG_ENDIAN
	return ((x & 0xff00ff) << 8) | ((x & 0xff00ff00) >> 8);
#else
#error BYTESEX
#endif
#endif
}

static inline __u32 fs32_to_cpu(struct sysv_sb_info *sbi, __fs32 n)
{
	if (sbi->s_bytesex == BYTESEX_PDP)
		return PDP_swab((__force __u32)n);
	else if (sbi->s_bytesex == BYTESEX_LE)
		return le32_to_cpu((__force __le32)n);
	else
		return be32_to_cpu((__force __be32)n);
}

static inline __fs32 cpu_to_fs32(struct sysv_sb_info *sbi, __u32 n)
{
	if (sbi->s_bytesex == BYTESEX_PDP)
		return (__force __fs32)PDP_swab(n);
	else if (sbi->s_bytesex == BYTESEX_LE)
		return (__force __fs32)cpu_to_le32(n);
	else
		return (__force __fs32)cpu_to_be32(n);
}

static inline __fs32 fs32_add(struct sysv_sb_info *sbi, __fs32 *n, int d)
{
	if (sbi->s_bytesex == BYTESEX_PDP)
		*(__u32*)n = PDP_swab(PDP_swab(*(__u32*)n)+d);
	else if (sbi->s_bytesex == BYTESEX_LE)
		le32_add_cpu((__le32 *)n, d);
	else
		be32_add_cpu((__be32 *)n, d);
	return *n;
}

static inline __u16 fs16_to_cpu(struct sysv_sb_info *sbi, __fs16 n)
{
	if (sbi->s_bytesex != BYTESEX_BE)
		return le16_to_cpu((__force __le16)n);
	else
		return be16_to_cpu((__force __be16)n);
}

static inline __fs16 cpu_to_fs16(struct sysv_sb_info *sbi, __u16 n)
{
	if (sbi->s_bytesex != BYTESEX_BE)
		return (__force __fs16)cpu_to_le16(n);
	else
		return (__force __fs16)cpu_to_be16(n);
}

static inline __fs16 fs16_add(struct sysv_sb_info *sbi, __fs16 *n, int d)
{
	if (sbi->s_bytesex != BYTESEX_BE)
		le16_add_cpu((__le16 *)n, d);
	else
		be16_add_cpu((__be16 *)n, d);
	return *n;
}

#endif /* _SYSV_H */
