// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2018 Netronome Systems, Inc. */
/* This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <errno.h>
#include <fcntl.h>
#include <libbpf.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/bpf.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include <bpf.h>
#include <perf-sys.h>

#include "main.h"

#define MMAP_PAGE_CNT	16

static bool stop;

struct event_ring_info {
	int fd;
	int key;
	unsigned int cpu;
	void *mem;
};

struct perf_event_sample {
	struct perf_event_header header;
	__u32 size;
	unsigned char data[];
};

static void int_exit(int signo)
{
	fprintf(stderr, "Stopping...\n");
	stop = true;
}

static void
print_bpf_output(struct event_ring_info *ring, struct perf_event_sample *e)
{
	struct {
		struct perf_event_header header;
		__u64 id;
		__u64 lost;
	} *lost = (void *)e;
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
		perror("Can't read clock for timestamp");
		return;
	}

	if (json_output) {
		jsonw_start_object(json_wtr);
		jsonw_name(json_wtr, "timestamp");
		jsonw_uint(json_wtr, ts.tv_sec * 1000000000ull + ts.tv_nsec);
		jsonw_name(json_wtr, "type");
		jsonw_uint(json_wtr, e->header.type);
		jsonw_name(json_wtr, "cpu");
		jsonw_uint(json_wtr, ring->cpu);
		jsonw_name(json_wtr, "index");
		jsonw_uint(json_wtr, ring->key);
		if (e->header.type == PERF_RECORD_SAMPLE) {
			jsonw_name(json_wtr, "data");
			print_data_json(e->data, e->size);
		} else if (e->header.type == PERF_RECORD_LOST) {
			jsonw_name(json_wtr, "lost");
			jsonw_start_object(json_wtr);
			jsonw_name(json_wtr, "id");
			jsonw_uint(json_wtr, lost->id);
			jsonw_name(json_wtr, "count");
			jsonw_uint(json_wtr, lost->lost);
			jsonw_end_object(json_wtr);
		}
		jsonw_end_object(json_wtr);
	} else {
		if (e->header.type == PERF_RECORD_SAMPLE) {
			printf("== @%ld.%ld CPU: %d index: %d =====\n",
			       (long)ts.tv_sec, ts.tv_nsec,
			       ring->cpu, ring->key);
			fprint_hex(stdout, e->data, e->size, " ");
			printf("\n");
		} else if (e->header.type == PERF_RECORD_LOST) {
			printf("lost %lld events\n", lost->lost);
		} else {
			printf("unknown event type=%d size=%d\n",
			       e->header.type, e->header.size);
		}
	}
}

static void
perf_event_read(struct event_ring_info *ring, void **buf, size_t *buf_len)
{
	volatile struct perf_event_mmap_page *header = ring->mem;
	__u64 buffer_size = MMAP_PAGE_CNT * get_page_size();
	__u64 data_tail = header->data_tail;
	__u64 data_head = header->data_head;
	void *base, *begin, *end;

	asm volatile("" ::: "memory"); /* in real code it should be smp_rmb() */
	if (data_head == data_tail)
		return;

	base = ((char *)header) + get_page_size();

	begin = base + data_tail % buffer_size;
	end = base + data_head % buffer_size;

	while (begin != end) {
		struct perf_event_sample *e;

		e = begin;
		if (begin + e->header.size > base + buffer_size) {
			long len = base + buffer_size - begin;

			if (*buf_len < e->header.size) {
				free(*buf);
				*buf = malloc(e->header.size);
				if (!*buf) {
					fprintf(stderr,
						"can't allocate memory");
					stop = true;
					return;
				}
				*buf_len = e->header.size;
			}

			memcpy(*buf, begin, len);
			memcpy(*buf + len, base, e->header.size - len);
			e = (void *)*buf;
			begin = base + e->header.size - len;
		} else if (begin + e->header.size == base + buffer_size) {
			begin = base;
		} else {
			begin += e->header.size;
		}

		print_bpf_output(ring, e);
	}

	__sync_synchronize(); /* smp_mb() */
	header->data_tail = data_head;
}

static int perf_mmap_size(void)
{
	return get_page_size() * (MMAP_PAGE_CNT + 1);
}

static void *perf_event_mmap(int fd)
{
	int mmap_size = perf_mmap_size();
	void *base;

	base = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (base == MAP_FAILED) {
		p_err("event mmap failed: %s\n", strerror(errno));
		return NULL;
	}

	return base;
}

static void perf_event_unmap(void *mem)
{
	if (munmap(mem, perf_mmap_size()))
		fprintf(stderr, "Can't unmap ring memory!\n");
}

static int bpf_perf_event_open(int map_fd, int key, int cpu)
{
	struct perf_event_attr attr = {
		.sample_type = PERF_SAMPLE_RAW,
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_BPF_OUTPUT,
	};
	int pmu_fd;

	pmu_fd = sys_perf_event_open(&attr, -1, cpu, -1, 0);
	if (pmu_fd < 0) {
		p_err("failed to open perf event %d for CPU %d", key, cpu);
		return -1;
	}

	if (bpf_map_update_elem(map_fd, &key, &pmu_fd, BPF_ANY)) {
		p_err("failed to update map for event %d for CPU %d", key, cpu);
		goto err_close;
	}
	if (ioctl(pmu_fd, PERF_EVENT_IOC_ENABLE, 0)) {
		p_err("failed to enable event %d for CPU %d", key, cpu);
		goto err_close;
	}

	return pmu_fd;

err_close:
	close(pmu_fd);
	return -1;
}

int do_event_pipe(int argc, char **argv)
{
	int i, nfds, map_fd, index = -1, cpu = -1;
	struct bpf_map_info map_info = {};
	struct event_ring_info *rings;
	size_t tmp_buf_sz = 0;
	void *tmp_buf = NULL;
	struct pollfd *pfds;
	__u32 map_info_len;
	bool do_all = true;

	map_info_len = sizeof(map_info);
	map_fd = map_parse_fd_and_info(&argc, &argv, &map_info, &map_info_len);
	if (map_fd < 0)
		return -1;

	if (map_info.type != BPF_MAP_TYPE_PERF_EVENT_ARRAY) {
		p_err("map is not a perf event array");
		goto err_close_map;
	}

	while (argc) {
		if (argc < 2)
			BAD_ARG();

		if (is_prefix(*argv, "cpu")) {
			char *endptr;

			NEXT_ARG();
			cpu = strtoul(*argv, &endptr, 0);
			if (*endptr) {
				p_err("can't parse %s as CPU ID", **argv);
				goto err_close_map;
			}

			NEXT_ARG();
		} else if (is_prefix(*argv, "index")) {
			char *endptr;

			NEXT_ARG();
			index = strtoul(*argv, &endptr, 0);
			if (*endptr) {
				p_err("can't parse %s as index", **argv);
				goto err_close_map;
			}

			NEXT_ARG();
		} else {
			BAD_ARG();
		}

		do_all = false;
	}

	if (!do_all) {
		if (index == -1 || cpu == -1) {
			p_err("cpu and index must be specified together");
			goto err_close_map;
		}

		nfds = 1;
	} else {
		nfds = min(get_possible_cpus(), map_info.max_entries);
		cpu = 0;
		index = 0;
	}

	rings = calloc(nfds, sizeof(rings[0]));
	if (!rings)
		goto err_close_map;

	pfds = calloc(nfds, sizeof(pfds[0]));
	if (!pfds)
		goto err_free_rings;

	for (i = 0; i < nfds; i++) {
		rings[i].cpu = cpu + i;
		rings[i].key = index + i;

		rings[i].fd = bpf_perf_event_open(map_fd, rings[i].key,
						  rings[i].cpu);
		if (rings[i].fd < 0)
			goto err_close_fds_prev;

		rings[i].mem = perf_event_mmap(rings[i].fd);
		if (!rings[i].mem)
			goto err_close_fds_current;

		pfds[i].fd = rings[i].fd;
		pfds[i].events = POLLIN;
	}

	signal(SIGINT, int_exit);
	signal(SIGHUP, int_exit);
	signal(SIGTERM, int_exit);

	if (json_output)
		jsonw_start_array(json_wtr);

	while (!stop) {
		poll(pfds, nfds, 200);
		for (i = 0; i < nfds; i++)
			perf_event_read(&rings[i], &tmp_buf, &tmp_buf_sz);
	}
	free(tmp_buf);

	if (json_output)
		jsonw_end_array(json_wtr);

	for (i = 0; i < nfds; i++) {
		perf_event_unmap(rings[i].mem);
		close(rings[i].fd);
	}
	free(pfds);
	free(rings);
	close(map_fd);

	return 0;

err_close_fds_prev:
	while (i--) {
		perf_event_unmap(rings[i].mem);
err_close_fds_current:
		close(rings[i].fd);
	}
	free(pfds);
err_free_rings:
	free(rings);
err_close_map:
	close(map_fd);
	return -1;
}
