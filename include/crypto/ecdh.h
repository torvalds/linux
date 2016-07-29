/*
 * ECDH params to be used with kpp API
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _CRYPTO_ECDH_
#define _CRYPTO_ECDH_

/* Curves IDs */
#define ECC_CURVE_NIST_P192	0x0001
#define ECC_CURVE_NIST_P256	0x0002

struct ecdh {
	unsigned short curve_id;
	char *key;
	unsigned short key_size;
};

int crypto_ecdh_key_len(const struct ecdh *params);
int crypto_ecdh_encode_key(char *buf, unsigned int len, const struct ecdh *p);
int crypto_ecdh_decode_key(const char *buf, unsigned int len, struct ecdh *p);

#endif
