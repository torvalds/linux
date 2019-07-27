/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_MMAP_H
#define __LIBPERF_INTERNAL_MMAP_H

/**
 * struct perf_mmap - perf's ring buffer mmap details
 *
 * @refcnt - e.g. code using PERF_EVENT_IOC_SET_OUTPUT to share this
 */
struct perf_mmap {
	void		*base;
};

#endif /* __LIBPERF_INTERNAL_MMAP_H */
