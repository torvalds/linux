/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/errno.h>
#include <linux/unicode.h>

#include "utf8n.h"

int utf8_validate(const struct unicode_map *um, const struct qstr *str)
{
	const struct utf8data *data = utf8nfdi(um->version);

	if (utf8nlen(data, str->name, str->len) < 0)
		return -1;
	return 0;
}
EXPORT_SYMBOL(utf8_validate);

int utf8_strncmp(const struct unicode_map *um,
		 const struct qstr *s1, const struct qstr *s2)
{
	const struct utf8data *data = utf8nfdi(um->version);
	struct utf8cursor cur1, cur2;
	int c1, c2;

	if (utf8ncursor(&cur1, data, s1->name, s1->len) < 0)
		return -EINVAL;

	if (utf8ncursor(&cur2, data, s2->name, s2->len) < 0)
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
	const struct utf8data *data = utf8nfdicf(um->version);
	struct utf8cursor cur1, cur2;
	int c1, c2;

	if (utf8ncursor(&cur1, data, s1->name, s1->len) < 0)
		return -EINVAL;

	if (utf8ncursor(&cur2, data, s2->name, s2->len) < 0)
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

int utf8_casefold(const struct unicode_map *um, const struct qstr *str,
		  unsigned char *dest, size_t dlen)
{
	const struct utf8data *data = utf8nfdicf(um->version);
	struct utf8cursor cur;
	size_t nlen = 0;

	if (utf8ncursor(&cur, data, str->name, str->len) < 0)
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

int utf8_normalize(const struct unicode_map *um, const struct qstr *str,
		   unsigned char *dest, size_t dlen)
{
	const struct utf8data *data = utf8nfdi(um->version);
	struct utf8cursor cur;
	ssize_t nlen = 0;

	if (utf8ncursor(&cur, data, str->name, str->len) < 0)
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

static int utf8_parse_version(const char *version, unsigned int *maj,
			      unsigned int *min, unsigned int *rev)
{
	substring_t args[3];
	char version_string[12];
	const struct match_token token[] = {
		{1, "%d.%d.%d"},
		{0, NULL}
	};

	strncpy(version_string, version, sizeof(version_string));

	if (match_token(version_string, token, args) != 1)
		return -EINVAL;

	if (match_int(&args[0], maj) || match_int(&args[1], min) ||
	    match_int(&args[2], rev))
		return -EINVAL;

	return 0;
}

struct unicode_map *utf8_load(const char *version)
{
	struct unicode_map *um = NULL;
	int unicode_version;

	if (version) {
		unsigned int maj, min, rev;

		if (utf8_parse_version(version, &maj, &min, &rev) < 0)
			return ERR_PTR(-EINVAL);

		if (!utf8version_is_supported(maj, min, rev))
			return ERR_PTR(-EINVAL);

		unicode_version = UNICODE_AGE(maj, min, rev);
	} else {
		unicode_version = utf8version_latest();
		printk(KERN_WARNING"UTF-8 version not specified. "
		       "Assuming latest supported version (%d.%d.%d).",
		       (unicode_version >> 16) & 0xff,
		       (unicode_version >> 8) & 0xff,
		       (unicode_version & 0xff));
	}

	um = kzalloc(sizeof(struct unicode_map), GFP_KERNEL);
	if (!um)
		return ERR_PTR(-ENOMEM);

	um->charset = "UTF-8";
	um->version = unicode_version;

	return um;
}
EXPORT_SYMBOL(utf8_load);

void utf8_unload(struct unicode_map *um)
{
	kfree(um);
}
EXPORT_SYMBOL(utf8_unload);

MODULE_LICENSE("GPL v2");
