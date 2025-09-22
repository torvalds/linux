/* $OpenBSD: asn1_lib.c,v 1.54 2022/05/05 19:18:56 jsing Exp $ */
/*
 * Copyright (c) 2021 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <limits.h>
#include <stdlib.h>

#include "bytestring.h"

int
asn1_get_identifier_cbs(CBS *cbs, int der_mode, uint8_t *out_class,
    int *out_constructed, uint32_t *out_tag_number)
{
	uint8_t tag_class, tag_val;
	int tag_constructed;
	uint32_t tag_number;

	/*
	 * Decode ASN.1 identifier octets - see ITU-T X.690 section 8.1.2.
	 */

	*out_class = 0;
	*out_constructed = 0;
	*out_tag_number = 0;

	if (!CBS_get_u8(cbs, &tag_val))
		return 0;

	/*
	 * ASN.1 tag class, encoding (primitive or constructed) and tag number
	 * are encoded in one or more identifier octets - the first octet
	 * contains the 2 bit tag class, the 1 bit encoding type and 5 bits
	 * of tag number.
	 *
	 * For tag numbers larger than 30 (0x1e) the 5 bit tag number in the
	 * first octet is set to all ones (0x1f) - the tag number is then
	 * encoded in subsequent octets - each of which have a one bit
	 * continuation flag and 7 bits of tag number in big-endian form.
	 * The encoding should not contain leading zeros but can for BER.
	 */
	tag_class = (tag_val >> 6) & 0x3;
	tag_constructed = (tag_val >> 5) & 0x1;
	tag_number = tag_val & 0x1f;

	/* Long form. */
	if (tag_number == 0x1f) {
		tag_number = 0;
		do {
			if (!CBS_get_u8(cbs, &tag_val))
				return 0;
			if (der_mode && tag_number == 0 && tag_val == 0x80)
				return 0;
			if (tag_number > (UINT32_MAX >> 7))
				return 0;
			tag_number = tag_number << 7 | (tag_val & 0x7f);
		} while ((tag_val & 0x80) != 0);
	}

	*out_class = tag_class;
	*out_constructed = tag_constructed;
	*out_tag_number = tag_number;

	return 1;
}

int
asn1_get_length_cbs(CBS *cbs, int der_mode, int *out_indefinite,
    size_t *out_length)
{
	uint8_t len_bytes;
	size_t length;
	uint8_t val;

	/*
	 * Decode ASN.1 length octets - see ITU-T X.690 section 8.1.3.
	 */

	*out_length = 0;
	*out_indefinite = 0;

	if (!CBS_get_u8(cbs, &val))
		return 0;

	/*
	 * Short form - length is encoded in the lower 7 bits of a single byte.
	 */
	if (val < 0x80) {
		*out_length = val;
		return 1;
	}

	/*
	 * Indefinite length - content continues until an End of Content (EOC)
	 * marker is reached. Must be used with constructed encoding.
	 */
	if (val == 0x80) {
		*out_indefinite = 1;
		return 1;
	}

	/*
	 * Long form - the lower 7 bits of the first byte specifies the number
	 * of bytes used to encode the length, the following bytes specify the
	 * length in big-endian form. The encoding should not contain leading
	 * zeros but can for BER. A length value of 0x7f is invalid.
	 */
	if ((len_bytes = val & 0x7f) == 0x7f)
		return 0;

	length = 0;

	while (len_bytes-- > 0) {
		if (!CBS_get_u8(cbs, &val))
			return 0;
		if (der_mode && length == 0 && val == 0)
			return 0;
		if (length > (SIZE_MAX >> 8))
			return 0;
		length = (length << 8) | val;
	}

	*out_length = length;

	return 1;
}

int
asn1_get_object_cbs(CBS *cbs, int der_mode, uint8_t *out_tag_class,
    int *out_constructed, uint32_t *out_tag_number, int *out_indefinite,
    size_t *out_length)
{
	int constructed, indefinite;
	uint32_t tag_number;
	uint8_t tag_class;
	size_t length;

	*out_tag_class = 0;
	*out_constructed = 0;
	*out_tag_number = 0;
	*out_indefinite = 0;
	*out_length = 0;

	if (!asn1_get_identifier_cbs(cbs, der_mode, &tag_class, &constructed,
	    &tag_number))
		return 0;
	if (!asn1_get_length_cbs(cbs, der_mode, &indefinite, &length))
		return 0;

	/* Indefinite length can only be used with constructed encoding. */
	if (indefinite && !constructed)
		return 0;

	*out_tag_class = tag_class;
	*out_constructed = constructed;
	*out_tag_number = tag_number;
	*out_indefinite = indefinite;
	*out_length = length;

	return 1;
}

int
asn1_get_primitive(CBS *cbs, int der_mode, uint32_t *out_tag_number,
    CBS *out_content)
{
	int constructed, indefinite;
	uint32_t tag_number;
	uint8_t tag_class;
	size_t length;

	*out_tag_number = 0;

	CBS_init(out_content, NULL, 0);

	if (!asn1_get_identifier_cbs(cbs, der_mode, &tag_class, &constructed,
	    &tag_number))
		return 0;
	if (!asn1_get_length_cbs(cbs, der_mode, &indefinite, &length))
		return 0;

	/* A primitive is not constructed and has a definite length. */
	if (constructed || indefinite)
		return 0;

	if (!CBS_get_bytes(cbs, out_content, length))
		return 0;

	*out_tag_number = tag_number;

	return 1;
}
