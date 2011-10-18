/*
 * Copyright (C) 2006, 2010  Novell, Inc.
 * Written by Andreas Gruenbacher <agruen@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/richacl_xattr.h>

MODULE_LICENSE("GPL");

/**
 * richacl_from_xattr  -  convert a richacl xattr into the in-memory representation
 */
struct richacl *
richacl_from_xattr(const void *value, size_t size)
{
	const struct richacl_xattr *xattr_acl = value;
	const struct richace_xattr *xattr_ace = (void *)(xattr_acl + 1);
	struct richacl *acl;
	struct richace *ace;
	int count;

	if (size < sizeof(struct richacl_xattr) ||
	    xattr_acl->a_version != ACL4_XATTR_VERSION ||
	    (xattr_acl->a_flags & ~ACL4_VALID_FLAGS))
		return ERR_PTR(-EINVAL);

	count = le16_to_cpu(xattr_acl->a_count);
	if (count > ACL4_XATTR_MAX_COUNT)
		return ERR_PTR(-EINVAL);

	acl = richacl_alloc(count);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	acl->a_flags = xattr_acl->a_flags;
	acl->a_owner_mask = le32_to_cpu(xattr_acl->a_owner_mask);
	if (acl->a_owner_mask & ~ACE4_VALID_MASK)
		goto fail_einval;
	acl->a_group_mask = le32_to_cpu(xattr_acl->a_group_mask);
	if (acl->a_group_mask & ~ACE4_VALID_MASK)
		goto fail_einval;
	acl->a_other_mask = le32_to_cpu(xattr_acl->a_other_mask);
	if (acl->a_other_mask & ~ACE4_VALID_MASK)
		goto fail_einval;

	if (((void *)xattr_ace + count * sizeof(*xattr_ace)) > (value + size))
		goto fail_einval;

	richacl_for_each_entry(ace, acl) {

		ace->e_type  = le16_to_cpu(xattr_ace->e_type);
		ace->e_flags = le16_to_cpu(xattr_ace->e_flags);
		ace->e_mask  = le32_to_cpu(xattr_ace->e_mask);
		ace->e_id    = le32_to_cpu(xattr_ace->e_id);

		if (ace->e_flags & ~ACE4_VALID_FLAGS)
			goto fail_einval;
		if (ace->e_type > ACE4_ACCESS_DENIED_ACE_TYPE ||
		    (ace->e_mask & ~ACE4_VALID_MASK))
			goto fail_einval;

		xattr_ace++;
	}

	return acl;

fail_einval:
	richacl_put(acl);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL_GPL(richacl_from_xattr);

/**
 * richacl_xattr_size  -  compute the size of the xattr representation of @acl
 */
size_t
richacl_xattr_size(const struct richacl *acl)
{
	size_t size = sizeof(struct richacl_xattr);

	size += sizeof(struct richace_xattr) * acl->a_count;
	return size;
}
EXPORT_SYMBOL_GPL(richacl_xattr_size);

/**
 * richacl_to_xattr  -  convert @acl into its xattr representation
 * @acl:	the richacl to convert
 * @buffer:	buffer of size richacl_xattr_size(@acl) for the result
 */
void
richacl_to_xattr(const struct richacl *acl, void *buffer)
{
	struct richacl_xattr *xattr_acl = buffer;
	struct richace_xattr *xattr_ace;
	const struct richace *ace;

	xattr_acl->a_version = ACL4_XATTR_VERSION;
	xattr_acl->a_flags = acl->a_flags;
	xattr_acl->a_count = cpu_to_le16(acl->a_count);

	xattr_acl->a_owner_mask = cpu_to_le32(acl->a_owner_mask);
	xattr_acl->a_group_mask = cpu_to_le32(acl->a_group_mask);
	xattr_acl->a_other_mask = cpu_to_le32(acl->a_other_mask);

	xattr_ace = (void *)(xattr_acl + 1);
	richacl_for_each_entry(ace, acl) {
		xattr_ace->e_type = cpu_to_le16(ace->e_type);
		xattr_ace->e_flags = cpu_to_le16(ace->e_flags &
						 ACE4_VALID_FLAGS);
		xattr_ace->e_mask = cpu_to_le32(ace->e_mask);
		xattr_ace->e_id = cpu_to_le32(ace->e_id);
		xattr_ace++;
	}
}
EXPORT_SYMBOL_GPL(richacl_to_xattr);
