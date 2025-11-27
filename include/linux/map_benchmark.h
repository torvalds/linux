/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 HiSilicon Limited.
 */

#ifndef _KERNEL_DMA_BENCHMARK_H
#define _KERNEL_DMA_BENCHMARK_H

#define DMA_MAP_BENCHMARK       _IOWR('d', 1, struct map_benchmark)
#define DMA_MAP_MAX_THREADS     1024
#define DMA_MAP_MAX_SECONDS     300
#define DMA_MAP_MAX_TRANS_DELAY (10 * NSEC_PER_MSEC)

#define DMA_MAP_BIDIRECTIONAL   0
#define DMA_MAP_TO_DEVICE       1
#define DMA_MAP_FROM_DEVICE     2

struct map_benchmark {
	__u64 avg_map_100ns; /* average map latency in 100ns */
	__u64 map_stddev; /* standard deviation of map latency */
	__u64 avg_unmap_100ns; /* as above */
	__u64 unmap_stddev;
	__u32 threads; /* how many threads will do map/unmap in parallel */
	__u32 seconds; /* how long the test will last */
	__s32 node; /* which numa node this benchmark will run on */
	__u32 dma_bits; /* DMA addressing capability */
	__u32 dma_dir; /* DMA data direction */
	__u32 dma_trans_ns; /* time for DMA transmission in ns */
	__u32 granule;  /* how many PAGE_SIZE will do map/unmap once a time */
	__u8 expansion[76]; /* For future use */
};
#endif /* _KERNEL_DMA_BENCHMARK_H */
