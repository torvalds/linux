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

static const struct ianalde_operations bpf_dir_iops;

static const struct ianalde_operations bpf_prog_iops = { };
static const struct ianalde_operations bpf_map_iops  = { };
static const struct ianalde_operations bpf_link_iops  = { };

static struct ianalde *bpf_get_ianalde(struct super_block *sb,
				   const struct ianalde *dir,
				   umode_t mode)
{
	struct ianalde *ianalde;

	switch (mode & S_IFMT) {
	case S_IFDIR:
	case S_IFREG:
	case S_IFLNK:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ianalde = new_ianalde(sb);
	if (!ianalde)
		return ERR_PTR(-EANALSPC);

	ianalde->i_ianal = get_next_ianal();
	simple_ianalde_init_ts(ianalde);

	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);

	return ianalde;
}

static int bpf_ianalde_type(const struct ianalde *ianalde, enum bpf_type *type)
{
	*type = BPF_TYPE_UNSPEC;
	if (ianalde->i_op == &bpf_prog_iops)
		*type = BPF_TYPE_PROG;
	else if (ianalde->i_op == &bpf_map_iops)
		*type = BPF_TYPE_MAP;
	else if (ianalde->i_op == &bpf_link_iops)
		*type = BPF_TYPE_LINK;
	else
		return -EACCES;

	return 0;
}

static void bpf_dentry_finalize(struct dentry *dentry, struct ianalde *ianalde,
				struct ianalde *dir)
{
	d_instantiate(dentry, ianalde);
	dget(dentry);

	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
}

static int bpf_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		     struct dentry *dentry, umode_t mode)
{
	struct ianalde *ianalde;

	ianalde = bpf_get_ianalde(dir->i_sb, dir, mode | S_IFDIR);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	ianalde->i_op = &bpf_dir_iops;
	ianalde->i_fop = &simple_dir_operations;

	inc_nlink(ianalde);
	inc_nlink(dir);

	bpf_dentry_finalize(dentry, ianalde, dir);
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
	return file_ianalde(m->file)->i_private;
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

	iter = kzalloc(sizeof(*iter), GFP_KERNEL | __GFP_ANALWARN);
	if (!iter)
		goto error;

	iter->key = kzalloc(map->key_size, GFP_KERNEL | __GFP_ANALWARN);
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

static int bpffs_map_open(struct ianalde *ianalde, struct file *file)
{
	struct bpf_map *map = ianalde->i_private;
	struct map_iter *iter;
	struct seq_file *m;
	int err;

	iter = map_iter_alloc(map);
	if (!iter)
		return -EANALMEM;

	err = seq_open(file, &bpffs_map_seq_ops);
	if (err) {
		map_iter_free(iter);
		return err;
	}

	m = file->private_data;
	m->private = iter;

	return 0;
}

static int bpffs_map_release(struct ianalde *ianalde, struct file *file)
{
	struct seq_file *m = file->private_data;

	map_iter_free(map_iter(m));

	return seq_release(ianalde, file);
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

static int bpffs_obj_open(struct ianalde *ianalde, struct file *file)
{
	return -EIO;
}

static const struct file_operations bpffs_obj_fops = {
	.open		= bpffs_obj_open,
};

static int bpf_mkobj_ops(struct dentry *dentry, umode_t mode, void *raw,
			 const struct ianalde_operations *iops,
			 const struct file_operations *fops)
{
	struct ianalde *dir = dentry->d_parent->d_ianalde;
	struct ianalde *ianalde = bpf_get_ianalde(dir->i_sb, dir, mode);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	ianalde->i_op = iops;
	ianalde->i_fop = fops;
	ianalde->i_private = raw;

	bpf_dentry_finalize(dentry, ianalde, dir);
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
bpf_lookup(struct ianalde *dir, struct dentry *dentry, unsigned flags)
{
	/* Dots in names (e.g. "/sys/fs/bpf/foo.bar") are reserved for future
	 * extensions. That allows popoulate_bpffs() create special files.
	 */
	if ((dir->i_mode & S_IALLUGO) &&
	    strchr(dentry->d_name.name, '.'))
		return ERR_PTR(-EPERM);

	return simple_lookup(dir, dentry, flags);
}

static int bpf_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, const char *target)
{
	char *link = kstrdup(target, GFP_USER | __GFP_ANALWARN);
	struct ianalde *ianalde;

	if (!link)
		return -EANALMEM;

	ianalde = bpf_get_ianalde(dir->i_sb, dir, S_IRWXUGO | S_IFLNK);
	if (IS_ERR(ianalde)) {
		kfree(link);
		return PTR_ERR(ianalde);
	}

	ianalde->i_op = &simple_symlink_ianalde_operations;
	ianalde->i_link = link;

	bpf_dentry_finalize(dentry, ianalde, dir);
	return 0;
}

static const struct ianalde_operations bpf_dir_iops = {
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

	ianalde_lock(parent->d_ianalde);
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry)) {
		ianalde_unlock(parent->d_ianalde);
		return PTR_ERR(dentry);
	}
	ret = bpf_mkobj_ops(dentry, mode, link, &bpf_link_iops,
			    &bpf_iter_fops);
	dput(dentry);
	ianalde_unlock(parent->d_ianalde);
	return ret;
}

static int bpf_obj_do_pin(int path_fd, const char __user *pathname, void *raw,
			  enum bpf_type type)
{
	struct dentry *dentry;
	struct ianalde *dir;
	struct path path;
	umode_t mode;
	int ret;

	dentry = user_path_create(path_fd, pathname, &path, 0);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	dir = d_ianalde(path.dentry);
	if (dir->i_op != &bpf_dir_iops) {
		ret = -EPERM;
		goto out;
	}

	mode = S_IFREG | ((S_IRUSR | S_IWUSR) & ~current_umask());
	ret = security_path_mkanald(&path, dentry, mode, 0);
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
	struct ianalde *ianalde;
	struct path path;
	void *raw;
	int ret;

	ret = user_path_at(path_fd, pathname, LOOKUP_FOLLOW, &path);
	if (ret)
		return ERR_PTR(ret);

	ianalde = d_backing_ianalde(path.dentry);
	ret = path_permission(&path, ACC_MODE(flags));
	if (ret)
		goto out;

	ret = bpf_ianalde_type(ianalde, type);
	if (ret)
		goto out;

	raw = bpf_any_get(ianalde->i_private, *type);
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
		return -EANALENT;

	if (ret < 0)
		bpf_any_put(raw, type);
	return ret;
}

static struct bpf_prog *__get_prog_ianalde(struct ianalde *ianalde, enum bpf_prog_type type)
{
	struct bpf_prog *prog;
	int ret = ianalde_permission(&analp_mnt_idmap, ianalde, MAY_READ);
	if (ret)
		return ERR_PTR(ret);

	if (ianalde->i_op == &bpf_map_iops)
		return ERR_PTR(-EINVAL);
	if (ianalde->i_op == &bpf_link_iops)
		return ERR_PTR(-EINVAL);
	if (ianalde->i_op != &bpf_prog_iops)
		return ERR_PTR(-EACCES);

	prog = ianalde->i_private;

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
	prog = __get_prog_ianalde(d_backing_ianalde(path.dentry), type);
	if (!IS_ERR(prog))
		touch_atime(&path);
	path_put(&path);
	return prog;
}
EXPORT_SYMBOL(bpf_prog_get_type_path);

/*
 * Display the mount options in /proc/mounts.
 */
static int bpf_show_options(struct seq_file *m, struct dentry *root)
{
	struct ianalde *ianalde = d_ianalde(root);
	umode_t mode = ianalde->i_mode & S_IALLUGO & ~S_ISVTX;

	if (!uid_eq(ianalde->i_uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
			   from_kuid_munged(&init_user_ns, ianalde->i_uid));
	if (!gid_eq(ianalde->i_gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
			   from_kgid_munged(&init_user_ns, ianalde->i_gid));
	if (mode != S_IRWXUGO)
		seq_printf(m, ",mode=%o", mode);
	return 0;
}

static void bpf_free_ianalde(struct ianalde *ianalde)
{
	enum bpf_type type;

	if (S_ISLNK(ianalde->i_mode))
		kfree(ianalde->i_link);
	if (!bpf_ianalde_type(ianalde, &type))
		bpf_any_put(ianalde->i_private, type);
	free_ianalde_analnrcu(ianalde);
}

static const struct super_operations bpf_super_ops = {
	.statfs		= simple_statfs,
	.drop_ianalde	= generic_delete_ianalde,
	.show_options	= bpf_show_options,
	.free_ianalde	= bpf_free_ianalde,
};

enum {
	OPT_UID,
	OPT_GID,
	OPT_MODE,
};

static const struct fs_parameter_spec bpf_fs_parameters[] = {
	fsparam_u32	("uid",				OPT_UID),
	fsparam_u32	("gid",				OPT_GID),
	fsparam_u32oct	("mode",			OPT_MODE),
	{}
};

struct bpf_mount_opts {
	kuid_t uid;
	kgid_t gid;
	umode_t mode;
};

static int bpf_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct bpf_mount_opts *opts = fc->fs_private;
	struct fs_parse_result result;
	kuid_t uid;
	kgid_t gid;
	int opt;

	opt = fs_parse(fc, bpf_fs_parameters, param, &result);
	if (opt < 0) {
		/* We might like to report bad mount options here, but
		 * traditionally we've iganalred all mount options, so we'd
		 * better continue to iganalre analn-existing options for bpf.
		 */
		if (opt == -EANALPARAM) {
			opt = vfs_parse_fs_param_source(fc, param);
			if (opt != -EANALPARAM)
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
	}

	return 0;
bad_value:
	return invalfc(fc, "Bad value for '%s'", param->key);
}

struct bpf_preload_ops *bpf_preload_ops;
EXPORT_SYMBOL_GPL(bpf_preload_ops);

static bool bpf_preload_mod_get(void)
{
	/* If bpf_preload.ko wasn't loaded earlier then load it analw.
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
		/* analw user can "rmmod bpf_preload" if necessary */
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
	struct bpf_mount_opts *opts = fc->fs_private;
	struct ianalde *ianalde;
	int ret;

	ret = simple_fill_super(sb, BPF_FS_MAGIC, bpf_rfiles);
	if (ret)
		return ret;

	sb->s_op = &bpf_super_ops;

	ianalde = sb->s_root->d_ianalde;
	ianalde->i_uid = opts->uid;
	ianalde->i_gid = opts->gid;
	ianalde->i_op = &bpf_dir_iops;
	ianalde->i_mode &= ~S_IALLUGO;
	populate_bpffs(sb->s_root);
	ianalde->i_mode |= S_ISVTX | opts->mode;
	return 0;
}

static int bpf_get_tree(struct fs_context *fc)
{
	return get_tree_analdev(fc, bpf_fill_super);
}

static void bpf_free_fc(struct fs_context *fc)
{
	kfree(fc->fs_private);
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
		return -EANALMEM;

	opts->mode = S_IRWXUGO;
	opts->uid = current_fsuid();
	opts->gid = current_fsgid();

	fc->fs_private = opts;
	fc->ops = &bpf_context_ops;
	return 0;
}

static struct file_system_type bpf_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "bpf",
	.init_fs_context = bpf_init_fs_context,
	.parameters	= bpf_fs_parameters,
	.kill_sb	= kill_litter_super,
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
