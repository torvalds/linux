/*
 * Copyright (C) 2010  Novell, Inc.
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

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/richacl.h>

/**
 * richacl_inherit_inode  -  compute inherited acl and file mode
 * @dir_acl:	acl of the containing directory
 * @inode:	inode of the new file (create mode in i_mode)
 *
 * The file permission bits in inode->i_mode must be set to the create mode.
 * If there is an inheritable acl, the maximum permissions that the acl grants
 * will be computed and permissions not granted by the acl will be removed from
 * inode->i_mode.  If there is no inheritable acl, the umask will be applied
 * instead.
 */
struct richacl *
richacl_inherit_inode(const struct richacl *dir_acl, struct inode *inode)
{
	struct richacl *acl;
	mode_t mask;

	acl = richacl_inherit(dir_acl, S_ISDIR(inode->i_mode));
	if (acl) {

		richacl_compute_max_masks(acl);

		/*
		 * Ensure that the acl will not grant any permissions beyond
		 * the create mode.
		 */
		acl->a_flags |= ACL4_MASKED;
		acl->a_owner_mask &= richacl_mode_to_mask(inode->i_mode >> 6) |
				     ACE4_POSIX_OWNER_ALLOWED;
		acl->a_group_mask &= richacl_mode_to_mask(inode->i_mode >> 3);
		acl->a_other_mask &= richacl_mode_to_mask(inode->i_mode);
		mask = ~S_IRWXUGO | richacl_masks_to_mode(acl);
	} else
		mask = ~current_umask();

	inode->i_mode &= mask;
	return acl;
}
EXPORT_SYMBOL_GPL(richacl_inherit_inode);
