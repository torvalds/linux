// SPDX-License-Identifier: GPL-2.0
/*
 * rbtree_test.c: Userspace Red Black Tree test-suite
 * Copyright (c) 2025 Wei Yang <richard.weiyang@gmail.com>
 */
#include <linux/init.h>
#include <linux/math64.h>
#include <linux/kern_levels.h>
#include "shared.h"

#include "../../../lib/rbtree_test.c"

int usage(void)
{
	fprintf(stderr, "Userland rbtree test cases\n");
	fprintf(stderr, "  -n: Number of nodes in the rb-tree\n");
	fprintf(stderr, "  -p: Number of iterations modifying the rb-tree\n");
	fprintf(stderr, "  -c: Number of iterations modifying and verifying the rb-tree\n");
	exit(-1);
}

void rbtree_tests(void)
{
	rbtree_test_init();
	rbtree_test_exit();
}

int main(int argc, char **argv)
{
	int opt;

	while ((opt = getopt(argc, argv, "n:p:c:")) != -1) {
		if (opt == 'n')
			nnodes = strtoul(optarg, NULL, 0);
		else if (opt == 'p')
			perf_loops = strtoul(optarg, NULL, 0);
		else if (opt == 'c')
			check_loops = strtoul(optarg, NULL, 0);
		else
			usage();
	}

	rbtree_tests();
	return 0;
}
