/* Asymmetric Public-key cryptography key type interface
 *
 * See Documentation/security/asymmetric-keys.txt
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _KEYS_ASYMMETRIC_TYPE_H
#define _KEYS_ASYMMETRIC_TYPE_H

#include <linux/key-type.h>

extern struct key_type key_type_asymmetric;

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
	void		*id[2];
};

extern bool asymmetric_key_id_same(const struct asymmetric_key_id *kid1,
				   const struct asymmetric_key_id *kid2);

extern struct asymmetric_key_id *asymmetric_key_generate_id(const void *val_1,
							    size_t len_1,
							    const void *val_2,
							    size_t len_2);

/*
 * The payload is at the discretion of the subtype.
 */

#endif /* _KEYS_ASYMMETRIC_TYPE_H */
