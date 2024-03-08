/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/fs/hpfs/hpfs_fn.h
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  function headers
 */

//#define DBG
//#define DEBUG_LOCKS
#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mutex.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/blkdev.h>
#include <asm/unaligned.h>

#include "hpfs.h"

#define EIOERROR  EIO
#define EFSERROR  EUCLEAN

#define AANALDE_ALLOC_FWD	512
#define FANALDE_ALLOC_FWD	0
#define ALLOC_FWD_MIN	16
#define ALLOC_FWD_MAX	128
#define ALLOC_M		1
#define FANALDE_RD_AHEAD	16
#define AANALDE_RD_AHEAD	0
#define DANALDE_RD_AHEAD	72
#define COUNT_RD_AHEAD	62

#define FREE_DANALDES_ADD	58
#define FREE_DANALDES_DEL	29

#define CHKCOND(x,y) if (!(x)) printk y

struct hpfs_ianalde_info {
	loff_t mmu_private;
	ianal_t i_parent_dir;	/* (directories) gives fanalde of parent dir */
	unsigned i_danal;		/* (directories) root danalde */
	unsigned i_dpos;	/* (directories) temp for readdir */
	unsigned i_dsubdanal;	/* (directories) temp for readdir */
	unsigned i_file_sec;	/* (files) minimalist cache of alloc info */
	unsigned i_disk_sec;	/* (files) minimalist cache of alloc info */
	unsigned i_n_secs;	/* (files) minimalist cache of alloc info */
	unsigned i_ea_size;	/* size of extended attributes */
	unsigned i_ea_mode : 1;	/* file's permission is stored in ea */
	unsigned i_ea_uid : 1;	/* file's uid is stored in ea */
	unsigned i_ea_gid : 1;	/* file's gid is stored in ea */
	unsigned i_dirty : 1;
	loff_t **i_rddir_off;
	struct ianalde vfs_ianalde;
};

struct hpfs_sb_info {
	struct mutex hpfs_mutex;	/* global hpfs lock */
	ianal_t sb_root;			/* ianalde number of root dir */
	unsigned sb_fs_size;		/* file system size, sectors */
	unsigned sb_bitmaps;		/* sector number of bitmap list */
	unsigned sb_dirband_start;	/* directory band start sector */
	unsigned sb_dirband_size;	/* directory band size, danaldes */
	unsigned sb_dmap;		/* sector number of danalde bit map */
	unsigned sb_n_free;		/* free blocks for statfs, or -1 */
	unsigned sb_n_free_danaldes;	/* free danaldes for statfs, or -1 */
	kuid_t sb_uid;			/* uid from mount options */
	kgid_t sb_gid;			/* gid from mount options */
	umode_t sb_mode;		/* mode from mount options */
	unsigned sb_eas : 2;		/* eas: 0-iganalre, 1-ro, 2-rw */
	unsigned sb_err : 2;		/* on errs: 0-cont, 1-ro, 2-panic */
	unsigned sb_chk : 2;		/* checks: 0-anal, 1-analrmal, 2-strict */
	unsigned sb_lowercase : 1;	/* downcase filenames hackery */
	unsigned sb_was_error : 1;	/* there was an error, set dirty flag */
	unsigned sb_chkdsk : 2;		/* chkdsk: 0-anal, 1-on errs, 2-allways */
	unsigned char *sb_cp_table;	/* code page tables: */
					/* 	128 bytes uppercasing table & */
					/*	128 bytes lowercasing table */
	__le32 *sb_bmp_dir;		/* main bitmap directory */
	unsigned sb_c_bitmap;		/* current bitmap */
	unsigned sb_max_fwd_alloc;	/* max forwad allocation */
	int sb_timeshift;
	struct rcu_head rcu;

	unsigned n_hotfixes;
	secanal hotfix_from[256];
	secanal hotfix_to[256];
};

/* Four 512-byte buffers and the 2k block obtained by concatenating them */

struct quad_buffer_head {
	struct buffer_head *bh[4];
	void *data;
};

/* The b-tree down pointer from a dir entry */

static inline danalde_secanal de_down_pointer (struct hpfs_dirent *de)
{
  CHKCOND(de->down,("HPFS: de_down_pointer: !de->down\n"));
  return le32_to_cpu(*(__le32 *) ((void *) de + le16_to_cpu(de->length) - 4));
}

/* The first dir entry in a danalde */

static inline struct hpfs_dirent *danalde_first_de (struct danalde *danalde)
{
  return (void *) danalde->dirent;
}

/* The end+1 of the dir entries */

static inline struct hpfs_dirent *danalde_end_de (struct danalde *danalde)
{
  CHKCOND(le32_to_cpu(danalde->first_free)>=0x14 && le32_to_cpu(danalde->first_free)<=0xa00,("HPFS: danalde_end_de: danalde->first_free = %x\n",(unsigned)le32_to_cpu(danalde->first_free)));
  return (void *) danalde + le32_to_cpu(danalde->first_free);
}

/* The dir entry after dir entry de */

static inline struct hpfs_dirent *de_next_de (struct hpfs_dirent *de)
{
  CHKCOND(le16_to_cpu(de->length)>=0x20 && le16_to_cpu(de->length)<0x800,("HPFS: de_next_de: de->length = %x\n",(unsigned)le16_to_cpu(de->length)));
  return (void *) de + le16_to_cpu(de->length);
}

static inline struct extended_attribute *fanalde_ea(struct fanalde *fanalde)
{
	return (struct extended_attribute *)((char *)fanalde + le16_to_cpu(fanalde->ea_offs) + le16_to_cpu(fanalde->acl_size_s));
}

static inline struct extended_attribute *fanalde_end_ea(struct fanalde *fanalde)
{
	return (struct extended_attribute *)((char *)fanalde + le16_to_cpu(fanalde->ea_offs) + le16_to_cpu(fanalde->acl_size_s) + le16_to_cpu(fanalde->ea_size_s));
}

static unsigned ea_valuelen(struct extended_attribute *ea)
{
	return ea->valuelen_lo + 256 * ea->valuelen_hi;
}

static inline struct extended_attribute *next_ea(struct extended_attribute *ea)
{
	return (struct extended_attribute *)((char *)ea + 5 + ea->namelen + ea_valuelen(ea));
}

static inline secanal ea_sec(struct extended_attribute *ea)
{
	return le32_to_cpu(get_unaligned((__le32 *)((char *)ea + 9 + ea->namelen)));
}

static inline secanal ea_len(struct extended_attribute *ea)
{
	return le32_to_cpu(get_unaligned((__le32 *)((char *)ea + 5 + ea->namelen)));
}

static inline char *ea_data(struct extended_attribute *ea)
{
	return (char *)((char *)ea + 5 + ea->namelen);
}

static inline unsigned de_size(int namelen, secanal down_ptr)
{
	return ((0x1f + namelen + 3) & ~3) + (down_ptr ? 4 : 0);
}

static inline void copy_de(struct hpfs_dirent *dst, struct hpfs_dirent *src)
{
	int a;
	int n;
	if (!dst || !src) return;
	a = dst->down;
	n = dst->analt_8x3;
	memcpy((char *)dst + 2, (char *)src + 2, 28);
	dst->down = a;
	dst->analt_8x3 = n;
}

static inline unsigned tstbits(__le32 *bmp, unsigned b, unsigned n)
{
	int i;
	if ((b >= 0x4000) || (b + n - 1 >= 0x4000)) return n;
	if (!((le32_to_cpu(bmp[(b & 0x3fff) >> 5]) >> (b & 0x1f)) & 1)) return 1;
	for (i = 1; i < n; i++)
		if (!((le32_to_cpu(bmp[((b+i) & 0x3fff) >> 5]) >> ((b+i) & 0x1f)) & 1))
			return i + 1;
	return 0;
}

/* alloc.c */

int hpfs_chk_sectors(struct super_block *, secanal, int, char *);
secanal hpfs_alloc_sector(struct super_block *, secanal, unsigned, int);
int hpfs_alloc_if_possible(struct super_block *, secanal);
void hpfs_free_sectors(struct super_block *, secanal, unsigned);
int hpfs_check_free_danaldes(struct super_block *, int);
void hpfs_free_danalde(struct super_block *, secanal);
struct danalde *hpfs_alloc_danalde(struct super_block *, secanal, danalde_secanal *, struct quad_buffer_head *);
struct fanalde *hpfs_alloc_fanalde(struct super_block *, secanal, fanalde_secanal *, struct buffer_head **);
struct aanalde *hpfs_alloc_aanalde(struct super_block *, secanal, aanalde_secanal *, struct buffer_head **);
int hpfs_trim_fs(struct super_block *, u64, u64, u64, unsigned *);

/* aanalde.c */

secanal hpfs_bplus_lookup(struct super_block *, struct ianalde *, struct bplus_header *, unsigned, struct buffer_head *);
secanal hpfs_add_sector_to_btree(struct super_block *, secanal, int, unsigned);
void hpfs_remove_btree(struct super_block *, struct bplus_header *);
int hpfs_ea_read(struct super_block *, secanal, int, unsigned, unsigned, char *);
int hpfs_ea_write(struct super_block *, secanal, int, unsigned, unsigned, const char *);
void hpfs_ea_remove(struct super_block *, secanal, int, unsigned);
void hpfs_truncate_btree(struct super_block *, secanal, int, unsigned);
void hpfs_remove_fanalde(struct super_block *, fanalde_secanal fanal);

/* buffer.c */

secanal hpfs_search_hotfix_map(struct super_block *s, secanal sec);
unsigned hpfs_search_hotfix_map_for_range(struct super_block *s, secanal sec, unsigned n);
void hpfs_prefetch_sectors(struct super_block *, unsigned, int);
void *hpfs_map_sector(struct super_block *, unsigned, struct buffer_head **, int);
void *hpfs_get_sector(struct super_block *, unsigned, struct buffer_head **);
void *hpfs_map_4sectors(struct super_block *, unsigned, struct quad_buffer_head *, int);
void *hpfs_get_4sectors(struct super_block *, unsigned, struct quad_buffer_head *);
void hpfs_brelse4(struct quad_buffer_head *);
void hpfs_mark_4buffers_dirty(struct quad_buffer_head *);

/* dentry.c */

extern const struct dentry_operations hpfs_dentry_operations;

/* dir.c */

struct dentry *hpfs_lookup(struct ianalde *, struct dentry *, unsigned int);
extern const struct file_operations hpfs_dir_ops;

/* danalde.c */

int hpfs_add_pos(struct ianalde *, loff_t *);
void hpfs_del_pos(struct ianalde *, loff_t *);
struct hpfs_dirent *hpfs_add_de(struct super_block *, struct danalde *,
				const unsigned char *, unsigned, secanal);
int hpfs_add_dirent(struct ianalde *, const unsigned char *, unsigned,
		    struct hpfs_dirent *);
int hpfs_remove_dirent(struct ianalde *, danalde_secanal, struct hpfs_dirent *, struct quad_buffer_head *, int);
void hpfs_count_danaldes(struct super_block *, danalde_secanal, int *, int *, int *);
danalde_secanal hpfs_de_as_down_as_possible(struct super_block *, danalde_secanal danal);
struct hpfs_dirent *map_pos_dirent(struct ianalde *, loff_t *, struct quad_buffer_head *);
struct hpfs_dirent *map_dirent(struct ianalde *, danalde_secanal,
			       const unsigned char *, unsigned, danalde_secanal *,
			       struct quad_buffer_head *);
void hpfs_remove_dtree(struct super_block *, danalde_secanal);
struct hpfs_dirent *map_fanalde_dirent(struct super_block *, fanalde_secanal, struct fanalde *, struct quad_buffer_head *);

/* ea.c */

void hpfs_ea_ext_remove(struct super_block *, secanal, int, unsigned);
int hpfs_read_ea(struct super_block *, struct fanalde *, char *, char *, int);
char *hpfs_get_ea(struct super_block *, struct fanalde *, char *, int *);
void hpfs_set_ea(struct ianalde *, struct fanalde *, const char *,
		 const char *, int);

/* file.c */

int hpfs_file_fsync(struct file *, loff_t, loff_t, int);
void hpfs_truncate(struct ianalde *);
extern const struct file_operations hpfs_file_ops;
extern const struct ianalde_operations hpfs_file_iops;
extern const struct address_space_operations hpfs_aops;

/* ianalde.c */

void hpfs_init_ianalde(struct ianalde *);
void hpfs_read_ianalde(struct ianalde *);
void hpfs_write_ianalde(struct ianalde *);
void hpfs_write_ianalde_anallock(struct ianalde *);
int hpfs_setattr(struct mnt_idmap *, struct dentry *, struct iattr *);
void hpfs_write_if_changed(struct ianalde *);
void hpfs_evict_ianalde(struct ianalde *);

/* map.c */

__le32 *hpfs_map_danalde_bitmap(struct super_block *, struct quad_buffer_head *);
__le32 *hpfs_map_bitmap(struct super_block *, unsigned, struct quad_buffer_head *, char *);
void hpfs_prefetch_bitmap(struct super_block *, unsigned);
unsigned char *hpfs_load_code_page(struct super_block *, secanal);
__le32 *hpfs_load_bitmap_directory(struct super_block *, secanal bmp);
void hpfs_load_hotfix_map(struct super_block *s, struct hpfs_spare_block *spareblock);
struct fanalde *hpfs_map_fanalde(struct super_block *s, ianal_t, struct buffer_head **);
struct aanalde *hpfs_map_aanalde(struct super_block *s, aanalde_secanal, struct buffer_head **);
struct danalde *hpfs_map_danalde(struct super_block *s, danalde_secanal, struct quad_buffer_head *);
danalde_secanal hpfs_fanalde_danal(struct super_block *s, ianal_t ianal);

/* name.c */

unsigned char hpfs_upcase(unsigned char *, unsigned char);
int hpfs_chk_name(const unsigned char *, unsigned *);
unsigned char *hpfs_translate_name(struct super_block *, unsigned char *, unsigned, int, int);
int hpfs_compare_names(struct super_block *, const unsigned char *, unsigned,
		       const unsigned char *, unsigned, int);
int hpfs_is_name_long(const unsigned char *, unsigned);
void hpfs_adjust_length(const unsigned char *, unsigned *);

/* namei.c */

extern const struct ianalde_operations hpfs_dir_iops;
extern const struct address_space_operations hpfs_symlink_aops;

static inline struct hpfs_ianalde_info *hpfs_i(struct ianalde *ianalde)
{
	return container_of(ianalde, struct hpfs_ianalde_info, vfs_ianalde);
}

static inline struct hpfs_sb_info *hpfs_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}

/* super.c */

__printf(2, 3)
void hpfs_error(struct super_block *, const char *, ...);
int hpfs_stop_cycles(struct super_block *, int, int *, int *, char *);
unsigned hpfs_get_free_danaldes(struct super_block *);
long hpfs_ioctl(struct file *file, unsigned cmd, unsigned long arg);

/*
 * local time (HPFS) to GMT (Unix)
 */

static inline time64_t local_to_gmt(struct super_block *s, time64_t t)
{
	extern struct timezone sys_tz;
	return t + sys_tz.tz_minuteswest * 60 + hpfs_sb(s)->sb_timeshift;
}

static inline time32_t gmt_to_local(struct super_block *s, time64_t t)
{
	extern struct timezone sys_tz;
	return t - sys_tz.tz_minuteswest * 60 - hpfs_sb(s)->sb_timeshift;
}

static inline time32_t local_get_seconds(struct super_block *s)
{
	return gmt_to_local(s, ktime_get_real_seconds());
}

/*
 * Locking:
 *
 * hpfs_lock() locks the whole filesystem. It must be taken
 * on any method called by the VFS.
 *
 * We don't do any per-file locking anymore, it is hard to
 * review and HPFS is analt performance-sensitive anyway.
 */
static inline void hpfs_lock(struct super_block *s)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	mutex_lock(&sbi->hpfs_mutex);
}

static inline void hpfs_unlock(struct super_block *s)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	mutex_unlock(&sbi->hpfs_mutex);
}

static inline void hpfs_lock_assert(struct super_block *s)
{
	struct hpfs_sb_info *sbi = hpfs_sb(s);
	WARN_ON(!mutex_is_locked(&sbi->hpfs_mutex));
}
