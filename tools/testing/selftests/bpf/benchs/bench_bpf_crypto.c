// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include <argp.h>
#include "bench.h"
#include "crypto_bench.skel.h"

#define MAX_CIPHER_LEN 32
static char *input;
static struct crypto_ctx {
	struct crypto_bench *skel;
	int pfd;
} ctx;

static struct crypto_args {
	u32 crypto_len;
	char *crypto_cipher;
} args = {
	.crypto_len = 16,
	.crypto_cipher = "ecb(aes)",
};

enum {
	ARG_CRYPTO_LEN = 5000,
	ARG_CRYPTO_CIPHER = 5001,
};

static const struct argp_option opts[] = {
	{ "crypto-len", ARG_CRYPTO_LEN, "CRYPTO_LEN", 0,
	  "Set the length of crypto buffer" },
	{ "crypto-cipher", ARG_CRYPTO_CIPHER, "CRYPTO_CIPHER", 0,
	  "Set the cipher to use (default:ecb(aes))" },
	{},
};

static error_t crypto_parse_arg(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case ARG_CRYPTO_LEN:
		args.crypto_len = strtoul(arg, NULL, 10);
		if (!args.crypto_len ||
		    args.crypto_len > sizeof(ctx.skel->bss->dst)) {
			fprintf(stderr, "Invalid crypto buffer len (limit %zu)\n",
				sizeof(ctx.skel->bss->dst));
			argp_usage(state);
		}
		break;
	case ARG_CRYPTO_CIPHER:
		args.crypto_cipher = strdup(arg);
		if (!strlen(args.crypto_cipher) ||
		    strlen(args.crypto_cipher) > MAX_CIPHER_LEN) {
			fprintf(stderr, "Invalid crypto cipher len (limit %d)\n",
				MAX_CIPHER_LEN);
			argp_usage(state);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

const struct argp bench_crypto_argp = {
	.options = opts,
	.parser = crypto_parse_arg,
};

static void crypto_validate(void)
{
	if (env.consumer_cnt != 0) {
		fprintf(stderr, "bpf crypto benchmark doesn't support consumer!\n");
		exit(1);
	}
}

static void crypto_setup(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);

	int err, pfd;
	size_t i, sz;

	sz = args.crypto_len;
	if (!sz || sz > sizeof(ctx.skel->bss->dst)) {
		fprintf(stderr, "invalid encrypt buffer size (source %zu, target %zu)\n",
			sz, sizeof(ctx.skel->bss->dst));
		exit(1);
	}

	setup_libbpf();

	ctx.skel = crypto_bench__open();
	if (!ctx.skel) {
		fprintf(stderr, "failed to open skeleton\n");
		exit(1);
	}

	snprintf(ctx.skel->bss->cipher, 128, "%s", args.crypto_cipher);
	memcpy(ctx.skel->bss->key, "12345678testtest", 16);
	ctx.skel->bss->key_len = 16;
	ctx.skel->bss->authsize = 0;

	srandom(time(NULL));
	input = malloc(sz);
	for (i = 0; i < sz - 1; i++)
		input[i] = '1' + random() % 9;
	input[sz - 1] = '\0';

	ctx.skel->rodata->len = args.crypto_len;

	err = crypto_bench__load(ctx.skel);
	if (err) {
		fprintf(stderr, "failed to load skeleton\n");
		crypto_bench__destroy(ctx.skel);
		exit(1);
	}

	pfd = bpf_program__fd(ctx.skel->progs.crypto_setup);
	if (pfd < 0) {
		fprintf(stderr, "failed to get fd for setup prog\n");
		crypto_bench__destroy(ctx.skel);
		exit(1);
	}

	err = bpf_prog_test_run_opts(pfd, &opts);
	if (err || ctx.skel->bss->status) {
		fprintf(stderr, "failed to run setup prog: err %d, status %d\n",
			err, ctx.skel->bss->status);
		crypto_bench__destroy(ctx.skel);
		exit(1);
	}
}

static void crypto_encrypt_setup(void)
{
	crypto_setup();
	ctx.pfd = bpf_program__fd(ctx.skel->progs.crypto_encrypt);
}

static void crypto_decrypt_setup(void)
{
	crypto_setup();
	ctx.pfd = bpf_program__fd(ctx.skel->progs.crypto_decrypt);
}

static void crypto_measure(struct bench_res *res)
{
	res->hits = atomic_swap(&ctx.skel->bss->hits, 0);
}

static void *crypto_producer(void *unused)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.repeat = 64,
		.data_in = input,
		.data_size_in = args.crypto_len,
	);

	while (true)
		(void)bpf_prog_test_run_opts(ctx.pfd, &opts);
	return NULL;
}

const struct bench bench_crypto_encrypt = {
	.name = "crypto-encrypt",
	.argp = &bench_crypto_argp,
	.validate = crypto_validate,
	.setup = crypto_encrypt_setup,
	.producer_thread = crypto_producer,
	.measure = crypto_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};

const struct bench bench_crypto_decrypt = {
	.name = "crypto-decrypt",
	.argp = &bench_crypto_argp,
	.validate = crypto_validate,
	.setup = crypto_decrypt_setup,
	.producer_thread = crypto_producer,
	.measure = crypto_measure,
	.report_progress = hits_drops_report_progress,
	.report_final = hits_drops_report_final,
};
