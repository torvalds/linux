/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Test SCX_KICK_WAIT forward progress under cyclic wait pressure.
 *
 * SCX_KICK_WAIT busy-waits until the target CPU enters the scheduling path.
 * If multiple CPUs form a wait cycle (A waits for B, B waits for C, C waits
 * for A), all CPUs deadlock unless the implementation breaks the cycle.
 *
 * This test creates that scenario: three CPUs are arranged in a ring. The BPF
 * scheduler's ops.enqueue() kicks the next CPU in the ring with SCX_KICK_WAIT
 * on every enqueue. Userspace pins 4 worker threads per CPU that loop calling
 * sched_yield(), generating a steady stream of enqueues and thus sustained
 * A->B->C->A kick_wait cycle pressure. The test passes if the system remains
 * responsive for 5 seconds without the scheduler being killed by the watchdog.
 */
#define _GNU_SOURCE

#include <bpf/bpf.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <scx/common.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "scx_test.h"
#include "cyclic_kick_wait.bpf.skel.h"

#define WORKERS_PER_CPU	4
#define NR_TEST_CPUS	3
#define NR_WORKERS	(NR_TEST_CPUS * WORKERS_PER_CPU)

struct worker_ctx {
	pthread_t tid;
	int cpu;
	volatile bool stop;
	volatile __u64 iters;
	bool started;
};

static void *worker_fn(void *arg)
{
	struct worker_ctx *worker = arg;
	cpu_set_t mask;

	CPU_ZERO(&mask);
	CPU_SET(worker->cpu, &mask);

	if (sched_setaffinity(0, sizeof(mask), &mask))
		return (void *)(uintptr_t)errno;

	while (!worker->stop) {
		sched_yield();
		worker->iters++;
	}

	return NULL;
}

static int join_worker(struct worker_ctx *worker)
{
	void *ret;
	struct timespec ts;
	int err;

	if (!worker->started)
		return 0;

	if (clock_gettime(CLOCK_REALTIME, &ts))
		return -errno;

	ts.tv_sec += 2;
	err = pthread_timedjoin_np(worker->tid, &ret, &ts);
	if (err == ETIMEDOUT)
		pthread_detach(worker->tid);
	if (err)
		return -err;

	if ((uintptr_t)ret)
		return -(int)(uintptr_t)ret;

	return 0;
}

static enum scx_test_status setup(void **ctx)
{
	struct cyclic_kick_wait *skel;

	skel = cyclic_kick_wait__open();
	SCX_FAIL_IF(!skel, "Failed to open skel");
	SCX_ENUM_INIT(skel);

	*ctx = skel;
	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct cyclic_kick_wait *skel = ctx;
	struct worker_ctx workers[NR_WORKERS] = {};
	struct bpf_link *link = NULL;
	enum scx_test_status status = SCX_TEST_PASS;
	int test_cpus[NR_TEST_CPUS];
	int nr_cpus = 0;
	cpu_set_t mask;
	int ret, i;

	if (sched_getaffinity(0, sizeof(mask), &mask)) {
		SCX_ERR("Failed to get affinity (%d)", errno);
		return SCX_TEST_FAIL;
	}

	for (i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, &mask))
			test_cpus[nr_cpus++] = i;
		if (nr_cpus == NR_TEST_CPUS)
			break;
	}

	if (nr_cpus < NR_TEST_CPUS)
		return SCX_TEST_SKIP;

	skel->rodata->test_cpu_a = test_cpus[0];
	skel->rodata->test_cpu_b = test_cpus[1];
	skel->rodata->test_cpu_c = test_cpus[2];

	if (cyclic_kick_wait__load(skel)) {
		SCX_ERR("Failed to load skel");
		return SCX_TEST_FAIL;
	}

	link = bpf_map__attach_struct_ops(skel->maps.cyclic_kick_wait_ops);
	if (!link) {
		SCX_ERR("Failed to attach scheduler");
		return SCX_TEST_FAIL;
	}

	for (i = 0; i < NR_WORKERS; i++)
		workers[i].cpu = test_cpus[i / WORKERS_PER_CPU];

	for (i = 0; i < NR_WORKERS; i++) {
		ret = pthread_create(&workers[i].tid, NULL, worker_fn, &workers[i]);
		if (ret) {
			SCX_ERR("Failed to create worker thread %d (%d)", i, ret);
			status = SCX_TEST_FAIL;
			goto out;
		}
		workers[i].started = true;
	}

	sleep(5);

	if (skel->data->uei.kind != EXIT_KIND(SCX_EXIT_NONE)) {
		SCX_ERR("Scheduler exited unexpectedly (kind=%llu code=%lld)",
			(unsigned long long)skel->data->uei.kind,
			(long long)skel->data->uei.exit_code);
		status = SCX_TEST_FAIL;
	}

out:
	for (i = 0; i < NR_WORKERS; i++)
		workers[i].stop = true;

	for (i = 0; i < NR_WORKERS; i++) {
		ret = join_worker(&workers[i]);
		if (ret && status == SCX_TEST_PASS) {
			SCX_ERR("Failed to join worker thread %d (%d)", i, ret);
			status = SCX_TEST_FAIL;
		}
	}

	if (link)
		bpf_link__destroy(link);

	return status;
}

static void cleanup(void *ctx)
{
	struct cyclic_kick_wait *skel = ctx;

	cyclic_kick_wait__destroy(skel);
}

struct scx_test cyclic_kick_wait = {
	.name = "cyclic_kick_wait",
	.description = "Verify SCX_KICK_WAIT forward progress under a 3-CPU wait cycle",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&cyclic_kick_wait)
