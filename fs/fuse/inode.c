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
#include <linux/parser.h>
#include <linux/statfs.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/exportfs.h>

MODULE_AUTHOR("Miklos Szeredi <miklos@szeredi.hu>");
MODULE_DESCRIPTION("Filesystem in Userspace");
MODULE_LICENSE("GPL");

static struct kmem_cache *fuse_inode_cachep;
struct list_head fuse_conn_list;
DEFINE_MUTEX(fuse_mutex);

#define FUSE_SUPER_MAGIC 0x65735546

#define FUSE_DEFAULT_BLKSIZE 512

struct fuse_mount_data {
	int fd;
	unsigned rootmode;
	unsigned user_id;
	unsigned group_id;
	unsigned fd_present:1;
	unsigned rootmode_present:1;
	unsigned user_id_present:1;
	unsigned group_id_present:1;
	unsigned flags;
	unsigned max_read;
	unsigned blksize;
};

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
	INIT_LIST_HEAD(&fi->write_files);
	INIT_LIST_HEAD(&fi->queued_writes);
	INIT_LIST_HEAD(&fi->writepages);
	init_waitqueue_head(&fi->page_waitq);
	fi->forget_req = fuse_request_alloc();
	if (!fi->forget_req) {
		kmem_cache_free(fuse_inode_cachep, inode);
		return NULL;
	}

	return inode;
}

static void fuse_destroy_inode(struct inode *inode)
{
	struct fuse_inode *fi = get_fuse_inode(inode);
	BUG_ON(!list_empty(&fi->write_files));
	BUG_ON(!list_empty(&fi->queued_writes));
	if (fi->forget_req)
		fuse_request_free(fi->forget_req);
	kmem_cache_free(fuse_inode_cachep, inode);
}

void fuse_send_forget(struct fuse_conn *fc, struct fuse_req *req,
		      u64 nodeid, u64 nlookup)
{
	struct fuse_forget_in *inarg = &req->misc.forget_in;
	inarg->nlookup = nlookup;
	req->in.h.opcode = FUSE_FORGET;
	req->in.h.nodeid = nodeid;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(struct fuse_forget_in);
	req->in.args[0].value = inarg;
	fuse_request_send_noreply(fc, req);
}

static void fuse_clear_inode(struct inode *inode)
{
	if (inode->i_sb->s_flags & MS_ACTIVE) {
		struct fuse_conn *fc = get_fuse_conn(inode);
		struct fuse_inode *fi = get_fuse_inode(inode);
		fuse_send_forget(fc, fi->forget_req, fi->nodeid, fi->nlookup);
		fi->forget_req = NULL;
	}
}

static int fuse_remount_fs(struct super_block *sb, int *flags, char *data)
{
	if (*flags & MS_MANDLOCK)
		return -EINVAL;

	return 0;
}

void fuse_truncate(struct address_space *mapping, loff_t offset)
{
	/* See vmtruncate() */
	unmap_mapping_range(mapping, offset + PAGE_SIZE - 1, 0, 1);
	truncate_inode_pages(mapping, offset);
	unmap_mapping_range(mapping, offset + PAGE_SIZE - 1, 0, 1);
}

void fuse_change_attributes_common(struct inode *inode, struct fuse_attr *attr,
				   u64 attr_valid)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_inode *fi = get_fuse_inode(inode);

	fi->attr_version = ++fc->attr_version;
	fi->i_time = attr_valid;

	inode->i_ino     = attr->ino;
	inode->i_mode    = (inode->i_mode & S_IFMT) | (attr->mode & 07777);
	inode->i_nlink   = attr->nlink;
	inode->i_uid     = attr->uid;
	inode->i_gid     = attr->gid;
	inode->i_blocks  = attr->blocks;
	inode->i_atime.tv_sec   = attr->atime;
	inode->i_atime.tv_nsec  = attr->atimensec;
	inode->i_mtime.tv_sec   = attr->mtime;
	inode->i_mtime.tv_nsec  = attr->mtimensec;
	inode->i_ctime.tv_sec   = attr->ctime;
	inode->i_ctime.tv_nsec  = attr->ctimensec;

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
	if (!(fc->flags & FUSE_DEFAULT_PERMISSIONS))
		inode->i_mode &= ~S_ISVTX;
}

void fuse_change_attributes(struct inode *inode, struct fuse_attr *attr,
			    u64 attr_valid, u64 attr_version)
{
	struct fuse_conn *fc = get_fuse_conn(inode);
	struct fuse_inode *fi = get_fuse_inode(inode);
	loff_t oldsize;

	spin_lock(&fc->lock);
	if (attr_version != 0 && fi->attr_version > attr_version) {
		spin_unlock(&fc->lock);
		return;
	}

	fuse_change_attributes_common(inode, attr, attr_valid);

	oldsize = inode->i_size;
	i_size_write(inode, attr->size);
	spin_unlock(&fc->lock);

	if (S_ISREG(inode->i_mode) && oldsize != attr->size) {
		if (attr->size < oldsize)
			fuse_truncate(inode->i_mapping, attr->size);
		invalidate_inode_pages2(inode->i_mapping);
	}
}

static void fuse_init_inode(struct inode *inode, struct fuse_attr *attr)
{
	inode->i_mode = attr->mode & S_IFMT;
	inode->i_size = attr->size;
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
		inode->i_flags |= S_NOATIME|S_NOCMTIME;
		inode->i_generation = generation;
		inode->i_data.backing_dev_info = &fc->bdi;
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
	if (offset >= 0) {
		pg_start = offset >> PAGE_CACHE_SHIFT;
		if (len <= 0)
			pg_end = -1;
		else
			pg_end = (offset + len - 1) >> PAGE_CACHE_SHIFT;
		invalidate_inode_pages2_range(inode->i_mapping,
					      pg_start, pg_end);
	}
	iput(inode);
	return 0;
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
		req->force = 1;
		fuse_request_send(fc, req);
		fuse_put_request(fc, req);
	}
}

static void fuse_bdi_destroy(struct fuse_conn *fc)
{
	if (fc->bdi_initialized)
		bdi_destroy(&fc->bdi);
}

void fuse_conn_kill(struct fuse_conn *fc)
{
	spin_lock(&fc->lock);
	fc->connected = 0;
	fc->blocked = 0;
	spin_unlock(&fc->lock);
	/* Flush all readers on this fs */
	kill_fasync(&fc->fasync, SIGIO, POLL_IN);
	wake_up_all(&fc->waitq);
	wake_up_all(&fc->blocked_waitq);
	wake_up_all(&fc->reserved_req_waitq);
	mutex_lock(&fuse_mutex);
	list_del(&fc->entry);
	fuse_ctl_remove_conn(fc);
	mutex_unlock(&fuse_mutex);
	fuse_bdi_destroy(fc);
}
EXPORT_SYMBOL_GPL(fuse_conn_kill);

static void fuse_put_super(struct super_block *sb)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	fuse_send_destroy(fc);
	fuse_conn_kill(fc);
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
	struct fuse_req *req;
	struct fuse_statfs_out outarg;
	int err;

	if (!fuse_allow_task(fc, current)) {
		buf->f_type = FUSE_SUPER_MAGIC;
		return 0;
	}

	req = fuse_get_req(fc);
	if (IS_ERR(req))
		return PTR_ERR(req);

	memset(&outarg, 0, sizeof(outarg));
	req->in.numargs = 0;
	req->in.h.opcode = FUSE_STATFS;
	req->in.h.nodeid = get_node_id(dentry->d_inode);
	req->out.numargs = 1;
	req->out.args[0].size =
		fc->minor < 4 ? FUSE_COMPAT_STATFS_SIZE : sizeof(outarg);
	req->out.args[0].value = &outarg;
	fuse_request_send(fc, req);
	err = req->out.h.error;
	if (!err)
		convert_fuse_statfs(buf, &outarg.st);
	fuse_put_request(fc, req);
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

static int parse_fuse_opt(char *opt, struct fuse_mount_data *d, int is_bdev)
{
	char *p;
	memset(d, 0, sizeof(struct fuse_mount_data));
	d->max_read = ~0;
	d->blksize = FUSE_DEFAULT_BLKSIZE;

	while ((p = strsep(&opt, ",")) != NULL) {
		int token;
		int value;
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
			if (match_int(&args[0], &value))
				return 0;
			d->user_id = value;
			d->user_id_present = 1;
			break;

		case OPT_GROUP_ID:
			if (match_int(&args[0], &value))
				return 0;
			d->group_id = value;
			d->group_id_present = 1;
			break;

		case OPT_DEFAULT_PERMISSIONS:
			d->flags |= FUSE_DEFAULT_PERMISSIONS;
			break;

		case OPT_ALLOW_OTHER:
			d->flags |= FUSE_ALLOW_OTHER;
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

static int fuse_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct fuse_conn *fc = get_fuse_conn_super(mnt->mnt_sb);

	seq_printf(m, ",user_id=%u", fc->user_id);
	seq_printf(m, ",group_id=%u", fc->group_id);
	if (fc->flags & FUSE_DEFAULT_PERMISSIONS)
		seq_puts(m, ",default_permissions");
	if (fc->flags & FUSE_ALLOW_OTHER)
		seq_puts(m, ",allow_other");
	if (fc->max_read != ~0)
		seq_printf(m, ",max_read=%u", fc->max_read);
	if (mnt->mnt_sb->s_bdev &&
	    mnt->mnt_sb->s_blocksize != FUSE_DEFAULT_BLKSIZE)
		seq_printf(m, ",blksize=%lu", mnt->mnt_sb->s_blocksize);
	return 0;
}

void fuse_conn_init(struct fuse_conn *fc)
{
	memset(fc, 0, sizeof(*fc));
	spin_lock_init(&fc->lock);
	mutex_init(&fc->inst_mutex);
	init_rwsem(&fc->killsb);
	atomic_set(&fc->count, 1);
	init_waitqueue_head(&fc->waitq);
	init_waitqueue_head(&fc->blocked_waitq);
	init_waitqueue_head(&fc->reserved_req_waitq);
	INIT_LIST_HEAD(&fc->pending);
	INIT_LIST_HEAD(&fc->processing);
	INIT_LIST_HEAD(&fc->io);
	INIT_LIST_HEAD(&fc->interrupts);
	INIT_LIST_HEAD(&fc->bg_queue);
	INIT_LIST_HEAD(&fc->entry);
	atomic_set(&fc->num_waiting, 0);
	fc->khctr = 0;
	fc->polled_files = RB_ROOT;
	fc->reqctr = 0;
	fc->blocked = 1;
	fc->attr_version = 1;
	get_random_bytes(&fc->scramble_key, sizeof(fc->scramble_key));
}
EXPORT_SYMBOL_GPL(fuse_conn_init);

void fuse_conn_put(struct fuse_conn *fc)
{
	if (atomic_dec_and_test(&fc->count)) {
		if (fc->destroy_req)
			fuse_request_free(fc->destroy_req);
		mutex_destroy(&fc->inst_mutex);
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
		struct qstr name;

		if (!fc->export_support)
			goto out_err;

		name.len = 1;
		name.name = ".";
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
	if (!IS_ERR(entry) && get_node_id(inode) != FUSE_ROOT_ID) {
		entry->d_op = &fuse_dentry_operations;
		fuse_invalidate_entry_cache(entry);
	}

	return entry;

 out_iput:
	iput(inode);
 out_err:
	return ERR_PTR(err);
}

static int fuse_encode_fh(struct dentry *dentry, u32 *fh, int *max_len,
			   int connectable)
{
	struct inode *inode = dentry->d_inode;
	bool encode_parent = connectable && !S_ISDIR(inode->i_mode);
	int len = encode_parent ? 6 : 3;
	u64 nodeid;
	u32 generation;

	if (*max_len < len)
		return  255;

	nodeid = get_fuse_inode(inode)->nodeid;
	generation = inode->i_generation;

	fh[0] = (u32)(nodeid >> 32);
	fh[1] = (u32)(nodeid & 0xffffffff);
	fh[2] = generation;

	if (encode_parent) {
		struct inode *parent;

		spin_lock(&dentry->d_lock);
		parent = dentry->d_parent->d_inode;
		nodeid = get_fuse_inode(parent)->nodeid;
		generation = parent->i_generation;
		spin_unlock(&dentry->d_lock);

		fh[3] = (u32)(nodeid >> 32);
		fh[4] = (u32)(nodeid & 0xffffffff);
		fh[5] = generation;
	}

	*max_len = len;
	return encode_parent ? 0x82 : 0x81;
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
	struct inode *child_inode = child->d_inode;
	struct fuse_conn *fc = get_fuse_conn(child_inode);
	struct inode *inode;
	struct dentry *parent;
	struct fuse_entry_out outarg;
	struct qstr name;
	int err;

	if (!fc->export_support)
		return ERR_PTR(-ESTALE);

	name.len = 2;
	name.name = "..";
	err = fuse_lookup_name(child_inode->i_sb, get_node_id(child_inode),
			       &name, &outarg, &inode);
	if (err) {
		if (err == -ENOENT)
			return ERR_PTR(-ESTALE);
		return ERR_PTR(err);
	}

	parent = d_obtain_alias(inode);
	if (!IS_ERR(parent) && get_node_id(inode) != FUSE_ROOT_ID) {
		parent->d_op = &fuse_dentry_operations;
		fuse_invalidate_entry_cache(parent);
	}

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
	.clear_inode	= fuse_clear_inode,
	.drop_inode	= generic_delete_inode,
	.remount_fs	= fuse_remount_fs,
	.put_super	= fuse_put_super,
	.umount_begin	= fuse_umount_begin,
	.statfs		= fuse_statfs,
	.show_options	= fuse_show_options,
};

static void process_init_reply(struct fuse_conn *fc, struct fuse_req *req)
{
	struct fuse_init_out *arg = &req->misc.init_out;

	if (req->out.h.error || arg->major != FUSE_KERNEL_VERSION)
		fc->conn_error = 1;
	else {
		unsigned long ra_pages;

		if (arg->minor >= 6) {
			ra_pages = arg->max_readahead / PAGE_CACHE_SIZE;
			if (arg->flags & FUSE_ASYNC_READ)
				fc->async_read = 1;
			if (!(arg->flags & FUSE_POSIX_LOCKS))
				fc->no_lock = 1;
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
		} else {
			ra_pages = fc->max_read / PAGE_CACHE_SIZE;
			fc->no_lock = 1;
		}

		fc->bdi.ra_pages = min(fc->bdi.ra_pages, ra_pages);
		fc->minor = arg->minor;
		fc->max_write = arg->minor < 5 ? 4096 : arg->max_write;
		fc->max_write = max_t(unsigned, 4096, fc->max_write);
		fc->conn_init = 1;
	}
	fc->blocked = 0;
	wake_up_all(&fc->blocked_waitq);
}

static void fuse_send_init(struct fuse_conn *fc, struct fuse_req *req)
{
	struct fuse_init_in *arg = &req->misc.init_in;

	arg->major = FUSE_KERNEL_VERSION;
	arg->minor = FUSE_KERNEL_MINOR_VERSION;
	arg->max_readahead = fc->bdi.ra_pages * PAGE_CACHE_SIZE;
	arg->flags |= FUSE_ASYNC_READ | FUSE_POSIX_LOCKS | FUSE_ATOMIC_O_TRUNC |
		FUSE_EXPORT_SUPPORT | FUSE_BIG_WRITES | FUSE_DONT_MASK;
	req->in.h.opcode = FUSE_INIT;
	req->in.numargs = 1;
	req->in.args[0].size = sizeof(*arg);
	req->in.args[0].value = arg;
	req->out.numargs = 1;
	/* Variable length arguement used for backward compatibility
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
	kfree(fc);
}

static int fuse_bdi_init(struct fuse_conn *fc, struct super_block *sb)
{
	int err;

	fc->bdi.ra_pages = (VM_MAX_READAHEAD * 1024) / PAGE_CACHE_SIZE;
	fc->bdi.unplug_io_fn = default_unplug_io_fn;
	/* fuse does it's own writeback accounting */
	fc->bdi.capabilities = BDI_CAP_NO_ACCT_WB;

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

static int fuse_fill_super(struct super_block *sb, void *data, int silent)
{
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

	if (!parse_fuse_opt((char *) data, &d, is_bdev))
		goto err;

	if (is_bdev) {
#ifdef CONFIG_BLOCK
		err = -EINVAL;
		if (!sb_set_blocksize(sb, d.blksize))
			goto err;
#endif
	} else {
		sb->s_blocksize = PAGE_CACHE_SIZE;
		sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	}
	sb->s_magic = FUSE_SUPER_MAGIC;
	sb->s_op = &fuse_super_operations;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_export_op = &fuse_export_operations;

	file = fget(d.fd);
	err = -EINVAL;
	if (!file)
		goto err;

	if (file->f_op != &fuse_dev_operations)
		goto err_fput;

	fc = kmalloc(sizeof(*fc), GFP_KERNEL);
	err = -ENOMEM;
	if (!fc)
		goto err_fput;

	fuse_conn_init(fc);

	fc->dev = sb->s_dev;
	fc->sb = sb;
	err = fuse_bdi_init(fc, sb);
	if (err)
		goto err_put_conn;

	/* Handle umasking inside the fuse code */
	if (sb->s_flags & MS_POSIXACL)
		fc->dont_mask = 1;
	sb->s_flags |= MS_POSIXACL;

	fc->release = fuse_free_conn;
	fc->flags = d.flags;
	fc->user_id = d.user_id;
	fc->group_id = d.group_id;
	fc->max_read = max_t(unsigned, 4096, d.max_read);

	/* Used by get_root_inode() */
	sb->s_fs_info = fc;

	err = -ENOMEM;
	root = fuse_get_root_inode(sb, d.rootmode);
	if (!root)
		goto err_put_conn;

	root_dentry = d_alloc_root(root);
	if (!root_dentry) {
		iput(root);
		goto err_put_conn;
	}

	init_req = fuse_request_alloc();
	if (!init_req)
		goto err_put_root;

	if (is_bdev) {
		fc->destroy_req = fuse_request_alloc();
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
	fc->connected = 1;
	file->private_data = fuse_conn_get(fc);
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
 err_put_conn:
	fuse_bdi_destroy(fc);
	fuse_conn_put(fc);
 err_fput:
	fput(file);
 err:
	return err;
}

static int fuse_get_sb(struct file_system_type *fs_type,
		       int flags, const char *dev_name,
		       void *raw_data, struct vfsmount *mnt)
{
	return get_sb_nodev(fs_type, flags, raw_data, fuse_fill_super, mnt);
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
	.get_sb		= fuse_get_sb,
	.kill_sb	= fuse_kill_sb_anon,
};

#ifdef CONFIG_BLOCK
static int fuse_get_sb_blk(struct file_system_type *fs_type,
			   int flags, const char *dev_name,
			   void *raw_data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, raw_data, fuse_fill_super,
			   mnt);
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
	.get_sb		= fuse_get_sb_blk,
	.kill_sb	= fuse_kill_sb_blk,
	.fs_flags	= FS_REQUIRES_DEV | FS_HAS_SUBTYPE,
};

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

	err = register_filesystem(&fuse_fs_type);
	if (err)
		goto out;

	err = register_fuseblk();
	if (err)
		goto out_unreg;

	fuse_inode_cachep = kmem_cache_create("fuse_inode",
					      sizeof(struct fuse_inode),
					      0, SLAB_HWCACHE_ALIGN,
					      fuse_inode_init_once);
	err = -ENOMEM;
	if (!fuse_inode_cachep)
		goto out_unreg2;

	return 0;

 out_unreg2:
	unregister_fuseblk();
 out_unreg:
	unregister_filesystem(&fuse_fs_type);
 out:
	return err;
}

static void fuse_fs_cleanup(void)
{
	unregister_filesystem(&fuse_fs_type);
	unregister_fuseblk();
	kmem_cache_destroy(fuse_inode_cachep);
}

static struct kobject *fuse_kobj;
static struct kobject *connections_kobj;

static int fuse_sysfs_init(void)
{
	int err;

	fuse_kobj = kobject_create_and_add("fuse", fs_kobj);
	if (!fuse_kobj) {
		err = -ENOMEM;
		goto out_err;
	}

	connections_kobj = kobject_create_and_add("connections", fuse_kobj);
	if (!connections_kobj) {
		err = -ENOMEM;
		goto out_fuse_unregister;
	}

	return 0;

 out_fuse_unregister:
	kobject_put(fuse_kobj);
 out_err:
	return err;
}

static void fuse_sysfs_cleanup(void)
{
	kobject_put(connections_kobj);
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
