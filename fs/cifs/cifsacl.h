/*
 *   fs/cifs/cifsacl.h
 *
 *   Copyright (c) International Business Machines  Corp., 2005
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

struct cifs_sid {
	__u8 revision; /* revision level */
	__u8 num_subauths;
	__u8 authority[6];
	__u8 sub_auth[4];
	/* next sub_auth if any ... */
} __attribute__((packed));

/* everyone */
const cifs_sid sid_everyone = {1, 1, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0}};
/* group users */
const cifs_sid sid_user = {1, 2 , {0, 0, 0, 0, 0, 5}, {32, 545, 0, 0}};

#endif /* _CIFSACL_H */
