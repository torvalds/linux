#ifndef __PERF_MMAP_H
#define __PERF_MMAP_H 1

#include <linux/compiler.h>
#include <linux/refcount.h>
#include <linux/types.h>
#include <linux/ring_buffer.h>
#include <stdbool.h>
#ifdef HAVE_AIO_SUPPORT
#include <aio.h>
#endif
#include "auxtrace.h"
#include "event.h"

struct aiocb;
/**
 * struct perf_mmap - perf's ring buffer mmap details
 *
 * @refcnt - e.g. code using PERF_EVENT_IOC_SET_OUTPUT to share this
 */
struct perf_mmap {
	void		 *base;
	int		 mask;
	int		 fd;
	int		 cpu;
	refcount_t	 refcnt;
	u64		 prev;
	u64		 start;
	u64		 end;
	bool		 overwrite;
	struct auxtrace_mmap auxtrace_mmap;
	char		 event_copy[PERF_SAMPLE_MAX_SIZE] __aligned(8);
#ifdef HAVE_AIO_SUPPORT
	struct {
		void		 **data;
		struct aiocb	 *cblocks;
		struct aiocb	 **aiocb;
		int		 nr_cblocks;
	} aio;
#endif
	cpu_set_t	affinity_mask;
	u64		flush;
};

/*
 * State machine of bkw_mmap_state:
 *
 *                     .________________(forbid)_____________.
 *                     |                                     V
 * NOTREADY --(0)--> RUNNING --(1)--> DATA_PENDING --(2)--> EMPTY
 *                     ^  ^              |   ^               |
 *                     |  |__(forbid)____/   |___(forbid)___/|
 *                     |                                     |
 *                      \_________________(3)_______________/
 *
 * NOTREADY     : Backward ring buffers are not ready
 * RUNNING      : Backward ring buffers are recording
 * DATA_PENDING : We are required to collect data from backward ring buffers
 * EMPTY        : We have collected data from backward ring buffers.
 *
 * (0): Setup backward ring buffer
 * (1): Pause ring buffers for reading
 * (2): Read from ring buffers
 * (3): Resume ring buffers for recording
 */
enum bkw_mmap_state {
	BKW_MMAP_NOTREADY,
	BKW_MMAP_RUNNING,
	BKW_MMAP_DATA_PENDING,
	BKW_MMAP_EMPTY,
};

struct mmap_params {
	int			    prot, mask, nr_cblocks, affinity, flush;
	struct auxtrace_mmap_params auxtrace_mp;
};

int perf_mmap__mmap(struct perf_mmap *map, struct mmap_params *mp, int fd, int cpu);
void perf_mmap__munmap(struct perf_mmap *map);

void perf_mmap__get(struct perf_mmap *map);
void perf_mmap__put(struct perf_mmap *map);

void perf_mmap__consume(struct perf_mmap *map);

static inline u64 perf_mmap__read_head(struct perf_mmap *mm)
{
	return ring_buffer_read_head(mm->base);
}

static inline void perf_mmap__write_tail(struct perf_mmap *md, u64 tail)
{
	ring_buffer_write_tail(md->base, tail);
}

union perf_event *perf_mmap__read_forward(struct perf_mmap *map);

union perf_event *perf_mmap__read_event(struct perf_mmap *map);

int perf_mmap__push(struct perf_mmap *md, void *to,
		    int push(struct perf_mmap *map, void *to, void *buf, size_t size));
#ifdef HAVE_AIO_SUPPORT
int perf_mmap__aio_push(struct perf_mmap *md, void *to, int idx,
			int push(void *to, struct aiocb *cblock, void *buf, size_t size, off_t off),
			off_t *off);
#else
static inline int perf_mmap__aio_push(struct perf_mmap *md __maybe_unused, void *to __maybe_unused, int idx __maybe_unused,
	int push(void *to, struct aiocb *cblock, void *buf, size_t size, off_t off) __maybe_unused,
	off_t *off __maybe_unused)
{
	return 0;
}
#endif

size_t perf_mmap__mmap_len(struct perf_mmap *map);

int perf_mmap__read_init(struct perf_mmap *md);
void perf_mmap__read_done(struct perf_mmap *map);
#endif /*__PERF_MMAP_H */
