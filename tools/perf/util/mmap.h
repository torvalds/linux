#ifndef __PERF_MMAP_H
#define __PERF_MMAP_H 1

#include <internal/mmap.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <perf/cpumap.h>
#ifdef HAVE_AIO_SUPPORT
#include <aio.h>
#endif
#include "auxtrace.h"
#include "util/compress.h"

struct aiocb;

struct mmap_cpu_mask {
	unsigned long *bits;
	size_t nbits;
};

#define MMAP_CPU_MASK_BYTES(m) \
	(BITS_TO_LONGS(((struct mmap_cpu_mask *)m)->nbits) * sizeof(unsigned long))

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
	struct mmap_cpu_mask	affinity_mask;
	void		*data;
	int		comp_level;
	struct perf_data_file *file;
	struct zstd_data      zstd_data;
};

struct mmap_params {
	struct perf_mmap_param core;
	int nr_cblocks, affinity, flush, comp_level;
	struct auxtrace_mmap_params auxtrace_mp;
};

int mmap__mmap(struct mmap *map, struct mmap_params *mp, int fd, struct perf_cpu cpu);
void mmap__munmap(struct mmap *map);

union perf_event *perf_mmap__read_forward(struct mmap *map);

int perf_mmap__push(struct mmap *md, void *to,
		    int push(struct mmap *map, void *to, void *buf, size_t size));

size_t mmap__mmap_len(struct mmap *map);

void mmap_cpu_mask__scnprintf(struct mmap_cpu_mask *mask, const char *tag);

int mmap_cpu_mask__duplicate(struct mmap_cpu_mask *original,
				struct mmap_cpu_mask *clone);

#endif /*__PERF_MMAP_H */
