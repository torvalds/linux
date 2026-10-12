// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Christian Brauner <brauner@kernel.org> */
#include <linux/fs/super_types.h>
#include <linux/fs_context.h>
#include <linux/magic.h>
#include <linux/splice.h>
#include <linux/uio.h>

#include "mount.h"

static const struct super_operations nullfs_super_operations = {
	.statfs	= simple_statfs,
};

static loff_t nullfs_dir_llseek(struct file *file, loff_t offset, int whence)
{
	/* an empty directory has two entries . and .. at offsets 0 and 1 */
	return generic_file_llseek_size(file, offset, whence, 2, 2);
}

static int nullfs_dir_readdir(struct file *file, struct dir_context *ctx)
{
	dir_emit_dots(file, ctx);
	return 0;
}

/* the one inode of nullfs is shared by every holder, so no locks on it */
static int nullfs_nolock(struct file *file, int cmd, struct file_lock *fl)
{
	return -ENOLCK;
}

/* what libfs gives an empty directory, plus the refusal of file locks */
static const struct file_operations nullfs_dir_operations = {
	.llseek		= nullfs_dir_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= nullfs_dir_readdir,
	.fsync		= noop_fsync,
	.lock		= nullfs_nolock,
	.flock		= nullfs_nolock,
	.fop_flags	= FOP_IMMUTABLE,
};

static ssize_t nullfs_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	return 0;
}

static ssize_t nullfs_file_splice_read(struct file *in, loff_t *ppos,
				       struct pipe_inode_info *pipe,
				       size_t len, unsigned int flags)
{
	return 0;
}

static ssize_t nullfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	size_t count = iov_iter_count(from);

	iov_iter_advance(from, count);
	return count;
}

static int nullfs_pipe_to_null(struct pipe_inode_info *pipe,
			       struct pipe_buffer *buf, struct splice_desc *sd)
{
	return sd->len;
}

static ssize_t nullfs_file_splice_write(struct pipe_inode_info *pipe,
					struct file *out, loff_t *ppos,
					size_t len, unsigned int flags)
{
	return splice_from_pipe(pipe, out, ppos, len, flags, nullfs_pipe_to_null);
}

static int nullfs_file_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
			       struct iattr *attr)
{
	if (attr->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID))
		return -EPERM;
	return 0;
}

static const struct inode_operations nullfs_file_inode_operations = {
	.setattr	= nullfs_file_setattr,
};

static const struct file_operations nullfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= nullfs_file_read_iter,
	.write_iter	= nullfs_file_write_iter,
	.splice_read	= nullfs_file_splice_read,
	.splice_write	= nullfs_file_splice_write,
	.fsync		= noop_fsync,
	.lock		= nullfs_nolock,
	.flock		= nullfs_nolock,
};

/*
 * An empty regular file on @sb as a dentry of its own, never hashed under
 * the root so no lookup finds it.
 */
struct dentry *nullfs_new_file(struct super_block *sb)
{
	struct dentry *dentry;
	struct inode *inode;

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	/* the root directory is 1 */
	inode->i_ino = 2;
	inode->i_mode = S_IFREG | 0666;
	inode->i_op = &nullfs_file_inode_operations;
	inode->i_fop = &nullfs_file_operations;
	simple_inode_init_ts(inode);
	inode->i_flags |= S_NOATIME;

	dentry = d_alloc_anon(sb);
	if (!dentry) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	d_instantiate(dentry, inode);
	return dentry;
}

static int nullfs_fs_fill_super(struct super_block *s, struct fs_context *fc)
{
	struct inode *inode;

	s->s_maxbytes		= MAX_LFS_FILESIZE;
	s->s_blocksize		= PAGE_SIZE;
	s->s_blocksize_bits	= PAGE_SHIFT;
	s->s_magic		= NULL_FS_MAGIC;
	s->s_op			= &nullfs_super_operations;
	s->s_export_op		= NULL;
	s->s_xattr		= NULL;
	s->s_time_gran		= 1;
	s->s_d_flags		= 0;

	inode = new_inode(s);
	if (!inode)
		return -ENOMEM;

	/* nullfs is permanently empty... */
	make_empty_dir_inode(inode);
	inode->i_fop = &nullfs_dir_operations;
	simple_inode_init_ts(inode);
	inode->i_ino	= 1;
	/* ... and immutable, reading it leaves no trace either. */
	inode->i_flags |= S_IMMUTABLE | S_NOATIME;

	s->s_root = d_make_root(inode);
	if (!s->s_root)
		return -ENOMEM;

	return 0;
}

static int nullfs_fs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, nullfs_fs_fill_super);
}

static const struct fs_context_operations nullfs_fs_context_ops = {
	.get_tree	= nullfs_fs_get_tree,
};

static int nullfs_init_fs_context(struct fs_context *fc)
{
	fc->ops		= &nullfs_fs_context_ops;
	fc->sb_flags	|= SB_NOUSER;
	fc->s_iflags	|= SB_I_NOEXEC | SB_I_NODEV;
	return 0;
}

struct file_system_type nullfs_fs_type = {
	.name			= "nullfs",
	.fs_flags		= FS_DISALLOW_NOTIFY,
	.init_fs_context	= nullfs_init_fs_context,
	.kill_sb		= kill_anon_super,
};
