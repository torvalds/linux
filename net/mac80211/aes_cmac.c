/*
 * AES-128-CMAC with TLen 16 for IEEE 802.11w BIP
 * Copyright 2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>

#include <net/mac80211.h>
#include "key.h"
#include "aes_cmac.h"

#define AES_BLOCK_SIZE 16
#define AES_CMAC_KEY_LEN 16
#define CMAC_TLEN 8 /* CMAC TLen = 64 bits (8 octets) */
#define AAD_LEN 20


static void gf_mulx(u8 *pad)
{
	int i, carry;

	carry = pad[0] & 0x80;
	for (i = 0; i < AES_BLOCK_SIZE - 1; i++)
		pad[i] = (pad[i] << 1) | (pad[i + 1] >> 7);
	pad[AES_BLOCK_SIZE - 1] <<= 1;
	if (carry)
		pad[AES_BLOCK_SIZE - 1] ^= 0x87;
}


static void aes_128_cmac_vector(struct crypto_cipher *tfm, size_t num_elem,
				const u8 *addr[], const size_t *len, u8 *mac)
{
	u8 scratch[2 * AES_BLOCK_SIZE];
	u8 *cbc, *pad;
	const u8 *pos, *end;
	size_t i, e, left, total_len;

	cbc = scratch;
	pad = scratch + AES_BLOCK_SIZE;

	memset(cbc, 0, AES_BLOCK_SIZE);

	total_len = 0;
	for (e = 0; e < num_elem; e++)
		total_len += len[e];
	left = total_len;

	e = 0;
	pos = addr[0];
	end = pos + len[0];

	while (left >= AES_BLOCK_SIZE) {
		for (i = 0; i < AES_BLOCK_SIZE; i++) {
			cbc[i] ^= *pos++;
			if (pos >= end) {
				e++;
				pos = addr[e];
				end = pos + len[e];
			}
		}
		if (left > AES_BLOCK_SIZE)
			crypto_cipher_encrypt_one(tfm, cbc, cbc);
		left -= AES_BLOCK_SIZE;
	}

	memset(pad, 0, AES_BLOCK_SIZE);
	crypto_cipher_encrypt_one(tfm, pad, pad);
	gf_mulx(pad);

	if (left || total_len == 0) {
		for (i = 0; i < left; i++) {
			cbc[i] ^= *pos++;
			if (pos >= end) {
				e++;
				pos = addr[e];
				end = pos + len[e];
			}
		}
		cbc[left] ^= 0x80;
		gf_mulx(pad);
	}

	for (i = 0; i < AES_BLOCK_SIZE; i++)
		pad[i] ^= cbc[i];
	crypto_cipher_encrypt_one(tfm, pad, pad);
	memcpy(mac, pad, CMAC_TLEN);
}


void ieee80211_aes_cmac(struct crypto_cipher *tfm, const u8 *aad,
			const u8 *data, size_t data_len, u8 *mic)
{
	const u8 *addr[3];
	size_t len[3];
	u8 zero[CMAC_TLEN];

	memset(zero, 0, CMAC_TLEN);
	addr[0] = aad;
	len[0] = AAD_LEN;
	addr[1] = data;
	len[1] = data_len - CMAC_TLEN;
	addr[2] = zero;
	len[2] = CMAC_TLEN;

	aes_128_cmac_vector(tfm, 3, addr, len, mic);
}


struct crypto_cipher * ieee80211_aes_cmac_key_setup(const u8 key[])
{
	struct crypto_cipher *tfm;

	tfm = crypto_alloc_cipher("aes", 0, CRYPTO_ALG_ASYNC);
	if (!IS_ERR(tfm))
		crypto_cipher_setkey(tfm, key, AES_CMAC_KEY_LEN);

	return tfm;
}


void ieee80211_aes_cmac_key_free(struct crypto_cipher *tfm)
{
	crypto_free_cipher(tfm);
}
