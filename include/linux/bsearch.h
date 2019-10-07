/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BSEARCH_H
#define _LINUX_BSEARCH_H

#include <linux/types.h>

void *bsearch(const void *key, const void *base, size_t num, size_t size,
	      cmp_func_t cmp);

#endif /* _LINUX_BSEARCH_H */
