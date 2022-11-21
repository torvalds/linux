// SPDX-License-Identifier: GPL-2.0
#include <linux/bitops.h>
#include <linux/fault-inject-usercopy.h>
#include <linux/instrumented.h>
#include <linux/uaccess.h>

/* out-of-line parts */

#ifndef INLINE_COPY_FROM_USER
unsigned long _copy_from_user(void *to, const void __user *from, unsigned long n)
{
	unsigned long res = n;
	might_fault();
	if (!should_fail_usercopy() && likely(access_ok(from, n))) {
		instrument_copy_from_user_before(to, from, n);
		res = raw_copy_from_user(to, from, n);
		instrument_copy_from_user_after(to, from, n, res);
	}
	if (unlikely(res))
		memset(to + (n - res), 0, res);
	return res;
}
EXPORT_SYMBOL(_copy_from_user);
#endif

#ifndef INLINE_COPY_TO_USER
unsigned long _copy_to_user(void __user *to, const void *from, unsigned long n)
{
	might_fault();
	if (should_fail_usercopy())
		return n;
	if (likely(access_ok(to, n))) {
		instrument_copy_to_user(to, from, n);
		n = raw_copy_to_user(to, from, n);
	}
	return n;
}
EXPORT_SYMBOL(_copy_to_user);
#endif

/**
 * check_zeroed_user: check if a userspace buffer only contains zero bytes
 * @from: Source address, in userspace.
 * @size: Size of buffer.
 *
 * This is effectively shorthand for "memchr_inv(from, 0, size) == NULL" for
 * userspace addresses (and is more efficient because we don't care where the
 * first non-zero byte is).
 *
 * Returns:
 *  * 0: There were non-zero bytes present in the buffer.
 *  * 1: The buffer was full of zero bytes.
 *  * -EFAULT: access to userspace failed.
 */
int check_zeroed_user(const void __user *from, size_t size)
{
	unsigned long val;
	uintptr_t align = (uintptr_t) from % sizeof(unsigned long);

	if (unlikely(size == 0))
		return 1;

	from -= align;
	size += align;

	if (!user_read_access_begin(from, size))
		return -EFAULT;

	unsafe_get_user(val, (unsigned long __user *) from, err_fault);
	if (align)
		val &= ~aligned_byte_mask(align);

	while (size > sizeof(unsigned long)) {
		if (unlikely(val))
			goto done;

		from += sizeof(unsigned long);
		size -= sizeof(unsigned long);

		unsafe_get_user(val, (unsigned long __user *) from, err_fault);
	}

	if (size < sizeof(unsigned long))
		val &= aligned_byte_mask(size);

done:
	user_read_access_end();
	return (val == 0);
err_fault:
	user_read_access_end();
	return -EFAULT;
}
EXPORT_SYMBOL(check_zeroed_user);
