// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011-2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from evlist.c builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 */

#include <sys/mman.h>
#include <inttypes.h>
#include <asm/bug.h>
#include <linux/zalloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // sysconf()
#include <perf/mmap.h>
#ifdef HAVE_LIBNUMA_SUPPORT
#include <numaif.h>
#endif
#include "cpumap.h"
#include "debug.h"
#include "event.h"
#include "mmap.h"
#include "../perf.h"
#include <internal/lib.h> /* page_size */
#include <linux/bitmap.h>

#define MASK_SIZE 1023
void mmap_cpu_mask__scnprintf(struct mmap_cpu_mask *mask, const char *tag)
{
	char buf[MASK_SIZE + 1];
	size_t len;

	len = bitmap_scnprintf(mask->bits, mask->nbits, buf, MASK_SIZE);
	buf[len] = '\0';
	pr_debug("%p: %s mask[%zd]: %s\n", mask, tag, mask->nbits, buf);
}

size_t mmap__mmap_len(struct mmap *map)
{
	return perf_mmap__mmap_len(&map->core);
}

int __weak auxtrace_mmap__mmap(struct auxtrace_mmap *mm __maybe_unused,
			       struct auxtrace_mmap_params *mp __maybe_unused,
			       void *userpg __maybe_unused,
			       int fd __maybe_unused)
{
	return 0;
}

void __weak auxtrace_mmap__munmap(struct auxtrace_mmap *mm __maybe_unused)
{
}

void __weak auxtrace_mmap_params__init(struct auxtrace_mmap_params *mp __maybe_unused,
				       off_t auxtrace_offset __maybe_unused,
				       unsigned int auxtrace_pages __maybe_unused,
				       bool auxtrace_overwrite __maybe_unused)
{
}

void __weak auxtrace_mmap_params__set_idx(struct auxtrace_mmap_params *mp __maybe_unused,
					  struct evlist *evlist __maybe_unused,
					  struct evsel *evsel __maybe_unused,
					  int idx __maybe_unused)
{
}

#ifdef HAVE_AIO_SUPPORT
static int perf_mmap__aio_enabled(struct mmap *map)
{
	return map->aio.nr_cblocks > 0;
}

#ifdef HAVE_LIBNUMA_SUPPORT
static int perf_mmap__aio_alloc(struct mmap *map, int idx)
{
	map->aio.data[idx] = mmap(NULL, mmap__mmap_len(map), PROT_READ|PROT_WRITE,
				  MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
	if (map->aio.data[idx] == MAP_FAILED) {
		map->aio.data[idx] = NULL;
		return -1;
	}

	return 0;
}

static void perf_mmap__aio_free(struct mmap *map, int idx)
{
	if (map->aio.data[idx]) {
		munmap(map->aio.data[idx], mmap__mmap_len(map));
		map->aio.data[idx] = NULL;
	}
}

static int perf_mmap__aio_bind(struct mmap *map, int idx, struct perf_cpu cpu, int affinity)
{
	void *data;
	size_t mmap_len;
	unsigned long *node_mask;
	unsigned long node_index;
	int err = 0;

	if (affinity != PERF_AFFINITY_SYS && cpu__max_node() > 1) {
		data = map->aio.data[idx];
		mmap_len = mmap__mmap_len(map);
		node_index = cpu__get_node(cpu);
		node_mask = bitmap_zalloc(node_index + 1);
		if (!node_mask) {
			pr_err("Failed to allocate node mask for mbind: error %m\n");
			return -1;
		}
		__set_bit(node_index, node_mask);
		if (mbind(data, mmap_len, MPOL_BIND, node_mask, node_index + 1 + 1, 0)) {
			pr_err("Failed to bind [%p-%p] AIO buffer to node %lu: error %m\n",
				data, data + mmap_len, node_index);
			err = -1;
		}
		bitmap_free(node_mask);
	}

	return err;
}
#else /* !HAVE_LIBNUMA_SUPPORT */
static int perf_mmap__aio_alloc(struct mmap *map, int idx)
{
	map->aio.data[idx] = malloc(mmap__mmap_len(map));
	if (map->aio.data[idx] == NULL)
		return -1;

	return 0;
}

static void perf_mmap__aio_free(struct mmap *map, int idx)
{
	zfree(&(map->aio.data[idx]));
}

static int perf_mmap__aio_bind(struct mmap *map __maybe_unused, int idx __maybe_unused,
		struct perf_cpu cpu __maybe_unused, int affinity __maybe_unused)
{
	return 0;
}
#endif

static int perf_mmap__aio_mmap(struct mmap *map, struct mmap_params *mp)
{
	int delta_max, i, prio, ret;

	map->aio.nr_cblocks = mp->nr_cblocks;
	if (map->aio.nr_cblocks) {
		map->aio.aiocb = calloc(map->aio.nr_cblocks, sizeof(struct aiocb *));
		if (!map->aio.aiocb) {
			pr_debug2("failed to allocate aiocb for data buffer, error %m\n");
			return -1;
		}
		map->aio.cblocks = calloc(map->aio.nr_cblocks, sizeof(struct aiocb));
		if (!map->aio.cblocks) {
			pr_debug2("failed to allocate cblocks for data buffer, error %m\n");
			return -1;
		}
		map->aio.data = calloc(map->aio.nr_cblocks, sizeof(void *));
		if (!map->aio.data) {
			pr_debug2("failed to allocate data buffer, error %m\n");
			return -1;
		}
		delta_max = sysconf(_SC_AIO_PRIO_DELTA_MAX);
		for (i = 0; i < map->aio.nr_cblocks; ++i) {
			ret = perf_mmap__aio_alloc(map, i);
			if (ret == -1) {
				pr_debug2("failed to allocate data buffer area, error %m");
				return -1;
			}
			ret = perf_mmap__aio_bind(map, i, map->core.cpu, mp->affinity);
			if (ret == -1)
				return -1;
			/*
			 * Use cblock.aio_fildes value different from -1
			 * to denote started aio write operation on the
			 * cblock so it requires explicit record__aio_sync()
			 * call prior the cblock may be reused again.
			 */
			map->aio.cblocks[i].aio_fildes = -1;
			/*
			 * Allocate cblocks with priority delta to have
			 * faster aio write system calls because queued requests
			 * are kept in separate per-prio queues and adding
			 * a new request will iterate thru shorter per-prio
			 * list. Blocks with numbers higher than
			 *  _SC_AIO_PRIO_DELTA_MAX go with priority 0.
			 */
			prio = delta_max - i;
			map->aio.cblocks[i].aio_reqprio = prio >= 0 ? prio : 0;
		}
	}

	return 0;
}

static void perf_mmap__aio_munmap(struct mmap *map)
{
	int i;

	for (i = 0; i < map->aio.nr_cblocks; ++i)
		perf_mmap__aio_free(map, i);
	if (map->aio.data)
		zfree(&map->aio.data);
	zfree(&map->aio.cblocks);
	zfree(&map->aio.aiocb);
}
#else /* !HAVE_AIO_SUPPORT */
static int perf_mmap__aio_enabled(struct mmap *map __maybe_unused)
{
	return 0;
}

static int perf_mmap__aio_mmap(struct mmap *map __maybe_unused,
			       struct mmap_params *mp __maybe_unused)
{
	return 0;
}

static void perf_mmap__aio_munmap(struct mmap *map __maybe_unused)
{
}
#endif

void mmap__munmap(struct mmap *map)
{
	bitmap_free(map->affinity_mask.bits);

#ifndef PYTHON_PERF
	zstd_fini(&map->zstd_data);
#endif

	perf_mmap__aio_munmap(map);
	if (map->data != NULL) {
		munmap(map->data, mmap__mmap_len(map));
		map->data = NULL;
	}
	auxtrace_mmap__munmap(&map->auxtrace_mmap);
}

static void build_node_mask(int node, struct mmap_cpu_mask *mask)
{
	int idx, nr_cpus;
	struct perf_cpu cpu;
	const struct perf_cpu_map *cpu_map = NULL;

	cpu_map = cpu_map__online();
	if (!cpu_map)
		return;

	nr_cpus = perf_cpu_map__nr(cpu_map);
	for (idx = 0; idx < nr_cpus; idx++) {
		cpu = perf_cpu_map__cpu(cpu_map, idx); /* map c index to online cpu index */
		if (cpu__get_node(cpu) == node)
			__set_bit(cpu.cpu, mask->bits);
	}
}

static int perf_mmap__setup_affinity_mask(struct mmap *map, struct mmap_params *mp)
{
	map->affinity_mask.nbits = cpu__max_cpu().cpu;
	map->affinity_mask.bits = bitmap_zalloc(map->affinity_mask.nbits);
	if (!map->affinity_mask.bits)
		return -1;

	if (mp->affinity == PERF_AFFINITY_NODE && cpu__max_node() > 1)
		build_node_mask(cpu__get_node(map->core.cpu), &map->affinity_mask);
	else if (mp->affinity == PERF_AFFINITY_CPU)
		__set_bit(map->core.cpu.cpu, map->affinity_mask.bits);

	return 0;
}

int mmap__mmap(struct mmap *map, struct mmap_params *mp, int fd, struct perf_cpu cpu)
{
	if (perf_mmap__mmap(&map->core, &mp->core, fd, cpu)) {
		pr_debug2("failed to mmap perf event ring buffer, error %d\n",
			  errno);
		return -1;
	}

	if (mp->affinity != PERF_AFFINITY_SYS &&
		perf_mmap__setup_affinity_mask(map, mp)) {
		pr_debug2("failed to alloc mmap affinity mask, error %d\n",
			  errno);
		return -1;
	}

	if (verbose == 2)
		mmap_cpu_mask__scnprintf(&map->affinity_mask, "mmap");

	map->core.flush = mp->flush;

	map->comp_level = mp->comp_level;
#ifndef PYTHON_PERF
	if (zstd_init(&map->zstd_data, map->comp_level)) {
		pr_debug2("failed to init mmap compressor, error %d\n", errno);
		return -1;
	}
#endif

	if (map->comp_level && !perf_mmap__aio_enabled(map)) {
		map->data = mmap(NULL, mmap__mmap_len(map), PROT_READ|PROT_WRITE,
				 MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
		if (map->data == MAP_FAILED) {
			pr_debug2("failed to mmap data buffer, error %d\n",
					errno);
			map->data = NULL;
			return -1;
		}
	}

	if (auxtrace_mmap__mmap(&map->auxtrace_mmap,
				&mp->auxtrace_mp, map->core.base, fd))
		return -1;

	return perf_mmap__aio_mmap(map, mp);
}

int perf_mmap__push(struct mmap *md, void *to,
		    int push(struct mmap *map, void *to, void *buf, size_t size))
{
	u64 head = perf_mmap__read_head(&md->core);
	unsigned char *data = md->core.base + page_size;
	unsigned long size;
	void *buf;
	int rc = 0;

	rc = perf_mmap__read_init(&md->core);
	if (rc < 0)
		return (rc == -EAGAIN) ? 1 : -1;

	size = md->core.end - md->core.start;

	if ((md->core.start & md->core.mask) + size != (md->core.end & md->core.mask)) {
		buf = &data[md->core.start & md->core.mask];
		size = md->core.mask + 1 - (md->core.start & md->core.mask);
		md->core.start += size;

		if (push(md, to, buf, size) < 0) {
			rc = -1;
			goto out;
		}
	}

	buf = &data[md->core.start & md->core.mask];
	size = md->core.end - md->core.start;
	md->core.start += size;

	if (push(md, to, buf, size) < 0) {
		rc = -1;
		goto out;
	}

	md->core.prev = head;
	perf_mmap__consume(&md->core);
out:
	return rc;
}

int mmap_cpu_mask__duplicate(struct mmap_cpu_mask *original, struct mmap_cpu_mask *clone)
{
	clone->nbits = original->nbits;
	clone->bits  = bitmap_zalloc(original->nbits);
	if (!clone->bits)
		return -ENOMEM;

	memcpy(clone->bits, original->bits, MMAP_CPU_MASK_BYTES(original));
	return 0;
}
