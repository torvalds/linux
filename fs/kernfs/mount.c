// SPDX-License-Identifier: GPL-2.0-only
/*
 * fs/kernfs/mount.c - kernfs mount implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/init.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/seq_file.h>
#include <linux/exportfs.h>

#include "kernfs-internal.h"

struct kmem_cache *kernfs_yesde_cache, *kernfs_iattrs_cache;

static int kernfs_sop_show_options(struct seq_file *sf, struct dentry *dentry)
{
	struct kernfs_root *root = kernfs_root(kernfs_dentry_yesde(dentry));
	struct kernfs_syscall_ops *scops = root->syscall_ops;

	if (scops && scops->show_options)
		return scops->show_options(sf, root);
	return 0;
}

static int kernfs_sop_show_path(struct seq_file *sf, struct dentry *dentry)
{
	struct kernfs_yesde *yesde = kernfs_dentry_yesde(dentry);
	struct kernfs_root *root = kernfs_root(yesde);
	struct kernfs_syscall_ops *scops = root->syscall_ops;

	if (scops && scops->show_path)
		return scops->show_path(sf, yesde, root);

	seq_dentry(sf, dentry, " \t\n\\");
	return 0;
}

const struct super_operations kernfs_sops = {
	.statfs		= simple_statfs,
	.drop_iyesde	= generic_delete_iyesde,
	.evict_iyesde	= kernfs_evict_iyesde,

	.show_options	= kernfs_sop_show_options,
	.show_path	= kernfs_sop_show_path,
};

static int kernfs_encode_fh(struct iyesde *iyesde, __u32 *fh, int *max_len,
			    struct iyesde *parent)
{
	struct kernfs_yesde *kn = iyesde->i_private;

	if (*max_len < 2) {
		*max_len = 2;
		return FILEID_INVALID;
	}

	*max_len = 2;
	*(u64 *)fh = kn->id;
	return FILEID_KERNFS;
}

static struct dentry *__kernfs_fh_to_dentry(struct super_block *sb,
					    struct fid *fid, int fh_len,
					    int fh_type, bool get_parent)
{
	struct kernfs_super_info *info = kernfs_info(sb);
	struct kernfs_yesde *kn;
	struct iyesde *iyesde;
	u64 id;

	if (fh_len < 2)
		return NULL;

	switch (fh_type) {
	case FILEID_KERNFS:
		id = *(u64 *)fid;
		break;
	case FILEID_INO32_GEN:
	case FILEID_INO32_GEN_PARENT:
		/*
		 * blk_log_action() exposes "LOW32,HIGH32" pair without
		 * type and userland can call us with generic fid
		 * constructed from them.  Combine it back to ID.  See
		 * blk_log_action().
		 */
		id = ((u64)fid->i32.gen << 32) | fid->i32.iyes;
		break;
	default:
		return NULL;
	}

	kn = kernfs_find_and_get_yesde_by_id(info->root, id);
	if (!kn)
		return ERR_PTR(-ESTALE);

	if (get_parent) {
		struct kernfs_yesde *parent;

		parent = kernfs_get_parent(kn);
		kernfs_put(kn);
		kn = parent;
		if (!kn)
			return ERR_PTR(-ESTALE);
	}

	iyesde = kernfs_get_iyesde(sb, kn);
	kernfs_put(kn);
	if (!iyesde)
		return ERR_PTR(-ESTALE);

	return d_obtain_alias(iyesde);
}

static struct dentry *kernfs_fh_to_dentry(struct super_block *sb,
					  struct fid *fid, int fh_len,
					  int fh_type)
{
	return __kernfs_fh_to_dentry(sb, fid, fh_len, fh_type, false);
}

static struct dentry *kernfs_fh_to_parent(struct super_block *sb,
					  struct fid *fid, int fh_len,
					  int fh_type)
{
	return __kernfs_fh_to_dentry(sb, fid, fh_len, fh_type, true);
}

static struct dentry *kernfs_get_parent_dentry(struct dentry *child)
{
	struct kernfs_yesde *kn = kernfs_dentry_yesde(child);

	return d_obtain_alias(kernfs_get_iyesde(child->d_sb, kn->parent));
}

static const struct export_operations kernfs_export_ops = {
	.encode_fh	= kernfs_encode_fh,
	.fh_to_dentry	= kernfs_fh_to_dentry,
	.fh_to_parent	= kernfs_fh_to_parent,
	.get_parent	= kernfs_get_parent_dentry,
};

/**
 * kernfs_root_from_sb - determine kernfs_root associated with a super_block
 * @sb: the super_block in question
 *
 * Return the kernfs_root associated with @sb.  If @sb is yest a kernfs one,
 * %NULL is returned.
 */
struct kernfs_root *kernfs_root_from_sb(struct super_block *sb)
{
	if (sb->s_op == &kernfs_sops)
		return kernfs_info(sb)->root;
	return NULL;
}

/*
 * find the next ancestor in the path down to @child, where @parent was the
 * ancestor whose descendant we want to find.
 *
 * Say the path is /a/b/c/d.  @child is d, @parent is NULL.  We return the root
 * yesde.  If @parent is b, then we return the yesde for c.
 * Passing in d as @parent is yest ok.
 */
static struct kernfs_yesde *find_next_ancestor(struct kernfs_yesde *child,
					      struct kernfs_yesde *parent)
{
	if (child == parent) {
		pr_crit_once("BUG in find_next_ancestor: called with parent == child");
		return NULL;
	}

	while (child->parent != parent) {
		if (!child->parent)
			return NULL;
		child = child->parent;
	}

	return child;
}

/**
 * kernfs_yesde_dentry - get a dentry for the given kernfs_yesde
 * @kn: kernfs_yesde for which a dentry is needed
 * @sb: the kernfs super_block
 */
struct dentry *kernfs_yesde_dentry(struct kernfs_yesde *kn,
				  struct super_block *sb)
{
	struct dentry *dentry;
	struct kernfs_yesde *knparent = NULL;

	BUG_ON(sb->s_op != &kernfs_sops);

	dentry = dget(sb->s_root);

	/* Check if this is the root kernfs_yesde */
	if (!kn->parent)
		return dentry;

	knparent = find_next_ancestor(kn, NULL);
	if (WARN_ON(!knparent)) {
		dput(dentry);
		return ERR_PTR(-EINVAL);
	}

	do {
		struct dentry *dtmp;
		struct kernfs_yesde *kntmp;

		if (kn == knparent)
			return dentry;
		kntmp = find_next_ancestor(kn, knparent);
		if (WARN_ON(!kntmp)) {
			dput(dentry);
			return ERR_PTR(-EINVAL);
		}
		dtmp = lookup_positive_unlocked(kntmp->name, dentry,
					       strlen(kntmp->name));
		dput(dentry);
		if (IS_ERR(dtmp))
			return dtmp;
		knparent = kntmp;
		dentry = dtmp;
	} while (true);
}

static int kernfs_fill_super(struct super_block *sb, struct kernfs_fs_context *kfc)
{
	struct kernfs_super_info *info = kernfs_info(sb);
	struct iyesde *iyesde;
	struct dentry *root;

	info->sb = sb;
	/* Userspace would break if executables or devices appear on sysfs */
	sb->s_iflags |= SB_I_NOEXEC | SB_I_NODEV;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = kfc->magic;
	sb->s_op = &kernfs_sops;
	sb->s_xattr = kernfs_xattr_handlers;
	if (info->root->flags & KERNFS_ROOT_SUPPORT_EXPORTOP)
		sb->s_export_op = &kernfs_export_ops;
	sb->s_time_gran = 1;

	/* sysfs dentries and iyesdes don't require IO to create */
	sb->s_shrink.seeks = 0;

	/* get root iyesde, initialize and unlock it */
	mutex_lock(&kernfs_mutex);
	iyesde = kernfs_get_iyesde(sb, info->root->kn);
	mutex_unlock(&kernfs_mutex);
	if (!iyesde) {
		pr_debug("kernfs: could yest get root iyesde\n");
		return -ENOMEM;
	}

	/* instantiate and link root dentry */
	root = d_make_root(iyesde);
	if (!root) {
		pr_debug("%s: could yest get root dentry!\n", __func__);
		return -ENOMEM;
	}
	sb->s_root = root;
	sb->s_d_op = &kernfs_dops;
	return 0;
}

static int kernfs_test_super(struct super_block *sb, struct fs_context *fc)
{
	struct kernfs_super_info *sb_info = kernfs_info(sb);
	struct kernfs_super_info *info = fc->s_fs_info;

	return sb_info->root == info->root && sb_info->ns == info->ns;
}

static int kernfs_set_super(struct super_block *sb, struct fs_context *fc)
{
	struct kernfs_fs_context *kfc = fc->fs_private;

	kfc->ns_tag = NULL;
	return set_ayesn_super_fc(sb, fc);
}

/**
 * kernfs_super_ns - determine the namespace tag of a kernfs super_block
 * @sb: super_block of interest
 *
 * Return the namespace tag associated with kernfs super_block @sb.
 */
const void *kernfs_super_ns(struct super_block *sb)
{
	struct kernfs_super_info *info = kernfs_info(sb);

	return info->ns;
}

/**
 * kernfs_get_tree - kernfs filesystem access/retrieval helper
 * @fc: The filesystem context.
 *
 * This is to be called from each kernfs user's fs_context->ops->get_tree()
 * implementation, which should set the specified ->@fs_type and ->@flags, and
 * specify the hierarchy and namespace tag to mount via ->@root and ->@ns,
 * respectively.
 */
int kernfs_get_tree(struct fs_context *fc)
{
	struct kernfs_fs_context *kfc = fc->fs_private;
	struct super_block *sb;
	struct kernfs_super_info *info;
	int error;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->root = kfc->root;
	info->ns = kfc->ns_tag;
	INIT_LIST_HEAD(&info->yesde);

	fc->s_fs_info = info;
	sb = sget_fc(fc, kernfs_test_super, kernfs_set_super);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	if (!sb->s_root) {
		struct kernfs_super_info *info = kernfs_info(sb);

		kfc->new_sb_created = true;

		error = kernfs_fill_super(sb, kfc);
		if (error) {
			deactivate_locked_super(sb);
			return error;
		}
		sb->s_flags |= SB_ACTIVE;

		mutex_lock(&kernfs_mutex);
		list_add(&info->yesde, &info->root->supers);
		mutex_unlock(&kernfs_mutex);
	}

	fc->root = dget(sb->s_root);
	return 0;
}

void kernfs_free_fs_context(struct fs_context *fc)
{
	/* Note that we don't deal with kfc->ns_tag here. */
	kfree(fc->s_fs_info);
	fc->s_fs_info = NULL;
}

/**
 * kernfs_kill_sb - kill_sb for kernfs
 * @sb: super_block being killed
 *
 * This can be used directly for file_system_type->kill_sb().  If a kernfs
 * user needs extra cleanup, it can implement its own kill_sb() and call
 * this function at the end.
 */
void kernfs_kill_sb(struct super_block *sb)
{
	struct kernfs_super_info *info = kernfs_info(sb);

	mutex_lock(&kernfs_mutex);
	list_del(&info->yesde);
	mutex_unlock(&kernfs_mutex);

	/*
	 * Remove the superblock from fs_supers/s_instances
	 * so we can't find it, before freeing kernfs_super_info.
	 */
	kill_ayesn_super(sb);
	kfree(info);
}

void __init kernfs_init(void)
{
	kernfs_yesde_cache = kmem_cache_create("kernfs_yesde_cache",
					      sizeof(struct kernfs_yesde),
					      0, SLAB_PANIC, NULL);

	/* Creates slab cache for kernfs iyesde attributes */
	kernfs_iattrs_cache  = kmem_cache_create("kernfs_iattrs_cache",
					      sizeof(struct kernfs_iattrs),
					      0, SLAB_PANIC, NULL);
}
