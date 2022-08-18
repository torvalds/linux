// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 HiSilicon Limited.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/types.h>
#include <linux/map_benchmark.h>

#define NSEC_PER_MSEC	1000000L

static char *directions[] = {
	"BIDIRECTIONAL",
	"TO_DEVICE",
	"FROM_DEVICE",
};

int main(int argc, char **argv)
{
	struct map_benchmark map;
	int fd, opt;
	/* default single thread, run 20 seconds on NUMA_NO_NODE */
	int threads = 1, seconds = 20, node = -1;
	/* default dma mask 32bit, bidirectional DMA */
	int bits = 32, xdelay = 0, dir = DMA_MAP_BIDIRECTIONAL;
	/* default granule 1 PAGESIZE */
	int granule = 1;

	int cmd = DMA_MAP_BENCHMARK;
	char *p;

	while ((opt = getopt(argc, argv, "t:s:n:b:d:x:g:")) != -1) {
		switch (opt) {
		case 't':
			threads = atoi(optarg);
			break;
		case 's':
			seconds = atoi(optarg);
			break;
		case 'n':
			node = atoi(optarg);
			break;
		case 'b':
			bits = atoi(optarg);
			break;
		case 'd':
			dir = atoi(optarg);
			break;
		case 'x':
			xdelay = atoi(optarg);
			break;
		case 'g':
			granule = atoi(optarg);
			break;
		default:
			return -1;
		}
	}

	if (threads <= 0 || threads > DMA_MAP_MAX_THREADS) {
		fprintf(stderr, "invalid number of threads, must be in 1-%d\n",
			DMA_MAP_MAX_THREADS);
		exit(1);
	}

	if (seconds <= 0 || seconds > DMA_MAP_MAX_SECONDS) {
		fprintf(stderr, "invalid number of seconds, must be in 1-%d\n",
			DMA_MAP_MAX_SECONDS);
		exit(1);
	}

	if (xdelay < 0 || xdelay > DMA_MAP_MAX_TRANS_DELAY) {
		fprintf(stderr, "invalid transmit delay, must be in 0-%ld\n",
			DMA_MAP_MAX_TRANS_DELAY);
		exit(1);
	}

	/* suppose the mininum DMA zone is 1MB in the world */
	if (bits < 20 || bits > 64) {
		fprintf(stderr, "invalid dma mask bit, must be in 20-64\n");
		exit(1);
	}

	if (dir != DMA_MAP_BIDIRECTIONAL && dir != DMA_MAP_TO_DEVICE &&
			dir != DMA_MAP_FROM_DEVICE) {
		fprintf(stderr, "invalid dma direction\n");
		exit(1);
	}

	if (granule < 1 || granule > 1024) {
		fprintf(stderr, "invalid granule size\n");
		exit(1);
	}

	fd = open("/sys/kernel/debug/dma_map_benchmark", O_RDWR);
	if (fd == -1) {
		perror("open");
		exit(1);
	}

	memset(&map, 0, sizeof(map));
	map.seconds = seconds;
	map.threads = threads;
	map.node = node;
	map.dma_bits = bits;
	map.dma_dir = dir;
	map.dma_trans_ns = xdelay;
	map.granule = granule;

	if (ioctl(fd, cmd, &map)) {
		perror("ioctl");
		exit(1);
	}

	printf("dma mapping benchmark: threads:%d seconds:%d node:%d dir:%s granule: %d\n",
			threads, seconds, node, dir[directions], granule);
	printf("average map latency(us):%.1f standard deviation:%.1f\n",
			map.avg_map_100ns/10.0, map.map_stddev/10.0);
	printf("average unmap latency(us):%.1f standard deviation:%.1f\n",
			map.avg_unmap_100ns/10.0, map.unmap_stddev/10.0);

	return 0;
}
