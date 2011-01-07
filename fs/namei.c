/*
 *  linux/fs/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

/* [Feb 1997 T. Schoebel-Theuer] Complete rewrite of the pathname
 * lookup logic.
 */
/* [Feb-Apr 2000, AV] Rewrite to the new namespace architecture.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/fsnotify.h>
#include <linux/personality.h>
#include <linux/security.h>
#include <linux/ima.h>
#include <linux/syscalls.h>
#include <linux/mount.h>
#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/device_cgroup.h>
#include <linux/fs_struct.h>
#include <asm/uaccess.h>

#include "internal.h"

/* [Feb-1997 T. Schoebel-Theuer]
 * Fundamental changes in the pathname lookup mechanisms (namei)
 * were necessary because of omirr.  The reason is that omirr needs
 * to know the _real_ pathname, not the user-supplied one, in case
 * of symlinks (and also when transname replacements occur).
 *
 * The new code replaces the old recursive symlink resolution with
 * an iterative one (in case of non-nested symlink chains).  It does
 * this with calls to <fs>_follow_link().
 * As a side effect, dir_namei(), _namei() and follow_link() are now 
 * replaced with a single function lookup_dentry() that can handle all 
 * the special cases of the former code.
 *
 * With the new dcache, the pathname is stored at each inode, at least as
 * long as the refcount of the inode is positive.  As a side effect, the
 * size of the dcache depends on the inode cache and thus is dynamic.
 *
 * [29-Apr-1998 C. Scott Ananian] Updated above description of symlink
 * resolution to correspond with current state of the code.
 *
 * Note that the symlink resolution is not *completely* iterative.
 * There is still a significant amount of tail- and mid- recursion in
 * the algorithm.  Also, note that <fs>_readlink() is not used in
 * lookup_dentry(): lookup_dentry() on the result of <fs>_readlink()
 * may return different results than <fs>_follow_link().  Many virtual
 * filesystems (including /proc) exhibit this behavior.
 */

/* [24-Feb-97 T. Schoebel-Theuer] Side effects caused by new implementation:
 * New symlink semantics: when open() is called with flags O_CREAT | O_EXCL
 * and the name already exists in form of a symlink, try to create the new
 * name indicated by the symlink. The old code always complained that the
 * name already exists, due to not following the symlink even if its target
 * is nonexistent.  The new semantics affects also mknod() and link() when
 * the name is a symlink pointing to a non-existant name.
 *
 * I don't know which semantics is the right one, since I have no access
 * to standards. But I found by trial that HP-UX 9.0 has the full "new"
 * semantics implemented, while SunOS 4.1.1 and Solaris (SunOS 5.4) have the
 * "old" one. Personally, I think the new semantics is much more logical.
 * Note that "ln old new" where "new" is a symlink pointing to a non-existing
 * file does succeed in both HP-UX and SunOs, but not in Solaris
 * and in the old Linux semantics.
 */

/* [16-Dec-97 Kevin Buhr] For security reasons, we change some symlink
 * semantics.  See the comments in "open_namei" and "do_link" below.
 *
 * [10-Sep-98 Alan Modra] Another symlink change.
 */

/* [Feb-Apr 2000 AV] Complete rewrite. Rules for symlinks:
 *	inside the path - always follow.
 *	in the last component in creation/removal/renaming - never follow.
 *	if LOOKUP_FOLLOW passed - follow.
 *	if the pathname has trailing slashes - follow.
 *	otherwise - don't follow.
 * (applied in that order).
 *
 * [Jun 2000 AV] Inconsistent behaviour of open() in case if flags==O_CREAT
 * restored for 2.4. This is the last surviving part of old 4.2BSD bug.
 * During the 2.4 we need to fix the userland stuff depending on it -
 * hopefully we will be able to get rid of that wart in 2.5. So far only
 * XEmacs seems to be relying on it...
 */
/*
 * [Sep 2001 AV] Single-semaphore locking scheme (kudos to David Holland)
 * implemented.  Let's see if raised priority of ->s_vfs_rename_mutex gives
 * any extra contention...
 */

/* In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 * PATH_MAX includes the nul terminator --RR.
 */
static int do_getname(const char __user *filename, char *page)
{
	int retval;
	unsigned long len = PATH_MAX;

	if (!segment_eq(get_fs(), KERNEL_DS)) {
		if ((unsigned long) filename >= TASK_SIZE)
			return -EFAULT;
		if (TASK_SIZE - (unsigned long) filename < PATH_MAX)
			len = TASK_SIZE - (unsigned long) filename;
	}

	retval = strncpy_from_user(page, filename, len);
	if (retval > 0) {
		if (retval < len)
			return 0;
		return -ENAMETOOLONG;
	} else if (!retval)
		retval = -ENOENT;
	return retval;
}

char * getname(const char __user * filename)
{
	char *tmp, *result;

	result = ERR_PTR(-ENOMEM);
	tmp = __getname();
	if (tmp)  {
		int retval = do_getname(filename, tmp);

		result = tmp;
		if (retval < 0) {
			__putname(tmp);
			result = ERR_PTR(retval);
		}
	}
	audit_getname(result);
	return result;
}

#ifdef CONFIG_AUDITSYSCALL
void putname(const char *name)
{
	if (unlikely(!audit_dummy_context()))
		audit_putname(name);
	else
		__putname(name);
}
EXPORT_SYMBOL(putname);
#endif

/*
 * This does basic POSIX ACL permission checking
 */
static inline int __acl_permission_check(struct inode *inode, int mask,
		int (*check_acl)(struct inode *inode, int mask), int rcu)
{
	umode_t			mode = inode->i_mode;

	mask &= MAY_READ | MAY_WRITE | MAY_EXEC;

	if (current_fsuid() == inode->i_uid)
		mode >>= 6;
	else {
		if (IS_POSIXACL(inode) && (mode & S_IRWXG) && check_acl) {
			if (rcu) {
				return -ECHILD;
			} else {
				int error = check_acl(inode, mask);
				if (error != -EAGAIN)
					return error;
			}
		}

		if (in_group_p(inode->i_gid))
			mode >>= 3;
	}

	/*
	 * If the DACs are ok we don't need any capability check.
	 */
	if ((mask & ~mode) == 0)
		return 0;
	return -EACCES;
}

static inline int acl_permission_check(struct inode *inode, int mask,
		int (*check_acl)(struct inode *inode, int mask))
{
	return __acl_permission_check(inode, mask, check_acl, 0);
}

/**
 * generic_permission  -  check for access rights on a Posix-like filesystem
 * @inode:	inode to check access rights for
 * @mask:	right to check for (%MAY_READ, %MAY_WRITE, %MAY_EXEC)
 * @check_acl:	optional callback to check for Posix ACLs
 *
 * Used to check for read/write/execute permissions on a file.
 * We use "fsuid" for this, letting us set arbitrary permissions
 * for filesystem access without changing the "normal" uids which
 * are used for other things..
 */
int generic_permission(struct inode *inode, int mask,
		int (*check_acl)(struct inode *inode, int mask))
{
	int ret;

	/*
	 * Do the basic POSIX ACL permission checks.
	 */
	ret = acl_permission_check(inode, mask, check_acl);
	if (ret != -EACCES)
		return ret;

	/*
	 * Read/write DACs are always overridable.
	 * Executable DACs are overridable if at least one exec bit is set.
	 */
	if (!(mask & MAY_EXEC) || execute_ok(inode))
		if (capable(CAP_DAC_OVERRIDE))
			return 0;

	/*
	 * Searching includes executable on directories, else just read.
	 */
	mask &= MAY_READ | MAY_WRITE | MAY_EXEC;
	if (mask == MAY_READ || (S_ISDIR(inode->i_mode) && !(mask & MAY_WRITE)))
		if (capable(CAP_DAC_READ_SEARCH))
			return 0;

	return -EACCES;
}

/**
 * inode_permission  -  check for access rights to a given inode
 * @inode:	inode to check permission on
 * @mask:	right to check for (%MAY_READ, %MAY_WRITE, %MAY_EXEC)
 *
 * Used to check for read/write/execute permissions on an inode.
 * We use "fsuid" for this, letting us set arbitrary permissions
 * for filesystem access without changing the "normal" uids which
 * are used for other things.
 */
int inode_permission(struct inode *inode, int mask)
{
	int retval;

	if (mask & MAY_WRITE) {
		umode_t mode = inode->i_mode;

		/*
		 * Nobody gets write access to a read-only fs.
		 */
		if (IS_RDONLY(inode) &&
		    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
			return -EROFS;

		/*
		 * Nobody gets write access to an immutable file.
		 */
		if (IS_IMMUTABLE(inode))
			return -EACCES;
	}

	if (inode->i_op->permission)
		retval = inode->i_op->permission(inode, mask);
	else
		retval = generic_permission(inode, mask, inode->i_op->check_acl);

	if (retval)
		return retval;

	retval = devcgroup_inode_permission(inode, mask);
	if (retval)
		return retval;

	return security_inode_permission(inode, mask);
}

/**
 * file_permission  -  check for additional access rights to a given file
 * @file:	file to check access rights for
 * @mask:	right to check for (%MAY_READ, %MAY_WRITE, %MAY_EXEC)
 *
 * Used to check for read/write/execute permissions on an already opened
 * file.
 *
 * Note:
 *	Do not use this function in new code.  All access checks should
 *	be done using inode_permission().
 */
int file_permission(struct file *file, int mask)
{
	return inode_permission(file->f_path.dentry->d_inode, mask);
}

/*
 * get_write_access() gets write permission for a file.
 * put_write_access() releases this write permission.
 * This is used for regular files.
 * We cannot support write (and maybe mmap read-write shared) accesses and
 * MAP_DENYWRITE mmappings simultaneously. The i_writecount field of an inode
 * can have the following values:
 * 0: no writers, no VM_DENYWRITE mappings
 * < 0: (-i_writecount) vm_area_structs with VM_DENYWRITE set exist
 * > 0: (i_writecount) users are writing to the file.
 *
 * Normally we operate on that counter with atomic_{inc,dec} and it's safe
 * except for the cases where we don't hold i_writecount yet. Then we need to
 * use {get,deny}_write_access() - these functions check the sign and refuse
 * to do the change if sign is wrong. Exclusion between them is provided by
 * the inode->i_lock spinlock.
 */

int get_write_access(struct inode * inode)
{
	spin_lock(&inode->i_lock);
	if (atomic_read(&inode->i_writecount) < 0) {
		spin_unlock(&inode->i_lock);
		return -ETXTBSY;
	}
	atomic_inc(&inode->i_writecount);
	spin_unlock(&inode->i_lock);

	return 0;
}

int deny_write_access(struct file * file)
{
	struct inode *inode = file->f_path.dentry->d_inode;

	spin_lock(&inode->i_lock);
	if (atomic_read(&inode->i_writecount) > 0) {
		spin_unlock(&inode->i_lock);
		return -ETXTBSY;
	}
	atomic_dec(&inode->i_writecount);
	spin_unlock(&inode->i_lock);

	return 0;
}

/**
 * path_get - get a reference to a path
 * @path: path to get the reference to
 *
 * Given a path increment the reference count to the dentry and the vfsmount.
 */
void path_get(struct path *path)
{
	mntget(path->mnt);
	dget(path->dentry);
}
EXPORT_SYMBOL(path_get);

/**
 * path_put - put a reference to a path
 * @path: path to put the reference to
 *
 * Given a path decrement the reference count to the dentry and the vfsmount.
 */
void path_put(struct path *path)
{
	dput(path->dentry);
	mntput(path->mnt);
}
EXPORT_SYMBOL(path_put);

/**
 * nameidata_drop_rcu - drop this nameidata out of rcu-walk
 * @nd: nameidata pathwalk data to drop
 * @Returns: 0 on success, -ECHLID on failure
 *
 * Path walking has 2 modes, rcu-walk and ref-walk (see
 * Documentation/filesystems/path-lookup.txt). __drop_rcu* functions attempt
 * to drop out of rcu-walk mode and take normal reference counts on dentries
 * and vfsmounts to transition to rcu-walk mode. __drop_rcu* functions take
 * refcounts at the last known good point before rcu-walk got stuck, so
 * ref-walk may continue from there. If this is not successful (eg. a seqcount
 * has changed), then failure is returned and path walk restarts from the
 * beginning in ref-walk mode.
 *
 * nameidata_drop_rcu attempts to drop the current nd->path and nd->root into
 * ref-walk. Must be called from rcu-walk context.
 */
static int nameidata_drop_rcu(struct nameidata *nd)
{
	struct fs_struct *fs = current->fs;
	struct dentry *dentry = nd->path.dentry;

	BUG_ON(!(nd->flags & LOOKUP_RCU));
	if (nd->root.mnt) {
		spin_lock(&fs->lock);
		if (nd->root.mnt != fs->root.mnt ||
				nd->root.dentry != fs->root.dentry)
			goto err_root;
	}
	spin_lock(&dentry->d_lock);
	if (!__d_rcu_to_refcount(dentry, nd->seq))
		goto err;
	BUG_ON(nd->inode != dentry->d_inode);
	spin_unlock(&dentry->d_lock);
	if (nd->root.mnt) {
		path_get(&nd->root);
		spin_unlock(&fs->lock);
	}
	mntget(nd->path.mnt);

	rcu_read_unlock();
	br_read_unlock(vfsmount_lock);
	nd->flags &= ~LOOKUP_RCU;
	return 0;
err:
	spin_unlock(&dentry->d_lock);
err_root:
	if (nd->root.mnt)
		spin_unlock(&fs->lock);
	return -ECHILD;
}

/* Try to drop out of rcu-walk mode if we were in it, otherwise do nothing.  */
static inline int nameidata_drop_rcu_maybe(struct nameidata *nd)
{
	if (nd->flags & LOOKUP_RCU)
		return nameidata_drop_rcu(nd);
	return 0;
}

/**
 * nameidata_dentry_drop_rcu - drop nameidata and dentry out of rcu-walk
 * @nd: nameidata pathwalk data to drop
 * @dentry: dentry to drop
 * @Returns: 0 on success, -ECHLID on failure
 *
 * nameidata_dentry_drop_rcu attempts to drop the current nd->path and nd->root,
 * and dentry into ref-walk. @dentry must be a path found by a do_lookup call on
 * @nd. Must be called from rcu-walk context.
 */
static int nameidata_dentry_drop_rcu(struct nameidata *nd, struct dentry *dentry)
{
	struct fs_struct *fs = current->fs;
	struct dentry *parent = nd->path.dentry;

	BUG_ON(!(nd->flags & LOOKUP_RCU));
	if (nd->root.mnt) {
		spin_lock(&fs->lock);
		if (nd->root.mnt != fs->root.mnt ||
				nd->root.dentry != fs->root.dentry)
			goto err_root;
	}
	spin_lock(&parent->d_lock);
	spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	if (!__d_rcu_to_refcount(dentry, nd->seq))
		goto err;
	/*
	 * If the sequence check on the child dentry passed, then the child has
	 * not been removed from its parent. This means the parent dentry must
	 * be valid and able to take a reference at this point.
	 */
	BUG_ON(!IS_ROOT(dentry) && dentry->d_parent != parent);
	BUG_ON(!parent->d_count);
	parent->d_count++;
	spin_unlock(&dentry->d_lock);
	spin_unlock(&parent->d_lock);
	if (nd->root.mnt) {
		path_get(&nd->root);
		spin_unlock(&fs->lock);
	}
	mntget(nd->path.mnt);

	rcu_read_unlock();
	br_read_unlock(vfsmount_lock);
	nd->flags &= ~LOOKUP_RCU;
	return 0;
err:
	spin_unlock(&dentry->d_lock);
	spin_unlock(&parent->d_lock);
err_root:
	if (nd->root.mnt)
		spin_unlock(&fs->lock);
	return -ECHILD;
}

/* Try to drop out of rcu-walk mode if we were in it, otherwise do nothing.  */
static inline int nameidata_dentry_drop_rcu_maybe(struct nameidata *nd, struct dentry *dentry)
{
	if (nd->flags & LOOKUP_RCU)
		return nameidata_dentry_drop_rcu(nd, dentry);
	return 0;
}

/**
 * nameidata_drop_rcu_last - drop nameidata ending path walk out of rcu-walk
 * @nd: nameidata pathwalk data to drop
 * @Returns: 0 on success, -ECHLID on failure
 *
 * nameidata_drop_rcu_last attempts to drop the current nd->path into ref-walk.
 * nd->path should be the final element of the lookup, so nd->root is discarded.
 * Must be called from rcu-walk context.
 */
static int nameidata_drop_rcu_last(struct nameidata *nd)
{
	struct dentry *dentry = nd->path.dentry;

	BUG_ON(!(nd->flags & LOOKUP_RCU));
	nd->flags &= ~LOOKUP_RCU;
	nd->root.mnt = NULL;
	spin_lock(&dentry->d_lock);
	if (!__d_rcu_to_refcount(dentry, nd->seq))
		goto err_unlock;
	BUG_ON(nd->inode != dentry->d_inode);
	spin_unlock(&dentry->d_lock);

	mntget(nd->path.mnt);

	rcu_read_unlock();
	br_read_unlock(vfsmount_lock);

	return 0;

err_unlock:
	spin_unlock(&dentry->d_lock);
	rcu_read_unlock();
	br_read_unlock(vfsmount_lock);
	return -ECHILD;
}

/* Try to drop out of rcu-walk mode if we were in it, otherwise do nothing.  */
static inline int nameidata_drop_rcu_last_maybe(struct nameidata *nd)
{
	if (likely(nd->flags & LOOKUP_RCU))
		return nameidata_drop_rcu_last(nd);
	return 0;
}

/**
 * release_open_intent - free up open intent resources
 * @nd: pointer to nameidata
 */
void release_open_intent(struct nameidata *nd)
{
	if (nd->intent.open.file->f_path.dentry == NULL)
		put_filp(nd->intent.open.file);
	else
		fput(nd->intent.open.file);
}

static inline struct dentry *
do_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	int status = dentry->d_op->d_revalidate(dentry, nd);
	if (unlikely(status <= 0)) {
		/*
		 * The dentry failed validation.
		 * If d_revalidate returned 0 attempt to invalidate
		 * the dentry otherwise d_revalidate is asking us
		 * to return a fail status.
		 */
		if (!status) {
			if (!d_invalidate(dentry)) {
				dput(dentry);
				dentry = NULL;
			}
		} else {
			dput(dentry);
			dentry = ERR_PTR(status);
		}
	}
	return dentry;
}

/*
 * force_reval_path - force revalidation of a dentry
 *
 * In some situations the path walking code will trust dentries without
 * revalidating them. This causes problems for filesystems that depend on
 * d_revalidate to handle file opens (e.g. NFSv4). When FS_REVAL_DOT is set
 * (which indicates that it's possible for the dentry to go stale), force
 * a d_revalidate call before proceeding.
 *
 * Returns 0 if the revalidation was successful. If the revalidation fails,
 * either return the error returned by d_revalidate or -ESTALE if the
 * revalidation it just returned 0. If d_revalidate returns 0, we attempt to
 * invalidate the dentry. It's up to the caller to handle putting references
 * to the path if necessary.
 */
static int
force_reval_path(struct path *path, struct nameidata *nd)
{
	int status;
	struct dentry *dentry = path->dentry;

	/*
	 * only check on filesystems where it's possible for the dentry to
	 * become stale. It's assumed that if this flag is set then the
	 * d_revalidate op will also be defined.
	 */
	if (!(dentry->d_sb->s_type->fs_flags & FS_REVAL_DOT))
		return 0;

	status = dentry->d_op->d_revalidate(dentry, nd);
	if (status > 0)
		return 0;

	if (!status) {
		d_invalidate(dentry);
		status = -ESTALE;
	}
	return status;
}

/*
 * Short-cut version of permission(), for calling on directories
 * during pathname resolution.  Combines parts of permission()
 * and generic_permission(), and tests ONLY for MAY_EXEC permission.
 *
 * If appropriate, check DAC only.  If not appropriate, or
 * short-cut DAC fails, then call ->permission() to do more
 * complete permission check.
 */
static inline int __exec_permission(struct inode *inode, int rcu)
{
	int ret;

	if (inode->i_op->permission) {
		if (rcu)
			return -ECHILD;
		ret = inode->i_op->permission(inode, MAY_EXEC);
		if (!ret)
			goto ok;
		return ret;
	}
	ret = __acl_permission_check(inode, MAY_EXEC, inode->i_op->check_acl, rcu);
	if (!ret)
		goto ok;
	if (rcu && ret == -ECHILD)
		return ret;

	if (capable(CAP_DAC_OVERRIDE) || capable(CAP_DAC_READ_SEARCH))
		goto ok;

	return ret;
ok:
	return security_inode_exec_permission(inode, rcu);
}

static int exec_permission(struct inode *inode)
{
	return __exec_permission(inode, 0);
}

static int exec_permission_rcu(struct inode *inode)
{
	return __exec_permission(inode, 1);
}

static __always_inline void set_root(struct nameidata *nd)
{
	if (!nd->root.mnt)
		get_fs_root(current->fs, &nd->root);
}

static int link_path_walk(const char *, struct nameidata *);

static __always_inline void set_root_rcu(struct nameidata *nd)
{
	if (!nd->root.mnt) {
		struct fs_struct *fs = current->fs;
		unsigned seq;

		do {
			seq = read_seqcount_begin(&fs->seq);
			nd->root = fs->root;
		} while (read_seqcount_retry(&fs->seq, seq));
	}
}

static __always_inline int __vfs_follow_link(struct nameidata *nd, const char *link)
{
	int ret;

	if (IS_ERR(link))
		goto fail;

	if (*link == '/') {
		set_root(nd);
		path_put(&nd->path);
		nd->path = nd->root;
		path_get(&nd->root);
	}
	nd->inode = nd->path.dentry->d_inode;

	ret = link_path_walk(link, nd);
	return ret;
fail:
	path_put(&nd->path);
	return PTR_ERR(link);
}

static void path_put_conditional(struct path *path, struct nameidata *nd)
{
	dput(path->dentry);
	if (path->mnt != nd->path.mnt)
		mntput(path->mnt);
}

static inline void path_to_nameidata(struct path *path, struct nameidata *nd)
{
	if (!(nd->flags & LOOKUP_RCU)) {
		dput(nd->path.dentry);
		if (nd->path.mnt != path->mnt)
			mntput(nd->path.mnt);
	}
	nd->path.mnt = path->mnt;
	nd->path.dentry = path->dentry;
}

static __always_inline int
__do_follow_link(struct path *path, struct nameidata *nd, void **p)
{
	int error;
	struct dentry *dentry = path->dentry;

	touch_atime(path->mnt, dentry);
	nd_set_link(nd, NULL);

	if (path->mnt != nd->path.mnt) {
		path_to_nameidata(path, nd);
		nd->inode = nd->path.dentry->d_inode;
		dget(dentry);
	}
	mntget(path->mnt);

	nd->last_type = LAST_BIND;
	*p = dentry->d_inode->i_op->follow_link(dentry, nd);
	error = PTR_ERR(*p);
	if (!IS_ERR(*p)) {
		char *s = nd_get_link(nd);
		error = 0;
		if (s)
			error = __vfs_follow_link(nd, s);
		else if (nd->last_type == LAST_BIND) {
			error = force_reval_path(&nd->path, nd);
			if (error)
				path_put(&nd->path);
		}
	}
	return error;
}

/*
 * This limits recursive symlink follows to 8, while
 * limiting consecutive symlinks to 40.
 *
 * Without that kind of total limit, nasty chains of consecutive
 * symlinks can cause almost arbitrarily long lookups. 
 */
static inline int do_follow_link(struct path *path, struct nameidata *nd)
{
	void *cookie;
	int err = -ELOOP;
	if (current->link_count >= MAX_NESTED_LINKS)
		goto loop;
	if (current->total_link_count >= 40)
		goto loop;
	BUG_ON(nd->depth >= MAX_NESTED_LINKS);
	cond_resched();
	err = security_inode_follow_link(path->dentry, nd);
	if (err)
		goto loop;
	current->link_count++;
	current->total_link_count++;
	nd->depth++;
	err = __do_follow_link(path, nd, &cookie);
	if (!IS_ERR(cookie) && path->dentry->d_inode->i_op->put_link)
		path->dentry->d_inode->i_op->put_link(path->dentry, nd, cookie);
	path_put(path);
	current->link_count--;
	nd->depth--;
	return err;
loop:
	path_put_conditional(path, nd);
	path_put(&nd->path);
	return err;
}

static int follow_up_rcu(struct path *path)
{
	struct vfsmount *parent;
	struct dentry *mountpoint;

	parent = path->mnt->mnt_parent;
	if (parent == path->mnt)
		return 0;
	mountpoint = path->mnt->mnt_mountpoint;
	path->dentry = mountpoint;
	path->mnt = parent;
	return 1;
}

int follow_up(struct path *path)
{
	struct vfsmount *parent;
	struct dentry *mountpoint;

	br_read_lock(vfsmount_lock);
	parent = path->mnt->mnt_parent;
	if (parent == path->mnt) {
		br_read_unlock(vfsmount_lock);
		return 0;
	}
	mntget(parent);
	mountpoint = dget(path->mnt->mnt_mountpoint);
	br_read_unlock(vfsmount_lock);
	dput(path->dentry);
	path->dentry = mountpoint;
	mntput(path->mnt);
	path->mnt = parent;
	return 1;
}

/*
 * serialization is taken care of in namespace.c
 */
static void __follow_mount_rcu(struct nameidata *nd, struct path *path,
				struct inode **inode)
{
	while (d_mountpoint(path->dentry)) {
		struct vfsmount *mounted;
		mounted = __lookup_mnt(path->mnt, path->dentry, 1);
		if (!mounted)
			return;
		path->mnt = mounted;
		path->dentry = mounted->mnt_root;
		nd->seq = read_seqcount_begin(&path->dentry->d_seq);
		*inode = path->dentry->d_inode;
	}
}

static int __follow_mount(struct path *path)
{
	int res = 0;
	while (d_mountpoint(path->dentry)) {
		struct vfsmount *mounted = lookup_mnt(path);
		if (!mounted)
			break;
		dput(path->dentry);
		if (res)
			mntput(path->mnt);
		path->mnt = mounted;
		path->dentry = dget(mounted->mnt_root);
		res = 1;
	}
	return res;
}

static void follow_mount(struct path *path)
{
	while (d_mountpoint(path->dentry)) {
		struct vfsmount *mounted = lookup_mnt(path);
		if (!mounted)
			break;
		dput(path->dentry);
		mntput(path->mnt);
		path->mnt = mounted;
		path->dentry = dget(mounted->mnt_root);
	}
}

int follow_down(struct path *path)
{
	struct vfsmount *mounted;

	mounted = lookup_mnt(path);
	if (mounted) {
		dput(path->dentry);
		mntput(path->mnt);
		path->mnt = mounted;
		path->dentry = dget(mounted->mnt_root);
		return 1;
	}
	return 0;
}

static int follow_dotdot_rcu(struct nameidata *nd)
{
	struct inode *inode = nd->inode;

	set_root_rcu(nd);

	while(1) {
		if (nd->path.dentry == nd->root.dentry &&
		    nd->path.mnt == nd->root.mnt) {
			break;
		}
		if (nd->path.dentry != nd->path.mnt->mnt_root) {
			struct dentry *old = nd->path.dentry;
			struct dentry *parent = old->d_parent;
			unsigned seq;

			seq = read_seqcount_begin(&parent->d_seq);
			if (read_seqcount_retry(&old->d_seq, nd->seq))
				return -ECHILD;
			inode = parent->d_inode;
			nd->path.dentry = parent;
			nd->seq = seq;
			break;
		}
		if (!follow_up_rcu(&nd->path))
			break;
		nd->seq = read_seqcount_begin(&nd->path.dentry->d_seq);
		inode = nd->path.dentry->d_inode;
	}
	__follow_mount_rcu(nd, &nd->path, &inode);
	nd->inode = inode;

	return 0;
}

static void follow_dotdot(struct nameidata *nd)
{
	set_root(nd);

	while(1) {
		struct dentry *old = nd->path.dentry;

		if (nd->path.dentry == nd->root.dentry &&
		    nd->path.mnt == nd->root.mnt) {
			break;
		}
		if (nd->path.dentry != nd->path.mnt->mnt_root) {
			/* rare case of legitimate dget_parent()... */
			nd->path.dentry = dget_parent(nd->path.dentry);
			dput(old);
			break;
		}
		if (!follow_up(&nd->path))
			break;
	}
	follow_mount(&nd->path);
	nd->inode = nd->path.dentry->d_inode;
}

/*
 * Allocate a dentry with name and parent, and perform a parent
 * directory ->lookup on it. Returns the new dentry, or ERR_PTR
 * on error. parent->d_inode->i_mutex must be held. d_lookup must
 * have verified that no child exists while under i_mutex.
 */
static struct dentry *d_alloc_and_lookup(struct dentry *parent,
				struct qstr *name, struct nameidata *nd)
{
	struct inode *inode = parent->d_inode;
	struct dentry *dentry;
	struct dentry *old;

	/* Don't create child dentry for a dead directory. */
	if (unlikely(IS_DEADDIR(inode)))
		return ERR_PTR(-ENOENT);

	dentry = d_alloc(parent, name);
	if (unlikely(!dentry))
		return ERR_PTR(-ENOMEM);

	old = inode->i_op->lookup(inode, dentry, nd);
	if (unlikely(old)) {
		dput(dentry);
		dentry = old;
	}
	return dentry;
}

/*
 *  It's more convoluted than I'd like it to be, but... it's still fairly
 *  small and for now I'd prefer to have fast path as straight as possible.
 *  It _is_ time-critical.
 */
static int do_lookup(struct nameidata *nd, struct qstr *name,
			struct path *path, struct inode **inode)
{
	struct vfsmount *mnt = nd->path.mnt;
	struct dentry *dentry, *parent = nd->path.dentry;
	struct inode *dir;
	/*
	 * See if the low-level filesystem might want
	 * to use its own hash..
	 */
	if (parent->d_op && parent->d_op->d_hash) {
		int err = parent->d_op->d_hash(parent, nd->inode, name);
		if (err < 0)
			return err;
	}

	/*
	 * Rename seqlock is not required here because in the off chance
	 * of a false negative due to a concurrent rename, we're going to
	 * do the non-racy lookup, below.
	 */
	if (nd->flags & LOOKUP_RCU) {
		unsigned seq;

		*inode = nd->inode;
		dentry = __d_lookup_rcu(parent, name, &seq, inode);
		if (!dentry) {
			if (nameidata_drop_rcu(nd))
				return -ECHILD;
			goto need_lookup;
		}
		/* Memory barrier in read_seqcount_begin of child is enough */
		if (__read_seqcount_retry(&parent->d_seq, nd->seq))
			return -ECHILD;

		nd->seq = seq;
		if (dentry->d_op && dentry->d_op->d_revalidate) {
			/* We commonly drop rcu-walk here */
			if (nameidata_dentry_drop_rcu(nd, dentry))
				return -ECHILD;
			goto need_revalidate;
		}
		path->mnt = mnt;
		path->dentry = dentry;
		__follow_mount_rcu(nd, path, inode);
	} else {
		dentry = __d_lookup(parent, name);
		if (!dentry)
			goto need_lookup;
found:
		if (dentry->d_op && dentry->d_op->d_revalidate)
			goto need_revalidate;
done:
		path->mnt = mnt;
		path->dentry = dentry;
		__follow_mount(path);
		*inode = path->dentry->d_inode;
	}
	return 0;

need_lookup:
	dir = parent->d_inode;
	BUG_ON(nd->inode != dir);

	mutex_lock(&dir->i_mutex);
	/*
	 * First re-do the cached lookup just in case it was created
	 * while we waited for the directory semaphore, or the first
	 * lookup failed due to an unrelated rename.
	 *
	 * This could use version numbering or similar to avoid unnecessary
	 * cache lookups, but then we'd have to do the first lookup in the
	 * non-racy way. However in the common case here, everything should
	 * be hot in cache, so would it be a big win?
	 */
	dentry = d_lookup(parent, name);
	if (likely(!dentry)) {
		dentry = d_alloc_and_lookup(parent, name, nd);
		mutex_unlock(&dir->i_mutex);
		if (IS_ERR(dentry))
			goto fail;
		goto done;
	}
	/*
	 * Uhhuh! Nasty case: the cache was re-populated while
	 * we waited on the semaphore. Need to revalidate.
	 */
	mutex_unlock(&dir->i_mutex);
	goto found;

need_revalidate:
	dentry = do_revalidate(dentry, nd);
	if (!dentry)
		goto need_lookup;
	if (IS_ERR(dentry))
		goto fail;
	goto done;

fail:
	return PTR_ERR(dentry);
}

/*
 * This is a temporary kludge to deal with "automount" symlinks; proper
 * solution is to trigger them on follow_mount(), so that do_lookup()
 * would DTRT.  To be killed before 2.6.34-final.
 */
static inline int follow_on_final(struct inode *inode, unsigned lookup_flags)
{
	return inode && unlikely(inode->i_op->follow_link) &&
		((lookup_flags & LOOKUP_FOLLOW) || S_ISDIR(inode->i_mode));
}

/*
 * Name resolution.
 * This is the basic name resolution function, turning a pathname into
 * the final dentry. We expect 'base' to be positive and a directory.
 *
 * Returns 0 and nd will have valid dentry and mnt on success.
 * Returns error and drops reference to input namei data on failure.
 */
static int link_path_walk(const char *name, struct nameidata *nd)
{
	struct path next;
	int err;
	unsigned int lookup_flags = nd->flags;
	
	while (*name=='/')
		name++;
	if (!*name)
		goto return_reval;

	if (nd->depth)
		lookup_flags = LOOKUP_FOLLOW | (nd->flags & LOOKUP_CONTINUE);

	/* At this point we know we have a real path component. */
	for(;;) {
		struct inode *inode;
		unsigned long hash;
		struct qstr this;
		unsigned int c;

		nd->flags |= LOOKUP_CONTINUE;
		if (nd->flags & LOOKUP_RCU) {
			err = exec_permission_rcu(nd->inode);
			if (err == -ECHILD) {
				if (nameidata_drop_rcu(nd))
					return -ECHILD;
				goto exec_again;
			}
		} else {
exec_again:
			err = exec_permission(nd->inode);
		}
 		if (err)
			break;

		this.name = name;
		c = *(const unsigned char *)name;

		hash = init_name_hash();
		do {
			name++;
			hash = partial_name_hash(c, hash);
			c = *(const unsigned char *)name;
		} while (c && (c != '/'));
		this.len = name - (const char *) this.name;
		this.hash = end_name_hash(hash);

		/* remove trailing slashes? */
		if (!c)
			goto last_component;
		while (*++name == '/');
		if (!*name)
			goto last_with_slashes;

		/*
		 * "." and ".." are special - ".." especially so because it has
		 * to be able to know about the current root directory and
		 * parent relationships.
		 */
		if (this.name[0] == '.') switch (this.len) {
			default:
				break;
			case 2:
				if (this.name[1] != '.')
					break;
				if (nd->flags & LOOKUP_RCU) {
					if (follow_dotdot_rcu(nd))
						return -ECHILD;
				} else
					follow_dotdot(nd);
				/* fallthrough */
			case 1:
				continue;
		}
		/* This does the actual lookups.. */
		err = do_lookup(nd, &this, &next, &inode);
		if (err)
			break;
		err = -ENOENT;
		if (!inode)
			goto out_dput;

		if (inode->i_op->follow_link) {
			/* We commonly drop rcu-walk here */
			if (nameidata_dentry_drop_rcu_maybe(nd, next.dentry))
				return -ECHILD;
			BUG_ON(inode != next.dentry->d_inode);
			err = do_follow_link(&next, nd);
			if (err)
				goto return_err;
			nd->inode = nd->path.dentry->d_inode;
			err = -ENOENT;
			if (!nd->inode)
				break;
		} else {
			path_to_nameidata(&next, nd);
			nd->inode = inode;
		}
		err = -ENOTDIR; 
		if (!nd->inode->i_op->lookup)
			break;
		continue;
		/* here ends the main loop */

last_with_slashes:
		lookup_flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;
last_component:
		/* Clear LOOKUP_CONTINUE iff it was previously unset */
		nd->flags &= lookup_flags | ~LOOKUP_CONTINUE;
		if (lookup_flags & LOOKUP_PARENT)
			goto lookup_parent;
		if (this.name[0] == '.') switch (this.len) {
			default:
				break;
			case 2:
				if (this.name[1] != '.')
					break;
				if (nd->flags & LOOKUP_RCU) {
					if (follow_dotdot_rcu(nd))
						return -ECHILD;
				} else
					follow_dotdot(nd);
				/* fallthrough */
			case 1:
				goto return_reval;
		}
		err = do_lookup(nd, &this, &next, &inode);
		if (err)
			break;
		if (follow_on_final(inode, lookup_flags)) {
			if (nameidata_dentry_drop_rcu_maybe(nd, next.dentry))
				return -ECHILD;
			BUG_ON(inode != next.dentry->d_inode);
			err = do_follow_link(&next, nd);
			if (err)
				goto return_err;
			nd->inode = nd->path.dentry->d_inode;
		} else {
			path_to_nameidata(&next, nd);
			nd->inode = inode;
		}
		err = -ENOENT;
		if (!nd->inode)
			break;
		if (lookup_flags & LOOKUP_DIRECTORY) {
			err = -ENOTDIR; 
			if (!nd->inode->i_op->lookup)
				break;
		}
		goto return_base;
lookup_parent:
		nd->last = this;
		nd->last_type = LAST_NORM;
		if (this.name[0] != '.')
			goto return_base;
		if (this.len == 1)
			nd->last_type = LAST_DOT;
		else if (this.len == 2 && this.name[1] == '.')
			nd->last_type = LAST_DOTDOT;
		else
			goto return_base;
return_reval:
		/*
		 * We bypassed the ordinary revalidation routines.
		 * We may need to check the cached dentry for staleness.
		 */
		if (nd->path.dentry && nd->path.dentry->d_sb &&
		    (nd->path.dentry->d_sb->s_type->fs_flags & FS_REVAL_DOT)) {
			if (nameidata_drop_rcu_maybe(nd))
				return -ECHILD;
			err = -ESTALE;
			/* Note: we do not d_invalidate() */
			if (!nd->path.dentry->d_op->d_revalidate(
					nd->path.dentry, nd))
				break;
		}
return_base:
		if (nameidata_drop_rcu_last_maybe(nd))
			return -ECHILD;
		return 0;
out_dput:
		if (!(nd->flags & LOOKUP_RCU))
			path_put_conditional(&next, nd);
		break;
	}
	if (!(nd->flags & LOOKUP_RCU))
		path_put(&nd->path);
return_err:
	return err;
}

static inline int path_walk_rcu(const char *name, struct nameidata *nd)
{
	current->total_link_count = 0;

	return link_path_walk(name, nd);
}

static inline int path_walk_simple(const char *name, struct nameidata *nd)
{
	current->total_link_count = 0;

	return link_path_walk(name, nd);
}

static int path_walk(const char *name, struct nameidata *nd)
{
	struct path save = nd->path;
	int result;

	current->total_link_count = 0;

	/* make sure the stuff we saved doesn't go away */
	path_get(&save);

	result = link_path_walk(name, nd);
	if (result == -ESTALE) {
		/* nd->path had been dropped */
		current->total_link_count = 0;
		nd->path = save;
		path_get(&nd->path);
		nd->flags |= LOOKUP_REVAL;
		result = link_path_walk(name, nd);
	}

	path_put(&save);

	return result;
}

static void path_finish_rcu(struct nameidata *nd)
{
	if (nd->flags & LOOKUP_RCU) {
		/* RCU dangling. Cancel it. */
		nd->flags &= ~LOOKUP_RCU;
		nd->root.mnt = NULL;
		rcu_read_unlock();
		br_read_unlock(vfsmount_lock);
	}
	if (nd->file)
		fput(nd->file);
}

static int path_init_rcu(int dfd, const char *name, unsigned int flags, struct nameidata *nd)
{
	int retval = 0;
	int fput_needed;
	struct file *file;

	nd->last_type = LAST_ROOT; /* if there are only slashes... */
	nd->flags = flags | LOOKUP_RCU;
	nd->depth = 0;
	nd->root.mnt = NULL;
	nd->file = NULL;

	if (*name=='/') {
		struct fs_struct *fs = current->fs;
		unsigned seq;

		br_read_lock(vfsmount_lock);
		rcu_read_lock();

		do {
			seq = read_seqcount_begin(&fs->seq);
			nd->root = fs->root;
			nd->path = nd->root;
			nd->seq = __read_seqcount_begin(&nd->path.dentry->d_seq);
		} while (read_seqcount_retry(&fs->seq, seq));

	} else if (dfd == AT_FDCWD) {
		struct fs_struct *fs = current->fs;
		unsigned seq;

		br_read_lock(vfsmount_lock);
		rcu_read_lock();

		do {
			seq = read_seqcount_begin(&fs->seq);
			nd->path = fs->pwd;
			nd->seq = __read_seqcount_begin(&nd->path.dentry->d_seq);
		} while (read_seqcount_retry(&fs->seq, seq));

	} else {
		struct dentry *dentry;

		file = fget_light(dfd, &fput_needed);
		retval = -EBADF;
		if (!file)
			goto out_fail;

		dentry = file->f_path.dentry;

		retval = -ENOTDIR;
		if (!S_ISDIR(dentry->d_inode->i_mode))
			goto fput_fail;

		retval = file_permission(file, MAY_EXEC);
		if (retval)
			goto fput_fail;

		nd->path = file->f_path;
		if (fput_needed)
			nd->file = file;

		nd->seq = __read_seqcount_begin(&nd->path.dentry->d_seq);
		br_read_lock(vfsmount_lock);
		rcu_read_lock();
	}
	nd->inode = nd->path.dentry->d_inode;
	return 0;

fput_fail:
	fput_light(file, fput_needed);
out_fail:
	return retval;
}

static int path_init(int dfd, const char *name, unsigned int flags, struct nameidata *nd)
{
	int retval = 0;
	int fput_needed;
	struct file *file;

	nd->last_type = LAST_ROOT; /* if there are only slashes... */
	nd->flags = flags;
	nd->depth = 0;
	nd->root.mnt = NULL;

	if (*name=='/') {
		set_root(nd);
		nd->path = nd->root;
		path_get(&nd->root);
	} else if (dfd == AT_FDCWD) {
		get_fs_pwd(current->fs, &nd->path);
	} else {
		struct dentry *dentry;

		file = fget_light(dfd, &fput_needed);
		retval = -EBADF;
		if (!file)
			goto out_fail;

		dentry = file->f_path.dentry;

		retval = -ENOTDIR;
		if (!S_ISDIR(dentry->d_inode->i_mode))
			goto fput_fail;

		retval = file_permission(file, MAY_EXEC);
		if (retval)
			goto fput_fail;

		nd->path = file->f_path;
		path_get(&file->f_path);

		fput_light(file, fput_needed);
	}
	nd->inode = nd->path.dentry->d_inode;
	return 0;

fput_fail:
	fput_light(file, fput_needed);
out_fail:
	return retval;
}

/* Returns 0 and nd will be valid on success; Retuns error, otherwise. */
static int do_path_lookup(int dfd, const char *name,
				unsigned int flags, struct nameidata *nd)
{
	int retval;

	/*
	 * Path walking is largely split up into 2 different synchronisation
	 * schemes, rcu-walk and ref-walk (explained in
	 * Documentation/filesystems/path-lookup.txt). These share much of the
	 * path walk code, but some things particularly setup, cleanup, and
	 * following mounts are sufficiently divergent that functions are
	 * duplicated. Typically there is a function foo(), and its RCU
	 * analogue, foo_rcu().
	 *
	 * -ECHILD is the error number of choice (just to avoid clashes) that
	 * is returned if some aspect of an rcu-walk fails. Such an error must
	 * be handled by restarting a traditional ref-walk (which will always
	 * be able to complete).
	 */
	retval = path_init_rcu(dfd, name, flags, nd);
	if (unlikely(retval))
		return retval;
	retval = path_walk_rcu(name, nd);
	path_finish_rcu(nd);
	if (nd->root.mnt) {
		path_put(&nd->root);
		nd->root.mnt = NULL;
	}

	if (unlikely(retval == -ECHILD || retval == -ESTALE)) {
		/* slower, locked walk */
		if (retval == -ESTALE)
			flags |= LOOKUP_REVAL;
		retval = path_init(dfd, name, flags, nd);
		if (unlikely(retval))
			return retval;
		retval = path_walk(name, nd);
		if (nd->root.mnt) {
			path_put(&nd->root);
			nd->root.mnt = NULL;
		}
	}

	if (likely(!retval)) {
		if (unlikely(!audit_dummy_context())) {
			if (nd->path.dentry && nd->inode)
				audit_inode(name, nd->path.dentry);
		}
	}

	return retval;
}

int path_lookup(const char *name, unsigned int flags,
			struct nameidata *nd)
{
	return do_path_lookup(AT_FDCWD, name, flags, nd);
}

int kern_path(const char *name, unsigned int flags, struct path *path)
{
	struct nameidata nd;
	int res = do_path_lookup(AT_FDCWD, name, flags, &nd);
	if (!res)
		*path = nd.path;
	return res;
}

/**
 * vfs_path_lookup - lookup a file path relative to a dentry-vfsmount pair
 * @dentry:  pointer to dentry of the base directory
 * @mnt: pointer to vfs mount of the base directory
 * @name: pointer to file name
 * @flags: lookup flags
 * @nd: pointer to nameidata
 */
int vfs_path_lookup(struct dentry *dentry, struct vfsmount *mnt,
		    const char *name, unsigned int flags,
		    struct nameidata *nd)
{
	int retval;

	/* same as do_path_lookup */
	nd->last_type = LAST_ROOT;
	nd->flags = flags;
	nd->depth = 0;

	nd->path.dentry = dentry;
	nd->path.mnt = mnt;
	path_get(&nd->path);
	nd->root = nd->path;
	path_get(&nd->root);
	nd->inode = nd->path.dentry->d_inode;

	retval = path_walk(name, nd);
	if (unlikely(!retval && !audit_dummy_context() && nd->path.dentry &&
				nd->inode))
		audit_inode(name, nd->path.dentry);

	path_put(&nd->root);
	nd->root.mnt = NULL;

	return retval;
}

static struct dentry *__lookup_hash(struct qstr *name,
		struct dentry *base, struct nameidata *nd)
{
	struct inode *inode = base->d_inode;
	struct dentry *dentry;
	int err;

	err = exec_permission(inode);
	if (err)
		return ERR_PTR(err);

	/*
	 * See if the low-level filesystem might want
	 * to use its own hash..
	 */
	if (base->d_op && base->d_op->d_hash) {
		err = base->d_op->d_hash(base, inode, name);
		dentry = ERR_PTR(err);
		if (err < 0)
			goto out;
	}

	/*
	 * Don't bother with __d_lookup: callers are for creat as
	 * well as unlink, so a lot of the time it would cost
	 * a double lookup.
	 */
	dentry = d_lookup(base, name);

	if (dentry && dentry->d_op && dentry->d_op->d_revalidate)
		dentry = do_revalidate(dentry, nd);

	if (!dentry)
		dentry = d_alloc_and_lookup(base, name, nd);
out:
	return dentry;
}

/*
 * Restricted form of lookup. Doesn't follow links, single-component only,
 * needs parent already locked. Doesn't follow mounts.
 * SMP-safe.
 */
static struct dentry *lookup_hash(struct nameidata *nd)
{
	return __lookup_hash(&nd->last, nd->path.dentry, nd);
}

static int __lookup_one_len(const char *name, struct qstr *this,
		struct dentry *base, int len)
{
	unsigned long hash;
	unsigned int c;

	this->name = name;
	this->len = len;
	if (!len)
		return -EACCES;

	hash = init_name_hash();
	while (len--) {
		c = *(const unsigned char *)name++;
		if (c == '/' || c == '\0')
			return -EACCES;
		hash = partial_name_hash(c, hash);
	}
	this->hash = end_name_hash(hash);
	return 0;
}

/**
 * lookup_one_len - filesystem helper to lookup single pathname component
 * @name:	pathname component to lookup
 * @base:	base directory to lookup from
 * @len:	maximum length @len should be interpreted to
 *
 * Note that this routine is purely a helper for filesystem usage and should
 * not be called by generic code.  Also note that by using this function the
 * nameidata argument is passed to the filesystem methods and a filesystem
 * using this helper needs to be prepared for that.
 */
struct dentry *lookup_one_len(const char *name, struct dentry *base, int len)
{
	int err;
	struct qstr this;

	WARN_ON_ONCE(!mutex_is_locked(&base->d_inode->i_mutex));

	err = __lookup_one_len(name, &this, base, len);
	if (err)
		return ERR_PTR(err);

	return __lookup_hash(&this, base, NULL);
}

int user_path_at(int dfd, const char __user *name, unsigned flags,
		 struct path *path)
{
	struct nameidata nd;
	char *tmp = getname(name);
	int err = PTR_ERR(tmp);
	if (!IS_ERR(tmp)) {

		BUG_ON(flags & LOOKUP_PARENT);

		err = do_path_lookup(dfd, tmp, flags, &nd);
		putname(tmp);
		if (!err)
			*path = nd.path;
	}
	return err;
}

static int user_path_parent(int dfd, const char __user *path,
			struct nameidata *nd, char **name)
{
	char *s = getname(path);
	int error;

	if (IS_ERR(s))
		return PTR_ERR(s);

	error = do_path_lookup(dfd, s, LOOKUP_PARENT, nd);
	if (error)
		putname(s);
	else
		*name = s;

	return error;
}

/*
 * It's inline, so penalty for filesystems that don't use sticky bit is
 * minimal.
 */
static inline int check_sticky(struct inode *dir, struct inode *inode)
{
	uid_t fsuid = current_fsuid();

	if (!(dir->i_mode & S_ISVTX))
		return 0;
	if (inode->i_uid == fsuid)
		return 0;
	if (dir->i_uid == fsuid)
		return 0;
	return !capable(CAP_FOWNER);
}

/*
 *	Check whether we can remove a link victim from directory dir, check
 *  whether the type of victim is right.
 *  1. We can't do it if dir is read-only (done in permission())
 *  2. We should have write and exec permissions on dir
 *  3. We can't remove anything from append-only dir
 *  4. We can't do anything with immutable dir (done in permission())
 *  5. If the sticky bit on dir is set we should either
 *	a. be owner of dir, or
 *	b. be owner of victim, or
 *	c. have CAP_FOWNER capability
 *  6. If the victim is append-only or immutable we can't do antyhing with
 *     links pointing to it.
 *  7. If we were asked to remove a directory and victim isn't one - ENOTDIR.
 *  8. If we were asked to remove a non-directory and victim isn't one - EISDIR.
 *  9. We can't remove a root or mountpoint.
 * 10. We don't allow removal of NFS sillyrenamed files; it's handled by
 *     nfs_async_unlink().
 */
static int may_delete(struct inode *dir,struct dentry *victim,int isdir)
{
	int error;

	if (!victim->d_inode)
		return -ENOENT;

	BUG_ON(victim->d_parent->d_inode != dir);
	audit_inode_child(victim, dir);

	error = inode_permission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;
	if (IS_APPEND(dir))
		return -EPERM;
	if (check_sticky(dir, victim->d_inode)||IS_APPEND(victim->d_inode)||
	    IS_IMMUTABLE(victim->d_inode) || IS_SWAPFILE(victim->d_inode))
		return -EPERM;
	if (isdir) {
		if (!S_ISDIR(victim->d_inode->i_mode))
			return -ENOTDIR;
		if (IS_ROOT(victim))
			return -EBUSY;
	} else if (S_ISDIR(victim->d_inode->i_mode))
		return -EISDIR;
	if (IS_DEADDIR(dir))
		return -ENOENT;
	if (victim->d_flags & DCACHE_NFSFS_RENAMED)
		return -EBUSY;
	return 0;
}

/*	Check whether we can create an object with dentry child in directory
 *  dir.
 *  1. We can't do it if child already exists (open has special treatment for
 *     this case, but since we are inlined it's OK)
 *  2. We can't do it if dir is read-only (done in permission())
 *  3. We should have write and exec permissions on dir
 *  4. We can't do it if dir is immutable (done in permission())
 */
static inline int may_create(struct inode *dir, struct dentry *child)
{
	if (child->d_inode)
		return -EEXIST;
	if (IS_DEADDIR(dir))
		return -ENOENT;
	return inode_permission(dir, MAY_WRITE | MAY_EXEC);
}

/*
 * p1 and p2 should be directories on the same fs.
 */
struct dentry *lock_rename(struct dentry *p1, struct dentry *p2)
{
	struct dentry *p;

	if (p1 == p2) {
		mutex_lock_nested(&p1->d_inode->i_mutex, I_MUTEX_PARENT);
		return NULL;
	}

	mutex_lock(&p1->d_inode->i_sb->s_vfs_rename_mutex);

	p = d_ancestor(p2, p1);
	if (p) {
		mutex_lock_nested(&p2->d_inode->i_mutex, I_MUTEX_PARENT);
		mutex_lock_nested(&p1->d_inode->i_mutex, I_MUTEX_CHILD);
		return p;
	}

	p = d_ancestor(p1, p2);
	if (p) {
		mutex_lock_nested(&p1->d_inode->i_mutex, I_MUTEX_PARENT);
		mutex_lock_nested(&p2->d_inode->i_mutex, I_MUTEX_CHILD);
		return p;
	}

	mutex_lock_nested(&p1->d_inode->i_mutex, I_MUTEX_PARENT);
	mutex_lock_nested(&p2->d_inode->i_mutex, I_MUTEX_CHILD);
	return NULL;
}

void unlock_rename(struct dentry *p1, struct dentry *p2)
{
	mutex_unlock(&p1->d_inode->i_mutex);
	if (p1 != p2) {
		mutex_unlock(&p2->d_inode->i_mutex);
		mutex_unlock(&p1->d_inode->i_sb->s_vfs_rename_mutex);
	}
}

int vfs_create(struct inode *dir, struct dentry *dentry, int mode,
		struct nameidata *nd)
{
	int error = may_create(dir, dentry);

	if (error)
		return error;

	if (!dir->i_op->create)
		return -EACCES;	/* shouldn't it be ENOSYS? */
	mode &= S_IALLUGO;
	mode |= S_IFREG;
	error = security_inode_create(dir, dentry, mode);
	if (error)
		return error;
	error = dir->i_op->create(dir, dentry, mode, nd);
	if (!error)
		fsnotify_create(dir, dentry);
	return error;
}

int may_open(struct path *path, int acc_mode, int flag)
{
	struct dentry *dentry = path->dentry;
	struct inode *inode = dentry->d_inode;
	int error;

	if (!inode)
		return -ENOENT;

	switch (inode->i_mode & S_IFMT) {
	case S_IFLNK:
		return -ELOOP;
	case S_IFDIR:
		if (acc_mode & MAY_WRITE)
			return -EISDIR;
		break;
	case S_IFBLK:
	case S_IFCHR:
		if (path->mnt->mnt_flags & MNT_NODEV)
			return -EACCES;
		/*FALLTHRU*/
	case S_IFIFO:
	case S_IFSOCK:
		flag &= ~O_TRUNC;
		break;
	}

	error = inode_permission(inode, acc_mode);
	if (error)
		return error;

	/*
	 * An append-only file must be opened in append mode for writing.
	 */
	if (IS_APPEND(inode)) {
		if  ((flag & O_ACCMODE) != O_RDONLY && !(flag & O_APPEND))
			return -EPERM;
		if (flag & O_TRUNC)
			return -EPERM;
	}

	/* O_NOATIME can only be set by the owner or superuser */
	if (flag & O_NOATIME && !is_owner_or_cap(inode))
		return -EPERM;

	/*
	 * Ensure there are no outstanding leases on the file.
	 */
	return break_lease(inode, flag);
}

static int handle_truncate(struct path *path)
{
	struct inode *inode = path->dentry->d_inode;
	int error = get_write_access(inode);
	if (error)
		return error;
	/*
	 * Refuse to truncate files with mandatory locks held on them.
	 */
	error = locks_verify_locked(inode);
	if (!error)
		error = security_path_truncate(path);
	if (!error) {
		error = do_truncate(path->dentry, 0,
				    ATTR_MTIME|ATTR_CTIME|ATTR_OPEN,
				    NULL);
	}
	put_write_access(inode);
	return error;
}

/*
 * Be careful about ever adding any more callers of this
 * function.  Its flags must be in the namei format, not
 * what get passed to sys_open().
 */
static int __open_namei_create(struct nameidata *nd, struct path *path,
				int open_flag, int mode)
{
	int error;
	struct dentry *dir = nd->path.dentry;

	if (!IS_POSIXACL(dir->d_inode))
		mode &= ~current_umask();
	error = security_path_mknod(&nd->path, path->dentry, mode, 0);
	if (error)
		goto out_unlock;
	error = vfs_create(dir->d_inode, path->dentry, mode, nd);
out_unlock:
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(nd->path.dentry);
	nd->path.dentry = path->dentry;

	if (error)
		return error;
	/* Don't check for write permission, don't truncate */
	return may_open(&nd->path, 0, open_flag & ~O_TRUNC);
}

/*
 * Note that while the flag value (low two bits) for sys_open means:
 *	00 - read-only
 *	01 - write-only
 *	10 - read-write
 *	11 - special
 * it is changed into
 *	00 - no permissions needed
 *	01 - read-permission
 *	10 - write-permission
 *	11 - read-write
 * for the internal routines (ie open_namei()/follow_link() etc)
 * This is more logical, and also allows the 00 "no perm needed"
 * to be used for symlinks (where the permissions are checked
 * later).
 *
*/
static inline int open_to_namei_flags(int flag)
{
	if ((flag+1) & O_ACCMODE)
		flag++;
	return flag;
}

static int open_will_truncate(int flag, struct inode *inode)
{
	/*
	 * We'll never write to the fs underlying
	 * a device file.
	 */
	if (special_file(inode->i_mode))
		return 0;
	return (flag & O_TRUNC);
}

static struct file *finish_open(struct nameidata *nd,
				int open_flag, int acc_mode)
{
	struct file *filp;
	int will_truncate;
	int error;

	will_truncate = open_will_truncate(open_flag, nd->path.dentry->d_inode);
	if (will_truncate) {
		error = mnt_want_write(nd->path.mnt);
		if (error)
			goto exit;
	}
	error = may_open(&nd->path, acc_mode, open_flag);
	if (error) {
		if (will_truncate)
			mnt_drop_write(nd->path.mnt);
		goto exit;
	}
	filp = nameidata_to_filp(nd);
	if (!IS_ERR(filp)) {
		error = ima_file_check(filp, acc_mode);
		if (error) {
			fput(filp);
			filp = ERR_PTR(error);
		}
	}
	if (!IS_ERR(filp)) {
		if (will_truncate) {
			error = handle_truncate(&nd->path);
			if (error) {
				fput(filp);
				filp = ERR_PTR(error);
			}
		}
	}
	/*
	 * It is now safe to drop the mnt write
	 * because the filp has had a write taken
	 * on its behalf.
	 */
	if (will_truncate)
		mnt_drop_write(nd->path.mnt);
	path_put(&nd->path);
	return filp;

exit:
	if (!IS_ERR(nd->intent.open.file))
		release_open_intent(nd);
	path_put(&nd->path);
	return ERR_PTR(error);
}

/*
 * Handle O_CREAT case for do_filp_open
 */
static struct file *do_last(struct nameidata *nd, struct path *path,
			    int open_flag, int acc_mode,
			    int mode, const char *pathname)
{
	struct dentry *dir = nd->path.dentry;
	struct file *filp;
	int error = -EISDIR;

	switch (nd->last_type) {
	case LAST_DOTDOT:
		follow_dotdot(nd);
		dir = nd->path.dentry;
	case LAST_DOT:
		if (nd->path.mnt->mnt_sb->s_type->fs_flags & FS_REVAL_DOT) {
			if (!dir->d_op->d_revalidate(dir, nd)) {
				error = -ESTALE;
				goto exit;
			}
		}
		/* fallthrough */
	case LAST_ROOT:
		goto exit;
	case LAST_BIND:
		audit_inode(pathname, dir);
		goto ok;
	}

	/* trailing slashes? */
	if (nd->last.name[nd->last.len])
		goto exit;

	mutex_lock(&dir->d_inode->i_mutex);

	path->dentry = lookup_hash(nd);
	path->mnt = nd->path.mnt;

	error = PTR_ERR(path->dentry);
	if (IS_ERR(path->dentry)) {
		mutex_unlock(&dir->d_inode->i_mutex);
		goto exit;
	}

	if (IS_ERR(nd->intent.open.file)) {
		error = PTR_ERR(nd->intent.open.file);
		goto exit_mutex_unlock;
	}

	/* Negative dentry, just create the file */
	if (!path->dentry->d_inode) {
		/*
		 * This write is needed to ensure that a
		 * ro->rw transition does not occur between
		 * the time when the file is created and when
		 * a permanent write count is taken through
		 * the 'struct file' in nameidata_to_filp().
		 */
		error = mnt_want_write(nd->path.mnt);
		if (error)
			goto exit_mutex_unlock;
		error = __open_namei_create(nd, path, open_flag, mode);
		if (error) {
			mnt_drop_write(nd->path.mnt);
			goto exit;
		}
		filp = nameidata_to_filp(nd);
		mnt_drop_write(nd->path.mnt);
		path_put(&nd->path);
		if (!IS_ERR(filp)) {
			error = ima_file_check(filp, acc_mode);
			if (error) {
				fput(filp);
				filp = ERR_PTR(error);
			}
		}
		return filp;
	}

	/*
	 * It already exists.
	 */
	mutex_unlock(&dir->d_inode->i_mutex);
	audit_inode(pathname, path->dentry);

	error = -EEXIST;
	if (open_flag & O_EXCL)
		goto exit_dput;

	if (__follow_mount(path)) {
		error = -ELOOP;
		if (open_flag & O_NOFOLLOW)
			goto exit_dput;
	}

	error = -ENOENT;
	if (!path->dentry->d_inode)
		goto exit_dput;

	if (path->dentry->d_inode->i_op->follow_link)
		return NULL;

	path_to_nameidata(path, nd);
	nd->inode = path->dentry->d_inode;
	error = -EISDIR;
	if (S_ISDIR(nd->inode->i_mode))
		goto exit;
ok:
	filp = finish_open(nd, open_flag, acc_mode);
	return filp;

exit_mutex_unlock:
	mutex_unlock(&dir->d_inode->i_mutex);
exit_dput:
	path_put_conditional(path, nd);
exit:
	if (!IS_ERR(nd->intent.open.file))
		release_open_intent(nd);
	path_put(&nd->path);
	return ERR_PTR(error);
}

/*
 * Note that the low bits of the passed in "open_flag"
 * are not the same as in the local variable "flag". See
 * open_to_namei_flags() for more details.
 */
struct file *do_filp_open(int dfd, const char *pathname,
		int open_flag, int mode, int acc_mode)
{
	struct file *filp;
	struct nameidata nd;
	int error;
	struct path path;
	int count = 0;
	int flag = open_to_namei_flags(open_flag);
	int flags;

	if (!(open_flag & O_CREAT))
		mode = 0;

	/* Must never be set by userspace */
	open_flag &= ~FMODE_NONOTIFY;

	/*
	 * O_SYNC is implemented as __O_SYNC|O_DSYNC.  As many places only
	 * check for O_DSYNC if the need any syncing at all we enforce it's
	 * always set instead of having to deal with possibly weird behaviour
	 * for malicious applications setting only __O_SYNC.
	 */
	if (open_flag & __O_SYNC)
		open_flag |= O_DSYNC;

	if (!acc_mode)
		acc_mode = MAY_OPEN | ACC_MODE(open_flag);

	/* O_TRUNC implies we need access checks for write permissions */
	if (open_flag & O_TRUNC)
		acc_mode |= MAY_WRITE;

	/* Allow the LSM permission hook to distinguish append 
	   access from general write access. */
	if (open_flag & O_APPEND)
		acc_mode |= MAY_APPEND;

	flags = LOOKUP_OPEN;
	if (open_flag & O_CREAT) {
		flags |= LOOKUP_CREATE;
		if (open_flag & O_EXCL)
			flags |= LOOKUP_EXCL;
	}
	if (open_flag & O_DIRECTORY)
		flags |= LOOKUP_DIRECTORY;
	if (!(open_flag & O_NOFOLLOW))
		flags |= LOOKUP_FOLLOW;

	filp = get_empty_filp();
	if (!filp)
		return ERR_PTR(-ENFILE);

	filp->f_flags = open_flag;
	nd.intent.open.file = filp;
	nd.intent.open.flags = flag;
	nd.intent.open.create_mode = mode;

	if (open_flag & O_CREAT)
		goto creat;

	/* !O_CREAT, simple open */
	error = do_path_lookup(dfd, pathname, flags, &nd);
	if (unlikely(error))
		goto out_filp;
	error = -ELOOP;
	if (!(nd.flags & LOOKUP_FOLLOW)) {
		if (nd.inode->i_op->follow_link)
			goto out_path;
	}
	error = -ENOTDIR;
	if (nd.flags & LOOKUP_DIRECTORY) {
		if (!nd.inode->i_op->lookup)
			goto out_path;
	}
	audit_inode(pathname, nd.path.dentry);
	filp = finish_open(&nd, open_flag, acc_mode);
	return filp;

creat:
	/* OK, have to create the file. Find the parent. */
	error = path_init_rcu(dfd, pathname,
			LOOKUP_PARENT | (flags & LOOKUP_REVAL), &nd);
	if (error)
		goto out_filp;
	error = path_walk_rcu(pathname, &nd);
	path_finish_rcu(&nd);
	if (unlikely(error == -ECHILD || error == -ESTALE)) {
		/* slower, locked walk */
		if (error == -ESTALE) {
reval:
			flags |= LOOKUP_REVAL;
		}
		error = path_init(dfd, pathname,
				LOOKUP_PARENT | (flags & LOOKUP_REVAL), &nd);
		if (error)
			goto out_filp;

		error = path_walk_simple(pathname, &nd);
	}
	if (unlikely(error))
		goto out_filp;
	if (unlikely(!audit_dummy_context()))
		audit_inode(pathname, nd.path.dentry);

	/*
	 * We have the parent and last component.
	 */
	nd.flags = flags;
	filp = do_last(&nd, &path, open_flag, acc_mode, mode, pathname);
	while (unlikely(!filp)) { /* trailing symlink */
		struct path holder;
		void *cookie;
		error = -ELOOP;
		/* S_ISDIR part is a temporary automount kludge */
		if (!(nd.flags & LOOKUP_FOLLOW) && !S_ISDIR(nd.inode->i_mode))
			goto exit_dput;
		if (count++ == 32)
			goto exit_dput;
		/*
		 * This is subtle. Instead of calling do_follow_link() we do
		 * the thing by hands. The reason is that this way we have zero
		 * link_count and path_walk() (called from ->follow_link)
		 * honoring LOOKUP_PARENT.  After that we have the parent and
		 * last component, i.e. we are in the same situation as after
		 * the first path_walk().  Well, almost - if the last component
		 * is normal we get its copy stored in nd->last.name and we will
		 * have to putname() it when we are done. Procfs-like symlinks
		 * just set LAST_BIND.
		 */
		nd.flags |= LOOKUP_PARENT;
		error = security_inode_follow_link(path.dentry, &nd);
		if (error)
			goto exit_dput;
		error = __do_follow_link(&path, &nd, &cookie);
		if (unlikely(error)) {
			if (!IS_ERR(cookie) && nd.inode->i_op->put_link)
				nd.inode->i_op->put_link(path.dentry, &nd, cookie);
			/* nd.path had been dropped */
			nd.path = path;
			goto out_path;
		}
		holder = path;
		nd.flags &= ~LOOKUP_PARENT;
		filp = do_last(&nd, &path, open_flag, acc_mode, mode, pathname);
		if (nd.inode->i_op->put_link)
			nd.inode->i_op->put_link(holder.dentry, &nd, cookie);
		path_put(&holder);
	}
out:
	if (nd.root.mnt)
		path_put(&nd.root);
	if (filp == ERR_PTR(-ESTALE) && !(flags & LOOKUP_REVAL))
		goto reval;
	return filp;

exit_dput:
	path_put_conditional(&path, &nd);
out_path:
	path_put(&nd.path);
out_filp:
	if (!IS_ERR(nd.intent.open.file))
		release_open_intent(&nd);
	filp = ERR_PTR(error);
	goto out;
}

/**
 * filp_open - open file and return file pointer
 *
 * @filename:	path to open
 * @flags:	open flags as per the open(2) second argument
 * @mode:	mode for the new file if O_CREAT is set, else ignored
 *
 * This is the helper to open a file from kernelspace if you really
 * have to.  But in generally you should not do this, so please move
 * along, nothing to see here..
 */
struct file *filp_open(const char *filename, int flags, int mode)
{
	return do_filp_open(AT_FDCWD, filename, flags, mode, 0);
}
EXPORT_SYMBOL(filp_open);

/**
 * lookup_create - lookup a dentry, creating it if it doesn't exist
 * @nd: nameidata info
 * @is_dir: directory flag
 *
 * Simple function to lookup and return a dentry and create it
 * if it doesn't exist.  Is SMP-safe.
 *
 * Returns with nd->path.dentry->d_inode->i_mutex locked.
 */
struct dentry *lookup_create(struct nameidata *nd, int is_dir)
{
	struct dentry *dentry = ERR_PTR(-EEXIST);

	mutex_lock_nested(&nd->path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	/*
	 * Yucky last component or no last component at all?
	 * (foo/., foo/.., /////)
	 */
	if (nd->last_type != LAST_NORM)
		goto fail;
	nd->flags &= ~LOOKUP_PARENT;
	nd->flags |= LOOKUP_CREATE | LOOKUP_EXCL;
	nd->intent.open.flags = O_EXCL;

	/*
	 * Do the final lookup.
	 */
	dentry = lookup_hash(nd);
	if (IS_ERR(dentry))
		goto fail;

	if (dentry->d_inode)
		goto eexist;
	/*
	 * Special case - lookup gave negative, but... we had foo/bar/
	 * From the vfs_mknod() POV we just have a negative dentry -
	 * all is fine. Let's be bastards - you had / on the end, you've
	 * been asking for (non-existent) directory. -ENOENT for you.
	 */
	if (unlikely(!is_dir && nd->last.name[nd->last.len])) {
		dput(dentry);
		dentry = ERR_PTR(-ENOENT);
	}
	return dentry;
eexist:
	dput(dentry);
	dentry = ERR_PTR(-EEXIST);
fail:
	return dentry;
}
EXPORT_SYMBOL_GPL(lookup_create);

int vfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	int error = may_create(dir, dentry);

	if (error)
		return error;

	if ((S_ISCHR(mode) || S_ISBLK(mode)) && !capable(CAP_MKNOD))
		return -EPERM;

	if (!dir->i_op->mknod)
		return -EPERM;

	error = devcgroup_inode_mknod(mode, dev);
	if (error)
		return error;

	error = security_inode_mknod(dir, dentry, mode, dev);
	if (error)
		return error;

	error = dir->i_op->mknod(dir, dentry, mode, dev);
	if (!error)
		fsnotify_create(dir, dentry);
	return error;
}

static int may_mknod(mode_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
	case 0: /* zero mode translates to S_IFREG */
		return 0;
	case S_IFDIR:
		return -EPERM;
	default:
		return -EINVAL;
	}
}

SYSCALL_DEFINE4(mknodat, int, dfd, const char __user *, filename, int, mode,
		unsigned, dev)
{
	int error;
	char *tmp;
	struct dentry *dentry;
	struct nameidata nd;

	if (S_ISDIR(mode))
		return -EPERM;

	error = user_path_parent(dfd, filename, &nd, &tmp);
	if (error)
		return error;

	dentry = lookup_create(&nd, 0);
	if (IS_ERR(dentry)) {
		error = PTR_ERR(dentry);
		goto out_unlock;
	}
	if (!IS_POSIXACL(nd.path.dentry->d_inode))
		mode &= ~current_umask();
	error = may_mknod(mode);
	if (error)
		goto out_dput;
	error = mnt_want_write(nd.path.mnt);
	if (error)
		goto out_dput;
	error = security_path_mknod(&nd.path, dentry, mode, dev);
	if (error)
		goto out_drop_write;
	switch (mode & S_IFMT) {
		case 0: case S_IFREG:
			error = vfs_create(nd.path.dentry->d_inode,dentry,mode,&nd);
			break;
		case S_IFCHR: case S_IFBLK:
			error = vfs_mknod(nd.path.dentry->d_inode,dentry,mode,
					new_decode_dev(dev));
			break;
		case S_IFIFO: case S_IFSOCK:
			error = vfs_mknod(nd.path.dentry->d_inode,dentry,mode,0);
			break;
	}
out_drop_write:
	mnt_drop_write(nd.path.mnt);
out_dput:
	dput(dentry);
out_unlock:
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);
	path_put(&nd.path);
	putname(tmp);

	return error;
}

SYSCALL_DEFINE3(mknod, const char __user *, filename, int, mode, unsigned, dev)
{
	return sys_mknodat(AT_FDCWD, filename, mode, dev);
}

int vfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int error = may_create(dir, dentry);

	if (error)
		return error;

	if (!dir->i_op->mkdir)
		return -EPERM;

	mode &= (S_IRWXUGO|S_ISVTX);
	error = security_inode_mkdir(dir, dentry, mode);
	if (error)
		return error;

	error = dir->i_op->mkdir(dir, dentry, mode);
	if (!error)
		fsnotify_mkdir(dir, dentry);
	return error;
}

SYSCALL_DEFINE3(mkdirat, int, dfd, const char __user *, pathname, int, mode)
{
	int error = 0;
	char * tmp;
	struct dentry *dentry;
	struct nameidata nd;

	error = user_path_parent(dfd, pathname, &nd, &tmp);
	if (error)
		goto out_err;

	dentry = lookup_create(&nd, 1);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out_unlock;

	if (!IS_POSIXACL(nd.path.dentry->d_inode))
		mode &= ~current_umask();
	error = mnt_want_write(nd.path.mnt);
	if (error)
		goto out_dput;
	error = security_path_mkdir(&nd.path, dentry, mode);
	if (error)
		goto out_drop_write;
	error = vfs_mkdir(nd.path.dentry->d_inode, dentry, mode);
out_drop_write:
	mnt_drop_write(nd.path.mnt);
out_dput:
	dput(dentry);
out_unlock:
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);
	path_put(&nd.path);
	putname(tmp);
out_err:
	return error;
}

SYSCALL_DEFINE2(mkdir, const char __user *, pathname, int, mode)
{
	return sys_mkdirat(AT_FDCWD, pathname, mode);
}

/*
 * We try to drop the dentry early: we should have
 * a usage count of 2 if we're the only user of this
 * dentry, and if that is true (possibly after pruning
 * the dcache), then we drop the dentry now.
 *
 * A low-level filesystem can, if it choses, legally
 * do a
 *
 *	if (!d_unhashed(dentry))
 *		return -EBUSY;
 *
 * if it cannot handle the case of removing a directory
 * that is still in use by something else..
 */
void dentry_unhash(struct dentry *dentry)
{
	dget(dentry);
	shrink_dcache_parent(dentry);
	spin_lock(&dentry->d_lock);
	if (dentry->d_count == 2)
		__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
}

int vfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error = may_delete(dir, dentry, 1);

	if (error)
		return error;

	if (!dir->i_op->rmdir)
		return -EPERM;

	mutex_lock(&dentry->d_inode->i_mutex);
	dentry_unhash(dentry);
	if (d_mountpoint(dentry))
		error = -EBUSY;
	else {
		error = security_inode_rmdir(dir, dentry);
		if (!error) {
			error = dir->i_op->rmdir(dir, dentry);
			if (!error) {
				dentry->d_inode->i_flags |= S_DEAD;
				dont_mount(dentry);
			}
		}
	}
	mutex_unlock(&dentry->d_inode->i_mutex);
	if (!error) {
		d_delete(dentry);
	}
	dput(dentry);

	return error;
}

static long do_rmdir(int dfd, const char __user *pathname)
{
	int error = 0;
	char * name;
	struct dentry *dentry;
	struct nameidata nd;

	error = user_path_parent(dfd, pathname, &nd, &name);
	if (error)
		return error;

	switch(nd.last_type) {
	case LAST_DOTDOT:
		error = -ENOTEMPTY;
		goto exit1;
	case LAST_DOT:
		error = -EINVAL;
		goto exit1;
	case LAST_ROOT:
		error = -EBUSY;
		goto exit1;
	}

	nd.flags &= ~LOOKUP_PARENT;

	mutex_lock_nested(&nd.path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = lookup_hash(&nd);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit2;
	error = mnt_want_write(nd.path.mnt);
	if (error)
		goto exit3;
	error = security_path_rmdir(&nd.path, dentry);
	if (error)
		goto exit4;
	error = vfs_rmdir(nd.path.dentry->d_inode, dentry);
exit4:
	mnt_drop_write(nd.path.mnt);
exit3:
	dput(dentry);
exit2:
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);
exit1:
	path_put(&nd.path);
	putname(name);
	return error;
}

SYSCALL_DEFINE1(rmdir, const char __user *, pathname)
{
	return do_rmdir(AT_FDCWD, pathname);
}

int vfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error = may_delete(dir, dentry, 0);

	if (error)
		return error;

	if (!dir->i_op->unlink)
		return -EPERM;

	mutex_lock(&dentry->d_inode->i_mutex);
	if (d_mountpoint(dentry))
		error = -EBUSY;
	else {
		error = security_inode_unlink(dir, dentry);
		if (!error) {
			error = dir->i_op->unlink(dir, dentry);
			if (!error)
				dont_mount(dentry);
		}
	}
	mutex_unlock(&dentry->d_inode->i_mutex);

	/* We don't d_delete() NFS sillyrenamed files--they still exist. */
	if (!error && !(dentry->d_flags & DCACHE_NFSFS_RENAMED)) {
		fsnotify_link_count(dentry->d_inode);
		d_delete(dentry);
	}

	return error;
}

/*
 * Make sure that the actual truncation of the file will occur outside its
 * directory's i_mutex.  Truncate can take a long time if there is a lot of
 * writeout happening, and we don't want to prevent access to the directory
 * while waiting on the I/O.
 */
static long do_unlinkat(int dfd, const char __user *pathname)
{
	int error;
	char *name;
	struct dentry *dentry;
	struct nameidata nd;
	struct inode *inode = NULL;

	error = user_path_parent(dfd, pathname, &nd, &name);
	if (error)
		return error;

	error = -EISDIR;
	if (nd.last_type != LAST_NORM)
		goto exit1;

	nd.flags &= ~LOOKUP_PARENT;

	mutex_lock_nested(&nd.path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = lookup_hash(&nd);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		/* Why not before? Because we want correct error value */
		if (nd.last.name[nd.last.len])
			goto slashes;
		inode = dentry->d_inode;
		if (inode)
			ihold(inode);
		error = mnt_want_write(nd.path.mnt);
		if (error)
			goto exit2;
		error = security_path_unlink(&nd.path, dentry);
		if (error)
			goto exit3;
		error = vfs_unlink(nd.path.dentry->d_inode, dentry);
exit3:
		mnt_drop_write(nd.path.mnt);
	exit2:
		dput(dentry);
	}
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);
	if (inode)
		iput(inode);	/* truncate the inode here */
exit1:
	path_put(&nd.path);
	putname(name);
	return error;

slashes:
	error = !dentry->d_inode ? -ENOENT :
		S_ISDIR(dentry->d_inode->i_mode) ? -EISDIR : -ENOTDIR;
	goto exit2;
}

SYSCALL_DEFINE3(unlinkat, int, dfd, const char __user *, pathname, int, flag)
{
	if ((flag & ~AT_REMOVEDIR) != 0)
		return -EINVAL;

	if (flag & AT_REMOVEDIR)
		return do_rmdir(dfd, pathname);

	return do_unlinkat(dfd, pathname);
}

SYSCALL_DEFINE1(unlink, const char __user *, pathname)
{
	return do_unlinkat(AT_FDCWD, pathname);
}

int vfs_symlink(struct inode *dir, struct dentry *dentry, const char *oldname)
{
	int error = may_create(dir, dentry);

	if (error)
		return error;

	if (!dir->i_op->symlink)
		return -EPERM;

	error = security_inode_symlink(dir, dentry, oldname);
	if (error)
		return error;

	error = dir->i_op->symlink(dir, dentry, oldname);
	if (!error)
		fsnotify_create(dir, dentry);
	return error;
}

SYSCALL_DEFINE3(symlinkat, const char __user *, oldname,
		int, newdfd, const char __user *, newname)
{
	int error;
	char *from;
	char *to;
	struct dentry *dentry;
	struct nameidata nd;

	from = getname(oldname);
	if (IS_ERR(from))
		return PTR_ERR(from);

	error = user_path_parent(newdfd, newname, &nd, &to);
	if (error)
		goto out_putname;

	dentry = lookup_create(&nd, 0);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out_unlock;

	error = mnt_want_write(nd.path.mnt);
	if (error)
		goto out_dput;
	error = security_path_symlink(&nd.path, dentry, from);
	if (error)
		goto out_drop_write;
	error = vfs_symlink(nd.path.dentry->d_inode, dentry, from);
out_drop_write:
	mnt_drop_write(nd.path.mnt);
out_dput:
	dput(dentry);
out_unlock:
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);
	path_put(&nd.path);
	putname(to);
out_putname:
	putname(from);
	return error;
}

SYSCALL_DEFINE2(symlink, const char __user *, oldname, const char __user *, newname)
{
	return sys_symlinkat(oldname, AT_FDCWD, newname);
}

int vfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry)
{
	struct inode *inode = old_dentry->d_inode;
	int error;

	if (!inode)
		return -ENOENT;

	error = may_create(dir, new_dentry);
	if (error)
		return error;

	if (dir->i_sb != inode->i_sb)
		return -EXDEV;

	/*
	 * A link to an append-only or immutable file cannot be created.
	 */
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;
	if (!dir->i_op->link)
		return -EPERM;
	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	error = security_inode_link(old_dentry, dir, new_dentry);
	if (error)
		return error;

	mutex_lock(&inode->i_mutex);
	error = dir->i_op->link(old_dentry, dir, new_dentry);
	mutex_unlock(&inode->i_mutex);
	if (!error)
		fsnotify_link(dir, inode, new_dentry);
	return error;
}

/*
 * Hardlinks are often used in delicate situations.  We avoid
 * security-related surprises by not following symlinks on the
 * newname.  --KAB
 *
 * We don't follow them on the oldname either to be compatible
 * with linux 2.0, and to avoid hard-linking to directories
 * and other special files.  --ADM
 */
SYSCALL_DEFINE5(linkat, int, olddfd, const char __user *, oldname,
		int, newdfd, const char __user *, newname, int, flags)
{
	struct dentry *new_dentry;
	struct nameidata nd;
	struct path old_path;
	int error;
	char *to;

	if ((flags & ~AT_SYMLINK_FOLLOW) != 0)
		return -EINVAL;

	error = user_path_at(olddfd, oldname,
			     flags & AT_SYMLINK_FOLLOW ? LOOKUP_FOLLOW : 0,
			     &old_path);
	if (error)
		return error;

	error = user_path_parent(newdfd, newname, &nd, &to);
	if (error)
		goto out;
	error = -EXDEV;
	if (old_path.mnt != nd.path.mnt)
		goto out_release;
	new_dentry = lookup_create(&nd, 0);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto out_unlock;
	error = mnt_want_write(nd.path.mnt);
	if (error)
		goto out_dput;
	error = security_path_link(old_path.dentry, &nd.path, new_dentry);
	if (error)
		goto out_drop_write;
	error = vfs_link(old_path.dentry, nd.path.dentry->d_inode, new_dentry);
out_drop_write:
	mnt_drop_write(nd.path.mnt);
out_dput:
	dput(new_dentry);
out_unlock:
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);
out_release:
	path_put(&nd.path);
	putname(to);
out:
	path_put(&old_path);

	return error;
}

SYSCALL_DEFINE2(link, const char __user *, oldname, const char __user *, newname)
{
	return sys_linkat(AT_FDCWD, oldname, AT_FDCWD, newname, 0);
}

/*
 * The worst of all namespace operations - renaming directory. "Perverted"
 * doesn't even start to describe it. Somebody in UCB had a heck of a trip...
 * Problems:
 *	a) we can get into loop creation. Check is done in is_subdir().
 *	b) race potential - two innocent renames can create a loop together.
 *	   That's where 4.4 screws up. Current fix: serialization on
 *	   sb->s_vfs_rename_mutex. We might be more accurate, but that's another
 *	   story.
 *	c) we have to lock _three_ objects - parents and victim (if it exists).
 *	   And that - after we got ->i_mutex on parents (until then we don't know
 *	   whether the target exists).  Solution: try to be smart with locking
 *	   order for inodes.  We rely on the fact that tree topology may change
 *	   only under ->s_vfs_rename_mutex _and_ that parent of the object we
 *	   move will be locked.  Thus we can rank directories by the tree
 *	   (ancestors first) and rank all non-directories after them.
 *	   That works since everybody except rename does "lock parent, lookup,
 *	   lock child" and rename is under ->s_vfs_rename_mutex.
 *	   HOWEVER, it relies on the assumption that any object with ->lookup()
 *	   has no more than 1 dentry.  If "hybrid" objects will ever appear,
 *	   we'd better make sure that there's no link(2) for them.
 *	d) some filesystems don't support opened-but-unlinked directories,
 *	   either because of layout or because they are not ready to deal with
 *	   all cases correctly. The latter will be fixed (taking this sort of
 *	   stuff into VFS), but the former is not going away. Solution: the same
 *	   trick as in rmdir().
 *	e) conversion from fhandle to dentry may come in the wrong moment - when
 *	   we are removing the target. Solution: we will have to grab ->i_mutex
 *	   in the fhandle_to_dentry code. [FIXME - current nfsfh.c relies on
 *	   ->i_mutex on parents, which works but leads to some truly excessive
 *	   locking].
 */
static int vfs_rename_dir(struct inode *old_dir, struct dentry *old_dentry,
			  struct inode *new_dir, struct dentry *new_dentry)
{
	int error = 0;
	struct inode *target;

	/*
	 * If we are going to change the parent - check write permissions,
	 * we'll need to flip '..'.
	 */
	if (new_dir != old_dir) {
		error = inode_permission(old_dentry->d_inode, MAY_WRITE);
		if (error)
			return error;
	}

	error = security_inode_rename(old_dir, old_dentry, new_dir, new_dentry);
	if (error)
		return error;

	target = new_dentry->d_inode;
	if (target)
		mutex_lock(&target->i_mutex);
	if (d_mountpoint(old_dentry)||d_mountpoint(new_dentry))
		error = -EBUSY;
	else {
		if (target)
			dentry_unhash(new_dentry);
		error = old_dir->i_op->rename(old_dir, old_dentry, new_dir, new_dentry);
	}
	if (target) {
		if (!error) {
			target->i_flags |= S_DEAD;
			dont_mount(new_dentry);
		}
		mutex_unlock(&target->i_mutex);
		if (d_unhashed(new_dentry))
			d_rehash(new_dentry);
		dput(new_dentry);
	}
	if (!error)
		if (!(old_dir->i_sb->s_type->fs_flags & FS_RENAME_DOES_D_MOVE))
			d_move(old_dentry,new_dentry);
	return error;
}

static int vfs_rename_other(struct inode *old_dir, struct dentry *old_dentry,
			    struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *target;
	int error;

	error = security_inode_rename(old_dir, old_dentry, new_dir, new_dentry);
	if (error)
		return error;

	dget(new_dentry);
	target = new_dentry->d_inode;
	if (target)
		mutex_lock(&target->i_mutex);
	if (d_mountpoint(old_dentry)||d_mountpoint(new_dentry))
		error = -EBUSY;
	else
		error = old_dir->i_op->rename(old_dir, old_dentry, new_dir, new_dentry);
	if (!error) {
		if (target)
			dont_mount(new_dentry);
		if (!(old_dir->i_sb->s_type->fs_flags & FS_RENAME_DOES_D_MOVE))
			d_move(old_dentry, new_dentry);
	}
	if (target)
		mutex_unlock(&target->i_mutex);
	dput(new_dentry);
	return error;
}

int vfs_rename(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry)
{
	int error;
	int is_dir = S_ISDIR(old_dentry->d_inode->i_mode);
	const unsigned char *old_name;

	if (old_dentry->d_inode == new_dentry->d_inode)
 		return 0;
 
	error = may_delete(old_dir, old_dentry, is_dir);
	if (error)
		return error;

	if (!new_dentry->d_inode)
		error = may_create(new_dir, new_dentry);
	else
		error = may_delete(new_dir, new_dentry, is_dir);
	if (error)
		return error;

	if (!old_dir->i_op->rename)
		return -EPERM;

	old_name = fsnotify_oldname_init(old_dentry->d_name.name);

	if (is_dir)
		error = vfs_rename_dir(old_dir,old_dentry,new_dir,new_dentry);
	else
		error = vfs_rename_other(old_dir,old_dentry,new_dir,new_dentry);
	if (!error)
		fsnotify_move(old_dir, new_dir, old_name, is_dir,
			      new_dentry->d_inode, old_dentry);
	fsnotify_oldname_free(old_name);

	return error;
}

SYSCALL_DEFINE4(renameat, int, olddfd, const char __user *, oldname,
		int, newdfd, const char __user *, newname)
{
	struct dentry *old_dir, *new_dir;
	struct dentry *old_dentry, *new_dentry;
	struct dentry *trap;
	struct nameidata oldnd, newnd;
	char *from;
	char *to;
	int error;

	error = user_path_parent(olddfd, oldname, &oldnd, &from);
	if (error)
		goto exit;

	error = user_path_parent(newdfd, newname, &newnd, &to);
	if (error)
		goto exit1;

	error = -EXDEV;
	if (oldnd.path.mnt != newnd.path.mnt)
		goto exit2;

	old_dir = oldnd.path.dentry;
	error = -EBUSY;
	if (oldnd.last_type != LAST_NORM)
		goto exit2;

	new_dir = newnd.path.dentry;
	if (newnd.last_type != LAST_NORM)
		goto exit2;

	oldnd.flags &= ~LOOKUP_PARENT;
	newnd.flags &= ~LOOKUP_PARENT;
	newnd.flags |= LOOKUP_RENAME_TARGET;

	trap = lock_rename(new_dir, old_dir);

	old_dentry = lookup_hash(&oldnd);
	error = PTR_ERR(old_dentry);
	if (IS_ERR(old_dentry))
		goto exit3;
	/* source must exist */
	error = -ENOENT;
	if (!old_dentry->d_inode)
		goto exit4;
	/* unless the source is a directory trailing slashes give -ENOTDIR */
	if (!S_ISDIR(old_dentry->d_inode->i_mode)) {
		error = -ENOTDIR;
		if (oldnd.last.name[oldnd.last.len])
			goto exit4;
		if (newnd.last.name[newnd.last.len])
			goto exit4;
	}
	/* source should not be ancestor of target */
	error = -EINVAL;
	if (old_dentry == trap)
		goto exit4;
	new_dentry = lookup_hash(&newnd);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto exit4;
	/* target should not be an ancestor of source */
	error = -ENOTEMPTY;
	if (new_dentry == trap)
		goto exit5;

	error = mnt_want_write(oldnd.path.mnt);
	if (error)
		goto exit5;
	error = security_path_rename(&oldnd.path, old_dentry,
				     &newnd.path, new_dentry);
	if (error)
		goto exit6;
	error = vfs_rename(old_dir->d_inode, old_dentry,
				   new_dir->d_inode, new_dentry);
exit6:
	mnt_drop_write(oldnd.path.mnt);
exit5:
	dput(new_dentry);
exit4:
	dput(old_dentry);
exit3:
	unlock_rename(new_dir, old_dir);
exit2:
	path_put(&newnd.path);
	putname(to);
exit1:
	path_put(&oldnd.path);
	putname(from);
exit:
	return error;
}

SYSCALL_DEFINE2(rename, const char __user *, oldname, const char __user *, newname)
{
	return sys_renameat(AT_FDCWD, oldname, AT_FDCWD, newname);
}

int vfs_readlink(struct dentry *dentry, char __user *buffer, int buflen, const char *link)
{
	int len;

	len = PTR_ERR(link);
	if (IS_ERR(link))
		goto out;

	len = strlen(link);
	if (len > (unsigned) buflen)
		len = buflen;
	if (copy_to_user(buffer, link, len))
		len = -EFAULT;
out:
	return len;
}

/*
 * A helper for ->readlink().  This should be used *ONLY* for symlinks that
 * have ->follow_link() touching nd only in nd_set_link().  Using (or not
 * using) it for any given inode is up to filesystem.
 */
int generic_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	struct nameidata nd;
	void *cookie;
	int res;

	nd.depth = 0;
	cookie = dentry->d_inode->i_op->follow_link(dentry, &nd);
	if (IS_ERR(cookie))
		return PTR_ERR(cookie);

	res = vfs_readlink(dentry, buffer, buflen, nd_get_link(&nd));
	if (dentry->d_inode->i_op->put_link)
		dentry->d_inode->i_op->put_link(dentry, &nd, cookie);
	return res;
}

int vfs_follow_link(struct nameidata *nd, const char *link)
{
	return __vfs_follow_link(nd, link);
}

/* get the link contents into pagecache */
static char *page_getlink(struct dentry * dentry, struct page **ppage)
{
	char *kaddr;
	struct page *page;
	struct address_space *mapping = dentry->d_inode->i_mapping;
	page = read_mapping_page(mapping, 0, NULL);
	if (IS_ERR(page))
		return (char*)page;
	*ppage = page;
	kaddr = kmap(page);
	nd_terminate_link(kaddr, dentry->d_inode->i_size, PAGE_SIZE - 1);
	return kaddr;
}

int page_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	struct page *page = NULL;
	char *s = page_getlink(dentry, &page);
	int res = vfs_readlink(dentry,buffer,buflen,s);
	if (page) {
		kunmap(page);
		page_cache_release(page);
	}
	return res;
}

void *page_follow_link_light(struct dentry *dentry, struct nameidata *nd)
{
	struct page *page = NULL;
	nd_set_link(nd, page_getlink(dentry, &page));
	return page;
}

void page_put_link(struct dentry *dentry, struct nameidata *nd, void *cookie)
{
	struct page *page = cookie;

	if (page) {
		kunmap(page);
		page_cache_release(page);
	}
}

/*
 * The nofs argument instructs pagecache_write_begin to pass AOP_FLAG_NOFS
 */
int __page_symlink(struct inode *inode, const char *symname, int len, int nofs)
{
	struct address_space *mapping = inode->i_mapping;
	struct page *page;
	void *fsdata;
	int err;
	char *kaddr;
	unsigned int flags = AOP_FLAG_UNINTERRUPTIBLE;
	if (nofs)
		flags |= AOP_FLAG_NOFS;

retry:
	err = pagecache_write_begin(NULL, mapping, 0, len-1,
				flags, &page, &fsdata);
	if (err)
		goto fail;

	kaddr = kmap_atomic(page, KM_USER0);
	memcpy(kaddr, symname, len-1);
	kunmap_atomic(kaddr, KM_USER0);

	err = pagecache_write_end(NULL, mapping, 0, len-1, len-1,
							page, fsdata);
	if (err < 0)
		goto fail;
	if (err < len-1)
		goto retry;

	mark_inode_dirty(inode);
	return 0;
fail:
	return err;
}

int page_symlink(struct inode *inode, const char *symname, int len)
{
	return __page_symlink(inode, symname, len,
			!(mapping_gfp_mask(inode->i_mapping) & __GFP_FS));
}

const struct inode_operations page_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
};

EXPORT_SYMBOL(user_path_at);
EXPORT_SYMBOL(follow_down);
EXPORT_SYMBOL(follow_up);
EXPORT_SYMBOL(get_write_access); /* binfmt_aout */
EXPORT_SYMBOL(getname);
EXPORT_SYMBOL(lock_rename);
EXPORT_SYMBOL(lookup_one_len);
EXPORT_SYMBOL(page_follow_link_light);
EXPORT_SYMBOL(page_put_link);
EXPORT_SYMBOL(page_readlink);
EXPORT_SYMBOL(__page_symlink);
EXPORT_SYMBOL(page_symlink);
EXPORT_SYMBOL(page_symlink_inode_operations);
EXPORT_SYMBOL(path_lookup);
EXPORT_SYMBOL(kern_path);
EXPORT_SYMBOL(vfs_path_lookup);
EXPORT_SYMBOL(inode_permission);
EXPORT_SYMBOL(file_permission);
EXPORT_SYMBOL(unlock_rename);
EXPORT_SYMBOL(vfs_create);
EXPORT_SYMBOL(vfs_follow_link);
EXPORT_SYMBOL(vfs_link);
EXPORT_SYMBOL(vfs_mkdir);
EXPORT_SYMBOL(vfs_mknod);
EXPORT_SYMBOL(generic_permission);
EXPORT_SYMBOL(vfs_readlink);
EXPORT_SYMBOL(vfs_rename);
EXPORT_SYMBOL(vfs_rmdir);
EXPORT_SYMBOL(vfs_symlink);
EXPORT_SYMBOL(vfs_unlink);
EXPORT_SYMBOL(dentry_unhash);
EXPORT_SYMBOL(generic_readlink);
