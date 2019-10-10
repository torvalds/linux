/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ecryptfs_format.h: helper functions for the encrypted key type
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

#ifndef __KEYS_ECRYPTFS_H
#define __KEYS_ECRYPTFS_H

#include <linux/ecryptfs.h>

#define PGP_DIGEST_ALGO_SHA512   10

u8 *ecryptfs_get_auth_tok_key(struct ecryptfs_auth_tok *auth_tok);
void ecryptfs_get_versions(int *major, int *minor, int *file_version);
int ecryptfs_fill_auth_tok(struct ecryptfs_auth_tok *auth_tok,
			   const char *key_desc);

#endif /* __KEYS_ECRYPTFS_H */
