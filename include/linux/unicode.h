/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNICODE_H
#define _LINUX_UNICODE_H

#include <linux/init.h>
#include <linux/dcache.h>

#define UNICODE_MAJ_SHIFT		16
#define UNICODE_MIN_SHIFT		8

#define UNICODE_AGE(MAJ, MIN, REV)			\
	(((unsigned int)(MAJ) << UNICODE_MAJ_SHIFT) |	\
	 ((unsigned int)(MIN) << UNICODE_MIN_SHIFT) |	\
	 ((unsigned int)(REV)))

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

struct unicode_map {
	unsigned int version;
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

#endif /* _LINUX_UNICODE_H */
