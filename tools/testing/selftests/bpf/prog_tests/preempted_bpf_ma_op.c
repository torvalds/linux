// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <stdbool.h>
#include <test_progs.h>

#include "preempted_bpf_ma_op.skel.h"

#define ALLOC_THREAD_NR 4
#define ALLOC_LOOP_NR 512

struct alloc_ctx {
	/* output */
	int run_err;
	/* input */
	int fd;
	bool *nomem_err;
};

static void *run_alloc_prog(void *data)
{
	struct alloc_ctx *ctx = data;
	cpu_set_t cpu_set;
	int i;

	CPU_ZERO(&cpu_set);
	CPU_SET(0, &cpu_set);
	pthread_setaffinity_np(pthread_self(), sizeof(cpu_set), &cpu_set);

	for (i = 0; i < ALLOC_LOOP_NR && !*ctx->nomem_err; i++) {
		LIBBPF_OPTS(bpf_test_run_opts, topts);
		int err;

		err = bpf_prog_test_run_opts(ctx->fd, &topts);
		ctx->run_err |= err | topts.retval;
	}

	return NULL;
}

void test_preempted_bpf_ma_op(void)
{
	struct alloc_ctx ctx[ALLOC_THREAD_NR];
	struct preempted_bpf_ma_op *skel;
	pthread_t tid[ALLOC_THREAD_NR];
	int i, err;

	skel = preempted_bpf_ma_op__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	err = preempted_bpf_ma_op__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto out;

	for (i = 0; i < ARRAY_SIZE(ctx); i++) {
		struct bpf_program *prog;
		char name[8];

		snprintf(name, sizeof(name), "test%d", i);
		prog = bpf_object__find_program_by_name(skel->obj, name);
		if (!ASSERT_OK_PTR(prog, "no test prog"))
			goto out;

		ctx[i].run_err = 0;
		ctx[i].fd = bpf_program__fd(prog);
		ctx[i].nomem_err = &skel->bss->nomem_err;
	}

	memset(tid, 0, sizeof(tid));
	for (i = 0; i < ARRAY_SIZE(tid); i++) {
		err = pthread_create(&tid[i], NULL, run_alloc_prog, &ctx[i]);
		if (!ASSERT_OK(err, "pthread_create"))
			break;
	}

	for (i = 0; i < ARRAY_SIZE(tid); i++) {
		if (!tid[i])
			break;
		pthread_join(tid[i], NULL);
		ASSERT_EQ(ctx[i].run_err, 0, "run prog err");
	}

	ASSERT_FALSE(skel->bss->nomem_err, "ENOMEM");
out:
	preempted_bpf_ma_op__destroy(skel);
}
