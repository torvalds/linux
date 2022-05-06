// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - Filesystem management and hooks
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/compiler_types.h>
#include <linux/dcache.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/lsm_hooks.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/wait_bit.h>
#include <linux/workqueue.h>
#include <uapi/linux/landlock.h>

#include "common.h"
#include "cred.h"
#include "fs.h"
#include "limits.h"
#include "object.h"
#include "ruleset.h"
#include "setup.h"

/* Underlying object management */

static void release_inode(struct landlock_object *const object)
	__releases(object->lock)
{
	struct inode *const inode = object->underobj;
	struct super_block *sb;

	if (!inode) {
		spin_unlock(&object->lock);
		return;
	}

	/*
	 * Protects against concurrent use by hook_sb_delete() of the reference
	 * to the underlying inode.
	 */
	object->underobj = NULL;
	/*
	 * Makes sure that if the filesystem is concurrently unmounted,
	 * hook_sb_delete() will wait for us to finish iput().
	 */
	sb = inode->i_sb;
	atomic_long_inc(&landlock_superblock(sb)->inode_refs);
	spin_unlock(&object->lock);
	/*
	 * Because object->underobj was not NULL, hook_sb_delete() and
	 * get_inode_object() guarantee that it is safe to reset
	 * landlock_inode(inode)->object while it is not NULL.  It is therefore
	 * not necessary to lock inode->i_lock.
	 */
	rcu_assign_pointer(landlock_inode(inode)->object, NULL);
	/*
	 * Now, new rules can safely be tied to @inode with get_inode_object().
	 */

	iput(inode);
	if (atomic_long_dec_and_test(&landlock_superblock(sb)->inode_refs))
		wake_up_var(&landlock_superblock(sb)->inode_refs);
}

static const struct landlock_object_underops landlock_fs_underops = {
	.release = release_inode
};

/* Ruleset management */

static struct landlock_object *get_inode_object(struct inode *const inode)
{
	struct landlock_object *object, *new_object;
	struct landlock_inode_security *inode_sec = landlock_inode(inode);

	rcu_read_lock();
retry:
	object = rcu_dereference(inode_sec->object);
	if (object) {
		if (likely(refcount_inc_not_zero(&object->usage))) {
			rcu_read_unlock();
			return object;
		}
		/*
		 * We are racing with release_inode(), the object is going
		 * away.  Wait for release_inode(), then retry.
		 */
		spin_lock(&object->lock);
		spin_unlock(&object->lock);
		goto retry;
	}
	rcu_read_unlock();

	/*
	 * If there is no object tied to @inode, then create a new one (without
	 * holding any locks).
	 */
	new_object = landlock_create_object(&landlock_fs_underops, inode);
	if (IS_ERR(new_object))
		return new_object;

	/*
	 * Protects against concurrent calls to get_inode_object() or
	 * hook_sb_delete().
	 */
	spin_lock(&inode->i_lock);
	if (unlikely(rcu_access_pointer(inode_sec->object))) {
		/* Someone else just created the object, bail out and retry. */
		spin_unlock(&inode->i_lock);
		kfree(new_object);

		rcu_read_lock();
		goto retry;
	}

	/*
	 * @inode will be released by hook_sb_delete() on its superblock
	 * shutdown, or by release_inode() when no more ruleset references the
	 * related object.
	 */
	ihold(inode);
	rcu_assign_pointer(inode_sec->object, new_object);
	spin_unlock(&inode->i_lock);
	return new_object;
}

/* All access rights that can be tied to files. */
/* clang-format off */
#define ACCESS_FILE ( \
	LANDLOCK_ACCESS_FS_EXECUTE | \
	LANDLOCK_ACCESS_FS_WRITE_FILE | \
	LANDLOCK_ACCESS_FS_READ_FILE)
/* clang-format on */

/*
 * @path: Should have been checked by get_path_from_fd().
 */
int landlock_append_fs_rule(struct landlock_ruleset *const ruleset,
			    const struct path *const path,
			    access_mask_t access_rights)
{
	int err;
	struct landlock_object *object;

	/* Files only get access rights that make sense. */
	if (!d_is_dir(path->dentry) &&
	    (access_rights | ACCESS_FILE) != ACCESS_FILE)
		return -EINVAL;
	if (WARN_ON_ONCE(ruleset->num_layers != 1))
		return -EINVAL;

	/* Transforms relative access rights to absolute ones. */
	access_rights |= LANDLOCK_MASK_ACCESS_FS & ~ruleset->fs_access_masks[0];
	object = get_inode_object(d_backing_inode(path->dentry));
	if (IS_ERR(object))
		return PTR_ERR(object);
	mutex_lock(&ruleset->lock);
	err = landlock_insert_rule(ruleset, object, access_rights);
	mutex_unlock(&ruleset->lock);
	/*
	 * No need to check for an error because landlock_insert_rule()
	 * increments the refcount for the new object if needed.
	 */
	landlock_put_object(object);
	return err;
}

/* Access-control management */

static inline layer_mask_t
unmask_layers(const struct landlock_ruleset *const domain,
	      const struct path *const path, const access_mask_t access_request,
	      layer_mask_t layer_mask)
{
	const struct landlock_rule *rule;
	const struct inode *inode;
	size_t i;

	if (d_is_negative(path->dentry))
		/* Ignore nonexistent leafs. */
		return layer_mask;
	inode = d_backing_inode(path->dentry);
	rcu_read_lock();
	rule = landlock_find_rule(
		domain, rcu_dereference(landlock_inode(inode)->object));
	rcu_read_unlock();
	if (!rule)
		return layer_mask;

	/*
	 * An access is granted if, for each policy layer, at least one rule
	 * encountered on the pathwalk grants the requested accesses,
	 * regardless of their position in the layer stack.  We must then check
	 * the remaining layers for each inode, from the first added layer to
	 * the last one.
	 */
	for (i = 0; i < rule->num_layers; i++) {
		const struct landlock_layer *const layer = &rule->layers[i];
		const layer_mask_t layer_bit = BIT_ULL(layer->level - 1);

		/* Checks that the layer grants access to the full request. */
		if ((layer->access & access_request) == access_request) {
			layer_mask &= ~layer_bit;

			if (layer_mask == 0)
				return layer_mask;
		}
	}
	return layer_mask;
}

static int check_access_path(const struct landlock_ruleset *const domain,
			     const struct path *const path,
			     const access_mask_t access_request)
{
	bool allowed = false;
	struct path walker_path;
	layer_mask_t layer_mask;
	size_t i;

	if (!access_request)
		return 0;
	if (WARN_ON_ONCE(!domain || !path))
		return 0;
	/*
	 * Allows access to pseudo filesystems that will never be mountable
	 * (e.g. sockfs, pipefs), but can still be reachable through
	 * /proc/<pid>/fd/<file-descriptor> .
	 */
	if ((path->dentry->d_sb->s_flags & SB_NOUSER) ||
	    (d_is_positive(path->dentry) &&
	     unlikely(IS_PRIVATE(d_backing_inode(path->dentry)))))
		return 0;
	if (WARN_ON_ONCE(domain->num_layers < 1))
		return -EACCES;

	/* Saves all layers handling a subset of requested accesses. */
	layer_mask = 0;
	for (i = 0; i < domain->num_layers; i++) {
		if (domain->fs_access_masks[i] & access_request)
			layer_mask |= BIT_ULL(i);
	}
	/* An access request not handled by the domain is allowed. */
	if (layer_mask == 0)
		return 0;

	walker_path = *path;
	path_get(&walker_path);
	/*
	 * We need to walk through all the hierarchy to not miss any relevant
	 * restriction.
	 */
	while (true) {
		struct dentry *parent_dentry;

		layer_mask = unmask_layers(domain, &walker_path, access_request,
					   layer_mask);
		if (layer_mask == 0) {
			/* Stops when a rule from each layer grants access. */
			allowed = true;
			break;
		}

jump_up:
		if (walker_path.dentry == walker_path.mnt->mnt_root) {
			if (follow_up(&walker_path)) {
				/* Ignores hidden mount points. */
				goto jump_up;
			} else {
				/*
				 * Stops at the real root.  Denies access
				 * because not all layers have granted access.
				 */
				allowed = false;
				break;
			}
		}
		if (unlikely(IS_ROOT(walker_path.dentry))) {
			/*
			 * Stops at disconnected root directories.  Only allows
			 * access to internal filesystems (e.g. nsfs, which is
			 * reachable through /proc/<pid>/ns/<namespace>).
			 */
			allowed = !!(walker_path.mnt->mnt_flags & MNT_INTERNAL);
			break;
		}
		parent_dentry = dget_parent(walker_path.dentry);
		dput(walker_path.dentry);
		walker_path.dentry = parent_dentry;
	}
	path_put(&walker_path);
	return allowed ? 0 : -EACCES;
}

static inline int current_check_access_path(const struct path *const path,
					    const access_mask_t access_request)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();

	if (!dom)
		return 0;
	return check_access_path(dom, path, access_request);
}

/* Inode hooks */

static void hook_inode_free_security(struct inode *const inode)
{
	/*
	 * All inodes must already have been untied from their object by
	 * release_inode() or hook_sb_delete().
	 */
	WARN_ON_ONCE(landlock_inode(inode)->object);
}

/* Super-block hooks */

/*
 * Release the inodes used in a security policy.
 *
 * Cf. fsnotify_unmount_inodes() and invalidate_inodes()
 */
static void hook_sb_delete(struct super_block *const sb)
{
	struct inode *inode, *prev_inode = NULL;

	if (!landlock_initialized)
		return;

	spin_lock(&sb->s_inode_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		struct landlock_object *object;

		/* Only handles referenced inodes. */
		if (!atomic_read(&inode->i_count))
			continue;

		/*
		 * Protects against concurrent modification of inode (e.g.
		 * from get_inode_object()).
		 */
		spin_lock(&inode->i_lock);
		/*
		 * Checks I_FREEING and I_WILL_FREE  to protect against a race
		 * condition when release_inode() just called iput(), which
		 * could lead to a NULL dereference of inode->security or a
		 * second call to iput() for the same Landlock object.  Also
		 * checks I_NEW because such inode cannot be tied to an object.
		 */
		if (inode->i_state & (I_FREEING | I_WILL_FREE | I_NEW)) {
			spin_unlock(&inode->i_lock);
			continue;
		}

		rcu_read_lock();
		object = rcu_dereference(landlock_inode(inode)->object);
		if (!object) {
			rcu_read_unlock();
			spin_unlock(&inode->i_lock);
			continue;
		}
		/* Keeps a reference to this inode until the next loop walk. */
		__iget(inode);
		spin_unlock(&inode->i_lock);

		/*
		 * If there is no concurrent release_inode() ongoing, then we
		 * are in charge of calling iput() on this inode, otherwise we
		 * will just wait for it to finish.
		 */
		spin_lock(&object->lock);
		if (object->underobj == inode) {
			object->underobj = NULL;
			spin_unlock(&object->lock);
			rcu_read_unlock();

			/*
			 * Because object->underobj was not NULL,
			 * release_inode() and get_inode_object() guarantee
			 * that it is safe to reset
			 * landlock_inode(inode)->object while it is not NULL.
			 * It is therefore not necessary to lock inode->i_lock.
			 */
			rcu_assign_pointer(landlock_inode(inode)->object, NULL);
			/*
			 * At this point, we own the ihold() reference that was
			 * originally set up by get_inode_object() and the
			 * __iget() reference that we just set in this loop
			 * walk.  Therefore the following call to iput() will
			 * not sleep nor drop the inode because there is now at
			 * least two references to it.
			 */
			iput(inode);
		} else {
			spin_unlock(&object->lock);
			rcu_read_unlock();
		}

		if (prev_inode) {
			/*
			 * At this point, we still own the __iget() reference
			 * that we just set in this loop walk.  Therefore we
			 * can drop the list lock and know that the inode won't
			 * disappear from under us until the next loop walk.
			 */
			spin_unlock(&sb->s_inode_list_lock);
			/*
			 * We can now actually put the inode reference from the
			 * previous loop walk, which is not needed anymore.
			 */
			iput(prev_inode);
			cond_resched();
			spin_lock(&sb->s_inode_list_lock);
		}
		prev_inode = inode;
	}
	spin_unlock(&sb->s_inode_list_lock);

	/* Puts the inode reference from the last loop walk, if any. */
	if (prev_inode)
		iput(prev_inode);
	/* Waits for pending iput() in release_inode(). */
	wait_var_event(&landlock_superblock(sb)->inode_refs,
		       !atomic_long_read(&landlock_superblock(sb)->inode_refs));
}

/*
 * Because a Landlock security policy is defined according to the filesystem
 * topology (i.e. the mount namespace), changing it may grant access to files
 * not previously allowed.
 *
 * To make it simple, deny any filesystem topology modification by landlocked
 * processes.  Non-landlocked processes may still change the namespace of a
 * landlocked process, but this kind of threat must be handled by a system-wide
 * access-control security policy.
 *
 * This could be lifted in the future if Landlock can safely handle mount
 * namespace updates requested by a landlocked process.  Indeed, we could
 * update the current domain (which is currently read-only) by taking into
 * account the accesses of the source and the destination of a new mount point.
 * However, it would also require to make all the child domains dynamically
 * inherit these new constraints.  Anyway, for backward compatibility reasons,
 * a dedicated user space option would be required (e.g. as a ruleset flag).
 */
static int hook_sb_mount(const char *const dev_name,
			 const struct path *const path, const char *const type,
			 const unsigned long flags, void *const data)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

static int hook_move_mount(const struct path *const from_path,
			   const struct path *const to_path)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

/*
 * Removing a mount point may reveal a previously hidden file hierarchy, which
 * may then grant access to files, which may have previously been forbidden.
 */
static int hook_sb_umount(struct vfsmount *const mnt, const int flags)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

static int hook_sb_remount(struct super_block *const sb, void *const mnt_opts)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

/*
 * pivot_root(2), like mount(2), changes the current mount namespace.  It must
 * then be forbidden for a landlocked process.
 *
 * However, chroot(2) may be allowed because it only changes the relative root
 * directory of the current process.  Moreover, it can be used to restrict the
 * view of the filesystem.
 */
static int hook_sb_pivotroot(const struct path *const old_path,
			     const struct path *const new_path)
{
	if (!landlock_get_current_domain())
		return 0;
	return -EPERM;
}

/* Path hooks */

static inline access_mask_t get_mode_access(const umode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFLNK:
		return LANDLOCK_ACCESS_FS_MAKE_SYM;
	case 0:
		/* A zero mode translates to S_IFREG. */
	case S_IFREG:
		return LANDLOCK_ACCESS_FS_MAKE_REG;
	case S_IFDIR:
		return LANDLOCK_ACCESS_FS_MAKE_DIR;
	case S_IFCHR:
		return LANDLOCK_ACCESS_FS_MAKE_CHAR;
	case S_IFBLK:
		return LANDLOCK_ACCESS_FS_MAKE_BLOCK;
	case S_IFIFO:
		return LANDLOCK_ACCESS_FS_MAKE_FIFO;
	case S_IFSOCK:
		return LANDLOCK_ACCESS_FS_MAKE_SOCK;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

/*
 * Creating multiple links or renaming may lead to privilege escalations if not
 * handled properly.  Indeed, we must be sure that the source doesn't gain more
 * privileges by being accessible from the destination.  This is getting more
 * complex when dealing with multiple layers.  The whole picture can be seen as
 * a multilayer partial ordering problem.  A future version of Landlock will
 * deal with that.
 */
static int hook_path_link(struct dentry *const old_dentry,
			  const struct path *const new_dir,
			  struct dentry *const new_dentry)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();

	if (!dom)
		return 0;
	/* The mount points are the same for old and new paths, cf. EXDEV. */
	if (old_dentry->d_parent != new_dir->dentry)
		/* Gracefully forbids reparenting. */
		return -EXDEV;
	if (unlikely(d_is_negative(old_dentry)))
		return -ENOENT;
	return check_access_path(
		dom, new_dir,
		get_mode_access(d_backing_inode(old_dentry)->i_mode));
}

static inline access_mask_t maybe_remove(const struct dentry *const dentry)
{
	if (d_is_negative(dentry))
		return 0;
	return d_is_dir(dentry) ? LANDLOCK_ACCESS_FS_REMOVE_DIR :
				  LANDLOCK_ACCESS_FS_REMOVE_FILE;
}

static int hook_path_rename(const struct path *const old_dir,
			    struct dentry *const old_dentry,
			    const struct path *const new_dir,
			    struct dentry *const new_dentry)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();

	if (!dom)
		return 0;
	/* The mount points are the same for old and new paths, cf. EXDEV. */
	if (old_dir->dentry != new_dir->dentry)
		/* Gracefully forbids reparenting. */
		return -EXDEV;
	if (unlikely(d_is_negative(old_dentry)))
		return -ENOENT;
	/* RENAME_EXCHANGE is handled because directories are the same. */
	return check_access_path(
		dom, old_dir,
		maybe_remove(old_dentry) | maybe_remove(new_dentry) |
			get_mode_access(d_backing_inode(old_dentry)->i_mode));
}

static int hook_path_mkdir(const struct path *const dir,
			   struct dentry *const dentry, const umode_t mode)
{
	return current_check_access_path(dir, LANDLOCK_ACCESS_FS_MAKE_DIR);
}

static int hook_path_mknod(const struct path *const dir,
			   struct dentry *const dentry, const umode_t mode,
			   const unsigned int dev)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();

	if (!dom)
		return 0;
	return check_access_path(dom, dir, get_mode_access(mode));
}

static int hook_path_symlink(const struct path *const dir,
			     struct dentry *const dentry,
			     const char *const old_name)
{
	return current_check_access_path(dir, LANDLOCK_ACCESS_FS_MAKE_SYM);
}

static int hook_path_unlink(const struct path *const dir,
			    struct dentry *const dentry)
{
	return current_check_access_path(dir, LANDLOCK_ACCESS_FS_REMOVE_FILE);
}

static int hook_path_rmdir(const struct path *const dir,
			   struct dentry *const dentry)
{
	return current_check_access_path(dir, LANDLOCK_ACCESS_FS_REMOVE_DIR);
}

/* File hooks */

static inline access_mask_t get_file_access(const struct file *const file)
{
	access_mask_t access = 0;

	if (file->f_mode & FMODE_READ) {
		/* A directory can only be opened in read mode. */
		if (S_ISDIR(file_inode(file)->i_mode))
			return LANDLOCK_ACCESS_FS_READ_DIR;
		access = LANDLOCK_ACCESS_FS_READ_FILE;
	}
	if (file->f_mode & FMODE_WRITE)
		access |= LANDLOCK_ACCESS_FS_WRITE_FILE;
	/* __FMODE_EXEC is indeed part of f_flags, not f_mode. */
	if (file->f_flags & __FMODE_EXEC)
		access |= LANDLOCK_ACCESS_FS_EXECUTE;
	return access;
}

static int hook_file_open(struct file *const file)
{
	const struct landlock_ruleset *const dom =
		landlock_get_current_domain();

	if (!dom)
		return 0;
	/*
	 * Because a file may be opened with O_PATH, get_file_access() may
	 * return 0.  This case will be handled with a future Landlock
	 * evolution.
	 */
	return check_access_path(dom, &file->f_path, get_file_access(file));
}

static struct security_hook_list landlock_hooks[] __lsm_ro_after_init = {
	LSM_HOOK_INIT(inode_free_security, hook_inode_free_security),

	LSM_HOOK_INIT(sb_delete, hook_sb_delete),
	LSM_HOOK_INIT(sb_mount, hook_sb_mount),
	LSM_HOOK_INIT(move_mount, hook_move_mount),
	LSM_HOOK_INIT(sb_umount, hook_sb_umount),
	LSM_HOOK_INIT(sb_remount, hook_sb_remount),
	LSM_HOOK_INIT(sb_pivotroot, hook_sb_pivotroot),

	LSM_HOOK_INIT(path_link, hook_path_link),
	LSM_HOOK_INIT(path_rename, hook_path_rename),
	LSM_HOOK_INIT(path_mkdir, hook_path_mkdir),
	LSM_HOOK_INIT(path_mknod, hook_path_mknod),
	LSM_HOOK_INIT(path_symlink, hook_path_symlink),
	LSM_HOOK_INIT(path_unlink, hook_path_unlink),
	LSM_HOOK_INIT(path_rmdir, hook_path_rmdir),

	LSM_HOOK_INIT(file_open, hook_file_open),
};

__init void landlock_add_fs_hooks(void)
{
	security_add_hooks(landlock_hooks, ARRAY_SIZE(landlock_hooks),
			   LANDLOCK_NAME);
}
