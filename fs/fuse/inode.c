/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/statfs.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/exportfs.h>
#include <linux/posix_acl.h>
#include <linux/pid_namespace.h>

MODULE_AUTHOR("Miklos Szeredi <miklos@szeredi.hu>");
MODULE_DESCRIPTION("Filesystem in Userspace");
MODULE_LICENSE("GPL");

static struct kmem_cache *fuse_inode_cachep;
struct list_head fuse_conn_list;
DEFINE_MUTEX(fuse_mutex);

static int set_global_limit(const char *val, const struct kernel_param *kp);

unsigned max_user_bgreq;
module_param_call(max_user_bgreq, set_global_limit, param_get_uint,
		  &max_user_bgreq, 0644);
__MODULE_PARM_TYPE(max_user_bgreq, "uint");
MODULE_PARM_DESC(max_user_bgreq,
 "Global limit for the maximum number of backgrounded requests an "
 "unprivileged user can set");

unsigned max_user_congthresh;
module_param_call(max_user_congthresh, set_global_limit, param_get_uint,
		  &max_user_congthresh, 0644);
__MODULE_PARM_TYPE(max_user_congthresh, "uint");
MODULE_PARM_DESC(max_user_congthresh,
 "Global limit for the maximum congestion threshold an "
 "unprivileged user can set");

#define FUSE_SUPER_MAGIC 0x65735546

#define FUSE_DEFAULT_BLKSIZE 512

/** Maximum number of outstanding background requests */
#define FUSE_DEFAULT_MAX_BACKGROUND 12

/** Congestion starts at 75% of maximum */
#define FUSE_DEFAULT_CONGESTION_THRESHOLD (FUSE_DEFAULT_MAX_BACKGROUND * 3 / 4)

#ifdef CONFIG_BLOCK
static struct file_system_type fuseblk_fs_type;
#endif

struct fuse_forget_link *fuse_alloc_forget(void)
{
	return kzalloc(sizeof(struct fuse_forget_link), GFP_KERNEL_ACCOUNT);
}

static struct inode *fuse_alloc_inode(struct super_block *sb)
{
	struct fuse_inode *fi;

	fi = kmem_cache_alloc(fuse_inode_cachep, GFP_KERNEL);
	if (!fi)
		return NULL;

	fi->i_time = 0;
	fi->inval_mask = 0;
	fi->nodeid = 0;
	fi->nlookup = 0;
	fi->attr_version = 0;
	fi->orig_ino = 0;
	fi->state = 0;
	mutex_init(&fi->mutex);
	init_rwsem(&fi->i_mmap_sem);
	spin_lock_init(&fi->lock);
	fi->forget = fuse_alloc_forget();
	if (!fi->forget)
		goto out_free;

	if (IS_ENABLED(CONFIG_FUSE_DAX) && !fuse_dax_inode_alloc(sb, fi))
		goto out_free_forget;

	return &fi->inode;

out_free_forget:
	kfree(fi->forget);
out_free:
	kmem_cache_free(fuse_inode_cachep, fi);
	return NULL;
}

static void fuse_free_inode(struct inode *inode)
{
	struct fuse_inode *fi = get_fuse_inode(inode);

	mutex_destroy(&fi->mutex);
	kfree(fi->forget);
#ifdef CONFIG_FUSE_DAX
	kfree(fi->dax);
#endif
	kmem_cache_free(fuse_inode_cachep, fi);
}

static void fuse_evict_inode(struct inode *inode)
{
	struct fuse_inode *fi = get_fuse_inode(inode);

	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	if (inode->i_sb->s_flags & SB_ACTIVE) {
		struct fuse_conn *fc = get_fuse_conn(inode);

		if (FUSE_IS_DAX(inode))
			fuse_dax_inode_cleanup(inode);
		if (fi->nlookup) {
			fuse_queue_forget(fc, fi->forget, fi->nodeid,
					  fi->nlookup);
			fi->forget = NULL;
		}
	}
	if (S_ISREG(inode->i_mode) && !is_bad_inode(inode)) {
		WARN_ON(!list_empty(&fi->write_files));
		WARN_ON(!list_empty(&fi->queued_writes));
	}
}

static int fuse_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;

	sync_filesystem(sb);
	if (fc->sb_flags & SB_MANDLOCK)
		return -EINVAL;

	return 0;
}

/*
 * ino_t is 32-bits on 32-bit arch. We have to squash the 64-bit value down
 * so that it will fit.
 */
static ino_t fuse_squash_ino(u64 ino64)
{
	ino_t ino = (ino_t) ino64;
	if (sizeof(ino_t) < sizeof(u64))
		ino ^= ino64 >> (sizeof(u64) - sizeof(ino_t)) * 8;
	return ino;
}

void fuse_change_attributes_common(struct inode *inode, struct fuse_attr *attr,
				   u64 attr_valid)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_inode *fi = get_fuse_inode(inode);

	lockdep_assert_held(&fi->lock);

	fi->attr_version = atomic64_inc_return(&fc->attr_version);
	fi->i_time = attr_valid;
	WRITE_ONCE(fi->inval_mask, 0);

	inode->i_ino     = fuse_squash_ino(attr->ino);
	inode->i_mode    = (inode->i_mode & S_IFMT) | (attr->mode & 07777);
	set_nlink(inode, attr->nlink);
	inode->i_uid     = make_kuid(fc->user_ns, attr->uid);
	inode->i_gid     = make_kgid(fc->user_ns, attr->gid);
	inode->i_blocks  = attr->blocks;
	inode->i_atime.tv_sec   = attr->atime;
	inode->i_atime.tv_nsec  = attr->atimensec;
	/* mtime from server may be stale due to local buffered write */
	if (!fc->writeback_cache || !S_ISREG(inode->i_mode)) {
		inode->i_mtime.tv_sec   = attr->mtime;
		inode->i_mtime.tv_nsec  = attr->mtimensec;
		inode->i_ctime.tv_sec   = attr->ctime;
		inode->i_ctime.tv_nsec  = attr->ctimensec;
	}

	if (attr->blksize != 0)
		inode->i_blkbits = ilog2(attr->blksize);
	else
		inode->i_blkbits = inode->i_sb->s_blocksize_bits;

	/*
	 * Don't set the sticky bit in i_mode, unless we want the VFS
	 * to check permissions.  This prevents failures due to the
	 * check in may_delete().
	 */
	fi->orig_i_mode = inode->i_mode;
	if (!fc->default_permissions)
		inode->i_mode &= ~S_ISVTX;

	fi->orig_ino = attr->ino;
}

void fuse_change_attributes(struct inode *inode, struct fuse_attr *attr,
			    u64 attr_valid, u64 attr_version)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_inode *fi = get_fuse_inode(inode);
	bool is_wb = fc->writeback_cache;
	loff_t oldsize;
	struct timespec64 old_mtime;

	spin_lock(&fi->lock);
	if ((attr_version != 0 && fi->attr_version > attr_version) ||
	    test_bit(FUSE_I_SIZE_UNSTABLE, &fi->state)) {
		spin_unlock(&fi->lock);
		return;
	}

	old_mtime = inode->i_mtime;
	fuse_change_attributes_common(inode, attr, attr_valid);

	oldsize = inode->i_size;
	/*
	 * In case of writeback_cache enabled, the cached writes beyond EOF
	 * extend local i_size without keeping userspace server in sync. So,
	 * attr->size coming from server can be stale. We cannot trust it.
	 */
	if (!is_wb || !S_ISREG(inode->i_mode))
		i_size_write(inode, attr->size);
	spin_unlock(&fi->lock);

	if (!is_wb && S_ISREG(inode->i_mode)) {
		bool inval = false;

		if (oldsize != attr->size) {
			truncate_pagecache(inode, attr->size);
			if (!fc->explicit_inval_data)
				inval = true;
		} else if (fc->auto_inval_data) {
			struct timespec64 new_mtime = {
				.tv_sec = attr->mtime,
				.tv_nsec = attr->mtimensec,
			};

			/*
			 * Auto inval mode also checks and invalidates if mtime
			 * has changed.
			 */
			if (!timespec64_equal(&old_mtime, &new_mtime))
				inval = true;
		}

		if (inval)
			invalidate_inode_pages2(inode->i_mapping);
	}
}

static void fuse_init_inode(struct inode *inode, struct fuse_attr *attr)
{
	inode->i_mode = attr->mode & S_IFMT;
	inode->i_size = attr->size;
	inode->i_mtime.tv_sec  = attr->mtime;
	inode->i_mtime.tv_nsec = attr->mtimensec;
	inode->i_ctime.tv_sec  = attr->ctime;
	inode->i_ctime.tv_nsec = attr->ctimensec;
	if (S_ISREG(inode->i_mode)) {
		fuse_init_common(inode);
		fuse_init_file_inode(inode);
	} else if (S_ISDIR(inode->i_mode))
		fuse_init_dir(inode);
	else if (S_ISLNK(inode->i_mode))
		fuse_init_symlink(inode);
	else if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
		 S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
		fuse_init_common(inode);
		init_special_inode(inode, inode->i_mode,
				   new_decode_dev(attr->rdev));
	} else
		BUG();
}

static int fuse_inode_eq(struct inode *inode, void *_nodeidp)
{
	u64 nodeid = *(u64 *) _nodeidp;
	if (get_node_id(inode) == nodeid)
		return 1;
	else
		return 0;
}

static int fuse_inode_set(struct inode *inode, void *_nodeidp)
{
	u64 nodeid = *(u64 *) _nodeidp;
	get_fuse_inode(inode)->nodeid = nodeid;
	return 0;
}

struct inode *fuse_iget(struct super_block *sb, u64 nodeid,
			int generation, struct fuse_attr *attr,
			u64 attr_valid, u64 attr_version)
{
	struct inode *inode;
	struct fuse_inode *fi;
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	/*
	 * Auto mount points get their node id from the submount root, which is
	 * not a unique identifier within this filesystem.
	 *
	 * To avoid conflicts, do not place submount points into the inode hash
	 * table.
	 */
	if (fc->auto_submounts && (attr->flags & FUSE_ATTR_SUBMOUNT) &&
	    S_ISDIR(attr->mode)) {
		inode = new_inode(sb);
		if (!inode)
			return NULL;

		fuse_init_inode(inode, attr);
		get_fuse_inode(inode)->nodeid = nodeid;
		inode->i_flags |= S_AUTOMOUNT;
		goto done;
	}

retry:
	inode = iget5_locked(sb, nodeid, fuse_inode_eq, fuse_inode_set, &nodeid);
	if (!inode)
		return NULL;

	if ((inode->i_state & I_NEW)) {
		inode->i_flags |= S_NOATIME;
		if (!fc->writeback_cache || !S_ISREG(attr->mode))
			inode->i_flags |= S_NOCMTIME;
		inode->i_generation = generation;
		fuse_init_inode(inode, attr);
		unlock_new_inode(inode);
	} else if ((inode->i_mode ^ attr->mode) & S_IFMT) {
		/* Inode has changed type, any I/O on the old should fail */
		make_bad_inode(inode);
		iput(inode);
		goto retry;
	}
done:
	fi = get_fuse_inode(inode);
	spin_lock(&fi->lock);
	fi->nlookup++;
	spin_unlock(&fi->lock);
	fuse_change_attributes(inode, attr, attr_valid, attr_version);

	return inode;
}

struct inode *fuse_ilookup(struct fuse_conn *fc, u64 nodeid,
			   struct fuse_mount **fm)
{
	struct fuse_mount *fm_iter;
	struct inode *inode;

	WARN_ON(!rwsem_is_locked(&fc->killsb));
	list_for_each_entry(fm_iter, &fc->mounts, fc_entry) {
		if (!fm_iter->sb)
			continue;

		inode = ilookup5(fm_iter->sb, nodeid, fuse_inode_eq, &nodeid);
		if (inode) {
			if (fm)
				*fm = fm_iter;
			return inode;
		}
	}

	return NULL;
}

int fuse_reverse_inval_inode(struct fuse_conn *fc, u64 nodeid,
			     loff_t offset, loff_t len)
{
	struct fuse_inode *fi;
	struct inode *inode;
	pgoff_t pg_start;
	pgoff_t pg_end;

	inode = fuse_ilookup(fc, nodeid, NULL);
	if (!inode)
		return -ENOENT;

	fi = get_fuse_inode(inode);
	spin_lock(&fi->lock);
	fi->attr_version = atomic64_inc_return(&fc->attr_version);
	spin_unlock(&fi->lock);

	fuse_invalidate_attr(inode);
	forget_all_cached_acls(inode);
	if (offset >= 0) {
		pg_start = offset >> PAGE_SHIFT;
		if (len <= 0)
			pg_end = -1;
		else
			pg_end = (offset + len - 1) >> PAGE_SHIFT;
		invalidate_inode_pages2_range(inode->i_mapping,
					      pg_start, pg_end);
	}
	iput(inode);
	return 0;
}

bool fuse_lock_inode(struct inode *inode)
{
	bool locked = false;

	if (!get_fuse_conn(inode)->parallel_dirops) {
		mutex_lock(&get_fuse_inode(inode)->mutex);
		locked = true;
	}

	return locked;
}

void fuse_unlock_inode(struct inode *inode, bool locked)
{
	if (locked)
		mutex_unlock(&get_fuse_inode(inode)->mutex);
}

static void fuse_umount_begin(struct super_block *sb)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	if (!fc->no_force_umount)
		fuse_abort_conn(fc);
}

static void fuse_send_destroy(struct fuse_mount *fm)
{
	if (fm->fc->conn_init) {
		FUSE_ARGS(args);

		args.opcode = FUSE_DESTROY;
		args.force = true;
		args.nocreds = true;
		fuse_simple_request(fm, &args);
	}
}

static void fuse_put_super(struct super_block *sb)
{
	struct fuse_mount *fm = get_fuse_mount_super(sb);

	fuse_mount_put(fm);
}

static void convert_fuse_statfs(struct kstatfs *stbuf, struct fuse_kstatfs *attr)
{
	stbuf->f_type    = FUSE_SUPER_MAGIC;
	stbuf->f_bsize   = attr->bsize;
	stbuf->f_frsize  = attr->frsize;
	stbuf->f_blocks  = attr->blocks;
	stbuf->f_bfree   = attr->bfree;
	stbuf->f_bavail  = attr->bavail;
	stbuf->f_files   = attr->files;
	stbuf->f_ffree   = attr->ffree;
	stbuf->f_namelen = attr->namelen;
	/* fsid is left zero */
}

static int fuse_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	FUSE_ARGS(args);
	struct fuse_statfs_out outarg;
	int err;

	if (!fuse_allow_current_process(fm->fc)) {
		buf->f_type = FUSE_SUPER_MAGIC;
		return 0;
	}

	memset(&outarg, 0, sizeof(outarg));
	args.in_numargs = 0;
	args.opcode = FUSE_STATFS;
	args.nodeid = get_node_id(d_inode(dentry));
	args.out_numargs = 1;
	args.out_args[0].size = sizeof(outarg);
	args.out_args[0].value = &outarg;
	err = fuse_simple_request(fm, &args);
	if (!err)
		convert_fuse_statfs(buf, &outarg.st);
	return err;
}

enum {
	OPT_SOURCE,
	OPT_SUBTYPE,
	OPT_FD,
	OPT_ROOTMODE,
	OPT_USER_ID,
	OPT_GROUP_ID,
	OPT_DEFAULT_PERMISSIONS,
	OPT_ALLOW_OTHER,
	OPT_MAX_READ,
	OPT_BLKSIZE,
	OPT_ERR
};

static const struct fs_parameter_spec fuse_fs_parameters[] = {
	fsparam_string	("source",		OPT_SOURCE),
	fsparam_u32	("fd",			OPT_FD),
	fsparam_u32oct	("rootmode",		OPT_ROOTMODE),
	fsparam_u32	("user_id",		OPT_USER_ID),
	fsparam_u32	("group_id",		OPT_GROUP_ID),
	fsparam_flag	("default_permissions",	OPT_DEFAULT_PERMISSIONS),
	fsparam_flag	("allow_other",		OPT_ALLOW_OTHER),
	fsparam_u32	("max_read",		OPT_MAX_READ),
	fsparam_u32	("blksize",		OPT_BLKSIZE),
	fsparam_string	("subtype",		OPT_SUBTYPE),
	{}
};

static int fuse_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct fuse_fs_context *ctx = fc->fs_private;
	int opt;

	if (fc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		/*
		 * Ignore options coming from mount(MS_REMOUNT) for backward
		 * compatibility.
		 */
		if (fc->oldapi)
			return 0;

		return invalfc(fc, "No changes allowed in reconfigure");
	}

	opt = fs_parse(fc, fuse_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case OPT_SOURCE:
		if (fc->source)
			return invalfc(fc, "Multiple sources specified");
		fc->source = param->string;
		param->string = NULL;
		break;

	case OPT_SUBTYPE:
		if (ctx->subtype)
			return invalfc(fc, "Multiple subtypes specified");
		ctx->subtype = param->string;
		param->string = NULL;
		return 0;

	case OPT_FD:
		ctx->fd = result.uint_32;
		ctx->fd_present = true;
		break;

	case OPT_ROOTMODE:
		if (!fuse_valid_type(result.uint_32))
			return invalfc(fc, "Invalid rootmode");
		ctx->rootmode = result.uint_32;
		ctx->rootmode_present = true;
		break;

	case OPT_USER_ID:
		ctx->user_id = make_kuid(fc->user_ns, result.uint_32);
		if (!uid_valid(ctx->user_id))
			return invalfc(fc, "Invalid user_id");
		ctx->user_id_present = true;
		break;

	case OPT_GROUP_ID:
		ctx->group_id = make_kgid(fc->user_ns, result.uint_32);
		if (!gid_valid(ctx->group_id))
			return invalfc(fc, "Invalid group_id");
		ctx->group_id_present = true;
		break;

	case OPT_DEFAULT_PERMISSIONS:
		ctx->default_permissions = true;
		break;

	case OPT_ALLOW_OTHER:
		ctx->allow_other = true;
		break;

	case OPT_MAX_READ:
		ctx->max_read = result.uint_32;
		break;

	case OPT_BLKSIZE:
		if (!ctx->is_bdev)
			return invalfc(fc, "blksize only supported for fuseblk");
		ctx->blksize = result.uint_32;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void fuse_free_fc(struct fs_context *fc)
{
	struct fuse_fs_context *ctx = fc->fs_private;

	if (ctx) {
		kfree(ctx->subtype);
		kfree(ctx);
	}
}

static int fuse_show_options(struct seq_file *m, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	if (fc->legacy_opts_show) {
		seq_printf(m, ",user_id=%u",
			   from_kuid_munged(fc->user_ns, fc->user_id));
		seq_printf(m, ",group_id=%u",
			   from_kgid_munged(fc->user_ns, fc->group_id));
		if (fc->default_permissions)
			seq_puts(m, ",default_permissions");
		if (fc->allow_other)
			seq_puts(m, ",allow_other");
		if (fc->max_read != ~0)
			seq_printf(m, ",max_read=%u", fc->max_read);
		if (sb->s_bdev && sb->s_blocksize != FUSE_DEFAULT_BLKSIZE)
			seq_printf(m, ",blksize=%lu", sb->s_blocksize);
	}
#ifdef CONFIG_FUSE_DAX
	if (fc->dax)
		seq_puts(m, ",dax");
#endif

	return 0;
}

static void fuse_iqueue_init(struct fuse_iqueue *fiq,
			     const struct fuse_iqueue_ops *ops,
			     void *priv)
{
	memset(fiq, 0, sizeof(struct fuse_iqueue));
	spin_lock_init(&fiq->lock);
	init_waitqueue_head(&fiq->waitq);
	INIT_LIST_HEAD(&fiq->pending);
	INIT_LIST_HEAD(&fiq->interrupts);
	fiq->forget_list_tail = &fiq->forget_list_head;
	fiq->connected = 1;
	fiq->ops = ops;
	fiq->priv = priv;
}

static void fuse_pqueue_init(struct fuse_pqueue *fpq)
{
	unsigned int i;

	spin_lock_init(&fpq->lock);
	for (i = 0; i < FUSE_PQ_HASH_SIZE; i++)
		INIT_LIST_HEAD(&fpq->processing[i]);
	INIT_LIST_HEAD(&fpq->io);
	fpq->connected = 1;
}

void fuse_conn_init(struct fuse_conn *fc, struct fuse_mount *fm,
		    struct user_namespace *user_ns,
		    const struct fuse_iqueue_ops *fiq_ops, void *fiq_priv)
{
	memset(fc, 0, sizeof(*fc));
	spin_lock_init(&fc->lock);
	spin_lock_init(&fc->bg_lock);
	init_rwsem(&fc->killsb);
	refcount_set(&fc->count, 1);
	atomic_set(&fc->dev_count, 1);
	init_waitqueue_head(&fc->blocked_waitq);
	fuse_iqueue_init(&fc->iq, fiq_ops, fiq_priv);
	INIT_LIST_HEAD(&fc->bg_queue);
	INIT_LIST_HEAD(&fc->entry);
	INIT_LIST_HEAD(&fc->devices);
	atomic_set(&fc->num_waiting, 0);
	fc->max_background = FUSE_DEFAULT_MAX_BACKGROUND;
	fc->congestion_threshold = FUSE_DEFAULT_CONGESTION_THRESHOLD;
	atomic64_set(&fc->khctr, 0);
	fc->polled_files = RB_ROOT;
	fc->blocked = 0;
	fc->initialized = 0;
	fc->connected = 1;
	atomic64_set(&fc->attr_version, 1);
	get_random_bytes(&fc->scramble_key, sizeof(fc->scramble_key));
	fc->pid_ns = get_pid_ns(task_active_pid_ns(current));
	fc->user_ns = get_user_ns(user_ns);
	fc->max_pages = FUSE_DEFAULT_MAX_PAGES_PER_REQ;

	INIT_LIST_HEAD(&fc->mounts);
	list_add(&fm->fc_entry, &fc->mounts);
	fm->fc = fc;
	refcount_set(&fm->count, 1);
}
EXPORT_SYMBOL_GPL(fuse_conn_init);

void fuse_conn_put(struct fuse_conn *fc)
{
	if (refcount_dec_and_test(&fc->count)) {
		struct fuse_iqueue *fiq = &fc->iq;

		if (IS_ENABLED(CONFIG_FUSE_DAX))
			fuse_dax_conn_free(fc);
		if (fiq->ops->release)
			fiq->ops->release(fiq);
		put_pid_ns(fc->pid_ns);
		put_user_ns(fc->user_ns);
		fc->release(fc);
	}
}
EXPORT_SYMBOL_GPL(fuse_conn_put);

struct fuse_conn *fuse_conn_get(struct fuse_conn *fc)
{
	refcount_inc(&fc->count);
	return fc;
}
EXPORT_SYMBOL_GPL(fuse_conn_get);

void fuse_mount_put(struct fuse_mount *fm)
{
	if (refcount_dec_and_test(&fm->count)) {
		if (fm->fc)
			fuse_conn_put(fm->fc);
		kfree(fm);
	}
}
EXPORT_SYMBOL_GPL(fuse_mount_put);

struct fuse_mount *fuse_mount_get(struct fuse_mount *fm)
{
	refcount_inc(&fm->count);
	return fm;
}
EXPORT_SYMBOL_GPL(fuse_mount_get);

static struct inode *fuse_get_root_inode(struct super_block *sb, unsigned mode)
{
	struct fuse_attr attr;
	memset(&attr, 0, sizeof(attr));

	attr.mode = mode;
	attr.ino = FUSE_ROOT_ID;
	attr.nlink = 1;
	return fuse_iget(sb, 1, 0, &attr, 0, 0);
}

struct fuse_inode_handle {
	u64 nodeid;
	u32 generation;
};

static struct dentry *fuse_get_dentry(struct super_block *sb,
				      struct fuse_inode_handle *handle)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);
	struct inode *inode;
	struct dentry *entry;
	int err = -ESTALE;

	if (handle->nodeid == 0)
		goto out_err;

	inode = ilookup5(sb, handle->nodeid, fuse_inode_eq, &handle->nodeid);
	if (!inode) {
		struct fuse_entry_out outarg;
		const struct qstr name = QSTR_INIT(".", 1);

		if (!fc->export_support)
			goto out_err;

		err = fuse_lookup_name(sb, handle->nodeid, &name, &outarg,
				       &inode);
		if (err && err != -ENOENT)
			goto out_err;
		if (err || !inode) {
			err = -ESTALE;
			goto out_err;
		}
		err = -EIO;
		if (get_node_id(inode) != handle->nodeid)
			goto out_iput;
	}
	err = -ESTALE;
	if (inode->i_generation != handle->generation)
		goto out_iput;

	entry = d_obtain_alias(inode);
	if (!IS_ERR(entry) && get_node_id(inode) != FUSE_ROOT_ID)
		fuse_invalidate_entry_cache(entry);

	return entry;

 out_iput:
	iput(inode);
 out_err:
	return ERR_PTR(err);
}

static int fuse_encode_fh(struct inode *inode, u32 *fh, int *max_len,
			   struct inode *parent)
{
	int len = parent ? 6 : 3;
	u64 nodeid;
	u32 generation;

	if (*max_len < len) {
		*max_len = len;
		return  FILEID_INVALID;
	}

	nodeid = get_fuse_inode(inode)->nodeid;
	generation = inode->i_generation;

	fh[0] = (u32)(nodeid >> 32);
	fh[1] = (u32)(nodeid & 0xffffffff);
	fh[2] = generation;

	if (parent) {
		nodeid = get_fuse_inode(parent)->nodeid;
		generation = parent->i_generation;

		fh[3] = (u32)(nodeid >> 32);
		fh[4] = (u32)(nodeid & 0xffffffff);
		fh[5] = generation;
	}

	*max_len = len;
	return parent ? 0x82 : 0x81;
}

static struct dentry *fuse_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct fuse_inode_handle handle;

	if ((fh_type != 0x81 && fh_type != 0x82) || fh_len < 3)
		return NULL;

	handle.nodeid = (u64) fid->raw[0] << 32;
	handle.nodeid |= (u64) fid->raw[1];
	handle.generation = fid->raw[2];
	return fuse_get_dentry(sb, &handle);
}

static struct dentry *fuse_fh_to_parent(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct fuse_inode_handle parent;

	if (fh_type != 0x82 || fh_len < 6)
		return NULL;

	parent.nodeid = (u64) fid->raw[3] << 32;
	parent.nodeid |= (u64) fid->raw[4];
	parent.generation = fid->raw[5];
	return fuse_get_dentry(sb, &parent);
}

static struct dentry *fuse_get_parent(struct dentry *child)
{
	struct inode *child_inode = d_inode(child);
	struct fuse_conn *fc = get_fuse_conn(child_inode);
	struct inode *inode;
	struct dentry *parent;
	struct fuse_entry_out outarg;
	const struct qstr name = QSTR_INIT("..", 2);
	int err;

	if (!fc->export_support)
		return ERR_PTR(-ESTALE);

	err = fuse_lookup_name(child_inode->i_sb, get_node_id(child_inode),
			       &name, &outarg, &inode);
	if (err) {
		if (err == -ENOENT)
			return ERR_PTR(-ESTALE);
		return ERR_PTR(err);
	}

	parent = d_obtain_alias(inode);
	if (!IS_ERR(parent) && get_node_id(inode) != FUSE_ROOT_ID)
		fuse_invalidate_entry_cache(parent);

	return parent;
}

static const struct export_operations fuse_export_operations = {
	.fh_to_dentry	= fuse_fh_to_dentry,
	.fh_to_parent	= fuse_fh_to_parent,
	.encode_fh	= fuse_encode_fh,
	.get_parent	= fuse_get_parent,
};

static const struct super_operations fuse_super_operations = {
	.alloc_inode    = fuse_alloc_inode,
	.free_inode     = fuse_free_inode,
	.evict_inode	= fuse_evict_inode,
	.write_inode	= fuse_write_inode,
	.drop_inode	= generic_delete_inode,
	.put_super	= fuse_put_super,
	.umount_begin	= fuse_umount_begin,
	.statfs		= fuse_statfs,
	.show_options	= fuse_show_options,
};

static void sanitize_global_limit(unsigned *limit)
{
	/*
	 * The default maximum number of async requests is calculated to consume
	 * 1/2^13 of the total memory, assuming 392 bytes per request.
	 */
	if (*limit == 0)
		*limit = ((totalram_pages() << PAGE_SHIFT) >> 13) / 392;

	if (*limit >= 1 << 16)
		*limit = (1 << 16) - 1;
}

static int set_global_limit(const char *val, const struct kernel_param *kp)
{
	int rv;

	rv = param_set_uint(val, kp);
	if (rv)
		return rv;

	sanitize_global_limit((unsigned *)kp->arg);

	return 0;
}

static void process_init_limits(struct fuse_conn *fc, struct fuse_init_out *arg)
{
	int cap_sys_admin = capable(CAP_SYS_ADMIN);

	if (arg->minor < 13)
		return;

	sanitize_global_limit(&max_user_bgreq);
	sanitize_global_limit(&max_user_congthresh);

	spin_lock(&fc->bg_lock);
	if (arg->max_background) {
		fc->max_background = arg->max_background;

		if (!cap_sys_admin && fc->max_background > max_user_bgreq)
			fc->max_background = max_user_bgreq;
	}
	if (arg->congestion_threshold) {
		fc->congestion_threshold = arg->congestion_threshold;

		if (!cap_sys_admin &&
		    fc->congestion_threshold > max_user_congthresh)
			fc->congestion_threshold = max_user_congthresh;
	}
	spin_unlock(&fc->bg_lock);
}

struct fuse_init_args {
	struct fuse_args args;
	struct fuse_init_in in;
	struct fuse_init_out out;
};

static void process_init_reply(struct fuse_mount *fm, struct fuse_args *args,
			       int error)
{
	struct fuse_conn *fc = fm->fc;
	struct fuse_init_args *ia = container_of(args, typeof(*ia), args);
	struct fuse_init_out *arg = &ia->out;
	bool ok = true;

	if (error || arg->major != FUSE_KERNEL_VERSION)
		ok = false;
	else {
		unsigned long ra_pages;

		process_init_limits(fc, arg);

		if (arg->minor >= 6) {
			ra_pages = arg->max_readahead / PAGE_SIZE;
			if (arg->flags & FUSE_ASYNC_READ)
				fc->async_read = 1;
			if (!(arg->flags & FUSE_POSIX_LOCKS))
				fc->no_lock = 1;
			if (arg->minor >= 17) {
				if (!(arg->flags & FUSE_FLOCK_LOCKS))
					fc->no_flock = 1;
			} else {
				if (!(arg->flags & FUSE_POSIX_LOCKS))
					fc->no_flock = 1;
			}
			if (arg->flags & FUSE_ATOMIC_O_TRUNC)
				fc->atomic_o_trunc = 1;
			if (arg->minor >= 9) {
				/* LOOKUP has dependency on proto version */
				if (arg->flags & FUSE_EXPORT_SUPPORT)
					fc->export_support = 1;
			}
			if (arg->flags & FUSE_BIG_WRITES)
				fc->big_writes = 1;
			if (arg->flags & FUSE_DONT_MASK)
				fc->dont_mask = 1;
			if (arg->flags & FUSE_AUTO_INVAL_DATA)
				fc->auto_inval_data = 1;
			else if (arg->flags & FUSE_EXPLICIT_INVAL_DATA)
				fc->explicit_inval_data = 1;
			if (arg->flags & FUSE_DO_READDIRPLUS) {
				fc->do_readdirplus = 1;
				if (arg->flags & FUSE_READDIRPLUS_AUTO)
					fc->readdirplus_auto = 1;
			}
			if (arg->flags & FUSE_ASYNC_DIO)
				fc->async_dio = 1;
			if (arg->flags & FUSE_WRITEBACK_CACHE)
				fc->writeback_cache = 1;
			if (arg->flags & FUSE_PARALLEL_DIROPS)
				fc->parallel_dirops = 1;
			if (arg->flags & FUSE_HANDLE_KILLPRIV)
				fc->handle_killpriv = 1;
			if (arg->time_gran && arg->time_gran <= 1000000000)
				fm->sb->s_time_gran = arg->time_gran;
			if ((arg->flags & FUSE_POSIX_ACL)) {
				fc->default_permissions = 1;
				fc->posix_acl = 1;
				fm->sb->s_xattr = fuse_acl_xattr_handlers;
			}
			if (arg->flags & FUSE_CACHE_SYMLINKS)
				fc->cache_symlinks = 1;
			if (arg->flags & FUSE_ABORT_ERROR)
				fc->abort_err = 1;
			if (arg->flags & FUSE_MAX_PAGES) {
				fc->max_pages =
					min_t(unsigned int, FUSE_MAX_MAX_PAGES,
					max_t(unsigned int, arg->max_pages, 1));
			}
			if (IS_ENABLED(CONFIG_FUSE_DAX) &&
			    arg->flags & FUSE_MAP_ALIGNMENT &&
			    !fuse_dax_check_alignment(fc, arg->map_alignment)) {
				ok = false;
			}
		} else {
			ra_pages = fc->max_read / PAGE_SIZE;
			fc->no_lock = 1;
			fc->no_flock = 1;
		}

		fm->sb->s_bdi->ra_pages =
				min(fm->sb->s_bdi->ra_pages, ra_pages);
		fc->minor = arg->minor;
		fc->max_write = arg->minor < 5 ? 4096 : arg->max_write;
		fc->max_write = max_t(unsigned, 4096, fc->max_write);
		fc->conn_init = 1;
	}
	kfree(ia);

	if (!ok) {
		fc->conn_init = 0;
		fc->conn_error = 1;
	}

	fuse_set_initialized(fc);
	wake_up_all(&fc->blocked_waitq);
}

void fuse_send_init(struct fuse_mount *fm)
{
	struct fuse_init_args *ia;

	ia = kzalloc(sizeof(*ia), GFP_KERNEL | __GFP_NOFAIL);

	ia->in.major = FUSE_KERNEL_VERSION;
	ia->in.minor = FUSE_KERNEL_MINOR_VERSION;
	ia->in.max_readahead = fm->sb->s_bdi->ra_pages * PAGE_SIZE;
	ia->in.flags |=
		FUSE_ASYNC_READ | FUSE_POSIX_LOCKS | FUSE_ATOMIC_O_TRUNC |
		FUSE_EXPORT_SUPPORT | FUSE_BIG_WRITES | FUSE_DONT_MASK |
		FUSE_SPLICE_WRITE | FUSE_SPLICE_MOVE | FUSE_SPLICE_READ |
		FUSE_FLOCK_LOCKS | FUSE_HAS_IOCTL_DIR | FUSE_AUTO_INVAL_DATA |
		FUSE_DO_READDIRPLUS | FUSE_READDIRPLUS_AUTO | FUSE_ASYNC_DIO |
		FUSE_WRITEBACK_CACHE | FUSE_NO_OPEN_SUPPORT |
		FUSE_PARALLEL_DIROPS | FUSE_HANDLE_KILLPRIV | FUSE_POSIX_ACL |
		FUSE_ABORT_ERROR | FUSE_MAX_PAGES | FUSE_CACHE_SYMLINKS |
		FUSE_NO_OPENDIR_SUPPORT | FUSE_EXPLICIT_INVAL_DATA;
#ifdef CONFIG_FUSE_DAX
	if (fm->fc->dax)
		ia->in.flags |= FUSE_MAP_ALIGNMENT;
#endif
	if (fm->fc->auto_submounts)
		ia->in.flags |= FUSE_SUBMOUNTS;

	ia->args.opcode = FUSE_INIT;
	ia->args.in_numargs = 1;
	ia->args.in_args[0].size = sizeof(ia->in);
	ia->args.in_args[0].value = &ia->in;
	ia->args.out_numargs = 1;
	/* Variable length argument used for backward compatibility
	   with interface version < 7.5.  Rest of init_out is zeroed
	   by do_get_request(), so a short reply is not a problem */
	ia->args.out_argvar = true;
	ia->args.out_args[0].size = sizeof(ia->out);
	ia->args.out_args[0].value = &ia->out;
	ia->args.force = true;
	ia->args.nocreds = true;
	ia->args.end = process_init_reply;

	if (fuse_simple_background(fm, &ia->args, GFP_KERNEL) != 0)
		process_init_reply(fm, &ia->args, -ENOTCONN);
}
EXPORT_SYMBOL_GPL(fuse_send_init);

void fuse_free_conn(struct fuse_conn *fc)
{
	WARN_ON(!list_empty(&fc->devices));
	kfree_rcu(fc, rcu);
}
EXPORT_SYMBOL_GPL(fuse_free_conn);

static int fuse_bdi_init(struct fuse_conn *fc, struct super_block *sb)
{
	int err;
	char *suffix = "";

	if (sb->s_bdev) {
		suffix = "-fuseblk";
		/*
		 * sb->s_bdi points to blkdev's bdi however we want to redirect
		 * it to our private bdi...
		 */
		bdi_put(sb->s_bdi);
		sb->s_bdi = &noop_backing_dev_info;
	}
	err = super_setup_bdi_name(sb, "%u:%u%s", MAJOR(fc->dev),
				   MINOR(fc->dev), suffix);
	if (err)
		return err;

	/* fuse does it's own writeback accounting */
	sb->s_bdi->capabilities &= ~BDI_CAP_WRITEBACK_ACCT;
	sb->s_bdi->capabilities |= BDI_CAP_STRICTLIMIT;

	/*
	 * For a single fuse filesystem use max 1% of dirty +
	 * writeback threshold.
	 *
	 * This gives about 1M of write buffer for memory maps on a
	 * machine with 1G and 10% dirty_ratio, which should be more
	 * than enough.
	 *
	 * Privileged users can raise it by writing to
	 *
	 *    /sys/class/bdi/<bdi>/max_ratio
	 */
	bdi_set_max_ratio(sb->s_bdi, 1);

	return 0;
}

struct fuse_dev *fuse_dev_alloc(void)
{
	struct fuse_dev *fud;
	struct list_head *pq;

	fud = kzalloc(sizeof(struct fuse_dev), GFP_KERNEL);
	if (!fud)
		return NULL;

	pq = kcalloc(FUSE_PQ_HASH_SIZE, sizeof(struct list_head), GFP_KERNEL);
	if (!pq) {
		kfree(fud);
		return NULL;
	}

	fud->pq.processing = pq;
	fuse_pqueue_init(&fud->pq);

	return fud;
}
EXPORT_SYMBOL_GPL(fuse_dev_alloc);

void fuse_dev_install(struct fuse_dev *fud, struct fuse_conn *fc)
{
	fud->fc = fuse_conn_get(fc);
	spin_lock(&fc->lock);
	list_add_tail(&fud->entry, &fc->devices);
	spin_unlock(&fc->lock);
}
EXPORT_SYMBOL_GPL(fuse_dev_install);

struct fuse_dev *fuse_dev_alloc_install(struct fuse_conn *fc)
{
	struct fuse_dev *fud;

	fud = fuse_dev_alloc();
	if (!fud)
		return NULL;

	fuse_dev_install(fud, fc);
	return fud;
}
EXPORT_SYMBOL_GPL(fuse_dev_alloc_install);

void fuse_dev_free(struct fuse_dev *fud)
{
	struct fuse_conn *fc = fud->fc;

	if (fc) {
		spin_lock(&fc->lock);
		list_del(&fud->entry);
		spin_unlock(&fc->lock);

		fuse_conn_put(fc);
	}
	kfree(fud->pq.processing);
	kfree(fud);
}
EXPORT_SYMBOL_GPL(fuse_dev_free);

static void fuse_fill_attr_from_inode(struct fuse_attr *attr,
				      const struct fuse_inode *fi)
{
	*attr = (struct fuse_attr){
		.ino		= fi->inode.i_ino,
		.size		= fi->inode.i_size,
		.blocks		= fi->inode.i_blocks,
		.atime		= fi->inode.i_atime.tv_sec,
		.mtime		= fi->inode.i_mtime.tv_sec,
		.ctime		= fi->inode.i_ctime.tv_sec,
		.atimensec	= fi->inode.i_atime.tv_nsec,
		.mtimensec	= fi->inode.i_mtime.tv_nsec,
		.ctimensec	= fi->inode.i_ctime.tv_nsec,
		.mode		= fi->inode.i_mode,
		.nlink		= fi->inode.i_nlink,
		.uid		= fi->inode.i_uid.val,
		.gid		= fi->inode.i_gid.val,
		.rdev		= fi->inode.i_rdev,
		.blksize	= 1u << fi->inode.i_blkbits,
	};
}

static void fuse_sb_defaults(struct super_block *sb)
{
	sb->s_magic = FUSE_SUPER_MAGIC;
	sb->s_op = &fuse_super_operations;
	sb->s_xattr = fuse_xattr_handlers;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_export_op = &fuse_export_operations;
	sb->s_iflags |= SB_I_IMA_UNVERIFIABLE_SIGNATURE;
	if (sb->s_user_ns != &init_user_ns)
		sb->s_iflags |= SB_I_UNTRUSTED_MOUNTER;
	sb->s_flags &= ~(SB_NOSEC | SB_I_VERSION);

	/*
	 * If we are not in the initial user namespace posix
	 * acls must be translated.
	 */
	if (sb->s_user_ns != &init_user_ns)
		sb->s_xattr = fuse_no_acl_xattr_handlers;
}

int fuse_fill_super_submount(struct super_block *sb,
			     struct fuse_inode *parent_fi)
{
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	struct super_block *parent_sb = parent_fi->inode.i_sb;
	struct fuse_attr root_attr;
	struct inode *root;

	fuse_sb_defaults(sb);
	fm->sb = sb;

	WARN_ON(sb->s_bdi != &noop_backing_dev_info);
	sb->s_bdi = bdi_get(parent_sb->s_bdi);

	sb->s_xattr = parent_sb->s_xattr;
	sb->s_time_gran = parent_sb->s_time_gran;
	sb->s_blocksize = parent_sb->s_blocksize;
	sb->s_blocksize_bits = parent_sb->s_blocksize_bits;
	sb->s_subtype = kstrdup(parent_sb->s_subtype, GFP_KERNEL);
	if (parent_sb->s_subtype && !sb->s_subtype)
		return -ENOMEM;

	fuse_fill_attr_from_inode(&root_attr, parent_fi);
	root = fuse_iget(sb, parent_fi->nodeid, 0, &root_attr, 0, 0);
	/*
	 * This inode is just a duplicate, so it is not looked up and
	 * its nlookup should not be incremented.  fuse_iget() does
	 * that, though, so undo it here.
	 */
	get_fuse_inode(root)->nlookup--;
	sb->s_d_op = &fuse_dentry_operations;
	sb->s_root = d_make_root(root);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

int fuse_fill_super_common(struct super_block *sb, struct fuse_fs_context *ctx)
{
	struct fuse_dev *fud = NULL;
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	struct fuse_conn *fc = fm->fc;
	struct inode *root;
	struct dentry *root_dentry;
	int err;

	err = -EINVAL;
	if (sb->s_flags & SB_MANDLOCK)
		goto err;

	fuse_sb_defaults(sb);

	if (ctx->is_bdev) {
#ifdef CONFIG_BLOCK
		err = -EINVAL;
		if (!sb_set_blocksize(sb, ctx->blksize))
			goto err;
#endif
	} else {
		sb->s_blocksize = PAGE_SIZE;
		sb->s_blocksize_bits = PAGE_SHIFT;
	}

	sb->s_subtype = ctx->subtype;
	ctx->subtype = NULL;
	if (IS_ENABLED(CONFIG_FUSE_DAX)) {
		err = fuse_dax_conn_alloc(fc, ctx->dax_dev);
		if (err)
			goto err;
	}

	if (ctx->fudptr) {
		err = -ENOMEM;
		fud = fuse_dev_alloc_install(fc);
		if (!fud)
			goto err_free_dax;
	}

	fc->dev = sb->s_dev;
	fm->sb = sb;
	err = fuse_bdi_init(fc, sb);
	if (err)
		goto err_dev_free;

	/* Handle umasking inside the fuse code */
	if (sb->s_flags & SB_POSIXACL)
		fc->dont_mask = 1;
	sb->s_flags |= SB_POSIXACL;

	fc->default_permissions = ctx->default_permissions;
	fc->allow_other = ctx->allow_other;
	fc->user_id = ctx->user_id;
	fc->group_id = ctx->group_id;
	fc->legacy_opts_show = ctx->legacy_opts_show;
	fc->max_read = max_t(unsigned int, 4096, ctx->max_read);
	fc->destroy = ctx->destroy;
	fc->no_control = ctx->no_control;
	fc->no_force_umount = ctx->no_force_umount;

	err = -ENOMEM;
	root = fuse_get_root_inode(sb, ctx->rootmode);
	sb->s_d_op = &fuse_root_dentry_operations;
	root_dentry = d_make_root(root);
	if (!root_dentry)
		goto err_dev_free;
	/* Root dentry doesn't have .d_revalidate */
	sb->s_d_op = &fuse_dentry_operations;

	mutex_lock(&fuse_mutex);
	err = -EINVAL;
	if (ctx->fudptr && *ctx->fudptr)
		goto err_unlock;

	err = fuse_ctl_add_conn(fc);
	if (err)
		goto err_unlock;

	list_add_tail(&fc->entry, &fuse_conn_list);
	sb->s_root = root_dentry;
	if (ctx->fudptr)
		*ctx->fudptr = fud;
	mutex_unlock(&fuse_mutex);
	return 0;

 err_unlock:
	mutex_unlock(&fuse_mutex);
	dput(root_dentry);
 err_dev_free:
	if (fud)
		fuse_dev_free(fud);
 err_free_dax:
	if (IS_ENABLED(CONFIG_FUSE_DAX))
		fuse_dax_conn_free(fc);
 err:
	return err;
}
EXPORT_SYMBOL_GPL(fuse_fill_super_common);

static int fuse_fill_super(struct super_block *sb, struct fs_context *fsc)
{
	struct fuse_fs_context *ctx = fsc->fs_private;
	struct file *file;
	int err;
	struct fuse_conn *fc;
	struct fuse_mount *fm;

	err = -EINVAL;
	file = fget(ctx->fd);
	if (!file)
		goto err;

	/*
	 * Require mount to happen from the same user namespace which
	 * opened /dev/fuse to prevent potential attacks.
	 */
	if ((file->f_op != &fuse_dev_operations) ||
	    (file->f_cred->user_ns != sb->s_user_ns))
		goto err_fput;
	ctx->fudptr = &file->private_data;

	fc = kmalloc(sizeof(*fc), GFP_KERNEL);
	err = -ENOMEM;
	if (!fc)
		goto err_fput;

	fm = kzalloc(sizeof(*fm), GFP_KERNEL);
	if (!fm) {
		kfree(fc);
		goto err_fput;
	}

	fuse_conn_init(fc, fm, sb->s_user_ns, &fuse_dev_fiq_ops, NULL);
	fc->release = fuse_free_conn;

	sb->s_fs_info = fm;

	err = fuse_fill_super_common(sb, ctx);
	if (err)
		goto err_put_conn;
	/*
	 * atomic_dec_and_test() in fput() provides the necessary
	 * memory barrier for file->private_data to be visible on all
	 * CPUs after this
	 */
	fput(file);
	fuse_send_init(get_fuse_mount_super(sb));
	return 0;

 err_put_conn:
	fuse_mount_put(fm);
	sb->s_fs_info = NULL;
 err_fput:
	fput(file);
 err:
	return err;
}

static int fuse_get_tree(struct fs_context *fc)
{
	struct fuse_fs_context *ctx = fc->fs_private;

	if (!ctx->fd_present || !ctx->rootmode_present ||
	    !ctx->user_id_present || !ctx->group_id_present)
		return -EINVAL;

#ifdef CONFIG_BLOCK
	if (ctx->is_bdev)
		return get_tree_bdev(fc, fuse_fill_super);
#endif

	return get_tree_nodev(fc, fuse_fill_super);
}

static const struct fs_context_operations fuse_context_ops = {
	.free		= fuse_free_fc,
	.parse_param	= fuse_parse_param,
	.reconfigure	= fuse_reconfigure,
	.get_tree	= fuse_get_tree,
};

/*
 * Set up the filesystem mount context.
 */
static int fuse_init_fs_context(struct fs_context *fc)
{
	struct fuse_fs_context *ctx;

	ctx = kzalloc(sizeof(struct fuse_fs_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->max_read = ~0;
	ctx->blksize = FUSE_DEFAULT_BLKSIZE;
	ctx->legacy_opts_show = true;

#ifdef CONFIG_BLOCK
	if (fc->fs_type == &fuseblk_fs_type) {
		ctx->is_bdev = true;
		ctx->destroy = true;
	}
#endif

	fc->fs_private = ctx;
	fc->ops = &fuse_context_ops;
	return 0;
}

bool fuse_mount_remove(struct fuse_mount *fm)
{
	struct fuse_conn *fc = fm->fc;
	bool last = false;

	down_write(&fc->killsb);
	list_del_init(&fm->fc_entry);
	if (list_empty(&fc->mounts))
		last = true;
	up_write(&fc->killsb);

	return last;
}
EXPORT_SYMBOL_GPL(fuse_mount_remove);

void fuse_conn_destroy(struct fuse_mount *fm)
{
	struct fuse_conn *fc = fm->fc;

	if (fc->destroy)
		fuse_send_destroy(fm);

	fuse_abort_conn(fc);
	fuse_wait_aborted(fc);

	if (!list_empty(&fc->entry)) {
		mutex_lock(&fuse_mutex);
		list_del(&fc->entry);
		fuse_ctl_remove_conn(fc);
		mutex_unlock(&fuse_mutex);
	}
}
EXPORT_SYMBOL_GPL(fuse_conn_destroy);

static void fuse_kill_sb_anon(struct super_block *sb)
{
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	bool last;

	if (fm) {
		last = fuse_mount_remove(fm);
		if (last)
			fuse_conn_destroy(fm);
	}
	kill_anon_super(sb);
}

static struct file_system_type fuse_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "fuse",
	.fs_flags	= FS_HAS_SUBTYPE | FS_USERNS_MOUNT,
	.init_fs_context = fuse_init_fs_context,
	.parameters	= fuse_fs_parameters,
	.kill_sb	= fuse_kill_sb_anon,
};
MODULE_ALIAS_FS("fuse");

#ifdef CONFIG_BLOCK
static void fuse_kill_sb_blk(struct super_block *sb)
{
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	bool last;

	if (fm) {
		last = fuse_mount_remove(fm);
		if (last)
			fuse_conn_destroy(fm);
	}
	kill_block_super(sb);
}

static struct file_system_type fuseblk_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "fuseblk",
	.init_fs_context = fuse_init_fs_context,
	.parameters	= fuse_fs_parameters,
	.kill_sb	= fuse_kill_sb_blk,
	.fs_flags	= FS_REQUIRES_DEV | FS_HAS_SUBTYPE,
};
MODULE_ALIAS_FS("fuseblk");

static inline int register_fuseblk(void)
{
	return register_filesystem(&fuseblk_fs_type);
}

static inline void unregister_fuseblk(void)
{
	unregister_filesystem(&fuseblk_fs_type);
}
#else
static inline int register_fuseblk(void)
{
	return 0;
}

static inline void unregister_fuseblk(void)
{
}
#endif

static void fuse_inode_init_once(void *foo)
{
	struct inode *inode = foo;

	inode_init_once(inode);
}

static int __init fuse_fs_init(void)
{
	int err;

	fuse_inode_cachep = kmem_cache_create("fuse_inode",
			sizeof(struct fuse_inode), 0,
			SLAB_HWCACHE_ALIGN|SLAB_ACCOUNT|SLAB_RECLAIM_ACCOUNT,
			fuse_inode_init_once);
	err = -ENOMEM;
	if (!fuse_inode_cachep)
		goto out;

	err = register_fuseblk();
	if (err)
		goto out2;

	err = register_filesystem(&fuse_fs_type);
	if (err)
		goto out3;

	return 0;

 out3:
	unregister_fuseblk();
 out2:
	kmem_cache_destroy(fuse_inode_cachep);
 out:
	return err;
}

static void fuse_fs_cleanup(void)
{
	unregister_filesystem(&fuse_fs_type);
	unregister_fuseblk();

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(fuse_inode_cachep);
}

static struct kobject *fuse_kobj;

static int fuse_sysfs_init(void)
{
	int err;

	fuse_kobj = kobject_create_and_add("fuse", fs_kobj);
	if (!fuse_kobj) {
		err = -ENOMEM;
		goto out_err;
	}

	err = sysfs_create_mount_point(fuse_kobj, "connections");
	if (err)
		goto out_fuse_unregister;

	return 0;

 out_fuse_unregister:
	kobject_put(fuse_kobj);
 out_err:
	return err;
}

static void fuse_sysfs_cleanup(void)
{
	sysfs_remove_mount_point(fuse_kobj, "connections");
	kobject_put(fuse_kobj);
}

static int __init fuse_init(void)
{
	int res;

	pr_info("init (API version %i.%i)\n",
		FUSE_KERNEL_VERSION, FUSE_KERNEL_MINOR_VERSION);

	INIT_LIST_HEAD(&fuse_conn_list);
	res = fuse_fs_init();
	if (res)
		goto err;

	res = fuse_dev_init();
	if (res)
		goto err_fs_cleanup;

	res = fuse_sysfs_init();
	if (res)
		goto err_dev_cleanup;

	res = fuse_ctl_init();
	if (res)
		goto err_sysfs_cleanup;

	sanitize_global_limit(&max_user_bgreq);
	sanitize_global_limit(&max_user_congthresh);

	return 0;

 err_sysfs_cleanup:
	fuse_sysfs_cleanup();
 err_dev_cleanup:
	fuse_dev_cleanup();
 err_fs_cleanup:
	fuse_fs_cleanup();
 err:
	return res;
}

static void __exit fuse_exit(void)
{
	pr_debug("exit\n");

	fuse_ctl_cleanup();
	fuse_sysfs_cleanup();
	fuse_fs_cleanup();
	fuse_dev_cleanup();
}

module_init(fuse_init);
module_exit(fuse_exit);
