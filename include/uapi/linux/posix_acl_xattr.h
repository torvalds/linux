/* SPDX-License-Identifier: LGPL-2.1+ WITH Linux-syscall-note */
/*
 * Copyright (C) 2002 Andreas Gruenbacher <a.gruenbacher@computer.org>
 * Copyright (C) 2016 Red Hat, Inc.
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 */

#ifndef __UAPI_POSIX_ACL_XATTR_H
#define __UAPI_POSIX_ACL_XATTR_H

#include <linux/types.h>

/* Supported ACL a_version fields */
#define POSIX_ACL_XATTR_VERSION	0x0002

/* An undefined entry e_id value */
#define ACL_UNDEFINED_ID	(-1)

struct posix_acl_xattr_entry {
	__le16			e_tag;
	__le16			e_perm;
	__le32			e_id;
};

struct posix_acl_xattr_header {
	__le32			a_version;
};

#endif	/* __UAPI_POSIX_ACL_XATTR_H */
