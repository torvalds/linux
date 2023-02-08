/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_STRING_HELPERS_H_
#define _LINUX_STRING_HELPERS_H_

#include <linux/bits.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/types.h>

struct device;
struct file;
struct task_struct;

static inline bool string_is_terminated(const char *s, int len)
{
	return memchr(s, '\0', len) ? true : false;
}

/* Descriptions of the types of units to
 * print in */
enum string_size_units {
	STRING_UNITS_10,	/* use powers of 10^3 (standard SI) */
	STRING_UNITS_2,		/* use binary powers of 2^10 */
};

void string_get_size(u64 size, u64 blk_size, enum string_size_units units,
		     char *buf, int len);

int parse_int_array_user(const char __user *from, size_t count, int **array);

#define UNESCAPE_SPACE		BIT(0)
#define UNESCAPE_OCTAL		BIT(1)
#define UNESCAPE_HEX		BIT(2)
#define UNESCAPE_SPECIAL	BIT(3)
#define UNESCAPE_ANY		\
	(UNESCAPE_SPACE | UNESCAPE_OCTAL | UNESCAPE_HEX | UNESCAPE_SPECIAL)

#define UNESCAPE_ALL_MASK	GENMASK(3, 0)

int string_unescape(char *src, char *dst, size_t size, unsigned int flags);

static inline int string_unescape_inplace(char *buf, unsigned int flags)
{
	return string_unescape(buf, buf, 0, flags);
}

static inline int string_unescape_any(char *src, char *dst, size_t size)
{
	return string_unescape(src, dst, size, UNESCAPE_ANY);
}

static inline int string_unescape_any_inplace(char *buf)
{
	return string_unescape_any(buf, buf, 0);
}

#define ESCAPE_SPACE		BIT(0)
#define ESCAPE_SPECIAL		BIT(1)
#define ESCAPE_NULL		BIT(2)
#define ESCAPE_OCTAL		BIT(3)
#define ESCAPE_ANY		\
	(ESCAPE_SPACE | ESCAPE_OCTAL | ESCAPE_SPECIAL | ESCAPE_NULL)
#define ESCAPE_NP		BIT(4)
#define ESCAPE_ANY_NP		(ESCAPE_ANY | ESCAPE_NP)
#define ESCAPE_HEX		BIT(5)
#define ESCAPE_NA		BIT(6)
#define ESCAPE_NAP		BIT(7)
#define ESCAPE_APPEND		BIT(8)

#define ESCAPE_ALL_MASK		GENMASK(8, 0)

int string_escape_mem(const char *src, size_t isz, char *dst, size_t osz,
		unsigned int flags, const char *only);

static inline int string_escape_mem_any_np(const char *src, size_t isz,
		char *dst, size_t osz, const char *only)
{
	return string_escape_mem(src, isz, dst, osz, ESCAPE_ANY_NP, only);
}

static inline int string_escape_str(const char *src, char *dst, size_t sz,
		unsigned int flags, const char *only)
{
	return string_escape_mem(src, strlen(src), dst, sz, flags, only);
}

static inline int string_escape_str_any_np(const char *src, char *dst,
		size_t sz, const char *only)
{
	return string_escape_str(src, dst, sz, ESCAPE_ANY_NP, only);
}

static inline void string_upper(char *dst, const char *src)
{
	do {
		*dst++ = toupper(*src);
	} while (*src++);
}

static inline void string_lower(char *dst, const char *src)
{
	do {
		*dst++ = tolower(*src);
	} while (*src++);
}

char *kstrdup_quotable(const char *src, gfp_t gfp);
char *kstrdup_quotable_cmdline(struct task_struct *task, gfp_t gfp);
char *kstrdup_quotable_file(struct file *file, gfp_t gfp);

char **kasprintf_strarray(gfp_t gfp, const char *prefix, size_t n);
void kfree_strarray(char **array, size_t n);

char **devm_kasprintf_strarray(struct device *dev, const char *prefix, size_t n);

static inline const char *str_yes_no(bool v)
{
	return v ? "yes" : "no";
}

static inline const char *str_on_off(bool v)
{
	return v ? "on" : "off";
}

static inline const char *str_enable_disable(bool v)
{
	return v ? "enable" : "disable";
}

static inline const char *str_enabled_disabled(bool v)
{
	return v ? "enabled" : "disabled";
}

static inline const char *str_read_write(bool v)
{
	return v ? "read" : "write";
}

#endif
