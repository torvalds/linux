/* keyctl kernel bits
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef __LINUX_KEYCTL_H
#define __LINUX_KEYCTL_H

#include <uapi/linux/keyctl.h>

struct kernel_pkey_query {
	__u32		supported_ops;	/* Which ops are supported */
	__u32		key_size;	/* Size of the key in bits */
	__u16		max_data_size;	/* Maximum size of raw data to sign in bytes */
	__u16		max_sig_size;	/* Maximum size of signature in bytes */
	__u16		max_enc_size;	/* Maximum size of encrypted blob in bytes */
	__u16		max_dec_size;	/* Maximum size of decrypted blob in bytes */
};

enum kernel_pkey_operation {
	kernel_pkey_encrypt,
	kernel_pkey_decrypt,
	kernel_pkey_sign,
	kernel_pkey_verify,
};

struct kernel_pkey_params {
	struct key	*key;
	const char	*encoding;	/* Encoding (eg. "oaep" or NULL for raw) */
	const char	*hash_algo;	/* Digest algorithm used (eg. "sha1") or NULL if N/A */
	char		*info;		/* Modified info string to be released later */
	__u32		in_len;		/* Input data size */
	union {
		__u32	out_len;	/* Output buffer size (enc/dec/sign) */
		__u32	in2_len;	/* 2nd input data size (verify) */
	};
	enum kernel_pkey_operation op : 8;
};

#endif /* __LINUX_KEYCTL_H */
