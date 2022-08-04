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
struct pipe_inode_info;

/*
 * block/bdev.c
 */
#ifdef CONFIG_BLOCK
extern void __init bdev_cache_init(void);

void emergency_thaw_bdev(struct super_block *sb);
#else
static inline void bdev_cache_init(void)
{
}
static inline int emergency_thaw_bdev(struct super_block *sb)
{
	return 0;
}
#endif /* CONFIG_BLOCK */

/*
 * buffer.c
 */
int __block_write_begin_int(struct folio *folio, loff_t pos, unsigned len,
		get_block_t *get_block, const struct iomap *iomap);

/*
 * char_dev.c
 */
extern void __init chrdev_init(void);

/*
 * fs_context.c
 */
extern const struct fs_context_operations legacy_fs_context_ops;
extern int parse_monolithic_mount_data(struct fs_context *, void *);
extern void vfs_clean_context(struct fs_context *fc);
extern int finish_clean_context(struct fs_context *fc);

/*
 * namei.c
 */
extern int filename_lookup(int dfd, struct filename *name, unsigned flags,
			   struct path *path, struct path *root);
extern int vfs_path_lookup(struct dentry *, struct vfsmount *,
			   const char *, unsigned int, struct path *);
int do_rmdir(int dfd, struct filename *name);
int do_unlinkat(int dfd, struct filename *name);
int may_linkat(struct user_namespace *mnt_userns, struct path *link);
int do_renameat2(int olddfd, struct filename *oldname, int newdfd,
		 struct filename *newname, unsigned int flags);
int do_mkdirat(int dfd, struct filename *name, umode_t mode);
int do_symlinkat(struct filename *from, int newdfd, struct filename *to);
int do_linkat(int olddfd, struct filename *old, int newdfd,
			struct filename *new, int flags);

/*
 * namespace.c
 */
extern struct vfsmount *lookup_mnt(const struct path *);
extern int finish_automount(struct vfsmount *, const struct path *);

extern int sb_prepare_remount_readonly(struct super_block *);

extern void __init mnt_init(void);

extern int __mnt_want_write_file(struct file *);
extern void __mnt_drop_write_file(struct file *);

extern void dissolve_on_fput(struct vfsmount *);
extern bool may_mount(void);

int path_mount(const char *dev_name, struct path *path,
		const char *type_page, unsigned long flags, void *data_page);
int path_umount(struct path *path, int flags);

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
struct super_block *user_get_super(dev_t, bool excl);
void put_super(struct super_block *sb);
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
extern struct file *do_file_open_root(const struct path *,
		const char *, const struct open_flags *);
extern struct open_how build_open_how(int flags, umode_t mode);
extern int build_open_flags(const struct open_how *how, struct open_flags *op);
extern struct file *__close_fd_get_file(unsigned int fd);

long do_sys_ftruncate(unsigned int fd, loff_t length, int small);
int chmod_common(const struct path *path, umode_t mode);
int do_fchownat(int dfd, const char __user *filename, uid_t user, gid_t group,
		int flag);
int chown_common(const struct path *path, uid_t user, gid_t group);
extern int vfs_open(const struct path *, struct file *);

/*
 * inode.c
 */
extern long prune_icache_sb(struct super_block *sb, struct shrink_control *sc);
extern int dentry_needs_remove_privs(struct dentry *dentry);

/*
 * fs-writeback.c
 */
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

int getname_statx_lookup_flags(int flags);
int do_statx(int dfd, struct filename *filename, unsigned int flags,
	     unsigned int mask, struct statx __user *buffer);

/*
 * fs/splice.c:
 */
long splice_file_to_pipe(struct file *in,
			 struct pipe_inode_info *opipe,
			 loff_t *offset,
			 size_t len, unsigned int flags);

/*
 * fs/xattr.c:
 */
struct xattr_name {
	char name[XATTR_NAME_MAX + 1];
};

struct xattr_ctx {
	/* Value of attribute */
	union {
		const void __user *cvalue;
		void __user *value;
	};
	void *kvalue;
	size_t size;
	/* Attribute name */
	struct xattr_name *kname;
	unsigned int flags;
};


ssize_t do_getxattr(struct user_namespace *mnt_userns,
		    struct dentry *d,
		    struct xattr_ctx *ctx);

int setxattr_copy(const char __user *name, struct xattr_ctx *ctx);
int do_setxattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		struct xattr_ctx *ctx);
