/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#include <bpf/bpf.h>
#include <sched.h>
#include <scx/common.h>
#include <sys/wait.h>
#include <unistd.h>
#include "exit.bpf.skel.h"
#include "scx_test.h"

#include "exit_test.h"

static enum scx_test_status run(void *ctx)
{
	enum exit_test_case tc;

	for (tc = 0; tc < NUM_EXITS; tc++) {
		struct exit *skel;
		struct bpf_link *link;
		char buf[16];

		skel = exit__open();
		skel->rodata->exit_point = tc;
		exit__load(skel);
		link = bpf_map__attach_struct_ops(skel->maps.exit_ops);
		if (!link) {
			SCX_ERR("Failed to attach scheduler");
			exit__destroy(skel);
			return SCX_TEST_FAIL;
		}

		/* Assumes uei.kind is written last */
		while (skel->data->uei.kind == EXIT_KIND(SCX_EXIT_NONE))
			sched_yield();

		SCX_EQ(skel->data->uei.kind, EXIT_KIND(SCX_EXIT_UNREG_BPF));
		SCX_EQ(skel->data->uei.exit_code, tc);
		sprintf(buf, "%d", tc);
		SCX_ASSERT(!strcmp(skel->data->uei.msg, buf));
		bpf_link__destroy(link);
		exit__destroy(skel);
	}

	return SCX_TEST_PASS;
}

struct scx_test exit_test = {
	.name = "exit",
	.description = "Verify we can cleanly exit a scheduler in multiple places",
	.run = run,
};
REGISTER_SCX_TEST(&exit_test)
