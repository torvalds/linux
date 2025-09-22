/*	$OpenBSD: bn_general.c,v 1.2 2023/04/11 05:53:53 jsing Exp $ */
/*
 * Copyright (c) 2022, 2023 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/resource.h>
#include <sys/time.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openssl/bn.h>

static void
benchmark_bn_copy_setup(BIGNUM *dst, BIGNUM *src, int n)
{
	if (!BN_set_bit(dst, n - 1))
		errx(1, "BN_set_bit");
	if (!BN_set_bit(src, n - 1))
		errx(1, "BN_set_bit");
}

static void
benchmark_bn_copy_run_once(BIGNUM *dst, BIGNUM *src)
{
	if (BN_copy(dst, src) == NULL)
		errx(1, "BN_copy");
}

struct benchmark {
	const char *desc;
	void (*setup)(BIGNUM *, BIGNUM *, int);
	void (*run_once)(BIGNUM *, BIGNUM *);
	int bits;
};

struct benchmark benchmarks[] = {
	{
		.desc = "BN_copy() 32 bits",
		.setup = benchmark_bn_copy_setup,
		.run_once = benchmark_bn_copy_run_once,
		.bits = 32,
	},
	{
		.desc = "BN_copy() 256 bits",
		.setup = benchmark_bn_copy_setup,
		.run_once = benchmark_bn_copy_run_once,
		.bits = 256,
	},
	{
		.desc = "BN_copy() 320 bits",
		.setup = benchmark_bn_copy_setup,
		.run_once = benchmark_bn_copy_run_once,
		.bits = 320,
	},
	{
		.desc = "BN_copy() 512 bits",
		.setup = benchmark_bn_copy_setup,
		.run_once = benchmark_bn_copy_run_once,
		.bits = 512,
	},
	{
		.desc = "BN_copy() 1024 bits",
		.setup = benchmark_bn_copy_setup,
		.run_once = benchmark_bn_copy_run_once,
		.bits = 1024,
	},
	{
		.desc = "BN_copy() 2048 bits",
		.setup = benchmark_bn_copy_setup,
		.run_once = benchmark_bn_copy_run_once,
		.bits = 2048,
	},
	{
		.desc = "BN_copy() 4096 bits",
		.setup = benchmark_bn_copy_setup,
		.run_once = benchmark_bn_copy_run_once,
		.bits = 4096,
	},
	{
		.desc = "BN_copy() 16384 bits",
		.setup = benchmark_bn_copy_setup,
		.run_once = benchmark_bn_copy_run_once,
		.bits = 16384,
	},
};

#define N_BENCHMARKS (sizeof(benchmarks) / sizeof(benchmarks[0]))

static int benchmark_stop;

static void
benchmark_sig_alarm(int sig)
{
	benchmark_stop = 1;
}

static void
benchmark_run(const struct benchmark *bm, int seconds)
{
	struct timespec start, end, duration;
	struct rusage rusage;
	BIGNUM *dst, *src;
	int i;

	signal(SIGALRM, benchmark_sig_alarm);

	if ((src = BN_new()) == NULL)
		errx(1, "BN_new");
	if ((dst = BN_new()) == NULL)
		errx(1, "BN_new");

	bm->setup(dst, src, bm->bits);

	benchmark_stop = 0;
	i = 0;
	alarm(seconds);

	if (getrusage(RUSAGE_SELF, &rusage) == -1)
		err(1, "getrusage failed");
	TIMEVAL_TO_TIMESPEC(&rusage.ru_utime, &start);

	fprintf(stderr, "Benchmarking %s for %ds: ", bm->desc, seconds);
	while (!benchmark_stop) {
		bm->run_once(dst, src);
		i++;
	}
	if (getrusage(RUSAGE_SELF, &rusage) == -1)
		err(1, "getrusage failed");
	TIMEVAL_TO_TIMESPEC(&rusage.ru_utime, &end);

	timespecsub(&end, &start, &duration);
	fprintf(stderr, "%d iterations in %f seconds - %llu op/s\n", i,
	    duration.tv_sec + duration.tv_nsec / 1000000000.0,
	    (uint64_t)i * 1000000000 /
	    (duration.tv_sec * 1000000000 + duration.tv_nsec));

	BN_free(src);
	BN_free(dst);
}

static void
benchmark_bn_general(void)
{
	const struct benchmark *bm;
	size_t i;

	for (i = 0; i < N_BENCHMARKS; i++) {
		bm = &benchmarks[i];
		benchmark_run(bm, 5);
	}
}

int
main(int argc, char **argv)
{
	int benchmark = 0, failed = 0;

	if (argc == 2 && strcmp(argv[1], "--benchmark") == 0)
		benchmark = 1;

	if (benchmark && !failed)
		benchmark_bn_general();

	return failed;
}
