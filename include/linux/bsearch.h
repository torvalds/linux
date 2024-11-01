/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BSEARCH_H
#define _LINUX_BSEARCH_H

#include <linux/types.h>

static __always_inline
void *__inline_bsearch(const void *key, const void *base, size_t num, size_t size, cmp_func_t cmp)
{
	const char *pivot;
	int result;

	while (num > 0) {
		pivot = base + (num >> 1) * size;
		result = cmp(key, pivot);

		if (result == 0)
			return (void *)pivot;

		if (result > 0) {
			base = pivot + size;
			num--;
		}
		num >>= 1;
	}

	return NULL;
}

extern void *bsearch(const void *key, const void *base, size_t num, size_t size, cmp_func_t cmp);

#endif /* _LINUX_BSEARCH_H */
