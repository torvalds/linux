// SPDX-License-Identifier: GPL-2.0
/*
 * Userspace test for SCX_ENQ_IMMED via the consume path.
 *
 * Validates that scx_bpf_dsq_move_to_local(USER_DSQ, SCX_ENQ_IMMED) on
 * a busy CPU triggers the IMMED slow path, re-enqueuing tasks through
 * ops.enqueue() with SCX_TASK_REENQ_IMMED.
 *
 * Skipped on single-CPU systems where local DSQ contention cannot occur.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "consume_immed.bpf.skel.h"
#include "scx_test.h"

#define NUM_WORKERS		4
#define TEST_DURATION_SEC	3

static volatile bool stop_workers;

static void *worker_fn(void *arg)
{
	while (!stop_workers) {
		volatile unsigned long i;

		for (i = 0; i < 100000UL; i++)
			;
		usleep(100);
	}
	return NULL;
}

static enum scx_test_status setup(void **ctx)
{
	struct consume_immed *skel;

	if (!__COMPAT_has_ksym("scx_bpf_dsq_move_to_local___v2")) {
		fprintf(stderr,
			"SKIP: scx_bpf_dsq_move_to_local v2 not available\n");
		return SCX_TEST_SKIP;
	}

	skel = consume_immed__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);

	skel->rodata->test_tgid = (u32)getpid();

	SCX_FAIL_IF(consume_immed__load(skel), "Failed to load skel");

	*ctx = skel;
	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct consume_immed *skel = ctx;
	struct bpf_link *link;
	pthread_t workers[NUM_WORKERS];
	long nproc;
	int i;
	u64 reenq;

	nproc = sysconf(_SC_NPROCESSORS_ONLN);
	if (nproc <= 1) {
		fprintf(stderr,
			"SKIP: single CPU, consume IMMED slow path may not trigger\n");
		return SCX_TEST_SKIP;
	}

	link = bpf_map__attach_struct_ops(skel->maps.consume_immed_ops);
	SCX_FAIL_IF(!link, "Failed to attach scheduler");

	stop_workers = false;
	for (i = 0; i < NUM_WORKERS; i++) {
		SCX_FAIL_IF(pthread_create(&workers[i], NULL, worker_fn, NULL),
			    "Failed to create worker %d", i);
	}

	sleep(TEST_DURATION_SEC);

	reenq = skel->bss->nr_consume_immed_reenq;

	stop_workers = true;
	for (i = 0; i < NUM_WORKERS; i++)
		pthread_join(workers[i], NULL);

	bpf_link__destroy(link);

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_UNREG));
	SCX_GT(reenq, 0);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct consume_immed *skel = ctx;

	consume_immed__destroy(skel);
}

struct scx_test consume_immed = {
	.name		= "consume_immed",
	.description	= "Verify SCX_ENQ_IMMED slow path via "
			  "scx_bpf_dsq_move_to_local() consume path",
	.setup		= setup,
	.run		= run,
	.cleanup	= cleanup,
};
REGISTER_SCX_TEST(&consume_immed)
