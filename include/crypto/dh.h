/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Diffie-Hellman secret to be used with kpp API along with helper functions
 *
 * Copyright (c) 2016, Intel Corporation
 * Authors: Salvatore Benedetto <salvatore.benedetto@intel.com>
 */
#ifndef _CRYPTO_DH_
#define _CRYPTO_DH_

/**
 * DOC: DH Helper Functions
 *
 * To use DH with the KPP cipher API, the following data structure and
 * functions should be used.
 *
 * To use DH with KPP, the following functions should be used to operate on
 * a DH private key. The packet private key that can be set with
 * the KPP API function call of crypto_kpp_set_secret.
 */

/**
 * struct dh - define a DH private key
 *
 * @key:	Private DH key
 * @p:		Diffie-Hellman parameter P
 * @g:		Diffie-Hellman generator G
 * @key_size:	Size of the private DH key
 * @p_size:	Size of DH parameter P
 * @g_size:	Size of DH generator G
 */
struct dh {
	const void *key;
	const void *p;
	const void *g;
	unsigned int key_size;
	unsigned int p_size;
	unsigned int g_size;
};

/**
 * crypto_dh_key_len() - Obtain the size of the private DH key
 * @params:	private DH key
 *
 * This function returns the packet DH key size. A caller can use that
 * with the provided DH private key reference to obtain the required
 * memory size to hold a packet key.
 *
 * Return: size of the key in bytes
 */
unsigned int crypto_dh_key_len(const struct dh *params);

/**
 * crypto_dh_encode_key() - encode the private key
 * @buf:	Buffer allocated by the caller to hold the packet DH
 *		private key. The buffer should be at least crypto_dh_key_len
 *		bytes in size.
 * @len:	Length of the packet private key buffer
 * @params:	Buffer with the caller-specified private key
 *
 * The DH implementations operate on a packet representation of the private
 * key.
 *
 * Return:	-EINVAL if buffer has insufficient size, 0 on success
 */
int crypto_dh_encode_key(char *buf, unsigned int len, const struct dh *params);

/**
 * crypto_dh_decode_key() - decode a private key
 * @buf:	Buffer holding a packet key that should be decoded
 * @len:	Length of the packet private key buffer
 * @params:	Buffer allocated by the caller that is filled with the
 *		unpacked DH private key.
 *
 * The unpacking obtains the private key by pointing @p to the correct location
 * in @buf. Thus, both pointers refer to the same memory.
 *
 * Return:	-EINVAL if buffer has insufficient size, 0 on success
 */
int crypto_dh_decode_key(const char *buf, unsigned int len, struct dh *params);

#endif
