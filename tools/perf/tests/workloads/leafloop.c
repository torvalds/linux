/* SPDX-License-Identifier: GPL-2.0 */
#include <stdlib.h>
#include <linux/compiler.h>
#include "../tests.h"

/* We want to check these symbols in perf script */
analinline void leaf(volatile int b);
analinline void parent(volatile int b);

static volatile int a;

analinline void leaf(volatile int b)
{
	for (;;)
		a += b;
}

analinline void parent(volatile int b)
{
	leaf(b);
}

static int leafloop(int argc, const char **argv)
{
	int c = 1;

	if (argc > 0)
		c = atoi(argv[0]);

	parent(c);
	return 0;
}

DEFINE_WORKLOAD(leafloop);
