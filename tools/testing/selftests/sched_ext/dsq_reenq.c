// SPDX-License-Identifier: GPL-2.0
/*
 * Userspace test for scx_bpf_dsq_reenq() semantics.
 *
 * Attaches the dsq_reenq BPF scheduler, runs workload threads that
 * sleep and yield to keep tasks on USER_DSQ, waits for the BPF timer
 * to fire several times, then verifies that at least one kfunc-triggered
 * reenqueue was observed (ops.enqueue() called with SCX_ENQ_REENQ and
 * SCX_TASK_REENQ_KFUNC in p->scx.flags).
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include <unistd.h>
#include <pthread.h>
#include "dsq_reenq.bpf.skel.h"
#include "scx_test.h"

#define NUM_WORKERS	4
#define TEST_DURATION_SEC 3

static volatile bool stop_workers;
static pthread_t workers[NUM_WORKERS];

static void *worker_fn(void *arg)
{
	while (!stop_workers) {
		usleep(500);
		sched_yield();
	}
	return NULL;
}

static enum scx_test_status setup(void **ctx)
{
	struct dsq_reenq *skel;

	if (!__COMPAT_has_ksym("scx_bpf_dsq_reenq")) {
		fprintf(stderr, "SKIP: scx_bpf_dsq_reenq() not available\n");
		return SCX_TEST_SKIP;
	}

	skel = dsq_reenq__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(dsq_reenq__load(skel), "Failed to load skel");

	*ctx = skel;
	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct dsq_reenq *skel = ctx;
	struct bpf_link *link;
	int i;

	link = bpf_map__attach_struct_ops(skel->maps.dsq_reenq_ops);
	SCX_FAIL_IF(!link, "Failed to attach scheduler");

	stop_workers = false;
	for (i = 0; i < NUM_WORKERS; i++) {
		SCX_FAIL_IF(pthread_create(&workers[i], NULL, worker_fn, NULL),
			    "Failed to create worker %d", i);
	}

	sleep(TEST_DURATION_SEC);

	stop_workers = true;
	for (i = 0; i < NUM_WORKERS; i++)
		pthread_join(workers[i], NULL);

	bpf_link__destroy(link);

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_UNREG));
	SCX_GT(skel->bss->nr_reenq_kfunc, 0);

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct dsq_reenq *skel = ctx;

	dsq_reenq__destroy(skel);
}

struct scx_test dsq_reenq = {
	.name		= "dsq_reenq",
	.description	= "Verify scx_bpf_dsq_reenq() triggers enqueue with "
			  "SCX_ENQ_REENQ and SCX_TASK_REENQ_KFUNC reason",
	.setup		= setup,
	.run		= run,
	.cleanup	= cleanup,
};
REGISTER_SCX_TEST(&dsq_reenq)
