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

#ifndef __RICHACL_XATTR_H
#define __RICHACL_XATTR_H

#include <linux/richacl.h>

#define RICHACL_XATTR "system.richacl"

struct richace_xattr {
	__le16		e_type;
	__le16		e_flags;
	__le32		e_mask;
	__le32		e_id;
};

struct richacl_xattr {
	unsigned char	a_version;
	unsigned char	a_flags;
	__le16		a_count;
	__le32		a_owner_mask;
	__le32		a_group_mask;
	__le32		a_other_mask;
};

#define ACL4_XATTR_VERSION	0
#define ACL4_XATTR_MAX_COUNT	1024

extern struct richacl *richacl_from_xattr(const void *, size_t);
extern size_t richacl_xattr_size(const struct richacl *acl);
extern void richacl_to_xattr(const struct richacl *, void *);

#endif /* __RICHACL_XATTR_H */
