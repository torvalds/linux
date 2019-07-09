/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/* keyctl.h: keyctl command IDs
 *
 * Copyright (C) 2004, 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_KEYCTL_H
#define _LINUX_KEYCTL_H

#include <linux/types.h>

/*
 * Keyring permission grant definitions
 */
enum key_ace_subject_type {
	KEY_ACE_SUBJ_STANDARD	= 0,	/* subject is one of key_ace_standard_subject */
	nr__key_ace_subject_type
};

enum key_ace_standard_subject {
	KEY_ACE_EVERYONE	= 0,	/* Everyone, including owner and group */
	KEY_ACE_GROUP		= 1,	/* The key's group */
	KEY_ACE_OWNER		= 2,	/* The owner of the key */
	KEY_ACE_POSSESSOR	= 3,	/* Any process that possesses of the key */
	nr__key_ace_standard_subject
};

#define KEY_ACE_VIEW		0x00000001 /* Can describe the key */
#define KEY_ACE_READ		0x00000002 /* Can read the key content */
#define KEY_ACE_WRITE		0x00000004 /* Can update/modify the key content */
#define KEY_ACE_SEARCH		0x00000008 /* Can find the key by search */
#define KEY_ACE_LINK		0x00000010 /* Can make a link to the key */
#define KEY_ACE_SET_SECURITY	0x00000020 /* Can set owner, group, ACL */
#define KEY_ACE_INVAL		0x00000040 /* Can invalidate the key */
#define KEY_ACE_REVOKE		0x00000080 /* Can revoke the key */
#define KEY_ACE_JOIN		0x00000100 /* Can join keyring */
#define KEY_ACE_CLEAR		0x00000200 /* Can clear keyring */
#define KEY_ACE__PERMS		0xffffffff

/*
 * Old-style permissions mask, deprecated in favour of ACL.
 */
#define KEY_POS_VIEW	0x01000000	/* possessor can view a key's attributes */
#define KEY_POS_READ	0x02000000	/* possessor can read key payload / view keyring */
#define KEY_POS_WRITE	0x04000000	/* possessor can update key payload / add link to keyring */
#define KEY_POS_SEARCH	0x08000000	/* possessor can find a key in search / search a keyring */
#define KEY_POS_LINK	0x10000000	/* possessor can create a link to a key/keyring */
#define KEY_POS_SETATTR	0x20000000	/* possessor can set key attributes */
#define KEY_POS_ALL	0x3f000000

#define KEY_USR_VIEW	0x00010000	/* user permissions... */
#define KEY_USR_READ	0x00020000
#define KEY_USR_WRITE	0x00040000
#define KEY_USR_SEARCH	0x00080000
#define KEY_USR_LINK	0x00100000
#define KEY_USR_SETATTR	0x00200000
#define KEY_USR_ALL	0x003f0000

#define KEY_GRP_VIEW	0x00000100	/* group permissions... */
#define KEY_GRP_READ	0x00000200
#define KEY_GRP_WRITE	0x00000400
#define KEY_GRP_SEARCH	0x00000800
#define KEY_GRP_LINK	0x00001000
#define KEY_GRP_SETATTR	0x00002000
#define KEY_GRP_ALL	0x00003f00

#define KEY_OTH_VIEW	0x00000001	/* third party permissions... */
#define KEY_OTH_READ	0x00000002
#define KEY_OTH_WRITE	0x00000004
#define KEY_OTH_SEARCH	0x00000008
#define KEY_OTH_LINK	0x00000010
#define KEY_OTH_SETATTR	0x00000020
#define KEY_OTH_ALL	0x0000003f

/* special process keyring shortcut IDs */
#define KEY_SPEC_THREAD_KEYRING		-1	/* - key ID for thread-specific keyring */
#define KEY_SPEC_PROCESS_KEYRING	-2	/* - key ID for process-specific keyring */
#define KEY_SPEC_SESSION_KEYRING	-3	/* - key ID for session-specific keyring */
#define KEY_SPEC_USER_KEYRING		-4	/* - key ID for UID-specific keyring */
#define KEY_SPEC_USER_SESSION_KEYRING	-5	/* - key ID for UID-session keyring */
#define KEY_SPEC_GROUP_KEYRING		-6	/* - key ID for GID-specific keyring */
#define KEY_SPEC_REQKEY_AUTH_KEY	-7	/* - key ID for assumed request_key auth key */
#define KEY_SPEC_REQUESTOR_KEYRING	-8	/* - key ID for request_key() dest keyring */

/* request-key default keyrings */
#define KEY_REQKEY_DEFL_NO_CHANGE		-1
#define KEY_REQKEY_DEFL_DEFAULT			0
#define KEY_REQKEY_DEFL_THREAD_KEYRING		1
#define KEY_REQKEY_DEFL_PROCESS_KEYRING		2
#define KEY_REQKEY_DEFL_SESSION_KEYRING		3
#define KEY_REQKEY_DEFL_USER_KEYRING		4
#define KEY_REQKEY_DEFL_USER_SESSION_KEYRING	5
#define KEY_REQKEY_DEFL_GROUP_KEYRING		6
#define KEY_REQKEY_DEFL_REQUESTOR_KEYRING	7

/* keyctl commands */
#define KEYCTL_GET_KEYRING_ID		0	/* ask for a keyring's ID */
#define KEYCTL_JOIN_SESSION_KEYRING	1	/* join or start named session keyring */
#define KEYCTL_UPDATE			2	/* update a key */
#define KEYCTL_REVOKE			3	/* revoke a key */
#define KEYCTL_CHOWN			4	/* set ownership of a key */
#define KEYCTL_SETPERM			5	/* set perms on a key */
#define KEYCTL_DESCRIBE			6	/* describe a key */
#define KEYCTL_CLEAR			7	/* clear contents of a keyring */
#define KEYCTL_LINK			8	/* link a key into a keyring */
#define KEYCTL_UNLINK			9	/* unlink a key from a keyring */
#define KEYCTL_SEARCH			10	/* search for a key in a keyring */
#define KEYCTL_READ			11	/* read a key or keyring's contents */
#define KEYCTL_INSTANTIATE		12	/* instantiate a partially constructed key */
#define KEYCTL_NEGATE			13	/* negate a partially constructed key */
#define KEYCTL_SET_REQKEY_KEYRING	14	/* set default request-key keyring */
#define KEYCTL_SET_TIMEOUT		15	/* set key timeout */
#define KEYCTL_ASSUME_AUTHORITY		16	/* assume request_key() authorisation */
#define KEYCTL_GET_SECURITY		17	/* get key security label */
#define KEYCTL_SESSION_TO_PARENT	18	/* apply session keyring to parent process */
#define KEYCTL_REJECT			19	/* reject a partially constructed key */
#define KEYCTL_INSTANTIATE_IOV		20	/* instantiate a partially constructed key */
#define KEYCTL_INVALIDATE		21	/* invalidate a key */
#define KEYCTL_GET_PERSISTENT		22	/* get a user's persistent keyring */
#define KEYCTL_DH_COMPUTE		23	/* Compute Diffie-Hellman values */
#define KEYCTL_PKEY_QUERY		24	/* Query public key parameters */
#define KEYCTL_PKEY_ENCRYPT		25	/* Encrypt a blob using a public key */
#define KEYCTL_PKEY_DECRYPT		26	/* Decrypt a blob using a public key */
#define KEYCTL_PKEY_SIGN		27	/* Create a public key signature */
#define KEYCTL_PKEY_VERIFY		28	/* Verify a public key signature */
#define KEYCTL_RESTRICT_KEYRING		29	/* Restrict keys allowed to link to a keyring */
#define KEYCTL_MOVE			30	/* Move keys between keyrings */
#define KEYCTL_CAPABILITIES		31	/* Find capabilities of keyrings subsystem */
#define KEYCTL_GRANT_PERMISSION		32	/* Grant a permit to a key */

/* keyctl structures */
struct keyctl_dh_params {
	union {
#ifndef __cplusplus
		__s32 private;
#endif
		__s32 priv;
	};
	__s32 prime;
	__s32 base;
};

struct keyctl_kdf_params {
	char __user *hashname;
	char __user *otherinfo;
	__u32 otherinfolen;
	__u32 __spare[8];
};

#define KEYCTL_SUPPORTS_ENCRYPT		0x01
#define KEYCTL_SUPPORTS_DECRYPT		0x02
#define KEYCTL_SUPPORTS_SIGN		0x04
#define KEYCTL_SUPPORTS_VERIFY		0x08

struct keyctl_pkey_query {
	__u32		supported_ops;	/* Which ops are supported */
	__u32		key_size;	/* Size of the key in bits */
	__u16		max_data_size;	/* Maximum size of raw data to sign in bytes */
	__u16		max_sig_size;	/* Maximum size of signature in bytes */
	__u16		max_enc_size;	/* Maximum size of encrypted blob in bytes */
	__u16		max_dec_size;	/* Maximum size of decrypted blob in bytes */
	__u32		__spare[10];
};

struct keyctl_pkey_params {
	__s32		key_id;		/* Serial no. of public key to use */
	__u32		in_len;		/* Input data size */
	union {
		__u32		out_len;	/* Output buffer size (encrypt/decrypt/sign) */
		__u32		in2_len;	/* 2nd input data size (verify) */
	};
	__u32		__spare[7];
};

#define KEYCTL_MOVE_EXCL	0x00000001 /* Do not displace from the to-keyring */

/*
 * Capabilities flags.  The capabilities list is an array of 8-bit integers;
 * each integer can carry up to 8 flags.
 */
#define KEYCTL_CAPS0_CAPABILITIES	0x01 /* KEYCTL_CAPABILITIES supported */
#define KEYCTL_CAPS0_PERSISTENT_KEYRINGS 0x02 /* Persistent keyrings enabled */
#define KEYCTL_CAPS0_DIFFIE_HELLMAN	0x04 /* Diffie-Hellman computation enabled */
#define KEYCTL_CAPS0_PUBLIC_KEY		0x08 /* Public key ops enabled */
#define KEYCTL_CAPS0_BIG_KEY		0x10 /* big_key-type enabled */
#define KEYCTL_CAPS0_INVALIDATE		0x20 /* KEYCTL_INVALIDATE supported */
#define KEYCTL_CAPS0_RESTRICT_KEYRING	0x40 /* KEYCTL_RESTRICT_KEYRING supported */
#define KEYCTL_CAPS0_MOVE		0x80 /* KEYCTL_MOVE supported */
#define KEYCTL_CAPS1_NS_KEYRING_NAME	0x01 /* Keyring names are per-user_namespace */
#define KEYCTL_CAPS1_NS_KEY_TAG		0x02 /* Key indexing can include a namespace tag */
#define KEYCTL_CAPS1_ACL_ALTERABLE	0x04 /* Keys have internal ACL that can be altered */

#endif /*  _LINUX_KEYCTL_H */
