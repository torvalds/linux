/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#ifndef _CIFSACL_H
#define _CIFSACL_H

#include "../common/smbacl.h"

#define READ_BIT        0x4
#define WRITE_BIT       0x2
#define EXEC_BIT        0x1

#define ACL_OWNER_MASK 0700
#define ACL_GROUP_MASK 0070
#define ACL_EVERYONE_MASK 0007

#define UBITSHIFT	6
#define GBITSHIFT	3

/*
 * Security Descriptor length containing DACL with 3 ACEs (one each for
 * owner, group and world).
 */
#define DEFAULT_SEC_DESC_LEN (sizeof(struct smb_ntsd) + \
			      sizeof(struct smb_acl) + \
			      (sizeof(struct smb_ace) * 4))

/*
 * The current SMB3 form of security descriptor is similar to what was used for
 * cifs (see above) but some fields are split, and fields in the struct below
 * matches names of fields to the spec, MS-DTYP (see sections 2.4.5 and
 * 2.4.6). Note that "CamelCase" fields are used in this struct in order to
 * match the MS-DTYP and MS-SMB2 specs which define the wire format.
 */
struct smb3_sd {
	__u8 Revision; /* revision level, MUST be one */
	__u8 Sbz1; /* only meaningful if 'RM' flag set below */
	__le16 Control;
	__le32 OffsetOwner;
	__le32 OffsetGroup;
	__le32 OffsetSacl;
	__le32 OffsetDacl;
} __packed;

/* Meaning of 'Control' field flags */
#define ACL_CONTROL_SR	0x8000	/* Self relative */
#define ACL_CONTROL_RM	0x4000	/* Resource manager control bits */
#define ACL_CONTROL_PS	0x2000	/* SACL protected from inherits */
#define ACL_CONTROL_PD	0x1000	/* DACL protected from inherits */
#define ACL_CONTROL_SI	0x0800	/* SACL Auto-Inherited */
#define ACL_CONTROL_DI	0x0400	/* DACL Auto-Inherited */
#define ACL_CONTROL_SC	0x0200	/* SACL computed through inheritance */
#define ACL_CONTROL_DC	0x0100	/* DACL computed through inheritance */
#define ACL_CONTROL_SS	0x0080	/* Create server ACL */
#define ACL_CONTROL_DT	0x0040	/* DACL provided by trusted source */
#define ACL_CONTROL_SD	0x0020	/* SACL defaulted */
#define ACL_CONTROL_SP	0x0010	/* SACL is present on object */
#define ACL_CONTROL_DD	0x0008	/* DACL defaulted */
#define ACL_CONTROL_DP	0x0004	/* DACL is present on object */
#define ACL_CONTROL_GD	0x0002	/* Group was defaulted */
#define ACL_CONTROL_OD	0x0001	/* User was defaulted */

/* Meaning of AclRevision flags */
#define ACL_REVISION	0x02 /* See section 2.4.4.1 of MS-DTYP */
#define ACL_REVISION_DS	0x04 /* Additional AceTypes allowed */

struct smb3_acl {
	u8 AclRevision; /* revision level */
	u8 Sbz1; /* MBZ */
	__le16 AclSize;
	__le16 AceCount;
	__le16 Sbz2; /* MBZ */
} __packed;

/*
 * Used to store the special 'NFS SIDs' used to persist the POSIX uid and gid
 * See http://technet.microsoft.com/en-us/library/hh509017(v=ws.10).aspx
 */
struct owner_sid {
	u8 Revision;
	u8 NumAuth;
	u8 Authority[6];
	__le32 SubAuthorities[3];
} __packed;

struct owner_group_sids {
	struct owner_sid owner;
	struct owner_sid group;
} __packed;

/*
 * Minimum security identifier can be one for system defined Users
 * and Groups such as NULL SID and World or Built-in accounts such
 * as Administrator and Guest and consists of
 * Revision + Num (Sub)Auths + Authority + Domain (one Subauthority)
 */
#define MIN_SID_LEN  (1 + 1 + 6 + 4) /* in bytes */

/*
 * Minimum security descriptor can be one without any SACL and DACL and can
 * consist of revision, type, and two sids of minimum size for owner and group
 */
#define MIN_SEC_DESC_LEN  (sizeof(struct smb_ntsd) + (2 * MIN_SID_LEN))

#endif /* _CIFSACL_H */
