// SPDX-License-Identifier: GPL-2.0-only
/*
 * ecryptfs_format.c: helper functions for the encrypted key type
 *
 * Copyright (C) 2006 International Business Machines Corp.
 * Copyright (C) 2010 Politecnico di Torino, Italy
 *                    TORSEC group -- http://security.polito.it
 *
 * Authors:
 * Michael A. Halcrow <mahalcro@us.ibm.com>
 * Tyler Hicks <tyhicks@ou.edu>
 * Roberto Sassu <roberto.sassu@polito.it>
 */

#include <linux/export.h>
#include <linux/string.h>
#include "ecryptfs_format.h"

u8 *ecryptfs_get_auth_tok_key(struct ecryptfs_auth_tok *auth_tok)
{
	return auth_tok->token.password.session_key_encryption_key;
}
EXPORT_SYMBOL(ecryptfs_get_auth_tok_key);

/*
 * ecryptfs_get_versions()
 *
 * Source code taken from the software 'ecryptfs-utils' version 83.
 *
 */
void ecryptfs_get_versions(int *major, int *minor, int *file_version)
{
	*major = ECRYPTFS_VERSION_MAJOR;
	*minor = ECRYPTFS_VERSION_MINOR;
	if (file_version)
		*file_version = ECRYPTFS_SUPPORTED_FILE_VERSION;
}
EXPORT_SYMBOL(ecryptfs_get_versions);

/*
 * ecryptfs_fill_auth_tok - fill the ecryptfs_auth_tok structure
 *
 * Fill the ecryptfs_auth_tok structure with required ecryptfs data.
 * The source code is inspired to the original function generate_payload()
 * shipped with the software 'ecryptfs-utils' version 83.
 *
 */
int ecryptfs_fill_auth_tok(struct ecryptfs_auth_tok *auth_tok,
			   const char *key_desc)
{
	int major, minor;

	ecryptfs_get_versions(&major, &minor, NULL);
	auth_tok->version = (((uint16_t)(major << 8) & 0xFF00)
			     | ((uint16_t)minor & 0x00FF));
	auth_tok->token_type = ECRYPTFS_PASSWORD;
	strncpy((char *)auth_tok->token.password.signature, key_desc,
		ECRYPTFS_PASSWORD_SIG_SIZE);
	auth_tok->token.password.session_key_encryption_key_bytes =
		ECRYPTFS_MAX_KEY_BYTES;
	/*
	 * Removed auth_tok->token.password.salt and
	 * auth_tok->token.password.session_key_encryption_key
	 * initialization from the original code
	 */
	/* TODO: Make the hash parameterizable via policy */
	auth_tok->token.password.flags |=
		ECRYPTFS_SESSION_KEY_ENCRYPTION_KEY_SET;
	/* The kernel code will encrypt the session key. */
	auth_tok->session_key.encrypted_key[0] = 0;
	auth_tok->session_key.encrypted_key_size = 0;
	/* Default; subject to change by kernel eCryptfs */
	auth_tok->token.password.hash_algo = PGP_DIGEST_ALGO_SHA512;
	auth_tok->token.password.flags &= ~(ECRYPTFS_PERSISTENT_PASSWORD);
	return 0;
}
EXPORT_SYMBOL(ecryptfs_fill_auth_tok);
