/* Public domain. */

#ifndef _LINUX_SORT_H
#define _LINUX_SORT_H

#include <linux/types.h>

void sort(void *, size_t, size_t, int (*)(const void *, const void *), void *);
void sort_r(void *, size_t, size_t,
    int (*)(const void *, const void *, const void *), void *, const void *);

#endif
