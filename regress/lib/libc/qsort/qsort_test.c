/*
 * Copyright (c) 2017 Todd C. Miller <millert@openbsd.org>
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

#include <sys/time.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <time.h>
#include <unistd.h>
#include <err.h>

/*
 * Test program based on Bentley & McIlroy's "Engineering a Sort Function".
 * Uses mergesort(3) to check the results.
 *
 * Additional "killer" input taken from:
 *  David R. Musser's "Introspective Sorting and Selection Algorithms"
 *  http://calmerthanyouare.org/2014/06/11/algorithmic-complexity-attacks-and-libc-qsort.html
 *  M. D. McIlroy's "A Killer Adversary for Quicksort"
 */

/*
 * TODO:
 *	test with unaligned elements (char[]?)
 */
struct test_distribution {
	const char *name;
	void (*fill)(void *x, int n, int m);
	int (*test)(struct test_distribution *d, int n, void *x, void *y, void *z);
	int (*cmp)(const void *v1, const void *v2);
	int (*cmp_checked)(const void *v1, const void *v2);
};

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

static size_t compares;
static size_t max_compares;
static size_t abrt_compares;
static sigjmp_buf cmpjmp;
static bool dump_table, timing, verbose;

extern int antiqsort(int n, int *a, int *ptr);

static int
cmp_i(const void *v1, const void *v2)
{
	const int a = *(const int *)v1;
	const int b = *(const int *)v2;

	return a > b ? 1 : a < b ? -1 : 0;
}

static int
cmp_checked_i(const void *v1, const void *v2)
{
	const int a = *(const int *)v1;
	const int b = *(const int *)v2;

	compares++;
	if (compares > abrt_compares)
		siglongjmp(cmpjmp, 1);
	return a > b ? 1 : a < b ? -1 : 0;
}

static int
cmp_ll(const void *v1, const void *v2)
{
	const long long a = *(const long long *)v1;
	const long long b = *(const long long *)v2;

	return a > b ? 1 : a < b ? -1 : 0;
}

static int
cmp_checked_ll(const void *v1, const void *v2)
{
	const long long a = *(const long long *)v1;
	const long long b = *(const long long *)v2;

	compares++;
	if (compares > abrt_compares)
		siglongjmp(cmpjmp, 1);
	return a > b ? 1 : a < b ? -1 : 0;
}

static int
cmp_d(const void *v1, const void *v2)
{
	const double a = *(const double *)v1;
	const double b = *(const double *)v2;

	return a > b ? 1 : a < b ? -1 : 0;
}

static int
cmp_checked_d(const void *v1, const void *v2)
{
	const double a = *(const double *)v1;
	const double b = *(const double *)v2;

	compares++;
	if (compares > abrt_compares)
		siglongjmp(cmpjmp, 1);
	return a > b ? 1 : a < b ? -1 : 0;
}

static int
check_result(char *sub, size_t es, char *got, char *expected, struct test_distribution *d,
    int m, int n)
{
	int i;

	if (verbose || compares > max_compares) {
		if (sub != NULL) {
			if (m != 0) {
				warnx("%s (%s): m: %d, n: %d, %zu compares, "
				    "max %zu(%zu)", d->name, sub, m, n,
				    compares, max_compares, abrt_compares);
			} else {
				warnx("%s (%s): n: %d, %zu compares, "
				    "max %zu(%zu)", d->name, sub, n,
				    compares, max_compares, abrt_compares);
			}
		} else {
			if (m != 0) {
				warnx("%s: m: %d, n: %d, %zu compares, "
				    "max %zu(%zu)", d->name, m, n,
				    compares, max_compares, abrt_compares);
			} else {
				warnx("%s: n: %d, %zu compares, "
				    "max %zu(%zu)", d->name, n,
				    compares, max_compares, abrt_compares);
			}
		}
	}

	for (i = 0; i < n; i++) {
		if (memcmp(got, expected, es) != 0)
			break;
		got += es;
		expected += es;
	}
	if (i == n)
		return 0;

	if (sub != NULL) {
		warnx("%s (%s): failure at %d, m: %d, n: %d",
		    d->name, sub, i, m, n);
	} else {
		warnx("%s: failure at %d, m: %d, n: %d",
		    d->name, i, m, n);
	}
	return 1;
}

#define FILL_SAWTOOTH(x, n, m)	do {					\
	int i;								\
									\
	for (i = 0; i < n; i++)						\
		x[i] = i % m;						\
} while (0)

static void
fill_sawtooth_i(void *v, int n, int m)
{
	int *x = v;
	FILL_SAWTOOTH(x, n, m);
}

static void
fill_sawtooth_ll(void *v, int n, int m)
{
	long long *x = v;
	FILL_SAWTOOTH(x, n, m);
}

static void
fill_sawtooth_double(void *v, int n, int m)
{
	double *x = v;
	FILL_SAWTOOTH(x, n, m);
}

#define FILL_RANDOM(x, n, m)	do {					\
	int i;								\
									\
	for (i = 0; i < n; i++)						\
		x[i] = arc4random_uniform(m);				\
} while (0)


static void
fill_random_i(void *v, int n, int m)
{
	int *x = v;
	FILL_RANDOM(x, n, m);
}

static void
fill_random_ll(void *v, int n, int m)
{
	long long *x = v;
	FILL_RANDOM(x, n, m);
}

static void
fill_random_double(void *v, int n, int m)
{
	double *x = v;
	FILL_RANDOM(x, n, m);
}

#define FILL_STAGGER(x, n, m)	do {					\
	int i;								\
									\
	for (i = 0; i < n; i++)						\
		x[i] = (i * m + i) % n;					\
} while (0)


static void
fill_stagger_i(void *v, int n, int m)
{
	int *x = v;
	FILL_STAGGER(x, n, m);
}

static void
fill_stagger_ll(void *v, int n, int m)
{
	long long *x = v;
	FILL_STAGGER(x, n, m);
}

static void
fill_stagger_double(void *v, int n, int m)
{
	double *x = v;
	FILL_STAGGER(x, n, m);
}

#define FILL_PLATEAU(x, n, m)	do {					\
	int i;								\
									\
	for (i = 0; i < n; i++)						\
		x[i] = MINIMUM(i, m);					\
} while (0)

static void
fill_plateau_i(void *v, int n, int m)
{
	int *x = v;
	FILL_PLATEAU(x, n, m);
}

static void
fill_plateau_ll(void *v, int n, int m)
{
	long long *x = v;
	FILL_PLATEAU(x, n, m);
}

static void
fill_plateau_double(void *v, int n, int m)
{
	double *x = v;
	FILL_PLATEAU(x, n, m);
}

#define FILL_SHUFFLE(x, n, m)	do {					\
	int i, j, k;							\
									\
	for (i = 0, j = 0, k = 1; i < n; i++)				\
		x[i] = arc4random_uniform(m) ? (j += 2) : (k += 2);	\
} while (0)

static void
fill_shuffle_i(void *v, int n, int m)
{
	int *x = v;
	FILL_SHUFFLE(x, n, m);
}

static void
fill_shuffle_ll(void *v, int n, int m)
{
	long long *x = v;
	FILL_SHUFFLE(x, n, m);
}

static void
fill_shuffle_double(void *v, int n, int m)
{
	double *x = v;
	FILL_SHUFFLE(x, n, m);
}

#define FILL_BSD_KILLER(x, n, m)	do {				\
	int i, k;							\
									\
	/* 4.4BSD insertion sort optimization killer, Mats Linander */	\
	k = n / 2;							\
	for (i = 0; i < n; i++) {					\
		if (i < k)						\
			x[i] = k - i;					\
		else if (i > k)						\
			x[i] = n + k + 1 - i;				\
		else							\
			x[i] = k + 1;					\
	}								\
} while (0)

static void
fill_bsd_killer_i(void *v, int n, int m)
{
	int *x = v;
	FILL_BSD_KILLER(x, n, m);

}

static void
fill_bsd_killer_ll(void *v, int n, int m)
{
	long long *x = v;
	FILL_BSD_KILLER(x, n, m);

}

static void
fill_bsd_killer_double(void *v, int n, int m)
{
	double *x = v;
	FILL_BSD_KILLER(x, n, m);

}

#define FILL_MED3_KILLER(x, n, m)	do {				\
	int i, k;							\
									\
	/*								\
	 * Median of 3 killer, as seen in David R Musser's		\
	 * "Introspective Sorting and Selection Algorithms"		\
	 */								\
	if (n & 1) {							\
		/* odd size, store last at end and make even. */	\
		x[n - 1] = n;						\
		n--;							\
	}								\
	k = n / 2;							\
	for (i = 0; i < n; i++) {					\
		if (i & 1) {						\
			x[i] = k + x[i - 1];				\
		} else {						\
			x[i] = i + 1;					\
		}							\
	}								\
} while (0)

static void
fill_med3_killer_i(void *v, int n, int m)
{
	int *x = v;
	FILL_MED3_KILLER(x, n, m);
}

static void
fill_med3_killer_ll(void *v, int n, int m)
{
	long long *x = v;
	FILL_MED3_KILLER(x, n, m);
}

static void
fill_med3_killer_double(void *v, int n, int m)
{
	double *x = v;
	FILL_MED3_KILLER(x, n, m);
}

static void
print_timing(struct test_distribution *d, char *sub, int m, int n, double elapsed)
{
	if (sub != NULL) {
		if (m != 0) {
			warnx("%s (%s): m: %d, n: %d, %f seconds",
			    d->name, sub, m, n, elapsed);
		} else {
			warnx("%s (%s): n: %d, %f seconds",
			    d->name, sub, n, elapsed);
		}
	} else {
		if (m != 0) {
			warnx("%s: m: %d, n: %d, %f seconds",
			    d->name, m, n, elapsed);
		} else {
			warnx("%s: n: %d, %f seconds",
			    d->name, n, elapsed);
		}
	}
}

static int
do_test(struct test_distribution *d, char *sub, int m, int n, size_t es, void *y, void *z)
{
	int ret = 0;
	struct timespec before, after;

	compares = 0;
	if (sigsetjmp(cmpjmp, 1) != 0) {
		if (sub != NULL) {
			warnx("%s (%s): qsort aborted after %zu compares, m: %d, n: %d",
			    d->name, sub, compares, m, n);
		} else {
			warnx("%s: qsort aborted after %zu compares, m: %d, n: %d",
			    d->name, compares, m, n);
		}
		ret = 1;
	} else {
		if (timing)
			clock_gettime(CLOCK_MONOTONIC, &before);
		qsort(y, n, es, d->cmp_checked);
		if (timing) {
			double elapsed;
			clock_gettime(CLOCK_MONOTONIC, &after);
			timespecsub(&after, &before, &after);
			elapsed = after.tv_sec + after.tv_nsec / 1000000000.0;
			print_timing(d, sub, m, n, elapsed);
		}
		if (check_result(sub, es, y, z, d, m, n) != 0)
			ret = 1;
	}
	return ret;
}

#define TEST_PERTURBED(d, n, x, y, z)	do {				\
	int i, j, m;							\
	const int es = sizeof(x[0]);					\
									\
	for (m = 1; m < 2 * n; m *= 2) {				\
		/* Fill in x[n] modified by m */			\
		d->fill(x, n, m);					\
									\
		/* Test on copy of x[] */				\
		for (i = 0; i < n; i++)					\
			y[i] = z[i] = x[i];				\
		if (mergesort(z, n, es, d->cmp) != 0)			\
			err(1, NULL);					\
		if (do_test(d, "copy", m, n, es, y, z) != 0)		\
			ret = 1;					\
									\
		/* Test on reversed copy of x[] */			\
		for (i = 0, j = n - 1; i < n; i++, j--)			\
			y[i] = z[i] = x[j];				\
		if (mergesort(z, n, es, d->cmp) != 0)			\
			err(1, NULL);					\
		if (do_test(d, "reversed", m, n, es, y, z) != 0)	\
			ret = 1;					\
									\
		/* Test with front half of x[] reversed */		\
		for (i = 0, j = (n / 2) - 1; i < n / 2; i++, j--)	\
			y[i] = z[i] = x[j];				\
		for (; i < n; i++)					\
			y[i] = z[i] = x[i];				\
		if (mergesort(z, n, es, d->cmp) != 0)			\
			err(1, NULL);					\
		if (do_test(d, "front reversed", m, n, es, y, z) != 0)	\
			ret = 1;					\
									\
		/* Test with back half of x[] reversed */		\
		for (i = 0; i < n / 2; i++)				\
			y[i] = z[i] = x[i];				\
		for (j = n - 1; i < n; i++, j--)			\
			y[i] = z[i] = x[j];				\
		if (mergesort(z, n, es, d->cmp) != 0)			\
			err(1, NULL);					\
		if (do_test(d, "back reversed", m, n, es, y, z) != 0)	\
			ret = 1;					\
									\
		/* Test on sorted copy of x[] */			\
		if (mergesort(x, n, es, d->cmp) != 0)			\
			err(1, NULL);					\
		for (i = 0; i < n; i++)					\
			y[i] = x[i];					\
		if (do_test(d, "sorted", m, n, es, y, x) != 0)		\
			ret = 1;					\
									\
		/* Test with i%5 added to x[i] (dither) */		\
		for (i = 0, j = n - 1; i < n; i++, j--)			\
			y[i] = z[i] = x[j] + (i % 5);			\
		if (mergesort(z, n, es, d->cmp) != 0)			\
			err(1, NULL);					\
		if (do_test(d, "dithered", m, n, es, y, z) != 0)	\
			ret = 1;					\
	}								\
} while (0)

static int
test_perturbed_i(struct test_distribution *d, int n, void *vx, void *vy, void *vz)
{
	int *x = vx;
	int *y = vx;
	int *z = vz;
	int ret = 0;

	TEST_PERTURBED(d, n, x, y, z);
	return ret;
}

static int
test_perturbed_ll(struct test_distribution *d, int n, void *vx, void *vy, void *vz)
{
	long long *x = vx;
	long long *y = vx;
	long long *z = vz;
	int ret = 0;

	TEST_PERTURBED(d, n, x, y, z);
	return ret;
}

static int
test_perturbed_double(struct test_distribution *d, int n, void *vx, void *vy, void *vz)
{
	double *x = vx;
	double *y = vx;
	double *z = vz;
	int ret = 0;

	TEST_PERTURBED(d, n, x, y, z);
	return ret;
}

/*
 * Like TEST_PERTURBED but because the input is tailored to cause
 * quicksort to go quadratic we don't perturb the input.
 */
#define TEST_SIMPLE(d, n, x, y, z)	do {				\
	int i, ret = 0;							\
									\
	/* Fill in x[n] */						\
	d->fill(x, n, 0);						\
									\
	/* Make a copy of x[] */					\
	for (i = 0; i < n; i++)						\
		y[i] = z[i] = x[i];					\
	if (mergesort(z, n, sizeof(z[0]), d->cmp) != 0)			\
		err(1, NULL);						\
	if (do_test(d, NULL, 0, n, sizeof(x[0]), y, z) != 0)		\
		ret = 1;						\
} while (0)

static int
test_simple_i(struct test_distribution *d, int n, void *vx, void *vy, void *vz)
{
	int *x = vx;
	int *y = vx;
	int *z = vz;
	int ret = 0;

	TEST_SIMPLE(d, n, x, y, z);
	return ret;
}

static int
test_simple_ll(struct test_distribution *d, int n, void *vx, void *vy, void *vz)
{
	long long *x = vx;
	long long *y = vx;
	long long *z = vz;
	int ret = 0;

	TEST_SIMPLE(d, n, x, y, z);
	return ret;
}

static int
test_simple_double(struct test_distribution *d, int n, void *vx, void *vy, void *vz)
{
	double *x = vx;
	double *y = vx;
	double *z = vz;
	int ret = 0;

	TEST_SIMPLE(d, n, x, y, z);
	return ret;
}

/*
 * Use D. McIlroy's "Killer Adversary for Quicksort"
 * We don't compare the results in this case.
 */
static int
test_antiqsort(struct test_distribution *d, int n, void *vx, void *vy, void *vz)
{
	struct timespec before, after;
	int *x = vx;
	int *y = vx;
	int i, ret = 0;

	/*
	 * We expect antiqsort to generate > 1.5 * nlgn compares.
	 * If introspection is not used, it will be > 10 * nlgn compares.
	 */
	if (timing)
		clock_gettime(CLOCK_MONOTONIC, &before);
	i = antiqsort(n, x, y);
	if (timing)
		clock_gettime(CLOCK_MONOTONIC, &after);
	if (i > abrt_compares)
		ret = 1;
	if (dump_table) {
		printf("/* %d items, %d compares */\n", n, i);
		printf("static const int table_%d[] = {", n);
		for (i = 0; i < n - 1; i++) {
			if ((i % 12) == 0)
				printf("\n\t");
			printf("%4d, ", x[i]);
		}
		printf("%4d\n};\n", x[i]);
	} else {
		if (timing) {
			double elapsed;
			timespecsub(&after, &before, &after);
			elapsed = after.tv_sec + after.tv_nsec / 1000000000.0;
			print_timing(d, NULL, 0, n, elapsed);
		}
		if (verbose || ret != 0) {
			warnx("%s: n: %d, %d compares, max %zu(%zu)",
			    d->name, n, i, max_compares, abrt_compares);
		}
	}

	return ret;
}

static struct test_distribution distributions[] = {
	{ "sawtooth_i", fill_sawtooth_i, test_perturbed_i, cmp_i, cmp_checked_i },
	{ "sawtooth_ll", fill_sawtooth_ll, test_perturbed_ll, cmp_ll, cmp_checked_ll },
	{ "sawtooth_d", fill_sawtooth_double, test_perturbed_double, cmp_d, cmp_checked_d },
	{ "random_i", fill_random_i, test_perturbed_i, cmp_i, cmp_checked_i },
	{ "random_ll", fill_random_ll, test_perturbed_ll, cmp_ll, cmp_checked_ll },
	{ "random_d", fill_random_double, test_perturbed_double, cmp_d, cmp_checked_d },
	{ "stagger_i", fill_stagger_i, test_perturbed_i, cmp_i, cmp_checked_i },
	{ "stagger_ll", fill_stagger_ll, test_perturbed_ll, cmp_ll, cmp_checked_ll },
	{ "stagger_d", fill_stagger_double, test_perturbed_double, cmp_d, cmp_checked_d },
	{ "plateau_i", fill_plateau_i, test_perturbed_i, cmp_i, cmp_checked_i },
	{ "plateau_ll", fill_plateau_ll, test_perturbed_ll, cmp_ll, cmp_checked_ll },
	{ "plateau_d", fill_plateau_double, test_perturbed_double, cmp_d, cmp_checked_d },
	{ "shuffle_i", fill_shuffle_i, test_perturbed_i, cmp_i, cmp_checked_i },
	{ "shuffle_ll", fill_shuffle_ll, test_perturbed_ll, cmp_ll, cmp_checked_ll },
	{ "shuffle_d", fill_shuffle_double, test_perturbed_double, cmp_d, cmp_checked_d },
	{ "bsd_killer_i", fill_bsd_killer_i, test_simple_i, cmp_i, cmp_checked_i },
	{ "bsd_killer_ll", fill_bsd_killer_ll, test_simple_ll, cmp_ll, cmp_checked_ll },
	{ "bsd_killer_d", fill_bsd_killer_double, test_simple_double, cmp_d, cmp_checked_d },
	{ "med3_killer_i", fill_med3_killer_i, test_simple_i, cmp_i, cmp_checked_i },
	{ "med3_killer_ll", fill_med3_killer_ll, test_simple_ll, cmp_ll, cmp_checked_ll },
	{ "med3_killer_d", fill_med3_killer_double, test_simple_double, cmp_d, cmp_checked_d },
	{ "antiqsort", NULL, test_antiqsort, cmp_i, cmp_checked_i },
	{ NULL }
};

static int
run_tests(int n, const char *name)
{
	void *x, *y, *z;
	int i, nlgn = 0;
	int ret = 0;
	size_t es;
	struct test_distribution *d;

	/*
	 * We expect A*n*lg(n) compares where A is between 1 and 2.
	 * For A > 1.5, print a warning.
	 * For A > 10 abort the test since qsort has probably gone quadratic.
	 */
	for (i = n - 1; i > 0; i >>= 1)
	    nlgn++;
	nlgn *= n;
	max_compares = nlgn * 1.5;
	abrt_compares = nlgn * 10;

	/* Allocate enough space to store ints or doubles. */
	es = MAXIMUM(sizeof(int), sizeof(double));
	x = reallocarray(NULL, n, es);
	y = reallocarray(NULL, n, es);
	z = reallocarray(NULL, n, es);
	if (x == NULL || y == NULL || z == NULL)
		err(1, NULL);

	for (d = distributions; d->name != NULL; d++) {
		if (name != NULL && strncmp(name, d->name, strlen(name)) != 0)
			continue;
		if (d->test(d, n, x, y, z) != 0)
			ret = 1;
	}

	free(x);
	free(y);
	free(z);
	return ret;
}

__dead void
usage(void)
{
        fprintf(stderr, "usage: qsort_test [-dvt] [-n test_name] [num ...]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char *nums[] = { "100", "1023", "1024", "1025", "4095", "4096", "4097" };
	struct test_distribution *d;
	int ch, n, ret = 0;
	char *name = NULL;

        while ((ch = getopt(argc, argv, "dn:tv")) != -1) {
                switch (ch) {
                case 'd':
                        dump_table = true;
                        break;
                case 'n':
			for (d = distributions; d->name != NULL; d++) {
				if (strncmp(optarg, d->name, strlen(optarg)) == 0)
					break;
			}
			if (d->name == NULL)
				errx(1, "unknown test %s", optarg);
                        name = optarg;
                        break;
                case 't':
                        timing = true;
                        break;
                case 'v':
                        verbose = true;
                        break;
                default:
                        usage();
                        break;
                }
        }
        argc -= optind;
        argv += optind;

	if (argc == 0) {
		argv = nums;
		argc = sizeof(nums) / sizeof(nums[0]);
	}

	while (argc > 0) {
		n = atoi(*argv);
		if (run_tests(n, name) != 0)
			ret = 1;
		argc--;
		argv++;
	}

	return ret;
}
