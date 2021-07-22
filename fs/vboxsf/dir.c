// SPDX-License-Identifier: MIT
/*
 * VirtualBox Guest Shared Folders support: Directory inode and file operations
 *
 * Copyright (C) 2006-2018 Oracle Corporation
 */

#include <linux/namei.h>
#include <linux/vbox_utils.h>
#include "vfsmod.h"

static int vboxsf_dir_open(struct inode *inode, struct file *file)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(inode->i_sb);
	struct shfl_createparms params = {};
	struct vboxsf_dir_info *sf_d;
	int err;

	sf_d = vboxsf_dir_info_alloc();
	if (!sf_d)
		return -ENOMEM;

	params.handle = SHFL_HANDLE_NIL;
	params.create_flags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_OPEN_IF_EXISTS |
			      SHFL_CF_ACT_FAIL_IF_NEW | SHFL_CF_ACCESS_READ;

	err = vboxsf_create_at_dentry(file_dentry(file), &params);
	if (err)
		goto err_free_dir_info;

	if (params.result != SHFL_FILE_EXISTS) {
		err = -ENOENT;
		goto err_close;
	}

	err = vboxsf_dir_read_all(sbi, sf_d, params.handle);
	if (err)
		goto err_close;

	vboxsf_close(sbi->root, params.handle);
	file->private_data = sf_d;
	return 0;

err_close:
	vboxsf_close(sbi->root, params.handle);
err_free_dir_info:
	vboxsf_dir_info_free(sf_d);
	return err;
}

static int vboxsf_dir_release(struct inode *inode, struct file *file)
{
	if (file->private_data)
		vboxsf_dir_info_free(file->private_data);

	return 0;
}

static unsigned int vboxsf_get_d_type(u32 mode)
{
	unsigned int d_type;

	switch (mode & SHFL_TYPE_MASK) {
	case SHFL_TYPE_FIFO:
		d_type = DT_FIFO;
		break;
	case SHFL_TYPE_DEV_CHAR:
		d_type = DT_CHR;
		break;
	case SHFL_TYPE_DIRECTORY:
		d_type = DT_DIR;
		break;
	case SHFL_TYPE_DEV_BLOCK:
		d_type = DT_BLK;
		break;
	case SHFL_TYPE_FILE:
		d_type = DT_REG;
		break;
	case SHFL_TYPE_SYMLINK:
		d_type = DT_LNK;
		break;
	case SHFL_TYPE_SOCKET:
		d_type = DT_SOCK;
		break;
	case SHFL_TYPE_WHITEOUT:
		d_type = DT_WHT;
		break;
	default:
		d_type = DT_UNKNOWN;
		break;
	}
	return d_type;
}

static bool vboxsf_dir_emit(struct file *dir, struct dir_context *ctx)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(file_inode(dir)->i_sb);
	struct vboxsf_dir_info *sf_d = dir->private_data;
	struct shfl_dirinfo *info;
	struct vboxsf_dir_buf *b;
	unsigned int d_type;
	loff_t i, cur = 0;
	ino_t fake_ino;
	void *end;
	int err;

	list_for_each_entry(b, &sf_d->info_list, head) {
try_next_entry:
		if (ctx->pos >= cur + b->entries) {
			cur += b->entries;
			continue;
		}

		/*
		 * Note the vboxsf_dir_info objects we are iterating over here
		 * are variable sized, so the info pointer may end up being
		 * unaligned. This is how we get the data from the host.
		 * Since vboxsf is only supported on x86 machines this is not
		 * a problem.
		 */
		for (i = 0, info = b->buf; i < ctx->pos - cur; i++) {
			end = &info->name.string.utf8[info->name.size];
			/* Only happens if the host gives us corrupt data */
			if (WARN_ON(end > (b->buf + b->used)))
				return false;
			info = end;
		}

		end = &info->name.string.utf8[info->name.size];
		if (WARN_ON(end > (b->buf + b->used)))
			return false;

		/* Info now points to the right entry, emit it. */
		d_type = vboxsf_get_d_type(info->info.attr.mode);

		/*
		 * On 32-bit systems pos is 64-bit signed, while ino is 32-bit
		 * unsigned so fake_ino may overflow, check for this.
		 */
		if ((ino_t)(ctx->pos + 1) != (u64)(ctx->pos + 1)) {
			vbg_err("vboxsf: fake ino overflow, truncating dir\n");
			return false;
		}
		fake_ino = ctx->pos + 1;

		if (sbi->nls) {
			char d_name[NAME_MAX];

			err = vboxsf_nlscpy(sbi, d_name, NAME_MAX,
					    info->name.string.utf8,
					    info->name.length);
			if (err) {
				/* skip erroneous entry and proceed */
				ctx->pos += 1;
				goto try_next_entry;
			}

			return dir_emit(ctx, d_name, strlen(d_name),
					fake_ino, d_type);
		}

		return dir_emit(ctx, info->name.string.utf8, info->name.length,
				fake_ino, d_type);
	}

	return false;
}

static int vboxsf_dir_iterate(struct file *dir, struct dir_context *ctx)
{
	bool emitted;

	do {
		emitted = vboxsf_dir_emit(dir, ctx);
		if (emitted)
			ctx->pos += 1;
	} while (emitted);

	return 0;
}

const struct file_operations vboxsf_dir_fops = {
	.open = vboxsf_dir_open,
	.iterate = vboxsf_dir_iterate,
	.release = vboxsf_dir_release,
	.read = generic_read_dir,
	.llseek = generic_file_llseek,
};

/*
 * This is called during name resolution/lookup to check if the @dentry in
 * the cache is still valid. the job is handled by vboxsf_inode_revalidate.
 */
static int vboxsf_dentry_revalidate(struct dentry *dentry, unsigned int flags)
{
	if (flags & LOOKUP_RCU)
		return -ECHILD;

	if (d_really_is_positive(dentry))
		return vboxsf_inode_revalidate(dentry) == 0;
	else
		return vboxsf_stat_dentry(dentry, NULL) == -ENOENT;
}

const struct dentry_operations vboxsf_dentry_ops = {
	.d_revalidate = vboxsf_dentry_revalidate
};

/* iops */

static struct dentry *vboxsf_dir_lookup(struct inode *parent,
					struct dentry *dentry,
					unsigned int flags)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(parent->i_sb);
	struct shfl_fsobjinfo fsinfo;
	struct inode *inode;
	int err;

	dentry->d_time = jiffies;

	err = vboxsf_stat_dentry(dentry, &fsinfo);
	if (err) {
		inode = (err == -ENOENT) ? NULL : ERR_PTR(err);
	} else {
		inode = vboxsf_new_inode(parent->i_sb);
		if (!IS_ERR(inode))
			vboxsf_init_inode(sbi, inode, &fsinfo, false);
	}

	return d_splice_alias(inode, dentry);
}

static int vboxsf_dir_instantiate(struct inode *parent, struct dentry *dentry,
				  struct shfl_fsobjinfo *info)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(parent->i_sb);
	struct vboxsf_inode *sf_i;
	struct inode *inode;

	inode = vboxsf_new_inode(parent->i_sb);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	sf_i = VBOXSF_I(inode);
	/* The host may have given us different attr then requested */
	sf_i->force_restat = 1;
	vboxsf_init_inode(sbi, inode, info, false);

	d_instantiate(dentry, inode);

	return 0;
}

static int vboxsf_dir_create(struct inode *parent, struct dentry *dentry,
			     umode_t mode, bool is_dir, bool excl, u64 *handle_ret)
{
	struct vboxsf_inode *sf_parent_i = VBOXSF_I(parent);
	struct vboxsf_sbi *sbi = VBOXSF_SBI(parent->i_sb);
	struct shfl_createparms params = {};
	int err;

	params.handle = SHFL_HANDLE_NIL;
	params.create_flags = SHFL_CF_ACT_CREATE_IF_NEW | SHFL_CF_ACCESS_READWRITE;
	if (is_dir)
		params.create_flags |= SHFL_CF_DIRECTORY;
	if (excl)
		params.create_flags |= SHFL_CF_ACT_FAIL_IF_EXISTS;

	params.info.attr.mode = (mode & 0777) |
				(is_dir ? SHFL_TYPE_DIRECTORY : SHFL_TYPE_FILE);
	params.info.attr.additional = SHFLFSOBJATTRADD_NOTHING;

	err = vboxsf_create_at_dentry(dentry, &params);
	if (err)
		return err;

	if (params.result != SHFL_FILE_CREATED)
		return -EPERM;

	err = vboxsf_dir_instantiate(parent, dentry, &params.info);
	if (err)
		goto out;

	/* parent directory access/change time changed */
	sf_parent_i->force_restat = 1;

out:
	if (err == 0 && handle_ret)
		*handle_ret = params.handle;
	else
		vboxsf_close(sbi->root, params.handle);

	return err;
}

static int vboxsf_dir_mkfile(struct user_namespace *mnt_userns,
			     struct inode *parent, struct dentry *dentry,
			     umode_t mode, bool excl)
{
	return vboxsf_dir_create(parent, dentry, mode, false, excl, NULL);
}

static int vboxsf_dir_mkdir(struct user_namespace *mnt_userns,
			    struct inode *parent, struct dentry *dentry,
			    umode_t mode)
{
	return vboxsf_dir_create(parent, dentry, mode, true, true, NULL);
}

static int vboxsf_dir_atomic_open(struct inode *parent, struct dentry *dentry,
				  struct file *file, unsigned int flags, umode_t mode)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(parent->i_sb);
	struct vboxsf_handle *sf_handle;
	struct dentry *res = NULL;
	u64 handle;
	int err;

	if (d_in_lookup(dentry)) {
		res = vboxsf_dir_lookup(parent, dentry, 0);
		if (IS_ERR(res))
			return PTR_ERR(res);

		if (res)
			dentry = res;
	}

	/* Only creates */
	if (!(flags & O_CREAT) || d_really_is_positive(dentry))
		return finish_no_open(file, res);

	err = vboxsf_dir_create(parent, dentry, mode, false, flags & O_EXCL, &handle);
	if (err)
		goto out;

	sf_handle = vboxsf_create_sf_handle(d_inode(dentry), handle, SHFL_CF_ACCESS_READWRITE);
	if (IS_ERR(sf_handle)) {
		vboxsf_close(sbi->root, handle);
		err = PTR_ERR(sf_handle);
		goto out;
	}

	err = finish_open(file, dentry, generic_file_open);
	if (err) {
		/* This also closes the handle passed to vboxsf_create_sf_handle() */
		vboxsf_release_sf_handle(d_inode(dentry), sf_handle);
		goto out;
	}

	file->private_data = sf_handle;
	file->f_mode |= FMODE_CREATED;
out:
	dput(res);
	return err;
}

static int vboxsf_dir_unlink(struct inode *parent, struct dentry *dentry)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(parent->i_sb);
	struct vboxsf_inode *sf_parent_i = VBOXSF_I(parent);
	struct inode *inode = d_inode(dentry);
	struct shfl_string *path;
	u32 flags;
	int err;

	if (S_ISDIR(inode->i_mode))
		flags = SHFL_REMOVE_DIR;
	else
		flags = SHFL_REMOVE_FILE;

	if (S_ISLNK(inode->i_mode))
		flags |= SHFL_REMOVE_SYMLINK;

	path = vboxsf_path_from_dentry(sbi, dentry);
	if (IS_ERR(path))
		return PTR_ERR(path);

	err = vboxsf_remove(sbi->root, path, flags);
	__putname(path);
	if (err)
		return err;

	/* parent directory access/change time changed */
	sf_parent_i->force_restat = 1;

	return 0;
}

static int vboxsf_dir_rename(struct user_namespace *mnt_userns,
			     struct inode *old_parent,
			     struct dentry *old_dentry,
			     struct inode *new_parent,
			     struct dentry *new_dentry,
			     unsigned int flags)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(old_parent->i_sb);
	struct vboxsf_inode *sf_old_parent_i = VBOXSF_I(old_parent);
	struct vboxsf_inode *sf_new_parent_i = VBOXSF_I(new_parent);
	u32 shfl_flags = SHFL_RENAME_FILE | SHFL_RENAME_REPLACE_IF_EXISTS;
	struct shfl_string *old_path, *new_path;
	int err;

	if (flags)
		return -EINVAL;

	old_path = vboxsf_path_from_dentry(sbi, old_dentry);
	if (IS_ERR(old_path))
		return PTR_ERR(old_path);

	new_path = vboxsf_path_from_dentry(sbi, new_dentry);
	if (IS_ERR(new_path)) {
		err = PTR_ERR(new_path);
		goto err_put_old_path;
	}

	if (d_inode(old_dentry)->i_mode & S_IFDIR)
		shfl_flags = 0;

	err = vboxsf_rename(sbi->root, old_path, new_path, shfl_flags);
	if (err == 0) {
		/* parent directories access/change time changed */
		sf_new_parent_i->force_restat = 1;
		sf_old_parent_i->force_restat = 1;
	}

	__putname(new_path);
err_put_old_path:
	__putname(old_path);
	return err;
}

static int vboxsf_dir_symlink(struct user_namespace *mnt_userns,
			      struct inode *parent, struct dentry *dentry,
			      const char *symname)
{
	struct vboxsf_inode *sf_parent_i = VBOXSF_I(parent);
	struct vboxsf_sbi *sbi = VBOXSF_SBI(parent->i_sb);
	int symname_size = strlen(symname) + 1;
	struct shfl_string *path, *ssymname;
	struct shfl_fsobjinfo info;
	int err;

	path = vboxsf_path_from_dentry(sbi, dentry);
	if (IS_ERR(path))
		return PTR_ERR(path);

	ssymname = kmalloc(SHFLSTRING_HEADER_SIZE + symname_size, GFP_KERNEL);
	if (!ssymname) {
		__putname(path);
		return -ENOMEM;
	}
	ssymname->length = symname_size - 1;
	ssymname->size = symname_size;
	memcpy(ssymname->string.utf8, symname, symname_size);

	err = vboxsf_symlink(sbi->root, path, ssymname, &info);
	kfree(ssymname);
	__putname(path);
	if (err) {
		/* -EROFS means symlinks are note support -> -EPERM */
		return (err == -EROFS) ? -EPERM : err;
	}

	err = vboxsf_dir_instantiate(parent, dentry, &info);
	if (err)
		return err;

	/* parent directory access/change time changed */
	sf_parent_i->force_restat = 1;
	return 0;
}

const struct inode_operations vboxsf_dir_iops = {
	.lookup  = vboxsf_dir_lookup,
	.create  = vboxsf_dir_mkfile,
	.mkdir   = vboxsf_dir_mkdir,
	.atomic_open = vboxsf_dir_atomic_open,
	.rmdir   = vboxsf_dir_unlink,
	.unlink  = vboxsf_dir_unlink,
	.rename  = vboxsf_dir_rename,
	.symlink = vboxsf_dir_symlink,
	.getattr = vboxsf_getattr,
	.setattr = vboxsf_setattr,
};
