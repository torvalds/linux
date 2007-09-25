/*
 *   fs/cifs/cifsacl.h
 *
 *   Copyright (c) International Business Machines  Corp., 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _CIFSACL_H
#define _CIFSACL_H

struct cifs_ntsd {
	__u16 revision; /* revision level */
	__u16 type;
	__u32 osidoffset;
	__u32 gsidoffset;
	__u32 sacloffset;
	__u32 dacloffset;
} __attribute__((packed));

struct cifs_sid {
	__u8 revision; /* revision level */
	__u8 num_auth;
	__u8 authority[6];
	__u32 sub_auth[4];
	__u32 rid;
} __attribute__((packed));

struct cifs_acl {
	__u16 revision; /* revision level */
	__u16 size;
	__u32 num_aces;
} __attribute__((packed));

struct cifs_ntace {
	__u8 type;
	__u8 flags;
	__u16 size;
	__u32 access_req;
} __attribute__((packed));

struct cifs_ace {
	__u8 revision; /* revision level */
	__u8 num_auth;
	__u8 authority[6];
	__u32 sub_auth[4];
	__u32 rid;
} __attribute__((packed));

/* everyone */
/* extern const struct cifs_sid sid_everyone;*/
/* group users */
/* extern const struct cifs_sid sid_user;*/

#endif /* _CIFSACL_H */
