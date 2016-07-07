#include <linux/compiler.h>
#include <linux/export.h>
#include <linux/kasan-checks.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include <asm/byteorder.h>
#include <asm/word-at-a-time.h>

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
#define IS_UNALIGNED(src, dst)	0
#else
#define IS_UNALIGNED(src, dst)	\
	(((long) dst | (long) src) & (sizeof(long) - 1))
#endif

/*
 * Do a strncpy, return length of string without final '\0'.
 * 'count' is the user-supplied count (return 'count' if we
 * hit it), 'max' is the address space maximum (and we return
 * -EFAULT if we hit it).
 */
static inline long do_strncpy_from_user(char *dst, const char __user *src, long count, unsigned long max)
{
	const struct word_at_a_time constants = WORD_AT_A_TIME_CONSTANTS;
	long res = 0;

	/*
	 * Truncate 'max' to the user-specified limit, so that
	 * we only have one limit we need to check in the loop
	 */
	if (max > count)
		max = count;

	if (IS_UNALIGNED(src, dst))
		goto byte_at_a_time;

	while (max >= sizeof(unsigned long)) {
		unsigned long c, data;

		/* Fall back to byte-at-a-time if we get a page fault */
		if (unlikely(unsafe_get_user(c,(unsigned long __user *)(src+res))))
			break;
		*(unsigned long *)(dst+res) = c;
		if (has_zero(c, &data, &constants)) {
			data = prep_zero_mask(c, data, &constants);
			data = create_zero_mask(data);
			return res + find_zero(data);
		}
		res += sizeof(unsigned long);
		max -= sizeof(unsigned long);
	}

byte_at_a_time:
	while (max) {
		char c;

		if (unlikely(unsafe_get_user(c,src+res)))
			return -EFAULT;
		dst[res] = c;
		if (!c)
			return res;
		res++;
		max--;
	}

	/*
	 * Uhhuh. We hit 'max'. But was that the user-specified maximum
	 * too? If so, that's ok - we got as much as the user asked for.
	 */
	if (res >= count)
		return res;

	/*
	 * Nope: we hit the address space limit, and we still had more
	 * characters the caller would have wanted. That's an EFAULT.
	 */
	return -EFAULT;
}

/**
 * strncpy_from_user: - Copy a NUL terminated string from userspace.
 * @dst:   Destination address, in kernel space.  This buffer must be at
 *         least @count bytes long.
 * @src:   Source address, in user space.
 * @count: Maximum number of bytes to copy, including the trailing NUL.
 *
 * Copies a NUL-terminated string from userspace to kernel space.
 *
 * On success, returns the length of the string (not including the trailing
 * NUL).
 *
 * If access to userspace fails, returns -EFAULT (some data may have been
 * copied).
 *
 * If @count is smaller than the length of the string, copies @count bytes
 * and returns @count.
 */
long strncpy_from_user(char *dst, const char __user *src, long count)
{
	unsigned long max_addr, src_addr;

	if (unlikely(count <= 0))
		return 0;

	max_addr = user_addr_max();
	src_addr = (unsigned long)src;
	if (likely(src_addr < max_addr)) {
		unsigned long max = max_addr - src_addr;
		long retval;

		kasan_check_write(dst, count);
		user_access_begin();
		retval = do_strncpy_from_user(dst, src, count, max);
		user_access_end();
		return retval;
	}
	return -EFAULT;
}
EXPORT_SYMBOL(strncpy_from_user);
