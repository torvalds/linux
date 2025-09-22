/*	$OpenBSD: bs_cbs.c,v 1.3 2024/05/25 15:12:47 tb Exp $	*/
/*
 * Copyright (c) 2014, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bytestring.h"

void
CBS_init(CBS *cbs, const uint8_t *data, size_t len)
{
	cbs->data = data;
	cbs->initial_len = len;
	cbs->len = len;
}

void
CBS_dup(const CBS *cbs, CBS *out)
{
	CBS_init(out, CBS_data(cbs), CBS_len(cbs));
	out->initial_len = cbs->initial_len;
}

static int
cbs_get(CBS *cbs, const uint8_t **p, size_t n)
{
	if (cbs->len < n)
		return 0;

	*p = cbs->data;
	cbs->data += n;
	cbs->len -= n;
	return 1;
}

static int
cbs_peek(CBS *cbs, const uint8_t **p, size_t n)
{
	if (cbs->len < n)
		return 0;

	*p = cbs->data;
	return 1;
}

size_t
CBS_offset(const CBS *cbs)
{
	return cbs->initial_len - cbs->len;
}

int
CBS_skip(CBS *cbs, size_t len)
{
	const uint8_t *dummy;
	return cbs_get(cbs, &dummy, len);
}

const uint8_t *
CBS_data(const CBS *cbs)
{
	return cbs->data;
}

size_t
CBS_len(const CBS *cbs)
{
	return cbs->len;
}

int
CBS_stow(const CBS *cbs, uint8_t **out_ptr, size_t *out_len)
{
	free(*out_ptr);
	*out_ptr = NULL;
	*out_len = 0;

	if (cbs->len == 0)
		return 1;

	if ((*out_ptr = malloc(cbs->len)) == NULL)
		return 0;

	memcpy(*out_ptr, cbs->data, cbs->len);

	*out_len = cbs->len;
	return 1;
}

int
CBS_strdup(const CBS *cbs, char **out_ptr)
{
	free(*out_ptr);
	*out_ptr = NULL;

	if (CBS_contains_zero_byte(cbs))
		return 0;

	*out_ptr = strndup((const char *)cbs->data, cbs->len);
	return (*out_ptr != NULL);
}

int
CBS_write_bytes(const CBS *cbs, uint8_t *dst, size_t dst_len, size_t *copied)
{
	if (dst_len < cbs->len)
		return 0;

	memmove(dst, cbs->data, cbs->len);

	if (copied != NULL)
		*copied = cbs->len;

	return 1;
}

int
CBS_contains_zero_byte(const CBS *cbs)
{
	return memchr(cbs->data, 0, cbs->len) != NULL;
}

int
CBS_mem_equal(const CBS *cbs, const uint8_t *data, size_t len)
{
	if (len != cbs->len)
		return 0;

	return timingsafe_memcmp(cbs->data, data, len) == 0;
}

static int
cbs_get_u(CBS *cbs, uint32_t *out, size_t len)
{
	uint32_t result = 0;
	size_t i;
	const uint8_t *data;

	if (len < 1 || len > 4)
		return 0;

	if (!cbs_get(cbs, &data, len))
		return 0;

	for (i = 0; i < len; i++) {
		result <<= 8;
		result |= data[i];
	}
	*out = result;
	return 1;
}

int
CBS_get_u8(CBS *cbs, uint8_t *out)
{
	const uint8_t *v;

	if (!cbs_get(cbs, &v, 1))
		return 0;

	*out = *v;
	return 1;
}

int
CBS_get_u16(CBS *cbs, uint16_t *out)
{
	uint32_t v;

	if (!cbs_get_u(cbs, &v, 2))
		return 0;

	*out = v;
	return 1;
}

int
CBS_get_u24(CBS *cbs, uint32_t *out)
{
	return cbs_get_u(cbs, out, 3);
}

int
CBS_get_u32(CBS *cbs, uint32_t *out)
{
	return cbs_get_u(cbs, out, 4);
}

int
CBS_get_u64(CBS *cbs, uint64_t *out)
{
	uint32_t a, b;

	if (cbs->len < 8)
		return 0;

	if (!CBS_get_u32(cbs, &a))
		return 0;
	if (!CBS_get_u32(cbs, &b))
		return 0;

	*out = (uint64_t)a << 32 | b;
	return 1;
}

int
CBS_get_last_u8(CBS *cbs, uint8_t *out)
{
	if (cbs->len == 0)
		return 0;

	*out = cbs->data[cbs->len - 1];
	cbs->len--;
	return 1;
}

int
CBS_get_bytes(CBS *cbs, CBS *out, size_t len)
{
	const uint8_t *v;

	if (!cbs_get(cbs, &v, len))
		return 0;

	CBS_init(out, v, len);
	return 1;
}

static int
cbs_get_length_prefixed(CBS *cbs, CBS *out, size_t len_len)
{
	uint32_t len;

	if (!cbs_get_u(cbs, &len, len_len))
		return 0;

	return CBS_get_bytes(cbs, out, len);
}

int
CBS_get_u8_length_prefixed(CBS *cbs, CBS *out)
{
	return cbs_get_length_prefixed(cbs, out, 1);
}

int
CBS_get_u16_length_prefixed(CBS *cbs, CBS *out)
{
	return cbs_get_length_prefixed(cbs, out, 2);
}

int
CBS_get_u24_length_prefixed(CBS *cbs, CBS *out)
{
	return cbs_get_length_prefixed(cbs, out, 3);
}

static int
cbs_peek_u(CBS *cbs, uint32_t *out, size_t len)
{
	uint32_t result = 0;
	size_t i;
	const uint8_t *data;

	if (len < 1 || len > 4)
		return 0;

	if (!cbs_peek(cbs, &data, len))
		return 0;

	for (i = 0; i < len; i++) {
		result <<= 8;
		result |= data[i];
	}
	*out = result;
	return 1;
}

int
CBS_peek_u8(CBS *cbs, uint8_t *out)
{
	const uint8_t *v;

	if (!cbs_peek(cbs, &v, 1))
		return 0;

	*out = *v;
	return 1;
}

int
CBS_peek_u16(CBS *cbs, uint16_t *out)
{
	uint32_t v;

	if (!cbs_peek_u(cbs, &v, 2))
		return 0;

	*out = v;
	return 1;
}

int
CBS_peek_u24(CBS *cbs, uint32_t *out)
{
	return cbs_peek_u(cbs, out, 3);
}

int
CBS_peek_u32(CBS *cbs, uint32_t *out)
{
	return cbs_peek_u(cbs, out, 4);
}

int
CBS_peek_last_u8(CBS *cbs, uint8_t *out)
{
	if (cbs->len == 0)
		return 0;

	*out = cbs->data[cbs->len - 1];
	return 1;
}

int
CBS_get_any_asn1_element(CBS *cbs, CBS *out, unsigned int *out_tag,
    size_t *out_header_len)
{
	return cbs_get_any_asn1_element_internal(cbs, out, out_tag,
	    out_header_len, 1);
}

/*
 * Review X.690 for details on ASN.1 DER encoding.
 *
 * If non-strict mode is enabled, then DER rules are relaxed
 * for indefinite constructs (violates DER but a little closer to BER).
 * Non-strict mode should only be used by bs_ber.c
 *
 * Sections 8, 10 and 11 for DER encoding
 */
int
cbs_get_any_asn1_element_internal(CBS *cbs, CBS *out, unsigned int *out_tag,
    size_t *out_header_len, int strict)
{
	uint8_t tag, length_byte;
	CBS header = *cbs;
	CBS throwaway;
	size_t len;

	if (out == NULL)
		out = &throwaway;

	/*
	 * Get identifier octet and length octet.  Only 1 octet for each
	 * is a CBS limitation.
	 */
	if (!CBS_get_u8(&header, &tag) || !CBS_get_u8(&header, &length_byte))
		return 0;

	/* CBS limitation: long form tags are not supported. */
	if ((tag & 0x1f) == 0x1f)
		return 0;

	if (out_tag != NULL)
		*out_tag = tag;

	if ((length_byte & 0x80) == 0) {
		/* Short form length. */
		len = ((size_t) length_byte) + 2;
		if (out_header_len != NULL)
			*out_header_len = 2;

	} else {
		/* Long form length. */
		const size_t num_bytes = length_byte & 0x7f;
		uint32_t len32;

		/* ASN.1 reserved value for future extensions */
		if (num_bytes == 0x7f)
			return 0;

		/* Handle indefinite form length */
		if (num_bytes == 0) {
			/* DER encoding doesn't allow for indefinite form. */
			if (strict)
				return 0;

			/* Primitive cannot use indefinite in BER or DER. */
			if ((tag & CBS_ASN1_CONSTRUCTED) == 0)
				return 0;

			/* Constructed, indefinite length allowed in BER. */
			if (out_header_len != NULL)
				*out_header_len = 2;
			return CBS_get_bytes(cbs, out, 2);
		}

		/* CBS limitation. */
		if (num_bytes > 4)
			return 0;

		if (!cbs_get_u(&header, &len32, num_bytes))
			return 0;

		/* DER has a minimum length octet requirement. */
		if (len32 < 128)
			/* Should have used short form instead */
			return 0;

		if ((len32 >> ((num_bytes - 1) * 8)) == 0)
			/* Length should have been at least one byte shorter. */
			return 0;

		len = len32;
		if (len + 2 + num_bytes < len)
			/* Overflow. */
			return 0;

		len += 2 + num_bytes;
		if (out_header_len != NULL)
			*out_header_len = 2 + num_bytes;
	}

	return CBS_get_bytes(cbs, out, len);
}

static int
cbs_get_asn1(CBS *cbs, CBS *out, unsigned int tag_value, int skip_header)
{
	size_t header_len;
	unsigned int tag;
	CBS throwaway;

	if (out == NULL)
		out = &throwaway;

	if (!CBS_get_any_asn1_element(cbs, out, &tag, &header_len) ||
	    tag != tag_value)
		return 0;

	if (skip_header && !CBS_skip(out, header_len))
		return 0;

	return 1;
}

int
CBS_get_asn1(CBS *cbs, CBS *out, unsigned int tag_value)
{
	return cbs_get_asn1(cbs, out, tag_value, 1 /* skip header */);
}

int
CBS_get_asn1_element(CBS *cbs, CBS *out, unsigned int tag_value)
{
	return cbs_get_asn1(cbs, out, tag_value, 0 /* include header */);
}

int
CBS_peek_asn1_tag(const CBS *cbs, unsigned int tag_value)
{
	if (CBS_len(cbs) < 1)
		return 0;

	/*
	 * Tag number 31 indicates the start of a long form number.
	 * This is valid in ASN.1, but CBS only supports short form.
	 */
	if ((tag_value & 0x1f) == 0x1f)
		return 0;

	return CBS_data(cbs)[0] == tag_value;
}

/* Encoding details are in ASN.1: X.690 section 8.3 */
int
CBS_get_asn1_uint64(CBS *cbs, uint64_t *out)
{
	CBS bytes;
	const uint8_t *data;
	size_t i, len;

	if (!CBS_get_asn1(cbs, &bytes, CBS_ASN1_INTEGER))
		return 0;

	*out = 0;
	data = CBS_data(&bytes);
	len = CBS_len(&bytes);

	if (len == 0)
		/* An INTEGER is encoded with at least one content octet. */
		return 0;

	if ((data[0] & 0x80) != 0)
		/* Negative number. */
		return 0;

	if (data[0] == 0 && len > 1 && (data[1] & 0x80) == 0)
		/* Violates smallest encoding rule: excessive leading zeros. */
		return 0;

	for (i = 0; i < len; i++) {
		if ((*out >> 56) != 0)
			/* Too large to represent as a uint64_t. */
			return 0;

		*out <<= 8;
		*out |= data[i];
	}

	return 1;
}

int
CBS_get_optional_asn1(CBS *cbs, CBS *out, int *out_present, unsigned int tag)
{
	if (CBS_peek_asn1_tag(cbs, tag)) {
		if (!CBS_get_asn1(cbs, out, tag))
			return 0;

		*out_present = 1;
	} else {
		*out_present = 0;
	}
	return 1;
}

int
CBS_get_optional_asn1_octet_string(CBS *cbs, CBS *out, int *out_present,
    unsigned int tag)
{
	CBS child;
	int present;

	if (!CBS_get_optional_asn1(cbs, &child, &present, tag))
		return 0;

	if (present) {
		if (!CBS_get_asn1(&child, out, CBS_ASN1_OCTETSTRING) ||
		    CBS_len(&child) != 0)
			return 0;
	} else {
		CBS_init(out, NULL, 0);
	}
	if (out_present)
		*out_present = present;

	return 1;
}

int
CBS_get_optional_asn1_uint64(CBS *cbs, uint64_t *out, unsigned int tag,
    uint64_t default_value)
{
	CBS child;
	int present;

	if (!CBS_get_optional_asn1(cbs, &child, &present, tag))
		return 0;

	if (present) {
		if (!CBS_get_asn1_uint64(&child, out) ||
		    CBS_len(&child) != 0)
			return 0;
	} else {
		*out = default_value;
	}
	return 1;
}

int
CBS_get_optional_asn1_bool(CBS *cbs, int *out, unsigned int tag,
    int default_value)
{
	CBS child, child2;
	int present;

	if (!CBS_get_optional_asn1(cbs, &child, &present, tag))
		return 0;

	if (present) {
		uint8_t boolean;

		if (!CBS_get_asn1(&child, &child2, CBS_ASN1_BOOLEAN) ||
		    CBS_len(&child2) != 1 || CBS_len(&child) != 0)
			return 0;

		boolean = CBS_data(&child2)[0];
		if (boolean == 0)
			*out = 0;
		else if (boolean == 0xff)
			*out = 1;
		else
			return 0;

	} else {
		*out = default_value;
	}
	return 1;
}
