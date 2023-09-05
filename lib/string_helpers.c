// SPDX-License-Identifier: GPL-2.0-only
/*
 * Helpers for formatting and printing strings
 *
 * Copyright 31 August 2008 James Bottomley
 * Copyright (C) 2013, Intel Corporation
 */
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/export.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/string_helpers.h>

/**
 * string_get_size - get the size in the specified units
 * @size:	The size to be converted in blocks
 * @blk_size:	Size of the block (use 1 for size in bytes)
 * @units:	units to use (powers of 1000 or 1024)
 * @buf:	buffer to format to
 * @len:	length of buffer
 *
 * This function returns a string formatted to 3 significant figures
 * giving the size in the required units.  @buf should have room for
 * at least 9 bytes and will always be zero terminated.
 *
 */
void string_get_size(u64 size, u64 blk_size, const enum string_size_units units,
		     char *buf, int len)
{
	static const char *const units_10[] = {
		"B", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"
	};
	static const char *const units_2[] = {
		"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "ZiB", "YiB"
	};
	static const char *const *const units_str[] = {
		[STRING_UNITS_10] = units_10,
		[STRING_UNITS_2] = units_2,
	};
	static const unsigned int divisor[] = {
		[STRING_UNITS_10] = 1000,
		[STRING_UNITS_2] = 1024,
	};
	static const unsigned int rounding[] = { 500, 50, 5 };
	int i = 0, j;
	u32 remainder = 0, sf_cap;
	char tmp[8];
	const char *unit;

	tmp[0] = '\0';

	if (blk_size == 0)
		size = 0;
	if (size == 0)
		goto out;

	/* This is Napier's algorithm.  Reduce the original block size to
	 *
	 * coefficient * divisor[units]^i
	 *
	 * we do the reduction so both coefficients are just under 32 bits so
	 * that multiplying them together won't overflow 64 bits and we keep
	 * as much precision as possible in the numbers.
	 *
	 * Note: it's safe to throw away the remainders here because all the
	 * precision is in the coefficients.
	 */
	while (blk_size >> 32) {
		do_div(blk_size, divisor[units]);
		i++;
	}

	while (size >> 32) {
		do_div(size, divisor[units]);
		i++;
	}

	/* now perform the actual multiplication keeping i as the sum of the
	 * two logarithms */
	size *= blk_size;

	/* and logarithmically reduce it until it's just under the divisor */
	while (size >= divisor[units]) {
		remainder = do_div(size, divisor[units]);
		i++;
	}

	/* work out in j how many digits of precision we need from the
	 * remainder */
	sf_cap = size;
	for (j = 0; sf_cap*10 < 1000; j++)
		sf_cap *= 10;

	if (units == STRING_UNITS_2) {
		/* express the remainder as a decimal.  It's currently the
		 * numerator of a fraction whose denominator is
		 * divisor[units], which is 1 << 10 for STRING_UNITS_2 */
		remainder *= 1000;
		remainder >>= 10;
	}

	/* add a 5 to the digit below what will be printed to ensure
	 * an arithmetical round up and carry it through to size */
	remainder += rounding[j];
	if (remainder >= 1000) {
		remainder -= 1000;
		size += 1;
	}

	if (j) {
		snprintf(tmp, sizeof(tmp), ".%03u", remainder);
		tmp[j+1] = '\0';
	}

 out:
	if (i >= ARRAY_SIZE(units_2))
		unit = "UNK";
	else
		unit = units_str[units][i];

	snprintf(buf, len, "%u%s %s", (u32)size,
		 tmp, unit);
}
EXPORT_SYMBOL(string_get_size);

/**
 * parse_int_array_user - Split string into a sequence of integers
 * @from:	The user space buffer to read from
 * @count:	The maximum number of bytes to read
 * @array:	Returned pointer to sequence of integers
 *
 * On success @array is allocated and initialized with a sequence of
 * integers extracted from the @from plus an additional element that
 * begins the sequence and specifies the integers count.
 *
 * Caller takes responsibility for freeing @array when it is no longer
 * needed.
 */
int parse_int_array_user(const char __user *from, size_t count, int **array)
{
	int *ints, nints;
	char *buf;
	int ret = 0;

	buf = memdup_user_nul(from, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	get_options(buf, 0, &nints);
	if (!nints) {
		ret = -ENOENT;
		goto free_buf;
	}

	ints = kcalloc(nints + 1, sizeof(*ints), GFP_KERNEL);
	if (!ints) {
		ret = -ENOMEM;
		goto free_buf;
	}

	get_options(buf, nints + 1, ints);
	*array = ints;

free_buf:
	kfree(buf);
	return ret;
}
EXPORT_SYMBOL(parse_int_array_user);

static bool unescape_space(char **src, char **dst)
{
	char *p = *dst, *q = *src;

	switch (*q) {
	case 'n':
		*p = '\n';
		break;
	case 'r':
		*p = '\r';
		break;
	case 't':
		*p = '\t';
		break;
	case 'v':
		*p = '\v';
		break;
	case 'f':
		*p = '\f';
		break;
	default:
		return false;
	}
	*dst += 1;
	*src += 1;
	return true;
}

static bool unescape_octal(char **src, char **dst)
{
	char *p = *dst, *q = *src;
	u8 num;

	if (isodigit(*q) == 0)
		return false;

	num = (*q++) & 7;
	while (num < 32 && isodigit(*q) && (q - *src < 3)) {
		num <<= 3;
		num += (*q++) & 7;
	}
	*p = num;
	*dst += 1;
	*src = q;
	return true;
}

static bool unescape_hex(char **src, char **dst)
{
	char *p = *dst, *q = *src;
	int digit;
	u8 num;

	if (*q++ != 'x')
		return false;

	num = digit = hex_to_bin(*q++);
	if (digit < 0)
		return false;

	digit = hex_to_bin(*q);
	if (digit >= 0) {
		q++;
		num = (num << 4) | digit;
	}
	*p = num;
	*dst += 1;
	*src = q;
	return true;
}

static bool unescape_special(char **src, char **dst)
{
	char *p = *dst, *q = *src;

	switch (*q) {
	case '\"':
		*p = '\"';
		break;
	case '\\':
		*p = '\\';
		break;
	case 'a':
		*p = '\a';
		break;
	case 'e':
		*p = '\e';
		break;
	default:
		return false;
	}
	*dst += 1;
	*src += 1;
	return true;
}

/**
 * string_unescape - unquote characters in the given string
 * @src:	source buffer (escaped)
 * @dst:	destination buffer (unescaped)
 * @size:	size of the destination buffer (0 to unlimit)
 * @flags:	combination of the flags.
 *
 * Description:
 * The function unquotes characters in the given string.
 *
 * Because the size of the output will be the same as or less than the size of
 * the input, the transformation may be performed in place.
 *
 * Caller must provide valid source and destination pointers. Be aware that
 * destination buffer will always be NULL-terminated. Source string must be
 * NULL-terminated as well.  The supported flags are::
 *
 *	UNESCAPE_SPACE:
 *		'\f' - form feed
 *		'\n' - new line
 *		'\r' - carriage return
 *		'\t' - horizontal tab
 *		'\v' - vertical tab
 *	UNESCAPE_OCTAL:
 *		'\NNN' - byte with octal value NNN (1 to 3 digits)
 *	UNESCAPE_HEX:
 *		'\xHH' - byte with hexadecimal value HH (1 to 2 digits)
 *	UNESCAPE_SPECIAL:
 *		'\"' - double quote
 *		'\\' - backslash
 *		'\a' - alert (BEL)
 *		'\e' - escape
 *	UNESCAPE_ANY:
 *		all previous together
 *
 * Return:
 * The amount of the characters processed to the destination buffer excluding
 * trailing '\0' is returned.
 */
int string_unescape(char *src, char *dst, size_t size, unsigned int flags)
{
	char *out = dst;

	while (*src && --size) {
		if (src[0] == '\\' && src[1] != '\0' && size > 1) {
			src++;
			size--;

			if (flags & UNESCAPE_SPACE &&
					unescape_space(&src, &out))
				continue;

			if (flags & UNESCAPE_OCTAL &&
					unescape_octal(&src, &out))
				continue;

			if (flags & UNESCAPE_HEX &&
					unescape_hex(&src, &out))
				continue;

			if (flags & UNESCAPE_SPECIAL &&
					unescape_special(&src, &out))
				continue;

			*out++ = '\\';
		}
		*out++ = *src++;
	}
	*out = '\0';

	return out - dst;
}
EXPORT_SYMBOL(string_unescape);

static bool escape_passthrough(unsigned char c, char **dst, char *end)
{
	char *out = *dst;

	if (out < end)
		*out = c;
	*dst = out + 1;
	return true;
}

static bool escape_space(unsigned char c, char **dst, char *end)
{
	char *out = *dst;
	unsigned char to;

	switch (c) {
	case '\n':
		to = 'n';
		break;
	case '\r':
		to = 'r';
		break;
	case '\t':
		to = 't';
		break;
	case '\v':
		to = 'v';
		break;
	case '\f':
		to = 'f';
		break;
	default:
		return false;
	}

	if (out < end)
		*out = '\\';
	++out;
	if (out < end)
		*out = to;
	++out;

	*dst = out;
	return true;
}

static bool escape_special(unsigned char c, char **dst, char *end)
{
	char *out = *dst;
	unsigned char to;

	switch (c) {
	case '\\':
		to = '\\';
		break;
	case '\a':
		to = 'a';
		break;
	case '\e':
		to = 'e';
		break;
	case '"':
		to = '"';
		break;
	default:
		return false;
	}

	if (out < end)
		*out = '\\';
	++out;
	if (out < end)
		*out = to;
	++out;

	*dst = out;
	return true;
}

static bool escape_null(unsigned char c, char **dst, char *end)
{
	char *out = *dst;

	if (c)
		return false;

	if (out < end)
		*out = '\\';
	++out;
	if (out < end)
		*out = '0';
	++out;

	*dst = out;
	return true;
}

static bool escape_octal(unsigned char c, char **dst, char *end)
{
	char *out = *dst;

	if (out < end)
		*out = '\\';
	++out;
	if (out < end)
		*out = ((c >> 6) & 0x07) + '0';
	++out;
	if (out < end)
		*out = ((c >> 3) & 0x07) + '0';
	++out;
	if (out < end)
		*out = ((c >> 0) & 0x07) + '0';
	++out;

	*dst = out;
	return true;
}

static bool escape_hex(unsigned char c, char **dst, char *end)
{
	char *out = *dst;

	if (out < end)
		*out = '\\';
	++out;
	if (out < end)
		*out = 'x';
	++out;
	if (out < end)
		*out = hex_asc_hi(c);
	++out;
	if (out < end)
		*out = hex_asc_lo(c);
	++out;

	*dst = out;
	return true;
}

/**
 * string_escape_mem - quote characters in the given memory buffer
 * @src:	source buffer (unescaped)
 * @isz:	source buffer size
 * @dst:	destination buffer (escaped)
 * @osz:	destination buffer size
 * @flags:	combination of the flags
 * @only:	NULL-terminated string containing characters used to limit
 *		the selected escape class. If characters are included in @only
 *		that would not normally be escaped by the classes selected
 *		in @flags, they will be copied to @dst unescaped.
 *
 * Description:
 * The process of escaping byte buffer includes several parts. They are applied
 * in the following sequence.
 *
 *	1. The character is not matched to the one from @only string and thus
 *	   must go as-is to the output.
 *	2. The character is matched to the printable and ASCII classes, if asked,
 *	   and in case of match it passes through to the output.
 *	3. The character is matched to the printable or ASCII class, if asked,
 *	   and in case of match it passes through to the output.
 *	4. The character is checked if it falls into the class given by @flags.
 *	   %ESCAPE_OCTAL and %ESCAPE_HEX are going last since they cover any
 *	   character. Note that they actually can't go together, otherwise
 *	   %ESCAPE_HEX will be ignored.
 *
 * Caller must provide valid source and destination pointers. Be aware that
 * destination buffer will not be NULL-terminated, thus caller have to append
 * it if needs. The supported flags are::
 *
 *	%ESCAPE_SPACE: (special white space, not space itself)
 *		'\f' - form feed
 *		'\n' - new line
 *		'\r' - carriage return
 *		'\t' - horizontal tab
 *		'\v' - vertical tab
 *	%ESCAPE_SPECIAL:
 *		'\"' - double quote
 *		'\\' - backslash
 *		'\a' - alert (BEL)
 *		'\e' - escape
 *	%ESCAPE_NULL:
 *		'\0' - null
 *	%ESCAPE_OCTAL:
 *		'\NNN' - byte with octal value NNN (3 digits)
 *	%ESCAPE_ANY:
 *		all previous together
 *	%ESCAPE_NP:
 *		escape only non-printable characters, checked by isprint()
 *	%ESCAPE_ANY_NP:
 *		all previous together
 *	%ESCAPE_HEX:
 *		'\xHH' - byte with hexadecimal value HH (2 digits)
 *	%ESCAPE_NA:
 *		escape only non-ascii characters, checked by isascii()
 *	%ESCAPE_NAP:
 *		escape only non-printable or non-ascii characters
 *	%ESCAPE_APPEND:
 *		append characters from @only to be escaped by the given classes
 *
 * %ESCAPE_APPEND would help to pass additional characters to the escaped, when
 * one of %ESCAPE_NP, %ESCAPE_NA, or %ESCAPE_NAP is provided.
 *
 * One notable caveat, the %ESCAPE_NAP, %ESCAPE_NP and %ESCAPE_NA have the
 * higher priority than the rest of the flags (%ESCAPE_NAP is the highest).
 * It doesn't make much sense to use either of them without %ESCAPE_OCTAL
 * or %ESCAPE_HEX, because they cover most of the other character classes.
 * %ESCAPE_NAP can utilize %ESCAPE_SPACE or %ESCAPE_SPECIAL in addition to
 * the above.
 *
 * Return:
 * The total size of the escaped output that would be generated for
 * the given input and flags. To check whether the output was
 * truncated, compare the return value to osz. There is room left in
 * dst for a '\0' terminator if and only if ret < osz.
 */
int string_escape_mem(const char *src, size_t isz, char *dst, size_t osz,
		      unsigned int flags, const char *only)
{
	char *p = dst;
	char *end = p + osz;
	bool is_dict = only && *only;
	bool is_append = flags & ESCAPE_APPEND;

	while (isz--) {
		unsigned char c = *src++;
		bool in_dict = is_dict && strchr(only, c);

		/*
		 * Apply rules in the following sequence:
		 *	- the @only string is supplied and does not contain a
		 *	  character under question
		 *	- the character is printable and ASCII, when @flags has
		 *	  %ESCAPE_NAP bit set
		 *	- the character is printable, when @flags has
		 *	  %ESCAPE_NP bit set
		 *	- the character is ASCII, when @flags has
		 *	  %ESCAPE_NA bit set
		 *	- the character doesn't fall into a class of symbols
		 *	  defined by given @flags
		 * In these cases we just pass through a character to the
		 * output buffer.
		 *
		 * When %ESCAPE_APPEND is passed, the characters from @only
		 * have been excluded from the %ESCAPE_NAP, %ESCAPE_NP, and
		 * %ESCAPE_NA cases.
		 */
		if (!(is_append || in_dict) && is_dict &&
					  escape_passthrough(c, &p, end))
			continue;

		if (!(is_append && in_dict) && isascii(c) && isprint(c) &&
		    flags & ESCAPE_NAP && escape_passthrough(c, &p, end))
			continue;

		if (!(is_append && in_dict) && isprint(c) &&
		    flags & ESCAPE_NP && escape_passthrough(c, &p, end))
			continue;

		if (!(is_append && in_dict) && isascii(c) &&
		    flags & ESCAPE_NA && escape_passthrough(c, &p, end))
			continue;

		if (flags & ESCAPE_SPACE && escape_space(c, &p, end))
			continue;

		if (flags & ESCAPE_SPECIAL && escape_special(c, &p, end))
			continue;

		if (flags & ESCAPE_NULL && escape_null(c, &p, end))
			continue;

		/* ESCAPE_OCTAL and ESCAPE_HEX always go last */
		if (flags & ESCAPE_OCTAL && escape_octal(c, &p, end))
			continue;

		if (flags & ESCAPE_HEX && escape_hex(c, &p, end))
			continue;

		escape_passthrough(c, &p, end);
	}

	return p - dst;
}
EXPORT_SYMBOL(string_escape_mem);

/*
 * Return an allocated string that has been escaped of special characters
 * and double quotes, making it safe to log in quotes.
 */
char *kstrdup_quotable(const char *src, gfp_t gfp)
{
	size_t slen, dlen;
	char *dst;
	const int flags = ESCAPE_HEX;
	const char esc[] = "\f\n\r\t\v\a\e\\\"";

	if (!src)
		return NULL;
	slen = strlen(src);

	dlen = string_escape_mem(src, slen, NULL, 0, flags, esc);
	dst = kmalloc(dlen + 1, gfp);
	if (!dst)
		return NULL;

	WARN_ON(string_escape_mem(src, slen, dst, dlen, flags, esc) != dlen);
	dst[dlen] = '\0';

	return dst;
}
EXPORT_SYMBOL_GPL(kstrdup_quotable);

/*
 * Returns allocated NULL-terminated string containing process
 * command line, with inter-argument NULLs replaced with spaces,
 * and other special characters escaped.
 */
char *kstrdup_quotable_cmdline(struct task_struct *task, gfp_t gfp)
{
	char *buffer, *quoted;
	int i, res;

	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buffer)
		return NULL;

	res = get_cmdline(task, buffer, PAGE_SIZE - 1);
	buffer[res] = '\0';

	/* Collapse trailing NULLs, leave res pointing to last non-NULL. */
	while (--res >= 0 && buffer[res] == '\0')
		;

	/* Replace inter-argument NULLs. */
	for (i = 0; i <= res; i++)
		if (buffer[i] == '\0')
			buffer[i] = ' ';

	/* Make sure result is printable. */
	quoted = kstrdup_quotable(buffer, gfp);
	kfree(buffer);
	return quoted;
}
EXPORT_SYMBOL_GPL(kstrdup_quotable_cmdline);

/*
 * Returns allocated NULL-terminated string containing pathname,
 * with special characters escaped, able to be safely logged. If
 * there is an error, the leading character will be "<".
 */
char *kstrdup_quotable_file(struct file *file, gfp_t gfp)
{
	char *temp, *pathname;

	if (!file)
		return kstrdup("<unknown>", gfp);

	/* We add 11 spaces for ' (deleted)' to be appended */
	temp = kmalloc(PATH_MAX + 11, GFP_KERNEL);
	if (!temp)
		return kstrdup("<no_memory>", gfp);

	pathname = file_path(file, temp, PATH_MAX + 11);
	if (IS_ERR(pathname))
		pathname = kstrdup("<too_long>", gfp);
	else
		pathname = kstrdup_quotable(pathname, gfp);

	kfree(temp);
	return pathname;
}
EXPORT_SYMBOL_GPL(kstrdup_quotable_file);

/**
 * kasprintf_strarray - allocate and fill array of sequential strings
 * @gfp: flags for the slab allocator
 * @prefix: prefix to be used
 * @n: amount of lines to be allocated and filled
 *
 * Allocates and fills @n strings using pattern "%s-%zu", where prefix
 * is provided by caller. The caller is responsible to free them with
 * kfree_strarray() after use.
 *
 * Returns array of strings or NULL when memory can't be allocated.
 */
char **kasprintf_strarray(gfp_t gfp, const char *prefix, size_t n)
{
	char **names;
	size_t i;

	names = kcalloc(n + 1, sizeof(char *), gfp);
	if (!names)
		return NULL;

	for (i = 0; i < n; i++) {
		names[i] = kasprintf(gfp, "%s-%zu", prefix, i);
		if (!names[i]) {
			kfree_strarray(names, i);
			return NULL;
		}
	}

	return names;
}
EXPORT_SYMBOL_GPL(kasprintf_strarray);

/**
 * kfree_strarray - free a number of dynamically allocated strings contained
 *                  in an array and the array itself
 *
 * @array: Dynamically allocated array of strings to free.
 * @n: Number of strings (starting from the beginning of the array) to free.
 *
 * Passing a non-NULL @array and @n == 0 as well as NULL @array are valid
 * use-cases. If @array is NULL, the function does nothing.
 */
void kfree_strarray(char **array, size_t n)
{
	unsigned int i;

	if (!array)
		return;

	for (i = 0; i < n; i++)
		kfree(array[i]);
	kfree(array);
}
EXPORT_SYMBOL_GPL(kfree_strarray);

struct strarray {
	char **array;
	size_t n;
};

static void devm_kfree_strarray(struct device *dev, void *res)
{
	struct strarray *array = res;

	kfree_strarray(array->array, array->n);
}

char **devm_kasprintf_strarray(struct device *dev, const char *prefix, size_t n)
{
	struct strarray *ptr;

	ptr = devres_alloc(devm_kfree_strarray, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	ptr->array = kasprintf_strarray(GFP_KERNEL, prefix, n);
	if (!ptr->array) {
		devres_free(ptr);
		return ERR_PTR(-ENOMEM);
	}

	ptr->n = n;
	devres_add(dev, ptr);

	return ptr->array;
}
EXPORT_SYMBOL_GPL(devm_kasprintf_strarray);

/**
 * strscpy_pad() - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @count: Size of destination buffer
 *
 * Copy the string, or as much of it as fits, into the dest buffer.  The
 * behavior is undefined if the string buffers overlap.  The destination
 * buffer is always %NUL terminated, unless it's zero-sized.
 *
 * If the source string is shorter than the destination buffer, zeros
 * the tail of the destination buffer.
 *
 * For full explanation of why you may want to consider using the
 * 'strscpy' functions please see the function docstring for strscpy().
 *
 * Returns:
 * * The number of characters copied (not including the trailing %NUL)
 * * -E2BIG if count is 0 or @src was truncated.
 */
ssize_t strscpy_pad(char *dest, const char *src, size_t count)
{
	ssize_t written;

	written = strscpy(dest, src, count);
	if (written < 0 || written == count - 1)
		return written;

	memset(dest + written + 1, 0, count - written - 1);

	return written;
}
EXPORT_SYMBOL(strscpy_pad);

/**
 * skip_spaces - Removes leading whitespace from @str.
 * @str: The string to be stripped.
 *
 * Returns a pointer to the first non-whitespace character in @str.
 */
char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}
EXPORT_SYMBOL(skip_spaces);

/**
 * strim - Removes leading and trailing whitespace from @s.
 * @s: The string to be stripped.
 *
 * Note that the first trailing whitespace is replaced with a %NUL-terminator
 * in the given string @s. Returns a pointer to the first non-whitespace
 * character in @s.
 */
char *strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return skip_spaces(s);
}
EXPORT_SYMBOL(strim);

/**
 * sysfs_streq - return true if strings are equal, modulo trailing newline
 * @s1: one string
 * @s2: another string
 *
 * This routine returns true iff two strings are equal, treating both
 * NUL and newline-then-NUL as equivalent string terminations.  It's
 * geared for use with sysfs input strings, which generally terminate
 * with newlines but are compared against values without newlines.
 */
bool sysfs_streq(const char *s1, const char *s2)
{
	while (*s1 && *s1 == *s2) {
		s1++;
		s2++;
	}

	if (*s1 == *s2)
		return true;
	if (!*s1 && *s2 == '\n' && !s2[1])
		return true;
	if (*s1 == '\n' && !s1[1] && !*s2)
		return true;
	return false;
}
EXPORT_SYMBOL(sysfs_streq);

/**
 * match_string - matches given string in an array
 * @array:	array of strings
 * @n:		number of strings in the array or -1 for NULL terminated arrays
 * @string:	string to match with
 *
 * This routine will look for a string in an array of strings up to the
 * n-th element in the array or until the first NULL element.
 *
 * Historically the value of -1 for @n, was used to search in arrays that
 * are NULL terminated. However, the function does not make a distinction
 * when finishing the search: either @n elements have been compared OR
 * the first NULL element was found.
 *
 * Return:
 * index of a @string in the @array if matches, or %-EINVAL otherwise.
 */
int match_string(const char * const *array, size_t n, const char *string)
{
	int index;
	const char *item;

	for (index = 0; index < n; index++) {
		item = array[index];
		if (!item)
			break;
		if (!strcmp(item, string))
			return index;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(match_string);

/**
 * __sysfs_match_string - matches given string in an array
 * @array: array of strings
 * @n: number of strings in the array or -1 for NULL terminated arrays
 * @str: string to match with
 *
 * Returns index of @str in the @array or -EINVAL, just like match_string().
 * Uses sysfs_streq instead of strcmp for matching.
 *
 * This routine will look for a string in an array of strings up to the
 * n-th element in the array or until the first NULL element.
 *
 * Historically the value of -1 for @n, was used to search in arrays that
 * are NULL terminated. However, the function does not make a distinction
 * when finishing the search: either @n elements have been compared OR
 * the first NULL element was found.
 */
int __sysfs_match_string(const char * const *array, size_t n, const char *str)
{
	const char *item;
	int index;

	for (index = 0; index < n; index++) {
		item = array[index];
		if (!item)
			break;
		if (sysfs_streq(item, str))
			return index;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(__sysfs_match_string);

/**
 * strreplace - Replace all occurrences of character in string.
 * @str: The string to operate on.
 * @old: The character being replaced.
 * @new: The character @old is replaced with.
 *
 * Replaces the each @old character with a @new one in the given string @str.
 *
 * Return: pointer to the string @str itself.
 */
char *strreplace(char *str, char old, char new)
{
	char *s = str;

	for (; *s; ++s)
		if (*s == old)
			*s = new;
	return str;
}
EXPORT_SYMBOL(strreplace);

/**
 * memcpy_and_pad - Copy one buffer to another with padding
 * @dest: Where to copy to
 * @dest_len: The destination buffer size
 * @src: Where to copy from
 * @count: The number of bytes to copy
 * @pad: Character to use for padding if space is left in destination.
 */
void memcpy_and_pad(void *dest, size_t dest_len, const void *src, size_t count,
		    int pad)
{
	if (dest_len > count) {
		memcpy(dest, src, count);
		memset(dest + count, pad,  dest_len - count);
	} else {
		memcpy(dest, src, dest_len);
	}
}
EXPORT_SYMBOL(memcpy_and_pad);

#ifdef CONFIG_FORTIFY_SOURCE
/* These are placeholders for fortify compile-time warnings. */
void __read_overflow2_field(size_t avail, size_t wanted) { }
EXPORT_SYMBOL(__read_overflow2_field);
void __write_overflow_field(size_t avail, size_t wanted) { }
EXPORT_SYMBOL(__write_overflow_field);

void fortify_panic(const char *name)
{
	pr_emerg("detected buffer overflow in %s\n", name);
	BUG();
}
EXPORT_SYMBOL(fortify_panic);
#endif /* CONFIG_FORTIFY_SOURCE */
