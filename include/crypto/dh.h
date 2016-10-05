/*
 * Diffie-Hellman secret to be used with kpp API along with helper functions
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
#ifndef _CRYPTO_DH_
#define _CRYPTO_DH_

struct dh {
	void *key;
	void *p;
	void *g;
	unsigned int key_size;
	unsigned int p_size;
	unsigned int g_size;
};

int crypto_dh_key_len(const struct dh *params);
int crypto_dh_encode_key(char *buf, unsigned int len, const struct dh *params);
int crypto_dh_decode_key(const char *buf, unsigned int len, struct dh *params);

#endif
