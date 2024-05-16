// SPDX-License-Identifier: GPL-2.0
#include <linux/ucs2_string.h>
#include <linux/module.h>

/* Return the number of unicode characters in data */
unsigned long
ucs2_strnlen(const ucs2_char_t *s, size_t maxlength)
{
        unsigned long length = 0;

        while (*s++ != 0 && length < maxlength)
                length++;
        return length;
}
EXPORT_SYMBOL(ucs2_strnlen);

unsigned long
ucs2_strlen(const ucs2_char_t *s)
{
        return ucs2_strnlen(s, ~0UL);
}
EXPORT_SYMBOL(ucs2_strlen);

/*
 * Return the number of bytes is the length of this string
 * Note: this is NOT the same as the number of unicode characters
 */
unsigned long
ucs2_strsize(const ucs2_char_t *data, unsigned long maxlength)
{
        return ucs2_strnlen(data, maxlength/sizeof(ucs2_char_t)) * sizeof(ucs2_char_t);
}
EXPORT_SYMBOL(ucs2_strsize);

/**
 * ucs2_strscpy() - Copy a UCS2 string into a sized buffer.
 *
 * @dst: Pointer to the destination buffer where to copy the string to.
 * @src: Pointer to the source buffer where to copy the string from.
 * @count: Size of the destination buffer, in UCS2 (16-bit) characters.
 *
 * Like strscpy(), only for UCS2 strings.
 *
 * Copy the source string @src, or as much of it as fits, into the destination
 * buffer @dst. The behavior is undefined if the string buffers overlap. The
 * destination buffer @dst is always NUL-terminated, unless it's zero-sized.
 *
 * Return: The number of characters copied into @dst (excluding the trailing
 * %NUL terminator) or -E2BIG if @count is 0 or @src was truncated due to the
 * destination buffer being too small.
 */
ssize_t ucs2_strscpy(ucs2_char_t *dst, const ucs2_char_t *src, size_t count)
{
	long res;

	/*
	 * Ensure that we have a valid amount of space. We need to store at
	 * least one NUL-character.
	 */
	if (count == 0 || WARN_ON_ONCE(count > INT_MAX / sizeof(*dst)))
		return -E2BIG;

	/*
	 * Copy at most 'count' characters, return early if we find a
	 * NUL-terminator.
	 */
	for (res = 0; res < count; res++) {
		ucs2_char_t c;

		c = src[res];
		dst[res] = c;

		if (!c)
			return res;
	}

	/*
	 * The loop above terminated without finding a NUL-terminator,
	 * exceeding the 'count': Enforce proper NUL-termination and return
	 * error.
	 */
	dst[count - 1] = 0;
	return -E2BIG;
}
EXPORT_SYMBOL(ucs2_strscpy);

int
ucs2_strncmp(const ucs2_char_t *a, const ucs2_char_t *b, size_t len)
{
        while (1) {
                if (len == 0)
                        return 0;
                if (*a < *b)
                        return -1;
                if (*a > *b)
                        return 1;
                if (*a == 0) /* implies *b == 0 */
                        return 0;
                a++;
                b++;
                len--;
        }
}
EXPORT_SYMBOL(ucs2_strncmp);

unsigned long
ucs2_utf8size(const ucs2_char_t *src)
{
	unsigned long i;
	unsigned long j = 0;

	for (i = 0; src[i]; i++) {
		u16 c = src[i];

		if (c >= 0x800)
			j += 3;
		else if (c >= 0x80)
			j += 2;
		else
			j += 1;
	}

	return j;
}
EXPORT_SYMBOL(ucs2_utf8size);

/*
 * copy at most maxlength bytes of whole utf8 characters to dest from the
 * ucs2 string src.
 *
 * The return value is the number of characters copied, not including the
 * final NUL character.
 */
unsigned long
ucs2_as_utf8(u8 *dest, const ucs2_char_t *src, unsigned long maxlength)
{
	unsigned int i;
	unsigned long j = 0;
	unsigned long limit = ucs2_strnlen(src, maxlength);

	for (i = 0; maxlength && i < limit; i++) {
		u16 c = src[i];

		if (c >= 0x800) {
			if (maxlength < 3)
				break;
			maxlength -= 3;
			dest[j++] = 0xe0 | (c & 0xf000) >> 12;
			dest[j++] = 0x80 | (c & 0x0fc0) >> 6;
			dest[j++] = 0x80 | (c & 0x003f);
		} else if (c >= 0x80) {
			if (maxlength < 2)
				break;
			maxlength -= 2;
			dest[j++] = 0xc0 | (c & 0x7c0) >> 6;
			dest[j++] = 0x80 | (c & 0x03f);
		} else {
			maxlength -= 1;
			dest[j++] = c & 0x7f;
		}
	}
	if (maxlength)
		dest[j] = '\0';
	return j;
}
EXPORT_SYMBOL(ucs2_as_utf8);

MODULE_LICENSE("GPL v2");
