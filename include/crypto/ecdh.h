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

/**
 * DOC: ECDH Helper Functions
 *
 * To use ECDH with the KPP cipher API, the following data structure and
 * functions should be used.
 *
 * The ECC curves known to the ECDH implementation are specified in this
 * header file.
 *
 * To use ECDH with KPP, the following functions should be used to operate on
 * an ECDH private key. The packet private key that can be set with
 * the KPP API function call of crypto_kpp_set_secret.
 */

/* Curves IDs */
#define ECC_CURVE_NIST_P192	0x0001
#define ECC_CURVE_NIST_P256	0x0002

/**
 * struct ecdh - define an ECDH private key
 *
 * @curve_id:	ECC curve the key is based on.
 * @key:	Private ECDH key
 * @key_size:	Size of the private ECDH key
 */
struct ecdh {
	unsigned short curve_id;
	char *key;
	unsigned short key_size;
};

/**
 * crypto_ecdh_key_len() - Obtain the size of the private ECDH key
 * @params:	private ECDH key
 *
 * This function returns the packet ECDH key size. A caller can use that
 * with the provided ECDH private key reference to obtain the required
 * memory size to hold a packet key.
 *
 * Return: size of the key in bytes
 */
int crypto_ecdh_key_len(const struct ecdh *params);

/**
 * crypto_ecdh_encode_key() - encode the private key
 * @buf:	Buffer allocated by the caller to hold the packet ECDH
 *		private key. The buffer should be at least crypto_ecdh_key_len
 *		bytes in size.
 * @len:	Length of the packet private key buffer
 * @p:		Buffer with the caller-specified private key
 *
 * The ECDH implementations operate on a packet representation of the private
 * key.
 *
 * Return:	-EINVAL if buffer has insufficient size, 0 on success
 */
int crypto_ecdh_encode_key(char *buf, unsigned int len, const struct ecdh *p);

/**
 * crypto_ecdh_decode_key() - decode a private key
 * @buf:	Buffer holding a packet key that should be decoded
 * @len:	Length of the packet private key buffer
 * @p:		Buffer allocated by the caller that is filled with the
 *		unpacked ECDH private key.
 *
 * The unpacking obtains the private key by pointing @p to the correct location
 * in @buf. Thus, both pointers refer to the same memory.
 *
 * Return:	-EINVAL if buffer has insufficient size, 0 on success
 */
int crypto_ecdh_decode_key(const char *buf, unsigned int len, struct ecdh *p);

#endif
