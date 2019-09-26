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
	int prot, mask, nr_cblocks, affinity, flush, comp_level;
	struct auxtrace_mmap_params auxtrace_mp;
};

int perf_mmap__mmap(struct mmap *map, struct mmap_params *mp, int fd, int cpu);
void perf_mmap__munmap(struct mmap *map);

void perf_mmap__get(struct mmap *map);
void perf_mmap__put(struct mmap *map);

void perf_mmap__consume(struct mmap *map);

static inline u64 perf_mmap__read_head(struct mmap *mm)
{
	return ring_buffer_read_head(mm->core.base);
}

static inline void perf_mmap__write_tail(struct mmap *md, u64 tail)
{
	ring_buffer_write_tail(md->core.base, tail);
}

union perf_event *perf_mmap__read_forward(struct mmap *map);

union perf_event *perf_mmap__read_event(struct mmap *map);

int perf_mmap__push(struct mmap *md, void *to,
		    int push(struct mmap *map, void *to, void *buf, size_t size));

size_t perf_mmap__mmap_len(struct mmap *map);

int perf_mmap__read_init(struct mmap *md);
void perf_mmap__read_done(struct mmap *map);
#endif /*__PERF_MMAP_H */
