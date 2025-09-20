/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include "../tests.h"

#define BENCH_RUNS 999999

static volatile int cnt;

static void brstack_bar(void) {
}				/* return */

static void brstack_foo(void) {
	brstack_bar();		/* call */
}				/* return */

static void brstack_bench(void) {
	void (*brstack_foo_ind)(void) = brstack_foo;

	if ((cnt++) % 3)	/* branch (cond) */
		brstack_foo();	/* call */
	brstack_bar();		/* call */
	brstack_foo_ind();	/* call (ind) */
}

static int brstack(int argc, const char **argv)
{
	int num_loops = BENCH_RUNS;

	if (argc > 0)
		num_loops = atoi(argv[0]);

	while (1) {
		if ((cnt++) > num_loops)
			break;
		brstack_bench();/* call */
	}			/* branch (uncond) */
	return 0;
}

DEFINE_WORKLOAD(brstack);
