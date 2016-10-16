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
#include <linux/parser.h>
#include <linux/statfs.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/exportfs.h>
#include <linux/posix_acl.h>

MODULE_AUTHOR("Miklos Szeredi <miklos@szeredi.hu>");
MODULE_DESCRIPTION("Filesystem in Userspace");
MODULE_LICENSE("GPL");

static struct kmem_cache *fuse_inode_cachep;
struct list_head fuse_conn_list;
DEFINE_MUTEX(fuse_mutex);

static int set_global_limit(const char *val, struct kernel_param *kp);

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

struct fuse_mount_data {
	int fd;
	unsigned rootmode;
	kuid_t user_id;
	kgid_t group_id;
	unsigned fd_present:1;
	unsigned rootmode_present:1;
	unsigned user_id_present:1;
	unsigned group_id_present:1;
	unsigned default_permissions:1;
	unsigned allow_other:1;
	unsigned max_read;
	unsigned blksize;
};

struct fuse_forget_link *fuse_alloc_forget(void)
{
	return kzalloc(sizeof(struct fuse_forget_link), GFP_KERNEL);
}

static struct inode *fuse_alloc_inode(struct super_block *sb)
{
	struct inode *inode;
	struct fuse_inode *fi;

	inode = kmem_cache_alloc(fuse_inode_cachep, GFP_KERNEL);
	if (!inode)
		return NULL;

	fi = get_fuse_inode(inode);
	fi->i_time = 0;
	fi->nodeid = 0;
	fi->nlookup = 0;
	fi->attr_version = 0;
	fi->writectr = 0;
	fi->orig_ino = 0;
	fi->state = 0;
	INIT_LIST_HEAD(&fi->write_files);
	INIT_LIST_HEAD(&fi->queued_writes);
	INIT_LIST_HEAD(&fi->writepages);
	init_waitqueue_head(&fi->page_waitq);
	mutex_init(&fi->mutex);
	fi->forget = fuse_alloc_forget();
	if (!fi->forget) {
		kmem_cache_free(fuse_inode_cachep, inode);
		return NULL;
	}

	return inode;
}

static void fuse_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(fuse_inode_cachep, inode);
}

static void fuse_destroy_inode(struct inode *inode)
{
	struct fuse_inode *fi = get_fuse_inode(inode);
	BUG_ON(!list_empty(&fi->write_files));
	BUG_ON(!list_empty(&fi->queued_writes));
	mutex_destroy(&fi->mutex);
	kfree(fi->forget);
	call_rcu(&inode->i_rcu, fuse_i_callback);
}

static void fuse_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	if (inode->i_sb->s_flags & MS_ACTIVE) {
		struct fuse_conn *fc = get_fuse_conn(inode);
		struct fuse_inode *fi = get_fuse_inode(inode);
		fuse_queue_forget(fc, fi->forget, fi->nodeid, fi->nlookup);
		fi->forget = NULL;
	}
}

static int fuse_remount_fs(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);
	if (*flags & MS_MANDLOCK)
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

	fi->attr_version = ++fc->attr_version;
	fi->i_time = attr_valid;

	inode->i_ino     = fuse_squash_ino(attr->ino);
	inode->i_mode    = (inode->i_mode & S_IFMT) | (attr->mode & 07777);
	set_nlink(inode, attr->nlink);
	inode->i_uid     = make_kuid(&init_user_ns, attr->uid);
	inode->i_gid     = make_kgid(&init_user_ns, attr->gid);
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
	struct timespec old_mtime;

	spin_lock(&fc->lock);
	if ((attr_version != 0 && fi->attr_version > attr_version) ||
	    test_bit(FUSE_I_SIZE_UNSTABLE, &fi->state)) {
		spin_unlock(&fc->lock);
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
	spin_unlock(&fc->lock);

	if (!is_wb && S_ISREG(inode->i_mode)) {
		bool inval = false;

		if (oldsize != attr->size) {
			truncate_pagecache(inode, attr->size);
			inval = true;
		} else if (fc->auto_inval_data) {
			struct timespec new_mtime = {
				.tv_sec = attr->mtime,
				.tv_nsec = attr->mtimensec,
			};

			/*
			 * Auto inval mode also checks and invalidates if mtime
			 * has changed.
			 */
			if (!timespec_equal(&old_mtime, &new_mtime))
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

int fuse_inode_eq(struct inode *inode, void *_nodeidp)
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

	fi = get_fuse_inode(inode);
	spin_lock(&fc->lock);
	fi->nlookup++;
	spin_unlock(&fc->lock);
	fuse_change_attributes(inode, attr, attr_valid, attr_version);

	return inode;
}

int fuse_reverse_inval_inode(struct super_block *sb, u64 nodeid,
			     loff_t offset, loff_t len)
{
	struct inode *inode;
	pgoff_t pg_start;
	pgoff_t pg_end;

	inode = ilookup5(sb, nodeid, fuse_inode_eq, &nodeid);
	if (!inode)
		return -ENOENT;

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

void fuse_lock_inode(struct inode *inode)
{
	if (!get_fuse_conn(inode)->parallel_dirops)
		mutex_lock(&get_fuse_inode(inode)->mutex);
}

void fuse_unlock_inode(struct inode *inode)
{
	if (!get_fuse_conn(inode)->parallel_dirops)
		mutex_unlock(&get_fuse_inode(inode)->mutex);
}

static void fuse_umount_begin(struct super_block *sb)
{
	fuse_abort_conn(get_fuse_conn_super(sb));
}

static void fuse_send_destroy(struct fuse_conn *fc)
{
	struct fuse_req *req = fc->destroy_req;
	if (req && fc->conn_init) {
		fc->destroy_req = NULL;
		req->in.h.opcode = FUSE_DESTROY;
		__set_bit(FR_FORCE, &req->flags);
		__clear_bit(FR_BACKGROUND, &req->flags);
		fuse_request_send(fc, req);
		fuse_put_request(fc, req);
	}
}

static void fuse_bdi_destroy(struct fuse_conn *fc)
{
	if (fc->bdi_initialized)
		bdi_destroy(&fc->bdi);
}

static void fuse_put_super(struct super_block *sb)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	fuse_send_destroy(fc);

	fuse_abort_conn(fc);
	mutex_lock(&fuse_mutex);
	list_del(&fc->entry);
	fuse_ctl_remove_conn(fc);
	mutex_unlock(&fuse_mutex);
	fuse_bdi_destroy(fc);

	fuse_conn_put(fc);
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
	struct fuse_conn *fc = get_fuse_conn_super(sb);
	FUSE_ARGS(args);
	struct fuse_statfs_out outarg;
	int err;

	if (!fuse_allow_current_process(fc)) {
		buf->f_type = FUSE_SUPER_MAGIC;
		return 0;
	}

	memset(&outarg, 0, sizeof(outarg));
	args.in.numargs = 0;
	args.in.h.opcode = FUSE_STATFS;
	args.in.h.nodeid = get_node_id(d_inode(dentry));
	args.out.numargs = 1;
	args.out.args[0].size = sizeof(outarg);
	args.out.args[0].value = &outarg;
	err = fuse_simple_request(fc, &args);
	if (!err)
		convert_fuse_statfs(buf, &outarg.st);
	return err;
}

enum {
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

static const match_table_t tokens = {
	{OPT_FD,			"fd=%u"},
	{OPT_ROOTMODE,			"rootmode=%o"},
	{OPT_USER_ID,			"user_id=%u"},
	{OPT_GROUP_ID,			"group_id=%u"},
	{OPT_DEFAULT_PERMISSIONS,	"default_permissions"},
	{OPT_ALLOW_OTHER,		"allow_other"},
	{OPT_MAX_READ,			"max_read=%u"},
	{OPT_BLKSIZE,			"blksize=%u"},
	{OPT_ERR,			NULL}
};

static int fuse_match_uint(substring_t *s, unsigned int *res)
{
	int err = -ENOMEM;
	char *buf = match_strdup(s);
	if (buf) {
		err = kstrtouint(buf, 10, res);
		kfree(buf);
	}
	return err;
}

static int parse_fuse_opt(char *opt, struct fuse_mount_data *d, int is_bdev)
{
	char *p;
	memset(d, 0, sizeof(struct fuse_mount_data));
	d->max_read = ~0;
	d->blksize = FUSE_DEFAULT_BLKSIZE;

	while ((p = strsep(&opt, ",")) != NULL) {
		int token;
		int value;
		unsigned uv;
		substring_t args[MAX_OPT_ARGS];
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case OPT_FD:
			if (match_int(&args[0], &value))
				return 0;
			d->fd = value;
			d->fd_present = 1;
			break;

		case OPT_ROOTMODE:
			if (match_octal(&args[0], &value))
				return 0;
			if (!fuse_valid_type(value))
				return 0;
			d->rootmode = value;
			d->rootmode_present = 1;
			break;

		case OPT_USER_ID:
			if (fuse_match_uint(&args[0], &uv))
				return 0;
			d->user_id = make_kuid(current_user_ns(), uv);
			if (!uid_valid(d->user_id))
				return 0;
			d->user_id_present = 1;
			break;

		case OPT_GROUP_ID:
			if (fuse_match_uint(&args[0], &uv))
				return 0;
			d->group_id = make_kgid(current_user_ns(), uv);
			if (!gid_valid(d->group_id))
				return 0;
			d->group_id_present = 1;
			break;

		case OPT_DEFAULT_PERMISSIONS:
			d->default_permissions = 1;
			break;

		case OPT_ALLOW_OTHER:
			d->allow_other = 1;
			break;

		case OPT_MAX_READ:
			if (match_int(&args[0], &value))
				return 0;
			d->max_read = value;
			break;

		case OPT_BLKSIZE:
			if (!is_bdev || match_int(&args[0], &value))
				return 0;
			d->blksize = value;
			break;

		default:
			return 0;
		}
	}

	if (!d->fd_present || !d->rootmode_present ||
	    !d->user_id_present || !d->group_id_present)
		return 0;

	return 1;
}

static int fuse_show_options(struct seq_file *m, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	seq_printf(m, ",user_id=%u", from_kuid_munged(&init_user_ns, fc->user_id));
	seq_printf(m, ",group_id=%u", from_kgid_munged(&init_user_ns, fc->group_id));
	if (fc->default_permissions)
		seq_puts(m, ",default_permissions");
	if (fc->allow_other)
		seq_puts(m, ",allow_other");
	if (fc->max_read != ~0)
		seq_printf(m, ",max_read=%u", fc->max_read);
	if (sb->s_bdev && sb->s_blocksize != FUSE_DEFAULT_BLKSIZE)
		seq_printf(m, ",blksize=%lu", sb->s_blocksize);
	return 0;
}

static void fuse_iqueue_init(struct fuse_iqueue *fiq)
{
	memset(fiq, 0, sizeof(struct fuse_iqueue));
	init_waitqueue_head(&fiq->waitq);
	INIT_LIST_HEAD(&fiq->pending);
	INIT_LIST_HEAD(&fiq->interrupts);
	fiq->forget_list_tail = &fiq->forget_list_head;
	fiq->connected = 1;
}

static void fuse_pqueue_init(struct fuse_pqueue *fpq)
{
	memset(fpq, 0, sizeof(struct fuse_pqueue));
	spin_lock_init(&fpq->lock);
	INIT_LIST_HEAD(&fpq->processing);
	INIT_LIST_HEAD(&fpq->io);
	fpq->connected = 1;
}

void fuse_conn_init(struct fuse_conn *fc)
{
	memset(fc, 0, sizeof(*fc));
	spin_lock_init(&fc->lock);
	init_rwsem(&fc->killsb);
	atomic_set(&fc->count, 1);
	atomic_set(&fc->dev_count, 1);
	init_waitqueue_head(&fc->blocked_waitq);
	init_waitqueue_head(&fc->reserved_req_waitq);
	fuse_iqueue_init(&fc->iq);
	INIT_LIST_HEAD(&fc->bg_queue);
	INIT_LIST_HEAD(&fc->entry);
	INIT_LIST_HEAD(&fc->devices);
	atomic_set(&fc->num_waiting, 0);
	fc->max_background = FUSE_DEFAULT_MAX_BACKGROUND;
	fc->congestion_threshold = FUSE_DEFAULT_CONGESTION_THRESHOLD;
	fc->khctr = 0;
	fc->polled_files = RB_ROOT;
	fc->blocked = 0;
	fc->initialized = 0;
	fc->connected = 1;
	fc->attr_version = 1;
	get_random_bytes(&fc->scramble_key, sizeof(fc->scramble_key));
}
EXPORT_SYMBOL_GPL(fuse_conn_init);

void fuse_conn_put(struct fuse_conn *fc)
{
	if (atomic_dec_and_test(&fc->count)) {
		if (fc->destroy_req)
			fuse_request_free(fc->destroy_req);
		fc->release(fc);
	}
}
EXPORT_SYMBOL_GPL(fuse_conn_put);

struct fuse_conn *fuse_conn_get(struct fuse_conn *fc)
{
	atomic_inc(&fc->count);
	return fc;
}
EXPORT_SYMBOL_GPL(fuse_conn_get);

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
	.destroy_inode  = fuse_destroy_inode,
	.evict_inode	= fuse_evict_inode,
	.write_inode	= fuse_write_inode,
	.drop_inode	= generic_delete_inode,
	.remount_fs	= fuse_remount_fs,
	.put_super	= fuse_put_super,
	.umount_begin	= fuse_umount_begin,
	.statfs		= fuse_statfs,
	.show_options	= fuse_show_options,
};

static void sanitize_global_limit(unsigned *limit)
{
	if (*limit == 0)
		*limit = ((totalram_pages << PAGE_SHIFT) >> 13) /
			 sizeof(struct fuse_req);

	if (*limit >= 1 << 16)
		*limit = (1 << 16) - 1;
}

static int set_global_limit(const char *val, struct kernel_param *kp)
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
}

static void process_init_reply(struct fuse_conn *fc, struct fuse_req *req)
{
	struct fuse_init_out *arg = &req->misc.init_out;

	if (req->out.h.error || arg->major != FUSE_KERNEL_VERSION)
		fc->conn_error = 1;
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
				fc->sb->s_time_gran = arg->time_gran;
			if ((arg->flags & FUSE_POSIX_ACL)) {
				fc->default_permissions = 1;
				fc->posix_acl = 1;
				fc->sb->s_xattr = fuse_acl_xattr_handlers;
			}
		} else {
			ra_pages = fc->max_read / PAGE_SIZE;
			fc->no_lock = 1;
			fc->no_flock = 1;
		}

		fc->bdi.ra_pages = min(fc->bdi.ra_pages, ra_pages);
		fc->minor = arg->minor;
		fc->max_write = arg->minor < 5 ? 4096 : arg->max_write;
		fc->max_write = max_t(unsigned, 4096, fc->max_write);
		fc->conn_init = 1;
	}
	fuse_set_initialized(fc);
	wake_up_all(&fc->blocked_waitq);
}

static void fuse_send_init(struct fuse_conn *fc, struct fuse_req *req)
{
	struct fuse_init_in *arg = &req->misc.init_in;

	arg->major = FUSE_KERNEL_VERSION;
	arg->minor = FUSE_KERNEL_MINOR_VERSION;
	arg->max_readahead = fc->bdi.ra_pages * PAGE_SIZE;
	arg->flags |= FUSE_ASYNC_READ | FUSE_POSIX_LOCKS | FUSE_ATOMIC_O_TRUNC |
		FUSE_EXPORT_SUPPORT | FUSE_BIG_WRITES | FUSE_DONT_MASK |
		FUSE_SPLICE_WRITE | FUSE_SPLICE_MOVE | FUSE_SPLICE_READ |
		FUSE_FLOCK_LOCKS | FUSE_HAS_IOCTL_DIR | FUSE_AUTO_INVAL_DATA |
		FUSE_DO_READDIRPLUS | FUSE_READDIRPLUS_AUTO | FUSE_ASYNC_DIO |
		FUSE_WRITEBACK_CACHE | FUSE_NO_OPEN_SUPPORT |
		FUSE_PARALLEL_DIROPS | FUSE_HANDLE_KILLPRIV | FUSE_POSIX_ACL;
	req->in.h.opcode = FUSE_INIT;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(*arg);
	req->in.args[0].value = arg;
	req->out.numargs = 1;
	/* Variable length argument used for backward compatibility
	   with interface version < 7.5.  Rest of init_out is zeroed
	   by do_get_request(), so a short reply is not a problem */
	req->out.argvar = 1;
	req->out.args[0].size = sizeof(struct fuse_init_out);
	req->out.args[0].value = &req->misc.init_out;
	req->end = process_init_reply;
	fuse_request_send_background(fc, req);
}

static void fuse_free_conn(struct fuse_conn *fc)
{
	WARN_ON(!list_empty(&fc->devices));
	kfree_rcu(fc, rcu);
}

static int fuse_bdi_init(struct fuse_conn *fc, struct super_block *sb)
{
	int err;

	fc->bdi.name = "fuse";
	fc->bdi.ra_pages = (VM_MAX_READAHEAD * 1024) / PAGE_SIZE;
	/* fuse does it's own writeback accounting */
	fc->bdi.capabilities = BDI_CAP_NO_ACCT_WB | BDI_CAP_STRICTLIMIT;

	err = bdi_init(&fc->bdi);
	if (err)
		return err;

	fc->bdi_initialized = 1;

	if (sb->s_bdev) {
		err =  bdi_register(&fc->bdi, NULL, "%u:%u-fuseblk",
				    MAJOR(fc->dev), MINOR(fc->dev));
	} else {
		err = bdi_register_dev(&fc->bdi, fc->dev);
	}

	if (err)
		return err;

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
	bdi_set_max_ratio(&fc->bdi, 1);

	return 0;
}

struct fuse_dev *fuse_dev_alloc(struct fuse_conn *fc)
{
	struct fuse_dev *fud;

	fud = kzalloc(sizeof(struct fuse_dev), GFP_KERNEL);
	if (fud) {
		fud->fc = fuse_conn_get(fc);
		fuse_pqueue_init(&fud->pq);

		spin_lock(&fc->lock);
		list_add_tail(&fud->entry, &fc->devices);
		spin_unlock(&fc->lock);
	}

	return fud;
}
EXPORT_SYMBOL_GPL(fuse_dev_alloc);

void fuse_dev_free(struct fuse_dev *fud)
{
	struct fuse_conn *fc = fud->fc;

	if (fc) {
		spin_lock(&fc->lock);
		list_del(&fud->entry);
		spin_unlock(&fc->lock);

		fuse_conn_put(fc);
	}
	kfree(fud);
}
EXPORT_SYMBOL_GPL(fuse_dev_free);

static int fuse_fill_super(struct super_block *sb, void *data, int silent)
{
	struct fuse_dev *fud;
	struct fuse_conn *fc;
	struct inode *root;
	struct fuse_mount_data d;
	struct file *file;
	struct dentry *root_dentry;
	struct fuse_req *init_req;
	int err;
	int is_bdev = sb->s_bdev != NULL;

	err = -EINVAL;
	if (sb->s_flags & MS_MANDLOCK)
		goto err;

	sb->s_flags &= ~(MS_NOSEC | MS_I_VERSION);

	if (!parse_fuse_opt(data, &d, is_bdev))
		goto err;

	if (is_bdev) {
#ifdef CONFIG_BLOCK
		err = -EINVAL;
		if (!sb_set_blocksize(sb, d.blksize))
			goto err;
#endif
	} else {
		sb->s_blocksize = PAGE_SIZE;
		sb->s_blocksize_bits = PAGE_SHIFT;
	}
	sb->s_magic = FUSE_SUPER_MAGIC;
	sb->s_op = &fuse_super_operations;
	sb->s_xattr = fuse_xattr_handlers;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_export_op = &fuse_export_operations;

	file = fget(d.fd);
	err = -EINVAL;
	if (!file)
		goto err;

	if ((file->f_op != &fuse_dev_operations) ||
	    (file->f_cred->user_ns != &init_user_ns))
		goto err_fput;

	fc = kmalloc(sizeof(*fc), GFP_KERNEL);
	err = -ENOMEM;
	if (!fc)
		goto err_fput;

	fuse_conn_init(fc);
	fc->release = fuse_free_conn;

	fud = fuse_dev_alloc(fc);
	if (!fud)
		goto err_put_conn;

	fc->dev = sb->s_dev;
	fc->sb = sb;
	err = fuse_bdi_init(fc, sb);
	if (err)
		goto err_dev_free;

	sb->s_bdi = &fc->bdi;

	/* Handle umasking inside the fuse code */
	if (sb->s_flags & MS_POSIXACL)
		fc->dont_mask = 1;
	sb->s_flags |= MS_POSIXACL;

	fc->default_permissions = d.default_permissions;
	fc->allow_other = d.allow_other;
	fc->user_id = d.user_id;
	fc->group_id = d.group_id;
	fc->max_read = max_t(unsigned, 4096, d.max_read);

	/* Used by get_root_inode() */
	sb->s_fs_info = fc;

	err = -ENOMEM;
	root = fuse_get_root_inode(sb, d.rootmode);
	root_dentry = d_make_root(root);
	if (!root_dentry)
		goto err_dev_free;
	/* only now - we want root dentry with NULL ->d_op */
	sb->s_d_op = &fuse_dentry_operations;

	init_req = fuse_request_alloc(0);
	if (!init_req)
		goto err_put_root;
	__set_bit(FR_BACKGROUND, &init_req->flags);

	if (is_bdev) {
		fc->destroy_req = fuse_request_alloc(0);
		if (!fc->destroy_req)
			goto err_free_init_req;
	}

	mutex_lock(&fuse_mutex);
	err = -EINVAL;
	if (file->private_data)
		goto err_unlock;

	err = fuse_ctl_add_conn(fc);
	if (err)
		goto err_unlock;

	list_add_tail(&fc->entry, &fuse_conn_list);
	sb->s_root = root_dentry;
	file->private_data = fud;
	mutex_unlock(&fuse_mutex);
	/*
	 * atomic_dec_and_test() in fput() provides the necessary
	 * memory barrier for file->private_data to be visible on all
	 * CPUs after this
	 */
	fput(file);

	fuse_send_init(fc, init_req);

	return 0;

 err_unlock:
	mutex_unlock(&fuse_mutex);
 err_free_init_req:
	fuse_request_free(init_req);
 err_put_root:
	dput(root_dentry);
 err_dev_free:
	fuse_dev_free(fud);
 err_put_conn:
	fuse_bdi_destroy(fc);
	fuse_conn_put(fc);
 err_fput:
	fput(file);
 err:
	return err;
}

static struct dentry *fuse_mount(struct file_system_type *fs_type,
		       int flags, const char *dev_name,
		       void *raw_data)
{
	return mount_nodev(fs_type, flags, raw_data, fuse_fill_super);
}

static void fuse_kill_sb_anon(struct super_block *sb)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	if (fc) {
		down_write(&fc->killsb);
		fc->sb = NULL;
		up_write(&fc->killsb);
	}

	kill_anon_super(sb);
}

static struct file_system_type fuse_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "fuse",
	.fs_flags	= FS_HAS_SUBTYPE,
	.mount		= fuse_mount,
	.kill_sb	= fuse_kill_sb_anon,
};
MODULE_ALIAS_FS("fuse");

#ifdef CONFIG_BLOCK
static struct dentry *fuse_mount_blk(struct file_system_type *fs_type,
			   int flags, const char *dev_name,
			   void *raw_data)
{
	return mount_bdev(fs_type, flags, dev_name, raw_data, fuse_fill_super);
}

static void fuse_kill_sb_blk(struct super_block *sb)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	if (fc) {
		down_write(&fc->killsb);
		fc->sb = NULL;
		up_write(&fc->killsb);
	}

	kill_block_super(sb);
}

static struct file_system_type fuseblk_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "fuseblk",
	.mount		= fuse_mount_blk,
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
					      SLAB_HWCACHE_ALIGN|SLAB_ACCOUNT,
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

	printk(KERN_INFO "fuse init (API version %i.%i)\n",
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
	printk(KERN_DEBUG "fuse exit\n");

	fuse_ctl_cleanup();
	fuse_sysfs_cleanup();
	fuse_fs_cleanup();
	fuse_dev_cleanup();
}

module_init(fuse_init);
module_exit(fuse_exit);
