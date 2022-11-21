/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Asymmetric Public-key cryptography key type interface
 *
 * See Documentation/crypto/asymmetric-keys.rst
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _KEYS_ASYMMETRIC_TYPE_H
#define _KEYS_ASYMMETRIC_TYPE_H

#include <linux/key-type.h>
#include <linux/verification.h>

extern struct key_type key_type_asymmetric;

/*
 * The key payload is four words.  The asymmetric-type key uses them as
 * follows:
 */
enum asymmetric_payload_bits {
	asym_crypto,		/* The data representing the key */
	asym_subtype,		/* Pointer to an asymmetric_key_subtype struct */
	asym_key_ids,		/* Pointer to an asymmetric_key_ids struct */
	asym_auth		/* The key's authorisation (signature, parent key ID) */
};

/*
 * Identifiers for an asymmetric key ID.  We have three ways of looking up a
 * key derived from an X.509 certificate:
 *
 * (1) Serial Number & Issuer.  Non-optional.  This is the only valid way to
 *     map a PKCS#7 signature to an X.509 certificate.
 *
 * (2) Issuer & Subject Unique IDs.  Optional.  These were the original way to
 *     match X.509 certificates, but have fallen into disuse in favour of (3).
 *
 * (3) Auth & Subject Key Identifiers.  Optional.  SKIDs are only provided on
 *     CA keys that are intended to sign other keys, so don't appear in end
 *     user certificates unless forced.
 *
 * We could also support an PGP key identifier, which is just a SHA1 sum of the
 * public key and certain parameters, but since we don't support PGP keys at
 * the moment, we shall ignore those.
 *
 * What we actually do is provide a place where binary identifiers can be
 * stashed and then compare against them when checking for an id match.
 */
struct asymmetric_key_id {
	unsigned short	len;
	unsigned char	data[];
};

struct asymmetric_key_ids {
	void		*id[3];
};

extern bool asymmetric_key_id_same(const struct asymmetric_key_id *kid1,
				   const struct asymmetric_key_id *kid2);

extern bool asymmetric_key_id_partial(const struct asymmetric_key_id *kid1,
				      const struct asymmetric_key_id *kid2);

extern struct asymmetric_key_id *asymmetric_key_generate_id(const void *val_1,
							    size_t len_1,
							    const void *val_2,
							    size_t len_2);
static inline
const struct asymmetric_key_ids *asymmetric_key_ids(const struct key *key)
{
	return key->payload.data[asym_key_ids];
}

static inline
const struct public_key *asymmetric_key_public_key(const struct key *key)
{
	return key->payload.data[asym_crypto];
}

extern struct key *find_asymmetric_key(struct key *keyring,
				       const struct asymmetric_key_id *id_0,
				       const struct asymmetric_key_id *id_1,
				       const struct asymmetric_key_id *id_2,
				       bool partial);

/*
 * The payload is at the discretion of the subtype.
 */

#endif /* _KEYS_ASYMMETRIC_TYPE_H */
