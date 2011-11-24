/*
 * RC4 stream cipher
 * Copyright (c) 2002-2005, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "rc4.h"

#define S_SWAP(a,b) do { u8 t = S[a]; S[a] = S[b]; S[b] = t; } while(0)

/**
 * rc4 - XOR RC4 stream to given data with skip-stream-start
 * @key: RC4 key
 * @keylen: RC4 key length
 * @skip: number of bytes to skip from the beginning of the RC4 stream
 * @data: data to be XOR'ed with RC4 stream
 * @data_len: buf length
 *
 * Generate RC4 pseudo random stream for the given key, skip beginning of the
 * stream, and XOR the end result with the data buffer to perform RC4
 * encryption/decryption.
 */
void rc4_skip(const u8 *key, size_t keylen, size_t skip,
	      u8 *data, size_t data_len)
{
	u32 i, j, k;
	u8 S[256], *pos;
	size_t kpos;

	/* Setup RC4 state */
	for (i = 0; i < 256; i++)
		S[i] = i;
	j = 0;
	kpos = 0;
	for (i = 0; i < 256; i++) {
		j = (j + S[i] + key[kpos]) & 0xff;
		kpos++;
		if (kpos >= keylen)
			kpos = 0;
		S_SWAP(i, j);
	}

	/* Skip the start of the stream */
	i = j = 0;
	for (k = 0; k < skip; k++) {
		i = (i + 1) & 0xff;
		j = (j + S[i]) & 0xff;
		S_SWAP(i, j);
	}

	/* Apply RC4 to data */
	pos = data;
	for (k = 0; k < data_len; k++) {
		i = (i + 1) & 0xff;
		j = (j + S[i]) & 0xff;
		S_SWAP(i, j);
		*pos++ ^= S[(S[i] + S[j]) & 0xff];
	}
}


/**
 * rc4 - XOR RC4 stream to given data
 * @buf: data to be XOR'ed with RC4 stream
 * @len: buf length
 * @key: RC4 key
 * @key_len: RC4 key length
 *
 * Generate RC4 pseudo random stream for the given key and XOR this with the
 * data buffer to perform RC4 encryption/decryption.
 */
void rc4(u8 *buf, size_t len, const u8 *key, size_t key_len)
{
	rc4_skip(key, key_len, 0, buf, len);
}
