/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/errno.h>
#include <linux/stringhash.h>

#include "utf8n.h"

int utf8_validate(const struct unicode_map *um, const struct qstr *str)
{
	if (utf8nlen(um, UTF8_NFDI, str->name, str->len) < 0)
		return -1;
	return 0;
}
EXPORT_SYMBOL(utf8_validate);

int utf8_strncmp(const struct unicode_map *um,
		 const struct qstr *s1, const struct qstr *s2)
{
	struct utf8cursor cur1, cur2;
	int c1, c2;

	if (utf8ncursor(&cur1, um, UTF8_NFDI, s1->name, s1->len) < 0)
		return -EINVAL;

	if (utf8ncursor(&cur2, um, UTF8_NFDI, s2->name, s2->len) < 0)
		return -EINVAL;

	do {
		c1 = utf8byte(&cur1);
		c2 = utf8byte(&cur2);

		if (c1 < 0 || c2 < 0)
			return -EINVAL;
		if (c1 != c2)
			return 1;
	} while (c1);

	return 0;
}
EXPORT_SYMBOL(utf8_strncmp);

int utf8_strncasecmp(const struct unicode_map *um,
		     const struct qstr *s1, const struct qstr *s2)
{
	struct utf8cursor cur1, cur2;
	int c1, c2;

	if (utf8ncursor(&cur1, um, UTF8_NFDICF, s1->name, s1->len) < 0)
		return -EINVAL;

	if (utf8ncursor(&cur2, um, UTF8_NFDICF, s2->name, s2->len) < 0)
		return -EINVAL;

	do {
		c1 = utf8byte(&cur1);
		c2 = utf8byte(&cur2);

		if (c1 < 0 || c2 < 0)
			return -EINVAL;
		if (c1 != c2)
			return 1;
	} while (c1);

	return 0;
}
EXPORT_SYMBOL(utf8_strncasecmp);

/* String cf is expected to be a valid UTF-8 casefolded
 * string.
 */
int utf8_strncasecmp_folded(const struct unicode_map *um,
			    const struct qstr *cf,
			    const struct qstr *s1)
{
	struct utf8cursor cur1;
	int c1, c2;
	int i = 0;

	if (utf8ncursor(&cur1, um, UTF8_NFDICF, s1->name, s1->len) < 0)
		return -EINVAL;

	do {
		c1 = utf8byte(&cur1);
		c2 = cf->name[i++];
		if (c1 < 0)
			return -EINVAL;
		if (c1 != c2)
			return 1;
	} while (c1);

	return 0;
}
EXPORT_SYMBOL(utf8_strncasecmp_folded);

int utf8_casefold(const struct unicode_map *um, const struct qstr *str,
		  unsigned char *dest, size_t dlen)
{
	struct utf8cursor cur;
	size_t nlen = 0;

	if (utf8ncursor(&cur, um, UTF8_NFDICF, str->name, str->len) < 0)
		return -EINVAL;

	for (nlen = 0; nlen < dlen; nlen++) {
		int c = utf8byte(&cur);

		dest[nlen] = c;
		if (!c)
			return nlen;
		if (c == -1)
			break;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(utf8_casefold);

int utf8_casefold_hash(const struct unicode_map *um, const void *salt,
		       struct qstr *str)
{
	struct utf8cursor cur;
	int c;
	unsigned long hash = init_name_hash(salt);

	if (utf8ncursor(&cur, um, UTF8_NFDICF, str->name, str->len) < 0)
		return -EINVAL;

	while ((c = utf8byte(&cur))) {
		if (c < 0)
			return -EINVAL;
		hash = partial_name_hash((unsigned char)c, hash);
	}
	str->hash = end_name_hash(hash);
	return 0;
}
EXPORT_SYMBOL(utf8_casefold_hash);

int utf8_normalize(const struct unicode_map *um, const struct qstr *str,
		   unsigned char *dest, size_t dlen)
{
	struct utf8cursor cur;
	ssize_t nlen = 0;

	if (utf8ncursor(&cur, um, UTF8_NFDI, str->name, str->len) < 0)
		return -EINVAL;

	for (nlen = 0; nlen < dlen; nlen++) {
		int c = utf8byte(&cur);

		dest[nlen] = c;
		if (!c)
			return nlen;
		if (c == -1)
			break;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(utf8_normalize);

static const struct utf8data *find_table_version(const struct utf8data *table,
		size_t nr_entries, unsigned int version)
{
	size_t i = nr_entries - 1;

	while (version < table[i].maxage)
		i--;
	if (version > table[i].maxage)
		return NULL;
	return &table[i];
}

struct unicode_map *utf8_load(unsigned int version)
{
	struct unicode_map *um;

	um = kzalloc(sizeof(struct unicode_map), GFP_KERNEL);
	if (!um)
		return ERR_PTR(-ENOMEM);
	um->version = version;

	um->tables = symbol_request(utf8_data_table);
	if (!um->tables)
		goto out_free_um;

	if (!utf8version_is_supported(um, version))
		goto out_symbol_put;
	um->ntab[UTF8_NFDI] = find_table_version(um->tables->utf8nfdidata,
			um->tables->utf8nfdidata_size, um->version);
	if (!um->ntab[UTF8_NFDI])
		goto out_symbol_put;
	um->ntab[UTF8_NFDICF] = find_table_version(um->tables->utf8nfdicfdata,
			um->tables->utf8nfdicfdata_size, um->version);
	if (!um->ntab[UTF8_NFDICF])
		goto out_symbol_put;
	return um;

out_symbol_put:
	symbol_put(utf8_data_table);
out_free_um:
	kfree(um);
	return ERR_PTR(-EINVAL);
}
EXPORT_SYMBOL(utf8_load);

void utf8_unload(struct unicode_map *um)
{
	if (um) {
		symbol_put(utf8_data_table);
		kfree(um);
	}
}
EXPORT_SYMBOL(utf8_unload);

/**
 * utf8_parse_version - Parse a UTF-8 version number from a string
 *
 * @version: input string
 *
 * Returns the parsed version on success, negative code on error
 */
int utf8_parse_version(char *version)
{
	substring_t args[3];
	unsigned int maj, min, rev;
	static const struct match_token token[] = {
		{1, "%d.%d.%d"},
		{0, NULL}
	};

	if (match_token(version, token, args) != 1)
		return -EINVAL;

	if (match_int(&args[0], &maj) || match_int(&args[1], &min) ||
	    match_int(&args[2], &rev))
		return -EINVAL;

	return UNICODE_AGE(maj, min, rev);
}
EXPORT_SYMBOL(utf8_parse_version);
