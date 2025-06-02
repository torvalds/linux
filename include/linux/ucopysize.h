/* SPDX-License-Identifier: GPL-2.0 */
/* Perform sanity checking for object sizes for uaccess.h and uio.h. */
#ifndef __LINUX_UCOPYSIZE_H__
#define __LINUX_UCOPYSIZE_H__

#include <linux/bug.h>

#ifdef CONFIG_HARDENED_USERCOPY
#include <linux/jump_label.h>
extern void __check_object_size(const void *ptr, unsigned long n,
					bool to_user);

DECLARE_STATIC_KEY_MAYBE(CONFIG_HARDENED_USERCOPY_DEFAULT_ON,
			   validate_usercopy_range);

static __always_inline void check_object_size(const void *ptr, unsigned long n,
					      bool to_user)
{
	if (!__builtin_constant_p(n) &&
	    static_branch_maybe(CONFIG_HARDENED_USERCOPY_DEFAULT_ON,
				&validate_usercopy_range)) {
		__check_object_size(ptr, n, to_user);
	}
}
#else
static inline void check_object_size(const void *ptr, unsigned long n,
				     bool to_user)
{ }
#endif /* CONFIG_HARDENED_USERCOPY */

extern void __compiletime_error("copy source size is too small")
__bad_copy_from(void);
extern void __compiletime_error("copy destination size is too small")
__bad_copy_to(void);

void __copy_overflow(int size, unsigned long count);

static inline void copy_overflow(int size, unsigned long count)
{
	if (IS_ENABLED(CONFIG_BUG))
		__copy_overflow(size, count);
}

static __always_inline __must_check bool
check_copy_size(const void *addr, size_t bytes, bool is_source)
{
	int sz = __builtin_object_size(addr, 0);
	if (unlikely(sz >= 0 && sz < bytes)) {
		if (!__builtin_constant_p(bytes))
			copy_overflow(sz, bytes);
		else if (is_source)
			__bad_copy_from();
		else
			__bad_copy_to();
		return false;
	}
	if (WARN_ON_ONCE(bytes > INT_MAX))
		return false;
	check_object_size(addr, bytes, is_source);
	return true;
}

#endif /* __LINUX_UCOPYSIZE_H__ */
