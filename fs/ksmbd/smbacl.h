/* SPDX-License-Identifier: LGPL-2.1+ */
/*
 *   Copyright (c) International Business Machines  Corp., 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *   Modified by Namjae Jeon (linkinjeon@kernel.org)
 */

#ifndef _SMBACL_H
#define _SMBACL_H

#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/posix_acl.h>
#include <linux/mnt_idmapping.h>

#include "mgmt/tree_connect.h"

#define NUM_AUTHS (6)	/* number of authority fields */
#define SID_MAX_SUB_AUTHORITIES (15) /* max number of sub authority fields */

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

/* Revision for ACLs */
#define SD_REVISION	1

/* Control flags for Security Descriptor */
#define OWNER_DEFAULTED		0x0001
#define GROUP_DEFAULTED		0x0002
#define DACL_PRESENT		0x0004
#define DACL_DEFAULTED		0x0008
#define SACL_PRESENT		0x0010
#define SACL_DEFAULTED		0x0020
#define DACL_TRUSTED		0x0040
#define SERVER_SECURITY		0x0080
#define DACL_AUTO_INHERIT_REQ	0x0100
#define SACL_AUTO_INHERIT_REQ	0x0200
#define DACL_AUTO_INHERITED	0x0400
#define SACL_AUTO_INHERITED	0x0800
#define DACL_PROTECTED		0x1000
#define SACL_PROTECTED		0x2000
#define RM_CONTROL_VALID	0x4000
#define SELF_RELATIVE		0x8000

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

struct ksmbd_conn;

struct smb_ntsd {
	__le16 revision; /* revision level */
	__le16 type;
	__le32 osidoffset;
	__le32 gsidoffset;
	__le32 sacloffset;
	__le32 dacloffset;
} __packed;

struct smb_sid {
	__u8 revision; /* revision level */
	__u8 num_subauth;
	__u8 authority[NUM_AUTHS];
	__le32 sub_auth[SID_MAX_SUB_AUTHORITIES]; /* sub_auth[num_subauth] */
} __packed;

/* size of a struct cifs_sid, sans sub_auth array */
#define CIFS_SID_BASE_SIZE (1 + 1 + NUM_AUTHS)

struct smb_acl {
	__le16 revision; /* revision level */
	__le16 size;
	__le32 num_aces;
} __packed;

struct smb_ace {
	__u8 type;
	__u8 flags;
	__le16 size;
	__le32 access_req;
	struct smb_sid sid; /* ie UUID of user or group who gets these perms */
} __packed;

struct smb_fattr {
	kuid_t	cf_uid;
	kgid_t	cf_gid;
	umode_t	cf_mode;
	__le32 daccess;
	struct posix_acl *cf_acls;
	struct posix_acl *cf_dacls;
};

struct posix_ace_state {
	u32 allow;
	u32 deny;
};

struct posix_user_ace_state {
	union {
		kuid_t uid;
		kgid_t gid;
	};
	struct posix_ace_state perms;
};

struct posix_ace_state_array {
	int n;
	struct posix_user_ace_state aces[];
};

/*
 * while processing the nfsv4 ace, this maintains the partial permissions
 * calculated so far:
 */

struct posix_acl_state {
	struct posix_ace_state owner;
	struct posix_ace_state group;
	struct posix_ace_state other;
	struct posix_ace_state everyone;
	struct posix_ace_state mask; /* deny unused in this case */
	struct posix_ace_state_array *users;
	struct posix_ace_state_array *groups;
};

int parse_sec_desc(struct user_namespace *user_ns, struct smb_ntsd *pntsd,
		   int acl_len, struct smb_fattr *fattr);
int build_sec_desc(struct user_namespace *user_ns, struct smb_ntsd *pntsd,
		   struct smb_ntsd *ppntsd, int ppntsd_size, int addition_info,
		   __u32 *secdesclen, struct smb_fattr *fattr);
int init_acl_state(struct posix_acl_state *state, int cnt);
void free_acl_state(struct posix_acl_state *state);
void posix_state_to_acl(struct posix_acl_state *state,
			struct posix_acl_entry *pace);
int compare_sids(const struct smb_sid *ctsid, const struct smb_sid *cwsid);
bool smb_inherit_flags(int flags, bool is_dir);
int smb_inherit_dacl(struct ksmbd_conn *conn, const struct path *path,
		     unsigned int uid, unsigned int gid);
int smb_check_perm_dacl(struct ksmbd_conn *conn, const struct path *path,
			__le32 *pdaccess, int uid);
int set_info_sec(struct ksmbd_conn *conn, struct ksmbd_tree_connect *tcon,
		 const struct path *path, struct smb_ntsd *pntsd, int ntsd_len,
		 bool type_check);
void id_to_sid(unsigned int cid, uint sidtype, struct smb_sid *ssid);
void ksmbd_init_domain(u32 *sub_auth);

static inline uid_t posix_acl_uid_translate(struct user_namespace *mnt_userns,
					    struct posix_acl_entry *pace)
{
	vfsuid_t vfsuid;

	/* If this is an idmapped mount, apply the idmapping. */
	vfsuid = make_vfsuid(mnt_userns, &init_user_ns, pace->e_uid);

	/* Translate the kuid into a userspace id ksmbd would see. */
	return from_kuid(&init_user_ns, vfsuid_into_kuid(vfsuid));
}

static inline gid_t posix_acl_gid_translate(struct user_namespace *mnt_userns,
					    struct posix_acl_entry *pace)
{
	vfsgid_t vfsgid;

	/* If this is an idmapped mount, apply the idmapping. */
	vfsgid = make_vfsgid(mnt_userns, &init_user_ns, pace->e_gid);

	/* Translate the kgid into a userspace id ksmbd would see. */
	return from_kgid(&init_user_ns, vfsgid_into_kgid(vfsgid));
}

#endif /* _SMBACL_H */
