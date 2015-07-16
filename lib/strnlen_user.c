#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/uaccess.h>

#include <asm/word-at-a-time.h>

/* Set bits in the first 'n' bytes when loaded from memory */
#ifdef __LITTLE_ENDIAN
#  define aligned_byte_mask(n) ((1ul << 8*(n))-1)
#else
#  define aligned_byte_mask(n) (~0xfful << (BITS_PER_LONG - 8 - 8*(n)))
#endif

/*
 * Do a strnlen, return length of string *with* final '\0'.
 * 'count' is the user-supplied count, while 'max' is the
 * address space maximum.
 *
 * Return 0 for exceptions (which includes hitting the address
 * space maximum), or 'count+1' if hitting the user-supplied
 * maximum count.
 *
 * NOTE! We can sometimes overshoot the user-supplied maximum
 * if it fits in a aligned 'long'. The caller needs to check
 * the return value against "> max".
 */
static inline long do_strnlen_user(const char __user *src, unsigned long count, unsigned long max)
{
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;
	long align, res = 0;
	unsigned long c;

	/*
	 * Truncate 'max' to the user-specified limit, so that
	 * we only have one limit we need to check in the loop
	 */
	if (max > count)
		max = count;

	/*
	 * Do everything aligned. But that means that we
	 * need to also expand the maximum..
	 */
	align = (sizeof(long) - 1) & (unsigned long)src;
	src -= align;
	max += align;

	if (unlikely(__get_user(c,(unsigned long __user *)src)))
		return 0;
	c |= aligned_byte_mask(align);

	for (;;) {
		unsigned long data;
		if (has_zero(c, &data, &constants)) {
			data = prep_zero_mask(c, data, &constants);
			data = create_zero_mask(data);
			return res + find_zero(data) + 1 - align;
		}
		res += sizeof(unsigned long);
		/* We already handled 'unsigned long' bytes. Did we do it all ? */
		if (unlikely(max <= sizeof(unsigned long)))
			break;
		max -= sizeof(unsigned long);
		if (unlikely(__get_user(c,(unsigned long __user *)(src+res))))
			return 0;
	}
	res -= align;

	/*
	 * Uhhuh. We hit 'max'. But was that the user-specified maximum
	 * too? If so, return the marker for "too long".
	 */
	if (res >= count)
		return count+1;

	/*
	 * Nope: we hit the address space limit, and we still had more
	 * characters the caller would have wanted. That's 0.
	 */
	return 0;
}

/**
 * strnlen_user: - Get the size of a user string INCLUDING final NUL.
 * @str: The string to measure.
 * @count: Maximum count (including NUL character)
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * If the string is too long, returns a number larger than @count. User
 * has to check the return value against "> count".
 * On exception (or invalid count), returns 0.
 *
 * NOTE! You should basically never use this function. There is
 * almost never any valid case for using the length of a user space
 * string, since the string can be changed at any time by other
 * threads. Use "strncpy_from_user()" instead to get a stable copy
 * of the string.
 */
long strnlen_user(const char __user *str, long count)
{
	unsigned long max_addr, src_addr;

	if (unlikely(count <= 0))
		return 0;

	max_addr = user_addr_max();
	src_addr = (unsigned long)str;
	if (likely(src_addr < max_addr)) {
		unsigned long max = max_addr - src_addr;
		return do_strnlen_user(str, count, max);
	}
	return 0;
}
EXPORT_SYMBOL(strnlen_user);

/**
 * strlen_user: - Get the size of a user string INCLUDING final NUL.
 * @str: The string to measure.
 *
 * Context: User context only. This function may sleep if pagefaults are
 *          enabled.
 *
 * Get the size of a NUL-terminated string in user space.
 *
 * Returns the size of the string INCLUDING the terminating NUL.
 * On exception, returns 0.
 *
 * If there is a limit on the length of a valid string, you may wish to
 * consider using strnlen_user() instead.
 */
long strlen_user(const char __user *str)
{
	unsigned long max_addr, src_addr;

	max_addr = user_addr_max();
	src_addr = (unsigned long)str;
	if (likely(src_addr < max_addr)) {
		unsigned long max = max_addr - src_addr;
		return do_strnlen_user(str, ~0ul, max);
	}
	return 0;
}
EXPORT_SYMBOL(strlen_user);
