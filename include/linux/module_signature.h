/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Module signature handling.
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_MODULE_SIGNATURE_H
#define _LINUX_MODULE_SIGNATURE_H

#include <linux/types.h>

/* In stripped ARM and x86-64 modules, ~ is surprisingly rare. */
#define MODULE_SIGNATURE_MARKER "~Module signature appended~\n"

enum module_signature_type {
	MODULE_SIGNATURE_TYPE_PKCS7 = 2,	/* Signature in PKCS#7 message */
};

/*
 * Module signature information block.
 *
 * The constituents of the signature section are, in order:
 *
 *	- Signer's name
 *	- Key identifier
 *	- Signature data
 *	- Information block
 */
struct module_signature {
	u8	algo;		/* Public-key crypto algorithm [0] */
	u8	hash;		/* Digest algorithm [0] */
	u8	id_type;	/* Key identifier type [enum module_signature_type] */
	u8	signer_len;	/* Length of signer's name [0] */
	u8	key_id_len;	/* Length of key identifier [0] */
	u8	__pad[3];
	__be32	sig_len;	/* Length of signature data */
};

int mod_check_sig(const struct module_signature *ms, size_t file_len,
		  const char *name);

#endif /* _LINUX_MODULE_SIGNATURE_H */
