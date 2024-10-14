// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal file system backend for holding eBPF maps and programs,
 * used by bpf(2) object pinning.
 *
 * Authors:
 *
 *	Daniel Borkmann <daniel@iogearbox.net>
 */

#include <linux/init.h>
#include <linux/magic.h>
#include <linux/major.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/kdev_t.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/bpf_trace.h>
#include <linux/kstrtox.h>
#include "preload/bpf_preload.h"

enum bpf_type {
	BPF_TYPE_UNSPEC	= 0,
	BPF_TYPE_PROG,
	BPF_TYPE_MAP,
	BPF_TYPE_LINK,
};

static void *bpf_any_get(void *raw, enum bpf_type type)
{
	switch (type) {
	case BPF_TYPE_PROG:
		bpf_prog_inc(raw);
		break;
	case BPF_TYPE_MAP:
		bpf_map_inc_with_uref(raw);
		break;
	case BPF_TYPE_LINK:
		bpf_link_inc(raw);
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
	case BPF_TYPE_LINK:
		bpf_link_put(raw);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}
}

static void *bpf_fd_probe_obj(u32 ufd, enum bpf_type *type)
{
	void *raw;

	raw = bpf_map_get_with_uref(ufd);
	if (!IS_ERR(raw)) {
		*type = BPF_TYPE_MAP;
		return raw;
	}

	raw = bpf_prog_get(ufd);
	if (!IS_ERR(raw)) {
		*type = BPF_TYPE_PROG;
		return raw;
	}

	raw = bpf_link_get_from_fd(ufd);
	if (!IS_ERR(raw)) {
		*type = BPF_TYPE_LINK;
		return raw;
	}

	return ERR_PTR(-EINVAL);
}

static const struct inode_operations bpf_dir_iops;

static const struct inode_operations bpf_prog_iops = { };
static const struct inode_operations bpf_map_iops  = { };
static const struct inode_operations bpf_link_iops  = { };

struct inode *bpf_get_inode(struct super_block *sb,
			    const struct inode *dir,
			    umode_t mode)
{
	struct inode *inode;

	switch (mode & S_IFMT) {
	case S_IFDIR:
	case S_IFREG:
	case S_IFLNK:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOSPC);

	inode->i_ino = get_next_ino();
	simple_inode_init_ts(inode);

	inode_init_owner(&nop_mnt_idmap, inode, dir, mode);

	return inode;
}

static int bpf_inode_type(const struct inode *inode, enum bpf_type *type)
{
	*type = BPF_TYPE_UNSPEC;
	if (inode->i_op == &bpf_prog_iops)
		*type = BPF_TYPE_PROG;
	else if (inode->i_op == &bpf_map_iops)
		*type = BPF_TYPE_MAP;
	else if (inode->i_op == &bpf_link_iops)
		*type = BPF_TYPE_LINK;
	else
		return -EACCES;

	return 0;
}

static void bpf_dentry_finalize(struct dentry *dentry, struct inode *inode,
				struct inode *dir)
{
	d_instantiate(dentry, inode);
	dget(dentry);

	inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));
}

static int bpf_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		     struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	inode = bpf_get_inode(dir->i_sb, dir, mode | S_IFDIR);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &bpf_dir_iops;
	inode->i_fop = &simple_dir_operations;

	inc_nlink(inode);
	inc_nlink(dir);

	bpf_dentry_finalize(dentry, inode, dir);
	return 0;
}

struct map_iter {
	void *key;
	bool done;
};

static struct map_iter *map_iter(struct seq_file *m)
{
	return m->private;
}

static struct bpf_map *seq_file_to_map(struct seq_file *m)
{
	return file_inode(m->file)->i_private;
}

static void map_iter_free(struct map_iter *iter)
{
	if (iter) {
		kfree(iter->key);
		kfree(iter);
	}
}

static struct map_iter *map_iter_alloc(struct bpf_map *map)
{
	struct map_iter *iter;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL | __GFP_NOWARN);
	if (!iter)
		goto error;

	iter->key = kzalloc(map->key_size, GFP_KERNEL | __GFP_NOWARN);
	if (!iter->key)
		goto error;

	return iter;

error:
	map_iter_free(iter);
	return NULL;
}

static void *map_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct bpf_map *map = seq_file_to_map(m);
	void *key = map_iter(m)->key;
	void *prev_key;

	(*pos)++;
	if (map_iter(m)->done)
		return NULL;

	if (unlikely(v == SEQ_START_TOKEN))
		prev_key = NULL;
	else
		prev_key = key;

	rcu_read_lock();
	if (map->ops->map_get_next_key(map, prev_key, key)) {
		map_iter(m)->done = true;
		key = NULL;
	}
	rcu_read_unlock();
	return key;
}

static void *map_seq_start(struct seq_file *m, loff_t *pos)
{
	if (map_iter(m)->done)
		return NULL;

	return *pos ? map_iter(m)->key : SEQ_START_TOKEN;
}

static void map_seq_stop(struct seq_file *m, void *v)
{
}

static int map_seq_show(struct seq_file *m, void *v)
{
	struct bpf_map *map = seq_file_to_map(m);
	void *key = map_iter(m)->key;

	if (unlikely(v == SEQ_START_TOKEN)) {
		seq_puts(m, "# WARNING!! The output is for debug purpose only\n");
		seq_puts(m, "# WARNING!! The output format will change\n");
	} else {
		map->ops->map_seq_show_elem(map, key, m);
	}

	return 0;
}

static const struct seq_operations bpffs_map_seq_ops = {
	.start	= map_seq_start,
	.next	= map_seq_next,
	.show	= map_seq_show,
	.stop	= map_seq_stop,
};

static int bpffs_map_open(struct inode *inode, struct file *file)
{
	struct bpf_map *map = inode->i_private;
	struct map_iter *iter;
	struct seq_file *m;
	int err;

	iter = map_iter_alloc(map);
	if (!iter)
		return -ENOMEM;

	err = seq_open(file, &bpffs_map_seq_ops);
	if (err) {
		map_iter_free(iter);
		return err;
	}

	m = file->private_data;
	m->private = iter;

	return 0;
}

static int bpffs_map_release(struct inode *inode, struct file *file)
{
	struct seq_file *m = file->private_data;

	map_iter_free(map_iter(m));

	return seq_release(inode, file);
}

/* bpffs_map_fops should only implement the basic
 * read operation for a BPF map.  The purpose is to
 * provide a simple user intuitive way to do
 * "cat bpffs/pathto/a-pinned-map".
 *
 * Other operations (e.g. write, lookup...) should be realized by
 * the userspace tools (e.g. bpftool) through the
 * BPF_OBJ_GET_INFO_BY_FD and the map's lookup/update
 * interface.
 */
static const struct file_operations bpffs_map_fops = {
	.open		= bpffs_map_open,
	.read		= seq_read,
	.release	= bpffs_map_release,
};

static int bpffs_obj_open(struct inode *inode, struct file *file)
{
	return -EIO;
}

static const struct file_operations bpffs_obj_fops = {
	.open		= bpffs_obj_open,
};

static int bpf_mkobj_ops(struct dentry *dentry, umode_t mode, void *raw,
			 const struct inode_operations *iops,
			 const struct file_operations *fops)
{
	struct inode *dir = dentry->d_parent->d_inode;
	struct inode *inode = bpf_get_inode(dir->i_sb, dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = iops;
	inode->i_fop = fops;
	inode->i_private = raw;

	bpf_dentry_finalize(dentry, inode, dir);
	return 0;
}

static int bpf_mkprog(struct dentry *dentry, umode_t mode, void *arg)
{
	return bpf_mkobj_ops(dentry, mode, arg, &bpf_prog_iops,
			     &bpffs_obj_fops);
}

static int bpf_mkmap(struct dentry *dentry, umode_t mode, void *arg)
{
	struct bpf_map *map = arg;

	return bpf_mkobj_ops(dentry, mode, arg, &bpf_map_iops,
			     bpf_map_support_seq_show(map) ?
			     &bpffs_map_fops : &bpffs_obj_fops);
}

static int bpf_mklink(struct dentry *dentry, umode_t mode, void *arg)
{
	struct bpf_link *link = arg;

	return bpf_mkobj_ops(dentry, mode, arg, &bpf_link_iops,
			     bpf_link_is_iter(link) ?
			     &bpf_iter_fops : &bpffs_obj_fops);
}

static struct dentry *
bpf_lookup(struct inode *dir, struct dentry *dentry, unsigned flags)
{
	/* Dots in names (e.g. "/sys/fs/bpf/foo.bar") are reserved for future
	 * extensions. That allows popoulate_bpffs() create special files.
	 */
	if ((dir->i_mode & S_IALLUGO) &&
	    strchr(dentry->d_name.name, '.'))
		return ERR_PTR(-EPERM);

	return simple_lookup(dir, dentry, flags);
}

static int bpf_symlink(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, const char *target)
{
	char *link = kstrdup(target, GFP_USER | __GFP_NOWARN);
	struct inode *inode;

	if (!link)
		return -ENOMEM;

	inode = bpf_get_inode(dir->i_sb, dir, S_IRWXUGO | S_IFLNK);
	if (IS_ERR(inode)) {
		kfree(link);
		return PTR_ERR(inode);
	}

	inode->i_op = &simple_symlink_inode_operations;
	inode->i_link = link;

	bpf_dentry_finalize(dentry, inode, dir);
	return 0;
}

static const struct inode_operations bpf_dir_iops = {
	.lookup		= bpf_lookup,
	.mkdir		= bpf_mkdir,
	.symlink	= bpf_symlink,
	.rmdir		= simple_rmdir,
	.rename		= simple_rename,
	.link		= simple_link,
	.unlink		= simple_unlink,
};

/* pin iterator link into bpffs */
static int bpf_iter_link_pin_kernel(struct dentry *parent,
				    const char *name, struct bpf_link *link)
{
	umode_t mode = S_IFREG | S_IRUSR;
	struct dentry *dentry;
	int ret;

	inode_lock(parent->d_inode);
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry)) {
		inode_unlock(parent->d_inode);
		return PTR_ERR(dentry);
	}
	ret = bpf_mkobj_ops(dentry, mode, link, &bpf_link_iops,
			    &bpf_iter_fops);
	dput(dentry);
	inode_unlock(parent->d_inode);
	return ret;
}

static int bpf_obj_do_pin(int path_fd, const char __user *pathname, void *raw,
			  enum bpf_type type)
{
	struct dentry *dentry;
	struct inode *dir;
	struct path path;
	umode_t mode;
	int ret;

	dentry = user_path_create(path_fd, pathname, &path, 0);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	dir = d_inode(path.dentry);
	if (dir->i_op != &bpf_dir_iops) {
		ret = -EPERM;
		goto out;
	}

	mode = S_IFREG | ((S_IRUSR | S_IWUSR) & ~current_umask());
	ret = security_path_mknod(&path, dentry, mode, 0);
	if (ret)
		goto out;

	switch (type) {
	case BPF_TYPE_PROG:
		ret = vfs_mkobj(dentry, mode, bpf_mkprog, raw);
		break;
	case BPF_TYPE_MAP:
		ret = vfs_mkobj(dentry, mode, bpf_mkmap, raw);
		break;
	case BPF_TYPE_LINK:
		ret = vfs_mkobj(dentry, mode, bpf_mklink, raw);
		break;
	default:
		ret = -EPERM;
	}
out:
	done_path_create(&path, dentry);
	return ret;
}

int bpf_obj_pin_user(u32 ufd, int path_fd, const char __user *pathname)
{
	enum bpf_type type;
	void *raw;
	int ret;

	raw = bpf_fd_probe_obj(ufd, &type);
	if (IS_ERR(raw))
		return PTR_ERR(raw);

	ret = bpf_obj_do_pin(path_fd, pathname, raw, type);
	if (ret != 0)
		bpf_any_put(raw, type);

	return ret;
}

static void *bpf_obj_do_get(int path_fd, const char __user *pathname,
			    enum bpf_type *type, int flags)
{
	struct inode *inode;
	struct path path;
	void *raw;
	int ret;

	ret = user_path_at(path_fd, pathname, LOOKUP_FOLLOW, &path);
	if (ret)
		return ERR_PTR(ret);

	inode = d_backing_inode(path.dentry);
	ret = path_permission(&path, ACC_MODE(flags));
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

int bpf_obj_get_user(int path_fd, const char __user *pathname, int flags)
{
	enum bpf_type type = BPF_TYPE_UNSPEC;
	int f_flags;
	void *raw;
	int ret;

	f_flags = bpf_get_file_flag(flags);
	if (f_flags < 0)
		return f_flags;

	raw = bpf_obj_do_get(path_fd, pathname, &type, f_flags);
	if (IS_ERR(raw))
		return PTR_ERR(raw);

	if (type == BPF_TYPE_PROG)
		ret = bpf_prog_new_fd(raw);
	else if (type == BPF_TYPE_MAP)
		ret = bpf_map_new_fd(raw, f_flags);
	else if (type == BPF_TYPE_LINK)
		ret = (f_flags != O_RDWR) ? -EINVAL : bpf_link_new_fd(raw);
	else
		return -ENOENT;

	if (ret < 0)
		bpf_any_put(raw, type);
	return ret;
}

static struct bpf_prog *__get_prog_inode(struct inode *inode, enum bpf_prog_type type)
{
	struct bpf_prog *prog;
	int ret = inode_permission(&nop_mnt_idmap, inode, MAY_READ);
	if (ret)
		return ERR_PTR(ret);

	if (inode->i_op == &bpf_map_iops)
		return ERR_PTR(-EINVAL);
	if (inode->i_op == &bpf_link_iops)
		return ERR_PTR(-EINVAL);
	if (inode->i_op != &bpf_prog_iops)
		return ERR_PTR(-EACCES);

	prog = inode->i_private;

	ret = security_bpf_prog(prog);
	if (ret < 0)
		return ERR_PTR(ret);

	if (!bpf_prog_get_ok(prog, &type, false))
		return ERR_PTR(-EINVAL);

	bpf_prog_inc(prog);
	return prog;
}

struct bpf_prog *bpf_prog_get_type_path(const char *name, enum bpf_prog_type type)
{
	struct bpf_prog *prog;
	struct path path;
	int ret = kern_path(name, LOOKUP_FOLLOW, &path);
	if (ret)
		return ERR_PTR(ret);
	prog = __get_prog_inode(d_backing_inode(path.dentry), type);
	if (!IS_ERR(prog))
		touch_atime(&path);
	path_put(&path);
	return prog;
}
EXPORT_SYMBOL(bpf_prog_get_type_path);

struct bpffs_btf_enums {
	const struct btf *btf;
	const struct btf_type *cmd_t;
	const struct btf_type *map_t;
	const struct btf_type *prog_t;
	const struct btf_type *attach_t;
};

static int find_bpffs_btf_enums(struct bpffs_btf_enums *info)
{
	const struct btf *btf;
	const struct btf_type *t;
	const char *name;
	int i, n;

	memset(info, 0, sizeof(*info));

	btf = bpf_get_btf_vmlinux();
	if (IS_ERR(btf))
		return PTR_ERR(btf);
	if (!btf)
		return -ENOENT;

	info->btf = btf;

	for (i = 1, n = btf_nr_types(btf); i < n; i++) {
		t = btf_type_by_id(btf, i);
		if (!btf_type_is_enum(t))
			continue;

		name = btf_name_by_offset(btf, t->name_off);
		if (!name)
			continue;

		if (strcmp(name, "bpf_cmd") == 0)
			info->cmd_t = t;
		else if (strcmp(name, "bpf_map_type") == 0)
			info->map_t = t;
		else if (strcmp(name, "bpf_prog_type") == 0)
			info->prog_t = t;
		else if (strcmp(name, "bpf_attach_type") == 0)
			info->attach_t = t;
		else
			continue;

		if (info->cmd_t && info->map_t && info->prog_t && info->attach_t)
			return 0;
	}

	return -ESRCH;
}

static bool find_btf_enum_const(const struct btf *btf, const struct btf_type *enum_t,
				const char *prefix, const char *str, int *value)
{
	const struct btf_enum *e;
	const char *name;
	int i, n, pfx_len = strlen(prefix);

	*value = 0;

	if (!btf || !enum_t)
		return false;

	for (i = 0, n = btf_vlen(enum_t); i < n; i++) {
		e = &btf_enum(enum_t)[i];

		name = btf_name_by_offset(btf, e->name_off);
		if (!name || strncasecmp(name, prefix, pfx_len) != 0)
			continue;

		/* match symbolic name case insensitive and ignoring prefix */
		if (strcasecmp(name + pfx_len, str) == 0) {
			*value = e->val;
			return true;
		}
	}

	return false;
}

static void seq_print_delegate_opts(struct seq_file *m,
				    const char *opt_name,
				    const struct btf *btf,
				    const struct btf_type *enum_t,
				    const char *prefix,
				    u64 delegate_msk, u64 any_msk)
{
	const struct btf_enum *e;
	bool first = true;
	const char *name;
	u64 msk;
	int i, n, pfx_len = strlen(prefix);

	delegate_msk &= any_msk; /* clear unknown bits */

	if (delegate_msk == 0)
		return;

	seq_printf(m, ",%s", opt_name);
	if (delegate_msk == any_msk) {
		seq_printf(m, "=any");
		return;
	}

	if (btf && enum_t) {
		for (i = 0, n = btf_vlen(enum_t); i < n; i++) {
			e = &btf_enum(enum_t)[i];
			name = btf_name_by_offset(btf, e->name_off);
			if (!name || strncasecmp(name, prefix, pfx_len) != 0)
				continue;
			msk = 1ULL << e->val;
			if (delegate_msk & msk) {
				/* emit lower-case name without prefix */
				seq_putc(m, first ? '=' : ':');
				name += pfx_len;
				while (*name) {
					seq_putc(m, tolower(*name));
					name++;
				}

				delegate_msk &= ~msk;
				first = false;
			}
		}
	}
	if (delegate_msk)
		seq_printf(m, "%c0x%llx", first ? '=' : ':', delegate_msk);
}

/*
 * Display the mount options in /proc/mounts.
 */
static int bpf_show_options(struct seq_file *m, struct dentry *root)
{
	struct inode *inode = d_inode(root);
	umode_t mode = inode->i_mode & S_IALLUGO & ~S_ISVTX;
	struct bpf_mount_opts *opts = root->d_sb->s_fs_info;
	u64 mask;

	if (!uid_eq(inode->i_uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			   from_kuid_munged(&init_user_ns, inode->i_uid));
	if (!gid_eq(inode->i_gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			   from_kgid_munged(&init_user_ns, inode->i_gid));
	if (mode != S_IRWXUGO)
		seq_printf(m, ",mode=%o", mode);

	if (opts->delegate_cmds || opts->delegate_maps ||
	    opts->delegate_progs || opts->delegate_attachs) {
		struct bpffs_btf_enums info;

		/* ignore errors, fallback to hex */
		(void)find_bpffs_btf_enums(&info);

		mask = (1ULL << __MAX_BPF_CMD) - 1;
		seq_print_delegate_opts(m, "delegate_cmds",
					info.btf, info.cmd_t, "BPF_",
					opts->delegate_cmds, mask);

		mask = (1ULL << __MAX_BPF_MAP_TYPE) - 1;
		seq_print_delegate_opts(m, "delegate_maps",
					info.btf, info.map_t, "BPF_MAP_TYPE_",
					opts->delegate_maps, mask);

		mask = (1ULL << __MAX_BPF_PROG_TYPE) - 1;
		seq_print_delegate_opts(m, "delegate_progs",
					info.btf, info.prog_t, "BPF_PROG_TYPE_",
					opts->delegate_progs, mask);

		mask = (1ULL << __MAX_BPF_ATTACH_TYPE) - 1;
		seq_print_delegate_opts(m, "delegate_attachs",
					info.btf, info.attach_t, "BPF_",
					opts->delegate_attachs, mask);
	}

	return 0;
}

static void bpf_free_inode(struct inode *inode)
{
	enum bpf_type type;

	if (S_ISLNK(inode->i_mode))
		kfree(inode->i_link);
	if (!bpf_inode_type(inode, &type))
		bpf_any_put(inode->i_private, type);
	free_inode_nonrcu(inode);
}

const struct super_operations bpf_super_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= bpf_show_options,
	.free_inode	= bpf_free_inode,
};

enum {
	OPT_UID,
	OPT_GID,
	OPT_MODE,
	OPT_DELEGATE_CMDS,
	OPT_DELEGATE_MAPS,
	OPT_DELEGATE_PROGS,
	OPT_DELEGATE_ATTACHS,
};

static const struct fs_parameter_spec bpf_fs_parameters[] = {
	fsparam_u32	("uid",				OPT_UID),
	fsparam_u32	("gid",				OPT_GID),
	fsparam_u32oct	("mode",			OPT_MODE),
	fsparam_string	("delegate_cmds",		OPT_DELEGATE_CMDS),
	fsparam_string	("delegate_maps",		OPT_DELEGATE_MAPS),
	fsparam_string	("delegate_progs",		OPT_DELEGATE_PROGS),
	fsparam_string	("delegate_attachs",		OPT_DELEGATE_ATTACHS),
	{}
};

static int bpf_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct bpf_mount_opts *opts = fc->s_fs_info;
	struct fs_parse_result result;
	kuid_t uid;
	kgid_t gid;
	int opt, err;

	opt = fs_parse(fc, bpf_fs_parameters, param, &result);
	if (opt < 0) {
		/* We might like to report bad mount options here, but
		 * traditionally we've ignored all mount options, so we'd
		 * better continue to ignore non-existing options for bpf.
		 */
		if (opt == -ENOPARAM) {
			opt = vfs_parse_fs_param_source(fc, param);
			if (opt != -ENOPARAM)
				return opt;

			return 0;
		}

		if (opt < 0)
			return opt;
	}

	switch (opt) {
	case OPT_UID:
		uid = make_kuid(current_user_ns(), result.uint_32);
		if (!uid_valid(uid))
			goto bad_value;

		/*
		 * The requested uid must be representable in the
		 * filesystem's idmapping.
		 */
		if (!kuid_has_mapping(fc->user_ns, uid))
			goto bad_value;

		opts->uid = uid;
		break;
	case OPT_GID:
		gid = make_kgid(current_user_ns(), result.uint_32);
		if (!gid_valid(gid))
			goto bad_value;

		/*
		 * The requested gid must be representable in the
		 * filesystem's idmapping.
		 */
		if (!kgid_has_mapping(fc->user_ns, gid))
			goto bad_value;

		opts->gid = gid;
		break;
	case OPT_MODE:
		opts->mode = result.uint_32 & S_IALLUGO;
		break;
	case OPT_DELEGATE_CMDS:
	case OPT_DELEGATE_MAPS:
	case OPT_DELEGATE_PROGS:
	case OPT_DELEGATE_ATTACHS: {
		struct bpffs_btf_enums info;
		const struct btf_type *enum_t;
		const char *enum_pfx;
		u64 *delegate_msk, msk = 0;
		char *p;
		int val;

		/* ignore errors, fallback to hex */
		(void)find_bpffs_btf_enums(&info);

		switch (opt) {
		case OPT_DELEGATE_CMDS:
			delegate_msk = &opts->delegate_cmds;
			enum_t = info.cmd_t;
			enum_pfx = "BPF_";
			break;
		case OPT_DELEGATE_MAPS:
			delegate_msk = &opts->delegate_maps;
			enum_t = info.map_t;
			enum_pfx = "BPF_MAP_TYPE_";
			break;
		case OPT_DELEGATE_PROGS:
			delegate_msk = &opts->delegate_progs;
			enum_t = info.prog_t;
			enum_pfx = "BPF_PROG_TYPE_";
			break;
		case OPT_DELEGATE_ATTACHS:
			delegate_msk = &opts->delegate_attachs;
			enum_t = info.attach_t;
			enum_pfx = "BPF_";
			break;
		default:
			return -EINVAL;
		}

		while ((p = strsep(&param->string, ":"))) {
			if (strcmp(p, "any") == 0) {
				msk |= ~0ULL;
			} else if (find_btf_enum_const(info.btf, enum_t, enum_pfx, p, &val)) {
				msk |= 1ULL << val;
			} else {
				err = kstrtou64(p, 0, &msk);
				if (err)
					return err;
			}
		}

		/* Setting delegation mount options requires privileges */
		if (msk && !capable(CAP_SYS_ADMIN))
			return -EPERM;

		*delegate_msk |= msk;
		break;
	}
	default:
		/* ignore unknown mount options */
		break;
	}

	return 0;
bad_value:
	return invalfc(fc, "Bad value for '%s'", param->key);
}

struct bpf_preload_ops *bpf_preload_ops;
EXPORT_SYMBOL_GPL(bpf_preload_ops);

static bool bpf_preload_mod_get(void)
{
	/* If bpf_preload.ko wasn't loaded earlier then load it now.
	 * When bpf_preload is built into vmlinux the module's __init
	 * function will populate it.
	 */
	if (!bpf_preload_ops) {
		request_module("bpf_preload");
		if (!bpf_preload_ops)
			return false;
	}
	/* And grab the reference, so the module doesn't disappear while the
	 * kernel is interacting with the kernel module and its UMD.
	 */
	if (!try_module_get(bpf_preload_ops->owner)) {
		pr_err("bpf_preload module get failed.\n");
		return false;
	}
	return true;
}

static void bpf_preload_mod_put(void)
{
	if (bpf_preload_ops)
		/* now user can "rmmod bpf_preload" if necessary */
		module_put(bpf_preload_ops->owner);
}

static DEFINE_MUTEX(bpf_preload_lock);

static int populate_bpffs(struct dentry *parent)
{
	struct bpf_preload_info objs[BPF_PRELOAD_LINKS] = {};
	int err = 0, i;

	/* grab the mutex to make sure the kernel interactions with bpf_preload
	 * are serialized
	 */
	mutex_lock(&bpf_preload_lock);

	/* if bpf_preload.ko wasn't built into vmlinux then load it */
	if (!bpf_preload_mod_get())
		goto out;

	err = bpf_preload_ops->preload(objs);
	if (err)
		goto out_put;
	for (i = 0; i < BPF_PRELOAD_LINKS; i++) {
		bpf_link_inc(objs[i].link);
		err = bpf_iter_link_pin_kernel(parent,
					       objs[i].link_name, objs[i].link);
		if (err) {
			bpf_link_put(objs[i].link);
			goto out_put;
		}
	}
out_put:
	bpf_preload_mod_put();
out:
	mutex_unlock(&bpf_preload_lock);
	return err;
}

static int bpf_fill_super(struct super_block *sb, struct fs_context *fc)
{
	static const struct tree_descr bpf_rfiles[] = { { "" } };
	struct bpf_mount_opts *opts = sb->s_fs_info;
	struct inode *inode;
	int ret;

	/* Mounting an instance of BPF FS requires privileges */
	if (fc->user_ns != &init_user_ns && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = simple_fill_super(sb, BPF_FS_MAGIC, bpf_rfiles);
	if (ret)
		return ret;

	sb->s_op = &bpf_super_ops;

	inode = sb->s_root->d_inode;
	inode->i_uid = opts->uid;
	inode->i_gid = opts->gid;
	inode->i_op = &bpf_dir_iops;
	inode->i_mode &= ~S_IALLUGO;
	populate_bpffs(sb->s_root);
	inode->i_mode |= S_ISVTX | opts->mode;
	return 0;
}

static int bpf_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, bpf_fill_super);
}

static void bpf_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations bpf_context_ops = {
	.free		= bpf_free_fc,
	.parse_param	= bpf_parse_param,
	.get_tree	= bpf_get_tree,
};

/*
 * Set up the filesystem mount context.
 */
static int bpf_init_fs_context(struct fs_context *fc)
{
	struct bpf_mount_opts *opts;

	opts = kzalloc(sizeof(struct bpf_mount_opts), GFP_KERNEL);
	if (!opts)
		return -ENOMEM;

	opts->mode = S_IRWXUGO;
	opts->uid = current_fsuid();
	opts->gid = current_fsgid();

	/* start out with no BPF token delegation enabled */
	opts->delegate_cmds = 0;
	opts->delegate_maps = 0;
	opts->delegate_progs = 0;
	opts->delegate_attachs = 0;

	fc->s_fs_info = opts;
	fc->ops = &bpf_context_ops;
	return 0;
}

static void bpf_kill_super(struct super_block *sb)
{
	struct bpf_mount_opts *opts = sb->s_fs_info;

	kill_litter_super(sb);
	kfree(opts);
}

static struct file_system_type bpf_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "bpf",
	.init_fs_context = bpf_init_fs_context,
	.parameters	= bpf_fs_parameters,
	.kill_sb	= bpf_kill_super,
	.fs_flags	= FS_USERNS_MOUNT,
};

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
