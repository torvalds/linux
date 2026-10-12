// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 fangqiurong <fangqiurong@kylinos.cn>
 */
#include <bpf/bpf.h>
#include <scx/common.h>
#include <time.h>
#include <unistd.h>
#include "dequeue_iter.bpf.skel.h"
#include "scx_test.h"

#define DQ_TARGET	10
#define DQ_DEADLINE_MS	3000

static unsigned long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000;
}

static enum scx_test_status setup(void **ctx)
{
	struct dequeue_iter *skel;

	skel = dequeue_iter__open();
	SCX_FAIL_IF(!skel, "Failed to open");
	SCX_ENUM_INIT(skel);
	SCX_FAIL_IF(dequeue_iter__load(skel), "Failed to load skel");

	*ctx = skel;

	return SCX_TEST_PASS;
}

static enum scx_test_status run(void *ctx)
{
	struct dequeue_iter *skel = ctx;
	struct bpf_link *link;
	unsigned long long end;

	link = bpf_map__attach_struct_ops(skel->maps.dequeue_iter_ops);
	SCX_FAIL_IF(!link, "Failed to attach scheduler");

	end = now_ms() + DQ_DEADLINE_MS;
	while (skel->bss->dq_count < DQ_TARGET && !UEI_EXITED(skel, uei) &&
	       now_ms() < end)
		usleep(100);

	bpf_link__destroy(link);

	SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_UNREG));

	if (skel->bss->dq_count < DQ_TARGET) {
		SCX_ERR("ops.dequeue() fired only %llu times",
			(unsigned long long)skel->bss->dq_count);
		return SCX_TEST_FAIL;
	}

	return SCX_TEST_PASS;
}

static void cleanup(void *ctx)
{
	struct dequeue_iter *skel = ctx;

	dequeue_iter__destroy(skel);
}

struct scx_test dequeue_iter = {
	.name = "dequeue_iter",
	.description = "Verify ops.dequeue() can iterate its source user DSQ",
	.setup = setup,
	.run = run,
	.cleanup = cleanup,
};
REGISTER_SCX_TEST(&dequeue_iter)
