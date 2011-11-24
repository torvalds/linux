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

#ifndef RC4_H
#define RC4_H

void rc4_skip(const u8 *key, size_t keylen, size_t skip,
	      u8 *data, size_t data_len);
void rc4(u8 *buf, size_t len, const u8 *key, size_t key_len);

#endif /* RC4_H */
