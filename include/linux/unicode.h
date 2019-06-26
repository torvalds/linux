/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_UNICODE_H
#define _LINUX_UNICODE_H

#include <linux/init.h>
#include <linux/dcache.h>

struct unicode_map {
	const char *charset;
	int version;
};

int utf8_validate(const struct unicode_map *um, const struct qstr *str);

int utf8_strncmp(const struct unicode_map *um,
		 const struct qstr *s1, const struct qstr *s2);

int utf8_strncasecmp(const struct unicode_map *um,
		 const struct qstr *s1, const struct qstr *s2);

int utf8_normalize(const struct unicode_map *um, const struct qstr *str,
		   unsigned char *dest, size_t dlen);

int utf8_casefold(const struct unicode_map *um, const struct qstr *str,
		  unsigned char *dest, size_t dlen);

struct unicode_map *utf8_load(const char *version);
void utf8_unload(struct unicode_map *um);

#endif /* _LINUX_UNICODE_H */
