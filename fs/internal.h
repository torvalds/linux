/* SPDX-License-Identifier: GPL-2.0-or-later */
/* fs/ internal definitions
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

struct super_block;
struct file_system_type;
struct iomap;
struct iomap_ops;
struct linux_binprm;
struct path;
struct mount;
struct shrink_control;
struct fs_context;
struct user_namespace;

/*
 * block_dev.c
 */
#ifdef CONFIG_BLOCK
extern void __init bdev_cache_init(void);

extern int __sync_blockdev(struct block_device *bdev, int wait);

#else
static inline void bdev_cache_init(void)
{
}

static inline int __sync_blockdev(struct block_device *bdev, int wait)
{
	return 0;
}
#endif

/*
 * buffer.c
 */
extern int __block_write_begin_int(struct page *page, loff_t pos, unsigned len,
		get_block_t *get_block, struct iomap *iomap);

/*
 * char_dev.c
 */
extern void __init chrdev_init(void);

/*
 * fs_context.c
 */
extern const struct fs_context_operations legacy_fs_context_ops;
extern int parse_monolithic_mount_data(struct fs_context *, void *);
extern void fc_drop_locked(struct fs_context *);
extern void vfs_clean_context(struct fs_context *fc);
extern int finish_clean_context(struct fs_context *fc);

/*
 * namei.c
 */
extern int filename_lookup(int dfd, struct filename *name, unsigned flags,
			   struct path *path, struct path *root);
extern int vfs_path_lookup(struct dentry *, struct vfsmount *,
			   const char *, unsigned int, struct path *);
long do_mknodat(int dfd, const char __user *filename, umode_t mode,
		unsigned int dev);
long do_mkdirat(int dfd, const char __user *pathname, umode_t mode);
long do_rmdir(int dfd, const char __user *pathname);
long do_unlinkat(int dfd, struct filename *name);
long do_symlinkat(const char __user *oldname, int newdfd,
		  const char __user *newname);
int do_linkat(int olddfd, const char __user *oldname, int newdfd,
	      const char __user *newname, int flags);

/*
 * namespace.c
 */
extern void *copy_mount_options(const void __user *);
extern char *copy_mount_string(const void __user *);

extern struct vfsmount *lookup_mnt(const struct path *);
extern int finish_automount(struct vfsmount *, struct path *);

extern int sb_prepare_remount_readonly(struct super_block *);

extern void __init mnt_init(void);

extern int __mnt_want_write_file(struct file *);
extern void __mnt_drop_write_file(struct file *);

extern void dissolve_on_fput(struct vfsmount *);
/*
 * fs_struct.c
 */
extern void chroot_fs_refs(const struct path *, const struct path *);

/*
 * file_table.c
 */
extern struct file *alloc_empty_file(int, const struct cred *);
extern struct file *alloc_empty_file_noaccount(int, const struct cred *);

/*
 * super.c
 */
extern int reconfigure_super(struct fs_context *);
extern bool trylock_super(struct super_block *sb);
extern struct super_block *user_get_super(dev_t);
extern bool mount_capable(struct fs_context *);

/*
 * open.c
 */
struct open_flags {
	int open_flag;
	umode_t mode;
	int acc_mode;
	int intent;
	int lookup_flags;
};
extern struct file *do_filp_open(int dfd, struct filename *pathname,
		const struct open_flags *op);
extern struct file *do_file_open_root(struct dentry *, struct vfsmount *,
		const char *, const struct open_flags *);
extern struct open_how build_open_how(int flags, umode_t mode);
extern int build_open_flags(const struct open_how *how, struct open_flags *op);

long do_sys_ftruncate(unsigned int fd, loff_t length, int small);
long do_faccessat(int dfd, const char __user *filename, int mode);
int do_fchmodat(int dfd, const char __user *filename, umode_t mode);
int do_fchownat(int dfd, const char __user *filename, uid_t user, gid_t group,
		int flag);

extern int vfs_open(const struct path *, struct file *);

/*
 * inode.c
 */
extern long prune_icache_sb(struct super_block *sb, struct shrink_control *sc);
extern void inode_add_lru(struct inode *inode);
extern int dentry_needs_remove_privs(struct dentry *dentry);

/*
 * fs-writeback.c
 */
extern void inode_io_list_del(struct inode *inode);

extern long get_nr_dirty_inodes(void);
extern int invalidate_inodes(struct super_block *, bool);

/*
 * dcache.c
 */
extern int d_set_mounted(struct dentry *dentry);
extern long prune_dcache_sb(struct super_block *sb, struct shrink_control *sc);
extern struct dentry *d_alloc_cursor(struct dentry *);
extern struct dentry * d_alloc_pseudo(struct super_block *, const struct qstr *);
extern char *simple_dname(struct dentry *, char *, int);
extern void dput_to_list(struct dentry *, struct list_head *);
extern void shrink_dentry_list(struct list_head *);

/*
 * read_write.c
 */
extern int rw_verify_area(int, struct file *, const loff_t *, size_t);

/*
 * pipe.c
 */
extern const struct file_operations pipefifo_fops;

/*
 * fs_pin.c
 */
extern void group_pin_kill(struct hlist_head *p);
extern void mnt_pin_kill(struct mount *m);

/*
 * fs/nsfs.c
 */
extern const struct dentry_operations ns_dentry_operations;

/* direct-io.c: */
int sb_init_dio_done_wq(struct super_block *sb);

/*
 * fs/stat.c:
 */
unsigned vfs_stat_set_lookup_flags(unsigned *lookup_flags, int flags);
int cp_statx(const struct kstat *stat, struct statx __user *buffer);
