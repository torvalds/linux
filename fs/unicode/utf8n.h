/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014 SGI.
 * All rights reserved.
 */

#ifndef UTF8NORM_H
#define UTF8NORM_H

#include <linux/types.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/unicode.h>

int utf8version_is_supported(const struct unicode_map *um, unsigned int version);

/*
 * Determine the length of the normalized from of the string,
 * excluding any terminating NULL byte.
 * Returns 0 if only ignorable code points are present.
 * Returns -1 if the input is not valid UTF-8.
 */
ssize_t utf8nlen(const struct unicode_map *um, enum utf8_normalization n,
		const char *s, size_t len);

/* Needed in struct utf8cursor below. */
#define UTF8HANGULLEAF	(12)

/*
 * Cursor structure used by the normalizer.
 */
struct utf8cursor {
	const struct unicode_map *um;
	enum utf8_normalization n;
	const char	*s;
	const char	*p;
	const char	*ss;
	const char	*sp;
	unsigned int	len;
	unsigned int	slen;
	short int	ccc;
	short int	nccc;
	unsigned char	hangul[UTF8HANGULLEAF];
};

/*
 * Initialize a utf8cursor to normalize a string.
 * Returns 0 on success.
 * Returns -1 on failure.
 */
int utf8ncursor(struct utf8cursor *u8c, const struct unicode_map *um,
		enum utf8_normalization n, const char *s, size_t len);

/*
 * Get the next byte in the normalization.
 * Returns a value > 0 && < 256 on success.
 * Returns 0 when the end of the normalization is reached.
 * Returns -1 if the string being normalized is not valid UTF-8.
 */
extern int utf8byte(struct utf8cursor *u8c);

struct utf8data {
	unsigned int maxage;
	unsigned int offset;
};

struct utf8data_table {
	const unsigned int *utf8agetab;
	int utf8agetab_size;

	const struct utf8data *utf8nfdicfdata;
	int utf8nfdicfdata_size;

	const struct utf8data *utf8nfdidata;
	int utf8nfdidata_size;

	const unsigned char *utf8data;
};

extern struct utf8data_table utf8_data_table;

#endif /* UTF8NORM_H */
