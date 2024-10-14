/* SPDX-License-Identifier: LGPL-2.1+ */
/*
 *   Copyright (c) International Business Machines  Corp., 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *   Modified by Namjae Jeon (linkinjeon@kernel.org)
 */

#ifndef _COMMON_SMBACL_H
#define _COMMON_SMBACL_H

#define NUM_AUTHS (6)	/* number of authority fields */
#define SID_MAX_SUB_AUTHORITIES (15) /* max number of sub authority fields */

/* ACE types - see MS-DTYP 2.4.4.1 */
#define ACCESS_ALLOWED_ACE_TYPE 0x00
#define ACCESS_DENIED_ACE_TYPE  0x01
#define SYSTEM_AUDIT_ACE_TYPE   0x02
#define SYSTEM_ALARM_ACE_TYPE   0x03
#define ACCESS_ALLOWED_COMPOUND_ACE_TYPE 0x04
#define ACCESS_ALLOWED_OBJECT_ACE_TYPE  0x05
#define ACCESS_DENIED_OBJECT_ACE_TYPE   0x06
#define SYSTEM_AUDIT_OBJECT_ACE_TYPE    0x07
#define SYSTEM_ALARM_OBJECT_ACE_TYPE    0x08
#define ACCESS_ALLOWED_CALLBACK_ACE_TYPE 0x09
#define ACCESS_DENIED_CALLBACK_ACE_TYPE 0x0A
#define ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE 0x0B
#define ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE  0x0C
#define SYSTEM_AUDIT_CALLBACK_ACE_TYPE  0x0D
#define SYSTEM_ALARM_CALLBACK_ACE_TYPE  0x0E /* Reserved */
#define SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE 0x0F
#define SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE 0x10 /* reserved */
#define SYSTEM_MANDATORY_LABEL_ACE_TYPE 0x11
#define SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE 0x12
#define SYSTEM_SCOPED_POLICY_ID_ACE_TYPE 0x13

/* ACE flags */
#define OBJECT_INHERIT_ACE		0x01
#define CONTAINER_INHERIT_ACE		0x02
#define NO_PROPAGATE_INHERIT_ACE	0x04
#define INHERIT_ONLY_ACE		0x08
#define INHERITED_ACE			0x10
#define SUCCESSFUL_ACCESS_ACE_FLAG	0x40
#define FAILED_ACCESS_ACE_FLAG		0x80

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

#define DOMAIN_USER_RID_LE	cpu_to_le32(513)

/*
 * ACE types - see MS-DTYP 2.4.4.1
 */
enum {
	ACCESS_ALLOWED,
	ACCESS_DENIED,
};

/*
 * Security ID types
 */
enum {
	SIDOWNER = 1,
	SIDGROUP,
	SIDCREATOR_OWNER,
	SIDCREATOR_GROUP,
	SIDUNIX_USER,
	SIDUNIX_GROUP,
	SIDNFS_USER,
	SIDNFS_GROUP,
	SIDNFS_MODE,
};

struct smb_ntsd {
	__le16 revision; /* revision level */
	__le16 type;
	__le32 osidoffset;
	__le32 gsidoffset;
	__le32 sacloffset;
	__le32 dacloffset;
} __attribute__((packed));

struct smb_sid {
	__u8 revision; /* revision level */
	__u8 num_subauth;
	__u8 authority[NUM_AUTHS];
	__le32 sub_auth[SID_MAX_SUB_AUTHORITIES]; /* sub_auth[num_subauth] */
} __attribute__((packed));

/* size of a struct smb_sid, sans sub_auth array */
#define CIFS_SID_BASE_SIZE (1 + 1 + NUM_AUTHS)

struct smb_acl {
	__le16 revision; /* revision level */
	__le16 size;
	__le32 num_aces;
} __attribute__((packed));

struct smb_ace {
	__u8 type; /* see above and MS-DTYP 2.4.4.1 */
	__u8 flags;
	__le16 size;
	__le32 access_req;
	struct smb_sid sid; /* ie UUID of user or group who gets these perms */
} __attribute__((packed));

#endif /* _COMMON_SMBACL_H */
