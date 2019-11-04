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
	return rcu_dereference(*acl_by_type(inode, type));
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

struct posix_acl *get_acl(struct inode *inode, int type)
{
	void *sentinel;
	struct posix_acl **p;
	struct posix_acl *acl;

	/*
	 * The sentinel is used to detect when another operation like
	 * set_cached_acl() or forget_cached_acl() races with get_acl().
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
	 * to just call ->get_acl to fetch the ACL ourself.  (This is going to
	 * be an unlikely race.)
	 */
	if (cmpxchg(p, ACL_NOT_CACHED, sentinel) != ACL_NOT_CACHED)
		/* fall through */ ;

	/*
	 * Normally, the ACL returned by ->get_acl will be cached.
	 * A filesystem can prevent that by calling
	 * forget_cached_acl(inode, type) in ->get_acl.
	 *
	 * If the filesystem doesn't have a get_acl() function at all, we'll
	 * just create the negative cache entry.
	 */
	if (!inode->i_op->get_acl) {
		set_cached_acl(inode, type, NULL);
		return NULL;
	}
	acl = inode->i_op->get_acl(inode, type);

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
	if (unlikely(cmpxchg(p, sentinel, acl) != sentinel))
		posix_acl_release(acl);
	return acl;
}
EXPORT_SYMBOL(get_acl);

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
static struct posix_acl *
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
posix_acl_permission(struct inode *inode, const struct posix_acl *acl, int want)
{
	const struct posix_acl_entry *pa, *pe, *mask_obj;
	int found = 0;

	want &= MAY_READ | MAY_WRITE | MAY_EXEC | MAY_NOT_BLOCK;

	FOREACH_ACL_ENTRY(pa, acl, pe) {
                switch(pa->e_tag) {
                        case ACL_USER_OBJ:
				/* (May have been checked already) */
				if (uid_eq(inode->i_uid, current_fsuid()))
                                        goto check_perm;
                                break;
                        case ACL_USER:
				if (uid_eq(pa->e_uid, current_fsuid()))
                                        goto mask;
				break;
                        case ACL_GROUP_OBJ:
                                if (in_group_p(inode->i_gid)) {
					found = 1;
					if ((pa->e_perm & want) == want)
						goto mask;
                                }
				break;
                        case ACL_GROUP:
				if (in_group_p(pa->e_gid)) {
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

int
posix_acl_chmod(struct inode *inode, umode_t mode)
{
	struct posix_acl *acl;
	int ret = 0;

	if (!IS_POSIXACL(inode))
		return 0;
	if (!inode->i_op->set_acl)
		return -EOPNOTSUPP;

	acl = get_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR_OR_NULL(acl)) {
		if (acl == ERR_PTR(-EOPNOTSUPP))
			return 0;
		return PTR_ERR(acl);
	}

	ret = __posix_acl_chmod(&acl, GFP_KERNEL, mode);
	if (ret)
		return ret;
	ret = inode->i_op->set_acl(inode, acl, ACL_TYPE_ACCESS);
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

	p = get_acl(dir, ACL_TYPE_DEFAULT);
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
 *
 * Update the file mode when setting an ACL: compute the new file permission
 * bits based on the ACL.  In addition, if the ACL is equivalent to the new
 * file mode, set *acl to NULL to indicate that no ACL should be set.
 *
 * As with chmod, clear the setgit bit if the caller is not in the owning group
 * or capable of CAP_FSETID (see inode_change_ok).
 *
 * Called from set_acl inode operations.
 */
int posix_acl_update_mode(struct inode *inode, umode_t *mode_p,
			  struct posix_acl **acl)
{
	umode_t mode = inode->i_mode;
	int error;

	error = posix_acl_equiv_mode(*acl, &mode);
	if (error < 0)
		return error;
	if (error == 0)
		*acl = NULL;
	if (!in_group_p(inode->i_gid) &&
	    !capable_wrt_inode_uidgid(inode, CAP_FSETID))
		mode &= ~S_ISGID;
	*mode_p = mode;
	return 0;
}
EXPORT_SYMBOL(posix_acl_update_mode);

/*
 * Fix up the uids and gids in posix acl extended attributes in place.
 */
static void posix_acl_fix_xattr_userns(
	struct user_namespace *to, struct user_namespace *from,
	void *value, size_t size)
{
	struct posix_acl_xattr_header *header = value;
	struct posix_acl_xattr_entry *entry = (void *)(header + 1), *end;
	int count;
	kuid_t uid;
	kgid_t gid;

	if (!value)
		return;
	if (size < sizeof(struct posix_acl_xattr_header))
		return;
	if (header->a_version != cpu_to_le32(POSIX_ACL_XATTR_VERSION))
		return;

	count = posix_acl_xattr_count(size);
	if (count < 0)
		return;
	if (count == 0)
		return;

	for (end = entry + count; entry != end; entry++) {
		switch(le16_to_cpu(entry->e_tag)) {
		case ACL_USER:
			uid = make_kuid(from, le32_to_cpu(entry->e_id));
			entry->e_id = cpu_to_le32(from_kuid(to, uid));
			break;
		case ACL_GROUP:
			gid = make_kgid(from, le32_to_cpu(entry->e_id));
			entry->e_id = cpu_to_le32(from_kgid(to, gid));
			break;
		default:
			break;
		}
	}
}

void posix_acl_fix_xattr_from_user(void *value, size_t size)
{
	struct user_namespace *user_ns = current_user_ns();
	if (user_ns == &init_user_ns)
		return;
	posix_acl_fix_xattr_userns(&init_user_ns, user_ns, value, size);
}

void posix_acl_fix_xattr_to_user(void *value, size_t size)
{
	struct user_namespace *user_ns = current_user_ns();
	if (user_ns == &init_user_ns)
		return;
	posix_acl_fix_xattr_userns(user_ns, &init_user_ns, value, size);
}

/*
 * Convert from extended attribute to in-memory representation.
 */
struct posix_acl *
posix_acl_from_xattr(struct user_namespace *user_ns,
		     const void *value, size_t size)
{
	const struct posix_acl_xattr_header *header = value;
	const struct posix_acl_xattr_entry *entry = (const void *)(header + 1), *end;
	int count;
	struct posix_acl *acl;
	struct posix_acl_entry *acl_e;

	if (!value)
		return NULL;
	if (size < sizeof(struct posix_acl_xattr_header))
		 return ERR_PTR(-EINVAL);
	if (header->a_version != cpu_to_le32(POSIX_ACL_XATTR_VERSION))
		return ERR_PTR(-EOPNOTSUPP);

	count = posix_acl_xattr_count(size);
	if (count < 0)
		return ERR_PTR(-EINVAL);
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
				acl_e->e_uid =
					make_kuid(user_ns,
						  le32_to_cpu(entry->e_id));
				if (!uid_valid(acl_e->e_uid))
					goto fail;
				break;
			case ACL_GROUP:
				acl_e->e_gid =
					make_kgid(user_ns,
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

static int
posix_acl_xattr_get(const struct xattr_handler *handler,
		    struct dentry *unused, struct inode *inode,
		    const char *name, void *value, size_t size, int flags)
{
	struct posix_acl *acl;
	int error;

	if (!IS_POSIXACL(inode))
		return -EOPNOTSUPP;
	if (S_ISLNK(inode->i_mode))
		return -EOPNOTSUPP;

	acl = get_acl(inode, handler->flags);
	if (IS_ERR(acl))
		return PTR_ERR(acl);
	if (acl == NULL)
		return -ENODATA;

	error = posix_acl_to_xattr(&init_user_ns, acl, value, size);
	posix_acl_release(acl);

	return error;
}

int
set_posix_acl(struct inode *inode, int type, struct posix_acl *acl)
{
	if (!IS_POSIXACL(inode))
		return -EOPNOTSUPP;
	if (!inode->i_op->set_acl)
		return -EOPNOTSUPP;

	if (type == ACL_TYPE_DEFAULT && !S_ISDIR(inode->i_mode))
		return acl ? -EACCES : 0;
	if (!inode_owner_or_capable(inode))
		return -EPERM;

	if (acl) {
		int ret = posix_acl_valid(inode->i_sb->s_user_ns, acl);
		if (ret)
			return ret;
	}
	return inode->i_op->set_acl(inode, acl, type);
}
EXPORT_SYMBOL(set_posix_acl);

static int
posix_acl_xattr_set(const struct xattr_handler *handler,
		    struct dentry *unused, struct inode *inode,
		    const char *name, const void *value,
		    size_t size, int flags)
{
	struct posix_acl *acl = NULL;
	int ret;

	if (value) {
		acl = posix_acl_from_xattr(&init_user_ns, value, size);
		if (IS_ERR(acl))
			return PTR_ERR(acl);
	}
	ret = set_posix_acl(inode, handler->flags, acl);
	posix_acl_release(acl);
	return ret;
}

static bool
posix_acl_xattr_list(struct dentry *dentry)
{
	return IS_POSIXACL(d_backing_inode(dentry));
}

const struct xattr_handler posix_acl_access_xattr_handler = {
	.name = XATTR_NAME_POSIX_ACL_ACCESS,
	.flags = ACL_TYPE_ACCESS,
	.list = posix_acl_xattr_list,
	.get = posix_acl_xattr_get,
	.set = posix_acl_xattr_set,
};
EXPORT_SYMBOL_GPL(posix_acl_access_xattr_handler);

const struct xattr_handler posix_acl_default_xattr_handler = {
	.name = XATTR_NAME_POSIX_ACL_DEFAULT,
	.flags = ACL_TYPE_DEFAULT,
	.list = posix_acl_xattr_list,
	.get = posix_acl_xattr_get,
	.set = posix_acl_xattr_set,
};
EXPORT_SYMBOL_GPL(posix_acl_default_xattr_handler);

int simple_set_acl(struct inode *inode, struct posix_acl *acl, int type)
{
	int error;

	if (type == ACL_TYPE_ACCESS) {
		error = posix_acl_update_mode(inode,
				&inode->i_mode, &acl);
		if (error)
			return error;
	}

	inode->i_ctime = current_time(inode);
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
