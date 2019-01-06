/*
 * fs/kernfs/mount.c - kernfs mount implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2.
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

struct kmem_cache *kernfs_node_cache;

static int kernfs_sop_remount_fs(struct super_block *sb, int *flags, char *data)
{
	struct kernfs_root *root = kernfs_info(sb)->root;
	struct kernfs_syscall_ops *scops = root->syscall_ops;

	if (scops && scops->remount_fs)
		return scops->remount_fs(root, flags, data);
	return 0;
}

static int kernfs_sop_show_options(struct seq_file *sf, struct dentry *dentry)
{
	struct kernfs_root *root = kernfs_root(kernfs_dentry_node(dentry));
	struct kernfs_syscall_ops *scops = root->syscall_ops;

	if (scops && scops->show_options)
		return scops->show_options(sf, root);
	return 0;
}

static int kernfs_sop_show_path(struct seq_file *sf, struct dentry *dentry)
{
	struct kernfs_node *node = kernfs_dentry_node(dentry);
	struct kernfs_root *root = kernfs_root(node);
	struct kernfs_syscall_ops *scops = root->syscall_ops;

	if (scops && scops->show_path)
		return scops->show_path(sf, node, root);

	seq_dentry(sf, dentry, " \t\n\\");
	return 0;
}

const struct super_operations kernfs_sops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= kernfs_evict_inode,

	.remount_fs	= kernfs_sop_remount_fs,
	.show_options	= kernfs_sop_show_options,
	.show_path	= kernfs_sop_show_path,
};

/*
 * Similar to kernfs_fh_get_inode, this one gets kernfs node from inode
 * number and generation
 */
struct kernfs_node *kernfs_get_node_by_id(struct kernfs_root *root,
	const union kernfs_node_id *id)
{
	struct kernfs_node *kn;

	kn = kernfs_find_and_get_node_by_ino(root, id->ino);
	if (!kn)
		return NULL;
	if (kn->id.generation != id->generation) {
		kernfs_put(kn);
		return NULL;
	}
	return kn;
}

static struct inode *kernfs_fh_get_inode(struct super_block *sb,
		u64 ino, u32 generation)
{
	struct kernfs_super_info *info = kernfs_info(sb);
	struct inode *inode;
	struct kernfs_node *kn;

	if (ino == 0)
		return ERR_PTR(-ESTALE);

	kn = kernfs_find_and_get_node_by_ino(info->root, ino);
	if (!kn)
		return ERR_PTR(-ESTALE);
	inode = kernfs_get_inode(sb, kn);
	kernfs_put(kn);
	if (!inode)
		return ERR_PTR(-ESTALE);

	if (generation && inode->i_generation != generation) {
		/* we didn't find the right inode.. */
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return inode;
}

static struct dentry *kernfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    kernfs_fh_get_inode);
}

static struct dentry *kernfs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    kernfs_fh_get_inode);
}

static struct dentry *kernfs_get_parent_dentry(struct dentry *child)
{
	struct kernfs_node *kn = kernfs_dentry_node(child);

	return d_obtain_alias(kernfs_get_inode(child->d_sb, kn->parent));
}

static const struct export_operations kernfs_export_ops = {
	.fh_to_dentry	= kernfs_fh_to_dentry,
	.fh_to_parent	= kernfs_fh_to_parent,
	.get_parent	= kernfs_get_parent_dentry,
};

/**
 * kernfs_root_from_sb - determine kernfs_root associated with a super_block
 * @sb: the super_block in question
 *
 * Return the kernfs_root associated with @sb.  If @sb is not a kernfs one,
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
 * node.  If @parent is b, then we return the node for c.
 * Passing in d as @parent is not ok.
 */
static struct kernfs_node *find_next_ancestor(struct kernfs_node *child,
					      struct kernfs_node *parent)
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
 * kernfs_node_dentry - get a dentry for the given kernfs_node
 * @kn: kernfs_node for which a dentry is needed
 * @sb: the kernfs super_block
 */
struct dentry *kernfs_node_dentry(struct kernfs_node *kn,
				  struct super_block *sb)
{
	struct dentry *dentry;
	struct kernfs_node *knparent = NULL;

	BUG_ON(sb->s_op != &kernfs_sops);

	dentry = dget(sb->s_root);

	/* Check if this is the root kernfs_node */
	if (!kn->parent)
		return dentry;

	knparent = find_next_ancestor(kn, NULL);
	if (WARN_ON(!knparent)) {
		dput(dentry);
		return ERR_PTR(-EINVAL);
	}

	do {
		struct dentry *dtmp;
		struct kernfs_node *kntmp;

		if (kn == knparent)
			return dentry;
		kntmp = find_next_ancestor(kn, knparent);
		if (WARN_ON(!kntmp)) {
			dput(dentry);
			return ERR_PTR(-EINVAL);
		}
		dtmp = lookup_one_len_unlocked(kntmp->name, dentry,
					       strlen(kntmp->name));
		dput(dentry);
		if (IS_ERR(dtmp))
			return dtmp;
		knparent = kntmp;
		dentry = dtmp;
	} while (true);
}

static int kernfs_fill_super(struct super_block *sb, unsigned long magic)
{
	struct kernfs_super_info *info = kernfs_info(sb);
	struct inode *inode;
	struct dentry *root;

	info->sb = sb;
	/* Userspace would break if executables or devices appear on sysfs */
	sb->s_iflags |= SB_I_NOEXEC | SB_I_NODEV;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = magic;
	sb->s_op = &kernfs_sops;
	sb->s_xattr = kernfs_xattr_handlers;
	if (info->root->flags & KERNFS_ROOT_SUPPORT_EXPORTOP)
		sb->s_export_op = &kernfs_export_ops;
	sb->s_time_gran = 1;

	/* get root inode, initialize and unlock it */
	mutex_lock(&kernfs_mutex);
	inode = kernfs_get_inode(sb, info->root->kn);
	mutex_unlock(&kernfs_mutex);
	if (!inode) {
		pr_debug("kernfs: could not get root inode\n");
		return -ENOMEM;
	}

	/* instantiate and link root dentry */
	root = d_make_root(inode);
	if (!root) {
		pr_debug("%s: could not get root dentry!\n", __func__);
		return -ENOMEM;
	}
	sb->s_root = root;
	sb->s_d_op = &kernfs_dops;
	return 0;
}

static int kernfs_test_super(struct super_block *sb, void *data)
{
	struct kernfs_super_info *sb_info = kernfs_info(sb);
	struct kernfs_super_info *info = data;

	return sb_info->root == info->root && sb_info->ns == info->ns;
}

static int kernfs_set_super(struct super_block *sb, void *data)
{
	int error;
	error = set_anon_super(sb, data);
	if (!error)
		sb->s_fs_info = data;
	return error;
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
 * kernfs_mount_ns - kernfs mount helper
 * @fs_type: file_system_type of the fs being mounted
 * @flags: mount flags specified for the mount
 * @root: kernfs_root of the hierarchy being mounted
 * @magic: file system specific magic number
 * @new_sb_created: tell the caller if we allocated a new superblock
 * @ns: optional namespace tag of the mount
 *
 * This is to be called from each kernfs user's file_system_type->mount()
 * implementation, which should pass through the specified @fs_type and
 * @flags, and specify the hierarchy and namespace tag to mount via @root
 * and @ns, respectively.
 *
 * The return value can be passed to the vfs layer verbatim.
 */
struct dentry *kernfs_mount_ns(struct file_system_type *fs_type, int flags,
				struct kernfs_root *root, unsigned long magic,
				bool *new_sb_created, const void *ns)
{
	struct super_block *sb;
	struct kernfs_super_info *info;
	int error;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->root = root;
	info->ns = ns;
	INIT_LIST_HEAD(&info->node);

	sb = sget_userns(fs_type, kernfs_test_super, kernfs_set_super, flags,
			 &init_user_ns, info);
	if (IS_ERR(sb) || sb->s_fs_info != info)
		kfree(info);
	if (IS_ERR(sb))
		return ERR_CAST(sb);

	if (new_sb_created)
		*new_sb_created = !sb->s_root;

	if (!sb->s_root) {
		struct kernfs_super_info *info = kernfs_info(sb);

		error = kernfs_fill_super(sb, magic);
		if (error) {
			deactivate_locked_super(sb);
			return ERR_PTR(error);
		}
		sb->s_flags |= SB_ACTIVE;

		mutex_lock(&kernfs_mutex);
		list_add(&info->node, &root->supers);
		mutex_unlock(&kernfs_mutex);
	}

	return dget(sb->s_root);
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
	list_del(&info->node);
	mutex_unlock(&kernfs_mutex);

	/*
	 * Remove the superblock from fs_supers/s_instances
	 * so we can't find it, before freeing kernfs_super_info.
	 */
	kill_anon_super(sb);
	kfree(info);
}

/**
 * kernfs_pin_sb: try to pin the superblock associated with a kernfs_root
 * @kernfs_root: the kernfs_root in question
 * @ns: the namespace tag
 *
 * Pin the superblock so the superblock won't be destroyed in subsequent
 * operations.  This can be used to block ->kill_sb() which may be useful
 * for kernfs users which dynamically manage superblocks.
 *
 * Returns NULL if there's no superblock associated to this kernfs_root, or
 * -EINVAL if the superblock is being freed.
 */
struct super_block *kernfs_pin_sb(struct kernfs_root *root, const void *ns)
{
	struct kernfs_super_info *info;
	struct super_block *sb = NULL;

	mutex_lock(&kernfs_mutex);
	list_for_each_entry(info, &root->supers, node) {
		if (info->ns == ns) {
			sb = info->sb;
			if (!atomic_inc_not_zero(&info->sb->s_active))
				sb = ERR_PTR(-EINVAL);
			break;
		}
	}
	mutex_unlock(&kernfs_mutex);
	return sb;
}

void __init kernfs_init(void)
{

	/*
	 * the slab is freed in RCU context, so kernfs_find_and_get_node_by_ino
	 * can access the slab lock free. This could introduce stale nodes,
	 * please see how kernfs_find_and_get_node_by_ino filters out stale
	 * nodes.
	 */
	kernfs_node_cache = kmem_cache_create("kernfs_node_cache",
					      sizeof(struct kernfs_node),
					      0,
					      SLAB_PANIC | SLAB_TYPESAFE_BY_RCU,
					      NULL);
}
