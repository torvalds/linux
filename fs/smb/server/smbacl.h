/* SPDX-License-Identifier: LGPL-2.1+ */
/*
 *   Copyright (c) International Business Machines  Corp., 2007
 *   Author(s): Steve French (sfrench@us.ibm.com)
 *   Modified by Namjae Jeon (linkinjeon@kernel.org)
 */

#ifndef _SMBACL_H
#define _SMBACL_H

#include "../common/smbacl.h"
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/posix_acl.h>
#include <linux/mnt_idmapping.h>

#include "mgmt/tree_connect.h"

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

struct ksmbd_conn;

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

int parse_sec_desc(struct mnt_idmap *idmap, struct smb_ntsd *pntsd,
		   int acl_len, struct smb_fattr *fattr);
int build_sec_desc(struct mnt_idmap *idmap, struct smb_ntsd *pntsd,
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
		 bool type_check, bool get_write);
void id_to_sid(unsigned int cid, uint sidtype, struct smb_sid *ssid);
void ksmbd_init_domain(u32 *sub_auth);

static inline uid_t posix_acl_uid_translate(struct mnt_idmap *idmap,
					    struct posix_acl_entry *pace)
{
	vfsuid_t vfsuid;

	/* If this is an idmapped mount, apply the idmapping. */
	vfsuid = make_vfsuid(idmap, &init_user_ns, pace->e_uid);

	/* Translate the kuid into a userspace id ksmbd would see. */
	return from_kuid(&init_user_ns, vfsuid_into_kuid(vfsuid));
}

static inline gid_t posix_acl_gid_translate(struct mnt_idmap *idmap,
					    struct posix_acl_entry *pace)
{
	vfsgid_t vfsgid;

	/* If this is an idmapped mount, apply the idmapping. */
	vfsgid = make_vfsgid(idmap, &init_user_ns, pace->e_gid);

	/* Translate the kgid into a userspace id ksmbd would see. */
	return from_kgid(&init_user_ns, vfsgid_into_kgid(vfsgid));
}

#endif /* _SMBACL_H */
