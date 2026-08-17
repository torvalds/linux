// SPDX-License-Identifier: GPL-2.0

#include <linux/string.h>

__rust_helper void *rust_helper_memchr(const void *s, int c, size_t n)
{
	return memchr(s, c, n);
}
