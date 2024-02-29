// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2002,2003 by Andreas Gruenbacher <a.gruenbacher@computer.org>
 *
 * Fixes from William Schumacher incorporated on 15 March 2001.
 *    (Reported by Charles Bertsch, <CBertsch@microtest.com>).
 */

/*
 *  This file contains generic functions for manipulating
 *  POSIX 1003.1e draft standard 17 ACLs.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/posix_acl.h>
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>
#include <linux/export.h>
#include <linux/user_namespace.h>
#include <linux/namei.h>
#include <linux/mnt_idmapping.h>
#include <linux/iversion.h>
#include <linux/security.h>
#include <linux/evm.h>
#include <linux/fsnotify.h>
#include <linux/filelock.h>

#include "internal.h"

static struct posix_acl **acl_by_type(struct inode *inode, int type)
{
	switch (type) {
	case ACL_TYPE_ACCESS:
		return &inode->i_acl;
	case ACL_TYPE_DEFAULT:
		return &inode->i_default_acl;
	default:
		BUG();
	}
}

struct posix_acl *get_cached_acl(struct inode *inode, int type)
{
	struct posix_acl **p = acl_by_type(inode, type);
	struct posix_acl *acl;

	for (;;) {
		rcu_read_lock();
		acl = rcu_dereference(*p);
		if (!acl || is_uncached_acl(acl) ||
		    refcount_inc_not_zero(&acl->a_refcount))
			break;
		rcu_read_unlock();
		cpu_relax();
	}
	rcu_read_unlock();
	return acl;
}
EXPORT_SYMBOL(get_cached_acl);

struct posix_acl *get_cached_acl_rcu(struct inode *inode, int type)
{
	struct posix_acl *acl = rcu_dereference(*acl_by_type(inode, type));

	if (acl == ACL_DONT_CACHE) {
		struct posix_acl *ret;

		ret = inode->i_op->get_inode_acl(inode, type, LOOKUP_RCU);
		if (!IS_ERR(ret))
			acl = ret;
	}

	return acl;
}
EXPORT_SYMBOL(get_cached_acl_rcu);

void set_cached_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	struct posix_acl **p = acl_by_type(inode, type);
	struct posix_acl *old;

	old = xchg(p, posix_acl_dup(acl));
	if (!is_uncached_acl(old))
		posix_acl_release(old);
}
EXPORT_SYMBOL(set_cached_acl);

static void __forget_cached_acl(struct posix_acl **p)
{
	struct posix_acl *old;

	old = xchg(p, ACL_NOT_CACHED);
	if (!is_uncached_acl(old))
		posix_acl_release(old);
}

void forget_cached_acl(struct inode *inode, int type)
{
	__forget_cached_acl(acl_by_type(inode, type));
}
EXPORT_SYMBOL(forget_cached_acl);

void forget_all_cached_acls(struct inode *inode)
{
	__forget_cached_acl(&inode->i_acl);
	__forget_cached_acl(&inode->i_default_acl);
}
EXPORT_SYMBOL(forget_all_cached_acls);

static struct posix_acl *__get_acl(struct mnt_idmap *idmap,
				   struct dentry *dentry, struct inode *inode,
				   int type)
{
	struct posix_acl *sentinel;
	struct posix_acl **p;
	struct posix_acl *acl;

	/*
	 * The sentinel is used to detect when another operation like
	 * set_cached_acl() or forget_cached_acl() races with get_inode_acl().
	 * It is guaranteed that is_uncached_acl(sentinel) is true.
	 */

	acl = get_cached_acl(inode, type);
	if (!is_uncached_acl(acl))
		return acl;

	if (!IS_POSIXACL(inode))
		return NULL;

	sentinel = uncached_acl_sentinel(current);
	p = acl_by_type(inode, type);

	/*
	 * If the ACL isn't being read yet, set our sentinel.  Otherwise, the
	 * current value of the ACL will not be ACL_NOT_CACHED and so our own
	 * sentinel will not be set; another task will update the cache.  We
	 * could wait for that other task to complete its job, but it's easier
	 * to just call ->get_inode_acl to fetch the ACL ourself.  (This is
	 * going to be an unlikely race.)
	 */
	cmpxchg(p, ACL_NOT_CACHED, sentinel);

	/*
	 * Normally, the ACL returned by ->get{_inode}_acl will be cached.
	 * A filesystem can prevent that by calling
	 * forget_cached_acl(inode, type) in ->get{_inode}_acl.
	 *
	 * If the filesystem doesn't have a get{_inode}_ acl() function at all,
	 * we'll just create the negative cache entry.
	 */
	if (dentry && inode->i_op->get_acl) {
		acl = inode->i_op->get_acl(idmap, dentry, type);
	} else if (inode->i_op->get_inode_acl) {
		acl = inode->i_op->get_inode_acl(inode, type, false);
	} else {
		set_cached_acl(inode, type, NULL);
		return NULL;
	}
	if (IS_ERR(acl)) {
		/*
		 * Remove our sentinel so that we don't block future attempts
		 * to cache the ACL.
		 */
		cmpxchg(p, sentinel, ACL_NOT_CACHED);
		return acl;
	}

	/*
	 * Cache the result, but only if our sentinel is still in place.
	 */
	posix_acl_dup(acl);
	if (unlikely(!try_cmpxchg(p, &sentinel, acl)))
		posix_acl_release(acl);
	return acl;
}

struct posix_acl *get_inode_acl(struct inode *inode, int type)
{
	return __get_acl(&nop_mnt_idmap, NULL, inode, type);
}
EXPORT_SYMBOL(get_inode_acl);

/*
 * Init a fresh posix_acl
 */
void
posix_acl_init(struct posix_acl *acl, int count)
{
	refcount_set(&acl->a_refcount, 1);
	acl->a_count = count;
}
EXPORT_SYMBOL(posix_acl_init);

/*
 * Allocate a new ACL with the specified number of entries.
 */
struct posix_acl *
posix_acl_alloc(int count, gfp_t flags)
{
	const size_t size = sizeof(struct posix_acl) +
	                    count * sizeof(struct posix_acl_entry);
	struct posix_acl *acl = kmalloc(size, flags);
	if (acl)
		posix_acl_init(acl, count);
	return acl;
}
EXPORT_SYMBOL(posix_acl_alloc);

/*
 * Clone an ACL.
 */
struct posix_acl *
posix_acl_clone(const struct posix_acl *acl, gfp_t flags)
{
	struct posix_acl *clone = NULL;

	if (acl) {
		int size = sizeof(struct posix_acl) + acl->a_count *
		           sizeof(struct posix_acl_entry);
		clone = kmemdup(acl, size, flags);
		if (clone)
			refcount_set(&clone->a_refcount, 1);
	}
	return clone;
}
EXPORT_SYMBOL_GPL(posix_acl_clone);

/*
 * Check if an acl is valid. Returns 0 if it is, or -E... otherwise.
 */
int
posix_acl_valid(struct user_namespace *user_ns, const struct posix_acl *acl)
{
	const struct posix_acl_entry *pa, *pe;
	int state = ACL_USER_OBJ;
	int needs_mask = 0;

	FOREACH_ACL_ENTRY(pa, acl, pe) {
		if (pa->e_perm & ~(ACL_READ|ACL_WRITE|ACL_EXECUTE))
			return -EINVAL;
		switch (pa->e_tag) {
			case ACL_USER_OBJ:
				if (state == ACL_USER_OBJ) {
					state = ACL_USER;
					break;
				}
				return -EINVAL;

			case ACL_USER:
				if (state != ACL_USER)
					return -EINVAL;
				if (!kuid_has_mapping(user_ns, pa->e_uid))
					return -EINVAL;
				needs_mask = 1;
				break;

			case ACL_GROUP_OBJ:
				if (state == ACL_USER) {
					state = ACL_GROUP;
					break;
				}
				return -EINVAL;

			case ACL_GROUP:
				if (state != ACL_GROUP)
					return -EINVAL;
				if (!kgid_has_mapping(user_ns, pa->e_gid))
					return -EINVAL;
				needs_mask = 1;
				break;

			case ACL_MASK:
				if (state != ACL_GROUP)
					return -EINVAL;
				state = ACL_OTHER;
				break;

			case ACL_OTHER:
				if (state == ACL_OTHER ||
				    (state == ACL_GROUP && !needs_mask)) {
					state = 0;
					break;
				}
				return -EINVAL;

			default:
				return -EINVAL;
		}
	}
	if (state == 0)
		return 0;
	return -EINVAL;
}
EXPORT_SYMBOL(posix_acl_valid);

/*
 * Returns 0 if the acl can be exactly represented in the traditional
 * file mode permission bits, or else 1. Returns -E... on error.
 */
int
posix_acl_equiv_mode(const struct posix_acl *acl, umode_t *mode_p)
{
	const struct posix_acl_entry *pa, *pe;
	umode_t mode = 0;
	int not_equiv = 0;

	/*
	 * A null ACL can always be presented as mode bits.
	 */
	if (!acl)
		return 0;

	FOREACH_ACL_ENTRY(pa, acl, pe) {
		switch (pa->e_tag) {
			case ACL_USER_OBJ:
				mode |= (pa->e_perm & S_IRWXO) << 6;
				break;
			case ACL_GROUP_OBJ:
				mode |= (pa->e_perm & S_IRWXO) << 3;
				break;
			case ACL_OTHER:
				mode |= pa->e_perm & S_IRWXO;
				break;
			case ACL_MASK:
				mode = (mode & ~S_IRWXG) |
				       ((pa->e_perm & S_IRWXO) << 3);
				not_equiv = 1;
				break;
			case ACL_USER:
			case ACL_GROUP:
				not_equiv = 1;
				break;
			default:
				return -EINVAL;
		}
	}
        if (mode_p)
                *mode_p = (*mode_p & ~S_IRWXUGO) | mode;
        return not_equiv;
}
EXPORT_SYMBOL(posix_acl_equiv_mode);

/*
 * Create an ACL representing the file mode permission bits of an inode.
 */
struct posix_acl *
posix_acl_from_mode(umode_t mode, gfp_t flags)
{
	struct posix_acl *acl = posix_acl_alloc(3, flags);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	acl->a_entries[0].e_tag  = ACL_USER_OBJ;
	acl->a_entries[0].e_perm = (mode & S_IRWXU) >> 6;

	acl->a_entries[1].e_tag  = ACL_GROUP_OBJ;
	acl->a_entries[1].e_perm = (mode & S_IRWXG) >> 3;

	acl->a_entries[2].e_tag  = ACL_OTHER;
	acl->a_entries[2].e_perm = (mode & S_IRWXO);
	return acl;
}
EXPORT_SYMBOL(posix_acl_from_mode);

/*
 * Return 0 if current is granted want access to the inode
 * by the acl. Returns -E... otherwise.
 */
int
posix_acl_permission(struct mnt_idmap *idmap, struct inode *inode,
		     const struct posix_acl *acl, int want)
{
	const struct posix_acl_entry *pa, *pe, *mask_obj;
	struct user_namespace *fs_userns = i_user_ns(inode);
	int found = 0;
	vfsuid_t vfsuid;
	vfsgid_t vfsgid;

	want &= MAY_READ | MAY_WRITE | MAY_EXEC;

	FOREACH_ACL_ENTRY(pa, acl, pe) {
                switch(pa->e_tag) {
                        case ACL_USER_OBJ:
				/* (May have been checked already) */
				vfsuid = i_uid_into_vfsuid(idmap, inode);
				if (vfsuid_eq_kuid(vfsuid, current_fsuid()))
                                        goto check_perm;
                                break;
                        case ACL_USER:
				vfsuid = make_vfsuid(idmap, fs_userns,
						     pa->e_uid);
				if (vfsuid_eq_kuid(vfsuid, current_fsuid()))
                                        goto mask;
				break;
                        case ACL_GROUP_OBJ:
				vfsgid = i_gid_into_vfsgid(idmap, inode);
				if (vfsgid_in_group_p(vfsgid)) {
					found = 1;
					if ((pa->e_perm & want) == want)
						goto mask;
                                }
				break;
                        case ACL_GROUP:
				vfsgid = make_vfsgid(idmap, fs_userns,
						     pa->e_gid);
				if (vfsgid_in_group_p(vfsgid)) {
					found = 1;
					if ((pa->e_perm & want) == want)
						goto mask;
                                }
                                break;
                        case ACL_MASK:
                                break;
                        case ACL_OTHER:
				if (found)
					return -EACCES;
				else
					goto check_perm;
			default:
				return -EIO;
                }
        }
	return -EIO;

mask:
	for (mask_obj = pa+1; mask_obj != pe; mask_obj++) {
		if (mask_obj->e_tag == ACL_MASK) {
			if ((pa->e_perm & mask_obj->e_perm & want) == want)
				return 0;
			return -EACCES;
		}
	}

check_perm:
	if ((pa->e_perm & want) == want)
		return 0;
	return -EACCES;
}

/*
 * Modify acl when creating a new inode. The caller must ensure the acl is
 * only referenced once.
 *
 * mode_p initially must contain the mode parameter to the open() / creat()
 * system calls. All permissions that are not granted by the acl are removed.
 * The permissions in the acl are changed to reflect the mode_p parameter.
 */
static int posix_acl_create_masq(struct posix_acl *acl, umode_t *mode_p)
{
	struct posix_acl_entry *pa, *pe;
	struct posix_acl_entry *group_obj = NULL, *mask_obj = NULL;
	umode_t mode = *mode_p;
	int not_equiv = 0;

	/* assert(atomic_read(acl->a_refcount) == 1); */

	FOREACH_ACL_ENTRY(pa, acl, pe) {
                switch(pa->e_tag) {
                        case ACL_USER_OBJ:
				pa->e_perm &= (mode >> 6) | ~S_IRWXO;
				mode &= (pa->e_perm << 6) | ~S_IRWXU;
				break;

			case ACL_USER:
			case ACL_GROUP:
				not_equiv = 1;
				break;

                        case ACL_GROUP_OBJ:
				group_obj = pa;
                                break;

                        case ACL_OTHER:
				pa->e_perm &= mode | ~S_IRWXO;
				mode &= pa->e_perm | ~S_IRWXO;
                                break;

                        case ACL_MASK:
				mask_obj = pa;
				not_equiv = 1;
                                break;

			default:
				return -EIO;
                }
        }

	if (mask_obj) {
		mask_obj->e_perm &= (mode >> 3) | ~S_IRWXO;
		mode &= (mask_obj->e_perm << 3) | ~S_IRWXG;
	} else {
		if (!group_obj)
			return -EIO;
		group_obj->e_perm &= (mode >> 3) | ~S_IRWXO;
		mode &= (group_obj->e_perm << 3) | ~S_IRWXG;
	}

	*mode_p = (*mode_p & ~S_IRWXUGO) | mode;
        return not_equiv;
}

/*
 * Modify the ACL for the chmod syscall.
 */
static int __posix_acl_chmod_masq(struct posix_acl *acl, umode_t mode)
{
	struct posix_acl_entry *group_obj = NULL, *mask_obj = NULL;
	struct posix_acl_entry *pa, *pe;

	/* assert(atomic_read(acl->a_refcount) == 1); */

	FOREACH_ACL_ENTRY(pa, acl, pe) {
		switch(pa->e_tag) {
			case ACL_USER_OBJ:
				pa->e_perm = (mode & S_IRWXU) >> 6;
				break;

			case ACL_USER:
			case ACL_GROUP:
				break;

			case ACL_GROUP_OBJ:
				group_obj = pa;
				break;

			case ACL_MASK:
				mask_obj = pa;
				break;

			case ACL_OTHER:
				pa->e_perm = (mode & S_IRWXO);
				break;

			default:
				return -EIO;
		}
	}

	if (mask_obj) {
		mask_obj->e_perm = (mode & S_IRWXG) >> 3;
	} else {
		if (!group_obj)
			return -EIO;
		group_obj->e_perm = (mode & S_IRWXG) >> 3;
	}

	return 0;
}

int
__posix_acl_create(struct posix_acl **acl, gfp_t gfp, umode_t *mode_p)
{
	struct posix_acl *clone = posix_acl_clone(*acl, gfp);
	int err = -ENOMEM;
	if (clone) {
		err = posix_acl_create_masq(clone, mode_p);
		if (err < 0) {
			posix_acl_release(clone);
			clone = NULL;
		}
	}
	posix_acl_release(*acl);
	*acl = clone;
	return err;
}
EXPORT_SYMBOL(__posix_acl_create);

int
__posix_acl_chmod(struct posix_acl **acl, gfp_t gfp, umode_t mode)
{
	struct posix_acl *clone = posix_acl_clone(*acl, gfp);
	int err = -ENOMEM;
	if (clone) {
		err = __posix_acl_chmod_masq(clone, mode);
		if (err) {
			posix_acl_release(clone);
			clone = NULL;
		}
	}
	posix_acl_release(*acl);
	*acl = clone;
	return err;
}
EXPORT_SYMBOL(__posix_acl_chmod);

/**
 * posix_acl_chmod - chmod a posix acl
 *
 * @idmap:	idmap of the mount @inode was found from
 * @dentry:	dentry to check permissions on
 * @mode:	the new mode of @inode
 *
 * If the dentry has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then
 * take care to map the inode according to @idmap before checking
 * permissions. On non-idmapped mounts or if permission checking is to be
 * performed on the raw inode simply pass @nop_mnt_idmap.
 */
int
 posix_acl_chmod(struct mnt_idmap *idmap, struct dentry *dentry,
		    umode_t mode)
{
	struct inode *inode = d_inode(dentry);
	struct posix_acl *acl;
	int ret = 0;

	if (!IS_POSIXACL(inode))
		return 0;
	if (!inode->i_op->set_acl)
		return -EOPNOTSUPP;

	acl = get_inode_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR_OR_NULL(acl)) {
		if (acl == ERR_PTR(-EOPNOTSUPP))
			return 0;
		return PTR_ERR(acl);
	}

	ret = __posix_acl_chmod(&acl, GFP_KERNEL, mode);
	if (ret)
		return ret;
	ret = inode->i_op->set_acl(idmap, dentry, acl, ACL_TYPE_ACCESS);
	posix_acl_release(acl);
	return ret;
}
EXPORT_SYMBOL(posix_acl_chmod);

int
posix_acl_create(struct inode *dir, umode_t *mode,
		struct posix_acl **default_acl, struct posix_acl **acl)
{
	struct posix_acl *p;
	struct posix_acl *clone;
	int ret;

	*acl = NULL;
	*default_acl = NULL;

	if (S_ISLNK(*mode) || !IS_POSIXACL(dir))
		return 0;

	p = get_inode_acl(dir, ACL_TYPE_DEFAULT);
	if (!p || p == ERR_PTR(-EOPNOTSUPP)) {
		*mode &= ~current_umask();
		return 0;
	}
	if (IS_ERR(p))
		return PTR_ERR(p);

	ret = -ENOMEM;
	clone = posix_acl_clone(p, GFP_NOFS);
	if (!clone)
		goto err_release;

	ret = posix_acl_create_masq(clone, mode);
	if (ret < 0)
		goto err_release_clone;

	if (ret == 0)
		posix_acl_release(clone);
	else
		*acl = clone;

	if (!S_ISDIR(*mode))
		posix_acl_release(p);
	else
		*default_acl = p;

	return 0;

err_release_clone:
	posix_acl_release(clone);
err_release:
	posix_acl_release(p);
	return ret;
}
EXPORT_SYMBOL_GPL(posix_acl_create);

/**
 * posix_acl_update_mode  -  update mode in set_acl
 * @idmap:	idmap of the mount @inode was found from
 * @inode:	target inode
 * @mode_p:	mode (pointer) for update
 * @acl:	acl pointer
 *
 * Update the file mode when setting an ACL: compute the new file permission
 * bits based on the ACL.  In addition, if the ACL is equivalent to the new
 * file mode, set *@acl to NULL to indicate that no ACL should be set.
 *
 * As with chmod, clear the setgid bit if the caller is not in the owning group
 * or capable of CAP_FSETID (see inode_change_ok).
 *
 * If the inode has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then
 * take care to map the inode according to @idmap before checking
 * permissions. On non-idmapped mounts or if permission checking is to be
 * performed on the raw inode simply pass @nop_mnt_idmap.
 *
 * Called from set_acl inode operations.
 */
int posix_acl_update_mode(struct mnt_idmap *idmap,
			  struct inode *inode, umode_t *mode_p,
			  struct posix_acl **acl)
{
	umode_t mode = inode->i_mode;
	int error;

	error = posix_acl_equiv_mode(*acl, &mode);
	if (error < 0)
		return error;
	if (error == 0)
		*acl = NULL;
	if (!vfsgid_in_group_p(i_gid_into_vfsgid(idmap, inode)) &&
	    !capable_wrt_inode_uidgid(idmap, inode, CAP_FSETID))
		mode &= ~S_ISGID;
	*mode_p = mode;
	return 0;
}
EXPORT_SYMBOL(posix_acl_update_mode);

/*
 * Fix up the uids and gids in posix acl extended attributes in place.
 */
static int posix_acl_fix_xattr_common(const void *value, size_t size)
{
	const struct posix_acl_xattr_header *header = value;
	int count;

	if (!header)
		return -EINVAL;
	if (size < sizeof(struct posix_acl_xattr_header))
		return -EINVAL;
	if (header->a_version != cpu_to_le32(POSIX_ACL_XATTR_VERSION))
		return -EOPNOTSUPP;

	count = posix_acl_xattr_count(size);
	if (count < 0)
		return -EINVAL;
	if (count == 0)
		return 0;

	return count;
}

/**
 * posix_acl_from_xattr - convert POSIX ACLs from backing store to VFS format
 * @userns: the filesystem's idmapping
 * @value: the uapi representation of POSIX ACLs
 * @size: the size of @void
 *
 * Filesystems that store POSIX ACLs in the unaltered uapi format should use
 * posix_acl_from_xattr() when reading them from the backing store and
 * converting them into the struct posix_acl VFS format. The helper is
 * specifically intended to be called from the acl inode operation.
 *
 * The posix_acl_from_xattr() function will map the raw {g,u}id values stored
 * in ACL_{GROUP,USER} entries into idmapping in @userns.
 *
 * Note that posix_acl_from_xattr() does not take idmapped mounts into account.
 * If it did it calling it from the get acl inode operation would return POSIX
 * ACLs mapped according to an idmapped mount which would mean that the value
 * couldn't be cached for the filesystem. Idmapped mounts are taken into
 * account on the fly during permission checking or right at the VFS -
 * userspace boundary before reporting them to the user.
 *
 * Return: Allocated struct posix_acl on success, NULL for a valid header but
 *         without actual POSIX ACL entries, or ERR_PTR() encoded error code.
 */
struct posix_acl *posix_acl_from_xattr(struct user_namespace *userns,
				       const void *value, size_t size)
{
	const struct posix_acl_xattr_header *header = value;
	const struct posix_acl_xattr_entry *entry = (const void *)(header + 1), *end;
	int count;
	struct posix_acl *acl;
	struct posix_acl_entry *acl_e;

	count = posix_acl_fix_xattr_common(value, size);
	if (count < 0)
		return ERR_PTR(count);
	if (count == 0)
		return NULL;
	
	acl = posix_acl_alloc(count, GFP_NOFS);
	if (!acl)
		return ERR_PTR(-ENOMEM);
	acl_e = acl->a_entries;
	
	for (end = entry + count; entry != end; acl_e++, entry++) {
		acl_e->e_tag  = le16_to_cpu(entry->e_tag);
		acl_e->e_perm = le16_to_cpu(entry->e_perm);

		switch(acl_e->e_tag) {
			case ACL_USER_OBJ:
			case ACL_GROUP_OBJ:
			case ACL_MASK:
			case ACL_OTHER:
				break;

			case ACL_USER:
				acl_e->e_uid = make_kuid(userns,
						le32_to_cpu(entry->e_id));
				if (!uid_valid(acl_e->e_uid))
					goto fail;
				break;
			case ACL_GROUP:
				acl_e->e_gid = make_kgid(userns,
						le32_to_cpu(entry->e_id));
				if (!gid_valid(acl_e->e_gid))
					goto fail;
				break;

			default:
				goto fail;
		}
	}
	return acl;

fail:
	posix_acl_release(acl);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL (posix_acl_from_xattr);

/*
 * Convert from in-memory to extended attribute representation.
 */
int
posix_acl_to_xattr(struct user_namespace *user_ns, const struct posix_acl *acl,
		   void *buffer, size_t size)
{
	struct posix_acl_xattr_header *ext_acl = buffer;
	struct posix_acl_xattr_entry *ext_entry;
	int real_size, n;

	real_size = posix_acl_xattr_size(acl->a_count);
	if (!buffer)
		return real_size;
	if (real_size > size)
		return -ERANGE;

	ext_entry = (void *)(ext_acl + 1);
	ext_acl->a_version = cpu_to_le32(POSIX_ACL_XATTR_VERSION);

	for (n=0; n < acl->a_count; n++, ext_entry++) {
		const struct posix_acl_entry *acl_e = &acl->a_entries[n];
		ext_entry->e_tag  = cpu_to_le16(acl_e->e_tag);
		ext_entry->e_perm = cpu_to_le16(acl_e->e_perm);
		switch(acl_e->e_tag) {
		case ACL_USER:
			ext_entry->e_id =
				cpu_to_le32(from_kuid(user_ns, acl_e->e_uid));
			break;
		case ACL_GROUP:
			ext_entry->e_id =
				cpu_to_le32(from_kgid(user_ns, acl_e->e_gid));
			break;
		default:
			ext_entry->e_id = cpu_to_le32(ACL_UNDEFINED_ID);
			break;
		}
	}
	return real_size;
}
EXPORT_SYMBOL (posix_acl_to_xattr);

/**
 * vfs_posix_acl_to_xattr - convert from kernel to userspace representation
 * @idmap: idmap of the mount
 * @inode: inode the posix acls are set on
 * @acl: the posix acls as represented by the vfs
 * @buffer: the buffer into which to convert @acl
 * @size: size of @buffer
 *
 * This converts @acl from the VFS representation in the filesystem idmapping
 * to the uapi form reportable to userspace. And mount and caller idmappings
 * are handled appropriately.
 *
 * Return: On success, the size of the stored uapi posix acls, on error a
 * negative errno.
 */
static ssize_t vfs_posix_acl_to_xattr(struct mnt_idmap *idmap,
				      struct inode *inode,
				      const struct posix_acl *acl, void *buffer,
				      size_t size)

{
	struct posix_acl_xattr_header *ext_acl = buffer;
	struct posix_acl_xattr_entry *ext_entry;
	struct user_namespace *fs_userns, *caller_userns;
	ssize_t real_size, n;
	vfsuid_t vfsuid;
	vfsgid_t vfsgid;

	real_size = posix_acl_xattr_size(acl->a_count);
	if (!buffer)
		return real_size;
	if (real_size > size)
		return -ERANGE;

	ext_entry = (void *)(ext_acl + 1);
	ext_acl->a_version = cpu_to_le32(POSIX_ACL_XATTR_VERSION);

	fs_userns = i_user_ns(inode);
	caller_userns = current_user_ns();
	for (n=0; n < acl->a_count; n++, ext_entry++) {
		const struct posix_acl_entry *acl_e = &acl->a_entries[n];
		ext_entry->e_tag  = cpu_to_le16(acl_e->e_tag);
		ext_entry->e_perm = cpu_to_le16(acl_e->e_perm);
		switch(acl_e->e_tag) {
		case ACL_USER:
			vfsuid = make_vfsuid(idmap, fs_userns, acl_e->e_uid);
			ext_entry->e_id = cpu_to_le32(from_kuid(
				caller_userns, vfsuid_into_kuid(vfsuid)));
			break;
		case ACL_GROUP:
			vfsgid = make_vfsgid(idmap, fs_userns, acl_e->e_gid);
			ext_entry->e_id = cpu_to_le32(from_kgid(
				caller_userns, vfsgid_into_kgid(vfsgid)));
			break;
		default:
			ext_entry->e_id = cpu_to_le32(ACL_UNDEFINED_ID);
			break;
		}
	}
	return real_size;
}

int
set_posix_acl(struct mnt_idmap *idmap, struct dentry *dentry,
	      int type, struct posix_acl *acl)
{
	struct inode *inode = d_inode(dentry);

	if (!IS_POSIXACL(inode))
		return -EOPNOTSUPP;
	if (!inode->i_op->set_acl)
		return -EOPNOTSUPP;

	if (type == ACL_TYPE_DEFAULT && !S_ISDIR(inode->i_mode))
		return acl ? -EACCES : 0;
	if (!inode_owner_or_capable(idmap, inode))
		return -EPERM;

	if (acl) {
		int ret = posix_acl_valid(inode->i_sb->s_user_ns, acl);
		if (ret)
			return ret;
	}
	return inode->i_op->set_acl(idmap, dentry, acl, type);
}
EXPORT_SYMBOL(set_posix_acl);

int posix_acl_listxattr(struct inode *inode, char **buffer,
			ssize_t *remaining_size)
{
	int err;

	if (!IS_POSIXACL(inode))
		return 0;

	if (inode->i_acl) {
		err = xattr_list_one(buffer, remaining_size,
				     XATTR_NAME_POSIX_ACL_ACCESS);
		if (err)
			return err;
	}

	if (inode->i_default_acl) {
		err = xattr_list_one(buffer, remaining_size,
				     XATTR_NAME_POSIX_ACL_DEFAULT);
		if (err)
			return err;
	}

	return 0;
}

static bool
posix_acl_xattr_list(struct dentry *dentry)
{
	return IS_POSIXACL(d_backing_inode(dentry));
}

/*
 * nop_posix_acl_access - legacy xattr handler for access POSIX ACLs
 *
 * This is the legacy POSIX ACL access xattr handler. It is used by some
 * filesystems to implement their ->listxattr() inode operation. New code
 * should never use them.
 */
const struct xattr_handler nop_posix_acl_access = {
	.name = XATTR_NAME_POSIX_ACL_ACCESS,
	.list = posix_acl_xattr_list,
};
EXPORT_SYMBOL_GPL(nop_posix_acl_access);

/*
 * nop_posix_acl_default - legacy xattr handler for default POSIX ACLs
 *
 * This is the legacy POSIX ACL default xattr handler. It is used by some
 * filesystems to implement their ->listxattr() inode operation. New code
 * should never use them.
 */
const struct xattr_handler nop_posix_acl_default = {
	.name = XATTR_NAME_POSIX_ACL_DEFAULT,
	.list = posix_acl_xattr_list,
};
EXPORT_SYMBOL_GPL(nop_posix_acl_default);

int simple_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		   struct posix_acl *acl, int type)
{
	int error;
	struct inode *inode = d_inode(dentry);

	if (type == ACL_TYPE_ACCESS) {
		error = posix_acl_update_mode(idmap, inode,
				&inode->i_mode, &acl);
		if (error)
			return error;
	}

	inode_set_ctime_current(inode);
	if (IS_I_VERSION(inode))
		inode_inc_iversion(inode);
	set_cached_acl(inode, type, acl);
	return 0;
}

int simple_acl_create(struct inode *dir, struct inode *inode)
{
	struct posix_acl *default_acl, *acl;
	int error;

	error = posix_acl_create(dir, &inode->i_mode, &default_acl, &acl);
	if (error)
		return error;

	set_cached_acl(inode, ACL_TYPE_DEFAULT, default_acl);
	set_cached_acl(inode, ACL_TYPE_ACCESS, acl);

	if (default_acl)
		posix_acl_release(default_acl);
	if (acl)
		posix_acl_release(acl);
	return 0;
}

static int vfs_set_acl_idmapped_mnt(struct mnt_idmap *idmap,
				    struct user_namespace *fs_userns,
				    struct posix_acl *acl)
{
	for (int n = 0; n < acl->a_count; n++) {
		struct posix_acl_entry *acl_e = &acl->a_entries[n];

		switch (acl_e->e_tag) {
		case ACL_USER:
			acl_e->e_uid = from_vfsuid(idmap, fs_userns,
						   VFSUIDT_INIT(acl_e->e_uid));
			break;
		case ACL_GROUP:
			acl_e->e_gid = from_vfsgid(idmap, fs_userns,
						   VFSGIDT_INIT(acl_e->e_gid));
			break;
		}
	}

	return 0;
}

/**
 * vfs_set_acl - set posix acls
 * @idmap: idmap of the mount
 * @dentry: the dentry based on which to set the posix acls
 * @acl_name: the name of the posix acl
 * @kacl: the posix acls in the appropriate VFS format
 *
 * This function sets @kacl. The caller must all posix_acl_release() on @kacl
 * afterwards.
 *
 * Return: On success 0, on error negative errno.
 */
int vfs_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		const char *acl_name, struct posix_acl *kacl)
{
	int acl_type;
	int error;
	struct inode *inode = d_inode(dentry);
	struct inode *delegated_inode = NULL;

	acl_type = posix_acl_type(acl_name);
	if (acl_type < 0)
		return -EINVAL;

	if (kacl) {
		/*
		 * If we're on an idmapped mount translate from mount specific
		 * vfs{g,u}id_t into global filesystem k{g,u}id_t.
		 * Afterwards we can cache the POSIX ACLs filesystem wide and -
		 * if this is a filesystem with a backing store - ultimately
		 * translate them to backing store values.
		 */
		error = vfs_set_acl_idmapped_mnt(idmap, i_user_ns(inode), kacl);
		if (error)
			return error;
	}

retry_deleg:
	inode_lock(inode);

	/*
	 * We only care about restrictions the inode struct itself places upon
	 * us otherwise POSIX ACLs aren't subject to any VFS restrictions.
	 */
	error = may_write_xattr(idmap, inode);
	if (error)
		goto out_inode_unlock;

	error = security_inode_set_acl(idmap, dentry, acl_name, kacl);
	if (error)
		goto out_inode_unlock;

	error = try_break_deleg(inode, &delegated_inode);
	if (error)
		goto out_inode_unlock;

	if (likely(!is_bad_inode(inode)))
		error = set_posix_acl(idmap, dentry, acl_type, kacl);
	else
		error = -EIO;
	if (!error) {
		fsnotify_xattr(dentry);
		evm_inode_post_set_acl(dentry, acl_name, kacl);
	}

out_inode_unlock:
	inode_unlock(inode);

	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}

	return error;
}
EXPORT_SYMBOL_GPL(vfs_set_acl);

/**
 * vfs_get_acl - get posix acls
 * @idmap: idmap of the mount
 * @dentry: the dentry based on which to retrieve the posix acls
 * @acl_name: the name of the posix acl
 *
 * This function retrieves @kacl from the filesystem. The caller must all
 * posix_acl_release() on @kacl.
 *
 * Return: On success POSIX ACLs in VFS format, on error negative errno.
 */
struct posix_acl *vfs_get_acl(struct mnt_idmap *idmap,
			      struct dentry *dentry, const char *acl_name)
{
	struct inode *inode = d_inode(dentry);
	struct posix_acl *acl;
	int acl_type, error;

	acl_type = posix_acl_type(acl_name);
	if (acl_type < 0)
		return ERR_PTR(-EINVAL);

	/*
	 * The VFS has no restrictions on reading POSIX ACLs so calling
	 * something like xattr_permission() isn't needed. Only LSMs get a say.
	 */
	error = security_inode_get_acl(idmap, dentry, acl_name);
	if (error)
		return ERR_PTR(error);

	if (!IS_POSIXACL(inode))
		return ERR_PTR(-EOPNOTSUPP);
	if (S_ISLNK(inode->i_mode))
		return ERR_PTR(-EOPNOTSUPP);

	acl = __get_acl(idmap, dentry, inode, acl_type);
	if (IS_ERR(acl))
		return acl;
	if (!acl)
		return ERR_PTR(-ENODATA);

	return acl;
}
EXPORT_SYMBOL_GPL(vfs_get_acl);

/**
 * vfs_remove_acl - remove posix acls
 * @idmap: idmap of the mount
 * @dentry: the dentry based on which to retrieve the posix acls
 * @acl_name: the name of the posix acl
 *
 * This function removes posix acls.
 *
 * Return: On success 0, on error negative errno.
 */
int vfs_remove_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		   const char *acl_name)
{
	int acl_type;
	int error;
	struct inode *inode = d_inode(dentry);
	struct inode *delegated_inode = NULL;

	acl_type = posix_acl_type(acl_name);
	if (acl_type < 0)
		return -EINVAL;

retry_deleg:
	inode_lock(inode);

	/*
	 * We only care about restrictions the inode struct itself places upon
	 * us otherwise POSIX ACLs aren't subject to any VFS restrictions.
	 */
	error = may_write_xattr(idmap, inode);
	if (error)
		goto out_inode_unlock;

	error = security_inode_remove_acl(idmap, dentry, acl_name);
	if (error)
		goto out_inode_unlock;

	error = try_break_deleg(inode, &delegated_inode);
	if (error)
		goto out_inode_unlock;

	if (likely(!is_bad_inode(inode)))
		error = set_posix_acl(idmap, dentry, acl_type, NULL);
	else
		error = -EIO;
	if (!error) {
		fsnotify_xattr(dentry);
		evm_inode_post_remove_acl(idmap, dentry, acl_name);
	}

out_inode_unlock:
	inode_unlock(inode);

	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}

	return error;
}
EXPORT_SYMBOL_GPL(vfs_remove_acl);

int do_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
	       const char *acl_name, const void *kvalue, size_t size)
{
	int error;
	struct posix_acl *acl = NULL;

	if (size) {
		/*
		 * Note that posix_acl_from_xattr() uses GFP_NOFS when it
		 * probably doesn't need to here.
		 */
		acl = posix_acl_from_xattr(current_user_ns(), kvalue, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
	}

	error = vfs_set_acl(idmap, dentry, acl_name, acl);
	posix_acl_release(acl);
	return error;
}

ssize_t do_get_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		   const char *acl_name, void *kvalue, size_t size)
{
	ssize_t error;
	struct posix_acl *acl;

	acl = vfs_get_acl(idmap, dentry, acl_name);
	if (IS_ERR(acl))
		return PTR_ERR(acl);

	error = vfs_posix_acl_to_xattr(idmap, d_inode(dentry),
				       acl, kvalue, size);
	posix_acl_release(acl);
	return error;
}
