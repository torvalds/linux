#ifndef __PERF_MMAP_H
#define __PERF_MMAP_H 1

#include <internal/mmap.h>
#include <linux/compiler.h>
#include <linux/refcount.h>
#include <linux/types.h>
#include <linux/ring_buffer.h>
#include <stdbool.h>
#include <pthread.h> // for cpu_set_t
#ifdef HAVE_AIO_SUPPORT
#include <aio.h>
#endif
#include "auxtrace.h"
#include "event.h"

struct aiocb;
/**
 * struct mmap - perf's ring buffer mmap details
 *
 * @refcnt - e.g. code using PERF_EVENT_IOC_SET_OUTPUT to share this
 */
struct mmap {
	struct perf_mmap	core;
	struct auxtrace_mmap auxtrace_mmap;
#ifdef HAVE_AIO_SUPPORT
	struct {
		void		 **data;
		struct aiocb	 *cblocks;
		struct aiocb	 **aiocb;
		int		 nr_cblocks;
	} aio;
#endif
	cpu_set_t	affinity_mask;
	void		*data;
	int		comp_level;
};

struct mmap_params {
	struct perf_mmap_param core;
	int nr_cblocks, affinity, flush, comp_level;
	struct auxtrace_mmap_params auxtrace_mp;
};

int mmap__mmap(struct mmap *map, struct mmap_params *mp, int fd, int cpu);
void mmap__munmap(struct mmap *map);

union perf_event *perf_mmap__read_forward(struct mmap *map);

int perf_mmap__push(struct mmap *md, void *to,
		    int push(struct mmap *map, void *to, void *buf, size_t size));

size_t mmap__mmap_len(struct mmap *map);

#endif /*__PERF_MMAP_H */
