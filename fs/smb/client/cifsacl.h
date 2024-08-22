/* SPDX-License-Identifier: LGPL-2.1 */
/*
 *
 *   Copyright (c) International Business Machines  Corp., 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *
 */

#ifndef _CIFSACL_H
#define _CIFSACL_H

#define NUM_AUTHS (6)	/* number of authority fields */
#define SID_MAX_SUB_AUTHORITIES (15) /* max number of sub authority fields */

#define READ_BIT        0x4
#define WRITE_BIT       0x2
#define EXEC_BIT        0x1

#define ACL_OWNER_MASK 0700
#define ACL_GROUP_MASK 0070
#define ACL_EVERYONE_MASK 0007

#define UBITSHIFT	6
#define GBITSHIFT	3

#define ACCESS_ALLOWED	0
#define ACCESS_DENIED	1

#define SIDOWNER 1
#define SIDGROUP 2

/*
 * Security Descriptor length containing DACL with 3 ACEs (one each for
 * owner, group and world).
 */
#define DEFAULT_SEC_DESC_LEN (sizeof(struct smb_ntsd) + \
			      sizeof(struct cifs_acl) + \
			      (sizeof(struct cifs_ace) * 4))

/*
 * Maximum size of a string representation of a SID:
 *
 * The fields are unsigned values in decimal. So:
 *
 * u8:  max 3 bytes in decimal
 * u32: max 10 bytes in decimal
 *
 * "S-" + 3 bytes for version field + 15 for authority field + NULL terminator
 *
 * For authority field, max is when all 6 values are non-zero and it must be
 * represented in hex. So "-0x" + 12 hex digits.
 *
 * Add 11 bytes for each subauthority field (10 bytes each + 1 for '-')
 */
#define SID_STRING_BASE_SIZE (2 + 3 + 15 + 1)
#define SID_STRING_SUBAUTH_SIZE (11) /* size of a single subauth string */

struct smb_ntsd {
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
	__u8 authority[NUM_AUTHS];
	__le32 sub_auth[SID_MAX_SUB_AUTHORITIES]; /* sub_auth[num_subauth] */
} __attribute__((packed));

/* size of a struct cifs_sid, sans sub_auth array */
#define CIFS_SID_BASE_SIZE (1 + 1 + NUM_AUTHS)

struct cifs_acl {
	__le16 revision; /* revision level */
	__le16 size;
	__le32 num_aces;
} __attribute__((packed));

/* ACE types - see MS-DTYP 2.4.4.1 */
#define ACCESS_ALLOWED_ACE_TYPE	0x00
#define ACCESS_DENIED_ACE_TYPE	0x01
#define SYSTEM_AUDIT_ACE_TYPE	0x02
#define SYSTEM_ALARM_ACE_TYPE	0x03
#define ACCESS_ALLOWED_COMPOUND_ACE_TYPE 0x04
#define ACCESS_ALLOWED_OBJECT_ACE_TYPE	0x05
#define ACCESS_DENIED_OBJECT_ACE_TYPE	0x06
#define SYSTEM_AUDIT_OBJECT_ACE_TYPE	0x07
#define SYSTEM_ALARM_OBJECT_ACE_TYPE	0x08
#define ACCESS_ALLOWED_CALLBACK_ACE_TYPE 0x09
#define ACCESS_DENIED_CALLBACK_ACE_TYPE	0x0A
#define ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE 0x0B
#define ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE  0x0C
#define SYSTEM_AUDIT_CALLBACK_ACE_TYPE	0x0D
#define SYSTEM_ALARM_CALLBACK_ACE_TYPE	0x0E /* Reserved */
#define SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE 0x0F
#define SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE 0x10 /* reserved */
#define SYSTEM_MANDATORY_LABEL_ACE_TYPE	0x11
#define SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE 0x12
#define SYSTEM_SCOPED_POLICY_ID_ACE_TYPE 0x13

/* ACE flags */
#define OBJECT_INHERIT_ACE	0x01
#define CONTAINER_INHERIT_ACE	0x02
#define NO_PROPAGATE_INHERIT_ACE 0x04
#define INHERIT_ONLY_ACE	0x08
#define INHERITED_ACE		0x10
#define SUCCESSFUL_ACCESS_ACE_FLAG 0x40
#define FAILED_ACCESS_ACE_FLAG	0x80

struct cifs_ace {
	__u8 type; /* see above and MS-DTYP 2.4.4.1 */
	__u8 flags;
	__le16 size;
	__le32 access_req;
	struct cifs_sid sid; /* ie UUID of user or group who gets these perms */
} __attribute__((packed));

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
#define ACL_CONTROL_DC	0x0100	/* DACL computed through inheritence */
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
