/*
 * Copyright (c) 2014 SGI.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef UTF8NORM_H
#define UTF8NORM_H

#include <linux/types.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/module.h>

/* Encoding a unicode version number as a single unsigned int. */
#define UNICODE_MAJ_SHIFT		(16)
#define UNICODE_MIN_SHIFT		(8)

#define UNICODE_AGE(MAJ, MIN, REV)			\
	(((unsigned int)(MAJ) << UNICODE_MAJ_SHIFT) |	\
	 ((unsigned int)(MIN) << UNICODE_MIN_SHIFT) |	\
	 ((unsigned int)(REV)))

/* Highest unicode version supported by the data tables. */
extern int utf8version_is_supported(u8 maj, u8 min, u8 rev);

/*
 * Look for the correct const struct utf8data for a unicode version.
 * Returns NULL if the version requested is too new.
 *
 * Two normalization forms are supported: nfdi and nfdicf.
 *
 * nfdi:
 *  - Apply unicode normalization form NFD.
 *  - Remove any Default_Ignorable_Code_Point.
 *
 * nfdicf:
 *  - Apply unicode normalization form NFD.
 *  - Remove any Default_Ignorable_Code_Point.
 *  - Apply a full casefold (C + F).
 */
extern const struct utf8data *utf8nfdi(unsigned int maxage);
extern const struct utf8data *utf8nfdicf(unsigned int maxage);

/*
 * Determine the maximum age of any unicode character in the string.
 * Returns 0 if only unassigned code points are present.
 * Returns -1 if the input is not valid UTF-8.
 */
extern int utf8agemax(const struct utf8data *data, const char *s);
extern int utf8nagemax(const struct utf8data *data, const char *s, size_t len);

/*
 * Determine the minimum age of any unicode character in the string.
 * Returns 0 if any unassigned code points are present.
 * Returns -1 if the input is not valid UTF-8.
 */
extern int utf8agemin(const struct utf8data *data, const char *s);
extern int utf8nagemin(const struct utf8data *data, const char *s, size_t len);

/*
 * Determine the length of the normalized from of the string,
 * excluding any terminating NULL byte.
 * Returns 0 if only ignorable code points are present.
 * Returns -1 if the input is not valid UTF-8.
 */
extern ssize_t utf8len(const struct utf8data *data, const char *s);
extern ssize_t utf8nlen(const struct utf8data *data, const char *s, size_t len);

/*
 * Cursor structure used by the normalizer.
 */
struct utf8cursor {
	const struct utf8data	*data;
	const char	*s;
	const char	*p;
	const char	*ss;
	const char	*sp;
	unsigned int	len;
	unsigned int	slen;
	short int	ccc;
	short int	nccc;
};

/*
 * Initialize a utf8cursor to normalize a string.
 * Returns 0 on success.
 * Returns -1 on failure.
 */
extern int utf8cursor(struct utf8cursor *u8c, const struct utf8data *data,
		      const char *s);
extern int utf8ncursor(struct utf8cursor *u8c, const struct utf8data *data,
		       const char *s, size_t len);

/*
 * Get the next byte in the normalization.
 * Returns a value > 0 && < 256 on success.
 * Returns 0 when the end of the normalization is reached.
 * Returns -1 if the string being normalized is not valid UTF-8.
 */
extern int utf8byte(struct utf8cursor *u8c);

#endif /* UTF8NORM_H */
