/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_LIB_H
#define __LIBPERF_INTERNAL_LIB_H

#include <sys/types.h>

extern unsigned int page_size;

ssize_t readn(int fd, void *buf, size_t n);
ssize_t writen(int fd, const void *buf, size_t n);

ssize_t preadn(int fd, void *buf, size_t n, off_t offs);

#endif /* __LIBPERF_INTERNAL_CPUMAP_H */
