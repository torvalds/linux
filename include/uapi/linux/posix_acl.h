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

#ifndef __UAPI_POSIX_ACL_H
#define __UAPI_POSIX_ACL_H

#define ACL_UNDEFINED_ID	(-1)

/* a_type field in acl_user_posix_entry_t */
#define ACL_TYPE_ACCESS		(0x8000)
#define ACL_TYPE_DEFAULT	(0x4000)

/* e_tag entry in struct posix_acl_entry */
#define ACL_USER_OBJ		(0x01)
#define ACL_USER		(0x02)
#define ACL_GROUP_OBJ		(0x04)
#define ACL_GROUP		(0x08)
#define ACL_MASK		(0x10)
#define ACL_OTHER		(0x20)

/* permissions in the e_perm field */
#define ACL_READ		(0x04)
#define ACL_WRITE		(0x02)
#define ACL_EXECUTE		(0x01)

#endif  /* __UAPI_POSIX_ACL_H */
