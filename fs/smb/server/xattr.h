/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2021 Samsung Electronics Co., Ltd.
 */

#ifndef __XATTR_H__
#define __XATTR_H__

/*
 * These are on-disk structures to store additional metadata into xattr to
 * reproduce windows filesystem semantics. And they are encoded with NDR to
 * compatible with samba's xattr meta format. The compatibility with samba
 * is important because it can lose the information(file attribute,
 * creation time, acls) about the existing files when switching between
 * ksmbd and samba.
 */

/*
 * Dos attribute flags used for what variable is valid.
 */
enum {
	XATTR_DOSINFO_ATTRIB		= 0x00000001,
	XATTR_DOSINFO_EA_SIZE		= 0x00000002,
	XATTR_DOSINFO_SIZE		= 0x00000004,
	XATTR_DOSINFO_ALLOC_SIZE	= 0x00000008,
	XATTR_DOSINFO_CREATE_TIME	= 0x00000010,
	XATTR_DOSINFO_CHANGE_TIME	= 0x00000020,
	XATTR_DOSINFO_ITIME		= 0x00000040
};

/*
 * Dos attribute structure which is compatible with samba's one.
 * Storing it into the xattr named "DOSATTRIB" separately from inode
 * allows ksmbd to faithfully reproduce windows filesystem semantics
 * on top of a POSIX filesystem.
 */
struct xattr_dos_attrib {
	__u16	version;	/* version 3 or version 4 */
	__u32	flags;		/* valid flags */
	__u32	attr;		/* Dos attribute */
	__u32	ea_size;	/* EA size */
	__u64	size;
	__u64	alloc_size;
	__u64	create_time;	/* File creation time */
	__u64	change_time;	/* File change time */
	__u64	itime;		/* Invented/Initial time */
};

/*
 * Enumeration is used for computing posix acl hash.
 */
enum {
	SMB_ACL_TAG_INVALID = 0,
	SMB_ACL_USER,
	SMB_ACL_USER_OBJ,
	SMB_ACL_GROUP,
	SMB_ACL_GROUP_OBJ,
	SMB_ACL_OTHER,
	SMB_ACL_MASK
};

#define SMB_ACL_READ			4
#define SMB_ACL_WRITE			2
#define SMB_ACL_EXECUTE			1

struct xattr_acl_entry {
	int type;
	uid_t uid;
	gid_t gid;
	mode_t perm;
};

/*
 * xattr_smb_acl structure is used for computing posix acl hash.
 */
struct xattr_smb_acl {
	int count;
	int next;
	struct xattr_acl_entry entries[] __counted_by(count);
};

/* 64bytes hash in xattr_ntacl is computed with sha256 */
#define XATTR_SD_HASH_TYPE_SHA256	0x1
#define XATTR_SD_HASH_SIZE		64

/*
 * xattr_ntacl is used for storing ntacl and hashes.
 * Hash is used for checking valid posix acl and ntacl in xattr.
 */
struct xattr_ntacl {
	__u16	version; /* version 4*/
	void	*sd_buf;
	__u32	sd_size;
	__u16	hash_type; /* hash type */
	__u8	desc[10]; /* posix_acl description */
	__u16	desc_len;
	__u64	current_time;
	__u8	hash[XATTR_SD_HASH_SIZE]; /* 64bytes hash for ntacl */
	__u8	posix_acl_hash[XATTR_SD_HASH_SIZE]; /* 64bytes hash for posix acl */
};

/* DOS ATTRIBUTE XATTR PREFIX */
#define DOS_ATTRIBUTE_PREFIX		"DOSATTRIB"
#define DOS_ATTRIBUTE_PREFIX_LEN	(sizeof(DOS_ATTRIBUTE_PREFIX) - 1)
#define XATTR_NAME_DOS_ATTRIBUTE	(XATTR_USER_PREFIX DOS_ATTRIBUTE_PREFIX)
#define XATTR_NAME_DOS_ATTRIBUTE_LEN	\
		(sizeof(XATTR_USER_PREFIX DOS_ATTRIBUTE_PREFIX) - 1)

/* STREAM XATTR PREFIX */
#define STREAM_PREFIX			"DosStream."
#define STREAM_PREFIX_LEN		(sizeof(STREAM_PREFIX) - 1)
#define XATTR_NAME_STREAM		(XATTR_USER_PREFIX STREAM_PREFIX)
#define XATTR_NAME_STREAM_LEN		(sizeof(XATTR_NAME_STREAM) - 1)

/* SECURITY DESCRIPTOR(NTACL) XATTR PREFIX */
#define SD_PREFIX			"NTACL"
#define SD_PREFIX_LEN	(sizeof(SD_PREFIX) - 1)
#define XATTR_NAME_SD	(XATTR_SECURITY_PREFIX SD_PREFIX)
#define XATTR_NAME_SD_LEN	\
		(sizeof(XATTR_SECURITY_PREFIX SD_PREFIX) - 1)

#endif /* __XATTR_H__ */
