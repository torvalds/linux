// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021. Huawei Technologies Co., Ltd */
#include <argp.h>
#include "bench.h"
#include "strncmp_bench.skel.h"

static struct strncmp_ctx {
	struct strncmp_bench *skel;
} ctx;

static struct strncmp_args {
	u32 cmp_str_len;
} args = {
	.cmp_str_len = 32,
};

enum {
	ARG_CMP_STR_LEN = 5000,
};

static const struct argp_option opts[] = {
	{ "cmp-str-len", ARG_CMP_STR_LEN, "CMP_STR_LEN", 0,
	  "Set the length of compared string" },
	{},
};

static error_t strncmp_parse_arg(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case ARG_CMP_STR_LEN:
		args.cmp_str_len = strtoul(arg, NULL, 10);
		if (!args.cmp_str_len ||
		    args.cmp_str_len >= sizeof(ctx.skel->bss->str)) {
			fprintf(stderr, "Invalid cmp str len (limit %zu)\n",
				sizeof(ctx.skel->bss->str));
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

const struct argp bench_strncmp_argp = {
	.options = opts,
	.parser = strncmp_parse_arg,
};

static void strncmp_validate(void)
{
	if (env.consumer_cnt != 1) {
		fprintf(stderr, "strncmp benchmark doesn't support multi-consumer!\n");
		exit(1);
	}
}

static void strncmp_setup(void)
{
	int err;
	char *target;
	size_t i, sz;

	sz = sizeof(ctx.skel->rodata->target);
	if (!sz || sz < sizeof(ctx.skel->bss->str)) {
		fprintf(stderr, "invalid string size (target %zu, src %zu)\n",
			sz, sizeof(ctx.skel->bss->str));
		exit(1);
	}

	setup_libbpf();

	ctx.skel = strncmp_bench__open();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	srandom(time(NULL));
	target = ctx.skel->rodata->target;
	for (i = 0; i < sz - 1; i++)
		target[i] = '1' + random() % 9;
	target[sz - 1] = '\0';

	ctx.skel->rodata->cmp_str_len = args.cmp_str_len;

	memcpy(ctx.skel->bss->str, target, args.cmp_str_len);
	ctx.skel->bss->str[args.cmp_str_len] = '\0';
	/* Make bss->str < rodata->target */
	ctx.skel->bss->str[args.cmp_str_len - 1] -= 1;

	err = strncmp_bench__load(ctx.skel);
	if (err) {
		fprintf(stderr, "failed to load skeleton\n");
		strncmp_bench__destroy(ctx.skel);
		exit(1);
	}
}

static void strncmp_attach_prog(struct bpf_program *prog)
{
	struct bpf_link *link;

	link = bpf_program__attach(prog);
	if (!link) {
		fprintf(stderr, "failed to attach program!\n");
		exit(1);
	}
}

static void strncmp_no_helper_setup(void)
{
	strncmp_setup();
	strncmp_attach_prog(ctx.skel->progs.strncmp_no_helper);
}

static void strncmp_helper_setup(void)
{
	strncmp_setup();
	strncmp_attach_prog(ctx.skel->progs.strncmp_helper);
}

static void *strncmp_producer(void *ctx)
{
	while (true)
		(void)syscall(__NR_getpgid);
	return NULL;
}

static void *strncmp_consumer(void *ctx)
{
	return NULL;
}

static void strncmp_measure(struct bench_res *res)
{
	res->hits = atomic_swap(&ctx.skel->bss->hits, 0);
}

const struct bench bench_strncmp_no_helper = {
	.name = "strncmp-no-helper",
	.validate = strncmp_validate,
	.setup = strncmp_no_helper_setup,
	.producer_thread = strncmp_producer,
	.consumer_thread = strncmp_consumer,
	.measure = strncmp_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_strncmp_helper = {
	.name = "strncmp-helper",
	.validate = strncmp_validate,
	.setup = strncmp_helper_setup,
	.producer_thread = strncmp_producer,
	.consumer_thread = strncmp_consumer,
	.measure = strncmp_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};
