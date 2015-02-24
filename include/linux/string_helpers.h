#ifndef _LINUX_STRING_HELPERS_H_
#define _LINUX_STRING_HELPERS_H_

#include <linux/types.h>

/* Descriptions of the types of units to
 * print in */
enum string_size_units {
	STRING_UNITS_10,	/* use powers of 10^3 (standard SI) */
	STRING_UNITS_2,		/* use binary powers of 2^10 */
};

void string_get_size(u64 size, enum string_size_units units,
		     char *buf, int len);

#define UNESCAPE_SPACE		0x01
#define UNESCAPE_OCTAL		0x02
#define UNESCAPE_HEX		0x04
#define UNESCAPE_SPECIAL	0x08
#define UNESCAPE_ANY		\
	(UNESCAPE_SPACE | UNESCAPE_OCTAL | UNESCAPE_HEX | UNESCAPE_SPECIAL)

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

#define ESCAPE_SPACE		0x01
#define ESCAPE_SPECIAL		0x02
#define ESCAPE_NULL		0x04
#define ESCAPE_OCTAL		0x08
#define ESCAPE_ANY		\
	(ESCAPE_SPACE | ESCAPE_OCTAL | ESCAPE_SPECIAL | ESCAPE_NULL)
#define ESCAPE_NP		0x10
#define ESCAPE_ANY_NP		(ESCAPE_ANY | ESCAPE_NP)
#define ESCAPE_HEX		0x20

int string_escape_mem(const char *src, size_t isz, char **dst, size_t osz,
		unsigned int flags, const char *esc);

static inline int string_escape_mem_any_np(const char *src, size_t isz,
		char **dst, size_t osz, const char *esc)
{
	return string_escape_mem(src, isz, dst, osz, ESCAPE_ANY_NP, esc);
}

static inline int string_escape_str(const char *src, char **dst, size_t sz,
		unsigned int flags, const char *esc)
{
	return string_escape_mem(src, strlen(src), dst, sz, flags, esc);
}

static inline int string_escape_str_any_np(const char *src, char **dst,
		size_t sz, const char *esc)
{
	return string_escape_str(src, dst, sz, ESCAPE_ANY_NP, esc);
}

#endif
