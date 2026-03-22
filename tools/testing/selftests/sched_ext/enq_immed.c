// SPDX-License-Identifier: GPL-2.0
/*
 * Userspace test for SCX_ENQ_IMMED via the direct insert path.
 *
 * Validates that dispatching tasks to a busy CPU's local DSQ with
 * SCX_OPS_ALWAYS_ENQ_IMMED triggers the IMMED slow path: the kernel
 * re-enqueues the task through ops.enqueue() with SCX_TASK_REENQ_IMMED.
 *
 * Skipped on single-CPU systems where local DSQ contention cannot occur.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "enq_immed.bpf.skel.h"
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
	struct enq_immed *skel;
	u64 v;

	if (!__COMPAT_read_enum("scx_ops_flags",
				"SCX_OPS_ALWAYS_ENQ_IMMED", &v)) {
		fprintf(stderr,
			"SKIP: SCX_OPS_ALWAYS_ENQ_IMMED not available\n");
		return SCX_TEST_SKIP;
	}

	skel = enq_immed__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);

	skel->rodata->test_tgid = (u32)getpid();

	SCX_FAIL_IF(enq_immed__load(skel), "Failed to load skel");

	*ctx = skel;
	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct enq_immed *skel = ctx;
	struct bpf_link *link;
	pthread_t workers[NUM_WORKERS];
	long nproc;
	int i;
	u64 reenq;

	nproc = sysconf(_SC_NPROCESSORS_ONLN);
	if (nproc <= 1) {
		fprintf(stderr,
			"SKIP: single CPU, IMMED slow path may not trigger\n");
		return SCX_TEST_SKIP;
	}

	link = bpf_map__attach_struct_ops(skel->maps.enq_immed_ops);
	SCX_FAIL_IF(!link, "Failed to attach scheduler");

	stop_workers = false;
	for (i = 0; i < NUM_WORKERS; i++) {
		SCX_FAIL_IF(pthread_create(&workers[i], NULL, worker_fn, NULL),
			    "Failed to create worker %d", i);
	}

	sleep(TEST_DURATION_SEC);

	reenq = skel->bss->nr_immed_reenq;

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
	struct enq_immed *skel = ctx;

	enq_immed__destroy(skel);
}

struct scx_test enq_immed = {
	.name		= "enq_immed",
	.description	= "Verify SCX_ENQ_IMMED slow path via direct insert "
			  "with SCX_OPS_ALWAYS_ENQ_IMMED",
	.setup		= setup,
	.run		= run,
	.cleanup	= cleanup,
};
REGISTER_SCX_TEST(&enq_immed)
