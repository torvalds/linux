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

#endif /*  _LINUX_KEYCTL_H */
