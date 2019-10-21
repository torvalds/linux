/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_MMAP_H
#define __LIBPERF_INTERNAL_MMAP_H

#include <linux/compiler.h>
#include <linux/refcount.h>
#include <linux/types.h>
#include <stdbool.h>

/* perf sample has 16 bits size limit */
#define PERF_SAMPLE_MAX_SIZE (1 << 16)

struct perf_mmap;

typedef void (*libperf_unmap_cb_t)(struct perf_mmap *map);

/**
 * struct perf_mmap - perf's ring buffer mmap details
 *
 * @refcnt - e.g. code using PERF_EVENT_IOC_SET_OUTPUT to share this
 */
struct perf_mmap {
	void			*base;
	int			 mask;
	int			 fd;
	int			 cpu;
	refcount_t		 refcnt;
	u64			 prev;
	u64			 start;
	u64			 end;
	bool			 overwrite;
	u64			 flush;
	libperf_unmap_cb_t	 unmap_cb;
	char			 event_copy[PERF_SAMPLE_MAX_SIZE] __aligned(8);
	struct perf_mmap	*next;
};

struct perf_mmap_param {
	int	prot;
	int	mask;
};

size_t perf_mmap__mmap_len(struct perf_mmap *map);

void perf_mmap__init(struct perf_mmap *map, struct perf_mmap *prev,
		     bool overwrite, libperf_unmap_cb_t unmap_cb);
int perf_mmap__mmap(struct perf_mmap *map, struct perf_mmap_param *mp,
		    int fd, int cpu);
void perf_mmap__munmap(struct perf_mmap *map);
void perf_mmap__get(struct perf_mmap *map);
void perf_mmap__put(struct perf_mmap *map);

u64 perf_mmap__read_head(struct perf_mmap *map);

#endif /* __LIBPERF_INTERNAL_MMAP_H */
