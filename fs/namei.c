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
#include <linux/export.h>
#include <linux/kernel.h>
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
#include <linux/posix_acl.h>
#include <linux/hash.h>
#include <asm/uaccess.h>

#include "internal.h"
#include "mount.h"

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
 * the name is a symlink pointing to a non-existent name.
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

#define EMBEDDED_NAME_MAX	(PATH_MAX - offsetof(struct filename, iname))

struct filename *
getname_flags(const char __user *filename, int flags, int *empty)
{
	struct filename *result;
	char *kname;
	int len;

	result = audit_reusename(filename);
	if (result)
		return result;

	result = __getname();
	if (unlikely(!result))
		return ERR_PTR(-ENOMEM);

	/*
	 * First, try to embed the struct filename inside the names_cache
	 * allocation
	 */
	kname = (char *)result->iname;
	result->name = kname;

	len = strncpy_from_user(kname, filename, EMBEDDED_NAME_MAX);
	if (unlikely(len < 0)) {
		__putname(result);
		return ERR_PTR(len);
	}

	/*
	 * Uh-oh. We have a name that's approaching PATH_MAX. Allocate a
	 * separate struct filename so we can dedicate the entire
	 * names_cache allocation for the pathname, and re-do the copy from
	 * userland.
	 */
	if (unlikely(len == EMBEDDED_NAME_MAX)) {
		const size_t size = offsetof(struct filename, iname[1]);
		kname = (char *)result;

		/*
		 * size is chosen that way we to guarantee that
		 * result->iname[0] is within the same object and that
		 * kname can't be equal to result->iname, no matter what.
		 */
		result = kzalloc(size, GFP_KERNEL);
		if (unlikely(!result)) {
			__putname(kname);
			return ERR_PTR(-ENOMEM);
		}
		result->name = kname;
		len = strncpy_from_user(kname, filename, PATH_MAX);
		if (unlikely(len < 0)) {
			__putname(kname);
			kfree(result);
			return ERR_PTR(len);
		}
		if (unlikely(len == PATH_MAX)) {
			__putname(kname);
			kfree(result);
			return ERR_PTR(-ENAMETOOLONG);
		}
	}

	result->refcnt = 1;
	/* The empty path is special. */
	if (unlikely(!len)) {
		if (empty)
			*empty = 1;
		if (!(flags & LOOKUP_EMPTY)) {
			putname(result);
			return ERR_PTR(-ENOENT);
		}
	}

	result->uptr = filename;
	result->aname = NULL;
	audit_getname(result);
	return result;
}

struct filename *
getname(const char __user * filename)
{
	return getname_flags(filename, 0, NULL);
}

struct filename *
getname_kernel(const char * filename)
{
	struct filename *result;
	int len = strlen(filename) + 1;

	result = __getname();
	if (unlikely(!result))
		return ERR_PTR(-ENOMEM);

	if (len <= EMBEDDED_NAME_MAX) {
		result->name = (char *)result->iname;
	} else if (len <= PATH_MAX) {
		struct filename *tmp;

		tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);
		if (unlikely(!tmp)) {
			__putname(result);
			return ERR_PTR(-ENOMEM);
		}
		tmp->name = (char *)result;
		result = tmp;
	} else {
		__putname(result);
		return ERR_PTR(-ENAMETOOLONG);
	}
	memcpy((char *)result->name, filename, len);
	result->uptr = NULL;
	result->aname = NULL;
	result->refcnt = 1;
	audit_getname(result);

	return result;
}

void putname(struct filename *name)
{
	BUG_ON(name->refcnt <= 0);

	if (--name->refcnt > 0)
		return;

	if (name->name != name->iname) {
		__putname(name->name);
		kfree(name);
	} else
		__putname(name);
}

static int check_acl(struct inode *inode, int mask)
{
#ifdef CONFIG_FS_POSIX_ACL
	struct posix_acl *acl;

	if (mask & MAY_NOT_BLOCK) {
		acl = get_cached_acl_rcu(inode, ACL_TYPE_ACCESS);
	        if (!acl)
	                return -EAGAIN;
		/* no ->get_acl() calls in RCU mode... */
		if (acl == ACL_NOT_CACHED)
			return -ECHILD;
	        return posix_acl_permission(inode, acl, mask & ~MAY_NOT_BLOCK);
	}

	acl = get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl) {
	        int error = posix_acl_permission(inode, acl, mask);
	        posix_acl_release(acl);
	        return error;
	}
#endif

	return -EAGAIN;
}

/*
 * This does the basic permission checking
 */
static int acl_permission_check(struct inode *inode, int mask)
{
	unsigned int mode = inode->i_mode;

	if (likely(uid_eq(current_fsuid(), inode->i_uid)))
		mode >>= 6;
	else {
		if (IS_POSIXACL(inode) && (mode & S_IRWXG)) {
			int error = check_acl(inode, mask);
			if (error != -EAGAIN)
				return error;
		}

		if (in_group_p(inode->i_gid))
			mode >>= 3;
	}

	/*
	 * If the DACs are ok we don't need any capability check.
	 */
	if ((mask & ~mode & (MAY_READ | MAY_WRITE | MAY_EXEC)) == 0)
		return 0;
	return -EACCES;
}

/**
 * generic_permission -  check for access rights on a Posix-like filesystem
 * @inode:	inode to check access rights for
 * @mask:	right to check for (%MAY_READ, %MAY_WRITE, %MAY_EXEC, ...)
 *
 * Used to check for read/write/execute permissions on a file.
 * We use "fsuid" for this, letting us set arbitrary permissions
 * for filesystem access without changing the "normal" uids which
 * are used for other things.
 *
 * generic_permission is rcu-walk aware. It returns -ECHILD in case an rcu-walk
 * request cannot be satisfied (eg. requires blocking or too much complexity).
 * It would then be called again in ref-walk mode.
 */
int generic_permission(struct inode *inode, int mask)
{
	int ret;

	/*
	 * Do the basic permission checks.
	 */
	ret = acl_permission_check(inode, mask);
	if (ret != -EACCES)
		return ret;

	if (S_ISDIR(inode->i_mode)) {
		/* DACs are overridable for directories */
		if (capable_wrt_inode_uidgid(inode, CAP_DAC_OVERRIDE))
			return 0;
		if (!(mask & MAY_WRITE))
			if (capable_wrt_inode_uidgid(inode,
						     CAP_DAC_READ_SEARCH))
				return 0;
		return -EACCES;
	}
	/*
	 * Read/write DACs are always overridable.
	 * Executable DACs are overridable when there is
	 * at least one exec bit set.
	 */
	if (!(mask & MAY_EXEC) || (inode->i_mode & S_IXUGO))
		if (capable_wrt_inode_uidgid(inode, CAP_DAC_OVERRIDE))
			return 0;

	/*
	 * Searching includes executable on directories, else just read.
	 */
	mask &= MAY_READ | MAY_WRITE | MAY_EXEC;
	if (mask == MAY_READ)
		if (capable_wrt_inode_uidgid(inode, CAP_DAC_READ_SEARCH))
			return 0;

	return -EACCES;
}
EXPORT_SYMBOL(generic_permission);

/*
 * We _really_ want to just do "generic_permission()" without
 * even looking at the inode->i_op values. So we keep a cache
 * flag in inode->i_opflags, that says "this has not special
 * permission function, use the fast case".
 */
static inline int do_inode_permission(struct inode *inode, int mask)
{
	if (unlikely(!(inode->i_opflags & IOP_FASTPERM))) {
		if (likely(inode->i_op->permission))
			return inode->i_op->permission(inode, mask);

		/* This gets set once for the inode lifetime */
		spin_lock(&inode->i_lock);
		inode->i_opflags |= IOP_FASTPERM;
		spin_unlock(&inode->i_lock);
	}
	return generic_permission(inode, mask);
}

/**
 * __inode_permission - Check for access rights to a given inode
 * @inode: Inode to check permission on
 * @mask: Right to check for (%MAY_READ, %MAY_WRITE, %MAY_EXEC)
 *
 * Check for read/write/execute permissions on an inode.
 *
 * When checking for MAY_APPEND, MAY_WRITE must also be set in @mask.
 *
 * This does not check for a read-only file system.  You probably want
 * inode_permission().
 */
int __inode_permission(struct inode *inode, int mask)
{
	int retval;

	if (unlikely(mask & MAY_WRITE)) {
		/*
		 * Nobody gets write access to an immutable file.
		 */
		if (IS_IMMUTABLE(inode))
			return -EACCES;
	}

	retval = do_inode_permission(inode, mask);
	if (retval)
		return retval;

	retval = devcgroup_inode_permission(inode, mask);
	if (retval)
		return retval;

	return security_inode_permission(inode, mask);
}
EXPORT_SYMBOL(__inode_permission);

/**
 * sb_permission - Check superblock-level permissions
 * @sb: Superblock of inode to check permission on
 * @inode: Inode to check permission on
 * @mask: Right to check for (%MAY_READ, %MAY_WRITE, %MAY_EXEC)
 *
 * Separate out file-system wide checks from inode-specific permission checks.
 */
static int sb_permission(struct super_block *sb, struct inode *inode, int mask)
{
	if (unlikely(mask & MAY_WRITE)) {
		umode_t mode = inode->i_mode;

		/* Nobody gets write access to a read-only fs. */
		if ((sb->s_flags & MS_RDONLY) &&
		    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
			return -EROFS;
	}
	return 0;
}

/**
 * inode_permission - Check for access rights to a given inode
 * @inode: Inode to check permission on
 * @mask: Right to check for (%MAY_READ, %MAY_WRITE, %MAY_EXEC)
 *
 * Check for read/write/execute permissions on an inode.  We use fs[ug]id for
 * this, letting us set arbitrary permissions for filesystem access without
 * changing the "normal" UIDs which are used for other things.
 *
 * When checking for MAY_APPEND, MAY_WRITE must also be set in @mask.
 */
int inode_permission(struct inode *inode, int mask)
{
	int retval;

	retval = sb_permission(inode->i_sb, inode, mask);
	if (retval)
		return retval;
	return __inode_permission(inode, mask);
}
EXPORT_SYMBOL(inode_permission);

/**
 * path_get - get a reference to a path
 * @path: path to get the reference to
 *
 * Given a path increment the reference count to the dentry and the vfsmount.
 */
void path_get(const struct path *path)
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
void path_put(const struct path *path)
{
	dput(path->dentry);
	mntput(path->mnt);
}
EXPORT_SYMBOL(path_put);

#define EMBEDDED_LEVELS 2
struct nameidata {
	struct path	path;
	struct qstr	last;
	struct path	root;
	struct inode	*inode; /* path.dentry.d_inode */
	unsigned int	flags;
	unsigned	seq, m_seq;
	int		last_type;
	unsigned	depth;
	int		total_link_count;
	struct saved {
		struct path link;
		void *cookie;
		const char *name;
		struct inode *inode;
		unsigned seq;
	} *stack, internal[EMBEDDED_LEVELS];
	struct filename	*name;
	struct nameidata *saved;
	unsigned	root_seq;
	int		dfd;
};

static void set_nameidata(struct nameidata *p, int dfd, struct filename *name)
{
	struct nameidata *old = current->nameidata;
	p->stack = p->internal;
	p->dfd = dfd;
	p->name = name;
	p->total_link_count = old ? old->total_link_count : 0;
	p->saved = old;
	current->nameidata = p;
}

static void restore_nameidata(void)
{
	struct nameidata *now = current->nameidata, *old = now->saved;

	current->nameidata = old;
	if (old)
		old->total_link_count = now->total_link_count;
	if (now->stack != now->internal) {
		kfree(now->stack);
		now->stack = now->internal;
	}
}

static int __nd_alloc_stack(struct nameidata *nd)
{
	struct saved *p;

	if (nd->flags & LOOKUP_RCU) {
		p= kmalloc(MAXSYMLINKS * sizeof(struct saved),
				  GFP_ATOMIC);
		if (unlikely(!p))
			return -ECHILD;
	} else {
		p= kmalloc(MAXSYMLINKS * sizeof(struct saved),
				  GFP_KERNEL);
		if (unlikely(!p))
			return -ENOMEM;
	}
	memcpy(p, nd->internal, sizeof(nd->internal));
	nd->stack = p;
	return 0;
}

/**
 * path_connected - Verify that a path->dentry is below path->mnt.mnt_root
 * @path: nameidate to verify
 *
 * Rename can sometimes move a file or directory outside of a bind
 * mount, path_connected allows those cases to be detected.
 */
static bool path_connected(const struct path *path)
{
	struct vfsmount *mnt = path->mnt;

	/* Only bind mounts can have disconnected paths */
	if (mnt->mnt_root == mnt->mnt_sb->s_root)
		return true;

	return is_subdir(path->dentry, mnt->mnt_root);
}

static inline int nd_alloc_stack(struct nameidata *nd)
{
	if (likely(nd->depth != EMBEDDED_LEVELS))
		return 0;
	if (likely(nd->stack != nd->internal))
		return 0;
	return __nd_alloc_stack(nd);
}

static void drop_links(struct nameidata *nd)
{
	int i = nd->depth;
	while (i--) {
		struct saved *last = nd->stack + i;
		struct inode *inode = last->inode;
		if (last->cookie && inode->i_op->put_link) {
			inode->i_op->put_link(inode, last->cookie);
			last->cookie = NULL;
		}
	}
}

static void terminate_walk(struct nameidata *nd)
{
	drop_links(nd);
	if (!(nd->flags & LOOKUP_RCU)) {
		int i;
		path_put(&nd->path);
		for (i = 0; i < nd->depth; i++)
			path_put(&nd->stack[i].link);
		if (nd->root.mnt && !(nd->flags & LOOKUP_ROOT)) {
			path_put(&nd->root);
			nd->root.mnt = NULL;
		}
	} else {
		nd->flags &= ~LOOKUP_RCU;
		if (!(nd->flags & LOOKUP_ROOT))
			nd->root.mnt = NULL;
		rcu_read_unlock();
	}
	nd->depth = 0;
}

/* path_put is needed afterwards regardless of success or failure */
static bool legitimize_path(struct nameidata *nd,
			    struct path *path, unsigned seq)
{
	int res = __legitimize_mnt(path->mnt, nd->m_seq);
	if (unlikely(res)) {
		if (res > 0)
			path->mnt = NULL;
		path->dentry = NULL;
		return false;
	}
	if (unlikely(!lockref_get_not_dead(&path->dentry->d_lockref))) {
		path->dentry = NULL;
		return false;
	}
	return !read_seqcount_retry(&path->dentry->d_seq, seq);
}

static bool legitimize_links(struct nameidata *nd)
{
	int i;
	for (i = 0; i < nd->depth; i++) {
		struct saved *last = nd->stack + i;
		if (unlikely(!legitimize_path(nd, &last->link, last->seq))) {
			drop_links(nd);
			nd->depth = i + 1;
			return false;
		}
	}
	return true;
}

/*
 * Path walking has 2 modes, rcu-walk and ref-walk (see
 * Documentation/filesystems/path-lookup.txt).  In situations when we can't
 * continue in RCU mode, we attempt to drop out of rcu-walk mode and grab
 * normal reference counts on dentries and vfsmounts to transition to rcu-walk
 * mode.  Refcounts are grabbed at the last known good point before rcu-walk
 * got stuck, so ref-walk may continue from there. If this is not successful
 * (eg. a seqcount has changed), then failure is returned and it's up to caller
 * to restart the path walk from the beginning in ref-walk mode.
 */

/**
 * unlazy_walk - try to switch to ref-walk mode.
 * @nd: nameidata pathwalk data
 * @dentry: child of nd->path.dentry or NULL
 * @seq: seq number to check dentry against
 * Returns: 0 on success, -ECHILD on failure
 *
 * unlazy_walk attempts to legitimize the current nd->path, nd->root and dentry
 * for ref-walk mode.  @dentry must be a path found by a do_lookup call on
 * @nd or NULL.  Must be called from rcu-walk context.
 * Nothing should touch nameidata between unlazy_walk() failure and
 * terminate_walk().
 */
static int unlazy_walk(struct nameidata *nd, struct dentry *dentry, unsigned seq)
{
	struct dentry *parent = nd->path.dentry;

	BUG_ON(!(nd->flags & LOOKUP_RCU));

	nd->flags &= ~LOOKUP_RCU;
	if (unlikely(!legitimize_links(nd)))
		goto out2;
	if (unlikely(!legitimize_mnt(nd->path.mnt, nd->m_seq)))
		goto out2;
	if (unlikely(!lockref_get_not_dead(&parent->d_lockref)))
		goto out1;

	/*
	 * For a negative lookup, the lookup sequence point is the parents
	 * sequence point, and it only needs to revalidate the parent dentry.
	 *
	 * For a positive lookup, we need to move both the parent and the
	 * dentry from the RCU domain to be properly refcounted. And the
	 * sequence number in the dentry validates *both* dentry counters,
	 * since we checked the sequence number of the parent after we got
	 * the child sequence number. So we know the parent must still
	 * be valid if the child sequence number is still valid.
	 */
	if (!dentry) {
		if (read_seqcount_retry(&parent->d_seq, nd->seq))
			goto out;
		BUG_ON(nd->inode != parent->d_inode);
	} else {
		if (!lockref_get_not_dead(&dentry->d_lockref))
			goto out;
		if (read_seqcount_retry(&dentry->d_seq, seq))
			goto drop_dentry;
	}

	/*
	 * Sequence counts matched. Now make sure that the root is
	 * still valid and get it if required.
	 */
	if (nd->root.mnt && !(nd->flags & LOOKUP_ROOT)) {
		if (unlikely(!legitimize_path(nd, &nd->root, nd->root_seq))) {
			rcu_read_unlock();
			dput(dentry);
			return -ECHILD;
		}
	}

	rcu_read_unlock();
	return 0;

drop_dentry:
	rcu_read_unlock();
	dput(dentry);
	goto drop_root_mnt;
out2:
	nd->path.mnt = NULL;
out1:
	nd->path.dentry = NULL;
out:
	rcu_read_unlock();
drop_root_mnt:
	if (!(nd->flags & LOOKUP_ROOT))
		nd->root.mnt = NULL;
	return -ECHILD;
}

static int unlazy_link(struct nameidata *nd, struct path *link, unsigned seq)
{
	if (unlikely(!legitimize_path(nd, link, seq))) {
		drop_links(nd);
		nd->depth = 0;
		nd->flags &= ~LOOKUP_RCU;
		nd->path.mnt = NULL;
		nd->path.dentry = NULL;
		if (!(nd->flags & LOOKUP_ROOT))
			nd->root.mnt = NULL;
		rcu_read_unlock();
	} else if (likely(unlazy_walk(nd, NULL, 0)) == 0) {
		return 0;
	}
	path_put(link);
	return -ECHILD;
}

static inline int d_revalidate(struct dentry *dentry, unsigned int flags)
{
	return dentry->d_op->d_revalidate(dentry, flags);
}

/**
 * complete_walk - successful completion of path walk
 * @nd:  pointer nameidata
 *
 * If we had been in RCU mode, drop out of it and legitimize nd->path.
 * Revalidate the final result, unless we'd already done that during
 * the path walk or the filesystem doesn't ask for it.  Return 0 on
 * success, -error on failure.  In case of failure caller does not
 * need to drop nd->path.
 */
static int complete_walk(struct nameidata *nd)
{
	struct dentry *dentry = nd->path.dentry;
	int status;

	if (nd->flags & LOOKUP_RCU) {
		if (!(nd->flags & LOOKUP_ROOT))
			nd->root.mnt = NULL;
		if (unlikely(unlazy_walk(nd, NULL, 0)))
			return -ECHILD;
	}

	if (likely(!(nd->flags & LOOKUP_JUMPED)))
		return 0;

	if (likely(!(dentry->d_flags & DCACHE_OP_WEAK_REVALIDATE)))
		return 0;

	status = dentry->d_op->d_weak_revalidate(dentry, nd->flags);
	if (status > 0)
		return 0;

	if (!status)
		status = -ESTALE;

	return status;
}

static void set_root(struct nameidata *nd)
{
	get_fs_root(current->fs, &nd->root);
}

static void set_root_rcu(struct nameidata *nd)
{
	struct fs_struct *fs = current->fs;
	unsigned seq;

	do {
		seq = read_seqcount_begin(&fs->seq);
		nd->root = fs->root;
		nd->root_seq = __read_seqcount_begin(&nd->root.dentry->d_seq);
	} while (read_seqcount_retry(&fs->seq, seq));
}

static void path_put_conditional(struct path *path, struct nameidata *nd)
{
	dput(path->dentry);
	if (path->mnt != nd->path.mnt)
		mntput(path->mnt);
}

static inline void path_to_nameidata(const struct path *path,
					struct nameidata *nd)
{
	if (!(nd->flags & LOOKUP_RCU)) {
		dput(nd->path.dentry);
		if (nd->path.mnt != path->mnt)
			mntput(nd->path.mnt);
	}
	nd->path.mnt = path->mnt;
	nd->path.dentry = path->dentry;
}

/*
 * Helper to directly jump to a known parsed path from ->follow_link,
 * caller must have taken a reference to path beforehand.
 */
void nd_jump_link(struct path *path)
{
	struct nameidata *nd = current->nameidata;
	path_put(&nd->path);

	nd->path = *path;
	nd->inode = nd->path.dentry->d_inode;
	nd->flags |= LOOKUP_JUMPED;
}

static inline void put_link(struct nameidata *nd)
{
	struct saved *last = nd->stack + --nd->depth;
	struct inode *inode = last->inode;
	if (last->cookie && inode->i_op->put_link)
		inode->i_op->put_link(inode, last->cookie);
	if (!(nd->flags & LOOKUP_RCU))
		path_put(&last->link);
}

int sysctl_protected_symlinks __read_mostly = 0;
int sysctl_protected_hardlinks __read_mostly = 0;

/**
 * may_follow_link - Check symlink following for unsafe situations
 * @nd: nameidata pathwalk data
 *
 * In the case of the sysctl_protected_symlinks sysctl being enabled,
 * CAP_DAC_OVERRIDE needs to be specifically ignored if the symlink is
 * in a sticky world-writable directory. This is to protect privileged
 * processes from failing races against path names that may change out
 * from under them by way of other users creating malicious symlinks.
 * It will permit symlinks to be followed only when outside a sticky
 * world-writable directory, or when the uid of the symlink and follower
 * match, or when the directory owner matches the symlink's owner.
 *
 * Returns 0 if following the symlink is allowed, -ve on error.
 */
static inline int may_follow_link(struct nameidata *nd)
{
	const struct inode *inode;
	const struct inode *parent;
	kuid_t puid;

	if (!sysctl_protected_symlinks)
		return 0;

	/* Allowed if owner and follower match. */
	inode = nd->stack[0].inode;
	if (uid_eq(current_cred()->fsuid, inode->i_uid))
		return 0;

	/* Allowed if parent directory not sticky and world-writable. */
	parent = nd->inode;
	if ((parent->i_mode & (S_ISVTX|S_IWOTH)) != (S_ISVTX|S_IWOTH))
		return 0;

	/* Allowed if parent directory and link owner match. */
	puid = parent->i_uid;
	if (uid_valid(puid) && uid_eq(puid, inode->i_uid))
		return 0;

	if (nd->flags & LOOKUP_RCU)
		return -ECHILD;

	audit_log_link_denied("follow_link", &nd->stack[0].link);
	return -EACCES;
}

/**
 * safe_hardlink_source - Check for safe hardlink conditions
 * @inode: the source inode to hardlink from
 *
 * Return false if at least one of the following conditions:
 *    - inode is not a regular file
 *    - inode is setuid
 *    - inode is setgid and group-exec
 *    - access failure for read and write
 *
 * Otherwise returns true.
 */
static bool safe_hardlink_source(struct inode *inode)
{
	umode_t mode = inode->i_mode;

	/* Special files should not get pinned to the filesystem. */
	if (!S_ISREG(mode))
		return false;

	/* Setuid files should not get pinned to the filesystem. */
	if (mode & S_ISUID)
		return false;

	/* Executable setgid files should not get pinned to the filesystem. */
	if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP))
		return false;

	/* Hardlinking to unreadable or unwritable sources is dangerous. */
	if (inode_permission(inode, MAY_READ | MAY_WRITE))
		return false;

	return true;
}

/**
 * may_linkat - Check permissions for creating a hardlink
 * @link: the source to hardlink from
 *
 * Block hardlink when all of:
 *  - sysctl_protected_hardlinks enabled
 *  - fsuid does not match inode
 *  - hardlink source is unsafe (see safe_hardlink_source() above)
 *  - not CAP_FOWNER in a namespace with the inode owner uid mapped
 *
 * Returns 0 if successful, -ve on error.
 */
static int may_linkat(struct path *link)
{
	struct inode *inode;

	if (!sysctl_protected_hardlinks)
		return 0;

	inode = link->dentry->d_inode;

	/* Source inode owner (or CAP_FOWNER) can hardlink all they like,
	 * otherwise, it must be a safe source.
	 */
	if (inode_owner_or_capable(inode) || safe_hardlink_source(inode))
		return 0;

	audit_log_link_denied("linkat", link);
	return -EPERM;
}

static __always_inline
const char *get_link(struct nameidata *nd)
{
	struct saved *last = nd->stack + nd->depth - 1;
	struct dentry *dentry = last->link.dentry;
	struct inode *inode = last->inode;
	int error;
	const char *res;

	if (!(nd->flags & LOOKUP_RCU)) {
		touch_atime(&last->link);
		cond_resched();
	} else if (atime_needs_update(&last->link, inode)) {
		if (unlikely(unlazy_walk(nd, NULL, 0)))
			return ERR_PTR(-ECHILD);
		touch_atime(&last->link);
	}

	error = security_inode_follow_link(dentry, inode,
					   nd->flags & LOOKUP_RCU);
	if (unlikely(error))
		return ERR_PTR(error);

	nd->last_type = LAST_BIND;
	res = inode->i_link;
	if (!res) {
		if (nd->flags & LOOKUP_RCU) {
			if (unlikely(unlazy_walk(nd, NULL, 0)))
				return ERR_PTR(-ECHILD);
		}
		res = inode->i_op->follow_link(dentry, &last->cookie);
		if (IS_ERR_OR_NULL(res)) {
			last->cookie = NULL;
			return res;
		}
	}
	if (*res == '/') {
		if (nd->flags & LOOKUP_RCU) {
			struct dentry *d;
			if (!nd->root.mnt)
				set_root_rcu(nd);
			nd->path = nd->root;
			d = nd->path.dentry;
			nd->inode = d->d_inode;
			nd->seq = nd->root_seq;
			if (unlikely(read_seqcount_retry(&d->d_seq, nd->seq)))
				return ERR_PTR(-ECHILD);
		} else {
			if (!nd->root.mnt)
				set_root(nd);
			path_put(&nd->path);
			nd->path = nd->root;
			path_get(&nd->root);
			nd->inode = nd->path.dentry->d_inode;
		}
		nd->flags |= LOOKUP_JUMPED;
		while (unlikely(*++res == '/'))
			;
	}
	if (!*res)
		res = NULL;
	return res;
}

/*
 * follow_up - Find the mountpoint of path's vfsmount
 *
 * Given a path, find the mountpoint of its source file system.
 * Replace @path with the path of the mountpoint in the parent mount.
 * Up is towards /.
 *
 * Return 1 if we went up a level and 0 if we were already at the
 * root.
 */
int follow_up(struct path *path)
{
	struct mount *mnt = real_mount(path->mnt);
	struct mount *parent;
	struct dentry *mountpoint;

	read_seqlock_excl(&mount_lock);
	parent = mnt->mnt_parent;
	if (parent == mnt) {
		read_sequnlock_excl(&mount_lock);
		return 0;
	}
	mntget(&parent->mnt);
	mountpoint = dget(mnt->mnt_mountpoint);
	read_sequnlock_excl(&mount_lock);
	dput(path->dentry);
	path->dentry = mountpoint;
	mntput(path->mnt);
	path->mnt = &parent->mnt;
	return 1;
}
EXPORT_SYMBOL(follow_up);

/*
 * Perform an automount
 * - return -EISDIR to tell follow_managed() to stop and return the path we
 *   were called with.
 */
static int follow_automount(struct path *path, struct nameidata *nd,
			    bool *need_mntput)
{
	struct vfsmount *mnt;
	int err;

	if (!path->dentry->d_op || !path->dentry->d_op->d_automount)
		return -EREMOTE;

	/* We don't want to mount if someone's just doing a stat -
	 * unless they're stat'ing a directory and appended a '/' to
	 * the name.
	 *
	 * We do, however, want to mount if someone wants to open or
	 * create a file of any type under the mountpoint, wants to
	 * traverse through the mountpoint or wants to open the
	 * mounted directory.  Also, autofs may mark negative dentries
	 * as being automount points.  These will need the attentions
	 * of the daemon to instantiate them before they can be used.
	 */
	if (!(nd->flags & (LOOKUP_PARENT | LOOKUP_DIRECTORY |
			   LOOKUP_OPEN | LOOKUP_CREATE | LOOKUP_AUTOMOUNT)) &&
	    path->dentry->d_inode)
		return -EISDIR;

	nd->total_link_count++;
	if (nd->total_link_count >= 40)
		return -ELOOP;

	mnt = path->dentry->d_op->d_automount(path);
	if (IS_ERR(mnt)) {
		/*
		 * The filesystem is allowed to return -EISDIR here to indicate
		 * it doesn't want to automount.  For instance, autofs would do
		 * this so that its userspace daemon can mount on this dentry.
		 *
		 * However, we can only permit this if it's a terminal point in
		 * the path being looked up; if it wasn't then the remainder of
		 * the path is inaccessible and we should say so.
		 */
		if (PTR_ERR(mnt) == -EISDIR && (nd->flags & LOOKUP_PARENT))
			return -EREMOTE;
		return PTR_ERR(mnt);
	}

	if (!mnt) /* mount collision */
		return 0;

	if (!*need_mntput) {
		/* lock_mount() may release path->mnt on error */
		mntget(path->mnt);
		*need_mntput = true;
	}
	err = finish_automount(mnt, path);

	switch (err) {
	case -EBUSY:
		/* Someone else made a mount here whilst we were busy */
		return 0;
	case 0:
		path_put(path);
		path->mnt = mnt;
		path->dentry = dget(mnt->mnt_root);
		return 0;
	default:
		return err;
	}

}

/*
 * Handle a dentry that is managed in some way.
 * - Flagged for transit management (autofs)
 * - Flagged as mountpoint
 * - Flagged as automount point
 *
 * This may only be called in refwalk mode.
 *
 * Serialization is taken care of in namespace.c
 */
static int follow_managed(struct path *path, struct nameidata *nd)
{
	struct vfsmount *mnt = path->mnt; /* held by caller, must be left alone */
	unsigned managed;
	bool need_mntput = false;
	int ret = 0;

	/* Given that we're not holding a lock here, we retain the value in a
	 * local variable for each dentry as we look at it so that we don't see
	 * the components of that value change under us */
	while (managed = ACCESS_ONCE(path->dentry->d_flags),
	       managed &= DCACHE_MANAGED_DENTRY,
	       unlikely(managed != 0)) {
		/* Allow the filesystem to manage the transit without i_mutex
		 * being held. */
		if (managed & DCACHE_MANAGE_TRANSIT) {
			BUG_ON(!path->dentry->d_op);
			BUG_ON(!path->dentry->d_op->d_manage);
			ret = path->dentry->d_op->d_manage(path->dentry, false);
			if (ret < 0)
				break;
		}

		/* Transit to a mounted filesystem. */
		if (managed & DCACHE_MOUNTED) {
			struct vfsmount *mounted = lookup_mnt(path);
			if (mounted) {
				dput(path->dentry);
				if (need_mntput)
					mntput(path->mnt);
				path->mnt = mounted;
				path->dentry = dget(mounted->mnt_root);
				need_mntput = true;
				continue;
			}

			/* Something is mounted on this dentry in another
			 * namespace and/or whatever was mounted there in this
			 * namespace got unmounted before lookup_mnt() could
			 * get it */
		}

		/* Handle an automount point */
		if (managed & DCACHE_NEED_AUTOMOUNT) {
			ret = follow_automount(path, nd, &need_mntput);
			if (ret < 0)
				break;
			continue;
		}

		/* We didn't change the current path point */
		break;
	}

	if (need_mntput && path->mnt == mnt)
		mntput(path->mnt);
	if (ret == -EISDIR)
		ret = 0;
	if (need_mntput)
		nd->flags |= LOOKUP_JUMPED;
	if (unlikely(ret < 0))
		path_put_conditional(path, nd);
	return ret;
}

int follow_down_one(struct path *path)
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
EXPORT_SYMBOL(follow_down_one);

static inline int managed_dentry_rcu(struct dentry *dentry)
{
	return (dentry->d_flags & DCACHE_MANAGE_TRANSIT) ?
		dentry->d_op->d_manage(dentry, true) : 0;
}

/*
 * Try to skip to top of mountpoint pile in rcuwalk mode.  Fail if
 * we meet a managed dentry that would need blocking.
 */
static bool __follow_mount_rcu(struct nameidata *nd, struct path *path,
			       struct inode **inode, unsigned *seqp)
{
	for (;;) {
		struct mount *mounted;
		/*
		 * Don't forget we might have a non-mountpoint managed dentry
		 * that wants to block transit.
		 */
		switch (managed_dentry_rcu(path->dentry)) {
		case -ECHILD:
		default:
			return false;
		case -EISDIR:
			return true;
		case 0:
			break;
		}

		if (!d_mountpoint(path->dentry))
			return !(path->dentry->d_flags & DCACHE_NEED_AUTOMOUNT);

		mounted = __lookup_mnt(path->mnt, path->dentry);
		if (!mounted)
			break;
		path->mnt = &mounted->mnt;
		path->dentry = mounted->mnt.mnt_root;
		nd->flags |= LOOKUP_JUMPED;
		*seqp = read_seqcount_begin(&path->dentry->d_seq);
		/*
		 * Update the inode too. We don't need to re-check the
		 * dentry sequence number here after this d_inode read,
		 * because a mount-point is always pinned.
		 */
		*inode = path->dentry->d_inode;
	}
	return !read_seqretry(&mount_lock, nd->m_seq) &&
		!(path->dentry->d_flags & DCACHE_NEED_AUTOMOUNT);
}

static int follow_dotdot_rcu(struct nameidata *nd)
{
	struct inode *inode = nd->inode;
	if (!nd->root.mnt)
		set_root_rcu(nd);

	while (1) {
		if (path_equal(&nd->path, &nd->root))
			break;
		if (nd->path.dentry != nd->path.mnt->mnt_root) {
			struct dentry *old = nd->path.dentry;
			struct dentry *parent = old->d_parent;
			unsigned seq;

			inode = parent->d_inode;
			seq = read_seqcount_begin(&parent->d_seq);
			if (unlikely(read_seqcount_retry(&old->d_seq, nd->seq)))
				return -ECHILD;
			nd->path.dentry = parent;
			nd->seq = seq;
			if (unlikely(!path_connected(&nd->path)))
				return -ENOENT;
			break;
		} else {
			struct mount *mnt = real_mount(nd->path.mnt);
			struct mount *mparent = mnt->mnt_parent;
			struct dentry *mountpoint = mnt->mnt_mountpoint;
			struct inode *inode2 = mountpoint->d_inode;
			unsigned seq = read_seqcount_begin(&mountpoint->d_seq);
			if (unlikely(read_seqretry(&mount_lock, nd->m_seq)))
				return -ECHILD;
			if (&mparent->mnt == nd->path.mnt)
				break;
			/* we know that mountpoint was pinned */
			nd->path.dentry = mountpoint;
			nd->path.mnt = &mparent->mnt;
			inode = inode2;
			nd->seq = seq;
		}
	}
	while (unlikely(d_mountpoint(nd->path.dentry))) {
		struct mount *mounted;
		mounted = __lookup_mnt(nd->path.mnt, nd->path.dentry);
		if (unlikely(read_seqretry(&mount_lock, nd->m_seq)))
			return -ECHILD;
		if (!mounted)
			break;
		nd->path.mnt = &mounted->mnt;
		nd->path.dentry = mounted->mnt.mnt_root;
		inode = nd->path.dentry->d_inode;
		nd->seq = read_seqcount_begin(&nd->path.dentry->d_seq);
	}
	nd->inode = inode;
	return 0;
}

/*
 * Follow down to the covering mount currently visible to userspace.  At each
 * point, the filesystem owning that dentry may be queried as to whether the
 * caller is permitted to proceed or not.
 */
int follow_down(struct path *path)
{
	unsigned managed;
	int ret;

	while (managed = ACCESS_ONCE(path->dentry->d_flags),
	       unlikely(managed & DCACHE_MANAGED_DENTRY)) {
		/* Allow the filesystem to manage the transit without i_mutex
		 * being held.
		 *
		 * We indicate to the filesystem if someone is trying to mount
		 * something here.  This gives autofs the chance to deny anyone
		 * other than its daemon the right to mount on its
		 * superstructure.
		 *
		 * The filesystem may sleep at this point.
		 */
		if (managed & DCACHE_MANAGE_TRANSIT) {
			BUG_ON(!path->dentry->d_op);
			BUG_ON(!path->dentry->d_op->d_manage);
			ret = path->dentry->d_op->d_manage(
				path->dentry, false);
			if (ret < 0)
				return ret == -EISDIR ? 0 : ret;
		}

		/* Transit to a mounted filesystem. */
		if (managed & DCACHE_MOUNTED) {
			struct vfsmount *mounted = lookup_mnt(path);
			if (!mounted)
				break;
			dput(path->dentry);
			mntput(path->mnt);
			path->mnt = mounted;
			path->dentry = dget(mounted->mnt_root);
			continue;
		}

		/* Don't handle automount points here */
		break;
	}
	return 0;
}
EXPORT_SYMBOL(follow_down);

/*
 * Skip to top of mountpoint pile in refwalk mode for follow_dotdot()
 */
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

static int follow_dotdot(struct nameidata *nd)
{
	if (!nd->root.mnt)
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
			if (unlikely(!path_connected(&nd->path)))
				return -ENOENT;
			break;
		}
		if (!follow_up(&nd->path))
			break;
	}
	follow_mount(&nd->path);
	nd->inode = nd->path.dentry->d_inode;
	return 0;
}

/*
 * This looks up the name in dcache, possibly revalidates the old dentry and
 * allocates a new one if not found or not valid.  In the need_lookup argument
 * returns whether i_op->lookup is necessary.
 *
 * dir->d_inode->i_mutex must be held
 */
static struct dentry *lookup_dcache(struct qstr *name, struct dentry *dir,
				    unsigned int flags, bool *need_lookup)
{
	struct dentry *dentry;
	int error;

	*need_lookup = false;
	dentry = d_lookup(dir, name);
	if (dentry) {
		if (dentry->d_flags & DCACHE_OP_REVALIDATE) {
			error = d_revalidate(dentry, flags);
			if (unlikely(error <= 0)) {
				if (error < 0) {
					dput(dentry);
					return ERR_PTR(error);
				} else {
					d_invalidate(dentry);
					dput(dentry);
					dentry = NULL;
				}
			}
		}
	}

	if (!dentry) {
		dentry = d_alloc(dir, name);
		if (unlikely(!dentry))
			return ERR_PTR(-ENOMEM);

		*need_lookup = true;
	}
	return dentry;
}

/*
 * Call i_op->lookup on the dentry.  The dentry must be negative and
 * unhashed.
 *
 * dir->d_inode->i_mutex must be held
 */
static struct dentry *lookup_real(struct inode *dir, struct dentry *dentry,
				  unsigned int flags)
{
	struct dentry *old;

	/* Don't create child dentry for a dead directory. */
	if (unlikely(IS_DEADDIR(dir))) {
		dput(dentry);
		return ERR_PTR(-ENOENT);
	}

	old = dir->i_op->lookup(dir, dentry, flags);
	if (unlikely(old)) {
		dput(dentry);
		dentry = old;
	}
	return dentry;
}

static struct dentry *__lookup_hash(struct qstr *name,
		struct dentry *base, unsigned int flags)
{
	bool need_lookup;
	struct dentry *dentry;

	dentry = lookup_dcache(name, base, flags, &need_lookup);
	if (!need_lookup)
		return dentry;

	return lookup_real(base->d_inode, dentry, flags);
}

/*
 *  It's more convoluted than I'd like it to be, but... it's still fairly
 *  small and for now I'd prefer to have fast path as straight as possible.
 *  It _is_ time-critical.
 */
static int lookup_fast(struct nameidata *nd,
		       struct path *path, struct inode **inode,
		       unsigned *seqp)
{
	struct vfsmount *mnt = nd->path.mnt;
	struct dentry *dentry, *parent = nd->path.dentry;
	int need_reval = 1;
	int status = 1;
	int err;

	/*
	 * Rename seqlock is not required here because in the off chance
	 * of a false negative due to a concurrent rename, we're going to
	 * do the non-racy lookup, below.
	 */
	if (nd->flags & LOOKUP_RCU) {
		unsigned seq;
		bool negative;
		dentry = __d_lookup_rcu(parent, &nd->last, &seq);
		if (!dentry)
			goto unlazy;

		/*
		 * This sequence count validates that the inode matches
		 * the dentry name information from lookup.
		 */
		*inode = d_backing_inode(dentry);
		negative = d_is_negative(dentry);
		if (read_seqcount_retry(&dentry->d_seq, seq))
			return -ECHILD;

		/*
		 * This sequence count validates that the parent had no
		 * changes while we did the lookup of the dentry above.
		 *
		 * The memory barrier in read_seqcount_begin of child is
		 *  enough, we can use __read_seqcount_retry here.
		 */
		if (__read_seqcount_retry(&parent->d_seq, nd->seq))
			return -ECHILD;

		*seqp = seq;
		if (unlikely(dentry->d_flags & DCACHE_OP_REVALIDATE)) {
			status = d_revalidate(dentry, nd->flags);
			if (unlikely(status <= 0)) {
				if (status != -ECHILD)
					need_reval = 0;
				goto unlazy;
			}
		}
		/*
		 * Note: do negative dentry check after revalidation in
		 * case that drops it.
		 */
		if (negative)
			return -ENOENT;
		path->mnt = mnt;
		path->dentry = dentry;
		if (likely(__follow_mount_rcu(nd, path, inode, seqp)))
			return 0;
unlazy:
		if (unlazy_walk(nd, dentry, seq))
			return -ECHILD;
	} else {
		dentry = __d_lookup(parent, &nd->last);
	}

	if (unlikely(!dentry))
		goto need_lookup;

	if (unlikely(dentry->d_flags & DCACHE_OP_REVALIDATE) && need_reval)
		status = d_revalidate(dentry, nd->flags);
	if (unlikely(status <= 0)) {
		if (status < 0) {
			dput(dentry);
			return status;
		}
		d_invalidate(dentry);
		dput(dentry);
		goto need_lookup;
	}

	if (unlikely(d_is_negative(dentry))) {
		dput(dentry);
		return -ENOENT;
	}
	path->mnt = mnt;
	path->dentry = dentry;
	err = follow_managed(path, nd);
	if (likely(!err))
		*inode = d_backing_inode(path->dentry);
	return err;

need_lookup:
	return 1;
}

/* Fast lookup failed, do it the slow way */
static int lookup_slow(struct nameidata *nd, struct path *path)
{
	struct dentry *dentry, *parent;

	parent = nd->path.dentry;
	BUG_ON(nd->inode != parent->d_inode);

	mutex_lock(&parent->d_inode->i_mutex);
	dentry = __lookup_hash(&nd->last, parent, nd->flags);
	mutex_unlock(&parent->d_inode->i_mutex);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	path->mnt = nd->path.mnt;
	path->dentry = dentry;
	return follow_managed(path, nd);
}

static inline int may_lookup(struct nameidata *nd)
{
	if (nd->flags & LOOKUP_RCU) {
		int err = inode_permission(nd->inode, MAY_EXEC|MAY_NOT_BLOCK);
		if (err != -ECHILD)
			return err;
		if (unlazy_walk(nd, NULL, 0))
			return -ECHILD;
	}
	return inode_permission(nd->inode, MAY_EXEC);
}

static inline int handle_dots(struct nameidata *nd, int type)
{
	if (type == LAST_DOTDOT) {
		if (nd->flags & LOOKUP_RCU) {
			return follow_dotdot_rcu(nd);
		} else
			return follow_dotdot(nd);
	}
	return 0;
}

static int pick_link(struct nameidata *nd, struct path *link,
		     struct inode *inode, unsigned seq)
{
	int error;
	struct saved *last;
	if (unlikely(nd->total_link_count++ >= MAXSYMLINKS)) {
		path_to_nameidata(link, nd);
		return -ELOOP;
	}
	if (!(nd->flags & LOOKUP_RCU)) {
		if (link->mnt == nd->path.mnt)
			mntget(link->mnt);
	}
	error = nd_alloc_stack(nd);
	if (unlikely(error)) {
		if (error == -ECHILD) {
			if (unlikely(unlazy_link(nd, link, seq)))
				return -ECHILD;
			error = nd_alloc_stack(nd);
		}
		if (error) {
			path_put(link);
			return error;
		}
	}

	last = nd->stack + nd->depth++;
	last->link = *link;
	last->cookie = NULL;
	last->inode = inode;
	last->seq = seq;
	return 1;
}

/*
 * Do we need to follow links? We _really_ want to be able
 * to do this check without having to look at inode->i_op,
 * so we keep a cache of "no, this doesn't need follow_link"
 * for the common case.
 */
static inline int should_follow_link(struct nameidata *nd, struct path *link,
				     int follow,
				     struct inode *inode, unsigned seq)
{
	if (likely(!d_is_symlink(link->dentry)))
		return 0;
	if (!follow)
		return 0;
	/* make sure that d_is_symlink above matches inode */
	if (nd->flags & LOOKUP_RCU) {
		if (read_seqcount_retry(&link->dentry->d_seq, seq))
			return -ECHILD;
	}
	return pick_link(nd, link, inode, seq);
}

enum {WALK_GET = 1, WALK_PUT = 2};

static int walk_component(struct nameidata *nd, int flags)
{
	struct path path;
	struct inode *inode;
	unsigned seq;
	int err;
	/*
	 * "." and ".." are special - ".." especially so because it has
	 * to be able to know about the current root directory and
	 * parent relationships.
	 */
	if (unlikely(nd->last_type != LAST_NORM)) {
		err = handle_dots(nd, nd->last_type);
		if (flags & WALK_PUT)
			put_link(nd);
		return err;
	}
	err = lookup_fast(nd, &path, &inode, &seq);
	if (unlikely(err)) {
		if (err < 0)
			return err;

		err = lookup_slow(nd, &path);
		if (err < 0)
			return err;

		seq = 0;	/* we are already out of RCU mode */
		err = -ENOENT;
		if (d_is_negative(path.dentry))
			goto out_path_put;
		inode = d_backing_inode(path.dentry);
	}

	if (flags & WALK_PUT)
		put_link(nd);
	err = should_follow_link(nd, &path, flags & WALK_GET, inode, seq);
	if (unlikely(err))
		return err;
	path_to_nameidata(&path, nd);
	nd->inode = inode;
	nd->seq = seq;
	return 0;

out_path_put:
	path_to_nameidata(&path, nd);
	return err;
}

/*
 * We can do the critical dentry name comparison and hashing
 * operations one word at a time, but we are limited to:
 *
 * - Architectures with fast unaligned word accesses. We could
 *   do a "get_unaligned()" if this helps and is sufficiently
 *   fast.
 *
 * - non-CONFIG_DEBUG_PAGEALLOC configurations (so that we
 *   do not trap on the (extremely unlikely) case of a page
 *   crossing operation.
 *
 * - Furthermore, we need an efficient 64-bit compile for the
 *   64-bit case in order to generate the "number of bytes in
 *   the final mask". Again, that could be replaced with a
 *   efficient population count instruction or similar.
 */
#ifdef CONFIG_DCACHE_WORD_ACCESS

#include <asm/word-at-a-time.h>

#ifdef CONFIG_64BIT

static inline unsigned int fold_hash(unsigned long hash)
{
	return hash_64(hash, 32);
}

#else	/* 32-bit case */

#define fold_hash(x) (x)

#endif

unsigned int full_name_hash(const unsigned char *name, unsigned int len)
{
	unsigned long a, mask;
	unsigned long hash = 0;

	for (;;) {
		a = load_unaligned_zeropad(name);
		if (len < sizeof(unsigned long))
			break;
		hash += a;
		hash *= 9;
		name += sizeof(unsigned long);
		len -= sizeof(unsigned long);
		if (!len)
			goto done;
	}
	mask = bytemask_from_count(len);
	hash += mask & a;
done:
	return fold_hash(hash);
}
EXPORT_SYMBOL(full_name_hash);

/*
 * Calculate the length and hash of the path component, and
 * return the "hash_len" as the result.
 */
static inline u64 hash_name(const char *name)
{
	unsigned long a, b, adata, bdata, mask, hash, len;
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;

	hash = a = 0;
	len = -sizeof(unsigned long);
	do {
		hash = (hash + a) * 9;
		len += sizeof(unsigned long);
		a = load_unaligned_zeropad(name+len);
		b = a ^ REPEAT_BYTE('/');
	} while (!(has_zero(a, &adata, &constants) | has_zero(b, &bdata, &constants)));

	adata = prep_zero_mask(a, adata, &constants);
	bdata = prep_zero_mask(b, bdata, &constants);

	mask = create_zero_mask(adata | bdata);

	hash += a & zero_bytemask(mask);
	len += find_zero(mask);
	return hashlen_create(fold_hash(hash), len);
}

#else

unsigned int full_name_hash(const unsigned char *name, unsigned int len)
{
	unsigned long hash = init_name_hash();
	while (len--)
		hash = partial_name_hash(*name++, hash);
	return end_name_hash(hash);
}
EXPORT_SYMBOL(full_name_hash);

/*
 * We know there's a real path component here of at least
 * one character.
 */
static inline u64 hash_name(const char *name)
{
	unsigned long hash = init_name_hash();
	unsigned long len = 0, c;

	c = (unsigned char)*name;
	do {
		len++;
		hash = partial_name_hash(c, hash);
		c = (unsigned char)name[len];
	} while (c && c != '/');
	return hashlen_create(end_name_hash(hash), len);
}

#endif

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
	int err;

	while (*name=='/')
		name++;
	if (!*name)
		return 0;

	/* At this point we know we have a real path component. */
	for(;;) {
		u64 hash_len;
		int type;

		err = may_lookup(nd);
 		if (err)
			return err;

		hash_len = hash_name(name);

		type = LAST_NORM;
		if (name[0] == '.') switch (hashlen_len(hash_len)) {
			case 2:
				if (name[1] == '.') {
					type = LAST_DOTDOT;
					nd->flags |= LOOKUP_JUMPED;
				}
				break;
			case 1:
				type = LAST_DOT;
		}
		if (likely(type == LAST_NORM)) {
			struct dentry *parent = nd->path.dentry;
			nd->flags &= ~LOOKUP_JUMPED;
			if (unlikely(parent->d_flags & DCACHE_OP_HASH)) {
				struct qstr this = { { .hash_len = hash_len }, .name = name };
				err = parent->d_op->d_hash(parent, &this);
				if (err < 0)
					return err;
				hash_len = this.hash_len;
				name = this.name;
			}
		}

		nd->last.hash_len = hash_len;
		nd->last.name = name;
		nd->last_type = type;

		name += hashlen_len(hash_len);
		if (!*name)
			goto OK;
		/*
		 * If it wasn't NUL, we know it was '/'. Skip that
		 * slash, and continue until no more slashes.
		 */
		do {
			name++;
		} while (unlikely(*name == '/'));
		if (unlikely(!*name)) {
OK:
			/* pathname body, done */
			if (!nd->depth)
				return 0;
			name = nd->stack[nd->depth - 1].name;
			/* trailing symlink, done */
			if (!name)
				return 0;
			/* last component of nested symlink */
			err = walk_component(nd, WALK_GET | WALK_PUT);
		} else {
			err = walk_component(nd, WALK_GET);
		}
		if (err < 0)
			return err;

		if (err) {
			const char *s = get_link(nd);

			if (IS_ERR(s))
				return PTR_ERR(s);
			err = 0;
			if (unlikely(!s)) {
				/* jumped */
				put_link(nd);
			} else {
				nd->stack[nd->depth - 1].name = name;
				name = s;
				continue;
			}
		}
		if (unlikely(!d_can_lookup(nd->path.dentry))) {
			if (nd->flags & LOOKUP_RCU) {
				if (unlazy_walk(nd, NULL, 0))
					return -ECHILD;
			}
			return -ENOTDIR;
		}
	}
}

static const char *path_init(struct nameidata *nd, unsigned flags)
{
	int retval = 0;
	const char *s = nd->name->name;

	if (!*s)
		flags &= ~LOOKUP_RCU;

	nd->last_type = LAST_ROOT; /* if there are only slashes... */
	nd->flags = flags | LOOKUP_JUMPED | LOOKUP_PARENT;
	nd->depth = 0;
	if (flags & LOOKUP_ROOT) {
		struct dentry *root = nd->root.dentry;
		struct inode *inode = root->d_inode;
		if (*s) {
			if (!d_can_lookup(root))
				return ERR_PTR(-ENOTDIR);
			retval = inode_permission(inode, MAY_EXEC);
			if (retval)
				return ERR_PTR(retval);
		}
		nd->path = nd->root;
		nd->inode = inode;
		if (flags & LOOKUP_RCU) {
			rcu_read_lock();
			nd->seq = __read_seqcount_begin(&nd->path.dentry->d_seq);
			nd->root_seq = nd->seq;
			nd->m_seq = read_seqbegin(&mount_lock);
		} else {
			path_get(&nd->path);
		}
		return s;
	}

	nd->root.mnt = NULL;

	nd->m_seq = read_seqbegin(&mount_lock);
	if (*s == '/') {
		if (flags & LOOKUP_RCU) {
			rcu_read_lock();
			set_root_rcu(nd);
			nd->seq = nd->root_seq;
		} else {
			set_root(nd);
			path_get(&nd->root);
		}
		nd->path = nd->root;
	} else if (nd->dfd == AT_FDCWD) {
		if (flags & LOOKUP_RCU) {
			struct fs_struct *fs = current->fs;
			unsigned seq;

			rcu_read_lock();

			do {
				seq = read_seqcount_begin(&fs->seq);
				nd->path = fs->pwd;
				nd->seq = __read_seqcount_begin(&nd->path.dentry->d_seq);
			} while (read_seqcount_retry(&fs->seq, seq));
		} else {
			get_fs_pwd(current->fs, &nd->path);
		}
	} else {
		/* Caller must check execute permissions on the starting path component */
		struct fd f = fdget_raw(nd->dfd);
		struct dentry *dentry;

		if (!f.file)
			return ERR_PTR(-EBADF);

		dentry = f.file->f_path.dentry;

		if (*s) {
			if (!d_can_lookup(dentry)) {
				fdput(f);
				return ERR_PTR(-ENOTDIR);
			}
		}

		nd->path = f.file->f_path;
		if (flags & LOOKUP_RCU) {
			rcu_read_lock();
			nd->inode = nd->path.dentry->d_inode;
			nd->seq = read_seqcount_begin(&nd->path.dentry->d_seq);
		} else {
			path_get(&nd->path);
			nd->inode = nd->path.dentry->d_inode;
		}
		fdput(f);
		return s;
	}

	nd->inode = nd->path.dentry->d_inode;
	if (!(flags & LOOKUP_RCU))
		return s;
	if (likely(!read_seqcount_retry(&nd->path.dentry->d_seq, nd->seq)))
		return s;
	if (!(nd->flags & LOOKUP_ROOT))
		nd->root.mnt = NULL;
	rcu_read_unlock();
	return ERR_PTR(-ECHILD);
}

static const char *trailing_symlink(struct nameidata *nd)
{
	const char *s;
	int error = may_follow_link(nd);
	if (unlikely(error))
		return ERR_PTR(error);
	nd->flags |= LOOKUP_PARENT;
	nd->stack[0].name = NULL;
	s = get_link(nd);
	return s ? s : "";
}

static inline int lookup_last(struct nameidata *nd)
{
	if (nd->last_type == LAST_NORM && nd->last.name[nd->last.len])
		nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

	nd->flags &= ~LOOKUP_PARENT;
	return walk_component(nd,
			nd->flags & LOOKUP_FOLLOW
				? nd->depth
					? WALK_PUT | WALK_GET
					: WALK_GET
				: 0);
}

/* Returns 0 and nd will be valid on success; Retuns error, otherwise. */
static int path_lookupat(struct nameidata *nd, unsigned flags, struct path *path)
{
	const char *s = path_init(nd, flags);
	int err;

	if (IS_ERR(s))
		return PTR_ERR(s);
	while (!(err = link_path_walk(s, nd))
		&& ((err = lookup_last(nd)) > 0)) {
		s = trailing_symlink(nd);
		if (IS_ERR(s)) {
			err = PTR_ERR(s);
			break;
		}
	}
	if (!err)
		err = complete_walk(nd);

	if (!err && nd->flags & LOOKUP_DIRECTORY)
		if (!d_can_lookup(nd->path.dentry))
			err = -ENOTDIR;
	if (!err) {
		*path = nd->path;
		nd->path.mnt = NULL;
		nd->path.dentry = NULL;
	}
	terminate_walk(nd);
	return err;
}

static int filename_lookup(int dfd, struct filename *name, unsigned flags,
			   struct path *path, struct path *root)
{
	int retval;
	struct nameidata nd;
	if (IS_ERR(name))
		return PTR_ERR(name);
	if (unlikely(root)) {
		nd.root = *root;
		flags |= LOOKUP_ROOT;
	}
	set_nameidata(&nd, dfd, name);
	retval = path_lookupat(&nd, flags | LOOKUP_RCU, path);
	if (unlikely(retval == -ECHILD))
		retval = path_lookupat(&nd, flags, path);
	if (unlikely(retval == -ESTALE))
		retval = path_lookupat(&nd, flags | LOOKUP_REVAL, path);

	if (likely(!retval))
		audit_inode(name, path->dentry, flags & LOOKUP_PARENT);
	restore_nameidata();
	putname(name);
	return retval;
}

/* Returns 0 and nd will be valid on success; Retuns error, otherwise. */
static int path_parentat(struct nameidata *nd, unsigned flags,
				struct path *parent)
{
	const char *s = path_init(nd, flags);
	int err;
	if (IS_ERR(s))
		return PTR_ERR(s);
	err = link_path_walk(s, nd);
	if (!err)
		err = complete_walk(nd);
	if (!err) {
		*parent = nd->path;
		nd->path.mnt = NULL;
		nd->path.dentry = NULL;
	}
	terminate_walk(nd);
	return err;
}

static struct filename *filename_parentat(int dfd, struct filename *name,
				unsigned int flags, struct path *parent,
				struct qstr *last, int *type)
{
	int retval;
	struct nameidata nd;

	if (IS_ERR(name))
		return name;
	set_nameidata(&nd, dfd, name);
	retval = path_parentat(&nd, flags | LOOKUP_RCU, parent);
	if (unlikely(retval == -ECHILD))
		retval = path_parentat(&nd, flags, parent);
	if (unlikely(retval == -ESTALE))
		retval = path_parentat(&nd, flags | LOOKUP_REVAL, parent);
	if (likely(!retval)) {
		*last = nd.last;
		*type = nd.last_type;
		audit_inode(name, parent->dentry, LOOKUP_PARENT);
	} else {
		putname(name);
		name = ERR_PTR(retval);
	}
	restore_nameidata();
	return name;
}

/* does lookup, returns the object with parent locked */
struct dentry *kern_path_locked(const char *name, struct path *path)
{
	struct filename *filename;
	struct dentry *d;
	struct qstr last;
	int type;

	filename = filename_parentat(AT_FDCWD, getname_kernel(name), 0, path,
				    &last, &type);
	if (IS_ERR(filename))
		return ERR_CAST(filename);
	if (unlikely(type != LAST_NORM)) {
		path_put(path);
		putname(filename);
		return ERR_PTR(-EINVAL);
	}
	mutex_lock_nested(&path->dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	d = __lookup_hash(&last, path->dentry, 0);
	if (IS_ERR(d)) {
		mutex_unlock(&path->dentry->d_inode->i_mutex);
		path_put(path);
	}
	putname(filename);
	return d;
}

int kern_path(const char *name, unsigned int flags, struct path *path)
{
	return filename_lookup(AT_FDCWD, getname_kernel(name),
			       flags, path, NULL);
}
EXPORT_SYMBOL(kern_path);

/**
 * vfs_path_lookup - lookup a file path relative to a dentry-vfsmount pair
 * @dentry:  pointer to dentry of the base directory
 * @mnt: pointer to vfs mount of the base directory
 * @name: pointer to file name
 * @flags: lookup flags
 * @path: pointer to struct path to fill
 */
int vfs_path_lookup(struct dentry *dentry, struct vfsmount *mnt,
		    const char *name, unsigned int flags,
		    struct path *path)
{
	struct path root = {.mnt = mnt, .dentry = dentry};
	/* the first argument of filename_lookup() is ignored with root */
	return filename_lookup(AT_FDCWD, getname_kernel(name),
			       flags , path, &root);
}
EXPORT_SYMBOL(vfs_path_lookup);

/**
 * lookup_one_len - filesystem helper to lookup single pathname component
 * @name:	pathname component to lookup
 * @base:	base directory to lookup from
 * @len:	maximum length @len should be interpreted to
 *
 * Note that this routine is purely a helper for filesystem usage and should
 * not be called by generic code.
 */
struct dentry *lookup_one_len(const char *name, struct dentry *base, int len)
{
	struct qstr this;
	unsigned int c;
	int err;

	WARN_ON_ONCE(!mutex_is_locked(&base->d_inode->i_mutex));

	this.name = name;
	this.len = len;
	this.hash = full_name_hash(name, len);
	if (!len)
		return ERR_PTR(-EACCES);

	if (unlikely(name[0] == '.')) {
		if (len < 2 || (len == 2 && name[1] == '.'))
			return ERR_PTR(-EACCES);
	}

	while (len--) {
		c = *(const unsigned char *)name++;
		if (c == '/' || c == '\0')
			return ERR_PTR(-EACCES);
	}
	/*
	 * See if the low-level filesystem might want
	 * to use its own hash..
	 */
	if (base->d_flags & DCACHE_OP_HASH) {
		int err = base->d_op->d_hash(base, &this);
		if (err < 0)
			return ERR_PTR(err);
	}

	err = inode_permission(base->d_inode, MAY_EXEC);
	if (err)
		return ERR_PTR(err);

	return __lookup_hash(&this, base, 0);
}
EXPORT_SYMBOL(lookup_one_len);

int user_path_at_empty(int dfd, const char __user *name, unsigned flags,
		 struct path *path, int *empty)
{
	return filename_lookup(dfd, getname_flags(name, flags, empty),
			       flags, path, NULL);
}
EXPORT_SYMBOL(user_path_at_empty);

/*
 * NB: most callers don't do anything directly with the reference to the
 *     to struct filename, but the nd->last pointer points into the name string
 *     allocated by getname. So we must hold the reference to it until all
 *     path-walking is complete.
 */
static inline struct filename *
user_path_parent(int dfd, const char __user *path,
		 struct path *parent,
		 struct qstr *last,
		 int *type,
		 unsigned int flags)
{
	/* only LOOKUP_REVAL is allowed in extra flags */
	return filename_parentat(dfd, getname(path), flags & LOOKUP_REVAL,
				 parent, last, type);
}

/**
 * mountpoint_last - look up last component for umount
 * @nd:   pathwalk nameidata - currently pointing at parent directory of "last"
 * @path: pointer to container for result
 *
 * This is a special lookup_last function just for umount. In this case, we
 * need to resolve the path without doing any revalidation.
 *
 * The nameidata should be the result of doing a LOOKUP_PARENT pathwalk. Since
 * mountpoints are always pinned in the dcache, their ancestors are too. Thus,
 * in almost all cases, this lookup will be served out of the dcache. The only
 * cases where it won't are if nd->last refers to a symlink or the path is
 * bogus and it doesn't exist.
 *
 * Returns:
 * -error: if there was an error during lookup. This includes -ENOENT if the
 *         lookup found a negative dentry. The nd->path reference will also be
 *         put in this case.
 *
 * 0:      if we successfully resolved nd->path and found it to not to be a
 *         symlink that needs to be followed. "path" will also be populated.
 *         The nd->path reference will also be put.
 *
 * 1:      if we successfully resolved nd->last and found it to be a symlink
 *         that needs to be followed. "path" will be populated with the path
 *         to the link, and nd->path will *not* be put.
 */
static int
mountpoint_last(struct nameidata *nd, struct path *path)
{
	int error = 0;
	struct dentry *dentry;
	struct dentry *dir = nd->path.dentry;

	/* If we're in rcuwalk, drop out of it to handle last component */
	if (nd->flags & LOOKUP_RCU) {
		if (unlazy_walk(nd, NULL, 0))
			return -ECHILD;
	}

	nd->flags &= ~LOOKUP_PARENT;

	if (unlikely(nd->last_type != LAST_NORM)) {
		error = handle_dots(nd, nd->last_type);
		if (error)
			return error;
		dentry = dget(nd->path.dentry);
		goto done;
	}

	mutex_lock(&dir->d_inode->i_mutex);
	dentry = d_lookup(dir, &nd->last);
	if (!dentry) {
		/*
		 * No cached dentry. Mounted dentries are pinned in the cache,
		 * so that means that this dentry is probably a symlink or the
		 * path doesn't actually point to a mounted dentry.
		 */
		dentry = d_alloc(dir, &nd->last);
		if (!dentry) {
			mutex_unlock(&dir->d_inode->i_mutex);
			return -ENOMEM;
		}
		dentry = lookup_real(dir->d_inode, dentry, nd->flags);
		if (IS_ERR(dentry)) {
			mutex_unlock(&dir->d_inode->i_mutex);
			return PTR_ERR(dentry);
		}
	}
	mutex_unlock(&dir->d_inode->i_mutex);

done:
	if (d_is_negative(dentry)) {
		dput(dentry);
		return -ENOENT;
	}
	if (nd->depth)
		put_link(nd);
	path->dentry = dentry;
	path->mnt = nd->path.mnt;
	error = should_follow_link(nd, path, nd->flags & LOOKUP_FOLLOW,
				   d_backing_inode(dentry), 0);
	if (unlikely(error))
		return error;
	mntget(path->mnt);
	follow_mount(path);
	return 0;
}

/**
 * path_mountpoint - look up a path to be umounted
 * @nd:		lookup context
 * @flags:	lookup flags
 * @path:	pointer to container for result
 *
 * Look up the given name, but don't attempt to revalidate the last component.
 * Returns 0 and "path" will be valid on success; Returns error otherwise.
 */
static int
path_mountpoint(struct nameidata *nd, unsigned flags, struct path *path)
{
	const char *s = path_init(nd, flags);
	int err;
	if (IS_ERR(s))
		return PTR_ERR(s);
	while (!(err = link_path_walk(s, nd)) &&
		(err = mountpoint_last(nd, path)) > 0) {
		s = trailing_symlink(nd);
		if (IS_ERR(s)) {
			err = PTR_ERR(s);
			break;
		}
	}
	terminate_walk(nd);
	return err;
}

static int
filename_mountpoint(int dfd, struct filename *name, struct path *path,
			unsigned int flags)
{
	struct nameidata nd;
	int error;
	if (IS_ERR(name))
		return PTR_ERR(name);
	set_nameidata(&nd, dfd, name);
	error = path_mountpoint(&nd, flags | LOOKUP_RCU, path);
	if (unlikely(error == -ECHILD))
		error = path_mountpoint(&nd, flags, path);
	if (unlikely(error == -ESTALE))
		error = path_mountpoint(&nd, flags | LOOKUP_REVAL, path);
	if (likely(!error))
		audit_inode(name, path->dentry, 0);
	restore_nameidata();
	putname(name);
	return error;
}

/**
 * user_path_mountpoint_at - lookup a path from userland in order to umount it
 * @dfd:	directory file descriptor
 * @name:	pathname from userland
 * @flags:	lookup flags
 * @path:	pointer to container to hold result
 *
 * A umount is a special case for path walking. We're not actually interested
 * in the inode in this situation, and ESTALE errors can be a problem. We
 * simply want track down the dentry and vfsmount attached at the mountpoint
 * and avoid revalidating the last component.
 *
 * Returns 0 and populates "path" on success.
 */
int
user_path_mountpoint_at(int dfd, const char __user *name, unsigned int flags,
			struct path *path)
{
	return filename_mountpoint(dfd, getname(name), path, flags);
}

int
kern_path_mountpoint(int dfd, const char *name, struct path *path,
			unsigned int flags)
{
	return filename_mountpoint(dfd, getname_kernel(name), path, flags);
}
EXPORT_SYMBOL(kern_path_mountpoint);

int __check_sticky(struct inode *dir, struct inode *inode)
{
	kuid_t fsuid = current_fsuid();

	if (uid_eq(inode->i_uid, fsuid))
		return 0;
	if (uid_eq(dir->i_uid, fsuid))
		return 0;
	return !capable_wrt_inode_uidgid(inode, CAP_FOWNER);
}
EXPORT_SYMBOL(__check_sticky);

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
static int may_delete(struct inode *dir, struct dentry *victim, bool isdir)
{
	struct inode *inode = d_backing_inode(victim);
	int error;

	if (d_is_negative(victim))
		return -ENOENT;
	BUG_ON(!inode);

	BUG_ON(victim->d_parent->d_inode != dir);
	audit_inode_child(dir, victim, AUDIT_TYPE_CHILD_DELETE);

	error = inode_permission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;
	if (IS_APPEND(dir))
		return -EPERM;

	if (check_sticky(dir, inode) || IS_APPEND(inode) ||
	    IS_IMMUTABLE(inode) || IS_SWAPFILE(inode))
		return -EPERM;
	if (isdir) {
		if (!d_is_dir(victim))
			return -ENOTDIR;
		if (IS_ROOT(victim))
			return -EBUSY;
	} else if (d_is_dir(victim))
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
	audit_inode_child(dir, child, AUDIT_TYPE_CHILD_CREATE);
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
	mutex_lock_nested(&p2->d_inode->i_mutex, I_MUTEX_PARENT2);
	return NULL;
}
EXPORT_SYMBOL(lock_rename);

void unlock_rename(struct dentry *p1, struct dentry *p2)
{
	mutex_unlock(&p1->d_inode->i_mutex);
	if (p1 != p2) {
		mutex_unlock(&p2->d_inode->i_mutex);
		mutex_unlock(&p1->d_inode->i_sb->s_vfs_rename_mutex);
	}
}
EXPORT_SYMBOL(unlock_rename);

int vfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
		bool want_excl)
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
	error = dir->i_op->create(dir, dentry, mode, want_excl);
	if (!error)
		fsnotify_create(dir, dentry);
	return error;
}
EXPORT_SYMBOL(vfs_create);

static int may_open(struct path *path, int acc_mode, int flag)
{
	struct dentry *dentry = path->dentry;
	struct inode *inode = dentry->d_inode;
	int error;

	/* O_PATH? */
	if (!acc_mode)
		return 0;

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
	if (flag & O_NOATIME && !inode_owner_or_capable(inode))
		return -EPERM;

	return 0;
}

static int handle_truncate(struct file *filp)
{
	struct path *path = &filp->f_path;
	struct inode *inode = path->dentry->d_inode;
	int error = get_write_access(inode);
	if (error)
		return error;
	/*
	 * Refuse to truncate files with mandatory locks held on them.
	 */
	error = locks_verify_locked(filp);
	if (!error)
		error = security_path_truncate(path);
	if (!error) {
		error = do_truncate(path->dentry, 0,
				    ATTR_MTIME|ATTR_CTIME|ATTR_OPEN,
				    filp);
	}
	put_write_access(inode);
	return error;
}

static inline int open_to_namei_flags(int flag)
{
	if ((flag & O_ACCMODE) == 3)
		flag--;
	return flag;
}

static int may_o_create(struct path *dir, struct dentry *dentry, umode_t mode)
{
	int error = security_path_mknod(dir, dentry, mode, 0);
	if (error)
		return error;

	error = inode_permission(dir->dentry->d_inode, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	return security_inode_create(dir->dentry->d_inode, dentry, mode);
}

/*
 * Attempt to atomically look up, create and open a file from a negative
 * dentry.
 *
 * Returns 0 if successful.  The file will have been created and attached to
 * @file by the filesystem calling finish_open().
 *
 * Returns 1 if the file was looked up only or didn't need creating.  The
 * caller will need to perform the open themselves.  @path will have been
 * updated to point to the new dentry.  This may be negative.
 *
 * Returns an error code otherwise.
 */
static int atomic_open(struct nameidata *nd, struct dentry *dentry,
			struct path *path, struct file *file,
			const struct open_flags *op,
			bool got_write, bool need_lookup,
			int *opened)
{
	struct inode *dir =  nd->path.dentry->d_inode;
	unsigned open_flag = open_to_namei_flags(op->open_flag);
	umode_t mode;
	int error;
	int acc_mode;
	int create_error = 0;
	struct dentry *const DENTRY_NOT_SET = (void *) -1UL;
	bool excl;

	BUG_ON(dentry->d_inode);

	/* Don't create child dentry for a dead directory. */
	if (unlikely(IS_DEADDIR(dir))) {
		error = -ENOENT;
		goto out;
	}

	mode = op->mode;
	if ((open_flag & O_CREAT) && !IS_POSIXACL(dir))
		mode &= ~current_umask();

	excl = (open_flag & (O_EXCL | O_CREAT)) == (O_EXCL | O_CREAT);
	if (excl)
		open_flag &= ~O_TRUNC;

	/*
	 * Checking write permission is tricky, bacuse we don't know if we are
	 * going to actually need it: O_CREAT opens should work as long as the
	 * file exists.  But checking existence breaks atomicity.  The trick is
	 * to check access and if not granted clear O_CREAT from the flags.
	 *
	 * Another problem is returing the "right" error value (e.g. for an
	 * O_EXCL open we want to return EEXIST not EROFS).
	 */
	if (((open_flag & (O_CREAT | O_TRUNC)) ||
	    (open_flag & O_ACCMODE) != O_RDONLY) && unlikely(!got_write)) {
		if (!(open_flag & O_CREAT)) {
			/*
			 * No O_CREATE -> atomicity not a requirement -> fall
			 * back to lookup + open
			 */
			goto no_open;
		} else if (open_flag & (O_EXCL | O_TRUNC)) {
			/* Fall back and fail with the right error */
			create_error = -EROFS;
			goto no_open;
		} else {
			/* No side effects, safe to clear O_CREAT */
			create_error = -EROFS;
			open_flag &= ~O_CREAT;
		}
	}

	if (open_flag & O_CREAT) {
		error = may_o_create(&nd->path, dentry, mode);
		if (error) {
			create_error = error;
			if (open_flag & O_EXCL)
				goto no_open;
			open_flag &= ~O_CREAT;
		}
	}

	if (nd->flags & LOOKUP_DIRECTORY)
		open_flag |= O_DIRECTORY;

	file->f_path.dentry = DENTRY_NOT_SET;
	file->f_path.mnt = nd->path.mnt;
	error = dir->i_op->atomic_open(dir, dentry, file, open_flag, mode,
				      opened);
	if (error < 0) {
		if (create_error && error == -ENOENT)
			error = create_error;
		goto out;
	}

	if (error) {	/* returned 1, that is */
		if (WARN_ON(file->f_path.dentry == DENTRY_NOT_SET)) {
			error = -EIO;
			goto out;
		}
		if (file->f_path.dentry) {
			dput(dentry);
			dentry = file->f_path.dentry;
		}
		if (*opened & FILE_CREATED)
			fsnotify_create(dir, dentry);
		if (!dentry->d_inode) {
			WARN_ON(*opened & FILE_CREATED);
			if (create_error) {
				error = create_error;
				goto out;
			}
		} else {
			if (excl && !(*opened & FILE_CREATED)) {
				error = -EEXIST;
				goto out;
			}
		}
		goto looked_up;
	}

	/*
	 * We didn't have the inode before the open, so check open permission
	 * here.
	 */
	acc_mode = op->acc_mode;
	if (*opened & FILE_CREATED) {
		WARN_ON(!(open_flag & O_CREAT));
		fsnotify_create(dir, dentry);
		acc_mode = MAY_OPEN;
	}
	error = may_open(&file->f_path, acc_mode, open_flag);
	if (error)
		fput(file);

out:
	dput(dentry);
	return error;

no_open:
	if (need_lookup) {
		dentry = lookup_real(dir, dentry, nd->flags);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);
	}
	if (create_error && !dentry->d_inode) {
		error = create_error;
		goto out;
	}
looked_up:
	path->dentry = dentry;
	path->mnt = nd->path.mnt;
	return 1;
}

/*
 * Look up and maybe create and open the last component.
 *
 * Must be called with i_mutex held on parent.
 *
 * Returns 0 if the file was successfully atomically created (if necessary) and
 * opened.  In this case the file will be returned attached to @file.
 *
 * Returns 1 if the file was not completely opened at this time, though lookups
 * and creations will have been performed and the dentry returned in @path will
 * be positive upon return if O_CREAT was specified.  If O_CREAT wasn't
 * specified then a negative dentry may be returned.
 *
 * An error code is returned otherwise.
 *
 * FILE_CREATE will be set in @*opened if the dentry was created and will be
 * cleared otherwise prior to returning.
 */
static int lookup_open(struct nameidata *nd, struct path *path,
			struct file *file,
			const struct open_flags *op,
			bool got_write, int *opened)
{
	struct dentry *dir = nd->path.dentry;
	struct inode *dir_inode = dir->d_inode;
	struct dentry *dentry;
	int error;
	bool need_lookup;

	*opened &= ~FILE_CREATED;
	dentry = lookup_dcache(&nd->last, dir, nd->flags, &need_lookup);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	/* Cached positive dentry: will open in f_op->open */
	if (!need_lookup && dentry->d_inode)
		goto out_no_open;

	if ((nd->flags & LOOKUP_OPEN) && dir_inode->i_op->atomic_open) {
		return atomic_open(nd, dentry, path, file, op, got_write,
				   need_lookup, opened);
	}

	if (need_lookup) {
		BUG_ON(dentry->d_inode);

		dentry = lookup_real(dir_inode, dentry, nd->flags);
		if (IS_ERR(dentry))
			return PTR_ERR(dentry);
	}

	/* Negative dentry, just create the file */
	if (!dentry->d_inode && (op->open_flag & O_CREAT)) {
		umode_t mode = op->mode;
		if (!IS_POSIXACL(dir->d_inode))
			mode &= ~current_umask();
		/*
		 * This write is needed to ensure that a
		 * rw->ro transition does not occur between
		 * the time when the file is created and when
		 * a permanent write count is taken through
		 * the 'struct file' in finish_open().
		 */
		if (!got_write) {
			error = -EROFS;
			goto out_dput;
		}
		*opened |= FILE_CREATED;
		error = security_path_mknod(&nd->path, dentry, mode, 0);
		if (error)
			goto out_dput;
		error = vfs_create(dir->d_inode, dentry, mode,
				   nd->flags & LOOKUP_EXCL);
		if (error)
			goto out_dput;
	}
out_no_open:
	path->dentry = dentry;
	path->mnt = nd->path.mnt;
	return 1;

out_dput:
	dput(dentry);
	return error;
}

/*
 * Handle the last step of open()
 */
static int do_last(struct nameidata *nd,
		   struct file *file, const struct open_flags *op,
		   int *opened)
{
	struct dentry *dir = nd->path.dentry;
	int open_flag = op->open_flag;
	bool will_truncate = (open_flag & O_TRUNC) != 0;
	bool got_write = false;
	int acc_mode = op->acc_mode;
	unsigned seq;
	struct inode *inode;
	struct path save_parent = { .dentry = NULL, .mnt = NULL };
	struct path path;
	bool retried = false;
	int error;

	nd->flags &= ~LOOKUP_PARENT;
	nd->flags |= op->intent;

	if (nd->last_type != LAST_NORM) {
		error = handle_dots(nd, nd->last_type);
		if (unlikely(error))
			return error;
		goto finish_open;
	}

	if (!(open_flag & O_CREAT)) {
		if (nd->last.name[nd->last.len])
			nd->flags |= LOOKUP_FOLLOW | LOOKUP_DIRECTORY;
		/* we _can_ be in RCU mode here */
		error = lookup_fast(nd, &path, &inode, &seq);
		if (likely(!error))
			goto finish_lookup;

		if (error < 0)
			return error;

		BUG_ON(nd->inode != dir->d_inode);
	} else {
		/* create side of things */
		/*
		 * This will *only* deal with leaving RCU mode - LOOKUP_JUMPED
		 * has been cleared when we got to the last component we are
		 * about to look up
		 */
		error = complete_walk(nd);
		if (error)
			return error;

		audit_inode(nd->name, dir, LOOKUP_PARENT);
		/* trailing slashes? */
		if (unlikely(nd->last.name[nd->last.len]))
			return -EISDIR;
	}

retry_lookup:
	if (op->open_flag & (O_CREAT | O_TRUNC | O_WRONLY | O_RDWR)) {
		error = mnt_want_write(nd->path.mnt);
		if (!error)
			got_write = true;
		/*
		 * do _not_ fail yet - we might not need that or fail with
		 * a different error; let lookup_open() decide; we'll be
		 * dropping this one anyway.
		 */
	}
	mutex_lock(&dir->d_inode->i_mutex);
	error = lookup_open(nd, &path, file, op, got_write, opened);
	mutex_unlock(&dir->d_inode->i_mutex);

	if (error <= 0) {
		if (error)
			goto out;

		if ((*opened & FILE_CREATED) ||
		    !S_ISREG(file_inode(file)->i_mode))
			will_truncate = false;

		audit_inode(nd->name, file->f_path.dentry, 0);
		goto opened;
	}

	if (*opened & FILE_CREATED) {
		/* Don't check for write permission, don't truncate */
		open_flag &= ~O_TRUNC;
		will_truncate = false;
		acc_mode = MAY_OPEN;
		path_to_nameidata(&path, nd);
		goto finish_open_created;
	}

	/*
	 * create/update audit record if it already exists.
	 */
	if (d_is_positive(path.dentry))
		audit_inode(nd->name, path.dentry, 0);

	/*
	 * If atomic_open() acquired write access it is dropped now due to
	 * possible mount and symlink following (this might be optimized away if
	 * necessary...)
	 */
	if (got_write) {
		mnt_drop_write(nd->path.mnt);
		got_write = false;
	}

	if (unlikely((open_flag & (O_EXCL | O_CREAT)) == (O_EXCL | O_CREAT))) {
		path_to_nameidata(&path, nd);
		return -EEXIST;
	}

	error = follow_managed(&path, nd);
	if (unlikely(error < 0))
		return error;

	BUG_ON(nd->flags & LOOKUP_RCU);
	seq = 0;	/* out of RCU mode, so the value doesn't matter */
	if (unlikely(d_is_negative(path.dentry))) {
		path_to_nameidata(&path, nd);
		return -ENOENT;
	}
	inode = d_backing_inode(path.dentry);
finish_lookup:
	if (nd->depth)
		put_link(nd);
	error = should_follow_link(nd, &path, nd->flags & LOOKUP_FOLLOW,
				   inode, seq);
	if (unlikely(error))
		return error;

	if ((nd->flags & LOOKUP_RCU) || nd->path.mnt != path.mnt) {
		path_to_nameidata(&path, nd);
	} else {
		save_parent.dentry = nd->path.dentry;
		save_parent.mnt = mntget(path.mnt);
		nd->path.dentry = path.dentry;

	}
	nd->inode = inode;
	nd->seq = seq;
	/* Why this, you ask?  _Now_ we might have grown LOOKUP_JUMPED... */
finish_open:
	error = complete_walk(nd);
	if (error) {
		path_put(&save_parent);
		return error;
	}
	audit_inode(nd->name, nd->path.dentry, 0);
	if (unlikely(d_is_symlink(nd->path.dentry)) && !(open_flag & O_PATH)) {
		error = -ELOOP;
		goto out;
	}
	error = -EISDIR;
	if ((open_flag & O_CREAT) && d_is_dir(nd->path.dentry))
		goto out;
	error = -ENOTDIR;
	if ((nd->flags & LOOKUP_DIRECTORY) && !d_can_lookup(nd->path.dentry))
		goto out;
	if (!d_is_reg(nd->path.dentry))
		will_truncate = false;

	if (will_truncate) {
		error = mnt_want_write(nd->path.mnt);
		if (error)
			goto out;
		got_write = true;
	}
finish_open_created:
	error = may_open(&nd->path, acc_mode, open_flag);
	if (error)
		goto out;

	BUG_ON(*opened & FILE_OPENED); /* once it's opened, it's opened */
	error = vfs_open(&nd->path, file, current_cred());
	if (!error) {
		*opened |= FILE_OPENED;
	} else {
		if (error == -EOPENSTALE)
			goto stale_open;
		goto out;
	}
opened:
	error = open_check_o_direct(file);
	if (error)
		goto exit_fput;
	error = ima_file_check(file, op->acc_mode, *opened);
	if (error)
		goto exit_fput;

	if (will_truncate) {
		error = handle_truncate(file);
		if (error)
			goto exit_fput;
	}
out:
	if (unlikely(error > 0)) {
		WARN_ON(1);
		error = -EINVAL;
	}
	if (got_write)
		mnt_drop_write(nd->path.mnt);
	path_put(&save_parent);
	return error;

exit_fput:
	fput(file);
	goto out;

stale_open:
	/* If no saved parent or already retried then can't retry */
	if (!save_parent.dentry || retried)
		goto out;

	BUG_ON(save_parent.dentry != dir);
	path_put(&nd->path);
	nd->path = save_parent;
	nd->inode = dir->d_inode;
	save_parent.mnt = NULL;
	save_parent.dentry = NULL;
	if (got_write) {
		mnt_drop_write(nd->path.mnt);
		got_write = false;
	}
	retried = true;
	goto retry_lookup;
}

static int do_tmpfile(struct nameidata *nd, unsigned flags,
		const struct open_flags *op,
		struct file *file, int *opened)
{
	static const struct qstr name = QSTR_INIT("/", 1);
	struct dentry *child;
	struct inode *dir;
	struct path path;
	int error = path_lookupat(nd, flags | LOOKUP_DIRECTORY, &path);
	if (unlikely(error))
		return error;
	error = mnt_want_write(path.mnt);
	if (unlikely(error))
		goto out;
	dir = path.dentry->d_inode;
	/* we want directory to be writable */
	error = inode_permission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		goto out2;
	if (!dir->i_op->tmpfile) {
		error = -EOPNOTSUPP;
		goto out2;
	}
	child = d_alloc(path.dentry, &name);
	if (unlikely(!child)) {
		error = -ENOMEM;
		goto out2;
	}
	dput(path.dentry);
	path.dentry = child;
	error = dir->i_op->tmpfile(dir, child, op->mode);
	if (error)
		goto out2;
	audit_inode(nd->name, child, 0);
	/* Don't check for other permissions, the inode was just created */
	error = may_open(&path, MAY_OPEN, op->open_flag);
	if (error)
		goto out2;
	file->f_path.mnt = path.mnt;
	error = finish_open(file, child, NULL, opened);
	if (error)
		goto out2;
	error = open_check_o_direct(file);
	if (error) {
		fput(file);
	} else if (!(op->open_flag & O_EXCL)) {
		struct inode *inode = file_inode(file);
		spin_lock(&inode->i_lock);
		inode->i_state |= I_LINKABLE;
		spin_unlock(&inode->i_lock);
	}
out2:
	mnt_drop_write(path.mnt);
out:
	path_put(&path);
	return error;
}

static struct file *path_openat(struct nameidata *nd,
			const struct open_flags *op, unsigned flags)
{
	const char *s;
	struct file *file;
	int opened = 0;
	int error;

	file = get_empty_filp();
	if (IS_ERR(file))
		return file;

	file->f_flags = op->open_flag;

	if (unlikely(file->f_flags & __O_TMPFILE)) {
		error = do_tmpfile(nd, flags, op, file, &opened);
		goto out2;
	}

	s = path_init(nd, flags);
	if (IS_ERR(s)) {
		put_filp(file);
		return ERR_CAST(s);
	}
	while (!(error = link_path_walk(s, nd)) &&
		(error = do_last(nd, file, op, &opened)) > 0) {
		nd->flags &= ~(LOOKUP_OPEN|LOOKUP_CREATE|LOOKUP_EXCL);
		s = trailing_symlink(nd);
		if (IS_ERR(s)) {
			error = PTR_ERR(s);
			break;
		}
	}
	terminate_walk(nd);
out2:
	if (!(opened & FILE_OPENED)) {
		BUG_ON(!error);
		put_filp(file);
	}
	if (unlikely(error)) {
		if (error == -EOPENSTALE) {
			if (flags & LOOKUP_RCU)
				error = -ECHILD;
			else
				error = -ESTALE;
		}
		file = ERR_PTR(error);
	}
	return file;
}

struct file *do_filp_open(int dfd, struct filename *pathname,
		const struct open_flags *op)
{
	struct nameidata nd;
	int flags = op->lookup_flags;
	struct file *filp;

	set_nameidata(&nd, dfd, pathname);
	filp = path_openat(&nd, op, flags | LOOKUP_RCU);
	if (unlikely(filp == ERR_PTR(-ECHILD)))
		filp = path_openat(&nd, op, flags);
	if (unlikely(filp == ERR_PTR(-ESTALE)))
		filp = path_openat(&nd, op, flags | LOOKUP_REVAL);
	restore_nameidata();
	return filp;
}

struct file *do_file_open_root(struct dentry *dentry, struct vfsmount *mnt,
		const char *name, const struct open_flags *op)
{
	struct nameidata nd;
	struct file *file;
	struct filename *filename;
	int flags = op->lookup_flags | LOOKUP_ROOT;

	nd.root.mnt = mnt;
	nd.root.dentry = dentry;

	if (d_is_symlink(dentry) && op->intent & LOOKUP_OPEN)
		return ERR_PTR(-ELOOP);

	filename = getname_kernel(name);
	if (IS_ERR(filename))
		return ERR_CAST(filename);

	set_nameidata(&nd, -1, filename);
	file = path_openat(&nd, op, flags | LOOKUP_RCU);
	if (unlikely(file == ERR_PTR(-ECHILD)))
		file = path_openat(&nd, op, flags);
	if (unlikely(file == ERR_PTR(-ESTALE)))
		file = path_openat(&nd, op, flags | LOOKUP_REVAL);
	restore_nameidata();
	putname(filename);
	return file;
}

static struct dentry *filename_create(int dfd, struct filename *name,
				struct path *path, unsigned int lookup_flags)
{
	struct dentry *dentry = ERR_PTR(-EEXIST);
	struct qstr last;
	int type;
	int err2;
	int error;
	bool is_dir = (lookup_flags & LOOKUP_DIRECTORY);

	/*
	 * Note that only LOOKUP_REVAL and LOOKUP_DIRECTORY matter here. Any
	 * other flags passed in are ignored!
	 */
	lookup_flags &= LOOKUP_REVAL;

	name = filename_parentat(dfd, name, lookup_flags, path, &last, &type);
	if (IS_ERR(name))
		return ERR_CAST(name);

	/*
	 * Yucky last component or no last component at all?
	 * (foo/., foo/.., /////)
	 */
	if (unlikely(type != LAST_NORM))
		goto out;

	/* don't fail immediately if it's r/o, at least try to report other errors */
	err2 = mnt_want_write(path->mnt);
	/*
	 * Do the final lookup.
	 */
	lookup_flags |= LOOKUP_CREATE | LOOKUP_EXCL;
	mutex_lock_nested(&path->dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = __lookup_hash(&last, path->dentry, lookup_flags);
	if (IS_ERR(dentry))
		goto unlock;

	error = -EEXIST;
	if (d_is_positive(dentry))
		goto fail;

	/*
	 * Special case - lookup gave negative, but... we had foo/bar/
	 * From the vfs_mknod() POV we just have a negative dentry -
	 * all is fine. Let's be bastards - you had / on the end, you've
	 * been asking for (non-existent) directory. -ENOENT for you.
	 */
	if (unlikely(!is_dir && last.name[last.len])) {
		error = -ENOENT;
		goto fail;
	}
	if (unlikely(err2)) {
		error = err2;
		goto fail;
	}
	putname(name);
	return dentry;
fail:
	dput(dentry);
	dentry = ERR_PTR(error);
unlock:
	mutex_unlock(&path->dentry->d_inode->i_mutex);
	if (!err2)
		mnt_drop_write(path->mnt);
out:
	path_put(path);
	putname(name);
	return dentry;
}

struct dentry *kern_path_create(int dfd, const char *pathname,
				struct path *path, unsigned int lookup_flags)
{
	return filename_create(dfd, getname_kernel(pathname),
				path, lookup_flags);
}
EXPORT_SYMBOL(kern_path_create);

void done_path_create(struct path *path, struct dentry *dentry)
{
	dput(dentry);
	mutex_unlock(&path->dentry->d_inode->i_mutex);
	mnt_drop_write(path->mnt);
	path_put(path);
}
EXPORT_SYMBOL(done_path_create);

inline struct dentry *user_path_create(int dfd, const char __user *pathname,
				struct path *path, unsigned int lookup_flags)
{
	return filename_create(dfd, getname(pathname), path, lookup_flags);
}
EXPORT_SYMBOL(user_path_create);

int vfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
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
EXPORT_SYMBOL(vfs_mknod);

static int may_mknod(umode_t mode)
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

SYSCALL_DEFINE4(mknodat, int, dfd, const char __user *, filename, umode_t, mode,
		unsigned, dev)
{
	struct dentry *dentry;
	struct path path;
	int error;
	unsigned int lookup_flags = 0;

	error = may_mknod(mode);
	if (error)
		return error;
retry:
	dentry = user_path_create(dfd, filename, &path, lookup_flags);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	if (!IS_POSIXACL(path.dentry->d_inode))
		mode &= ~current_umask();
	error = security_path_mknod(&path, dentry, mode, dev);
	if (error)
		goto out;
	switch (mode & S_IFMT) {
		case 0: case S_IFREG:
			error = vfs_create(path.dentry->d_inode,dentry,mode,true);
			break;
		case S_IFCHR: case S_IFBLK:
			error = vfs_mknod(path.dentry->d_inode,dentry,mode,
					new_decode_dev(dev));
			break;
		case S_IFIFO: case S_IFSOCK:
			error = vfs_mknod(path.dentry->d_inode,dentry,mode,0);
			break;
	}
out:
	done_path_create(&path, dentry);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}

SYSCALL_DEFINE3(mknod, const char __user *, filename, umode_t, mode, unsigned, dev)
{
	return sys_mknodat(AT_FDCWD, filename, mode, dev);
}

int vfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int error = may_create(dir, dentry);
	unsigned max_links = dir->i_sb->s_max_links;

	if (error)
		return error;

	if (!dir->i_op->mkdir)
		return -EPERM;

	mode &= (S_IRWXUGO|S_ISVTX);
	error = security_inode_mkdir(dir, dentry, mode);
	if (error)
		return error;

	if (max_links && dir->i_nlink >= max_links)
		return -EMLINK;

	error = dir->i_op->mkdir(dir, dentry, mode);
	if (!error)
		fsnotify_mkdir(dir, dentry);
	return error;
}
EXPORT_SYMBOL(vfs_mkdir);

SYSCALL_DEFINE3(mkdirat, int, dfd, const char __user *, pathname, umode_t, mode)
{
	struct dentry *dentry;
	struct path path;
	int error;
	unsigned int lookup_flags = LOOKUP_DIRECTORY;

retry:
	dentry = user_path_create(dfd, pathname, &path, lookup_flags);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	if (!IS_POSIXACL(path.dentry->d_inode))
		mode &= ~current_umask();
	error = security_path_mkdir(&path, dentry, mode);
	if (!error)
		error = vfs_mkdir(path.dentry->d_inode, dentry, mode);
	done_path_create(&path, dentry);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}

SYSCALL_DEFINE2(mkdir, const char __user *, pathname, umode_t, mode)
{
	return sys_mkdirat(AT_FDCWD, pathname, mode);
}

/*
 * The dentry_unhash() helper will try to drop the dentry early: we
 * should have a usage count of 1 if we're the only user of this
 * dentry, and if that is true (possibly after pruning the dcache),
 * then we drop the dentry now.
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
	shrink_dcache_parent(dentry);
	spin_lock(&dentry->d_lock);
	if (dentry->d_lockref.count == 1)
		__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL(dentry_unhash);

int vfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error = may_delete(dir, dentry, 1);

	if (error)
		return error;

	if (!dir->i_op->rmdir)
		return -EPERM;

	dget(dentry);
	mutex_lock(&dentry->d_inode->i_mutex);

	error = -EBUSY;
	if (is_local_mountpoint(dentry))
		goto out;

	error = security_inode_rmdir(dir, dentry);
	if (error)
		goto out;

	shrink_dcache_parent(dentry);
	error = dir->i_op->rmdir(dir, dentry);
	if (error)
		goto out;

	dentry->d_inode->i_flags |= S_DEAD;
	dont_mount(dentry);
	detach_mounts(dentry);

out:
	mutex_unlock(&dentry->d_inode->i_mutex);
	dput(dentry);
	if (!error)
		d_delete(dentry);
	return error;
}
EXPORT_SYMBOL(vfs_rmdir);

static long do_rmdir(int dfd, const char __user *pathname)
{
	int error = 0;
	struct filename *name;
	struct dentry *dentry;
	struct path path;
	struct qstr last;
	int type;
	unsigned int lookup_flags = 0;
retry:
	name = user_path_parent(dfd, pathname,
				&path, &last, &type, lookup_flags);
	if (IS_ERR(name))
		return PTR_ERR(name);

	switch (type) {
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

	error = mnt_want_write(path.mnt);
	if (error)
		goto exit1;

	mutex_lock_nested(&path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = __lookup_hash(&last, path.dentry, lookup_flags);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit2;
	if (!dentry->d_inode) {
		error = -ENOENT;
		goto exit3;
	}
	error = security_path_rmdir(&path, dentry);
	if (error)
		goto exit3;
	error = vfs_rmdir(path.dentry->d_inode, dentry);
exit3:
	dput(dentry);
exit2:
	mutex_unlock(&path.dentry->d_inode->i_mutex);
	mnt_drop_write(path.mnt);
exit1:
	path_put(&path);
	putname(name);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}

SYSCALL_DEFINE1(rmdir, const char __user *, pathname)
{
	return do_rmdir(AT_FDCWD, pathname);
}

/**
 * vfs_unlink - unlink a filesystem object
 * @dir:	parent directory
 * @dentry:	victim
 * @delegated_inode: returns victim inode, if the inode is delegated.
 *
 * The caller must hold dir->i_mutex.
 *
 * If vfs_unlink discovers a delegation, it will return -EWOULDBLOCK and
 * return a reference to the inode in delegated_inode.  The caller
 * should then break the delegation on that inode and retry.  Because
 * breaking a delegation may take a long time, the caller should drop
 * dir->i_mutex before doing so.
 *
 * Alternatively, a caller may pass NULL for delegated_inode.  This may
 * be appropriate for callers that expect the underlying filesystem not
 * to be NFS exported.
 */
int vfs_unlink(struct inode *dir, struct dentry *dentry, struct inode **delegated_inode)
{
	struct inode *target = dentry->d_inode;
	int error = may_delete(dir, dentry, 0);

	if (error)
		return error;

	if (!dir->i_op->unlink)
		return -EPERM;

	mutex_lock(&target->i_mutex);
	if (is_local_mountpoint(dentry))
		error = -EBUSY;
	else {
		error = security_inode_unlink(dir, dentry);
		if (!error) {
			error = try_break_deleg(target, delegated_inode);
			if (error)
				goto out;
			error = dir->i_op->unlink(dir, dentry);
			if (!error) {
				dont_mount(dentry);
				detach_mounts(dentry);
			}
		}
	}
out:
	mutex_unlock(&target->i_mutex);

	/* We don't d_delete() NFS sillyrenamed files--they still exist. */
	if (!error && !(dentry->d_flags & DCACHE_NFSFS_RENAMED)) {
		fsnotify_link_count(target);
		d_delete(dentry);
	}

	return error;
}
EXPORT_SYMBOL(vfs_unlink);

/*
 * Make sure that the actual truncation of the file will occur outside its
 * directory's i_mutex.  Truncate can take a long time if there is a lot of
 * writeout happening, and we don't want to prevent access to the directory
 * while waiting on the I/O.
 */
static long do_unlinkat(int dfd, const char __user *pathname)
{
	int error;
	struct filename *name;
	struct dentry *dentry;
	struct path path;
	struct qstr last;
	int type;
	struct inode *inode = NULL;
	struct inode *delegated_inode = NULL;
	unsigned int lookup_flags = 0;
retry:
	name = user_path_parent(dfd, pathname,
				&path, &last, &type, lookup_flags);
	if (IS_ERR(name))
		return PTR_ERR(name);

	error = -EISDIR;
	if (type != LAST_NORM)
		goto exit1;

	error = mnt_want_write(path.mnt);
	if (error)
		goto exit1;
retry_deleg:
	mutex_lock_nested(&path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = __lookup_hash(&last, path.dentry, lookup_flags);
	error = PTR_ERR(dentry);
	if (!IS_ERR(dentry)) {
		/* Why not before? Because we want correct error value */
		if (last.name[last.len])
			goto slashes;
		inode = dentry->d_inode;
		if (d_is_negative(dentry))
			goto slashes;
		ihold(inode);
		error = security_path_unlink(&path, dentry);
		if (error)
			goto exit2;
		error = vfs_unlink(path.dentry->d_inode, dentry, &delegated_inode);
exit2:
		dput(dentry);
	}
	mutex_unlock(&path.dentry->d_inode->i_mutex);
	if (inode)
		iput(inode);	/* truncate the inode here */
	inode = NULL;
	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}
	mnt_drop_write(path.mnt);
exit1:
	path_put(&path);
	putname(name);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		inode = NULL;
		goto retry;
	}
	return error;

slashes:
	if (d_is_negative(dentry))
		error = -ENOENT;
	else if (d_is_dir(dentry))
		error = -EISDIR;
	else
		error = -ENOTDIR;
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
EXPORT_SYMBOL(vfs_symlink);

SYSCALL_DEFINE3(symlinkat, const char __user *, oldname,
		int, newdfd, const char __user *, newname)
{
	int error;
	struct filename *from;
	struct dentry *dentry;
	struct path path;
	unsigned int lookup_flags = 0;

	from = getname(oldname);
	if (IS_ERR(from))
		return PTR_ERR(from);
retry:
	dentry = user_path_create(newdfd, newname, &path, lookup_flags);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out_putname;

	error = security_path_symlink(&path, dentry, from->name);
	if (!error)
		error = vfs_symlink(path.dentry->d_inode, dentry, from->name);
	done_path_create(&path, dentry);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
out_putname:
	putname(from);
	return error;
}

SYSCALL_DEFINE2(symlink, const char __user *, oldname, const char __user *, newname)
{
	return sys_symlinkat(oldname, AT_FDCWD, newname);
}

/**
 * vfs_link - create a new link
 * @old_dentry:	object to be linked
 * @dir:	new parent
 * @new_dentry:	where to create the new link
 * @delegated_inode: returns inode needing a delegation break
 *
 * The caller must hold dir->i_mutex
 *
 * If vfs_link discovers a delegation on the to-be-linked file in need
 * of breaking, it will return -EWOULDBLOCK and return a reference to the
 * inode in delegated_inode.  The caller should then break the delegation
 * and retry.  Because breaking a delegation may take a long time, the
 * caller should drop the i_mutex before doing so.
 *
 * Alternatively, a caller may pass NULL for delegated_inode.  This may
 * be appropriate for callers that expect the underlying filesystem not
 * to be NFS exported.
 */
int vfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry, struct inode **delegated_inode)
{
	struct inode *inode = old_dentry->d_inode;
	unsigned max_links = dir->i_sb->s_max_links;
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
	/* Make sure we don't allow creating hardlink to an unlinked file */
	if (inode->i_nlink == 0 && !(inode->i_state & I_LINKABLE))
		error =  -ENOENT;
	else if (max_links && inode->i_nlink >= max_links)
		error = -EMLINK;
	else {
		error = try_break_deleg(inode, delegated_inode);
		if (!error)
			error = dir->i_op->link(old_dentry, dir, new_dentry);
	}

	if (!error && (inode->i_state & I_LINKABLE)) {
		spin_lock(&inode->i_lock);
		inode->i_state &= ~I_LINKABLE;
		spin_unlock(&inode->i_lock);
	}
	mutex_unlock(&inode->i_mutex);
	if (!error)
		fsnotify_link(dir, inode, new_dentry);
	return error;
}
EXPORT_SYMBOL(vfs_link);

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
	struct path old_path, new_path;
	struct inode *delegated_inode = NULL;
	int how = 0;
	int error;

	if ((flags & ~(AT_SYMLINK_FOLLOW | AT_EMPTY_PATH)) != 0)
		return -EINVAL;
	/*
	 * To use null names we require CAP_DAC_READ_SEARCH
	 * This ensures that not everyone will be able to create
	 * handlink using the passed filedescriptor.
	 */
	if (flags & AT_EMPTY_PATH) {
		if (!capable(CAP_DAC_READ_SEARCH))
			return -ENOENT;
		how = LOOKUP_EMPTY;
	}

	if (flags & AT_SYMLINK_FOLLOW)
		how |= LOOKUP_FOLLOW;
retry:
	error = user_path_at(olddfd, oldname, how, &old_path);
	if (error)
		return error;

	new_dentry = user_path_create(newdfd, newname, &new_path,
					(how & LOOKUP_REVAL));
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto out;

	error = -EXDEV;
	if (old_path.mnt != new_path.mnt)
		goto out_dput;
	error = may_linkat(&old_path);
	if (unlikely(error))
		goto out_dput;
	error = security_path_link(old_path.dentry, &new_path, new_dentry);
	if (error)
		goto out_dput;
	error = vfs_link(old_path.dentry, new_path.dentry->d_inode, new_dentry, &delegated_inode);
out_dput:
	done_path_create(&new_path, new_dentry);
	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error) {
			path_put(&old_path);
			goto retry;
		}
	}
	if (retry_estale(error, how)) {
		path_put(&old_path);
		how |= LOOKUP_REVAL;
		goto retry;
	}
out:
	path_put(&old_path);

	return error;
}

SYSCALL_DEFINE2(link, const char __user *, oldname, const char __user *, newname)
{
	return sys_linkat(AT_FDCWD, oldname, AT_FDCWD, newname, 0);
}

/**
 * vfs_rename - rename a filesystem object
 * @old_dir:	parent of source
 * @old_dentry:	source
 * @new_dir:	parent of destination
 * @new_dentry:	destination
 * @delegated_inode: returns an inode needing a delegation break
 * @flags:	rename flags
 *
 * The caller must hold multiple mutexes--see lock_rename()).
 *
 * If vfs_rename discovers a delegation in need of breaking at either
 * the source or destination, it will return -EWOULDBLOCK and return a
 * reference to the inode in delegated_inode.  The caller should then
 * break the delegation and retry.  Because breaking a delegation may
 * take a long time, the caller should drop all locks before doing
 * so.
 *
 * Alternatively, a caller may pass NULL for delegated_inode.  This may
 * be appropriate for callers that expect the underlying filesystem not
 * to be NFS exported.
 *
 * The worst of all namespace operations - renaming directory. "Perverted"
 * doesn't even start to describe it. Somebody in UCB had a heck of a trip...
 * Problems:
 *	a) we can get into loop creation.
 *	b) race potential - two innocent renames can create a loop together.
 *	   That's where 4.4 screws up. Current fix: serialization on
 *	   sb->s_vfs_rename_mutex. We might be more accurate, but that's another
 *	   story.
 *	c) we have to lock _four_ objects - parents and victim (if it exists),
 *	   and source (if it is not a directory).
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
 *	d) conversion from fhandle to dentry may come in the wrong moment - when
 *	   we are removing the target. Solution: we will have to grab ->i_mutex
 *	   in the fhandle_to_dentry code. [FIXME - current nfsfh.c relies on
 *	   ->i_mutex on parents, which works but leads to some truly excessive
 *	   locking].
 */
int vfs_rename(struct inode *old_dir, struct dentry *old_dentry,
	       struct inode *new_dir, struct dentry *new_dentry,
	       struct inode **delegated_inode, unsigned int flags)
{
	int error;
	bool is_dir = d_is_dir(old_dentry);
	struct inode *source = old_dentry->d_inode;
	struct inode *target = new_dentry->d_inode;
	bool new_is_dir = false;
	unsigned max_links = new_dir->i_sb->s_max_links;
	struct name_snapshot old_name;

	/*
	 * Check source == target.
	 * On overlayfs need to look at underlying inodes.
	 */
	if (vfs_select_inode(old_dentry, 0) == vfs_select_inode(new_dentry, 0))
		return 0;

	error = may_delete(old_dir, old_dentry, is_dir);
	if (error)
		return error;

	if (!target) {
		error = may_create(new_dir, new_dentry);
	} else {
		new_is_dir = d_is_dir(new_dentry);

		if (!(flags & RENAME_EXCHANGE))
			error = may_delete(new_dir, new_dentry, is_dir);
		else
			error = may_delete(new_dir, new_dentry, new_is_dir);
	}
	if (error)
		return error;

	if (!old_dir->i_op->rename && !old_dir->i_op->rename2)
		return -EPERM;

	if (flags && !old_dir->i_op->rename2)
		return -EINVAL;

	/*
	 * If we are going to change the parent - check write permissions,
	 * we'll need to flip '..'.
	 */
	if (new_dir != old_dir) {
		if (is_dir) {
			error = inode_permission(source, MAY_WRITE);
			if (error)
				return error;
		}
		if ((flags & RENAME_EXCHANGE) && new_is_dir) {
			error = inode_permission(target, MAY_WRITE);
			if (error)
				return error;
		}
	}

	error = security_inode_rename(old_dir, old_dentry, new_dir, new_dentry,
				      flags);
	if (error)
		return error;

	take_dentry_name_snapshot(&old_name, old_dentry);
	dget(new_dentry);
	if (!is_dir || (flags & RENAME_EXCHANGE))
		lock_two_nondirectories(source, target);
	else if (target)
		mutex_lock(&target->i_mutex);

	error = -EBUSY;
	if (is_local_mountpoint(old_dentry) || is_local_mountpoint(new_dentry))
		goto out;

	if (max_links && new_dir != old_dir) {
		error = -EMLINK;
		if (is_dir && !new_is_dir && new_dir->i_nlink >= max_links)
			goto out;
		if ((flags & RENAME_EXCHANGE) && !is_dir && new_is_dir &&
		    old_dir->i_nlink >= max_links)
			goto out;
	}
	if (is_dir && !(flags & RENAME_EXCHANGE) && target)
		shrink_dcache_parent(new_dentry);
	if (!is_dir) {
		error = try_break_deleg(source, delegated_inode);
		if (error)
			goto out;
	}
	if (target && !new_is_dir) {
		error = try_break_deleg(target, delegated_inode);
		if (error)
			goto out;
	}
	if (!old_dir->i_op->rename2) {
		error = old_dir->i_op->rename(old_dir, old_dentry,
					      new_dir, new_dentry);
	} else {
		WARN_ON(old_dir->i_op->rename != NULL);
		error = old_dir->i_op->rename2(old_dir, old_dentry,
					       new_dir, new_dentry, flags);
	}
	if (error)
		goto out;

	if (!(flags & RENAME_EXCHANGE) && target) {
		if (is_dir)
			target->i_flags |= S_DEAD;
		dont_mount(new_dentry);
		detach_mounts(new_dentry);
	}
	if (!(old_dir->i_sb->s_type->fs_flags & FS_RENAME_DOES_D_MOVE)) {
		if (!(flags & RENAME_EXCHANGE))
			d_move(old_dentry, new_dentry);
		else
			d_exchange(old_dentry, new_dentry);
	}
out:
	if (!is_dir || (flags & RENAME_EXCHANGE))
		unlock_two_nondirectories(source, target);
	else if (target)
		mutex_unlock(&target->i_mutex);
	dput(new_dentry);
	if (!error) {
		fsnotify_move(old_dir, new_dir, old_name.name, is_dir,
			      !(flags & RENAME_EXCHANGE) ? target : NULL, old_dentry);
		if (flags & RENAME_EXCHANGE) {
			fsnotify_move(new_dir, old_dir, old_dentry->d_name.name,
				      new_is_dir, NULL, new_dentry);
		}
	}
	release_dentry_name_snapshot(&old_name);

	return error;
}
EXPORT_SYMBOL(vfs_rename);

SYSCALL_DEFINE5(renameat2, int, olddfd, const char __user *, oldname,
		int, newdfd, const char __user *, newname, unsigned int, flags)
{
	struct dentry *old_dentry, *new_dentry;
	struct dentry *trap;
	struct path old_path, new_path;
	struct qstr old_last, new_last;
	int old_type, new_type;
	struct inode *delegated_inode = NULL;
	struct filename *from;
	struct filename *to;
	unsigned int lookup_flags = 0, target_flags = LOOKUP_RENAME_TARGET;
	bool should_retry = false;
	int error;

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return -EINVAL;

	if ((flags & (RENAME_NOREPLACE | RENAME_WHITEOUT)) &&
	    (flags & RENAME_EXCHANGE))
		return -EINVAL;

	if ((flags & RENAME_WHITEOUT) && !capable(CAP_MKNOD))
		return -EPERM;

	if (flags & RENAME_EXCHANGE)
		target_flags = 0;

retry:
	from = user_path_parent(olddfd, oldname,
				&old_path, &old_last, &old_type, lookup_flags);
	if (IS_ERR(from)) {
		error = PTR_ERR(from);
		goto exit;
	}

	to = user_path_parent(newdfd, newname,
				&new_path, &new_last, &new_type, lookup_flags);
	if (IS_ERR(to)) {
		error = PTR_ERR(to);
		goto exit1;
	}

	error = -EXDEV;
	if (old_path.mnt != new_path.mnt)
		goto exit2;

	error = -EBUSY;
	if (old_type != LAST_NORM)
		goto exit2;

	if (flags & RENAME_NOREPLACE)
		error = -EEXIST;
	if (new_type != LAST_NORM)
		goto exit2;

	error = mnt_want_write(old_path.mnt);
	if (error)
		goto exit2;

retry_deleg:
	trap = lock_rename(new_path.dentry, old_path.dentry);

	old_dentry = __lookup_hash(&old_last, old_path.dentry, lookup_flags);
	error = PTR_ERR(old_dentry);
	if (IS_ERR(old_dentry))
		goto exit3;
	/* source must exist */
	error = -ENOENT;
	if (d_is_negative(old_dentry))
		goto exit4;
	new_dentry = __lookup_hash(&new_last, new_path.dentry, lookup_flags | target_flags);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto exit4;
	error = -EEXIST;
	if ((flags & RENAME_NOREPLACE) && d_is_positive(new_dentry))
		goto exit5;
	if (flags & RENAME_EXCHANGE) {
		error = -ENOENT;
		if (d_is_negative(new_dentry))
			goto exit5;

		if (!d_is_dir(new_dentry)) {
			error = -ENOTDIR;
			if (new_last.name[new_last.len])
				goto exit5;
		}
	}
	/* unless the source is a directory trailing slashes give -ENOTDIR */
	if (!d_is_dir(old_dentry)) {
		error = -ENOTDIR;
		if (old_last.name[old_last.len])
			goto exit5;
		if (!(flags & RENAME_EXCHANGE) && new_last.name[new_last.len])
			goto exit5;
	}
	/* source should not be ancestor of target */
	error = -EINVAL;
	if (old_dentry == trap)
		goto exit5;
	/* target should not be an ancestor of source */
	if (!(flags & RENAME_EXCHANGE))
		error = -ENOTEMPTY;
	if (new_dentry == trap)
		goto exit5;

	error = security_path_rename(&old_path, old_dentry,
				     &new_path, new_dentry, flags);
	if (error)
		goto exit5;
	error = vfs_rename(old_path.dentry->d_inode, old_dentry,
			   new_path.dentry->d_inode, new_dentry,
			   &delegated_inode, flags);
exit5:
	dput(new_dentry);
exit4:
	dput(old_dentry);
exit3:
	unlock_rename(new_path.dentry, old_path.dentry);
	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}
	mnt_drop_write(old_path.mnt);
exit2:
	if (retry_estale(error, lookup_flags))
		should_retry = true;
	path_put(&new_path);
	putname(to);
exit1:
	path_put(&old_path);
	putname(from);
	if (should_retry) {
		should_retry = false;
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
exit:
	return error;
}

SYSCALL_DEFINE4(renameat, int, olddfd, const char __user *, oldname,
		int, newdfd, const char __user *, newname)
{
	return sys_renameat2(olddfd, oldname, newdfd, newname, 0);
}

SYSCALL_DEFINE2(rename, const char __user *, oldname, const char __user *, newname)
{
	return sys_renameat2(AT_FDCWD, oldname, AT_FDCWD, newname, 0);
}

int vfs_whiteout(struct inode *dir, struct dentry *dentry)
{
	int error = may_create(dir, dentry);
	if (error)
		return error;

	if (!dir->i_op->mknod)
		return -EPERM;

	return dir->i_op->mknod(dir, dentry,
				S_IFCHR | WHITEOUT_MODE, WHITEOUT_DEV);
}
EXPORT_SYMBOL(vfs_whiteout);

int readlink_copy(char __user *buffer, int buflen, const char *link)
{
	int len = PTR_ERR(link);
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
EXPORT_SYMBOL(readlink_copy);

/*
 * A helper for ->readlink().  This should be used *ONLY* for symlinks that
 * have ->follow_link() touching nd only in nd_set_link().  Using (or not
 * using) it for any given inode is up to filesystem.
 */
int generic_readlink(struct dentry *dentry, char __user *buffer, int buflen)
{
	void *cookie;
	struct inode *inode = d_inode(dentry);
	const char *link = inode->i_link;
	int res;

	if (!link) {
		link = inode->i_op->follow_link(dentry, &cookie);
		if (IS_ERR(link))
			return PTR_ERR(link);
	}
	res = readlink_copy(buffer, buflen, link);
	if (inode->i_op->put_link)
		inode->i_op->put_link(inode, cookie);
	return res;
}
EXPORT_SYMBOL(generic_readlink);

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
	int res = readlink_copy(buffer, buflen, page_getlink(dentry, &page));
	if (page) {
		kunmap(page);
		page_cache_release(page);
	}
	return res;
}
EXPORT_SYMBOL(page_readlink);

const char *page_follow_link_light(struct dentry *dentry, void **cookie)
{
	struct page *page = NULL;
	char *res = page_getlink(dentry, &page);
	if (!IS_ERR(res))
		*cookie = page;
	return res;
}
EXPORT_SYMBOL(page_follow_link_light);

void page_put_link(struct inode *unused, void *cookie)
{
	struct page *page = cookie;
	kunmap(page);
	page_cache_release(page);
}
EXPORT_SYMBOL(page_put_link);

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

	kaddr = kmap_atomic(page);
	memcpy(kaddr, symname, len-1);
	kunmap_atomic(kaddr);

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
EXPORT_SYMBOL(__page_symlink);

int page_symlink(struct inode *inode, const char *symname, int len)
{
	return __page_symlink(inode, symname, len,
			!mapping_gfp_constraint(inode->i_mapping, __GFP_FS));
}
EXPORT_SYMBOL(page_symlink);

const struct inode_operations page_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
};
EXPORT_SYMBOL(page_symlink_inode_operations);
