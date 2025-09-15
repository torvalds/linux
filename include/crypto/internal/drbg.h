/* SPDX-License-Identifier: GPL-2.0 */

/*
 * NIST SP800-90A DRBG derivation function
 *
 * Copyright (C) 2014, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _INTERNAL_DRBG_H
#define _INTERNAL_DRBG_H

/*
 * Convert an integer into a byte representation of this integer.
 * The byte representation is big-endian
 *
 * @val value to be converted
 * @buf buffer holding the converted integer -- caller must ensure that
 *      buffer size is at least 32 bit
 */
static inline void drbg_cpu_to_be32(__u32 val, unsigned char *buf)
{
	struct s {
		__be32 conv;
	};
	struct s *conversion = (struct s *)buf;

	conversion->conv = cpu_to_be32(val);
}

/*
 * Concatenation Helper and string operation helper
 *
 * SP800-90A requires the concatenation of different data. To avoid copying
 * buffers around or allocate additional memory, the following data structure
 * is used to point to the original memory with its size. In addition, it
 * is used to build a linked list. The linked list defines the concatenation
 * of individual buffers. The order of memory block referenced in that
 * linked list determines the order of concatenation.
 */
struct drbg_string {
	const unsigned char *buf;
	size_t len;
	struct list_head list;
};

static inline void drbg_string_fill(struct drbg_string *string,
				    const unsigned char *buf, size_t len)
{
	string->buf = buf;
	string->len = len;
	INIT_LIST_HEAD(&string->list);
}

#endif //_INTERNAL_DRBG_H
