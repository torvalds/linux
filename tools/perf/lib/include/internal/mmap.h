/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_MMAP_H
#define __LIBPERF_INTERNAL_MMAP_H

#include <linux/compiler.h>
#include <linux/refcount.h>
#include <linux/types.h>
#include <stdbool.h>

/* perf sample has 16 bits size limit */
#define PERF_SAMPLE_MAX_SIZE (1 << 16)

/**
 * struct perf_mmap - perf's ring buffer mmap details
 *
 * @refcnt - e.g. code using PERF_EVENT_IOC_SET_OUTPUT to share this
 */
struct perf_mmap {
	void		*base;
	int		 mask;
	int		 fd;
	int		 cpu;
	refcount_t	 refcnt;
	u64		 prev;
	u64		 start;
	u64		 end;
	bool		 overwrite;
	u64		 flush;
	char		 event_copy[PERF_SAMPLE_MAX_SIZE] __aligned(8);
};

#endif /* __LIBPERF_INTERNAL_MMAP_H */
