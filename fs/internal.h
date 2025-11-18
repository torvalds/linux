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
struct pipe_inode_info;
struct iov_iter;
struct mnt_idmap;
struct ns_common;

/*
 * block/bdev.c
 */
#ifdef CONFIG_BLOCK
extern void __init bdev_cache_init(void);
#else
static inline void bdev_cache_init(void)
{
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
			   struct path *path, const struct path *root);
int do_rmdir(int dfd, struct filename *name);
int do_unlinkat(int dfd, struct filename *name);
int may_linkat(struct mnt_idmap *idmap, const struct path *link);
int do_renameat2(int olddfd, struct filename *oldname, int newdfd,
		 struct filename *newname, unsigned int flags);
int do_mkdirat(int dfd, struct filename *name, umode_t mode);
int do_symlinkat(struct filename *from, int newdfd, struct filename *to);
int do_linkat(int olddfd, struct filename *old, int newdfd,
			struct filename *new, int flags);
int vfs_tmpfile(struct mnt_idmap *idmap,
		const struct path *parentpath,
		struct file *file, umode_t mode);
struct dentry *d_hash_and_lookup(struct dentry *, struct qstr *);

/*
 * namespace.c
 */
extern struct vfsmount *lookup_mnt(const struct path *);
extern int finish_automount(struct vfsmount *, const struct path *);

extern int sb_prepare_remount_readonly(struct super_block *);

extern void __init mnt_init(void);

int mnt_get_write_access_file(struct file *file);
void mnt_put_write_access_file(struct file *file);

extern void dissolve_on_fput(struct vfsmount *);
extern bool may_mount(void);

int path_mount(const char *dev_name, const struct path *path,
		const char *type_page, unsigned long flags, void *data_page);
int path_umount(const struct path *path, int flags);

int show_path(struct seq_file *m, struct dentry *root);

/*
 * fs_struct.c
 */
extern void chroot_fs_refs(const struct path *, const struct path *);

/*
 * file_table.c
 */
struct file *alloc_empty_file(int flags, const struct cred *cred);
struct file *alloc_empty_file_noaccount(int flags, const struct cred *cred);
struct file *alloc_empty_backing_file(int flags, const struct cred *cred);
void backing_file_set_user_path(struct file *f, const struct path *path);

static inline void file_put_write_access(struct file *file)
{
	put_write_access(file->f_inode);
	mnt_put_write_access(file->f_path.mnt);
	if (unlikely(file->f_mode & FMODE_BACKING))
		mnt_put_write_access(backing_file_user_path(file)->mnt);
}

static inline void put_file_access(struct file *file)
{
	if ((file->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ) {
		i_readcount_dec(file->f_inode);
	} else if (file->f_mode & FMODE_WRITER) {
		file_put_write_access(file);
	}
}

void fput_close_sync(struct file *);
void fput_close(struct file *);

/*
 * super.c
 */
extern int reconfigure_super(struct fs_context *);
extern bool super_trylock_shared(struct super_block *sb);
struct super_block *user_get_super(dev_t, bool excl);
void put_super(struct super_block *sb);
extern bool mount_capable(struct fs_context *);
int sb_init_dio_done_wq(struct super_block *sb);

/*
 * Prepare superblock for changing its read-only state (i.e., either remount
 * read-write superblock read-only or vice versa). After this function returns
 * mnt_is_readonly() will return true for any mount of the superblock if its
 * caller is able to observe any changes done by the remount. This holds until
 * sb_end_ro_state_change() is called.
 */
static inline void sb_start_ro_state_change(struct super_block *sb)
{
	WRITE_ONCE(sb->s_readonly_remount, 1);
	/*
	 * For RO->RW transition, the barrier pairs with the barrier in
	 * mnt_is_readonly() making sure if mnt_is_readonly() sees SB_RDONLY
	 * cleared, it will see s_readonly_remount set.
	 * For RW->RO transition, the barrier pairs with the barrier in
	 * mnt_get_write_access() before the mnt_is_readonly() check.
	 * The barrier makes sure if mnt_get_write_access() sees MNT_WRITE_HOLD
	 * already cleared, it will see s_readonly_remount set.
	 */
	smp_wmb();
}

/*
 * Ends section changing read-only state of the superblock. After this function
 * returns if mnt_is_readonly() returns false, the caller will be able to
 * observe all the changes remount did to the superblock.
 */
static inline void sb_end_ro_state_change(struct super_block *sb)
{
	/*
	 * This barrier provides release semantics that pairs with
	 * the smp_rmb() acquire semantics in mnt_is_readonly().
	 * This barrier pair ensure that when mnt_is_readonly() sees
	 * 0 for sb->s_readonly_remount, it will also see all the
	 * preceding flag changes that were made during the RO state
	 * change.
	 */
	smp_wmb();
	WRITE_ONCE(sb->s_readonly_remount, 0);
}

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
struct file *file_close_fd_locked(struct files_struct *files, unsigned fd);

int do_ftruncate(struct file *file, loff_t length, int small);
int do_sys_ftruncate(unsigned int fd, loff_t length, int small);
int chmod_common(const struct path *path, umode_t mode);
int do_fchownat(int dfd, const char __user *filename, uid_t user, gid_t group,
		int flag);
int chown_common(const struct path *path, uid_t user, gid_t group);
extern int vfs_open(const struct path *, struct file *);

/*
 * inode.c
 */
extern long prune_icache_sb(struct super_block *sb, struct shrink_control *sc);
int dentry_needs_remove_privs(struct mnt_idmap *, struct dentry *dentry);
bool in_group_or_capable(struct mnt_idmap *idmap,
			 const struct inode *inode, vfsgid_t vfsgid);

/*
 * fs-writeback.c
 */
extern long get_nr_dirty_inodes(void);

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
extern void shrink_dcache_for_umount(struct super_block *);
extern struct dentry *__d_lookup(const struct dentry *, const struct qstr *);
extern struct dentry *__d_lookup_rcu(const struct dentry *parent,
				const struct qstr *name, unsigned *seq);
extern void d_genocide(struct dentry *);

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
int open_namespace(struct ns_common *ns);

/*
 * fs/stat.c:
 */

int do_statx(int dfd, struct filename *filename, unsigned int flags,
	     unsigned int mask, struct statx __user *buffer);
int do_statx_fd(int fd, unsigned int flags, unsigned int mask,
		struct statx __user *buffer);

/*
 * fs/splice.c:
 */
ssize_t splice_file_to_pipe(struct file *in,
			    struct pipe_inode_info *opipe,
			    loff_t *offset,
			    size_t len, unsigned int flags);

/*
 * fs/xattr.c:
 */
struct xattr_name {
	char name[XATTR_NAME_MAX + 1];
};

struct kernel_xattr_ctx {
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

ssize_t file_getxattr(struct file *file, struct kernel_xattr_ctx *ctx);
ssize_t filename_getxattr(int dfd, struct filename *filename,
			  unsigned int lookup_flags, struct kernel_xattr_ctx *ctx);
int file_setxattr(struct file *file, struct kernel_xattr_ctx *ctx);
int filename_setxattr(int dfd, struct filename *filename,
		      unsigned int lookup_flags, struct kernel_xattr_ctx *ctx);
int setxattr_copy(const char __user *name, struct kernel_xattr_ctx *ctx);
int import_xattr_name(struct xattr_name *kname, const char __user *name);

int may_write_xattr(struct mnt_idmap *idmap, struct inode *inode);

#ifdef CONFIG_FS_POSIX_ACL
int do_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
	       const char *acl_name, const void *kvalue, size_t size);
ssize_t do_get_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		   const char *acl_name, void *kvalue, size_t size);
#else
static inline int do_set_acl(struct mnt_idmap *idmap,
			     struct dentry *dentry, const char *acl_name,
			     const void *kvalue, size_t size)
{
	return -EOPNOTSUPP;
}
static inline ssize_t do_get_acl(struct mnt_idmap *idmap,
				 struct dentry *dentry, const char *acl_name,
				 void *kvalue, size_t size)
{
	return -EOPNOTSUPP;
}
#endif

ssize_t __kernel_write_iter(struct file *file, struct iov_iter *from, loff_t *pos);

/*
 * fs/attr.c
 */
struct mnt_idmap *alloc_mnt_idmap(struct user_namespace *mnt_userns);
struct mnt_idmap *mnt_idmap_get(struct mnt_idmap *idmap);
void mnt_idmap_put(struct mnt_idmap *idmap);
struct stashed_operations {
	struct dentry *(*stash_dentry)(struct dentry **stashed,
				       struct dentry *dentry);
	void (*put_data)(void *data);
	int (*init_inode)(struct inode *inode, void *data);
};
int path_from_stashed(struct dentry **stashed, struct vfsmount *mnt, void *data,
		      struct path *path);
void stashed_dentry_prune(struct dentry *dentry);
struct dentry *stash_dentry(struct dentry **stashed, struct dentry *dentry);
struct dentry *stashed_dentry_get(struct dentry **stashed);
/**
 * path_mounted - check whether path is mounted
 * @path: path to check
 *
 * Determine whether @path refers to the root of a mount.
 *
 * Return: true if @path is the root of a mount, false if not.
 */
static inline bool path_mounted(const struct path *path)
{
	return path->mnt->mnt_root == path->dentry;
}
void file_f_owner_release(struct file *file);
bool file_seek_cur_needs_f_lock(struct file *file);
int statmount_mnt_idmap(struct mnt_idmap *idmap, struct seq_file *seq, bool uid_map);
struct dentry *find_next_child(struct dentry *parent, struct dentry *prev);
int anon_inode_getattr(struct mnt_idmap *idmap, const struct path *path,
		       struct kstat *stat, u32 request_mask,
		       unsigned int query_flags);
int anon_inode_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		       struct iattr *attr);
void pidfs_get_root(struct path *path);
void nsfs_get_root(struct path *path);
