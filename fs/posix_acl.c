/*
 * linux/fs/posix_acl.c
 *
 *  Copyright (C) 2002 by Andreas Gruenbacher <a.gruenbacher@computer.org>
 *
 *  Fixes from William Schumacher incorporated on 15 March 2001.
 *     (Reported by Charles Bertsch, <CBertsch@microtest.com>).
 */

/*
 *  This file contains generic functions for manipulating
 *  POSIX 1003.1e draft standard 17 ACLs.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/atomic.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/posix_acl.h>
#include <linux/module.h>

#include <linux/errno.h>

EXPORT_SYMBOL(posix_acl_alloc);
EXPORT_SYMBOL(posix_acl_clone);
EXPORT_SYMBOL(posix_acl_valid);
EXPORT_SYMBOL(posix_acl_equiv_mode);
EXPORT_SYMBOL(posix_acl_from_mode);
EXPORT_SYMBOL(posix_acl_create_masq);
EXPORT_SYMBOL(posix_acl_chmod_masq);
EXPORT_SYMBOL(posix_acl_permission);

/*
 * Allocate a new ACL with the specified number of entries.
 */
struct posix_acl *
posix_acl_alloc(int count, gfp_t flags)
{
	const size_t size = sizeof(struct posix_acl) +
	                    count * sizeof(struct posix_acl_entry);
	struct posix_acl *acl = kmalloc(size, flags);
	if (acl) {
		atomic_set(&acl->a_refcount, 1);
		acl->a_count = count;
	}
	return acl;
}

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
			atomic_set(&clone->a_refcount, 1);
	}
	return clone;
}

/*
 * Check if an acl is valid. Returns 0 if it is, or -E... otherwise.
 */
int
posix_acl_valid(const struct posix_acl *acl)
{
	const struct posix_acl_entry *pa, *pe;
	int state = ACL_USER_OBJ;
	unsigned int id = 0;  /* keep gcc happy */
	int needs_mask = 0;

	FOREACH_ACL_ENTRY(pa, acl, pe) {
		if (pa->e_perm & ~(ACL_READ|ACL_WRITE|ACL_EXECUTE))
			return -EINVAL;
		switch (pa->e_tag) {
			case ACL_USER_OBJ:
				if (state == ACL_USER_OBJ) {
					id = 0;
					state = ACL_USER;
					break;
				}
				return -EINVAL;

			case ACL_USER:
				if (state != ACL_USER)
					return -EINVAL;
				if (pa->e_id == ACL_UNDEFINED_ID ||
				    pa->e_id < id)
					return -EINVAL;
				id = pa->e_id + 1;
				needs_mask = 1;
				break;

			case ACL_GROUP_OBJ:
				if (state == ACL_USER) {
					id = 0;
					state = ACL_GROUP;
					break;
				}
				return -EINVAL;

			case ACL_GROUP:
				if (state != ACL_GROUP)
					return -EINVAL;
				if (pa->e_id == ACL_UNDEFINED_ID ||
				    pa->e_id < id)
					return -EINVAL;
				id = pa->e_id + 1;
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

/*
 * Returns 0 if the acl can be exactly represented in the traditional
 * file mode permission bits, or else 1. Returns -E... on error.
 */
int
posix_acl_equiv_mode(const struct posix_acl *acl, mode_t *mode_p)
{
	const struct posix_acl_entry *pa, *pe;
	mode_t mode = 0;
	int not_equiv = 0;

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

/*
 * Create an ACL representing the file mode permission bits of an inode.
 */
struct posix_acl *
posix_acl_from_mode(mode_t mode, gfp_t flags)
{
	struct posix_acl *acl = posix_acl_alloc(3, flags);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	acl->a_entries[0].e_tag  = ACL_USER_OBJ;
	acl->a_entries[0].e_id   = ACL_UNDEFINED_ID;
	acl->a_entries[0].e_perm = (mode & S_IRWXU) >> 6;

	acl->a_entries[1].e_tag  = ACL_GROUP_OBJ;
	acl->a_entries[1].e_id   = ACL_UNDEFINED_ID;
	acl->a_entries[1].e_perm = (mode & S_IRWXG) >> 3;

	acl->a_entries[2].e_tag  = ACL_OTHER;
	acl->a_entries[2].e_id   = ACL_UNDEFINED_ID;
	acl->a_entries[2].e_perm = (mode & S_IRWXO);
	return acl;
}

/*
 * Return 0 if current is granted want access to the inode
 * by the acl. Returns -E... otherwise.
 */
int
posix_acl_permission(struct inode *inode, const struct posix_acl *acl, int want)
{
	const struct posix_acl_entry *pa, *pe, *mask_obj;
	int found = 0;

	FOREACH_ACL_ENTRY(pa, acl, pe) {
                switch(pa->e_tag) {
                        case ACL_USER_OBJ:
				/* (May have been checked already) */
                                if (inode->i_uid == current->fsuid)
                                        goto check_perm;
                                break;
                        case ACL_USER:
                                if (pa->e_id == current->fsuid)
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
                                if (in_group_p(pa->e_id)) {
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
int
posix_acl_create_masq(struct posix_acl *acl, mode_t *mode_p)
{
	struct posix_acl_entry *pa, *pe;
	struct posix_acl_entry *group_obj = NULL, *mask_obj = NULL;
	mode_t mode = *mode_p;
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
int
posix_acl_chmod_masq(struct posix_acl *acl, mode_t mode)
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
