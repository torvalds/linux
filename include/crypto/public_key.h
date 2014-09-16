/* Asymmetric public-key algorithm definitions
 *
 * See Documentation/crypto/asymmetric-keys.txt
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_PUBLIC_KEY_H
#define _LINUX_PUBLIC_KEY_H

#include <linux/mpi.h>
#include <keys/asymmetric-type.h>
#include <crypto/hash_info.h>

enum pkey_algo {
	PKEY_ALGO_DSA,
	PKEY_ALGO_RSA,
	PKEY_ALGO__LAST
};

extern const char *const pkey_algo_name[PKEY_ALGO__LAST];
extern const struct public_key_algorithm *pkey_algo[PKEY_ALGO__LAST];

/* asymmetric key implementation supports only up to SHA224 */
#define PKEY_HASH__LAST		(HASH_ALGO_SHA224 + 1)

enum pkey_id_type {
	PKEY_ID_PGP,		/* OpenPGP generated key ID */
	PKEY_ID_X509,		/* X.509 arbitrary subjectKeyIdentifier */
	PKEY_ID_TYPE__LAST
};

extern const char *const pkey_id_type_name[PKEY_ID_TYPE__LAST];

/*
 * Cryptographic data for the public-key subtype of the asymmetric key type.
 *
 * Note that this may include private part of the key as well as the public
 * part.
 */
struct public_key {
	const struct public_key_algorithm *algo;
	u8	capabilities;
#define PKEY_CAN_ENCRYPT	0x01
#define PKEY_CAN_DECRYPT	0x02
#define PKEY_CAN_SIGN		0x04
#define PKEY_CAN_VERIFY		0x08
	enum pkey_algo pkey_algo : 8;
	enum pkey_id_type id_type : 8;
	union {
		MPI	mpi[5];
		struct {
			MPI	p;	/* DSA prime */
			MPI	q;	/* DSA group order */
			MPI	g;	/* DSA group generator */
			MPI	y;	/* DSA public-key value = g^x mod p */
			MPI	x;	/* DSA secret exponent (if present) */
		} dsa;
		struct {
			MPI	n;	/* RSA public modulus */
			MPI	e;	/* RSA public encryption exponent */
			MPI	d;	/* RSA secret encryption exponent (if present) */
			MPI	p;	/* RSA secret prime (if present) */
			MPI	q;	/* RSA secret prime (if present) */
		} rsa;
	};
};

extern void public_key_destroy(void *payload);

/*
 * Public key cryptography signature data
 */
struct public_key_signature {
	u8 *digest;
	u8 digest_size;			/* Number of bytes in digest */
	u8 nr_mpi;			/* Occupancy of mpi[] */
	enum pkey_algo pkey_algo : 8;
	enum hash_algo pkey_hash_algo : 8;
	union {
		MPI mpi[2];
		struct {
			MPI s;		/* m^d mod n */
		} rsa;
		struct {
			MPI r;
			MPI s;
		} dsa;
	};
};

struct key;
extern int verify_signature(const struct key *key,
			    const struct public_key_signature *sig);

struct asymmetric_key_id;
extern struct key *x509_request_asymmetric_key(struct key *keyring,
					       const struct asymmetric_key_id *kid);

#endif /* _LINUX_PUBLIC_KEY_H */
