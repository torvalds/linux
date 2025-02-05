/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNICODE_H
#define _LINUX_UNICODE_H

#include <linux/init.h>
#include <linux/dcache.h>

struct utf8data;
struct utf8data_table;

#define UNICODE_MAJ_SHIFT		16
#define UNICODE_MIN_SHIFT		8

#define UNICODE_AGE(MAJ, MIN, REV)			\
	(((unsigned int)(MAJ) << UNICODE_MAJ_SHIFT) |	\
	 ((unsigned int)(MIN) << UNICODE_MIN_SHIFT) |	\
	 ((unsigned int)(REV)))

#define UTF8_LATEST        UNICODE_AGE(12, 1, 0)

static inline u8 unicode_major(unsigned int age)
{
	return (age >> UNICODE_MAJ_SHIFT) & 0xff;
}

static inline u8 unicode_minor(unsigned int age)
{
	return (age >> UNICODE_MIN_SHIFT) & 0xff;
}

static inline u8 unicode_rev(unsigned int age)
{
	return age & 0xff;
}

/*
 * Two normalization forms are supported:
 * 1) NFDI
 *   - Apply unicode normalization form NFD.
 *   - Remove any Default_Ignorable_Code_Point.
 * 2) NFDICF
 *   - Apply unicode normalization form NFD.
 *   - Remove any Default_Ignorable_Code_Point.
 *   - Apply a full casefold (C + F).
 */
enum utf8_normalization {
	UTF8_NFDI = 0,
	UTF8_NFDICF,
	UTF8_NMAX,
};

struct unicode_map {
	unsigned int version;
	const struct utf8data *ntab[UTF8_NMAX];
	const struct utf8data_table *tables;
};

int utf8_validate(const struct unicode_map *um, const struct qstr *str);

int utf8_strncmp(const struct unicode_map *um,
		 const struct qstr *s1, const struct qstr *s2);

int utf8_strncasecmp(const struct unicode_map *um,
		 const struct qstr *s1, const struct qstr *s2);
int utf8_strncasecmp_folded(const struct unicode_map *um,
			    const struct qstr *cf,
			    const struct qstr *s1);

int utf8_normalize(const struct unicode_map *um, const struct qstr *str,
		   unsigned char *dest, size_t dlen);

int utf8_casefold(const struct unicode_map *um, const struct qstr *str,
		  unsigned char *dest, size_t dlen);

int utf8_casefold_hash(const struct unicode_map *um, const void *salt,
		       struct qstr *str);

struct unicode_map *utf8_load(unsigned int version);
void utf8_unload(struct unicode_map *um);

int utf8_parse_version(char *version);

#endif /* _LINUX_UNICODE_H */
