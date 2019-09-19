/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SORT_H
#define _LINUX_SORT_H

#include <linux/types.h>

void sort_r(void *base, size_t num, size_t size,
	    int (*cmp)(const void *, const void *, const void *),
	    void (*swap)(void *, void *, int),
	    const void *priv);

void sort(void *base, size_t num, size_t size,
	  int (*cmp)(const void *, const void *),
	  void (*swap)(void *, void *, int));

#endif
