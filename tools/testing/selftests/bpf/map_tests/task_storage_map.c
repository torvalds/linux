// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022. Huawei Technologies Co., Ltd */
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_util.h"
#include "test_maps.h"
#include "task_local_storage_helpers.h"
#include "read_bpf_task_storage_busy.skel.h"

struct lookup_ctx {
	bool start;
	bool stop;
	int pid_fd;
	int map_fd;
	int loop;
};

static void *lookup_fn(void *arg)
{
	struct lookup_ctx *ctx = arg;
	long value;
	int i = 0;

	while (!ctx->start)
		usleep(1);

	while (!ctx->stop && i++ < ctx->loop)
		bpf_map_lookup_elem(ctx->map_fd, &ctx->pid_fd, &value);
	return NULL;
}

static void abort_lookup(struct lookup_ctx *ctx, pthread_t *tids, unsigned int nr)
{
	unsigned int i;

	ctx->stop = true;
	ctx->start = true;
	for (i = 0; i < nr; i++)
		pthread_join(tids[i], NULL);
}

void test_task_storage_map_stress_lookup(void)
{
#define MAX_NR_THREAD 4096
	unsigned int i, nr = 256, loop = 8192, cpu = 0;
	struct read_bpf_task_storage_busy *skel;
	pthread_t tids[MAX_NR_THREAD];
	struct lookup_ctx ctx;
	cpu_set_t old, new;
	const char *cfg;
	int err;

	cfg = getenv("TASK_STORAGE_MAP_NR_THREAD");
	if (cfg) {
		nr = atoi(cfg);
		if (nr > MAX_NR_THREAD)
			nr = MAX_NR_THREAD;
	}
	cfg = getenv("TASK_STORAGE_MAP_NR_LOOP");
	if (cfg)
		loop = atoi(cfg);
	cfg = getenv("TASK_STORAGE_MAP_PIN_CPU");
	if (cfg)
		cpu = atoi(cfg);

	skel = read_bpf_task_storage_busy__open_and_load();
	err = libbpf_get_error(skel);
	CHECK(err, "open_and_load", "error %d\n", err);

	/* Only for a fully preemptible kernel */
	if (!skel->kconfig->CONFIG_PREEMPT) {
		printf("%s SKIP (no CONFIG_PREEMPT)\n", __func__);
		read_bpf_task_storage_busy__destroy(skel);
		skips++;
		return;
	}

	/* Save the old affinity setting */
	sched_getaffinity(getpid(), sizeof(old), &old);

	/* Pinned on a specific CPU */
	CPU_ZERO(&new);
	CPU_SET(cpu, &new);
	sched_setaffinity(getpid(), sizeof(new), &new);

	ctx.start = false;
	ctx.stop = false;
	ctx.pid_fd = sys_pidfd_open(getpid(), 0);
	ctx.map_fd = bpf_map__fd(skel->maps.task);
	ctx.loop = loop;
	for (i = 0; i < nr; i++) {
		err = pthread_create(&tids[i], NULL, lookup_fn, &ctx);
		if (err) {
			abort_lookup(&ctx, tids, i);
			CHECK(err, "pthread_create", "error %d\n", err);
			goto out;
		}
	}

	ctx.start = true;
	for (i = 0; i < nr; i++)
		pthread_join(tids[i], NULL);

	skel->bss->pid = getpid();
	err = read_bpf_task_storage_busy__attach(skel);
	CHECK(err, "attach", "error %d\n", err);

	/* Trigger program */
	sys_gettid();
	skel->bss->pid = 0;

	CHECK(skel->bss->busy != 0, "bad bpf_task_storage_busy", "got %d\n", skel->bss->busy);
out:
	read_bpf_task_storage_busy__destroy(skel);
	/* Restore affinity setting */
	sched_setaffinity(getpid(), sizeof(old), &old);
	printf("%s:PASS\n", __func__);
}
