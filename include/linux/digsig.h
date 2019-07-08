/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2011 Nokia Corporation
 * Copyright (C) 2011 Intel Corporation
 *
 * Author:
 * Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *                 <dmitry.kasatkin@intel.com>
 */

#ifndef _DIGSIG_H
#define _DIGSIG_H

#include <linux/key.h>

enum pubkey_algo {
	PUBKEY_ALGO_RSA,
	PUBKEY_ALGO_MAX,
};

enum digest_algo {
	DIGEST_ALGO_SHA1,
	DIGEST_ALGO_SHA256,
	DIGEST_ALGO_MAX
};

struct pubkey_hdr {
	uint8_t		version;	/* key format version */
	uint32_t	timestamp;	/* key made, always 0 for now */
	uint8_t		algo;
	uint8_t		nmpi;
	char		mpi[0];
} __packed;

struct signature_hdr {
	uint8_t		version;	/* signature format version */
	uint32_t	timestamp;	/* signature made */
	uint8_t		algo;
	uint8_t		hash;
	uint8_t		keyid[8];
	uint8_t		nmpi;
	char		mpi[0];
} __packed;

#if defined(CONFIG_SIGNATURE) || defined(CONFIG_SIGNATURE_MODULE)

int digsig_verify(struct key *keyring, const char *sig, int siglen,
					const char *digest, int digestlen);

#else

static inline int digsig_verify(struct key *keyring, const char *sig,
				int siglen, const char *digest, int digestlen)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_SIGNATURE */

#endif /* _DIGSIG_H */
