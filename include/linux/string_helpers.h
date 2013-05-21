#ifndef _LINUX_STRING_HELPERS_H_
#define _LINUX_STRING_HELPERS_H_

#include <linux/types.h>

/* Descriptions of the types of units to
 * print in */
enum string_size_units {
	STRING_UNITS_10,	/* use powers of 10^3 (standard SI) */
	STRING_UNITS_2,		/* use binary powers of 2^10 */
};

int string_get_size(u64 size, enum string_size_units units,
		    char *buf, int len);

#define UNESCAPE_SPACE		0x01
#define UNESCAPE_OCTAL		0x02
#define UNESCAPE_HEX		0x04
#define UNESCAPE_SPECIAL	0x08
#define UNESCAPE_ANY		\
	(UNESCAPE_SPACE | UNESCAPE_OCTAL | UNESCAPE_HEX | UNESCAPE_SPECIAL)

/**
 * string_unescape - unquote characters in the given string
 * @src:	source buffer (escaped)
 * @dst:	destination buffer (unescaped)
 * @size:	size of the destination buffer (0 to unlimit)
 * @flags:	combination of the flags (bitwise OR):
 *	%UNESCAPE_SPACE:
 *		'\f' - form feed
 *		'\n' - new line
 *		'\r' - carriage return
 *		'\t' - horizontal tab
 *		'\v' - vertical tab
 *	%UNESCAPE_OCTAL:
 *		'\NNN' - byte with octal value NNN (1 to 3 digits)
 *	%UNESCAPE_HEX:
 *		'\xHH' - byte with hexadecimal value HH (1 to 2 digits)
 *	%UNESCAPE_SPECIAL:
 *		'\"' - double quote
 *		'\\' - backslash
 *		'\a' - alert (BEL)
 *		'\e' - escape
 *	%UNESCAPE_ANY:
 *		all previous together
 *
 * Returns amount of characters processed to the destination buffer excluding
 * trailing '\0'.
 *
 * Because the size of the output will be the same as or less than the size of
 * the input, the transformation may be performed in place.
 *
 * Caller must provide valid source and destination pointers. Be aware that
 * destination buffer will always be NULL-terminated. Source string must be
 * NULL-terminated as well.
 */
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

#endif
