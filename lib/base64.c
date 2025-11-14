// SPDX-License-Identifier: GPL-2.0
/*
 * base64.c - Base64 with support for multiple variants
 *
 * Copyright (c) 2020 Hannes Reinecke, SUSE
 *
 * Based on the base64url routines from fs/crypto/fname.c
 * (which are using the URL-safe Base64 encoding),
 * modified to support multiple Base64 variants.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/base64.h>

static const char base64_tables[][65] = {
	[BASE64_STD] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/",
	[BASE64_URLSAFE] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_",
	[BASE64_IMAP] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,",
};

/*
 * Initialize the base64 reverse mapping for a single character
 * This macro maps a character to its corresponding base64 value,
 * returning -1 if the character is invalid.
 * char 'A'-'Z' maps to 0-25, 'a'-'z' maps to 26-51, '0'-'9' maps to 52-61,
 * ch_62 maps to 62, ch_63 maps to 63, and other characters return -1
 */
#define INIT_1(v, ch_62, ch_63) \
	[v] = (v) >= 'A' && (v) <= 'Z' ? (v) - 'A' \
		: (v) >= 'a' && (v) <= 'z' ? (v) - 'a' + 26 \
		: (v) >= '0' && (v) <= '9' ? (v) - '0' + 52 \
		: (v) == (ch_62) ? 62 : (v) == (ch_63) ? 63 : -1

/*
 * Recursive macros to generate multiple Base64 reverse mapping table entries.
 * Each macro generates a sequence of entries in the lookup table:
 * INIT_2 generates 2 entries, INIT_4 generates 4, INIT_8 generates 8, and so on up to INIT_32.
 */
#define INIT_2(v, ...) INIT_1(v, __VA_ARGS__), INIT_1((v) + 1, __VA_ARGS__)
#define INIT_4(v, ...) INIT_2(v, __VA_ARGS__), INIT_2((v) + 2, __VA_ARGS__)
#define INIT_8(v, ...) INIT_4(v, __VA_ARGS__), INIT_4((v) + 4, __VA_ARGS__)
#define INIT_16(v, ...) INIT_8(v, __VA_ARGS__), INIT_8((v) + 8, __VA_ARGS__)
#define INIT_32(v, ...) INIT_16(v, __VA_ARGS__), INIT_16((v) + 16, __VA_ARGS__)

#define BASE64_REV_INIT(ch_62, ch_63) { \
	[0 ... 0x1f] = -1, \
	INIT_32(0x20, ch_62, ch_63), \
	INIT_32(0x40, ch_62, ch_63), \
	INIT_32(0x60, ch_62, ch_63), \
	[0x80 ... 0xff] = -1 }

static const s8 base64_rev_maps[][256] = {
	[BASE64_STD] = BASE64_REV_INIT('+', '/'),
	[BASE64_URLSAFE] = BASE64_REV_INIT('-', '_'),
	[BASE64_IMAP] = BASE64_REV_INIT('+', ',')
};

#undef BASE64_REV_INIT
#undef INIT_32
#undef INIT_16
#undef INIT_8
#undef INIT_4
#undef INIT_2
#undef INIT_1
/**
 * base64_encode() - Base64-encode some binary data
 * @src: the binary data to encode
 * @srclen: the length of @src in bytes
 * @dst: (output) the Base64-encoded string.  Not NUL-terminated.
 * @padding: whether to append '=' padding characters
 * @variant: which base64 variant to use
 *
 * Encodes data using the selected Base64 variant.
 *
 * Return: the length of the resulting Base64-encoded string in bytes.
 */
int base64_encode(const u8 *src, int srclen, char *dst, bool padding, enum base64_variant variant)
{
	u32 ac = 0;
	char *cp = dst;
	const char *base64_table = base64_tables[variant];

	while (srclen >= 3) {
		ac = src[0] << 16 | src[1] << 8 | src[2];
		*cp++ = base64_table[ac >> 18];
		*cp++ = base64_table[(ac >> 12) & 0x3f];
		*cp++ = base64_table[(ac >> 6) & 0x3f];
		*cp++ = base64_table[ac & 0x3f];

		src += 3;
		srclen -= 3;
	}

	switch (srclen) {
	case 2:
		ac = src[0] << 16 | src[1] << 8;
		*cp++ = base64_table[ac >> 18];
		*cp++ = base64_table[(ac >> 12) & 0x3f];
		*cp++ = base64_table[(ac >> 6) & 0x3f];
		if (padding)
			*cp++ = '=';
		break;
	case 1:
		ac = src[0] << 16;
		*cp++ = base64_table[ac >> 18];
		*cp++ = base64_table[(ac >> 12) & 0x3f];
		if (padding) {
			*cp++ = '=';
			*cp++ = '=';
		}
		break;
	}
	return cp - dst;
}
EXPORT_SYMBOL_GPL(base64_encode);

/**
 * base64_decode() - Base64-decode a string
 * @src: the string to decode.  Doesn't need to be NUL-terminated.
 * @srclen: the length of @src in bytes
 * @dst: (output) the decoded binary data
 * @padding: whether to append '=' padding characters
 * @variant: which base64 variant to use
 *
 * Decodes a string using the selected Base64 variant.
 *
 * Return: the length of the resulting decoded binary data in bytes,
 *	   or -1 if the string isn't a valid Base64 string.
 */
int base64_decode(const char *src, int srclen, u8 *dst, bool padding, enum base64_variant variant)
{
	u8 *bp = dst;
	s8 input[4];
	s32 val;
	const u8 *s = (const u8 *)src;
	const s8 *base64_rev_tables = base64_rev_maps[variant];

	while (srclen >= 4) {
		input[0] = base64_rev_tables[s[0]];
		input[1] = base64_rev_tables[s[1]];
		input[2] = base64_rev_tables[s[2]];
		input[3] = base64_rev_tables[s[3]];

		val = input[0] << 18 | input[1] << 12 | input[2] << 6 | input[3];

		if (unlikely(val < 0)) {
			if (!padding || srclen != 4 || s[3] != '=')
				return -1;
			padding = 0;
			srclen = s[2] == '=' ? 2 : 3;
			break;
		}

		*bp++ = val >> 16;
		*bp++ = val >> 8;
		*bp++ = val;

		s += 4;
		srclen -= 4;
	}

	if (likely(!srclen))
		return bp - dst;
	if (padding || srclen == 1)
		return -1;

	val = (base64_rev_tables[s[0]] << 12) | (base64_rev_tables[s[1]] << 6);
	*bp++ = val >> 10;

	if (srclen == 2) {
		if (val & 0x800003ff)
			return -1;
	} else {
		val |= base64_rev_tables[s[2]];
		if (val & 0x80000003)
			return -1;
		*bp++ = val >> 2;
	}
	return bp - dst;
}
EXPORT_SYMBOL_GPL(base64_decode);
