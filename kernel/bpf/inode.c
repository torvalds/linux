/*
 * Minimal file system backend for holding eBPF maps and programs,
 * used by bpf(2) object pinning.
 *
 * Authors:
 *
 *	Daniel Borkmann <daniel@iogearbox.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/magic.h>
#include <linux/major.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/filter.h>
#include <linux/bpf.h>

enum bpf_type {
	BPF_TYPE_UNSPEC	= 0,
	BPF_TYPE_PROG,
	BPF_TYPE_MAP,
};

static void *bpf_any_get(void *raw, enum bpf_type type)
{
	switch (type) {
	case BPF_TYPE_PROG:
		raw = bpf_prog_inc(raw);
		break;
	case BPF_TYPE_MAP:
		raw = bpf_map_inc(raw, true);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return raw;
}

static void bpf_any_put(void *raw, enum bpf_type type)
{
	switch (type) {
	case BPF_TYPE_PROG:
		bpf_prog_put(raw);
		break;
	case BPF_TYPE_MAP:
		bpf_map_put_with_uref(raw);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

static void *bpf_fd_probe_obj(u32 ufd, enum bpf_type *type)
{
	void *raw;

	*type = BPF_TYPE_MAP;
	raw = bpf_map_get_with_uref(ufd);
	if (IS_ERR(raw)) {
		*type = BPF_TYPE_PROG;
		raw = bpf_prog_get(ufd);
	}

	return raw;
}

static const struct inode_operations bpf_dir_iops;

static const struct inode_operations bpf_prog_iops = { };
static const struct inode_operations bpf_map_iops  = { };

static struct inode *bpf_get_inode(struct super_block *sb,
				   const struct inode *dir,
				   umode_t mode)
{
	struct inode *inode;

	switch (mode & S_IFMT) {
	case S_IFDIR:
	case S_IFREG:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOSPC);

	inode->i_ino = get_next_ino();
	inode->i_atime = CURRENT_TIME;
	inode->i_mtime = inode->i_atime;
	inode->i_ctime = inode->i_atime;

	inode_init_owner(inode, dir, mode);

	return inode;
}

static int bpf_inode_type(const struct inode *inode, enum bpf_type *type)
{
	*type = BPF_TYPE_UNSPEC;
	if (inode->i_op == &bpf_prog_iops)
		*type = BPF_TYPE_PROG;
	else if (inode->i_op == &bpf_map_iops)
		*type = BPF_TYPE_MAP;
	else
		return -EACCES;

	return 0;
}

static bool bpf_dname_reserved(const struct dentry *dentry)
{
	return strchr(dentry->d_name.name, '.');
}

static int bpf_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	if (bpf_dname_reserved(dentry))
		return -EPERM;

	inode = bpf_get_inode(dir->i_sb, dir, mode | S_IFDIR);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &bpf_dir_iops;
	inode->i_fop = &simple_dir_operations;

	inc_nlink(inode);
	inc_nlink(dir);

	d_instantiate(dentry, inode);
	dget(dentry);

	return 0;
}

static int bpf_mkobj_ops(struct inode *dir, struct dentry *dentry,
			 umode_t mode, const struct inode_operations *iops)
{
	struct inode *inode;

	if (bpf_dname_reserved(dentry))
		return -EPERM;

	inode = bpf_get_inode(dir->i_sb, dir, mode | S_IFREG);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = iops;
	inode->i_private = dentry->d_fsdata;

	d_instantiate(dentry, inode);
	dget(dentry);

	return 0;
}

static int bpf_mkobj(struct inode *dir, struct dentry *dentry, umode_t mode,
		     dev_t devt)
{
	enum bpf_type type = MINOR(devt);

	if (MAJOR(devt) != UNNAMED_MAJOR || !S_ISREG(mode) ||
	    dentry->d_fsdata == NULL)
		return -EPERM;

	switch (type) {
	case BPF_TYPE_PROG:
		return bpf_mkobj_ops(dir, dentry, mode, &bpf_prog_iops);
	case BPF_TYPE_MAP:
		return bpf_mkobj_ops(dir, dentry, mode, &bpf_map_iops);
	default:
		return -EPERM;
	}
}

static const struct inode_operations bpf_dir_iops = {
	.lookup		= simple_lookup,
	.mknod		= bpf_mkobj,
	.mkdir		= bpf_mkdir,
	.rmdir		= simple_rmdir,
	.unlink		= simple_unlink,
};

static int bpf_obj_do_pin(const struct filename *pathname, void *raw,
			  enum bpf_type type)
{
	struct dentry *dentry;
	struct inode *dir;
	struct path path;
	umode_t mode;
	dev_t devt;
	int ret;

	dentry = kern_path_create(AT_FDCWD, pathname->name, &path, 0);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	mode = S_IFREG | ((S_IRUSR | S_IWUSR) & ~current_umask());
	devt = MKDEV(UNNAMED_MAJOR, type);

	ret = security_path_mknod(&path, dentry, mode, devt);
	if (ret)
		goto out;

	dir = d_inode(path.dentry);
	if (dir->i_op != &bpf_dir_iops) {
		ret = -EPERM;
		goto out;
	}

	dentry->d_fsdata = raw;
	ret = vfs_mknod(dir, dentry, mode, devt);
	dentry->d_fsdata = NULL;
out:
	done_path_create(&path, dentry);
	return ret;
}

int bpf_obj_pin_user(u32 ufd, const char __user *pathname)
{
	struct filename *pname;
	enum bpf_type type;
	void *raw;
	int ret;

	pname = getname(pathname);
	if (IS_ERR(pname))
		return PTR_ERR(pname);

	raw = bpf_fd_probe_obj(ufd, &type);
	if (IS_ERR(raw)) {
		ret = PTR_ERR(raw);
		goto out;
	}

	ret = bpf_obj_do_pin(pname, raw, type);
	if (ret != 0)
		bpf_any_put(raw, type);
out:
	putname(pname);
	return ret;
}

static void *bpf_obj_do_get(const struct filename *pathname,
			    enum bpf_type *type)
{
	struct inode *inode;
	struct path path;
	void *raw;
	int ret;

	ret = kern_path(pathname->name, LOOKUP_FOLLOW, &path);
	if (ret)
		return ERR_PTR(ret);

	inode = d_backing_inode(path.dentry);
	ret = inode_permission(inode, MAY_WRITE);
	if (ret)
		goto out;

	ret = bpf_inode_type(inode, type);
	if (ret)
		goto out;

	raw = bpf_any_get(inode->i_private, *type);
	if (!IS_ERR(raw))
		touch_atime(&path);

	path_put(&path);
	return raw;
out:
	path_put(&path);
	return ERR_PTR(ret);
}

int bpf_obj_get_user(const char __user *pathname)
{
	enum bpf_type type = BPF_TYPE_UNSPEC;
	struct filename *pname;
	int ret = -ENOENT;
	void *raw;

	pname = getname(pathname);
	if (IS_ERR(pname))
		return PTR_ERR(pname);

	raw = bpf_obj_do_get(pname, &type);
	if (IS_ERR(raw)) {
		ret = PTR_ERR(raw);
		goto out;
	}

	if (type == BPF_TYPE_PROG)
		ret = bpf_prog_new_fd(raw);
	else if (type == BPF_TYPE_MAP)
		ret = bpf_map_new_fd(raw);
	else
		goto out;

	if (ret < 0)
		bpf_any_put(raw, type);
out:
	putname(pname);
	return ret;
}

static void bpf_evict_inode(struct inode *inode)
{
	enum bpf_type type;

	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

	if (!bpf_inode_type(inode, &type))
		bpf_any_put(inode->i_private, type);
}

static const struct super_operations bpf_super_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= bpf_evict_inode,
};

static int bpf_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr bpf_rfiles[] = { { "" } };
	struct inode *inode;
	int ret;

	ret = simple_fill_super(sb, BPF_FS_MAGIC, bpf_rfiles);
	if (ret)
		return ret;

	sb->s_op = &bpf_super_ops;

	inode = sb->s_root->d_inode;
	inode->i_op = &bpf_dir_iops;
	inode->i_mode &= ~S_IALLUGO;
	inode->i_mode |= S_ISVTX | S_IRWXUGO;

	return 0;
}

static struct dentry *bpf_mount(struct file_system_type *type, int flags,
				const char *dev_name, void *data)
{
	return mount_nodev(type, flags, data, bpf_fill_super);
}

static struct file_system_type bpf_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "bpf",
	.mount		= bpf_mount,
	.kill_sb	= kill_litter_super,
	.fs_flags	= FS_USERNS_MOUNT,
};

MODULE_ALIAS_FS("bpf");

static int __init bpf_init(void)
{
	int ret;

	ret = sysfs_create_mount_point(fs_kobj, "bpf");
	if (ret)
		return ret;

	ret = register_filesystem(&bpf_fs_type);
	if (ret)
		sysfs_remove_mount_point(fs_kobj, "bpf");

	return ret;
}
fs_initcall(bpf_init);
