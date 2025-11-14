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
	int bits = 0;
	int i;
	char *cp = dst;
	const char *base64_table = base64_tables[variant];

	for (i = 0; i < srclen; i++) {
		ac = (ac << 8) | src[i];
		bits += 8;
		do {
			bits -= 6;
			*cp++ = base64_table[(ac >> bits) & 0x3f];
		} while (bits >= 6);
	}
	if (bits) {
		*cp++ = base64_table[(ac << (6 - bits)) & 0x3f];
		bits -= 6;
	}
	if (padding) {
		while (bits < 0) {
			*cp++ = '=';
			bits += 2;
		}
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
 * This implementation hasn't been optimized for performance.
 *
 * Return: the length of the resulting decoded binary data in bytes,
 *	   or -1 if the string isn't a valid Base64 string.
 */
int base64_decode(const char *src, int srclen, u8 *dst, bool padding, enum base64_variant variant)
{
	u32 ac = 0;
	int bits = 0;
	int i;
	u8 *bp = dst;
	s8 ch;

	for (i = 0; i < srclen; i++) {
		if (padding) {
			if (src[i] == '=') {
				ac = (ac << 6);
				bits += 6;
				if (bits >= 8)
					bits -= 8;
				continue;
			}
		}
		ch = base64_rev_maps[variant][(u8)src[i]];
		if (ch == -1)
			return -1;
		ac = (ac << 6) | ch;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			*bp++ = (u8)(ac >> bits);
		}
	}
	if (ac & ((1 << bits) - 1))
		return -1;
	return bp - dst;
}
EXPORT_SYMBOL_GPL(base64_decode);
