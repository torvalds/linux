/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_UCOPYSIZE_H__
#define __LINUX_UCOPYSIZE_H__

#include <linux/bug.h>

static inline void check_object_size(const void *ptr, unsigned long n,
				     bool to_user)
{ }

static inline void copy_overflow(int size, unsigned long count)
{
}

static __always_inline __must_check bool
check_copy_size(const void *addr, size_t bytes, bool is_source)
{
	return true;
}

#endif /* __LINUX_UCOPYSIZE_H__ */
