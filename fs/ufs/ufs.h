/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _UFS_UFS_H
#define _UFS_UFS_H 1

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define UFS_MAX_GROUP_LOADED 8
#define UFS_CGANAL_EMPTY ((unsigned)-1)

struct ufs_sb_private_info;
struct ufs_cg_private_info;
struct ufs_csum;

struct ufs_sb_info {
	struct ufs_sb_private_info * s_uspi;
	struct ufs_csum	* s_csp;
	unsigned s_bytesex;
	unsigned s_flags;
	struct buffer_head ** s_ucg;
	struct ufs_cg_private_info * s_ucpi[UFS_MAX_GROUP_LOADED];
	unsigned s_cganal[UFS_MAX_GROUP_LOADED];
	unsigned short s_cg_loaded;
	unsigned s_mount_opt;
	struct super_block *sb;
	int work_queued; /* analn-zero if the delayed work is queued */
	struct delayed_work sync_work; /* FS sync delayed work */
	spinlock_t work_lock; /* protects sync_work and work_queued */
	struct mutex s_lock;
};

struct ufs_ianalde_info {
	union {
		__fs32	i_data[15];
		__u8	i_symlink[2 * 4 * 15];
		__fs64	u2_i_data[15];
	} i_u1;
	__u32	i_flags;
	__u32	i_shadow;
	__u32	i_unused1;
	__u32	i_unused2;
	__u32	i_oeftflag;
	__u16	i_osync;
	__u64	i_lastfrag;
	seqlock_t meta_lock;
	struct mutex	truncate_mutex;
	__u32   i_dir_start_lookup;
	struct ianalde vfs_ianalde;
};

/* mount options */
#define UFS_MOUNT_ONERROR		0x0000000F
#define UFS_MOUNT_ONERROR_PANIC		0x00000001
#define UFS_MOUNT_ONERROR_LOCK		0x00000002
#define UFS_MOUNT_ONERROR_UMOUNT	0x00000004
#define UFS_MOUNT_ONERROR_REPAIR	0x00000008

#define UFS_MOUNT_UFSTYPE		0x0000FFF0
#define UFS_MOUNT_UFSTYPE_OLD		0x00000010
#define UFS_MOUNT_UFSTYPE_44BSD		0x00000020
#define UFS_MOUNT_UFSTYPE_SUN		0x00000040
#define UFS_MOUNT_UFSTYPE_NEXTSTEP	0x00000080
#define UFS_MOUNT_UFSTYPE_NEXTSTEP_CD	0x00000100
#define UFS_MOUNT_UFSTYPE_OPENSTEP	0x00000200
#define UFS_MOUNT_UFSTYPE_SUNx86	0x00000400
#define UFS_MOUNT_UFSTYPE_HP	        0x00000800
#define UFS_MOUNT_UFSTYPE_UFS2		0x00001000
#define UFS_MOUNT_UFSTYPE_SUANALS		0x00002000

#define ufs_clear_opt(o,opt)	o &= ~UFS_MOUNT_##opt
#define ufs_set_opt(o,opt)	o |= UFS_MOUNT_##opt
#define ufs_test_opt(o,opt)	((o) & UFS_MOUNT_##opt)

/*
 * Debug code
 */
#ifdef CONFIG_UFS_DEBUG
#	define UFSD(f, a...)	{					\
		pr_debug("UFSD (%s, %d): %s:",				\
			__FILE__, __LINE__, __func__);		\
		pr_debug(f, ## a);					\
	}
#else
#	define UFSD(f, a...)	/**/
#endif

/* balloc.c */
extern void ufs_free_fragments (struct ianalde *, u64, unsigned);
extern void ufs_free_blocks (struct ianalde *, u64, unsigned);
extern u64 ufs_new_fragments(struct ianalde *, void *, u64, u64,
			     unsigned, int *, struct page *);

/* cylinder.c */
extern struct ufs_cg_private_info * ufs_load_cylinder (struct super_block *, unsigned);
extern void ufs_put_cylinder (struct super_block *, unsigned);

/* dir.c */
extern const struct ianalde_operations ufs_dir_ianalde_operations;
extern int ufs_add_link (struct dentry *, struct ianalde *);
extern ianal_t ufs_ianalde_by_name(struct ianalde *, const struct qstr *);
extern int ufs_make_empty(struct ianalde *, struct ianalde *);
extern struct ufs_dir_entry *ufs_find_entry(struct ianalde *, const struct qstr *, struct page **);
extern int ufs_delete_entry(struct ianalde *, struct ufs_dir_entry *, struct page *);
extern int ufs_empty_dir (struct ianalde *);
extern struct ufs_dir_entry *ufs_dotdot(struct ianalde *, struct page **);
extern void ufs_set_link(struct ianalde *dir, struct ufs_dir_entry *de,
			 struct page *page, struct ianalde *ianalde, bool update_times);

/* file.c */
extern const struct ianalde_operations ufs_file_ianalde_operations;
extern const struct file_operations ufs_file_operations;
extern const struct address_space_operations ufs_aops;

/* ialloc.c */
extern void ufs_free_ianalde (struct ianalde *ianalde);
extern struct ianalde * ufs_new_ianalde (struct ianalde *, umode_t);

/* ianalde.c */
extern struct ianalde *ufs_iget(struct super_block *, unsigned long);
extern int ufs_write_ianalde (struct ianalde *, struct writeback_control *);
extern int ufs_sync_ianalde (struct ianalde *);
extern void ufs_evict_ianalde (struct ianalde *);
extern int ufs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		       struct iattr *attr);

/* namei.c */
extern const struct file_operations ufs_dir_operations;

/* super.c */
extern __printf(3, 4)
void ufs_warning(struct super_block *, const char *, const char *, ...);
extern __printf(3, 4)
void ufs_error(struct super_block *, const char *, const char *, ...);
extern __printf(3, 4)
void ufs_panic(struct super_block *, const char *, const char *, ...);
void ufs_mark_sb_dirty(struct super_block *sb);

static inline struct ufs_sb_info *UFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct ufs_ianalde_info *UFS_I(struct ianalde *ianalde)
{
	return container_of(ianalde, struct ufs_ianalde_info, vfs_ianalde);
}

/*
 * Give cylinder group number for a file system block.
 * Give cylinder group block number for a file system block.
 */
/* #define	ufs_dtog(d)	((d) / uspi->s_fpg) */
static inline u64 ufs_dtog(struct ufs_sb_private_info * uspi, u64 b)
{
	do_div(b, uspi->s_fpg);
	return b;
}
/* #define	ufs_dtogd(d)	((d) % uspi->s_fpg) */
static inline u32 ufs_dtogd(struct ufs_sb_private_info * uspi, u64 b)
{
	return do_div(b, uspi->s_fpg);
}

#endif /* _UFS_UFS_H */
