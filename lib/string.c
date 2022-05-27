// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/lib/string.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This file should be used only for "library" routines that may have
 * alternative implementations on specific architectures (generally
 * found in <asm-xx/string.h>), or get overloaded by FORTIFY_SOURCE.
 * (Specifically, this file is built with __NO_FORTIFY.)
 *
 * Other helper functions should live in string_helpers.c.
 */

#define __NO_FORTIFY
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/bug.h>
#include <linux/errno.h>
#include <linux/slab.h>

#include <asm/unaligned.h>
#include <asm/byteorder.h>
#include <asm/word-at-a-time.h>
#include <asm/page.h>

#ifndef __HAVE_ARCH_STRNCASECMP
/**
 * strncasecmp - Case insensitive, length-limited string comparison
 * @s1: One string
 * @s2: The other string
 * @len: the maximum number of characters to compare
 */
int strncasecmp(const char *s1, const char *s2, size_t len)
{
	/* Yes, Virginia, it had better be unsigned */
	unsigned char c1, c2;

	if (!len)
		return 0;

	do {
		c1 = *s1++;
		c2 = *s2++;
		if (!c1 || !c2)
			break;
		if (c1 == c2)
			continue;
		c1 = tolower(c1);
		c2 = tolower(c2);
		if (c1 != c2)
			break;
	} while (--len);
	return (int)c1 - (int)c2;
}
EXPORT_SYMBOL(strncasecmp);
#endif

#ifndef __HAVE_ARCH_STRCASECMP
int strcasecmp(const char *s1, const char *s2)
{
	int c1, c2;

	do {
		c1 = tolower(*s1++);
		c2 = tolower(*s2++);
	} while (c1 == c2 && c1 != 0);
	return c1 - c2;
}
EXPORT_SYMBOL(strcasecmp);
#endif

#ifndef __HAVE_ARCH_STRCPY
/**
 * strcpy - Copy a %NUL terminated string
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 */
char *strcpy(char *dest, const char *src)
{
	char *tmp = dest;

	while ((*dest++ = *src++) != '\0')
		/* nothing */;
	return tmp;
}
EXPORT_SYMBOL(strcpy);
#endif

#ifndef __HAVE_ARCH_STRNCPY
/**
 * strncpy - Copy a length-limited, C-string
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @count: The maximum number of bytes to copy
 *
 * The result is not %NUL-terminated if the source exceeds
 * @count bytes.
 *
 * In the case where the length of @src is less than  that  of
 * count, the remainder of @dest will be padded with %NUL.
 *
 */
char *strncpy(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	while (count) {
		if ((*tmp = *src) != 0)
			src++;
		tmp++;
		count--;
	}
	return dest;
}
EXPORT_SYMBOL(strncpy);
#endif

#ifndef __HAVE_ARCH_STRLCPY
/**
 * strlcpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer
 *
 * Compatible with ``*BSD``: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero). It does not pad
 * out the result like strncpy() does.
 */
size_t strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}
EXPORT_SYMBOL(strlcpy);
#endif

#ifndef __HAVE_ARCH_STRSCPY
/**
 * strscpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @count: Size of destination buffer
 *
 * Copy the string, or as much of it as fits, into the dest buffer.  The
 * behavior is undefined if the string buffers overlap.  The destination
 * buffer is always NUL terminated, unless it's zero-sized.
 *
 * Preferred to strlcpy() since the API doesn't require reading memory
 * from the src string beyond the specified "count" bytes, and since
 * the return value is easier to error-check than strlcpy()'s.
 * In addition, the implementation is robust to the string changing out
 * from underneath it, unlike the current strlcpy() implementation.
 *
 * Preferred to strncpy() since it always returns a valid string, and
 * doesn't unnecessarily force the tail of the destination buffer to be
 * zeroed.  If zeroing is desired please use strscpy_pad().
 *
 * Returns:
 * * The number of characters copied (not including the trailing %NUL)
 * * -E2BIG if count is 0 or @src was truncated.
 */
ssize_t strscpy(char *dest, const char *src, size_t count)
{
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;
	size_t max = count;
	long res = 0;

	if (count == 0 || WARN_ON_ONCE(count > INT_MAX))
		return -E2BIG;

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	/*
	 * If src is unaligned, don't cross a page boundary,
	 * since we don't know if the next page is mapped.
	 */
	if ((long)src & (sizeof(long) - 1)) {
		size_t limit = PAGE_SIZE - ((long)src & (PAGE_SIZE - 1));
		if (limit < max)
			max = limit;
	}
#else
	/* If src or dest is unaligned, don't do word-at-a-time. */
	if (((long) dest | (long) src) & (sizeof(long) - 1))
		max = 0;
#endif

	while (max >= sizeof(unsigned long)) {
		unsigned long c, data;

		c = read_word_at_a_time(src+res);
		if (has_zero(c, &data, &constants)) {
			data = prep_zero_mask(c, data, &constants);
			data = create_zero_mask(data);
			*(unsigned long *)(dest+res) = c & zero_bytemask(data);
			return res + find_zero(data);
		}
		*(unsigned long *)(dest+res) = c;
		res += sizeof(unsigned long);
		count -= sizeof(unsigned long);
		max -= sizeof(unsigned long);
	}

	while (count) {
		char c;

		c = src[res];
		dest[res] = c;
		if (!c)
			return res;
		res++;
		count--;
	}

	/* Hit buffer length without finding a NUL; force NUL-termination. */
	if (res)
		dest[res-1] = '\0';

	return -E2BIG;
}
EXPORT_SYMBOL(strscpy);
#endif

/**
 * stpcpy - copy a string from src to dest returning a pointer to the new end
 *          of dest, including src's %NUL-terminator. May overrun dest.
 * @dest: pointer to end of string being copied into. Must be large enough
 *        to receive copy.
 * @src: pointer to the beginning of string being copied from. Must not overlap
 *       dest.
 *
 * stpcpy differs from strcpy in a key way: the return value is a pointer
 * to the new %NUL-terminating character in @dest. (For strcpy, the return
 * value is a pointer to the start of @dest). This interface is considered
 * unsafe as it doesn't perform bounds checking of the inputs. As such it's
 * not recommended for usage. Instead, its definition is provided in case
 * the compiler lowers other libcalls to stpcpy.
 */
char *stpcpy(char *__restrict__ dest, const char *__restrict__ src);
char *stpcpy(char *__restrict__ dest, const char *__restrict__ src)
{
	while ((*dest++ = *src++) != '\0')
		/* nothing */;
	return --dest;
}
EXPORT_SYMBOL(stpcpy);

#ifndef __HAVE_ARCH_STRCAT
/**
 * strcat - Append one %NUL-terminated string to another
 * @dest: The string to be appended to
 * @src: The string to append to it
 */
char *strcat(char *dest, const char *src)
{
	char *tmp = dest;

	while (*dest)
		dest++;
	while ((*dest++ = *src++) != '\0')
		;
	return tmp;
}
EXPORT_SYMBOL(strcat);
#endif

#ifndef __HAVE_ARCH_STRNCAT
/**
 * strncat - Append a length-limited, C-string to another
 * @dest: The string to be appended to
 * @src: The string to append to it
 * @count: The maximum numbers of bytes to copy
 *
 * Note that in contrast to strncpy(), strncat() ensures the result is
 * terminated.
 */
char *strncat(char *dest, const char *src, size_t count)
{
	char *tmp = dest;

	if (count) {
		while (*dest)
			dest++;
		while ((*dest++ = *src++) != 0) {
			if (--count == 0) {
				*dest = '\0';
				break;
			}
		}
	}
	return tmp;
}
EXPORT_SYMBOL(strncat);
#endif

#ifndef __HAVE_ARCH_STRLCAT
/**
 * strlcat - Append a length-limited, C-string to another
 * @dest: The string to be appended to
 * @src: The string to append to it
 * @count: The size of the destination buffer.
 */
size_t strlcat(char *dest, const char *src, size_t count)
{
	size_t dsize = strlen(dest);
	size_t len = strlen(src);
	size_t res = dsize + len;

	/* This would be a bug */
	BUG_ON(dsize >= count);

	dest += dsize;
	count -= dsize;
	if (len >= count)
		len = count-1;
	memcpy(dest, src, len);
	dest[len] = 0;
	return res;
}
EXPORT_SYMBOL(strlcat);
#endif

#ifndef __HAVE_ARCH_STRCMP
/**
 * strcmp - Compare two strings
 * @cs: One string
 * @ct: Another string
 */
int strcmp(const char *cs, const char *ct)
{
	unsigned char c1, c2;

	while (1) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
	}
	return 0;
}
EXPORT_SYMBOL(strcmp);
#endif

#ifndef __HAVE_ARCH_STRNCMP
/**
 * strncmp - Compare two length-limited strings
 * @cs: One string
 * @ct: Another string
 * @count: The maximum number of bytes to compare
 */
int strncmp(const char *cs, const char *ct, size_t count)
{
	unsigned char c1, c2;

	while (count) {
		c1 = *cs++;
		c2 = *ct++;
		if (c1 != c2)
			return c1 < c2 ? -1 : 1;
		if (!c1)
			break;
		count--;
	}
	return 0;
}
EXPORT_SYMBOL(strncmp);
#endif

#ifndef __HAVE_ARCH_STRCHR
/**
 * strchr - Find the first occurrence of a character in a string
 * @s: The string to be searched
 * @c: The character to search for
 *
 * Note that the %NUL-terminator is considered part of the string, and can
 * be searched for.
 */
char *strchr(const char *s, int c)
{
	for (; *s != (char)c; ++s)
		if (*s == '\0')
			return NULL;
	return (char *)s;
}
EXPORT_SYMBOL(strchr);
#endif

#ifndef __HAVE_ARCH_STRCHRNUL
/**
 * strchrnul - Find and return a character in a string, or end of string
 * @s: The string to be searched
 * @c: The character to search for
 *
 * Returns pointer to first occurrence of 'c' in s. If c is not found, then
 * return a pointer to the null byte at the end of s.
 */
char *strchrnul(const char *s, int c)
{
	while (*s && *s != (char)c)
		s++;
	return (char *)s;
}
EXPORT_SYMBOL(strchrnul);
#endif

/**
 * strnchrnul - Find and return a character in a length limited string,
 * or end of string
 * @s: The string to be searched
 * @count: The number of characters to be searched
 * @c: The character to search for
 *
 * Returns pointer to the first occurrence of 'c' in s. If c is not found,
 * then return a pointer to the last character of the string.
 */
char *strnchrnul(const char *s, size_t count, int c)
{
	while (count-- && *s && *s != (char)c)
		s++;
	return (char *)s;
}

#ifndef __HAVE_ARCH_STRRCHR
/**
 * strrchr - Find the last occurrence of a character in a string
 * @s: The string to be searched
 * @c: The character to search for
 */
char *strrchr(const char *s, int c)
{
	const char *last = NULL;
	do {
		if (*s == (char)c)
			last = s;
	} while (*s++);
	return (char *)last;
}
EXPORT_SYMBOL(strrchr);
#endif

#ifndef __HAVE_ARCH_STRNCHR
/**
 * strnchr - Find a character in a length limited string
 * @s: The string to be searched
 * @count: The number of characters to be searched
 * @c: The character to search for
 *
 * Note that the %NUL-terminator is considered part of the string, and can
 * be searched for.
 */
char *strnchr(const char *s, size_t count, int c)
{
	while (count--) {
		if (*s == (char)c)
			return (char *)s;
		if (*s++ == '\0')
			break;
	}
	return NULL;
}
EXPORT_SYMBOL(strnchr);
#endif

#ifndef __HAVE_ARCH_STRLEN
/**
 * strlen - Find the length of a string
 * @s: The string to be sized
 */
size_t strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}
EXPORT_SYMBOL(strlen);
#endif

#ifndef __HAVE_ARCH_STRNLEN
/**
 * strnlen - Find the length of a length-limited string
 * @s: The string to be sized
 * @count: The maximum number of bytes to search
 */
size_t strnlen(const char *s, size_t count)
{
	const char *sc;

	for (sc = s; count-- && *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}
EXPORT_SYMBOL(strnlen);
#endif

#ifndef __HAVE_ARCH_STRSPN
/**
 * strspn - Calculate the length of the initial substring of @s which only contain letters in @accept
 * @s: The string to be searched
 * @accept: The string to search for
 */
size_t strspn(const char *s, const char *accept)
{
	const char *p;

	for (p = s; *p != '\0'; ++p) {
		if (!strchr(accept, *p))
			break;
	}
	return p - s;
}
EXPORT_SYMBOL(strspn);
#endif

#ifndef __HAVE_ARCH_STRCSPN
/**
 * strcspn - Calculate the length of the initial substring of @s which does not contain letters in @reject
 * @s: The string to be searched
 * @reject: The string to avoid
 */
size_t strcspn(const char *s, const char *reject)
{
	const char *p;

	for (p = s; *p != '\0'; ++p) {
		if (strchr(reject, *p))
			break;
	}
	return p - s;
}
EXPORT_SYMBOL(strcspn);
#endif

#ifndef __HAVE_ARCH_STRPBRK
/**
 * strpbrk - Find the first occurrence of a set of characters
 * @cs: The string to be searched
 * @ct: The characters to search for
 */
char *strpbrk(const char *cs, const char *ct)
{
	const char *sc1, *sc2;

	for (sc1 = cs; *sc1 != '\0'; ++sc1) {
		for (sc2 = ct; *sc2 != '\0'; ++sc2) {
			if (*sc1 == *sc2)
				return (char *)sc1;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(strpbrk);
#endif

#ifndef __HAVE_ARCH_STRSEP
/**
 * strsep - Split a string into tokens
 * @s: The string to be searched
 * @ct: The characters to search for
 *
 * strsep() updates @s to point after the token, ready for the next call.
 *
 * It returns empty tokens, too, behaving exactly like the libc function
 * of that name. In fact, it was stolen from glibc2 and de-fancy-fied.
 * Same semantics, slimmer shape. ;)
 */
char *strsep(char **s, const char *ct)
{
	char *sbegin = *s;
	char *end;

	if (sbegin == NULL)
		return NULL;

	end = strpbrk(sbegin, ct);
	if (end)
		*end++ = '\0';
	*s = end;
	return sbegin;
}
EXPORT_SYMBOL(strsep);
#endif

#ifndef __HAVE_ARCH_MEMSET
/**
 * memset - Fill a region of memory with the given value
 * @s: Pointer to the start of the area.
 * @c: The byte to fill the area with
 * @count: The size of the area.
 *
 * Do not use memset() to access IO space, use memset_io() instead.
 */
void *memset(void *s, int c, size_t count)
{
	char *xs = s;

	while (count--)
		*xs++ = c;
	return s;
}
EXPORT_SYMBOL(memset);
#endif

#ifndef __HAVE_ARCH_MEMSET16
/**
 * memset16() - Fill a memory area with a uint16_t
 * @s: Pointer to the start of the area.
 * @v: The value to fill the area with
 * @count: The number of values to store
 *
 * Differs from memset() in that it fills with a uint16_t instead
 * of a byte.  Remember that @count is the number of uint16_ts to
 * store, not the number of bytes.
 */
void *memset16(uint16_t *s, uint16_t v, size_t count)
{
	uint16_t *xs = s;

	while (count--)
		*xs++ = v;
	return s;
}
EXPORT_SYMBOL(memset16);
#endif

#ifndef __HAVE_ARCH_MEMSET32
/**
 * memset32() - Fill a memory area with a uint32_t
 * @s: Pointer to the start of the area.
 * @v: The value to fill the area with
 * @count: The number of values to store
 *
 * Differs from memset() in that it fills with a uint32_t instead
 * of a byte.  Remember that @count is the number of uint32_ts to
 * store, not the number of bytes.
 */
void *memset32(uint32_t *s, uint32_t v, size_t count)
{
	uint32_t *xs = s;

	while (count--)
		*xs++ = v;
	return s;
}
EXPORT_SYMBOL(memset32);
#endif

#ifndef __HAVE_ARCH_MEMSET64
/**
 * memset64() - Fill a memory area with a uint64_t
 * @s: Pointer to the start of the area.
 * @v: The value to fill the area with
 * @count: The number of values to store
 *
 * Differs from memset() in that it fills with a uint64_t instead
 * of a byte.  Remember that @count is the number of uint64_ts to
 * store, not the number of bytes.
 */
void *memset64(uint64_t *s, uint64_t v, size_t count)
{
	uint64_t *xs = s;

	while (count--)
		*xs++ = v;
	return s;
}
EXPORT_SYMBOL(memset64);
#endif

#ifndef __HAVE_ARCH_MEMCPY
/**
 * memcpy - Copy one area of memory to another
 * @dest: Where to copy to
 * @src: Where to copy from
 * @count: The size of the area.
 *
 * You should not use this function to access IO space, use memcpy_toio()
 * or memcpy_fromio() instead.
 */
void *memcpy(void *dest, const void *src, size_t count)
{
	char *tmp = dest;
	const char *s = src;

	while (count--)
		*tmp++ = *s++;
	return dest;
}
EXPORT_SYMBOL(memcpy);
#endif

#ifndef __HAVE_ARCH_MEMMOVE
/**
 * memmove - Copy one area of memory to another
 * @dest: Where to copy to
 * @src: Where to copy from
 * @count: The size of the area.
 *
 * Unlike memcpy(), memmove() copes with overlapping areas.
 */
void *memmove(void *dest, const void *src, size_t count)
{
	char *tmp;
	const char *s;

	if (dest <= src) {
		tmp = dest;
		s = src;
		while (count--)
			*tmp++ = *s++;
	} else {
		tmp = dest;
		tmp += count;
		s = src;
		s += count;
		while (count--)
			*--tmp = *--s;
	}
	return dest;
}
EXPORT_SYMBOL(memmove);
#endif

#ifndef __HAVE_ARCH_MEMCMP
/**
 * memcmp - Compare two areas of memory
 * @cs: One area of memory
 * @ct: Another area of memory
 * @count: The size of the area.
 */
#undef memcmp
__visible int memcmp(const void *cs, const void *ct, size_t count)
{
	const unsigned char *su1, *su2;
	int res = 0;

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (count >= sizeof(unsigned long)) {
		const unsigned long *u1 = cs;
		const unsigned long *u2 = ct;
		do {
			if (get_unaligned(u1) != get_unaligned(u2))
				break;
			u1++;
			u2++;
			count -= sizeof(unsigned long);
		} while (count >= sizeof(unsigned long));
		cs = u1;
		ct = u2;
	}
#endif
	for (su1 = cs, su2 = ct; 0 < count; ++su1, ++su2, count--)
		if ((res = *su1 - *su2) != 0)
			break;
	return res;
}
EXPORT_SYMBOL(memcmp);
#endif

#ifndef __HAVE_ARCH_BCMP
/**
 * bcmp - returns 0 if and only if the buffers have identical contents.
 * @a: pointer to first buffer.
 * @b: pointer to second buffer.
 * @len: size of buffers.
 *
 * The sign or magnitude of a non-zero return value has no particular
 * meaning, and architectures may implement their own more efficient bcmp(). So
 * while this particular implementation is a simple (tail) call to memcmp, do
 * not rely on anything but whether the return value is zero or non-zero.
 */
int bcmp(const void *a, const void *b, size_t len)
{
	return memcmp(a, b, len);
}
EXPORT_SYMBOL(bcmp);
#endif

#ifndef __HAVE_ARCH_MEMSCAN
/**
 * memscan - Find a character in an area of memory.
 * @addr: The memory area
 * @c: The byte to search for
 * @size: The size of the area.
 *
 * returns the address of the first occurrence of @c, or 1 byte past
 * the area if @c is not found
 */
void *memscan(void *addr, int c, size_t size)
{
	unsigned char *p = addr;

	while (size) {
		if (*p == (unsigned char)c)
			return (void *)p;
		p++;
		size--;
	}
  	return (void *)p;
}
EXPORT_SYMBOL(memscan);
#endif

#ifndef __HAVE_ARCH_STRSTR
/**
 * strstr - Find the first substring in a %NUL terminated string
 * @s1: The string to be searched
 * @s2: The string to search for
 */
char *strstr(const char *s1, const char *s2)
{
	size_t l1, l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	l1 = strlen(s1);
	while (l1 >= l2) {
		l1--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}
EXPORT_SYMBOL(strstr);
#endif

#ifndef __HAVE_ARCH_STRNSTR
/**
 * strnstr - Find the first substring in a length-limited string
 * @s1: The string to be searched
 * @s2: The string to search for
 * @len: the maximum number of characters to search
 */
char *strnstr(const char *s1, const char *s2, size_t len)
{
	size_t l2;

	l2 = strlen(s2);
	if (!l2)
		return (char *)s1;
	while (len >= l2) {
		len--;
		if (!memcmp(s1, s2, l2))
			return (char *)s1;
		s1++;
	}
	return NULL;
}
EXPORT_SYMBOL(strnstr);
#endif

#ifndef __HAVE_ARCH_MEMCHR
/**
 * memchr - Find a character in an area of memory.
 * @s: The memory area
 * @c: The byte to search for
 * @n: The size of the area.
 *
 * returns the address of the first occurrence of @c, or %NULL
 * if @c is not found
 */
void *memchr(const void *s, int c, size_t n)
{
	const unsigned char *p = s;
	while (n-- != 0) {
        	if ((unsigned char)c == *p++) {
			return (void *)(p - 1);
		}
	}
	return NULL;
}
EXPORT_SYMBOL(memchr);
#endif

static void *check_bytes8(const u8 *start, u8 value, unsigned int bytes)
{
	while (bytes) {
		if (*start != value)
			return (void *)start;
		start++;
		bytes--;
	}
	return NULL;
}

/**
 * memchr_inv - Find an unmatching character in an area of memory.
 * @start: The memory area
 * @c: Find a character other than c
 * @bytes: The size of the area.
 *
 * returns the address of the first character other than @c, or %NULL
 * if the whole buffer contains just @c.
 */
void *memchr_inv(const void *start, int c, size_t bytes)
{
	u8 value = c;
	u64 value64;
	unsigned int words, prefix;

	if (bytes <= 16)
		return check_bytes8(start, value, bytes);

	value64 = value;
#if defined(CONFIG_ARCH_HAS_FAST_MULTIPLIER) && BITS_PER_LONG == 64
	value64 *= 0x0101010101010101ULL;
#elif defined(CONFIG_ARCH_HAS_FAST_MULTIPLIER)
	value64 *= 0x01010101;
	value64 |= value64 << 32;
#else
	value64 |= value64 << 8;
	value64 |= value64 << 16;
	value64 |= value64 << 32;
#endif

	prefix = (unsigned long)start % 8;
	if (prefix) {
		u8 *r;

		prefix = 8 - prefix;
		r = check_bytes8(start, value, prefix);
		if (r)
			return r;
		start += prefix;
		bytes -= prefix;
	}

	words = bytes / 8;

	while (words) {
		if (*(u64 *)start != value64)
			return check_bytes8(start, value, 8);
		start += 8;
		words--;
	}

	return check_bytes8(start, value, bytes % 8);
}
EXPORT_SYMBOL(memchr_inv);
