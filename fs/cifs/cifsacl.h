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


#define NUM_AUTHS 6 /* number of authority fields */
#define NUM_SUBAUTHS 5 /* number of sub authority fields */
#define NUM_WK_SIDS 7 /* number of well known sids */
#define SIDNAMELENGTH 20 /* long enough for the ones we care about */

#define READ_BIT        0x4
#define WRITE_BIT       0x2
#define EXEC_BIT        0x1

#define UBITSHIFT	6
#define GBITSHIFT	3

struct cifs_ntsd {
	__le16 revision; /* revision level */
	__le16 type;
	__le32 osidoffset;
	__le32 gsidoffset;
	__le32 sacloffset;
	__le32 dacloffset;
} __attribute__((packed));

struct cifs_sid {
	__u8 revision; /* revision level */
	__u8 num_subauth;
	__u8 authority[6];
	__le32 sub_auth[5]; /* sub_auth[num_subauth] */ /* BB FIXME endianness BB */
} __attribute__((packed));

struct cifs_acl {
	__le16 revision; /* revision level */
	__le16 size;
	__le32 num_aces;
} __attribute__((packed));

struct cifs_ntace { /* first part of ACE which contains perms */
	__u8 type;
	__u8 flags;
	__le16 size;
	__le32 access_req;
} __attribute__((packed));

struct cifs_ace { /* last part of ACE which includes user info */
	__u8 revision; /* revision level */
	__u8 num_subauth;
	__u8 authority[6];
	__le32 sub_auth[5];
} __attribute__((packed));

struct cifs_wksid {
	struct cifs_sid cifssid;
	char sidname[SIDNAMELENGTH];
} __attribute__((packed));

#ifdef CONFIG_CIFS_EXPERIMENTAL

extern int match_sid(struct cifs_sid *);
extern int compare_sids(struct cifs_sid *, struct cifs_sid *);

#endif /*  CONFIG_CIFS_EXPERIMENTAL */

#endif /* _CIFSACL_H */
